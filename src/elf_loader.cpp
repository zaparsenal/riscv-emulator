#include "rvemu/elf_loader.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <vector>

namespace rvemu {
namespace {

constexpr std::size_t kElf32HeaderSize = 52U;
constexpr std::size_t kElf32ProgramHeaderSize = 32U;
constexpr std::uint16_t kExtendedProgramHeaderCount = 0xFFFFU;
constexpr std::uint64_t kGuestAddressSpaceSize = 0x1'0000'0000ULL;

constexpr std::uint8_t kElfClass32 = 1U;
constexpr std::uint8_t kElfDataLittleEndian = 1U;
constexpr std::uint8_t kElfVersionCurrent = 1U;
constexpr std::uint8_t kElfOsAbiNone = 0U;
constexpr std::uint16_t kElfTypeExecutable = 2U;
constexpr std::uint16_t kMachineRiscV = 243U;
constexpr std::uint32_t kProgramHeaderLoad = 1U;
constexpr std::uint32_t kProgramHeaderDynamic = 2U;
constexpr std::uint32_t kProgramHeaderInterpreter = 3U;
constexpr std::uint32_t kProgramHeaderTls = 7U;
constexpr std::uint32_t kProgramFlagExecute = 0x1U;

struct LoadSegment {
  std::uint32_t file_offset;
  std::uint32_t virtual_address;
  std::uint32_t file_size;
  std::uint32_t memory_size;
  std::uint32_t flags;
};

[[nodiscard]] std::uint16_t read_u16_le(
    const std::span<const std::uint8_t> image, const std::size_t offset) {
  return static_cast<std::uint16_t>(image[offset]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(image[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t read_u32_le(
    const std::span<const std::uint8_t> image, const std::size_t offset) {
  return static_cast<std::uint32_t>(image[offset]) |
         (static_cast<std::uint32_t>(image[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(image[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(image[offset + 3U]) << 24U);
}

[[nodiscard]] constexpr bool range_fits(const std::uint64_t offset,
                                        const std::uint64_t size,
                                        const std::uint64_t limit) noexcept {
  return offset <= limit && size <= limit - offset;
}

[[nodiscard]] constexpr bool is_power_of_two(
    const std::uint32_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] ElfLoadResult failure(
    const ElfLoadErrorCode code,
    const std::optional<std::size_t> program_header_index = std::nullopt) {
  return ElfLoadFailure{code, program_header_index};
}

}  // namespace

std::string_view elf_load_error_message(const ElfLoadErrorCode code) noexcept {
  switch (code) {
    case ElfLoadErrorCode::FileOpenFailed:
      return "the ELF file could not be opened";
    case ElfLoadErrorCode::FileReadFailed:
      return "the ELF file could not be read completely";
    case ElfLoadErrorCode::FileTooLarge:
      return "the ELF file is too large for the host";
    case ElfLoadErrorCode::TruncatedHeader:
      return "the ELF32 header is truncated";
    case ElfLoadErrorCode::InvalidMagic:
      return "the file does not have ELF magic bytes";
    case ElfLoadErrorCode::UnsupportedClass:
      return "only 32-bit ELF files are supported";
    case ElfLoadErrorCode::UnsupportedEndianness:
      return "only little-endian ELF files are supported";
    case ElfLoadErrorCode::UnsupportedIdentificationVersion:
      return "the ELF identification version is unsupported";
    case ElfLoadErrorCode::UnsupportedOsAbi:
      return "the ELF OS ABI is unsupported";
    case ElfLoadErrorCode::UnsupportedAbiVersion:
      return "the ELF ABI version is unsupported";
    case ElfLoadErrorCode::UnsupportedFileType:
      return "only fixed-address executable ELF files are supported";
    case ElfLoadErrorCode::UnsupportedMachine:
      return "the ELF file does not target RISC-V";
    case ElfLoadErrorCode::UnsupportedFileVersion:
      return "the ELF file version is unsupported";
    case ElfLoadErrorCode::UnsupportedIsaFlags:
      return "the ELF file requires an unsupported RISC-V ABI or ISA mode";
    case ElfLoadErrorCode::InvalidElfHeaderSize:
      return "the ELF header size is invalid";
    case ElfLoadErrorCode::InvalidProgramHeaderOffset:
      return "the program header table overlaps the ELF header";
    case ElfLoadErrorCode::MissingProgramHeaderTable:
      return "the executable has no program header table";
    case ElfLoadErrorCode::UnsupportedProgramHeaderCount:
      return "extended program header numbering is unsupported";
    case ElfLoadErrorCode::InvalidProgramHeaderEntrySize:
      return "the ELF32 program header entry size is invalid";
    case ElfLoadErrorCode::ProgramHeaderTableOutOfBounds:
      return "the program header table is outside the file";
    case ElfLoadErrorCode::UnsupportedDynamicLinking:
      return "dynamic linking information is unsupported";
    case ElfLoadErrorCode::UnsupportedThreadLocalStorage:
      return "thread-local storage is unsupported";
    case ElfLoadErrorCode::LoadSegmentsOutOfOrder:
      return "loadable segments are not sorted by virtual address";
    case ElfLoadErrorCode::SegmentFileSizeExceedsMemorySize:
      return "a loadable segment is smaller in memory than in the file";
    case ElfLoadErrorCode::SegmentFileRangeOutOfBounds:
      return "a loadable segment refers outside the ELF file";
    case ElfLoadErrorCode::SegmentAddressRangeOverflow:
      return "a loadable segment exceeds the 32-bit guest address space";
    case ElfLoadErrorCode::SegmentOutsideMemory:
      return "a loadable segment is outside mapped guest memory";
    case ElfLoadErrorCode::InvalidSegmentAlignment:
      return "a loadable segment has invalid ELF alignment";
    case ElfLoadErrorCode::OverlappingLoadSegments:
      return "loadable segments overlap in guest memory";
    case ElfLoadErrorCode::NoLoadableSegments:
      return "the executable has no nonempty loadable segments";
    case ElfLoadErrorCode::EntryPointMisaligned:
      return "the entry point is not four-byte aligned";
    case ElfLoadErrorCode::EntryPointNotExecutable:
      return "the entry point is not in an executable loadable segment";
  }
  return "unknown ELF load error";
}

ElfLoadResult load_elf32(const std::span<const std::uint8_t> image,
                         CpuState& state, Memory& memory) {
  if (image.size() < kElf32HeaderSize) {
    return failure(ElfLoadErrorCode::TruncatedHeader);
  }
  if (image[0U] != 0x7FU || image[1U] != 'E' || image[2U] != 'L' ||
      image[3U] != 'F') {
    return failure(ElfLoadErrorCode::InvalidMagic);
  }
  if (image[4U] != kElfClass32) {
    return failure(ElfLoadErrorCode::UnsupportedClass);
  }
  if (image[5U] != kElfDataLittleEndian) {
    return failure(ElfLoadErrorCode::UnsupportedEndianness);
  }
  if (image[6U] != kElfVersionCurrent) {
    return failure(ElfLoadErrorCode::UnsupportedIdentificationVersion);
  }
  if (image[7U] != kElfOsAbiNone) {
    return failure(ElfLoadErrorCode::UnsupportedOsAbi);
  }
  if (image[8U] != 0U) {
    return failure(ElfLoadErrorCode::UnsupportedAbiVersion);
  }
  if (read_u16_le(image, 16U) != kElfTypeExecutable) {
    return failure(ElfLoadErrorCode::UnsupportedFileType);
  }
  if (read_u16_le(image, 18U) != kMachineRiscV) {
    return failure(ElfLoadErrorCode::UnsupportedMachine);
  }
  if (read_u32_le(image, 20U) != kElfVersionCurrent) {
    return failure(ElfLoadErrorCode::UnsupportedFileVersion);
  }
  if (read_u32_le(image, 36U) != 0U) {
    return failure(ElfLoadErrorCode::UnsupportedIsaFlags);
  }
  if (read_u16_le(image, 40U) != kElf32HeaderSize) {
    return failure(ElfLoadErrorCode::InvalidElfHeaderSize);
  }

  const std::uint32_t entry_point = read_u32_le(image, 24U);
  const std::uint32_t program_header_offset = read_u32_le(image, 28U);
  const std::uint16_t program_header_entry_size = read_u16_le(image, 42U);
  const std::uint16_t program_header_count = read_u16_le(image, 44U);

  if (program_header_count == 0U) {
    return failure(ElfLoadErrorCode::MissingProgramHeaderTable);
  }
  if (program_header_count == kExtendedProgramHeaderCount) {
    return failure(ElfLoadErrorCode::UnsupportedProgramHeaderCount);
  }
  if (program_header_entry_size != kElf32ProgramHeaderSize) {
    return failure(ElfLoadErrorCode::InvalidProgramHeaderEntrySize);
  }
  if (program_header_offset < kElf32HeaderSize) {
    return failure(ElfLoadErrorCode::InvalidProgramHeaderOffset);
  }

  const std::uint64_t program_header_table_size =
      static_cast<std::uint64_t>(program_header_count) *
      program_header_entry_size;
  if (!range_fits(program_header_offset, program_header_table_size,
                  image.size())) {
    return failure(ElfLoadErrorCode::ProgramHeaderTableOutOfBounds);
  }

  std::vector<LoadSegment> load_segments;
  load_segments.reserve(program_header_count);
  std::optional<std::uint32_t> previous_virtual_address;
  std::optional<std::uint64_t> previous_nonempty_end;

  for (std::size_t index = 0U; index < program_header_count; ++index) {
    const std::size_t header_offset =
        static_cast<std::size_t>(program_header_offset) +
        index * kElf32ProgramHeaderSize;
    const std::uint32_t type = read_u32_le(image, header_offset);

    if (type == kProgramHeaderDynamic || type == kProgramHeaderInterpreter) {
      return failure(ElfLoadErrorCode::UnsupportedDynamicLinking, index);
    }
    if (type == kProgramHeaderTls) {
      return failure(ElfLoadErrorCode::UnsupportedThreadLocalStorage, index);
    }
    if (type != kProgramHeaderLoad) {
      continue;
    }

    const std::uint32_t file_offset = read_u32_le(image, header_offset + 4U);
    const std::uint32_t virtual_address =
        read_u32_le(image, header_offset + 8U);
    const std::uint32_t file_size = read_u32_le(image, header_offset + 16U);
    const std::uint32_t memory_size = read_u32_le(image, header_offset + 20U);
    const std::uint32_t flags = read_u32_le(image, header_offset + 24U);
    const std::uint32_t alignment = read_u32_le(image, header_offset + 28U);

    if (previous_virtual_address.has_value() &&
        virtual_address < *previous_virtual_address) {
      return failure(ElfLoadErrorCode::LoadSegmentsOutOfOrder, index);
    }
    previous_virtual_address = virtual_address;

    if (file_size > memory_size) {
      return failure(ElfLoadErrorCode::SegmentFileSizeExceedsMemorySize, index);
    }
    if (!range_fits(file_offset, file_size, image.size())) {
      return failure(ElfLoadErrorCode::SegmentFileRangeOutOfBounds, index);
    }
    if (alignment > 1U &&
        (!is_power_of_two(alignment) ||
         virtual_address % alignment != file_offset % alignment)) {
      return failure(ElfLoadErrorCode::InvalidSegmentAlignment, index);
    }

    const std::uint64_t segment_end =
        static_cast<std::uint64_t>(virtual_address) + memory_size;
    if (segment_end > kGuestAddressSpaceSize) {
      return failure(ElfLoadErrorCode::SegmentAddressRangeOverflow, index);
    }
    if (memory_size != 0U) {
      if (virtual_address < memory.base_address() ||
          segment_end > memory.end_address_exclusive()) {
        return failure(ElfLoadErrorCode::SegmentOutsideMemory, index);
      }
      if (previous_nonempty_end.has_value() &&
          virtual_address < *previous_nonempty_end) {
        return failure(ElfLoadErrorCode::OverlappingLoadSegments, index);
      }
      previous_nonempty_end = segment_end;
    }

    load_segments.push_back(LoadSegment{file_offset, virtual_address, file_size,
                                        memory_size, flags});
  }

  const auto nonempty_segment_count =
      std::ranges::count_if(load_segments, [](const LoadSegment& segment) {
        return segment.memory_size != 0U;
      });
  if (nonempty_segment_count == 0) {
    return failure(ElfLoadErrorCode::NoLoadableSegments);
  }
  if ((entry_point & 0x3U) != 0U) {
    return failure(ElfLoadErrorCode::EntryPointMisaligned);
  }

  const bool entry_is_executable = std::ranges::any_of(
      load_segments, [entry_point](const LoadSegment& segment) {
        const std::uint64_t segment_end =
            static_cast<std::uint64_t>(segment.virtual_address) +
            segment.memory_size;
        return segment.memory_size != 0U &&
               (segment.flags & kProgramFlagExecute) != 0U &&
               entry_point >= segment.virtual_address &&
               static_cast<std::uint64_t>(entry_point) < segment_end;
      });
  if (!entry_is_executable) {
    return failure(ElfLoadErrorCode::EntryPointNotExecutable);
  }

  std::uint64_t file_bytes_loaded = 0U;
  std::uint64_t zero_fill_bytes = 0U;
  for (const LoadSegment& segment : load_segments) {
    for (std::uint32_t offset = 0U; offset < segment.file_size; ++offset) {
      memory.write8(segment.virtual_address + offset,
                    image[static_cast<std::size_t>(segment.file_offset) +
                          offset]);
    }
    for (std::uint32_t offset = segment.file_size;
         offset < segment.memory_size; ++offset) {
      memory.write8(segment.virtual_address + offset, 0U);
    }
    file_bytes_loaded += segment.file_size;
    zero_fill_bytes += segment.memory_size - segment.file_size;
  }
  state.set_program_counter(entry_point);

  return ElfLoadSuccess{entry_point,
                        static_cast<std::size_t>(nonempty_segment_count),
                        file_bytes_loaded, zero_fill_bytes};
}

ElfLoadResult load_elf32_file(const std::filesystem::path& path,
                              CpuState& state, Memory& memory) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    return failure(ElfLoadErrorCode::FileOpenFailed);
  }

  const std::ifstream::pos_type end_position = file.tellg();
  if (end_position == std::ifstream::pos_type{-1}) {
    return failure(ElfLoadErrorCode::FileReadFailed);
  }
  const auto file_size =
      static_cast<std::uintmax_t>(static_cast<std::streamoff>(end_position));
  if (file_size > std::numeric_limits<std::size_t>::max() ||
      file_size >
          static_cast<std::uintmax_t>(
              std::numeric_limits<std::streamsize>::max())) {
    return failure(ElfLoadErrorCode::FileTooLarge);
  }

  std::vector<std::uint8_t> image(static_cast<std::size_t>(file_size));
  file.seekg(0, std::ios::beg);
  if (!image.empty()) {
    file.read(reinterpret_cast<char*>(image.data()),
              static_cast<std::streamsize>(image.size()));
  }
  if (!file) {
    return failure(ElfLoadErrorCode::FileReadFailed);
  }
  return load_elf32(image, state, memory);
}

}  // namespace rvemu

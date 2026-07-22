#include "rvemu/elf_loader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "rvemu/execution_engine.hpp"

namespace rvemu {
namespace {

constexpr std::size_t kElfHeaderSize = 52U;
constexpr std::size_t kProgramHeaderSize = 32U;
constexpr std::size_t kProgramHeaderOffset = kElfHeaderSize;

struct SegmentSpec {
  std::uint32_t type{1U};
  std::uint32_t file_offset{0x100U};
  std::uint32_t virtual_address{0x1000U};
  std::uint32_t physical_address{0U};
  std::uint32_t memory_size{8U};
  std::uint32_t flags{0x5U};
  std::uint32_t alignment{4U};
  std::vector<std::uint8_t> payload{0x13U, 0x00U, 0x00U, 0x00U};
};

void put_u16_le(std::vector<std::uint8_t>& image, const std::size_t offset,
                const std::uint16_t value) {
  image[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  image[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32_le(std::vector<std::uint8_t>& image, const std::size_t offset,
                const std::uint32_t value) {
  image[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  image[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  image[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  image[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

[[nodiscard]] std::size_t program_header_field(const std::size_t index,
                                               const std::size_t field) {
  return kProgramHeaderOffset + index * kProgramHeaderSize + field;
}

[[nodiscard]] std::vector<std::uint8_t> make_elf(
    const std::span<const SegmentSpec> segments,
    const std::uint32_t entry_point = 0x1000U) {
  std::size_t image_size =
      kProgramHeaderOffset + segments.size() * kProgramHeaderSize;
  for (const SegmentSpec& segment : segments) {
    image_size = std::max(
        image_size, static_cast<std::size_t>(segment.file_offset) +
                        segment.payload.size());
  }
  std::vector<std::uint8_t> image(image_size, 0U);

  image[0U] = 0x7FU;
  image[1U] = 'E';
  image[2U] = 'L';
  image[3U] = 'F';
  image[4U] = 1U;
  image[5U] = 1U;
  image[6U] = 1U;
  image[7U] = 0U;
  image[8U] = 0U;
  put_u16_le(image, 16U, 2U);
  put_u16_le(image, 18U, 243U);
  put_u32_le(image, 20U, 1U);
  put_u32_le(image, 24U, entry_point);
  put_u32_le(image, 28U, static_cast<std::uint32_t>(kProgramHeaderOffset));
  put_u32_le(image, 36U, 0U);
  put_u16_le(image, 40U, static_cast<std::uint16_t>(kElfHeaderSize));
  put_u16_le(image, 42U, static_cast<std::uint16_t>(kProgramHeaderSize));
  put_u16_le(image, 44U, static_cast<std::uint16_t>(segments.size()));

  for (std::size_t index = 0U; index < segments.size(); ++index) {
    const SegmentSpec& segment = segments[index];
    put_u32_le(image, program_header_field(index, 0U), segment.type);
    put_u32_le(image, program_header_field(index, 4U), segment.file_offset);
    put_u32_le(image, program_header_field(index, 8U),
               segment.virtual_address);
    put_u32_le(image, program_header_field(index, 12U),
               segment.physical_address);
    put_u32_le(image, program_header_field(index, 16U),
               static_cast<std::uint32_t>(segment.payload.size()));
    put_u32_le(image, program_header_field(index, 20U), segment.memory_size);
    put_u32_le(image, program_header_field(index, 24U), segment.flags);
    put_u32_le(image, program_header_field(index, 28U), segment.alignment);
    std::copy(segment.payload.begin(), segment.payload.end(),
              image.begin() + segment.file_offset);
  }
  return image;
}

[[nodiscard]] std::vector<std::uint8_t> make_valid_elf() {
  const std::array segments{SegmentSpec{}};
  return make_elf(segments);
}

[[nodiscard]] ElfLoadFailure expect_failure(const ElfLoadResult& result) {
  EXPECT_TRUE(std::holds_alternative<ElfLoadFailure>(result));
  return std::get<ElfLoadFailure>(result);
}

TEST(ElfLoaderTest, LoadsFileBytesZeroFillsAndChangesOnlyTheProgramCounter) {
  CpuState state;
  state.write_register(5U, 0xA5A5A5A5U);
  state.set_program_counter(0x1234U);
  Memory memory(0x1000U, 32U);
  for (std::uint32_t address = 0x1000U; address < 0x1020U; ++address) {
    memory.write8(address, 0xCCU);
  }

  const ElfLoadResult result = load_elf32(make_valid_elf(), state, memory);

  ASSERT_TRUE(std::holds_alternative<ElfLoadSuccess>(result));
  const ElfLoadSuccess success = std::get<ElfLoadSuccess>(result);
  EXPECT_EQ(success.entry_point, 0x1000U);
  EXPECT_EQ(success.loadable_segments, 1U);
  EXPECT_EQ(success.file_bytes_loaded, 4U);
  EXPECT_EQ(success.zero_fill_bytes, 4U);
  EXPECT_EQ(state.program_counter(), 0x1000U);
  EXPECT_EQ(state.read_register(5U), 0xA5A5A5A5U);
  EXPECT_EQ(memory.read32(0x1000U), 0x00000013U);
  EXPECT_EQ(memory.read32(0x1004U), 0U);
  EXPECT_EQ(memory.read8(0x1008U), 0xCCU);
}

TEST(ElfLoaderTest, LoadsDisjointSegmentsAndIgnoresPhysicalAddressesAndNotes) {
  SegmentSpec text;
  text.physical_address = 0xDEADBEEFU;
  SegmentSpec note;
  note.type = 4U;
  note.file_offset = 0x108U;
  note.virtual_address = 0xFFFFFFFFU;
  note.memory_size = 0xFFFFFFFFU;
  note.payload.clear();
  SegmentSpec data;
  data.file_offset = 0x110U;
  data.virtual_address = 0x1010U;
  data.physical_address = 0xCAFEBABEU;
  data.memory_size = 4U;
  data.flags = 0x6U;
  data.payload = {0x78U, 0x56U, 0x34U, 0x12U};
  const std::array segments{text, note, data};
  CpuState state;
  Memory memory(0x1000U, 32U);

  const ElfLoadResult result = load_elf32(make_elf(segments), state, memory);

  ASSERT_TRUE(std::holds_alternative<ElfLoadSuccess>(result));
  EXPECT_EQ(std::get<ElfLoadSuccess>(result).loadable_segments, 2U);
  EXPECT_EQ(memory.read32(0x1000U), 0x00000013U);
  EXPECT_EQ(memory.read32(0x1010U), 0x12345678U);
}

TEST(ElfLoaderTest, AcceptsNoAlignmentAndEndsAtTopOfAddressSpace) {
  SegmentSpec segment;
  segment.file_offset = 0x101U;
  segment.virtual_address = 0xFFFFFFF8U;
  segment.memory_size = 8U;
  segment.alignment = 1U;
  const std::array segments{segment};
  CpuState state;
  Memory memory(0xFFFFFFF0U, 16U);

  const ElfLoadResult result =
      load_elf32(make_elf(segments, 0xFFFFFFF8U), state, memory);

  ASSERT_TRUE(std::holds_alternative<ElfLoadSuccess>(result));
  EXPECT_EQ(memory.read32(0xFFFFFFF8U), 0x00000013U);
  EXPECT_EQ(memory.read32(0xFFFFFFFCU), 0U);
}

TEST(ElfLoaderTest, AcceptsZeroAlignmentAndAFileEmptyLoadSegment) {
  SegmentSpec text;
  text.memory_size = 4U;
  text.alignment = 0U;
  SegmentSpec bss;
  bss.file_offset = 0x200U;
  bss.virtual_address = 0x1008U;
  bss.memory_size = 4U;
  bss.flags = 0x6U;
  bss.alignment = 4U;
  bss.payload.clear();
  const std::array segments{text, bss};
  CpuState state;
  Memory memory(0x1000U, 16U);
  memory.write32(0x1008U, 0xFFFFFFFFU);

  const ElfLoadResult result = load_elf32(make_elf(segments), state, memory);

  ASSERT_TRUE(std::holds_alternative<ElfLoadSuccess>(result));
  EXPECT_EQ(std::get<ElfLoadSuccess>(result).loadable_segments, 2U);
  EXPECT_EQ(memory.read32(0x1008U), 0U);
}

TEST(ElfLoaderTest, LoadedInstructionsExecuteThroughTheCore) {
  SegmentSpec segment;
  segment.memory_size = 8U;
  segment.payload = {0x13U, 0x00U, 0x00U, 0x00U,
                     0x73U, 0x00U, 0x10U, 0x00U};
  const std::array segments{segment};
  CpuState state;
  Memory memory(0x1000U, 16U);
  ASSERT_TRUE(std::holds_alternative<ElfLoadSuccess>(
      load_elf32(make_elf(segments), state, memory)));
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(2U);

  ASSERT_TRUE(std::holds_alternative<RunTrapped>(result));
  const RunTrapped trapped = std::get<RunTrapped>(result);
  EXPECT_EQ(trapped.instructions_executed, 1U);
  EXPECT_EQ(trapped.trap.cause, TrapCause::Breakpoint);
  EXPECT_EQ(trapped.trap.program_counter, 0x1004U);
}

TEST(ElfLoaderTest, RejectsTruncatedAndMisidentifiedFiles) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  std::vector<std::uint8_t> truncated(kElfHeaderSize - 1U, 0U);
  EXPECT_EQ(expect_failure(load_elf32(truncated, state, memory)).code,
            ElfLoadErrorCode::TruncatedHeader);

  std::vector<std::uint8_t> image = make_valid_elf();
  image[2U] = 'X';
  EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code,
            ElfLoadErrorCode::InvalidMagic);
}

TEST(ElfLoaderTest, RejectsUnsupportedIdentificationFields) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  const auto check = [&](const std::size_t offset, const std::uint8_t value,
                         const ElfLoadErrorCode expected) {
    std::vector<std::uint8_t> image = make_valid_elf();
    image[offset] = value;
    EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code, expected);
  };

  check(4U, 2U, ElfLoadErrorCode::UnsupportedClass);
  check(5U, 2U, ElfLoadErrorCode::UnsupportedEndianness);
  check(6U, 0U, ElfLoadErrorCode::UnsupportedIdentificationVersion);
  check(7U, 3U, ElfLoadErrorCode::UnsupportedOsAbi);
  check(8U, 1U, ElfLoadErrorCode::UnsupportedAbiVersion);
}

TEST(ElfLoaderTest, RejectsUnsupportedExecutableHeaderFields) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  const auto check_u16 = [&](const std::size_t offset, const std::uint16_t value,
                             const ElfLoadErrorCode expected) {
    std::vector<std::uint8_t> image = make_valid_elf();
    put_u16_le(image, offset, value);
    EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code, expected);
  };
  const auto check_u32 = [&](const std::size_t offset, const std::uint32_t value,
                             const ElfLoadErrorCode expected) {
    std::vector<std::uint8_t> image = make_valid_elf();
    put_u32_le(image, offset, value);
    EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code, expected);
  };

  check_u16(16U, 3U, ElfLoadErrorCode::UnsupportedFileType);
  check_u16(18U, 62U, ElfLoadErrorCode::UnsupportedMachine);
  check_u32(20U, 0U, ElfLoadErrorCode::UnsupportedFileVersion);
  check_u32(36U, 1U, ElfLoadErrorCode::UnsupportedIsaFlags);
  check_u16(40U, 64U, ElfLoadErrorCode::InvalidElfHeaderSize);
}

TEST(ElfLoaderTest, RejectsInvalidProgramHeaderTableMetadata) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  const auto check_u16 = [&](const std::size_t offset, const std::uint16_t value,
                             const ElfLoadErrorCode expected) {
    std::vector<std::uint8_t> image = make_valid_elf();
    put_u16_le(image, offset, value);
    EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code, expected);
  };

  check_u16(44U, 0U, ElfLoadErrorCode::MissingProgramHeaderTable);
  check_u16(44U, 0xFFFFU, ElfLoadErrorCode::UnsupportedProgramHeaderCount);
  check_u16(42U, 31U, ElfLoadErrorCode::InvalidProgramHeaderEntrySize);

  std::vector<std::uint8_t> overlapping_header = make_valid_elf();
  put_u32_le(overlapping_header, 28U, 16U);
  EXPECT_EQ(expect_failure(load_elf32(overlapping_header, state, memory)).code,
            ElfLoadErrorCode::InvalidProgramHeaderOffset);

  std::vector<std::uint8_t> truncated_table = make_valid_elf();
  put_u32_le(truncated_table, 28U, 0xFFFFFFF0U);
  EXPECT_EQ(expect_failure(load_elf32(truncated_table, state, memory)).code,
            ElfLoadErrorCode::ProgramHeaderTableOutOfBounds);
}

TEST(ElfLoaderTest, RejectsUnsupportedRuntimeSegments) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  SegmentSpec segment;
  for (const std::uint32_t type : {2U, 3U}) {
    segment.type = type;
    const std::array dynamic_segment{segment};
    const ElfLoadFailure error = expect_failure(
        load_elf32(make_elf(dynamic_segment), state, memory));
    EXPECT_EQ(error.code, ElfLoadErrorCode::UnsupportedDynamicLinking);
    EXPECT_EQ(error.program_header_index, 0U);
  }

  segment.type = 7U;
  const std::array tls{segment};
  const ElfLoadFailure error =
      expect_failure(load_elf32(make_elf(tls), state, memory));
  EXPECT_EQ(error.code, ElfLoadErrorCode::UnsupportedThreadLocalStorage);
  EXPECT_EQ(error.program_header_index, 0U);
}

TEST(ElfLoaderTest, RejectsSegmentWhoseFileSizeExceedsMemorySize) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  std::vector<std::uint8_t> image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 20U), 3U);

  const ElfLoadFailure error = expect_failure(load_elf32(image, state, memory));

  EXPECT_EQ(error.code, ElfLoadErrorCode::SegmentFileSizeExceedsMemorySize);
  EXPECT_EQ(error.program_header_index, 0U);
}

TEST(ElfLoaderTest, RejectsSegmentFileRangesOutsideTheImageWithoutWraparound) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  std::vector<std::uint8_t> image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 4U), 0xFFFFFFFEU);

  const ElfLoadFailure error = expect_failure(load_elf32(image, state, memory));

  EXPECT_EQ(error.code, ElfLoadErrorCode::SegmentFileRangeOutOfBounds);
}

TEST(ElfLoaderTest, RejectsSegmentAddressOverflowAndUnmappedRanges) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  std::vector<std::uint8_t> image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 8U), 0xFFFFFFFCU);
  put_u32_le(image, program_header_field(0U, 20U), 8U);
  EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code,
            ElfLoadErrorCode::SegmentAddressRangeOverflow);

  image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 8U), 0x100CU);
  EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code,
            ElfLoadErrorCode::SegmentOutsideMemory);
}

TEST(ElfLoaderTest, RejectsInvalidSegmentAlignment) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  std::vector<std::uint8_t> image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 28U), 3U);
  EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code,
            ElfLoadErrorCode::InvalidSegmentAlignment);

  image = make_valid_elf();
  put_u32_le(image, program_header_field(0U, 28U), 8U);
  put_u32_le(image, program_header_field(0U, 4U), 0xFCU);
  EXPECT_EQ(expect_failure(load_elf32(image, state, memory)).code,
            ElfLoadErrorCode::InvalidSegmentAlignment);
}

TEST(ElfLoaderTest, RejectsOutOfOrderAndOverlappingLoadSegments) {
  SegmentSpec first;
  SegmentSpec second;
  second.file_offset = 0x110U;
  second.virtual_address = 0x0FF0U;
  second.memory_size = 8U;
  const std::array out_of_order{first, second};
  CpuState state;
  Memory memory(0x0FE0U, 64U);
  EXPECT_EQ(
      expect_failure(load_elf32(make_elf(out_of_order), state, memory)).code,
      ElfLoadErrorCode::LoadSegmentsOutOfOrder);

  second.virtual_address = 0x1004U;
  const std::array overlapping{first, second};
  EXPECT_EQ(
      expect_failure(load_elf32(make_elf(overlapping), state, memory)).code,
      ElfLoadErrorCode::OverlappingLoadSegments);
}

TEST(ElfLoaderTest, RejectsImagesWithoutNonemptyLoadableSegments) {
  SegmentSpec segment;
  segment.memory_size = 0U;
  segment.payload.clear();
  const std::array segments{segment};
  CpuState state;
  Memory memory(0x1000U, 16U);

  EXPECT_EQ(expect_failure(load_elf32(make_elf(segments), state, memory)).code,
            ElfLoadErrorCode::NoLoadableSegments);
}

TEST(ElfLoaderTest, RejectsMisalignedOrNonexecutableEntryPoints) {
  CpuState state;
  Memory memory(0x1000U, 16U);
  EXPECT_EQ(
      expect_failure(load_elf32(make_elf(
                                    std::array{SegmentSpec{}}, 0x1002U),
                                state, memory))
          .code,
      ElfLoadErrorCode::EntryPointMisaligned);

  SegmentSpec segment;
  segment.flags = 0x6U;
  const std::array segments{segment};
  EXPECT_EQ(expect_failure(load_elf32(make_elf(segments), state, memory)).code,
            ElfLoadErrorCode::EntryPointNotExecutable);
}

TEST(ElfLoaderTest, LateValidationFailureLeavesCpuAndMemoryUnchanged) {
  SegmentSpec first;
  SegmentSpec second;
  second.file_offset = 0x110U;
  second.virtual_address = 0x1010U;
  second.memory_size = 8U;
  second.flags = 0x6U;
  const std::array segments{first, second};
  CpuState state;
  state.set_program_counter(0xABCDEF00U);
  state.write_register(7U, 0x12345678U);
  Memory memory(0x1000U, 32U);
  for (std::uint32_t address = 0x1000U; address < 0x1020U; ++address) {
    memory.write8(address, 0xA5U);
  }

  const ElfLoadResult result =
      load_elf32(make_elf(segments, 0x1010U), state, memory);

  EXPECT_EQ(expect_failure(result).code,
            ElfLoadErrorCode::EntryPointNotExecutable);
  EXPECT_EQ(state.program_counter(), 0xABCDEF00U);
  EXPECT_EQ(state.read_register(7U), 0x12345678U);
  for (std::uint32_t address = 0x1000U; address < 0x1020U; ++address) {
    EXPECT_EQ(memory.read8(address), 0xA5U);
  }
}

TEST(ElfLoaderTest, LoadsFromAFileAndReportsMissingFiles) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "rvemu-elf-loader-test-image.bin";
  {
    const std::vector<std::uint8_t> image = make_valid_elf();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    ASSERT_TRUE(output.good());
  }

  CpuState state;
  Memory memory(0x1000U, 16U);
  EXPECT_TRUE(std::holds_alternative<ElfLoadSuccess>(
      load_elf32_file(path, state, memory)));
  std::error_code remove_error;
  std::filesystem::remove(path, remove_error);
  EXPECT_FALSE(remove_error);

  const ElfLoadFailure error =
      expect_failure(load_elf32_file(path, state, memory));
  EXPECT_EQ(error.code, ElfLoadErrorCode::FileOpenFailed);
}

TEST(ElfLoaderTest, ErrorMessagesAreHumanReadable) {
  EXPECT_EQ(elf_load_error_message(ElfLoadErrorCode::InvalidMagic),
            "the file does not have ELF magic bytes");
}

}  // namespace
}  // namespace rvemu

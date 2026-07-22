#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

#include "rvemu/cpu_state.hpp"
#include "rvemu/memory.hpp"

namespace rvemu {

enum class ElfLoadErrorCode {
  FileOpenFailed,
  FileReadFailed,
  FileTooLarge,
  TruncatedHeader,
  InvalidMagic,
  UnsupportedClass,
  UnsupportedEndianness,
  UnsupportedIdentificationVersion,
  UnsupportedOsAbi,
  UnsupportedAbiVersion,
  UnsupportedFileType,
  UnsupportedMachine,
  UnsupportedFileVersion,
  UnsupportedIsaFlags,
  InvalidElfHeaderSize,
  InvalidProgramHeaderOffset,
  MissingProgramHeaderTable,
  UnsupportedProgramHeaderCount,
  InvalidProgramHeaderEntrySize,
  ProgramHeaderTableOutOfBounds,
  UnsupportedDynamicLinking,
  UnsupportedThreadLocalStorage,
  LoadSegmentsOutOfOrder,
  SegmentFileSizeExceedsMemorySize,
  SegmentFileRangeOutOfBounds,
  SegmentAddressRangeOverflow,
  SegmentOutsideMemory,
  InvalidSegmentAlignment,
  OverlappingLoadSegments,
  NoLoadableSegments,
  EntryPointMisaligned,
  EntryPointNotExecutable,
};

struct ElfLoadFailure {
  ElfLoadErrorCode code;
  std::optional<std::size_t> program_header_index;
};

struct ElfLoadSuccess {
  std::uint32_t entry_point;
  std::size_t loadable_segments;
  std::uint64_t file_bytes_loaded;
  std::uint64_t zero_fill_bytes;
};

using ElfLoadResult = std::variant<ElfLoadSuccess, ElfLoadFailure>;

[[nodiscard]] std::string_view elf_load_error_message(
    ElfLoadErrorCode code) noexcept;

[[nodiscard]] ElfLoadResult load_elf32(std::span<const std::uint8_t> image,
                                       CpuState& state, Memory& memory);

[[nodiscard]] ElfLoadResult load_elf32_file(
    const std::filesystem::path& path, CpuState& state, Memory& memory);

}  // namespace rvemu

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <variant>

#include "rvemu/elf_loader.hpp"
#include "rvemu/program_session.hpp"

namespace rvemu::act {

struct SmokeOptions {
  std::uint32_t memory_base{0x80000000U};
  std::size_t memory_size{16U * 1024U * 1024U};
  std::uint64_t step_limit{50'000'000U};
};

struct SmokePassed {
  SessionStatistics statistics;
};

struct SmokeFailed {
  SessionStatistics statistics;
  std::uint32_t exit_code;
};

struct SmokeLoadFailed {
  ElfLoadFailure failure;
};

struct SmokeStepLimitReached {
  SessionStatistics statistics;
};

struct SmokeTrapped {
  SessionStatistics statistics;
  Trap trap;
};

struct SmokeUnhandledEnvironmentCall {
  SessionStatistics statistics;
  EnvironmentCall call;
};

struct SmokeUnexpectedBreakpoint {
  SessionStatistics statistics;
  std::uint32_t program_counter;
};

using SmokeResult =
    std::variant<SmokePassed, SmokeFailed, SmokeLoadFailed,
                 SmokeStepLimitReached, SmokeTrapped,
                 SmokeUnhandledEnvironmentCall, SmokeUnexpectedBreakpoint>;

[[nodiscard]] SmokeResult run_self_checking_elf(
    std::span<const std::uint8_t> image,
    const SmokeOptions& options = SmokeOptions{});

[[nodiscard]] SmokeResult run_self_checking_elf_file(
    const std::filesystem::path& path,
    const SmokeOptions& options = SmokeOptions{});

}  // namespace rvemu::act

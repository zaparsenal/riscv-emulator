#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <streambuf>
#include <string>
#include <string_view>
#include <variant>

#include "rvemu/elf_loader.hpp"
#include "rvemu/linux_syscalls.hpp"
#include "rvemu/stack.hpp"

namespace rvemu::cli {

inline constexpr std::uint32_t kDefaultMemoryBase = 0x80000000U;
inline constexpr std::size_t kDefaultMemorySize = 16U * 1024U * 1024U;
inline constexpr std::uint64_t kDefaultMaximumSteps = 50'000'000U;

inline constexpr int kUsageExitStatus = 2;
inline constexpr int kInfrastructureFailureExitStatus = 125;

struct Options {
  std::filesystem::path program_path;
  std::uint32_t memory_base{kDefaultMemoryBase};
  std::size_t memory_size{kDefaultMemorySize};
  std::size_t stack_size{kDefaultFreestandingStackSize};
  std::uint64_t maximum_steps{kDefaultMaximumSteps};
};

struct ParseSuccess {
  Options options;
};

struct ParseHelp {};

struct ParseFailure {
  std::string message;
};

using ParseResult = std::variant<ParseSuccess, ParseHelp, ParseFailure>;

class StreamOutputSink final : public OutputSink {
 public:
  StreamOutputSink(std::streambuf& standard_output,
                   std::streambuf& standard_error) noexcept;

  [[nodiscard]] OutputWriteResult write(
      OutputStream stream, std::span<const Memory::Byte> bytes) override;

 private:
  std::streambuf& standard_output_;
  std::streambuf& standard_error_;
};

enum class HostedProgramErrorCode {
  InvalidConfiguration,
  HostAllocationFailure,
};

struct HostedProgramFailure {
  HostedProgramErrorCode code;
};

struct HostedElfLoadFailure {
  ElfLoadFailure failure;
};

struct HostedStackInitializationFailure {
  StackInitializationFailure failure;
  ElfLoadSuccess loaded_image;
  std::uint64_t memory_end_exclusive;
};

struct HostedSessionFinished {
  SessionRunResult result;
  std::uint32_t final_program_counter;
};

using HostedProgramResult =
    std::variant<HostedProgramFailure, HostedElfLoadFailure,
                 HostedStackInitializationFailure, HostedSessionFinished>;

[[nodiscard]] ParseResult parse_arguments(
    std::span<const std::string_view> arguments);

void print_usage(std::ostream& output);

[[nodiscard]] HostedProgramResult run_program(const Options& options,
                                              OutputSink& output);

[[nodiscard]] int run_cli(const Options& options, OutputSink& output,
                          std::ostream& diagnostics);

}  // namespace rvemu::cli

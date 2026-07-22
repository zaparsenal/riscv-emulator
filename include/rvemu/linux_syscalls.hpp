#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "rvemu/program_session.hpp"

namespace rvemu {

enum class OutputStream {
  StandardOutput,
  StandardError,
};

struct OutputWriteCompleted {
  std::size_t bytes_written;
};

struct OutputWriteFailed {};

using OutputWriteResult =
    std::variant<OutputWriteCompleted, OutputWriteFailed>;

class OutputSink {
 public:
  virtual ~OutputSink() = default;

  [[nodiscard]] virtual OutputWriteResult write(
      OutputStream stream, std::span<const Memory::Byte> bytes) = 0;
};

class LinuxSyscallEnvironment final : public EnvironmentCallHandler {
 public:
  static constexpr std::uint32_t kWrite = 64U;
  static constexpr std::uint32_t kExit = 93U;
  static constexpr std::uint32_t kExitGroup = 94U;

  explicit LinuxSyscallEnvironment(OutputSink& output) noexcept;

  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall& call, const Memory& memory) noexcept override;

 private:
  [[nodiscard]] EnvironmentCallResult handle_write(
      const EnvironmentCall& call, const Memory& memory) noexcept;

  OutputSink& output_;
};

}  // namespace rvemu

#include "rvemu/linux_syscalls.hpp"

#include <algorithm>
#include <cstdint>
#include <variant>

namespace rvemu {
namespace {

constexpr std::uint32_t kStandardOutputFileDescriptor = 1U;
constexpr std::uint32_t kStandardErrorFileDescriptor = 2U;
constexpr std::uint32_t kMaximumWriteSize = 0x7FFFF000U;

constexpr std::uint32_t kErrorIo = 5U;
constexpr std::uint32_t kErrorBadFileDescriptor = 9U;
constexpr std::uint32_t kErrorFault = 14U;
constexpr std::uint32_t kErrorNotImplemented = 38U;

[[nodiscard]] constexpr std::uint32_t error_return(
    const std::uint32_t error_number) noexcept {
  return 0U - error_number;
}

[[nodiscard]] EnvironmentCallResume error_result(
    const std::uint32_t error_number) noexcept {
  return EnvironmentCallResume{error_return(error_number)};
}

}  // namespace

LinuxSyscallEnvironment::LinuxSyscallEnvironment(OutputSink& output) noexcept
    : output_(output) {}

EnvironmentCallResult LinuxSyscallEnvironment::handle(
    const EnvironmentCall& call, const Memory& memory) noexcept {
  switch (call.number) {
    case kWrite:
      return handle_write(call, memory);
    case kExit:
    case kExitGroup:
      return EnvironmentCallExit{call.arguments[0U] & 0xFFU};
    default:
      return error_result(kErrorNotImplemented);
  }
}

EnvironmentCallResult LinuxSyscallEnvironment::handle_write(
    const EnvironmentCall& call, const Memory& memory) noexcept {
  const std::uint32_t file_descriptor = call.arguments[0U];
  OutputStream stream;
  if (file_descriptor == kStandardOutputFileDescriptor) {
    stream = OutputStream::StandardOutput;
  } else if (file_descriptor == kStandardErrorFileDescriptor) {
    stream = OutputStream::StandardError;
  } else {
    return error_result(kErrorBadFileDescriptor);
  }

  const std::uint32_t address = call.arguments[1U];
  const std::uint32_t requested_size = call.arguments[2U];
  if (requested_size == 0U) {
    return EnvironmentCallResume{0U};
  }
  const std::uint32_t effective_size =
      std::min(requested_size, kMaximumWriteSize);

  std::span<const Memory::Byte> bytes;
  try {
    bytes = memory.read_span(address, effective_size);
  } catch (const MemoryAccessError&) {
    return error_result(kErrorFault);
  }

  OutputWriteResult output_result = OutputWriteFailed{};
  try {
    output_result = output_.write(stream, bytes);
  } catch (...) {
    return error_result(kErrorIo);
  }

  if (const auto* completed =
          std::get_if<OutputWriteCompleted>(&output_result)) {
    if (completed->bytes_written <= bytes.size()) {
      return EnvironmentCallResume{
          static_cast<std::uint32_t>(completed->bytes_written)};
    }
  }
  return error_result(kErrorIo);
}

}  // namespace rvemu

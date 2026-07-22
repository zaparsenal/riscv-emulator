#include "act_smoke.hpp"

#include <utility>
#include <variant>

namespace rvemu::act {
namespace {

constexpr std::uint32_t kLinuxExit = 93U;
constexpr std::uint32_t kLinuxExitGroup = 94U;

class ExitEnvironment final : public EnvironmentCallHandler {
 public:
  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall& call, const Memory&) override {
    if (call.number == kLinuxExit || call.number == kLinuxExitGroup) {
      return EnvironmentCallExit{call.arguments[0U]};
    }
    return EnvironmentCallNotHandled{};
  }
};

template <typename LoadElf>
[[nodiscard]] SmokeResult run_with_loader(LoadElf&& load_elf,
                                          const SmokeOptions& options) {
  CpuState state;
  Memory memory(options.memory_base, options.memory_size);
  const ElfLoadResult load_result =
      std::forward<LoadElf>(load_elf)(state, memory);
  if (const auto* failure = std::get_if<ElfLoadFailure>(&load_result)) {
    return SmokeLoadFailed{*failure};
  }

  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  const SessionRunResult run_result = session.run(options.step_limit);
  if (const auto* exited = std::get_if<SessionRunExited>(&run_result)) {
    if (exited->exit.exit_code == 0U) {
      return SmokePassed{exited->statistics};
    }
    return SmokeFailed{exited->statistics, exited->exit.exit_code};
  }
  if (const auto* limit =
          std::get_if<SessionStepLimitReached>(&run_result)) {
    return SmokeStepLimitReached{limit->statistics};
  }
  if (const auto* trapped = std::get_if<SessionRunTrapped>(&run_result)) {
    return SmokeTrapped{trapped->statistics, trapped->trap};
  }
  if (const auto* unhandled =
          std::get_if<SessionRunUnhandledEnvironmentCall>(&run_result)) {
    return SmokeUnhandledEnvironmentCall{
        unhandled->statistics, unhandled->environment_call.call};
  }
  const auto& breakpoint = std::get<SessionBreakpointReached>(run_result);
  return SmokeUnexpectedBreakpoint{breakpoint.statistics,
                                   breakpoint.program_counter};
}

}  // namespace

SmokeResult run_self_checking_elf(const std::span<const std::uint8_t> image,
                                  const SmokeOptions& options) {
  return run_with_loader(
      [image](CpuState& state, Memory& memory) {
        return load_elf32(image, state, memory);
      },
      options);
}

SmokeResult run_self_checking_elf_file(const std::filesystem::path& path,
                                       const SmokeOptions& options) {
  return run_with_loader(
      [&path](CpuState& state, Memory& memory) {
        return load_elf32_file(path, state, memory);
      },
      options);
}

}  // namespace rvemu::act

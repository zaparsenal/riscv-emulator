#include "rvemu/program_session.hpp"

#include <algorithm>
#include <variant>

namespace rvemu {
namespace {

constexpr std::size_t kReturnValueRegister = 10U;
constexpr std::size_t kFirstArgumentRegister = 10U;
constexpr std::size_t kEnvironmentCallNumberRegister = 17U;
constexpr std::uint32_t kInstructionSize = 4U;

[[nodiscard]] bool contains_breakpoint(
    const std::span<const std::uint32_t> breakpoints,
    const std::uint32_t program_counter) {
  return std::ranges::find(breakpoints, program_counter) != breakpoints.end();
}

}  // namespace

ProgramSession::ProgramSession(CpuState& state, Memory& memory,
                               EnvironmentCallHandler& environment) noexcept
    : state_(state),
      memory_(memory),
      environment_(environment),
      engine_(state, memory) {}

SessionStepResult ProgramSession::step() {
  const StepResult execution_result = engine_.step();
  if (const auto* completed = std::get_if<StepCompleted>(&execution_result)) {
    return SessionInstructionCompleted{*completed};
  }

  const Trap trap = std::get<Trap>(execution_result);
  if (trap.cause != TrapCause::EnvironmentCallFromUserMode) {
    return trap;
  }

  const EnvironmentCall call = capture_environment_call(trap.program_counter);
  const EnvironmentCallResult environment_result =
      environment_.handle(call, memory_);
  if (const auto* resumed =
          std::get_if<EnvironmentCallResume>(&environment_result)) {
    state_.write_register(kReturnValueRegister, resumed->return_value);
    state_.advance_program_counter(kInstructionSize);
    return SessionEnvironmentCallResumed{
        call, resumed->return_value, state_.program_counter()};
  }
  if (const auto* exited =
          std::get_if<EnvironmentCallExit>(&environment_result)) {
    return SessionExited{call, exited->exit_code};
  }
  return SessionUnhandledEnvironmentCall{call, trap};
}

SessionRunResult ProgramSession::run(
    const std::uint64_t step_limit,
    const std::span<const std::uint32_t> breakpoints) {
  SessionStatistics statistics{0U, 0U, 0U};

  while (statistics.guest_steps < step_limit) {
    if (contains_breakpoint(breakpoints, state_.program_counter())) {
      return SessionBreakpointReached{statistics, state_.program_counter()};
    }

    const SessionStepResult result = step();
    if (std::holds_alternative<SessionInstructionCompleted>(result)) {
      ++statistics.guest_steps;
      ++statistics.instructions_retired;
      continue;
    }
    if (std::holds_alternative<SessionEnvironmentCallResumed>(result)) {
      ++statistics.guest_steps;
      ++statistics.environment_calls_handled;
      continue;
    }
    if (const auto* exited = std::get_if<SessionExited>(&result)) {
      ++statistics.guest_steps;
      ++statistics.environment_calls_handled;
      return SessionRunExited{statistics, *exited};
    }
    if (const auto* unhandled =
            std::get_if<SessionUnhandledEnvironmentCall>(&result)) {
      return SessionRunUnhandledEnvironmentCall{statistics, *unhandled};
    }
    return SessionRunTrapped{statistics, std::get<Trap>(result)};
  }

  return SessionStepLimitReached{statistics};
}

EnvironmentCall ProgramSession::capture_environment_call(
    const std::uint32_t program_counter) const {
  EnvironmentCall call{program_counter,
                       state_.read_register(kEnvironmentCallNumberRegister),
                       {}};
  for (std::size_t index = 0U; index < call.arguments.size(); ++index) {
    call.arguments[index] =
        state_.read_register(kFirstArgumentRegister + index);
  }
  return call;
}

}  // namespace rvemu

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <variant>

#include "rvemu/cpu_state.hpp"
#include "rvemu/execution_engine.hpp"
#include "rvemu/memory.hpp"

namespace rvemu {

struct EnvironmentCall {
  static constexpr std::size_t kArgumentCount = 6U;

  std::uint32_t program_counter;
  std::uint32_t number;
  std::array<std::uint32_t, kArgumentCount> arguments;
};

struct EnvironmentCallResume {
  std::uint32_t return_value;
};

struct EnvironmentCallExit {
  std::uint32_t exit_code;
};

struct EnvironmentCallNotHandled {};

using EnvironmentCallResult =
    std::variant<EnvironmentCallResume, EnvironmentCallExit,
                 EnvironmentCallNotHandled>;

class EnvironmentCallHandler {
 public:
  virtual ~EnvironmentCallHandler() = default;

  [[nodiscard]] virtual EnvironmentCallResult handle(
      const EnvironmentCall& call, const Memory& memory) = 0;
};

struct SessionInstructionCompleted {
  StepCompleted step;
};

struct SessionEnvironmentCallResumed {
  EnvironmentCall call;
  std::uint32_t return_value;
  std::uint32_t next_program_counter;
};

struct SessionExited {
  EnvironmentCall call;
  std::uint32_t exit_code;
};

struct SessionUnhandledEnvironmentCall {
  EnvironmentCall call;
  Trap trap;
};

using SessionStepResult =
    std::variant<SessionInstructionCompleted, SessionEnvironmentCallResumed,
                 SessionExited, SessionUnhandledEnvironmentCall, Trap>;

struct SessionStatistics {
  std::uint64_t guest_steps;
  std::uint64_t instructions_retired;
  std::uint64_t environment_calls_handled;
};

struct SessionStepLimitReached {
  SessionStatistics statistics;
};

struct SessionBreakpointReached {
  SessionStatistics statistics;
  std::uint32_t program_counter;
};

struct SessionRunExited {
  SessionStatistics statistics;
  SessionExited exit;
};

struct SessionRunUnhandledEnvironmentCall {
  SessionStatistics statistics;
  SessionUnhandledEnvironmentCall environment_call;
};

struct SessionRunTrapped {
  SessionStatistics statistics;
  Trap trap;
};

using SessionRunResult =
    std::variant<SessionStepLimitReached, SessionBreakpointReached,
                 SessionRunExited, SessionRunUnhandledEnvironmentCall,
                 SessionRunTrapped>;

class ProgramSession final {
 public:
  ProgramSession(CpuState& state, Memory& memory,
                 EnvironmentCallHandler& environment,
                 ExecutionObserver* observer = nullptr) noexcept;

  [[nodiscard]] SessionStepResult step();
  [[nodiscard]] SessionRunResult run(
      std::uint64_t step_limit,
      std::span<const std::uint32_t> breakpoints = {});

 private:
  [[nodiscard]] EnvironmentCall capture_environment_call(
      std::uint32_t program_counter) const;

  CpuState& state_;
  Memory& memory_;
  EnvironmentCallHandler& environment_;
  ExecutionEngine engine_;
};

}  // namespace rvemu

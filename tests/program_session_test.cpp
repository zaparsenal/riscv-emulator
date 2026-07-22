#include "rvemu/program_session.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kEcallInstruction = 0x00000073U;
constexpr std::uint32_t kEbreakInstruction = 0x00100073U;
constexpr std::uint32_t kAddiX5X0One = 0x00100293U;

class RecordingEnvironment final : public EnvironmentCallHandler {
 public:
  explicit RecordingEnvironment(EnvironmentCallResult result)
      : result_(result) {}

  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall& call, const Memory& memory) override {
    last_call_ = call;
    memory_seen_ = &memory;
    ++call_count_;
    return result_;
  }

  void set_result(EnvironmentCallResult result) { result_ = result; }

  [[nodiscard]] const std::optional<EnvironmentCall>& last_call() const {
    return last_call_;
  }

  [[nodiscard]] const Memory* memory_seen() const { return memory_seen_; }

  [[nodiscard]] std::size_t call_count() const { return call_count_; }

 private:
  EnvironmentCallResult result_;
  std::optional<EnvironmentCall> last_call_;
  const Memory* memory_seen_{nullptr};
  std::size_t call_count_{0U};
};

void write_environment_call(Memory& memory, const std::uint32_t address = 0U) {
  memory.write32(address, kEcallInstruction);
}

TEST(ProgramSessionTest, OrdinaryInstructionUsesTheExistingExecutionEngine) {
  CpuState state;
  Memory memory(8U);
  memory.write32(0U, kAddiX5X0One);
  RecordingEnvironment environment(EnvironmentCallNotHandled{});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionInstructionCompleted>(result));
  const SessionInstructionCompleted completed =
      std::get<SessionInstructionCompleted>(result);
  EXPECT_EQ(completed.step.program_counter, 0U);
  EXPECT_EQ(completed.step.next_program_counter, 4U);
  EXPECT_EQ(state.read_register(5U), 1U);
  EXPECT_EQ(environment.call_count(), 0U);
}

TEST(ProgramSessionTest, CapturesLinuxStyleNumberAndSixArgumentsBeforeDispatch) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  state.write_register(17U, 64U);
  for (std::size_t index = 0U; index < EnvironmentCall::kArgumentCount;
       ++index) {
    state.write_register(10U + index,
                         static_cast<std::uint32_t>(0x100U + index));
  }
  RecordingEnvironment environment(EnvironmentCallResume{4U});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionEnvironmentCallResumed>(result));
  ASSERT_TRUE(environment.last_call().has_value());
  EXPECT_EQ(environment.last_call()->program_counter, 0U);
  EXPECT_EQ(environment.last_call()->number, 64U);
  for (std::size_t index = 0U; index < EnvironmentCall::kArgumentCount;
       ++index) {
    EXPECT_EQ(environment.last_call()->arguments[index], 0x100U + index);
  }
  EXPECT_EQ(environment.memory_seen(), &memory);
}

TEST(ProgramSessionTest, ResumedEnvironmentCallWritesA0AndAdvancesPc) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  state.write_register(10U, 0xAAAAAAAAU);
  for (std::size_t index = 11U; index <= 17U; ++index) {
    state.write_register(index, static_cast<std::uint32_t>(0x11110000U + index));
  }
  state.write_register(5U, 0x55555555U);
  RecordingEnvironment environment(EnvironmentCallResume{0x12345678U});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  const auto& resumed = std::get<SessionEnvironmentCallResumed>(result);
  EXPECT_EQ(resumed.call.arguments[0U], 0xAAAAAAAAU);
  EXPECT_EQ(resumed.return_value, 0x12345678U);
  EXPECT_EQ(resumed.next_program_counter, 4U);
  EXPECT_EQ(state.read_register(10U), 0x12345678U);
  for (std::size_t index = 11U; index <= 17U; ++index) {
    EXPECT_EQ(state.read_register(index),
              static_cast<std::uint32_t>(0x11110000U + index));
  }
  EXPECT_EQ(state.read_register(5U), 0x55555555U);
  EXPECT_EQ(state.program_counter(), 4U);
}

TEST(ProgramSessionTest, ResumedEnvironmentCallWrapsPcAtAddressSpaceTop) {
  CpuState state;
  Memory memory(0xFFFFFFFCU, 4U);
  write_environment_call(memory, 0xFFFFFFFCU);
  state.set_program_counter(0xFFFFFFFCU);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionEnvironmentCallResumed>(result));
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(ProgramSessionTest, ExitLeavesPreciseEcallStateUnchanged) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  state.write_register(10U, 7U);
  RecordingEnvironment environment(EnvironmentCallExit{7U});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(std::holds_alternative<SessionExited>(result));
  EXPECT_EQ(std::get<SessionExited>(result).exit_code, 7U);
  EXPECT_EQ(state.program_counter(), 0U);
  EXPECT_EQ(state.read_register(10U), 7U);
}

TEST(ProgramSessionTest, UnhandledCallPreservesTheOriginalPreciseTrap) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  memory.write32(4U, 0xA5A5A5A5U);
  state.write_register(10U, 0x12345678U);
  state.write_register(17U, 0xFFFFFFFFU);
  RecordingEnvironment environment(EnvironmentCallNotHandled{});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionUnhandledEnvironmentCall>(result));
  const SessionUnhandledEnvironmentCall unhandled =
      std::get<SessionUnhandledEnvironmentCall>(result);
  EXPECT_EQ(unhandled.call.number, 0xFFFFFFFFU);
  EXPECT_EQ(unhandled.trap.cause, TrapCause::EnvironmentCallFromUserMode);
  EXPECT_EQ(unhandled.trap.program_counter, 0U);
  EXPECT_EQ(state.program_counter(), 0U);
  EXPECT_EQ(state.read_register(10U), 0x12345678U);
  EXPECT_EQ(state.read_register(17U), 0xFFFFFFFFU);
  EXPECT_EQ(memory.read32(4U), 0xA5A5A5A5U);
}

TEST(ProgramSessionTest, NonEnvironmentTrapPassesThroughWithoutDispatch) {
  CpuState state;
  Memory memory(8U);
  memory.write32(0U, kEbreakInstruction);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  EXPECT_EQ(std::get<Trap>(result).cause, TrapCause::Breakpoint);
  EXPECT_EQ(environment.call_count(), 0U);
}

TEST(ProgramSessionTest, RunCountsRetiredInstructionsSeparatelyFromCalls) {
  CpuState state;
  Memory memory(16U);
  memory.write32(0U, kAddiX5X0One);
  write_environment_call(memory, 4U);
  memory.write32(8U, kEbreakInstruction);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(10U);

  ASSERT_TRUE(std::holds_alternative<SessionRunTrapped>(result));
  const SessionRunTrapped trapped = std::get<SessionRunTrapped>(result);
  EXPECT_EQ(trapped.statistics.instructions_retired, 1U);
  EXPECT_EQ(trapped.statistics.environment_calls_handled, 1U);
  EXPECT_EQ(trapped.statistics.guest_steps, 2U);
  EXPECT_EQ(trapped.trap.cause, TrapCause::Breakpoint);
  EXPECT_EQ(trapped.trap.program_counter, 8U);
}

TEST(ProgramSessionTest, StepLimitAlsoBoundsResumedEnvironmentCalls) {
  CpuState state;
  Memory memory(12U);
  write_environment_call(memory, 0U);
  write_environment_call(memory, 4U);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(1U);

  ASSERT_TRUE(std::holds_alternative<SessionStepLimitReached>(result));
  const SessionStatistics statistics =
      std::get<SessionStepLimitReached>(result).statistics;
  EXPECT_EQ(statistics.instructions_retired, 0U);
  EXPECT_EQ(statistics.environment_calls_handled, 1U);
  EXPECT_EQ(statistics.guest_steps, 1U);
  EXPECT_EQ(environment.call_count(), 1U);
  EXPECT_EQ(state.program_counter(), 4U);
}

TEST(ProgramSessionTest, RunReportsExitAndCountsTheHandledCall) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  RecordingEnvironment environment(EnvironmentCallExit{42U});
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(4U);

  ASSERT_TRUE(std::holds_alternative<SessionRunExited>(result));
  const SessionRunExited exited = std::get<SessionRunExited>(result);
  EXPECT_EQ(exited.statistics.instructions_retired, 0U);
  EXPECT_EQ(exited.statistics.environment_calls_handled, 1U);
  EXPECT_EQ(exited.statistics.guest_steps, 1U);
  EXPECT_EQ(exited.exit.exit_code, 42U);
}

TEST(ProgramSessionTest, RunReportsUnhandledCallWithoutCountingAGuestStep) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  state.write_register(17U, 0xFFFFFFFFU);
  RecordingEnvironment environment(EnvironmentCallNotHandled{});
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(4U);

  ASSERT_TRUE(
      std::holds_alternative<SessionRunUnhandledEnvironmentCall>(result));
  const SessionRunUnhandledEnvironmentCall unhandled =
      std::get<SessionRunUnhandledEnvironmentCall>(result);
  EXPECT_EQ(unhandled.statistics.guest_steps, 0U);
  EXPECT_EQ(unhandled.statistics.instructions_retired, 0U);
  EXPECT_EQ(unhandled.statistics.environment_calls_handled, 0U);
  EXPECT_EQ(unhandled.environment_call.call.number, 0xFFFFFFFFU);
  EXPECT_EQ(unhandled.environment_call.trap.cause,
            TrapCause::EnvironmentCallFromUserMode);
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(ProgramSessionTest, BreakpointStopsBeforeExecutingItsInstruction) {
  CpuState state;
  Memory memory(8U);
  memory.write32(0U, kAddiX5X0One);
  RecordingEnvironment environment(EnvironmentCallNotHandled{});
  ProgramSession session(state, memory, environment);
  const std::array<std::uint32_t, 1U> breakpoints{0U};

  const SessionRunResult result = session.run(4U, breakpoints);

  ASSERT_TRUE(std::holds_alternative<SessionBreakpointReached>(result));
  const SessionBreakpointReached breakpoint =
      std::get<SessionBreakpointReached>(result);
  EXPECT_EQ(breakpoint.program_counter, 0U);
  EXPECT_EQ(breakpoint.statistics.guest_steps, 0U);
  EXPECT_EQ(breakpoint.statistics.instructions_retired, 0U);
  EXPECT_EQ(state.read_register(5U), 0U);
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(ProgramSessionTest, BreakpointAfterCallObservesTheResumedPc) {
  CpuState state;
  Memory memory(12U);
  write_environment_call(memory, 0U);
  memory.write32(4U, kAddiX5X0One);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);
  const std::array<std::uint32_t, 1U> breakpoints{4U};

  const SessionRunResult result = session.run(4U, breakpoints);

  ASSERT_TRUE(std::holds_alternative<SessionBreakpointReached>(result));
  const SessionBreakpointReached breakpoint =
      std::get<SessionBreakpointReached>(result);
  EXPECT_EQ(breakpoint.program_counter, 4U);
  EXPECT_EQ(breakpoint.statistics.guest_steps, 1U);
  EXPECT_EQ(breakpoint.statistics.instructions_retired, 0U);
  EXPECT_EQ(breakpoint.statistics.environment_calls_handled, 1U);
  EXPECT_EQ(state.read_register(5U), 0U);
}

TEST(ProgramSessionTest, ZeroStepLimitDoesNotExecuteOrDispatch) {
  CpuState state;
  Memory memory(8U);
  write_environment_call(memory);
  RecordingEnvironment environment(EnvironmentCallResume{0U});
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(0U);

  ASSERT_TRUE(std::holds_alternative<SessionStepLimitReached>(result));
  EXPECT_EQ(environment.call_count(), 0U);
  EXPECT_EQ(state.program_counter(), 0U);
}

}  // namespace
}  // namespace rvemu

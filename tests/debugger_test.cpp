#include "rvemu_debugger.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu::debugger {
namespace {

constexpr std::uint32_t kAddiX1X0One = 0x00100093U;
constexpr std::uint32_t kAddiX1X1One = 0x00108093U;
constexpr std::uint32_t kEnvironmentCall = 0x00000073U;

class ExitEnvironment final : public EnvironmentCallHandler {
 public:
  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall& call, const Memory&) noexcept override {
    if (call.number == 93U || call.number == 94U) {
      return EnvironmentCallExit{call.arguments[0U] & 0xFFU};
    }
    return EnvironmentCallResume{0U};
  }
};

TEST(InteractiveDebuggerTest, DisplaysRegistersAndLittleEndianMemory) {
  CpuState state;
  state.write_register(2U, 0xA5A5A5A5U);
  Memory memory(32U);
  memory.write32(0U, 0x12345678U);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input("help\nregisters\nmemory 0x0 4\nquit\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<DebuggerQuit>(result));
  EXPECT_EQ(std::get<DebuggerQuit>(result).statistics.guest_steps, 0U);
  EXPECT_NE(output.str().find("pc = 0x00000000"), std::string::npos);
  EXPECT_NE(output.str().find("continue, c"), std::string::npos);
  EXPECT_NE(output.str().find("x2 (sp) = 0xa5a5a5a5"), std::string::npos);
  EXPECT_NE(output.str().find("0x00000000: 78 56 34 12"),
            std::string::npos);
}

TEST(InteractiveDebuggerTest, StopsBeforeBreakpointAndSingleStepsFromIt) {
  CpuState state;
  Memory memory(16U);
  memory.write32(0U, kAddiX1X0One);
  memory.write32(4U, kAddiX1X1One);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input(
      "break 4\ncontinue\nregisters\nstep\nquit\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<DebuggerQuit>(result));
  const SessionStatistics statistics =
      std::get<DebuggerQuit>(result).statistics;
  EXPECT_EQ(statistics.guest_steps, 2U);
  EXPECT_EQ(statistics.instructions_retired, 2U);
  EXPECT_EQ(state.read_register(1U), 2U);
  EXPECT_EQ(state.program_counter(), 8U);
  EXPECT_NE(output.str().find("breakpoint reached at PC 0x00000004"),
            std::string::npos);
  EXPECT_NE(output.str().find("x1 (ra) = 0x00000001"), std::string::npos);
  EXPECT_NE(output.str().find("stepped to PC 0x00000008"),
            std::string::npos);
}

TEST(InteractiveDebuggerTest, ContinueStepsOverPreviousBreakpointAndExits) {
  CpuState state;
  state.write_register(10U, 7U);
  state.write_register(17U, 93U);
  Memory memory(16U);
  memory.write32(0U, kAddiX1X0One);
  memory.write32(4U, kAddiX1X1One);
  memory.write32(8U, kEnvironmentCall);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input(
      "break 4\nbreak 8\ncontinue\ndelete 8\ncontinue\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<SessionRunExited>(result));
  const SessionRunExited& exited = std::get<SessionRunExited>(result);
  EXPECT_EQ(exited.exit.exit_code, 7U);
  EXPECT_EQ(exited.statistics.guest_steps, 3U);
  EXPECT_EQ(exited.statistics.instructions_retired, 2U);
  EXPECT_EQ(exited.statistics.environment_calls_handled, 1U);
  EXPECT_EQ(state.read_register(1U), 2U);
  EXPECT_EQ(state.program_counter(), 8U);
  EXPECT_NE(output.str().find("guest exited with status 7"),
            std::string::npos);
  EXPECT_NE(output.str().find("breakpoint deleted at 0x00000008"),
            std::string::npos);
}

TEST(InteractiveDebuggerTest, EnforcesOneCumulativeGuestStepLimit) {
  CpuState state;
  Memory memory(16U);
  memory.write32(0U, kAddiX1X0One);
  memory.write32(4U, kAddiX1X1One);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 2U);
  std::istringstream input("step\nstep\ncontinue\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<SessionStepLimitReached>(result));
  const SessionStatistics statistics =
      std::get<SessionStepLimitReached>(result).statistics;
  EXPECT_EQ(statistics.guest_steps, 2U);
  EXPECT_EQ(statistics.instructions_retired, 2U);
  EXPECT_EQ(state.program_counter(), 8U);
}

TEST(InteractiveDebuggerTest, ReportsInvalidCommandsAndInspectionRanges) {
  CpuState state;
  Memory memory(8U);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input(
      "break 2\nbreak 12\nmemory 7 2\nmemory 0 0\nwat\n"
      "delete 4\nbreak 0\nbreak 0\ndelete all\nbreak\nquit\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  EXPECT_TRUE(std::holds_alternative<DebuggerQuit>(result));
  EXPECT_NE(output.str().find("four-byte aligned"), std::string::npos);
  EXPECT_NE(output.str().find("cannot set breakpoint"), std::string::npos);
  EXPECT_NE(output.str().find("cannot inspect memory"), std::string::npos);
  EXPECT_NE(output.str().find("count from 1 to 256"), std::string::npos);
  EXPECT_NE(output.str().find("unknown debugger command: wat"),
            std::string::npos);
  EXPECT_NE(output.str().find("no breakpoint exists at 0x00000004"),
            std::string::npos);
  EXPECT_NE(output.str().find("breakpoint already exists at 0x00000000"),
            std::string::npos);
  EXPECT_NE(output.str().find("all breakpoints deleted"), std::string::npos);
  EXPECT_NE(output.str().find("no breakpoints"), std::string::npos);
}

TEST(InteractiveDebuggerTest, ReturnsArchitecturalTrapWithoutCountingAStep) {
  CpuState state;
  Memory memory(4U);
  memory.write32(0U, 0xFFFFFFFFU);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input("step\n");
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<SessionRunTrapped>(result));
  const SessionRunTrapped& trapped = std::get<SessionRunTrapped>(result);
  EXPECT_EQ(trapped.trap.cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(trapped.statistics.guest_steps, 0U);
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(InteractiveDebuggerTest, EndOfInputLeavesStateUntouched) {
  CpuState state;
  Memory memory(4U);
  ExitEnvironment environment;
  ProgramSession session(state, memory, environment);
  InteractiveDebugger debugger(state, memory, session, 10U);
  std::istringstream input;
  std::ostringstream output;

  const DebuggerResult result = debugger.run(input, output);

  ASSERT_TRUE(std::holds_alternative<DebuggerQuit>(result));
  EXPECT_EQ(std::get<DebuggerQuit>(result).statistics.guest_steps, 0U);
  EXPECT_EQ(state.program_counter(), 0U);
  EXPECT_NE(output.str().find("input closed; leaving debugger"),
            std::string::npos);
}

}  // namespace
}  // namespace rvemu::debugger

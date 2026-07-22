#include "rvemu/linux_syscalls.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kEcallInstruction = 0x00000073U;

struct OutputRecord {
  OutputStream stream;
  std::vector<Memory::Byte> bytes;
};

class RecordingOutputSink final : public OutputSink {
 public:
  [[nodiscard]] OutputWriteResult write(
      const OutputStream stream,
      const std::span<const Memory::Byte> bytes) override {
    if (throw_on_write_) {
      throw std::runtime_error("output failure");
    }
    records_.push_back(OutputRecord{
        stream, std::vector<Memory::Byte>(bytes.begin(), bytes.end())});
    if (next_result_.has_value()) {
      return *next_result_;
    }
    return OutputWriteCompleted{bytes.size()};
  }

  void set_next_result(OutputWriteResult result) {
    next_result_ = std::move(result);
  }

  void set_throw_on_write() noexcept { throw_on_write_ = true; }

  [[nodiscard]] const std::vector<OutputRecord>& records() const noexcept {
    return records_;
  }

 private:
  std::vector<OutputRecord> records_;
  std::optional<OutputWriteResult> next_result_;
  bool throw_on_write_{false};
};

[[nodiscard]] EnvironmentCall make_call(
    const std::uint32_t number,
    const std::array<std::uint32_t, EnvironmentCall::kArgumentCount>&
        arguments = {}) {
  return EnvironmentCall{0x1000U, number, arguments};
}

[[nodiscard]] std::uint32_t resume_value(
    const EnvironmentCallResult& result) {
  return std::get<EnvironmentCallResume>(result).return_value;
}

TEST(LinuxSyscallEnvironmentTest, WriteCopiesStdoutBytesAndReturnsTheirCount) {
  Memory memory(0x1000U, 16U);
  memory.write8(0x1003U, static_cast<Memory::Byte>('R'));
  memory.write8(0x1004U, 0U);
  memory.write8(0x1005U, static_cast<Memory::Byte>('V'));
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0x1003U, 3U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 3U);
  ASSERT_EQ(output.records().size(), 1U);
  EXPECT_EQ(output.records()[0U].stream, OutputStream::StandardOutput);
  EXPECT_EQ(output.records()[0U].bytes,
            (std::vector<Memory::Byte>{static_cast<Memory::Byte>('R'),
                                       0U,
                                       static_cast<Memory::Byte>('V')}));
}

TEST(LinuxSyscallEnvironmentTest, WriteRoutesFileDescriptorTwoToStderr) {
  Memory memory(4U);
  memory.write8(0U, 0xA5U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      2U, 0U, 1U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 1U);
  ASSERT_EQ(output.records().size(), 1U);
  EXPECT_EQ(output.records()[0U].stream, OutputStream::StandardError);
}

TEST(LinuxSyscallEnvironmentTest, WriteReportsPartialSinkProgress) {
  Memory memory(4U);
  RecordingOutputSink output;
  output.set_next_result(OutputWriteCompleted{2U});
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0U, 4U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 2U);
  EXPECT_EQ(output.records().size(), 1U);
}

TEST(LinuxSyscallEnvironmentTest, WriteRejectsUnsupportedFileDescriptorsFirst) {
  Memory memory(4U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, 3U> file_descriptors{
      0U, 3U, 0xFFFFFFFFU};

  for (const std::uint32_t file_descriptor : file_descriptors) {
    const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
        file_descriptor, 0xFFFFFFFFU, 0U, 0U, 0U, 0U};
    const EnvironmentCallResult result = environment.handle(
        make_call(LinuxSyscallEnvironment::kWrite, arguments), memory);

    EXPECT_EQ(resume_value(result), 0U - 9U);
  }
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, ZeroLengthWriteDoesNotAccessMemoryOrSink) {
  Memory memory(4U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0xFFFFFFFFU, 0U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 0U);
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, WriteRejectsEveryInvalidGuestRangeAtomically) {
  Memory memory(0x1000U, 8U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::array<std::uint32_t, 2U>, 3U> ranges{{
      {0x0FFFU, 1U},
      {0x1006U, 3U},
      {0xFFFFFFFFU, 2U},
  }};

  for (const auto& range : ranges) {
    const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
        1U, range[0U], range[1U], 0U, 0U, 0U};
    const EnvironmentCallResult result = environment.handle(
        make_call(LinuxSyscallEnvironment::kWrite, arguments), memory);
    EXPECT_EQ(resume_value(result), 0U - 14U);
  }
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, WriteAcceptsARangeEndingAtAddressSpaceTop) {
  Memory memory(0xFFFFFFFCU, 4U);
  memory.write8(0xFFFFFFFEU, 0xA5U);
  memory.write8(0xFFFFFFFFU, 0x5AU);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0xFFFFFFFEU, 2U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 2U);
  ASSERT_EQ(output.records().size(), 1U);
  EXPECT_EQ(output.records()[0U].bytes,
            (std::vector<Memory::Byte>{0xA5U, 0x5AU}));
}

TEST(LinuxSyscallEnvironmentTest, WriteMapsSinkFailureAndExceptionToIoError) {
  Memory memory(4U);
  RecordingOutputSink failed_output;
  failed_output.set_next_result(OutputWriteFailed{});
  LinuxSyscallEnvironment failed_environment(failed_output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0U, 1U, 0U, 0U, 0U};

  const EnvironmentCallResult failed = failed_environment.handle(
      make_call(LinuxSyscallEnvironment::kWrite, arguments), memory);

  EXPECT_EQ(resume_value(failed), 0U - 5U);

  RecordingOutputSink throwing_output;
  throwing_output.set_throw_on_write();
  LinuxSyscallEnvironment throwing_environment(throwing_output);

  const EnvironmentCallResult threw = throwing_environment.handle(
      make_call(LinuxSyscallEnvironment::kWrite, arguments), memory);

  EXPECT_EQ(resume_value(threw), 0U - 5U);
}

TEST(LinuxSyscallEnvironmentTest, WriteRejectsImpossibleSinkProgress) {
  Memory memory(4U);
  RecordingOutputSink output;
  output.set_next_result(OutputWriteCompleted{2U});
  LinuxSyscallEnvironment environment(output);
  const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
      1U, 0U, 1U, 0U, 0U, 0U};

  const EnvironmentCallResult result =
      environment.handle(make_call(LinuxSyscallEnvironment::kWrite, arguments),
                         memory);

  EXPECT_EQ(resume_value(result), 0U - 5U);
}

TEST(LinuxSyscallEnvironmentTest, ExitCallsUseTheLowEightStatusBits) {
  Memory memory(4U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  const std::array<std::array<std::uint32_t, 2U>, 4U> statuses{{
      {0U, 0U},
      {255U, 255U},
      {256U, 0U},
      {0xFFFFFFFFU, 255U},
  }};

  for (const auto& status : statuses) {
    const std::array<std::uint32_t, EnvironmentCall::kArgumentCount> arguments{
        status[0U], 0U, 0U, 0U, 0U, 0U};
    const EnvironmentCallResult exit_result = environment.handle(
        make_call(LinuxSyscallEnvironment::kExit, arguments), memory);
    const EnvironmentCallResult group_result = environment.handle(
        make_call(LinuxSyscallEnvironment::kExitGroup, arguments), memory);

    EXPECT_EQ(std::get<EnvironmentCallExit>(exit_result).exit_code,
              status[1U]);
    EXPECT_EQ(std::get<EnvironmentCallExit>(group_result).exit_code,
              status[1U]);
  }
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, UnknownCallReturnsNotImplementedError) {
  Memory memory(4U);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);

  const EnvironmentCallResult result =
      environment.handle(make_call(0xFFFFFFFFU), memory);

  EXPECT_EQ(resume_value(result), 0U - 38U);
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, ProgramSessionResumesUnknownCallWithEnosys) {
  CpuState state;
  Memory memory(8U);
  memory.write32(0U, kEcallInstruction);
  state.write_register(17U, 0xFFFFFFFFU);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionEnvironmentCallResumed>(result));
  EXPECT_EQ(state.read_register(10U), 0U - 38U);
  EXPECT_EQ(state.program_counter(), 4U);
  EXPECT_TRUE(output.records().empty());
}

TEST(LinuxSyscallEnvironmentTest, ProgramSessionResumesAfterSuccessfulWrite) {
  CpuState state;
  Memory memory(16U);
  memory.write32(0U, kEcallInstruction);
  memory.write8(8U, static_cast<Memory::Byte>('!'));
  state.write_register(10U, 1U);
  state.write_register(11U, 8U);
  state.write_register(12U, 1U);
  state.write_register(17U, LinuxSyscallEnvironment::kWrite);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(
      std::holds_alternative<SessionEnvironmentCallResumed>(result));
  EXPECT_EQ(state.read_register(10U), 1U);
  EXPECT_EQ(state.program_counter(), 4U);
  ASSERT_EQ(output.records().size(), 1U);
  EXPECT_EQ(output.records()[0U].bytes,
            (std::vector<Memory::Byte>{static_cast<Memory::Byte>('!')}));
}

TEST(LinuxSyscallEnvironmentTest, ProgramSessionExitKeepsPreciseEcallState) {
  CpuState state;
  Memory memory(8U);
  memory.write32(0U, kEcallInstruction);
  state.write_register(10U, 0x102U);
  state.write_register(17U, LinuxSyscallEnvironment::kExitGroup);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  ProgramSession session(state, memory, environment);

  const SessionStepResult result = session.step();

  ASSERT_TRUE(std::holds_alternative<SessionExited>(result));
  EXPECT_EQ(std::get<SessionExited>(result).exit_code, 2U);
  EXPECT_EQ(state.read_register(10U), 0x102U);
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(LinuxSyscallEnvironmentTest, ProgramSessionRunsWriteThenExitProgram) {
  constexpr std::uint32_t kAddiA7X0ExitGroup = 0x05E00893U;
  constexpr std::uint32_t kAddiA0X0Seven = 0x00700513U;

  CpuState state;
  Memory memory(24U);
  memory.write32(0U, kEcallInstruction);
  memory.write32(4U, kAddiA7X0ExitGroup);
  memory.write32(8U, kAddiA0X0Seven);
  memory.write32(12U, kEcallInstruction);
  memory.write8(20U, static_cast<Memory::Byte>('!'));
  state.write_register(10U, 1U);
  state.write_register(11U, 20U);
  state.write_register(12U, 1U);
  state.write_register(17U, LinuxSyscallEnvironment::kWrite);
  RecordingOutputSink output;
  LinuxSyscallEnvironment environment(output);
  ProgramSession session(state, memory, environment);

  const SessionRunResult result = session.run(10U);

  ASSERT_TRUE(std::holds_alternative<SessionRunExited>(result));
  const SessionRunExited exited = std::get<SessionRunExited>(result);
  EXPECT_EQ(exited.statistics.guest_steps, 4U);
  EXPECT_EQ(exited.statistics.instructions_retired, 2U);
  EXPECT_EQ(exited.statistics.environment_calls_handled, 2U);
  EXPECT_EQ(exited.exit.exit_code, 7U);
  EXPECT_EQ(exited.exit.call.program_counter, 12U);
  EXPECT_EQ(exited.exit.call.arguments[0U], 7U);
  EXPECT_EQ(state.program_counter(), 12U);
  ASSERT_EQ(output.records().size(), 1U);
  EXPECT_EQ(output.records()[0U].bytes,
            (std::vector<Memory::Byte>{static_cast<Memory::Byte>('!')}));
}

}  // namespace
}  // namespace rvemu

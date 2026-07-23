#include "rvemu/execution_engine.hpp"
#include "rvemu/program_session.hpp"

#include <array>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x1000U;
constexpr std::uint32_t kDataAddress = 0x1080U;
constexpr std::uint32_t kEcallInstruction = 0x00000073U;

[[nodiscard]] constexpr std::uint32_t encode_addi(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

[[nodiscard]] constexpr std::uint32_t encode_load(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source, const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Load);
}

[[nodiscard]] constexpr std::uint32_t encode_store(
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0xFFFU;
  return ((bits >> 5U) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         ((bits & 0x1FU) << 7U) | static_cast<std::uint8_t>(Opcode::Store);
}

[[nodiscard]] constexpr std::uint32_t encode_branch(
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0x1FFFU;
  return (((bits >> 12U) & 0x01U) << 31U) |
         (((bits >> 5U) & 0x3FU) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (((bits >> 1U) & 0x0FU) << 8U) |
         (((bits >> 11U) & 0x01U) << 7U) |
         static_cast<std::uint8_t>(Opcode::Branch);
}

[[nodiscard]] constexpr std::uint32_t encode_jal(
    const std::uint8_t destination, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0x1FFFFFU;
  return (((bits >> 20U) & 0x01U) << 31U) |
         (((bits >> 1U) & 0x3FFU) << 21U) |
         (((bits >> 11U) & 0x01U) << 20U) |
         (((bits >> 12U) & 0xFFU) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Jal);
}

[[nodiscard]] constexpr std::uint32_t encode_jalr(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Jalr);
}

class RecordingObserver final : public ExecutionObserver {
 public:
  RecordingObserver(const CpuState& state, const Memory& memory) noexcept
      : state_(state), memory_(memory) {}

  void observe(const ExecutionObservation& observation) noexcept override {
    observations.push_back(observation);
    committed_program_counters.push_back(state_.program_counter());
    if (observation.data_memory_access.has_value() &&
        observation.data_memory_access->kind ==
            DataMemoryAccessKind::Store) {
      committed_store_bytes.push_back(
          memory_.read8(observation.data_memory_access->address));
    }
  }

  std::vector<ExecutionObservation> observations;
  std::vector<std::uint32_t> committed_program_counters;
  std::vector<std::uint8_t> committed_store_bytes;

 private:
  const CpuState& state_;
  const Memory& memory_;
};

class ResumeEnvironment final : public EnvironmentCallHandler {
 public:
  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall&, const Memory&) noexcept override {
    return EnvironmentCallResume{0U};
  }
};

TEST(ExecutionObservationTest,
     ReportsAnOrdinaryInstructionAfterArchitecturalCommit) {
  CpuState state;
  Memory memory(kBaseAddress, 256U);
  state.set_program_counter(kBaseAddress);
  const std::uint32_t raw = encode_addi(1U, 0U, 42);
  memory.write32(kBaseAddress, raw);
  RecordingObserver observer(state, memory);
  ExecutionEngine engine(state, memory, &observer);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  ASSERT_EQ(observer.observations.size(), 1U);
  const ExecutionObservation& observation = observer.observations.front();
  EXPECT_EQ(observation.program_counter, kBaseAddress);
  EXPECT_EQ(observation.instruction.raw, raw);
  EXPECT_EQ(observation.next_program_counter, kBaseAddress + 4U);
  EXPECT_FALSE(observation.data_memory_access.has_value());
  EXPECT_FALSE(observation.control_flow.has_value());
  EXPECT_EQ(observer.committed_program_counters.front(), kBaseAddress + 4U);
  EXPECT_EQ(state.read_register(1U), 42U);

  const StepCompleted& completed = std::get<StepCompleted>(result);
  EXPECT_EQ(completed.program_counter, observation.program_counter);
  EXPECT_EQ(completed.next_program_counter,
            observation.next_program_counter);
}

TEST(ExecutionObservationTest,
     ReportsOnlySuccessfulDataAccessesWithTheirAddressAndWidth) {
  CpuState state;
  Memory memory(kBaseAddress, 256U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, kDataAddress);
  state.write_register(2U, 0xAABBCCDDU);
  memory.write32(kBaseAddress, encode_load(3U, 1U, 1U, 0));
  memory.write32(kBaseAddress + 4U, encode_store(0U, 1U, 2U, 3));
  memory.write16(kDataAddress, 0x1234U);
  RecordingObserver observer(state, memory);
  ExecutionEngine engine(state, memory, &observer);

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));
  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));

  ASSERT_EQ(observer.observations.size(), 2U);
  const auto& load = observer.observations[0U].data_memory_access;
  ASSERT_TRUE(load.has_value());
  EXPECT_EQ(load->kind, DataMemoryAccessKind::Load);
  EXPECT_EQ(load->address, kDataAddress);
  EXPECT_EQ(load->width_bytes, 2U);
  EXPECT_EQ(state.read_register(3U), 0x1234U);

  const auto& store = observer.observations[1U].data_memory_access;
  ASSERT_TRUE(store.has_value());
  EXPECT_EQ(store->kind, DataMemoryAccessKind::Store);
  EXPECT_EQ(store->address, kDataAddress + 3U);
  EXPECT_EQ(store->width_bytes, 1U);
  ASSERT_EQ(observer.committed_store_bytes.size(), 1U);
  EXPECT_EQ(observer.committed_store_bytes.front(), 0xDDU);
}

TEST(ExecutionObservationTest, ReportsEverySupportedDataAccessWidth) {
  struct AccessCase {
    DataMemoryAccessKind kind;
    std::uint8_t function3;
    std::uint8_t width_bytes;
  };
  constexpr std::array cases{
      AccessCase{DataMemoryAccessKind::Load, 0U, 1U},
      AccessCase{DataMemoryAccessKind::Load, 1U, 2U},
      AccessCase{DataMemoryAccessKind::Load, 2U, 4U},
      AccessCase{DataMemoryAccessKind::Load, 4U, 1U},
      AccessCase{DataMemoryAccessKind::Load, 5U, 2U},
      AccessCase{DataMemoryAccessKind::Store, 0U, 1U},
      AccessCase{DataMemoryAccessKind::Store, 1U, 2U},
      AccessCase{DataMemoryAccessKind::Store, 2U, 4U},
  };

  for (const AccessCase test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 256U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, kDataAddress);
    state.write_register(2U, 0xAABBCCDDU);
    memory.write32(kDataAddress, 0x12345678U);
    const std::uint32_t raw =
        test_case.kind == DataMemoryAccessKind::Load
            ? encode_load(2U, test_case.function3, 1U, 0)
            : encode_store(test_case.function3, 1U, 2U, 0);
    memory.write32(kBaseAddress, raw);
    RecordingObserver observer(state, memory);
    ExecutionEngine engine(state, memory, &observer);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
    ASSERT_EQ(observer.observations.size(), 1U);
    const auto& access = observer.observations.front().data_memory_access;
    ASSERT_TRUE(access.has_value());
    EXPECT_EQ(access->kind, test_case.kind);
    EXPECT_EQ(access->address, kDataAddress);
    EXPECT_EQ(access->width_bytes, test_case.width_bytes);
  }
}

TEST(ExecutionObservationTest,
     ReportsConditionalDirectAndIndirectControlFlowOutcomes) {
  CpuState state;
  Memory memory(kBaseAddress, 256U);
  RecordingObserver observer(state, memory);
  ExecutionEngine engine(state, memory, &observer);

  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 1U);
  state.write_register(2U, 2U);
  memory.write32(kBaseAddress, encode_branch(0U, 1U, 2U, 8));
  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));

  state.set_program_counter(kBaseAddress + 4U);
  state.write_register(2U, 1U);
  memory.write32(kBaseAddress + 4U, encode_branch(0U, 1U, 2U, 8));
  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));

  state.set_program_counter(kBaseAddress + 8U);
  memory.write32(kBaseAddress + 8U, encode_jal(0U, 8));
  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));

  state.set_program_counter(kBaseAddress + 12U);
  state.write_register(3U, kBaseAddress + 24U);
  memory.write32(kBaseAddress + 12U, encode_jalr(0U, 3U, 0));
  ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));

  ASSERT_EQ(observer.observations.size(), 4U);
  const ControlFlowObservation untaken =
      *observer.observations[0U].control_flow;
  EXPECT_EQ(untaken.kind, ControlFlowKind::ConditionalBranch);
  EXPECT_FALSE(untaken.taken);
  EXPECT_EQ(observer.observations[0U].next_program_counter,
            kBaseAddress + 4U);

  const ControlFlowObservation taken =
      *observer.observations[1U].control_flow;
  EXPECT_EQ(taken.kind, ControlFlowKind::ConditionalBranch);
  EXPECT_TRUE(taken.taken);
  EXPECT_EQ(observer.observations[1U].next_program_counter,
            kBaseAddress + 12U);

  const ControlFlowObservation direct =
      *observer.observations[2U].control_flow;
  EXPECT_EQ(direct.kind, ControlFlowKind::DirectJump);
  EXPECT_TRUE(direct.taken);
  EXPECT_EQ(observer.observations[2U].next_program_counter,
            kBaseAddress + 16U);

  const ControlFlowObservation indirect =
      *observer.observations[3U].control_flow;
  EXPECT_EQ(indirect.kind, ControlFlowKind::IndirectJump);
  EXPECT_TRUE(indirect.taken);
  EXPECT_EQ(observer.observations[3U].next_program_counter,
            kBaseAddress + 24U);
}

TEST(ExecutionObservationTest, TrappingInstructionsProduceNoObservation) {
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, 0U);
  RecordingObserver observer(state, memory);
  ExecutionEngine engine(state, memory, &observer);

  const StepResult illegal = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(illegal));
  EXPECT_TRUE(observer.observations.empty());

  state.write_register(1U, kBaseAddress + 1U);
  memory.write32(kBaseAddress, encode_load(2U, 1U, 1U, 0));

  const StepResult misaligned_load = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(misaligned_load));
  EXPECT_EQ(std::get<Trap>(misaligned_load).cause,
            TrapCause::LoadAddressMisaligned);
  EXPECT_TRUE(observer.observations.empty());
}

TEST(ExecutionObservationTest,
     BoundedRunReportsEveryRetiredInstructionInProgramOrder) {
  CpuState state;
  Memory memory(kBaseAddress, 12U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 1));
  memory.write32(kBaseAddress + 4U, encode_addi(1U, 1U, 1));
  memory.write32(kBaseAddress + 8U, 0U);
  RecordingObserver observer(state, memory);
  ExecutionEngine engine(state, memory, &observer);

  const RunResult result = engine.run(3U);

  ASSERT_TRUE(std::holds_alternative<RunTrapped>(result));
  EXPECT_EQ(std::get<RunTrapped>(result).instructions_executed, 2U);
  ASSERT_EQ(observer.observations.size(), 2U);
  EXPECT_EQ(observer.observations[0U].program_counter, kBaseAddress);
  EXPECT_EQ(observer.observations[1U].program_counter, kBaseAddress + 4U);
  EXPECT_EQ(state.read_register(1U), 2U);
}

TEST(ExecutionObservationTest,
     ProgramSessionForwardsRetirementsButNotHostedEnvironmentCalls) {
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 7));
  memory.write32(kBaseAddress + 4U, kEcallInstruction);
  ResumeEnvironment environment;
  RecordingObserver observer(state, memory);
  ProgramSession session(state, memory, environment, &observer);

  const SessionRunResult result = session.run(2U);

  ASSERT_TRUE(std::holds_alternative<SessionStepLimitReached>(result));
  const SessionStatistics statistics =
      std::get<SessionStepLimitReached>(result).statistics;
  EXPECT_EQ(statistics.guest_steps, 2U);
  EXPECT_EQ(statistics.instructions_retired, 1U);
  EXPECT_EQ(statistics.environment_calls_handled, 1U);
  ASSERT_EQ(observer.observations.size(), 1U);
  EXPECT_EQ(observer.observations.front().instruction.raw,
            encode_addi(1U, 0U, 7));
  EXPECT_EQ(state.program_counter(), kBaseAddress + 8U);
}

}  // namespace
}  // namespace rvemu

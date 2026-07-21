#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x4000U;

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
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source, const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Jalr);
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

class ControlFlowInstructionTest : public ::testing::Test {
 protected:
  ControlFlowInstructionTest()
      : memory_(kBaseAddress, 64U), engine_(state_, memory_) {
    state_.set_program_counter(kBaseAddress);
  }

  [[nodiscard]] StepResult execute(const std::uint32_t address,
                                   const std::uint32_t instruction) {
    state_.set_program_counter(address);
    memory_.write32(address, instruction);
    return engine_.step();
  }

  [[nodiscard]] StepResult execute(const std::uint32_t instruction) {
    return execute(kBaseAddress, instruction);
  }

  CpuState state_;
  Memory memory_;
  ExecutionEngine engine_;
};

TEST_F(ControlFlowInstructionTest, JalWritesLinkAndUsesPcRelativeTarget) {
  const StepResult result = execute(encode_jal(1U, 12));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  const auto& completed = std::get<StepCompleted>(result);
  EXPECT_EQ(state_.read_register(1U), kBaseAddress + 4U);
  EXPECT_EQ(completed.next_program_counter, kBaseAddress + 12U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 12U);
}

TEST_F(ControlFlowInstructionTest, JalSupportsBackwardTargets) {
  const StepResult result = execute(kBaseAddress + 8U, encode_jal(1U, -8));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), kBaseAddress + 12U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

TEST(ControlFlowInstructionStandaloneTest, JalTargetWrapsModuloThirtyTwoBits) {
  CpuState state;
  Memory memory(0xFFFFFFFCU, 4U);
  state.set_program_counter(0xFFFFFFFCU);
  memory.write32(0xFFFFFFFCU, encode_jal(1U, 8));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(1U), 0U);
  EXPECT_EQ(state.program_counter(), 4U);
}

TEST_F(ControlFlowInstructionTest, JalMayDiscardLinkThroughZeroRegister) {
  const StepResult result = execute(encode_jal(0U, 8));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(0U), 0U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 8U);
}

TEST_F(ControlFlowInstructionTest, MisalignedJalTargetTrapsBeforeLinkWrite) {
  state_.write_register(1U, 0xDEADBEEFU);

  const StepResult result = execute(encode_jal(1U, 2));

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::InstructionAddressMisaligned);
  EXPECT_EQ(trap.program_counter, kBaseAddress);
  EXPECT_EQ(trap.value, kBaseAddress + 2U);
  EXPECT_EQ(state_.read_register(1U), 0xDEADBEEFU);
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

TEST_F(ControlFlowInstructionTest, JalrClearsTargetBitZeroAndWritesLink) {
  state_.write_register(2U, kBaseAddress + 8U);

  const StepResult result = execute(encode_jalr(1U, 0U, 2U, 1));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), kBaseAddress + 4U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 8U);
}

TEST_F(ControlFlowInstructionTest, JalrReadsAliasedSourceBeforeWritingLink) {
  state_.write_register(1U, kBaseAddress + 12U);

  const StepResult result = execute(encode_jalr(1U, 0U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), kBaseAddress + 4U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 12U);
}

TEST_F(ControlFlowInstructionTest, JalrUsesSignExtendedImmediate) {
  state_.write_register(2U, kBaseAddress + 12U);

  const StepResult result = execute(encode_jalr(1U, 0U, 2U, -4));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), kBaseAddress + 4U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 8U);
}

TEST_F(ControlFlowInstructionTest, MisalignedJalrTargetTrapsBeforeLinkWrite) {
  state_.write_register(1U, 0xDEADBEEFU);
  state_.write_register(2U, kBaseAddress + 2U);

  const StepResult result = execute(encode_jalr(1U, 0U, 2U, 1));

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::InstructionAddressMisaligned);
  EXPECT_EQ(trap.value, kBaseAddress + 2U);
  EXPECT_EQ(state_.read_register(1U), 0xDEADBEEFU);
  EXPECT_EQ(state_.read_register(2U), kBaseAddress + 2U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

TEST(ControlFlowInstructionStandaloneTest,
     ReservedJalrFunctionFieldsTrapWithoutSideEffects) {
  for (std::uint8_t function3 = 1U; function3 <= 7U; ++function3) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 0xDEADBEEFU);
    state.write_register(2U, kBaseAddress + 8U);
    const std::uint32_t instruction = encode_jalr(1U, function3, 2U, 0);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    EXPECT_EQ(std::get<Trap>(result).cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(state.read_register(1U), 0xDEADBEEFU);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

TEST(ControlFlowInstructionStandaloneTest,
     EveryConditionalBranchHandlesTakenAndNotTakenCases) {
  struct BranchCase {
    std::uint8_t function3;
    std::uint32_t source1;
    std::uint32_t source2;
    bool taken;
  };
  constexpr std::array cases{
      BranchCase{0U, 7U, 7U, true},
      BranchCase{0U, 7U, 8U, false},
      BranchCase{1U, 7U, 8U, true},
      BranchCase{1U, 7U, 7U, false},
      BranchCase{4U, 0x80000000U, 0x7FFFFFFFU, true},
      BranchCase{4U, 0x7FFFFFFFU, 0x80000000U, false},
      BranchCase{5U, 0x7FFFFFFFU, 0x80000000U, true},
      BranchCase{5U, 0x80000000U, 0x7FFFFFFFU, false},
      BranchCase{5U, 0x80000000U, 0x80000000U, true},
      BranchCase{6U, 0U, 0xFFFFFFFFU, true},
      BranchCase{6U, 0xFFFFFFFFU, 0U, false},
      BranchCase{7U, 0xFFFFFFFFU, 0U, true},
      BranchCase{7U, 0U, 0xFFFFFFFFU, false},
      BranchCase{7U, 0xFFFFFFFFU, 0xFFFFFFFFU, true},
  };

  for (const auto& test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, test_case.source1);
    state.write_register(2U, test_case.source2);
    memory.write32(kBaseAddress,
                   encode_branch(test_case.function3, 1U, 2U, 8));
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
    const std::uint32_t expected_program_counter =
        kBaseAddress + (test_case.taken ? 8U : 4U);
    EXPECT_EQ(state.program_counter(), expected_program_counter);
  }
}

TEST_F(ControlFlowInstructionTest, TakenBranchSupportsBackwardTarget) {
  state_.write_register(1U, 5U);
  state_.write_register(2U, 5U);

  const StepResult result = execute(
      kBaseAddress + 8U, encode_branch(0U, 1U, 2U, -8));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

TEST(ControlFlowInstructionStandaloneTest,
     TakenBranchTargetWrapsModuloThirtyTwoBits) {
  CpuState state;
  Memory memory(0xFFFFFFFCU, 4U);
  state.set_program_counter(0xFFFFFFFCU);
  state.write_register(1U, 5U);
  state.write_register(2U, 5U);
  memory.write32(0xFFFFFFFCU, encode_branch(0U, 1U, 2U, 4));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST_F(ControlFlowInstructionTest,
       UntakenBranchDoesNotValidateMisalignedTarget) {
  state_.write_register(1U, 1U);
  state_.write_register(2U, 2U);

  const StepResult result = execute(encode_branch(0U, 1U, 2U, 2));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST_F(ControlFlowInstructionTest,
       TakenMisalignedBranchTrapsWithoutArchitecturalMutation) {
  state_.write_register(1U, 7U);
  state_.write_register(2U, 7U);

  const StepResult result = execute(encode_branch(0U, 1U, 2U, 2));

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::InstructionAddressMisaligned);
  EXPECT_EQ(trap.value, kBaseAddress + 2U);
  EXPECT_EQ(state_.read_register(1U), 7U);
  EXPECT_EQ(state_.read_register(2U), 7U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

TEST(ControlFlowInstructionStandaloneTest,
     ReservedBranchFunctionFieldsTrapWithoutSideEffects) {
  constexpr std::array<std::uint8_t, 2U> invalid_function3{2U, 3U};

  for (const std::uint8_t function3 : invalid_function3) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 5U);
    state.write_register(2U, 5U);
    const std::uint32_t instruction =
        encode_branch(function3, 1U, 2U, 8);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

TEST_F(ControlFlowInstructionTest, RunCanRetireBoundedSelfLoop) {
  memory_.write32(kBaseAddress, encode_jal(0U, 0));

  const RunResult result = engine_.run(3U);

  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(result));
  EXPECT_EQ(std::get<InstructionLimitReached>(result).instructions_executed, 3U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress);
}

}  // namespace
}  // namespace rvemu

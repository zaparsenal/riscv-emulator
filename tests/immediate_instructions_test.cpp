#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x2000U;

[[nodiscard]] constexpr std::uint32_t encode_op_immediate(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source, const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

[[nodiscard]] constexpr std::uint32_t encode_shift_immediate(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source, const std::uint8_t function7,
    const std::uint8_t shift_amount) noexcept {
  return (static_cast<std::uint32_t>(function7) << 25U) |
         ((static_cast<std::uint32_t>(shift_amount) & 0x1FU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

[[nodiscard]] constexpr std::uint32_t encode_upper_immediate(
    const Opcode opcode, const std::uint8_t destination,
    const std::uint32_t immediate) noexcept {
  return (immediate & 0xFFFFF000U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(opcode);
}

class ImmediateInstructionTest : public ::testing::Test {
 protected:
  ImmediateInstructionTest()
      : memory_(kBaseAddress, 4U), engine_(state_, memory_) {
    state_.set_program_counter(kBaseAddress);
  }

  [[nodiscard]] StepResult execute(const std::uint32_t instruction) {
    memory_.write32(kBaseAddress, instruction);
    return engine_.step();
  }

  CpuState state_;
  Memory memory_;
  ExecutionEngine engine_;
};

TEST_F(ImmediateInstructionTest, LuiWritesUpperImmediate) {
  const StepResult result =
      execute(encode_upper_immediate(Opcode::Lui, 5U, 0xABCDE000U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(5U), 0xABCDE000U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST_F(ImmediateInstructionTest, LuiCannotModifyZeroRegister) {
  const StepResult result =
      execute(encode_upper_immediate(Opcode::Lui, 0U, 0xFFFFF000U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(0U), 0U);
}

TEST_F(ImmediateInstructionTest, AuipcUsesAddressOfCurrentInstruction) {
  const StepResult result =
      execute(encode_upper_immediate(Opcode::Auipc, 6U, 0xFFFFF000U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(6U), 0x1000U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST(ImmediateInstructionStandaloneTest, AuipcWrapsAtAddressSpaceBoundary) {
  CpuState state;
  Memory memory(0xFFFFFFFCU, 4U);
  state.set_program_counter(0xFFFFFFFCU);
  memory.write32(
      0xFFFFFFFCU,
      encode_upper_immediate(Opcode::Auipc, 6U, 0x00001000U));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(6U), 0x00000FFCU);
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST_F(ImmediateInstructionTest, AddiWrapsPositiveOverflow) {
  state_.write_register(1U, std::numeric_limits<std::uint32_t>::max());

  const StepResult result = execute(encode_op_immediate(2U, 0U, 1U, 1));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0U);
}

TEST_F(ImmediateInstructionTest, SltiUsesSignedRiscVOrderingAtBoundaries) {
  struct SignedComparisonCase {
    std::uint32_t source;
    std::uint32_t expected;
  };
  constexpr std::array cases{
      SignedComparisonCase{0x80000000U, 1U},
      SignedComparisonCase{0x7FFFFFFFU, 0U},
      SignedComparisonCase{0xFFFFFFFFU, 0U},
  };

  for (const auto& test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, test_case.source);
    memory.write32(kBaseAddress,
                   encode_op_immediate(2U, 2U, 1U, -1));
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
    EXPECT_EQ(state.read_register(2U), test_case.expected);
  }
}

TEST_F(ImmediateInstructionTest, SltiuComparesSignExtendedImmediateAsUnsigned) {
  state_.write_register(1U, 0U);
  const StepResult less_result =
      execute(encode_op_immediate(2U, 3U, 1U, -1));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(less_result));
  EXPECT_EQ(state_.read_register(2U), 1U);

  CpuState equal_state;
  Memory equal_memory(kBaseAddress, 4U);
  equal_state.set_program_counter(kBaseAddress);
  equal_state.write_register(1U, 0xFFFFFFFFU);
  equal_memory.write32(kBaseAddress,
                       encode_op_immediate(2U, 3U, 1U, -1));
  ExecutionEngine equal_engine(equal_state, equal_memory);

  const StepResult equal_result = equal_engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(equal_result));
  EXPECT_EQ(equal_state.read_register(2U), 0U);
}

TEST_F(ImmediateInstructionTest, XoriUsesSignExtendedImmediate) {
  state_.write_register(1U, 0xAA55F00FU);

  const StepResult result = execute(encode_op_immediate(2U, 4U, 1U, -256));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x55AA0F0FU);
}

TEST_F(ImmediateInstructionTest, OriUsesSignExtendedImmediate) {
  state_.write_register(1U, 0xAA55000FU);

  const StepResult result = execute(encode_op_immediate(2U, 6U, 1U, 0x0F0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0xAA5500FFU);
}

TEST_F(ImmediateInstructionTest, AndiUsesSignExtendedImmediate) {
  state_.write_register(1U, 0xAA55F00FU);

  const StepResult result = execute(encode_op_immediate(2U, 7U, 1U, -256));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0xAA55F000U);
}

TEST_F(ImmediateInstructionTest, SlliSupportsZeroAndMaximumShiftAmounts) {
  state_.write_register(1U, 1U);
  const StepResult maximum_result =
      execute(encode_shift_immediate(2U, 1U, 1U, 0x00U, 31U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(maximum_result));
  EXPECT_EQ(state_.read_register(2U), 0x80000000U);

  CpuState zero_state;
  Memory zero_memory(kBaseAddress, 4U);
  zero_state.set_program_counter(kBaseAddress);
  zero_state.write_register(1U, 0xA5A5A5A5U);
  zero_memory.write32(
      kBaseAddress, encode_shift_immediate(2U, 1U, 1U, 0x00U, 0U));
  ExecutionEngine zero_engine(zero_state, zero_memory);

  const StepResult zero_result = zero_engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(zero_result));
  EXPECT_EQ(zero_state.read_register(2U), 0xA5A5A5A5U);
}

TEST_F(ImmediateInstructionTest, SrliPerformsLogicalRightShift) {
  state_.write_register(1U, 0x80000001U);

  const StepResult result =
      execute(encode_shift_immediate(2U, 5U, 1U, 0x00U, 31U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 1U);
}

TEST_F(ImmediateInstructionTest, SraiExplicitlySignFillsNegativeValues) {
  state_.write_register(1U, 0x80000000U);

  const StepResult result =
      execute(encode_shift_immediate(2U, 5U, 1U, 0x20U, 4U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0xF8000000U);
}

TEST_F(ImmediateInstructionTest, SraiKeepsPositiveValuesPositive) {
  state_.write_register(1U, 0x7FFFFFFFU);

  const StepResult result =
      execute(encode_shift_immediate(2U, 5U, 1U, 0x20U, 31U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0U);
}

TEST_F(ImmediateInstructionTest, SraiByZeroLeavesValueUnchanged) {
  state_.write_register(1U, 0x80000001U);

  const StepResult result =
      execute(encode_shift_immediate(2U, 5U, 1U, 0x20U, 0U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x80000001U);
}

TEST_F(ImmediateInstructionTest,
       ReservedShiftEncodingsTrapWithoutArchitecturalSideEffects) {
  constexpr std::array invalid_instructions{
      encode_shift_immediate(2U, 1U, 1U, 0x01U, 3U),
      encode_shift_immediate(2U, 5U, 1U, 0x01U, 3U),
      encode_shift_immediate(2U, 5U, 1U, 0x21U, 3U),
  };

  for (const std::uint32_t instruction : invalid_instructions) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 0x12345678U);
    state.write_register(2U, 0xDEADBEEFU);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.read_register(1U), 0x12345678U);
    EXPECT_EQ(state.read_register(2U), 0xDEADBEEFU);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

}  // namespace
}  // namespace rvemu

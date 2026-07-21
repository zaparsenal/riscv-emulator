#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x3000U;

[[nodiscard]] constexpr std::uint32_t encode_register_operation(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source1, const std::uint8_t source2,
    const std::uint8_t function7) noexcept {
  return (static_cast<std::uint32_t>(function7) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Op);
}

class RegisterInstructionTest : public ::testing::Test {
 protected:
  RegisterInstructionTest()
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

TEST_F(RegisterInstructionTest, AddWrapsOverflowModuloThirtyTwoBits) {
  state_.write_register(1U, std::numeric_limits<std::uint32_t>::max());
  state_.write_register(2U, 1U);

  const StepResult result =
      execute(encode_register_operation(3U, 0U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST_F(RegisterInstructionTest, SubWrapsUnderflowModuloThirtyTwoBits) {
  state_.write_register(1U, 0U);
  state_.write_register(2U, 1U);

  const StepResult result =
      execute(encode_register_operation(3U, 0U, 1U, 2U, 0x20U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0xFFFFFFFFU);
}

TEST_F(RegisterInstructionTest, SllUsesOnlyLowFiveShiftCountBits) {
  state_.write_register(1U, 1U);
  state_.write_register(2U, 63U);

  const StepResult result =
      execute(encode_register_operation(3U, 1U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0x80000000U);
}

TEST(RegisterInstructionStandaloneTest, SltUsesSignedRiscVOrdering) {
  struct SignedComparisonCase {
    std::uint32_t left;
    std::uint32_t right;
    std::uint32_t expected;
  };
  constexpr std::array cases{
      SignedComparisonCase{0x80000000U, 0x7FFFFFFFU, 1U},
      SignedComparisonCase{0x7FFFFFFFU, 0x80000000U, 0U},
      SignedComparisonCase{0xFFFFFFFFU, 0xFFFFFFFFU, 0U},
  };

  for (const auto& test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, test_case.left);
    state.write_register(2U, test_case.right);
    memory.write32(
        kBaseAddress,
        encode_register_operation(3U, 2U, 1U, 2U, 0x00U));
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
    EXPECT_EQ(state.read_register(3U), test_case.expected);
  }
}

TEST_F(RegisterInstructionTest, SltuUsesUnsignedOrdering) {
  state_.write_register(1U, 0xFFFFFFFFU);
  state_.write_register(2U, 0U);

  const StepResult result =
      execute(encode_register_operation(3U, 3U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0U);
}

TEST_F(RegisterInstructionTest, XorCombinesBothSources) {
  state_.write_register(1U, 0xAA55F00FU);
  state_.write_register(2U, 0x0FF00FF0U);

  const StepResult result =
      execute(encode_register_operation(3U, 4U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0xA5A5FFFFU);
}

TEST_F(RegisterInstructionTest, SrlZeroFillsAndMasksShiftCount) {
  state_.write_register(1U, 0x80000001U);
  state_.write_register(2U, 63U);

  const StepResult result =
      execute(encode_register_operation(3U, 5U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 1U);
}

TEST_F(RegisterInstructionTest, SraSignFillsAndMasksShiftCount) {
  state_.write_register(1U, 0x80000000U);
  state_.write_register(2U, 36U);

  const StepResult result =
      execute(encode_register_operation(3U, 5U, 1U, 2U, 0x20U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0xF8000000U);
}

TEST_F(RegisterInstructionTest, OrCombinesBothSources) {
  state_.write_register(1U, 0xAA55000FU);
  state_.write_register(2U, 0x00FF0FF0U);

  const StepResult result =
      execute(encode_register_operation(3U, 6U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0xAAFF0FFFU);
}

TEST_F(RegisterInstructionTest, AndCombinesBothSources) {
  state_.write_register(1U, 0xAA55F00FU);
  state_.write_register(2U, 0x0FF00FF0U);

  const StepResult result =
      execute(encode_register_operation(3U, 7U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(3U), 0x0A500000U);
}

TEST_F(RegisterInstructionTest, DestinationMayAliasFirstSource) {
  state_.write_register(1U, 5U);
  state_.write_register(2U, 7U);

  const StepResult result =
      execute(encode_register_operation(1U, 0U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), 12U);
  EXPECT_EQ(state_.read_register(2U), 7U);
}

TEST_F(RegisterInstructionTest, DestinationMayAliasSecondSource) {
  state_.write_register(1U, 5U);
  state_.write_register(2U, 7U);

  const StepResult result =
      execute(encode_register_operation(2U, 0U, 1U, 2U, 0x20U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), 5U);
  EXPECT_EQ(state_.read_register(2U), 0xFFFFFFFEU);
}

TEST_F(RegisterInstructionTest, CannotModifyZeroRegister) {
  state_.write_register(1U, 5U);
  state_.write_register(2U, 7U);

  const StepResult result =
      execute(encode_register_operation(0U, 0U, 1U, 2U, 0x00U));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(0U), 0U);
}

TEST(RegisterInstructionStandaloneTest,
     ReservedEncodingsTrapWithoutArchitecturalSideEffects) {
  constexpr std::array invalid_instructions{
      encode_register_operation(3U, 0U, 1U, 2U, 0x10U),
      encode_register_operation(3U, 1U, 1U, 2U, 0x20U),
      encode_register_operation(3U, 2U, 1U, 2U, 0x20U),
      encode_register_operation(3U, 3U, 1U, 2U, 0x20U),
      encode_register_operation(3U, 4U, 1U, 2U, 0x20U),
      encode_register_operation(3U, 5U, 1U, 2U, 0x10U),
      encode_register_operation(3U, 6U, 1U, 2U, 0x20U),
      encode_register_operation(3U, 7U, 1U, 2U, 0x20U),
  };

  for (const std::uint32_t instruction : invalid_instructions) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 0x12345678U);
    state.write_register(2U, 0x87654321U);
    state.write_register(3U, 0xDEADBEEFU);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.read_register(1U), 0x12345678U);
    EXPECT_EQ(state.read_register(2U), 0x87654321U);
    EXPECT_EQ(state.read_register(3U), 0xDEADBEEFU);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

TEST(RegisterInstructionStandaloneTest,
     Rv32mEncodingRemainsIllegalUntilExtensionIsImplemented) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 6U);
  state.write_register(2U, 7U);
  constexpr std::uint32_t multiply =
      encode_register_operation(3U, 0U, 1U, 2U, 0x01U);
  memory.write32(kBaseAddress, multiply);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  EXPECT_EQ(std::get<Trap>(result).cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(state.read_register(3U), 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

}  // namespace
}  // namespace rvemu

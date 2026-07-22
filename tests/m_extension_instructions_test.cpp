#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x7000U;
constexpr std::uint8_t kMFunction7 = 0x01U;

[[nodiscard]] constexpr std::uint32_t encode_m_operation(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source1, const std::uint8_t source2,
    const std::uint8_t function7 = kMFunction7) noexcept {
  return (static_cast<std::uint32_t>(function7) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Op);
}

struct MOperationCase {
  const char* name;
  std::uint8_t function3;
  std::uint32_t source1;
  std::uint32_t source2;
  std::uint32_t expected;
};

void expect_m_operation(const MOperationCase& test_case) {
  SCOPED_TRACE(test_case.name);
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, test_case.source1);
  state.write_register(2U, test_case.source2);
  memory.write32(kBaseAddress,
                 encode_m_operation(3U, test_case.function3, 1U, 2U));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(3U), test_case.expected);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(MExtensionInstructionTest, MulReturnsTheLowProductWord) {
  constexpr std::array cases{
      MOperationCase{"negative times positive", 0U, 0xFFFFFFFFU, 2U,
                     0xFFFFFFFEU},
      MOperationCase{"low word overflow", 0U, 0x80000000U, 2U, 0U},
      MOperationCase{"ordinary product", 0U, 0x12345678U, 0x10U,
                     0x23456780U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, MulhReturnsSignedHighProductWord) {
  constexpr std::array cases{
      MOperationCase{"negative product", 1U, 0xFFFFFFFEU, 3U, 0xFFFFFFFFU},
      MOperationCase{"minimum times two", 1U, 0x80000000U, 2U,
                     0xFFFFFFFFU},
      MOperationCase{"minimum squared", 1U, 0x80000000U, 0x80000000U,
                     0x40000000U},
      MOperationCase{"maximum squared", 1U, 0x7FFFFFFFU, 0x7FFFFFFFU,
                     0x3FFFFFFFU},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, MulhsuTreatsOnlyTheFirstOperandAsSigned) {
  constexpr std::array cases{
      MOperationCase{"negative one times unsigned maximum", 2U, 0xFFFFFFFFU,
                     0xFFFFFFFFU, 0xFFFFFFFFU},
      MOperationCase{"minimum times unsigned maximum", 2U, 0x80000000U,
                     0xFFFFFFFFU, 0x80000000U},
      MOperationCase{"maximum times unsigned maximum", 2U, 0x7FFFFFFFU,
                     0xFFFFFFFFU, 0x7FFFFFFEU},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, MulhuReturnsUnsignedHighProductWord) {
  constexpr std::array cases{
      MOperationCase{"unsigned maximum squared", 3U, 0xFFFFFFFFU,
                     0xFFFFFFFFU, 0xFFFFFFFEU},
      MOperationCase{"high carry", 3U, 0x80000000U, 2U, 1U},
      MOperationCase{"zero product", 3U, 0U, 0xFFFFFFFFU, 0U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, DivRoundsSignedQuotientsTowardZero) {
  constexpr std::array cases{
      MOperationCase{"positive by positive", 4U, 7U, 3U, 2U},
      MOperationCase{"negative by positive", 4U, 0xFFFFFFF9U, 3U,
                     0xFFFFFFFEU},
      MOperationCase{"positive by negative", 4U, 7U, 0xFFFFFFFDU,
                     0xFFFFFFFEU},
      MOperationCase{"negative by negative", 4U, 0xFFFFFFF9U, 0xFFFFFFFDU,
                     2U},
      MOperationCase{"smaller magnitude", 4U, 1U, 0x80000000U, 0U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, DivuUsesUnsignedOperands) {
  constexpr std::array cases{
      MOperationCase{"unsigned maximum by two", 5U, 0xFFFFFFFFU, 2U,
                     0x7FFFFFFFU},
      MOperationCase{"smaller dividend", 5U, 1U, 2U, 0U},
      MOperationCase{"zero dividend", 5U, 0U, 7U, 0U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, RemKeepsTheSignedDividendSign) {
  constexpr std::array cases{
      MOperationCase{"positive by positive", 6U, 7U, 3U, 1U},
      MOperationCase{"negative by positive", 6U, 0xFFFFFFF9U, 3U,
                     0xFFFFFFFFU},
      MOperationCase{"positive by negative", 6U, 7U, 0xFFFFFFFDU, 1U},
      MOperationCase{"negative by negative", 6U, 0xFFFFFFF9U, 0xFFFFFFFDU,
                     0xFFFFFFFFU},
      MOperationCase{"smaller magnitude", 6U, 1U, 0x80000000U, 1U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, RemuUsesUnsignedOperands) {
  constexpr std::array cases{
      MOperationCase{"unsigned maximum by two", 7U, 0xFFFFFFFFU, 2U, 1U},
      MOperationCase{"smaller dividend", 7U, 1U, 2U, 1U},
      MOperationCase{"zero dividend", 7U, 0U, 7U, 0U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, DivisionByZeroReturnsArchitecturalValues) {
  constexpr std::array cases{
      MOperationCase{"DIV", 4U, 0x12345678U, 0U, 0xFFFFFFFFU},
      MOperationCase{"DIVU", 5U, 0x12345678U, 0U, 0xFFFFFFFFU},
      MOperationCase{"REM", 6U, 0x92345678U, 0U, 0x92345678U},
      MOperationCase{"REMU", 7U, 0x92345678U, 0U, 0x92345678U},
  };

  for (const auto& test_case : cases) {
    expect_m_operation(test_case);
  }
}

TEST(MExtensionInstructionTest, SignedDivisionOverflowUsesDefinedResults) {
  expect_m_operation(MOperationCase{"DIV overflow", 4U, 0x80000000U,
                                    0xFFFFFFFFU, 0x80000000U});
  expect_m_operation(
      MOperationCase{"REM overflow", 6U, 0x80000000U, 0xFFFFFFFFU, 0U});
}

TEST(MExtensionInstructionTest, DestinationMayAliasEitherSource) {
  {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 6U);
    state.write_register(2U, 7U);
    memory.write32(kBaseAddress, encode_m_operation(1U, 0U, 1U, 2U));
    ExecutionEngine engine(state, memory);

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));
    EXPECT_EQ(state.read_register(1U), 42U);
    EXPECT_EQ(state.read_register(2U), 7U);
  }

  {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 49U);
    state.write_register(2U, 7U);
    memory.write32(kBaseAddress, encode_m_operation(2U, 4U, 1U, 2U));
    ExecutionEngine engine(state, memory);

    ASSERT_TRUE(std::holds_alternative<StepCompleted>(engine.step()));
    EXPECT_EQ(state.read_register(1U), 49U);
    EXPECT_EQ(state.read_register(2U), 7U);
  }
}

TEST(MExtensionInstructionTest, ResultWriteToX0IsDiscardedButRetires) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 7U);
  state.write_register(2U, 0U);
  memory.write32(kBaseAddress, encode_m_operation(0U, 4U, 1U, 2U));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(0U), 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(MExtensionInstructionTest, NonMFunction7EncodingsRemainIllegal) {
  for (std::uint8_t function3 = 0U; function3 < 8U; ++function3) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 6U);
    state.write_register(2U, 7U);
    state.write_register(3U, 0xDEADBEEFU);
    const std::uint32_t instruction =
        encode_m_operation(3U, function3, 1U, 2U, 0x02U);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.read_register(3U), 0xDEADBEEFU);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

}  // namespace
}  // namespace rvemu

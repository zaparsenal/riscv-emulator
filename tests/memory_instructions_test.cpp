#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x5000U;
constexpr std::uint32_t kDataAddress = kBaseAddress + 0x80U;

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

class MemoryInstructionTest : public ::testing::Test {
 protected:
  MemoryInstructionTest()
      : memory_(kBaseAddress, 256U), engine_(state_, memory_) {
    state_.set_program_counter(kBaseAddress);
    state_.write_register(1U, kDataAddress);
  }

  [[nodiscard]] StepResult execute(const std::uint32_t instruction) {
    memory_.write32(kBaseAddress, instruction);
    return engine_.step();
  }

  CpuState state_;
  Memory memory_;
  ExecutionEngine engine_;
};

TEST_F(MemoryInstructionTest, LbSignExtendsNegativeByte) {
  memory_.write8(kDataAddress, 0x80U);

  const StepResult result = execute(encode_load(2U, 0U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0xFFFFFF80U);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST_F(MemoryInstructionTest, LbPreservesPositiveByte) {
  memory_.write8(kDataAddress, 0x7FU);

  const StepResult result = execute(encode_load(2U, 0U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x7FU);
}

TEST_F(MemoryInstructionTest, LhSignExtendsNegativeHalfword) {
  memory_.write16(kDataAddress, 0x8001U);

  const StepResult result = execute(encode_load(2U, 1U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0xFFFF8001U);
}

TEST_F(MemoryInstructionTest, LwLoadsAllBits) {
  memory_.write32(kDataAddress, 0x89ABCDEFU);

  const StepResult result = execute(encode_load(2U, 2U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x89ABCDEFU);
}

TEST_F(MemoryInstructionTest, LbuZeroExtendsByte) {
  memory_.write8(kDataAddress, 0x80U);

  const StepResult result = execute(encode_load(2U, 4U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x80U);
}

TEST_F(MemoryInstructionTest, LhuZeroExtendsHalfword) {
  memory_.write16(kDataAddress, 0x8001U);

  const StepResult result = execute(encode_load(2U, 5U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x8001U);
}

TEST_F(MemoryInstructionTest, LoadsUseSignExtendedNegativeOffset) {
  state_.write_register(1U, kDataAddress + 4U);
  memory_.write32(kDataAddress, 0x12345678U);

  const StepResult result = execute(encode_load(2U, 2U, 1U, -4));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(2U), 0x12345678U);
}

TEST_F(MemoryInstructionTest, LoadDestinationMayAliasBaseRegister) {
  memory_.write32(kDataAddress, 0x12345678U);

  const StepResult result = execute(encode_load(1U, 2U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state_.read_register(1U), 0x12345678U);
}

TEST(MemoryInstructionStandaloneTest, LoadEffectiveAddressWrapsModuloThirtyTwoBits) {
  CpuState state;
  Memory memory(0U, 8U);
  state.set_program_counter(4U);
  state.write_register(1U, 0xFFFFFFFCU);
  memory.write32(0U, 0xA5A5F00DU);
  memory.write32(4U, encode_load(2U, 2U, 1U, 4));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(2U), 0xA5A5F00DU);
  EXPECT_EQ(state.program_counter(), 8U);
}

TEST(MemoryInstructionStandaloneTest,
     MisalignedLoadsTrapWithoutDestinationMutation) {
  struct MisalignedLoadCase {
    std::uint8_t function3;
    std::uint32_t address;
  };
  constexpr std::array cases{
      MisalignedLoadCase{1U, kDataAddress + 1U},
      MisalignedLoadCase{2U, kDataAddress + 2U},
      MisalignedLoadCase{5U, kDataAddress + 1U},
  };

  for (const auto& test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 256U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, test_case.address);
    state.write_register(2U, 0xDEADBEEFU);
    memory.write32(kBaseAddress,
                   encode_load(2U, test_case.function3, 1U, 0));
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::LoadAddressMisaligned);
    EXPECT_EQ(trap.value, test_case.address);
    EXPECT_EQ(state.read_register(2U), 0xDEADBEEFU);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

TEST(MemoryInstructionStandaloneTest,
     LoadCrossingMappedEndRaisesAccessFaultWithoutDestinationMutation) {
  CpuState state;
  Memory memory(kBaseAddress, 10U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, kBaseAddress + 8U);
  state.write_register(2U, 0xDEADBEEFU);
  memory.write32(kBaseAddress, encode_load(2U, 2U, 1U, 0));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::LoadAccessFault);
  EXPECT_EQ(trap.value, kBaseAddress + 8U);
  EXPECT_EQ(state.read_register(2U), 0xDEADBEEFU);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(MemoryInstructionStandaloneTest, FaultingLoadToX0StillRaisesTrap) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, kBaseAddress + 4U);
  memory.write32(kBaseAddress, encode_load(0U, 0U, 1U, 0));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  EXPECT_EQ(std::get<Trap>(result).cause, TrapCause::LoadAccessFault);
  EXPECT_EQ(state.read_register(0U), 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(MemoryInstructionStandaloneTest,
     ReservedLoadFunctionFieldsTrapWithoutMemoryAccess) {
  constexpr std::array<std::uint8_t, 3U> invalid_function3{3U, 6U, 7U};

  for (const std::uint8_t function3 : invalid_function3) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 0xFFFFFFFFU);
    state.write_register(2U, 0xDEADBEEFU);
    const std::uint32_t instruction = encode_load(2U, function3, 1U, 0);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.read_register(2U), 0xDEADBEEFU);
  }
}

TEST_F(MemoryInstructionTest, SbWritesOnlyLowByte) {
  state_.write_register(2U, 0xAABBCCDDU);
  memory_.write8(kDataAddress, 0U);
  memory_.write8(kDataAddress + 1U, 0x5AU);

  const StepResult result = execute(encode_store(0U, 1U, 2U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read8(kDataAddress), 0xDDU);
  EXPECT_EQ(memory_.read8(kDataAddress + 1U), 0x5AU);
  EXPECT_EQ(state_.program_counter(), kBaseAddress + 4U);
}

TEST_F(MemoryInstructionTest, SbAllowsUnalignedByteAddress) {
  state_.write_register(2U, 0xABU);

  const StepResult result = execute(encode_store(0U, 1U, 2U, 1));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read8(kDataAddress + 1U), 0xABU);
}

TEST_F(MemoryInstructionTest, ShWritesLowHalfwordInLittleEndianOrder) {
  state_.write_register(2U, 0xAABBCCDDU);

  const StepResult result = execute(encode_store(1U, 1U, 2U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read8(kDataAddress), 0xDDU);
  EXPECT_EQ(memory_.read8(kDataAddress + 1U), 0xCCU);
  EXPECT_EQ(memory_.read16(kDataAddress), 0xCCDDU);
}

TEST_F(MemoryInstructionTest, SwWritesAllBits) {
  state_.write_register(2U, 0x89ABCDEFU);

  const StepResult result = execute(encode_store(2U, 1U, 2U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read32(kDataAddress), 0x89ABCDEFU);
}

TEST_F(MemoryInstructionTest, StoresUseSignExtendedNegativeOffset) {
  state_.write_register(1U, kDataAddress + 4U);
  state_.write_register(2U, 0x12345678U);

  const StepResult result = execute(encode_store(2U, 1U, 2U, -4));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read32(kDataAddress), 0x12345678U);
}

TEST(MemoryInstructionStandaloneTest, StoreEffectiveAddressWrapsModuloThirtyTwoBits) {
  CpuState state;
  Memory memory(0U, 8U);
  state.set_program_counter(4U);
  state.write_register(1U, 0xFFFFFFFCU);
  state.write_register(2U, 0x11223344U);
  memory.write32(4U, encode_store(2U, 1U, 2U, 4));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory.read32(0U), 0x11223344U);
  EXPECT_EQ(state.program_counter(), 8U);
}

TEST_F(MemoryInstructionTest, StoreBaseAndDataMayAlias) {
  state_.write_register(1U, kDataAddress);

  const StepResult result = execute(encode_store(2U, 1U, 1U, 0));

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(memory_.read32(kDataAddress), kDataAddress);
  EXPECT_EQ(state_.read_register(1U), kDataAddress);
}

TEST(MemoryInstructionStandaloneTest,
     MisalignedStoresTrapWithoutMemoryMutation) {
  struct MisalignedStoreCase {
    std::uint8_t function3;
    std::uint32_t address;
  };
  constexpr std::array cases{
      MisalignedStoreCase{1U, kDataAddress + 1U},
      MisalignedStoreCase{2U, kDataAddress + 2U},
  };

  for (const auto& test_case : cases) {
    CpuState state;
    Memory memory(kBaseAddress, 256U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, test_case.address);
    state.write_register(2U, 0xFFFFFFFFU);
    memory.write32(kBaseAddress,
                   encode_store(test_case.function3, 1U, 2U, 0));
    memory.write32(kDataAddress, 0x11223344U);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::StoreAddressMisaligned);
    EXPECT_EQ(trap.value, test_case.address);
    EXPECT_EQ(memory.read32(kDataAddress), 0x11223344U);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

TEST(MemoryInstructionStandaloneTest,
     StoreCrossingMappedEndRaisesAccessFaultWithoutPartialWrite) {
  CpuState state;
  Memory memory(kBaseAddress, 10U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, kBaseAddress + 8U);
  state.write_register(2U, 0xFFFFFFFFU);
  memory.write32(kBaseAddress, encode_store(2U, 1U, 2U, 0));
  memory.write8(kBaseAddress + 8U, 0xAAU);
  memory.write8(kBaseAddress + 9U, 0xBBU);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::StoreAccessFault);
  EXPECT_EQ(trap.value, kBaseAddress + 8U);
  EXPECT_EQ(memory.read8(kBaseAddress + 8U), 0xAAU);
  EXPECT_EQ(memory.read8(kBaseAddress + 9U), 0xBBU);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(MemoryInstructionStandaloneTest,
     ReservedStoreFunctionFieldsTrapWithoutMemoryMutation) {
  constexpr std::array<std::uint8_t, 5U> invalid_function3{3U, 4U, 5U, 6U, 7U};

  for (const std::uint8_t function3 : invalid_function3) {
    CpuState state;
    Memory memory(kBaseAddress, 256U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, kDataAddress);
    state.write_register(2U, 0xFFFFFFFFU);
    const std::uint32_t instruction = encode_store(function3, 1U, 2U, 0);
    memory.write32(kBaseAddress, instruction);
    memory.write32(kDataAddress, 0x11223344U);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(memory.read32(kDataAddress), 0x11223344U);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

}  // namespace
}  // namespace rvemu

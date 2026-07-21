#include "rvemu/execution_engine.hpp"

#include <cstdint>
#include <limits>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x1000U;

[[nodiscard]] constexpr std::uint32_t encode_addi(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

TEST(ExecutionEngineTest, FetchesDecodesAndExecutesAddImmediate) {
  CpuState state;
  Memory memory(kBaseAddress, 16U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 42));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  const auto& completed = std::get<StepCompleted>(result);
  EXPECT_EQ(completed.program_counter, kBaseAddress);
  EXPECT_EQ(completed.next_program_counter, kBaseAddress + 4U);
  EXPECT_EQ(completed.instruction.opcode, Opcode::OpImmediate);
  EXPECT_EQ(state.read_register(1U), 42U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(ExecutionEngineTest, AppliesSignExtendedImmediateWithUnsignedWraparound) {
  CpuState state;
  Memory memory(kBaseAddress, 16U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 1U);
  memory.write32(kBaseAddress, encode_addi(2U, 1U, -2));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(2U), 0xFFFFFFFFU);
}

TEST(ExecutionEngineTest, WrapsAddImmediateOverflowModuloThirtyTwoBits) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, std::numeric_limits<std::uint32_t>::max());
  memory.write32(kBaseAddress, encode_addi(2U, 1U, 1));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(2U), 0U);
}

TEST(ExecutionEngineTest, CannotModifyZeroRegister) {
  CpuState state;
  Memory memory(kBaseAddress, 16U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(0U, 0U, -1));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(0U), 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(ExecutionEngineTest, RunExecutesUntilItsInstructionLimit) {
  CpuState state;
  Memory memory(kBaseAddress, 16U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 3));
  memory.write32(kBaseAddress + 4U, encode_addi(1U, 1U, -1));
  memory.write32(kBaseAddress + 8U, encode_addi(2U, 1U, 5));
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(3U);

  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(result));
  EXPECT_EQ(std::get<InstructionLimitReached>(result).instructions_executed, 3U);
  EXPECT_EQ(state.read_register(1U), 2U);
  EXPECT_EQ(state.read_register(2U), 7U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 12U);
}

TEST(ExecutionEngineTest, ZeroInstructionLimitDoesNotFetch) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, 0U);
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(0U);

  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(result));
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(ExecutionEngineTest, WrapsProgramCounterAtTopOfAddressSpace) {
  CpuState state;
  Memory memory(0xFFFFFFFCU, 4U);
  state.set_program_counter(0xFFFFFFFCU);
  memory.write32(0xFFFFFFFCU, encode_addi(1U, 0U, 1));
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(std::get<StepCompleted>(result).next_program_counter, 0U);
  EXPECT_EQ(state.program_counter(), 0U);
  EXPECT_EQ(state.read_register(1U), 1U);
}

TEST(ExecutionEngineTest, TrapsOnMisalignedInstructionAddress) {
  CpuState state;
  Memory memory(kBaseAddress, 16U);
  state.set_program_counter(kBaseAddress + 2U);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::InstructionAddressMisaligned);
  EXPECT_EQ(trap.program_counter, kBaseAddress + 2U);
  EXPECT_EQ(trap.value, kBaseAddress + 2U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 2U);
}

TEST(ExecutionEngineTest, TranslatesMemoryFailureToInstructionAccessFault) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress + 4U);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::InstructionAccessFault);
  EXPECT_EQ(trap.program_counter, kBaseAddress + 4U);
  EXPECT_EQ(trap.value, kBaseAddress + 4U);
}

TEST(ExecutionEngineTest, TrapsOnUnknownInstructionEncoding) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, 0U);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(trap.program_counter, kBaseAddress);
  EXPECT_EQ(trap.value, 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(ExecutionEngineTest, TrapsOnDecodedButNotYetImplementedInstruction) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  constexpr std::uint32_t lui =
      0x12345000U | (1U << 7U) | static_cast<std::uint8_t>(Opcode::Lui);
  memory.write32(kBaseAddress, lui);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  EXPECT_EQ(std::get<Trap>(result).cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(state.read_register(1U), 0U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(ExecutionEngineTest, RunStopsAtTrapAndReportsRetiredInstructionCount) {
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 7));
  memory.write32(kBaseAddress + 4U, 0U);
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(10U);

  ASSERT_TRUE(std::holds_alternative<RunTrapped>(result));
  const auto& trapped = std::get<RunTrapped>(result);
  EXPECT_EQ(trapped.instructions_executed, 1U);
  EXPECT_EQ(trapped.trap.cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(trapped.trap.program_counter, kBaseAddress + 4U);
  EXPECT_EQ(state.read_register(1U), 7U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

}  // namespace
}  // namespace rvemu

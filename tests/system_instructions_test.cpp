#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr std::uint32_t kBaseAddress = 0x6000U;
constexpr std::uint32_t kCanonicalFence = 0x0FF0000FU;
constexpr std::uint32_t kFenceTso = 0x8330000FU;
constexpr std::uint32_t kEcall = 0x00000073U;
constexpr std::uint32_t kEbreak = 0x00100073U;

[[nodiscard]] constexpr std::uint32_t encode_addi(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

TEST(SystemInstructionTest, CanonicalFenceRetiresWithoutArchitecturalMutation) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 0x12345678U);
  memory.write32(kBaseAddress, kCanonicalFence);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  const auto& completed = std::get<StepCompleted>(result);
  EXPECT_EQ(completed.instruction.raw, kCanonicalFence);
  EXPECT_EQ(completed.next_program_counter, kBaseAddress + 4U);
  EXPECT_EQ(state.read_register(1U), 0x12345678U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(SystemInstructionTest, FenceTsoIsHandledAsAConservativeFence) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, kFenceTso);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(SystemInstructionTest, ReservedFenceFieldsAreIgnoredForCompatibility) {
  constexpr std::uint32_t reserved_fence =
      (0x5A5U << 20U) | (7U << 15U) | (9U << 7U) |
      static_cast<std::uint8_t>(Opcode::MiscMemory);
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(7U, 0xAAAAAAAAU);
  state.write_register(9U, 0xBBBBBBBBU);
  memory.write32(kBaseAddress, reserved_fence);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.read_register(7U), 0xAAAAAAAAU);
  EXPECT_EQ(state.read_register(9U), 0xBBBBBBBBU);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(SystemInstructionTest, FenceParticipatesInBoundedExecution) {
  CpuState state;
  Memory memory(kBaseAddress, 12U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 1));
  memory.write32(kBaseAddress + 4U, kCanonicalFence);
  memory.write32(kBaseAddress + 8U, encode_addi(1U, 1U, 1));
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(3U);

  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(result));
  EXPECT_EQ(std::get<InstructionLimitReached>(result).instructions_executed, 3U);
  EXPECT_EQ(state.read_register(1U), 2U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 12U);
}

TEST(SystemInstructionTest, FenceIRequiresTheSeparateZifenceiExtension) {
  constexpr std::uint32_t fence_i =
      (1U << 12U) | static_cast<std::uint8_t>(Opcode::MiscMemory);
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, fence_i);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(trap.value, fence_i);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(SystemInstructionTest, EcallRaisesPreciseUserEnvironmentCallTrap) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(10U, 0x12345678U);
  memory.write32(kBaseAddress, kEcall);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::EnvironmentCallFromUserMode);
  EXPECT_EQ(trap.program_counter, kBaseAddress);
  EXPECT_EQ(trap.value, 0U);
  EXPECT_EQ(state.read_register(10U), 0x12345678U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(SystemInstructionTest, EbreakRaisesPreciseBreakpointTrap) {
  CpuState state;
  Memory memory(kBaseAddress, 4U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 0x12345678U);
  memory.write32(kBaseAddress, kEbreak);
  ExecutionEngine engine(state, memory);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<Trap>(result));
  const auto& trap = std::get<Trap>(result);
  EXPECT_EQ(trap.cause, TrapCause::Breakpoint);
  EXPECT_EQ(trap.program_counter, kBaseAddress);
  EXPECT_EQ(trap.value, 0U);
  EXPECT_EQ(state.read_register(1U), 0x12345678U);
  EXPECT_EQ(state.program_counter(), kBaseAddress);
}

TEST(SystemInstructionTest, RequestedTrapIsNotCountedAsRetired) {
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, kCanonicalFence);
  memory.write32(kBaseAddress + 4U, kEcall);
  ExecutionEngine engine(state, memory);

  const RunResult result = engine.run(10U);

  ASSERT_TRUE(std::holds_alternative<RunTrapped>(result));
  const auto& trapped = std::get<RunTrapped>(result);
  EXPECT_EQ(trapped.instructions_executed, 1U);
  EXPECT_EQ(trapped.trap.cause, TrapCause::EnvironmentCallFromUserMode);
  EXPECT_EQ(trapped.trap.program_counter, kBaseAddress + 4U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
}

TEST(SystemInstructionTest, NonBaseSystemEncodingsAreIllegalInstructions) {
  constexpr std::array invalid_instructions{
      kEcall | (1U << 7U),   // ECALL with nonzero rd
      kEcall | (1U << 15U),  // ECALL with nonzero rs1
      kEbreak | (1U << 7U),  // EBREAK with nonzero rd
      0x00200073U,           // unsupported privileged SYSTEM encoding
      0xC0002073U,           // CSRRS requires Zicsr
  };

  for (const std::uint32_t instruction : invalid_instructions) {
    CpuState state;
    Memory memory(kBaseAddress, 4U);
    state.set_program_counter(kBaseAddress);
    state.write_register(1U, 0x12345678U);
    memory.write32(kBaseAddress, instruction);
    ExecutionEngine engine(state, memory);

    const StepResult result = engine.step();

    ASSERT_TRUE(std::holds_alternative<Trap>(result));
    const auto& trap = std::get<Trap>(result);
    EXPECT_EQ(trap.cause, TrapCause::IllegalInstruction);
    EXPECT_EQ(trap.value, instruction);
    EXPECT_EQ(state.read_register(1U), 0x12345678U);
    EXPECT_EQ(state.program_counter(), kBaseAddress);
  }
}

}  // namespace
}  // namespace rvemu

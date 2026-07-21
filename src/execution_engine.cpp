#include "rvemu/execution_engine.hpp"

#include <cstdint>
#include <variant>

namespace rvemu {

ExecutionEngine::ExecutionEngine(CpuState& state, Memory& memory) noexcept
    : state_(state), memory_(memory) {}

StepResult ExecutionEngine::step() {
  const std::uint32_t program_counter = state_.program_counter();
  if ((program_counter & 0x3U) != 0U) {
    return instruction_trap(TrapCause::InstructionAddressMisaligned,
                            program_counter);
  }

  std::uint32_t raw = 0U;
  try {
    raw = memory_.read32(program_counter);
  } catch (const MemoryAccessError&) {
    return instruction_trap(TrapCause::InstructionAccessFault,
                            program_counter);
  }

  const auto instruction = decode_instruction(raw);
  if (!instruction.has_value()) {
    return instruction_trap(TrapCause::IllegalInstruction, raw);
  }

  if (instruction->opcode != Opcode::OpImmediate ||
      instruction->function3 != 0U) {
    return instruction_trap(TrapCause::IllegalInstruction, raw);
  }

  const std::uint32_t value =
      state_.read_register(instruction->source1) + instruction->immediate;
  state_.write_register(instruction->destination, value);

  const std::uint32_t next_program_counter = program_counter + 4U;
  state_.set_program_counter(next_program_counter);
  return StepCompleted{*instruction, program_counter, next_program_counter};
}

RunResult ExecutionEngine::run(const std::uint64_t instruction_limit) {
  for (std::uint64_t executed = 0U; executed < instruction_limit; ++executed) {
    StepResult result = step();
    if (const auto* trap = std::get_if<Trap>(&result); trap != nullptr) {
      return RunTrapped{executed, *trap};
    }
  }
  return InstructionLimitReached{instruction_limit};
}

Trap ExecutionEngine::instruction_trap(const TrapCause cause,
                                       const std::uint32_t value) const
    noexcept {
  return Trap{cause, state_.program_counter(), value};
}

}  // namespace rvemu

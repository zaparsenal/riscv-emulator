#include "rvemu/execution_engine.hpp"

#include <cstdint>
#include <optional>
#include <variant>

namespace rvemu {
namespace {

constexpr std::uint32_t kEcallInstruction = 0x00000073U;
constexpr std::uint32_t kEbreakInstruction = 0x00100073U;

[[nodiscard]] constexpr bool signed_less_than(const std::uint32_t left,
                                               const std::uint32_t right)
    noexcept {
  constexpr std::uint32_t kSignBit = 0x80000000U;
  return (left ^ kSignBit) < (right ^ kSignBit);
}

[[nodiscard]] constexpr std::uint32_t arithmetic_shift_right(
    const std::uint32_t value, const std::uint32_t shift_amount) noexcept {
  if (shift_amount == 0U) {
    return value;
  }

  std::uint32_t result = value >> shift_amount;
  if ((value & 0x80000000U) != 0U) {
    result |= 0xFFFFFFFFU << (32U - shift_amount);
  }
  return result;
}

[[nodiscard]] std::optional<std::uint32_t> execute_op_immediate(
    const DecodedInstruction& instruction, const std::uint32_t source) noexcept {
  const std::uint32_t shift_amount = instruction.immediate & 0x1FU;

  switch (instruction.function3) {
    case 0x0U:  // ADDI
      return source + instruction.immediate;
    case 0x1U:  // SLLI
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source << shift_amount;
    case 0x2U:  // SLTI
      return signed_less_than(source, instruction.immediate) ? 1U : 0U;
    case 0x3U:  // SLTIU
      return source < instruction.immediate ? 1U : 0U;
    case 0x4U:  // XORI
      return source ^ instruction.immediate;
    case 0x5U:  // SRLI/SRAI
      if (instruction.function7 == 0x00U) {
        return source >> shift_amount;
      }
      if (instruction.function7 == 0x20U) {
        return arithmetic_shift_right(source, shift_amount);
      }
      return std::nullopt;
    case 0x6U:  // ORI
      return source | instruction.immediate;
    case 0x7U:  // ANDI
      return source & instruction.immediate;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::uint32_t> execute_op(
    const DecodedInstruction& instruction, const std::uint32_t source1,
    const std::uint32_t source2) noexcept {
  const std::uint32_t shift_amount = source2 & 0x1FU;

  switch (instruction.function3) {
    case 0x0U:  // ADD/SUB
      if (instruction.function7 == 0x00U) {
        return source1 + source2;
      }
      if (instruction.function7 == 0x20U) {
        return source1 - source2;
      }
      return std::nullopt;
    case 0x1U:  // SLL
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source1 << shift_amount;
    case 0x2U:  // SLT
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return signed_less_than(source1, source2) ? 1U : 0U;
    case 0x3U:  // SLTU
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source1 < source2 ? 1U : 0U;
    case 0x4U:  // XOR
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source1 ^ source2;
    case 0x5U:  // SRL/SRA
      if (instruction.function7 == 0x00U) {
        return source1 >> shift_amount;
      }
      if (instruction.function7 == 0x20U) {
        return arithmetic_shift_right(source1, shift_amount);
      }
      return std::nullopt;
    case 0x6U:  // OR
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source1 | source2;
    case 0x7U:  // AND
      if (instruction.function7 != 0x00U) {
        return std::nullopt;
      }
      return source1 & source2;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<bool> evaluate_branch(
    const DecodedInstruction& instruction, const std::uint32_t source1,
    const std::uint32_t source2) noexcept {
  switch (instruction.function3) {
    case 0x0U:  // BEQ
      return source1 == source2;
    case 0x1U:  // BNE
      return source1 != source2;
    case 0x4U:  // BLT
      return signed_less_than(source1, source2);
    case 0x5U:  // BGE
      return !signed_less_than(source1, source2);
    case 0x6U:  // BLTU
      return source1 < source2;
    case 0x7U:  // BGEU
      return source1 >= source2;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] constexpr bool is_instruction_aligned(
    const std::uint32_t address) noexcept {
  return (address & 0x3U) == 0U;
}

[[nodiscard]] constexpr std::uint32_t sign_extend_byte(
    const std::uint8_t value) noexcept {
  const std::uint32_t extended = value;
  return (extended & 0x80U) != 0U ? extended | 0xFFFFFF00U : extended;
}

[[nodiscard]] constexpr std::uint32_t sign_extend_halfword(
    const std::uint16_t value) noexcept {
  const std::uint32_t extended = value;
  return (extended & 0x8000U) != 0U ? extended | 0xFFFF0000U : extended;
}

struct ExecutionFailure {
  TrapCause cause;
  std::uint32_t value;
};

using LoadResult = std::variant<std::uint32_t, ExecutionFailure>;

[[nodiscard]] LoadResult execute_load(const DecodedInstruction& instruction,
                                      Memory& memory,
                                      const std::uint32_t address) {
  try {
    switch (instruction.function3) {
      case 0x0U:  // LB
        return sign_extend_byte(memory.read8(address));
      case 0x1U:  // LH
        return sign_extend_halfword(memory.read16(address));
      case 0x2U:  // LW
        return memory.read32(address);
      case 0x4U:  // LBU
        return static_cast<std::uint32_t>(memory.read8(address));
      case 0x5U:  // LHU
        return static_cast<std::uint32_t>(memory.read16(address));
      default:
        return ExecutionFailure{TrapCause::IllegalInstruction,
                                instruction.raw};
    }
  } catch (const MemoryMisalignmentError&) {
    return ExecutionFailure{TrapCause::LoadAddressMisaligned, address};
  } catch (const MemoryOutOfBoundsError&) {
    return ExecutionFailure{TrapCause::LoadAccessFault, address};
  }
}

[[nodiscard]] std::optional<ExecutionFailure> execute_store(
    const DecodedInstruction& instruction, Memory& memory,
    const std::uint32_t address, const std::uint32_t value) {
  try {
    switch (instruction.function3) {
      case 0x0U:  // SB
        memory.write8(address, static_cast<std::uint8_t>(value & 0xFFU));
        break;
      case 0x1U:  // SH
        memory.write16(address, static_cast<std::uint16_t>(value & 0xFFFFU));
        break;
      case 0x2U:  // SW
        memory.write32(address, value);
        break;
      default:
        return ExecutionFailure{TrapCause::IllegalInstruction,
                                instruction.raw};
    }
  } catch (const MemoryMisalignmentError&) {
    return ExecutionFailure{TrapCause::StoreAddressMisaligned, address};
  } catch (const MemoryOutOfBoundsError&) {
    return ExecutionFailure{TrapCause::StoreAccessFault, address};
  }

  return std::nullopt;
}

}  // namespace

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

  const std::uint32_t sequential_program_counter = program_counter + 4U;
  std::uint32_t next_program_counter = sequential_program_counter;
  std::optional<std::uint32_t> destination_value;
  bool instruction_supported = false;

  switch (instruction->opcode) {
    case Opcode::Lui:
      destination_value = instruction->immediate;
      instruction_supported = true;
      break;
    case Opcode::Auipc:
      destination_value = program_counter + instruction->immediate;
      instruction_supported = true;
      break;
    case Opcode::OpImmediate:
      destination_value = execute_op_immediate(
          *instruction, state_.read_register(instruction->source1));
      instruction_supported = destination_value.has_value();
      break;
    case Opcode::Op:
      destination_value = execute_op(
          *instruction, state_.read_register(instruction->source1),
          state_.read_register(instruction->source2));
      instruction_supported = destination_value.has_value();
      break;
    case Opcode::Jal:
      next_program_counter = program_counter + instruction->immediate;
      if (!is_instruction_aligned(next_program_counter)) {
        return instruction_trap(TrapCause::InstructionAddressMisaligned,
                                next_program_counter);
      }
      destination_value = sequential_program_counter;
      instruction_supported = true;
      break;
    case Opcode::Jalr:
      if (instruction->function3 != 0U) {
        break;
      }
      next_program_counter =
          (state_.read_register(instruction->source1) +
           instruction->immediate) &
          ~std::uint32_t{1U};
      if (!is_instruction_aligned(next_program_counter)) {
        return instruction_trap(TrapCause::InstructionAddressMisaligned,
                                next_program_counter);
      }
      destination_value = sequential_program_counter;
      instruction_supported = true;
      break;
    case Opcode::Branch: {
      const auto branch_taken = evaluate_branch(
          *instruction, state_.read_register(instruction->source1),
          state_.read_register(instruction->source2));
      if (!branch_taken.has_value()) {
        break;
      }
      if (*branch_taken) {
        next_program_counter = program_counter + instruction->immediate;
        if (!is_instruction_aligned(next_program_counter)) {
          return instruction_trap(TrapCause::InstructionAddressMisaligned,
                                  next_program_counter);
        }
      }
      instruction_supported = true;
      break;
    }
    case Opcode::Load: {
      const std::uint32_t address =
          state_.read_register(instruction->source1) + instruction->immediate;
      const LoadResult load_result = execute_load(*instruction, memory_, address);
      if (const auto* failure = std::get_if<ExecutionFailure>(&load_result);
          failure != nullptr) {
        return instruction_trap(failure->cause, failure->value);
      }
      destination_value = std::get<std::uint32_t>(load_result);
      instruction_supported = true;
      break;
    }
    case Opcode::Store: {
      const std::uint32_t address =
          state_.read_register(instruction->source1) + instruction->immediate;
      const auto failure = execute_store(
          *instruction, memory_, address,
          state_.read_register(instruction->source2));
      if (failure.has_value()) {
        return instruction_trap(failure->cause, failure->value);
      }
      instruction_supported = true;
      break;
    }
    case Opcode::MiscMemory:
      if (instruction->function3 == 0U) {  // FENCE
        instruction_supported = true;
      }
      break;
    case Opcode::System:
      if (raw == kEcallInstruction) {
        return instruction_trap(TrapCause::EnvironmentCallFromUserMode, 0U);
      }
      if (raw == kEbreakInstruction) {
        return instruction_trap(TrapCause::Breakpoint, 0U);
      }
      break;
    default:
      break;
  }

  if (!instruction_supported) {
    return instruction_trap(TrapCause::IllegalInstruction, raw);
  }

  if (destination_value.has_value()) {
    state_.write_register(instruction->destination, *destination_value);
  }

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

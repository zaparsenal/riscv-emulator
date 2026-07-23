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

[[nodiscard]] constexpr bool is_negative_word(
    const std::uint32_t value) noexcept {
  return (value & 0x80000000U) != 0U;
}

[[nodiscard]] constexpr std::uint32_t signed_magnitude(
    const std::uint32_t value) noexcept {
  return is_negative_word(value) ? 0U - value : value;
}

[[nodiscard]] constexpr std::uint32_t apply_sign(
    const std::uint32_t magnitude, const bool negative) noexcept {
  return negative ? 0U - magnitude : magnitude;
}

[[nodiscard]] constexpr std::uint64_t signed_product_bits(
    const std::uint32_t source1, const std::uint32_t source2,
    const bool source2_is_signed) noexcept {
  const bool source1_negative = is_negative_word(source1);
  const bool source2_negative =
      source2_is_signed && is_negative_word(source2);
  const std::uint64_t source2_magnitude =
      source2_is_signed ? signed_magnitude(source2) : source2;
  const std::uint64_t magnitude =
      static_cast<std::uint64_t>(signed_magnitude(source1)) *
      source2_magnitude;
  return source1_negative != source2_negative
             ? std::uint64_t{0U} - magnitude
             : magnitude;
}

struct SignedDivisionResult {
  std::uint32_t quotient;
  std::uint32_t remainder;
};

[[nodiscard]] constexpr SignedDivisionResult signed_divide(
    const std::uint32_t dividend, const std::uint32_t divisor) noexcept {
  if (divisor == 0U) {
    return SignedDivisionResult{0xFFFFFFFFU, dividend};
  }

  const bool dividend_negative = is_negative_word(dividend);
  const bool divisor_negative = is_negative_word(divisor);
  const std::uint32_t dividend_magnitude = signed_magnitude(dividend);
  const std::uint32_t divisor_magnitude = signed_magnitude(divisor);
  const std::uint32_t quotient_magnitude =
      dividend_magnitude / divisor_magnitude;
  const std::uint32_t remainder_magnitude =
      dividend_magnitude % divisor_magnitude;
  return SignedDivisionResult{
      apply_sign(quotient_magnitude,
                 dividend_negative != divisor_negative),
      apply_sign(remainder_magnitude, dividend_negative)};
}

[[nodiscard]] constexpr std::optional<std::uint32_t> execute_m_extension(
    const DecodedInstruction& instruction, const std::uint32_t source1,
    const std::uint32_t source2) noexcept {
  if (instruction.function7 != 0x01U) {
    return std::nullopt;
  }

  const std::uint64_t unsigned_product =
      static_cast<std::uint64_t>(source1) * source2;
  switch (instruction.function3) {
    case 0x0U:  // MUL
      return static_cast<std::uint32_t>(unsigned_product);
    case 0x1U:  // MULH
      return static_cast<std::uint32_t>(
          signed_product_bits(source1, source2, true) >> 32U);
    case 0x2U:  // MULHSU
      return static_cast<std::uint32_t>(
          signed_product_bits(source1, source2, false) >> 32U);
    case 0x3U:  // MULHU
      return static_cast<std::uint32_t>(unsigned_product >> 32U);
    case 0x4U:  // DIV
      return signed_divide(source1, source2).quotient;
    case 0x5U:  // DIVU
      return source2 == 0U ? 0xFFFFFFFFU : source1 / source2;
    case 0x6U:  // REM
      return signed_divide(source1, source2).remainder;
    case 0x7U:  // REMU
      return source2 == 0U ? source1 : source1 % source2;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::uint32_t> execute_op(
    const DecodedInstruction& instruction, const std::uint32_t source1,
    const std::uint32_t source2) noexcept {
  if (instruction.function7 == 0x01U) {
    return execute_m_extension(instruction, source1, source2);
  }

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

[[nodiscard]] constexpr std::uint8_t data_access_width(
    const DecodedInstruction& instruction) noexcept {
  switch (instruction.function3) {
    case 0x0U:
    case 0x4U:
      return 1U;
    case 0x1U:
    case 0x5U:
      return 2U;
    case 0x2U:
      return 4U;
    default:
      return 0U;
  }
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

ExecutionEngine::ExecutionEngine(CpuState& state, Memory& memory,
                                 ExecutionObserver* observer) noexcept
    : state_(state), memory_(memory), observer_(observer) {}

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
  std::optional<DataMemoryAccessObservation> data_memory_access;
  std::optional<ControlFlowObservation> control_flow;
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
      control_flow =
          ControlFlowObservation{ControlFlowKind::DirectJump, true};
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
      control_flow =
          ControlFlowObservation{ControlFlowKind::IndirectJump, true};
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
      control_flow = ControlFlowObservation{
          ControlFlowKind::ConditionalBranch, *branch_taken};
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
      data_memory_access = DataMemoryAccessObservation{
          DataMemoryAccessKind::Load, address,
          data_access_width(*instruction)};
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
      data_memory_access = DataMemoryAccessObservation{
          DataMemoryAccessKind::Store, address,
          data_access_width(*instruction)};
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

  const ExecutionObservation observation{
      *instruction, program_counter, next_program_counter, data_memory_access,
      control_flow};
  state_.set_program_counter(next_program_counter);
  if (observer_ != nullptr) {
    observer_->observe(observation);
  }
  return observation;
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

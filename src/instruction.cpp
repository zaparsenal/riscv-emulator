#include "rvemu/instruction.hpp"

#include <cstdint>
#include <optional>

namespace rvemu {
namespace {

[[nodiscard]] constexpr std::uint8_t field(const std::uint32_t raw,
                                            const unsigned shift,
                                            const std::uint32_t mask) noexcept {
  return static_cast<std::uint8_t>((raw >> shift) & mask);
}

[[nodiscard]] constexpr std::uint32_t sign_extend(
    std::uint32_t value, const unsigned bit_count) noexcept {
  const std::uint32_t sign_bit = 1U << (bit_count - 1U);
  const std::uint32_t mask = (1U << bit_count) - 1U;
  value &= mask;
  return (value ^ sign_bit) - sign_bit;
}

[[nodiscard]] std::optional<Opcode> decode_opcode(
    const std::uint32_t raw) noexcept {
  switch (raw & 0x7FU) {
    case static_cast<std::uint8_t>(Opcode::Load):
      return Opcode::Load;
    case static_cast<std::uint8_t>(Opcode::MiscMemory):
      return Opcode::MiscMemory;
    case static_cast<std::uint8_t>(Opcode::OpImmediate):
      return Opcode::OpImmediate;
    case static_cast<std::uint8_t>(Opcode::Auipc):
      return Opcode::Auipc;
    case static_cast<std::uint8_t>(Opcode::Store):
      return Opcode::Store;
    case static_cast<std::uint8_t>(Opcode::Op):
      return Opcode::Op;
    case static_cast<std::uint8_t>(Opcode::Lui):
      return Opcode::Lui;
    case static_cast<std::uint8_t>(Opcode::Branch):
      return Opcode::Branch;
    case static_cast<std::uint8_t>(Opcode::Jalr):
      return Opcode::Jalr;
    case static_cast<std::uint8_t>(Opcode::Jal):
      return Opcode::Jal;
    case static_cast<std::uint8_t>(Opcode::System):
      return Opcode::System;
    default:
      return std::nullopt;
  }
}

[[nodiscard]] constexpr InstructionFormat format_for_opcode(
    const Opcode opcode) noexcept {
  switch (opcode) {
    case Opcode::Op:
      return InstructionFormat::R;
    case Opcode::Load:
    case Opcode::MiscMemory:
    case Opcode::OpImmediate:
    case Opcode::Jalr:
    case Opcode::System:
      return InstructionFormat::I;
    case Opcode::Store:
      return InstructionFormat::S;
    case Opcode::Branch:
      return InstructionFormat::B;
    case Opcode::Auipc:
    case Opcode::Lui:
      return InstructionFormat::U;
    case Opcode::Jal:
      return InstructionFormat::J;
  }
  return InstructionFormat::I;
}

}  // namespace

std::optional<DecodedInstruction> decode_instruction(
    const std::uint32_t raw) noexcept {
  const auto opcode = decode_opcode(raw);
  if (!opcode.has_value()) {
    return std::nullopt;
  }

  DecodedInstruction instruction{};
  instruction.raw = raw;
  instruction.opcode = *opcode;
  instruction.format = format_for_opcode(*opcode);

  switch (instruction.format) {
    case InstructionFormat::R:
      instruction.destination = field(raw, 7U, 0x1FU);
      instruction.function3 = field(raw, 12U, 0x07U);
      instruction.source1 = field(raw, 15U, 0x1FU);
      instruction.source2 = field(raw, 20U, 0x1FU);
      instruction.function7 = field(raw, 25U, 0x7FU);
      break;
    case InstructionFormat::I:
      instruction.destination = field(raw, 7U, 0x1FU);
      instruction.function3 = field(raw, 12U, 0x07U);
      instruction.source1 = field(raw, 15U, 0x1FU);
      instruction.function7 = field(raw, 25U, 0x7FU);
      instruction.immediate = sign_extend(raw >> 20U, 12U);
      break;
    case InstructionFormat::S: {
      instruction.function3 = field(raw, 12U, 0x07U);
      instruction.source1 = field(raw, 15U, 0x1FU);
      instruction.source2 = field(raw, 20U, 0x1FU);
      const std::uint32_t immediate = ((raw >> 25U) << 5U) |
                                      ((raw >> 7U) & 0x1FU);
      instruction.immediate = sign_extend(immediate, 12U);
      break;
    }
    case InstructionFormat::B: {
      instruction.function3 = field(raw, 12U, 0x07U);
      instruction.source1 = field(raw, 15U, 0x1FU);
      instruction.source2 = field(raw, 20U, 0x1FU);
      const std::uint32_t immediate = ((raw >> 31U) << 12U) |
                                      (((raw >> 7U) & 0x01U) << 11U) |
                                      (((raw >> 25U) & 0x3FU) << 5U) |
                                      (((raw >> 8U) & 0x0FU) << 1U);
      instruction.immediate = sign_extend(immediate, 13U);
      break;
    }
    case InstructionFormat::U:
      instruction.destination = field(raw, 7U, 0x1FU);
      instruction.immediate = raw & 0xFFFFF000U;
      break;
    case InstructionFormat::J: {
      instruction.destination = field(raw, 7U, 0x1FU);
      const std::uint32_t immediate = ((raw >> 31U) << 20U) |
                                      (((raw >> 12U) & 0xFFU) << 12U) |
                                      (((raw >> 20U) & 0x01U) << 11U) |
                                      (((raw >> 21U) & 0x3FFU) << 1U);
      instruction.immediate = sign_extend(immediate, 21U);
      break;
    }
  }

  return instruction;
}

}  // namespace rvemu

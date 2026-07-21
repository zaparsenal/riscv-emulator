#pragma once

#include <cstdint>
#include <optional>

namespace rvemu {

enum class Opcode : std::uint8_t {
  Load = 0x03,
  MiscMemory = 0x0F,
  OpImmediate = 0x13,
  Auipc = 0x17,
  Store = 0x23,
  Op = 0x33,
  Lui = 0x37,
  Branch = 0x63,
  Jalr = 0x67,
  Jal = 0x6F,
  System = 0x73,
};

enum class InstructionFormat : std::uint8_t {
  R,
  I,
  S,
  B,
  U,
  J,
};

struct DecodedInstruction {
  std::uint32_t raw{0U};
  std::uint32_t immediate{0U};
  Opcode opcode{Opcode::Load};
  InstructionFormat format{InstructionFormat::I};
  std::uint8_t destination{0U};
  std::uint8_t source1{0U};
  std::uint8_t source2{0U};
  std::uint8_t function3{0U};
  std::uint8_t function7{0U};
};

[[nodiscard]] std::optional<DecodedInstruction> decode_instruction(
    std::uint32_t raw) noexcept;

}  // namespace rvemu

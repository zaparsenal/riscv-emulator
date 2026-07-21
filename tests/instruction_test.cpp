#include "rvemu/instruction.hpp"

#include <array>
#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

[[nodiscard]] constexpr std::uint32_t encode_r(
    const Opcode opcode, const std::uint8_t destination,
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::uint8_t function7) noexcept {
  return (static_cast<std::uint32_t>(function7) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(opcode);
}

[[nodiscard]] constexpr std::uint32_t encode_i(
    const Opcode opcode, const std::uint8_t destination,
    const std::uint8_t function3, const std::uint8_t source1,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(opcode);
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0xFFFU;
  return ((bits >> 5U) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         ((bits & 0x1FU) << 7U) | static_cast<std::uint8_t>(Opcode::Store);
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0x1FFFU;
  return (((bits >> 12U) & 0x01U) << 31U) |
         (((bits >> 5U) & 0x3FU) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (((bits >> 1U) & 0x0FU) << 8U) |
         (((bits >> 11U) & 0x01U) << 7U) |
         static_cast<std::uint8_t>(Opcode::Branch);
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    const std::uint8_t destination, const std::int32_t immediate) noexcept {
  const std::uint32_t bits = static_cast<std::uint32_t>(immediate) & 0x1FFFFFU;
  return (((bits >> 20U) & 0x01U) << 31U) |
         (((bits >> 1U) & 0x3FFU) << 21U) |
         (((bits >> 11U) & 0x01U) << 20U) |
         (((bits >> 12U) & 0xFFU) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Jal);
}

TEST(InstructionDecoderTest, DecodesRTypeFields) {
  constexpr std::uint32_t raw = encode_r(Opcode::Op, 1U, 0U, 2U, 3U, 0x20U);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->raw, raw);
  EXPECT_EQ(decoded->opcode, Opcode::Op);
  EXPECT_EQ(decoded->format, InstructionFormat::R);
  EXPECT_EQ(decoded->destination, 1U);
  EXPECT_EQ(decoded->source1, 2U);
  EXPECT_EQ(decoded->source2, 3U);
  EXPECT_EQ(decoded->function3, 0U);
  EXPECT_EQ(decoded->function7, 0x20U);
  EXPECT_EQ(decoded->immediate, 0U);
}

TEST(InstructionDecoderTest, DecodesAndSignExtendsITypeImmediate) {
  constexpr std::uint32_t raw =
      encode_i(Opcode::OpImmediate, 5U, 0U, 6U, -2048);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->format, InstructionFormat::I);
  EXPECT_EQ(decoded->destination, 5U);
  EXPECT_EQ(decoded->source1, 6U);
  EXPECT_EQ(decoded->immediate, 0xFFFFF800U);
}

TEST(InstructionDecoderTest, DecodesAndSignExtendsSTypeImmediate) {
  constexpr std::uint32_t raw = encode_s(2U, 4U, 7U, -16);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->format, InstructionFormat::S);
  EXPECT_EQ(decoded->source1, 4U);
  EXPECT_EQ(decoded->source2, 7U);
  EXPECT_EQ(decoded->function3, 2U);
  EXPECT_EQ(decoded->immediate, 0xFFFFFFF0U);
}

TEST(InstructionDecoderTest, ReconstructsBTypeImmediate) {
  constexpr std::uint32_t raw = encode_b(0U, 1U, 2U, -4);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->format, InstructionFormat::B);
  EXPECT_EQ(decoded->immediate, 0xFFFFFFFCU);
}

TEST(InstructionDecoderTest, PreservesUTypeImmediateBits) {
  constexpr std::uint32_t raw = 0xABCDE000U | (9U << 7U) |
                                static_cast<std::uint8_t>(Opcode::Lui);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->format, InstructionFormat::U);
  EXPECT_EQ(decoded->destination, 9U);
  EXPECT_EQ(decoded->immediate, 0xABCDE000U);
}

TEST(InstructionDecoderTest, ReconstructsAndSignExtendsJTypeImmediate) {
  constexpr std::uint32_t raw = encode_j(1U, -1048576);

  const auto decoded = decode_instruction(raw);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->format, InstructionFormat::J);
  EXPECT_EQ(decoded->destination, 1U);
  EXPECT_EQ(decoded->immediate, 0xFFF00000U);
}

TEST(InstructionDecoderTest, RejectsUnknownMajorOpcodes) {
  EXPECT_FALSE(decode_instruction(0x00000000U).has_value());
  EXPECT_FALSE(decode_instruction(0xFFFFFFFFU).has_value());
}

TEST(InstructionDecoderTest, RecognizesEveryRv32IMajorOpcode) {
  constexpr std::array cases{
      std::pair{Opcode::Load, InstructionFormat::I},
      std::pair{Opcode::MiscMemory, InstructionFormat::I},
      std::pair{Opcode::OpImmediate, InstructionFormat::I},
      std::pair{Opcode::Auipc, InstructionFormat::U},
      std::pair{Opcode::Store, InstructionFormat::S},
      std::pair{Opcode::Op, InstructionFormat::R},
      std::pair{Opcode::Lui, InstructionFormat::U},
      std::pair{Opcode::Branch, InstructionFormat::B},
      std::pair{Opcode::Jalr, InstructionFormat::I},
      std::pair{Opcode::Jal, InstructionFormat::J},
      std::pair{Opcode::System, InstructionFormat::I},
  };

  for (const auto& [opcode, format] : cases) {
    const auto decoded =
        decode_instruction(static_cast<std::uint8_t>(opcode));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->opcode, opcode);
    EXPECT_EQ(decoded->format, format);
  }
}

}  // namespace
}  // namespace rvemu

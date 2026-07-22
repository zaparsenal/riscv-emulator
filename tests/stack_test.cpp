#include "rvemu/stack.hpp"

#include <cstdint>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

[[nodiscard]] StackInitializationFailure expect_failure(
    const StackInitializationResult& result) {
  EXPECT_TRUE(std::holds_alternative<StackInitializationFailure>(result));
  return std::get<StackInitializationFailure>(result);
}

TEST(StackInitializationTest, AlignsTopAndSizeAndWritesOnlyStackPointer) {
  CpuState state;
  state.set_program_counter(0x12345678U);
  state.write_register(2U, 0xAAAAAAAAU);
  state.write_register(5U, 0x55555555U);
  Memory memory(0x1000U, 0x105U);
  memory.write32(0x1000U, 0xA5A5F00DU);

  const StackInitializationResult result =
      initialize_freestanding_stack(state, memory, 0x1000U, 0x1050U, 17U);

  ASSERT_TRUE(std::holds_alternative<StackInitializationSuccess>(result));
  const StackInitializationSuccess success =
      std::get<StackInitializationSuccess>(result);
  EXPECT_EQ(success.initial_stack_pointer, 0x1100U);
  EXPECT_EQ(success.stack_bottom, 0x10E0U);
  EXPECT_EQ(success.stack_top_exclusive, 0x1100U);
  EXPECT_EQ(success.stack_size_bytes, 32U);
  EXPECT_EQ(state.read_register(2U), 0x1100U);
  EXPECT_EQ(state.read_register(5U), 0x55555555U);
  EXPECT_EQ(state.program_counter(), 0x12345678U);
  EXPECT_EQ(memory.read32(0x1000U), 0xA5A5F00DU);
}

TEST(StackInitializationTest, AllowsImageToEndExactlyAtStackBottom) {
  CpuState state;
  Memory memory(0x1000U, 0x100U);

  const StackInitializationResult result =
      initialize_freestanding_stack(state, memory, 0x1000U, 0x1080U, 0x80U);

  ASSERT_TRUE(std::holds_alternative<StackInitializationSuccess>(result));
  EXPECT_EQ(std::get<StackInitializationSuccess>(result).stack_bottom,
            0x1080U);
}

TEST(StackInitializationTest, RejectsImageOverlapWithoutMutation) {
  CpuState state;
  state.write_register(2U, 0xA5A5A5A5U);
  Memory memory(0x1000U, 0x100U);

  const StackInitializationResult result =
      initialize_freestanding_stack(state, memory, 0x1000U, 0x1081U, 0x80U);

  EXPECT_EQ(expect_failure(result).code,
            StackInitializationErrorCode::StackOverlapsLoadedImage);
  EXPECT_EQ(state.read_register(2U), 0xA5A5A5A5U);
}

TEST(StackInitializationTest, RejectsInvalidOccupiedRangesWithoutMutation) {
  CpuState state;
  state.write_register(2U, 0xA5A5A5A5U);
  Memory memory(0x1000U, 0x100U);

  EXPECT_EQ(expect_failure(initialize_freestanding_stack(
                               state, memory, 0x0FFFU, 0x1000U, 16U))
                .code,
            StackInitializationErrorCode::InvalidOccupiedRange);
  EXPECT_EQ(expect_failure(initialize_freestanding_stack(
                               state, memory, 0x1000U, 0x1101U, 16U))
                .code,
            StackInitializationErrorCode::InvalidOccupiedRange);
  EXPECT_EQ(expect_failure(initialize_freestanding_stack(
                               state, memory, 0x1080U, 0x107FU, 16U))
                .code,
            StackInitializationErrorCode::InvalidOccupiedRange);
  EXPECT_EQ(state.read_register(2U), 0xA5A5A5A5U);
}

TEST(StackInitializationTest, RejectsZeroOrOversizedStacksWithoutMutation) {
  CpuState state;
  state.write_register(2U, 0xA5A5A5A5U);
  Memory memory(0x1000U, 0x40U);

  EXPECT_EQ(expect_failure(initialize_freestanding_stack(
                               state, memory, 0x1000U, 0x1000U, 0U))
                .code,
            StackInitializationErrorCode::InvalidStackSize);
  EXPECT_EQ(expect_failure(initialize_freestanding_stack(
                               state, memory, 0x1000U, 0x1000U, 0x41U))
                .code,
            StackInitializationErrorCode::StackDoesNotFit);
  EXPECT_EQ(state.read_register(2U), 0xA5A5A5A5U);
}

TEST(StackInitializationTest, RejectsMappingWithoutAlignedStackCapacity) {
  CpuState state;
  Memory memory(0x1001U, 15U);

  const StackInitializationResult result =
      initialize_freestanding_stack(state, memory, 0x1001U, 0x1001U, 1U);

  EXPECT_EQ(expect_failure(result).code,
            StackInitializationErrorCode::StackDoesNotFit);
  EXPECT_EQ(state.read_register(2U), 0U);
}

TEST(StackInitializationTest, RepresentsAddressSpaceTopWithZeroStackPointer) {
  CpuState state;
  state.write_register(2U, 0xA5A5A5A5U);
  Memory memory(0xFFFFFF00U, 0x100U);

  const StackInitializationResult result = initialize_freestanding_stack(
      state, memory, 0xFFFFFF00U, 0xFFFFFF40U, 0x80U);

  ASSERT_TRUE(std::holds_alternative<StackInitializationSuccess>(result));
  const StackInitializationSuccess success =
      std::get<StackInitializationSuccess>(result);
  EXPECT_EQ(success.stack_bottom, 0xFFFFFF80ULL);
  EXPECT_EQ(success.stack_top_exclusive, 0x100000000ULL);
  EXPECT_EQ(success.initial_stack_pointer, 0U);
  EXPECT_EQ(state.read_register(2U), 0U);
}

TEST(StackInitializationTest, ErrorMessagesAreStableAndReadable) {
  EXPECT_EQ(stack_initialization_error_message(
                StackInitializationErrorCode::StackOverlapsLoadedImage),
            "the requested stack overlaps the loaded image");
}

}  // namespace
}  // namespace rvemu

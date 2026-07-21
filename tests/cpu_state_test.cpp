#include "rvemu/cpu_state.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

TEST(CpuStateTest, StartsWithZeroedRegistersAndProgramCounter) {
  const CpuState state;

  for (std::size_t index = 0; index < CpuState::kRegisterCount; ++index) {
    EXPECT_EQ(state.read_register(index), 0U);
  }
  EXPECT_EQ(state.program_counter(), 0U);
}

TEST(CpuStateTest, StoresValuesInGeneralPurposeRegisters) {
  CpuState state;

  for (std::size_t index = 1; index < CpuState::kRegisterCount; ++index) {
    state.write_register(index, static_cast<std::uint32_t>(index * 17U));
  }

  for (std::size_t index = 1; index < CpuState::kRegisterCount; ++index) {
    EXPECT_EQ(state.read_register(index), index * 17U);
  }
}

TEST(CpuStateTest, IgnoresWritesToZeroRegister) {
  CpuState state;

  state.write_register(0U, std::numeric_limits<std::uint32_t>::max());

  EXPECT_EQ(state.read_register(0U), 0U);
}

TEST(CpuStateTest, RejectsInvalidRegisterIndices) {
  CpuState state;

  EXPECT_THROW((void)state.read_register(CpuState::kRegisterCount),
               std::out_of_range);
  EXPECT_THROW(state.write_register(CpuState::kRegisterCount, 42U),
               std::out_of_range);
}

TEST(CpuStateTest, UpdatesAndAdvancesProgramCounterWithWordSemantics) {
  CpuState state;

  state.set_program_counter(0x1000U);
  state.advance_program_counter();
  EXPECT_EQ(state.program_counter(), 0x1004U);

  state.set_program_counter(std::numeric_limits<std::uint32_t>::max() - 1U);
  state.advance_program_counter(4U);
  EXPECT_EQ(state.program_counter(), 2U);
}

TEST(CpuStateTest, ResetClearsRegistersAndProgramCounter) {
  CpuState state;
  state.write_register(1U, 0xDEADBEEFU);
  state.write_register(31U, 0xFFFFFFFFU);
  state.set_program_counter(0x80000000U);

  state.reset();

  for (std::size_t index = 0; index < CpuState::kRegisterCount; ++index) {
    EXPECT_EQ(state.read_register(index), 0U);
  }
  EXPECT_EQ(state.program_counter(), 0U);
}

}  // namespace
}  // namespace rvemu

#include "rvemu/memory.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

TEST(MemoryTest, CreatesZeroInitializedRegionAtRequestedBaseAddress) {
  const Memory memory(0x1000U, 8U);

  EXPECT_EQ(memory.base_address(), 0x1000U);
  EXPECT_EQ(memory.end_address_exclusive(), 0x1008U);
  EXPECT_EQ(memory.size_bytes(), 8U);
  for (std::uint32_t address = 0x1000U; address < 0x1008U; ++address) {
    EXPECT_EQ(memory.read8(address), 0U);
  }
}

TEST(MemoryTest, ReadsAndWritesLittleEndianValues) {
  Memory memory(16U);

  memory.write32(4U, 0x89ABCDEFU);
  memory.write16(8U, 0x1357U);

  EXPECT_EQ(memory.read8(4U), 0xEFU);
  EXPECT_EQ(memory.read8(5U), 0xCDU);
  EXPECT_EQ(memory.read8(6U), 0xABU);
  EXPECT_EQ(memory.read8(7U), 0x89U);
  EXPECT_EQ(memory.read16(4U), 0xCDEFU);
  EXPECT_EQ(memory.read16(6U), 0x89ABU);
  EXPECT_EQ(memory.read32(4U), 0x89ABCDEFU);
  EXPECT_EQ(memory.read8(8U), 0x57U);
  EXPECT_EQ(memory.read8(9U), 0x13U);
  EXPECT_EQ(memory.read16(8U), 0x1357U);
}

TEST(MemoryTest, ByteWritesComposeLittleEndianValues) {
  Memory memory(8U);
  memory.write8(0U, 0x78U);
  memory.write8(1U, 0x56U);
  memory.write8(2U, 0x34U);
  memory.write8(3U, 0x12U);

  EXPECT_EQ(memory.read16(0U), 0x5678U);
  EXPECT_EQ(memory.read32(0U), 0x12345678U);
}

TEST(MemoryTest, SupportsARegionEndingAtTheTopOfTheAddressSpace) {
  Memory memory(0xFFFFFFFCU, 4U);

  memory.write32(0xFFFFFFFCU, 0xA5A5F00DU);

  EXPECT_EQ(memory.read32(0xFFFFFFFCU), 0xA5A5F00DU);
  EXPECT_EQ(memory.end_address_exclusive(), 0x100000000ULL);
}

TEST(MemoryTest, RejectsRegionsBeyondTheAddressSpace) {
  EXPECT_THROW((void)Memory(0xFFFFFFFFU, 2U), std::invalid_argument);
}

TEST(MemoryTest, RejectsAccessesOutsideTheMappedRegion) {
  Memory memory(0x1000U, 8U);

  EXPECT_THROW((void)memory.read8(0x0FFFU), MemoryOutOfBoundsError);
  EXPECT_THROW((void)memory.read8(0x1008U), MemoryOutOfBoundsError);
  EXPECT_THROW((void)memory.read32(0x1008U), MemoryOutOfBoundsError);
  EXPECT_THROW(memory.write8(0x1008U, 1U), MemoryOutOfBoundsError);
}

TEST(MemoryTest, RejectsAccessesThatCrossTheEndOfTheMappedRegion) {
  Memory memory(6U);
  memory.write8(4U, 0xAAU);
  memory.write8(5U, 0xBBU);

  EXPECT_THROW((void)memory.read32(4U), MemoryOutOfBoundsError);
  EXPECT_THROW(memory.write32(4U, 0xFFFFFFFFU), MemoryOutOfBoundsError);
  EXPECT_EQ(memory.read8(4U), 0xAAU);
  EXPECT_EQ(memory.read8(5U), 0xBBU);
}

TEST(MemoryTest, ReportsAccessDetailsForBoundsErrors) {
  Memory memory(4U);

  try {
    memory.write32(4U, 0x12345678U);
    FAIL() << "expected an out-of-bounds access";
  } catch (const MemoryOutOfBoundsError& error) {
    EXPECT_EQ(error.address(), 4U);
    EXPECT_EQ(error.width(), 4U);
    EXPECT_EQ(error.operation(), MemoryOperation::Write);
  }
}

TEST(MemoryTest, RejectsMisalignedHalfwordAndWordAccesses) {
  Memory memory(16U);

  EXPECT_THROW((void)memory.read16(1U), MemoryMisalignmentError);
  EXPECT_THROW(memory.write16(3U, 0x1234U), MemoryMisalignmentError);
  EXPECT_THROW((void)memory.read32(2U), MemoryMisalignmentError);
  EXPECT_THROW(memory.write32(6U, 0x12345678U), MemoryMisalignmentError);
}

TEST(MemoryTest, FailedWritesLeaveMemoryUnchanged) {
  Memory memory(8U);
  memory.write32(0U, 0x11223344U);

  EXPECT_THROW(memory.write32(6U, 0xFFFFFFFFU), MemoryMisalignmentError);
  EXPECT_THROW(memory.write32(8U, 0xFFFFFFFFU), MemoryOutOfBoundsError);

  EXPECT_EQ(memory.read32(0U), 0x11223344U);
  EXPECT_EQ(memory.read32(4U), 0U);
}

TEST(MemoryTest, ClearRestoresAllBytesToZero) {
  Memory memory(8U);
  memory.write32(0U, std::numeric_limits<std::uint32_t>::max());
  memory.write32(4U, 0xA5A5A5A5U);

  memory.clear();

  EXPECT_EQ(memory.read32(0U), 0U);
  EXPECT_EQ(memory.read32(4U), 0U);
}

}  // namespace
}  // namespace rvemu

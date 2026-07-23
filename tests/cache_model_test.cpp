#include "rvemu/cache_model.hpp"
#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr CacheConfiguration kSmallCache{16U, 4U, 2U};

[[nodiscard]] ExecutionObservation make_observation(
    const std::uint32_t program_counter,
    const std::optional<DataMemoryAccessObservation> data_access =
        std::nullopt) {
  return ExecutionObservation{
      DecodedInstruction{}, program_counter, program_counter + 4U,
      data_access, std::nullopt};
}

[[nodiscard]] constexpr std::uint32_t encode_addi(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

TEST(CacheModelTest, RejectsEveryInvalidConfigurationGeometry) {
  struct InvalidCase {
    CacheConfiguration configuration;
    CacheConfigurationErrorCode expected;
  };
  constexpr std::array cases{
      InvalidCase{CacheConfiguration{0U, 4U, 1U},
                  CacheConfigurationErrorCode::ZeroCapacity},
      InvalidCase{CacheConfiguration{16U, 2U, 1U},
                  CacheConfigurationErrorCode::LineSizeTooSmall},
      InvalidCase{CacheConfiguration{24U, 6U, 1U},
                  CacheConfigurationErrorCode::LineSizeNotPowerOfTwo},
      InvalidCase{CacheConfiguration{16U, 4U, 0U},
                  CacheConfigurationErrorCode::ZeroAssociativity},
      InvalidCase{
          CacheConfiguration{
              std::numeric_limits<std::size_t>::max(), 4U,
              std::numeric_limits<std::size_t>::max()},
          CacheConfigurationErrorCode::GeometryOverflow},
      InvalidCase{CacheConfiguration{24U, 4U, 4U},
                  CacheConfigurationErrorCode::CapacityNotDivisible},
  };

  for (const InvalidCase& test_case : cases) {
    try {
      (void)SetAssociativeCache(test_case.configuration);
      FAIL() << "expected invalid cache configuration";
    } catch (const CacheConfigurationError& error) {
      EXPECT_EQ(error.code(), test_case.expected);
      EXPECT_EQ(error.what(),
                cache_configuration_error_message(test_case.expected));
    }
  }
}

TEST(CacheModelTest, UsesAddressLinesSetsAndDeterministicLruReplacement) {
  SetAssociativeCache cache(kSmallCache);

  EXPECT_EQ(cache.access(0U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(3U), CacheAccessResult::Hit);
  EXPECT_EQ(cache.access(8U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(0U), CacheAccessResult::Hit);
  EXPECT_EQ(cache.access(16U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(0U), CacheAccessResult::Hit);
  EXPECT_EQ(cache.access(8U), CacheAccessResult::Miss);

  const CacheStatistics statistics = cache.statistics();
  EXPECT_EQ(statistics.accesses, 7U);
  EXPECT_EQ(statistics.hits, 3U);
  EXPECT_EQ(statistics.misses, 4U);
}

TEST(CacheModelTest, KeepsDifferentSetsIndependent) {
  SetAssociativeCache cache(CacheConfiguration{8U, 4U, 1U});

  EXPECT_EQ(cache.access(0U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(4U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(0U), CacheAccessResult::Hit);
  EXPECT_EQ(cache.access(4U), CacheAccessResult::Hit);
}

TEST(CacheModelTest, ResetRestoresColdStateAndZeroStatistics) {
  SetAssociativeCache cache(kSmallCache);
  EXPECT_EQ(cache.access(0x100U), CacheAccessResult::Miss);
  EXPECT_EQ(cache.access(0x100U), CacheAccessResult::Hit);

  cache.reset();

  const CacheStatistics reset_statistics = cache.statistics();
  EXPECT_EQ(reset_statistics.accesses, 0U);
  EXPECT_EQ(reset_statistics.hits, 0U);
  EXPECT_EQ(reset_statistics.misses, 0U);
  EXPECT_EQ(cache.access(0x100U), CacheAccessResult::Miss);
}

TEST(CacheModelTest, SplitModelTracksInstructionLoadAndStoreStreams) {
  const SplitCacheConfiguration configuration{kSmallCache, kSmallCache};
  SplitCacheModel model(configuration);

  model.observe(make_observation(
      0x1000U,
      DataMemoryAccessObservation{DataMemoryAccessKind::Load, 0x2000U, 4U}));
  model.observe(make_observation(
      0x1004U,
      DataMemoryAccessObservation{DataMemoryAccessKind::Store, 0x2000U, 1U}));
  model.observe(make_observation(0x1000U));

  const SplitCacheStatistics statistics = model.statistics();
  EXPECT_EQ(statistics.instruction.accesses, 3U);
  EXPECT_EQ(statistics.instruction.hits, 1U);
  EXPECT_EQ(statistics.instruction.misses, 2U);
  EXPECT_EQ(statistics.data.total.accesses, 2U);
  EXPECT_EQ(statistics.data.total.hits, 1U);
  EXPECT_EQ(statistics.data.total.misses, 1U);
  EXPECT_EQ(statistics.data.loads.accesses, 1U);
  EXPECT_EQ(statistics.data.loads.misses, 1U);
  EXPECT_EQ(statistics.data.stores.accesses, 1U);
  EXPECT_EQ(statistics.data.stores.hits, 1U);
  EXPECT_EQ(model.configuration().instruction.capacity_bytes,
            kSmallCache.capacity_bytes);
}

TEST(CacheModelTest, SplitModelResetClearsBothCachesAndAllCounters) {
  SplitCacheModel model(SplitCacheConfiguration{kSmallCache, kSmallCache});
  const ExecutionObservation observation = make_observation(
      0x1000U,
      DataMemoryAccessObservation{DataMemoryAccessKind::Load, 0x2000U, 4U});
  model.observe(observation);

  model.reset();
  model.observe(observation);

  const SplitCacheStatistics statistics = model.statistics();
  EXPECT_EQ(statistics.instruction.accesses, 1U);
  EXPECT_EQ(statistics.instruction.hits, 0U);
  EXPECT_EQ(statistics.instruction.misses, 1U);
  EXPECT_EQ(statistics.data.total.accesses, 1U);
  EXPECT_EQ(statistics.data.total.hits, 0U);
  EXPECT_EQ(statistics.data.total.misses, 1U);
}

TEST(CacheModelTest, ConsumesCompletedEngineObservationsWithoutChangingState) {
  constexpr std::uint32_t kBaseAddress = 0x1000U;
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 1));
  memory.write32(kBaseAddress + 4U, encode_addi(1U, 1U, 1));
  SplitCacheModel model(
      SplitCacheConfiguration{kSmallCache, kSmallCache});
  ExecutionEngine engine(state, memory, &model);

  const RunResult result = engine.run(2U);

  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(result));
  EXPECT_EQ(state.read_register(1U), 2U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 8U);
  const SplitCacheStatistics statistics = model.statistics();
  EXPECT_EQ(statistics.instruction.accesses, 2U);
  EXPECT_EQ(statistics.instruction.hits, 0U);
  EXPECT_EQ(statistics.instruction.misses, 2U);
  EXPECT_EQ(statistics.data.total.accesses, 0U);
}

}  // namespace
}  // namespace rvemu

#include "rvemu/performance_analyzer.hpp"
#include "rvemu/program_session.hpp"

#include <cstdint>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr CacheConfiguration kSmallCache{16U, 4U, 2U};
constexpr PerformanceAnalyzerConfiguration kConfiguration{
    SplitCacheConfiguration{kSmallCache, kSmallCache},
    BranchPredictorConfiguration{
        BranchPredictionStrategy::AlwaysNotTaken, 0U,
        TwoBitCounterState::WeaklyNotTaken}};

[[nodiscard]] ExecutionObservation make_observation(
    const std::uint32_t program_counter,
    const std::optional<DataMemoryAccessObservation> data_access =
        std::nullopt,
    const std::optional<ControlFlowObservation> control_flow =
        std::nullopt) {
  return ExecutionObservation{
      DecodedInstruction{}, program_counter, program_counter + 4U,
      data_access, control_flow};
}

[[nodiscard]] constexpr std::uint32_t encode_addi(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

class ResumeEnvironment final : public EnvironmentCallHandler {
 public:
  [[nodiscard]] EnvironmentCallResult handle(
      const EnvironmentCall&, const Memory&) noexcept override {
    return EnvironmentCallResume{0U};
  }
};

TEST(PerformanceAnalyzerTest, PreservesTypedCacheConfigurationFailures) {
  PerformanceAnalyzerConfiguration configuration = kConfiguration;
  configuration.cache.instruction.capacity_bytes = 0U;

  try {
    (void)PerformanceAnalyzer(configuration);
    FAIL() << "expected invalid cache configuration";
  } catch (const CacheConfigurationError& error) {
    EXPECT_EQ(error.code(), CacheConfigurationErrorCode::ZeroCapacity);
  }
}

TEST(PerformanceAnalyzerTest,
     PreservesTypedBranchPredictorConfigurationFailures) {
  PerformanceAnalyzerConfiguration configuration = kConfiguration;
  configuration.branch_predictor.table_entries = 3U;
  configuration.branch_predictor.strategy =
      BranchPredictionStrategy::BimodalTwoBit;

  try {
    (void)PerformanceAnalyzer(configuration);
    FAIL() << "expected invalid branch predictor configuration";
  } catch (const BranchPredictorConfigurationError& error) {
    EXPECT_EQ(
        error.code(),
        BranchPredictorConfigurationErrorCode::TableEntriesNotPowerOfTwo);
  }
}

TEST(PerformanceAnalyzerTest,
     ProducesOneConsistentSnapshotFromBothIndependentModels) {
  PerformanceAnalyzer analyzer(kConfiguration);

  analyzer.observe(make_observation(
      0x1000U,
      DataMemoryAccessObservation{
          DataMemoryAccessKind::Load, 0x2000U, 4U},
      ControlFlowObservation{
          ControlFlowKind::ConditionalBranch, true}));
  analyzer.observe(make_observation(
      0x1004U,
      DataMemoryAccessObservation{
          DataMemoryAccessKind::Store, 0x2000U, 1U}));
  analyzer.observe(make_observation(
      0x1000U, std::nullopt,
      ControlFlowObservation{
          ControlFlowKind::ConditionalBranch, false}));

  const PerformanceAnalyzerStatistics statistics = analyzer.statistics();
  EXPECT_EQ(statistics.instructions_observed, 3U);
  EXPECT_EQ(statistics.cache.instruction.accesses, 3U);
  EXPECT_EQ(statistics.cache.instruction.hits, 1U);
  EXPECT_EQ(statistics.cache.instruction.misses, 2U);
  EXPECT_EQ(statistics.cache.data.total.accesses, 2U);
  EXPECT_EQ(statistics.cache.data.total.hits, 1U);
  EXPECT_EQ(statistics.cache.data.total.misses, 1U);
  EXPECT_EQ(statistics.cache.data.loads.accesses, 1U);
  EXPECT_EQ(statistics.cache.data.stores.accesses, 1U);
  EXPECT_EQ(statistics.branch_predictor.predictions, 2U);
  EXPECT_EQ(statistics.branch_predictor.correct, 1U);
  EXPECT_EQ(statistics.branch_predictor.incorrect, 1U);
}

TEST(PerformanceAnalyzerTest, ResetClearsAndColdsEveryOwnedModel) {
  PerformanceAnalyzer analyzer(kConfiguration);
  const ExecutionObservation observation = make_observation(
      0x1000U,
      DataMemoryAccessObservation{
          DataMemoryAccessKind::Load, 0x2000U, 4U},
      ControlFlowObservation{
          ControlFlowKind::ConditionalBranch, false});
  analyzer.observe(observation);

  analyzer.reset();

  const PerformanceAnalyzerStatistics reset_statistics =
      analyzer.statistics();
  EXPECT_EQ(reset_statistics.instructions_observed, 0U);
  EXPECT_EQ(reset_statistics.cache.instruction.accesses, 0U);
  EXPECT_EQ(reset_statistics.cache.data.total.accesses, 0U);
  EXPECT_EQ(reset_statistics.branch_predictor.predictions, 0U);

  analyzer.observe(observation);
  const PerformanceAnalyzerStatistics cold_statistics =
      analyzer.statistics();
  EXPECT_EQ(cold_statistics.cache.instruction.misses, 1U);
  EXPECT_EQ(cold_statistics.cache.data.total.misses, 1U);
  EXPECT_EQ(cold_statistics.branch_predictor.correct, 1U);
}

TEST(PerformanceAnalyzerTest,
     ProgramSessionFeedsOnlyArchitecturallyRetiredInstructions) {
  constexpr std::uint32_t kBaseAddress = 0x1000U;
  constexpr std::uint32_t kEcallInstruction = 0x00000073U;
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  memory.write32(kBaseAddress, encode_addi(1U, 0U, 7));
  memory.write32(kBaseAddress + 4U, kEcallInstruction);
  ResumeEnvironment environment;
  PerformanceAnalyzer analyzer(kConfiguration);
  ProgramSession session(state, memory, environment, &analyzer);

  const SessionRunResult result = session.run(2U);

  ASSERT_TRUE(std::holds_alternative<SessionStepLimitReached>(result));
  EXPECT_EQ(state.read_register(1U), 7U);
  EXPECT_EQ(state.program_counter(), kBaseAddress + 8U);
  const PerformanceAnalyzerStatistics statistics = analyzer.statistics();
  EXPECT_EQ(statistics.instructions_observed, 1U);
  EXPECT_EQ(statistics.cache.instruction.accesses, 1U);
  EXPECT_EQ(statistics.branch_predictor.predictions, 0U);
  EXPECT_EQ(analyzer.configuration().cache.instruction.capacity_bytes,
            kSmallCache.capacity_bytes);
}

}  // namespace
}  // namespace rvemu

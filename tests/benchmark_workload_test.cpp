#include "benchmark_workload.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu::benchmarking {
namespace {

constexpr std::uint64_t kTestInstructionLimit = 4096U;
constexpr std::array<WorkloadKind, 3U> kEveryWorkload{
    WorkloadKind::IntegerMix,
    WorkloadKind::MemoryStride,
    WorkloadKind::BranchPattern,
};

TEST(BenchmarkWorkloadTest, RejectsUnknownWorkloadKinds) {
  EXPECT_THROW(
      (void)BenchmarkWorkload(static_cast<WorkloadKind>(0xFFU)),
      std::invalid_argument);
}

TEST(BenchmarkWorkloadTest, ExposesStableNamesAndModelConfiguration) {
  EXPECT_EQ(workload_name(WorkloadKind::IntegerMix),
            std::string_view("IntegerMix"));
  EXPECT_EQ(workload_name(WorkloadKind::MemoryStride),
            std::string_view("MemoryStride"));
  EXPECT_EQ(workload_name(WorkloadKind::BranchPattern),
            std::string_view("BranchPattern"));

  const PerformanceAnalyzerConfiguration configuration =
      benchmark_performance_configuration();
  EXPECT_EQ(configuration.cache.instruction.capacity_bytes, 4096U);
  EXPECT_EQ(configuration.cache.instruction.line_size_bytes, 64U);
  EXPECT_EQ(configuration.cache.instruction.associativity, 2U);
  EXPECT_EQ(configuration.cache.data.capacity_bytes, 4096U);
  EXPECT_EQ(configuration.branch_predictor.strategy,
            BranchPredictionStrategy::BimodalTwoBit);
  EXPECT_EQ(configuration.branch_predictor.table_entries, 256U);
  EXPECT_EQ(configuration.branch_predictor.initial_counter,
            TwoBitCounterState::WeaklyNotTaken);
  EXPECT_EQ(configuration.cycle_costs.base_instruction_cycles, 1U);
  EXPECT_EQ(
      configuration.cycle_costs
          .instruction_cache_miss_penalty_cycles,
      10U);
  EXPECT_EQ(
      configuration.cycle_costs.data_cache_miss_penalty_cycles,
      20U);
  EXPECT_EQ(
      configuration.cycle_costs
          .branch_misprediction_penalty_cycles,
      5U);
}

TEST(BenchmarkWorkloadTest,
     EveryWorkloadRunsExactlyAndProducesAValidCycleEstimate) {
  for (const WorkloadKind kind : kEveryWorkload) {
    SCOPED_TRACE(workload_name(kind));
    BenchmarkWorkload workload(kind);

    const RunResult result = workload.run(kTestInstructionLimit);

    const auto* limit =
        std::get_if<InstructionLimitReached>(&result);
    ASSERT_NE(limit, nullptr);
    EXPECT_EQ(limit->instructions_executed, kTestInstructionLimit);

    const PerformanceAnalyzerStatistics statistics =
        workload.statistics();
    EXPECT_EQ(statistics.instructions_observed,
              kTestInstructionLimit);
    EXPECT_EQ(statistics.cache.instruction.accesses,
              kTestInstructionLimit);
    EXPECT_EQ(statistics.cache.instruction.accesses,
              statistics.cache.instruction.hits +
                  statistics.cache.instruction.misses);

    const CycleEstimateResult estimate_result =
        workload.estimate_cycles();
    const auto* estimate =
        std::get_if<CycleEstimate>(&estimate_result);
    ASSERT_NE(estimate, nullptr);
    EXPECT_GE(estimate->total_cycles, kTestInstructionLimit);
  }
}

TEST(BenchmarkWorkloadTest, ResetRestoresColdDeterministicModelState) {
  BenchmarkWorkload workload(WorkloadKind::MemoryStride);
  const RunResult first_result =
      workload.run(kTestInstructionLimit);
  ASSERT_TRUE(
      std::holds_alternative<InstructionLimitReached>(first_result));
  const PerformanceAnalyzerStatistics first = workload.statistics();
  const std::uint32_t first_data_word =
      workload.memory().read32(kDataBaseAddress);

  workload.reset();

  const PerformanceAnalyzerStatistics reset = workload.statistics();
  EXPECT_EQ(reset.instructions_observed, 0U);
  EXPECT_EQ(reset.cache.instruction.accesses, 0U);
  EXPECT_EQ(reset.cache.data.total.accesses, 0U);
  EXPECT_EQ(reset.branch_predictor.predictions, 0U);
  EXPECT_EQ(workload.state().program_counter(), kMemoryBaseAddress);
  EXPECT_EQ(workload.memory().read32(kDataBaseAddress), 0U);

  const RunResult second_result =
      workload.run(kTestInstructionLimit);
  ASSERT_TRUE(
      std::holds_alternative<InstructionLimitReached>(second_result));
  const PerformanceAnalyzerStatistics second = workload.statistics();
  EXPECT_EQ(second.instructions_observed, first.instructions_observed);
  EXPECT_EQ(second.cache.instruction.hits,
            first.cache.instruction.hits);
  EXPECT_EQ(second.cache.instruction.misses,
            first.cache.instruction.misses);
  EXPECT_EQ(second.cache.data.total.hits,
            first.cache.data.total.hits);
  EXPECT_EQ(second.cache.data.total.misses,
            first.cache.data.total.misses);
  EXPECT_EQ(second.branch_predictor.correct,
            first.branch_predictor.correct);
  EXPECT_EQ(second.branch_predictor.incorrect,
            first.branch_predictor.incorrect);
  EXPECT_EQ(workload.memory().read32(kDataBaseAddress),
            first_data_word);
}

TEST(BenchmarkWorkloadTest, ScenariosExerciseTheirIntendedSubsystems) {
  BenchmarkWorkload integer_workload(WorkloadKind::IntegerMix);
  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(
      integer_workload.run(kTestInstructionLimit)));
  const PerformanceAnalyzerStatistics integer_statistics =
      integer_workload.statistics();
  EXPECT_EQ(integer_statistics.cache.data.total.accesses, 0U);
  EXPECT_EQ(integer_statistics.branch_predictor.predictions, 0U);

  BenchmarkWorkload memory_workload(WorkloadKind::MemoryStride);
  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(
      memory_workload.run(kTestInstructionLimit)));
  const PerformanceAnalyzerStatistics memory_statistics =
      memory_workload.statistics();
  EXPECT_GT(memory_statistics.cache.data.loads.accesses, 0U);
  EXPECT_GT(memory_statistics.cache.data.stores.accesses, 0U);
  EXPECT_GT(memory_statistics.cache.data.total.hits, 0U);
  EXPECT_GT(memory_statistics.cache.data.total.misses, 0U);
  EXPECT_GT(memory_statistics.branch_predictor.predictions, 0U);

  BenchmarkWorkload branch_workload(WorkloadKind::BranchPattern);
  ASSERT_TRUE(std::holds_alternative<InstructionLimitReached>(
      branch_workload.run(kTestInstructionLimit)));
  const PerformanceAnalyzerStatistics branch_statistics =
      branch_workload.statistics();
  EXPECT_EQ(branch_statistics.cache.data.total.accesses, 0U);
  EXPECT_GT(branch_statistics.branch_predictor.correct, 0U);
  EXPECT_GT(branch_statistics.branch_predictor.incorrect, 0U);
}

}  // namespace
}  // namespace rvemu::benchmarking

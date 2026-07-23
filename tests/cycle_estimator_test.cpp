#include "rvemu/cycle_estimator.hpp"

#include <cstdint>
#include <limits>
#include <string_view>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

TEST(CycleEstimatorTest, RejectsZeroBaseInstructionCost) {
  try {
    (void)CycleEstimator(CycleCostConfiguration{0U, 1U, 2U, 3U});
    FAIL() << "expected invalid cycle cost configuration";
  } catch (const CycleCostConfigurationError& error) {
    EXPECT_EQ(
        error.code(),
        CycleCostConfigurationErrorCode::ZeroBaseInstructionCycles);
    EXPECT_STREQ(error.what(),
                 "base instruction cycle cost must be positive");
  }
}

TEST(CycleEstimatorTest, CalculatesEachComponentAndTotal) {
  const CycleEstimator estimator(
      CycleCostConfiguration{1U, 10U, 20U, 5U});

  const CycleEstimateResult result =
      estimator.estimate(CycleEventCounts{100U, 5U, 3U, 2U});

  const auto* estimate = std::get_if<CycleEstimate>(&result);
  ASSERT_NE(estimate, nullptr);
  EXPECT_EQ(estimate->base_instruction_cycles, 100U);
  EXPECT_EQ(estimate->instruction_cache_miss_cycles, 50U);
  EXPECT_EQ(estimate->data_cache_miss_cycles, 60U);
  EXPECT_EQ(estimate->branch_misprediction_cycles, 10U);
  EXPECT_EQ(estimate->total_cycles, 220U);
}

TEST(CycleEstimatorTest, AllowsZeroPenalties) {
  const CycleEstimator estimator(CycleCostConfiguration{3U, 0U, 0U, 0U});

  EXPECT_EQ(estimator.configuration().base_instruction_cycles, 3U);
  const CycleEstimateResult result =
      estimator.estimate(CycleEventCounts{7U, 9U, 11U, 13U});

  const auto* estimate = std::get_if<CycleEstimate>(&result);
  ASSERT_NE(estimate, nullptr);
  EXPECT_EQ(estimate->base_instruction_cycles, 21U);
  EXPECT_EQ(estimate->instruction_cache_miss_cycles, 0U);
  EXPECT_EQ(estimate->data_cache_miss_cycles, 0U);
  EXPECT_EQ(estimate->branch_misprediction_cycles, 0U);
  EXPECT_EQ(estimate->total_cycles, 21U);
}

TEST(CycleEstimatorTest, AcceptsExactMaximumTotal) {
  constexpr std::uint64_t kMaximum =
      std::numeric_limits<std::uint64_t>::max();
  const CycleEstimator estimator(
      CycleCostConfiguration{kMaximum, 0U, 0U, 0U});

  const CycleEstimateResult result =
      estimator.estimate(CycleEventCounts{1U, 0U, 0U, 0U});

  const auto* estimate = std::get_if<CycleEstimate>(&result);
  ASSERT_NE(estimate, nullptr);
  EXPECT_EQ(estimate->total_cycles, kMaximum);
}

void expect_overflow(const CycleCostConfiguration configuration,
                     const CycleEventCounts counts,
                     const CycleEstimateErrorCode expected_error) {
  const CycleEstimator estimator(configuration);
  const CycleEstimateResult result = estimator.estimate(counts);

  const auto* failure = std::get_if<CycleEstimateFailure>(&result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->code, expected_error);
}

TEST(CycleEstimatorTest, ReportsBaseInstructionComponentOverflow) {
  expect_overflow(
      CycleCostConfiguration{
          std::numeric_limits<std::uint64_t>::max(), 0U, 0U, 0U},
      CycleEventCounts{2U, 0U, 0U, 0U},
      CycleEstimateErrorCode::BaseInstructionCostOverflow);
}

TEST(CycleEstimatorTest, ReportsInstructionCacheMissComponentOverflow) {
  expect_overflow(
      CycleCostConfiguration{
          1U, std::numeric_limits<std::uint64_t>::max(), 0U, 0U},
      CycleEventCounts{0U, 2U, 0U, 0U},
      CycleEstimateErrorCode::InstructionCacheMissCostOverflow);
}

TEST(CycleEstimatorTest, ReportsDataCacheMissComponentOverflow) {
  expect_overflow(
      CycleCostConfiguration{
          1U, 0U, std::numeric_limits<std::uint64_t>::max(), 0U},
      CycleEventCounts{0U, 0U, 2U, 0U},
      CycleEstimateErrorCode::DataCacheMissCostOverflow);
}

TEST(CycleEstimatorTest, ReportsBranchMispredictionComponentOverflow) {
  expect_overflow(
      CycleCostConfiguration{
          1U, 0U, 0U, std::numeric_limits<std::uint64_t>::max()},
      CycleEventCounts{0U, 0U, 0U, 2U},
      CycleEstimateErrorCode::BranchMispredictionCostOverflow);
}

TEST(CycleEstimatorTest, ReportsOverflowWhenComponentsDoNotFitTogether) {
  constexpr std::uint64_t kMaximum =
      std::numeric_limits<std::uint64_t>::max();
  const CycleEstimator estimator(
      CycleCostConfiguration{kMaximum, 1U, 0U, 0U});

  const CycleEstimateResult result =
      estimator.estimate(CycleEventCounts{1U, 1U, 0U, 0U});

  const auto* failure = std::get_if<CycleEstimateFailure>(&result);
  ASSERT_NE(failure, nullptr);
  EXPECT_EQ(failure->code, CycleEstimateErrorCode::TotalOverflow);
}

TEST(CycleEstimatorTest, ProvidesStableFailureMessages) {
  EXPECT_EQ(
      cycle_estimate_error_message(
          CycleEstimateErrorCode::BaseInstructionCostOverflow),
      std::string_view("base instruction cycle cost overflow"));
  EXPECT_EQ(
      cycle_estimate_error_message(
          CycleEstimateErrorCode::InstructionCacheMissCostOverflow),
      std::string_view("instruction-cache miss cycle cost overflow"));
  EXPECT_EQ(
      cycle_estimate_error_message(
          CycleEstimateErrorCode::DataCacheMissCostOverflow),
      std::string_view("data-cache miss cycle cost overflow"));
  EXPECT_EQ(
      cycle_estimate_error_message(
          CycleEstimateErrorCode::BranchMispredictionCostOverflow),
      std::string_view("branch-misprediction cycle cost overflow"));
  EXPECT_EQ(
      cycle_estimate_error_message(CycleEstimateErrorCode::TotalOverflow),
      std::string_view("total estimated cycle count overflow"));
}

}  // namespace
}  // namespace rvemu

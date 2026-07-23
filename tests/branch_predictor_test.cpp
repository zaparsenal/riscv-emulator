#include "rvemu/branch_predictor.hpp"
#include "rvemu/execution_engine.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

namespace rvemu {
namespace {

constexpr BranchPredictorConfiguration kAlwaysNotTaken{
    BranchPredictionStrategy::AlwaysNotTaken, 0U,
    TwoBitCounterState::WeaklyNotTaken};
constexpr BranchPredictorConfiguration kAlwaysTaken{
    BranchPredictionStrategy::AlwaysTaken, 0U,
    TwoBitCounterState::WeaklyNotTaken};

[[nodiscard]] ExecutionObservation make_control_flow_observation(
    const std::uint32_t program_counter, const ControlFlowKind kind,
    const bool taken) {
  return ExecutionObservation{
      DecodedInstruction{}, program_counter,
      program_counter + (taken ? 8U : 4U), std::nullopt,
      ControlFlowObservation{kind, taken}};
}

[[nodiscard]] constexpr std::uint32_t encode_branch(
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

TEST(BranchPredictorTest, RejectsEveryInvalidConfiguration) {
  struct InvalidCase {
    BranchPredictorConfiguration configuration;
    BranchPredictorConfigurationErrorCode expected;
  };
  constexpr std::array cases{
      InvalidCase{
          BranchPredictorConfiguration{
              static_cast<BranchPredictionStrategy>(0xFFU), 0U,
              TwoBitCounterState::WeaklyNotTaken},
          BranchPredictorConfigurationErrorCode::UnknownStrategy},
      InvalidCase{
          BranchPredictorConfiguration{
              BranchPredictionStrategy::AlwaysTaken, 1U,
              TwoBitCounterState::WeaklyNotTaken},
          BranchPredictorConfigurationErrorCode::StaticStrategyHasTable},
      InvalidCase{
          BranchPredictorConfiguration{
              BranchPredictionStrategy::BimodalTwoBit, 0U,
              TwoBitCounterState::WeaklyNotTaken},
          BranchPredictorConfigurationErrorCode::MissingTableEntries},
      InvalidCase{
          BranchPredictorConfiguration{
              BranchPredictionStrategy::BimodalTwoBit, 3U,
              TwoBitCounterState::WeaklyNotTaken},
          BranchPredictorConfigurationErrorCode::
              TableEntriesNotPowerOfTwo},
      InvalidCase{
          BranchPredictorConfiguration{
              BranchPredictionStrategy::BimodalTwoBit, 4U,
              static_cast<TwoBitCounterState>(4U)},
          BranchPredictorConfigurationErrorCode::InvalidInitialCounter},
  };

  for (const InvalidCase& test_case : cases) {
    try {
      (void)BranchPredictorModel(test_case.configuration);
      FAIL() << "expected invalid branch predictor configuration";
    } catch (const BranchPredictorConfigurationError& error) {
      EXPECT_EQ(error.code(), test_case.expected);
      EXPECT_EQ(
          error.what(),
          branch_predictor_configuration_error_message(test_case.expected));
    }
  }
}

TEST(BranchPredictorTest, AlwaysNotTakenTracksEveryOutcomeCounter) {
  BranchPredictorModel predictor(kAlwaysNotTaken);

  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, false));
  predictor.observe(make_control_flow_observation(
      0x1004U, ControlFlowKind::ConditionalBranch, true));

  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.predictions, 2U);
  EXPECT_EQ(statistics.correct, 1U);
  EXPECT_EQ(statistics.incorrect, 1U);
  EXPECT_EQ(statistics.predicted_taken, 0U);
  EXPECT_EQ(statistics.predicted_not_taken, 2U);
  EXPECT_EQ(statistics.actual_taken, 1U);
  EXPECT_EQ(statistics.actual_not_taken, 1U);
}

TEST(BranchPredictorTest, AlwaysTakenPredictsWithoutDynamicState) {
  BranchPredictorModel predictor(kAlwaysTaken);

  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, true));
  predictor.observe(make_control_flow_observation(
      0x1004U, ControlFlowKind::ConditionalBranch, true));
  predictor.observe(make_control_flow_observation(
      0x1008U, ControlFlowKind::ConditionalBranch, false));

  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.predictions, 3U);
  EXPECT_EQ(statistics.correct, 2U);
  EXPECT_EQ(statistics.incorrect, 1U);
  EXPECT_EQ(statistics.predicted_taken, 3U);
  EXPECT_EQ(statistics.predicted_not_taken, 0U);
}

TEST(BranchPredictorTest, IgnoresNonbranchesAndUnconditionalJumps) {
  BranchPredictorModel predictor(kAlwaysTaken);
  predictor.observe(ExecutionObservation{
      DecodedInstruction{}, 0x1000U, 0x1004U, std::nullopt,
      std::nullopt});
  predictor.observe(make_control_flow_observation(
      0x1004U, ControlFlowKind::DirectJump, true));
  predictor.observe(make_control_flow_observation(
      0x1008U, ControlFlowKind::IndirectJump, true));

  EXPECT_EQ(predictor.statistics().predictions, 0U);
}

TEST(BranchPredictorTest, BimodalCountersPredictBeforeSaturatingUpdate) {
  const BranchPredictorConfiguration configuration{
      BranchPredictionStrategy::BimodalTwoBit, 1U,
      TwoBitCounterState::WeaklyNotTaken};
  BranchPredictorModel predictor(configuration);
  constexpr std::array outcomes{true, true, false, false, false, false};

  for (const bool taken : outcomes) {
    predictor.observe(make_control_flow_observation(
        0x1000U, ControlFlowKind::ConditionalBranch, taken));
  }

  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.predictions, 6U);
  EXPECT_EQ(statistics.correct, 3U);
  EXPECT_EQ(statistics.incorrect, 3U);
  EXPECT_EQ(statistics.predicted_taken, 3U);
  EXPECT_EQ(statistics.predicted_not_taken, 3U);
  EXPECT_EQ(statistics.actual_taken, 2U);
  EXPECT_EQ(statistics.actual_not_taken, 4U);
}

TEST(BranchPredictorTest, BimodalTableIndexesPcWordsIndependently) {
  const BranchPredictorConfiguration configuration{
      BranchPredictionStrategy::BimodalTwoBit, 2U,
      TwoBitCounterState::WeaklyNotTaken};
  BranchPredictorModel predictor(configuration);

  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, true));
  predictor.observe(make_control_flow_observation(
      0x1004U, ControlFlowKind::ConditionalBranch, false));
  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, true));
  predictor.observe(make_control_flow_observation(
      0x1004U, ControlFlowKind::ConditionalBranch, false));

  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.correct, 3U);
  EXPECT_EQ(statistics.incorrect, 1U);
  EXPECT_EQ(statistics.predicted_taken, 1U);
  EXPECT_EQ(statistics.predicted_not_taken, 3U);
}

TEST(BranchPredictorTest, ResetRestoresInitialTableAndZeroCounters) {
  const BranchPredictorConfiguration configuration{
      BranchPredictionStrategy::BimodalTwoBit, 4U,
      TwoBitCounterState::WeaklyTaken};
  BranchPredictorModel predictor(configuration);
  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, false));

  predictor.reset();
  predictor.observe(make_control_flow_observation(
      0x1000U, ControlFlowKind::ConditionalBranch, true));

  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.predictions, 1U);
  EXPECT_EQ(statistics.correct, 1U);
  EXPECT_EQ(statistics.incorrect, 0U);
  EXPECT_EQ(predictor.configuration().table_entries, 4U);
}

TEST(BranchPredictorTest,
     ConsumesResolvedEngineBranchWithoutChangingExecution) {
  constexpr std::uint32_t kBaseAddress = 0x1000U;
  CpuState state;
  Memory memory(kBaseAddress, 8U);
  state.set_program_counter(kBaseAddress);
  state.write_register(1U, 7U);
  state.write_register(2U, 7U);
  memory.write32(kBaseAddress, encode_branch(0U, 1U, 2U, 4));
  BranchPredictorModel predictor(kAlwaysNotTaken);
  ExecutionEngine engine(state, memory, &predictor);

  const StepResult result = engine.step();

  ASSERT_TRUE(std::holds_alternative<StepCompleted>(result));
  EXPECT_EQ(state.program_counter(), kBaseAddress + 4U);
  const BranchPredictionStatistics statistics = predictor.statistics();
  EXPECT_EQ(statistics.predictions, 1U);
  EXPECT_EQ(statistics.correct, 0U);
  EXPECT_EQ(statistics.actual_taken, 1U);
}

}  // namespace
}  // namespace rvemu

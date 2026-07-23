#include "rvemu/branch_predictor.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace rvemu {
namespace {

[[nodiscard]] constexpr bool is_power_of_two(
    const std::size_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] constexpr bool is_valid_counter(
    const TwoBitCounterState counter) noexcept {
  return static_cast<std::uint8_t>(counter) <=
         static_cast<std::uint8_t>(TwoBitCounterState::StronglyTaken);
}

[[nodiscard]] std::optional<BranchPredictorConfigurationErrorCode>
validate_configuration(const BranchPredictorConfiguration& configuration) {
  if (!is_valid_counter(configuration.initial_counter)) {
    return BranchPredictorConfigurationErrorCode::InvalidInitialCounter;
  }

  switch (configuration.strategy) {
    case BranchPredictionStrategy::AlwaysNotTaken:
    case BranchPredictionStrategy::AlwaysTaken:
      if (configuration.table_entries != 0U) {
        return BranchPredictorConfigurationErrorCode::StaticStrategyHasTable;
      }
      return std::nullopt;
    case BranchPredictionStrategy::BimodalTwoBit:
      if (configuration.table_entries == 0U) {
        return BranchPredictorConfigurationErrorCode::MissingTableEntries;
      }
      if (!is_power_of_two(configuration.table_entries)) {
        return BranchPredictorConfigurationErrorCode::
            TableEntriesNotPowerOfTwo;
      }
      return std::nullopt;
  }
  return BranchPredictorConfigurationErrorCode::UnknownStrategy;
}

[[nodiscard]] BranchPredictorConfiguration checked_configuration(
    const BranchPredictorConfiguration configuration) {
  const auto error = validate_configuration(configuration);
  if (error.has_value()) {
    throw BranchPredictorConfigurationError(*error);
  }
  return configuration;
}

[[nodiscard]] std::size_t counter_count(
    const BranchPredictorConfiguration& configuration) noexcept {
  return configuration.strategy ==
                 BranchPredictionStrategy::BimodalTwoBit
             ? configuration.table_entries
             : 0U;
}

}  // namespace

std::string_view branch_predictor_configuration_error_message(
    const BranchPredictorConfigurationErrorCode code) noexcept {
  switch (code) {
    case BranchPredictorConfigurationErrorCode::UnknownStrategy:
      return "unknown branch prediction strategy";
    case BranchPredictorConfigurationErrorCode::StaticStrategyHasTable:
      return "static branch predictors must not configure table entries";
    case BranchPredictorConfigurationErrorCode::MissingTableEntries:
      return "bimodal branch predictor table must contain entries";
    case BranchPredictorConfigurationErrorCode::TableEntriesNotPowerOfTwo:
      return "bimodal branch predictor table size must be a power of two";
    case BranchPredictorConfigurationErrorCode::InvalidInitialCounter:
      return "invalid initial two-bit branch predictor counter";
  }
  return "unknown branch predictor configuration error";
}

BranchPredictorConfigurationError::BranchPredictorConfigurationError(
    const BranchPredictorConfigurationErrorCode code)
    : std::invalid_argument(
          std::string(branch_predictor_configuration_error_message(code))),
      code_(code) {}

BranchPredictorConfigurationErrorCode
BranchPredictorConfigurationError::code() const noexcept {
  return code_;
}

BranchPredictorModel::BranchPredictorModel(
    const BranchPredictorConfiguration configuration)
    : configuration_(checked_configuration(configuration)),
      counters_(counter_count(configuration),
                static_cast<std::uint8_t>(configuration.initial_counter)),
      statistics_{0U, 0U, 0U, 0U, 0U, 0U, 0U} {}

void BranchPredictorModel::observe(
    const ExecutionObservation& observation) noexcept {
  if (!observation.control_flow.has_value() ||
      observation.control_flow->kind !=
          ControlFlowKind::ConditionalBranch) {
    return;
  }

  const bool predicted = predict(observation.program_counter);
  const bool actual = observation.control_flow->taken;
  ++statistics_.predictions;
  if (predicted) {
    ++statistics_.predicted_taken;
  } else {
    ++statistics_.predicted_not_taken;
  }
  if (actual) {
    ++statistics_.actual_taken;
  } else {
    ++statistics_.actual_not_taken;
  }
  if (predicted == actual) {
    ++statistics_.correct;
  } else {
    ++statistics_.incorrect;
  }

  update(observation.program_counter, actual);
}

void BranchPredictorModel::reset() noexcept {
  std::ranges::fill(
      counters_,
      static_cast<std::uint8_t>(configuration_.initial_counter));
  statistics_ = BranchPredictionStatistics{0U, 0U, 0U, 0U, 0U, 0U, 0U};
}

const BranchPredictorConfiguration&
BranchPredictorModel::configuration() const noexcept {
  return configuration_;
}

BranchPredictionStatistics BranchPredictorModel::statistics() const noexcept {
  return statistics_;
}

bool BranchPredictorModel::predict(
    const std::uint32_t program_counter) const noexcept {
  switch (configuration_.strategy) {
    case BranchPredictionStrategy::AlwaysNotTaken:
      return false;
    case BranchPredictionStrategy::AlwaysTaken:
      return true;
    case BranchPredictionStrategy::BimodalTwoBit:
      return counters_[table_index(program_counter)] >=
             static_cast<std::uint8_t>(TwoBitCounterState::WeaklyTaken);
  }
  return false;
}

void BranchPredictorModel::update(const std::uint32_t program_counter,
                                  const bool taken) noexcept {
  if (configuration_.strategy !=
      BranchPredictionStrategy::BimodalTwoBit) {
    return;
  }

  std::uint8_t& counter = counters_[table_index(program_counter)];
  if (taken &&
      counter <
          static_cast<std::uint8_t>(TwoBitCounterState::StronglyTaken)) {
    ++counter;
  } else if (!taken &&
             counter >
                 static_cast<std::uint8_t>(
                     TwoBitCounterState::StronglyNotTaken)) {
    --counter;
  }
}

std::size_t BranchPredictorModel::table_index(
    const std::uint32_t program_counter) const noexcept {
  const std::size_t word_address =
      static_cast<std::size_t>(program_counter >> 2U);
  return word_address & (configuration_.table_entries - 1U);
}

}  // namespace rvemu

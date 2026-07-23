#include "rvemu/cycle_estimator.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace rvemu {
namespace {

[[nodiscard]] std::optional<std::uint64_t> checked_multiply(
    const std::uint64_t left, const std::uint64_t right) noexcept {
  if (left != 0U &&
      right > std::numeric_limits<std::uint64_t>::max() / left) {
    return std::nullopt;
  }
  return left * right;
}

[[nodiscard]] bool checked_add(std::uint64_t& total,
                               const std::uint64_t value) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - total) {
    return false;
  }
  total += value;
  return true;
}

}  // namespace

std::string_view cycle_cost_configuration_error_message(
    const CycleCostConfigurationErrorCode code) noexcept {
  switch (code) {
    case CycleCostConfigurationErrorCode::ZeroBaseInstructionCycles:
      return "base instruction cycle cost must be positive";
  }
  return "unknown cycle cost configuration error";
}

CycleCostConfigurationError::CycleCostConfigurationError(
    const CycleCostConfigurationErrorCode code)
    : std::invalid_argument(
          std::string(cycle_cost_configuration_error_message(code))),
      code_(code) {}

CycleCostConfigurationErrorCode CycleCostConfigurationError::code()
    const noexcept {
  return code_;
}

std::string_view cycle_estimate_error_message(
    const CycleEstimateErrorCode code) noexcept {
  switch (code) {
    case CycleEstimateErrorCode::BaseInstructionCostOverflow:
      return "base instruction cycle cost overflow";
    case CycleEstimateErrorCode::InstructionCacheMissCostOverflow:
      return "instruction-cache miss cycle cost overflow";
    case CycleEstimateErrorCode::DataCacheMissCostOverflow:
      return "data-cache miss cycle cost overflow";
    case CycleEstimateErrorCode::BranchMispredictionCostOverflow:
      return "branch-misprediction cycle cost overflow";
    case CycleEstimateErrorCode::TotalOverflow:
      return "total estimated cycle count overflow";
  }
  return "unknown cycle estimate error";
}

CycleEstimator::CycleEstimator(
    const CycleCostConfiguration configuration)
    : configuration_(configuration) {
  if (configuration.base_instruction_cycles == 0U) {
    throw CycleCostConfigurationError(
        CycleCostConfigurationErrorCode::ZeroBaseInstructionCycles);
  }
}

const CycleCostConfiguration& CycleEstimator::configuration() const noexcept {
  return configuration_;
}

CycleEstimateResult CycleEstimator::estimate(
    const CycleEventCounts event_counts) const noexcept {
  const std::optional<std::uint64_t> base_instruction_cycles =
      checked_multiply(event_counts.instructions,
                       configuration_.base_instruction_cycles);
  if (!base_instruction_cycles.has_value()) {
    return CycleEstimateFailure{
        CycleEstimateErrorCode::BaseInstructionCostOverflow};
  }

  const std::optional<std::uint64_t> instruction_cache_miss_cycles =
      checked_multiply(
          event_counts.instruction_cache_misses,
          configuration_.instruction_cache_miss_penalty_cycles);
  if (!instruction_cache_miss_cycles.has_value()) {
    return CycleEstimateFailure{
        CycleEstimateErrorCode::InstructionCacheMissCostOverflow};
  }

  const std::optional<std::uint64_t> data_cache_miss_cycles =
      checked_multiply(
          event_counts.data_cache_misses,
          configuration_.data_cache_miss_penalty_cycles);
  if (!data_cache_miss_cycles.has_value()) {
    return CycleEstimateFailure{
        CycleEstimateErrorCode::DataCacheMissCostOverflow};
  }

  const std::optional<std::uint64_t> branch_misprediction_cycles =
      checked_multiply(
          event_counts.branch_mispredictions,
          configuration_.branch_misprediction_penalty_cycles);
  if (!branch_misprediction_cycles.has_value()) {
    return CycleEstimateFailure{
        CycleEstimateErrorCode::BranchMispredictionCostOverflow};
  }

  std::uint64_t total_cycles = *base_instruction_cycles;
  if (!checked_add(total_cycles, *instruction_cache_miss_cycles) ||
      !checked_add(total_cycles, *data_cache_miss_cycles) ||
      !checked_add(total_cycles, *branch_misprediction_cycles)) {
    return CycleEstimateFailure{CycleEstimateErrorCode::TotalOverflow};
  }

  return CycleEstimate{
      *base_instruction_cycles,
      *instruction_cache_miss_cycles,
      *data_cache_miss_cycles,
      *branch_misprediction_cycles,
      total_cycles};
}

}  // namespace rvemu

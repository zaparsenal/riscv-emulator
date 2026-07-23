#pragma once

#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <variant>

namespace rvemu {

struct CycleCostConfiguration {
  std::uint64_t base_instruction_cycles{1U};
  std::uint64_t instruction_cache_miss_penalty_cycles{0U};
  std::uint64_t data_cache_miss_penalty_cycles{0U};
  std::uint64_t branch_misprediction_penalty_cycles{0U};
};

enum class CycleCostConfigurationErrorCode : std::uint8_t {
  ZeroBaseInstructionCycles,
};

[[nodiscard]] std::string_view cycle_cost_configuration_error_message(
    CycleCostConfigurationErrorCode code) noexcept;

class CycleCostConfigurationError final : public std::invalid_argument {
 public:
  explicit CycleCostConfigurationError(
      CycleCostConfigurationErrorCode code);

  [[nodiscard]] CycleCostConfigurationErrorCode code() const noexcept;

 private:
  CycleCostConfigurationErrorCode code_;
};

struct CycleEventCounts {
  std::uint64_t instructions;
  std::uint64_t instruction_cache_misses;
  std::uint64_t data_cache_misses;
  std::uint64_t branch_mispredictions;
};

enum class CycleEstimateErrorCode : std::uint8_t {
  BaseInstructionCostOverflow,
  InstructionCacheMissCostOverflow,
  DataCacheMissCostOverflow,
  BranchMispredictionCostOverflow,
  TotalOverflow,
};

[[nodiscard]] std::string_view cycle_estimate_error_message(
    CycleEstimateErrorCode code) noexcept;

struct CycleEstimateFailure {
  CycleEstimateErrorCode code;
};

struct CycleEstimate {
  std::uint64_t base_instruction_cycles;
  std::uint64_t instruction_cache_miss_cycles;
  std::uint64_t data_cache_miss_cycles;
  std::uint64_t branch_misprediction_cycles;
  std::uint64_t total_cycles;
};

using CycleEstimateResult =
    std::variant<CycleEstimate, CycleEstimateFailure>;

class CycleEstimator final {
 public:
  explicit CycleEstimator(CycleCostConfiguration configuration);

  [[nodiscard]] const CycleCostConfiguration& configuration() const noexcept;
  [[nodiscard]] CycleEstimateResult estimate(
      CycleEventCounts event_counts) const noexcept;

 private:
  CycleCostConfiguration configuration_;
};

}  // namespace rvemu

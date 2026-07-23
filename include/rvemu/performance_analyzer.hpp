#pragma once

#include <cstdint>

#include "rvemu/branch_predictor.hpp"
#include "rvemu/cache_model.hpp"
#include "rvemu/cycle_estimator.hpp"
#include "rvemu/execution_observation.hpp"

namespace rvemu {

struct PerformanceAnalyzerConfiguration {
  SplitCacheConfiguration cache;
  BranchPredictorConfiguration branch_predictor;
  CycleCostConfiguration cycle_costs{};
};

struct PerformanceAnalyzerStatistics {
  std::uint64_t instructions_observed;
  SplitCacheStatistics cache;
  BranchPredictionStatistics branch_predictor;
};

class PerformanceAnalyzer final : public ExecutionObserver {
 public:
  explicit PerformanceAnalyzer(
      PerformanceAnalyzerConfiguration configuration);

  void observe(const ExecutionObservation& observation) noexcept override;
  void reset() noexcept;

  [[nodiscard]] const PerformanceAnalyzerConfiguration& configuration() const
      noexcept;
  [[nodiscard]] PerformanceAnalyzerStatistics statistics() const noexcept;
  [[nodiscard]] CycleEstimateResult estimate_cycles() const noexcept;

 private:
  PerformanceAnalyzerConfiguration configuration_;
  SplitCacheModel cache_;
  BranchPredictorModel branch_predictor_;
  CycleEstimator cycle_estimator_;
  std::uint64_t instructions_observed_;
};

}  // namespace rvemu

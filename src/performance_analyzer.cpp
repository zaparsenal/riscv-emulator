#include "rvemu/performance_analyzer.hpp"

#include <cstdint>

namespace rvemu {

PerformanceAnalyzer::PerformanceAnalyzer(
    const PerformanceAnalyzerConfiguration configuration)
    : configuration_(configuration),
      cache_(configuration.cache),
      branch_predictor_(configuration.branch_predictor),
      cycle_estimator_(configuration.cycle_costs),
      instructions_observed_(0U) {}

void PerformanceAnalyzer::observe(
    const ExecutionObservation& observation) noexcept {
  cache_.observe(observation);
  branch_predictor_.observe(observation);
  ++instructions_observed_;
}

void PerformanceAnalyzer::reset() noexcept {
  cache_.reset();
  branch_predictor_.reset();
  instructions_observed_ = 0U;
}

const PerformanceAnalyzerConfiguration&
PerformanceAnalyzer::configuration() const noexcept {
  return configuration_;
}

PerformanceAnalyzerStatistics PerformanceAnalyzer::statistics() const
    noexcept {
  return PerformanceAnalyzerStatistics{
      instructions_observed_, cache_.statistics(),
      branch_predictor_.statistics()};
}

CycleEstimateResult PerformanceAnalyzer::estimate_cycles() const noexcept {
  const PerformanceAnalyzerStatistics current_statistics = statistics();
  return cycle_estimator_.estimate(CycleEventCounts{
      current_statistics.instructions_observed,
      current_statistics.cache.instruction.misses,
      current_statistics.cache.data.total.misses,
      current_statistics.branch_predictor.incorrect});
}

}  // namespace rvemu

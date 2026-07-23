#include "benchmark_workload.hpp"

#include <cstdint>
#include <variant>

#include <benchmark/benchmark.h>

namespace rvemu::benchmarking {
namespace {

class ScopedTimerPause final {
 public:
  explicit ScopedTimerPause(benchmark::State& state) : state_(state) {
    state_.PauseTiming();
  }

  ~ScopedTimerPause() { state_.ResumeTiming(); }

  ScopedTimerPause(const ScopedTimerPause&) = delete;
  ScopedTimerPause& operator=(const ScopedTimerPause&) = delete;

 private:
  benchmark::State& state_;
};

[[nodiscard]] double as_double(const std::uint64_t value) noexcept {
  return static_cast<double>(value);
}

[[nodiscard]] double percentage(const std::uint64_t numerator,
                                const std::uint64_t denominator) noexcept {
  if (denominator == 0U) {
    return 0.0;
  }
  return 100.0 * as_double(numerator) / as_double(denominator);
}

void add_counters(benchmark::State& benchmark_state,
                  const PerformanceAnalyzerStatistics& statistics,
                  const CycleEstimate& estimate) {
  benchmark_state.counters["guest_instructions"] =
      as_double(statistics.instructions_observed);
  benchmark_state.counters["guest_instructions_per_second"] =
      benchmark::Counter(
          as_double(kInstructionsPerIteration),
          benchmark::Counter::kIsIterationInvariantRate);

  benchmark_state.counters["icache_accesses"] =
      as_double(statistics.cache.instruction.accesses);
  benchmark_state.counters["icache_hits"] =
      as_double(statistics.cache.instruction.hits);
  benchmark_state.counters["icache_misses"] =
      as_double(statistics.cache.instruction.misses);
  benchmark_state.counters["icache_hit_percent"] =
      percentage(statistics.cache.instruction.hits,
                 statistics.cache.instruction.accesses);

  benchmark_state.counters["dcache_accesses"] =
      as_double(statistics.cache.data.total.accesses);
  benchmark_state.counters["dcache_hits"] =
      as_double(statistics.cache.data.total.hits);
  benchmark_state.counters["dcache_misses"] =
      as_double(statistics.cache.data.total.misses);
  benchmark_state.counters["dcache_loads"] =
      as_double(statistics.cache.data.loads.accesses);
  benchmark_state.counters["dcache_stores"] =
      as_double(statistics.cache.data.stores.accesses);
  benchmark_state.counters["dcache_hit_percent"] =
      percentage(statistics.cache.data.total.hits,
                 statistics.cache.data.total.accesses);

  benchmark_state.counters["branch_predictions"] =
      as_double(statistics.branch_predictor.predictions);
  benchmark_state.counters["branch_correct"] =
      as_double(statistics.branch_predictor.correct);
  benchmark_state.counters["branch_mispredictions"] =
      as_double(statistics.branch_predictor.incorrect);
  benchmark_state.counters["branch_accuracy_percent"] =
      percentage(statistics.branch_predictor.correct,
                 statistics.branch_predictor.predictions);

  benchmark_state.counters["estimated_cycles"] =
      as_double(estimate.total_cycles);
  benchmark_state.counters["estimated_cpi"] =
      as_double(estimate.total_cycles) /
      as_double(statistics.instructions_observed);
}

void run_workload(benchmark::State& benchmark_state,
                  const WorkloadKind kind) {
  BenchmarkWorkload workload(kind);
  PerformanceAnalyzerStatistics last_statistics{};
  CycleEstimate last_estimate{};
  bool completed = false;

  for (auto iteration : benchmark_state) {
    (void)iteration;
    completed = false;
    {
      ScopedTimerPause pause(benchmark_state);
      workload.reset();
    }

    const RunResult result =
        workload.run(kInstructionsPerIteration);

    {
      ScopedTimerPause pause(benchmark_state);
      const auto* limit =
          std::get_if<InstructionLimitReached>(&result);
      if (limit == nullptr ||
          limit->instructions_executed != kInstructionsPerIteration) {
        benchmark_state.SkipWithError(
            "benchmark workload did not reach its exact instruction limit");
        break;
      }

      last_statistics = workload.statistics();
      const CycleEstimateResult estimate_result =
          workload.estimate_cycles();
      const auto* estimate =
          std::get_if<CycleEstimate>(&estimate_result);
      if (estimate == nullptr) {
        benchmark_state.SkipWithError(
            "benchmark workload cycle estimate overflowed");
        break;
      }
      last_estimate = *estimate;
      completed = true;
    }
  }

  if (completed) {
    add_counters(benchmark_state, last_statistics, last_estimate);
  }
}

void BM_RV32IM(benchmark::State& benchmark_state,
               const WorkloadKind kind) {
  run_workload(benchmark_state, kind);
}

BENCHMARK_CAPTURE(BM_RV32IM, IntegerMix, WorkloadKind::IntegerMix);
BENCHMARK_CAPTURE(BM_RV32IM, MemoryStride, WorkloadKind::MemoryStride);
BENCHMARK_CAPTURE(BM_RV32IM, BranchPattern, WorkloadKind::BranchPattern);

}  // namespace
}  // namespace rvemu::benchmarking

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    return 1;
  }

  benchmark::AddCustomContext(
      "rvemu_workload_instructions", "65536");
  benchmark::AddCustomContext(
      "rvemu_cache_model", "split-4096B-64B-2way");
  benchmark::AddCustomContext(
      "rvemu_branch_predictor",
      "bimodal-256-entry-weakly-not-taken");
  benchmark::AddCustomContext(
      "rvemu_cycle_costs",
      "base=1,icache_miss=10,dcache_miss=20,branch_miss=5");

  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}

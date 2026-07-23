#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "rvemu/cpu_state.hpp"
#include "rvemu/execution_engine.hpp"
#include "rvemu/memory.hpp"
#include "rvemu/performance_analyzer.hpp"

namespace rvemu::benchmarking {

inline constexpr std::uint32_t kMemoryBaseAddress = 0x1000U;
inline constexpr std::size_t kMemorySizeBytes = 16U * 1024U;
inline constexpr std::uint32_t kDataBaseAddress = 0x2000U;
inline constexpr std::uint64_t kInstructionsPerIteration = 65'536U;

enum class WorkloadKind : std::uint8_t {
  IntegerMix,
  MemoryStride,
  BranchPattern,
};

[[nodiscard]] std::string_view workload_name(WorkloadKind kind) noexcept;
[[nodiscard]] PerformanceAnalyzerConfiguration
benchmark_performance_configuration() noexcept;

class BenchmarkWorkload final {
 public:
  explicit BenchmarkWorkload(WorkloadKind kind);

  void reset();
  [[nodiscard]] RunResult run(std::uint64_t instruction_limit);

  [[nodiscard]] WorkloadKind kind() const noexcept;
  [[nodiscard]] const CpuState& state() const noexcept;
  [[nodiscard]] const Memory& memory() const noexcept;
  [[nodiscard]] PerformanceAnalyzerStatistics statistics() const noexcept;
  [[nodiscard]] CycleEstimateResult estimate_cycles() const noexcept;

 private:
  WorkloadKind kind_;
  CpuState state_;
  Memory memory_;
  PerformanceAnalyzer analyzer_;
};

}  // namespace rvemu::benchmarking

#include "benchmark_workload.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

#include "rvemu/instruction.hpp"

namespace rvemu::benchmarking {
namespace {

constexpr std::uint8_t kMultiplyFunction7 = 0x01U;

[[nodiscard]] WorkloadKind validate_kind(const WorkloadKind kind) {
  switch (kind) {
    case WorkloadKind::IntegerMix:
    case WorkloadKind::MemoryStride:
    case WorkloadKind::BranchPattern:
      return kind;
  }
  throw std::invalid_argument("unknown benchmark workload kind");
}

[[nodiscard]] constexpr std::uint32_t encode_immediate_operation(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source, const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::OpImmediate);
}

[[nodiscard]] constexpr std::uint32_t encode_register_operation(
    const std::uint8_t destination, const std::uint8_t function3,
    const std::uint8_t source1, const std::uint8_t source2,
    const std::uint8_t function7 = 0U) noexcept {
  return (static_cast<std::uint32_t>(function7) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Op);
}

[[nodiscard]] constexpr std::uint32_t encode_load_word(
    const std::uint8_t destination, const std::uint8_t source,
    const std::int32_t immediate) noexcept {
  return ((static_cast<std::uint32_t>(immediate) & 0xFFFU) << 20U) |
         (static_cast<std::uint32_t>(source) << 15U) | (2U << 12U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Load);
}

[[nodiscard]] constexpr std::uint32_t encode_store_word(
    const std::uint8_t address_source, const std::uint8_t value_source,
    const std::int32_t immediate) noexcept {
  const std::uint32_t bits =
      static_cast<std::uint32_t>(immediate) & 0xFFFU;
  return ((bits >> 5U) << 25U) |
         (static_cast<std::uint32_t>(value_source) << 20U) |
         (static_cast<std::uint32_t>(address_source) << 15U) |
         (2U << 12U) | ((bits & 0x1FU) << 7U) |
         static_cast<std::uint8_t>(Opcode::Store);
}

[[nodiscard]] constexpr std::uint32_t encode_branch(
    const std::uint8_t function3, const std::uint8_t source1,
    const std::uint8_t source2, const std::int32_t immediate) noexcept {
  const std::uint32_t bits =
      static_cast<std::uint32_t>(immediate) & 0x1FFFU;
  return (((bits >> 12U) & 0x01U) << 31U) |
         (((bits >> 5U) & 0x3FU) << 25U) |
         (static_cast<std::uint32_t>(source2) << 20U) |
         (static_cast<std::uint32_t>(source1) << 15U) |
         (static_cast<std::uint32_t>(function3) << 12U) |
         (((bits >> 1U) & 0x0FU) << 8U) |
         (((bits >> 11U) & 0x01U) << 7U) |
         static_cast<std::uint8_t>(Opcode::Branch);
}

[[nodiscard]] constexpr std::uint32_t encode_jump(
    const std::int32_t immediate) noexcept {
  const std::uint32_t bits =
      static_cast<std::uint32_t>(immediate) & 0x1FFFFFU;
  return (((bits >> 20U) & 0x01U) << 31U) |
         (((bits >> 1U) & 0x3FFU) << 21U) |
         (((bits >> 11U) & 0x01U) << 20U) |
         (((bits >> 12U) & 0xFFU) << 12U) |
         static_cast<std::uint8_t>(Opcode::Jal);
}

[[nodiscard]] constexpr std::uint32_t encode_lui(
    const std::uint8_t destination,
    const std::uint32_t immediate) noexcept {
  return (immediate & 0xFFFFF000U) |
         (static_cast<std::uint32_t>(destination) << 7U) |
         static_cast<std::uint8_t>(Opcode::Lui);
}

constexpr std::array<std::uint32_t, 6U> kIntegerMixProgram{
    encode_immediate_operation(1U, 0U, 1U, 1),
    encode_immediate_operation(2U, 0U, 2U, 3),
    encode_register_operation(3U, 4U, 1U, 2U),
    encode_register_operation(
        4U, 0U, 3U, 2U, kMultiplyFunction7),
    encode_register_operation(5U, 0U, 5U, 4U),
    encode_jump(-20),
};

constexpr std::array<std::uint32_t, 7U> kMemoryStrideProgram{
    encode_load_word(3U, 1U, 0),
    encode_immediate_operation(3U, 0U, 3U, 1),
    encode_store_word(1U, 3U, 0),
    encode_immediate_operation(1U, 0U, 1U, 4),
    encode_branch(6U, 1U, 2U, -16),
    encode_lui(1U, kDataBaseAddress),
    encode_jump(-24),
};

constexpr std::array<std::uint32_t, 7U> kBranchPatternProgram{
    encode_immediate_operation(1U, 0U, 1U, 1),
    encode_immediate_operation(2U, 7U, 1U, 1),
    encode_branch(0U, 2U, 0U, 8),
    encode_immediate_operation(3U, 0U, 3U, 1),
    encode_branch(1U, 1U, 4U, -16),
    encode_immediate_operation(1U, 0U, 0U, 0),
    encode_jump(-24),
};

void load_program(Memory& memory,
                  const std::span<const std::uint32_t> program) {
  std::uint32_t address = kMemoryBaseAddress;
  for (const std::uint32_t instruction : program) {
    memory.write32(address, instruction);
    address += sizeof(instruction);
  }
}

}  // namespace

std::string_view workload_name(const WorkloadKind kind) noexcept {
  switch (kind) {
    case WorkloadKind::IntegerMix:
      return "IntegerMix";
    case WorkloadKind::MemoryStride:
      return "MemoryStride";
    case WorkloadKind::BranchPattern:
      return "BranchPattern";
  }
  return "Unknown";
}

PerformanceAnalyzerConfiguration benchmark_performance_configuration()
    noexcept {
  constexpr CacheConfiguration kCache{4096U, 64U, 2U};
  return PerformanceAnalyzerConfiguration{
      SplitCacheConfiguration{kCache, kCache},
      BranchPredictorConfiguration{
          BranchPredictionStrategy::BimodalTwoBit, 256U,
          TwoBitCounterState::WeaklyNotTaken},
      CycleCostConfiguration{1U, 10U, 20U, 5U}};
}

BenchmarkWorkload::BenchmarkWorkload(const WorkloadKind kind)
    : kind_(validate_kind(kind)),
      state_(),
      memory_(kMemoryBaseAddress, kMemorySizeBytes),
      analyzer_(benchmark_performance_configuration()) {
  reset();
}

void BenchmarkWorkload::reset() {
  state_.reset();
  memory_.clear();
  analyzer_.reset();
  state_.set_program_counter(kMemoryBaseAddress);

  switch (kind_) {
    case WorkloadKind::IntegerMix:
      load_program(memory_, kIntegerMixProgram);
      state_.write_register(1U, 1U);
      state_.write_register(2U, 3U);
      break;
    case WorkloadKind::MemoryStride:
      load_program(memory_, kMemoryStrideProgram);
      state_.write_register(1U, kDataBaseAddress);
      state_.write_register(2U, kDataBaseAddress + 1024U);
      break;
    case WorkloadKind::BranchPattern:
      load_program(memory_, kBranchPatternProgram);
      state_.write_register(4U, 256U);
      break;
  }
}

RunResult BenchmarkWorkload::run(const std::uint64_t instruction_limit) {
  ExecutionEngine engine(state_, memory_, &analyzer_);
  return engine.run(instruction_limit);
}

WorkloadKind BenchmarkWorkload::kind() const noexcept {
  return kind_;
}

const CpuState& BenchmarkWorkload::state() const noexcept {
  return state_;
}

const Memory& BenchmarkWorkload::memory() const noexcept {
  return memory_;
}

PerformanceAnalyzerStatistics BenchmarkWorkload::statistics() const noexcept {
  return analyzer_.statistics();
}

CycleEstimateResult BenchmarkWorkload::estimate_cycles() const noexcept {
  return analyzer_.estimate_cycles();
}

}  // namespace rvemu::benchmarking

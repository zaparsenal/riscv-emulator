#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "rvemu/execution_observation.hpp"

namespace rvemu {

enum class BranchPredictionStrategy : std::uint8_t {
  AlwaysNotTaken,
  AlwaysTaken,
  BimodalTwoBit,
};

enum class TwoBitCounterState : std::uint8_t {
  StronglyNotTaken = 0U,
  WeaklyNotTaken = 1U,
  WeaklyTaken = 2U,
  StronglyTaken = 3U,
};

struct BranchPredictorConfiguration {
  BranchPredictionStrategy strategy;
  std::size_t table_entries;
  TwoBitCounterState initial_counter;
};

enum class BranchPredictorConfigurationErrorCode : std::uint8_t {
  UnknownStrategy,
  StaticStrategyHasTable,
  MissingTableEntries,
  TableEntriesNotPowerOfTwo,
  InvalidInitialCounter,
};

[[nodiscard]] std::string_view branch_predictor_configuration_error_message(
    BranchPredictorConfigurationErrorCode code) noexcept;

class BranchPredictorConfigurationError final
    : public std::invalid_argument {
 public:
  explicit BranchPredictorConfigurationError(
      BranchPredictorConfigurationErrorCode code);

  [[nodiscard]] BranchPredictorConfigurationErrorCode code() const noexcept;

 private:
  BranchPredictorConfigurationErrorCode code_;
};

struct BranchPredictionStatistics {
  std::uint64_t predictions;
  std::uint64_t correct;
  std::uint64_t incorrect;
  std::uint64_t predicted_taken;
  std::uint64_t predicted_not_taken;
  std::uint64_t actual_taken;
  std::uint64_t actual_not_taken;
};

class BranchPredictorModel final : public ExecutionObserver {
 public:
  explicit BranchPredictorModel(
      BranchPredictorConfiguration configuration);

  void observe(const ExecutionObservation& observation) noexcept override;
  void reset() noexcept;

  [[nodiscard]] const BranchPredictorConfiguration& configuration() const
      noexcept;
  [[nodiscard]] BranchPredictionStatistics statistics() const noexcept;

 private:
  [[nodiscard]] bool predict(std::uint32_t program_counter) const noexcept;
  void update(std::uint32_t program_counter, bool taken) noexcept;
  [[nodiscard]] std::size_t table_index(
      std::uint32_t program_counter) const noexcept;

  BranchPredictorConfiguration configuration_;
  std::vector<std::uint8_t> counters_;
  BranchPredictionStatistics statistics_;
};

}  // namespace rvemu

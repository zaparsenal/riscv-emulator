#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "rvemu/execution_observation.hpp"

namespace rvemu {

struct CacheConfiguration {
  std::size_t capacity_bytes;
  std::size_t line_size_bytes;
  std::size_t associativity;
};

enum class CacheConfigurationErrorCode : std::uint8_t {
  ZeroCapacity,
  LineSizeTooSmall,
  LineSizeNotPowerOfTwo,
  ZeroAssociativity,
  GeometryOverflow,
  CapacityNotDivisible,
};

[[nodiscard]] std::string_view cache_configuration_error_message(
    CacheConfigurationErrorCode code) noexcept;

class CacheConfigurationError final : public std::invalid_argument {
 public:
  explicit CacheConfigurationError(CacheConfigurationErrorCode code);

  [[nodiscard]] CacheConfigurationErrorCode code() const noexcept;

 private:
  CacheConfigurationErrorCode code_;
};

enum class CacheAccessResult : std::uint8_t {
  Hit,
  Miss,
};

struct CacheStatistics {
  std::uint64_t accesses;
  std::uint64_t hits;
  std::uint64_t misses;
};

class SetAssociativeCache final {
 public:
  explicit SetAssociativeCache(CacheConfiguration configuration);

  [[nodiscard]] CacheAccessResult access(std::uint32_t address) noexcept;
  void reset() noexcept;

  [[nodiscard]] const CacheConfiguration& configuration() const noexcept;
  [[nodiscard]] CacheStatistics statistics() const noexcept;

 private:
  struct CacheLine {
    std::uint64_t tag;
    bool valid;
  };

  CacheConfiguration configuration_;
  std::size_t set_count_;
  std::vector<CacheLine> lines_;
  CacheStatistics statistics_;
};

struct SplitCacheConfiguration {
  CacheConfiguration instruction;
  CacheConfiguration data;
};

struct DataCacheStatistics {
  CacheStatistics total;
  CacheStatistics loads;
  CacheStatistics stores;
};

struct SplitCacheStatistics {
  CacheStatistics instruction;
  DataCacheStatistics data;
};

class SplitCacheModel final : public ExecutionObserver {
 public:
  explicit SplitCacheModel(SplitCacheConfiguration configuration);

  void observe(const ExecutionObservation& observation) noexcept override;
  void reset() noexcept;

  [[nodiscard]] const SplitCacheConfiguration& configuration() const noexcept;
  [[nodiscard]] SplitCacheStatistics statistics() const noexcept;

 private:
  SplitCacheConfiguration configuration_;
  SetAssociativeCache instruction_cache_;
  SetAssociativeCache data_cache_;
  CacheStatistics load_statistics_;
  CacheStatistics store_statistics_;
};

}  // namespace rvemu

#include "rvemu/cache_model.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace rvemu {
namespace {

[[nodiscard]] constexpr bool is_power_of_two(
    const std::size_t value) noexcept {
  return value != 0U && (value & (value - 1U)) == 0U;
}

[[nodiscard]] std::optional<CacheConfigurationErrorCode>
validate_configuration(
    const CacheConfiguration& configuration) {
  if (configuration.capacity_bytes == 0U) {
    return CacheConfigurationErrorCode::ZeroCapacity;
  }
  if (configuration.line_size_bytes < 4U) {
    return CacheConfigurationErrorCode::LineSizeTooSmall;
  }
  if (!is_power_of_two(configuration.line_size_bytes)) {
    return CacheConfigurationErrorCode::LineSizeNotPowerOfTwo;
  }
  if (configuration.associativity == 0U) {
    return CacheConfigurationErrorCode::ZeroAssociativity;
  }
  if (configuration.associativity >
      std::numeric_limits<std::size_t>::max() /
          configuration.line_size_bytes) {
    return CacheConfigurationErrorCode::GeometryOverflow;
  }

  const std::size_t set_capacity =
      configuration.line_size_bytes * configuration.associativity;
  if (configuration.capacity_bytes % set_capacity != 0U) {
    return CacheConfigurationErrorCode::CapacityNotDivisible;
  }
  return std::nullopt;
}

[[nodiscard]] std::size_t checked_set_count(
    const CacheConfiguration& configuration) {
  const std::optional<CacheConfigurationErrorCode> error =
      validate_configuration(configuration);
  if (error.has_value()) {
    throw CacheConfigurationError(*error);
  }

  return configuration.capacity_bytes /
         (configuration.line_size_bytes * configuration.associativity);
}

void record_access(CacheStatistics& statistics,
                   const CacheAccessResult result) noexcept {
  ++statistics.accesses;
  if (result == CacheAccessResult::Hit) {
    ++statistics.hits;
  } else {
    ++statistics.misses;
  }
}

}  // namespace

std::string_view cache_configuration_error_message(
    const CacheConfigurationErrorCode code) noexcept {
  switch (code) {
    case CacheConfigurationErrorCode::ZeroCapacity:
      return "cache capacity must be positive";
    case CacheConfigurationErrorCode::LineSizeTooSmall:
      return "cache line size must be at least four bytes";
    case CacheConfigurationErrorCode::LineSizeNotPowerOfTwo:
      return "cache line size must be a power of two";
    case CacheConfigurationErrorCode::ZeroAssociativity:
      return "cache associativity must be positive";
    case CacheConfigurationErrorCode::GeometryOverflow:
      return "cache line size and associativity overflow";
    case CacheConfigurationErrorCode::CapacityNotDivisible:
      return "cache capacity must be divisible by line size times associativity";
  }
  return "unknown cache configuration error";
}

CacheConfigurationError::CacheConfigurationError(
    const CacheConfigurationErrorCode code)
    : std::invalid_argument(
          std::string(cache_configuration_error_message(code))),
      code_(code) {}

CacheConfigurationErrorCode CacheConfigurationError::code() const noexcept {
  return code_;
}

SetAssociativeCache::SetAssociativeCache(
    const CacheConfiguration configuration)
    : configuration_(configuration),
      set_count_(checked_set_count(configuration)),
      lines_(configuration.capacity_bytes / configuration.line_size_bytes,
             CacheLine{0U, false}),
      statistics_{0U, 0U, 0U} {}

CacheAccessResult SetAssociativeCache::access(
    const std::uint32_t address) noexcept {
  const std::uint64_t block_number =
      address / static_cast<std::uint64_t>(configuration_.line_size_bytes);
  const std::size_t set_index =
      static_cast<std::size_t>(block_number % set_count_);
  const std::uint64_t tag = block_number / set_count_;
  const std::size_t set_begin = set_index * configuration_.associativity;

  std::size_t selected_way = configuration_.associativity;
  bool hit = false;
  for (std::size_t way = 0U; way < configuration_.associativity; ++way) {
    const CacheLine& line = lines_[set_begin + way];
    if (line.valid && line.tag == tag) {
      selected_way = way;
      hit = true;
      break;
    }
    if (!line.valid && selected_way == configuration_.associativity) {
      selected_way = way;
    }
  }

  if (selected_way == configuration_.associativity) {
    selected_way = configuration_.associativity - 1U;
  }

  const CacheLine selected_line =
      hit ? lines_[set_begin + selected_way] : CacheLine{tag, true};
  for (std::size_t way = selected_way; way > 0U; --way) {
    lines_[set_begin + way] = lines_[set_begin + way - 1U];
  }
  lines_[set_begin] = selected_line;

  const CacheAccessResult result =
      hit ? CacheAccessResult::Hit : CacheAccessResult::Miss;
  record_access(statistics_, result);
  return result;
}

void SetAssociativeCache::reset() noexcept {
  std::ranges::fill(lines_, CacheLine{0U, false});
  statistics_ = CacheStatistics{0U, 0U, 0U};
}

const CacheConfiguration& SetAssociativeCache::configuration() const noexcept {
  return configuration_;
}

CacheStatistics SetAssociativeCache::statistics() const noexcept {
  return statistics_;
}

SplitCacheModel::SplitCacheModel(
    const SplitCacheConfiguration configuration)
    : configuration_(configuration),
      instruction_cache_(configuration.instruction),
      data_cache_(configuration.data),
      load_statistics_{0U, 0U, 0U},
      store_statistics_{0U, 0U, 0U} {}

void SplitCacheModel::observe(
    const ExecutionObservation& observation) noexcept {
  (void)instruction_cache_.access(observation.program_counter);
  if (!observation.data_memory_access.has_value()) {
    return;
  }

  const CacheAccessResult result =
      data_cache_.access(observation.data_memory_access->address);
  if (observation.data_memory_access->kind ==
      DataMemoryAccessKind::Load) {
    record_access(load_statistics_, result);
  } else {
    record_access(store_statistics_, result);
  }
}

void SplitCacheModel::reset() noexcept {
  instruction_cache_.reset();
  data_cache_.reset();
  load_statistics_ = CacheStatistics{0U, 0U, 0U};
  store_statistics_ = CacheStatistics{0U, 0U, 0U};
}

const SplitCacheConfiguration& SplitCacheModel::configuration() const
    noexcept {
  return configuration_;
}

SplitCacheStatistics SplitCacheModel::statistics() const noexcept {
  return SplitCacheStatistics{
      instruction_cache_.statistics(),
      DataCacheStatistics{
          data_cache_.statistics(), load_statistics_, store_statistics_}};
}

}  // namespace rvemu

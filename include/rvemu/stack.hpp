#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <variant>

#include "rvemu/cpu_state.hpp"
#include "rvemu/memory.hpp"

namespace rvemu {

inline constexpr std::size_t kDefaultFreestandingStackSize =
    1U * 1024U * 1024U;

enum class StackInitializationErrorCode {
  InvalidOccupiedRange,
  InvalidStackSize,
  StackDoesNotFit,
  StackOverlapsLoadedImage,
};

struct StackInitializationFailure {
  StackInitializationErrorCode code;
};

struct StackInitializationSuccess {
  std::uint32_t initial_stack_pointer;
  std::uint64_t stack_bottom;
  std::uint64_t stack_top_exclusive;
  std::uint64_t stack_size_bytes;
};

using StackInitializationResult =
    std::variant<StackInitializationSuccess, StackInitializationFailure>;

[[nodiscard]] std::string_view stack_initialization_error_message(
    StackInitializationErrorCode code) noexcept;

[[nodiscard]] StackInitializationResult initialize_freestanding_stack(
    CpuState& state, const Memory& memory,
    std::uint64_t occupied_address_begin,
    std::uint64_t occupied_address_end_exclusive,
    std::size_t requested_stack_size =
        kDefaultFreestandingStackSize) noexcept;

}  // namespace rvemu

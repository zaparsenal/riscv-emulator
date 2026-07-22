#include "rvemu/stack.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace rvemu {
namespace {

constexpr std::size_t kStackPointerRegister = 2U;
constexpr std::uint64_t kStackAlignment = 16U;
constexpr std::uint64_t kAddressSpaceSize = 0x1'0000'0000ULL;

[[nodiscard]] StackInitializationResult failure(
    const StackInitializationErrorCode code) noexcept {
  return StackInitializationFailure{code};
}

}  // namespace

std::string_view stack_initialization_error_message(
    const StackInitializationErrorCode code) noexcept {
  switch (code) {
    case StackInitializationErrorCode::InvalidOccupiedRange:
      return "the loaded image range is outside mapped memory";
    case StackInitializationErrorCode::InvalidStackSize:
      return "the requested stack size is zero or cannot be aligned";
    case StackInitializationErrorCode::StackDoesNotFit:
      return "the requested stack does not fit in mapped memory";
    case StackInitializationErrorCode::StackOverlapsLoadedImage:
      return "the requested stack overlaps the loaded image";
  }
  return "unknown stack initialization error";
}

StackInitializationResult initialize_freestanding_stack(
    CpuState& state, const Memory& memory,
    const std::uint64_t occupied_address_begin,
    const std::uint64_t occupied_address_end_exclusive,
    const std::size_t requested_stack_size) noexcept {
  static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));

  const std::uint64_t memory_base = memory.base_address();
  const std::uint64_t memory_end = memory.end_address_exclusive();
  if (occupied_address_begin < memory_base ||
      occupied_address_begin > occupied_address_end_exclusive ||
      occupied_address_end_exclusive > memory_end) {
    return failure(StackInitializationErrorCode::InvalidOccupiedRange);
  }
  if (requested_stack_size == 0U) {
    return failure(StackInitializationErrorCode::InvalidStackSize);
  }

  const auto requested_size =
      static_cast<std::uint64_t>(requested_stack_size);
  if (requested_size > kAddressSpaceSize ||
      requested_size >
          std::numeric_limits<std::uint64_t>::max() -
              (kStackAlignment - 1U)) {
    return failure(StackInitializationErrorCode::InvalidStackSize);
  }
  const std::uint64_t stack_size =
      (requested_size + kStackAlignment - 1U) & ~(kStackAlignment - 1U);
  const std::uint64_t stack_top =
      memory_end & ~(kStackAlignment - 1U);
  if (stack_top < memory_base || stack_size > stack_top - memory_base) {
    return failure(StackInitializationErrorCode::StackDoesNotFit);
  }

  const std::uint64_t stack_bottom = stack_top - stack_size;
  if (occupied_address_end_exclusive > stack_bottom) {
    return failure(StackInitializationErrorCode::StackOverlapsLoadedImage);
  }

  const auto stack_pointer =
      static_cast<std::uint32_t>(stack_top & 0xFFFFFFFFULL);
  state.write_register(kStackPointerRegister, stack_pointer);
  return StackInitializationSuccess{stack_pointer, stack_bottom, stack_top,
                                    stack_size};
}

}  // namespace rvemu

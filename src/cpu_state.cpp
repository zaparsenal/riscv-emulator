#include "rvemu/cpu_state.hpp"

#include <algorithm>
#include <stdexcept>

namespace rvemu {

CpuState::Word CpuState::read_register(const std::size_t index) const {
  validate_register_index(index);
  if (index == kZeroRegister) {
    return 0U;
  }
  return registers_[index];
}

void CpuState::write_register(const std::size_t index, const Word value) {
  validate_register_index(index);
  if (index != kZeroRegister) {
    registers_[index] = value;
  }
}

CpuState::Word CpuState::program_counter() const noexcept {
  return program_counter_;
}

void CpuState::set_program_counter(const Word value) noexcept {
  program_counter_ = value;
}

void CpuState::advance_program_counter(const Word byte_count) noexcept {
  program_counter_ += byte_count;
}

void CpuState::reset() noexcept {
  std::ranges::fill(registers_, 0U);
  program_counter_ = 0U;
}

void CpuState::validate_register_index(const std::size_t index) {
  if (index >= kRegisterCount) {
    throw std::out_of_range("RISC-V register index must be in [0, 31]");
  }
}

}  // namespace rvemu

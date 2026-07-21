#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace rvemu {

class CpuState final {
 public:
  using Word = std::uint32_t;

  static constexpr std::size_t kRegisterCount = 32;
  static constexpr std::size_t kZeroRegister = 0;

  [[nodiscard]] Word read_register(std::size_t index) const;
  void write_register(std::size_t index, Word value);

  [[nodiscard]] Word program_counter() const noexcept;
  void set_program_counter(Word value) noexcept;
  void advance_program_counter(Word byte_count = 4U) noexcept;

  void reset() noexcept;

 private:
  static void validate_register_index(std::size_t index);

  std::array<Word, kRegisterCount> registers_{};
  Word program_counter_{0U};
};

}  // namespace rvemu

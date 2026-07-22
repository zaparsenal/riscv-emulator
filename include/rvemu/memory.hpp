#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace rvemu {

enum class MemoryOperation {
  Read,
  Write,
};

class MemoryAccessError : public std::runtime_error {
 public:
  MemoryAccessError(std::string message, std::uint32_t address,
                    std::size_t width, MemoryOperation operation);

  [[nodiscard]] std::uint32_t address() const noexcept;
  [[nodiscard]] std::size_t width() const noexcept;
  [[nodiscard]] MemoryOperation operation() const noexcept;

 private:
  std::uint32_t address_;
  std::size_t width_;
  MemoryOperation operation_;
};

class MemoryOutOfBoundsError final : public MemoryAccessError {
 public:
  MemoryOutOfBoundsError(std::uint32_t address, std::size_t width,
                         MemoryOperation operation);
};

class MemoryMisalignmentError final : public MemoryAccessError {
 public:
  MemoryMisalignmentError(std::uint32_t address, std::size_t width,
                          MemoryOperation operation);
};

class Memory final {
 public:
  using Address = std::uint32_t;
  using Byte = std::uint8_t;

  explicit Memory(std::size_t size_bytes);
  Memory(Address base_address, std::size_t size_bytes);

  [[nodiscard]] Address base_address() const noexcept;
  [[nodiscard]] std::uint64_t end_address_exclusive() const noexcept;
  [[nodiscard]] std::size_t size_bytes() const noexcept;

  [[nodiscard]] Byte read8(Address address) const;
  [[nodiscard]] std::uint16_t read16(Address address) const;
  [[nodiscard]] std::uint32_t read32(Address address) const;
  [[nodiscard]] std::span<const Byte> read_span(Address address,
                                                std::size_t width) const;

  void write8(Address address, Byte value);
  void write16(Address address, std::uint16_t value);
  void write32(Address address, std::uint32_t value);

  void clear() noexcept;

 private:
  [[nodiscard]] std::size_t checked_offset(Address address, std::size_t width,
                                           MemoryOperation operation) const;
  static void check_alignment(Address address, std::size_t width,
                              MemoryOperation operation);

  Address base_address_;
  std::vector<Byte> bytes_;
};

}  // namespace rvemu

#include "rvemu/memory.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <utility>

namespace rvemu {
namespace {

constexpr std::uint64_t kAddressSpaceSize =
    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;

[[nodiscard]] const char* operation_name(const MemoryOperation operation) {
  return operation == MemoryOperation::Read ? "read" : "write";
}

[[nodiscard]] std::string access_error_message(
    const char* reason, const std::uint32_t address, const std::size_t width,
    const MemoryOperation operation) {
  std::ostringstream message;
  message << "memory " << operation_name(operation) << " " << reason
          << ": address=0x" << std::hex << address << std::dec
          << ", width=" << width;
  return message.str();
}

[[nodiscard]] std::size_t validated_region_size(
    const std::uint32_t base_address, const std::size_t size_bytes) {
  const auto available_address_space =
      kAddressSpaceSize - static_cast<std::uint64_t>(base_address);
  if (size_bytes > available_address_space) {
    throw std::invalid_argument(
        "memory region extends beyond the 32-bit address space");
  }
  return size_bytes;
}

}  // namespace

MemoryAccessError::MemoryAccessError(std::string message,
                                     const std::uint32_t address,
                                     const std::size_t width,
                                     const MemoryOperation operation)
    : std::runtime_error(std::move(message)),
      address_(address),
      width_(width),
      operation_(operation) {}

std::uint32_t MemoryAccessError::address() const noexcept { return address_; }

std::size_t MemoryAccessError::width() const noexcept { return width_; }

MemoryOperation MemoryAccessError::operation() const noexcept {
  return operation_;
}

MemoryOutOfBoundsError::MemoryOutOfBoundsError(
    const std::uint32_t address, const std::size_t width,
    const MemoryOperation operation)
    : MemoryAccessError(access_error_message("is out of bounds", address, width,
                                             operation),
                        address, width, operation) {}

MemoryMisalignmentError::MemoryMisalignmentError(
    const std::uint32_t address, const std::size_t width,
    const MemoryOperation operation)
    : MemoryAccessError(access_error_message("is misaligned", address, width,
                                             operation),
                        address, width, operation) {}

Memory::Memory(const std::size_t size_bytes) : Memory(0U, size_bytes) {}

Memory::Memory(const Address base_address, const std::size_t size_bytes)
    : base_address_(base_address),
      bytes_(validated_region_size(base_address, size_bytes), 0U) {}

Memory::Address Memory::base_address() const noexcept { return base_address_; }

std::uint64_t Memory::end_address_exclusive() const noexcept {
  return static_cast<std::uint64_t>(base_address_) + bytes_.size();
}

std::size_t Memory::size_bytes() const noexcept { return bytes_.size(); }

Memory::Byte Memory::read8(const Address address) const {
  return bytes_[checked_offset(address, sizeof(Byte), MemoryOperation::Read)];
}

std::uint16_t Memory::read16(const Address address) const {
  check_alignment(address, sizeof(std::uint16_t), MemoryOperation::Read);
  const auto offset =
      checked_offset(address, sizeof(std::uint16_t), MemoryOperation::Read);
  return static_cast<std::uint16_t>(bytes_[offset]) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(bytes_[offset + 1U]) << 8U);
}

std::uint32_t Memory::read32(const Address address) const {
  check_alignment(address, sizeof(std::uint32_t), MemoryOperation::Read);
  const auto offset =
      checked_offset(address, sizeof(std::uint32_t), MemoryOperation::Read);
  return static_cast<std::uint32_t>(bytes_[offset]) |
         (static_cast<std::uint32_t>(bytes_[offset + 1U]) << 8U) |
         (static_cast<std::uint32_t>(bytes_[offset + 2U]) << 16U) |
         (static_cast<std::uint32_t>(bytes_[offset + 3U]) << 24U);
}

std::span<const Memory::Byte> Memory::read_span(
    const Address address, const std::size_t width) const {
  const auto offset = checked_offset(address, width, MemoryOperation::Read);
  return std::span<const Byte>(bytes_).subspan(offset, width);
}

void Memory::write8(const Address address, const Byte value) {
  bytes_[checked_offset(address, sizeof(Byte), MemoryOperation::Write)] = value;
}

void Memory::write16(const Address address, const std::uint16_t value) {
  check_alignment(address, sizeof(value), MemoryOperation::Write);
  const auto offset =
      checked_offset(address, sizeof(value), MemoryOperation::Write);
  bytes_[offset] = static_cast<Byte>(value & 0xFFU);
  bytes_[offset + 1U] = static_cast<Byte>((value >> 8U) & 0xFFU);
}

void Memory::write32(const Address address, const std::uint32_t value) {
  check_alignment(address, sizeof(value), MemoryOperation::Write);
  const auto offset =
      checked_offset(address, sizeof(value), MemoryOperation::Write);
  bytes_[offset] = static_cast<Byte>(value & 0xFFU);
  bytes_[offset + 1U] = static_cast<Byte>((value >> 8U) & 0xFFU);
  bytes_[offset + 2U] = static_cast<Byte>((value >> 16U) & 0xFFU);
  bytes_[offset + 3U] = static_cast<Byte>((value >> 24U) & 0xFFU);
}

void Memory::clear() noexcept { std::ranges::fill(bytes_, 0U); }

std::size_t Memory::checked_offset(const Address address,
                                   const std::size_t width,
                                   const MemoryOperation operation) const {
  const auto address64 = static_cast<std::uint64_t>(address);
  const auto base64 = static_cast<std::uint64_t>(base_address_);

  if (address64 < base64) {
    throw MemoryOutOfBoundsError(address, width, operation);
  }

  const auto offset64 = address64 - base64;
  if (offset64 > static_cast<std::uint64_t>(bytes_.size()) ||
      width > bytes_.size() - static_cast<std::size_t>(offset64)) {
    throw MemoryOutOfBoundsError(address, width, operation);
  }

  return static_cast<std::size_t>(offset64);
}

void Memory::check_alignment(const Address address, const std::size_t width,
                             const MemoryOperation operation) {
  if (static_cast<std::size_t>(address) % width != 0U) {
    throw MemoryMisalignmentError(address, width, operation);
  }
}

}  // namespace rvemu

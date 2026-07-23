#pragma once

#include <cstdint>
#include <optional>

#include "rvemu/instruction.hpp"

namespace rvemu {

enum class DataMemoryAccessKind : std::uint8_t {
  Load,
  Store,
};

struct DataMemoryAccessObservation {
  DataMemoryAccessKind kind;
  std::uint32_t address;
  std::uint8_t width_bytes;
};

enum class ControlFlowKind : std::uint8_t {
  ConditionalBranch,
  DirectJump,
  IndirectJump,
};

struct ControlFlowObservation {
  ControlFlowKind kind;
  bool taken;
};

struct ExecutionObservation {
  DecodedInstruction instruction;
  std::uint32_t program_counter;
  std::uint32_t next_program_counter;
  std::optional<DataMemoryAccessObservation> data_memory_access;
  std::optional<ControlFlowObservation> control_flow;
};

class ExecutionObserver {
 public:
  virtual ~ExecutionObserver() = default;

  virtual void observe(const ExecutionObservation& observation) noexcept = 0;
};

}  // namespace rvemu

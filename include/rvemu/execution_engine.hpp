#pragma once

#include <cstdint>
#include <variant>

#include "rvemu/cpu_state.hpp"
#include "rvemu/instruction.hpp"
#include "rvemu/memory.hpp"

namespace rvemu {

enum class TrapCause : std::uint32_t {
  InstructionAddressMisaligned = 0U,
  InstructionAccessFault = 1U,
  IllegalInstruction = 2U,
  LoadAddressMisaligned = 4U,
  LoadAccessFault = 5U,
  StoreAddressMisaligned = 6U,
  StoreAccessFault = 7U,
};

struct Trap {
  TrapCause cause;
  std::uint32_t program_counter;
  std::uint32_t value;
};

struct StepCompleted {
  DecodedInstruction instruction;
  std::uint32_t program_counter;
  std::uint32_t next_program_counter;
};

using StepResult = std::variant<StepCompleted, Trap>;

struct InstructionLimitReached {
  std::uint64_t instructions_executed;
};

struct RunTrapped {
  std::uint64_t instructions_executed;
  Trap trap;
};

using RunResult = std::variant<InstructionLimitReached, RunTrapped>;

class ExecutionEngine final {
 public:
  ExecutionEngine(CpuState& state, Memory& memory) noexcept;

  [[nodiscard]] StepResult step();
  [[nodiscard]] RunResult run(std::uint64_t instruction_limit);

 private:
  [[nodiscard]] Trap instruction_trap(TrapCause cause,
                                      std::uint32_t value) const noexcept;

  CpuState& state_;
  Memory& memory_;
};

}  // namespace rvemu

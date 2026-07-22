#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <variant>
#include <vector>

#include "rvemu/cpu_state.hpp"
#include "rvemu/memory.hpp"
#include "rvemu/program_session.hpp"

namespace rvemu::debugger {

struct DebuggerQuit {
  SessionStatistics statistics;
};

using DebuggerResult =
    std::variant<DebuggerQuit, SessionStepLimitReached, SessionRunExited,
                 SessionRunUnhandledEnvironmentCall, SessionRunTrapped>;

class InteractiveDebugger final {
 public:
  InteractiveDebugger(CpuState& state, const Memory& memory,
                      ProgramSession& session,
                      std::uint64_t maximum_guest_steps) noexcept;

  [[nodiscard]] DebuggerResult run(std::istream& input,
                                   std::ostream& output);

 private:
  [[nodiscard]] std::optional<DebuggerResult> execute_one(
      std::ostream& output, bool report_success);
  [[nodiscard]] std::optional<DebuggerResult> continue_execution(
      std::ostream& output);

  void print_help(std::ostream& output) const;
  void print_registers(std::ostream& output) const;
  void print_memory(std::ostream& output, std::uint32_t address,
                    std::size_t count) const;
  void print_breakpoints(std::ostream& output) const;
  void add_breakpoint(std::ostream& output, std::uint32_t address);
  void remove_breakpoint(std::ostream& output, std::uint32_t address);

  CpuState& state_;
  const Memory& memory_;
  ProgramSession& session_;
  std::uint64_t maximum_guest_steps_;
  SessionStatistics statistics_{0U, 0U, 0U};
  std::vector<std::uint32_t> breakpoints_;
  bool stopped_at_breakpoint_{false};
};

}  // namespace rvemu::debugger

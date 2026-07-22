#include "rvemu_debugger.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace rvemu::debugger {
namespace {

constexpr std::size_t kMaximumMemoryDisplayBytes = 256U;
constexpr std::size_t kMemoryBytesPerLine = 16U;

constexpr std::array<std::string_view, CpuState::kRegisterCount>
    kApplicationRegisterNames{
        "zero", "ra",  "sp",  "gp",  "tp",  "t0",  "t1",  "t2",
        "s0",   "s1",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
        "a6",   "a7",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
        "s8",   "s9",  "s10", "s11", "t3",  "t4",  "t5",  "t6"};

template <typename Integer>
[[nodiscard]] bool parse_unsigned(const std::string_view text,
                                  Integer& value) noexcept {
  static_assert(std::is_unsigned_v<Integer>);
  if (text.empty() || text.front() == '+' || text.front() == '-') {
    return false;
  }

  int base = 10;
  std::string_view digits = text;
  if (text.size() >= 2U && text[0U] == '0' &&
      (text[1U] == 'x' || text[1U] == 'X')) {
    base = 16;
    digits.remove_prefix(2U);
  }
  if (digits.empty()) {
    return false;
  }

  Integer parsed{};
  const auto result =
      std::from_chars(digits.data(), digits.data() + digits.size(), parsed, base);
  if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
    return false;
  }
  value = parsed;
  return true;
}

void print_hex32(std::ostream& output, const std::uint32_t value) {
  const std::ios_base::fmtflags flags = output.flags();
  const char fill = output.fill();
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  output.flags(flags);
  output.fill(fill);
}

void add_statistics(SessionStatistics& total,
                    const SessionStatistics& additional) noexcept {
  total.guest_steps += additional.guest_steps;
  total.instructions_retired += additional.instructions_retired;
  total.environment_calls_handled += additional.environment_calls_handled;
}

[[nodiscard]] std::vector<std::string> split_command(
    const std::string& line) {
  std::istringstream input(line);
  std::vector<std::string> words;
  std::string word;
  while (input >> word) {
    words.push_back(std::move(word));
  }
  return words;
}

[[nodiscard]] bool is_command(const std::string_view word,
                              const std::string_view long_name,
                              const std::string_view short_name) noexcept {
  return word == long_name || word == short_name;
}

}  // namespace

InteractiveDebugger::InteractiveDebugger(
    CpuState& state, const Memory& memory, ProgramSession& session,
    const std::uint64_t maximum_guest_steps) noexcept
    : state_(state),
      memory_(memory),
      session_(session),
      maximum_guest_steps_(maximum_guest_steps) {}

DebuggerResult InteractiveDebugger::run(std::istream& input,
                                         std::ostream& output) {
  output << "rvemu interactive debugger; type 'help' for commands\n"
            "stopped at PC ";
  print_hex32(output, state_.program_counter());
  output << '\n';

  std::string line;
  while (true) {
    output << "(rvemu) " << std::flush;
    if (!std::getline(input, line)) {
      output << "\ninput closed; leaving debugger\n";
      return DebuggerQuit{statistics_};
    }

    const std::vector<std::string> words = split_command(line);
    if (words.empty()) {
      continue;
    }
    const std::string_view command = words[0U];

    if (is_command(command, "help", "h")) {
      if (words.size() != 1U) {
        output << "usage: help\n";
      } else {
        print_help(output);
      }
      continue;
    }
    if (is_command(command, "step", "s")) {
      if (words.size() != 1U) {
        output << "usage: step\n";
        continue;
      }
      if (statistics_.guest_steps >= maximum_guest_steps_) {
        return SessionStepLimitReached{statistics_};
      }
      if (const auto terminal = execute_one(output, true)) {
        return *terminal;
      }
      continue;
    }
    if (is_command(command, "continue", "c")) {
      if (words.size() != 1U) {
        output << "usage: continue\n";
        continue;
      }
      if (const auto terminal = continue_execution(output)) {
        return *terminal;
      }
      continue;
    }
    if (is_command(command, "registers", "r")) {
      if (words.size() != 1U) {
        output << "usage: registers\n";
      } else {
        print_registers(output);
      }
      continue;
    }
    if (is_command(command, "memory", "x")) {
      if (words.size() < 2U || words.size() > 3U) {
        output << "usage: memory ADDRESS [COUNT]\n";
        continue;
      }
      std::uint32_t address = 0U;
      std::size_t count = 16U;
      if (!parse_unsigned(words[1U], address) ||
          (words.size() == 3U && !parse_unsigned(words[2U], count)) ||
          count == 0U || count > kMaximumMemoryDisplayBytes) {
        output << "memory requires a 32-bit address and a count from 1 to "
               << kMaximumMemoryDisplayBytes << '\n';
        continue;
      }
      print_memory(output, address, count);
      continue;
    }
    if (is_command(command, "break", "b")) {
      if (words.size() == 1U) {
        print_breakpoints(output);
        continue;
      }
      if (words.size() != 2U) {
        output << "usage: break [ADDRESS]\n";
        continue;
      }
      std::uint32_t address = 0U;
      if (!parse_unsigned(words[1U], address)) {
        output << "break requires a valid 32-bit address\n";
        continue;
      }
      add_breakpoint(output, address);
      continue;
    }
    if (is_command(command, "delete", "d")) {
      if (words.size() != 2U) {
        output << "usage: delete ADDRESS|all\n";
        continue;
      }
      if (words[1U] == "all") {
        breakpoints_.clear();
        stopped_at_breakpoint_ = false;
        output << "all breakpoints deleted\n";
        continue;
      }
      std::uint32_t address = 0U;
      if (!parse_unsigned(words[1U], address)) {
        output << "delete requires a valid 32-bit address or 'all'\n";
        continue;
      }
      remove_breakpoint(output, address);
      continue;
    }
    if (is_command(command, "quit", "q")) {
      if (words.size() != 1U) {
        output << "usage: quit\n";
        continue;
      }
      output << "leaving debugger\n";
      return DebuggerQuit{statistics_};
    }

    output << "unknown debugger command: " << command
           << "; type 'help' for commands\n";
  }
}

std::optional<DebuggerResult> InteractiveDebugger::execute_one(
    std::ostream& output, const bool report_success) {
  stopped_at_breakpoint_ = false;
  const SessionStepResult result = session_.step();
  if (std::holds_alternative<SessionInstructionCompleted>(result)) {
    ++statistics_.guest_steps;
    ++statistics_.instructions_retired;
  } else if (std::holds_alternative<SessionEnvironmentCallResumed>(result)) {
    ++statistics_.guest_steps;
    ++statistics_.environment_calls_handled;
  } else if (const auto* exited = std::get_if<SessionExited>(&result)) {
    ++statistics_.guest_steps;
    ++statistics_.environment_calls_handled;
    output << "guest exited with status " << exited->exit_code << '\n';
    return DebuggerResult{SessionRunExited{statistics_, *exited}};
  } else if (const auto* unhandled =
                 std::get_if<SessionUnhandledEnvironmentCall>(&result)) {
    return DebuggerResult{
        SessionRunUnhandledEnvironmentCall{statistics_, *unhandled}};
  } else {
    return DebuggerResult{SessionRunTrapped{statistics_, std::get<Trap>(result)}};
  }

  if (report_success) {
    output << "stepped to PC ";
    print_hex32(output, state_.program_counter());
    output << '\n';
  }
  return std::nullopt;
}

std::optional<DebuggerResult> InteractiveDebugger::continue_execution(
    std::ostream& output) {
  if (statistics_.guest_steps >= maximum_guest_steps_) {
    return DebuggerResult{SessionStepLimitReached{statistics_}};
  }

  if (stopped_at_breakpoint_) {
    if (const auto terminal = execute_one(output, false)) {
      return terminal;
    }
    if (statistics_.guest_steps >= maximum_guest_steps_) {
      return DebuggerResult{SessionStepLimitReached{statistics_}};
    }
  }

  const std::uint64_t remaining_steps =
      maximum_guest_steps_ - statistics_.guest_steps;
  const SessionRunResult result = session_.run(remaining_steps, breakpoints_);

  if (const auto* breakpoint =
          std::get_if<SessionBreakpointReached>(&result)) {
    add_statistics(statistics_, breakpoint->statistics);
    stopped_at_breakpoint_ = true;
    output << "breakpoint reached at PC ";
    print_hex32(output, breakpoint->program_counter);
    output << '\n';
    return std::nullopt;
  }
  if (const auto* limit = std::get_if<SessionStepLimitReached>(&result)) {
    add_statistics(statistics_, limit->statistics);
    return DebuggerResult{SessionStepLimitReached{statistics_}};
  }
  if (const auto* exited = std::get_if<SessionRunExited>(&result)) {
    add_statistics(statistics_, exited->statistics);
    output << "guest exited with status " << exited->exit.exit_code << '\n';
    return DebuggerResult{SessionRunExited{statistics_, exited->exit}};
  }
  if (const auto* unhandled =
          std::get_if<SessionRunUnhandledEnvironmentCall>(&result)) {
    add_statistics(statistics_, unhandled->statistics);
    return DebuggerResult{SessionRunUnhandledEnvironmentCall{
        statistics_, unhandled->environment_call}};
  }

  const SessionRunTrapped& trapped = std::get<SessionRunTrapped>(result);
  add_statistics(statistics_, trapped.statistics);
  return DebuggerResult{SessionRunTrapped{statistics_, trapped.trap}};
}

void InteractiveDebugger::print_help(std::ostream& output) const {
  output << "commands:\n"
            "  continue, c             run until a breakpoint or termination\n"
            "  step, s                 execute one guest step\n"
            "  break, b [ADDRESS]      list or add a breakpoint\n"
            "  delete, d ADDRESS|all   remove breakpoint(s)\n"
            "  registers, r            display PC and all integer registers\n"
            "  memory, x ADDRESS [N]   display 1-256 bytes (default 16)\n"
            "  help, h                 display this help\n"
            "  quit, q                 leave without running further\n";
}

void InteractiveDebugger::print_registers(std::ostream& output) const {
  output << "pc = ";
  print_hex32(output, state_.program_counter());
  output << '\n';
  for (std::size_t index = 0U; index < CpuState::kRegisterCount; ++index) {
    output << 'x' << index << " (" << kApplicationRegisterNames[index]
           << ") = ";
    print_hex32(output, state_.read_register(index));
    output << '\n';
  }
}

void InteractiveDebugger::print_memory(std::ostream& output,
                                       const std::uint32_t address,
                                       const std::size_t count) const {
  std::span<const Memory::Byte> bytes;
  try {
    bytes = memory_.read_span(address, count);
  } catch (const MemoryAccessError& error) {
    output << "cannot inspect memory: " << error.what() << '\n';
    return;
  }

  const std::ios_base::fmtflags flags = output.flags();
  const char fill = output.fill();
  for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
    if (offset % kMemoryBytesPerLine == 0U) {
      if (offset != 0U) {
        output << '\n';
      }
      print_hex32(output, address + static_cast<std::uint32_t>(offset));
      output << ':';
    }
    output << ' ' << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned int>(bytes[offset]);
  }
  output << '\n';
  output.flags(flags);
  output.fill(fill);
}

void InteractiveDebugger::print_breakpoints(std::ostream& output) const {
  if (breakpoints_.empty()) {
    output << "no breakpoints\n";
    return;
  }
  for (const std::uint32_t address : breakpoints_) {
    print_hex32(output, address);
    output << '\n';
  }
}

void InteractiveDebugger::add_breakpoint(std::ostream& output,
                                         const std::uint32_t address) {
  if ((address & 0x3U) != 0U) {
    output << "breakpoint addresses must be four-byte aligned\n";
    return;
  }
  try {
    (void)memory_.read_span(address, sizeof(std::uint32_t));
  } catch (const MemoryAccessError& error) {
    output << "cannot set breakpoint: " << error.what() << '\n';
    return;
  }

  const auto position =
      std::ranges::lower_bound(breakpoints_, address);
  if (position != breakpoints_.end() && *position == address) {
    output << "breakpoint already exists at ";
  } else {
    breakpoints_.insert(position, address);
    output << "breakpoint added at ";
  }
  print_hex32(output, address);
  output << '\n';
}

void InteractiveDebugger::remove_breakpoint(std::ostream& output,
                                            const std::uint32_t address) {
  const auto position =
      std::ranges::lower_bound(breakpoints_, address);
  if (position == breakpoints_.end() || *position != address) {
    output << "no breakpoint exists at ";
  } else {
    breakpoints_.erase(position);
    if (address == state_.program_counter()) {
      stopped_at_breakpoint_ = false;
    }
    output << "breakpoint deleted at ";
  }
  print_hex32(output, address);
  output << '\n';
}

}  // namespace rvemu::debugger

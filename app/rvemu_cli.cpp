#include "rvemu_cli.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <new>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

#include "rvemu_debugger.hpp"
#include "rvemu/cpu_state.hpp"
#include "rvemu/memory.hpp"
#include "rvemu/program_session.hpp"

namespace rvemu::cli {
namespace {

constexpr std::uint64_t kGuestAddressSpaceSize = 0x1'0000'0000ULL;

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

[[nodiscard]] ParseResult parse_failure(const std::string_view message) {
  return ParseFailure{std::string(message)};
}

[[nodiscard]] const char* trap_cause_name(const TrapCause cause) noexcept {
  switch (cause) {
    case TrapCause::InstructionAddressMisaligned:
      return "instruction-address-misaligned";
    case TrapCause::InstructionAccessFault:
      return "instruction-access-fault";
    case TrapCause::IllegalInstruction:
      return "illegal-instruction";
    case TrapCause::Breakpoint:
      return "breakpoint";
    case TrapCause::LoadAddressMisaligned:
      return "load-address-misaligned";
    case TrapCause::LoadAccessFault:
      return "load-access-fault";
    case TrapCause::StoreAddressMisaligned:
      return "store-address-misaligned";
    case TrapCause::StoreAccessFault:
      return "store-access-fault";
    case TrapCause::EnvironmentCallFromUserMode:
      return "environment-call-from-user-mode";
  }
  return "unknown";
}

void print_hex32(std::ostream& output, const std::uint32_t value) {
  const std::ios_base::fmtflags flags = output.flags();
  const char fill = output.fill();
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  output.flags(flags);
  output.fill(fill);
}

void print_statistics(std::ostream& output,
                      const SessionStatistics& statistics) {
  output << "guest steps=" << statistics.guest_steps
         << ", retired instructions=" << statistics.instructions_retired
         << ", handled environment calls="
         << statistics.environment_calls_handled;
}

[[nodiscard]] bool valid_memory_configuration(
    const Options& options) noexcept {
  static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t));
  const auto memory_size = static_cast<std::uint64_t>(options.memory_size);
  return options.memory_size != 0U && options.stack_size != 0U &&
         options.stack_size <= options.memory_size &&
         options.maximum_steps != 0U &&
         memory_size <=
             kGuestAddressSpaceSize -
                 static_cast<std::uint64_t>(options.memory_base);
}

template <typename Execute>
[[nodiscard]] HostedProgramResult with_loaded_program(
    const Options& options, OutputSink& output, Execute&& execute) {
  if (!valid_memory_configuration(options)) {
    return HostedProgramFailure{HostedProgramErrorCode::InvalidConfiguration};
  }

  try {
    CpuState state;
    Memory memory(options.memory_base, options.memory_size);
    const ElfLoadResult load_result =
        load_elf32_file(options.program_path, state, memory);
    if (const auto* failure = std::get_if<ElfLoadFailure>(&load_result)) {
      return HostedElfLoadFailure{*failure};
    }

    const ElfLoadSuccess loaded = std::get<ElfLoadSuccess>(load_result);
    const StackInitializationResult stack_result =
        initialize_freestanding_stack(
            state, memory, loaded.loaded_address_begin,
            loaded.loaded_address_end_exclusive, options.stack_size);
    if (const auto* failure =
            std::get_if<StackInitializationFailure>(&stack_result)) {
      return HostedStackInitializationFailure{
          *failure, loaded, memory.end_address_exclusive()};
    }

    LinuxSyscallEnvironment environment(output);
    ProgramSession session(state, memory, environment);
    return std::forward<Execute>(execute)(state, memory, session);
  } catch (const std::bad_alloc&) {
    return HostedProgramFailure{HostedProgramErrorCode::HostAllocationFailure};
  } catch (const std::length_error&) {
    return HostedProgramFailure{HostedProgramErrorCode::HostAllocationFailure};
  } catch (const std::invalid_argument&) {
    return HostedProgramFailure{HostedProgramErrorCode::InvalidConfiguration};
  }
}

[[nodiscard]] HostedProgramResult run_debugger_program(
    const Options& options, OutputSink& output, std::istream& debugger_input,
    std::ostream& debugger_output) {
  return with_loaded_program(
      options, output,
      [&](CpuState& state, const Memory& memory,
          ProgramSession& session) -> HostedProgramResult {
        debugger::InteractiveDebugger interactive(
            state, memory, session, options.maximum_steps);
        const debugger::DebuggerResult result =
            interactive.run(debugger_input, debugger_output);
        if (const auto* quit = std::get_if<debugger::DebuggerQuit>(&result)) {
          return HostedDebuggerQuit{quit->statistics,
                                    state.program_counter()};
        }
        if (const auto* limit =
                std::get_if<SessionStepLimitReached>(&result)) {
          return HostedSessionFinished{SessionRunResult{*limit},
                                       state.program_counter()};
        }
        if (const auto* exited = std::get_if<SessionRunExited>(&result)) {
          return HostedSessionFinished{SessionRunResult{*exited},
                                       state.program_counter()};
        }
        if (const auto* unhandled =
                std::get_if<SessionRunUnhandledEnvironmentCall>(&result)) {
          return HostedSessionFinished{SessionRunResult{*unhandled},
                                       state.program_counter()};
        }
        return HostedSessionFinished{
            SessionRunResult{std::get<SessionRunTrapped>(result)},
            state.program_counter()};
      });
}

}  // namespace

StreamOutputSink::StreamOutputSink(std::streambuf& standard_output,
                                   std::streambuf& standard_error) noexcept
    : standard_output_(standard_output), standard_error_(standard_error) {}

OutputWriteResult StreamOutputSink::write(
    const OutputStream stream,
    const std::span<const Memory::Byte> bytes) {
  if (bytes.empty()) {
    return OutputWriteCompleted{0U};
  }
  if (bytes.size() >
      static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
    return OutputWriteFailed{};
  }

  std::streambuf& target = stream == OutputStream::StandardOutput
                               ? standard_output_
                               : standard_error_;
  const auto requested = static_cast<std::streamsize>(bytes.size());
  const std::streamsize written = target.sputn(
      reinterpret_cast<const char*>(bytes.data()), requested);
  if (written <= 0 || written > requested) {
    return OutputWriteFailed{};
  }
  return OutputWriteCompleted{static_cast<std::size_t>(written)};
}

ParseResult parse_arguments(
    const std::span<const std::string_view> arguments) {
  Options options;
  std::optional<std::filesystem::path> program_path;
  bool positional_only = false;
  bool help_requested = false;
  bool memory_base_seen = false;
  bool memory_size_seen = false;
  bool stack_size_seen = false;
  bool maximum_steps_seen = false;
  bool debug_seen = false;

  for (std::size_t index = 0U; index < arguments.size(); ++index) {
    const std::string_view argument = arguments[index];
    if (!positional_only && argument == "--") {
      positional_only = true;
      continue;
    }
    if (!positional_only && (argument == "-h" || argument == "--help")) {
      if (help_requested) {
        return parse_failure("the help option was provided more than once");
      }
      help_requested = true;
      continue;
    }
    if (!positional_only && argument == "--debug") {
      if (debug_seen) {
        return parse_failure("--debug was provided more than once");
      }
      debug_seen = true;
      options.debug = true;
      continue;
    }

    const auto parse_option_value = [&]() -> std::optional<std::string_view> {
      if (index + 1U >= arguments.size()) {
        return std::nullopt;
      }
      ++index;
      if (arguments[index].empty()) {
        return std::nullopt;
      }
      return arguments[index];
    };

    if (!positional_only && argument == "--memory-base") {
      if (memory_base_seen) {
        return parse_failure("--memory-base was provided more than once");
      }
      memory_base_seen = true;
      const auto value = parse_option_value();
      if (!value.has_value() ||
          !parse_unsigned(*value, options.memory_base)) {
        return parse_failure("--memory-base requires a valid 32-bit address");
      }
      continue;
    }
    if (!positional_only && argument == "--memory-size") {
      if (memory_size_seen) {
        return parse_failure("--memory-size was provided more than once");
      }
      memory_size_seen = true;
      const auto value = parse_option_value();
      if (!value.has_value() ||
          !parse_unsigned(*value, options.memory_size)) {
        return parse_failure("--memory-size requires a valid byte count");
      }
      continue;
    }
    if (!positional_only && argument == "--stack-size") {
      if (stack_size_seen) {
        return parse_failure("--stack-size was provided more than once");
      }
      stack_size_seen = true;
      const auto value = parse_option_value();
      if (!value.has_value() ||
          !parse_unsigned(*value, options.stack_size)) {
        return parse_failure("--stack-size requires a valid byte count");
      }
      continue;
    }
    if (!positional_only && argument == "--max-steps") {
      if (maximum_steps_seen) {
        return parse_failure("--max-steps was provided more than once");
      }
      maximum_steps_seen = true;
      const auto value = parse_option_value();
      if (!value.has_value() ||
          !parse_unsigned(*value, options.maximum_steps)) {
        return parse_failure("--max-steps requires a valid count");
      }
      continue;
    }
    if (!positional_only && !argument.empty() && argument.front() == '-') {
      return parse_failure("an unknown command-line option was provided");
    }
    if (argument.empty()) {
      return parse_failure("the ELF path cannot be empty");
    }
    if (program_path.has_value()) {
      return parse_failure("exactly one ELF path is required");
    }
    program_path = std::filesystem::path(argument);
  }

  if (help_requested) {
    if (arguments.size() != 1U) {
      return parse_failure("--help cannot be combined with other arguments");
    }
    return ParseHelp{};
  }
  if (!program_path.has_value()) {
    return parse_failure("an ELF path is required");
  }
  if (options.memory_size == 0U || options.stack_size == 0U ||
      options.maximum_steps == 0U) {
    return parse_failure("memory size, stack size, and max steps must be nonzero");
  }
  if (options.stack_size > options.memory_size) {
    return parse_failure("the stack cannot be larger than mapped memory");
  }
  if (!valid_memory_configuration(options)) {
    return parse_failure("the memory mapping exceeds the 32-bit address space");
  }

  options.program_path = std::move(*program_path);
  return ParseSuccess{std::move(options)};
}

void print_usage(std::ostream& output) {
  output << "usage: rvemu [options] <program.elf>\n"
            "\n"
            "options:\n"
            "  --memory-base ADDRESS  guest mapping base (default 0x80000000)\n"
            "  --memory-size BYTES    guest mapping size (default 16777216)\n"
            "  --stack-size BYTES     reserved stack size (default 1048576)\n"
            "  --max-steps COUNT      strict guest-step limit (default 50000000)\n"
            "  --debug                start in the interactive debugger\n"
            "  -h, --help             show this help\n";
}

HostedProgramResult run_program(const Options& options, OutputSink& output) {
  return with_loaded_program(
      options, output,
      [&](CpuState& state, const Memory&,
          ProgramSession& session) -> HostedProgramResult {
        SessionRunResult session_result = session.run(options.maximum_steps);
        return HostedSessionFinished{std::move(session_result),
                                     state.program_counter()};
      });
}

int run_cli(const Options& options, OutputSink& output,
            std::ostream& diagnostics) {
  if (options.debug) {
    diagnostics << "rvemu: debugger mode requires an input stream\n";
    return kInfrastructureFailureExitStatus;
  }
  std::istringstream unused_input;
  return run_cli(options, output, unused_input, diagnostics);
}

int run_cli(const Options& options, OutputSink& output,
            std::istream& debugger_input, std::ostream& diagnostics) {
  const HostedProgramResult program_result =
      options.debug
          ? run_debugger_program(options, output, debugger_input, diagnostics)
          : run_program(options, output);
  if (const auto* failure =
          std::get_if<HostedProgramFailure>(&program_result)) {
    diagnostics << "rvemu: ";
    if (failure->code == HostedProgramErrorCode::InvalidConfiguration) {
      diagnostics << "invalid guest memory or execution configuration\n";
    } else {
      diagnostics << "the host could not allocate required memory\n";
    }
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* failure =
          std::get_if<HostedElfLoadFailure>(&program_result)) {
    diagnostics << "rvemu: failed to load '" << options.program_path.string()
                << "': " << elf_load_error_message(failure->failure.code);
    if (failure->failure.program_header_index.has_value()) {
      diagnostics << " (program header "
                  << *failure->failure.program_header_index << ')';
    }
    diagnostics << '\n';
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* failure =
          std::get_if<HostedStackInitializationFailure>(&program_result)) {
    diagnostics << "rvemu: stack initialization failed: "
                << stack_initialization_error_message(failure->failure.code)
                << " (requested=" << options.stack_size << ", mapping=";
    print_hex32(diagnostics, options.memory_base);
    diagnostics << "..0x" << std::hex << failure->memory_end_exclusive
                << std::dec << ", loaded=";
    print_hex32(diagnostics, failure->loaded_image.loaded_address_begin);
    diagnostics << "..0x" << std::hex
                << failure->loaded_image.loaded_address_end_exclusive
                << std::dec << ")\n";
    return kInfrastructureFailureExitStatus;
  }
  if (std::holds_alternative<HostedDebuggerQuit>(program_result)) {
    return 0;
  }

  const HostedSessionFinished& finished =
      std::get<HostedSessionFinished>(program_result);
  if (const auto* exited =
          std::get_if<SessionRunExited>(&finished.result)) {
    return static_cast<int>(exited->exit.exit_code);
  }
  if (const auto* limit =
          std::get_if<SessionStepLimitReached>(&finished.result)) {
    diagnostics << "rvemu: guest-step limit " << options.maximum_steps
                << " reached at PC ";
    print_hex32(diagnostics, finished.final_program_counter);
    diagnostics << " (";
    print_statistics(diagnostics, limit->statistics);
    diagnostics << ")\n";
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* trapped = std::get_if<SessionRunTrapped>(&finished.result)) {
    diagnostics << "rvemu: guest trapped: "
                << trap_cause_name(trapped->trap.cause) << " (cause "
                << static_cast<std::uint32_t>(trapped->trap.cause)
                << ") at PC ";
    print_hex32(diagnostics, trapped->trap.program_counter);
    diagnostics << ", value ";
    print_hex32(diagnostics, trapped->trap.value);
    diagnostics << " (";
    print_statistics(diagnostics, trapped->statistics);
    diagnostics << ")\n";
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* unhandled =
          std::get_if<SessionRunUnhandledEnvironmentCall>(&finished.result)) {
    diagnostics << "rvemu: unhandled environment call "
                << unhandled->environment_call.call.number << " at PC ";
    print_hex32(diagnostics,
                unhandled->environment_call.call.program_counter);
    diagnostics << " (";
    print_statistics(diagnostics, unhandled->statistics);
    diagnostics << ")\n";
    return kInfrastructureFailureExitStatus;
  }

  const SessionBreakpointReached& breakpoint =
      std::get<SessionBreakpointReached>(finished.result);
  diagnostics << "rvemu: unexpected breakpoint at PC ";
  print_hex32(diagnostics, breakpoint.program_counter);
  diagnostics << " (";
  print_statistics(diagnostics, breakpoint.statistics);
  diagnostics << ")\n";
  return kInfrastructureFailureExitStatus;
}

}  // namespace rvemu::cli

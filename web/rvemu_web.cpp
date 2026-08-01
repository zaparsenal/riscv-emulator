#include "rvemu_web_api.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "rvemu/cpu_state.hpp"
#include "rvemu/elf_loader.hpp"
#include "rvemu/instruction.hpp"
#include "rvemu/linux_syscalls.hpp"
#include "rvemu/memory.hpp"
#include "rvemu/performance_analyzer.hpp"
#include "rvemu/program_session.hpp"
#include "rvemu/stack.hpp"

namespace {

constexpr std::uint32_t kMemoryBase = 0x80000000U;
constexpr std::size_t kMemorySize = 16U * 1024U * 1024U;
constexpr std::size_t kStackSize = 1U * 1024U * 1024U;
constexpr std::uint32_t kMaximumMemoryRead = 512U;

[[nodiscard]] rvemu::PerformanceAnalyzerConfiguration
performance_configuration() {
  return rvemu::PerformanceAnalyzerConfiguration{
      .cache =
          rvemu::SplitCacheConfiguration{
              .instruction =
                  rvemu::CacheConfiguration{4096U, 64U, 2U},
              .data = rvemu::CacheConfiguration{4096U, 64U, 2U}},
      .branch_predictor =
          rvemu::BranchPredictorConfiguration{
              rvemu::BranchPredictionStrategy::BimodalTwoBit, 256U,
              rvemu::TwoBitCounterState::WeaklyNotTaken},
      .cycle_costs = rvemu::CycleCostConfiguration{1U, 10U, 20U, 5U}};
}

[[nodiscard]] std::string hex32(const std::uint32_t value) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return output.str();
}

void append_json_string(std::ostringstream& output,
                        const std::string_view value) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  output << '"';
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    switch (byte) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (byte < 0x20U || byte >= 0x7FU) {
          output << "\\u00" << kHexDigits[(byte >> 4U) & 0x0FU]
                 << kHexDigits[byte & 0x0FU];
        } else {
          output << static_cast<char>(byte);
        }
        break;
    }
  }
  output << '"';
}

[[nodiscard]] std::string signed_decimal(const std::uint32_t value) {
  if ((value & 0x80000000U) == 0U) {
    return std::to_string(value);
  }
  const std::uint32_t magnitude = (~value) + 1U;
  return "-" + std::to_string(magnitude);
}

[[nodiscard]] std::string register_name(const std::uint8_t index) {
  static constexpr std::array<std::string_view, 32U> kNames = {
      "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
      "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
      "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
      "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
  return std::string(kNames.at(index));
}

struct AssemblyText {
  std::string mnemonic;
  std::string operands;
};

[[nodiscard]] AssemblyText disassemble(const std::uint32_t raw) {
  const std::optional<rvemu::DecodedInstruction> decoded =
      rvemu::decode_instruction(raw);
  if (!decoded.has_value()) {
    return {".word", hex32(raw)};
  }

  const rvemu::DecodedInstruction& instruction = *decoded;
  const std::string rd = register_name(instruction.destination);
  const std::string rs1 = register_name(instruction.source1);
  const std::string rs2 = register_name(instruction.source2);
  const std::string immediate = signed_decimal(instruction.immediate);
  const std::uint8_t funct3 = instruction.function3;
  const std::uint8_t funct7 = instruction.function7;

  switch (instruction.opcode) {
    case rvemu::Opcode::Lui:
      return {"lui", rd + ", " + hex32(instruction.immediate)};
    case rvemu::Opcode::Auipc:
      return {"auipc", rd + ", " + hex32(instruction.immediate)};
    case rvemu::Opcode::Jal:
      return {"jal", rd + ", " + immediate};
    case rvemu::Opcode::Jalr:
      if (funct3 == 0U) {
        return {"jalr", rd + ", " + immediate + "(" + rs1 + ")"};
      }
      break;
    case rvemu::Opcode::Branch: {
      static constexpr std::array<std::string_view, 8U> kNames = {
          "beq", "bne", "", "", "blt", "bge", "bltu", "bgeu"};
      if (!kNames.at(funct3).empty()) {
        return {std::string(kNames.at(funct3)),
                rs1 + ", " + rs2 + ", " + immediate};
      }
      break;
    }
    case rvemu::Opcode::Load: {
      static constexpr std::array<std::string_view, 8U> kNames = {
          "lb", "lh", "lw", "", "lbu", "lhu", "", ""};
      if (!kNames.at(funct3).empty()) {
        return {std::string(kNames.at(funct3)),
                rd + ", " + immediate + "(" + rs1 + ")"};
      }
      break;
    }
    case rvemu::Opcode::Store: {
      static constexpr std::array<std::string_view, 8U> kNames = {
          "sb", "sh", "sw", "", "", "", "", ""};
      if (!kNames.at(funct3).empty()) {
        return {std::string(kNames.at(funct3)),
                rs2 + ", " + immediate + "(" + rs1 + ")"};
      }
      break;
    }
    case rvemu::Opcode::OpImmediate: {
      static constexpr std::array<std::string_view, 8U> kNames = {
          "addi", "slli", "slti", "sltiu", "xori", "srli", "ori",
          "andi"};
      std::string mnemonic(kNames.at(funct3));
      std::string operand = immediate;
      if (funct3 == 1U) {
        if (funct7 != 0U) {
          break;
        }
        operand = std::to_string((raw >> 20U) & 0x1FU);
      } else if (funct3 == 5U) {
        if (funct7 == 0x20U) {
          mnemonic = "srai";
        } else if (funct7 != 0U) {
          break;
        }
        operand = std::to_string((raw >> 20U) & 0x1FU);
      }
      return {mnemonic, rd + ", " + rs1 + ", " + operand};
    }
    case rvemu::Opcode::Op: {
      if (funct7 == 0x01U) {
        static constexpr std::array<std::string_view, 8U> kMNames = {
            "mul", "mulh", "mulhsu", "mulhu", "div", "divu", "rem",
            "remu"};
        return {std::string(kMNames.at(funct3)),
                rd + ", " + rs1 + ", " + rs2};
      }
      static constexpr std::array<std::string_view, 8U> kNames = {
          "add", "sll", "slt", "sltu", "xor", "srl", "or", "and"};
      std::string mnemonic(kNames.at(funct3));
      if (funct7 == 0x20U && funct3 == 0U) {
        mnemonic = "sub";
      } else if (funct7 == 0x20U && funct3 == 5U) {
        mnemonic = "sra";
      } else if (funct7 != 0U) {
        break;
      }
      return {mnemonic, rd + ", " + rs1 + ", " + rs2};
    }
    case rvemu::Opcode::MiscMemory:
      if (funct3 == 0U) {
        return {"fence", ""};
      }
      break;
    case rvemu::Opcode::System:
      if (raw == 0x00000073U) {
        return {"ecall", ""};
      }
      if (raw == 0x00100073U) {
        return {"ebreak", ""};
      }
      break;
  }
  return {".word", hex32(raw)};
}

[[nodiscard]] std::string_view trap_name(const rvemu::TrapCause cause) {
  switch (cause) {
    case rvemu::TrapCause::InstructionAddressMisaligned:
      return "instruction address misaligned";
    case rvemu::TrapCause::InstructionAccessFault:
      return "instruction access fault";
    case rvemu::TrapCause::IllegalInstruction:
      return "illegal instruction";
    case rvemu::TrapCause::Breakpoint:
      return "guest breakpoint";
    case rvemu::TrapCause::LoadAddressMisaligned:
      return "load address misaligned";
    case rvemu::TrapCause::LoadAccessFault:
      return "load access fault";
    case rvemu::TrapCause::StoreAddressMisaligned:
      return "store address misaligned";
    case rvemu::TrapCause::StoreAccessFault:
      return "store access fault";
    case rvemu::TrapCause::EnvironmentCallFromUserMode:
      return "environment call from user mode";
  }
  return "unknown trap";
}

class BrowserOutputSink final : public rvemu::OutputSink {
 public:
  [[nodiscard]] rvemu::OutputWriteResult write(
      const rvemu::OutputStream stream,
      const std::span<const rvemu::Memory::Byte> bytes) override {
    std::string& destination =
        stream == rvemu::OutputStream::StandardOutput ? standard_output_
                                                      : standard_error_;
    destination.append(reinterpret_cast<const char*>(bytes.data()),
                       bytes.size());
    return rvemu::OutputWriteCompleted{bytes.size()};
  }

  [[nodiscard]] const std::string& standard_output() const noexcept {
    return standard_output_;
  }

  [[nodiscard]] const std::string& standard_error() const noexcept {
    return standard_error_;
  }

 private:
  std::string standard_output_;
  std::string standard_error_;
};

enum class MachineStatus {
  Empty,
  Ready,
  Paused,
  Breakpoint,
  Exited,
  Trapped,
  Error,
};

[[nodiscard]] std::string_view status_name(const MachineStatus status) {
  switch (status) {
    case MachineStatus::Empty:
      return "empty";
    case MachineStatus::Ready:
      return "ready";
    case MachineStatus::Paused:
      return "paused";
    case MachineStatus::Breakpoint:
      return "breakpoint";
    case MachineStatus::Exited:
      return "exited";
    case MachineStatus::Trapped:
      return "trapped";
    case MachineStatus::Error:
      return "error";
  }
  return "error";
}

class DashboardMachine final {
 public:
  DashboardMachine() = default;

  [[nodiscard]] const char* load_elf(const std::uint8_t* image,
                                    const std::size_t size) {
    if (image == nullptr || size == 0U) {
      return set_error("Select a non-empty ELF file.");
    }

    try {
      std::vector<std::uint8_t> candidate(image, image + size);
      if (!load_image(candidate, true)) {
        return response_.c_str();
      }
      image_ = std::move(candidate);
      return snapshot();
    } catch (const std::bad_alloc&) {
      return set_error("The browser could not allocate memory for this ELF.");
    } catch (const std::exception& error) {
      return set_error(error.what());
    }
  }

  [[nodiscard]] const char* reset() {
    if (image_.empty()) {
      return set_error("Load an ELF before resetting the machine.");
    }
    try {
      if (!load_image(image_, false)) {
        return response_.c_str();
      }
      return snapshot();
    } catch (const std::exception& error) {
      return set_error(error.what());
    }
  }

  [[nodiscard]] const char* step() {
    if (!can_execute()) {
      return snapshot();
    }
    perform_step();
    return snapshot();
  }

  [[nodiscard]] const char* run_batch(const std::uint32_t maximum_steps) {
    if (!can_execute() || maximum_steps == 0U) {
      return snapshot();
    }

    for (std::uint32_t index = 0U; index < maximum_steps; ++index) {
      if (std::ranges::binary_search(breakpoints_, state_->program_counter())) {
        status_ = MachineStatus::Breakpoint;
        message_ = "Execution paused at a host breakpoint.";
        break;
      }
      perform_step();
      if (!can_execute()) {
        break;
      }
    }
    if (status_ == MachineStatus::Ready) {
      status_ = MachineStatus::Paused;
    }
    return snapshot();
  }

  [[nodiscard]] const char* set_breakpoint(const std::uint32_t address,
                                           const bool enabled) {
    const auto position =
        std::ranges::lower_bound(breakpoints_, address);
    if (enabled && (position == breakpoints_.end() || *position != address)) {
      breakpoints_.insert(position, address);
    } else if (!enabled && position != breakpoints_.end() &&
               *position == address) {
      breakpoints_.erase(position);
    }
    return snapshot();
  }

  [[nodiscard]] const char* read_memory(const std::uint32_t address,
                                        const std::uint32_t requested_size) {
    if (!memory_) {
      return set_error_object("No guest memory is available.");
    }
    if (requested_size == 0U || requested_size > kMaximumMemoryRead) {
      return set_error_object("Memory reads must contain 1 to 512 bytes.");
    }

    try {
      const std::span<const rvemu::Memory::Byte> bytes =
          memory_->read_span(address, requested_size);
      std::ostringstream output;
      output << "{\"ok\":true,\"address\":\"" << hex32(address)
             << "\",\"bytes\":[";
      for (std::size_t index = 0U; index < bytes.size(); ++index) {
        if (index != 0U) {
          output << ',';
        }
        output << static_cast<unsigned>(bytes[index]);
      }
      output << "]}";
      response_ = output.str();
    } catch (const rvemu::MemoryAccessError& error) {
      return set_error_object(error.what());
    }
    return response_.c_str();
  }

  [[nodiscard]] const char* snapshot() {
    std::ostringstream output;
    output << "{\"ok\":true,\"ready\":" << (state_ ? "true" : "false")
           << ",\"status\":";
    append_json_string(output, status_name(status_));
    output << ",\"message\":";
    append_json_string(output, message_);
    output << ",\"pc\":" << (state_ ? state_->program_counter() : 0U)
           << ",\"pcHex\":";
    append_json_string(output,
                       state_ ? hex32(state_->program_counter()) : "—");

    append_instruction(output);
    append_registers(output);
    append_load_information(output);
    append_runtime_statistics(output);
    append_output(output);
    append_breakpoints(output);
    append_last_event(output);
    output << '}';
    response_ = output.str();
    return response_.c_str();
  }

 private:
  [[nodiscard]] bool load_image(const std::vector<std::uint8_t>& image,
                                const bool clear_breakpoints) {
    auto state = std::make_unique<rvemu::CpuState>();
    auto memory = std::make_unique<rvemu::Memory>(kMemoryBase, kMemorySize);
    const rvemu::ElfLoadResult result =
        rvemu::load_elf32(image, *state, *memory);
    if (const auto* failure = std::get_if<rvemu::ElfLoadFailure>(&result)) {
      std::ostringstream message;
      message << "ELF load failed: "
              << rvemu::elf_load_error_message(failure->code);
      if (failure->program_header_index.has_value()) {
        message << " (program header " << *failure->program_header_index
                << ')';
      }
      set_error(message.str());
      return false;
    }

    const rvemu::ElfLoadSuccess loaded =
        std::get<rvemu::ElfLoadSuccess>(result);
    const rvemu::StackInitializationResult stack_result =
        rvemu::initialize_freestanding_stack(
            *state, *memory, loaded.loaded_address_begin,
            loaded.loaded_address_end_exclusive, kStackSize);
    if (const auto* failure =
            std::get_if<rvemu::StackInitializationFailure>(&stack_result)) {
      set_error(std::string("Stack setup failed: ") +
                std::string(rvemu::stack_initialization_error_message(
                    failure->code)));
      return false;
    }

    state_ = std::move(state);
    memory_ = std::move(memory);
    analyzer_ = std::make_unique<rvemu::PerformanceAnalyzer>(
        performance_configuration());
    output_ = std::make_unique<BrowserOutputSink>();
    environment_ =
        std::make_unique<rvemu::LinuxSyscallEnvironment>(*output_);
    session_ = std::make_unique<rvemu::ProgramSession>(
        *state_, *memory_, *environment_, analyzer_.get());
    load_information_ = loaded;
    stack_information_ = std::get<rvemu::StackInitializationSuccess>(
        stack_result);
    status_ = MachineStatus::Ready;
    message_ = "ELF loaded. Machine is ready at its entry point.";
    guest_steps_ = 0U;
    retired_instructions_ = 0U;
    environment_calls_ = 0U;
    changed_registers_.clear();
    last_event_json_ = "null";
    if (clear_breakpoints) {
      breakpoints_.clear();
    }
    return true;
  }

  [[nodiscard]] bool can_execute() const noexcept {
    return session_ && status_ != MachineStatus::Exited &&
           status_ != MachineStatus::Trapped &&
           status_ != MachineStatus::Error;
  }

  void perform_step() {
    std::array<std::uint32_t, rvemu::CpuState::kRegisterCount> before{};
    for (std::size_t index = 0U; index < before.size(); ++index) {
      before[index] = state_->read_register(index);
    }
    const rvemu::PerformanceAnalyzerStatistics performance_before =
        analyzer_->statistics();

    const rvemu::SessionStepResult result = session_->step();
    changed_registers_.clear();
    for (std::size_t index = 0U; index < before.size(); ++index) {
      if (before[index] != state_->read_register(index)) {
        changed_registers_.push_back(index);
      }
    }

    if (const auto* completed =
            std::get_if<rvemu::SessionInstructionCompleted>(&result)) {
      ++guest_steps_;
      ++retired_instructions_;
      status_ = MachineStatus::Ready;
      message_ = "Instruction retired.";
      record_instruction_event(completed->step, performance_before);
      return;
    }
    if (const auto* resumed =
            std::get_if<rvemu::SessionEnvironmentCallResumed>(&result)) {
      ++guest_steps_;
      ++environment_calls_;
      status_ = MachineStatus::Ready;
      message_ = "Guest environment call completed.";
      record_call_event("syscall", resumed->call,
                        std::optional<std::uint32_t>(resumed->return_value));
      return;
    }
    if (const auto* exited = std::get_if<rvemu::SessionExited>(&result)) {
      ++guest_steps_;
      ++environment_calls_;
      status_ = MachineStatus::Exited;
      message_ = "Guest exited with status " +
                 std::to_string(exited->exit_code & 0xFFU) + ".";
      record_call_event("exit", exited->call,
                        std::optional<std::uint32_t>(exited->exit_code));
      return;
    }
    if (const auto* unhandled =
            std::get_if<rvemu::SessionUnhandledEnvironmentCall>(&result)) {
      status_ = MachineStatus::Trapped;
      message_ = "Unhandled environment call " +
                 std::to_string(unhandled->call.number) + ".";
      record_trap_event(unhandled->trap);
      return;
    }

    const rvemu::Trap trap = std::get<rvemu::Trap>(result);
    status_ = MachineStatus::Trapped;
    message_ = std::string("Execution trapped: ") +
               std::string(trap_name(trap.cause)) + ".";
    record_trap_event(trap);
  }

  void record_instruction_event(
      const rvemu::ExecutionObservation& observation,
      const rvemu::PerformanceAnalyzerStatistics& before) {
    const rvemu::PerformanceAnalyzerStatistics after = analyzer_->statistics();
    const AssemblyText assembly = disassemble(observation.instruction.raw);
    const bool instruction_miss =
        after.cache.instruction.misses > before.cache.instruction.misses;

    std::ostringstream output;
    output << "{\"kind\":\"instruction\",\"pc\":\""
           << hex32(observation.program_counter) << "\",\"nextPc\":\""
           << hex32(observation.next_program_counter) << "\",\"mnemonic\":";
    append_json_string(output, assembly.mnemonic);
    output << ",\"assembly\":";
    append_json_string(
        output, assembly.operands.empty()
                    ? assembly.mnemonic
                    : assembly.mnemonic + " " + assembly.operands);
    output << ",\"instructionCache\":\""
           << (instruction_miss ? "miss" : "hit") << "\"";

    if (observation.data_memory_access.has_value()) {
      const bool data_miss = after.cache.data.total.misses >
                             before.cache.data.total.misses;
      output << ",\"dataCache\":\"" << (data_miss ? "miss" : "hit")
             << "\",\"dataAddress\":\""
             << hex32(observation.data_memory_access->address) << "\"";
    } else {
      output << ",\"dataCache\":null,\"dataAddress\":null";
    }

    if (observation.control_flow.has_value()) {
      const bool conditional = observation.control_flow->kind ==
                               rvemu::ControlFlowKind::ConditionalBranch;
      output << ",\"branchTaken\":"
             << (observation.control_flow->taken ? "true" : "false");
      if (conditional) {
        const bool correct = after.branch_predictor.correct >
                             before.branch_predictor.correct;
        const bool predicted_taken = after.branch_predictor.predicted_taken >
                                     before.branch_predictor.predicted_taken;
        output << ",\"branchPrediction\":\""
               << (correct ? "correct" : "miss")
               << "\",\"predictedTaken\":"
               << (predicted_taken ? "true" : "false");
      } else {
        output << ",\"branchPrediction\":null,\"predictedTaken\":null";
      }
    } else {
      output << ",\"branchTaken\":null,\"branchPrediction\":null,"
                "\"predictedTaken\":null";
    }
    output << '}';
    last_event_json_ = output.str();
  }

  void record_call_event(const std::string_view kind,
                         const rvemu::EnvironmentCall& call,
                         const std::optional<std::uint32_t> result) {
    std::ostringstream output;
    output << "{\"kind\":";
    append_json_string(output, kind);
    output << ",\"pc\":\"" << hex32(call.program_counter)
           << "\",\"number\":" << call.number << ",\"result\":";
    if (result.has_value()) {
      output << *result;
    } else {
      output << "null";
    }
    output << '}';
    last_event_json_ = output.str();
  }

  void record_trap_event(const rvemu::Trap& trap) {
    std::ostringstream output;
    output << "{\"kind\":\"trap\",\"pc\":\""
           << hex32(trap.program_counter) << "\",\"cause\":";
    append_json_string(output, trap_name(trap.cause));
    output << ",\"value\":\"" << hex32(trap.value) << "\"}";
    last_event_json_ = output.str();
  }

  void append_instruction(std::ostringstream& output) const {
    output << ",\"instruction\":";
    if (!state_ || !memory_) {
      output << "null";
      return;
    }
    try {
      const std::uint32_t address = state_->program_counter();
      const std::uint32_t raw = memory_->read32(address);
      const AssemblyText assembly = disassemble(raw);
      output << "{\"address\":" << address << ",\"addressHex\":\""
             << hex32(address) << "\",\"raw\":" << raw
             << ",\"rawHex\":\"" << hex32(raw) << "\",\"mnemonic\":";
      append_json_string(output, assembly.mnemonic);
      output << ",\"operands\":";
      append_json_string(output, assembly.operands);
      output << ",\"assembly\":";
      append_json_string(output,
                         assembly.operands.empty()
                             ? assembly.mnemonic
                             : assembly.mnemonic + " " + assembly.operands);
      output << '}';
    } catch (const rvemu::MemoryAccessError&) {
      output << "null";
    }
  }

  void append_registers(std::ostringstream& output) const {
    output << ",\"registers\":[";
    if (state_) {
      for (std::size_t index = 0U;
           index < rvemu::CpuState::kRegisterCount; ++index) {
        if (index != 0U) {
          output << ',';
        }
        const std::uint32_t value = state_->read_register(index);
        output << "{\"index\":" << index << ",\"abi\":";
        append_json_string(output, register_name(
                                       static_cast<std::uint8_t>(index)));
        output << ",\"value\":" << value << ",\"hex\":\""
               << hex32(value) << "\",\"changed\":"
               << (std::ranges::find(changed_registers_, index) !=
                           changed_registers_.end()
                       ? "true"
                       : "false")
               << '}';
      }
    }
    output << ']';
  }

  void append_load_information(std::ostringstream& output) const {
    output << ",\"load\":";
    if (!load_information_.has_value() || !stack_information_.has_value()) {
      output << "null";
      return;
    }
    const rvemu::ElfLoadSuccess& loaded = *load_information_;
    const rvemu::StackInitializationSuccess& stack = *stack_information_;
    output << "{\"entryPoint\":\"" << hex32(loaded.entry_point)
           << "\",\"segments\":" << loaded.loadable_segments
           << ",\"fileBytes\":" << loaded.file_bytes_loaded
           << ",\"zeroFillBytes\":" << loaded.zero_fill_bytes
           << ",\"rangeBegin\":\""
           << hex32(loaded.loaded_address_begin)
           << "\",\"rangeEnd\":\""
           << hex32(static_cast<std::uint32_t>(
                  loaded.loaded_address_end_exclusive & 0xFFFFFFFFULL))
           << "\",\"stackPointer\":\""
           << hex32(stack.initial_stack_pointer) << "\",\"memoryBase\":\""
           << hex32(kMemoryBase) << "\",\"memoryBytes\":" << kMemorySize
           << '}';
  }

  void append_runtime_statistics(std::ostringstream& output) const {
    output << ",\"statistics\":{\"guestSteps\":" << guest_steps_
           << ",\"retired\":" << retired_instructions_
           << ",\"environmentCalls\":" << environment_calls_;
    if (!analyzer_) {
      output << ",\"instructionCache\":{\"accesses\":0,\"hits\":0,"
                "\"misses\":0},\"dataCache\":{\"accesses\":0,"
                "\"hits\":0,\"misses\":0},\"branches\":{"
                "\"predictions\":0,\"correct\":0,\"incorrect\":0},"
                "\"estimatedCycles\":0}";
      return;
    }

    const rvemu::PerformanceAnalyzerStatistics statistics =
        analyzer_->statistics();
    const rvemu::CacheStatistics instruction = statistics.cache.instruction;
    const rvemu::CacheStatistics data = statistics.cache.data.total;
    const rvemu::BranchPredictionStatistics branch =
        statistics.branch_predictor;
    output << ",\"instructionCache\":{\"accesses\":"
           << instruction.accesses << ",\"hits\":" << instruction.hits
           << ",\"misses\":" << instruction.misses
           << "},\"dataCache\":{\"accesses\":" << data.accesses
           << ",\"hits\":" << data.hits << ",\"misses\":"
           << data.misses << "},\"branches\":{\"predictions\":"
           << branch.predictions << ",\"correct\":" << branch.correct
           << ",\"incorrect\":" << branch.incorrect
           << ",\"predictedTaken\":" << branch.predicted_taken
           << ",\"actualTaken\":" << branch.actual_taken << '}';
    const rvemu::CycleEstimateResult estimate = analyzer_->estimate_cycles();
    if (const auto* cycles = std::get_if<rvemu::CycleEstimate>(&estimate)) {
      output << ",\"estimatedCycles\":" << cycles->total_cycles;
    } else {
      output << ",\"estimatedCycles\":null";
    }
    output << '}';
  }

  void append_output(std::ostringstream& output) const {
    output << ",\"stdout\":";
    append_json_string(output, output_ ? output_->standard_output() : "");
    output << ",\"stderr\":";
    append_json_string(output, output_ ? output_->standard_error() : "");
  }

  void append_breakpoints(std::ostringstream& output) const {
    output << ",\"breakpoints\":[";
    for (std::size_t index = 0U; index < breakpoints_.size(); ++index) {
      if (index != 0U) {
        output << ',';
      }
      append_json_string(output, hex32(breakpoints_[index]));
    }
    output << ']';
  }

  void append_last_event(std::ostringstream& output) const {
    output << ",\"lastEvent\":" << last_event_json_;
  }

  const char* set_error(const std::string_view message) {
    status_ = MachineStatus::Error;
    message_ = std::string(message);
    std::ostringstream output;
    output << "{\"ok\":false,\"error\":";
    append_json_string(output, message);
    output << '}';
    response_ = output.str();
    return response_.c_str();
  }

  const char* set_error_object(const std::string_view message) {
    std::ostringstream output;
    output << "{\"ok\":false,\"error\":";
    append_json_string(output, message);
    output << '}';
    response_ = output.str();
    return response_.c_str();
  }

  std::unique_ptr<rvemu::CpuState> state_;
  std::unique_ptr<rvemu::Memory> memory_;
  std::unique_ptr<rvemu::PerformanceAnalyzer> analyzer_;
  std::unique_ptr<BrowserOutputSink> output_;
  std::unique_ptr<rvemu::LinuxSyscallEnvironment> environment_;
  std::unique_ptr<rvemu::ProgramSession> session_;
  std::vector<std::uint8_t> image_;
  std::vector<std::uint32_t> breakpoints_;
  std::vector<std::size_t> changed_registers_;
  std::optional<rvemu::ElfLoadSuccess> load_information_;
  std::optional<rvemu::StackInitializationSuccess> stack_information_;
  MachineStatus status_{MachineStatus::Empty};
  std::string message_{"Load an RV32IM ELF to begin."};
  std::string last_event_json_{"null"};
  std::string response_;
  std::uint64_t guest_steps_{0U};
  std::uint64_t retired_instructions_{0U};
  std::uint64_t environment_calls_{0U};
};

DashboardMachine& machine() {
  static DashboardMachine instance;
  return instance;
}

}  // namespace

extern "C" {

const char* rvemu_initialize() { return machine().snapshot(); }

std::uint8_t* rvemu_allocate(const std::size_t size) {
  return new (std::nothrow) std::uint8_t[size];
}

void rvemu_deallocate(std::uint8_t* pointer) { delete[] pointer; }

const char* rvemu_load_elf(const std::uint8_t* image, const std::size_t size) {
  return machine().load_elf(image, size);
}

const char* rvemu_reset() { return machine().reset(); }

const char* rvemu_step() { return machine().step(); }

const char* rvemu_run_batch(const std::uint32_t maximum_steps) {
  return machine().run_batch(maximum_steps);
}

const char* rvemu_set_breakpoint(const std::uint32_t address,
                                 const int enabled) {
  return machine().set_breakpoint(address, enabled != 0);
}

const char* rvemu_read_memory(const std::uint32_t address,
                              const std::uint32_t size) {
  return machine().read_memory(address, size);
}

const char* rvemu_snapshot() { return machine().snapshot(); }

}  // extern "C"

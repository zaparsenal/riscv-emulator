#include "act_smoke.hpp"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <variant>

namespace {

constexpr int kTestFailedExitStatus = 1;
constexpr int kInfrastructureFailureExitStatus = 2;

void print_summary(const bool passed, const std::filesystem::path& path) {
  std::cout << "RVCP-SUMMARY: TEST " << (passed ? "PASSED" : "FAILED")
            << " - Test File \"" << path.filename().string() << "\"\n";
}

void print_statistics(const rvemu::SessionStatistics& statistics) {
  std::cerr << "guest steps: " << statistics.guest_steps
            << ", retired instructions: "
            << statistics.instructions_retired
            << ", handled environment calls: "
            << statistics.environment_calls_handled << '\n';
}

int report_result(const rvemu::act::SmokeResult& result,
                  const std::filesystem::path& path) {
  if (const auto* passed = std::get_if<rvemu::act::SmokePassed>(&result)) {
    print_summary(true, path);
    print_statistics(passed->statistics);
    return 0;
  }
  if (const auto* failed = std::get_if<rvemu::act::SmokeFailed>(&result)) {
    print_summary(false, path);
    std::cerr << "self-checking ELF exited with code " << failed->exit_code
              << '\n';
    print_statistics(failed->statistics);
    return kTestFailedExitStatus;
  }
  if (const auto* load =
          std::get_if<rvemu::act::SmokeLoadFailed>(&result)) {
    std::cerr << "ACT smoke infrastructure failure: "
              << rvemu::elf_load_error_message(load->failure.code);
    if (load->failure.program_header_index.has_value()) {
      std::cerr << " (program header "
                << *load->failure.program_header_index << ')';
    }
    std::cerr << '\n';
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* limit =
          std::get_if<rvemu::act::SmokeStepLimitReached>(&result)) {
    std::cerr << "ACT smoke infrastructure failure: the instruction limit "
                 "was reached\n";
    print_statistics(limit->statistics);
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* trapped = std::get_if<rvemu::act::SmokeTrapped>(&result)) {
    std::cerr << "ACT smoke infrastructure failure: trap cause "
              << static_cast<std::uint32_t>(trapped->trap.cause)
              << " at PC 0x" << std::hex << std::setw(8) << std::setfill('0')
              << trapped->trap.program_counter << ", value 0x" << std::setw(8)
              << trapped->trap.value << std::dec << '\n';
    print_statistics(trapped->statistics);
    return kInfrastructureFailureExitStatus;
  }
  if (const auto* unhandled =
          std::get_if<rvemu::act::SmokeUnhandledEnvironmentCall>(&result)) {
    std::cerr << "ACT smoke infrastructure failure: unhandled environment "
                 "call "
              << unhandled->call.number << " at PC 0x" << std::hex
              << std::setw(8) << std::setfill('0')
              << unhandled->call.program_counter << std::dec << '\n';
    print_statistics(unhandled->statistics);
    return kInfrastructureFailureExitStatus;
  }
  const auto& breakpoint =
      std::get<rvemu::act::SmokeUnexpectedBreakpoint>(result);
  std::cerr << "ACT smoke infrastructure failure: unexpected breakpoint at "
               "PC 0x"
            << std::hex << std::setw(8) << std::setfill('0')
            << breakpoint.program_counter << std::dec << '\n';
  print_statistics(breakpoint.statistics);
  return kInfrastructureFailureExitStatus;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
  if (argc != 2) {
    std::cerr << "usage: rvemu-act-smoke <self-checking-rv32im-elf>\n";
    return kInfrastructureFailureExitStatus;
  }

  const std::filesystem::path path(argv[1]);
  return report_result(rvemu::act::run_self_checking_elf_file(path), path);
}

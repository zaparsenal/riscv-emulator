#include "rvemu_cli.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <variant>
#include <vector>

int main(const int argc, const char* const argv[]) {
  try {
    std::vector<std::string_view> arguments;
    if (argc > 1) {
      arguments.reserve(static_cast<std::size_t>(argc - 1));
    }
    for (int index = 1; index < argc; ++index) {
      arguments.emplace_back(argv[index]);
    }

    const rvemu::cli::ParseResult parsed =
        rvemu::cli::parse_arguments(arguments);
    if (std::holds_alternative<rvemu::cli::ParseHelp>(parsed)) {
      rvemu::cli::print_usage(std::cout);
      return 0;
    }
    if (const auto* failure =
            std::get_if<rvemu::cli::ParseFailure>(&parsed)) {
      std::cerr << "rvemu: " << failure->message << '\n';
      rvemu::cli::print_usage(std::cerr);
      return rvemu::cli::kUsageExitStatus;
    }

    rvemu::cli::StreamOutputSink output(*std::cout.rdbuf(), *std::cerr.rdbuf());
    return rvemu::cli::run_cli(
        std::get<rvemu::cli::ParseSuccess>(parsed).options, output, std::cerr);
  } catch (const std::exception& error) {
    std::cerr << "rvemu: internal host failure: " << error.what() << '\n';
    return rvemu::cli::kInfrastructureFailureExitStatus;
  } catch (...) {
    std::cerr << "rvemu: unknown internal host failure\n";
    return rvemu::cli::kInfrastructureFailureExitStatus;
  }
}

#include "rvemu_cli.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <sstream>
#include <streambuf>
#include <string>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace rvemu::cli {
namespace {

constexpr std::size_t kElfHeaderSize = 52U;
constexpr std::size_t kProgramHeaderSize = 32U;
constexpr std::size_t kProgramHeaderOffset = kElfHeaderSize;
constexpr std::size_t kPayloadOffset = 0x100U;
constexpr std::uint32_t kProgramAddress = 0x80000000U;

void put_u16_le(std::vector<std::uint8_t>& image, const std::size_t offset,
                const std::uint16_t value) {
  image[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  image[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void put_u32_le(std::vector<std::uint8_t>& image, const std::size_t offset,
                const std::uint32_t value) {
  image[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  image[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  image[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
  image[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

void append_instruction(std::vector<std::uint8_t>& payload,
                        const std::uint32_t instruction) {
  const std::size_t offset = payload.size();
  payload.resize(offset + sizeof(instruction));
  put_u32_le(payload, offset, instruction);
}

[[nodiscard]] std::vector<std::uint8_t> make_elf(
    const std::span<const std::uint8_t> payload,
    const std::uint32_t memory_size = 0U) {
  const auto file_size = static_cast<std::uint32_t>(payload.size());
  const std::uint32_t segment_memory_size =
      memory_size == 0U ? file_size : memory_size;
  std::vector<std::uint8_t> image(kPayloadOffset + payload.size(), 0U);
  image[0U] = 0x7FU;
  image[1U] = 'E';
  image[2U] = 'L';
  image[3U] = 'F';
  image[4U] = 1U;
  image[5U] = 1U;
  image[6U] = 1U;
  put_u16_le(image, 16U, 2U);
  put_u16_le(image, 18U, 243U);
  put_u32_le(image, 20U, 1U);
  put_u32_le(image, 24U, kProgramAddress);
  put_u32_le(image, 28U, static_cast<std::uint32_t>(kProgramHeaderOffset));
  put_u16_le(image, 40U, static_cast<std::uint16_t>(kElfHeaderSize));
  put_u16_le(image, 42U, static_cast<std::uint16_t>(kProgramHeaderSize));
  put_u16_le(image, 44U, 1U);

  put_u32_le(image, kProgramHeaderOffset, 1U);
  put_u32_le(image, kProgramHeaderOffset + 4U,
             static_cast<std::uint32_t>(kPayloadOffset));
  put_u32_le(image, kProgramHeaderOffset + 8U, kProgramAddress);
  put_u32_le(image, kProgramHeaderOffset + 16U, file_size);
  put_u32_le(image, kProgramHeaderOffset + 20U, segment_memory_size);
  put_u32_le(image, kProgramHeaderOffset + 24U, 0x5U);
  put_u32_le(image, kProgramHeaderOffset + 28U, 4U);
  std::copy(payload.begin(), payload.end(), image.begin() + kPayloadOffset);
  return image;
}

[[nodiscard]] std::vector<std::uint8_t> make_program_payload() {
  std::vector<std::uint8_t> payload;
  append_instruction(payload, 0x00100513U);  // addi a0, zero, 1
  append_instruction(payload, 0x800005B7U);  // lui a1, 0x80000
  append_instruction(payload, 0x02458593U);  // addi a1, a1, 36
  append_instruction(payload, 0x00100613U);  // addi a2, zero, 1
  append_instruction(payload, 0x04000893U);  // addi a7, zero, 64
  append_instruction(payload, 0x00000073U);  // ecall
  append_instruction(payload, 0x00700513U);  // addi a0, zero, 7
  append_instruction(payload, 0x05E00893U);  // addi a7, zero, 94
  append_instruction(payload, 0x00000073U);  // ecall
  payload.push_back(static_cast<std::uint8_t>('!'));
  return payload;
}

class TemporaryElf final {
 public:
  explicit TemporaryElf(const std::span<const std::uint8_t> image) {
    static std::uint64_t next_id = 0U;
    path_ = std::filesystem::temp_directory_path() /
            ("rvemu-cli-test-" + std::to_string(next_id++) + ".elf");
    std::ofstream output(path_, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(image.data()),
                 static_cast<std::streamsize>(image.size()));
    if (!output) {
      throw std::runtime_error("failed to create temporary ELF test file");
    }
  }

  ~TemporaryElf() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  TemporaryElf(const TemporaryElf&) = delete;
  TemporaryElf& operator=(const TemporaryElf&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

struct OutputRecord {
  OutputStream stream;
  std::vector<Memory::Byte> bytes;
};

class RecordingOutputSink final : public OutputSink {
 public:
  [[nodiscard]] OutputWriteResult write(
      const OutputStream stream,
      const std::span<const Memory::Byte> bytes) override {
    records.push_back(OutputRecord{
        stream, std::vector<Memory::Byte>(bytes.begin(), bytes.end())});
    return OutputWriteCompleted{bytes.size()};
  }

  std::vector<OutputRecord> records;
};

class PrefixStreambuf final : public std::streambuf {
 public:
  explicit PrefixStreambuf(const std::size_t limit) : limit_(limit) {}

  [[nodiscard]] const std::string& bytes() const noexcept { return bytes_; }

 protected:
  std::streamsize xsputn(const char* source,
                         const std::streamsize count) override {
    if (count <= 0) {
      return 0;
    }
    const auto requested = static_cast<std::size_t>(count);
    const std::size_t consumed = std::min(limit_, requested);
    bytes_.append(source, consumed);
    return static_cast<std::streamsize>(consumed);
  }

 private:
  std::size_t limit_;
  std::string bytes_;
};

[[nodiscard]] ParseResult parse(
    const std::initializer_list<std::string_view> arguments) {
  return parse_arguments(
      std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

[[nodiscard]] Options test_options(const std::filesystem::path& path,
                                   const std::uint64_t maximum_steps = 32U) {
  return Options{path, kProgramAddress, 0x10000U, 0x1000U, maximum_steps};
}

TEST(RvemuCliArgumentsTest, UsesDocumentedDefaults) {
  const ParseResult result = parse({"program.elf"});

  ASSERT_TRUE(std::holds_alternative<ParseSuccess>(result));
  const Options& options = std::get<ParseSuccess>(result).options;
  EXPECT_EQ(options.program_path, std::filesystem::path("program.elf"));
  EXPECT_EQ(options.memory_base, kDefaultMemoryBase);
  EXPECT_EQ(options.memory_size, kDefaultMemorySize);
  EXPECT_EQ(options.stack_size, kDefaultFreestandingStackSize);
  EXPECT_EQ(options.maximum_steps, kDefaultMaximumSteps);
}

TEST(RvemuCliArgumentsTest, AcceptsDecimalHexadecimalAndOptionTerminator) {
  const ParseResult result =
      parse({"--memory-base", "0x1000", "--memory-size", "65536",
             "--stack-size", "0X1000", "--max-steps", "123", "--",
             "-program.elf"});

  ASSERT_TRUE(std::holds_alternative<ParseSuccess>(result));
  const Options& options = std::get<ParseSuccess>(result).options;
  EXPECT_EQ(options.program_path, std::filesystem::path("-program.elf"));
  EXPECT_EQ(options.memory_base, 0x1000U);
  EXPECT_EQ(options.memory_size, 65536U);
  EXPECT_EQ(options.stack_size, 0x1000U);
  EXPECT_EQ(options.maximum_steps, 123U);
}

TEST(RvemuCliArgumentsTest, RecognizesHelpOnlyByItself) {
  EXPECT_TRUE(std::holds_alternative<ParseHelp>(parse({"--help"})));
  EXPECT_TRUE(std::holds_alternative<ParseHelp>(parse({"-h"})));
}

TEST(RvemuCliArgumentsTest, RejectsMalformedAndAmbiguousArguments) {
  const std::vector<std::vector<std::string_view>> invalid_cases{
      {},
      {""},
      {"one.elf", "two.elf"},
      {"--unknown", "one.elf"},
      {"--help", "one.elf"},
      {"--help", "--help"},
      {"--memory-base"},
      {"--memory-base", "-1", "one.elf"},
      {"--memory-base", "0x100000000", "one.elf"},
      {"--memory-base", "0xffffffff", "--memory-size", "2", "one.elf"},
      {"--memory-base", "1", "--memory-base", "2", "one.elf"},
      {"--memory-size", "0", "one.elf"},
      {"--memory-size", "4", "--stack-size", "8", "one.elf"},
      {"--memory-size", "4", "--memory-size", "4", "one.elf"},
      {"--stack-size", "0", "one.elf"},
      {"--stack-size", "4", "--stack-size", "4", "one.elf"},
      {"--max-steps", "0", "one.elf"},
      {"--max-steps", "0x", "one.elf"},
      {"--max-steps", "1", "--max-steps", "1", "one.elf"},
  };

  for (const auto& arguments : invalid_cases) {
    SCOPED_TRACE(testing::PrintToString(arguments));
    EXPECT_TRUE(std::holds_alternative<ParseFailure>(
        parse_arguments(std::span<const std::string_view>(arguments))));
  }
}

TEST(RvemuCliArgumentsTest, UsageDocumentsEverySupportedOption) {
  std::ostringstream output;

  print_usage(output);

  EXPECT_NE(output.str().find("rvemu [options] <program.elf>"),
            std::string::npos);
  EXPECT_NE(output.str().find("--memory-base"), std::string::npos);
  EXPECT_NE(output.str().find("--memory-size"), std::string::npos);
  EXPECT_NE(output.str().find("--stack-size"), std::string::npos);
  EXPECT_NE(output.str().find("--max-steps"), std::string::npos);
}

TEST(StreamOutputSinkTest, PreservesBinaryBytesAndRoutesBothStreams) {
  std::stringbuf standard_output;
  std::stringbuf standard_error;
  StreamOutputSink output(standard_output, standard_error);
  const std::array<Memory::Byte, 3U> stdout_bytes{'R', 0U, 'V'};
  const std::array<Memory::Byte, 1U> stderr_bytes{'!'};

  const OutputWriteResult stdout_result =
      output.write(OutputStream::StandardOutput, stdout_bytes);
  const OutputWriteResult stderr_result =
      output.write(OutputStream::StandardError, stderr_bytes);

  EXPECT_EQ(std::get<OutputWriteCompleted>(stdout_result).bytes_written, 3U);
  EXPECT_EQ(std::get<OutputWriteCompleted>(stderr_result).bytes_written, 1U);
  EXPECT_EQ(standard_output.str(), std::string("R\0V", 3U));
  EXPECT_EQ(standard_error.str(), "!");
}

TEST(StreamOutputSinkTest, ReportsPartialProgressAndZeroProgressFailure) {
  PrefixStreambuf partial_buffer(2U);
  PrefixStreambuf failed_buffer(0U);
  StreamOutputSink output(partial_buffer, failed_buffer);
  const std::array<Memory::Byte, 3U> bytes{'a', 'b', 'c'};

  const OutputWriteResult partial =
      output.write(OutputStream::StandardOutput, bytes);
  const OutputWriteResult failed =
      output.write(OutputStream::StandardError, bytes);

  EXPECT_EQ(std::get<OutputWriteCompleted>(partial).bytes_written, 2U);
  EXPECT_EQ(partial_buffer.bytes(), "ab");
  EXPECT_TRUE(std::holds_alternative<OutputWriteFailed>(failed));
}

TEST(RvemuCliRunnerTest, LoadsWritesAndExitsARealElfFile) {
  const std::vector<std::uint8_t> payload = make_program_payload();
  const std::vector<std::uint8_t> image = make_elf(payload);
  const TemporaryElf elf(image);
  RecordingOutputSink output;

  const HostedProgramResult result = run_program(test_options(elf.path()), output);

  ASSERT_TRUE(std::holds_alternative<HostedSessionFinished>(result));
  const HostedSessionFinished& finished =
      std::get<HostedSessionFinished>(result);
  ASSERT_TRUE(std::holds_alternative<SessionRunExited>(finished.result));
  const SessionRunExited& exited = std::get<SessionRunExited>(finished.result);
  EXPECT_EQ(exited.exit.exit_code, 7U);
  EXPECT_EQ(exited.statistics.guest_steps, 9U);
  EXPECT_EQ(exited.statistics.instructions_retired, 7U);
  EXPECT_EQ(exited.statistics.environment_calls_handled, 2U);
  EXPECT_EQ(finished.final_program_counter, kProgramAddress + 32U);
  ASSERT_EQ(output.records.size(), 1U);
  EXPECT_EQ(output.records[0U].stream, OutputStream::StandardOutput);
  EXPECT_EQ(output.records[0U].bytes,
            (std::vector<Memory::Byte>{static_cast<Memory::Byte>('!')}));
}

TEST(RvemuCliRunnerTest, ReturnsTheGuestExitStatusWithoutDiagnostics) {
  const std::vector<std::uint8_t> payload = make_program_payload();
  const TemporaryElf elf(make_elf(payload));
  RecordingOutputSink output;
  std::ostringstream diagnostics;

  const int status =
      run_cli(test_options(elf.path()), output, diagnostics);

  EXPECT_EQ(status, 7);
  EXPECT_TRUE(diagnostics.str().empty());
}

TEST(RvemuCliRunnerTest, RejectsInvalidHostConfigurationBeforeLoading) {
  RecordingOutputSink output;
  Options options = test_options("unused.elf");
  options.memory_base = 0xFFFFFFFFU;
  options.memory_size = 2U;

  const HostedProgramResult result = run_program(options, output);

  ASSERT_TRUE(std::holds_alternative<HostedProgramFailure>(result));
  EXPECT_EQ(std::get<HostedProgramFailure>(result).code,
            HostedProgramErrorCode::InvalidConfiguration);
}

TEST(RvemuCliRunnerTest, ReportsMalformedAndMissingElfFiles) {
  const std::array<std::uint8_t, 4U> malformed{0x7FU, 'E', 'L', 'F'};
  const TemporaryElf elf(malformed);
  RecordingOutputSink output;

  const HostedProgramResult malformed_result =
      run_program(test_options(elf.path()), output);
  const HostedProgramResult missing_result =
      run_program(test_options(elf.path().string() + ".missing"), output);

  ASSERT_TRUE(
      std::holds_alternative<HostedElfLoadFailure>(malformed_result));
  EXPECT_EQ(std::get<HostedElfLoadFailure>(malformed_result).failure.code,
            ElfLoadErrorCode::TruncatedHeader);
  ASSERT_TRUE(std::holds_alternative<HostedElfLoadFailure>(missing_result));
  EXPECT_EQ(std::get<HostedElfLoadFailure>(missing_result).failure.code,
            ElfLoadErrorCode::FileOpenFailed);
}

TEST(RvemuCliRunnerTest, ReportsWhenTheStackWouldOverlapTheLoadedImage) {
  std::vector<std::uint8_t> payload;
  append_instruction(payload, 0x00000013U);
  const TemporaryElf elf(make_elf(payload, 0xF800U));
  RecordingOutputSink output;

  const HostedProgramResult result = run_program(test_options(elf.path()), output);

  ASSERT_TRUE(
      std::holds_alternative<HostedStackInitializationFailure>(result));
  const HostedStackInitializationFailure& failure =
      std::get<HostedStackInitializationFailure>(result);
  EXPECT_EQ(failure.failure.code,
            StackInitializationErrorCode::StackOverlapsLoadedImage);
  EXPECT_EQ(failure.loaded_image.loaded_address_end_exclusive,
            0x8000F800ULL);
}

TEST(RvemuCliRunnerTest, ReportsStepLimitWithFinalPcAndStatistics) {
  std::vector<std::uint8_t> payload;
  append_instruction(payload, 0x0000006FU);
  const TemporaryElf elf(make_elf(payload));
  RecordingOutputSink output;
  std::ostringstream diagnostics;

  const int status =
      run_cli(test_options(elf.path(), 3U), output, diagnostics);

  EXPECT_EQ(status, kInfrastructureFailureExitStatus);
  EXPECT_NE(diagnostics.str().find("guest-step limit 3"), std::string::npos);
  EXPECT_NE(diagnostics.str().find("PC 0x80000000"), std::string::npos);
  EXPECT_NE(diagnostics.str().find("guest steps=3"), std::string::npos);
}

TEST(RvemuCliRunnerTest, ReportsArchitecturalTrapsPrecisely) {
  std::vector<std::uint8_t> payload;
  append_instruction(payload, 0xFFFFFFFFU);
  const TemporaryElf elf(make_elf(payload));
  RecordingOutputSink output;
  std::ostringstream diagnostics;

  const int status = run_cli(test_options(elf.path()), output, diagnostics);

  EXPECT_EQ(status, kInfrastructureFailureExitStatus);
  EXPECT_NE(diagnostics.str().find("illegal-instruction"), std::string::npos);
  EXPECT_NE(diagnostics.str().find("PC 0x80000000"), std::string::npos);
  EXPECT_NE(diagnostics.str().find("value 0xffffffff"), std::string::npos);
}

TEST(RvemuCliRunnerTest, UnsupportedLinuxCallReturnsEnosysAndExecutionContinues) {
  std::vector<std::uint8_t> payload;
  append_instruction(payload, 0x00100893U);
  append_instruction(payload, 0x00000073U);
  append_instruction(payload, 0x00900513U);
  append_instruction(payload, 0x05E00893U);
  append_instruction(payload, 0x00000073U);
  const TemporaryElf elf(make_elf(payload));
  RecordingOutputSink output;
  std::ostringstream diagnostics;

  const int status = run_cli(test_options(elf.path()), output, diagnostics);

  EXPECT_EQ(status, 9);
  EXPECT_TRUE(diagnostics.str().empty());
}

}  // namespace
}  // namespace rvemu::cli

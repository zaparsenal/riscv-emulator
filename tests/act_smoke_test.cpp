#include "act_smoke.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace rvemu::act {
namespace {

constexpr std::size_t kElfHeaderSize = 52U;
constexpr std::size_t kProgramHeaderSize = 32U;
constexpr std::size_t kProgramHeaderOffset = kElfHeaderSize;
constexpr std::size_t kPayloadOffset = 0x100U;
constexpr std::uint32_t kProgramAddress = 0x1000U;

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

[[nodiscard]] std::vector<std::uint8_t> make_elf(
    const std::span<const std::uint32_t> instructions) {
  std::vector<std::uint8_t> image(
      kPayloadOffset + instructions.size() * sizeof(std::uint32_t), 0U);
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
  const std::uint32_t payload_size =
      static_cast<std::uint32_t>(instructions.size() * sizeof(std::uint32_t));
  put_u32_le(image, kProgramHeaderOffset + 16U, payload_size);
  put_u32_le(image, kProgramHeaderOffset + 20U, payload_size);
  put_u32_le(image, kProgramHeaderOffset + 24U, 0x5U);
  put_u32_le(image, kProgramHeaderOffset + 28U, 4U);
  for (std::size_t index = 0U; index < instructions.size(); ++index) {
    put_u32_le(image, kPayloadOffset + index * sizeof(std::uint32_t),
               instructions[index]);
  }
  return image;
}

[[nodiscard]] SmokeOptions test_options(const std::uint64_t step_limit = 8U) {
  return SmokeOptions{kProgramAddress, 0x1000U, step_limit};
}

TEST(ActSmokeTest, ZeroExitCodePasses) {
  const std::array instructions{0x00000513U, 0x05D00893U, 0x00000073U};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options());

  ASSERT_TRUE(std::holds_alternative<SmokePassed>(result));
  const SessionStatistics statistics =
      std::get<SmokePassed>(result).statistics;
  EXPECT_EQ(statistics.guest_steps, 3U);
  EXPECT_EQ(statistics.instructions_retired, 2U);
  EXPECT_EQ(statistics.environment_calls_handled, 1U);
}

TEST(ActSmokeTest, NonzeroExitCodeFailsWithoutHostErrorTranslation) {
  const std::array instructions{0x00700513U, 0x05D00893U, 0x00000073U};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options());

  ASSERT_TRUE(std::holds_alternative<SmokeFailed>(result));
  const SmokeFailed failure = std::get<SmokeFailed>(result);
  EXPECT_EQ(failure.exit_code, 7U);
  EXPECT_EQ(failure.statistics.guest_steps, 3U);
}

TEST(ActSmokeTest, ExitGroupIsAlsoAccepted) {
  const std::array instructions{0x00000513U, 0x05E00893U, 0x00000073U};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options());

  EXPECT_TRUE(std::holds_alternative<SmokePassed>(result));
}

TEST(ActSmokeTest, UnknownEnvironmentCallIsInfrastructureFailure) {
  const std::array instructions{0x00100893U, 0x00000073U};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options());

  ASSERT_TRUE(
      std::holds_alternative<SmokeUnhandledEnvironmentCall>(result));
  const SmokeUnhandledEnvironmentCall failure =
      std::get<SmokeUnhandledEnvironmentCall>(result);
  EXPECT_EQ(failure.call.number, 1U);
  EXPECT_EQ(failure.call.program_counter, kProgramAddress + 4U);
  EXPECT_EQ(failure.statistics.instructions_retired, 1U);
}

TEST(ActSmokeTest, ArchitecturalTrapIsInfrastructureFailure) {
  const std::array instructions{0xFFFFFFFFU};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options());

  ASSERT_TRUE(std::holds_alternative<SmokeTrapped>(result));
  const SmokeTrapped trapped = std::get<SmokeTrapped>(result);
  EXPECT_EQ(trapped.trap.cause, TrapCause::IllegalInstruction);
  EXPECT_EQ(trapped.trap.program_counter, kProgramAddress);
  EXPECT_EQ(trapped.statistics.guest_steps, 0U);
}

TEST(ActSmokeTest, StepLimitStopsAnInfiniteTestDeterministically) {
  const std::array instructions{0x0000006FU};

  const SmokeResult result =
      run_self_checking_elf(make_elf(instructions), test_options(3U));

  ASSERT_TRUE(std::holds_alternative<SmokeStepLimitReached>(result));
  const SessionStatistics statistics =
      std::get<SmokeStepLimitReached>(result).statistics;
  EXPECT_EQ(statistics.guest_steps, 3U);
  EXPECT_EQ(statistics.instructions_retired, 3U);
  EXPECT_EQ(statistics.environment_calls_handled, 0U);
}

TEST(ActSmokeTest, MalformedElfIsReportedBeforeExecution) {
  const std::array<std::uint8_t, 4U> malformed{0x7FU, 'E', 'L', 'F'};

  const SmokeResult result =
      run_self_checking_elf(malformed, test_options());

  ASSERT_TRUE(std::holds_alternative<SmokeLoadFailed>(result));
  EXPECT_EQ(std::get<SmokeLoadFailed>(result).failure.code,
            ElfLoadErrorCode::TruncatedHeader);
}

TEST(ActSmokeTest, MissingElfFileIsReportedBeforeExecution) {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      "rvemu-act-smoke-file-that-does-not-exist.elf";

  const SmokeResult result =
      run_self_checking_elf_file(path, test_options());

  ASSERT_TRUE(std::holds_alternative<SmokeLoadFailed>(result));
  EXPECT_EQ(std::get<SmokeLoadFailed>(result).failure.code,
            ElfLoadErrorCode::FileOpenFailed);
}

}  // namespace
}  // namespace rvemu::act

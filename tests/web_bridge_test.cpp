#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "rvemu_web_api.hpp"

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_demo_elf() {
  const std::string path =
      std::string(RVEMU_TEST_SOURCE_DIR) +
      "/frontend/public/demo/rv32im-dashboard.elf";
  std::ifstream input(path, std::ios::binary);
  EXPECT_TRUE(input.is_open());
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input),
                                   std::istreambuf_iterator<char>());
}

void expect_contains(const char* json, const std::string& fragment) {
  ASSERT_NE(json, nullptr);
  EXPECT_NE(std::string(json).find(fragment), std::string::npos)
      << "response: " << json;
}

TEST(WebBridgeTest, LoadsRunsInspectsAndResetsTheDemoElf) {
  const std::vector<std::uint8_t> image = read_demo_elf();
  ASSERT_FALSE(image.empty());

  const char* loaded = rvemu_load_elf(image.data(), image.size());
  expect_contains(loaded, "\"status\":\"ready\"");
  expect_contains(loaded, "\"entryPoint\":\"0x80000000\"");

  const char* breakpoint = rvemu_set_breakpoint(0x80000028U, 1);
  expect_contains(breakpoint, "0x80000028");
  const char* stopped = rvemu_run_batch(100U);
  expect_contains(stopped, "\"status\":\"breakpoint\"");
  expect_contains(stopped, "rvemu: hello from RV32IM");

  const char* stepped = rvemu_step();
  expect_contains(stepped, "\"kind\":\"instruction\"");
  rvemu_set_breakpoint(0x80000028U, 0);
  const char* finished = rvemu_run_batch(1000U);
  expect_contains(finished, "\"status\":\"exited\"");
  expect_contains(finished, "\"predictions\":");
  expect_contains(finished, "\"estimatedCycles\":");

  const char* memory = rvemu_read_memory(0x80000000U, 16U);
  expect_contains(memory, "\"ok\":true");
  expect_contains(memory, "\"bytes\":[");

  rvemu_set_breakpoint(0x80000028U, 1);
  const char* reset = rvemu_reset();
  expect_contains(reset, "\"status\":\"ready\"");
  expect_contains(reset, "\"guestSteps\":0");
  expect_contains(reset, "0x80000028");
}

}  // namespace

cmake_minimum_required(VERSION 3.20)

project(
  riscv_emulator
  VERSION 0.1.0
  DESCRIPTION "A correctness-first RV32IM emulator and performance analyzer"
  LANGUAGES CXX
)

include(CTest)
include(cmake/ProjectOptions.cmake)

add_library(rvemu_core
  src/branch_predictor.cpp
  src/cache_model.cpp
  src/cpu_state.cpp
  src/cycle_estimator.cpp
  src/elf_loader.cpp
  src/execution_engine.cpp
  src/instruction.cpp
  src/linux_syscalls.cpp
  src/memory.cpp
  src/performance_analyzer.cpp
  src/program_session.cpp
  src/stack.cpp
)
add_library(rvemu::core ALIAS rvemu_core)

target_include_directories(rvemu_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rvemu_core PRIVATE rvemu_project_options)

option(
  RVEMU_BUILD_CLI
  "Build the rvemu command-line frontend"
  ON
)

if(RVEMU_BUILD_CLI OR BUILD_TESTING)
  add_library(rvemu_cli_support STATIC
    app/rvemu_cli.cpp
    app/rvemu_debugger.cpp
  )
  add_library(rvemu::cli_support ALIAS rvemu_cli_support)
  target_include_directories(rvemu_cli_support PUBLIC app)
  target_link_libraries(
    rvemu_cli_support
    PUBLIC rvemu::core
    PRIVATE rvemu_project_options
  )
endif()

if(RVEMU_BUILD_CLI)
  add_executable(rvemu_cli app/rvemu_main.cpp)
  set_target_properties(rvemu_cli PROPERTIES OUTPUT_NAME rvemu)
  target_link_libraries(
    rvemu_cli
    PRIVATE rvemu::cli_support rvemu_project_options
  )
endif()

option(
  RVEMU_BUILD_ACT_SMOKE_RUNNER
  "Build the runner for self-checking ACT 4 RV32I/M ELF files"
  OFF
)

if(RVEMU_BUILD_ACT_SMOKE_RUNNER OR BUILD_TESTING)
  add_library(rvemu_act_smoke_support STATIC
    tools/act_smoke.cpp
  )
  add_library(rvemu::act_smoke_support ALIAS rvemu_act_smoke_support)
  target_include_directories(rvemu_act_smoke_support PUBLIC tools)
  target_link_libraries(
    rvemu_act_smoke_support
    PUBLIC rvemu::core
    PRIVATE rvemu_project_options
  )
endif()

if(RVEMU_BUILD_ACT_SMOKE_RUNNER OR BUILD_TESTING)
  add_executable(rvemu_act_smoke tools/act_smoke_main.cpp)
  set_target_properties(rvemu_act_smoke PROPERTIES OUTPUT_NAME rvemu-act-smoke)
  target_link_libraries(
    rvemu_act_smoke
    PRIVATE rvemu_act_smoke_support rvemu_project_options
  )
endif()

option(
  RVEMU_BUILD_BENCHMARKS
  "Build the reproducible RV32IM benchmark harness"
  OFF
)

if(RVEMU_BUILD_BENCHMARKS OR BUILD_TESTING)
  add_library(rvemu_benchmark_support STATIC
    benchmarks/benchmark_workload.cpp
  )
  add_library(rvemu::benchmark_support ALIAS rvemu_benchmark_support)
  target_include_directories(rvemu_benchmark_support PUBLIC benchmarks)
  target_link_libraries(
    rvemu_benchmark_support
    PUBLIC rvemu::core
    PRIVATE rvemu_project_options
  )
endif()

if(RVEMU_BUILD_BENCHMARKS)
  option(
    RVEMU_USE_SYSTEM_BENCHMARK
    "Use an installed Google Benchmark package"
    OFF
  )

  if(RVEMU_USE_SYSTEM_BENCHMARK)
    find_package(benchmark CONFIG REQUIRED)
  else()
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googlebenchmark
      GIT_REPOSITORY https://github.com/google/benchmark.git
      GIT_TAG 192ef10025eb2c4cdd392bc502f0c852196baa48
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(googlebenchmark)
  endif()

  add_executable(rvemu_benchmarks
    benchmarks/rvemu_benchmarks.cpp
  )
  target_link_libraries(
    rvemu_benchmarks
    PRIVATE
      rvemu::benchmark_support
      benchmark::benchmark
      rvemu_project_options
  )

  if(BUILD_TESTING)
    add_test(
      NAME rvemu_benchmarks_smoke
      COMMAND rvemu_benchmarks --benchmark_dry_run
    )
  endif()
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

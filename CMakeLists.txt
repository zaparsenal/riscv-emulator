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
  src/cpu_state.cpp
  src/elf_loader.cpp
  src/execution_engine.cpp
  src/instruction.cpp
  src/memory.cpp
  src/program_session.cpp
)
add_library(rvemu::core ALIAS rvemu_core)

target_include_directories(rvemu_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(rvemu_core PRIVATE rvemu_project_options)

option(
  RVEMU_BUILD_ACT_SMOKE_RUNNER
  "Build the runner for self-checking ACT 4 RV32I/M ELF files"
  ON
)

if(RVEMU_BUILD_ACT_SMOKE_RUNNER OR BUILD_TESTING)
  add_library(rvemu_act_smoke_support STATIC
    tools/act_smoke.cpp
  )
  target_include_directories(rvemu_act_smoke_support PUBLIC tools)
  target_link_libraries(
    rvemu_act_smoke_support
    PUBLIC rvemu::core
    PRIVATE rvemu_project_options
  )
endif()

if(RVEMU_BUILD_ACT_SMOKE_RUNNER)
  add_executable(rvemu_act_smoke tools/act_smoke_main.cpp)
  set_target_properties(rvemu_act_smoke PROPERTIES OUTPUT_NAME rvemu-act-smoke)
  target_link_libraries(
    rvemu_act_smoke
    PRIVATE rvemu_act_smoke_support rvemu_project_options
  )
endif()

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

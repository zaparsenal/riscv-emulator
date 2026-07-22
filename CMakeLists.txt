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

if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

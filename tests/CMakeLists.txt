include(FetchContent)

option(
  RVEMU_USE_SYSTEM_GTEST
  "Use an installed GoogleTest package instead of the pinned source dependency"
  OFF
)

if(RVEMU_USE_SYSTEM_GTEST)
  find_package(GTest CONFIG REQUIRED)
else()
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(rvemu_tests
  act_smoke_test.cpp
  control_flow_instructions_test.cpp
  cpu_state_test.cpp
  elf_loader_test.cpp
  execution_engine_test.cpp
  immediate_instructions_test.cpp
  instruction_test.cpp
  linux_syscalls_test.cpp
  memory_test.cpp
  memory_instructions_test.cpp
  m_extension_instructions_test.cpp
  program_session_test.cpp
  register_instructions_test.cpp
  rvemu_cli_test.cpp
  stack_test.cpp
  system_instructions_test.cpp
)
target_link_libraries(
  rvemu_tests
  PRIVATE
    rvemu::core
    rvemu::act_smoke_support
    rvemu::cli_support
    rvemu_project_options
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(rvemu_tests)

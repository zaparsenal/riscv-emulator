add_library(rvemu_project_options INTERFACE)
target_compile_features(rvemu_project_options INTERFACE cxx_std_20)

if(MSVC)
  target_compile_options(rvemu_project_options INTERFACE /W4 /permissive-)
else()
  target_compile_options(
    rvemu_project_options
    INTERFACE
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
  )
endif()

option(
  RVEMU_ENABLE_SANITIZERS
  "Enable AddressSanitizer and UndefinedBehaviorSanitizer"
  OFF
)

if(RVEMU_ENABLE_SANITIZERS)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(
      rvemu_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
    target_link_options(
      rvemu_project_options
      INTERFACE
        -fno-omit-frame-pointer
        -fsanitize=address,undefined
    )
  else()
    message(FATAL_ERROR "RVEMU_ENABLE_SANITIZERS requires Clang or GCC")
  endif()
endif()

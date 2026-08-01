#pragma once

#include <cstddef>
#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#define RVEMU_WEB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define RVEMU_WEB_EXPORT
#endif

extern "C" {

RVEMU_WEB_EXPORT const char* rvemu_initialize();
RVEMU_WEB_EXPORT std::uint8_t* rvemu_allocate(std::size_t size);
RVEMU_WEB_EXPORT void rvemu_deallocate(std::uint8_t* pointer);
RVEMU_WEB_EXPORT const char* rvemu_load_elf(const std::uint8_t* image,
                                           std::size_t size);
RVEMU_WEB_EXPORT const char* rvemu_reset();
RVEMU_WEB_EXPORT const char* rvemu_step();
RVEMU_WEB_EXPORT const char* rvemu_run_batch(std::uint32_t maximum_steps);
RVEMU_WEB_EXPORT const char* rvemu_set_breakpoint(std::uint32_t address,
                                                 int enabled);
RVEMU_WEB_EXPORT const char* rvemu_read_memory(std::uint32_t address,
                                              std::uint32_t size);
RVEMU_WEB_EXPORT const char* rvemu_snapshot();

}

#undef RVEMU_WEB_EXPORT

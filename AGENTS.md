# AGENTS.md

## Purpose

This repository implements a correctness-first RV32IM CPU emulator and
performance analyzer in C++20. The intended end state can load real RISC-V ELF
programs, provide a small syscall environment and interactive debugger, model
caches and branch predictors, run reproducible benchmarks, and use QEMU or
Spike for differential validation where practical.

Do not claim support or performance that has not been implemented and measured.
Correctness takes priority over optimization.

## Current state

Milestone 1 is complete. The repository has:

- C++20/CMake project scaffolding
- a reusable `rvemu::core` library
- `CpuState` with 32 registers, a 32-bit PC, reset, checked register indices,
  and enforced zero-register behavior
- `Memory` with a configurable 32-bit base address, zero initialization,
  checked byte/halfword/word operations, natural-alignment enforcement, and
  explicit little-endian encoding
- typed memory errors carrying address, width, and read/write operation details
- GoogleTest unit tests
- optional AddressSanitizer and UndefinedBehaviorSanitizer support
- GCC and Clang GitHub Actions CI
- a pinned GoogleTest source dependency by default, avoiding host-package ABI
  mismatches; `RVEMU_USE_SYSTEM_GTEST=ON` is an explicit opt-in

There is no instruction decoder, execution loop, ISA implementation, executable,
ELF loader, syscall layer, debugger, performance model, or benchmark suite yet.

## Architecture and directory structure

```text
include/rvemu/cpu_state.hpp  Public CPU-state API
include/rvemu/memory.hpp     Public checked-memory API and error types
src/cpu_state.cpp            CPU-state implementation
src/memory.cpp               Little-endian memory implementation
tests/                       Focused GoogleTest unit tests
cmake/ProjectOptions.cmake   Warnings, C++20, and sanitizer settings
.github/workflows/ci.yml     GCC/Clang sanitizer CI
README.md                    Human-facing project documentation
```

The CMake library target is `rvemu_core`, with the namespaced alias
`rvemu::core`.

## Design decisions

- Architectural words and registers use `std::uint32_t`. Use explicit bit-level
  conversions for signed ISA behavior later; do not rely on signed overflow.
- `CpuState` does not expose its register array. Reads and writes validate the
  index, and writes to `x0` are ignored.
- PC addition uses unsigned 32-bit wraparound. Instruction alignment is an
  execution-layer concern.
- `Memory` models one contiguous region and translates guest addresses to
  offsets. The exclusive end is represented with `std::uint64_t` so a mapping
  may validly end at `0x1'0000'0000`.
- Byte accesses may use any mapped address. Halfword and word accesses require
  natural alignment and throw `MemoryMisalignmentError` otherwise.
- Bounds and alignment are checked before writes, so a failed operation cannot
  partially mutate memory.
- Guest failures currently surface as typed C++ exceptions. A future execution
  layer should translate applicable errors into architectural traps.
- Multi-byte memory values are assembled explicitly as little-endian; host
  endianness must not affect behavior.

## Coding conventions

- Require C++20 and keep public code in namespace `rvemu`.
- Prefer fixed-width integer types for architectural state and encoded fields.
- Mark observations `[[nodiscard]]` and non-throwing operations `noexcept` where
  accurate.
- Avoid implementation-defined signed shifts and signed overflow. Make sign
  extension and truncation explicit and test their boundary cases.
- Keep interfaces small and independently testable.
- Compile project-owned code with the warning set in
  `cmake/ProjectOptions.cmake`; new code must be warning-clean on GCC and Clang.
- Add focused tests for every instruction and subsystem, including failures and
  architectural edge cases.
- Do not weaken tests to accept incorrect behavior, fabricate benchmark data, or
  silently turn invalid guest behavior into a host crash.

## Build and test commands

Preferred local validation:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRVEMU_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To build without tests or downloading GoogleTest:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel
```

## Workflow expectations

Before editing, inspect the directory, Git status, current branch, configured
remote, and this file. Preserve all user changes. Work in small milestones.
After a meaningful change:

1. Build the project.
2. Run relevant tests, preferably with sanitizers.
3. Update `README.md` and this file when behavior, architecture, commands, or
   milestone status changes.
4. Make a focused commit with a descriptive message.
5. Push to the configured remote. If it fails, retain local commits and report
   the exact error.

Do not combine unrelated changes, rewrite history, discard user work, or publish
unmeasured performance claims.

## Milestones and next steps

Completed:

- Milestone 1: project scaffolding, CPU state, checked little-endian memory,
  foundational tests, sanitizer option, and CI.

Next milestone:

- Define decoded instruction data and architectural trap/result types.
- Implement 32-bit instruction fetch with alignment and bounds validation.
- Build a fetch-decode-execute step/run API without prematurely implementing the
  whole ISA.
- Add focused tests for fetch, invalid encodings, PC updates, and trap behavior.
- Update both documentation files, validate, commit, and push.

Later milestones, in order: complete RV32I, RV32M, ELF loading, syscalls,
interactive debugging, cache/branch models, reproducible benchmarks, and
differential testing.

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

Milestones 1 and 2 are complete. The repository has:

- C++20/CMake project scaffolding
- a reusable `rvemu::core` library
- `CpuState` with 32 registers, a 32-bit PC, reset, checked register indices,
  and enforced zero-register behavior
- `Memory` with a configurable 32-bit base address, zero initialization,
  checked byte/halfword/word operations, natural-alignment enforcement, and
  explicit little-endian encoding
- typed memory errors carrying address, width, and read/write operation details
- `DecodedInstruction` and `decode_instruction` support for R/I/S/B/U/J layouts,
  RV32I major-opcode recognition, and explicitly sign-extended immediates
- architectural instruction trap causes and variant-based step/run result types
- `ExecutionEngine` with aligned fetch, decode, a strict instruction limit, and
  first-trap termination
- tested `ADDI` execution with modulo-2^32 arithmetic and enforced `x0`
- GoogleTest unit tests
- optional AddressSanitizer and UndefinedBehaviorSanitizer support
- GCC and Clang GitHub Actions CI
- a pinned GoogleTest source dependency by default, avoiding host-package ABI
  mismatches; `RVEMU_USE_SYSTEM_GTEST=ON` is an explicit opt-in

`ADDI` is the only executable instruction, so RV32I remains incomplete. There is
no executable, ELF loader, syscall layer, debugger, performance model, or
benchmark suite yet.

## Architecture and directory structure

```text
include/rvemu/cpu_state.hpp  Public CPU-state API
include/rvemu/instruction.hpp Public decoded-instruction types and decoder
include/rvemu/execution_engine.hpp Public trap, result, and execution-loop API
include/rvemu/memory.hpp     Public checked-memory API and error types
src/cpu_state.cpp            CPU-state implementation
src/instruction.cpp          Format decoding and immediate reconstruction
src/execution_engine.cpp     Fetch, ADDI execution, step, and bounded run
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
- Direct memory API failures surface as typed C++ exceptions. The execution
  layer translates instruction-fetch failures into architectural traps and
  should do the same for applicable data-access errors in later ISA work.
- Multi-byte memory values are assembled explicitly as little-endian; host
  endianness must not affect behavior.
- The decoder recognizes major opcodes and normalizes the fields relevant to
  each format. Instruction-specific legality checks belong to execution.
- Encoded immediates remain `std::uint32_t`; sign extension uses unsigned bit
  operations so later arithmetic has defined modulo-2^32 behavior.
- `StepResult` and `RunResult` are variants, preventing invalid success/trap
  combinations. A trapping instruction is not counted as retired and leaves the
  PC at its address.
- Instruction fetch requires four-byte alignment. Memory failures during fetch
  become `InstructionAccessFault`; unknown or unimplemented instructions become
  `IllegalInstruction` until implemented.
- `run()` always takes a strict instruction limit. This makes tests deterministic
  and prevents an accidental unbounded host loop.

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

When concluding a work session, give the user two concise summaries: **Done**
for what changed and **Next** for the next logical milestone.

## Milestones and next steps

Completed:

- Milestone 1: project scaffolding, CPU state, checked little-endian memory,
  foundational tests, sanitizer option, and CI.
- Milestone 2: decoded instruction formats, aligned fetch, architectural
  instruction traps, bounded step/run execution, and the initial `ADDI`
  implementation.

Next milestone:

- Implement the RV32I upper-immediate and integer-immediate instruction groups:
  `LUI`, `AUIPC`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, `SLLI`, `SRLI`, and
  `SRAI` (with existing `ADDI`).
- Validate reserved shift-immediate encodings and trap without architectural
  side effects.
- Add focused signed-boundary, shift-count, overflow, `x0`, and PC-relative tests.
- Update both documentation files, validate, commit, and push.

Later RV32I slices should cover register-register ALU, control flow, memory,
fence/system behavior, and then complete RV32I validation. After that: RV32M,
ELF loading, syscalls, interactive debugging, cache/branch models, reproducible
benchmarks, and differential testing.

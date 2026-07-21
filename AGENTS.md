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

Milestones 1 through 3 are complete, including the full base RV32I instruction
set. The repository has:

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
- tested execution of `LUI`, `AUIPC`, `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`,
  `ANDI`, `SLLI`, `SRLI`, `SRAI`, `ADD`, `SUB`, `SLL`, `SLT`, `SLTU`, `XOR`,
  `SRL`, `SRA`, `OR`, `AND`, `JAL`, `JALR`, `BEQ`, `BNE`, `BLT`, `BGE`,
  `BLTU`, `BGEU`, `LB`, `LH`, `LW`, `LBU`, `LHU`, `SB`, `SH`, `SW`, `FENCE`,
  `ECALL`, and `EBREAK`
- modulo-2^32 arithmetic, PC-relative wraparound, host-independent signed
  comparison and arithmetic shift, effective-address wraparound, and enforced
  `x0`
- five-bit masking of register-provided shift counts
- strict reserved ALU-encoding validation before architectural mutation
- precise, atomic control-flow target validation and link-register updates
- architectural data-access traps with precise destination, memory, and PC
  behavior
- conservative single-hart `FENCE` execution and precise, non-retiring
  breakpoint and user-environment-call traps
- GoogleTest unit tests
- optional AddressSanitizer and UndefinedBehaviorSanitizer support
- GCC and Clang GitHub Actions CI
- a pinned GoogleTest source dependency by default, avoiding host-package ABI
  mismatches; `RVEMU_USE_SYSTEM_GTEST=ON` is an explicit opt-in

RV32I is complete. RV32M is not implemented. There is no executable, ELF loader,
syscall layer, debugger, performance model, or benchmark suite yet.

## Architecture and directory structure

```text
include/rvemu/cpu_state.hpp  Public CPU-state API
include/rvemu/instruction.hpp Public decoded-instruction types and decoder
include/rvemu/execution_engine.hpp Public trap, result, and execution-loop API
include/rvemu/memory.hpp     Public checked-memory API and error types
src/cpu_state.cpp            CPU-state implementation
src/instruction.cpp          Format decoding and immediate reconstruction
src/execution_engine.cpp     Fetch, current ISA execution, step, and bounded run
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
  layer translates instruction-fetch and data-access failures into precise
  architectural traps.
- Multi-byte memory values are assembled explicitly as little-endian; host
  endianness must not affect behavior.
- The decoder recognizes major opcodes and normalizes the fields relevant to
  each format. Instruction-specific legality checks belong to execution.
- Encoded immediates remain `std::uint32_t`; sign extension uses unsigned bit
  operations so later arithmetic has defined modulo-2^32 behavior.
- Signed comparisons XOR both operands with `0x80000000` before unsigned
  comparison. This produces RV32 signed ordering without host signed casts.
- Arithmetic right shift uses an unsigned logical shift plus an explicit
  sign-fill mask. Never replace it with a right shift of a negative host value.
- RV32I shift-immediate encodings are legal only with `funct7 == 0x00` for
  `SLLI`/`SRLI`, or `funct7 == 0x20` for `SRAI`. Reserved encodings trap before
  register or PC mutation. RV32I leaves reserved-instruction behavior
  unspecified; deterministic `IllegalInstruction` is this emulator's platform
  policy.
- RV32I register-register ALU encodings use `funct7 == 0x00`, except `SUB` and
  `SRA`, which use `0x20`. Register shift counts are always masked with `0x1f`.
- Both register operands are read before the destination is written. Preserve
  this ordering so destination/source aliasing remains architecturally correct.
- `funct7 == 0x01` register operations are RV32M encodings. They intentionally
  trap until the M extension milestone and must not be mislabeled as RV32I.
- JAL and taken branches add their sign-extended immediate to the address of the
  current instruction using modulo-2^32 arithmetic. JALR adds its immediate to
  the original `rs1` value, then clears target bit zero.
- Under `IALIGN=32`, taken jump/branch targets must be four-byte aligned. Check
  alignment before writing a link register or PC. Untaken branches must not
  validate or trap on their unused target.
- Control-flow execution uses pending effects: optional destination value plus
  next PC. Preserve this commit-after-validation structure for precise traps.
- Loads and stores add their sign-extended 12-bit immediate to the original
  `rs1` value with unsigned 32-bit wraparound. Read source operands before
  committing any destination so load and store aliasing remains correct.
- `LB` and `LH` use explicit unsigned sign extension; `LBU` and `LHU` zero
  extend. Stores explicitly truncate to 8 or 16 bits where applicable.
- Translate data misalignment and bounds failures to the corresponding load or
  store architectural trap. A faulting load must not change `rd`; a faulting
  store must not partially change memory; neither may advance the PC. Loads to
  `x0` must still access memory and may still trap.
- Validate reserved load/store `funct3` fields before attempting memory access.
  Their deterministic `IllegalInstruction` trap carries the raw instruction,
  not an effective address.
- In the current synchronous single-hart memory model, `FENCE` has no additional
  runtime action beyond retiring normally. Accept every `MISC-MEM` encoding with
  `funct3 == 0`, including nonzero `rd`/`rs1` and reserved `fm` or set fields:
  the base specification requires conservative forward-compatible treatment.
- `FENCE.I` has `funct3 == 1` and belongs to `Zifencei`; it remains an illegal
  instruction until that extension is intentionally added.
- Recognize only the exact `ECALL` (`0x00000073`) and `EBREAK` (`0x00100073`)
  encodings in the base `SYSTEM` opcode. Other `SYSTEM` encodings include
  privileged or `Zicsr` instructions and remain illegal.
- `ECALL` and `EBREAK` are precise requested traps: do not advance the PC,
  mutate registers, or count the instruction as retired. Their trap value is
  zero. With no privilege-mode state yet, classify `ECALL` as
  `EnvironmentCallFromUserMode`; the future user-level syscall layer will
  interpret that trap.
- JALR only accepts `funct3 == 0`. Branch `funct3` values 2 and 3 are reserved;
  this emulator deterministically returns `IllegalInstruction` without mutation.
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
- Milestone 3a: RV32I upper-immediate and integer-immediate instructions with
  signed-boundary, shift, PC-relative, reserved-encoding, and `x0` tests.
- Milestone 3b: RV32I register-register ALU instructions with exact `funct7`
  validation, five-bit shift masking, overflow, aliasing, and `x0` tests.
- Milestone 3c: RV32I jumps and conditional branches with precise target
  alignment traps, signed/unsigned decisions, aliasing, wraparound, backward
  targets, and bounded-loop tests.
- Milestone 3d: RV32I loads and stores with sign/zero extension, truncation,
  little-endian access, effective-address wraparound, aliasing, and precise data
  traps.
- Milestone 3e: RV32I `FENCE`, `ECALL`, and `EBREAK` with forward-compatible
  fence handling, exact system encodings, and precise non-retiring traps. Base
  RV32I is complete.

Next milestone:

- Implement all eight RV32M operations: `MUL`, `MULH`, `MULHSU`, `MULHU`,
  `DIV`, `DIVU`, `REM`, and `REMU`.
- Use explicit-width unsigned arithmetic and bit-level signed handling so host
  overflow and signed-shift behavior cannot affect architectural results.
- Test high-word multiplication, signed boundaries, division by zero, the
  `INT32_MIN / -1` overflow case, register aliasing, writes to `x0`, and strict
  `funct7 == 0x01` decoding.
- Update both documentation files, validate, commit, and push.

After RV32M: ELF loading, syscalls, interactive debugging, cache/branch models,
reproducible benchmarks, and differential testing.

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

Milestones 1 through 6 are complete, including the full RV32IM instruction set,
static ELF32 loading, hosted syscalls, freestanding stack setup, and a runnable
command-line frontend. The first interactive-debugger milestone is also
complete. The repository has:

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
  `ECALL`, `EBREAK`, `MUL`, `MULH`, `MULHSU`, `MULHU`, `DIV`, `DIVU`, `REM`,
  and `REMU`
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
- host-independent full-width RV32M products and signed division behavior
- `load_elf32` for validated in-memory ELF images and `load_elf32_file` for
  loading a real file through the same parser
- explicit little-endian parsing of static ELF32 RISC-V `ET_EXEC` headers and
  program headers without casting untrusted bytes to host structures
- complete prevalidation, virtual-address `PT_LOAD` copying, memory-tail zero
  fill, executable entry-point initialization, and all-or-nothing failure
- typed ELF load failures covering format, compatibility, bounds, alignment,
  ordering, overlap, entry point, and file-I/O problems
- successful ELF metadata containing the lowest and exclusive-highest occupied
  addresses across nonempty loadable segments
- `ProgramSession`, which delegates normal execution to `ExecutionEngine` and
  dispatches precise user environment-call traps through an injected handler
- RV32 Linux-style environment-call capture from `a7` and `a0` through `a5`
- explicit resume, exit, and unhandled call outcomes with precise register and
  PC effects
- separate guest-step, instruction-retirement, and handled-call statistics
- pre-execution host breakpoints that do not modify guest memory
- `Memory::read_span` for a single checked, unaligned, zero-copy, read-only byte
  range used by host services
- `LinuxSyscallEnvironment` with injected stdout/stderr output, `write` (64),
  `exit` (93), `exit_group` (94), Linux-style error returns, and precise session
  integration
- `initialize_freestanding_stack` with checked 16-byte alignment, image-overlap
  detection, typed errors, and failure nonmutation
- a pure CLI parser, binary-safe standard-stream sink, typed hosted runner, and
  `rvemu` executable with configurable mapping, stack, and strict step limit
- normal guest exit-status propagation and precise diagnostics for loader,
  stack, limit, trap, unhandled-call, breakpoint, and host failures
- `InteractiveDebugger` with a strict cumulative step budget, host breakpoints,
  single stepping, register display, checked byte-memory display, and scripted
  stream injection for deterministic tests
- `rvemu-act-smoke` support that classifies self-checking ELF pass/fail outcomes
  separately from loader, trap, unknown-call, breakpoint, and limit failures
- an immutable official ACT image pin, observed tool versions, I/M-oriented UDB
  and derived Sail configuration, clean generation harness, compatibility
  auditor, non-privileged halt macros, linker script, and ten-test smoke manifest
- GoogleTest unit tests
- optional AddressSanitizer and UndefinedBehaviorSanitizer support
- GCC and Clang GitHub Actions CI
- a pinned GoogleTest source dependency by default, avoiding host-package ABI
  mismatches; `RVEMU_USE_SYSTEM_GTEST=ON` is an explicit opt-in

RV32IM, the static ELF loader, shared program-session contract, minimal
output/termination syscalls, freestanding stack initializer, and command-line
runner are complete. There is no full process-startup image, interactive
debugger beyond the initial command set, performance model, or benchmark suite
yet.

The ACT 4 harness generated all 47 selected non-privileged RV32I/M ELFs from
Sail results, but it is not a conformance result. The compatibility audit stops
before execution because all files are spuriously RVC-flagged and each contains
three unsupported CSR reads in ACT's failure-diagnostic path.

## Architecture and directory structure

```text
include/rvemu/cpu_state.hpp  Public CPU-state API
include/rvemu/elf_loader.hpp Public ELF load results and file/span loader APIs
include/rvemu/instruction.hpp Public decoded-instruction types and decoder
include/rvemu/execution_engine.hpp Public trap, result, and execution-loop API
include/rvemu/linux_syscalls.hpp Public output sink and hosted Linux-call API
include/rvemu/memory.hpp     Public checked-memory API and error types
include/rvemu/program_session.hpp Public hosted-run and environment-call API
include/rvemu/stack.hpp      Public freestanding stack-initialization API
app/rvemu_cli.hpp            Testable command-line and hosted-run API
app/rvemu_cli.cpp            Parsing, stream output, orchestration, diagnostics
app/rvemu_main.cpp           Process entry point and exception boundary
app/rvemu_debugger.hpp       Interactive debugger result and command-loop API
app/rvemu_debugger.cpp       Breakpoints, stepping, and state inspection
tools/act_smoke.hpp         Typed self-checking ELF smoke-runner API
tools/act_smoke.cpp         ACT load, execution, and result classification
tools/act_smoke_main.cpp    `rvemu-act-smoke` reporting executable
conformance/act4/           Pinned ACT inputs, macros, linker, and manifest
src/cpu_state.cpp            CPU-state implementation
src/elf_loader.cpp           ELF32 parsing, validation, and transactional load
src/instruction.cpp          Format decoding and immediate reconstruction
src/execution_engine.cpp     Fetch, current ISA execution, step, and bounded run
src/linux_syscalls.cpp       Hosted write/exit syscall policy and validation
src/memory.cpp               Little-endian memory implementation
src/program_session.cpp      Hosted execution, call dispatch, and breakpoints
src/stack.cpp                Checked 16-byte-aligned stack placement
tests/                       Focused GoogleTest unit tests
cmake/ProjectOptions.cmake   Warnings, C++20, and sanitizer settings
.github/workflows/ci.yml     GCC/Clang sanitizer CI
README.md                    Human-facing project documentation
```

The CMake library target is `rvemu_core`, with the namespaced alias
`rvemu::core`. `rvemu_cli_support` (`rvemu::cli_support`) contains the testable
frontend policy, and the `rvemu_cli` target produces the `rvemu` executable.

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
- `Memory::read_span` performs the same widened bounds validation as scalar
  reads but imposes no alignment. Its returned view remains valid while the
  same `Memory` object exists and has not been moved; writes and `clear()` may
  change the viewed bytes but never resize the backing storage.
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
- `funct7 == 0x01` register operations are RV32M encodings. Their `funct3`
  values 0 through 7 map in order to `MUL`, `MULH`, `MULHSU`, `MULHU`, `DIV`,
  `DIVU`, `REM`, and `REMU`; other `funct7` values must continue through the
  strict RV32I legality checks.
- Form multiplication results in unsigned 64-bit space. For signed high-word
  variants, convert each signed operand to unsigned magnitude, multiply, and
  apply two's-complement negation to the 64-bit product when required. Do not
  use host signed multiplication or implementation-defined casts.
- Signed division similarly operates on unsigned magnitudes and reapplies the
  quotient and dividend signs. This rounds toward zero and handles
  `0x80000000 / 0xffffffff` without host signed overflow.
- RV32M division never traps. A zero divisor returns `0xffffffff` for `DIV` and
  `DIVU`, while `REM` and `REMU` return the dividend. Signed division overflow
  returns `0x80000000` with remainder zero.
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
- `ProgramSession` is a host-policy layer above `ExecutionEngine`; do not move
  syscall behavior into instruction execution. Normal instructions and
  non-`ECALL` traps must preserve the existing engine semantics.
- Capture a user environment call only after an exact
  `EnvironmentCallFromUserMode` trap. Snapshot `a7` as its number and `a0`
  through `a5` as its six arguments before invoking the handler.
- Environment-call handlers receive `const Memory&`. Current output and exit
  policies do not require guest mutation, and keeping the view read-only avoids
  hidden partial state changes on a failed call.
- A resumed call writes only its result to `a0` and advances the precise trap PC
  by four using architectural wraparound. An exit or unhandled decision leaves
  CPU and memory state at the trapping `ECALL`; the event preserves the original
  call snapshot.
- A session guest step is one retired instruction, one resumed environment
  call, or one terminating environment call. Keep guest steps, architectural
  retirement, and handled-call counts separate so future performance models do
  not treat host-handled `ECALL`s as retired CPU instructions.
- Session breakpoints are host-side PC addresses checked before execution. They
  must not patch guest memory. `step()` bypasses breakpoints so a debugger can
  deliberately advance after stopping.
- The supported generated-code target is little-endian RV32IM, `IALIGN=32`, and
  the ILP32 soft-float ABI. Compile fixtures explicitly with
  `-march=rv32im -mabi=ilp32`; do not let default RVC or floating-point targets
  silently enter tests.
- `LinuxSyscallEnvironment` recognizes only `write` 64, `exit` 93, and
  `exit_group` 94. Unsupported calls resume with two's-complement `-ENOSYS`;
  they are handled host calls, not unhandled architectural traps.
- Hosted `write` accepts only file descriptors 1 and 2 and maps them to distinct
  sink channels. Reject other descriptors with `-EBADF` before inspecting the
  guest pointer, including for a zero count. A zero count on a recognized
  descriptor succeeds without inspecting the pointer or calling the sink.
- Clamp a nonzero requested write to Linux's `0x7ffff000` transfer maximum, then
  obtain one complete `Memory::read_span` before calling the sink. Invalid
  ranges return `-EFAULT` with no sink call. This validation is atomic with
  respect to guest memory access, including at the top of the address space.
- Call the output sink at most once. A valid partial prefix resumes with that
  count and is not retried. A failed result, thrown exception, or count larger
  than the offered span maps to `-EIO`. `OutputWriteFailed` contractually means
  zero bytes were consumed; sinks must not throw after producing side effects
  because an unknown partial result cannot be reconstructed.
- `exit` and `exit_group` both terminate this single-hart environment with
  `a0 & 0xff`. The session exit event retains the raw captured `a0`; CPU state
  remains at the precise `ECALL`.
- The ACT smoke runner uses a separate exit-only environment: status zero is a
  self-check pass and any raw nonzero status is a test failure. Unknown calls
  remain infrastructure failures rather than being converted to `-ENOSYS`.
- ACT smoke execution uses a fixed default mapping at `0x80000000`, 16 MiB of
  memory, and a 50,000,000 guest-step limit. Loader errors, traps, unhandled
  calls, breakpoints, and limit exhaustion must never be mislabeled as a failed
  architectural assertion.
- `rvemu-act-smoke` prints ACT's exact `RVCP-SUMMARY` form only for pass or test
  failure. Its process statuses are 0 pass, 1 test failure, and 2 infrastructure
  failure.
- `RVEMU_BUILD_ACT_SMOKE_RUNNER` defaults off. Test-enabled builds compile the
  runner regardless, and no-test builds may opt in explicitly. With the CLI's
  default enabled, `BUILD_TESTING=OFF` builds `rvemu_core` and `rvemu`; add
  `RVEMU_BUILD_CLI=OFF` for a core-only build.
- Treat `conformance/act4/pins.json` and its adjacent macro, linker, manifest,
  configuration, audit, generation script, and README as the current runner
  contract. The official image is pinned by immutable multi-arch digest with
  Sail 0.12, GCC 16.1.0, binutils 2.46, and UDB 0.1.13 as observed versions.
- UDB 0.1.13 requires an `Sm` scaffold to define `MXLEN` and `MUTABLE_MISA_M`,
  even for an I/M-only non-privileged selection. Keep privileged tests disabled,
  do not define `STANDARD_SM_SUPPORTED`, and do not mistake the scaffold for
  machine-mode or CSR support.
- The pinned ACT run selects 47 non-privileged RV32I/M tests and completes 188
  build tasks. Audit every generated ELF before selecting the ten-file smoke
  subset. Unknown objdump directives, non-32-bit encodings, unsupported
  opcodes, unexpected system instructions, flags, attributes, or ECALL counts
  must block execution.
- Current pinned output is intentionally blocked: 47 RVC flags, zero 16-bit
  instructions, and 141 unsupported CSR reads (mcause, mtval, and mstatus once
  per ELF) in `failedtest_trap_x7_x9`. Do not clear flags, relax the loader, or
  claim a conformance pass while that diagnostic dependency remains.
- The ELF loader supports fixed-address, 32-bit, little-endian RISC-V `ET_EXEC`
  files. It intentionally rejects `ET_DYN`; load bias, dynamic relocations, and
  an interpreter are not modeled.
- Parse ELF fields with explicit little-endian byte assembly. Never cast a file
  buffer to `Elf32_Ehdr` or `Elf32_Phdr`, because alignment, host endianness,
  structure layout, and untrusted bounds must not influence correctness.
- Require ELF class 32, little-endian encoding, current identification and file
  versions, `EM_RISCV`, exact ELF32 header sizes, System V/unspecified OS ABI
  version 0, and `e_flags == 0`. The flags policy excludes RVC, hard-float ABI,
  RVE, TSO, RV64ILP32, RVY, reserved, and non-standard modes that current RV32IM
  execution cannot promise to support.
- `e_flags == 0` does not fully identify the required ISA. Additional extension
  requirements can live in `.riscv.attributes`; until those are parsed,
  unsupported instructions still fail precisely during execution.
- Validate the complete program-header table and every relevant segment before
  modifying `Memory` or the PC. Range checks use widened unsigned arithmetic so
  file offsets and 32-bit guest addresses cannot wrap.
- Load `PT_LOAD` bytes at `p_vaddr`, never `p_paddr`, and zero-fill
  `p_memsz - p_filesz`. Preserve registers and bytes outside segment ranges.
- Enforce the generic ABI's ascending `p_vaddr` order and alignment congruence.
  Reject intersecting nonempty destination ranges as a conservative platform
  policy because the generic ABI defines no byte-level overlap precedence.
- Require a four-byte-aligned entry point inside a nonempty executable
  `PT_LOAD`. This is an emulator platform policy derived from RV32IM's current
  `IALIGN=32`, not a generic ELF structural rule.
- Reject `PT_INTERP`, `PT_DYNAMIC`, and `PT_TLS` until their runtime facilities
  exist. Ignore unknown non-loadable headers and `p_paddr` fields.
- The flat `Memory` model cannot enforce ELF segment permissions. `PF_X` is
  currently used only to validate the entry point.
- Successful ELF loads report the envelope from the first nonempty `PT_LOAD`
  virtual address through the last segment's exclusive end. Runtime placement
  uses this metadata rather than guessing occupancy from memory contents.
- `initialize_freestanding_stack` aligns the mapping end down to 16 bytes,
  rounds the requested size up to 16 bytes, and reserves the range immediately
  below that top. An image ending exactly at the stack bottom is valid.
- Stack validation completes before mutation. Success writes only `x2`; it does
  not alter the PC, other registers, or any memory bytes. A mapping ending at
  `0x1'0000'0000` intentionally encodes its initial RV32 `sp` as zero.
- The stack reservation is descriptive in flat memory: there is no guard page
  or enforcement if guest code moves below its bottom. The current freestanding
  startup does not write `argc`, `argv`, environment data, or an auxiliary
  vector.
- CLI numeric options accept complete unsigned decimal or `0x` hexadecimal
  forms. Require exactly one ELF path, reject duplicate/zero/overflowing
  settings, support `--`, and retain a strict positive guest-step limit.
- CLI defaults are base `0x80000000`, 16 MiB mapped memory, 1 MiB reserved
  stack, and 50,000,000 guest steps. Normal guest exit returns its low eight
  bits; help returns 0, syntax errors 2, and infrastructure failures 125.
- Keep argument parsing, stream output, run orchestration, and reporting in the
  CLI support library. `main` should remain only the process adapter and final
  exception boundary.
- `StreamOutputSink` calls the selected stream buffer once. A positive partial
  prefix is exact progress; zero or impossible progress is failure and is not
  retried.
- Keep `InteractiveDebugger` in the host frontend layer. It observes
  `CpuState`/`Memory` and drives `ProgramSession`; it must not add debugger
  policy to instruction execution or patch guest code.
- Debugger breakpoints are sorted, unique, four-byte-aligned mapped addresses.
  A continue that follows a breakpoint stop executes that instruction once via
  `ProgramSession::step()` before resuming with the breakpoint set. Removing a
  different breakpoint must not lose this stopped-state behavior.
- All debugger step and continue commands share the CLI's one cumulative guest
  step limit. Aggregate retired-instruction and handled-call counts exactly as
  `ProgramSession` defines them; breakpoint stops and traps consume no step.
- Register inspection is read-only and includes the PC plus all 32 integer
  registers with ABI names. Memory inspection is a checked read-only byte span
  limited to 1-256 bytes; errors remain recoverable command errors.
- Debugger input/output are injected streams for tests. The production CLI uses
  stdin for commands and stderr for the prompt/status so guest stdout remains
  distinct. EOF and explicit quit leave without further guest execution.

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

To build the CLI without tests or downloading GoogleTest:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF
cmake --build build --parallel
```

To build only `rvemu::core`:

```sh
cmake -S . -B build-core \
  -DBUILD_TESTING=OFF \
  -DRVEMU_BUILD_CLI=OFF
cmake --build build-core --parallel
```

To compile the ACT smoke runner without tests:

```sh
cmake -S . -B build-act \
  -DBUILD_TESTING=OFF \
  -DRVEMU_BUILD_ACT_SMOKE_RUNNER=ON
cmake --build build-act --parallel
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
- Milestone 4: all eight RV32M operations with full-width multiplication,
  host-independent signed arithmetic, defined zero-divisor/overflow behavior,
  aliasing, `x0`, and strict-encoding tests. RV32IM is complete.
- Milestone 5: static ELF32 loading from spans or files with explicit
  little-endian parsing, strict RISC-V compatibility checks, widened range
  validation, `PT_LOAD` copy/zero-fill behavior, entry-point setup, typed
  failures, transactional state guarantees, and execution integration tests.
- Milestone 6a: a shared `ProgramSession` above the unchanged execution engine,
  with Linux-style call capture, explicit resume/exit/unhandled transitions,
  separate accounting, strict run limits, host breakpoints, and focused tests.
- Milestone 6b: injected stdout/stderr byte output, Linux RV32 `write`, `exit`,
  and `exit_group`, deterministic error returns, zero-copy checked guest ranges,
  partial/failing sink behavior, and complete write-to-exit session tests.
- Milestone 6c: occupied ELF address metadata, checked 16-byte freestanding
  stack placement, a validated CLI, binary-safe host streams, real file
  execution, guest exit propagation, and failure diagnostics.
- Milestone 7a: typed self-checking ACT ELF execution, bounded result
  classification, exact ACT summary output, pinned baseline metadata,
  non-privileged halt/linker inputs, a ten-test I/M smoke manifest, and focused
  runner tests. This is plumbing only; no official ACT result is claimed.
- Milestone 7b: immutable official tool-image pins, validated I/M-oriented UDB
  and derived Sail inputs, reproducible 47-ELF generation, and a strict
  compatibility audit that records the current RVC-flag and CSR blockers before
  emulator execution. No conformance pass is claimed.
- Milestone 8a: an interactive debugger above `ProgramSession` with mapped
  host breakpoints, step-over-on-continue behavior, single stepping, cumulative
  limits/accounting, full integer-register display, checked byte-memory display,
  recoverable command errors, and end-to-end CLI tests.

Next milestone:

- Define one read-only execution-observation event contract before implementing
  performance models. It must capture only facts already established by a
  completed architectural transition (PC/instruction, memory access, and
  control-flow outcome) without changing execution results.
- Once that contract is stable, cache and branch-prediction models may proceed
  in parallel and converge through one owner for event semantics and cycle
  formulas. Do not publish performance figures before the benchmark harness
  measures them.
- Remove ACT's spurious RVC flag and CSR-based failure diagnostics at generation
  time without weakening rvemu's loader or execution target. Then rerun the
  complete 47-file audit, execute the ten-file smoke subset, and add it to CI
  only after a verified pass. Broader generated coverage belongs in scheduled
  or manual CI.
- The ACT baseline is `riscv-arch-test` commit
  `585fbaf97a7df6e2f0fe8808edd3ad839eb1afe3` and official image digest
  `sha256:e2a3982d778cfa5b85502b71dc7cb90891c4119d0b9335f4aa74e71a2206e0e4`.
  Record every pinned input rather than relying on moving tags.
- Do not claim full `riscv-arch-test` coverage while CSR, privilege, and trap
  handler state are absent. Keep RVC, floating point, atomics, and other
  unsupported extensions out of the initial test selection.
- Update both documentation files, validate, commit, and push.

After the observation contract and initial conformance pass: cache/branch
models, reproducible benchmarks, and expanded differential testing against
Sail, QEMU, or Spike.

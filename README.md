# RISC-V Emulator

A correctness-first software implementation of a 32-bit RISC-V processor and
performance-analysis environment, written in C++20. The long-term target is
RV32IM execution of real ELF programs, with debugging, cache and branch models,
reproducible benchmarks, and differential validation against established
emulators.

The project is being built in small, tested milestones. It now has a working
fetch-decode-execute pipeline with complete RV32IM instruction support, a
validated loader for static 32-bit RISC-V ELF executables, and a runnable
`rvemu` command-line frontend. A host-side program session and a small
Linux-style syscall environment provide program output and termination without
changing architectural instruction behavior.

## Current status

The foundational state, memory, execution-loop, RV32I, RV32M, and ELF-loading
milestones are complete:

- 32 unsigned 32-bit integer registers
- an enforced `x0 == 0` invariant
- a 32-bit program counter with defined wraparound behavior
- reset support for CPU state
- contiguous, zero-initialized memory at a configurable 32-bit base address
- bounds-checked byte, halfword, and word accesses
- bounds-checked, zero-copy read-only byte spans for hosted output
- typed memory errors for out-of-bounds and misaligned accesses
- little-endian reads and writes
- decoding for the R, I, S, B, U, and J instruction layouts, including explicit
  sign extension of encoded immediates
- recognition of the RV32I major opcodes
- aligned 32-bit instruction fetch
- typed step and bounded-run results using architectural instruction trap causes
- execution of RV32I upper-immediate, integer-immediate, register-register ALU,
  jump, conditional-branch, load, store, memory-ordering, and environment groups
- execution of all RV32M low/high multiplication, division, and remainder
  operations
- host-independent signed comparisons and arithmetic right shifts
- host-independent signed products, quotient rounding, and remainder signs
- five-bit masking of register-provided shift counts
- strict rejection of reserved ALU encodings without state changes
- precise control-flow traps with atomic PC/link-register effects
- modulo-2^32 effective-address calculation for data accesses
- architectural load/store misalignment and access-fault traps with precise
  register, memory, and PC effects
- conservative `FENCE` execution for the single-hart, strongly ordered memory
  model, including forward-compatible handling of reserved fence fields
- precise, non-retiring `ECALL` and `EBREAK` traps
- bounded execution of loops through the existing instruction-limit API
- loading from either an in-memory byte span or an ELF file path
- explicit parsing of fixed-address, 32-bit, little-endian RISC-V executables
- complete prevalidation of ELF and program headers before architectural state
  changes
- loading of nonoverlapping `PT_LOAD` segments by virtual address, including
  zero-filled memory tails
- atomic load failures that preserve memory and the program counter
- typed ELF load results with stable error categories and optional failing
  program-header indexes
- successful ELF-load metadata describing the complete occupied address
  envelope for later runtime placement
- a `ProgramSession` wrapper that captures user environment calls using the
  RV32 Linux register convention (`a7` plus `a0`-`a5`)
- injectable environment-call handling that can resume execution, terminate a
  program, or preserve the original precise trap as unhandled
- separate counts for guest steps, architecturally retired instructions, and
  handled environment calls
- host breakpoints that stop before execution without modifying guest memory
- injectable stdout/stderr byte output through Linux RV32 `write` (64)
- deterministic termination through `exit` (93) and `exit_group` (94)
- Linux-style `-EBADF`, `-EFAULT`, `-EIO`, and `-ENOSYS` syscall results
- a minimal freestanding stack reservation at the top of mapped memory, with a
  16-byte-aligned initial `sp` and checked separation from the loaded image
- a production-facing `rvemu` executable with validated memory, stack, and
  strict guest-step-limit options
- end-to-end ELF execution with binary-safe stdout/stderr routing, guest exit
  status propagation, and precise loader, stack, limit, and trap diagnostics
- a typed `rvemu-act-smoke` runner for already-generated, self-checking ACT 4
  RV32I/M ELF files
- pinned ACT 4 inputs, a non-privileged halt ABI, linker script, and a ten-test
  I/M smoke manifest without any unearned conformance claim
- GoogleTest coverage for state, memory, instruction formats, fetch, execution,
  signed boundaries, shift limits, operand aliasing, PC-relative wraparound,
  taken and untaken branches, load extension, little-endian stores, effective
  address wraparound, misaligned targets and data, instruction limits, illegal
  instructions, access faults, high-word multiplication, division by zero, and
  signed division overflow, ELF range arithmetic, malformed headers, segment
  alignment and ordering, entry-point validation, transactional loading,
  environment-call transitions, session limits, breakpoints, syscall routing,
  invalid guest output ranges, partial writes, sink failures, exit statuses,
  stack placement and nonmutation, command-line parsing, hosted ELF execution,
  ACT runner pass/fail and infrastructure classifications
- optional AddressSanitizer and UndefinedBehaviorSanitizer instrumentation
- GitHub Actions validation with GCC and Clang

RV32IM execution, static ELF32 loading, the shared program session, minimal
output/termination syscalls, freestanding stack setup, and the command-line
runner are complete. There is currently no full process-startup stack,
interactive debugger, performance model, or published benchmark result.

The ACT 4 smoke runner and reproducible generation/audit harness are also
present, but no official ACT result is claimed. The pinned environment generated
the selected ELFs successfully; a compatibility audit correctly blocked their
execution because ACT emitted unsupported metadata and CSR diagnostics.

## Supported instructions

| Group | Instructions |
| --- | --- |
| Upper immediate | `LUI`, `AUIPC` |
| Integer immediate | `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, `SLLI`, `SRLI`, `SRAI` |
| Register-register ALU | `ADD`, `SUB`, `SLL`, `SLT`, `SLTU`, `XOR`, `SRL`, `SRA`, `OR`, `AND` |
| Integer multiply/divide | `MUL`, `MULH`, `MULHSU`, `MULHU`, `DIV`, `DIVU`, `REM`, `REMU` |
| Jumps | `JAL`, `JALR` |
| Conditional branches | `BEQ`, `BNE`, `BLT`, `BGE`, `BLTU`, `BGEU` |
| Loads | `LB`, `LH`, `LW`, `LBU`, `LHU` |
| Stores | `SB`, `SH`, `SW` |
| Memory ordering | `FENCE` |
| Environment | `ECALL`, `EBREAK` |

Instruction behavior follows the
[RV32I Base Integer Instruction Set, version 2.1](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html)
and the
[M Extension for Integer Multiplication and Division, version 2.0](https://docs.riscv.org/reference/isa/v20260120/unpriv/m-st-ext.html).
Where the specification leaves reserved encodings unspecified, this emulator
chooses a deterministic `IllegalInstruction` trap.

Instructions outside the support table currently return an `IllegalInstruction`
trap. They belong to unsupported extensions, privileged execution, or reserved
encoding space rather than the supported RV32IM target.

The current platform target is little-endian RV32IM with `IALIGN=32` and the
ILP32 soft-float ABI. Toolchain-generated test programs must explicitly use
`-march=rv32im -mabi=ilp32`; compressed, floating-point, atomic, CSR, and
privileged instructions are outside the current scope.

## Architecture

The implementation builds a reusable library, `rvemu::core`, and the `rvemu`
command-line executable:

- `CpuState` owns the 32 general-purpose registers and program counter. Its API
  prevents direct mutation of the register array and ignores all writes to
  `x0`.
- `Memory` owns a contiguous byte region mapped into the 32-bit address space.
  Multi-byte operations are explicitly little-endian. Halfword and word
  operations require natural alignment; invalid accesses throw typed errors
  before modifying memory. `read_span` validates an arbitrary byte range once
  and returns a read-only view without imposing alignment.
- `decode_instruction` recognizes RV32I major opcodes and produces normalized
  register, function, format, and immediate fields. Immediate reconstruction is
  host-independent and avoids signed-overflow assumptions.
- `ExecutionEngine` references a `CpuState` and `Memory`. `step()` fetches,
  decodes, and executes one instruction. `run(limit)` executes until its strict
  instruction limit or the first trap. Both APIs return variants, so successful
  execution cannot be confused with a trap.
- `ProgramSession` owns no architectural state. It wraps an `ExecutionEngine`,
  dispatches precise user environment-call traps to an injected handler,
  applies the handler's explicit resume/exit decision, and supports host-side
  breakpoints and bounded program runs.
- `load_elf32` and `load_elf32_file` validate a complete executable before
  loading its segments into `Memory` and setting the entry-point PC. Failures
  return an `ElfLoadFailure`; successful loads report the segment and byte
  counts they actually loaded plus the occupied address envelope.
- `initialize_freestanding_stack` reserves a configurable range at the top of
  mapped memory, rejects overlap with the loaded image, and writes only the
  16-byte-aligned initial stack pointer in `x2`.
- The CLI support layer owns pure argument parsing, standard-stream output,
  load/stack/session orchestration, and diagnostics. The small `main` function
  supplies process streams and maps normal guest exit to the host exit status.

Execution computes pending effects before committing them. A misaligned taken
jump or branch therefore traps at the control-transfer instruction without
changing its link register or PC. Untaken branches advance normally without
validating an unused target.

Loads and stores add their sign-extended immediate to the original base-register
value with 32-bit wraparound. Loads explicitly sign- or zero-extend their result,
and stores truncate to the selected width. Data-memory exceptions become precise
architectural traps: a failed load leaves its destination unchanged, a failed
store cannot partially alter memory, and either failure leaves the PC at the
faulting instruction. Even a load targeting `x0` still performs the access and
can trap.

`FENCE` retires as a conservative ordering operation. Because the current
memory model is single-hart, synchronous, and has no devices or deferred memory
operations, it requires no additional runtime action. All `FENCE` field
configurations are accepted as required for base-ISA forward compatibility;
`FENCE.I` remains unsupported because it belongs to the separate `Zifencei`
extension.

`ECALL` and `EBREAK` are exact-encoding, precise traps: they do not advance the
PC, mutate registers, or count as retired instructions. Until privilege modes
are modeled, `ECALL` is reported as `EnvironmentCallFromUserMode`.

`ProgramSession` may consume that precise `ECALL` trap. It snapshots `a7` as the
call number and `a0` through `a5` as arguments before dispatching to a handler
that receives read-only memory. A resume decision writes only the return value
to `a0` and advances the PC by four; an exit or unhandled decision leaves the
trapping architectural state unchanged. This keeps host policy out of the CPU
core and gives later syscall, CLI, and debugger work one stable boundary.

Session run limits count guest steps: a retired instruction, a resumed call,
or a terminating call each consumes one step. Statistics keep those steps
separate from architectural retirement and handled-call counts. Breakpoints are
checked against the PC before execution and do not patch guest code; direct
`step()` calls intentionally ignore them.

`LinuxSyscallEnvironment` implements the current hosted ABI. It recognizes
`write` (64), `exit` (93), and `exit_group` (94), using `a0` through `a2` for
the `write` file descriptor, guest address, and byte count. File descriptors 1
and 2 are routed to distinct stdout/stderr channels on an injected `OutputSink`;
other descriptors return `-EBADF`. Unknown calls return `-ENOSYS` and resume.

Before invoking the sink once, `write` obtains one checked read-only view of the
entire effective guest range. Invalid ranges return `-EFAULT` without output,
including ranges that cross the mapping or wrap past the 32-bit address space.
A zero-length write to a recognized descriptor returns zero without inspecting
its pointer. Linux's `0x7ffff000` maximum transfer size is applied before range
validation. A sink may report a valid partial prefix, which is returned without
retrying. Sink failure, an exception, or an impossible byte count returns
`-EIO`; a failed sink result must mean that it consumed no bytes.

Both exit calls terminate the single-hart session with the low eight bits of
`a0`. The raw argument remains available in the captured `EnvironmentCall`, and
the architectural PC and registers remain at the precise trapping `ECALL`.

RV32M signed operations use unsigned sign-and-magnitude intermediates rather
than host signed casts. Multiplication constructs the full 64-bit product before
selecting its low or high word. Signed division divides unsigned magnitudes and
then reapplies the architectural signs, which provides round-toward-zero
quotients without host overflow. Division by zero does not trap: quotients are
all ones and remainders equal the dividend. The `INT32_MIN / -1` overflow case
returns `INT32_MIN` with remainder zero.

Signed comparisons use sign-bit-biased unsigned ordering, and arithmetic right
shift explicitly constructs the sign-fill bits. This keeps RV32I behavior
independent of the host compiler's signed-shift and overflow choices.

Keeping CPU state, memory, decoding, execution, program hosting, and ELF loading
independently testable gives later syscall, debugging, and performance features
clear integration points.

## ELF loading

The loader accepts static `ET_EXEC` files that use the standard 32-bit,
little-endian RISC-V ELF layout, the System V/unspecified OS ABI, soft-float ABI
flags, and four-byte instruction alignment. It reads every multi-byte field
explicitly in little-endian order rather than mapping host C++ structures over
untrusted file bytes.

Before changing memory, the loader validates the full program-header table and
every loadable segment using widened range arithmetic. It then copies each
`PT_LOAD` file image to `p_vaddr`, zero-fills the remaining `p_memsz` bytes, and
sets the PC to an aligned entry point inside an executable segment. `p_paddr`
is intentionally ignored, as required for System V application executables.
Bytes outside loadable segments and all integer registers are preserved.

This first loader deliberately rejects position-independent `ET_DYN` files,
dynamic-linker/interpreter segments, thread-local storage, compressed or
hard-float ABI flags, overlapping load segments, and extended program-header
numbering. These cases need runtime features or policies that the emulator does
not yet model. ELF segment permissions are inspected for entry-point validation
but are not enforced by the current flat `Memory` type.

The implementation follows the
[generic ELF header](https://gabi.xinuos.com/elf/02-eheader.html),
[generic ELF program-loading](https://gabi.xinuos.com/elf/07-pheader.html), and
[RISC-V ELF psABI](https://riscv-non-isa.github.io/riscv-elf-psabi-doc/#elf-object-files)
specifications.

## Command-line execution

A normal build creates `build/rvemu`. It accepts one static RV32IM ELF file and
the following options; byte counts and addresses may be decimal or prefixed
hexadecimal values:

```text
usage: rvemu [options] <program.elf>

  --memory-base ADDRESS  guest mapping base (default 0x80000000)
  --memory-size BYTES    guest mapping size (default 16777216)
  --stack-size BYTES     reserved stack size (default 1048576)
  --max-steps COUNT      strict guest-step limit (default 50000000)
  -h, --help             show help
```

For example:

```sh
build/rvemu --max-steps 1000000 path/to/program.elf
```

The mapping must contain every nonempty `PT_LOAD` segment and leave room above
the image for the requested stack. The runner aligns `sp` down to a 16-byte
boundary and reserves the stack below it. This is intentionally a freestanding
startup contract: it does not yet place `argc`, `argv`, environment strings, or
an auxiliary vector on the stack.

Normal termination returns the guest's low-eight-bit exit status. Help returns
0, command-line syntax errors return 2, and host-side loader, stack, execution,
or internal failures return 125 with a diagnostic. Because guest statuses are
preserved, a normally exiting guest may also legitimately return 2 or 125.

## ACT 4 smoke runner

`rvemu-act-smoke` is a deliberately narrow bridge for already-generated ACT 4
self-checking ELF files. It loads an ELF into 16 MiB of memory at `0x80000000`,
runs at most 50,000,000 guest steps, and accepts only `exit` or `exit_group` as
the test halt convention. Status zero produces the ACT `RVCP-SUMMARY` pass line;
a nonzero status is a test failure. Loader errors, traps, unknown environment
calls, unexpected breakpoints, and step-limit exhaustion are infrastructure
failures, not architectural test failures.

The reproducibility inputs under `conformance/act4/` pin `riscv-arch-test`
commit `585fbaf97a7df6e2f0fe8808edd3ad839eb1afe3` and the official multi-arch
ACT image by immutable digest. The observed image contains Sail 0.12, GCC
16.1.0, binutils 2.46, and UDB 0.1.13. The directory also records the target
memory map, derived Sail configuration, linker script, halt macros, exclusions,
and ten representative I/M smoke paths.

The pinned environment successfully generated all 47 selected non-privileged
RV32I/M ELFs from Sail results (188 build tasks succeeded). The compatibility
audit then stopped before emulator execution: all 47 files carry the RVC ELF
flag despite containing no 16-bit instructions, and ACT's failure-diagnostic
path places three unsupported CSR reads in every file—141 CSR instructions in
total. The harness does not rewrite those files, relax the loader, or claim a
pass. See `conformance/act4/README.md` for the reproduction command, immutable
pins, audit evidence, and exact boundary.

## Repository layout

```text
.
├── .github/workflows/ci.yml  # GCC/Clang sanitizer CI
├── app/                      # CLI support and executable entry point
├── cmake/                    # Shared compiler and sanitizer options
├── conformance/act4/         # Pinned ACT 4 smoke inputs and limitations
├── include/rvemu/            # Public C++ interfaces
├── src/                      # Core implementation
├── tests/                    # GoogleTest unit tests
├── tools/                    # ACT smoke support and executable
├── AGENTS.md                 # Context for future coding-agent sessions
└── CMakeLists.txt            # Top-level build definition
```

## Requirements

- CMake 3.20 or newer
- a C++20 compiler (recent GCC, Clang, or MSVC)
- Git and network access during the first test-enabled configuration

GoogleTest 1.15.2 is fetched and built by default for reproducibility. An
ABI-compatible installed package can be selected explicitly with
`-DRVEMU_USE_SYSTEM_GTEST=ON`.

## Build and test

Configure a sanitizer-enabled debug build:

```sh
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRVEMU_ENABLE_SANITIZERS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For a normal build, omit `-DRVEMU_ENABLE_SANITIZERS=ON`. Set
`-DBUILD_TESTING=OFF` to avoid downloading test dependencies while still
building `rvemu`. To build only the reusable core library, also set
`-DRVEMU_BUILD_CLI=OFF`.

The ACT smoke executable is built as `build/rvemu-act-smoke` whenever tests are
enabled. To build it without tests, opt in explicitly:

```sh
cmake -S . -B build-act \
  -DBUILD_TESTING=OFF \
  -DRVEMU_BUILD_ACT_SMOKE_RUNNER=ON
cmake --build build-act --parallel
build-act/rvemu-act-smoke path/to/self-checking-rv32im.elf
```

Its process exit status is 0 for a passing self-check, 1 for a reported test
failure, and 2 for runner/infrastructure failure.

## Usage

The public headers can be used from another CMake target after linking
`rvemu::core`:

```cpp
#include <rvemu/cpu_state.hpp>
#include <rvemu/elf_loader.hpp>
#include <rvemu/execution_engine.hpp>
#include <rvemu/linux_syscalls.hpp>
#include <rvemu/memory.hpp>
#include <rvemu/program_session.hpp>

rvemu::CpuState cpu;
rvemu::Memory memory(0x80000000U, 64U * 1024U);
cpu.set_program_counter(0x80000000U);

// addi x1, x0, 42
memory.write32(0x80000000U, 0x02A00093U);

rvemu::ExecutionEngine engine(cpu, memory);
const rvemu::StepResult result = engine.step();
```

Callers can inspect `StepResult` as either `StepCompleted` or `Trap`. A bounded
`run(instruction_limit)` call returns either `InstructionLimitReached` or
`RunTrapped` and reports the number of successfully retired instructions.

Load a statically linked ELF file before constructing the execution engine:

```cpp
rvemu::CpuState cpu;
rvemu::Memory memory(0x80000000U, 16U * 1024U * 1024U);

const rvemu::ElfLoadResult loaded =
    rvemu::load_elf32_file("program.elf", cpu, memory);
if (const auto* failure = std::get_if<rvemu::ElfLoadFailure>(&loaded)) {
  // Log rvemu::elf_load_error_message(failure->code).
}
```

The memory mapping must already cover every nonempty loadable segment. Loading
does not reset registers or clear unrelated memory.

## Roadmap

1. **Complete:** C++20/CMake scaffolding, CPU state, checked little-endian
   memory, tests, sanitizers, and CI.
2. **Complete:** instruction representation, all base instruction formats,
   aligned fetch, typed traps, and bounded step/run execution.
3. **Complete:** all base RV32I instruction families, including precise traps
   and deterministic handling of reserved encodings.
4. **Complete:** all RV32M multiplication, division, and remainder operations,
   including architectural zero-divisor and signed-overflow behavior.
5. **Complete:** load validated, static 32-bit little-endian RISC-V ELF files.
6. **Complete:** the shared program-session contract, host breakpoints,
   accounting, Linux-style output/termination syscalls, freestanding stack
   setup, and command-line executable are complete.
7. **In progress:** the ACT 4 runner, immutable tool pins, I/M-oriented UDB and
   Sail configuration, generation harness, and compatibility audit are
   complete. All 47 selected ELFs generate, but execution is correctly blocked
   by spurious RVC flags and CSR-based ACT failure diagnostics.
8. Add an interactive debugger with stepping and state inspection, building on
   the existing host-breakpoint mechanism.
9. Add configurable cache and branch-prediction models.
10. Add reproducible workloads and report only measured benchmark results.
11. Expand differential validation against Sail, QEMU, or Spike where each is
    practical.

## Current limitations

- Memory is one contiguous mapped region, not yet a sparse address map or bus.
- Misaligned halfword and word accesses always fail; no emulation mode exists.
- `Zicsr`, `Zifencei`, privileged instructions, and other extensions are not
  implemented. Their encodings produce an `IllegalInstruction` trap.
- Decoding currently validates the major opcode and reconstructs its format;
  instruction-specific function-field validation occurs in the execution layer.
- Memory APIs use typed C++ exceptions. Instruction-fetch and data-access
  failures are translated into architectural trap results by `ExecutionEngine`.
- There is no CSR or architectural trap-handler state yet; traps are returned to
  the host caller.
- There is no privilege-mode state. `ECALL` is currently classified as a
  user-mode environment call. The hosted environment implements only `write`,
  `exit`, and `exit_group`, not a complete Linux process ABI.
- ELF loading currently supports only fixed-address `ET_EXEC` files with
  `e_flags == 0`, System V/unspecified OS ABI version 0, sorted and
  nonoverlapping `PT_LOAD` ranges, and no dynamic-linking or TLS segments.
- ELF section headers and `.riscv.attributes` are not parsed. A file with zero
  ELF flags can therefore contain an unsupported extension; execution will
  still trap if it reaches an unsupported instruction.
- ELF `PF_R`, `PF_W`, and `PF_X` permissions are not enforced by flat memory.
- The CLI creates only a freestanding 16-byte-aligned stack pointer. It does not
  build a Linux process-startup image containing `argc`, `argv`, environment
  strings, or an auxiliary vector, and the flat memory model does not enforce
  stack underflow below the reserved range.
- The CLI accepts no guest arguments yet. Its default contiguous mapping is 16
  MiB at `0x80000000`; programs linked elsewhere must select matching memory
  options.
- Hosted output supports only stdout and stderr. There is no guest stdin, file
  table, signal model, or broader Linux syscall emulation; all sink failures map
  to `-EIO` because `SIGPIPE` and richer host error translation are absent.
- Host breakpoints are supported for bounded session runs, but there is no
  interactive debugger or register/memory command interface yet.
- ACT generated all 47 selected non-privileged RV32I/M ELFs, but the audit
  correctly blocks execution because every file is RVC-flagged and contains
  three CSR reads in its failure path. No architectural conformance result is
  published yet.
- There are no performance claims or results yet.

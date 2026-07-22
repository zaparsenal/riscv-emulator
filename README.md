# RISC-V Emulator

A correctness-first software implementation of a 32-bit RISC-V processor and
performance-analysis environment, written in C++20. The long-term target is
RV32IM execution of real ELF programs, with debugging, cache and branch models,
reproducible benchmarks, and differential validation against established
emulators.

The project is being built in small, tested milestones. It now has a working
fetch-decode-execute pipeline with complete RV32IM instruction support and a
validated loader for static 32-bit RISC-V ELF executables.

## Current status

The foundational state, memory, execution-loop, RV32I, RV32M, and ELF-loading
milestones are complete:

- 32 unsigned 32-bit integer registers
- an enforced `x0 == 0` invariant
- a 32-bit program counter with defined wraparound behavior
- reset support for CPU state
- contiguous, zero-initialized memory at a configurable 32-bit base address
- bounds-checked byte, halfword, and word accesses
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
- GoogleTest coverage for state, memory, instruction formats, fetch, execution,
  signed boundaries, shift limits, operand aliasing, PC-relative wraparound,
  taken and untaken branches, load extension, little-endian stores, effective
  address wraparound, misaligned targets and data, instruction limits, illegal
  instructions, access faults, high-word multiplication, division by zero, and
  signed division overflow, ELF range arithmetic, malformed headers, segment
  alignment and ordering, entry-point validation, and transactional loading
- optional AddressSanitizer and UndefinedBehaviorSanitizer instrumentation
- GitHub Actions validation with GCC and Clang

RV32IM execution and static ELF32 loading are complete. There is currently no
command-line executable, system-call layer, debugger, performance model, or
published benchmark result.

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

## Architecture

The implementation currently builds one reusable library, `rvemu::core`:

- `CpuState` owns the 32 general-purpose registers and program counter. Its API
  prevents direct mutation of the register array and ignores all writes to
  `x0`.
- `Memory` owns a contiguous byte region mapped into the 32-bit address space.
  Multi-byte operations are explicitly little-endian. Halfword and word
  operations require natural alignment; invalid accesses throw typed errors
  before modifying memory.
- `decode_instruction` recognizes RV32I major opcodes and produces normalized
  register, function, format, and immediate fields. Immediate reconstruction is
  host-independent and avoids signed-overflow assumptions.
- `ExecutionEngine` references a `CpuState` and `Memory`. `step()` fetches,
  decodes, and executes one instruction. `run(limit)` executes until its strict
  instruction limit or the first trap. Both APIs return variants, so successful
  execution cannot be confused with a trap.
- `load_elf32` and `load_elf32_file` validate a complete executable before
  loading its segments into `Memory` and setting the entry-point PC. Failures
  return an `ElfLoadFailure`; successful loads report the segment and byte
  counts they actually loaded.

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
are modeled, `ECALL` is reported as `EnvironmentCallFromUserMode`; the later
system-call environment will consume that trap rather than changing instruction
semantics.

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

Keeping CPU state, memory, decoding, execution, and ELF loading independently
testable gives later syscall, debugging, and performance features clear
integration points.

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

## Repository layout

```text
.
├── .github/workflows/ci.yml  # GCC/Clang sanitizer CI
├── cmake/                    # Shared compiler and sanitizer options
├── include/rvemu/            # Public C++ interfaces
├── src/                      # Core implementation
├── tests/                    # GoogleTest unit tests
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
`-DBUILD_TESTING=OFF` to build only the core library and avoid downloading test
dependencies.

## Usage

The public headers can be used from another CMake target after linking
`rvemu::core`:

```cpp
#include <rvemu/cpu_state.hpp>
#include <rvemu/elf_loader.hpp>
#include <rvemu/execution_engine.hpp>
#include <rvemu/memory.hpp>

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
6. **Next:** add a minimal system-call environment for output and termination.
7. Add an interactive debugger with stepping, breakpoints, and state inspection.
8. Add configurable cache and branch-prediction models.
9. Add reproducible workloads and report only measured benchmark results.
10. Add practical differential tests against QEMU or Spike.

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
  user-mode environment call for the future user-level syscall layer.
- ELF loading currently supports only fixed-address `ET_EXEC` files with
  `e_flags == 0`, System V/unspecified OS ABI version 0, sorted and
  nonoverlapping `PT_LOAD` ranges, and no dynamic-linking or TLS segments.
- ELF section headers and `.riscv.attributes` are not parsed. A file with zero
  ELF flags can therefore contain an unsupported extension; execution will
  still trap if it reaches an unsupported instruction.
- ELF `PF_R`, `PF_W`, and `PF_X` permissions are not enforced by flat memory.
- The loader is available through the C++ library, but there is no command-line
  frontend yet.
- There are no performance claims or results yet.

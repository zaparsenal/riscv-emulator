# RISC-V Emulator

A correctness-first software implementation of a 32-bit RISC-V processor and
performance-analysis environment, written in C++20. The long-term target is
RV32IM execution of real ELF programs, with debugging, cache and branch models,
reproducible benchmarks, and differential validation against established
emulators.

The project is being built in small, tested milestones. It now has a working
fetch-decode-execute pipeline with intentionally limited instruction support.

## Current status

The foundational state, memory, and execution-loop milestones are complete:

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
- execution of the RV32I upper-immediate and integer-immediate groups
- host-independent signed comparisons and arithmetic right shifts
- strict rejection of reserved shift-immediate encodings without state changes
- GoogleTest coverage for state, memory, instruction formats, fetch, execution,
  signed boundaries, shift limits, PC-relative wraparound, instruction limits,
  illegal instructions, and access faults
- optional AddressSanitizer and UndefinedBehaviorSanitizer instrumentation
- GitHub Actions validation with GCC and Clang

RV32I is not complete, and RV32M has not started. There is currently no
command-line executable, ELF loader, system-call layer, debugger, performance
model, or published benchmark result.

## Supported instructions

| Group | Instructions |
| --- | --- |
| Upper immediate | `LUI`, `AUIPC` |
| Integer immediate | `ADDI`, `SLTI`, `SLTIU`, `XORI`, `ORI`, `ANDI`, `SLLI`, `SRLI`, `SRAI` |

Instruction behavior follows the
[RV32I Base Integer Instruction Set, version 2.1](https://docs.riscv.org/reference/isa/v20260120/unpriv/rv32.html).
Where the specification leaves reserved encodings unspecified, this emulator
chooses a deterministic `IllegalInstruction` trap.

All other instructions currently return an `IllegalInstruction` trap. This is a
temporary implementation boundary, not a claim that architecturally valid
instructions are illegal in RV32I.

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

Signed comparisons use sign-bit-biased unsigned ordering, and arithmetic right
shift explicitly constructs the sign-fill bits. This keeps RV32I behavior
independent of the host compiler's signed-shift and overflow choices.

Keeping CPU state, memory, decoding, and execution independently testable gives
later ELF loading, debugging, and performance models clear integration points.

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

## Roadmap

1. **Complete:** C++20/CMake scaffolding, CPU state, checked little-endian
   memory, tests, sanitizers, and CI.
2. **Complete:** instruction representation, all base instruction formats,
   aligned fetch, typed traps, and bounded step/run execution.
3. **In progress:** implement and exhaustively test RV32I in focused instruction
   families; upper-immediate and integer-immediate operations are complete.
4. Implement and exhaustively test the RV32M extension.
5. Load validated 32-bit little-endian RISC-V ELF files.
6. Add a minimal system-call environment for output and termination.
7. Add an interactive debugger with stepping, breakpoints, and state inspection.
8. Add configurable cache and branch-prediction models.
9. Add reproducible workloads and report only measured benchmark results.
10. Add practical differential tests against QEMU or Spike.

## Current limitations

- Memory is one contiguous mapped region, not yet a sparse address map or bus.
- Misaligned halfword and word accesses always fail; no emulation mode exists.
- Only the instructions in the support table execute. Other recognized opcodes
  currently produce an `IllegalInstruction` trap until implemented.
- Decoding currently validates the major opcode and reconstructs its format;
  instruction-specific function-field validation occurs in the execution layer.
- Memory APIs use typed C++ exceptions. Instruction-fetch failures are translated
  into architectural trap results by `ExecutionEngine`.
- There is no CSR or architectural trap-handler state yet; traps are returned to
  the host caller.
- There are no performance claims or results yet.

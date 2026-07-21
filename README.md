# RISC-V Emulator

A correctness-first software implementation of a 32-bit RISC-V processor and
performance-analysis environment, written in C++20. The long-term target is
RV32IM execution of real ELF programs, with debugging, cache and branch models,
reproducible benchmarks, and differential validation against established
emulators.

The project is being built in small, tested milestones. It does **not** execute
instructions yet.

## Current status

The foundational state and memory milestone is complete:

- 32 unsigned 32-bit integer registers
- an enforced `x0 == 0` invariant
- a 32-bit program counter with defined wraparound behavior
- reset support for CPU state
- contiguous, zero-initialized memory at a configurable 32-bit base address
- bounds-checked byte, halfword, and word accesses
- explicit traps (C++ exceptions) for out-of-bounds and misaligned accesses
- little-endian reads and writes
- GoogleTest coverage for state, endian behavior, address-space boundaries,
  alignment, failed writes, and reset behavior
- optional AddressSanitizer and UndefinedBehaviorSanitizer instrumentation
- GitHub Actions validation with GCC and Clang

No RV32I or RV32M instructions are supported yet. There is currently no command
line executable, ELF loader, system-call layer, debugger, performance model, or
published benchmark result.

## Architecture

The implementation currently builds one reusable library, `rvemu::core`:

- `CpuState` owns the 32 general-purpose registers and program counter. Its API
  prevents direct mutation of the register array and ignores all writes to
  `x0`.
- `Memory` owns a contiguous byte region mapped into the 32-bit address space.
  Multi-byte operations are explicitly little-endian. Halfword and word
  operations require natural alignment; invalid accesses throw typed errors
  before modifying memory.

Keeping CPU state and memory independent will let the execution engine, ELF
loader, debugger, and performance models share clear interfaces in later
milestones.

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
#include <rvemu/memory.hpp>

rvemu::CpuState cpu;
cpu.write_register(1, 42);
cpu.advance_program_counter();

rvemu::Memory memory(0x80000000U, 64U * 1024U);
memory.write32(0x80000000U, 0x12345678U);
```

## Roadmap

1. **Complete:** C++20/CMake scaffolding, CPU state, checked little-endian
   memory, tests, sanitizers, and CI.
2. Add instruction representation, decoding, and the fetch-decode-execute loop.
3. Implement and exhaustively test RV32I.
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
- Errors currently use C++ exceptions. The execution engine will translate
  relevant failures into architectural traps when trap handling is introduced.
- The program counter permits any value; instruction alignment belongs to the
  future fetch/execution layer.
- There are no performance claims or results yet.

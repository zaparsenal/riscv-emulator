# RISC-V Emulator

A C++20 emulator for running **32-bit RISC-V (RV32IM)** programs and exploring how they execute. It can load static ELF files, step through instructions in a debugger, and collect cache and branch-prediction statistics through its library API.

## Get started

You need CMake 3.20+, a C++20 compiler, and Git. The first build may download test dependencies.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Run a statically linked, 32-bit little-endian RISC-V ELF executable:

```sh
build/rvemu path/to/program.elf
```

The default guest memory map starts at `0x80000000` and spans 16 MiB. If your program is linked elsewhere, set `--memory-base` and `--memory-size` to cover its loadable segments. Run `build/rvemu --help` for all options.

To inspect execution interactively:

```sh
build/rvemu --debug path/to/program.elf
```

The debugger supports `step`, `continue`, breakpoints, register display, and memory display. Type `help` at its prompt for commands.

## What it supports

- The RV32I base integer and RV32M multiply/divide instructions, with checked memory accesses and precise traps.
- Static ELF32 RISC-V executables with fixed addresses; the loader validates segments before changing guest state.
- A small hosted environment for `write`, `exit`, and `exit_group` system calls.
- Library APIs for split instruction/data cache models, branch predictors, cycle estimates, and deterministic benchmark workloads.
- A pinned architectural-test smoke setup: ten representative RV32I/M tests passed from 47 generated and audited compatible files. This is a scoped smoke result, not full architecture certification.

## Useful build options

| Goal | CMake option |
| --- | --- |
| Build without downloading test dependencies | `-DBUILD_TESTING=OFF` |
| Build only the reusable library | `-DRVEMU_BUILD_CLI=OFF -DBUILD_TESTING=OFF` |
| Enable sanitizers in a Debug build | `-DRVEMU_ENABLE_SANITIZERS=ON` |
| Build benchmark executable | `-DRVEMU_BUILD_BENCHMARKS=ON` |

## Scope and limits

This is a freestanding runner with a small Linux-style syscall subset, not a full Linux process emulator. It does not provide guest arguments, files, stdin, dynamic linking, CSRs, privileged modes, or extensions beyond RV32IM. The debugger does not yet show source lines or symbols. Cache and branch analysis is available through the library, not the CLI; cycle figures are configured estimates rather than measured hardware cycles.

For implementation details and the full history of supported behavior, see the code and [earlier README revisions](https://github.com/zaparsenal/riscv-emulator/commits/main/README.md).

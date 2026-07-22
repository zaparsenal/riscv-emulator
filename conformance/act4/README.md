# ACT 4 RV32I/M smoke plumbing

This directory defines the first, deliberately limited bridge between rvemu
and ACT 4. It does **not** claim certification or complete architectural-test
coverage.

The reproducibility baseline is recorded in `pins.json`. The ACT repository is
pinned to commit `585fbaf97a7df6e2f0fe8808edd3ad839eb1afe3`; its lock files pin
the framework, generator, UDB, Python, and Ruby inputs. The upstream baseline
uses Sail 0.12 with GCC 15 and binutils 2.44. A future CI job must additionally
pin the official container by immutable digest before generated results are
treated as reproducible artifacts.

## What is implemented

`rvemu-act-smoke` loads a static RV32 ELF at the ACT memory map, runs at most
50,000,000 guest steps, and recognizes only the termination convention in
`rvmodel_macros.h`:

- Linux-style `exit` (93) or `exit_group` (94)
- status zero means the self-check passed
- any nonzero status means the self-check failed
- a loader error, unexpected trap, unknown environment call, or step-limit
  stop is an infrastructure failure rather than a test failure

The executable prints the exact `RVCP-SUMMARY` pass/fail form expected by ACT
4's `run_tests.py`. Test diagnostics and execution counts go to standard error.
The ten paths in `smoke-tests.txt` cover representative RV32I arithmetic,
shift, branch, jump, load, and store behavior plus signed multiply/divide and
unsigned remainder. They are a smoke selection only.

Test-enabled builds compile the runner automatically. To build it without the
GoogleTest dependency, configure with `-DBUILD_TESTING=OFF` and
`-DRVEMU_BUILD_ACT_SMOKE_RUNNER=ON`. An already-generated compatible
self-checking ELF can then be run with:

```sh
build/rvemu-act-smoke path/to/I-add-00.elf
```

## Generation boundary

The checked-in model macros intentionally disable standard M-mode boot code and
console output. The checked-in linker script fixes the ELF base at the runner's
`0x80000000` memory base. The runner derives the summary from the pass/fail halt
status. A DUT configuration used to generate these ELFs must likewise omit
`STANDARD_SM_SUPPORTED`; otherwise ACT emits CSR-based trap setup and cleanup
that an RV32IM-only emulator cannot execute. Compiling with an I/M extension
filter alone is not sufficient because ACT's standard configuration also
describes privileged state.

The current development machine did not have `riscv64-unknown-elf-gcc`,
`sail_riscv_sim`, `mise`, or `uv`, so no official ACT-generated ELF was run and
no conformance result is recorded. The next integration step is to validate a
minimal I/M-only UDB configuration in the pinned official environment, generate
the listed ELFs from Sail signatures, and add a CI job using an immutable
container digest. CSR, privilege, exception, RVC, floating-point, atomic, and
other extension tests must remain excluded.

# ACT 4 RV32IM generation and audit

This directory defines a deliberately limited bridge between rvemu and ACT 4.
It does **not** claim certification or a passing architectural-test result.

## Reproducibility baseline

`pins.json` records every external input used by the harness:

- `riscv-arch-test` commit
  `585fbaf97a7df6e2f0fe8808edd3ad839eb1afe3`
- official tool image
  `ghcr.io/riscv/act4-build@sha256:e2a3982d778cfa5b85502b71dc7cb90891c4119d0b9335f4aa74e71a2206e0e4`
- upstream image workflow run
  <https://github.com/riscv/riscv-arch-test/actions/runs/29643967685>
- Sail 0.12, GCC 16.1.0, binutils 2.46, UDB 0.1.13, uv 0.11.28,
  and Ruby 3.4.10 as observed in that immutable image

The image index contains native Linux AMD64 and ARM64 manifests, both recorded
in `pins.json`. The generation script refuses a different ACT commit, tracked
changes in the ACT checkout, or an existing output directory.

The pinned image contains Bundler 4.0.16, while the pinned source lockfile
records the Bundler 4.0.13 checksum. The preinstalled locked gem set is
complete, but an ordinary `bundle check` tries to rewrite the source lockfile.
The harness sets `BUNDLE_FROZEN=true`; this makes the satisfied dependency
check read-only and avoids changing the pinned checkout.

## Configuration boundary

`config/rvemu-rv32im.yaml` implements `I` and `M`, plus an `Sm` configuration
scaffold. UDB 0.1.13 defines both `MXLEN` and `MUTABLE_MISA_M` only when `Sm`
is present, so a strict `I,M`-only fully configured UDB file is invalid. The
scaffold is not a claim that rvemu implements machine-mode CSRs:

- `include_priv_tests` is false
- ACT is invoked with only the `I,M` extension filter
- `STANDARD_SM_SUPPORTED` is not defined
- boot-to-machine-mode setup is empty
- interrupt hooks are inert and exist only because ACT requires their macros
  to be defined at compile time

`prepare_sail_config.py` starts from the pinned ACT RV32 Sail configuration,
checks its expected enabled-extension set, disables A, C, floating point,
CSRs, `FENCE.I`, counters, and atomic extensions, and leaves only M/Zmmul
enabled. This derived file is written into the run directory; the upstream
baseline and repository stay unchanged.

The configuration validates under UDB 0.1.13 and selects 47 non-privileged
RV32I/M tests. `smoke-tests.txt` names ten representative final ELFs that the
runner will execute only after every selected ELF passes the compatibility
audit.

## Verified generation and smoke result

On 2026-07-23, the pinned environment validated the configuration and built all
47 selected self-checking ELFs from Sail results. ACT reported all 188 build
tasks succeeded. The complete compatibility audit then reported:

- 47 ELFs audited
- zero RVC-flagged or otherwise unexpectedly flagged ELFs
- zero non-32-bit instructions
- zero unsupported instructions
- zero unexpected ECALL counts

The ten-file smoke manifest then passed on rvemu. It covers representative
integer arithmetic, shifts, branches, indirect jumps, loads, stores,
multiplication, signed division, and unsigned remainder. This is a verified
smoke result for that exact pinned selection, not certification or a claim that
rvemu passes the complete architectural test suite.

`riscv_arch_test.h` is a generation-time model overlay copied into ACT's DUT
include directory. It makes two narrowly scoped adaptations:

- ACT's temporary `.option rvc` alignment requests become `.option norvc`, so
  32-bit-only files no longer acquire `EF_RISCV_RVC`.
- only the final `RVTEST_SELFCHECK` build replaces ACT's machine-CSR failure
  diagnostics with a direct jump to rvemu's existing failure exit. Sail keeps
  the original diagnostics, and every integer mismatch or unexpected trap
  still terminates the DUT test as failure.

The harness does not modify generated ELFs or relax rvemu's loader. The audit
uses RISC-V mapping symbols to exclude explicitly marked inline data from
instruction decoding while continuing to reject unknown encodings in every
`$x` code region. Parser tests preserve that distinction.

The generated `elf-audit.json` contains the SHA-256 digest, architecture
attribute, decoded-instruction count, ELF flags, and rejected instructions for
each input. `rvemu-results.log` contains the exact ten smoke outcomes and
execution counts.

## Running the reproducible audit

Build the existing smoke runner first:

```sh
cmake -S . -B build-act \
  -DBUILD_TESTING=OFF \
  -DRVEMU_BUILD_ACT_SMOKE_RUNNER=ON
cmake --build build-act --parallel
```

Create a clean checkout at the pinned ACT commit, then give the harness a new
output directory:

```sh
git clone https://github.com/riscv-non-isa/riscv-arch-test.git /tmp/riscv-arch-test
git -C /tmp/riscv-arch-test checkout 585fbaf97a7df6e2f0fe8808edd3ad839eb1afe3

conformance/act4/run_official_smoke.sh \
  /tmp/riscv-arch-test \
  build-act/rvemu-act-smoke \
  build-act4-official
```

Success writes `generation.log`, `elf-audit.json`, and `rvemu-results.log` in
the new output directory. Any generation failure, incompatible ELF, failing
self-check, trap, unknown environment call, breakpoint, or step-limit
exhaustion makes the script exit nonzero.

The same pinned generate-audit-run sequence is available through the
`ACT conformance smoke` GitHub Actions workflow. It runs weekly and may also be
started manually. It is deliberately separate from per-push CI because the
pinned container and full 47-file regeneration are substantially heavier than
the unit-test suite.

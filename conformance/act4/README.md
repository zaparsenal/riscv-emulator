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

## Verified generation result and current blocker

On 2026-07-22, the pinned environment validated the configuration and built
all 47 selected self-checking ELFs from Sail results. ACT reported 188 build
tasks succeeded and no generation failure.

`audit_generated_elfs.py` then audited every generated ELF using the pinned
RISC-V `readelf` and `objdump`. The audit intentionally stopped execution:

- all 47 ELFs have `e_flags == 0x1` (`EF_RISCV_RVC`)
- no 16-bit instruction was present in any disassembly
- each ELF contains three unsupported `CSRR[S]` reads, for 141 total:
  `mcause`, `mtval`, and `mstatus`
- those CSR reads are emitted in ACT's `failedtest_trap_x7_x9` diagnostic path
- there was no `FENCE.I`, xRET, WFI, SFENCE/HFENCE, breakpoint, unexpected
  system instruction, or unexpected ECALL count

ACT's setup uses temporary `.option rvc` alignment regions even for these
32-bit-only tests, which marks the final ELF as RVC. Clearing that flag alone
would not be sufficient because the compiled diagnostic path still contains
Zicsr instructions. The harness therefore does not rewrite the ELFs, relax
rvemu's loader, run the ten-file subset, or claim a pass.

The generated `elf-audit.json` contains the SHA-256 digest, architecture
attribute, decoded-instruction count, ELF flags, and rejected instructions for
each of the 47 inputs.

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

The current pinned ACT output is expected to make the script exit nonzero at
the audit stage. Inspect `build-act4-official/generation.log` and
`build-act4-official/elf-audit.json` for the evidence. If ACT later emits an
entirely RV32IM-compatible set, the same script will continue automatically to
the ten-file rvemu smoke run and write `rvemu-results.log`.

No CI conformance job is enabled yet: a job known to stop before execution
would not constitute a useful or honest conformance signal. The next ACT step
is to remove the RVC flag and CSR diagnostic dependency at generation time,
rerun this complete audit, and only then execute and consider checking in the
verified smoke artifacts.

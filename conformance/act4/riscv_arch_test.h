// rvemu model overlay for the pinned ACT 4 environment.
//
// The upstream non-privileged setup temporarily enables RVC solely to request
// aligned code placement. That sets EF_RISCV_RVC even when no compressed
// instruction is emitted. rvemu intentionally targets IALIGN=32 RV32IM, so
// translate every later `.option rvc` request to `.option norvc`.
#define rvc norvc

#include "rvtest_config.h"
#undef H_SUPPORTED
#include "derived_config.h"
#include "encoding.h"
#include "utils.h"
#include "rvmodel_macros.h"
#ifndef RVTEST_SELFCHECK
  #include "sail_macros.h"
#endif
#include "check_defines.h"
#include "signature.h"
#include "rvtest_macros.h"
#include "rvtest_pmp_macros.h"
#ifdef RVTEST_VECTOR
  #include "rvtest_macros_vector.h"
#endif
#ifdef RVTEST_HYPERVISOR
  #include "rvtest_macros_hypervisor.h"
#endif
#include "rvtest_trap_handler.h"
#include "rvtest_failure_code.h"

// ACT's generic failure diagnostics read machine CSRs even when machine-mode
// support is not selected. Those reads are unreachable during a passing
// non-privileged test, but they make the final RV32IM self-checking ELF
// incompatible with rvemu. The Sail signature build retains ACT's full
// diagnostics. Only the DUT build uses the model's exit-only failure path;
// every relevant integer-test failure label still terminates as failure.
#ifdef RVTEST_SELFCHECK
  .purgem RVTEST_FAILURE_CODE
  .macro RVTEST_FAILURE_CODE
    failedtest_x5_x4:
    failedtest_x8_x7:
    failedtest_x14_x13:
    failedtest_trap_x7_x9:
      j rvmodel_halt_fail
  .endm
#endif

#include "rvtest_setup.h"

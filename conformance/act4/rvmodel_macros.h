// ACT 4 model hooks for rvemu's deliberately non-privileged smoke target.
// These macros are only for self-checking RV32I/M tests built without the
// standard privileged setup and cleanup paths.

#ifndef RVEMU_ACT4_RVMODEL_MACROS_H
#define RVEMU_ACT4_RVMODEL_MACROS_H

#define RVMODEL_DATA_SECTION
#define RVMODEL_BOOT
#define RVMODEL_BOOT_TO_MMODE

#define RVMODEL_HALT_PASS \
  li a0, 0;                \
  li a7, 93;               \
  ecall

#define RVMODEL_HALT_FAIL \
  li a0, 1;                \
  li a7, 93;               \
  ecall

#define RVMODEL_IO_INIT(_R1, _R2, _R3)
#define RVMODEL_IO_WRITE_STR(_R1, _R2, _R3, _STR_PTR)

// ACT requires every platform hook to be defined even when privileged and
// interrupt tests are excluded. These hooks must remain inert: rvemu has no
// interrupt controller or privileged trap state.
#define RVMODEL_INTERRUPT_LATENCY 0
#define RVMODEL_TIMER_INT_SOON_DELAY 0
#define RVMODEL_SET_MEXT_INT(_R1, _R2)
#define RVMODEL_CLR_MEXT_INT(_R1, _R2)
#define RVMODEL_SET_MSW_INT(_R1, _R2)
#define RVMODEL_CLR_MSW_INT(_R1, _R2)
#define RVMODEL_SET_SEXT_INT(_R1, _R2)
#define RVMODEL_CLR_SEXT_INT(_R1, _R2)
#define RVMODEL_SET_SSW_INT(_R1, _R2)
#define RVMODEL_CLR_SSW_INT(_R1, _R2)

#endif

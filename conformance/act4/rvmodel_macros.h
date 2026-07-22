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

#endif

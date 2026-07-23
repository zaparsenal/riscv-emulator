#!/usr/bin/env python3

import unittest

from audit_generated_elfs import audit_disassembly


class AuditDisassemblyTest(unittest.TestCase):
    def test_skips_data_mapping_regions_but_keeps_auditing_code(self) -> None:
        disassembly = """
00000000 <$xrv32i2p1>:
   0: 00000013 addi zero,zero,0
00000004 <$d>:
   4: ffffffff .word 0xffffffff
00000008 <$xrv32i2p1>:
   8: 00000073 ecall
"""

        decoded, ecalls, non_32_bit, unsupported = audit_disassembly(disassembly)

        self.assertEqual(decoded, 2)
        self.assertEqual(ecalls, 1)
        self.assertEqual(non_32_bit, [])
        self.assertEqual(unsupported, [])

    def test_unknown_encodings_in_code_remain_failures(self) -> None:
        disassembly = """
00000000 <$xrv32i2p1>:
   0: ffffffff .word 0xffffffff
   4: 0001 csrrw zero,zero,zero
"""

        decoded, ecalls, non_32_bit, unsupported = audit_disassembly(disassembly)

        self.assertEqual(decoded, 1)
        self.assertEqual(ecalls, 0)
        self.assertEqual(non_32_bit, ["4: 0001 csrrw zero,zero,zero"])
        self.assertEqual(unsupported, ["0: ffffffff .word 0xffffffff"])

    def test_files_without_mapping_symbols_are_fully_audited(self) -> None:
        disassembly = "   0: ffffffff .word 0xffffffff\n"

        decoded, ecalls, non_32_bit, unsupported = audit_disassembly(disassembly)

        self.assertEqual(decoded, 1)
        self.assertEqual(ecalls, 0)
        self.assertEqual(non_32_bit, [])
        self.assertEqual(unsupported, ["0: ffffffff .word 0xffffffff"])


if __name__ == "__main__":
    unittest.main()

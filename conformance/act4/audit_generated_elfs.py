#!/usr/bin/env python3
"""Audit generated ACT ELFs against rvemu's currently supported execution set."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path


EXPECTED_ELF_COUNT = 47
EXPECTED_ARCH_ATTRIBUTES = {
    "rv32i2p1_zicsr2p0_zifencei2p0",
    "rv32i2p1_m2p0_zicsr2p0_zifencei2p0_zmmul1p0",
}
RV32IM_OPCODES = {
    0x03,  # LOAD
    0x0F,  # MISC-MEM (FENCE only)
    0x13,  # OP-IMM
    0x17,  # AUIPC
    0x23,  # STORE
    0x33,  # OP / RV32M
    0x37,  # LUI
    0x63,  # BRANCH
    0x67,  # JALR
    0x6F,  # JAL
    0x73,  # SYSTEM (ECALL only)
}
INSTRUCTION_RE = re.compile(r"^\s*[0-9a-f]+:\s+([0-9a-f]+)\s+(\S+)")
MAPPING_SYMBOL_RE = re.compile(r"^\s*[0-9a-f]+\s+<(\$[dx][^>]*)>:$")
ARCH_RE = re.compile(r'Tag_RISCV_arch:\s+"([^"]+)"')


def run_tool(arguments: list[str]) -> str:
    return subprocess.run(arguments, check=True, capture_output=True, text=True).stdout


def audit_disassembly(disassembly: str) -> tuple[int, int, list[str], list[str]]:
    decoded_count = 0
    ecall_count = 0
    non_32_bit: list[str] = []
    unsupported: list[str] = []
    in_code = True

    for line in disassembly.splitlines():
        mapping_match = MAPPING_SYMBOL_RE.match(line)
        if mapping_match is not None:
            in_code = mapping_match.group(1).startswith("$x")
            continue

        match = INSTRUCTION_RE.match(line)
        if match is None or not in_code:
            continue
        encoding, mnemonic = match.groups()
        if len(encoding) != 8:
            non_32_bit.append(line.strip())
            continue

        decoded_count += 1
        if mnemonic.startswith("."):
            unsupported.append(line.strip())
            continue
        word = int(encoding, 16)
        opcode = word & 0x7F
        if opcode not in RV32IM_OPCODES:
            unsupported.append(line.strip())
        elif opcode == 0x0F and ((word >> 12) & 0x7) != 0:
            unsupported.append(line.strip())
        elif opcode == 0x73:
            if word == 0x00000073 and mnemonic == "ecall":
                ecall_count += 1
            else:
                unsupported.append(line.strip())

    return decoded_count, ecall_count, non_32_bit, unsupported


def audit_elf(path: Path, objdump: str, readelf: str) -> dict[str, object]:
    data = path.read_bytes()
    if len(data) < 52 or data[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError(f"{path}: not a 32-bit little-endian ELF")
    if int.from_bytes(data[18:20], "little") != 243:
        raise ValueError(f"{path}: not a RISC-V ELF")

    flags = int.from_bytes(data[36:40], "little")
    attributes = run_tool([readelf, "-A", str(path)])
    arch_match = ARCH_RE.search(attributes)
    arch = arch_match.group(1) if arch_match else "missing"
    if arch not in EXPECTED_ARCH_ATTRIBUTES:
        raise ValueError(f"{path}: unexpected Tag_RISCV_arch {arch!r}")

    disassembly = run_tool(
        [objdump, "-d", "--show-all-symbols", "-M", "no-aliases", str(path)]
    )
    decoded_count, ecall_count, non_32_bit, unsupported = audit_disassembly(disassembly)

    if decoded_count == 0:
        raise ValueError(f"{path}: objdump found no instructions")

    return {
        "sha256": hashlib.sha256(data).hexdigest(),
        "arch": arch,
        "decoded_instructions": decoded_count,
        "ecalls": ecall_count,
        "e_flags": flags,
        "non_32_bit_instructions": non_32_bit,
        "unsupported_instructions": unsupported,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-root", required=True, type=Path)
    parser.add_argument("--report", required=True, type=Path)
    parser.add_argument("--objdump", default="riscv64-unknown-elf-objdump")
    parser.add_argument("--readelf", default="riscv64-unknown-elf-readelf")
    args = parser.parse_args()

    elf_paths = sorted(args.input_root.rglob("*.elf"))
    if len(elf_paths) != EXPECTED_ELF_COUNT:
        raise ValueError(f"expected {EXPECTED_ELF_COUNT} selected ELFs, found {len(elf_paths)}")

    elf_reports: dict[str, dict[str, object]] = {}
    for elf_path in elf_paths:
        relative = str(elf_path.relative_to(args.input_root))
        elf_reports[relative] = audit_elf(elf_path, args.objdump, args.readelf)

    rvc_flagged = sum(report["e_flags"] == 1 for report in elf_reports.values())
    unexpected_flags = sum(report["e_flags"] not in (0, 1) for report in elf_reports.values())
    non_32_bit = sum(len(report["non_32_bit_instructions"]) for report in elf_reports.values())
    unsupported = sum(len(report["unsupported_instructions"]) for report in elf_reports.values())
    unexpected_ecalls = sum(report["ecalls"] != 2 for report in elf_reports.values())
    summary = {
        "audited_elfs": len(elf_paths),
        "rvc_flagged_elfs": rvc_flagged,
        "unexpected_elf_flags": unexpected_flags,
        "non_32_bit_instructions": non_32_bit,
        "unsupported_instructions": unsupported,
        "unexpected_ecall_counts": unexpected_ecalls,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(
        json.dumps({"summary": summary, "elfs": elf_reports}, indent=2, sort_keys=True) + "\n"
    )

    if rvc_flagged or unexpected_flags or non_32_bit or unsupported or unexpected_ecalls:
        print(
            "generated ELF audit blocked execution: "
            f"RVC flags={rvc_flagged}, unexpected flags={unexpected_flags}, "
            f"non-32-bit instructions={non_32_bit}, unsupported instructions={unsupported}, "
            f"unexpected ECALL counts={unexpected_ecalls}"
        )
        print(f"audit report: {args.report}")
        return 1

    print(f"audited {len(elf_paths)} RV32IM ELFs; report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

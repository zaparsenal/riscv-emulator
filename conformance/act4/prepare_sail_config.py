#!/usr/bin/env python3
"""Derive rvemu's RV32IM Sail configuration from the pinned ACT baseline."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


PINNED_ENABLED_EXTENSIONS = {
    "M",
    "A",
    "F",
    "D",
    "Zicntr",
    "Zicsr",
    "Zifencei",
    "Zihpm",
    "Zmmul",
    "Zaamo",
    "Zalrsc",
    "Zca",
    "Zcf",
    "Zcd",
}
RVEMU_ENABLED_EXTENSIONS = {"M", "Zmmul"}


def parse_jsonc(path: Path) -> dict[str, object]:
    content = re.sub(r"^\s*//.*$", "", path.read_text(), flags=re.MULTILINE)
    return json.loads(content)


def enabled_extensions(config: dict[str, object]) -> set[str]:
    extensions = config["extensions"]
    if not isinstance(extensions, dict):
        raise ValueError("Sail configuration has no extension mapping")
    return {
        name
        for name, settings in extensions.items()
        if isinstance(settings, dict) and settings.get("supported") is True
    }


def prepare(source: Path, destination: Path) -> None:
    config = parse_jsonc(source)
    actual_enabled = enabled_extensions(config)
    if actual_enabled != PINNED_ENABLED_EXTENSIONS:
        raise ValueError(
            "pinned Sail baseline changed: expected enabled extensions "
            f"{sorted(PINNED_ENABLED_EXTENSIONS)}, found {sorted(actual_enabled)}"
        )

    base = config["base"]
    extensions = config["extensions"]
    if not isinstance(base, dict) or not isinstance(extensions, dict):
        raise ValueError("Sail configuration is missing base or extension settings")

    if base.get("xlen") != 32 or base.get("E") is not False:
        raise ValueError("pinned Sail baseline is not RV32I")

    for name in PINNED_ENABLED_EXTENSIONS - RVEMU_ENABLED_EXTENSIONS:
        settings = extensions.get(name)
        if not isinstance(settings, dict):
            raise ValueError(f"Sail configuration is missing {name}")
        settings["supported"] = False

    mstatus = base.get("mstatus")
    if not isinstance(mstatus, dict):
        raise ValueError("Sail configuration is missing base.mstatus")
    mstatus["fs_legal_states"] = "ExtContext_Off"

    if enabled_extensions(config) != RVEMU_ENABLED_EXTENSIONS:
        raise ValueError("failed to derive an M-only Sail extension set")

    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(config, indent=2) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)


if __name__ == "__main__":
    main()

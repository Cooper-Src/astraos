#!/usr/bin/env python3
"""Emit a tiny valid FDT used by AstraOS's QEMU fallback-boot test."""

from __future__ import annotations

import struct
import sys
from pathlib import Path


FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_END = 9


def be32(value: int) -> bytes:
    return struct.pack(">I", value)


def padded(data: bytes) -> bytes:
    return data + (b"\0" * ((-len(data)) % 4))


def build_fixture() -> bytes:
    strings = b"model\0compatible\0stdout-path\0"
    name_offsets = {
        "model": 0,
        "compatible": len(b"model\0"),
        "stdout-path": len(b"model\0compatible\0"),
    }
    structure = bytearray()

    structure += be32(FDT_BEGIN_NODE)
    structure += padded(b"\0")
    model = b"AstraOS CI fixture\0"
    structure += be32(FDT_PROP)
    structure += be32(len(model))
    structure += be32(name_offsets["model"])
    structure += padded(model)

    compatible = b"astraos,ci-fixture\0qemu,virt\0"
    structure += be32(FDT_PROP)
    structure += be32(len(compatible))
    structure += be32(name_offsets["compatible"])
    structure += padded(compatible)

    structure += be32(FDT_BEGIN_NODE)
    structure += padded(b"chosen\0")
    stdout_path = b"serial0:115200n8\0"
    structure += be32(FDT_PROP)
    structure += be32(len(stdout_path))
    structure += be32(name_offsets["stdout-path"])
    structure += padded(stdout_path)
    structure += be32(FDT_END_NODE)
    structure += be32(FDT_END_NODE)
    structure += be32(FDT_END)

    reserve_offset = 40
    reserve_map = b"\0" * 16
    structure_offset = reserve_offset + len(reserve_map)
    strings_offset = structure_offset + len(structure)
    total_size = strings_offset + len(strings)
    header = struct.pack(
        ">10I",
        FDT_MAGIC,
        total_size,
        structure_offset,
        strings_offset,
        reserve_offset,
        17,
        16,
        0,
        len(strings),
        len(structure),
    )
    return header + reserve_map + bytes(structure) + strings


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(f"usage: {argv[0]} OUTPUT.dtb", file=sys.stderr)
        return 2
    output = Path(argv[1])
    data = build_fixture()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

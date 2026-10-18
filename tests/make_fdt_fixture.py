#!/usr/bin/env python3
"""Emit a small valid FDT fixture used by AstraOS UART/loader integration tests."""

from __future__ import annotations

import struct
import sys
from pathlib import Path

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_END = 9
NUL = bytes((0,))

STRING_NAMES = (
    "#address-cells",
    "#size-cells",
    "model",
    "compatible",
    "stdout-path",
    "serial0",
    "reg",
)


def be32(value: int) -> bytes:
    return struct.pack(">I", value)


def padded(data: bytes) -> bytes:
    return data + (bytes((-len(data)) % 4))


def build_fixture() -> bytes:
    strings = b"".join(name.encode("ascii") + NUL for name in STRING_NAMES)
    name_offsets = {}
    offset = 0
    for name in STRING_NAMES:
        name_offsets[name] = offset
        offset += len(name.encode("ascii")) + 1

    structure = bytearray()

    def begin_node(name: str) -> None:
        structure.extend(be32(FDT_BEGIN_NODE))
        structure.extend(padded(name.encode("ascii") + NUL))

    def end_node() -> None:
        structure.extend(be32(FDT_END_NODE))

    def prop(name: str, value: bytes) -> None:
        structure.extend(be32(FDT_PROP))
        structure.extend(be32(len(value)))
        structure.extend(be32(name_offsets[name]))
        structure.extend(padded(value))

    begin_node("")
    prop("#address-cells", be32(2))
    prop("#size-cells", be32(2))
    prop("model", b"AstraOS CI fixture" + NUL)
    prop("compatible", b"astraos,ci-fixture" + NUL + b"qemu,virt" + NUL)

    begin_node("aliases")
    prop("serial0", b"/pl011@9000000" + NUL)
    end_node()

    begin_node("chosen")
    prop("stdout-path", b"serial0:115200n8" + NUL)
    end_node()

    begin_node("pl011@9000000")
    prop("compatible", b"arm,pl011" + NUL + b"arm,primecell" + NUL)
    prop("reg", be32(0) + be32(0x09000000) + be32(0) + be32(0x1000))
    end_node()

    end_node()
    structure.extend(be32(FDT_END))

    reserve_offset = 40
    reserve_map = bytes(16)
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
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_bytes(build_fixture())
    except OSError as error:
        print(f"{argv[0]}: cannot write FDT fixture: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

"""Find PPC lis/addi/lfs references to an absolute address in a decoded XEX."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("address", type=lambda value: int(value, 0))
    parser.add_argument(
        "--image",
        type=Path,
        default=Path(__file__).resolve().parent.parent
        / "archive" / "graphics-debug-2026-09-07" / "timing-analysis" / "default-image.bin",
    )
    args = parser.parse_args()
    image = args.image.read_bytes()
    base = 0x82000000
    address = args.address
    high = (address + 0x8000) >> 16
    low = signed16(address & 0xFFFF)
    print(f"Target 0x{address:08X}: lis/addis immediate 0x{high & 0xFFFF:04X}, low {low}")
    results: list[tuple[int, int, int]] = []
    for offset in range(0, len(image) - 4, 4):
        instruction = struct.unpack_from(">I", image, offset)[0]
        opcode = instruction >> 26
        register = (instruction >> 21) & 31
        ra = (instruction >> 16) & 31
        immediate = instruction & 0xFFFF
        # lis is addis rD,r0,imm. Accept addis as well for visibility.
        if opcode != 15 or immediate != (high & 0xFFFF):
            continue
        if ra != 0:
            continue
        for next_offset in range(offset + 4, min(offset + 4 * 25, len(image) - 4), 4):
            next_instruction = struct.unpack_from(">I", image, next_offset)[0]
            next_opcode = next_instruction >> 26
            next_ra = (next_instruction >> 16) & 31
            next_immediate = signed16(next_instruction & 0xFFFF)
            # lfs / lfd / lwz / stfs / stfd / stw with the same base register.
            if next_opcode in {32, 36, 48, 50, 52, 54} and next_ra == register and next_immediate == low:
                results.append((base + offset, base + next_offset, next_instruction))
    if not results:
        print("No direct references found.")
    for lis_address, access_address, access_instruction in results:
        print(f"lis 0x{lis_address:08X}, access 0x{access_address:08X}, instruction 0x{access_instruction:08X}")


if __name__ == "__main__":
    main()

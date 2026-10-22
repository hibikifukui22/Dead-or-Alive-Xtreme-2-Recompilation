"""Locate common frame-rate constants in the locally decoded XEX image."""

from pathlib import Path
import struct

root = Path(__file__).resolve().parent.parent
image = (root / "logs" / "default-image.bin").read_bytes()

for name, value in (("30.0", 30.0), ("60.0", 60.0), ("1/30", 1.0 / 30.0), ("1/60", 1.0 / 60.0)):
    pattern = struct.pack(">f", value)
    offsets = []
    offset = image.find(pattern)
    while offset != -1:
        offsets.append(offset)
        offset = image.find(pattern, offset + 1)
    addresses = ", ".join(f"0x{0x82000000 + offset:08X}" for offset in offsets[:64])
    print(f"{name}: {len(offsets)} occurrence(s){': ' + addresses if addresses else ''}")

"""Check every pointer in the observed startup table has a generated entry."""
import pathlib
import re
import struct

root = pathlib.Path(__file__).resolve().parent.parent
image = (root / 'logs/default-image.bin').read_bytes()
registry = (root / 'generated/default/doax2_init.cpp').read_text()
registered = {int(value, 16) for value in re.findall(r'\{ 0x([0-9A-F]+), sub_', registry)}
missing = []
for slot in range(0x82A80B4C, 0x82A81C9C, 4):
    target = struct.unpack_from('>I', image, slot - 0x82000000)[0]
    if target not in registered:
        missing.append((hex(slot), hex(target)))
if missing:
    raise SystemExit(f'Unregistered startup targets: {missing}')
print('PASS: all 1,108 pointers in the audited startup table are registered.')

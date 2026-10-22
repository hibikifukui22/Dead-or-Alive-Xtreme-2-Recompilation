"""Read the unencrypted XEX header and record reproducible input metadata."""
import hashlib
import json
import pathlib
import struct

root = pathlib.Path(__file__).resolve().parent.parent
path = root / 'game/default.xex'
data = path.read_bytes()
if data[:4] != b'XEX2':
    raise ValueError('Expected an Xbox 360 XEX2 executable')
u32 = lambda offset: struct.unpack_from('>I', data, offset)[0]
headers = dict(struct.unpack_from('>II', data, 24 + i * 8)
               for i in range(u32(20)))
result = {'file': 'game/default.xex', 'bytes': len(data),
          'sha256': hashlib.sha256(data).hexdigest(),
          'format': 'XEX2'}
if 0x00040006 in headers:
    offset = headers[0x00040006]
    result.update(media_id=f'{u32(offset):08X}',
                  version_raw=f'{u32(offset+4):08X}',
                  base_version_raw=f'{u32(offset+8):08X}',
                  title_id=f'{u32(offset+12):08X}',
                  disc_number=data[offset+18], disc_count=data[offset+19])
output = json.dumps(result, indent=2) + '\n'
(root / 'input-metadata.json').write_text(output, encoding='utf-8')
print(output)

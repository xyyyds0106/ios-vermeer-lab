"""Package Image + raw payload in boot v4, preserving local stock OS version."""
from pathlib import Path
import struct
import sys

kernel_path, payload_path, stock_path, out_path = map(Path, sys.argv[1:5])
kernel = kernel_path.read_bytes()
payload = payload_path.read_bytes()
with stock_path.open('rb') as stream:
    stock = stream.read(4096)
assert stock[:8] == b'ANDROID!'
assert struct.unpack_from('<I', stock, 40)[0] == 4
assert kernel[56:60] == b'ARM\x64'
assert payload[:8] == b'SWEETXNU'
header = bytearray(4096)
header[:8] = b'ANDROID!'
struct.pack_into('<IIII', header, 8, len(kernel), len(payload),
                 struct.unpack_from('<I', stock, 16)[0], 1584)
struct.pack_into('<I', header, 40, 4)
header[44:56] = b'console=null'
image = header + kernel + bytes(-len(kernel) % 4096)
image += payload + bytes(-len(payload) % 4096)
out_path.write_bytes(image)
print(f'{out_path.name}: {len(image)} bytes; boot v4; temporary boot only')

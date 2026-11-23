"""Rebuild the checked-in glTF sampler control from the alpha fixture's quad."""
import base64
import json
from pathlib import Path
import struct
import zlib


def chunk(kind, data):
    return (struct.pack('>I', len(data)) + kind + data
            + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff))


assets = Path(__file__).parent / 'assets'
gltf = json.loads((assets / 'oit-opaque.gltf').read_text(encoding='utf-8'))
rows = b''.join(bytes([0]) + b''.join(
    bytes([((x+y) & 1)*255]*3 + [255]) for x in range(256)) for y in range(256))
png = (b'\x89PNG\r\n\x1a\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', 256, 256, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
gltf['images'][0]['uri'] = 'data:image/png;base64,' + base64.b64encode(png).decode()
gltf['samplers'][0].update(minFilter=9987, magFilter=9728)
gltf['materials'][0]['pbrMetallicRoughness']['baseColorFactor'] = [1, 1, 1, 1]
(assets / 'mipmaps.gltf').write_text(json.dumps(gltf), encoding='utf-8')

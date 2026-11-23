"""Deterministic core glTF alpha-mode fixtures; no external assets required."""
import base64
import json
from pathlib import Path
import struct
import zlib

def chunk(name, data):
    return struct.pack('>I', len(data)) + name + data + struct.pack('>I', zlib.crc32(name + data) & 0xffffffff)

png = b'\x89PNG\r\n\x1a\n'
png += chunk(b'IHDR', struct.pack('>IIBBBBB', 2, 1, 8, 6, 0, 0, 0))
png += chunk(b'IDAT', zlib.compress(bytes([0, 255, 0, 0, 64, 0, 0, 255, 192])))
png += chunk(b'IEND', b'')
positions = [-1,-1,0, 1,-1,0, 1,1,0, -1,1,0]
normals = [0,0,1] * 4
uv = [0,1, 1,1, 1,0, 0,0]
data = struct.pack('<32f6H', *(positions + normals + uv + [0,1,2,0,2,3]))
for mode in ('BLEND', 'MASK', 'OPAQUE'):
    material = {'alphaMode':mode, 'doubleSided':True, 'extensions':{'KHR_materials_unlit':{}},
                'pbrMetallicRoughness':{'baseColorTexture':{'index':0}, 'baseColorFactor':[1,1,1,.1 if mode=='OPAQUE' else 1]}}
    if mode == 'MASK': material['alphaCutoff'] = .5
    gltf = {'asset':{'version':'2.0'}, 'extensionsUsed':['KHR_materials_unlit'], 'scene':0,
            'scenes':[{'nodes':[0]}], 'nodes':[{'mesh':0}],
            'buffers':[{'uri':'data:application/octet-stream;base64,'+base64.b64encode(data).decode(), 'byteLength':len(data)}],
            'bufferViews':[{'buffer':0,'byteOffset':o,'byteLength':n} for o,n in [(0,48),(48,48),(96,32),(128,12)]],
            'accessors':[{'bufferView':0,'componentType':5126,'count':4,'type':'VEC3','min':[-1,-1,0],'max':[1,1,0]},
                         {'bufferView':1,'componentType':5126,'count':4,'type':'VEC3'},
                         {'bufferView':2,'componentType':5126,'count':4,'type':'VEC2'},
                         {'bufferView':3,'componentType':5123,'count':6,'type':'SCALAR'}],
            'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0}]}],
            'materials':[material], 'images':[{'uri':'data:image/png;base64,'+base64.b64encode(png).decode()}],
            'textures':[{'source':0,'sampler':0}], 'samplers':[{'magFilter':9728,'minFilter':9728,'wrapS':33071,'wrapT':33071}]}
    (Path(__file__).parent/'assets'/f'oit-{mode.lower()}.gltf').write_text(json.dumps(gltf), encoding='utf-8')

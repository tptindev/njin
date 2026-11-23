"""Regenerate the small, self-contained glTF material regression assets."""
import base64
import json
from pathlib import Path
import struct
import zlib


def png(width, height, rgba):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    rows = b"".join(b"\0" + bytes(rgba[y * width * 4:(y + 1) * width * 4]) for y in range(height))
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    return "data:image/png;base64," + base64.b64encode(data + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")).decode()


def main():
    data = bytearray()
    views, accessors = [], []

    def accessor(values, components, kind="f", component_type=5126):
        while len(data) % 4:
            data.append(0)
        payload = struct.pack("<" + kind * len(values), *values)
        views.append({"buffer": 0, "byteOffset": len(data), "byteLength": len(payload)})
        data.extend(payload)
        accessors.append({"bufferView": len(views) - 1, "componentType": component_type,
                          "count": len(values) // components, "type": {1: "SCALAR", 2: "VEC2", 3: "VEC3", 4: "VEC4"}[components]})
        return len(accessors) - 1

    position = accessor([-.5, -.5, 0, .5, -.5, 0, .5, .5, 0, -.5, .5, 0], 3)
    accessors[position].update(min=[-.5, -.5, 0], max=[.5, .5, 0])
    normal = accessor([0, 0, 1] * 4, 3)
    uv0 = accessor([.25, .5] * 4, 2)
    uv1 = accessor([0, 1, 1, 1, 1, 0, 0, 0], 2)
    tangent = accessor([1, 0, 0, -1] * 4, 4)
    indices = accessor([0, 1, 2, 0, 2, 3], 1, "H", 5123)
    images = [png(1, 1, [255, 255, 255, 255]),
              png(2, 1, [255, 128, 128, 255, 128, 128, 255, 255]),
              png(1, 1, [0, 255, 0, 255]), png(1, 1, [128, 0, 0, 255]),
              png(1, 1, [0, 0, 255, 255])]
    gltf = {
        "asset": {"version": "2.0", "generator": "Njin deterministic material map fixture"},
        "extensionsUsed": ["KHR_texture_transform", "KHR_materials_emissive_strength"],
        "extensionsRequired": ["KHR_texture_transform", "KHR_materials_emissive_strength"],
        "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
        "bufferViews": views, "accessors": accessors,
        "images": [{"uri": image} for image in images],
        "samplers": [{"magFilter": 9728, "minFilter": 9728, "wrapS": 33071, "wrapT": 33648}],
        "textures": [{"source": i, "sampler": 0} for i in range(5)],
        "materials": [{"name": "mapped", "doubleSided": True,
                       "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 1, "roughnessFactor": 1,
                                               "metallicRoughnessTexture": {"index": 2}},
                       "normalTexture": {"index": 1, "texCoord": 1, "scale": 1,
                                         "extensions": {"KHR_texture_transform": {"offset": [1, 0], "scale": [-1, 1]}}},
                       "occlusionTexture": {"index": 3, "strength": .5},
                       "emissiveTexture": {"index": 4}, "emissiveFactor": [0, 0, .5],
                       "extensions": {"KHR_materials_emissive_strength": {"emissiveStrength": 2}}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "TEXCOORD_0": uv0,
                                                       "TEXCOORD_1": uv1, "TANGENT": tangent}, "indices": indices, "material": 0}]}],
        "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0,
    }
    directory = Path(__file__).parent / "assets"
    (directory / "pbr-maps.gltf").write_text(json.dumps(gltf, indent=2) + "\n", encoding="utf-8")
    del gltf["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_1"]
    (directory / "pbr-maps-missing-uv.gltf").write_text(json.dumps(gltf, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

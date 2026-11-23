"""Rebuild the static triangle and dense skinned/morph LOD fixtures."""
from pathlib import Path
import base64
import json
import struct


def generate(animated):
    blob = bytearray()
    views, accessors = [], []

    def stream(values, fmt, components, kind, gl_type):
        while len(blob) % 4:
            blob.append(0)
        data = struct.pack("<" + fmt * len(values), *values)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data)})
        blob.extend(data)
        accessors.append({"bufferView": len(views) - 1, "componentType": gl_type,
                          "count": len(values) // components, "type": kind})
        return len(accessors) - 1

    if animated:
        positions = [(x / 10 - 1, y / 10 - 1, 0) for y in range(21) for x in range(21)]
    else:
        positions = [(-1, -1, 0), (1, -1, 0), (0, 1, 0)]
    count = len(positions)
    p = stream([c for v in positions for c in v], "f", 3, "VEC3", 5126)
    n = stream([c for _ in positions for c in (0, 0, 1)], "f", 3, "VEC3", 5126)
    primitive = {"attributes": {"POSITION": p, "NORMAL": n}}
    doc = {"asset": {"version": "2.0"}, "scene": 0,
           "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
           "meshes": [{"primitives": [primitive]}]}
    if animated:
        joints = stream([0] * (count * 4), "B", 4, "VEC4", 5121)
        weights = stream([c for _ in positions for c in (1, 0, 0, 0)], "f", 4, "VEC4", 5126)
        primitive["attributes"].update(JOINTS_0=joints, WEIGHTS_0=weights)
        indices = []
        for y in range(20):
            for x in range(20):
                a = y * 21 + x
                indices.extend((a, a + 1, a + 22, a, a + 22, a + 21))
        primitive["indices"] = stream(indices, "H", 1, "SCALAR", 5123)
        identity = [float(i % 5 == 0) for i in range(16)]
        bind = stream(identity * 2, "f", 16, "MAT4", 5126)
        morph = stream([c for x, y, z in positions for c in (0, .5 * (x + 1), 0)], "f", 3, "VEC3", 5126)
        primitive["targets"] = [{"POSITION": morph}]
        doc["meshes"][0].update(weights=[0], extras={"targetNames": ["slope"]})
        doc["nodes"] = [{"children": [1, 3]}, {"name": "root", "children": [2]},
                        {"name": "tip"}, {"mesh": 0, "skin": 0}]
        doc["skins"] = [{"joints": [1, 2], "inverseBindMatrices": bind}]
        times = stream([0, 1], "f", 1, "SCALAR", 5126)
        translation = stream([0, 0, 0, 2, 0, 0], "f", 3, "VEC3", 5126)
        morph_weights = stream([0, 1], "f", 1, "SCALAR", 5126)
        doc["animations"] = [
            {"name": name, "samplers": [{"input": times, "output": output, "interpolation": "LINEAR"}],
             "channels": [{"sampler": 0, "target": {"node": node, "path": path}}]}
            for name, output, node, path in [("move", translation, 0, "translation"),
                                            ("morph", morph_weights, 3, "weights")]]
    if not animated:
        uv = stream([0, 1, 1, 1, .5, 0], "f", 2, "VEC2", 5126)
        primitive["attributes"]["TEXCOORD_0"] = uv
    doc.update(bufferViews=views, accessors=accessors,
               buffers=[{"byteLength": len(blob), "uri": "data:application/octet-stream;base64,"
                         + base64.b64encode(blob).decode("ascii")}])
    name = "lod-animated-grid" if animated else "lod-triangle"
    (Path(__file__).parent / "assets" / f"{name}.gltf").write_text(json.dumps(doc, separators=(",", ":")), encoding="utf8")
    if not animated:
        doc["scenes"][0]["nodes"] = [0, 1]
        doc["nodes"] = [{"mesh": 0, "translation": [-2, 0, 0]}, {"mesh": 0, "translation": [2, 0, 0]}]
        primitive["material"] = 0
        doc["images"] = [{"uri": "atlas-green.png"}]
        doc["textures"] = [{"source": 0}]
        doc["materials"] = [{"name": "green", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}, "extensions": {"KHR_materials_unlit": {}}}]
        doc["extensionsUsed"] = ["KHR_materials_unlit"]
        (Path(__file__).parent / "assets" / "lod-merge.gltf").write_text(json.dumps(doc, separators=(",", ":")), encoding="utf8")


if __name__ == "__main__":
    generate(False)
    generate(True)

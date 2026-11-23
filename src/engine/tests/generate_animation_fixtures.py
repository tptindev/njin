"""Deterministic glTF skin/ancestor animation/morph fixture; no external tools."""
import copy
import base64
import json
from pathlib import Path
import struct

data = bytearray()
views, accessors = [], []


def accessor(values, kind, component=5126):
    while len(data) % 4:
        data.append(0)
    offset = len(data)
    count = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": 16}[kind]
    data.extend(struct.pack("<" + ("f" if component == 5126 else "B") * len(values), *values))
    views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset})
    accessors.append({"bufferView": len(views) - 1, "componentType": component, "count": len(values) // count, "type": kind})
    return len(accessors) - 1


position = accessor([-.5,-.5,0,.5,-.5,0,.5,.5,0,-.5,-.5,0,.5,.5,0,-.5,.5,0], "VEC3")
normal = accessor([0,0,1] * 6, "VEC3")
joints = accessor([0,0,0,0] * 6, "VEC4", 5121)
weights = accessor([1,0,0,0] * 6, "VEC4")
inverse = accessor([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1] * 2, "MAT4")
delta = accessor([0,.5,0] * 6, "VEC3")
times = accessor([0,1], "SCALAR")
move = accessor([0,0,0,2,0,0], "VEC3")
scale = accessor([1,1,1,2,2,2], "VEC3")
rotate = accessor([0,0,0,1,0,0,2**-.5,2**-.5], "VEC4")
morph_times = accessor([0,2], "SCALAR")
morph = accessor([0,0,1,0,1,0], "SCALAR")


def clip(name, node, path, output, interpolation="LINEAR", input_times=times):
    return {"name": name, "samplers": [{"input": input_times, "output": output, "interpolation": interpolation}],
            "channels": [{"sampler": 0, "target": {"node": node, "path": path}}]}


document = {
    "asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0]}],
    "nodes": [{"name": "animated-ancestor", "children": [1,3]}, {"name": "root", "children": [2]},
              {"name": "tip"}, {"name": "skin-mesh", "mesh": 0, "skin": 0}],
    "skins": [{"joints": [1,2], "inverseBindMatrices": inverse}],
    "meshes": [{"name": "panel", "weights": [0], "extras": {"targetNames": ["up"]},
                "primitives": [{"attributes": {"POSITION": position, "NORMAL": normal, "JOINTS_0": joints, "WEIGHTS_0": weights},
                                "targets": [{"POSITION": delta}], "material": 0}]}],
    "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [1,1,1,1]}, "doubleSided": True,
                   "extensions": {"KHR_materials_unlit": {}}}], "extensionsUsed": ["KHR_materials_unlit"],
    "animations": [clip("move",0,"translation",move), clip("scale",1,"scale",scale),
                   clip("rotate",1,"rotation",rotate), clip("morph",3,"weights",morph,"CUBICSPLINE",morph_times),clip("step",0,"translation",move,"STEP"),clip("tip",2,"translation",move)],
    "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64," + base64.b64encode(data).decode()}],
    "bufferViews": views, "accessors": accessors,
}
assets = Path(__file__).parent / "assets"
(assets / "animated-model.gltf").write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
# Nonidentity mesh/rest-joint transforms with a nonidentity inverse-bind matrix.
bind = copy.deepcopy(document)
bind["nodes"][1]["translation"] = [3,0,0]
bind["nodes"][3]["translation"] = [2,0,0]
bind_data = bytearray(data)
for k in range(2):
    struct.pack_into("<f", bind_data, views[inverse]["byteOffset"] + k*64 + 48, -1)
bind["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(bind_data).decode()
(assets / "animated-bind-model.gltf").write_text(json.dumps(bind, separators=(",", ":")) + "\n", encoding="utf-8")
weighted = copy.deepcopy(document)
weighted_data = bytearray(data)
for v in range(6):
    struct.pack_into("<4B", weighted_data, views[joints]["byteOffset"] + v*4, 0,1,0,0)
    struct.pack_into("<4f", weighted_data, views[weights]["byteOffset"] + v*16, .5,.5,0,0)
weighted["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(weighted_data).decode()
(assets / "animated-weighted-model.gltf").write_text(json.dumps(weighted, separators=(",", ":")) + "\n", encoding="utf-8")
invalid = copy.deepcopy(document)
invalid_data = bytearray(data)
invalid_data[views[joints]["byteOffset"]] = 127
invalid["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(invalid_data).decode()
(assets / "animated-invalid-model.gltf").write_text(json.dumps(invalid, separators=(",", ":")) + "\n", encoding="utf-8")
# Two mesh nodes share a named target but keep independent defaults/curves.
node_morphs = copy.deepcopy(document)
node_morphs["skins"] = []
node_morphs["nodes"][3].pop("skin")
node_morphs["nodes"].append({"name": "independent-morph", "mesh": 0, "translation": [2,0,0], "weights": [1]})
node_morphs["nodes"][0]["children"].append(4)
node_morphs["animations"] = [document["animations"][3]]
(assets / "animated-node-morphs.gltf").write_text(json.dumps(node_morphs, separators=(",", ":")) + "\n", encoding="utf-8")
# The joint is a scene root: exercise the null-parent case without raylib's loader.
document["scenes"][0]["nodes"] = [1,3]
document["nodes"][0]["children"] = []
document["animations"][0]["channels"][0]["target"]["node"] = 1
(assets / "animated-root-model.gltf").write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")

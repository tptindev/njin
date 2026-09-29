"""Builds the models of the platformer3d sample.

Run: python src/games/platformer3d/tools/make_assets.py
Needs only the standard library. The output lands in
src/games/platformer3d/assets and is committed.

  robot.glb  the player: a boxy robot on a 7-bone skeleton (glTF skin) with
             three clips, "idle", "run" and "jump"; every box follows one bone,
             coloured by vertex colours
  hill.glb   a grassy hill next to the start platform, walked on through a
             triangle-mesh body
"""

import json
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))


class Glb:
    """Collects buffer views and accessors, writes one binary glTF."""

    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def add(self, fmt, values, comp, type_, count, target=None, minmax=False):
        while len(self.bin) % 4:
            self.bin.append(0)
        offset = len(self.bin)
        self.bin += struct.pack("<" + fmt * len(values), *values)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(self.bin) - offset}
        if target:
            view["target"] = target
        self.views.append(view)
        acc = {"bufferView": len(self.views) - 1, "componentType": comp, "count": count, "type": type_}
        if minmax:
            n = len(values) // count
            acc["min"] = [min(values[i::n]) for i in range(n)]
            acc["max"] = [max(values[i::n]) for i in range(n)]
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def write(self, path, gltf):
        gltf["asset"] = {"version": "2.0", "generator": "njin platformer3d make_assets.py"}
        gltf["bufferViews"] = self.views
        gltf["accessors"] = self.accessors
        while len(self.bin) % 4:
            self.bin.append(0)
        gltf["buffers"] = [{"byteLength": len(self.bin)}]
        js = json.dumps(gltf, separators=(",", ":")).encode()
        while len(js) % 4:
            js += b" "
        total = 12 + 8 + len(js) + 8 + len(self.bin)
        with open(path, "wb") as f:
            f.write(struct.pack("<III", 0x46546C67, 2, total))
            f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
            f.write(struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin))


FLOAT, UBYTE, USHORT = 5126, 5121, 5123
ARRAY, ELEMENTS = 34962, 34963


def box(lo, hi):
    """24 vertices (a normal per face) and 36 indices of an axis-aligned box."""
    (x0, y0, z0), (x1, y1, z1) = lo, hi
    faces = [
        ((1, 0, 0), [(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)]),
        ((-1, 0, 0), [(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)]),
        ((0, 1, 0), [(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)]),
        ((0, -1, 0), [(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)]),
        ((0, 0, 1), [(x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]),
        ((0, 0, -1), [(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)]),
    ]
    verts, idx = [], []
    for n, quad in faces:
        base = len(verts)
        verts += [(p, n) for p in quad]
        idx += [base, base + 1, base + 2, base, base + 2, base + 3]
    return verts, idx


def quat_axis(axis, degrees):
    a = math.radians(degrees) / 2
    s = math.sin(a)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(a)]


def make_robot():
    # Bone heads in model space; the robot stands on y = 0 and faces +z.
    bones = [
        ("hips", None, (0.0, 0.52, 0.0)),
        ("spine", 0, (0.0, 0.58, 0.0)),
        ("head", 1, (0.0, 0.9, 0.0)),
        ("arm_l", 1, (0.25, 0.84, 0.0)),
        ("arm_r", 1, (-0.25, 0.84, 0.0)),
        ("leg_l", 0, (0.1, 0.5, 0.0)),
        ("leg_r", 0, (-0.1, 0.5, 0.0)),
    ]
    yellow = (0.95, 0.75, 0.2, 1.0)
    light = (1.0, 0.86, 0.45, 1.0)
    dark = (0.08, 0.09, 0.12, 1.0)
    steel = (0.35, 0.4, 0.5, 1.0)
    orange = (0.95, 0.55, 0.2, 1.0)
    parts = [
        ((-0.2, 0.5, -0.13), (0.2, 0.88, 0.13), 1, yellow),
        ((-0.17, 0.9, -0.15), (0.17, 1.12, 0.15), 2, light),
        ((0.04, 0.98, 0.15), (0.1, 1.04, 0.17), 2, dark),
        ((-0.1, 0.98, 0.15), (-0.04, 1.04, 0.17), 2, dark),
        ((-0.03, 1.12, -0.03), (0.03, 1.2, 0.03), 2, orange),
        ((0.21, 0.54, -0.05), (0.3, 0.86, 0.05), 3, orange),
        ((-0.3, 0.54, -0.05), (-0.21, 0.86, 0.05), 4, orange),
        ((0.03, 0.0, -0.07), (0.17, 0.5, 0.07), 5, steel),
        ((-0.17, 0.0, -0.07), (-0.03, 0.5, 0.07), 6, steel),
        ((0.03, 0.0, -0.07), (0.17, 0.06, 0.13), 5, dark),
        ((-0.17, 0.0, -0.07), (-0.03, 0.06, 0.13), 6, dark),
    ]
    pos, nrm, col, joints, weights, idx = [], [], [], [], [], []
    for lo, hi, bone, color in parts:
        verts, ids = box(lo, hi)
        base = len(pos) // 3
        for p, n in verts:
            pos += p
            nrm += n
            col += color
            joints += [bone, 0, 0, 0]
            weights += [1.0, 0.0, 0.0, 0.0]
        idx += [base + i for i in ids]
    count = len(pos) // 3

    g = Glb()
    a_pos = g.add("f", pos, FLOAT, "VEC3", count, ARRAY, minmax=True)
    a_nrm = g.add("f", nrm, FLOAT, "VEC3", count, ARRAY)
    a_col = g.add("f", col, FLOAT, "VEC4", count, ARRAY)
    a_joint = g.add("B", joints, UBYTE, "VEC4", count, ARRAY)
    a_weight = g.add("f", weights, FLOAT, "VEC4", count, ARRAY)
    a_idx = g.add("H", idx, USHORT, "SCALAR", len(idx), ELEMENTS)
    inv_bind = []
    for _, _, head in bones:
        inv_bind += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -head[0], -head[1], -head[2], 1]
    a_ibm = g.add("f", inv_bind, FLOAT, "MAT4", len(bones))

    # Nodes: 0 the skinned mesh, 1 the armature (Blender exports one above the
    # root bone, and raylib expects it), 2.. the bones.
    first = 2
    nodes = [{"name": "robot", "mesh": 0, "skin": 0}, {"name": "armature", "children": [first]}]
    for i, (name, parent, head) in enumerate(bones):
        ph = bones[parent][2] if parent is not None else (0.0, 0.0, 0.0)
        nodes.append({"name": name, "translation": [head[k] - ph[k] for k in range(3)]})
    for i, (_, parent, _) in enumerate(bones):
        if parent is not None:
            nodes[parent + first].setdefault("children", []).append(i + first)

    x, z = (1, 0, 0), (0, 0, 1)

    def clip(name, duration, keys, tracks, hips_bob=None):
        """keys: times; tracks: {bone: (axis, [degrees per key])}."""
        channels, samplers = [], []
        a_time = g.add("f", keys, FLOAT, "SCALAR", len(keys), minmax=True)
        for bone, (axis, degs) in tracks.items():
            rot = []
            for d in degs:
                rot += quat_axis(axis, d)
            samplers.append({"input": a_time, "output": g.add("f", rot, FLOAT, "VEC4", len(keys))})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": bone + first, "path": "rotation"}})
        if hips_bob:
            t = []
            for dy in hips_bob:
                t += [0.0, bones[0][2][1] + dy, 0.0]
            samplers.append({"input": a_time, "output": g.add("f", t, FLOAT, "VEC3", len(keys))})
            channels.append({"sampler": len(samplers) - 1, "target": {"node": first, "path": "translation"}})
        assert abs(keys[-1] - duration) < 1e-6
        return {"name": name, "channels": channels, "samplers": samplers}

    idle_t = [0.0, 1.0, 2.0]
    run_t = [0.0, 0.15, 0.3, 0.45, 0.6]
    jump_t = [0.0, 0.2]
    animations = [
        clip("idle", 2.0, idle_t,
             {1: (x, [0, 3, 0]), 2: (x, [0, -5, 0]), 3: (z, [4, 9, 4]), 4: (z, [-4, -9, -4])},
             hips_bob=[0.0, -0.015, 0.0]),
        clip("run", 0.6, run_t,
             {5: (x, [-40, 0, 40, 0, -40]), 6: (x, [40, 0, -40, 0, 40]),
              3: (x, [35, 0, -35, 0, 35]), 4: (x, [-35, 0, 35, 0, -35]),
              1: (x, [8, 8, 8, 8, 8]), 2: (x, [-6, -6, -6, -6, -6])},
             hips_bob=[0.0, 0.04, 0.0, 0.04, 0.0]),
        clip("jump", 0.2, jump_t,
             {3: (z, [10, 150]), 4: (z, [-10, -150]), 5: (x, [0, -30]), 6: (x, [0, 20])}),
    ]

    gltf = {
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": nodes,
        "meshes": [{"primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "COLOR_0": a_col,
                                                   "JOINTS_0": a_joint, "WEIGHTS_0": a_weight},
                                    "indices": a_idx, "material": 0}]}],
        "materials": [{"name": "robot", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
        "skins": [{"joints": list(range(first, len(bones) + first)), "inverseBindMatrices": a_ibm, "skeleton": first}],
        "animations": animations,
    }
    g.write(os.path.join(OUT, "robot.glb"), gltf)


def make_hill():
    # 8 x 8 units, centred on the origin: 0.5 high at the rim (level with the
    # start platform), a round top 1.7 high, and a skirt down to -0.5.
    n = 24
    size = 8.0

    def height(x, z):
        r2 = x * x + z * z
        return 0.5 + 1.2 * math.exp(-r2 / 5.0) * (1.0 - min(1.0, max(abs(x), abs(z)) / 4.0) ** 3)

    pos, nrm, col, idx = [], [], [], []
    grid = []
    for j in range(n + 1):
        for i in range(n + 1):
            x = -size / 2 + size * i / n
            z = -size / 2 + size * j / n
            grid.append((x, height(x, z), z))
    for j in range(n + 1):
        for i in range(n + 1):
            x, y, z = grid[j * (n + 1) + i]
            e = 0.05
            dx = (height(x + e, z) - height(x - e, z)) / (2 * e)
            dz = (height(x, z + e) - height(x, z - e)) / (2 * e)
            l = math.sqrt(dx * dx + 1 + dz * dz)
            pos += [x, y, z]
            nrm += [-dx / l, 1 / l, -dz / l]
            k = (y - 0.5) / 1.2
            col += [0.35 + 0.15 * k, 0.62 + 0.1 * k, 0.3, 1.0]
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            b, c, d = a + 1, a + n + 1, a + n + 2
            idx += [a, c, b, b, c, d]
    # The skirt: each rim edge dropped to y = -0.5, facing out.
    rims = [
        ([j * (n + 1) + n for j in range(n + 1)], (1, 0, 0)),
        ([j * (n + 1) for j in range(n, -1, -1)], (-1, 0, 0)),
        ([n * (n + 1) + i for i in range(n, -1, -1)], (0, 0, 1)),
        ([i for i in range(n + 1)], (0, 0, -1)),
    ]
    for ring, normal in rims:
        base = len(pos) // 3
        for v in ring:
            x, y, z = grid[v]
            pos += [x, y, z, x, -0.5, z]
            nrm += list(normal) * 2
            col += [0.45, 0.36, 0.25, 1.0] * 2
        for k in range(len(ring) - 1):
            t0, b0, t1, b1 = base + 2 * k, base + 2 * k + 1, base + 2 * k + 2, base + 2 * k + 3
            idx += [t0, t1, b0, b0, t1, b1]
    count = len(pos) // 3
    g = Glb()
    a_pos = g.add("f", pos, FLOAT, "VEC3", count, ARRAY, minmax=True)
    a_nrm = g.add("f", nrm, FLOAT, "VEC3", count, ARRAY)
    a_col = g.add("f", col, FLOAT, "VEC4", count, ARRAY)
    a_idx = g.add("H", idx, USHORT, "SCALAR", len(idx), ELEMENTS)
    gltf = {
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "hill", "mesh": 0}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": a_pos, "NORMAL": a_nrm, "COLOR_0": a_col},
                                    "indices": a_idx, "material": 0}]}],
        "materials": [{"name": "grass", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
    }
    g.write(os.path.join(OUT, "hill.glb"), gltf)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_robot()
    make_hill()
    print("wrote", OUT)

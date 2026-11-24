"""Writes the small IQM and VOX files of the model_formats test (src/engine/tests/assets/).

    python src/engine/tests/assets/make_model_formats.py

bend.iqm: a quad (4 vertices, 2 triangles) on two bones, the lower half on the root bone
and the upper half on its child one unit up, and one clip "bend" of 3 frames at 30 fps:
rest, the child turned 90 degrees about Z, and the same again (the legacy renderer's
end-of-clip fix replaces the last frame with the one before, so it repeats it here).
blocks.vox: a 4x3x2 MagicaVoxel model with three voxels of palette colours 1, 2 and 3.
The M3D file is written by make_m3d.cpp (it needs m3d.h's exporter).
"""
import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def iqm():
    text = b"\0bend\0root\0child\0"
    name_mesh, name_bend, name_root, name_child = 0, 1, 6, 11  # offsets into text (0: empty)
    name_mesh = 0
    positions = [(-0.5, 0, 0), (0.5, 0, 0), (0.5, 2, 0), (-0.5, 2, 0)]
    normals = [(0, 0, 1)] * 4
    texcoords = [(0, 1), (1, 1), (1, 0), (0, 0)]
    blend_index = [(0, 0, 0, 0), (0, 0, 0, 0), (1, 0, 0, 0), (1, 0, 0, 0)]
    blend_weight = [(255, 0, 0, 0)] * 4
    triangles = [(0, 2, 1), (0, 3, 2)]  # IQM fronts are clockwise (raylib reverses them)
    # joints: name, parent, translate, rotate (x, y, z, w), scale; local to the parent
    joints = [(name_root, -1, (0, 0, 0), (0, 0, 0, 1), (1, 1, 1)),
              (name_child, 0, (0, 1, 0), (0, 0, 0, 1), (1, 1, 1))]
    half = math.sqrt(0.5)
    child_frames = [(0, 0, 0, 1), (0, 0, half, half), (0, 0, half, half)]

    # Poses: every channel animated (mask 0x3ff), offset 0, scale 1/65535 over [-1, 1]
    # for rotations and positions, so a channel value is offset + u16 * scale.
    def pose_channels(rest_t, rest_s):
        offset = [rest_t[0] - 1, rest_t[1] - 1, rest_t[2] - 1, -1, -1, -1, -1, rest_s[0] - 1, rest_s[1] - 1, rest_s[2] - 1]
        scale = [2 / 65535] * 10
        return offset, scale

    poses = []
    for j in joints:
        off, sc = pose_channels(j[2], j[4])
        poses.append((j[1], 0x3FF, off, sc))

    def quantize(value, off, sc):
        return max(0, min(65535, round((value - off) / sc)))

    frames = []
    for f in range(3):
        for p, j in enumerate(joints):
            rot = (0, 0, 0, 1) if p == 0 else child_frames[f]
            values = list(j[2]) + list(rot) + list(j[4])
            off, sc = poses[p][2], poses[p][3]
            frames += [quantize(v, off[c], sc[c]) for c, v in enumerate(values)]

    header_size = 16 + 27 * 4
    out = bytearray(header_size)
    def place(data):
        offset = len(out)
        out.extend(data)
        while len(out) % 4:
            out.append(0)
        return offset

    ofs_text = place(text)
    va = [  # type, format, size, data
        (0, 7, 3, b"".join(struct.pack("<3f", *v) for v in positions)),
        (2, 7, 3, b"".join(struct.pack("<3f", *v) for v in normals)),
        (1, 7, 2, b"".join(struct.pack("<2f", *v) for v in texcoords)),
        (4, 1, 4, b"".join(struct.pack("<4B", *v) for v in blend_index)),
        (5, 1, 4, b"".join(struct.pack("<4B", *v) for v in blend_weight)),
    ]
    va_offsets = [place(d) for _, _, _, d in va]
    ofs_vertexarrays = place(b"".join(struct.pack("<5I", t, 0, fmt, size, o) for (t, fmt, size, _), o in zip(va, va_offsets)))
    ofs_triangles = place(b"".join(struct.pack("<3I", *t) for t in triangles))
    ofs_meshes = place(struct.pack("<6I", name_mesh, 0, 0, 4, 0, 2))
    ofs_joints = place(b"".join(struct.pack("<Ii3f4f3f", n, p, *t, *r, *s) for n, p, t, r, s in joints))
    ofs_poses = place(b"".join(struct.pack("<iI10f10f", p, m, *o, *s) for p, m, o, s in poses))
    ofs_anims = place(struct.pack("<3IfI", name_bend, 0, 3, 30.0, 1))
    ofs_frames = place(struct.pack("<%dH" % len(frames), *frames))
    struct.pack_into("<16s27I", out, 0, b"INTERQUAKEMODEL\0", 2, len(out), 0,
                     len(text), ofs_text, 1, ofs_meshes, len(va), 4, ofs_vertexarrays, 2, ofs_triangles, 0,
                     len(joints), ofs_joints, len(poses), ofs_poses, 1, ofs_anims, 3, 20, ofs_frames, 0, 0, 0, 0, 0)
    with open(os.path.join(HERE, "bend.iqm"), "wb") as f:
        f.write(out)


def vox():
    def chunk(tag, content, children=b""):
        return tag + struct.pack("<2I", len(content), len(children)) + content + children
    size = chunk(b"SIZE", struct.pack("<3I", 4, 3, 2))
    voxels = [(0, 0, 0, 1), (3, 0, 0, 2), (1, 2, 1, 3)]
    xyzi = chunk(b"XYZI", struct.pack("<I", len(voxels)) + b"".join(struct.pack("<4B", *v) for v in voxels))
    palette = bytearray(256 * 4)
    for i, c in enumerate([(255, 0, 0), (0, 255, 0), (0, 0, 255)]):  # colour index k is palette entry k - 1
        palette[i * 4:i * 4 + 4] = bytes((*c, 255))
    rgba = chunk(b"RGBA", bytes(palette))
    main = chunk(b"MAIN", b"", size + xyzi + rgba)
    with open(os.path.join(HERE, "blocks.vox"), "wb") as f:
        f.write(b"VOX " + struct.pack("<I", 150) + main)


iqm()
vox()

"""Copies the pieces of Quaternius' Downtown City MegaKit that sandtable builds
its city from into src/games/sandtable/assets/models/city.

    python src/games/sandtable/tools/make_city_kit.py "<path to Downtown City MegaKit[Standard]>"

Needs Pillow. What it writes is committed, so building the game does not need
Python or the kit. For each piece it keeps the glTF and its .bin, and:
- only the base colour textures, shrunk to TEXTURE_SIZE (the engine draws the
  city instanced, which reads neither normal nor ORM maps, and from a sand
  table's height 512 pixels are plenty);
- no vertex colours (the kit stores wear masks there, which raylib would
  multiply into the colour);
- opaque glass (see-through glass would show the ground behind the wall).
It also writes kit.json: every piece's bounds and the boxes of its glass, in
the piece's own metres, so the game can size the pieces to a building and
light the windows at night.
"""

import json
import os
import shutil
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets", "models", "city"))
TEXTURE_SIZE = 512

PIECES = [
    # Painted plaster (the Trim set): street fronts, tinted per house.
    "Trim_Plain_3", "Trim_Window", "Trim_FirstFloor_Window_001", "Trim_FirstFloor_Wall",
    "Trim_Column_Center", "Trim_Wall_Guard", "Cornice_Trim_Center", "DoorFrame_Trim",
    # Red brick: markets, schools, sheds, the old quarter.
    "Brick_Plain_3", "Brick_Window_Square_Single", "Brick_Window_Trim_Single", "Brick_Window_CurvedDouble",
    "Brick_BottomTrim", "Brick_TopTrim", "Brick_CornerColumn_Center", "Cornice_Brick_Center", "DoorFrame_Wooden",
    # Glass and metal: flats, hotels, the new town.
    "Metal_Plain_3", "Metal_Window_Half", "Metal_FullWindow", "Metal_Window", "Metal_FirstFloor_Window",
    "Metal_FirstFloor_Wall", "Metal_Column_Center", "Cornice_Metal_Center", "DoorFrame_Metal_Single",
    # Doors and things on the street.
    "Door_1", "Door_2", "Prop_ACUnit", "Prop_Bollard", "Prop_Planter_Single", "Prop_ManholeCover",
]

GLASS = [0.16, 0.2, 0.26, 1.0]


def bounds(g, prims):
    lo, hi = [1e9] * 3, [-1e9] * 3
    for p in prims:
        a = g["accessors"][p["attributes"]["POSITION"]]
        for i in range(3):
            lo[i] = min(lo[i], a["min"][i])
            hi[i] = max(hi[i], a["max"][i])
    return [round(v, 4) for v in lo], [round(v, 4) for v in hi]


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    kit = sys.argv[1]
    src = os.path.join(kit, "Exports", "glTF (Godot)")
    textures = os.path.join(kit, "Textures")
    os.makedirs(os.path.join(OUT, "textures"), exist_ok=True)
    table = {}
    used_images = set()
    for name in PIECES:
        g = json.load(open(os.path.join(src, name + ".gltf"), encoding="utf-8"))
        # Only the base colour textures, by file name.
        keep = {}  # old texture index -> image file name
        for m in g.get("materials", []):
            for key in ("normalTexture", "occlusionTexture", "emissiveTexture"):
                m.pop(key, None)
            pbr = m.setdefault("pbrMetallicRoughness", {})
            pbr.pop("metallicRoughnessTexture", None)
            if "baseColorTexture" in pbr:
                t = pbr["baseColorTexture"]["index"]
                keep[t] = g["images"][g["textures"][t]["source"]]["uri"]
            if m.get("name") == "MI_Glass":
                pbr["baseColorFactor"] = GLASS
                m.pop("alphaMode", None)
        # Renumber the kept textures, each its own image.
        new_index = {old: i for i, old in enumerate(sorted(keep))}
        g["images"] = [{"uri": "textures/" + keep[old]} for old in sorted(keep)]
        g["textures"] = [{"source": i, "sampler": 0} for i in range(len(keep))]
        g["samplers"] = [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}]
        for m in g.get("materials", []):
            pbr = m.get("pbrMetallicRoughness", {})
            if "baseColorTexture" in pbr:
                pbr["baseColorTexture"] = {"index": new_index[pbr["baseColorTexture"]["index"]]}
        used_images.update(keep.values())
        if not keep:
            for key in ("images", "textures", "samplers"):
                g.pop(key, None)
        # No vertex colours, no second UV set.
        prims = [p for mesh in g["meshes"] for p in mesh["primitives"]]
        for p in prims:
            for key in ("COLOR_0", "COLOR_1", "TEXCOORD_1"):
                p["attributes"].pop(key, None)
        lo, hi = bounds(g, prims)
        glass = []
        for p in prims:
            mat = g["materials"][p["material"]].get("name", "") if "material" in p else ""
            if mat == "MI_Glass":
                glo, ghi = bounds(g, [p])
                glass.append(glo + ghi)
        table[name] = {"min": lo, "max": hi, "glass": glass}
        with open(os.path.join(OUT, name + ".gltf"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(g, f, indent=1)
        for b in g.get("buffers", []):
            shutil.copyfile(os.path.join(src, b["uri"]), os.path.join(OUT, b["uri"]))
    for img in sorted(used_images):
        im = Image.open(os.path.join(textures, img)).convert("RGB")
        im.resize((TEXTURE_SIZE, TEXTURE_SIZE), Image.LANCZOS).save(os.path.join(OUT, "textures", img), optimize=True)
    with open(os.path.join(OUT, "kit.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(table, f, indent=1)
    shutil.copyfile(os.path.join(kit, "License_Standard.txt"), os.path.join(OUT, "LICENSE-kit.txt"))
    print(f"{len(PIECES)} pieces, {len(used_images)} textures -> {OUT}")


if __name__ == "__main__":
    main()

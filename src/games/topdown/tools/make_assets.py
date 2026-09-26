"""Builds the art, sound and Tiled map of the top-down sample.

Run: python src/games/topdown/tools/make_assets.py
Needs Pillow. Reuses the tone and song helpers of the platformer's script.
The output lands in src/games/topdown/assets and is committed.
"""

import math
import os
import random
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))
sys.path.insert(0, os.path.join(HERE, "..", "..", "platformer", "tools"))
import make_assets as pa  # noqa: E402

T = 16
C = pa.C
px, rect = pa.px, pa.rect


def make_tiles():
    rng = random.Random(5)
    img = Image.new("RGBA", (8 * T, 2 * T), (0, 0, 0, 0))

    def cell(i):
        return (i % 8) * T, (i // 8) * T

    # 0 grass, 1 grass with a flower, 2 path, 3..6 water (animated), 7 stone wall
    for i in (0, 1):
        ox, oy = cell(i)
        rect(img, ox, oy, ox + 15, oy + 15, C["grass"])
        for _ in range(10):
            px(img, ox + rng.randrange(16), oy + rng.randrange(16), C["grass_dark"])
    ox, oy = cell(1)
    for (x, y) in ((7, 8), (6, 9), (8, 9), (7, 10)):
        px(img, ox + x, oy + y, C["pink"])
    px(img, ox + 7, oy + 9, C["yellow"])
    ox, oy = cell(2)
    rect(img, ox, oy, ox + 15, oy + 15, C["dirt_light"])
    for _ in range(14):
        px(img, ox + rng.randrange(16), oy + rng.randrange(16), C["dirt"])
    for f in range(4):
        ox, oy = cell(3 + f)
        rect(img, ox, oy, ox + 15, oy + 15, C["water"])
        for x in range(T):
            y = 5 + int(round(math.sin((x + f * 4) / 16 * 2 * math.pi) * 1.5))
            px(img, ox + x, oy + y, C["water_light"])
            y2 = 12 + int(round(math.sin((x - f * 4) / 16 * 2 * math.pi)))
            px(img, ox + x, oy + y2, C["water_dark"])
    ox, oy = cell(7)
    rect(img, ox, oy, ox + 15, oy + 15, C["stone"])
    for y in (0, 8):
        rect(img, ox, oy + y, ox + 15, oy + y, C["stone_dark"])
    for (x, y0) in ((0, 0), (8, 8)):
        rect(img, ox + x, oy + y0, ox + x, oy + y0 + 7, C["stone_dark"])
    rect(img, ox, oy + 15, ox + 15, oy + 15, C["stone_dark"])
    img.save(os.path.join(OUT, "tiles.png"))


def draw_player(img, ox, oy, frame):
    """0-2 facing down (idle, walk a, walk b), 3-5 facing up."""
    up = frame >= 3
    step = frame % 3
    for y in range(4, 13):
        for x in range(4, 12):
            px(img, ox + x, oy + y, C["leaf"] if not (y in (4, 12) and x in (4, 11)) else (0, 0, 0, 0))
    rect(img, ox + 4, oy + 4, ox + 11, oy + 5, C["grass_dark"])
    if not up:
        for x in (6, 9):
            px(img, ox + x, oy + 8, C["outline"])
            px(img, ox + x, oy + 7, C["white"])
    else:
        rect(img, ox + 5, oy + 7, ox + 10, oy + 8, C["grass_dark"])
    for (x, y) in ((8, 1), (9, 1), (9, 2), (7, 2), (8, 3)):
        px(img, ox + x, oy + y, C["grass_dark"])
    legs = {0: ((5, 0), (9, 0)), 1: ((5, 1), (9, 0)), 2: ((5, 0), (9, 1))}[step]
    for (x, lift) in legs:
        rect(img, ox + x, oy + 13 - lift, ox + x + 1, oy + 14 - lift, C["outline"])


def make_sprites():
    img = Image.new("RGBA", (8 * T, 8 * T), (0, 0, 0, 0))

    def cell(i):
        return (i % 8) * T, (i // 8) * T

    for f in range(6):
        draw_player(img, *cell(f), f)
    # slime: 8, 9 bounce; 10 hurt
    for f in range(3):
        ox, oy = cell(8 + f)
        h = 9 if f == 1 else 7
        for y in range(15 - h, 15):
            for x in range(2, 14):
                if (x - 7.5) ** 2 / 36 + (y - 14.5) ** 2 / (h * h) <= 1:
                    px(img, ox + x, oy + y, C["white"] if f == 2 else C["pink"])
        if f != 2:
            for x in (5, 9):
                px(img, ox + x, oy + 15 - h // 2 - 2, C["outline"])
                px(img, ox + x, oy + 15 - h // 2 - 3, C["white"])
    # chest 16 closed, 17 open
    for f in range(2):
        ox, oy = cell(16 + f)
        rect(img, ox + 2, oy + 7, ox + 13, oy + 14, C["wood"])
        rect(img, ox + 2, oy + 7, ox + 13, oy + 8, C["wood_dark"])
        rect(img, ox + 7, oy + 9, ox + 8, oy + 11, C["yellow"])
        if f == 1:
            rect(img, ox + 2, oy + 3, ox + 13, oy + 6, C["wood_dark"])
            rect(img, ox + 4, oy + 8, ox + 11, oy + 9, C["yellow"])
    # hearts 18 full, 19 empty
    for f in range(2):
        ox, oy = cell(18 + f)
        color = C["red"] if f == 0 else C["gray"]
        for (x0, x1, y) in ((3, 6, 4), (9, 12, 4), (2, 13, 5), (2, 13, 6), (3, 12, 7), (4, 11, 8), (5, 10, 9), (6, 9, 10), (7, 8, 11)):
            rect(img, ox + x0, oy + y, ox + x1, oy + y, color)
    # npc 20, 21
    for f in range(2):
        ox, oy = cell(20 + f)
        for y in range(3, 15):
            for x in range(3, 13):
                if (x - 7.5) ** 2 / 25 + (y - 9) ** 2 / 36 <= 1:
                    px(img, ox + x, oy + y, C["purple"])
        rect(img, ox + 5, oy + 8, ox + 10, oy + 11, C["skin"])
        for x in (6, 9):
            px(img, ox + x, oy + 9, C["outline"] if f == 0 else C["skin"])
        rect(img, ox + 4, oy + 3, ox + 11, oy + 4, C["light_gray"])  # a beard-ish hat brim
    # signpost 22
    ox, oy = cell(22)
    rect(img, ox + 7, oy + 8, ox + 8, oy + 15, C["wood_dark"])
    rect(img, ox + 2, oy + 2, ox + 13, oy + 9, C["wood"])
    rect(img, ox + 2, oy + 9, ox + 13, oy + 9, C["wood_dark"])
    for x in range(4, 12, 2):
        px(img, ox + x, oy + 4, C["wood_dark"])
        px(img, ox + x + 1, oy + 6, C["wood_dark"])
    # tree 32x32 at (0, 96): a dark canopy with lighter tufts and an outline
    ox, oy = 0, 96
    rect(img, ox + 13, oy + 20, ox + 18, oy + 31, C["wood_dark"])
    rect(img, ox + 13, oy + 20, ox + 14, oy + 31, C["wood"])
    for y in range(0, 25):
        for x in range(1, 31):
            d = (x - 15.5) ** 2 / 210 + (y - 12) ** 2 / 140
            if d <= 1:
                edge = d > 0.86
                col = C["outline"] if edge and y > 12 else C["grass_dark"]
                if not edge and (x * 5 + y * 3) % 11 == 0:
                    col = C["leaf"]
                if not edge and y < 9 and (x + y) % 9 == 0:
                    col = C["grass"]
                px(img, ox + x, oy + y, col)
    # portrait 32x32 at (64, 96)
    ox, oy = 64, 96
    for y in range(32):
        for x in range(32):
            if (x - 15.5) ** 2 / 196 + (y - 17) ** 2 / 225 <= 1:
                px(img, ox + x, oy + y, C["purple"])
    rect(img, ox + 9, oy + 14, ox + 22, oy + 25, C["skin"])
    for x in (11, 18):
        rect(img, ox + x, oy + 17, ox + x + 2, oy + 19, C["outline"])
    rect(img, ox + 6, oy + 8, ox + 25, oy + 11, C["light_gray"])
    rect(img, ox + 10, oy + 26, ox + 21, oy + 29, C["light_gray"])
    img.save(os.path.join(OUT, "sprites.png"))
    icon = img.crop((0, 0, 16, 16)).resize((256, 256), Image.NEAREST)
    icon.save(os.path.join(HERE, "..", "icon.ico"), sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])


# -------------------------------------------------------------- map

W, H = 44, 30
FIRST_GRASS, PATH, WATER, WALL = 0, 2, 3, 7


def build_map():
    rng = random.Random(11)
    ground = [[0] * W for _ in range(H)]
    walls = [[0] * W for _ in range(H)]
    for y in range(H):
        for x in range(W):
            ground[y][x] = 1 + (2 if rng.random() < 0.07 else 1 - 1)  # gid 1 grass, 2 flowered
    # gid = tile id + 1
    for y in range(H):
        for x in range(W):
            ground[y][x] = 2 if rng.random() < 0.08 else 1
    # a winding path from the start to the keep
    path = [(4, 24), (10, 24), (10, 18), (18, 18), (18, 12), (28, 12), (28, 6), (36, 6)]
    for (x0, y0), (x1, y1) in zip(path, path[1:]):
        for x in range(min(x0, x1), max(x0, x1) + 1):
            for y in range(min(y0, y1), max(y0, y1) + 1):
                for dx in (0, 1):
                    for dy in (0, 1):
                        if 0 <= x + dx < W and 0 <= y + dy < H:
                            ground[y + dy][x + dx] = PATH + 1
    # border walls
    for x in range(W):
        walls[0][x] = walls[H - 1][x] = WALL + 1
    for y in range(H):
        walls[y][0] = walls[y][W - 1] = WALL + 1
    # a pond
    for y in range(20, 27):
        for x in range(24, 34):
            if (x - 29) ** 2 / 25 + (y - 23.5) ** 2 / 10 <= 1:
                walls[y][x] = WATER + 1
    # the keep: a stone room at the end of the path with a door gap
    for x in range(32, 42):
        walls[2][x] = walls[9][x] = WALL + 1
    for y in range(2, 10):
        walls[y][32] = walls[y][41] = WALL + 1
    for y in (5, 6):
        walls[y][32] = 0
    for y in range(3, 9):
        for x in range(33, 41):
            ground[y][x] = PATH + 1
    # some broken wall pieces to walk around
    for (x, y) in ((14, 8), (15, 8), (16, 8), (14, 9), (22, 20), (22, 21), (23, 21)):
        walls[y][x] = WALL + 1
    return ground, walls


OBJECTS = [
    ("player", 4.5, 24.5, {}),
    ("npc", 7.5, 22.5, {"dialog": "elder"}),
    ("slime", 12.5, 20.5, {}), ("slime", 20.5, 16.5, {}), ("slime", 22.5, 10.5, {}),
    ("slime", 26.5, 15.5, {}), ("slime", 30.5, 9.5, {}), ("slime", 36.5, 4.5, {}),
    ("chest", 38.5, 5.5, {}),
    ("sign", 9.5, 26.5, {"text": "@sign.hello"}),
]
TREES = [(3, 3), (6, 5), (9, 3), (12, 6), (5, 10), (2, 14), (8, 13), (13, 12), (16, 3), (20, 5), (24, 3),
         (20, 9), (12, 15), (4, 18), (3, 27), (14, 22), (16, 26), (19, 22), (21, 25), (35, 13), (38, 15),
         (33, 18), (40, 20), (37, 24), (26, 27), (28, 16), (23, 14), (8, 8)]


def write_tmx():
    ground, walls = build_map()
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<map version="1.10" tiledversion="1.10.2" orientation="orthogonal" renderorder="right-down" '
             f'width="{W}" height="{H}" tilewidth="{T}" tileheight="{T}" infinite="0" '
             f'backgroundcolor="#3a7d44" nextlayerid="4" nextobjectid="100">',
             ' <properties>', '  <property name="name" value="@level.name"/>', ' </properties>',
             ' <tileset firstgid="1" source="tiles.tsx"/>']

    def layer(lid, name, grid, solid=False):
        out = [f' <layer id="{lid}" name="{name}" width="{W}" height="{H}">']
        if solid:
            out += ['  <properties>', '   <property name="solid" type="bool" value="true"/>', '  </properties>']
        out.append('  <data encoding="csv">')
        out.append(",\n".join(",".join(str(v) for v in row) for row in grid))
        out += ['  </data>', ' </layer>']
        return out

    lines += layer(1, "Ground", ground)
    lines += layer(2, "Walls", walls, solid=True)
    lines.append(' <objectgroup id="3" name="Entities">')
    oid = 1
    for kind, x, y, props in OBJECTS:
        lines.append(f'  <object id="{oid}" name="{kind}{oid}" type="{kind}" x="{x * T:g}" y="{y * T:g}">')
        if props:
            lines.append('   <properties>')
            for k, v in props.items():
                lines.append(f'    <property name="{k}" value="{v}"/>')
            lines.append('   </properties>')
        lines += ['   <point/>', '  </object>']
        oid += 1
    for (x, y) in TREES:
        lines += [f'  <object id="{oid}" name="tree{oid}" type="tree" x="{x * T + 8}" y="{y * T + 16}">',
                  '   <point/>', '  </object>']
        oid += 1
    lines += [' </objectgroup>', '</map>']
    with open(os.path.join(OUT, "map.tmx"), "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    tsx = ['<?xml version="1.0" encoding="UTF-8"?>',
           f'<tileset version="1.10" tiledversion="1.10.2" name="tiles" tilewidth="{T}" tileheight="{T}" '
           f'tilecount="16" columns="8">',
           f' <image source="tiles.png" width="{8 * T}" height="{2 * T}"/>',
           ' <tile id="3">', '  <animation>']
    tsx += [f'   <frame tileid="{3 + f}" duration="220"/>' for f in range(4)]
    tsx += ['  </animation>', ' </tile>', '</tileset>']
    with open(os.path.join(OUT, "tiles.tsx"), "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(tsx) + "\n")


def make_sounds():
    snd = os.path.join(OUT, "sounds")
    os.makedirs(snd, exist_ok=True)
    random.seed(9)
    tone, write = pa.tone, pa.write_wav
    write(os.path.join(snd, "swing.wav"), tone(lambda t: 500 - 900 * t, 0.12, "noise", 0.2))
    write(os.path.join(snd, "hit.wav"), tone(lambda t: 240 - 500 * t, 0.14, "square", 0.3))
    write(os.path.join(snd, "hurt.wav"), tone(lambda t: 200 - 300 * t, 0.3, "square", 0.3))
    write(os.path.join(snd, "dash.wav"), tone(lambda t: 200 + 800 * t, 0.12, "noise", 0.15))
    write(os.path.join(snd, "blip.wav"), tone(lambda t: 700, 0.03, "square", 0.12))
    write(os.path.join(snd, "select.wav"), tone(lambda t: 880, 0.05, "square", 0.15))
    win = []
    for f in (523, 659, 784, 1047, 784, 1047):
        win += tone(lambda t, f=f: f, 0.12, "square", 0.2)
    write(os.path.join(snd, "win.wav"), win)
    mus = os.path.join(OUT, "music")
    os.makedirs(mus, exist_ok=True)
    bass, lead = [], []
    for root in (43, 40, 41, 38):
        bass += [root, None, root + 7, None, root + 12, None, root + 7, None]
    melody = [67, None, 71, None, 74, None, 71, None, 64, None, 67, None, 71, None, 67, None,
              65, None, 69, None, 72, None, 69, None, 62, 64, 65, None, 67, None, None, None]
    pa.make_song(os.path.join(mus, "forest.wav"), 96, bass, melody)


def main():
    os.makedirs(OUT, exist_ok=True)
    make_tiles()
    make_sprites()
    write_tmx()
    make_sounds()
    print("ok")


if __name__ == "__main__":
    main()

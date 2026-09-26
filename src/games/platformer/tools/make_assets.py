"""Builds the art, sound and Tiled levels of the platformer sample.

Run from anywhere: python src/games/platformer/tools/make_assets.py
Needs Pillow. Everything it writes lands in src/games/platformer/assets and is
committed, so building the game does not need Python.
"""

import math
import os
import random
import struct
import wave

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))
T = 16

# A small palette, so tiles and sprites sit together.
C = {
    "outline": (34, 32, 52),
    "grass": (106, 190, 48),
    "grass_dark": (55, 148, 110),
    "dirt": (143, 86, 59),
    "dirt_dark": (102, 57, 49),
    "dirt_light": (180, 118, 80),
    "stone": (132, 126, 135),
    "stone_dark": (89, 86, 82),
    "stone_light": (155, 173, 183),
    "wood": (184, 111, 80),
    "wood_dark": (116, 63, 57),
    "water": (91, 110, 225),
    "water_light": (99, 155, 255),
    "water_dark": (48, 96, 130),
    "white": (255, 255, 255),
    "yellow": (251, 242, 54),
    "gold": (223, 113, 38),
    "red": (217, 87, 99),
    "red_dark": (172, 50, 50),
    "pink": (215, 123, 186),
    "purple": (118, 66, 138),
    "violet": (153, 229, 255),
    "leaf": (153, 229, 80),
    "skin": (238, 195, 154),
    "brown": (102, 57, 49),
    "gray": (105, 106, 106),
    "light_gray": (203, 219, 252),
}


def ensure(path):
    os.makedirs(path, exist_ok=True)


def px(img, x, y, color):
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), color + (255,) if len(color) == 3 else color)


def rect(img, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px(img, x, y, color)


# ---------------------------------------------------------------- tiles

def dirt_fill(img, ox, oy, rng, top_limit=None):
    """Fills a 16x16 cell with dirt; `top_limit(x)` is the first dirt row."""
    for y in range(T):
        for x in range(T):
            if top_limit is not None and y < top_limit(x):
                continue
            c = C["dirt"]
            if rng.random() < 0.08:
                c = C["dirt_dark"]
            elif rng.random() < 0.05:
                c = C["dirt_light"]
            px(img, ox + x, oy + y, c)


def grass_edge(img, ox, oy, surface):
    """Grass along a surface: surface(x) is the top row of the ground at x."""
    for x in range(T):
        s = surface(x)
        for d in range(3):
            y = s + d
            if 0 <= y < T:
                px(img, ox + x, oy + y, C["grass"] if d < 2 else C["grass_dark"])
        if 0 <= s - 1 < T and x % 5 == 2:
            px(img, ox + x, oy + s - 1, C["grass"])  # a blade sticking up


def make_tiles():
    rng = random.Random(7)
    cols, rows = 8, 4
    img = Image.new("RGBA", (cols * T, rows * T), (0, 0, 0, 0))

    def cell(i):
        return (i % cols) * T, (i // cols) * T

    # 0 grass top, 1 dirt
    ox, oy = cell(0)
    dirt_fill(img, ox, oy, rng)
    grass_edge(img, ox, oy, lambda x: 0)
    ox, oy = cell(1)
    dirt_fill(img, ox, oy, rng)
    # 2 stone brick
    ox, oy = cell(2)
    rect(img, ox, oy, ox + 15, oy + 15, C["stone"])
    for y in (0, 8):
        rect(img, ox, oy + y, ox + 15, oy + y, C["stone_dark"])
    for (x, y0) in ((0, 0), (8, 8)):
        rect(img, ox + x, oy + y0, ox + x, oy + y0 + 7, C["stone_dark"])
    for (x, y) in ((2, 2), (11, 10), (5, 12)):
        px(img, ox + x, oy + y, C["stone_light"])
    # 3 one-way plank
    ox, oy = cell(3)
    rect(img, ox, oy, ox + 15, oy + 4, C["wood"])
    rect(img, ox, oy + 5, ox + 15, oy + 5, C["wood_dark"])
    for x in (3, 11):
        rect(img, ox + x, oy + 6, ox + x + 1, oy + 9, C["wood_dark"])
    rect(img, ox, oy, ox + 15, oy, C["dirt_light"])

    # Slopes: surface(x) is the first ground row at column x.
    slopes = {
        4: lambda x: 15 - x,                   # slope_r (45, high right)
        5: lambda x: x,                        # slope_l
        6: lambda x: 15 - x // 2,              # slope_r_low
        7: lambda x: 7 - x // 2,               # slope_r_high
        8: lambda x: x // 2,                   # slope_l_high
        9: lambda x: 8 + x // 2,               # slope_l_low
    }
    for i, surf in slopes.items():
        ox, oy = cell(i)
        dirt_fill(img, ox, oy, rng, surf)
        grass_edge(img, ox, oy, surf)

    # 10..13 water, animated: a wave line that slides.
    for f in range(4):
        ox, oy = cell(10 + f)
        rect(img, ox, oy + 3, ox + 15, oy + 15, C["water"])
        for x in range(T):
            y = 3 + int(round(math.sin((x + f * 4) / 16 * 2 * math.pi)))
            px(img, ox + x, oy + y, C["water_light"])
            px(img, ox + x, oy + y + 1, C["water_light"])
        for (x, y) in ((4 + f * 3) % 16, 9), ((12 + f * 2) % 16, 12):
            px(img, ox + x, oy + y, C["water_dark"])
    # 14 bush, 15 flower (deco)
    ox, oy = cell(14)
    for y in range(T):
        for x in range(T):
            if (x - 8) ** 2 / 64 + (y - 13) ** 2 / 40 <= 1 and y > 6:
                px(img, ox + x, oy + y, C["grass_dark"] if (x + y) % 5 == 0 else C["grass"])
    ox, oy = cell(15)
    rect(img, ox + 7, oy + 9, ox + 8, oy + 15, C["grass_dark"])
    for (x, y) in ((7, 6), (6, 7), (8, 7), (7, 8), (9, 6)):
        px(img, ox + x, oy + y, C["pink"])
    px(img, ox + 7, oy + 7, C["yellow"])
    # 16..17 torch flame (animated deco)
    for f in range(2):
        ox, oy = cell(16 + f)
        rect(img, ox + 7, oy + 9, ox + 8, oy + 15, C["wood_dark"])
        h = 5 + f
        for y in range(h):
            w = max(1, 3 - abs(y - 2))
            for x in range(8 - w, 8 + w - 1 + 1):
                px(img, ox + x, oy + 9 - y - 1, C["yellow"] if y < 2 else C["gold"])
    img.save(os.path.join(OUT, "tiles.png"))
    return cols, rows


# ---------------------------------------------------------------- sprites

def draw_player(img, ox, oy, frame):
    """A green sprout: frame 0-1 idle, 2-5 run, 6 jump, 7 fall, 8 wall, 9 hurt."""
    bob = 1 if frame == 1 else 0
    body_top = 5 + bob
    squash = 1 if frame == 7 else 0
    color = C["leaf"] if frame != 9 else C["red"]
    # leaf on the head
    for (x, y) in ((8, 1), (9, 1), (9, 2), (10, 2), (8, 3), (7, 2)):
        px(img, ox + x, oy + y + bob, C["grass_dark"])
    px(img, ox + 8, oy + 4 + bob, C["grass_dark"])
    # body
    for y in range(body_top, 14 - squash):
        for x in range(4, 12):
            edge = y in (body_top, 13 - squash) and x in (4, 11)
            if not edge:
                px(img, ox + x, oy + y, color)
    rect(img, ox + 4, oy + body_top, ox + 11, oy + body_top, C["grass_dark"])
    # eyes
    ey = body_top + 3
    for x in (7, 10):
        px(img, ox + x, oy + ey, C["outline"])
    px(img, ox + 7, oy + ey - 1, C["white"])
    # legs
    legs = {2: ((5, 0), (10, 1)), 3: ((6, 1), (9, 0)), 4: ((5, 1), (10, 0)), 5: ((6, 0), (9, 1))}
    if frame in legs:
        for (x, lift) in legs[frame]:
            rect(img, ox + x, oy + 14 - lift, ox + x + 1, oy + 15 - lift, C["outline"])
    elif frame == 6:
        rect(img, ox + 5, oy + 13, ox + 6, oy + 14, C["outline"])
        rect(img, ox + 9, oy + 13, ox + 10, oy + 14, C["outline"])
    else:
        rect(img, ox + 5, oy + 14, ox + 6, oy + 15, C["outline"])
        rect(img, ox + 9, oy + 14, ox + 10, oy + 15, C["outline"])
    if frame == 8:  # wall slide: a hand against the wall
        rect(img, ox + 12, oy + 8, ox + 13, oy + 9, C["grass_dark"])


def make_sprites():
    cols, rows = 8, 8
    img = Image.new("RGBA", (cols * T, rows * T), (0, 0, 0, 0))

    def cell(i):
        return (i % cols) * T, (i // cols) * T

    for f in range(10):
        draw_player(img, *cell(f), f)
    # coin: 16..19 spinning
    for f, w in enumerate((5, 3, 1, 3)):
        ox, oy = cell(16 + f)
        for y in range(3, 13):
            for x in range(8 - w, 8 + w):
                dy = (y - 7.5) / 5
                dx = (x - 7.5) / max(w, 1)
                if dx * dx + dy * dy <= 1.05:
                    px(img, ox + x, oy + y, C["yellow"] if x < 8 else C["gold"])
    # walker: 24..25 walk, 26 squashed
    for f in range(3):
        ox, oy = cell(24 + f)
        top = 5 if f < 2 else 11
        for y in range(top, 15):
            for x in range(3, 13):
                if (x - 7.5) ** 2 / 25 + (y - 14) ** 2 / ((15 - top) ** 2) <= 1:
                    px(img, ox + x, oy + y, C["red"])
        if f < 2:
            for x in (6, 9):
                px(img, ox + x, oy + 9, C["white"])
                px(img, ox + x, oy + 10, C["outline"])
            feet = (4, 10) if f == 0 else (5, 9)
            for x in feet:
                rect(img, ox + x, oy + 15, ox + x + 1, oy + 15, C["red_dark"])
    # checkpoint: 32 off, 33..34 waving
    for f in range(3):
        ox, oy = cell(32 + f)
        rect(img, ox + 3, oy + 1, ox + 3, oy + 15, C["stone_light"])
        color = C["gray"] if f == 0 else C["grass"]
        for y in range(2, 8):
            wave_off = 0 if f == 0 else (1 if (y + f) % 3 == 0 else 0)
            rect(img, ox + 4, oy + y, ox + 11 - wave_off - abs(y - 5) // 2, oy + y, color)
    # exit portal: 35..36
    for f in range(2):
        ox, oy = cell(35 + f)
        for y in range(T):
            for x in range(T):
                d = math.hypot(x - 7.5, (y - 8) * 0.8)
                if d < 7:
                    ring = int(d + f * 1.5) % 3
                    px(img, ox + x, oy + y, (C["purple"], C["violet"], C["pink"])[ring])
    # spikes: 40
    ox, oy = cell(40)
    for s in range(4):
        for y in range(8):
            for x in range(s * 4, s * 4 + 4):
                if abs(x - (s * 4 + 1.5)) <= y / 4:
                    px(img, ox + x, oy + 8 + y, C["light_gray"] if x <= s * 4 + 1 else C["gray"])
    # sign: 41
    ox, oy = cell(41)
    rect(img, ox + 7, oy + 9, ox + 8, oy + 15, C["wood_dark"])
    rect(img, ox + 2, oy + 3, ox + 13, oy + 9, C["wood"])
    rect(img, ox + 2, oy + 9, ox + 13, oy + 9, C["wood_dark"])
    for x in range(4, 12, 2):
        px(img, ox + x, oy + 5, C["wood_dark"])
        px(img, ox + x + 1, oy + 7, C["wood_dark"])
    # owl npc: 42..43 (blink)
    for f in range(2):
        ox, oy = cell(42 + f)
        for y in range(3, 15):
            for x in range(3, 13):
                if (x - 7.5) ** 2 / 25 + (y - 9) ** 2 / 36 <= 1:
                    px(img, ox + x, oy + y, C["brown"])
        rect(img, ox + 5, oy + 10, ox + 10, oy + 13, C["skin"])
        for x in (5, 9):
            rect(img, ox + x, oy + 6, ox + x + 1, oy + 7, C["white"] if f == 0 else C["brown"])
            if f == 0:
                px(img, ox + x + 1, oy + 7, C["outline"])
        px(img, ox + 7, oy + 8, C["gold"])
        px(img, ox + 8, oy + 8, C["gold"])
        for x in (4, 11):
            px(img, ox + x, oy + 3, C["brown"])
    # moving platform: 48x8 at row 6 (y = 96)
    rect(img, 0, 96, 47, 101, C["stone"])
    rect(img, 0, 96, 47, 96, C["stone_light"])
    rect(img, 0, 102, 47, 103, C["stone_dark"])
    for x in (0, 15, 31, 47):
        rect(img, x, 97, x, 101, C["stone_dark"])
    # owl portrait: 32x32 at (64, 96)
    ox, oy = 64, 96
    for y in range(32):
        for x in range(32):
            if (x - 15.5) ** 2 / 196 + (y - 17) ** 2 / 225 <= 1:
                px(img, ox + x, oy + y, C["brown"])
    rect(img, ox + 9, oy + 20, ox + 22, oy + 28, C["skin"])
    for x in (9, 18):
        rect(img, ox + x, oy + 11, ox + x + 4, oy + 15, C["white"])
        rect(img, ox + x + 2, oy + 13, ox + x + 3, oy + 14, C["outline"])
    rect(img, ox + 15, oy + 16, ox + 16, oy + 18, C["gold"])
    for (x, y) in ((7, 3), (8, 4), (24, 3), (23, 4)):
        px(img, ox + x, oy + y, C["brown"])
    img.save(os.path.join(OUT, "sprites.png"))
    # The window icon, from the idle frame.
    icon = img.crop((0, 0, 16, 16)).resize((256, 256), Image.NEAREST)
    icon.save(os.path.join(OUT, "..", "icon.ico"), sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
    icon.resize((64, 64), Image.NEAREST).save(os.path.join(OUT, "icon.png"))


# ---------------------------------------------------------------- tileset (Tiled .tsx)

TILE_SHAPES = {3: "one_way", 4: "slope_r", 5: "slope_l", 6: "slope_r_low", 7: "slope_r_high",
               8: "slope_l_high", 9: "slope_l_low"}
DECO_NONE = [10, 11, 12, 13, 14, 15, 16, 17]


def make_tsx(cols, rows):
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<tileset version="1.10" tiledversion="1.10.2" name="tiles" tilewidth="{T}" '
             f'tileheight="{T}" tilecount="{cols * rows}" columns="{cols}">',
             f' <image source="tiles.png" width="{cols * T}" height="{rows * T}"/>']
    for tid in sorted(set(TILE_SHAPES) | set(DECO_NONE)):
        lines.append(f' <tile id="{tid}">')
        shape = TILE_SHAPES.get(tid, "none")
        lines.append('  <properties>')
        lines.append(f'   <property name="collision" value="{shape}"/>')
        lines.append('  </properties>')
        if tid == 10:
            lines.append('  <animation>')
            for f in range(4):
                lines.append(f'   <frame tileid="{10 + f}" duration="180"/>')
            lines.append('  </animation>')
        if tid == 16:
            lines.append('  <animation>')
            lines.append('   <frame tileid="16" duration="120"/>')
            lines.append('   <frame tileid="17" duration="120"/>')
            lines.append('  </animation>')
        lines.append(' </tile>')
    lines.append('</tileset>')
    with open(os.path.join(OUT, "tiles.tsx"), "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


# ---------------------------------------------------------------- levels

class Level:
    """A level drawn in code: a ground layer, a deco layer and objects."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.ground = [[None] * w for _ in range(h)]  # None, "#", "=", "-", slope ids
        self.deco = [[0] * w for _ in range(h)]       # tile id + 1, 0 empty
        self.objects = []
        self.props = {}

    def fill(self, x0, y0, x1, y1, kind="#"):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.ground[y][x] = kind

    def clear(self, x0, y0, x1, y1):
        self.fill(x0, y0, x1, y1, None)

    def put(self, x, y, kind):
        self.ground[y][x] = kind

    def water(self, x0, x1, y):
        for x in range(x0, x1 + 1):
            self.deco[y][x] = 10 + 1
            for yy in range(y + 1, self.h):
                if self.ground[yy][x] is None:
                    self.deco[yy][x] = 10 + 1  # still water below the surface

    def decorate(self, x, y, tid):
        self.deco[y][x] = tid + 1

    def obj(self, kind, x, y, **props):
        """A point object at the bottom-centre of tile (x, y) unless px given."""
        self.objects.append({"type": kind, "x": x, "y": y, "props": props})

    def tile_ids(self):
        ids = []
        for y in range(self.h):
            row = []
            for x in range(self.w):
                k = self.ground[y][x]
                if k is None:
                    row.append(0)
                elif k == "#":
                    above = self.ground[y - 1][x] if y > 0 else None
                    row.append((0 if above in (None, "-") else 1) + 1)
                elif k == "=":
                    row.append(2 + 1)
                elif k == "-":
                    row.append(3 + 1)
                else:
                    row.append(k + 1)
            ids.append(row)
        return ids


def tmx_props(props, indent):
    if not props:
        return []
    out = [indent + "<properties>"]
    for k, v in props.items():
        if isinstance(v, bool):
            out.append(f'{indent} <property name="{k}" type="bool" value="{str(v).lower()}"/>')
        elif isinstance(v, int):
            out.append(f'{indent} <property name="{k}" type="int" value="{v}"/>')
        elif isinstance(v, float):
            out.append(f'{indent} <property name="{k}" type="float" value="{v}"/>')
        else:
            out.append(f'{indent} <property name="{k}" value="{v}"/>')
    out.append(indent + "</properties>")
    return out


def write_tmx(level, name):
    w, h = level.w, level.h
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             f'<map version="1.10" tiledversion="1.10.2" orientation="orthogonal" renderorder="right-down" '
             f'width="{w}" height="{h}" tilewidth="{T}" tileheight="{T}" infinite="0" '
             f'backgroundcolor="#5fcde4" nextlayerid="4" nextobjectid="{len(level.objects) + 1}">']
    lines += tmx_props(level.props, " ")
    lines.append(' <tileset firstgid="1" source="tiles.tsx"/>')

    def layer(lid, lname, grid, props=None):
        out = [f' <layer id="{lid}" name="{lname}" width="{w}" height="{h}">']
        out += tmx_props(props or {}, "  ")
        out.append('  <data encoding="csv">')
        rows = [",".join(str(v) for v in row) for row in grid]
        out.append(",\n".join(rows))
        out.append('  </data>')
        out.append(' </layer>')
        return out

    lines += layer(1, "Deco", level.deco)
    lines += layer(2, "Ground", level.tile_ids(), {"solid": True})
    lines.append(' <objectgroup id="3" name="Entities">')
    for i, o in enumerate(level.objects, start=1):
        x = o["x"] * T + T / 2
        y = o["y"] * T + T
        if "path" in o["props"]:
            path = o["props"].pop("path")
            pts = " ".join(f"{(px_ - o['x']) * T},{(py_ - o['y']) * T}" for px_, py_ in path)
            lines.append(f'  <object id="{i}" name="{o["type"]}{i}" type="{o["type"]}" x="{x:g}" y="{o["y"] * T + T / 2:g}">')
            lines += tmx_props(o["props"], "   ")
            lines.append(f'   <polyline points="{pts}"/>')
            lines.append('  </object>')
            continue
        size = o["props"].pop("size", None)
        if size is not None:
            ww, hh = size
            lines.append(f'  <object id="{i}" name="{o["type"]}{i}" type="{o["type"]}" '
                         f'x="{o["x"] * T:g}" y="{o["y"] * T:g}" width="{ww * T:g}" height="{hh * T:g}">')
            lines += tmx_props(o["props"], "   ")
            lines.append('  </object>')
            continue
        lines.append(f'  <object id="{i}" name="{o["type"]}{i}" type="{o["type"]}" x="{x:g}" y="{y:g}">')
        lines += tmx_props(o["props"], "   ")
        lines.append('   <point/>')
        lines.append('  </object>')
    lines.append(' </objectgroup>')
    lines.append('</map>')
    with open(os.path.join(OUT, name), "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    # A text preview, for eyeballing the layout.
    preview = []
    for y in range(h):
        row = ""
        for x in range(w):
            k = level.ground[y][x]
            ch = {None: " ", "#": "#", "=": "=", "-": "-", 4: "/", 5: "\\", 6: "a", 7: "b", 8: "c", 9: "d"}[k]
            if ch == " " and level.deco[y][x] == 11:
                ch = "~"
            row += ch
        preview.append(row)
    for o in level.objects:
        if 0 <= o["y"] < h and 0 <= o["x"] < w:
            r = preview[o["y"]]
            preview[o["y"]] = r[:o["x"]] + o["type"][0].upper() + r[o["x"] + 1:]
    return "\n".join(preview)


def level1():
    L = Level(96, 20)
    L.props = {"name": "@level.1", "music": "level", "next": "level2.tmx"}
    G = 16  # ground top row
    L.fill(0, 0, 1, 19, "=")            # left wall
    L.fill(94, 0, 95, 19, "=")          # right wall
    L.fill(2, G, 36, 19)
    # start area
    L.obj("player", 4, G - 1)
    L.obj("npc", 8, G - 1, dialog="owl")
    L.obj("sign", 12, G - 1, text="@sign.move")
    L.decorate(6, G - 1, 15)
    L.decorate(14, G - 1, 14)
    # gentle hill (22.5 degrees)
    L.put(16, G - 1, 6)
    L.put(17, G - 1, 7)
    L.fill(18, G - 1, 22, G - 1)
    L.put(23, G - 1, 8)
    L.put(24, G - 1, 9)
    for x in (19, 21):
        L.obj("coin", x, G - 3)
    # 45 degree hill
    L.put(27, G - 1, 4)
    L.fill(28, G - 1, 33, G - 1)
    L.put(28, G - 2, 4)
    L.fill(29, G - 2, 32, G - 2)
    L.put(33, G - 2, 5)
    L.put(34, G - 1, 5)
    L.obj("walker", 30, G - 3)
    L.decorate(31, G - 3, 16)
    # water pit with a one-way bridge and a moving platform
    L.fill(37, 18, 52, 19)
    L.water(37, 52, 17)
    L.obj("hazard", 37, 17, size=(16, 3))
    for x in (38, 39, 40):
        L.put(x, 14, "-")
    L.obj("coin", 39, 12)
    L.obj("platform", 42, 14, path=[(42, 14), (49, 14)], speed=28.0, wait=0.5)
    L.obj("coin", 46, 12)
    L.fill(53, G, 70, 19)
    L.obj("sign", 55, G - 1, text="@sign.drop")
    L.obj("checkpoint", 57, G - 1)
    L.obj("walker", 64, G - 1)
    L.obj("coin", 60, G - 2)
    L.obj("coin", 62, G - 2)
    # a stone tower climbed by one-way ledges and wall jumps
    L.fill(66, 8, 67, G - 1, "=")
    for (x0, y) in ((61, 13), (62, 10)):
        for x in range(x0, x0 + 3):
            L.put(x, y, "-")
    L.obj("sign", 59, G - 1, text="@sign.wall")
    L.obj("coin", 63, 8)
    L.fill(68, 8, 75, 8, "=")
    L.obj("coin", 71, 6)
    L.obj("coin", 73, 6)
    # down a 45 degree slope back to the ground
    L.fill(76, 8, 76, 19)
    for i in range(7):
        x = 77 + i
        y = 9 + i
        if y > G - 1:
            break
        L.put(x, y, 5)
        L.fill(x, y + 1, x, 19)
    L.fill(76, 8, 76, 8)
    L.fill(84, G, 93, 19)
    # spikes with a one-way shelf above
    L.obj("spikes", 86, G - 1)
    L.obj("spikes", 87, G - 1)
    for x in range(85, 89):
        L.put(x, 13, "-")
    L.obj("coin", 86, 11)
    L.obj("exit", 91, G - 1)
    L.decorate(90, G - 1, 16)
    return L


def level2():
    L = Level(40, 44)
    L.props = {"name": "@level.2", "music": "level", "next": ""}
    L.fill(0, 0, 1, 43, "=")
    L.fill(38, 0, 39, 43, "=")
    L.fill(2, 41, 37, 43)
    L.obj("player", 4, 40)
    L.obj("sign", 8, 40, text="@sign.climb")
    L.obj("checkpoint", 11, 40)
    # A wall-jump shaft between the left border and a stone wall. Open at the
    # bottom; its top is a ledge that leads to the lift.
    L.fill(6, 21, 7, 37, "=")
    L.fill(6, 20, 12, 20, "=")
    L.obj("coin", 4, 30)
    L.obj("coin", 4, 24)
    L.obj("platform", 14, 21, path=[(14, 21), (14, 12)], speed=24.0, wait=0.8)
    # Or the long way: one-way ledges zig-zagging up on the right.
    for i, y in enumerate(range(38, 16, -3)):
        x0 = 19 if i % 2 == 0 else 25
        for x in range(x0, x0 + 5):
            L.put(x, y, "-")
        L.obj("coin", x0 + 2, y - 2)
    for x in range(31, 36):
        L.put(x, 14, "-")
    L.obj("spikes", 15, 40)
    L.obj("spikes", 16, 40)
    L.obj("walker", 32, 40)
    # The top floor: solid on the left, one-way where the ledges come up.
    L.fill(16, 11, 30, 11, "=")
    for x in range(31, 38):
        L.put(x, 11, "-")
    L.obj("checkpoint", 18, 10)
    L.obj("walker", 26, 10)
    L.obj("coin", 22, 8)
    L.obj("coin", 28, 8)
    L.obj("exit", 36, 10)
    L.decorate(34, 10, 16)
    return L


# ---------------------------------------------------------------- sound

RATE = 22050


def write_wav(path, samples, rate=RATE):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 30000)) for s in samples))


def tone(freq_fn, seconds, wave_fn="square", vol=0.5, decay=True):
    out = []
    phase = 0.0
    n = int(seconds * RATE)
    for i in range(n):
        t = i / RATE
        f = freq_fn(t)
        phase += f / RATE
        p = phase % 1.0
        if wave_fn == "square":
            s = 1.0 if p < 0.5 else -1.0
        elif wave_fn == "tri":
            s = 4 * abs(p - 0.5) - 1
        elif wave_fn == "noise":
            s = random.uniform(-1, 1)
        else:
            s = math.sin(2 * math.pi * p)
        env = (1 - i / n) ** 2 if decay else 1.0
        attack = min(1.0, i / 60)
        out.append(s * vol * env * attack)
    return out


def make_sounds():
    snd = os.path.join(OUT, "sounds")
    ensure(snd)
    random.seed(3)
    write_wav(os.path.join(snd, "jump.wav"), tone(lambda t: 300 + 900 * t, 0.16, "square", 0.25))
    write_wav(os.path.join(snd, "coin.wav"),
              tone(lambda t: 988 if t < 0.06 else 1319, 0.22, "square", 0.22))
    write_wav(os.path.join(snd, "stomp.wav"), tone(lambda t: 220 - 400 * t, 0.15, "square", 0.3))
    write_wav(os.path.join(snd, "hurt.wav"),
              [a * 0.6 + b * 0.4 for a, b in zip(tone(lambda t: 160 - 200 * t, 0.3, "square", 0.3),
                                                  tone(lambda t: 0, 0.3, "noise", 0.3))])
    write_wav(os.path.join(snd, "land.wav"), tone(lambda t: 90, 0.07, "noise", 0.18))
    arp = []
    for f in (523, 659, 784, 1047):
        arp += tone(lambda t, f=f: f, 0.08, "square", 0.2)
    write_wav(os.path.join(snd, "checkpoint.wav"), arp)
    write_wav(os.path.join(snd, "blip.wav"), tone(lambda t: 660, 0.03, "square", 0.12))
    write_wav(os.path.join(snd, "select.wav"), tone(lambda t: 880, 0.05, "square", 0.15))
    win = []
    for f in (523, 659, 784, 659, 784, 1047):
        win += tone(lambda t, f=f: f, 0.12, "square", 0.2)
    write_wav(os.path.join(snd, "win.wav"), win)


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def make_song(path, bpm, bass, lead, bars_repeat=2):
    """A short chiptune loop: bass and lead as MIDI note lists, one per eighth."""
    eighth = 60.0 / bpm / 2
    n_steps = len(bass)
    total = int(n_steps * eighth * RATE)
    mix = [0.0] * total
    for voice, notes, form, vol in ((0, bass, "tri", 0.35), (1, lead, "square", 0.12)):
        for i, n in enumerate(notes):
            if n is None:
                continue
            start = int(i * eighth * RATE)
            seg = tone(lambda t, n=n: note_hz(n), eighth * 0.95, form, vol, decay=voice == 1)
            for j, s in enumerate(seg):
                if start + j < total:
                    mix[start + j] += s
    write_wav(path, mix * bars_repeat)


def make_music():
    mus = os.path.join(OUT, "music")
    ensure(mus)
    # Title: slow and calm, A minor.
    bass = []
    lead = []
    for root in (45, 41, 43, 40):
        bass += [root, None, root + 7, None, root + 12, None, root + 7, None]
    for chord in ((69, 72, 76), (65, 69, 72), (67, 71, 74), (64, 68, 71)):
        for k in range(8):
            lead.append(chord[k % 3] + (12 if k >= 6 else 0) if k % 2 == 0 else None)
    make_song(os.path.join(mus, "title.wav"), 90, bass, lead)
    # Level: bouncy, C major.
    bass = []
    lead = []
    for root in (48, 45, 41, 43):
        bass += [root, root + 12, root, root + 12, root, root + 12, root + 7, root + 12]
    melody = [72, 74, 76, None, 79, 76, 74, None, 72, 69, 72, None, 74, 76, 74, None,
              69, 72, 74, None, 77, 76, 74, 72, 71, 74, 79, None, 77, 76, 74, 71]
    lead = melody
    make_song(os.path.join(mus, "level.wav"), 132, bass, lead)


def main():
    ensure(OUT)
    cols, rows = make_tiles()
    make_tsx(cols, rows)
    make_sprites()
    for lv, name in ((level1(), "level1.tmx"), (level2(), "level2.tmx")):
        print(name)
        print(write_tmx(lv, name))
    make_sounds()
    make_music()


if __name__ == "__main__":
    main()

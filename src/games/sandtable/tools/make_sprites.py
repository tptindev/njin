"""Builds assets/sprites.png, the pixel art of the sand table game.

Run from anywhere: python src/games/sandtable/tools/make_sprites.py
Needs Pillow. The PNG is committed, so building the game does not need Python.

Layout (sprites.h in the game reads the same numbers):
- Soldiers at (0, 0): 8x8 cells, facing right. Columns are the frames idle,
  walk 1, walk 2, attack 1, attack 2, dead. Rows are arm * 3 + variant, the
  variant being player, enemy, or a white silhouette for the hit flash.
- Chips at (0, 168): 25x25 cells, the chip centred. Columns are
  tier * 2 + side (player, enemy), rows are arms.
- Arm symbols at (0, 294): 5x5 in white, one every 6 pixels, in arm order.
  The game tints them (the flags over the blocks in battle).
"""

import math
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets", "sprites.png"))

CELL = 8
FRAMES = 6
CHIP_CELL = 25
CHIP_Y = 168
ARMS = 7
SYMBOL_Y = CHIP_Y + 25 * ARMS
TIERS = 7

SIDES = [
    {"c": (205, 52, 44), "d": (128, 28, 24), "l": (245, 120, 96)},   # player
    {"c": (40, 112, 172), "d": (22, 60, 104), "l": (120, 178, 228)},  # enemy
]
COMMON = {
    "o": (38, 28, 24),     # outline, boots
    "s": (236, 196, 156),  # skin
    "m": (206, 210, 218),  # metal
    "n": (122, 126, 138),  # dark metal, helmets
    "w": (150, 104, 60),   # wood
    "h": (150, 98, 56),    # horse
    "H": (96, 60, 34),     # horse, dark
    "k": (58, 58, 66),     # gun
    "f": (255, 214, 96),   # muzzle fire
    "g": (150, 146, 150),  # elephant
    "G": (98, 94, 102),    # elephant, shade and legs
    "i": (244, 238, 222),  # tusk
    "x": (120, 172, 204),  # water spray
}

# --- Soldiers: 8x8, facing right. Frames: idle, walk1, walk2, atk1, atk2, dead.

LEGS_IDLE = ["..c..c..", "..o..o.."]
LEGS_WALK1 = [".c...c..", ".o....o."]
LEGS_WALK2 = ["...cc...", "...oo..."]


def humanoid(top, legs):
    return top + legs


INF_TOP = [
    "...nn...",
    "..nnnn..",
    "...ss...",
    "..cccww.",
    "..cccww.",
    "..dddww.",
]
INF_ATK1 = [
    "...nn.m.",
    "..nnnnm.",
    "...ss.m.",
    "..cccsw.",
    "..cccww.",
    "..dddww.",
]
INF_ATK2 = [
    "...nn...",
    "..nnnn..",
    "...ss...",
    "..cccsmm",
    "..ccww..",
    "..ddww..",
]
INFANTRY = [
    humanoid(INF_TOP, LEGS_IDLE),
    humanoid(INF_TOP, LEGS_WALK1),
    humanoid(INF_TOP, LEGS_WALK2),
    humanoid(INF_ATK1, LEGS_IDLE),
    humanoid(INF_ATK2, LEGS_WALK2),
    ["........", "........", "........", "........", "......ww", ".nsccddw", "..ccddo.", "........"],
]

SPEAR_TOP = [
    "......m.",
    "...nn.w.",
    "...ss.w.",
    "..cccsw.",
    "..ccc.w.",
    "..ddd.w.",
]
SPEAR_ATK = [
    "........",
    "...nn...",
    "...ss...",
    "..cccswm",
    "..ccc...",
    "..ddd...",
]
SPEAR_LUNGE = [
    "........",
    "....nn..",
    "....ss..",
    "...cccsm",
    "..wccc..",
    "...ddd..",
]
SPEARMAN = [
    humanoid(SPEAR_TOP, ["..c..cw.", "..o..o.."]),
    humanoid(SPEAR_TOP, [".c...cw.", ".o....o."]),
    humanoid(SPEAR_TOP, ["...cc.w.", "...oo..."]),
    humanoid(SPEAR_ATK, LEGS_IDLE),
    humanoid(SPEAR_LUNGE, [".c...c..", ".o....o."]),
    ["........", "........", "........", "........", "........", ".nsccddw", "..ccddo.", "wwwwwwm."],
]

ARCHER_TOP = [
    "...dd...",
    "..dss.w.",
    "..cccs.w",
    "..ccc..w",
    "..ccc..w",
    "..dddsw.",
]
ARCHER_DRAW = [
    "...dd...",
    "..dss.w.",
    "..cccso.",
    "..ccsnnw",
    "..ccc.ow",
    "..dddsw.",
]
ARCHER_LOOSE = [
    "...dd...",
    "..dss.w.",
    "..cccsow",
    "..ccc.ow",
    "..ccc.ow",
    "..ddd.w.",
]
ARCHER = [
    humanoid(ARCHER_TOP, LEGS_IDLE),
    humanoid(ARCHER_TOP, LEGS_WALK1),
    humanoid(ARCHER_TOP, LEGS_WALK2),
    humanoid(ARCHER_DRAW, LEGS_IDLE),
    humanoid(ARCHER_LOOSE, LEGS_IDLE),
    ["........", "........", "........", "........", "......w.", ".dsccddw", "..ccddow", "......w."],
]

CAV_TOP = [
    "...nn...",
    "...ss...",
    "..ccc.hH",
    ".hhcchhH",
    "hhhhhhh.",
    ".hhhhh..",
]
CAV_ATK = [
    "...nn...",
    "...ssmmm",
    "..cccshH",
    ".hhcchhH",
    "hhhhhhh.",
    ".hhhhh..",
]
CAVALRY = [
    CAV_TOP + [".H..H.H.", ".o..o.o."],
    CAV_TOP + ["H..H..H.", "o...o..o"],
    CAV_TOP + [".H.H.H..", "..o.o.o."],
    CAV_ATK + ["H..H..H.", "o...o..o"],
    CAV_ATK + [".H.H.H..", "..o.o.o."],
    ["........", "........", "........", "..s.....", ".ncc....", "hhhhhhH.", "hhhhhhhH", ".HH.HH.."],
]

GUN = [
    "........",
    ".nn.....",
    ".ss.....",
    ".cc.kkkk",
    ".cckkkkk",
    ".dd.wkw.",
]
GUN_FIRE = [
    "........",
    ".nn.....",
    ".ss....f",
    ".cc.kkkf",
    ".cckkkkf",
    ".dd.wkw.",
]
GUN_RECOIL = [
    "........",
    ".nn.....",
    ".ss.....",
    ".cckkkk.",
    ".ckkkkk.",
    ".ddwkw..",
]
ARTILLERY = [
    GUN + [".c.wkkw.", ".o..ww.."],
    GUN + ["c..wkkw.", "o...ww.."],
    GUN + ["..cwkkw.", "..o.ww.."],
    GUN_FIRE + [".c.wkkw.", ".o..ww.."],
    GUN_RECOIL + [".c.wkkw.", ".o.ww..."],
    ["........", "........", "........", "........", "......kk", "..kkkkk.", "kkkw.ww.", "...ww..."],
]

# Drawn at twice the size in the game, so a war elephant towers over the men.
ELE_TOP = [
    "..cc....",
    ".cddc...",
    "gggggg..",
    "gggggggg",
    "ggggggGi",
    "Gggggg.g",
]
ELE_RAISE = [
    "..cc...g",
    ".cddc..g",
    "gggggggi",
    "gggggggg",
    "gggggg..",
    "Gggggg..",
]
ELEPHANT = [
    ELE_TOP + [".GG..GG.", ".GG..GG."],
    ELE_TOP + ["GG...GG.", "GG....GG"],
    ELE_TOP + [".GG.GG..", ".GG.GG.."],
    ELE_RAISE + [".GG...G.", ".GG....."],
    ELE_TOP + [".GG..GG.", "GGG..GGG"],
    ["........", "........", "........", "..cc....", ".gggggg.", "gggggggi", "GGGGGGG.", "........"],
]

# A war boat seen from the side: sail in the side's colour, crew on deck,
# oars dipping. Drawn at twice the size in the game.
BOAT_TOP = [
    "...c....",
    "...cc...",
    "...ccc..",
    "...w....",
    "wswswsw.",
    "wwwwwwww",
]
BOAT_SHOOT = [
    "...c....",
    "...cc...",
    "...ccc.m",
    "...w..w.",
    "wswswsw.",
    "wwwwwwww",
]
BOAT = [
    BOAT_TOP + [".wHHHHw.", "..x..x.."],
    BOAT_TOP + ["HwHHHHwH", ".x....x."],
    BOAT_TOP + [".wHHHHw.", "x.x..x.x"],
    BOAT_SHOOT + [".wHHHHw.", "..x..x.."],
    BOAT_TOP + [".wHHHHw.", "..x..x.."],
    ["........", "........", "........", "........", "........", "..HHHH..", ".wwwwww.", "xxxxxxxx"],
]

SOLDIERS = [INFANTRY, SPEARMAN, ARCHER, CAVALRY, ARTILLERY, ELEPHANT, BOAT]

# --- Chip symbols, drawn in the side colour on the chip's ivory inlay.

SYMBOLS_5 = [
    ["o...o", ".o.o.", "..o..", ".o.o.", "o...o"],
    ["..o..", ".ooo.", "o.o.o", "..o..", "..o.."],
    ["ooo..", "o..o.", "o..o.", "o..o.", "ooo.."],
    ["....o", "...o.", "..o..", ".o...", "o...."],
    [".....", ".ooo.", ".ooo.", ".ooo.", "....."],
    [".ooo.", "ooooo", "o...o", "o...o", "o...o"],
    ["..o..", "..oo.", "..o..", "ooooo", ".ooo."],
]
SYMBOLS_7 = [
    ["oo...oo", ".oo.oo.", "..ooo..", "...o...", "..ooo..", ".oo.oo.", "oo...oo"],
    ["...o...", "..ooo..", ".ooooo.", "o..o..o", "...o...", "...o...", "...o..."],
    ["oooo...", "oo.oo..", "oo..oo.", "oo..oo.", "oo..oo.", "oo.oo..", "oooo..."],
    [".....oo", "....ooo", "...ooo.", "..ooo..", ".ooo...", "ooo....", "oo....."],
    [".......", "..ooo..", ".ooooo.", ".ooooo.", ".ooooo.", "..ooo..", "......."],
    [".ooooo.", "ooooooo", "ooooooo", "oo...oo", "oo...oo", "oo...oo", "oo...oo"],
    ["...o...", "...oo..", "...ooo.", "...o...", "ooooooo", ".ooooo.", "..ooo.."],
]

TIER_BODY = [
    ((232, 228, 216), (70, 70, 76)),
    ((196, 44, 40), (245, 240, 230)),
    ((40, 140, 72), (245, 240, 230)),
    ((40, 40, 46), (230, 200, 90)),
    ((116, 52, 156), (245, 240, 230)),
    ((232, 150, 30), (50, 36, 24)),
    ((206, 176, 72), (120, 24, 24)),
]
IVORY = (242, 234, 214)


def colour(ch, side):
    if ch in side:
        return side[ch]
    return COMMON[ch]


def put_grid(px, ox, oy, grid, side, flash):
    assert len(grid) == CELL, grid
    for y, row in enumerate(grid):
        assert len(row) == CELL, row
        for x, ch in enumerate(row):
            if ch == ".":
                continue
            c = (255, 255, 255) if flash else colour(ch, side)
            px[ox + x, oy + y] = c + (255,)


def put_chip(px, ox, oy, tier, side, arm):
    body, stripe = TIER_BODY[tier]
    d = 13 + 2 * tier
    r = d / 2.0
    cx = cy = CHIP_CELL / 2.0
    for y in range(CHIP_CELL):
        for x in range(CHIP_CELL):
            dx, dy = x + 0.5 - cx, y + 0.5 - cy
            dist = math.hypot(dx, dy)
            if dist > r:
                continue
            if dist > r - 1.0:
                c = COMMON["o"]
            elif dist > r - 2.6:
                ang = math.degrees(math.atan2(dy, dx)) % 60.0
                c = stripe if 22.0 <= ang <= 38.0 else body
            elif dist > r - 3.6:
                c = SIDES[side]["c"]
            else:
                c = IVORY
            px[ox + x, oy + y] = c + (255,)
    sym = SYMBOLS_7[arm] if d >= 17 else SYMBOLS_5[arm]
    n = len(sym)
    sx = ox + (CHIP_CELL - n) // 2
    sy = oy + (CHIP_CELL - n) // 2
    for y, row in enumerate(sym):
        for x, ch in enumerate(row):
            if ch == "o":
                px[sx + x, sy + y] = SIDES[side]["d"] + (255,)


def main():
    width = max(CELL * FRAMES, CHIP_CELL * TIERS * 2)
    height = SYMBOL_Y + 5
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    px = img.load()
    for arm, frames in enumerate(SOLDIERS):
        assert len(frames) == FRAMES
        for variant in range(3):
            side = SIDES[min(variant, 1)]
            for f, grid in enumerate(frames):
                put_grid(px, f * CELL, (arm * 3 + variant) * CELL, grid, side, variant == 2)
    for arm in range(ARMS):
        for tier in range(TIERS):
            for side in range(2):
                put_chip(px, (tier * 2 + side) * CHIP_CELL, CHIP_Y + arm * CHIP_CELL, tier, side, arm)
    for arm, sym in enumerate(SYMBOLS_5):
        for y, row in enumerate(sym):
            for x, ch in enumerate(row):
                if ch == "o":
                    px[arm * 6 + x, SYMBOL_Y + y] = (255, 255, 255, 255)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("wrote", OUT, img.size)


if __name__ == "__main__":
    main()

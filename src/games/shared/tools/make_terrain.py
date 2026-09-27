"""Builds terrain.png for the platformer and top-down samples: 47-tile autotile sets.

Run from anywhere: python src/games/shared/tools/make_terrain.py
Needs Pillow. The output lands in src/games/{platformer,topdown}/assets and is committed.

Each set has 47 tiles, in the order of njin::autotile_index() for autotile_blob: the rank of the
neighbour mask (njin::autotile_neighbor) among the 47 valid masks, ascending. The shape of a tile
comes from the mask alone: an open side gets a lip, two open sides that meet get a round convex
corner, and a joined pair with an empty diagonal gets a curved lip around the inner corner.
"""

import hashlib
import math
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
GAMES = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(GAMES, "platformer", "tools"))
import make_assets as pa  # noqa: E402

T = 16
C = pa.C
px, rect = pa.px, pa.rect
SAND = (226, 196, 122)
SAND_DARK = (190, 150, 84)

UP, UP_RIGHT, RIGHT, DOWN_RIGHT, DOWN, DOWN_LEFT, LEFT, UP_LEFT = 1, 2, 4, 8, 16, 32, 64, 128


def reduce_mask(mask):
    """Same as the engine: a diagonal only counts when both orthogonals beside it are joined."""
    if not (mask & UP and mask & RIGHT):
        mask &= ~UP_RIGHT
    if not (mask & RIGHT and mask & DOWN):
        mask &= ~DOWN_RIGHT
    if not (mask & DOWN and mask & LEFT):
        mask &= ~DOWN_LEFT
    if not (mask & LEFT and mask & UP):
        mask &= ~UP_LEFT
    return mask


def blob_masks():
    """The 47 valid masks in index order."""
    return [m for m in range(256) if reduce_mask(m) == m]


def shape(mask, radius=5):
    """Classifies the 16x16 pixels of a tile: None (empty) or (distance to the edge, 'top' or 'side')."""
    open_up, open_right = not mask & UP, not mask & RIGHT
    open_down, open_left = not mask & DOWN, not mask & LEFT
    corners = []  # convex corners: (up?, left?)
    if open_up and open_left:
        corners.append((True, True))
    if open_up and open_right:
        corners.append((True, False))
    if open_down and open_right:
        corners.append((False, False))
    if open_down and open_left:
        corners.append((False, True))
    inner = []  # inner corners: (point x, point y, top?)
    if not open_up and not open_right and not mask & UP_RIGHT:
        inner.append((T, 0, True))
    if not open_down and not open_right and not mask & DOWN_RIGHT:
        inner.append((T, T, False))
    if not open_down and not open_left and not mask & DOWN_LEFT:
        inner.append((0, T, False))
    if not open_up and not open_left and not mask & UP_LEFT:
        inner.append((0, 0, True))

    out = [[None] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            fx, fy = x + 0.5, y + 0.5
            best = (99.0, "side")
            in_arc = False
            gone = False
            for up, left in corners:
                if (fx < radius if left else fx > T - radius) and (fy < radius if up else fy > T - radius):
                    in_arc = True
                    cx = radius if left else T - radius
                    cy = radius if up else T - radius
                    d = radius - math.hypot(fx - cx, fy - cy)
                    if d < 0:
                        gone = True
                    elif d < best[0]:
                        best = (d, "top" if up else "side")
            if gone:
                continue
            if not in_arc:
                for is_open, d, label in ((open_up, fy, "top"), (open_left, fx, "side"),
                                          (open_right, T - fx, "side"), (open_down, T - fy, "side")):
                    if is_open and d < best[0]:
                        best = (d, label)
            for cx, cy, top in inner:
                d = math.hypot(fx - cx, fy - cy)
                if d < best[0]:
                    best = (d, "top" if top else "side")
            out[y][x] = best
    return out


def noise(tag, x, y):
    """Deterministic 0..1 value, so regenerating the sheet gives the same bytes."""
    h = hashlib.md5(f"{tag}:{x}:{y}".encode()).digest()[0]
    return h / 255.0


def paint(img, ox, oy, cells, tag, body, top_lip, side_lip, outline):
    """body(x, y) and the lip colours turn a classified tile into pixels."""
    for y in range(T):
        for x in range(T):
            c = cells[y][x]
            if c is None:
                continue
            d, label = c
            if d < 1.0:
                color = outline
            elif d < 3.0 and label == "top":
                color = top_lip(d)
            elif d < 2.0:
                color = side_lip
            else:
                color = body(x, y, tag)
            px(img, ox + x, oy + y, color)


def dirt_body(x, y, tag):
    n = noise(tag, x, y)
    if n < 0.08:
        return C["dirt_dark"]
    if n > 0.95:
        return C["dirt_light"]
    return C["dirt"]


def grass_lip(d):
    return C["grass"] if d < 2.0 else C["grass_dark"]


def stone_body(x, y, tag):
    if y % 8 == 0 or (x + (8 if (y // 8) % 2 else 0)) % 16 == 0:
        return C["stone_dark"]
    if noise(tag, x, y) > 0.96:
        return C["stone_light"]
    return C["stone"]


def grass_body(x, y, tag):
    return C["grass_dark"] if noise(tag, x, y) < 0.07 else C["grass"]


def cell_origin(i, cols):
    return (i % cols) * T, (i // cols) * T


def make_platformer(masks):
    cols = 47
    img = Image.new("RGBA", (cols * T, 3 * T), (0, 0, 0, 0))
    for i, m in enumerate(masks):
        cells = shape(m)
        paint(img, i * T, 0, cells, "g", dirt_body, grass_lip, C["dirt_dark"], C["outline"])
        paint(img, i * T, T, cells, "s", stone_body, lambda d: C["stone_light"], C["stone_dark"], C["outline"])
    # row 2: 94 one-way plank, 95 bush, 96 flower (same as tiles.png)
    ox, oy = 0, 2 * T
    rect(img, ox, oy, ox + 15, oy + 4, C["wood"])
    rect(img, ox, oy + 5, ox + 15, oy + 5, C["wood_dark"])
    for x in (3, 11):
        rect(img, ox + x, oy + 6, ox + x + 1, oy + 9, C["wood_dark"])
    rect(img, ox, oy, ox + 15, oy, C["dirt_light"])
    ox = T
    for y in range(T):
        for x in range(T):
            if (x - 8) ** 2 / 64 + (y - 13) ** 2 / 40 <= 1 and y > 6:
                px(img, ox + x, oy + y, C["grass_dark"] if (x + y) % 5 == 0 else C["grass"])
    ox = 2 * T
    rect(img, ox + 7, oy + 9, ox + 8, oy + 15, C["grass_dark"])
    for (x, y) in ((7, 6), (6, 7), (8, 7), (7, 8), (9, 6)):
        px(img, ox + x, oy + y, C["pink"])
    px(img, ox + 7, oy + 7, C["yellow"])
    img.save(os.path.join(GAMES, "platformer", "assets", "terrain.png"))


def make_topdown(masks):
    cols = 47
    img = Image.new("RGBA", (cols * T, 3 * T), (0, 0, 0, 0))
    for i, m in enumerate(masks):
        cells = shape(m)
        # land: grass with a sand shore all round
        paint(img, i * T, 0, cells, "l", grass_body, lambda d: SAND, SAND, SAND_DARK)
        # rock: raised, with a dark outline
        paint(img, i * T, T, cells, "r", stone_body, lambda d: C["stone_light"], C["stone_dark"], C["outline"])
    # row 2: 94..97 water (animated), 98 flower
    for f in range(4):
        ox, oy = f * T, 2 * T
        rect(img, ox, oy, ox + 15, oy + 15, C["water"])
        for x in range(T):
            y = 5 + int(round(math.sin((x + f * 4) / 16 * 2 * math.pi) * 1.5))
            px(img, ox + x, oy + y, C["water_light"])
            y2 = 12 + int(round(math.sin((x - f * 4) / 16 * 2 * math.pi)))
            px(img, ox + x, oy + y2, C["water_dark"])
    ox, oy = 4 * T, 2 * T
    for (x, y) in ((7, 8), (6, 9), (8, 9), (7, 10)):
        px(img, ox + x, oy + y, C["pink"])
    px(img, ox + 7, oy + 9, C["yellow"])
    px(img, ox + 7, oy + 11, C["grass_dark"])
    px(img, ox + 7, oy + 12, C["grass_dark"])
    img.save(os.path.join(GAMES, "topdown", "assets", "terrain.png"))


def main():
    masks = blob_masks()
    assert len(masks) == 47
    make_platformer(masks)
    make_topdown(masks)
    print("ok")


if __name__ == "__main__":
    main()

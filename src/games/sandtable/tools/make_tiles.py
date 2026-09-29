"""Builds assets/tiles.png, the terrain tileset of the sand table game.

Run from anywhere: python src/games/sandtable/tools/make_tiles.py
Needs Pillow. The PNG is committed, so building the game does not need Python.

Tiles are 8x8 pixels; the game draws each pixel as 4 world units. Layout, in
tiles (levels.cpp reads the same numbers):
- Row 0: plains 0-3, forest 4-6, ford 7.
- Rows 1-4: hill, mountain, river, stream. Each is the 47-tile blob set in the
  order of njin::autotile_index(): column i is the i-th of the 47 valid
  neighbour masks, sorted. Their outside is transparent: the game draws the
  plains under them.
"""

import math
import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets", "tiles.png"))

T = 8        # tile size, pixels
COLS = 48    # tiles per row in the sheet (47 used by the blob sets)

UP, UR, RIGHT, DR, DOWN, DL, LEFT, UL = 1, 2, 4, 8, 16, 32, 64, 128


def canonical(mask):
    # A corner counts only when both sides next to it are there too.
    if not (mask & UP and mask & RIGHT):
        mask &= ~UR
    if not (mask & RIGHT and mask & DOWN):
        mask &= ~DR
    if not (mask & DOWN and mask & LEFT):
        mask &= ~DL
    if not (mask & LEFT and mask & UP):
        mask &= ~UL
    return mask


BLOB_MASKS = sorted({canonical(m) for m in range(256)})
assert len(BLOB_MASKS) == 47


def hash01(*v):
    h = 2166136261
    for x in v:
        h = ((h ^ (x & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    h ^= h >> 13
    h = (h * 0x5BD1E995) & 0xFFFFFFFF
    h ^= h >> 15
    return (h & 0xFFFF) / 65535.0


def blob_distance(mask, radius=3.0):
    """Distance, in pixels, from each pixel of the centre tile to the edge of
    its terrain, 0 where it is outside. The terrain is the tile and those of
    its neighbours in `mask`, with the convex corners rounded off."""
    n = 3 * T
    solid = [[False] * n for _ in range(n)]
    cells = {(1, 1): True}
    for bit, (cx, cy) in [(UP, (1, 0)), (UR, (2, 0)), (RIGHT, (2, 1)), (DR, (2, 2)), (DOWN, (1, 2)),
                          (DL, (0, 2)), (LEFT, (0, 1)), (UL, (0, 0))]:
        cells[(cx, cy)] = bool(mask & bit)
    for (cx, cy), on in cells.items():
        if on:
            for y in range(T):
                for x in range(T):
                    solid[cy * T + y][cx * T + x] = True
    disk = [(dx, dy) for dy in range(-3, 4) for dx in range(-3, 4) if dx * dx + dy * dy <= radius * radius]

    def at(g, x, y):
        return g[y][x] if 0 <= x < n and 0 <= y < n else True  # beyond the 3x3 counts as terrain

    eroded = [[all(at(solid, x + dx, y + dy) for dx, dy in disk) for x in range(n)] for y in range(n)]
    opened = [[any(at(eroded, x + dx, y + dy) for dx, dy in disk) for x in range(n)] for y in range(n)]
    dist = [[0.0] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            gx, gy = T + x, T + y
            if not opened[gy][gx]:
                continue
            best = 9.0
            for dy in range(-6, 7):
                for dx in range(-6, 7):
                    if not at(opened, gx + dx, gy + dy):
                        best = min(best, math.hypot(dx, dy))
            dist[y][x] = best
    return dist


# --- Colours -------------------------------------------------------------

GRASS = (104, 148, 72)
GRASS_DARK = (86, 128, 60)
GRASS_LIGHT = (124, 166, 84)
FLOWER_Y = (232, 212, 96)
FLOWER_W = (236, 236, 228)
BANK = (152, 126, 82)


def hill(d, x, y, k):
    if d <= 1.0:
        return (70, 106, 52)
    if d <= 2.0 and (x + y) % 2 == 0:
        return (98, 136, 66)
    return (132, 170, 88) if hash01(k, x, y, 1) > 0.12 else (150, 186, 100)


def mountain(d, x, y, k):
    if d <= 1.0:
        return (58, 54, 56)
    if d <= 2.0:
        return (100, 94, 92)
    if d >= 6.0:
        return (236, 240, 244) if hash01(k, x, y, 2) > 0.2 else (200, 206, 214)
    r = hash01(k, x, y, 3)
    if r < 0.18:
        return (164, 156, 150)
    if r < 0.3:
        return (112, 106, 102)
    return (138, 130, 124)


def river(d, x, y, k):
    if d <= 1.0:
        return BANK
    if d <= 2.2:
        return (92, 150, 178)
    if (x + 2 * y + k) % 7 == 0:
        return (118, 172, 204)
    return (54, 104, 148)


def stream(d, x, y, k):
    if d <= 1.0:
        return BANK
    if (2 * x + y + k) % 5 == 0:
        return (166, 208, 228)
    return (104, 162, 190)


def plains(v):
    tile = [[GRASS] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            r = hash01(v, x, y, 10)
            if r < 0.14:
                tile[y][x] = GRASS_DARK
            elif r < 0.2:
                tile[y][x] = GRASS_LIGHT
    # A few flowers on some variants.
    if v in (1, 3):
        for i in range(2):
            fx = int(hash01(v, i, 11) * 7)
            fy = int(hash01(v, i, 12) * 7)
            tile[fy][fx] = FLOWER_Y if i == 0 else FLOWER_W
    return tile


def forest(v):
    # Tree crowns seen from above over transparent ground: two or three per tile.
    tile = [[None] * T for _ in range(T)]
    spots = [(2, 2), (5, 5), (5, 1)] if v == 0 else [(2, 5), (5, 2)] if v == 1 else [(3, 3), (6, 6), (1, 6)]
    for cx, cy in spots:
        for y in range(T):
            for x in range(T):
                dx, dy = x - cx, y - cy
                if dx * dx + dy * dy <= 3:
                    tile[y][x] = (48, 84, 44)
        tile[cy - 1][max(0, cx - 1)] = (80, 128, 62)
        if cy + 2 < T:
            tile[cy + 2][cx] = (38, 58, 34)  # shadow
    return tile


def ford():
    tile = [[(194, 174, 126)] * T for _ in range(T)]
    for y in range(T):
        for x in range(T):
            if (x + y * 3) % 6 == 0:
                tile[y][x] = (120, 168, 196)
            elif hash01(x, y, 20) < 0.1:
                tile[y][x] = (150, 140, 112)
    return tile


def put(px, col, row, tile):
    for y in range(T):
        for x in range(T):
            c = tile[y][x]
            if c is not None:
                px[col * T + x, row * T + y] = c + (255,)


def main():
    img = Image.new("RGBA", (COLS * T, 5 * T), (0, 0, 0, 0))
    px = img.load()
    for v in range(4):
        put(px, v, 0, plains(v))
    for v in range(3):
        put(px, 4 + v, 0, forest(v))
    put(px, 7, 0, ford())

    for i, mask in enumerate(BLOB_MASKS):
        dist = blob_distance(mask)
        for row, paint in enumerate([hill, mountain, river, stream], start=1):
            tile = [[None] * T for _ in range(T)]
            for y in range(T):
                for x in range(T):
                    d = dist[y][x]
                    if d > 0.0:
                        tile[y][x] = paint(d, x, y, i)
            put(px, i, row, tile)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    img.save(OUT)
    print("wrote", OUT, img.size)


if __name__ == "__main__":
    main()

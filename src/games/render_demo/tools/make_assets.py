"""Builds the art of the render demo: a tileset and a few small sprites.

Run: python src/games/render_demo/tools/make_assets.py
Needs Pillow. The output lands in src/games/render_demo/assets and is committed.

The sprites are separate small files on purpose: the demo packs them into an
atlas (or loads them one by one) so you can see what that does to the draw
calls.
"""

import math
import os
import random

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))
T = 16

GRASS = (74, 140, 72, 255)
GRASS_DARK = (58, 116, 60, 255)
GRASS_LIGHT = (98, 164, 84, 255)
WATER = [(52, 110, 180, 255), (62, 124, 196, 255), (76, 140, 208, 255), (62, 124, 196, 255)]
FOAM = (200, 226, 244, 255)
TRUNK = (110, 74, 44, 255)
LEAF = (44, 112, 60, 255)
LEAF_LIGHT = (72, 148, 76, 255)
STONE = (128, 128, 138, 255)
STONE_DARK = (92, 92, 104, 255)
PINK = (240, 130, 160, 255)
YELLOW = (244, 214, 90, 255)
SKIN = (236, 186, 146, 255)
CLOTH = (200, 70, 60, 255)
CLOTH_DARK = (150, 46, 44, 255)
OUTLINE = (30, 30, 40, 255)


def put(img, x, y, color):
    if 0 <= x < img.width and 0 <= y < img.height:
        img.putpixel((x, y), color)


def rect(img, x0, y0, x1, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            put(img, x, y, color)


def disc(img, cx, cy, r, color):
    for y in range(cy - r, cy + r + 1):
        for x in range(cx - r, cx + r + 1):
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r // 2:
                put(img, x, y, color)


def make_tiles():
    rng = random.Random(3)
    img = Image.new("RGBA", (6 * T, T), (0, 0, 0, 0))
    for i in (0, 1):
        rect(img, i * T, 0, i * T + 15, 15, GRASS)
        for _ in range(12):
            put(img, i * T + rng.randrange(16), rng.randrange(16), GRASS_DARK)
        for _ in range(5):
            put(img, i * T + rng.randrange(16), rng.randrange(16), GRASS_LIGHT)
    # Water, four frames of the same ripple pattern shifting right.
    for f in range(4):
        ox = (2 + f) * T
        rect(img, ox, 0, ox + 15, 15, WATER[f])
        for row in (3, 9, 13):
            for x in range(16):
                if (x + f * 4 + row) % 8 < 3:
                    put(img, ox + x, row, FOAM)
    img.save(os.path.join(OUT, "tiles.png"))


def new(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def make_sprites():
    d = os.path.join(OUT, "sprites")
    os.makedirs(d, exist_ok=True)

    tree = new(16, 24)
    rect(tree, 7, 14, 9, 23, TRUNK)
    disc(tree, 8, 9, 7, LEAF)
    disc(tree, 6, 7, 3, LEAF_LIGHT)
    tree.save(os.path.join(d, "tree.png"))

    bush = new(16, 12)
    disc(bush, 8, 7, 5, LEAF)
    disc(bush, 4, 8, 3, LEAF)
    disc(bush, 12, 8, 3, LEAF)
    disc(bush, 7, 5, 2, LEAF_LIGHT)
    bush.save(os.path.join(d, "bush.png"))

    rock = new(12, 10)
    disc(rock, 6, 6, 4, STONE)
    rect(rock, 3, 8, 9, 9, STONE_DARK)
    disc(rock, 5, 5, 1, (170, 170, 180, 255))
    rock.save(os.path.join(d, "rock.png"))

    flower = new(8, 10)
    rect(flower, 3, 5, 3, 9, LEAF)
    disc(flower, 3, 3, 2, PINK)
    put(flower, 3, 3, YELLOW)
    flower.save(os.path.join(d, "flower.png"))

    hero = new(12, 16)
    rect(hero, 3, 0, 8, 4, SKIN)
    rect(hero, 3, 0, 8, 1, TRUNK)
    rect(hero, 2, 5, 9, 11, CLOTH)
    rect(hero, 2, 9, 9, 11, CLOTH_DARK)
    rect(hero, 3, 12, 4, 15, OUTLINE)
    rect(hero, 7, 12, 8, 15, OUTLINE)
    hero.save(os.path.join(d, "hero.png"))

    # A soft white blob for particles: tinted and faded by the emitter.
    spark = new(16, 16)
    for y in range(16):
        for x in range(16):
            dist = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5 / 8.0
            if dist < 1.0:
                a = int(255 * (1.0 - dist) ** 1.5)
                put(spark, x, y, (255, 255, 255, a))
    spark.save(os.path.join(d, "spark.png"))


def make_lut():
    """A 256 x 1 colour ramp for the dusk grade (key 8): the shader looks a pixel's
    brightness up in it, so dark goes to blue-violet and light to warm cream."""
    stops = [(0.0, (14, 12, 40)), (0.35, (96, 52, 120)), (0.7, (240, 132, 92)), (1.0, (255, 238, 196))]
    img = Image.new("RGBA", (256, 1))
    for x in range(256):
        t = x / 255.0
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t <= t1:
                k = (t - t0) / (t1 - t0)
                img.putpixel((x, 0), tuple(int(round(a + (b - a) * k)) for a, b in zip(c0, c1)) + (255,))
                break
    img.save(os.path.join(OUT, "ramp.png"))


def make_noise():
    """A 128 x 128 grey value noise that tiles, for the haze (key 9). Four octaves
    of a lattice with wrapped corners, so the texture repeats without a seam."""
    size = 128
    rng = random.Random(7)
    img = Image.new("RGBA", (size, size))
    octaves = []
    for cells in (4, 8, 16, 32):
        octaves.append((cells, [[rng.random() for _ in range(cells)] for _ in range(cells)]))

    def smooth(t):
        return t * t * (3.0 - 2.0 * t)

    def sample(cells, grid, x, y):
        gx, gy = x / size * cells, y / size * cells
        x0, y0 = int(math.floor(gx)), int(math.floor(gy))
        fx, fy = smooth(gx - x0), smooth(gy - y0)
        a, b = grid[y0 % cells][x0 % cells], grid[y0 % cells][(x0 + 1) % cells]
        c, d = grid[(y0 + 1) % cells][x0 % cells], grid[(y0 + 1) % cells][(x0 + 1) % cells]
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

    for y in range(size):
        for x in range(size):
            v, total, amp = 0.0, 0.0, 1.0
            for cells, grid in octaves:
                v += sample(cells, grid, x, y) * amp
                total += amp
                amp *= 0.5
            g = int(round(v / total * 255))
            img.putpixel((x, y), (g, g, g, 255))
    img.save(os.path.join(OUT, "noise.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_tiles()
    make_sprites()
    make_lut()
    make_noise()
    print("wrote", OUT)

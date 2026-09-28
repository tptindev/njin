"""Builds the art of the lighting demo: a tileset, and small sprites with the maps the PBR lights read.

Run: python src/games/lighting_demo/tools/make_assets.py
Needs Pillow. The output lands in src/games/lighting_demo/assets and is committed.

For every sprite that has them:
  name.png     the picture (albedo)
  name_n.png   the normal map, OpenGL style (green up)
  name_m.png   the material map, raylib's "MRA": red metallic, green roughness, blue ambient occlusion
  name_e.png   the emissive map: what glows by itself
  name_t.png   a mask: the only pixels that block light for light_occluder_pixels
"""

import math
import os
import random

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))
T = 16

STONE = (150, 146, 140, 255)
STONE_DARK = (122, 118, 114, 255)
WOOD = (150, 108, 70, 255)
WOOD_DARK = (118, 82, 52, 255)
GRASS = (86, 148, 78, 255)
GRASS_DARK = (70, 126, 66, 255)
BRICK = (126, 84, 70, 255)
BRICK_DARK = (92, 60, 52, 255)
MORTAR = (70, 62, 60, 255)
DIRT = (120, 92, 64, 255)
DIRT_TOP = (96, 150, 80, 255)
BACK = (70, 70, 92, 255)
BACK_DARK = (58, 58, 78, 255)
TRUNK = (110, 74, 44, 255)
LEAF = (44, 112, 60, 255)
LEAF_LIGHT = (72, 148, 76, 255)
ROCK = (128, 128, 138, 255)
ROCK_DARK = (92, 92, 104, 255)
SKIN = (236, 186, 146, 255)
CLOTH = (60, 110, 200, 255)
CLOTH_DARK = (40, 76, 150, 255)
BELT = (200, 170, 80, 255)
OUTLINE = (30, 30, 40, 255)
CRYSTAL = (90, 220, 240, 255)
CRYSTAL_CORE = (200, 255, 255, 255)


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


def new(w, h):
    return Image.new("RGBA", (w, h), (0, 0, 0, 0))


def speckle(img, x0, y0, colors, rng, count):
    for _ in range(count):
        put(img, x0 + rng.randrange(T), y0 + rng.randrange(T), rng.choice(colors))


def make_tiles():
    """One row of 16 x 16 tiles:
    0 stone floor, 1 stone floor (variant), 2 wood floor, 3 grass, 4 brick wall, 5 dirt with grass on top,
    6 dirt, 7 dark back wall (behind a side view level)."""
    rng = random.Random(7)
    img = new(8 * T, T)
    for i in (0, 1):
        x0 = i * T
        rect(img, x0, 0, x0 + T - 1, T - 1, STONE)
        rect(img, x0, 0, x0 + T - 1, 0, STONE_DARK)
        rect(img, x0, 0, x0, T - 1, STONE_DARK)
        if i == 1:
            rect(img, x0 + 8, 0, x0 + 8, T - 1, STONE_DARK)
        speckle(img, x0, 0, [STONE_DARK, (164, 160, 154, 255)], rng, 14)
    x0 = 2 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, WOOD)
    for y in (0, 5, 10, 15):
        rect(img, x0, y, x0 + T - 1, y, WOOD_DARK)
    speckle(img, x0, 0, [WOOD_DARK], rng, 8)
    x0 = 3 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, GRASS)
    speckle(img, x0, 0, [GRASS_DARK, (104, 168, 90, 255)], rng, 30)
    x0 = 4 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, MORTAR)
    for row in range(4):
        shift = 0 if row % 2 == 0 else 4
        for bx in range(-1, 2):
            left = x0 + bx * 8 + shift
            rect(img, max(left, x0), row * 4, min(left + 6, x0 + T - 1), row * 4 + 2, BRICK if (row + bx) % 2 else BRICK_DARK)
    x0 = 5 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, DIRT)
    rect(img, x0, 0, x0 + T - 1, 3, DIRT_TOP)
    speckle(img, x0, 4, [(100, 76, 52, 255)], rng, 10)
    x0 = 6 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, DIRT)
    speckle(img, x0, 0, [(100, 76, 52, 255), (136, 106, 76, 255)], rng, 14)
    x0 = 7 * T
    rect(img, x0, 0, x0 + T - 1, T - 1, BACK)
    rect(img, x0, 7, x0 + T - 1, 7, BACK_DARK)
    rect(img, x0, 15, x0 + T - 1, 15, BACK_DARK)
    rect(img, x0 + 5, 0, x0 + 5, 7, BACK_DARK)
    rect(img, x0 + 12, 8, x0 + 12, 15, BACK_DARK)
    img.save(os.path.join(OUT, "tiles.png"))


def normal_map(img, strength=2.2, radius=2):
    """Bulges a sprite: its opacity, blurred, is a height; the slope of that is the normal."""
    w, h = img.size
    alpha = [[img.getpixel((x, y))[3] / 255.0 for x in range(w)] for y in range(h)]

    def at(grid, x, y):
        return grid[min(max(y, 0), h - 1)][min(max(x, 0), w - 1)]

    height = alpha
    for _ in range(radius):  # a few box blurs are close enough to a gaussian
        height = [[sum(at(height, x + dx, y + dy) for dx in (-1, 0, 1) for dy in (-1, 0, 1)) / 9.0
                   for x in range(w)] for y in range(h)]
    out = Image.new("RGBA", (w, h), (128, 128, 255, 0))
    for y in range(h):
        for x in range(w):
            if alpha[y][x] == 0:
                continue
            dx = (at(height, x + 1, y) - at(height, x - 1, y)) * 0.5 * strength
            dy = (at(height, x, y + 1) - at(height, x, y - 1)) * 0.5 * strength
            nx, ny, nz = -dx, dy, 1.0  # green up: down the picture is negative dy
            n = math.sqrt(nx * nx + ny * ny + nz * nz)
            out.putpixel((x, y), (int((nx / n * 0.5 + 0.5) * 255), int((ny / n * 0.5 + 0.5) * 255),
                                  int((nz / n * 0.5 + 0.5) * 255), 255))
    return out


def sphere_normals(size):
    """The exact normals of a ball filling the picture: the clearest way to see what a normal map does."""
    out = Image.new("RGBA", (size, size), (128, 128, 255, 0))
    r = size / 2.0
    for y in range(size):
        for x in range(size):
            nx = (x + 0.5 - r) / r
            ny = -(y + 0.5 - r) / r  # green up
            d = nx * nx + ny * ny
            if d >= 1.0:
                continue
            nz = math.sqrt(1.0 - d)
            out.putpixel((x, y), (int((nx * 0.5 + 0.5) * 255), int((ny * 0.5 + 0.5) * 255), int((nz * 0.5 + 0.5) * 255), 255))
    return out


def material_map(img, rough, metal=0.0, occlusion_bottom=1.0):
    """A flat material over the opaque pixels; the occlusion darkens towards the bottom (where it meets the ground)."""
    w, h = img.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            if img.getpixel((x, y))[3] == 0:
                continue
            ao = 1.0 - (1.0 - occlusion_bottom) * (y / max(h - 1, 1)) ** 1.5
            out.putpixel((x, y), (int(metal * 255), int(rough * 255), int(ao * 255), 255))
    return out


def save(img, d, name):
    img.save(os.path.join(d, name + ".png"))


def make_sprites():
    d = os.path.join(OUT, "sprites")
    os.makedirs(d, exist_ok=True)

    tree = new(16, 24)
    rect(tree, 7, 14, 9, 23, TRUNK)
    disc(tree, 8, 9, 7, LEAF)
    disc(tree, 6, 7, 3, LEAF_LIGHT)
    save(tree, d, "tree")
    save(normal_map(tree), d, "tree_n")
    # Only the trunk blocks light for the pixel shadows: the leaves let it through.
    trunk = new(16, 24)
    for y in range(tree.height):
        for x in range(tree.width):
            if tree.getpixel((x, y))[:3] == TRUNK[:3]:
                trunk.putpixel((x, y), (255, 255, 255, 255))
    save(trunk, d, "tree_t")

    rock = new(16, 12)
    disc(rock, 8, 7, 5, ROCK)
    disc(rock, 4, 8, 3, ROCK)
    rect(rock, 2, 10, 13, 11, ROCK_DARK)
    disc(rock, 6, 5, 1, (170, 170, 180, 255))
    save(rock, d, "rock")
    save(normal_map(rock, strength=3.0), d, "rock_n")
    save(material_map(rock, 0.35, 0.0, 0.6), d, "rock_m")

    # A crate: flat planks, a bevelled rim (the normal map) and iron corners (metal in the material map).
    crate = new(16, 16)
    rect(crate, 0, 0, 15, 15, WOOD_DARK)
    rect(crate, 2, 2, 13, 13, WOOD)
    for y in (5, 10):
        rect(crate, 2, y, 13, y, WOOD_DARK)
    iron = (150, 150, 160, 255)
    for cx, cy in ((0, 0), (13, 0), (0, 13), (13, 13)):
        rect(crate, cx, cy, cx + 2, cy + 2, iron)
    save(crate, d, "crate")
    n = Image.new("RGBA", (16, 16), (128, 128, 255, 255))
    for y in range(16):
        for x in range(16):
            nx = -0.6 if x < 2 else (0.6 if x > 13 else 0.0)
            ny = 0.6 if y < 2 else (-0.6 if y > 13 else 0.0)
            ln = math.sqrt(nx * nx + ny * ny + 1.0)
            n.putpixel((x, y), (int((nx / ln * 0.5 + 0.5) * 255), int((ny / ln * 0.5 + 0.5) * 255), int((1.0 / ln * 0.5 + 0.5) * 255), 255))
    save(n, d, "crate_n")
    m = material_map(crate, 0.85, 0.0, 1.0)
    for y in range(16):
        for x in range(16):
            if crate.getpixel((x, y))[:3] == iron[:3]:
                m.putpixel((x, y), (255, int(0.3 * 255), 255, 255))
    save(m, d, "crate_m")

    # A ball, in white so the lights' colours show, with the exact normals of a sphere. The four
    # material maps are the four corners of the PBR model: rough / glossy, plastic / metal.
    ball = new(24, 24)
    disc(ball, 12, 12, 11, (235, 235, 235, 255))
    for y in range(24):
        for x in range(24):
            if (x + 0.5 - 12) ** 2 + (y + 0.5 - 12) ** 2 >= 144:
                put(ball, x, y, (0, 0, 0, 0))
    save(ball, d, "ball")
    save(sphere_normals(24), d, "ball_n")
    for name, rough, metal in (("rough", 0.9, 0.0), ("glossy", 0.2, 0.0), ("brushed", 0.55, 1.0), ("mirror", 0.12, 1.0)):
        save(material_map(ball, rough, metal, 1.0), d, "ball_m_" + name)

    # A gold ball: metal tints its reflections with its own colour.
    gold = ball.copy()
    for y in range(24):
        for x in range(24):
            if gold.getpixel((x, y))[3] > 0:
                gold.putpixel((x, y), (226, 184, 72, 255))
    save(gold, d, "gold")

    # A crystal that glows: the emissive map is its core.
    crystal = new(10, 16)
    for y in range(16):
        half = int(4 * (1 - abs(y - 7) / 8.0)) + 1
        rect(crystal, 5 - half, y, 4 + half, y, CRYSTAL)
    rect(crystal, 4, 3, 5, 12, CRYSTAL_CORE)
    save(crystal, d, "crystal")
    save(normal_map(crystal, strength=3.0, radius=1), d, "crystal_n")
    glow = new(10, 16)
    for y in range(16):
        for x in range(10):
            c = crystal.getpixel((x, y))
            if c[3] > 0:
                glow.putpixel((x, y), c if c[:3] == CRYSTAL_CORE[:3] else (40, 150, 170, 255))
    save(glow, d, "crystal_e")

    hero = new(12, 16)
    rect(hero, 3, 0, 8, 4, SKIN)
    rect(hero, 3, 0, 8, 1, TRUNK)
    rect(hero, 2, 5, 9, 11, CLOTH)
    rect(hero, 2, 9, 9, 11, CLOTH_DARK)
    rect(hero, 2, 8, 9, 8, BELT)
    rect(hero, 3, 12, 4, 15, OUTLINE)
    rect(hero, 7, 12, 8, 15, OUTLINE)
    save(hero, d, "hero")
    save(normal_map(hero), d, "hero_n")
    hm = material_map(hero, 0.75, 0.0, 0.8)
    for y in range(16):
        for x in range(12):
            if hero.getpixel((x, y))[:3] == BELT[:3]:
                hm.putpixel((x, y), (255, int(0.3 * 255), 255, 255))
    save(hm, d, "hero_m")

    # A lamp post for the side view: the lamp's glass glows.
    lamp = new(8, 32)
    rect(lamp, 3, 6, 4, 31, (60, 60, 70, 255))
    rect(lamp, 1, 0, 6, 6, (60, 60, 70, 255))
    rect(lamp, 2, 1, 5, 5, (255, 220, 150, 255))
    save(lamp, d, "lamp")
    lamp_e = new(8, 32)
    rect(lamp_e, 2, 1, 5, 5, (255, 220, 150, 255))
    save(lamp_e, d, "lamp_e")


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_tiles()
    make_sprites()
    print("assets written to", OUT)

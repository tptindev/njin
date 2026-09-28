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


def material_map(img, rough, metal=0.0, occlusion_bottom=0.6, metal_where=None):
    """A material map for the PBR lights, packed as raylib's pbr example does ("MRA"): red is
    metallic, green is roughness, blue is ambient occlusion (1 = open, darker towards the
    bottom of the sprite, where it meets the ground). `metal_where(color)` picks pixels that
    are metal, with their own roughness."""
    w, h = img.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(h):
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            if a == 0:
                continue
            ro, me = rough, metal
            if metal_where is not None:
                picked = metal_where((r, g, b, a))
                if picked is not None:
                    ro, me = picked
            ao = 1.0 - (1.0 - occlusion_bottom) * (y / max(h - 1, 1)) ** 1.5
            out.putpixel((x, y), (int(me * 255), int(ro * 255), int(ao * 255), 255))
    return out


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

    # Normal maps (key M): the silhouette blurred into a height map, whose slope
    # is the normal. Green points up, as most tools export them.
    # A gold ball, to see metal: bright reflections, tinted by the gold.
    orb = new(12, 12)
    disc(orb, 6, 6, 5, (226, 184, 72, 255))
    disc(orb, 4, 4, 1, (255, 236, 160, 255))
    orb.save(os.path.join(d, "orb.png"))

    for name, img in (("tree", tree), ("bush", bush), ("rock", rock), ("hero", hero)):
        normal_map(img).save(os.path.join(d, name + "_n.png"))
    normal_map(orb, strength=4.0, radius=1).save(os.path.join(d, "orb_n.png"))

    # Material maps (roughness, metallic, occlusion).
    material_map(tree, 0.92, 0.0, 0.5).save(os.path.join(d, "tree_m.png"))
    material_map(bush, 0.95, 0.0, 0.55).save(os.path.join(d, "bush_m.png"))
    material_map(rock, 0.32, 0.0, 0.65).save(os.path.join(d, "rock_m.png"))  # polished stone
    # The hero's dark red belt is metal; the rest is cloth.
    material_map(hero, 0.7, 0.0, 0.8,
                 metal_where=lambda c: (0.28, 1.0) if c[:3] == CLOTH_DARK[:3] else None).save(os.path.join(d, "hero_m.png"))
    material_map(orb, 0.36, 1.0, 0.9).save(os.path.join(d, "orb_m.png"))

    # Only the trunk of a tree blocks light for the pixel-perfect shadows: a mask with the trunk's own alpha.
    trunk = Image.new("RGBA", tree.size, (0, 0, 0, 0))
    for y in range(tree.height):
        for x in range(tree.width):
            if tree.getpixel((x, y))[:3] == TRUNK[:3]:
                trunk.putpixel((x, y), (255, 255, 255, 255))
    trunk.save(os.path.join(d, "tree_t.png"))

    # The flowers glow: the emissive map is the flower without its stem.
    glow = flower.copy()
    for y in range(glow.height):
        for x in range(glow.width):
            if glow.getpixel((x, y))[:3] == LEAF[:3]:
                glow.putpixel((x, y), (0, 0, 0, 0))
    glow.save(os.path.join(d, "flower_e.png"))

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

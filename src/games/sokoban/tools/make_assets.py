"""Builds the crate texture of the sokoban sample.

Run: python src/games/sokoban/tools/make_assets.py
Needs Pillow. The output lands in src/games/sokoban/assets and is committed.
The image is grey-brown wood; the game tints it (brown, green on a goal).
"""

import os
import random

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets"))

SIZE = 128


def make_crate():
    rng = random.Random(3)
    img = Image.new("RGB", (SIZE, SIZE))
    d = ImageDraw.Draw(img)
    # Four horizontal planks, each with its own shade and grain.
    plank = SIZE // 4
    for i in range(4):
        base = 205 + rng.randrange(-12, 12)
        y0 = i * plank
        d.rectangle([0, y0, SIZE - 1, y0 + plank - 1], fill=(base, base, base))
        for _ in range(9):
            y = y0 + rng.randrange(3, plank - 3)
            x0 = rng.randrange(-20, SIZE)
            shade = base - rng.randrange(18, 40)
            d.line([x0, y, x0 + rng.randrange(20, 70), y + rng.choice((-1, 0, 1))], fill=(shade,) * 3)
        d.line([0, y0, SIZE - 1, y0], fill=(120, 120, 120), width=2)
    # A frame around the face and a cross brace, darker, with nails.
    frame = 12
    dark = (150, 150, 150)
    d.rectangle([0, 0, SIZE - 1, frame - 1], fill=dark)
    d.rectangle([0, SIZE - frame, SIZE - 1, SIZE - 1], fill=dark)
    d.rectangle([0, 0, frame - 1, SIZE - 1], fill=dark)
    d.rectangle([SIZE - frame, 0, SIZE - 1, SIZE - 1], fill=dark)
    d.line([frame, frame, SIZE - frame, SIZE - frame], fill=dark, width=10)
    for x, y in ((6, 6), (SIZE - 7, 6), (6, SIZE - 7), (SIZE - 7, SIZE - 7)):
        d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(90, 90, 90))
    d.rectangle([0, 0, SIZE - 1, SIZE - 1], outline=(100, 100, 100), width=2)
    img.save(os.path.join(OUT, "crate.png"))


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    make_crate()
    print("wrote", os.path.join(OUT, "crate.png"))

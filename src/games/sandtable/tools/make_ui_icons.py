"""Draws the HUD's icons into src/games/sandtable/assets/ui.

    python src/games/sandtable/tools/make_ui_icons.py

Needs Pillow. Each icon is a white glyph on transparent ground, drawn four
times larger and shrunk for smooth edges; the game tints it. What it writes is
committed, so building the game does not need Python.
"""

import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "assets", "ui"))
SIZE = 64
K = 4  # drawn at SIZE * K
W = (255, 255, 255, 255)


def canvas():
    im = Image.new("RGBA", (SIZE * K, SIZE * K), (0, 0, 0, 0))
    return im, ImageDraw.Draw(im)


def s(*v):
    return [x * K for x in v]


def save(im, name):
    im.resize((SIZE, SIZE), Image.LANCZOS).save(os.path.join(OUT, name + ".png"))


def person(d, cx, top, scale=1.0):
    r = 7 * scale
    d.ellipse(s(cx - r, top, cx + r, top + 2 * r), fill=W)
    w = 13 * scale
    d.rounded_rectangle(s(cx - w, top + 2 * r + 2, cx + w, top + 2 * r + 24 * scale), radius=9 * K * scale, fill=W)


def people():
    im, d = canvas()
    person(d, 20, 16, 0.85)
    person(d, 44, 16, 0.85)
    # The one in front, with a clear gap round him.
    gap = Image.new("L", im.size, 0)
    gd = ImageDraw.Draw(gap)
    gd.ellipse(s(32 - 11, 12, 32 + 11, 34), fill=255)
    gd.rounded_rectangle(s(32 - 19, 34, 32 + 19, 62), radius=12 * K, fill=255)
    im.paste((0, 0, 0, 0), (0, 0), gap)
    person(d, 32, 15, 1.1)
    return im


def flag():
    im, d = canvas()
    d.rounded_rectangle(s(14, 8, 19, 58), radius=2 * K, fill=W)
    d.polygon(s(19, 10, 52, 12, 44, 22, 52, 32, 19, 32), fill=W)
    d.rounded_rectangle(s(8, 54, 32, 59), radius=2 * K, fill=W)
    return im


def coins():
    im, d = canvas()
    for i in range(3):
        y = 44 - i * 10
        d.ellipse(s(10, y, 42, y + 12), fill=W)
        d.rectangle(s(10, y + 6, 42, y + 12), fill=W)
        d.ellipse(s(10, y + 6, 42, y + 18), fill=W)
        d.arc(s(10, y, 42, y + 12), 0, 180, fill=(0, 0, 0, 0), width=2 * K)
    d.ellipse(s(30, 8, 58, 36), fill=W)
    d.ellipse(s(36, 14, 52, 30), outline=(0, 0, 0, 0), width=3 * K)
    return im


def bug():
    im, d = canvas()
    d.ellipse(s(22, 22, 42, 56), fill=W)
    d.ellipse(s(25, 12, 39, 26), fill=W)
    for y in (30, 39, 48):
        d.line(s(22, y, 12, y - 4), fill=W, width=3 * K)
        d.line(s(42, y, 52, y - 4), fill=W, width=3 * K)
    d.line(s(28, 14, 22, 6), fill=W, width=3 * K)
    d.line(s(36, 14, 42, 6), fill=W, width=3 * K)
    d.line(s(32, 26, 32, 55), fill=(0, 0, 0, 0), width=2 * K)
    return im


def pause():
    im, d = canvas()
    d.rounded_rectangle(s(18, 14, 28, 50), radius=2 * K, fill=W)
    d.rounded_rectangle(s(36, 14, 46, 50), radius=2 * K, fill=W)
    return im


def play():
    im, d = canvas()
    d.polygon(s(20, 12, 50, 32, 20, 52), fill=W)
    return im


def fast():
    im, d = canvas()
    d.polygon(s(10, 14, 32, 32, 10, 50), fill=W)
    d.polygon(s(32, 14, 54, 32, 32, 50), fill=W)
    return im


def chevron(up):
    im, d = canvas()
    pts = s(14, 40, 32, 22, 50, 40) if up else s(14, 24, 32, 42, 50, 24)
    d.line(pts, fill=W, width=7 * K, joint="curve")
    return im


def close():
    im, d = canvas()
    d.line(s(18, 18, 46, 46), fill=W, width=6 * K)
    d.line(s(46, 18, 18, 46), fill=W, width=6 * K)
    return im


def house():
    im, d = canvas()
    d.polygon(s(8, 30, 32, 10, 56, 30), fill=W)
    d.rectangle(s(14, 30, 50, 56), fill=W)
    d.rectangle(s(27, 40, 37, 56), fill=(0, 0, 0, 0))
    return im


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in [("people", people), ("flag", flag), ("coins", coins), ("bug", bug), ("pause", pause),
                     ("play", play), ("fast", fast), ("up", lambda: chevron(True)), ("down", lambda: chevron(False)),
                     ("close", close), ("house", house)]:
        save(fn(), name)
    print("icons ->", OUT)


if __name__ == "__main__":
    main()

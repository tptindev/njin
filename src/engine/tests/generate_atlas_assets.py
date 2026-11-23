"""Rebuild deterministic atlas PNG fixtures with the Python standard library."""
from pathlib import Path
import struct
import zlib


def write_png(name, width, height, pixel):
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload
                + struct.pack(">I", zlib.crc32(kind + payload)))

    rows = b"".join(b"\0" + bytes(c for x in range(width) for c in pixel(x, y))
                    for y in range(height))
    data = (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    (Path(__file__).parent / "assets" / f"atlas-{name}.png").write_bytes(data)


if __name__ == "__main__":
    colors = [(255, 0, 0, 255), (0, 255, 0, 255),
              (0, 0, 255, 255), (255, 255, 255, 255)]
    write_png("quad", 4, 4, lambda x, y: colors[(y // 2) * 2 + x // 2])
    for name, size, color in [
        ("cyan", 4, (0, 255, 255, 255)),
        ("magenta", 4, (255, 0, 255, 255)),
        ("yellow", 12, (255, 255, 0, 255)),
        ("large", 20, (255, 128, 0, 255)),
        ("red", 4, (255, 0, 0, 255)),
        ("green", 4, (0, 255, 0, 255)),
    ]:
        write_png(name, size, size, lambda x, y, c=color: c)

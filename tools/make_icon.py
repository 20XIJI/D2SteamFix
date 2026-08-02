"""Render src/steamfix.ico.

Each size is drawn at its own resolution rather than downscaled from one large
image, so the 16px variant stays legible in Explorer and Task Manager.
"""

import struct
from io import BytesIO

from PIL import Image, ImageDraw, ImageFont

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"

BACKDROP = (18, 22, 33, 255)      # near-black, cool cast
ACCENT = (255, 148, 46, 255)      # amber slash
TEXT = (245, 247, 250, 255)


def render(size: int) -> Image.Image:
    # Supersample so the rounded corners and slash stay clean at small sizes.
    scale = 8 if size <= 48 else 2
    box = size * scale
    image = Image.new("RGBA", (box, box), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)

    radius = round(box * 0.22)
    draw.rounded_rectangle([0, 0, box - 1, box - 1], radius=radius, fill=BACKDROP)

    target = box * 0.72
    point = 1
    while True:
        candidate = ImageFont.truetype(FONT, point + 1)
        left, top, right, bottom = candidate.getbbox("D2")
        if right - left > target or bottom - top > box * 0.54 or point > box:
            break
        point += 1
    font = ImageFont.truetype(FONT, point)

    # White D, amber 2. Colour carries the mark instead of an added glyph, which is what
    # survives at 16px -- a slash or badge there collapses into noise.
    left, top, right, bottom = font.getbbox("D2")
    origin_x = (box - (right + left)) / 2
    origin_y = (box - (bottom + top)) / 2 - box * 0.02
    draw.text((origin_x, origin_y), "D", font=font, fill=TEXT)
    draw.text((origin_x + font.getlength("D"), origin_y), "2", font=font, fill=ACCENT)

    # Amber underline: steady, and reads as a base rather than a prohibition.
    bar_height = max(1, round(box * 0.055))
    bar_inset = round(box * 0.2)
    draw.rounded_rectangle(
        [bar_inset, round(box * 0.80), box - bar_inset, round(box * 0.80) + bar_height],
        radius=bar_height // 2,
        fill=ACCENT,
    )

    return image.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    frames = [render(size) for size in SIZES]

    payloads = []
    for frame in frames:
        buffer = BytesIO()
        frame.save(buffer, format="PNG", optimize=True)
        payloads.append(buffer.getvalue())

    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = len(header) + 16 * len(frames)
    entries = b""
    for frame, payload in zip(frames, payloads):
        side = 0 if frame.width == 256 else frame.width
        entries += struct.pack(
            "<BBBBHHII", side, side, 0, 0, 1, 32, len(payload), offset
        )
        offset += len(payload)

    with open("src/steamfix.ico", "wb") as handle:
        handle.write(header + entries + b"".join(payloads))

    print(f"wrote src/steamfix.ico ({offset} bytes, {len(frames)} sizes)")


if __name__ == "__main__":
    main()

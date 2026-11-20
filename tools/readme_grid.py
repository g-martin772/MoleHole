#!/usr/bin/env python3
"""Lays rendered PNGs out in a labelled grid for the README (used by tools/gen_readme_images.sh).

Usage: readme_grid.py OUT.jpg RAW_DIR COLUMNS FONT "shot=label;shot=label;..."
Every shot is RAW_DIR/<shot>.png; all must have the same size. Needs Pillow.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont


def main() -> int:
    target, raw, columns, font_path, panels = sys.argv[1:6]
    columns = int(columns)
    entries = [entry.split("=", 1) for entry in panels.split(";") if entry]
    missing = [shot for shot, _ in entries if not os.path.exists(os.path.join(raw, shot + ".png"))]
    if missing:
        print(f"{target}: missing renders {', '.join(missing)}", file=sys.stderr)
        return 1
    images = [(Image.open(os.path.join(raw, shot + ".png")).convert("RGB"), label) for shot, label in entries]
    width, height = images[0][0].size
    gap = 6
    rows = (len(images) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * width + (columns - 1) * gap, rows * height + (rows - 1) * gap), (13, 17, 23))
    font = ImageFont.truetype(font_path, max(16, height // 22))
    draw = ImageDraw.Draw(sheet, "RGBA")
    pad = max(4, height // 45)
    for i, (image, label) in enumerate(images):
        x, y = (i % columns) * (width + gap), (i // columns) * (height + gap)
        sheet.paste(image, (x, y))
        origin = (x + 2 * pad, y + 2 * pad)
        box = draw.textbbox(origin, label, font=font)
        draw.rounded_rectangle((box[0] - pad, box[1] - pad, box[2] + pad, box[3] + pad), radius=pad, fill=(0, 0, 0, 170))
        draw.text(origin, label, font=font, fill=(240, 240, 240))
    sheet.save(target, quality=88, optimize=True, progressive=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())

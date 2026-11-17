#!/usr/bin/env python3
"""
png_to_bmp.py - convert images to 24-bit BMP for the DE1-SoC board.

The board has no PNG decoder (and no Python), so images are converted on the PC
and copied over. This is the only Python script in the project.

Usage:
    python3 scripts/png_to_bmp.py IMAGE [IMAGE ...] [--out-dir out] [--resize 256x256]

For each input it writes <out-dir>/<name>.bmp (or <name>_<W>x<H>.bmp when resized):
24-bit RGB, uncompressed, which is what HPS/src/image.c reads.
"""

import argparse
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("png_to_bmp.py needs Pillow (python3 -m pip install pillow)")

# Option 2 (buffer inside the accelerator) is sized for 256x256 pixels.
OPTION2_MAX_PIXELS = 256 * 256


def parse_size(text):
    try:
        w, h = (int(v) for v in text.lower().split("x"))
        if w <= 0 or h <= 0:
            raise ValueError
        return w, h
    except ValueError:
        raise argparse.ArgumentTypeError("size must look like 256x256")


def convert(path, out_dir, resize):
    name = os.path.splitext(os.path.basename(path))[0]
    with Image.open(path) as im:
        rgb = im.convert("RGB")
    if resize:
        name = f"{name}_{resize[0]}x{resize[1]}"
        lanczos = getattr(Image, "Resampling", Image).LANCZOS  # old and new Pillow
        rgb = rgb.resize(resize, lanczos)

    out_path = os.path.join(out_dir, name + ".bmp")
    rgb.save(out_path, format="BMP")  # 24-bit, uncompressed, rows stored bottom-up

    w, h = rgb.size
    pw, ph = -(-w // 8) * 8, -(-h // 8) * 8          # padded to multiples of 8
    blocks = (pw // 8) * (ph // 8)
    fits = "yes" if pw * ph <= OPTION2_MAX_PIXELS else "no (options 1, 3, 4 only)"
    print(f"{path} -> {out_path}")
    print(f"    {w}x{h}, padded {pw}x{ph}, {blocks} blocks, fits option 2 buffer: {fits}")


def main():
    ap = argparse.ArgumentParser(description="Convert images to 24-bit BMP for the board.")
    ap.add_argument("images", nargs="+")
    ap.add_argument("--out-dir", default="out", help="output directory (default out/)")
    ap.add_argument("--resize", type=parse_size, help="resize first, e.g. 256x256")
    args = ap.parse_args()

    os.makedirs(args.out_dir, exist_ok=True)
    for path in args.images:
        convert(path, args.out_dir, args.resize)
    return 0


if __name__ == "__main__":
    sys.exit(main())

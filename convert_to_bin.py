#!/usr/bin/env python3
"""
Convert an image to the 466x466 RGB565 (little-endian) raw .bin the Trailmaster
image-frame feature reads. Matches the device's own uploader packing exactly:
    rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)   # low byte first

Usage:
    python3 convert_to_bin.py <input image> <output .bin> [--fit contain|cover]
      contain (default): whole image fit inside 466x466 on black (good for logos)
      cover:             fill 466x466, center-cropping the overflow (good for photos)
"""
import sys
from PIL import Image

SIZE = 466

def main():
    if len(sys.argv) < 3:
        print("Usage: convert_to_bin.py <input> <output.bin> [--fit contain|cover]", file=sys.stderr)
        sys.exit(1)
    src, dst = sys.argv[1], sys.argv[2]
    fit = "contain"
    if "--fit" in sys.argv:
        fit = sys.argv[sys.argv.index("--fit") + 1]

    im = Image.open(src).convert("RGB")
    if fit == "cover":
        # scale to fill, center-crop
        scale = max(SIZE / im.width, SIZE / im.height)
        nw, nh = round(im.width * scale), round(im.height * scale)
        im = im.resize((nw, nh), Image.LANCZOS)
        left, top = (nw - SIZE) // 2, (nh - SIZE) // 2
        im = im.crop((left, top, left + SIZE, top + SIZE))
    else:
        # contain on black
        scale = min(SIZE / im.width, SIZE / im.height)
        nw, nh = round(im.width * scale), round(im.height * scale)
        im = im.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGB", (SIZE, SIZE), (0, 0, 0))
        canvas.paste(im, ((SIZE - nw) // 2, (SIZE - nh) // 2))
        im = canvas

    px = im.load()
    out = bytearray(SIZE * SIZE * 2)
    j = 0
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            out[j] = v & 0xFF          # low byte first (little-endian)
            out[j + 1] = (v >> 8) & 0xFF
            j += 2
    with open(dst, "wb") as f:
        f.write(out)
    print(f"Wrote {dst} ({len(out)} bytes, expected {SIZE*SIZE*2})")

if __name__ == "__main__":
    main()

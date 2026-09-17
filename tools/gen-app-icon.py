#!/usr/bin/env python3
"""Resizes a source icon image into the Android mipmap-*/ic_launcher.png
buckets an app needs. Used by android/bundle-app-android.sh (and callable
directly) rather than hand-maintaining five resized copies of every icon.

Usage: gen-app-icon.py <source-image> <path/to/app/src/main/res>
"""
import os
import sys

from PIL import Image

SIZES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    src_path, res_dir = sys.argv[1], sys.argv[2]

    img = Image.open(src_path).convert("RGB")
    # Source icons are already square; if someone hands in a non-square
    # image, center-crop it to square first so it doesn't look squashed.
    w, h = img.size
    if w != h:
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        img = img.crop((left, top, left + side, top + side))

    for bucket, size in SIZES.items():
        out_dir = os.path.join(res_dir, bucket)
        os.makedirs(out_dir, exist_ok=True)
        img.resize((size, size), Image.LANCZOS).save(os.path.join(out_dir, "ic_launcher.png"))

    print(f"Wrote ic_launcher.png into {len(SIZES)} mipmap buckets under {res_dir}")


if __name__ == "__main__":
    main()

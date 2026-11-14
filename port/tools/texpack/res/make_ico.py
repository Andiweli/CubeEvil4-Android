#!/usr/bin/env python3
"""The Windows icon of the CubeEvil4 Texture Packer (res/packer.ico), made from the Android packer app's symbol
(port/android-decomp/packer/src/main/res/drawable-nodpi/packer_symbol.png, from its tools/make_icon.py): the red
CubeEvil4 sign on a dark rounded square, as the Android app's icon. Sizes 16 to 256 pixels, each drawn at four times its
size and scaled down.

    python port/tools/texpack/res/make_ico.py        (needs Pillow)
"""
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SYMBOL = ROOT / "android-decomp/packer/src/main/res/drawable-nodpi/packer_symbol.png"
OUT = HERE / "packer.ico"
DARK = (27, 20, 22, 255)
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]


def icon(size):
    big = size * 4
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # small icons fill more of their square, as Windows' own
    margin = 0 if size <= 24 else round(big * 0.03)
    d.rounded_rectangle([margin, margin, big - 1 - margin, big - 1 - margin], radius=round(big * 0.2), fill=DARK)
    sym = Image.open(SYMBOL).convert("RGBA")
    # the symbol PNG is the adaptive icon's foreground (its middle two thirds hold the sign)
    crop = sym.crop((sym.width // 6, sym.height // 6, sym.width * 5 // 6, sym.height * 5 // 6))
    inner = big - 2 * margin - round(big * (0.06 if size <= 24 else 0.12))
    crop = crop.resize((inner, inner), Image.LANCZOS)
    img.alpha_composite(crop, ((big - inner) // 2, (big - inner) // 2))
    return img.resize((size, size), Image.LANCZOS)


def main():
    images = [icon(s) for s in SIZES]
    images[-1].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

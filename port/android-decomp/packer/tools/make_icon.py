#!/usr/bin/env python3
"""The Texture Packer's icon, made from CubeEvil4's (app/src/main/res/drawable-nodpi/icon_cubeevil4.png): the same
symbol, red on a dark ground instead of dark on red, so the two apps look related but differ at a glance.

    python packer/tools/make_icon.py        (from port/android-decomp; needs Pillow)

Writes packer/src/main/res/drawable-nodpi/packer_symbol.png (the adaptive icon's foreground, transparent around the
symbol) and packer_icon.png (the whole icon for Android 7.x).
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "app/src/main/res/drawable-nodpi/icon_cubeevil4.png"
OUT = ROOT / "packer/src/main/res/drawable-nodpi"
RED = (214, 48, 44)
DARK = (27, 20, 22)


def symbol(size):
    """The symbol in red, transparent elsewhere (the source's dark strokes on red)."""
    src = Image.open(SOURCE).convert("RGB").resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px, po = src.load(), out.load()
    for y in range(size):
        for x in range(size):
            r, g, b = px[x, y]
            # the red ground is bright in red, the strokes are dark: their darkness is the symbol's cover
            cover = max(0.0, min(1.0, (r - 70) / (150 - 70)))
            alpha = round(255 * (1.0 - cover))
            if alpha:
                po[x, y] = RED + (alpha,)
    return out


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    symbol(384).save(OUT / "packer_symbol.png", optimize=True)
    icon = Image.new("RGBA", (192, 192), (0, 0, 0, 0))
    ImageDraw.Draw(icon).rounded_rectangle((8, 8, 184, 184), radius=36, fill=DARK + (255,))
    s = symbol(176)
    icon.alpha_composite(s, (8, 8))
    icon.save(OUT / "packer_icon.png", optimize=True)
    print("written:", OUT / "packer_symbol.png", OUT / "packer_icon.png")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Draw the ZATI launcher icon.

The mark is the instrument itself, reduced to what survives at 48px: a pad
grid on the app's white chassis, with four pads lit from the fixed fragment
palette in cut order. That palette IS the product, so the icon states it
rather than describing it. Everything is flat, with the same square-ish
corner radius as the real pads: no gradients, no bevels, no lettering (type
is illegible at launcher size).

Run: python3 ci/make_icon.py   ->   ci/icon.png (1024x1024)
"""
from PIL import Image, ImageDraw

S = 1024                      # master size; Android downsamples from here
CHASSIS = (255, 255, 255)
PAD_OFF = (227, 227, 221)     # ShardColours::padTop
# Four evenly spaced hues from the fixed 8-fragment ZATI palette (Zati.h).
ROJO     = (232, 84, 74)      # zati 1
AMBAR    = (240, 190, 68)     # zati 3
TURQUESA = (74, 196, 168)     # zati 5
VIOLETA  = (140, 110, 224)    # zati 7

# Lit pads in SP order (01 = bottom-left), climbing the diagonal. The colours
# are taken from the fixed fragment palette in cut order, which is the whole
# idea of the product: the mark IS the colour system. The diagonal keeps it
# balanced instead of bottom-heavy.
LIT = {0: ROJO, 5: AMBAR, 10: TURQUESA, 15: VIOLETA}


def main() -> None:
    img = Image.new("RGBA", (S, S), CHASSIS + (255,))
    d = ImageDraw.Draw(img)

    # Chassis: a hairline inset frame so the icon still reads as an object
    # on a white launcher background.
    d.rounded_rectangle([6, 6, S - 7, S - 7], radius=int(S * 0.22),
                        outline=(214, 214, 206), width=8)

    cols = rows = 4
    margin = int(S * 0.13)
    gap = int(S * 0.055)
    span = S - 2 * margin
    cell = (span - (cols - 1) * gap) // cols
    grid = cols * cell + (cols - 1) * gap
    x0 = y0 = (S - grid) // 2
    radius = int(cell * 0.14)          # matches the app's near-square pads

    for r in range(rows):
        for c in range(cols):
            idx = r * cols + c          # 0 = bottom-left, SP numbering
            vr = rows - 1 - r           # flip to visual row
            x = x0 + c * (cell + gap)
            y = y0 + vr * (cell + gap)
            box = [x, y, x + cell, y + cell]

            colour = LIT.get(idx)
            if colour is not None:
                d.rounded_rectangle(box, radius=radius, fill=colour)
            else:
                d.rounded_rectangle(box, radius=radius, fill=PAD_OFF)


    out = __file__.rsplit("/", 1)[0] + "/icon.png"
    img.convert("RGB").save(out, "PNG")
    print("wrote", out, img.size)


if __name__ == "__main__":
    main()

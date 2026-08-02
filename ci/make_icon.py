#!/usr/bin/env python3
"""Draw the COLORS launcher icon.

The mark is the instrument itself, reduced to what survives at 48px: a pad
grid on the app's white chassis. Three of the sixteen pads are lit in the
three primaries the whole product is built on (blue / red / yellow) — the
name stated in the only vocabulary the app uses. Everything is flat, with
the same square-ish corner radius as the real pads: no gradients, no bevels,
no lettering (type is illegible at launcher size).

Run: python3 ci/make_icon.py   ->   ci/icon.png (1024x1024)
"""
from PIL import Image, ImageDraw

S = 1024                      # master size; Android downsamples from here
CHASSIS = (255, 255, 255)
PAD_OFF = (227, 227, 221)     # ShardColours::padTop
AZUL = (47, 111, 237)         # accent
ROJO = (224, 34, 44)          # red
AMARILLO = (240, 180, 0)      # yellow
INK = (28, 28, 26)            # ink

# Lit pads in SP order (01 = bottom-left), climbing the diagonal: blue, red,
# yellow, then ink — the four colours the whole product is built from. The
# diagonal keeps the mark balanced instead of bottom-heavy.
LIT = {0: AZUL, 5: ROJO, 10: AMARILLO, 15: INK}


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

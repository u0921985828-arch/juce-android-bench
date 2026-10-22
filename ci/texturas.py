#!/usr/bin/env python3
#  Las cuatro baldosas del chasis, de las de PixelLab a Source/texturas/*.png.
#
#      python3 -I ci/texturas.py papel.png grafito.png acero.png laca.png
#
#  Cada baldosa se pasa a gris, se le quita el marco (un octavo por lado) y se guarda
#  como relieve: 128 es la media y 40 una desviacion tipica. Ni escalado ni desenfoque:
#  la app la repite en mosaico, un pixel suyo por pixel, con las juntas en espejo.
import sys, os
import numpy as np
from PIL import Image

NOMBRES = ("papel", "grafito", "acero", "laca")

def relieve (path):
    a = np.asarray (Image.open (path).convert ("L"), dtype=np.float64)
    b = a.shape[0] // 8, a.shape[1] // 8
    a = a[b[0]:a.shape[0] - b[0], b[1]:a.shape[1] - b[1]]
    d = (a - a.mean()) / (a.std() or 1.0) * 40.0
    return Image.fromarray (np.clip (np.rint (d + 128.0), 0, 255).astype (np.uint8), "L")

if len (sys.argv) != 5:
    sys.exit ("uso: texturas.py papel.png grafito.png acero.png laca.png")
fuera = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "Source", "texturas")
os.makedirs (fuera, exist_ok=True)
for nombre, p in zip (NOMBRES, sys.argv[1:]):
    im = relieve (p)
    im.convert ("RGB").save (os.path.join (fuera, nombre + ".png"), optimize=True)
    print ("escrito", nombre, "%dx%d" % im.size)

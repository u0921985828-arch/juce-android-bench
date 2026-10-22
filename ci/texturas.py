#!/usr/bin/env python3
#  Las cuatro texturas del chasis, de las imagenes de PixelLab a Source/texturas/*.png.
#
#      python3 -I ci/texturas.py papel.png grafito.png acero.png laca.png
#
#  Cada imagen viene ya del tamano del chasis. Se pasa a gris, se quita el borde liso
#  que a veces deja PixelLab y la luz de gran escala (la pone la app), y se guarda
#  como relieve: 128 es la media y 40 una desviacion tipica. Ni escalado ni desenfoque:
#  cada pixel de PixelLab llega entero.
import sys, os
import numpy as np
from PIL import Image, ImageFilter

NOMBRES = ("papel", "grafito", "acero", "laca")

def sin_borde (a):
    #  filas y columnas lisas del borde, como la franja gris de los lados
    plana = lambda v: v.std() < 3.0
    t, b, l, r = 0, a.shape[0], 0, a.shape[1]
    while t < b - 1 and plana (a[t, l:r]): t += 1
    while b > t + 1 and plana (a[b - 1, l:r]): b -= 1
    while l < r - 1 and plana (a[t:b, l]): l += 1
    while r > l + 1 and plana (a[t:b, r - 1]): r -= 1
    return a[t:b, l:r]

def relieve (path):
    im = Image.open (path).convert ("L")
    a = sin_borde (np.asarray (im, dtype=np.float64))
    #  la luz de gran escala fuera: solo se resta, el pixel no se toca
    luz = np.asarray (Image.fromarray (a.astype (np.uint8)).filter (ImageFilter.GaussianBlur (12)), dtype=np.float64)
    d = a - luz
    d = (d - d.mean()) / (d.std() or 1.0) * 40.0
    return Image.fromarray (np.clip (np.rint (d + 128.0), 0, 255).astype (np.uint8), "L")

if len (sys.argv) != 5:
    sys.exit ("uso: texturas.py papel.png grafito.png acero.png laca.png")
fuera = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "Source", "texturas")
os.makedirs (fuera, exist_ok=True)
for nombre, p in zip (NOMBRES, sys.argv[1:]):
    im = relieve (p)
    im.convert ("RGB").save (os.path.join (fuera, nombre + ".png"), optimize=True)
    print ("escrito", nombre, "%dx%d" % im.size)

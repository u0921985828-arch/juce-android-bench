#!/usr/bin/env python3
#  Las cuatro texturas del chasis, de las baldosas de PixelLab a Source/Texturas.h.
#
#      python3 -I ci/texturas.py papel.png grafito.png acero.png laca.png
#
#  Cada PNG se pasa a gris, se hace continua por los bordes (mezcla con su copia
#  desplazada media baldosa), se suaviza para quitar el escalon del pixel art y se
#  guarda como desviacion de la media: la app pone el color, esto solo el relieve.
import sys, os
from PIL import Image, ImageFilter

LADO = 64

def baldosa (path):
    #  sin el marco que PixelLab deja en el borde: se leia como una reja de baldosas
    im = Image.open (path).convert ("L")
    c = im.width // 8
    im = im.crop ((c, c, im.width - c, im.height - c)).resize ((LADO, LADO), Image.BICUBIC)
    a = list (im.get_flattened_data())
    v = lambda x, y: a[(y % LADO) * LADO + (x % LADO)]
    h = LADO // 2
    out = []
    for y in range (LADO):
        for x in range (LADO):
            #  peso 1 en el centro y 0 en el borde: el borde lo pone la copia desplazada
            wx = 1.0 - abs (x - h + 0.5) / h
            wy = 1.0 - abs (y - h + 0.5) / h
            w = min (wx, wy)
            out.append (w * v (x, y) + (1.0 - w) * v (x + h, y + h))
    #  suavizado circular: se tesela 3x3, se desenfoca y se recorta el centro
    t = Image.new ("L", (LADO * 3, LADO * 3))
    t.putdata ([int (round (out[(y % LADO) * LADO + (x % LADO)])) for y in range (LADO * 3) for x in range (LADO * 3)])
    t = t.filter (ImageFilter.GaussianBlur (1.8)).crop ((LADO, LADO, 2 * LADO, 2 * LADO))
    d = list (t.get_flattened_data())
    m = sum (d) / len (d)
    sd = (sum ((x - m) ** 2 for x in d) / len (d)) ** 0.5 or 1.0
    return [max (-127, min (127, int (round ((x - m) / sd * 40)))) for x in d]

if len (sys.argv) != 5:
    sys.exit ("uso: texturas.py papel.png grafito.png acero.png laca.png")
fuera = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "Source", "Texturas.h")
with open (fuera, "w") as f:
    f.write ("#pragma once\n//  Generado por ci/texturas.py desde baldosas de PixelLab. No editar a mano.\n")
    f.write ("//  Desviacion del gris medio, 40 = una desviacion tipica.\n\n")
    f.write ("namespace Texturas\n{\n    inline constexpr int kLado = %d;\n\n" % LADO)
    f.write ("    inline constexpr signed char kRelieve[4][%d] =\n    {\n" % (LADO * LADO))
    for p in sys.argv[1:]:
        d = baldosa (p)
        f.write ("        {\n")
        for i in range (0, len (d), 32):
            f.write ("            " + ",".join (str (x) for x in d[i:i + 32]) + ",\n")
        f.write ("        },\n")
    f.write ("    };\n}\n")
print ("escrito", os.path.normpath (fuera))

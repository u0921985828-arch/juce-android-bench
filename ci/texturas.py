#!/usr/bin/env python3
#  Las cuatro texturas del chasis, de las baldosas de PixelLab a Source/Texturas.h.
#
#      python3 -I ci/texturas.py papel.png grafito.png acero.png laca.png
#
#  Cada PNG se pasa a gris sin el marco del borde y sin tocar un pixel: ni escalado,
#  ni mezcla, ni desenfoque. La app lo estira al ancho del chasis con el pixel entero.
#  Se guarda como desviacion de la media: la app pone el color, esto solo el relieve.
import sys, os
from PIL import Image

def baldosa (path):
    #  sin el marco que PixelLab deja en el borde: se leia como una reja de baldosas
    im = Image.open (path).convert ("L")
    c = im.width // 8
    im = im.crop ((c, c, im.width - c, im.height - c))
    d = list (im.get_flattened_data())
    m = sum (d) / len (d)
    sd = (sum ((x - m) ** 2 for x in d) / len (d)) ** 0.5 or 1.0
    return im.width, [max (-127, min (127, int (round ((x - m) / sd * 40)))) for x in d]

if len (sys.argv) != 5:
    sys.exit ("uso: texturas.py papel.png grafito.png acero.png laca.png")
fuera = os.path.join (os.path.dirname (os.path.abspath (__file__)), "..", "Source", "Texturas.h")
lados, datos = zip (*(baldosa (p) for p in sys.argv[1:]))
if len (set (lados)) != 1: sys.exit ("las cuatro baldosas tienen que medir lo mismo")
LADO = lados[0]
with open (fuera, "w") as f:
    f.write ("#pragma once\n//  Generado por ci/texturas.py desde baldosas de PixelLab. No editar a mano.\n")
    f.write ("//  Desviacion del gris medio, 40 = una desviacion tipica.\n\n")
    f.write ("namespace Texturas\n{\n    inline constexpr int kLado = %d;\n\n" % LADO)
    f.write ("    inline constexpr signed char kRelieve[4][%d] =\n    {\n" % (LADO * LADO))
    for d in datos:
        f.write ("        {\n")
        for i in range (0, len (d), LADO):
            f.write ("            " + ",".join (str (x) for x in d[i:i + LADO]) + ",\n")
        f.write ("        },\n")
    f.write ("    };\n}\n")
print ("escrito", os.path.normpath (fuera))

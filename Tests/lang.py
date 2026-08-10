#!/usr/bin/env python3
"""ZATI - el banco de la TABLA de idiomas.

expo.py compara lo que se DIBUJA en dos idiomas, y por eso no ve nada de esto:
una clave que no existe en la tabla sale igual en los cuatro idiomas y parece
"identica a proposito", y una fila duplicada nunca llega a usarse. Esto mira la
tabla y el codigo, no la pantalla.

Encontro tres cosas la primera vez que se ejecuto: la fila MODO estaba dos
veces, T("KIT") habia quedado apuntando a una clave que se renombro, y el texto
que explica MASTER contra PISTAS en la ficha de exportacion no estaba en la
tabla - o sea, en espanol en las cuatro compilaciones.
"""
import re, sys, os, glob, collections

HERE = os.path.dirname (os.path.abspath (__file__))
SRC  = os.path.join (HERE, "..", "Source")

def joined_literals (text):
    """Une los literales que C++ concatena solos: T("a" "b") es la clave "ab".
    Sin esto, el comprobador da falsos positivos en cada texto largo."""
    return re.sub (r'"\s*\n\s*"', "", text)

def main():
    lang = joined_literals (open (os.path.join (SRC, "Lang.cpp"), encoding="utf8").read())
    blk  = lang[lang.index ("const Row kTable[]"):]
    blk  = blk[:blk.index ("\n    };")]
    rows = re.findall (r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                       r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                       r'\s*"((?:[^"\\]|\\.)*)"\s*\}', blk, re.S)
    keys = [r[0] for r in rows]
    bad  = []

    for k, c in collections.Counter (keys).items():
        if c > 1: bad.append ("fila duplicada: %r (la segunda no se usa nunca)" % k)

    for r in rows:
        for i, lg in ((2, "en"), (3, "zh"), (4, "ar")):
            if not r[i].strip():
                bad.append ("fila %r sin %s" % (r[0], lg))

    code = ""
    for f in sorted (glob.glob (os.path.join (SRC, "*.cpp")) + glob.glob (os.path.join (SRC, "*.h"))):
        code += joined_literals (open (f, encoding="utf8").read())
    used = set (re.findall (r'T \("((?:[^"\\]|\\.)*)"', code))
    for k in sorted (used - set (keys)):
        bad.append ("clave usada y NO en la tabla (sale en espanol en los cuatro): %r" % k)

    print ("%d filas, %d claves usadas en el codigo" % (len (rows), len (used)))
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("la tabla esta completa")

main()

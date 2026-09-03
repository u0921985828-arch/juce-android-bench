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

def sin_comentarios (text):
    """Quita los comentarios respetando los literales.

    Hace falta porque esta prueba se caza a si misma: el comentario que explica
    que se recogen los literales de dentro de un `T ("...")` esta escrito ASI,
    con las comillas, en `Lang.cpp` y en `MainComponent_Audit.cpp` — o sea que
    la expresion de abajo lo lee como una clave y sale `FALLA ... '...'`. Una
    clave nombrada en prosa no es una clave usada.

    Se escribe a mano y no con una regex porque `//` vive tambien dentro de una
    cadena (una URL, por ejemplo) y borrar desde ahi partiria el literal."""
    out, i, n = [], 0, len (text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":                      # literal: se copia entero
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append (text[i:j + 1]); i = j + 1
        elif text.startswith ("//", i):
            j = text.find ("\n", i);  i = n if j < 0 else j
        elif text.startswith ("/*", i):
            j = text.find ("*/", i);  i = n if j < 0 else j + 2
        else:
            out.append (c); i += 1
    return "".join (out)

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
        code += joined_literals (sin_comentarios (open (f, encoding="utf8").read()))
    used = set (re.findall (r'T \("((?:[^"\\]|\\.)*)"', code))

    #  Y LAS CLAVES QUE LLEGAN POR VARIABLE, que esta prueba no puede ver.
    #
    #  Los nombres de los parametros de un efecto pasan por `T()` desde
    #  `fxDefs[f].param[pi]`, o sea que aqui no hay literal que recoger: la
    #  expresion de arriba busca lo que esta escrito DENTRO de un `T ("...")`.
    #  Asi estuvo «TONE» -CTRL 2 de DRV- sin fila en la tabla desde el dia que
    #  ese efecto existe, diciendo lo mismo en las cuatro compilaciones, y con
    #  veinte nombres de parametro mas por venir eso deja de ser un descuido.
    #
    #  Se leen de `fxDefs` en el fuente y no de una lista escrita aqui: una
    #  tabla copiada en dos sitios son dos tablas.
    fx = joined_literals (open (os.path.join (SRC, "MainComponent.cpp"), encoding="utf8").read())
    i  = fx.index ("const MainComponent::FxDef MainComponent::fxDefs")
    blkfx = fx[i:fx.index ("\n};", i)]
    params = set()
    for trio in re.findall (r'\{\s*"[A-Z]{2,3}"\s*,\s*\{([^}]*)\}', blkfx):
        params.update (x.strip().strip ('"') for x in trio.split (",") if x.strip())
    used |= params

    for k in sorted (used - set (keys)):
        bad.append ("clave usada y NO en la tabla (sale en espanol en los cuatro): %r" % k)

    print ("%d filas, %d claves usadas en el codigo (%d de ellas parametros de efecto)"
           % (len (rows), len (used), len (params)))
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("la tabla esta completa")

main()

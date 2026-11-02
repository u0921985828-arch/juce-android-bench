#!/usr/bin/env python3
# ============================================================================
#  LAS FLECHAS DEL PLATO (plato#paginas). Del telefono: «tienen que ocupar
#  literal el mismo espacio que ocupan los caracteres, desde la parte mas baja
#  a la mas alta; tira unas lineas y que no sobrepasen esas lineas».
#
#  Foto a 4x en la carcasa MAR (titulo claro, flechas ambar) y se mide la tinta
#  fila a fila: la banda de las mayusculas del titulo (R y E) contra la de cada
#  flecha. La flecha no puede salirse de la banda y tiene que llenarla (como
#  mucho un pixel de 1x de holgura por cada lado).
#
#      python3 Tests/flechas.py [ruta/al/Zati]
# ============================================================================
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import PANTALLA, display_alive                                    # noqa: E402

ESCALA = 4


def foto (tam, que):
    """La foto a 4x, recortada al 40 % de arriba (donde esta el plato): (ancho, alto, pixel(x, y) -> (r, g, b))."""
    casa = tempfile.mkdtemp (prefix="zati-flechas-")
    png = os.path.join (casa, "f.png")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa, XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=tam, ZATI_LANG="es", ZATI_SKIN="3", ZATI_OPEN=que,
                ZATI_SHOT=png, ZATI_SHOT_SCALE=str (ESCALA))
    try:
        subprocess.run ([APP], env=env, capture_output=True, timeout=180)
        if not os.path.exists (png): return None
        raw = subprocess.run (["convert", png, "-gravity", "north", "-crop", "100%x40%+0+0", "+repage",
                               "-depth", "8", "rgb:-"], capture_output=True).stdout
        dims = subprocess.run (["identify", "-format", "%w %h", png], capture_output=True, text=True).stdout.split()
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    W = int (dims[0]); H = len (raw) // (3 * W)
    return W, H, (lambda x, y: tuple (raw[3 * (y * W + x): 3 * (y * W + x) + 3]))


AMBAR = lambda r, g, b: r > 170 and g > 110 and b < 110
CLARO = lambda r, g, b: r > 150 and g > 150 and b > 140 and abs (r - g) < 30


def mide (tam, que):
    f = foto (tam, que)
    if f is None:
        return None, "sin foto"
    W, H, px = f
    cols = {}
    for y in range (H):
        for x in range (W):
            if AMBAR (*px (x, y)): cols.setdefault (x, []).append (y)
    #  Las flechas son dos manchas ambar estrechas a la misma altura, una a cada lado del centro. Se busca una estrecha,
    #  y en SU banda de filas (y solo ahi, que el LCD y la barra de colores de arriba tambien tienen ambar) la pareja.
    def agrupa (columnas):
        gs, g = [], []
        for x in sorted (columnas):
            if g and x - g[-1] > 2 * ESCALA: gs.append (g); g = []
            g.append (x)
        return gs + ([g] if g else [])
    def banda (gr, c): ys = [y for x in gr for y in c[x]]; return (min (ys), max (ys))
    pares = []
    for gr in agrupa (cols):
        if not (3 <= len (gr) <= 12 * ESCALA): continue
        b0, b1 = banda (gr, cols)
        if b1 - b0 > 20 * ESCALA: continue
        sub = {x: [y for y in ys if b0 - 2 <= y <= b1 + 2] for x, ys in cols.items()}
        sub = {x: ys for x, ys in sub.items() if ys}
        lados = [g2 for g2 in agrupa (sub) if 3 <= len (g2) <= 12 * ESCALA]
        izqs = [g2 for g2 in lados if g2[-1] < W / 2]
        ders = [g2 for g2 in lados if g2[0] > W / 2]
        if izqs and ders:
            pares.append ((izqs[-1], ders[0], sub)); break
    if not pares:
        return None, "no salen dos flechas a la misma altura"
    izq, der, sub = pares[0]
    fi, fd = banda (izq, sub), banda (der, sub)
    y0, y1 = fi[0] - 8 * ESCALA, fi[1] + 8 * ESCALA
    #  Las mayusculas SIN tildes: por columna, la primera y la ultima fila con tinta, y de todas las columnas la altura
    #  que mas se repite arriba y abajo. Una tilde solo sube unas pocas columnas y no cambia la moda.
    from collections import Counter
    arriba, abajo = Counter(), Counter()
    for x in range (izq[-1] + 1, der[0]):
        ys = [y for y in range (max (0, y0), min (H, y1)) if CLARO (*px (x, y))]
        if ys: arriba[min (ys)] += 1; abajo[max (ys)] += 1
    titulo = (arriba.most_common (1)[0][0], abajo.most_common (1)[0][0]) if arriba else None
    return (titulo, fi, fd), None


def main():
    if not display_alive():
        print ("no hay DISPLAY vivo"); return 1
    malo = []
    for tam in ("393x851", "280x653", "412x915"):
        for que in ("platop1", "platop2"):
            r, err = mide (tam, que)
            if err:
                malo.append ("%s %s: %s" % (tam, que, err)); continue
            t, fi, fd = r
            print ("%-8s %-8s mayusculas %s   flecha izq %s   der %s   (filas a %dx)" % (tam, que, t, fi, fd, ESCALA))
            for nombre, f in (("izquierda", fi), ("derecha", fd)):
                if f is None or t is None:
                    malo.append ("%s %s: no se mide la flecha %s" % (tam, que, nombre)); continue
                if f[0] < t[0] or f[1] > t[1]:
                    malo.append ("%s %s: la flecha %s se sale de las mayusculas: %s contra %s" % (tam, que, nombre, f, t))
                if f[0] - t[0] > ESCALA or t[1] - f[1] > ESCALA:
                    malo.append ("%s %s: la flecha %s no llena el alto de las mayusculas: %s contra %s" % (tam, que, nombre, f, t))
    for m in malo: print ("FALLA  " + m)
    if not malo: print ("las flechas del plato ocupan el alto de las mayusculas del titulo, sin salirse")
    return 1 if malo else 0


if __name__ == "__main__":
    sys.exit (main())

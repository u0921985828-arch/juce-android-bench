#!/usr/bin/env python3
#  EL ACABADO DEL CHASIS: un brillo que se ve, que no ensucia y que no come contraste.
#
#  El grano se probo y se quito porque se leia como suciedad (ver ZatiColours).
#  Esto mide el fondo horneado de cada carcasa, escrito por la app:
#
#      ZATI_FONDO=x.png ZATI_SKIN=n ./Zati
#
#  1. SE VE: en la misma fila, el punto del brillo contra el extremo opuesto, dE >= 1.
#  2. NO ENSUCIA: ningun pixel se separa de su vecino mas de 2 niveles por canal (un
#     degradado de 8 bits salta de uno en uno; el grano, de seis), y una fila
#     entera no abre mas de 6 dE (muy leve).
#  3. NO COME CONTRASTE: la tinta contra el pixel mas claro del fondo sigue en MIN_TEXT.
#
#      python3 Tests/acabado.py
#      ACABADO_RUIDO=1 python3 Tests/acabado.py     # el grano de antes: tiene que fallar
import os, sys, subprocess, tempfile, shutil, random

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.join (HERE, "..")
sys.path.insert (0, HERE)
from kits import PANTALLA                                          # noqa: E402
from skins import parse, SKINS, dE, ratio, MIN_TEXT                # noqa: E402

APP = os.environ.get ("ZATI_BIN") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
W, H = 400, 800
fallos = []

def mide (nombre, ok, detalle=""):
    print ("  %s  %-46s %s" % ("ok " if ok else "MAL", nombre, detalle))
    if not ok: fallos.append (nombre)

def fondo (skin):
    casa = tempfile.mkdtemp (prefix="zati-acabado-")
    png = os.path.join (casa, "f.png")
    try:
        env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa, ZATI_FONDO=png, ZATI_SKIN=str (skin))
        try: subprocess.run ([APP], env=env, capture_output=True, timeout=60)
        except subprocess.TimeoutExpired: return None
        if not os.path.exists (png): return None
        raw = subprocess.run (["convert", png, "-depth", "8", "RGB:-"], capture_output=True).stdout
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    if len (raw) != W * H * 3: return None
    q = [[(raw[(y * W + x) * 3] << 16) | (raw[(y * W + x) * 3 + 1] << 8) | raw[(y * W + x) * 3 + 2]
          for x in range (W)] for y in range (H)]
    if os.environ.get ("ACABADO_RUIDO"):
        rnd = random.Random (skin)
        for y in range (H):
            for x in range (W):
                d = rnd.randint (-3, 3)
                p = q[y][x]
                q[y][x] = sum (max (0, min (255, ((p >> s) & 255) + d)) << s for s in (16, 8, 0))
    return q

tabla = parse()
for i, nombre in enumerate (SKINS):
    q = fondo (i)
    if q is None:
        mide ("%s: el fondo se dibuja" % nombre, False, "la app no escribio el PNG")
        continue
    yb = int (H * 0.05)
    se_ve = dE (q[yb][int (W * 0.2)], q[yb][W - 1])
    mide ("%s: el brillo se ve" % nombre, se_ve >= 1.0, "dE %.2f (liston 1.0)" % se_ve)

    salto = lambda a, b: max (abs (((a >> s) & 255) - ((b >> s) & 255)) for s in (16, 8, 0))
    vecino = 0
    for y in range (0, H - 1, 3):
        for x in range (0, W - 1, 3):
            vecino = max (vecino, salto (q[y][x], q[y][x + 1]), salto (q[y][x], q[y + 1][x]))
    fila = max (dE (min (r), max (r)) for r in (q[y] for y in range (0, H, 40)))
    mide ("%s: el brillo no ensucia" % nombre, vecino <= 2 and fila <= 6.0,
          "vecino %d niveles (<= 2)  fila %.2f dE (<= 6)" % (vecino, fila))

    ink = tabla[i]['ink']
    peor = min (ratio (ink, p) for r in q[::20] for p in r[::20])
    mide ("%s: la tinta sigue leyendose" % nombre, peor >= MIN_TEXT,
          "ratio %.2f (liston %.2f)" % (peor, MIN_TEXT))

print ("VEREDICTO: %s" % ("OK  el acabado de las cuatro carcasas se ve, no ensucia y no come contraste"
                          if not fallos else "MAL  " + ", ".join (fallos)))
sys.exit (1 if fallos else 0)

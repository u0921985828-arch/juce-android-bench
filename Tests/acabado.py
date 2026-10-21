#!/usr/bin/env python3
#  EL ACABADO DEL CHASIS: un brillo que se ve, que no ensucia y que no come contraste.
#
#  El grano se probo y se quito porque se leia como suciedad (ver ZatiColours).
#  Esto mide el fondo horneado de cada carcasa, escrito por la app:
#
#      ZATI_FONDO=x.png ZATI_SKIN=n ./Zati
#
#  1. SE VE: en la misma fila, el punto del brillo contra el extremo opuesto, dE >= 1,
#     medido sobre la media de 31 px para que la textura no tape el brillo.
#  2. NO ENSUCIA: ningun pixel se separa de su vecino mas de 8 niveles por canal (sin
#     escalones; el grano lo caza la forma de la regla 4), y una fila de la media de 31 px
#     no abre mas de 12 dE (el degradado da 3 a 5; las manchas grandes del material, el resto).
#  3. NO COME CONTRASTE: la tinta contra el pixel mas claro del fondo sigue en MIN_TEXT.
#  4. LA TEXTURA SE LEE COMO MATERIAL: quitado el degradado (media de 31 px), lo que
#     queda tiene cuerpo a un palmo (desviacion >= 3 niveles, pico <= 24) y es una forma y
#     no ruido (autocorrelacion a 3 px >= 0.4; el grano de antes da ~0).
#
#      python3 Tests/acabado.py
#      ACABADO_RUIDO=1 python3 Tests/acabado.py     # el grano de antes: tiene que fallar
import os, sys, subprocess, tempfile, shutil, random, math

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

def media (q, R=15):
    #  acabado: media de 31 px por canal, con el borde recortado a la imagen.
    out = [[0] * W for _ in range (H)]
    for s in (16, 8, 0):
        col = [[0] * (W + 1) for _ in range (H + 1)]
        for y in range (H):
            fila, acc = q[y], 0
            for x in range (W):
                acc += (fila[x] >> s) & 255
                col[y + 1][x + 1] = col[y][x + 1] + acc
        for y in range (H):
            y0, y1 = max (0, y - R), min (H, y + R + 1)
            for x in range (W):
                x0, x1 = max (0, x - R), min (W, x + R + 1)
                v = (col[y1][x1] - col[y0][x1] - col[y1][x0] + col[y0][x0]) / ((y1 - y0) * (x1 - x0))
                out[y][x] |= int (round (v)) << s
    return out

def textura (nombre, q):
    #  acabado: residuo tras quitar el degradado, en la ventana central de 200x200.
    lv = [[(((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255)) / 3.0 for p in r] for r in q]
    R, x0, y0, N = 15, 100, 300, 200
    col = [[0.0] * (W + 1) for _ in range (H + 1)]
    for y in range (H):
        for x in range (W): col[y + 1][x + 1] = lv[y][x] + col[y][x + 1] + col[y + 1][x] - col[y][x]
    caja = lambda y, x: (col[y + R + 1][x + R + 1] - col[y - R][x + R + 1]
                         - col[y + R + 1][x - R] + col[y - R][x - R]) / float ((2 * R + 1) ** 2)
    res = [[lv[y][x] - caja (y, x) for x in range (x0, x0 + N)] for y in range (y0, y0 + N)]
    vals = [v for r in res for v in r]
    m = sum (vals) / len (vals)
    var = sum ((v - m) ** 2 for v in vals) / len (vals)
    sd = math.sqrt (var)
    pico = max (abs (v - m) for v in vals)
    L = 3
    cov = sum ((res[y][x] - m) * (res[y][x + L] - m) + (res[y][x] - m) * (res[y + L][x] - m)
               for y in range (N - L) for x in range (N - L)) / (2.0 * (N - L) ** 2)
    ac = cov / var if var > 1e-9 else 0.0
    mide ("%s: la textura se lee como material" % nombre, sd >= 3.0 and pico <= 24.0 and ac >= 0.4,
          "desviacion %.2f (>= 3)  pico %.1f (<= 24)  forma %.2f (>= 0.4)" % (sd, pico, ac))

tabla = parse()
for i, nombre in enumerate (SKINS):
    q = fondo (i)
    if q is None:
        mide ("%s: el fondo se dibuja" % nombre, False, "la app no escribio el PNG")
        continue
    m = media (q)
    yb = int (H * 0.05)
    se_ve = dE (m[yb][int (W * 0.2)], m[yb][W - 1])
    mide ("%s: el brillo se ve" % nombre, se_ve >= 1.0, "dE %.2f (liston 1.0)" % se_ve)

    salto = lambda a, b: max (abs (((a >> s) & 255) - ((b >> s) & 255)) for s in (16, 8, 0))
    vecino = 0
    for y in range (0, H - 1, 3):
        for x in range (0, W - 1, 3):
            vecino = max (vecino, salto (q[y][x], q[y][x + 1]), salto (q[y][x], q[y + 1][x]))
    fila = max (dE (min (r), max (r)) for r in (m[y] for y in range (0, H, 40)))
    mide ("%s: el brillo no ensucia" % nombre, vecino <= 8 and fila <= 12.0,
          "vecino %d niveles (<= 8)  fila %.2f dE (<= 12)" % (vecino, fila))

    textura (nombre, q)

    ink = tabla[i]['ink']
    peor = min (ratio (ink, p) for r in q[::20] for p in r[::20])
    mide ("%s: la tinta sigue leyendose" % nombre, peor >= MIN_TEXT,
          "ratio %.2f (liston %.2f)" % (peor, MIN_TEXT))

print ("VEREDICTO: %s" % ("OK  el acabado de las cuatro carcasas se ve, no ensucia y no come contraste"
                          if not fallos else "MAL  " + ", ".join (fallos)))
sys.exit (1 if fallos else 0)

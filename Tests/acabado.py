#!/usr/bin/env python3
#  EL ACABADO DEL CHASIS: un brillo que se ve, que no ensucia y que no come contraste.
#
#  El grano se probo y se quito porque se leia como suciedad (ver ZatiColours).
#  Esto mide el fondo horneado de cada carcasa, escrito por la app:
#
#      ZATI_FONDO=x.png ZATI_SKIN=n ./Zati
#
#  1. SE VE: en la misma franja de arriba, el punto del brillo contra el extremo opuesto,
#     dE >= 1, cada uno la media de un recuadro de 61x31 para que la textura no tape el brillo.
#  2. NO ENSUCIA: una fila de la media de 31 px no abre mas de 12 dE (el degradado da 3
#     a 5; las manchas grandes del material, el resto). El grano lo caza la regla 5.
#  3. NO COME CONTRASTE: la tinta contra el pixel mas claro del fondo sigue en MIN_TEXT.
#  4. LA TEXTURA SE LEE COMO MATERIAL: quitado el degradado (media de 31 px), lo que
#     queda tiene cuerpo a un palmo (desviacion >= 3 niveles) y ninguna mota pasa de un
#     octavo de la escala (pico <= 32). Que sea material y no ruido lo dice la regla 5.
#  5. ES LA BALDOSA DE PIXELLAB EN MOSAICO: la textura del fondo se compara con
#     Source/texturas/<carcasa>.png repetida como la repite la app (un pixel suyo por
#     pixel, juntas en espejo). Correlacion >= 0.9. Estirada, a otra escala o desenfocada
#     no llega.
#
#      ACABADO_BORROSO=1 python3 Tests/acabado.py   # desenfocada 3x3: tiene que fallar
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
    if os.environ.get ("ACABADO_BORROSO"):
        c = [r[:] for r in q]
        for y in range (1, H - 1):
            for x in range (1, W - 1):
                q[y][x] = sum ((sum ((c[y + j][x + i] >> s) & 255 for j in (-1, 0, 1) for i in (-1, 0, 1)) // 9) << s
                               for s in (16, 8, 0))
    if os.environ.get ("ACABADO_RUIDO"):
        rnd = random.Random (skin)
        for y in range (H):
            for x in range (W):
                d = rnd.randint (-3, 3)
                p = q[y][x]
                q[y][x] = sum (max (0, min (255, ((p >> s) & 255) + d)) << s for s in (16, 8, 0))
    return q

def recuadro (q, x0, y0, w=61, h=31):
    #  acabado: el color medio de un recuadro, por canal
    px = [q[y][x] for y in range (y0, y0 + h) for x in range (x0, x0 + w)]
    return sum (int (round (sum ((p >> s) & 255 for p in px) / float (len (px)))) << s for s in (16, 8, 0))

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

def origen (nombre):
    png = os.path.join (ROOT, "Source", "texturas", nombre.lower() + ".png")
    if not os.path.exists (png): return None
    sz = subprocess.run (["identify", "-format", "%w %h", png], capture_output=True, text=True).stdout.split()
    sw, sh = int (sz[0]), int (sz[1])
    raw = subprocess.run (["convert", png, "-colorspace", "Gray", "-depth", "8", "gray:-"], capture_output=True).stdout
    if len (raw) != sw * sh: return None
    return sw, sh, [list (raw[y * sw:(y + 1) * sw]) for y in range (sh)]

R, X0, Y0, N = 15, 100, 300, 200

def residuo (lv):
    #  acabado: lo que queda tras quitar la media de 31 px, en la ventana central de 200x200.
    col = [[0.0] * (W + 1) for _ in range (H + 1)]
    for y in range (H):
        for x in range (W): col[y + 1][x + 1] = lv[y][x] + col[y][x + 1] + col[y + 1][x] - col[y][x]
    caja = lambda y, x: (col[y + R + 1][x + R + 1] - col[y - R][x + R + 1]
                         - col[y + R + 1][x - R] + col[y - R][x - R]) / float ((2 * R + 1) ** 2)
    return [[lv[y][x] - caja (y, x) for x in range (X0, X0 + N)] for y in range (Y0, Y0 + N)]

def textura (nombre, q):
    lv = [[(((p >> 16) & 255) + ((p >> 8) & 255) + (p & 255)) / 3.0 for p in r] for r in q]
    res = residuo (lv)
    vals = [v for r in res for v in r]
    m = sum (vals) / len (vals)
    var = sum ((v - m) ** 2 for v in vals) / len (vals)
    sd = math.sqrt (var)
    pico = max (abs (v - m) for v in vals)
    fuente = origen (nombre)
    if fuente is None:
        mide ("%s: la textura es la de PixelLab" % nombre, False, "no esta Source/texturas/%s.png" % nombre.lower())
    else:
        #  textura: la baldosa en mosaico, un pixel suyo por pixel, juntas en espejo
        sw, sh, src = fuente
        espejo = lambda v, n: v % (2 * n) if v % (2 * n) < n else 2 * n - 1 - v % (2 * n)
        cx = [espejo (x, sw) for x in range (W)]
        cy = [espejo (y, sh) for y in range (H)]
        e = residuo ([[float (src[cy[y]][cx[x]]) for x in range (W)] for y in range (H)])
        b = [v for r in e for v in r]
        bm = sum (b) / len (b)
        a = [v - m for v in vals]
        b = [v - bm for v in b]
        sa = math.sqrt (sum (v * v for v in a)); sb = math.sqrt (sum (v * v for v in b))
        corr = sum (p * q for p, q in zip (a, b)) / (sa * sb) if sa > 0 and sb > 0 else 0.0
        mide ("%s: la textura es la de PixelLab" % nombre, corr >= 0.9,
              "correlacion %.2f con la baldosa en mosaico (>= 0.9)" % corr)
    mide ("%s: la textura se lee como material" % nombre, sd >= 3.0 and pico <= 32.0,
          "desviacion %.2f (>= 3)  pico %.1f (<= 32)" % (sd, pico))

tabla = parse()
for i, nombre in enumerate (SKINS):
    q = fondo (i)
    if q is None:
        mide ("%s: el fondo se dibuja" % nombre, False, "la app no escribio el PNG")
        continue
    m = media (q)
    yb, xb = int (H * 0.05), int (W * 0.2)
    se_ve = dE (recuadro (q, xb - 30, yb - 15), recuadro (q, W - 61, yb - 15))
    mide ("%s: el brillo se ve" % nombre, se_ve >= 1.0, "dE %.2f (liston 1.0)" % se_ve)

    fila = max (dE (min (r), max (r)) for r in (m[y] for y in range (0, H, 40)))
    mide ("%s: el brillo no ensucia" % nombre, fila <= 12.0, "fila %.2f dE (<= 12)" % fila)

    textura (nombre, q)

    ink = tabla[i]['ink']
    peor = min (ratio (ink, p) for r in q[::20] for p in r[::20])
    mide ("%s: la tinta sigue leyendose" % nombre, peor >= MIN_TEXT,
          "ratio %.2f (liston %.2f)" % (peor, MIN_TEXT))

print ("VEREDICTO: %s" % ("OK  el acabado de las cuatro carcasas se ve, no ensucia y no come contraste"
                          if not fallos else "MAL  " + ", ".join (fallos)))
sys.exit (1 if fallos else 0)

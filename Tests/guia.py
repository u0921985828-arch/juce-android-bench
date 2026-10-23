#!/usr/bin/env python3
"""LA TARJETA DE LA GUIA SE LEE, RESPIRA Y SEÑALA.

tour.py mide que cada paso apunte a algo y diga la verdad. Esto mide como se
ve: el aire de la tarjeta, la letra del parrafo y el anillo. Las siete reglas
salen de mirar los veinticuatro pasos a 393x851:

  AIRE     el aire bajo el texto es el de encima; la tarjeta mide lo que su
           parrafo y no lo que el mas largo de los veinticuatro.
  MARGEN   la tarjeta y la botonera no se pegan al filo de la pantalla.
  LETRA    parrafo de 15 o mas, interlinea de 1.4 o mas y contraste 7:1.
  TAPA     ni la tarjeta ni la botonera tapan lo que el anillo enseña.
  CORTE    el filo del hueco no parte un control ni un rotulo por la mitad.
  CHAPA    el numero del paso no cae encima de un control ni de un rotulo.
  ANILLO   lo que el anillo enseña tiene aire dentro: no lo roza, salvo
           que el vecino de fuera no deje el hueco.

Las medidas las publica la app (UiAudit::tourDockR y compañia), no se
adivinan en la foto.
"""
import json, os, shutil, subprocess, sys, tempfile, re
from concurrent.futures import ThreadPoolExecutor

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA, display_alive

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
PASOS = int (re.search (r"kTourPasos\s*=\s*(\d+)",
                        open (os.path.join (ROOT, "Source", "MainComponent.h"), encoding="utf8").read()).group (1))
PANTALLAS = ["393x851", "412x915"]
IDIOMAS   = ["es", "en", "zh", "ar"]

MARGEN, LETRA, INTERLINEA, CONTRASTE, AIRE_ANILLO = 8, 15.0, 1.4, 7.0, 4
TOL_AIRE = 8


def corre (paso, size, lang):
    casa = tempfile.mkdtemp (prefix="zati-guia-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG=lang,
                ZATI_OPEN="tour%d" % paso, DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    t, capa, cosas, fichas = None, None, [], []
    for l in out.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: j = json.loads (l)
        except Exception: continue
        if j.get ("apertura") == "tour": capa = j.get ("capa")
        elif "tarjeta" in j and "capa" in j: fichas.append (j)
        elif "tour" in j and "dock" in j: t = j
        elif "rotulo" in j or j.get ("kind") in ("button", "slider"):
            cosas.append (j)
    #  Lo que cae bajo una ficha de capa mayor no se ve: ni se corta ni roza.
    tapado = lambda c: any (f["capa"] != capa and f["capa"] > c.get ("capa", 0)
                            and f["x"] <= c["x"] + c["w"] / 2 < f["x"] + f["w"] and f["y"] <= c["y"] + c["h"] / 2 < f["y"] + f["h"]
                            for f in fichas)
    if t is not None:
        t["_cosas"] = [c for c in cosas if c.get ("capa") != capa and c.get ("w", 0) > 0 and c.get ("h", 0) > 0
                       and c.get ("on", 1) and not tapado (c)]
    return t


def R (v): return (v[0], v[1], v[0] + v[2], v[1] + v[3])
def vacio (r): return r[2] <= r[0] or r[3] <= r[1]
def corta (a, b): return (max (a[0], b[0]), max (a[1], b[1]), min (a[2], b[2]), min (a[3], b[3]))
def dentro (a, b, tol=1): return a[0] >= b[0] - tol and a[1] >= b[1] - tol and a[2] <= b[2] + tol and a[3] <= b[3] + tol


def lum (h):
    c = [int (h[i:i + 2], 16) / 255.0 for i in (len (h) - 6, len (h) - 4, len (h) - 2)]
    c = [x / 12.92 if x <= 0.03928 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


def contraste (a, b):
    la, lb = lum (a), lum (b)
    return (max (la, lb) + 0.05) / (min (la, lb) + 0.05)


def nombre (c): return c.get ("rotulo") or c.get ("sample") or c.get ("path", "?").split ("/")[-1]


fallas, n = [], 0
def mide (regla, ok, donde, detalle):
    global n
    n += 1
    if not ok: fallas.append ("  FALLA  %-7s %-22s %s" % (regla, donde, detalle))


if not display_alive():
    print ("guia: no hay pantalla %s" % PANTALLA); sys.exit (2)

casos = [(p, s, l) for s in PANTALLAS for l in IDIOMAS for p in range (PASOS)]
with ThreadPoolExecutor (6) as ex:
    res = list (ex.map (lambda c: corre (*c), casos))

for (p, size, lang), t in zip (casos, res):
    donde = "paso %d %s %s" % (p + 1, lang, size)
    if t is None:
        mide ("ARRANCA", False, donde, "la app no publico la guia"); continue
    W, H = (int (x) for x in size.split ("x"))
    dock, bar, btn, tit, tex = R (t["dock"]), R (t["bar"]), R (t["btn"]), R (t["titulo"]), R (t["texto"])
    foco = (t["focoX"], t["focoY"], t["focoX"] + t["focoW"], t["focoY"] + t["focoH"])
    cosas = t["_cosas"]

    arriba = tit[1] - dock[1]
    abajo = (btn[1] if dentro (btn, dock) else dock[3]) - tex[3]
    mide ("AIRE", arriba - 4 <= abajo <= arriba + TOL_AIRE, donde,
          "aire bajo el texto %d px, encima %d px" % (abajo, arriba))

    for nom, r in (("tarjeta", dock), ("botonera", bar)):
        if vacio (r): continue
        mide ("MARGEN", r[0] >= MARGEN and r[2] <= W - MARGEN and r[3] <= H - MARGEN, donde,
              "%s de %d a %d, abajo en %d (pantalla %dx%d)" % (nom, r[0], r[2], r[3], W, H))

    mide ("LETRA", t["letra"] >= LETRA and t["linea"] >= INTERLINEA * t["letra"] - 0.01
          and contraste (t["tinta"], t["fondo"]) >= CONTRASTE, donde,
          "letra %.1f, interlinea %.2f, contraste %.1f" % (t["letra"], t["linea"] / max (1e-6, t["letra"]),
                                                           contraste (t["tinta"], t["fondo"])))
    if vacio (foco): continue

    tapa = [nom for nom, r in (("tarjeta", dock), ("botonera", bar)) if not vacio (r) and not vacio (corta (foco, r))]
    mide ("TAPA", not tapa, donde, "el hueco queda debajo de " + ", ".join (tapa))

    cortadas = [nombre (c) for c in cosas
                if not vacio (corta (foco, R ((c["x"], c["y"], c["w"], c["h"]))))
                and not dentro (R ((c["x"], c["y"], c["w"], c["h"])), foco)
                and not dentro (foco, R ((c["x"], c["y"], c["w"], c["h"])))]
    mide ("CORTE", not cortadas, donde, "el filo parte %d: %s" % (len (cortadas), ", ".join (cortadas[:4])))

    if t["chapa"][2] > 0:
        ch = R (t["chapa"])
        pisa = [nombre (c) for c in cosas if not vacio (corta (ch, R ((c["x"], c["y"], c["w"], c["h"]))))]
        mide ("CHAPA", not pisa, donde, "la chapa pisa %s" % ", ".join (pisa[:3]))

    #  El aire que se pide por cada lado es AIRE_ANILLO, o el hueco que deja el vecino de fuera
    #  si es menor: dos filas pegadas no dejan sitio a un anillo sin partir la de al lado.
    rs = [R ((c["x"], c["y"], c["w"], c["h"])) for c in cosas]
    fuera = [r for r in rs if not dentro (r, foco, 0)] + [x for x in (dock, bar) if not vacio (x)]
    def hueco (r, lado):
        cab = {0: W - r[2], 1: r[0], 2: H - r[3], 3: r[1]}[lado]
        for o in fuera:
            if lado in (0, 1) and (o[3] <= r[1] or o[1] >= r[3]): continue
            if lado in (2, 3) and (o[2] <= r[0] or o[0] >= r[2]): continue
            d = {0: o[0] - r[2], 1: r[0] - o[2], 2: o[1] - r[3], 3: r[1] - o[3]}[lado]
            if d >= 0: cab = min (cab, d)
        return cab
    peor = None
    for r in rs:
        if not dentro (r, foco, 0): continue
        for lado, a in enumerate ((foco[2] - r[2], r[0] - foco[0], foco[3] - r[3], r[1] - foco[1])):
            pide = min (AIRE_ANILLO, hueco (r, lado))
            if peor is None or a - pide < peor[0]: peor = (a - pide, a, pide)
    if peor is not None:
        mide ("ANILLO", peor[0] >= 0, donde, "aire dentro del anillo %d px donde cabian %d" % (peor[1], peor[2]))

por = {}
for f in fallas:
    k = f.split()[1]; por[k] = por.get (k, 0) + 1
for f in fallas[:60]: print (f)
if len (fallas) > 60: print ("  ... y %d mas" % (len (fallas) - 60))
print ("por regla: " + ", ".join ("%s %d" % kv for kv in sorted (por.items())) if por else "por regla: ninguna")
print ("guia: %d comprobaciones, %d FALLA" % (n, len (fallas)))
sys.exit (1 if fallas else 0)

#!/usr/bin/env python3
"""EL DEDO DE LO QUE MAS SE TOCA (visual #5).

Pestanas de modulo, transporte y los switches M/S/ST de la mesa median 40, el
suelo de la casa. La auditoria pide 44 para lo que se toca sin mirar:

  1. De pie, donde el cristal lo paga (393x851, 412x915, 800x1280), las seis
     pestanas y el transporte miden 44 de alto.
  2. En esas pantallas M, S y ST miden 44 de ancho (el alto es el de la fila:
     las 16 filas entran en la ficha).
  3. Donde no cabe (360x640) nada baja de lo que tenia: transporte a 40.
"""
import json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN", os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati"))
PESTANAS   = ["PAD", "SEC", "CANCIÓN", "MEZCLA", "XY", "AJUSTES"]
TRANSPORTE = ["CARGAR", "REC", "PLAY"]
SWITCHES   = ["M", "S", "ST"]
DEDO = 44


def tamanos (size, hoja):
    casa = tempfile.mkdtemp (prefix="zati-dedo-")
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", ZATI_OPEN=hoja, DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        out = ""
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    vistos = {}
    for m in re.finditer (r'\{[^{}]*"text":"([^"]*)"[^{}]*\}', out):
        try: d = json.loads (m.group (0))
        except Exception: continue
        t = d.get ("text")
        if t in PESTANAS + TRANSPORTE + SWITCHES and t not in vistos and d.get ("w", 0) > 0:
            vistos[t] = (d["w"], d["h"])
    return vistos


def main():
    malas = []
    for size in ("393x851", "412x915", "800x1280"):
        t = tamanos (size, "mix")
        print ("%-9s %s" % (size, json.dumps (t, ensure_ascii=False)))
        for n in PESTANAS + TRANSPORTE:
            if n not in t: malas.append ("%s: %s no aparece" % (size, n))
            elif t[n][1] < DEDO: malas.append ("%s: %s mide %d de alto (%d)" % (size, n, t[n][1], DEDO))
        for n in SWITCHES:
            if n not in t: malas.append ("%s: %s no aparece en la mesa" % (size, n))
            elif t[n][0] < DEDO: malas.append ("%s: %s mide %d de ancho (%d)" % (size, n, t[n][0], DEDO))
    t = tamanos ("360x640", "")
    print ("%-9s %s" % ("360x640", json.dumps (t, ensure_ascii=False)))
    for n in TRANSPORTE:
        if t.get (n, (0, 0))[1] < 40: malas.append ("360x640: %s baja de 40" % n)
    if malas:
        for m in malas: print ("FALLA  " + m)
        sys.exit (1)
    print ("pestanas, transporte y switches de la mesa a 44 donde caben")

main()

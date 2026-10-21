#!/usr/bin/env python3
"""LOS LIENZOS EXISTEN PARA TALKBACK (accesibilidad#0).

La rejilla de pasos, el piano, la cancion, el XY, la curva del EQ y el
teclado se pintan enteros: sin manejador propio JUCE les da rol
`unspecified`, sin titulo ni acciones. Cada uno tiene que:

  1. presentarse como mando ajustable (rol slider) y con nombre;
  2. tener mas de una posicion, y anunciar algo distinto en la primera y en
     la ultima (el cursor se mueve y se oye donde esta);
  3. y el toque doble hace lo del dedo: llama UNA vez a su accion.
"""
import json, os, subprocess, sys, tempfile, shutil

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN", os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati"))
SLIDER = 5    # juce::AccessibilityRole::slider
NOMBRES = ("pasos", "piano", "cancion", "xy", "eq", "teclado")


def main():
    casa = tempfile.mkdtemp (prefix="zati-acc-")
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="393x851",
                ZATI_LANG="es", ZATI_OPEN="accesible", DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        out = ""
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    filas = {}
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ('{"accesible"'):
            try:
                d = json.loads (l); filas[d["accesible"]] = d
            except Exception: pass

    malas = []
    for n in NOMBRES:
        d = filas.get (n)
        if d is None: malas.append ("%s: la auditoria no lo mide" % n); continue
        print ("%-8s rol %2d  %-22s %4d posiciones  «%s» .. «%s»  toque %d" %
               (n, d["rol"], "«%s»" % d["titulo"], d["posiciones"], d["primero"], d["ultimo"], d["llamadas"]))
        if d["rol"] != SLIDER: malas.append ("1. %s: rol %d, tiene que ser slider" % (n, d["rol"]))
        if not d["titulo"]:    malas.append ("1. %s: sin nombre" % n)
        if d["posiciones"] < 2 or not d["primero"] or d["primero"] == d["ultimo"]:
            malas.append ("2. %s: el cursor no se mueve o no dice donde esta" % n)
        if d["llamadas"] != 1: malas.append ("3. %s: el toque doble llamo %d veces a su accion" % (n, d["llamadas"]))
    for m in malas: print ("  FALLA " + m)
    print ("FALLA" if malas else "los seis lienzos se recorren y se tocan con TalkBack")
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

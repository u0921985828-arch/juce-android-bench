#!/usr/bin/env python3
"""EL PRIMER ARRANQUE TRAE ALGO QUE TOCAR (producto#1).

La maquina abria con los 64 sonidos y el patron 1 vacio: PLAY no sonaba.

  1. Sin sesion, el patron 1 trae KICK, SNARE, HAT y un bajo del banco D
     (pads 49..64) con al menos una nota que no es la raiz.
  2. Y se guarda: tras guardar y volver a abrir, el patron es el mismo.
  3. DEMO esta en PROYECTOS y es de solo lectura: abre el compas con sonidos,
     GUARDAR lo copia con otro nombre sin escribir DEMO, y BORRAR no lo quita.
"""
import json, os, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN", os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati"))


def corre (casa, que):
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="393x851",
                ZATI_LANG="es", ZATI_OPEN=que, DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        return None
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ('{"demo') and (que != "demosolo") == l.startswith ('{"demo"'):
            try: return json.loads (l)
            except Exception: pass
    return None


def main():
    malas = []
    casa = tempfile.mkdtemp (prefix="zati-demo-")
    try:
        a = corre (casa, "demo")
        corre (casa, "salvo")
        sesion = [d for d, _, fs in os.walk (casa) if "state.xml" in fs]
        b = corre (casa, "demo")
        c = corre (casa, "demosolo")
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    if a is None or b is None or c is None:
        print ("demo: la auditoria no contesto"); return 1

    nombres = {g[3] for g in a["golpes"]}
    for n in ("KICK", "SNARE", "HAT"):
        if n not in nombres: malas.append ("1. el patron 1 no trae %s" % n)
    bajo = [g for g in a["golpes"] if 48 <= g[0] < 64]
    if not bajo: malas.append ("1. el patron 1 no trae bajo del banco D")
    elif not any (g[2] != 0 for g in bajo): malas.append ("1. el bajo no sale de la raiz")
    if not sesion: malas.append ("2. guardar no dejo sesion: la segunda vuelta seria otro primer arranque")
    if sorted (map (tuple, a["golpes"])) != sorted (map (tuple, b["golpes"])):
        malas.append ("2. al volver a abrir el patron cambia: %d golpes antes, %d despues" % (len (a["golpes"]), len (b["golpes"])))

    if not c["enlista"]: malas.append ("3. DEMO no sale en PROYECTOS")
    elif c["golpes"] < len (a["golpes"]) or c["sonidos"] < 16 or c["abierto"] != "DEMO":
        malas.append ("3. abrir DEMO trae %d golpes y %d sonidos (abierto: %r)" % (c["golpes"], c["sonidos"], c["abierto"]))
    if c["carpeta"]: malas.append ("3. GUARDAR escribio encima de DEMO")
    if c["enlista"] and c["guardado"] in ("", "DEMO"): malas.append ("3. GUARDAR sobre DEMO no lo copio con otro nombre (%r)" % c["guardado"])
    if not c["sigue"]: malas.append ("3. BORRAR quito DEMO de la lista")
    print ("demosolo: en lista %d, %d golpes y %d sonidos al abrir, guardado como %r, sigue %d (%s)"
           % (c["enlista"], c["golpes"], c["sonidos"], c["guardado"], c["sigue"], c["aviso"]))
    print ("demo: %d golpes en %d pasos (%s), bajo %d notas; reabierto %d" %
           (len (a["golpes"]), a["largo"], ", ".join (sorted (nombres)) or "nada", len (bajo), len (b["golpes"])))
    for m in malas: print ("  FALLA " + m)
    print ("FALLA" if malas else "el primer arranque trae un compas que suena y se guarda")
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

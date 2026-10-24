#!/usr/bin/env python3
# ============================================================================
#  LA FICHA DE INSTRUMENTO ABRE COMO QUEDA.
#
#  Del telefono: «la pantalla de instrumentos deberia de iniciar asi, no con la
#  barra a la derecha». Con la receta sin mover VOLVER no se ve, y la ficha le
#  guardaba igual una fila entera debajo: 40 px mas de alto, la barra en los
#  moviles donde eso pasa del tope, y al tocar un mando VOLVER subia al titulo
#  y la ficha encogia. Medido en 412x915 con el binario anterior: `vst` pide
#  758 y `vstm` 718.
#
#  La regla: lo que pide la ficha no depende de que VOLVER se vea, en ningun
#  tamano. Abrir y tocar un mando no mueve la tarjeta.
#
#      python3 Tests/ficha.py            (ZATI_BIN para otro binario)
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import PANTALLA, display_alive

TAMANOS = ["360x640", "393x851", "412x915", "344x882", "280x653"]


def tarjeta (size, ficha):
    casa = tempfile.mkdtemp (prefix="zati-ficha-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG="es", ZATI_OPEN=ficha)
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("tarjeta") == 1: return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes" % APP)
        return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes")
        return 1
    malas = []
    for size in TAMANOS:
        a, b = tarjeta (size, "vst"), tarjeta (size, "vstm")
        if a is None or b is None:
            malas.append ("%s: la app no publico la tarjeta" % size)
            continue
        barra = "con barra" if a["pedido"] > a["tope"] else "sin barra"
        print ("%-8s abre pidiendo %4d, tocada %4d, tope %4d  %s"
               % (size, a["pedido"], b["pedido"], a["tope"], barra))
        if a["pedido"] != b["pedido"]:
            malas.append ("%s: la ficha abre pidiendo %d y tras tocar un mando %d: "
                          "guarda la fila de VOLVER sin VOLVER" % (size, a["pedido"], b["pedido"]))
    for m in malas: print ("FALLA  " + m)
    print ("OK  la ficha de instrumento abre con el alto que tendra" if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

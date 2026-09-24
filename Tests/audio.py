#!/usr/bin/env python3
# ============================================================================
#  QUE SE DECIDE CON LO QUE CONTESTA EL TELEFONO.
#
#  La sonda del carril rapido -`AudioPath::probeFastPath`- abre hasta seis
#  flujos de AAudio al arrancar y de su respuesta salen los DOS numeros que el
#  parche de JUCE lee antes de abrir el flujo de verdad: la `usage` que se pide
#  y si se salta la tentativa en float. Son lo unico de todo ese fichero que
#  cambia el sonido de la maquina, y no los medía nadie: la sonda vive tras
#  `#if JUCE_ANDROID`, o sea que en el banco NUNCA se ejecutaba y nunca podia
#  fallar. Una regla que no puede fallar no es una regla.
#
#  Asi que se partio en dos. El contacto con AAudio -que la llamada lleve
#  callback de datos y arranque el flujo antes de leer el veredicto- solo lo
#  comprueba un telefono y se declara como limite en el comentario. La DECISION
#  se saco a `AudioPath::concluye`, que es pura, y es lo que se mide aqui con
#  seis tablas sinteticas.
#
#  Lo que cada tabla existe para cazar:
#
#  - `ninguno abre`: sin respuesta no se le toca nada a JUCE. Un `usage`
#    distinto de cero aqui seria cambiar el flujo de verdad sin ninguna medida
#    detras.
#  - `compartido siempre`: el telefono de la bitacora, `via compartida
#    MEZCLADOR` con `mmap disponible`. Tambien tiene que salir usage 0.
#  - `game float exclusiva` y `game 16b exclusiva`: los dos casos en los que SI
#    se le tuerce el brazo a JUCE, y con que numeros.
#  - `abre exclusiva y no arranca`: la regla que esta tanda anade. AAudio puede
#    conceder el constructor y negar el START, y leer el modo de reparto antes
#    del START es leer una intencion. Gana el que arranco.
#  - `sin isMMapUsed`: si el simbolo oculto no esta, no decimos saberlo.
#
#      python3 Tests/audio.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

#  La pantalla que se comprueba es la que se usa. Ver nuevo.py.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

kGAME = 14

#  Lo esperado, tabla a tabla. Solo los campos que DECIDEN: `usage` e `i16` son
#  los que viajan al flujo de verdad, `excl` y `gano` son de que fila salieron.
ESPERADO = {
    "ninguno abre":                {"ran": 0, "excl": 0, "usage": 0,     "i16": 0, "gano": -1},
    "compartido siempre":          {"ran": 1, "excl": 0, "usage": 0,     "i16": 0, "gano": 0},
    "game float exclusiva":        {"ran": 1, "excl": 1, "usage": kGAME, "i16": 0, "gano": 2},
    "game 16b exclusiva":          {"ran": 1, "excl": 1, "usage": kGAME, "i16": 1, "gano": 3},
    "abre exclusiva y no arranca": {"ran": 1, "excl": 0, "usage": 0,     "i16": 0, "gano": 2},
    "sin isMMapUsed":              {"ran": 1, "excl": 0, "usage": 0,     "i16": 0, "gano": 0,
                                    "mmap": 0},
}


def corre():
    casa = tempfile.mkdtemp (prefix="zati-audio-")
    try:
        env = dict (os.environ, DISPLAY=PANTALLA)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_AUDIO": "1"})
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "audio" in d:
            filas[d["audio"]] = d
    return filas


def main():
    if not os.path.exists (APP):
        print ("no hay binario: cmake --build build"); return 1

    filas = corre()
    fallos = []

    for que, quiero in ESPERADO.items():
        d = filas.get (que)
        if d is None:
            fallos.append ("%s: la app no volco esta tabla" % que)
            continue

        for campo, valor in quiero.items():
            if d.get (campo) != valor:
                fallos.append ("%s: %s = %s y se esperaba %s"
                               % (que, campo, d.get (campo), valor))

        print ("%-30s ran %d  excl %d  mmap %d  gano %2d  usage %2d  i16 %d   %s"
               % (que, d["ran"], d["excl"], d["mmap"], d["gano"], d["usage"],
                  d["i16"], d["texto"]))

    #  Y LA COMPROBACION QUE NO ES POR TABLA: sin exclusiva no se le toca nada
    #  a JUCE, NUNCA. Escrita aparte porque es la invariante, y una invariante
    #  comprobada solo dentro de la lista de arriba se pierde el dia que
    #  alguien anada una tabla y se olvide de ponerle los dos ceros.
    for que, d in filas.items():
        if not d["excl"] and (d["usage"] != 0 or d["i16"] != 0):
            fallos.append ("%s: sin exclusiva, y sale usage %d i16 %d - eso cambia "
                           "el flujo de verdad sin ninguna medida detras"
                           % (que, d["usage"], d["i16"]))

    print()
    if fallos:
        for f in fallos: print ("FALLA  " + f)
        return 1
    print ("las %d tablas deciden lo que tienen que decidir" % len (ESPERADO))
    return 0


sys.exit (main())

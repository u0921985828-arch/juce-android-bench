#!/usr/bin/env python3
# ============================================================================
#  GUARDAR SIN HUECOS.
#
#  Lo que la persona no puede recuperar si se pierde, medido donde se pierde:
#  en disco, y en el instante entre dos llamadas al sistema. Tribunal 2026-09,
#  hallazgos 7.1 a 7.5 y 8.4.
#
#   1. EL RELEVO. `juce::File::moveFileTo` BORRA el destino y despues renombra,
#      asi que entre las dos llamadas no existe ni el viejo ni el nuevo. Se
#      corre la app bajo strace y se cuenta cada unlink de un destino seguido
#      de un rename encima: tiene que ser cero, con al menos nueve relevos
#      (tres de estado, tres de texto y tres de WAV) para que el cero diga algo.
#   2. EL TEMPORAL SE RESCATA: state.xml.tmp validado y sin state.xml vuelve a
#      ser la sesion.
#   3. EL ESTADO VA DELANTE DE LOS BORRADOS: tras un troceado en 8, los 7 pads
#      que pasan a salir del pad 0 lo dicen en disco cuando sus WAV ya no
#      estan. Antes el estado llegaba hasta 20 s tarde.
#   4. EL MISMO ESTADO NO SE ESCRIBE DOS VECES.
#   5. UNA ESCRITURA QUE FALLA NO DICE «GUARDADO».
#   6. EL PROYECTO ABIERTO NO SE REESCRIBE AL IRSE AL FONDO.
#
#      python3 Tests/guardado.py
# ============================================================================
import json, os, re, shutil, subprocess, sys, tempfile

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  Nueve relevos escribe la sonda sobre fichero existente, mas los del
#  troceado y el autosave: menos de nueve renames a un destino es que la
#  sonda no llego a correr, y entonces el cero de huecos no vale nada.
MIN_RELEVOS = 9

UNLINK = re.compile (r'unlink(?:at)?\((?:AT_FDCWD, )?"([^"]+)"')
RENAME = re.compile (r'rename(?:at2?)?\((?:AT_FDCWD, )?"([^"]+)", (?:AT_FDCWD, )?"([^"]+)"')


def corre (casa):
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es", DISPLAY=PANTALLA,
                ZATI_GUARDADO="1")
    traza = os.path.join (casa, "strace.txt")
    out = subprocess.run (["strace", "-f", "-qq", "-o", traza,
                           "-e", "trace=unlink,unlinkat,rename,renameat,renameat2", APP],
                          env=env, capture_output=True, text=True, timeout=600).stdout
    fila = None
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ('{"guardado"'):
            fila = json.loads (l)
    with open (traza, errors="replace") as f:
        lineas = f.read().splitlines()
    return fila, lineas


def huecos (lineas):
    """Destinos borrados y luego tapados por un rename: cada uno es un instante
    sin fichero. Solo cuentan los unlink que tuvieron exito (`= 0`)."""
    borrados, n, relevos = set(), 0, 0
    for l in lineas:
        m = UNLINK.search (l)
        if m and l.rstrip().endswith ("= 0"):
            borrados.add (m.group (1))
            continue
        m = RENAME.search (l)
        if m and l.rstrip().endswith ("= 0"):
            dest = m.group (2)
            #  Solo los DESTINOS: un temporal que se borra y se vuelve a crear
            #  no deja a nadie sin fichero.
            if os.path.basename (dest) in ("state.xml", "relevo.txt", "relevo.wav") \
               or re.match (r"pad\d\d\.wav$", os.path.basename (dest)):
                relevos += 1
                if dest in borrados:
                    n += 1
            borrados.discard (dest)
    return n, relevos


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if shutil.which ("strace") is None:
        sys.exit ("falta strace: sin el no hay forma de ver el instante entre dos llamadas")

    malas = []
    casa = tempfile.mkdtemp (prefix="zati-guardado-")
    try:
        r, lineas = corre (casa)
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    if r is None:
        print ("FALLA  la sonda ZATI_GUARDADO no contesto"); return 1

    n, relevos = huecos (lineas)
    print ("  relevos sobre un destino: %d (minimo %d); instantes sin fichero: %d" % (relevos, MIN_RELEVOS, n))
    if relevos < MIN_RELEVOS:
        malas.append ("relevo: solo %d renames a un destino, la sonda no llego a medir" % relevos)
    if n != 0:
        malas.append ("relevo: %d veces se borro el destino antes de renombrar encima" % n)

    print ("  temporal rescatado: %s" % ("si" if r["rescata"] else "NO"))
    if r["rescata"] != 1:
        malas.append ("rescate: state.xml.tmp validado no vuelve a ser la sesion")

    t = r["trozos"] - 1
    print ("  troceado en %d: %d/%d pads dicen en disco que salen del 0, %d/%d WAV borrados"
           % (r["trozos"], r["estado_al_dia"], t, r["wav_borrados"], t))
    if r["wav_borrados"] != t:
        malas.append ("troceado: el escritor no borro los %d WAV que sobran (%d)" % (t, r["wav_borrados"]))
    if r["estado_al_dia"] != t:
        malas.append ("troceado: el estado en disco no dice que %d pads salen del 0 (%d) con sus WAV ya borrados"
                      % (t, r["estado_al_dia"]))

    print ("  mismo estado dos veces: %d escritos, %d ahorrados" % (r["escritos"], r["iguales"]))
    if r["escritos"] > 1 or r["iguales"] < 1:
        malas.append ("estado igual: %d escrituras y %d ahorros, se esperaba como mucho 1 y al menos 1"
                      % (r["escritos"], r["iguales"]))

    print ("  escritura que falla: devuelve %d, apunta GUARDADO %d" % (r["falla_devuelve"], r["falla_apunta"]))
    if r["falla_devuelve"] != 0 or r["falla_apunta"] != 0:
        malas.append ("falla: un rename que no ocurre devuelve %d y apunta GUARDADO %d"
                      % (r["falla_devuelve"], r["falla_apunta"]))

    print ("  project.xml del proyecto abierto tras irse al fondo: %s"
           % ("intacto" if r["proyecto_intacto"] == 1 else "REESCRITO"))
    if r["proyecto_intacto"] != 1:
        malas.append ("proyecto: autosave reescribe project.xml sin sus muestras")

    for m in malas: print ("FALLA ", m)
    print ("VEREDICTO:", "OK" if not malas else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

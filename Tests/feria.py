#!/usr/bin/env python3
# ============================================================================
#  LA FERIA, HALLAZGO POR HALLAZGO.
#
#  Cinco jueces jugaron con la app sin saber nada de ella (feria 2026-10) y
#  escribieron lo que les fallo. Cada regla es uno de esos fallos medido por
#  el camino que tomo el juez, con ZATI_FERIA (ver auditFeria):
#
#   a. RUTEO. El primer efecto sin reparto entra en los 64 pads; con reparto
#      solo en el elegido (J1, J2, J5: la reverb no llegaba al fichero).
#   b. XY. Lo que encendio la persona sigue encendido al soltar; lo que
#      encendio el toque se apaga al soltar (J3).
#   c. DESHACER no se mueve cuando aparece REHACER (J3).
#   d. LA REJILLA no salta al primer toque (J2, fotos 03 -> 04).
#   e. EL PAD ELEGIDO vuelve con el estado (J4).
#   f. TEXTO BASURA no mueve el mando: «abc» y vacio dejan el valor (J4).
#   g. EL SEGURO? que caduca desarma y lo dice (J4, fotos 29-33).
#   h. ATRAS cierra lo de arriba y despues no hace nada (J3).
#   i. EL LIMITADOR se ve: master +12 y pads x4 escriben LIM en el cristal (J2).
#   j. BORRAR otro proyecto con uno abierto no tira la app (B1, J4). Con
#      ZATI_FERIA_APP apuntando al binario ASan, ademas, cero errores.
#   k. UN CAMBIO SE GUARDA AL SOLTAR: el tempo llega a disco 1.5 s despues del
#      ultimo toque, y sin toque no (el control), que el estado entero era
#      cada 20 s y J4 espero 8.
#
#      python3 Tests/feria.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_FERIA_APP") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre (casa):
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="393x851", ZATI_LANG="es", DISPLAY=PANTALLA,
                ZATI_FERIA="1", ASAN_OPTIONS="detect_leaks=0:halt_on_error=0")
    p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=900)
    fila = None
    for l in p.stdout.splitlines():
        l = l.strip()
        if l.startswith ('{"feria"'):
            fila = json.loads (l)
    asan = p.stderr.count ("ERROR: AddressSanitizer")
    return fila, asan


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build-rel)")
    casa = tempfile.mkdtemp (prefix="zati-feria-")
    try:
        r, asan = corre (casa)
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    if r is None:
        print ("FALLA  la sonda ZATI_FERIA no contesto (errores ASan: %d)" % asan); return 1

    malas = []
    def regla (nombre, ok, texto):
        print ("  %-8s %s  %s" % (nombre, "ok   " if ok else "FALLA", texto))
        if not ok: malas.append (nombre)

    regla ("ruteo", r["ruteo_todos"] == 64 and r["ruteo_uno"] == 2 and r["ruteo_elegido"] == 1,
           "sin reparto %d pads al canal (64); con reparto %d (2: el que habia y el elegido)"
           % (r["ruteo_todos"], r["ruteo_uno"]))
    regla ("xy", r["xy_sigue"] == 1 and r["xy_entra"] == 1 and r["xy_sale"] == 1,
           "encendido por la persona sigue %d; el toque enciende %d y al soltar apaga %d"
           % (r["xy_sigue"], r["xy_entra"], r["xy_sale"]))
    regla ("deshacer", r["undo_con"] == r["undo_sin"],
           "DESHACER %s con REHACER, %s sin" % (r["undo_con"], r["undo_sin"]))
    regla ("rejilla", r["grid_con"] == r["grid_sin"],
           "rejilla %s sin paso elegido, %s con" % (r["grid_sin"], r["grid_con"]))
    regla ("pad", r["pad_vuelve"] == 9, "pad elegido tras deshacer el estado: %d (9)" % r["pad_vuelve"])
    regla ("texto", r["bpm_basura"] == 127 and r["bpm_vacio"] == 127 and r["bpm_cifra"] == 90
                    and abs (r["master_basura"] - 0.5) < 1e-9,
           "BPM 127 + abc = %g, + vacio = %g, + 90 = %g; master 0.5 + abc = %g"
           % (r["bpm_basura"], r["bpm_vacio"], r["bpm_cifra"], r["master_basura"]))
    regla ("caduca", r["caduca_desarma"] == 1 and r["caduca_dice"] == 1,
           "SEGURO? caducado: desarma %d, lo dice %d" % (r["caduca_desarma"], r["caduca_dice"]))
    regla ("atras", r["atras1"] == 1 and r["atras2"] == 0 and r["atras_cerro"] == 1,
           "atras con SET abierto %d y cerrado %d; despues %d"
           % (r["atras1"], r["atras_cerro"], r["atras2"]))
    regla ("lim", r["sin_aparato"] == 1 and r["recorte"] > 1.0012 and r["lim_db"] > 0.01,
           "recorte %.3f (sin aparato %d), el cristal dice LIM -%.1f dB"
           % (r["recorte"], r["sin_aparato"], r["lim_db"]))
    regla ("borrar", r["fila_uno"] >= 0 and r["borra_fuera"] == 1 and r["borra_dice"] == 1
                     and r["borra_abierto"] == 1 and asan == 0,
           "UNO borrado con DOS abierto: carpeta fuera %d, lo dice %d, DOS sigue %d, errores ASan %d"
           % (r["borra_fuera"], r["borra_dice"], r["borra_abierto"], asan))
    regla ("guardado", r["bpm_antes"] == 120 and r["bpm_sin_soltar"] == 120 and r["bpm_tras_soltar"] == 131,
           "tempo en disco: %g antes, %g a los 2 s sin soltar (control), %g a los 2 s de soltar"
           % (r["bpm_antes"], r["bpm_sin_soltar"], r["bpm_tras_soltar"]))

    for m in malas: print ("FALLA ", m)
    print ("VEREDICTO:", "OK" if not malas else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

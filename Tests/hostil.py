#!/usr/bin/env python3
# ============================================================================
#  UN PROYECTO ROTO NO TUMBA LA APP.
#
#  project.xml es un fichero que la persona puede tocar: copiarlo de otro
#  telefono, editarlo a mano, quedarse a medias si el disco se llena. La app
#  nunca escribe un <PAD i> fuera de rango, pero los lee: finishProjectOpen
#  contaba los pads sin sonido indexando uiSample con el `i` del fichero tal
#  cual, y con i=2147483647 la app caia al abrirlo (SIGSEGV, exit 139;
#  auditoria 2026-10, estabilidad#0). La restauracion de sesion ya lo
#  guardaba con isPositiveAndBelow; la apertura de proyecto no.
#
#  Los catorce casos son los de la auditoria (xml de be45698), escritos aqui
#  para que el banco no dependa de una carpeta de fuera: indices de PAD y de
#  BANK fuera de rango por los dos lados, numeros que no son numeros, una
#  version futura, 20000 pads, 10000 pasos, un fichero truncado y una raiz
#  que no es ZATI. Cada uno se abre con ZATI_VIEJO -el mismo camino que
#  Tests/session.py usa para los proyectos de otra epoca- en una casa limpia.
#  La regla es una: la app sale con 0 y llega a contar el fichero.
#
#      python3 Tests/hostil.py
# ============================================================================
import os, shutil, subprocess, sys, tempfile

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

CAB = '<?xml version="1.0" encoding="UTF-8"?>\n'

def casos():
    pasos = " ".join (str (i) for i in range (10000))
    return {
        "x01-pad-i-9999":     CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="9999" name="LEJOS" has="1"/></PADS></ZATI>',
        "x02-pad-i-negativo": CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="-3" name="LEJOS" has="1"/></PADS></ZATI>',
        "x03-nan-e-inf":      CAB + '<ZATI version="1" bpm="nan" swing="1e300" selectedPattern="-7" selectedPad="-9">'
                                    '<PADS><PAD i="0" name="X" pitch="1e300" gain="-inf" pan="nan" corte="-1"/></PADS>'
                                    '<BANKS><BANK i="0" len="-5" steps="x y z"/></BANKS></ZATI>',
        "x04-bank-i-gigante": CAB + '<ZATI version="1" bpm="120.0"><BANKS><BANK i="2147483647" len="99999" steps="1 1 1"/>'
                                    '<BANK i="-1" len="16" steps="1"/></BANKS></ZATI>',
        "x05-version-futura": CAB + '<ZATI version="999" bpm="120.0"><PADS><PAD i="0" name="FUTURO" canal="99" '
                                    'sends="9,9,9,9,9,9,9,9,9,9,9,9"/></PADS><song bars="100000" mode="7" '
                                    'lane0="1,-1000,-99999,2147483647" lane9="1" bmudos="-1,-1,-1,-1"/></ZATI>',
        "x06-nombre-100k":    CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="0" name="' + "N" * 100000 + '"/></PADS></ZATI>',
        "x07-20000-pads":     CAB + '<ZATI version="1" bpm="120.0"><PADS>'
                                    + "".join ('<PAD i="%d" name="P%d" has="1"/>' % (i % 70, i) for i in range (20000))
                                    + '</PADS></ZATI>',
        "x08-steps-10000":    CAB + '<ZATI version="1" bpm="120.0"><BANKS><BANK i="0" len="16" steps="' + pasos + '"/></BANKS></ZATI>',
        "x09-truncado":       CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="0" na',
        "x10-raiz-otra":      CAB + '<NOZATI version="1"><PADS><PAD i="0" has="1"/></PADS></NOZATI>',
        "x11-bloques":        CAB + '<ZATI version="1" bpm="120.0"><song bars="8" mode="1" '
                                    'bloques="9999:-5:2147483647:1;-1:-1:-1:-1;0:0:0:0:0:0:0"/></ZATI>',
        "x12-pad-i-64":       CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="64" name="LEJOS" has="1"/></PADS></ZATI>',
        "x13-pad-i-intmax":   CAB + '<ZATI version="1" bpm="120.0" selectedPattern="0"><PADS><PAD i="2147483647" name="LEJOS" has="1"/></PADS></ZATI>',
        "x14-pad-i-1e8":      CAB + '<ZATI version="1" bpm="120.0"><PADS><PAD i="100000000" name="LEJOS" has="1"/></PADS></ZATI>',
    }

def abre (nombre, xml):
    casa = tempfile.mkdtemp (prefix="zati-hostil-")
    try:
        dir_ = os.path.join (casa, "viejos")
        os.makedirs (dir_)
        with open (os.path.join (dir_, nombre + ".xml"), "w", encoding="utf8") as f:
            f.write (xml)
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "DISPLAY": PANTALLA, "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915",
                     "ZATI_LANG": "es", "ZATI_VIEJO": dir_})
        try:
            r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
        except subprocess.TimeoutExpired:
            return None, False
        return r.returncode, ('"viejo"' in r.stdout)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario en %s" % APP)
    malas = []
    for nombre, xml in casos().items():
        rc, contado = abre (nombre, xml)
        bien = rc == 0 and contado
        print ("%-20s exit %-5s %s   %s" % (nombre, rc, "contado" if contado else "sin contar",
                                            "ok" if bien else "FALLA"))
        if not bien:
            malas.append ("%s: exit %s%s" % (nombre, rc, "" if contado else ", no llego a contar el fichero"))
    print()
    for m in malas: print ("FALLA ", m)
    print ("VEREDICTO:", "OK" if not malas else "%d FALLA" % len (malas))
    return 1 if malas else 0

if __name__ == "__main__":
    sys.exit (main())

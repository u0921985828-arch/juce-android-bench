#!/usr/bin/env python3
"""LO QUE EL SISTEMA YA DECIDIO (accesibilidad #28, #29 y #31).

Tres ajustes de Android que la app ignoraba: el tiempo de pulsacion larga
(fijo en 420 ms), «quitar animaciones» (el movimiento venia encendido) y la
zona del gesto de atras (solo se restaban las barras). En el escritorio no
existen: ZATI_SISTEMA los convierte en ENTRADA, como ZATI_INSETS los margenes.

  1. Ningun manejador lee el 420 escrito: todos piden `Metrics::pulsacion()`.
  2. Con el sistema en 900 ms, la pulsacion vale 900; sin decir nada, 420.
  3. Con «quitar animaciones» y sin preferencia guardada, el movimiento nace
     apagado, y un pad que se refresca con el destello a medias no lo ensena.
  4. Con gestos de 30 a cada lado y la curva del EQ en la cara, se pide excluir los
     filos sobre lo que se arrastra en horizontal: dentro de la franja, y no
     mas de 200 de alto por lado (lo que Android concede). Sin gestos, nada.
"""
import glob, json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN", os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati"))
W, H, FILO = 393, 851, 30


def corre (sistema):
    casa = tempfile.mkdtemp (prefix="zati-sis-")
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="%dx%d" % (W, H),
                ZATI_LANG="es", ZATI_OPEN="sistema", DISPLAY=PANTALLA)
    if sistema: env["ZATI_SISTEMA"] = sistema
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        out = ""
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ('{"sistema"'):
            try: return json.loads (l)
            except Exception: pass
    return None


def main():
    malas = []

    fijos = []
    for f in sorted (glob.glob (os.path.join (ROOT, "Source", "*.h")) + glob.glob (os.path.join (ROOT, "Source", "*.cpp"))):
        if os.path.basename (f) == "ZatiLookAndFeel.h": continue
        for n, l in enumerate (open (f, encoding="utf8"), 1):
            codigo = l.split ("//")[0]
            if re.search (r"\bholdMs\b", codigo) and "pulsacionLargaMs" not in codigo:   # el respaldo si no contesta
                fijos.append ("%s:%d" % (os.path.basename (f), n))
    print ("manejadores con el 420 escrito: %d %s" % (len (fijos), " ".join (fijos)))
    if fijos: malas.append ("%d manejadores leen Metrics::holdMs y no la del sistema" % len (fijos))

    con = corre ("pulsacion=900;animacion=0;gestos=0,%d,0,%d" % (FILO, FILO))
    sin = corre (None)
    if con is None or sin is None:
        print ("FALLA  la app no publico la linea del sistema (con %s, sin %s)" % (con is not None, sin is not None))
        sys.exit (1)
    print ("con el sistema: %s" % json.dumps (con))
    print ("sin decir nada: %s" % json.dumps (sin))

    if con.get ("pulsacion") != 900: malas.append ("pulsacion %s con el sistema en 900" % con.get ("pulsacion"))
    if sin.get ("pulsacion") != 420: malas.append ("pulsacion %s sin sistema (420)" % sin.get ("pulsacion"))
    if con.get ("movimiento") != 0:  malas.append ("con «quitar animaciones» el movimiento nace %s" % con.get ("movimiento"))
    if sin.get ("movimiento") != 1:  malas.append ("sin sistema el movimiento nace %s" % sin.get ("movimiento"))
    if con.get ("destello_quieto", 1) > 0.0:
        malas.append ("con el movimiento apagado el pad ensena destello %s" % con.get ("destello_quieto"))

    zonas = con.get ("excluye", [])
    if not zonas: malas.append ("con gestos y la curva del EQ en la cara no se excluye nada")
    alto = {"izq": 0, "der": 0}
    for x, y, w, h in zonas:
        if x >= 0 and x + w <= FILO:          alto["izq"] += h
        elif x >= W - FILO and x + w <= W:    alto["der"] += h
        else: malas.append ("zona %s fuera de la franja del gesto" % [x, y, w, h])
    for lado, a in alto.items():
        if a > 200: malas.append ("%d de alto excluidos a la %s (Android concede 200)" % (a, lado))
    if sin.get ("excluye"): malas.append ("sin gestos se excluye %s" % sin.get ("excluye"))

    if malas:
        for m in malas: print ("FALLA  " + m)
        sys.exit (1)
    print ("la pulsacion, el movimiento y el gesto de atras son los del sistema")

main()

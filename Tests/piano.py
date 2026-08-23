#!/usr/bin/env python3
# ============================================================================
#  EL PIANO ROLL, Y SOBRE TODO EL COMPAS.
#
#  Esta pagina fallo CINCO veces seguidas por el mismo sitio y ninguna de las
#  seis reglas del banco pudo verlo, porque los cinco son fallos de INDICE y
#  no de geometria: un piano que escribe en el compas de al lado se maqueta
#  perfecto, no solapa nada, no corta ningun rotulo y esta traducido.
#
#  Los cinco, por orden de aparicion:
#
#  1. onCelda pasaba la COLUMNA (0..15) donde pianoCellToggled espera el PASO
#     del patron. En el compas 1 la nota se escribia dieciseis pasos antes.
#  2. La tapa de compas llamaba solo a refreshStepGrid, asi que en la pagina
#     del PIANO cambiar de compas movia selectedBar y dejaba la vista en el
#     compas anterior. La FICHA no es la PAGINA - el mismo fallo que ya tuvo
#     el cabezal.
#  3. refreshPiano vaciaba las celdas DENTRO del bucle de columnas validas,
#     asi que las de la derecha se quedaban con lo que hubiera antes.
#  4. Y el cols = jmin (16, jmax (1, len - base)) daba UNA columna donde
#     tocaban CERO, o sea que garantizaba quince columnas viejas.
#  5. La goma seguia pasando la columna despues de que onCelda ya estuviera
#     arreglada: borrar en el compas 1 se llevaba la nota del compas 0.
#
#  Los cinco dan el mismo sintoma - "la rejilla no responde" - y por eso se
#  miden juntos. Y se miden por el CAMINO DE VERDAD: la tapa de compas se
#  pulsa (barButtons[1]->onClick) y la nota se escribe por el gesto de la
#  rejilla (pianoGrid.onCelda), no llamando a pianoCellToggled por dentro,
#  que es justo el sitio donde ninguno de los cinco existia.
#
#  Mas lo que ya se medira antes y nadie juzgaba: acorde, quitar la raiz,
#  cambiar de pad saltando huecos, EUCLIDES y que oir una tecla no afine el
#  pad. Los numeros los imprimia la app desde hace tiempo; sin este fichero
#  una regresion ahi no fallaba, se publicaba.
#
#      python3 Tests/piano.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (
        ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre():
    casa = tempfile.mkdtemp (prefix="zati-piano-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": "sec", "ZATI_PIANO": "1"})
        r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for linea in r.stdout.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "piano" in d:
            filas.setdefault (d["piano"], []).append (d)
    return filas, r.stdout


fallos = []
hechas = []
def mide (nombre, ok, texto):
    hechas.append (nombre)
    print (("  OK   " if ok else "  FALLA") + "  " + nombre + "  " + texto)
    if not ok:
        fallos.append (nombre)


filas, bruto = corre()
if not filas:
    print ("sin volcado: la app no imprimio nada con ZATI_PIANO=1")
    print (bruto[-2000:])
    sys.exit (1)

def una (k):
    v = filas.get (k)
    return v[0] if v else None

#  --- lo que ya se imprimia y nadie juzgaba -------------------------------
d = una ("acorde")
if d is None:
    mide ("acorde", False, "no salio")
else:
    mide ("acorde", sorted (d["notas"]) == [0, 4, 7],
           "triada por la rejilla: " + str (d["notas"]))

d = una ("sin raiz")
#  Quitar la raiz asciende la siguiente: quedan dos notas y ninguna es el 0.
mide ("sin raiz", d is not None and sorted (d["notas"]) == [4, 7],
       "" if d is None else str (d["notas"]))

d = una ("pads")
#  Un kit de tres sonidos -0, 5 y 33- y las tapas saltan los sesenta huecos.
mide ("pads", d is not None and (d["tras1"], d["tras2"], d["banco2"], d["atras"]) == (5, 33, 2, 5),
       "" if d is None else "0 -> %d -> %d (banco %d) -> %d" % (d["tras1"], d["tras2"], d["banco2"], d["atras"]))

esperado = {5: [0, 4, 7, 10, 13], 4: [0, 4, 8, 12], 3: [0, 6, 11]}
for d in filas.get ("euclides", []):
    n = d["golpes"]
    mide ("euclides %d" % n, d["pasos"] == esperado.get (n),
           str (d["pasos"]))

d = una ("teclado")
#  Pasear doce semitonos por el teclado no puede reafinar el pad.
mide ("teclado", d is not None and abs (d["pitch_antes"] - d["pitch_despues"]) < 1e-6,
       "" if d is None else "pitch %.2f -> %.2f" % (d["pitch_antes"], d["pitch_despues"]))

#  --- el compas, que es lo que motivo este fichero ------------------------
d = una ("compas")
if d is None:
    mide ("compas", False, "no salio")
else:
    #  La tapa deja selectedBar en 1 y la nota cae en el paso 19 -compas 1,
    #  columna 3- con el semitono 5. El testigo del compas 0 sigue en su sitio
    #  con el 9: si la nota nueva lo hubiera pisado, nota3 valdria 5.
    mide ("compas sel",  d["sel"] == 1, "selectedBar %d" % d["sel"])
    mide ("compas escribe", d["paso19"] == 1 and d["nota19"] == 5,
           "paso 19 puesto=%d nota=%d" % (d["paso19"], d["nota19"]))
    mide ("compas testigo", d["paso3"] == 1 and d["nota3"] == 9,
           "paso 3 puesto=%d nota=%d" % (d["paso3"], d["nota3"]))

d = una ("vista")
#  Y la vista tiene que haberse repintado con la tapa: la columna 3 lleva el 5
#  del compas 1 y no el 9 del 0. Este es el fallo 2 y el unico que se ve.
mide ("compas vista", d is not None and d["col3"] == 5,
       "" if d is None else "columna 3 = %d (5 es el compas 1, 9 el 0)" % d["col3"])

d = una ("goma")
mide ("goma", d is not None and d["paso19"] == 0 and d["paso3"] == 1,
       "" if d is None else "borrado 19=%d, testigo 3=%d" % (d["paso19"], d["paso3"]))

d = una ("encoge")
#  El patron pasa de 32 a 16 pasos con el compas 1 puesto. refreshPiano tiene
#  que acotar el compas a 0 y dejar UNA sola nota dibujada -el testigo-, no
#  quince columnas del compas que ya no existe.
if d is None:
    mide ("encoge", False, "no salio")
else:
    mide ("encoge acota", d["sel"] == 0, "selectedBar %d" % d["sel"])
    mide ("encoge limpia", d["puestas"] == 1 and d["col3"] == 9,
           "%d celdas puestas, columna 3 = %d" % (d["puestas"], d["col3"]))

#  --- el gesto, que hasta ahora no se podia medir -------------------------
#
#  Las dos se miden por pianoGrid.gesto -pixeles- y no por onCelda: llamar al
#  callback salta el codigo que decide QUE celda es, que es donde vive el
#  arrastre. Es la misma leccion de los cinco del compas por el otro lado.
d = una ("arrastre")
#  Un dedo que baja tres filas en la misma columna escribe UNA nota, no tres:
#  bajar el dedo dejaba una en cada fila por la que pasaba.
mide ("arrastre en su fila", d is not None and d["filas"] == 1 and d["pasos"] == 1,
       "" if d is None else "%d notas en la columna, %d columnas escritas"
                            % (d["filas"], d["pasos"]))

d = una ("lapiz")
#  Los DOS numeros: con el lapiz armado, tocar encima de una nota que ya esta
#  la deja; sin el, la alterna. Solo el primero lo cumple una herramienta que
#  no hace nada, y solo el segundo lo cumple no haberla anadido.
mide ("lapiz escribe y no borra", d is not None and d["con"] == 1 and d["sin"] == 0,
       "" if d is None else "con lapiz quedan %d, alternando %d" % (d["con"], d["sin"]))

#  --- el modo: un estado y tres tapas -------------------------------------
#
#  Las dos filas: encender desde la ficha del secuenciador y APAGAR desde la
#  cara. Solo la primera la pasa un codigo en el que cada tapa escribe su
#  propio estado; la segunda es la que dice que el estado es uno.
modo = filas.get ("modo", [])
if len (modo) != 2:
    mide ("modo cancion/patron", False, "salieron %d lineas de 2" % len (modo))
else:
    a, b = modo
    mide ("modo: enciende y las tres lo dicen",
           (a["motor"], a["cara"], a["sec"], a["cancion"]) == (1, 1, 1, 1),
           "motor %d cara %d sec %d cancion %d" % (a["motor"], a["cara"], a["sec"], a["cancion"]))
    mide ("modo: apaga desde la cara",
           (b["motor"], b["cara"], b["sec"], b["cancion"]) == (0, 0, 0, 0),
           "motor %d cara %d sec %d cancion %d" % (b["motor"], b["cara"], b["sec"], b["cancion"]))

print()
print ("piano: %d comprobaciones, %d FALLA" % (len (hechas), len (fallos)))
sys.exit (1 if fallos else 0)

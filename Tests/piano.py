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
#  miden juntos. Y se miden por el CAMINO DE VERDAD: el compas se cambia
#  ARRASTRANDO la barra -la fila de tapas de compas dejo de existir cuando la
#  ventana paso a ser continua- y la nota se escribe por el gesto de la
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
    #  La barra mueve la ventana y la nota cae en `base + 3`, que es la unica
    #  cuenta que el gesto promete: con la ventana CONTINUA una pagina ya no es
    #  un compas -avanza las columnas que quepan y se acota en total-visibles-
    #  asi que pedir «el compas 1» era pedirle a la barra algo que no hace.
    #
    #  Tres cifras: que la ventana se haya MOVIDO -sin eso «escribio en su
    #  sitio» lo cumple una barra muerta-, que la nota este donde la ventana
    #  dice, y que el testigo del principio siga con su 9. Si la nota nueva lo
    #  hubiera pisado, nota3 valdria 5.
    mide ("compas mueve", d["base"] > 0, "primer paso %d" % d["base"])
    mide ("compas escribe", d["escrito"] == 1 and d["nota"] == 5,
           "paso %d puesto=%d nota=%d" % (d["base"] + 3, d["escrito"], d["nota"]))
    mide ("compas testigo", d["paso3"] == 1 and d["nota3"] == 9,
           "paso 3 puesto=%d nota=%d" % (d["paso3"], d["nota3"]))

d = una ("vista")
#  Y la vista tiene que haberse repintado con la barra: la columna 3 lleva el 5
#  que se acaba de escribir y no el 9 del principio. Este es el fallo 2 y el
#  unico que se ve.
mide ("compas vista", d is not None and d["col3"] == 5,
       "" if d is None else "columna 3 = %d (5 es lo nuevo, 9 el testigo)" % d["col3"])

#  --- LA BARRA ALCANZA TODO ------------------------------------------------
#
#  La fila de tapas de compas contestaba esto sola: con 1, 2, 3 y 4 dibujadas,
#  «se llega al compas 4» era evidente. Con una ventana continua deja de serlo,
#  y ademas el DIBUJO no lo dice: el pulgar tiene suelo -un dedo- asi que llega
#  al filo de la pista aunque la vista se quede a pasos del final.
#
#  CON DOS CIFRAS, que una sola se engaña por los dos lados: «el ultimo paso se
#  ve» lo cumple igual una barra clavada en el final, y «el primero se ve» una
#  clavada en el principio. Y las dos contra lo que la APP dice que hay -total y
#  visibles- y no contra un numero escrito aqui: la ventana cambia con el zoom y
#  con el ancho de la pantalla, asi que un 31 escrito en el script mediria otra
#  rejilla en cuanto alguno de los dos se mueva.
d = una ("barra")
if d is None:
    mide ("barra", False, "no salio")
else:
    mide ("barra llega al final", d["ultimo"] == d["total"] - 1,
           "ultimo paso visible %d de %d" % (d["ultimo"], d["total"]))
    mide ("y vuelve al principio", d["primero"] == 0,
           "primer paso visible %d" % d["primero"])

d = una ("goma")
#  La goma recibe una COLUMNA y el codigo la convierte con la ventana, asi que
#  el paso que borra sale de `base`. El testigo vive fuera de la ventana.
mide ("goma", d is not None and d["frotado"] == 0 and d["paso3"] == 1,
       "" if d is None else "borrado %d=%d, testigo 3=%d"
                              % (d["base"] + 3, d["frotado"], d["paso3"]))

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

#  --- el transporte: un estado, tres tapas y un ROTULO ---------------------
#
#  La misma pregunta que el modo, en el sitio donde nadie la hacia: el
#  comentario de `ponModoCancion` daba por hecho que PLAY ya tenia embudo y no
#  lo tenia -diez escritores, cada uno sincronizando las tapas que se
#  acordaba-. Seis caminos: los tres que puede tomar un dedo y los dos que no
#  lo son, que son los que peor estaban.
#
#  Con el ROTULO y no solo con el toggle, que es lo que separa las dos formas
#  de escribirlo mal: los dos caminos del foco de audio hacian
#  `setToggleState` y nada mas, asi que la tapa decia PLAY con la maquina
#  rodando - un estado correcto por dentro y una cara que miente.
tr = filas.get ("transporte", [])
ESPERADO = [("sec", 1), ("cara", 0), ("cancion", 1), ("cara", 0),
            ("rec", 1), ("llamada", 0)]
if len (tr) != len (ESPERADO):
    mide ("transporte: un estado y tres tapas", False,
          "salieron %d lineas de %d" % (len (tr), len (ESPERADO)))
else:
    for fila, (quien, on) in zip (tr, ESPERADO):
        rot = "STOP" if on else "PLAY"
        ok  = ((fila["motor"], fila["cara"], fila["sec"], fila["cancion"])
                  == (on, on, on, on)
               and fila["rotulos"] == [rot, rot, rot])
        mide ("transporte desde %s" % quien, ok,
              "motor %d cara %d sec %d cancion %d  rotulos %s"
              % (fila["motor"], fila["cara"], fila["sec"], fila["cancion"],
                 ",".join (fila["rotulos"])))

#  --- TOCAR EL PAD QUE ASOMA POR DEBAJO DE LA FICHA ----------------------
#
#  La tarjeta se centra al 78 % PARA QUE la maquina se siga viendo, y se veia y
#  no se podia tocar: Sheet::mouseDown cerraba la ficha con cualquier toque
#  fuera de la tarjeta. Cambiar el pad que edita el secuenciador costaba
#  cerrar, elegir y volver a abrir, con la rejilla de pads delante todo el rato.
#
#  DOS cifras y no una: que el pad cambie Y que la ficha siga abierta. Solo con
#  la primera pasaria un codigo que selecciona y ademas cierra, que es
#  exactamente lo que no se quiere. Y se parte de OTRO pad -"antes"- o
#  "elegido == tocado" saldria verde con el toque cayendo al vacio.
d = una ("detras")
if d is None:
    mide ("tocar el pad de detras", False, "no salio")
else:
    mide ("tocar el pad de detras",
          d["pad"] >= 0 and d["antes"] != d["pad"] and d["elegido"] == d["pad"],
          "se toco el %d estando en el %d y quedo el %d" % (d["pad"], d["antes"], d["elegido"]))
    mide ("y la ficha no se cierra", d["abierta"] == 1,
          "abierta=%d" % d["abierta"])

#  --- Y LA REJILLA DE DIECISEIS ------------------------------------------
#
#  PAD -/+ pasean de uno en uno; para llegar al 11 hace falta una rejilla. En
#  cuatro por cuatro porque dieciseis en fila ya esta medido y no cabe -26 px
#  en 280-, y ENCIMA de la tarjeta porque la pagina del piano es de lienzo y
#  cuatro filas de tapas se comerian la mitad de la rejilla de tono.
#
#  Se mide tambien la CELDA: una rejilla encendida y de 0x0 pasa las seis
#  reglas de geometria, que es justo lo que la septima existe para cazar.
d = una ("rejilla")
if d is None:
    mide ("la rejilla de pads", False, "no salio")
else:
    mide ("la rejilla de pads abre", d["abrio"] == 1 and d["celda_w"] >= 40 and d["celda_h"] >= 40,
          "celda %dx%d" % (d["celda_w"], d["celda_h"]))
    #  Y llega al 11 de un gesto, que es para lo que existe.
    mide ("y elige el pad 11", d["elegido"] == 10, "elegido %d" % (d["elegido"] + 1))
    #  Y se cierra sola: dejarla abierta es un segundo toque para volver.
    mide ("y se cierra al elegir", d["cerro"] == 1, "cerro=%d" % d["cerro"])

#  --- LA SELECCION, MOVER EN BLOQUE, COPIAR Y PEGAR ----------------------
#
#  El piano se podia escribir nota a nota y no se podia EDITAR: para mover una
#  figura habia que borrarla y volver a escribirla, y copiar no existia. La
#  seleccion es la quinta herramienta -no un gesto nuevo- porque esta rejilla es
#  de lienzo y arrastrar ya significa dos cosas.
#
#  Se mide por el GESTO en pixeles, que es donde vive: llamar a pianoBanda o a
#  pianoMueveSel por dentro salta el codigo que decide si el arrastre empieza
#  DENTRO de la seleccion, que es lo unico que separa mover de volver a
#  seleccionar.
d = una ("sel")
if d is None:
    mide ("la seleccion del piano", False, "no salio")
else:
    #  La banda coge lo que cubre Y SOLO ESO: hay una cuarta nota fuera del
    #  rectangulo, y "selecciono" lo cumple igual una banda que lo coge todo.
    mide ("la banda coge lo que cubre",
          d["seleccionadas"] == 3 and d["fuera_quieta"] == 1,
          "%d notas, la de fuera %s" % (d["seleccionadas"],
                                        "quieta" if d["fuera_quieta"] else "SE MOVIO"))

    #  Y MOVER CONSERVA LA FIGURA: llegan las tres, y con SUS TRES LARGOS
    #  distintos. Sin el largo la prueba pasa con una figura aplastada, que es
    #  la leccion de copiarFila contada en el piano - una copia que se deja el
    #  largo ha dejado de ser la misma figura.
    mide ("mover conserva las tres y su largo",
          d["tras_mover"] == [1, 1, 1] and d["largos_ok"] == 3,
          "llegaron %s, largos %d de 3" % (d["tras_mover"], d["largos_ok"]))

    #  Y UNA SOLA ENTRADA DE DESHACER para el bloque entero: deshacer un
    #  movimiento de tres notas tres veces no es deshacer, es contar.
    mide ("y una sola entrada de deshacer", d["undo"] == 1,
          "%d entradas" % d["undo"])

    #  COPIAR Y PEGAR, en otro compas: el portapapeles es RELATIVO, asi que
    #  pegar cae donde se mira y no donde se copio.
    mide ("copiar y pegar en otro compas",
          d["copiadas"] == 3 and d["pegadas"] == 3,
          "%d copiadas, %d pegadas" % (d["copiadas"], d["pegadas"]))

#  --- EL ZOOM HORIZONTAL --------------------------------------------------
#
#  Ocho columnas son medio compas con celdas del doble de ancho -que es lo que
#  hace falta para escribir en 1/32- y treinta y dos son dos compases para ver
#  la frase. Con el suelo decidiendo, como todo en esta casa: medido en 412x915,
#  32 columnas dan una celda de 10 px contra un suelo de 12, o sea dos compases
#  que se ven y no se pueden escribir.
#
#  Se mide POR LA TAPA y no poniendo el numero por dentro, que es lo unico que
#  ve la escalera: el ciclo salta el paso que no cabe.
MIN_CELL = 12
d = una ("zoom")
if d is None:
    mide ("el zoom del piano", False, "no salio")
else:
    mide ("el zoom da mas de una vista",
          len (set (d["cols"])) >= 2,
          "columnas %s" % (d["cols"],))
    #  Y NINGUNA CELDA POR DEBAJO DEL DEDO. Es la cifra que justifica la
    #  escalera: si un dia baja del suelo, la decision se cae.
    mide ("y ninguna celda baja del suelo",
          all (w >= MIN_CELL for w in d["ancho"]),
          "anchos %s px (suelo %d)" % (d["ancho"], MIN_CELL))

#  --- LA CUADRICULA SE DESPLAZA CON LAS NOTAS ------------------------------
#
#  «Cuando arrastras la barra lateral, se desplazan las notas pero no las
#  cuadriculas, con lo que puede dar a confundirse donde pone uno las notas
#  siguiendo los pasos de los beat». La causa cabia en una linea: el fondo se
#  teñia por la COLUMNA y `StepGrid` lo hace por el PASO desde el dia que la
#  ventana es continua — el piano no recibia el paso de la primera columna, asi
#  que no tenia con que.
#
#  Ninguna de las catorce reglas de `expo.py` puede verlo: una cuadricula que
#  marca el contratiempo se maqueta perfecta. Es la familia de los cinco fallos
#  del compas.
#
#  Lo mide la app PINTANDO -las lineas de compas son las unicas de alto
#  completo, asi que la primera fila de la imagen las delata sin saber nada de
#  la formula- y publica en que PASO cayo cada una. Con DOS cifras: que la
#  cuadricula se MUEVA -un dibujo que no mira la ventana da la misma lista en
#  las dos- y que caiga donde tiene que caer, que es una IDENTIDAD y no un
#  numero escrito aqui: un pulso es un paso multiplo de cuatro.
#
#  Roto a proposito devolviendo `c % 4`: la ventana en 2 saca [6, 10, 14].
d = una ("cuadricula")
if d is None:
    mide ("la cuadricula del piano", False, "no salio")
else:
    v0, v2 = d["ventana0"], d["ventana2"]
    mide ("la cuadricula se dibuja",
          len (v0) >= 2 and len (v2) >= 2,
          "ventana 0 %s, ventana 2 %s" % (v0, v2))
    mide ("y se mueve con la ventana",
          v0 != v2, "ventana 0 %s, ventana 2 %s" % (v0, v2))
    mala = [x for x in v0 + v2 if x % 4 != 0]
    mide ("y cae en el pulso del PATRON",
          not mala,
          "ventana 0 %s, ventana 2 %s" % (v0, v2))

print()
print ("piano: %d comprobaciones, %d FALLA" % (len (hechas), len (fallos)))
sys.exit (1 if fallos else 0)

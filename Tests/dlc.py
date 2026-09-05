#!/usr/bin/env python3
# ============================================================================
#  EL CONTENIDO DESCARGABLE: el catalogo, el candado y lo que llega a los pads.
#
#  La maquina venia con sesenta y cuatro sonidos y no habia forma de meter mas
#  que de uno en uno. Un instrumento son dieciseis presets -o sea un banco
#  entero, que es la unidad que la maquina ya tenia- y un pack son hasta
#  dieciseis instrumentos. Ver Source/Instrumentos.h.
#
#  ESTA PRUEBA EXISTE POR EL CANDADO, que es la parte que se puede escribir mal
#  de las dos maneras y las dos parecen bien:
#
#  - Un candado que dice que no y CARGA IGUAL sale verde en cualquier prueba
#    que mire la linea de estado. Por eso se cuentan PADS y no texto.
#  - Y un candado que no abre nunca pasa esa misma prueba y no vale nada. Por
#    eso se concede la licencia y se vuelve a preguntar. Las dos mitades o
#    ninguna: es la misma regla que ya costo una medida en QUITAR RUIDO y en
#    el bucle de la cancion.
#
#  Los packs los PLANTA la app (plantaPacksDePrueba), porque el catalogo de
#  verdad depende de lo que haya en el disco de cada telefono: sin ese andamio
#  el banco solo puede medir el pack de dentro y jamas la rejilla llena ni el
#  candado. Es lo mismo que hace ZATI_SKIN con la carcasa - convierte en
#  ENTRADA lo que si no seria "lo que hubiera".
#
#  Con HOME temporal, que ademas es lo que hace que la licencia se mida: se
#  escribe en el directorio interno de la app y no dentro del pack, asi que una
#  corrida con el HOME de verdad heredaria la del dia anterior.
#
#      python3 Tests/dlc.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (
        ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre(abrir="pads"):
    casa = tempfile.mkdtemp (prefix="zati-dlc-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": abrir, "ZATI_DLC": "1"})
        r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    packs, filas = [], {}
    for linea in r.stdout.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if d.get ("dlc") == "pack":
            packs.append (d)
        elif "dlc" in d:
            filas[d["dlc"]] = d
        elif "inst" in d:
            filas[d["inst"]] = d
    return packs, filas, r.stdout


fallos, hechas = [], []
def mide (nombre, ok, texto=""):
    hechas.append (nombre)
    print (("  OK   " if ok else "  FALLA") + "  " + nombre + ("  " + texto if texto else ""))
    if not ok:
        fallos.append (nombre)


packs, filas, bruto = corre()
#  Y UNA SEGUNDA CORRIDA CON EL MENU ABIERTO. El volcado del menu sale del
#  maquetado, y con ZATI_OPEN=pads esa ficha no se maqueta nunca - que es
#  exactamente el punto ciego que ya costo tres tapas de la pagina PASO.
_, filasMenu, _ = corre ("instg")
filas.update (filasMenu)
if not packs:
    print ("sin volcado: la app no imprimio nada con ZATI_DLC=1")
    print (bruto[-2000:])
    sys.exit (1)

por_id = { p["id"]: p for p in packs }

#  --- EL CATALOGO --------------------------------------------------------
#  Tres: el de dentro y los dos plantados. Y el de dentro EL PRIMERO, que es
#  lo que hace que el menu se explique solo la primera vez que se abre: un
#  catalogo de contenido que abre vacio se lee como una funcion rota.
mide ("cuatro packs", len (packs) == 4, "%d packs: %s" % (len (packs), ", ".join (p["id"] for p in packs)))
#  Y SINTES EL PRIMERO desde que un pad puede llevar un instrumento: es lo que
#  esta ficha ES ahora. La fabrica va detras, que sigue siendo lo que recarga un
#  banco entero.
mide ("los instrumentos primero",
       packs and packs[0]["id"] == "SINTES" and packs[0]["dentro"] == 1
             and packs[0]["instr"] == 16,
       "" if not packs else "%s dentro=%d instr=%d"
                            % (packs[0]["id"], packs[0]["dentro"], packs[0]["instr"]))
#  Cuatro instrumentos y no sesenta y cuatro sonidos: los cuatro bancos de
#  fabrica YA eran cuatro instrumentos de dieciseis presets, solo que no habia
#  donde ensenarlos.
f = por_id.get ("ZATI")
mide ("la fabrica son cuatro bancos",
       f is not None and f["instr"] == 4, "" if f is None else "%d instrumentos" % f["instr"])
mide ("la fabrica esta abierta", f is not None and f["abierto"] == 1)
mide ("la fabrica va detras de los instrumentos",
       f is not None and packs.index (f) == 1,
       "" if f is None else "la fabrica es el pack %d" % packs.index (f))

d = por_id.get ("01 DEMO")
mide ("pack abierto", d is not None and d["pago"] == 0 and d["abierto"] == 1 and d["instr"] == 16,
       "" if d is None else "pago=%d abierto=%d instr=%d" % (d["pago"], d["abierto"], d["instr"]))
#  El nombre sale del manifiesto y no de la carpeta: "01 DEMO" es el orden.
mide ("nombre del manifiesto", d is not None and d["nombre"] == "DEMO",
       "" if d is None else d["nombre"])

d = por_id.get ("02 ESTUDIO")
mide ("pack de pago cerrado", d is not None and d["pago"] == 1 and d["abierto"] == 0,
       "" if d is None else "pago=%d abierto=%d" % (d["pago"], d["abierto"]))
#  Y SE LISTA IGUAL. Un pack cerrado que no aparece no se compra nunca; lo que
#  hace falta es que se vea QUE hay y que al tocarlo diga por que no suena.
mide ("y aun asi se lista", d is not None and d["instr"] == 16,
       "" if d is None else "%d instrumentos" % d["instr"])

#  --- EL CANDADO, LAS DOS MITADES ----------------------------------------
d = filas.get ("cerrado")
#  Se mide el REPARTO -que empieza en pushUndo, y es sincrono- y no los pads.
#  Contar pads era una linea que imprimia OK: el reparto de un instrumento de
#  disco es asincrono y sin bucle de mensajes los pads salen a cero con el
#  candado puesto y sin el. Se descubrio rompiendo el candado a proposito y
#  viendo que seguia verde.
mide ("cerrado no reparte", d is not None and d["reparto"] == 0,
       "" if d is None else "%d entradas en deshacer" % d["reparto"])

d = filas.get ("licencia")
mide ("con licencia abre", d is not None and d["abierto"] == 1,
       "" if d is None else "abierto=%d" % d["abierto"])

#  --- LO QUE LLEGA A LOS PADS --------------------------------------------
d = filas.get ("abierto")
mide ("los 16 traen audio", d is not None and d["conAudio"] == 16,
       "" if d is None else "%d de 16" % d["conAudio"])
#  Las dos cifras de delante son el ORDEN y no el nombre. Sin ellas "10" se
#  ordena antes que "2" y el pack vuelve barajado; ensenandolas, la tapa dice
#  el andamio.
mide ("y ninguno ensena su numero", d is not None and d["conNombre"] == 16,
       "" if d is None else "%d de 16 sin cifra delante" % d["conNombre"])

#  --- Y QUE EL MENU DIGA COMO SE LLAMA CADA UNO --------------------------
#  El menu era una rejilla de cuatro por cuatro con dieciseis dibujos y ni una
#  palabra, y la queja fue exactamente esa: "muy bonitos los sprites pero no se
#  cual es el nombre de cada uno". No es que el rotulo se cayera por poco - en
#  412x915 la celda de cuatro mide 84 px, su caja de rotulo 74, y "CUERDA PULS"
#  pide 83: el nombre no cabia AUNQUE se cayera el dibujo.
#
#  Se cuenta lo que se PINTA -reparteTapa, la misma funcion que dibuja- y no lo
#  que la tapa tiene guardado: el rotulo puede estar puesto y no salir, que es
#  como el fallo pudo durar una tanda entera sin que ninguna regla lo dijera.
menu = filas.get ("menu")
mide ("los dieciseis dicen su nombre",
       menu is not None and menu["conNombre"] == 16,
       "" if menu is None else "%d de 16 con nombre, celda %dx%d"
                               % (menu["conNombre"], menu["celdaW"], menu["celdaH"]))
#  Y LAS DOS COSAS. Quedarse con la palabra y soltar el dibujo es lo que hacia
#  la lista de antes, y entonces sobra haberlos dibujado; el arreglo es que
#  quepan los dos, y eso solo lo dice contarlos a la vez.
mide ("y ademas su dibujo",
       menu is not None and menu["conDibujo"] == 16,
       "" if menu is None else "%d de 16 con dibujo" % menu["conDibujo"])
#  Y EL PIE, DENTRO DEL CUERPO. Las dos frases que explican la ficha -elige el
#  pad arriba y el instrumento abajo- las colocaba el PINTOR con una cuenta que
#  solo valia para la lista: con la rejilla puesta caian 704 px mas abajo, o sea
#  fuera del cuerpo, o sea que no las ha visto nadie desde que la rejilla
#  existe. Un rotulo que se pinta fuera no lo ve ninguna de las otras reglas.
mide ("y el pie se ve",
       menu is not None and menu["pieAbajo"] <= menu["cuerpo"],
       "" if menu is None else "el pie acaba en %d y el cuerpo mide %d"
                               % (menu["pieAbajo"], menu["cuerpo"]))

d = filas.get ("fabrica")
#  La fabrica no son ficheros: se sintetiza o sale de los recursos incrustados,
#  asi que entra por su propia puerta. Si esta en cero, el instrumento de dentro
#  se ha quedado sin camino.
mide ("la fabrica llena el banco", d is not None and d["pads"] == 16,
       "" if d is None else "%d pads" % d["pads"])

#  --- Y QUE EL INSTRUMENTO NUEVO NO HEREDE EL PAD DE DEBAJO ---------------
#
#  assignSampleToPad -por donde entran TODOS los caminos que ponen sonido en un
#  pad- reiniciaba el recorte y nada mas, asi que la afinacion, el corte del
#  filtro, el reves, el choke y los seis envios se quedaban donde los dejo el
#  sonido anterior. Cargar un instrumento en un banco que habias ajustado daba
#  dieciseis sonidos nuevos que no se oian bien, sin nada que dijera por que.
#  Es la misma herencia que ya se cazo en NUEVO.
#
#  La app ensucia los dieciseis ANTES de cargar -pitch +12, reves, choke 3, el
#  corte a 200 Hz, el RECORTE a cero y el pad en el canal 7- y esto mira lo peor
#  de cada uno: con el maximo, un solo pad que herede lo canta. Roto a proposito
#  (quitando ponPadPorDefecto de cargaFabricaEnBanco) vuelven los cinco.
#
#  Y EL RECORTE SE MIDE AL REVES QUE ANTES, que es lo que cambio con la mesa:
#  ya no es «cuanto manda este pad» -eso es del canal- sino «con cuanto se
#  guardo», y nace en UNO. Hereda si vuelve en CERO, que es lo que el proyecto
#  de ayer le dejo puesto, asi que lo que se mira es el MINIMO y tiene que
#  salir 1. Una cifra que cambia de significado se vuelve a elegir; no se
#  afloja.
d = filas.get ("herencia")
mide ("el instrumento nuevo no hereda",
      d is not None and d["pitch"] == 0 and d["reves"] == 0
      and d["choke"] == 0 and d["envio"] == 1 and d["corte"] >= 20000
      and d["canal"] == 0,
      "" if d is None else "pitch %g  corte %g Hz  reves %d  choke %d  recorte %g  canal %d"
        % (d["pitch"], d["corte"], d["reves"], d["choke"], d["envio"], d["canal"]))

#  Y LA MITAD QUE NO SE VE: el corte lo escribe tambien el bloqueo de paso,
#  desde el hilo de audio, y assignSampleToPad no empujaba setPadCutoff nunca.
#  El mando decia "abierto" y el pad sonaba filtrado - un pad y su mando
#  contando cosas distintas es la unica clase de fallo que no se ve mirando la
#  pantalla. Por eso el banco ensucia el corte SOLO en el motor.
mide ("y el espejo dice lo que el motor",
      d is not None and d["espejo_mal"] == 0,
      "" if d is None else "%d pads con el mando y el motor en desacuerdo" % d["espejo_mal"])

print()
print ("dlc: %d comprobaciones, %d FALLA" % (len (hechas), len (fallos)))
sys.exit (1 if fallos else 0)

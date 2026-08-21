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


def corre():
    casa = tempfile.mkdtemp (prefix="zati-dlc-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": "pads", "ZATI_DLC": "1"})
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
    return packs, filas, r.stdout


fallos, hechas = [], []
def mide (nombre, ok, texto=""):
    hechas.append (nombre)
    print (("  OK   " if ok else "  FALLA") + "  " + nombre + ("  " + texto if texto else ""))
    if not ok:
        fallos.append (nombre)


packs, filas, bruto = corre()
if not packs:
    print ("sin volcado: la app no imprimio nada con ZATI_DLC=1")
    print (bruto[-2000:])
    sys.exit (1)

por_id = { p["id"]: p for p in packs }

#  --- EL CATALOGO --------------------------------------------------------
#  Tres: el de dentro y los dos plantados. Y el de dentro EL PRIMERO, que es
#  lo que hace que el menu se explique solo la primera vez que se abre: un
#  catalogo de contenido que abre vacio se lee como una funcion rota.
mide ("tres packs", len (packs) == 3, "%d packs: %s" % (len (packs), ", ".join (p["id"] for p in packs)))
mide ("la fabrica es el primero",
       packs and packs[0]["id"] == "ZATI" and packs[0]["dentro"] == 1,
       "" if not packs else "%s dentro=%d" % (packs[0]["id"], packs[0]["dentro"]))
#  Cuatro instrumentos y no sesenta y cuatro sonidos: los cuatro bancos de
#  fabrica YA eran cuatro instrumentos de dieciseis presets, solo que no habia
#  donde ensenarlos.
mide ("la fabrica son cuatro bancos",
       packs and packs[0]["instr"] == 4, "" if not packs else "%d instrumentos" % packs[0]["instr"])
mide ("la fabrica esta abierta", packs and packs[0]["abierto"] == 1)

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

d = filas.get ("fabrica")
#  La fabrica no son ficheros: se sintetiza o sale de los recursos incrustados,
#  asi que entra por su propia puerta. Si esta en cero, el instrumento de dentro
#  se ha quedado sin camino.
mide ("la fabrica llena el banco", d is not None and d["pads"] == 16,
       "" if d is None else "%d pads" % d["pads"])

print()
print ("dlc: %d comprobaciones, %d FALLA" % (len (hechas), len (fallos)))
sys.exit (1 if fallos else 0)

#!/usr/bin/env python3
# ============================================================================
#  LO QUE GASTA LA APP SIN QUE NADIE LA TOQUE.
#
#  El motor se mide corriendo bloques (Tests/Cpu.cpp) y la respuesta de ese
#  banco fue que el motor no es el problema: una sesion entera -secuencia
#  rodando, seis efectos abiertos, filtros por pad, cuatro voces en modo
#  TONO- se lleva el 2 % del presupuesto de un bloque. La CPU de esta app se
#  iba por el otro lado, y por un sitio que ningun perfilador de audio mira:
#  la cara repintandose para no cambiar nada.
#
#  Las fichas de esta app ocupan la VENTANA ENTERA y llevan un velo al 45 %.
#  Eso quiere decir que un repaint() de ficha no repinta la ficha: repinta el
#  chasis, los dieciseis pads, el espectro y los cuarenta controles que hay
#  debajo del velo. Tres sitios lo pedian treinta veces por segundo -el
#  renglon de la cadena en SEC, la rejilla de CANCION y el recuadro de AUDIO
#  en AJUSTES- para mover un texto que no habia cambiado.
#
#  Asi que la prueba no cuenta milisegundos, que dependen de la maquina.
#  Cuenta FOTOGRAMAS COMPLETOS: cuantas veces se ha pintado el fondo de la
#  cara mientras la app estaba abierta y quieta. Ese numero no depende del
#  ordenador, y con la app quieta tiene que ser practicamente cero.
#
#      python3 Tests/cpu.py [segundos]
# ============================================================================
import json, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

SEGUNDOS = int (sys.argv[1]) if len (sys.argv) > 1 else 8

#  Las fichas que se abren solas y se quedan abiertas. Las que piden un
#  dialogo del sistema quedan fuera: en un banco sin pantalla no vuelven.
#  «eq» ES LA CARA CON EL ANALIZADOR PUESTO, y hace falta que este: la curva
#  vive en el PLATO -o sea en la cara, no en una ficha- y solo se enseña con el
#  EQ en una ranura y con el foco. Sin esta entrada, las dos FFT de 1024 por
#  tick no las corre nadie en el unico banco que mide por RELOJ, que es
#  exactamente publicar un numero sin mirarlo.
FICHAS = ["", "eq", "pads", "sec", "song", "mix", "mixc", "set", "proj", "midi", "gest",
          "xy", "rack", "rackf", "chop", "browse", "plato"]

#  EL TOPE, en VENTANAS y no en llamadas. Un arranque pinta el fondo una vez y
#  una ficha que se abre puede pedir otro; a partir de ahi, con nadie tocando
#  nada, no hay motivo para ninguno mas.
#
#  Y en ventanas porque contar LLAMADAS no separa un fotograma de una banda -la
#  leccion que la tabla de abajo ya tenia y esta no-: medido, una ficha abierta
#  sobre una maquina con dos efectos encendidos entra 370 veces en
#  `MainComponent::paint` con un recorte de 2622 pixeles, que son **0.007**
#  ventanas. Tres ventanas dejan sitio al asentamiento de los margenes del
#  sistema sin dejar pasar un repintado periodico: a treinta por segundo, ocho
#  segundos son doscientas cuarenta.
TOPE = 3

#  Y LA MISMA PREGUNTA CON LA MAQUINA SONANDO, que es el estado que este banco
#  no medi­a.
#
#  En un escritorio no hay tarjeta de sonido, asi que el motor no renderiza, el
#  osciloscopio ve silencio y `SpectrumDisplay::setSamples` se rinde en su
#  guardia: la pieza mas grande de la cara -su propio comentario la llama «el
#  coste en reposo mas grande de la app»- no se repintaba NUNCA aqui. En un
#  telefono con algo sonando se repinta treinta veces por segundo, se vea o no.
#  `ZATI_SONANDO` bombea los bloques que le tocan a cada tick y el camino
#  entero -motor, colas, osciloscopio, VU, destellos- corre de verdad.
#
#  Y AQUI SE CUENTAN PIXELES Y NO LLAMADAS, que es lo que separa las dos cosas
#  que estaban pasando a la vez: el cabezal de la rejilla de pasos entra en
#  `MainComponent::paint` treinta veces por segundo y esta BIEN, porque pide su
#  banda; la ficha que tapa la cara entraba las mismas veces y pedia la
#  ventana. Contando llamadas los dos salen igual. Medido en MEZCLA, en 8 s:
#  42 346 644 pixeles antes -112 fotogramas equivalentes, o sea pintando la
#  maquina entera con una tarjeta delante- y 380 660 despues, uno.
#
#  Y LA CIFRA QUE VIAJA PASA A SER POR CUADRO, que es lo que la separacion del
#  reloj y el dibujo obliga a decir en voz alta.
#
#  Hasta esta tanda el dibujo colgaba de UN temporizador a `uiIntervalMs`, o sea
#  entre 10 y 30 cuadros por segundo segun la gama, y ocho segundos eran ~133
#  cuadros CLAVADOS: un total y un por-cuadro eran el mismo numero con otra
#  escala. Desde que el repintado cuelga del vblank la cadencia la pone el panel
#  -60, 90 o 120- o el `ZATI_VBLANK` de aqui abajo, asi que **los pixeles
#  totales suben por definicion** y el numero de la tanda anterior deja de ser
#  comparable. Lo que no puede empeorar es el coste POR CUADRO, y eso es lo que
#  se juzga.
#
#  Y la cadencia se fija, que si no esta medida depende de la maquina: X11
#  entrega vblanks a la frecuencia que declare el display —100 Hz en Xvfb, que
#  no declara ninguna— asi que sin `ZATI_VBLANK` el mismo binario daria un
#  numero distinto en el portatil y en el runner. Es la misma razon por la que
#  esta prueba corre SOLA.
VBLANK_HZ = 60

#  El tope sale de la POBLACION y no de un numero redondo. Medido a 60 Hz con
#  la maquina sonando: la ficha que mas pinta CON RAZON es SEC con **0.007**
#  ventanas por cuadro —el cabezal de la rejilla de pasos, que pide su banda—
#  y una ficha que repinta lo que no se ve mide como la CARA, **0.50**. Cinco
#  centesimas estan siete veces por encima de la primera y diez por debajo de
#  la segunda.
TOPE_SONANDO = 0.05


#  Y EL CUADRO COMO LO PAGA EL TELEFONO, que es lo que todo lo de arriba no
#  podia ver.
#
#  Las ventanas por cuadro de arriba son las del ESCRITORIO, que respeta el
#  recorte: la aguja que se mueve pide su banda y se pinta su banda. Un Android
#  con aceleracion por hardware no -desde la API 21 la vista ignora el
#  rectangulo de `invalidate` y se vuelve a grabar entera, y JUCE la pinta por
#  software en un bufer del tamano de la pantalla-, asi que alli cada una de
#  esas bandas era la ventana ENTERA. Del telefono: «es menos ligera», y el
#  gesto, «en el Mixer». Medido a la escala de un 412x915 de verdad, 2.625, con
#  la maquina sonando: la mesa pagaba 1.004 ventanas por cuadro -la cara, el
#  velo, la tarjeta y las dieciseis filas- a sesenta cuadros por segundo.
#
#  Lo que se juzga es lo que la cara VUELVE A PINTAR en ese cuadro entero, en
#  ventanas, que no depende de la maquina; los milisegundos se imprimen porque
#  son la otra mitad de la historia, pero un tope en milisegundos seria un tope
#  del portatil. Y con la segunda cifra al lado: los pixeles en que ese cuadro
#  difiere de la cara pintada entera tras mover un fader y un MUTE. Una imagen
#  vieja ahorra lo mismo que una buena; solo las dos juntas dicen que el ahorro
#  es de verdad. Los mandos que se mueven son uno por vista -el fader y el MUTE
#  del pad 0, los del canal 0, el primer CTRL de la cara- y una vista en la que
#  no se ve ninguno no prueba nada: por eso la tercera columna.
ESCALA_TELEFONO = 2.625
#  La mesa repinta sus agujas y nada mas: 0.425 medido, con las dieciseis
#  bandas y la del master juntas en un recorte. Media ventana deja pasar eso y
#  no deja pasar el cuadro entero, que mide 1.004.
TOPE_TELEFONO = 0.5


#  Y LO VIEJO, QUE ES LO QUE ESE AHORRO TRAE CONSIGO.
#
#  La cara guardada solo vuelve a pintar lo que alguien invalido, asi que lo
#  que cambia sin su `repaint` se queda viejo EN EL TELEFONO. Antes alli no se
#  podia notar -cada cuadro era la ventana entera- y aqui tampoco: las fotos
#  del banco pintan la cara de cero. Se mira por dos sitios, porque cada uno
#  ve lo que el otro no:
#
#    - VIVO: tras los seis segundos sonando, con los temporizadores y los
#      hilos de verdad, la ventana contra la cara pintada entera. Asi salio la
#      linea de continuidad: «64 PADS» en la ventana con la app diciendo «A
#      SALVO», 214 pixeles.
#    - TRAS CADA TAPA: la misma comparacion despues de cada una de las ~2500
#      apretadas de `ZATI_TAPAS`, con la imagen puesta al dia antes de apretar.
#      Asi salio la banda de los ocho colores: apagada en la ventana y
#      encendida en la app tras cargar la fabrica, 1504 pixeles. Esta corrida
#      va de arriba abajo -ningun temporizador salta- y por eso hace falta la
#      otra.
#
#  Y con un recuento debajo, que una comprobacion que no mira nada sale en
#  cero: el minimo de apretadas es el de `Tests/atasco.py`, y la parte de la
#  ventana mirada por apretada tiene suelo. Sin poner la imagen al dia antes
#  de cada una, reabrir la ficha invalidaba la ventana entera y la
#  comprobacion miraba el 0.6 % de media: cero viejas, sin haber mirado.
MINIMO_CENTINELA = 2000
MIRADO_CENTINELA = 0.10

#  Y LO QUE CADA TAPA MANDA VOLVER A PINTAR, de la misma corrida.
#
#  «El switch de Mo/St va fluido como debe ser, el de M y S no». En el
#  telefono la cara vive en una imagen y cada pixel pendiente se pinta en
#  software en el cuadro siguiente: la ventana entera son 47-66 ms. El ST de la
#  mesa repintaba su tapa -0.0034 de la ventana- y el M y el S la ficha, que
#  ocupa la ventana entera: 1.0. Cada linea `op` de ZATI_TAPAS dice ahora que
#  parte de la ventana queda pendiente tras la tapa (`sucio`) y si la ventana
#  conserva su forma (`sitio`): una tapa EN SU SITIO -un switch, un paso, un
#  pad- no abre ni cierra nada, y lo que manda repintar tiene que ser lo suyo.
#
#  Tres reglas, medidas en 412x915 contra el binario de antes (e35a813):
#    - M y S de la mesa: la mediana por debajo de TOPE_MS (antes 1.0).
#    - las tapas de pad y de celda -rotulo numerico- en su sitio, de media por
#      debajo de TOPE_NUMERO (antes 0.47): un pad tocado debajo de cualquier
#      ficha repintaba la ficha entera, y luego los dieciseis.
#    - ninguna tapa en su sitio pide la ventana entera (TOPE_ENTERA) salvo las
#      que de verdad la cambian entera, escritas abajo con su razon.
TOPE_MS     = 0.05
TOPE_NUMERO = 0.30
TOPE_ENTERA = 0.90
#  Deshacer y rehacer devuelven el proyecto entero; el aspecto cambia el color
#  de todo. En los cinco idiomas, que la corrida de tapas pasa por todos.
ENTERAS_CON_RAZON = re.compile (
    r"/(DESHACER|REHACER|UNDO|REDO|撤销|重做|تراجع|إعادة|PAPEL|GRAFITO|ACERO|LACA)$")


#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA.
#
#  `display_alive` caia a ":99" cuando `DISPLAY` no esta puesta y las corridas
#  heredaban `os.environ` tal cual, o sea SIN pantalla: la comprobacion decia
#  que si contra :99 y los treinta y cuatro arranques se iban sin ventana, con
#  el veredicto entero saliendo «no contesto». Una hora buscando un cambio que
#  no era —el binario a mano, con la misma orden y `DISPLAY=:99` delante, daba
#  su linea a la primera—. Es la misma regla que este banco lleva escrita desde
#  el principio: una regla con dos respuestas acierta por accidente.
from kits import PANTALLA                                          # noqa: E402
DISPLAY = PANTALLA


def display_alive():
    try:
        return subprocess.run (["xdpyinfo", "-display", DISPLAY],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (ficha, sonando=False, quieta=False, cuadro=False):
    #  CON SU PROPIO HOME, que es lo que le faltaba. Sin el, la app restaura la
    #  SESION que dejara la ultima prueba que corriera - y `Tests/ranuras.py` y
    #  `Tests/dinamica.py` dejan DOS efectos encendidos, cuyas lamparas laten a
    #  proposito («nothing lit means nothing repainted»). Un veredicto que
    #  depende de lo que dejara el de antes no es un veredicto: es el mismo
    #  fallo que `Tests/session.py` acaba de pagar en `proyecto()`.
    casa = tempfile.mkdtemp (prefix="zati-cpu-")
    env = dict (os.environ)
    env["DISPLAY"] = DISPLAY
    env.update ({"HOME": casa,
                 "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                 "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_DEMO": "1", "ZATI_OPEN": ficha, "ZATI_SPIN": str (SEGUNDOS),
                 "ZATI_VBLANK": str (VBLANK_HZ)})
    if sonando: env["ZATI_SONANDO"] = "1"
    if cuadro:  env["ZATI_CUADRO"] = str (ESCALA_TELEFONO)

    #  APAGAR EL MOVIMIENTO se pide como se pide de verdad: escribiendo la
    #  PREFERENCIA en el HOME de la corrida. No hace falta una entrada de banco
    #  nueva —hay HOME propio, asi que es determinista— y ademas asi se mide el
    #  camino entero: fichero, `loadMovPref` y la guardia de `pintaCuadro`. Una
    #  variable de entorno se habria saltado los dos primeros.
    if quieta:
        cfg = os.path.join (casa, ".config")
        os.makedirs (cfg, exist_ok=True)
        with open (os.path.join (cfg, "zati-movimiento.txt"), "w") as f:
            f.write ("0")

    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=SEGUNDOS + 90).stdout
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"spin"' in linea:
            try:
                return json.loads (linea)
            except Exception:
                pass
    return None


#  LAS ~2500 APRETADAS DE `ZATI_TAPAS`, cada una comprobada contra la cara
#  pintada entera. Ver MINIMO_CENTINELA. Con su propio HOME, como `corre`, y
#  con el plazo de `Tests/atasco.py`, que es la misma corrida.
def centinela():
    casa = tempfile.mkdtemp (prefix="zati-centinela-")
    env = dict (os.environ)
    env["DISPLAY"] = DISPLAY
    env.update ({"HOME": casa,
                 "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                 "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_TAPAS": "1", "ZATI_CENTINELA": "1"})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=2400).stdout
    except subprocess.TimeoutExpired:
        return None, [], []
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    #  Y las lineas `op` que traen lo que cada tapa deja pendiente. Ver TOPE_MS.
    total, viejas, ops = None, [], []
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'): continue
        if '"centinela"' not in linea and '"sucio"' not in linea: continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if   d.get ("centinela") == "total": total = d
        elif d.get ("centinela") == "vieja": viejas.append (d)
        elif "sucio" in d:                   ops.append (d)
    return total, viejas, ops


#  Las tres reglas de TOPE_MS sobre las lineas `op` de la corrida de tapas.
#  Aparte para poder juzgar con ellas una corrida guardada: asi se probo que
#  fallan en el binario de antes.
def juzga_sucio (ops):
    malas = []
    print()
    print ("y lo que cada tapa en su sitio manda volver a pintar, como parte de la ventana")
    sitio = [o for o in ops if int (o.get ("sitio", 0)) == 1]
    if len (ops) < MINIMO_CENTINELA:
        malas.append ("solo %d tapas dicen lo que dejan pendiente y el minimo son %d: la "
                      "sonda no publica `sucio`" % (len (ops), MINIMO_CENTINELA))
    else:
        def mediana (xs):
            xs = sorted (xs)
            return xs[len (xs) // 2] if xs else -1.0
        ms  = [float (o["sucio"]) for o in sitio if re.search (r"^mixc?/(M|S)$", o.get ("que", ""))]
        num = [float (o["sucio"]) for o in sitio if re.search (r"/\d+$", o.get ("que", ""))]
        enteras = [o for o in sitio if float (o["sucio"]) >= TOPE_ENTERA
                   and not ENTERAS_CON_RAZON.search (o.get ("que", ""))]
        media = sum (float (o["sucio"]) for o in sitio) / max (1, len (sitio))
        print ("  %d tapas, %d en su sitio; media %.3f de la ventana" % (len (ops), len (sitio), media))
        print ("  M y S de la mesa: %d, mediana %.4f (tope %.2f)" % (len (ms), mediana (ms), TOPE_MS))
        print ("  pads y celdas: %d, media %.3f (tope %.2f)"
               % (len (num), sum (num) / max (1, len (num)), TOPE_NUMERO))
        print ("  la ventana entera sin razon escrita: %d" % len (enteras))
        for o in enteras[:12]:
            print ("    %-28s %.3f" % (o.get ("que"), float (o["sucio"])))
        if not ms:
            malas.append ("la corrida de tapas no paso por el M ni el S de la mesa")
        elif mediana (ms) > TOPE_MS:
            malas.append ("el M y el S de la mesa mandan repintar %.3f de la ventana (mediana) y "
                          "el tope es %.2f: el ST de al lado pinta su tapa"
                          % (mediana (ms), TOPE_MS))
        if not num:
            malas.append ("la corrida de tapas no paso por ningun pad")
        elif sum (num) / len (num) > TOPE_NUMERO:
            malas.append ("tocar un pad o una celda manda repintar %.3f de la ventana de media y "
                          "el tope es %.2f" % (sum (num) / len (num), TOPE_NUMERO))
        if enteras:
            malas.append ("%d tapas en su sitio mandan repintar la ventana entera (la primera, "
                          "%s)" % (len (enteras), enteras[0].get ("que")))
    return malas


#  Ventanas repintadas POR CUADRO. Contar LLAMADAS no separa un fotograma de
#  una banda; contar PIXELES entre el area de la ventana y los cuadros que se
#  pintaron, si.
def ventanas (r):
    if r is None: return -1.0
    vent = max (1, int (r.get ("ventana", 1)))
    cuad = max (1, int (r.get ("cuadros", 1)))
    return int (r.get ("pixeles", 0)) / float (vent) / float (cuad)


def cabezal():
    """El repintado parcial del cabezal, comprobado pixel a pixel.

    La rejilla de pasos ya no se repinta entera cuando el cabezal se mueve:
    se repinta la union de donde estaba y donde esta. Eso es correcto solo si
    TODO lo que cambia de un fotograma al siguiente cae dentro de esa union,
    y eso no se juzga leyendo el codigo. Se pinta la rejilla dos veces entera
    y se comparan los pixeles: un fallo aqui deja un rastro de marcas por la
    rejilla, que es el tipo de fallo que solo se ve en un video."""
    env = dict (os.environ)
    env["DISPLAY"] = DISPLAY
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_HEAD": "1"})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=900).stdout
    except subprocess.TimeoutExpired:
        return None
    fuera = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"cabezal"' in linea:
            try:
                d = json.loads (linea)
                fuera[str (d["cabezal"])] = d
            except Exception:
                pass
    return fuera or None


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada:", APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo");  return 1

    print ("ficha    entradas  ventanas   CPU ms / %d s" % SEGUNDOS)
    malas = []
    opacosMalos = []
    for f in FICHAS:
        r = corre (f)
        if r is None:
            print ("%-8s  --   no contesto" % (f or "(cara)"));  malas.append (f or "(cara)")
            continue
        #  EN PIXELES Y NO EN LLAMADAS, que es la leccion que la tabla de
        #  abajo aprendio y esta no: «contar llamadas no separa un fotograma de
        #  una banda». Medido, una ficha abierta sobre una maquina con DOS
        #  efectos encendidos entra 370 veces en `MainComponent::paint` con un
        #  recorte de **2622 pixeles** -las dos lamparas, que laten a
        #  proposito- y eso salia como «se repinta sola» al lado de una que
        #  repinta la ventana entera. Son 0.007 ventanas: dos ordenes de
        #  magnitud.
        fondos = int (r.get ("fondos", -1))
        vent = max (1, int (r.get ("ventana", 1)))
        equi = int (r.get ("pixeles", 0)) / float (vent)
        print ("%-8s %6d  %7.2f %10.0f%s"
               % (f or "(cara)", fondos, equi, r.get ("cpu_ms", 0.0),
                  "   <-- se repinta sola" if equi > TOPE else ""))
        if equi > TOPE:
            malas.append (f or "(cara)")
        #  Y LOS OPACOS, QUE CUBREN SU CAJA. Ver `auditOpacos`: uno que deja
        #  pixeles al aire los mezcla una vez y otra en la imagen de la cara.
        #  En el EQ se miran DOS -la cara y la curva-, o no se ha mirado la
        #  que importa.
        hu, op = int (r.get ("opacos_huecos", -1)), int (r.get ("opacos", 0))
        if hu != 0:
            malas.append ("%s: %s pixeles sin cubrir en los %d componentes opacos que se ven"
                          % (f or "(cara)", hu if hu > 0 else "no contesto:", op))
            opacosMalos.append ((f or "(cara)", hu, op))
        if f == "eq" and op < 2:
            malas.append ("eq: se miraron %d componentes opacos y la curva es uno de dos" % op)
    for f, hu, op in opacosMalos:
        print ("  %-8s %d pixeles sin cubrir en %d opacos   <-- un opaco que no cubre su caja"
               % (f, hu, op))

    print()
    caraLate = 0.0
    print ("y con la maquina SONANDO, en ventanas repintadas POR CUADRO (%d Hz)"
           % VBLANK_HZ)
    print ("ficha    por cuadro  cuadros   CPU ms")
    for f in FICHAS:
        r = corre (f, sonando=True)
        if r is None:
            print ("%-8s  --   no contesto" % (f or "(cara)"));  malas.append (f or "(cara) sonando")
            continue
        vent = max (1, int (r.get ("ventana", 1)))
        cuad = max (1, int (r.get ("cuadros", 1)))
        equi = int (r.get ("pixeles", 0)) / float (vent) / float (cuad)
        #  La cara no se juzga: ahi el cristal y los destellos de los pads SE
        #  VEN, asi que repintarlos es el trabajo. Se imprime porque es el
        #  techo contra el que se leen las demas, y porque el dia que suba hay
        #  que enterarse.
        #
        #  Y «eq» es la CARA CON EL EQ PUESTO y no una ficha: la curva vive en
        #  el plato, o sea en la cara. Se lee CONTRA la cara y no contra el tope
        #  de las fichas, que es lo que hace de esta fila una medida y no un
        #  rojo: lo que separa a las dos es lo que cuesta el analizador —dos FFT
        #  de 1024 por cuadro, la mancha de entrada y la linea de salida—.
        #
        #  Y «plato» es la MISMA cara con un efecto que no es el EQ: el visor
        #  vive en el plato igual que la curva, asi que se lee contra la cara y
        #  no contra el tope de las fichas. Lo que separa a las tres es lo que
        #  cuesta cada superficie; lo que se juzga de esta es la columna QUIETA,
        #  que es donde un visor que se repintara para siempre lo diria.
        cara = (f in ("", "eq", "plato"))
        #  Y LA MESA, desde que cada fader lleva su aguja: «que se vea constante
        #  el vumetro en cada canal». Dieciseis agujas que se mueven con la
        #  maquina sonando SE VEN, igual que el cristal de la cara, y lo que se
        #  repinta es la banda de 22 px de cada fader cuya aguja se movio un
        #  pixel; pero el cuadro las junta en un recorte -getClipBounds es la
        #  caja de las dieciseis- y sale la columna entera. Se lee CONTRA LA
        #  CARA, que es el techo de lo que se ve: una mesa que repintara mas
        #  que la cara con su cristal y sus destellos lo diria aqui.
        #  Medido: 0.000 antes de la aguja, 0.434 (mix) y 0.472 (mixc) con ella,
        #  contra 0.706 de la cara; quieta sigue en 0.01.
        mesa = (f in ("mix", "mixc"))
        if f == "": caraLate = equi        # el techo con el que se lee lo de abajo
        tope = caraLate if mesa else TOPE_SONANDO
        mal  = (not cara) and equi > tope
        print ("%-8s %8.3f %8d %9.0f%s" % (f or "(cara)", equi, cuad, r.get ("cpu_ms", 0.0),
                                        "   (se ve: no se juzga)" if cara else
                                        "   (se ve: las agujas, contra la cara)" if (mesa and not mal) else
                                        ("   <-- pinta lo que no se ve" if mal else "")))
        if mal:
            malas.append ((f or "(cara)") + " sonando")
    #  EL CUADRO DEL TELEFONO. Ver ESCALA_TELEFONO.
    print()
    print ("y el cuadro ENTERO como lo pinta un Android, a %.3f, sonando" % ESCALA_TELEFONO)
    print ("ficha    ventanas  distintos  movidos   vivo   ms (mediana / p95, de esta maquina)")
    for f in ("", "mix", "mixc"):
        r = corre (f, sonando=True, cuadro=True)
        nombre = f or "(cara)"
        if r is None or "cuadro_ventanas" not in r:
            print ("%-8s  --   no contesto" % nombre);  malas.append (nombre + " en el telefono")
            continue
        vt = float (r["cuadro_ventanas"])
        di = int (r.get ("cuadro_distintos", -1))
        mv = int (r.get ("cuadro_movidos", 0))
        vi = int (r.get ("cuadro_vivo", -1))
        print ("%-8s %8.3f %10d %8d %6d   %6.2f / %6.2f%s"
               % (nombre, vt, di, mv, vi, float (r.get ("cuadro_ms_mediana", -1)),
                  float (r.get ("cuadro_ms_p95", -1)),
                  "   <-- pinta la ventana entera en cada cuadro" if vt > TOPE_TELEFONO else
                  ("   <-- ensena algo viejo" if di != 0 else
                   ("   <-- ensena algo viejo sin tocar nada, en %s"
                    % (r.get ("cuadro_vivo_en") or "?") if vi != 0 else
                    ("   <-- no se movio nada que se vea" if mv < 1 else "")))))
        if vt < 0 or vt > TOPE_TELEFONO:
            malas.append ("%s repinta %.3f ventanas por cuadro en el telefono (tope %.2f)"
                          % (nombre, vt, TOPE_TELEFONO))
        if di != 0:
            malas.append ("%s: el cuadro del telefono difiere en %d pixeles de la cara "
                          "pintada entera" % (nombre, di))
        #  Y CON ALGO MOVIDO, que si no la segunda cifra es cero por no haber
        #  mirado: la primera version de la medida no movia nada de la cara ni
        #  de CANALES, y con la imagen rota a proposito las dos daban cero.
        if mv < 1:
            malas.append ("%s: la comprobacion de lo viejo no movio ningun mando que se vea"
                          % nombre)
        #  Y LO VIEJO QUE DEJA LA APP SOLA. Ver MINIMO_CENTINELA. -1 es que no
        #  se pudo mirar, que tampoco es un cero.
        if vi != 0:
            malas.append ("%s: tras %d s sonando la ventana difiere en %d pixeles de la "
                          "cara pintada entera, en %s caja %s"
                          % (nombre, SEGUNDOS, vi, r.get ("cuadro_vivo_en") or "?",
                             r.get ("cuadro_vivo_caja")))
        #  Mirando al menos la mitad que el cuadro no vuelve a pintar: lo que
        #  esta pendiente sale nuevo por construccion y no se compara, y un
        #  cuadro puede pedir hasta TOPE_TELEFONO. Medido: 0.80 la cara, 0.97
        #  la mesa, 0.95 CANALES.
        #  Y LO MISMO TRAS MOVER: el MUTE que se pulsaba repintaba la mesa
        #  entera, todo quedaba pendiente y en la mesa y en CANALES no se
        #  comparaba ni un pixel. Cero distintos sin mirar no es cero.
        cm = float (r.get ("cuadro_mirado", 0.0))
        if cm < 1.0 - TOPE_TELEFONO:
            malas.append ("%s: la comprobacion de lo viejo tras mover miro el %.0f %% de la "
                          "ventana y el suelo es el %.0f %%"
                          % (nombre, cm * 100.0, (1.0 - TOPE_TELEFONO) * 100.0))
        vm = float (r.get ("cuadro_vivo_mirado", 0.0))
        if vm < 1.0 - TOPE_TELEFONO:
            malas.append ("%s: la comprobacion de lo viejo sin tocar nada miro el %.0f %% de la "
                          "ventana y el suelo es el %.0f %%"
                          % (nombre, vm * 100.0, (1.0 - TOPE_TELEFONO) * 100.0))

    #  Y TRAS CADA TAPA. Ver MINIMO_CENTINELA.
    print()
    print ("y lo que la cara guardada ensenaria tras cada tapa de la app")
    total, viejas, ops = centinela()
    if total is None:
        print ("la sonda ZATI_CENTINELA no contesto")
        malas.append ("la sonda ZATI_CENTINELA no contesto: no hay nada que juzgar")
    else:
        comp  = int (total.get ("comprobadas", 0))
        mir   = float (total.get ("mirado", 0.0))
        print ("  %d apretadas comprobadas, %d con algo viejo, %.3f de la ventana mirada por "
               "apretada" % (comp, int (total.get ("viejas", -1)), mir))
        for v in viejas[:12]:
            print ("    %-28s %6d px  caja %-20s en %s"
                   % (v.get ("que"), int (v.get ("px", 0)), v.get ("caja"), v.get ("quien")))
        if len (viejas) > 12:
            print ("    ... y %d mas" % (len (viejas) - 12))
        if viejas or int (total.get ("viejas", -1)) != 0:
            peor = max (viejas, key=lambda v: int (v.get ("px", 0))) if viejas else {}
            malas.append ("%d apretadas dejan la ventana con algo viejo (la peor, %s: %d px en %s)"
                          % (max (len (viejas), int (total.get ("viejas", 0))), peor.get ("que"),
                             int (peor.get ("px", 0)), peor.get ("quien")))
        if len (viejas) != int (total.get ("viejas", -1)):
            malas.append ("el recuento dice %s apretadas con algo viejo y hay %d lineas: una "
                          "de las dos miente" % (total.get ("viejas"), len (viejas)))
        if comp < MINIMO_CENTINELA:
            malas.append ("solo se comprobaron %d apretadas y el minimo son %d: la sonda no "
                          "esta recorriendo la app" % (comp, MINIMO_CENTINELA))
        if mir < MIRADO_CENTINELA:
            malas.append ("la comprobacion tras cada tapa mira el %.1f %% de la ventana y el "
                          "suelo es el %.0f %%: cero viejas sin haber mirado"
                          % (mir * 100.0, MIRADO_CENTINELA * 100.0))

        #  Y LO QUE MANDAN REPINTAR. Ver TOPE_MS.
        malas.extend (juzga_sucio (ops))

    #  Y QUE EL VBLANK ESTE VIVO, que es la comprobacion sin la cual todo lo
    #  de arriba puede salir verde con el dibujo cayendose al reloj.
    #
    #  `pintaCuadro` cuelga del vblank y tiene una red debajo: si el peer no
    #  entrega ninguno, lo llama el temporizador de mantenimiento a la cadencia
    #  del SUELO. Esa red es correcta —«ningun camino puede dejar la app en
    #  silencio»— y hace que un vblank roto no falle nada: la app seguiria
    #  pintando, a 16.7 cuadros por segundo, con las veintiocho pruebas en
    #  verde. Es *una linea que imprime OK* aplicada a la tanda entera.
    #
    #  Asi que se corre UNA sin `ZATI_VBLANK` -o sea con la cadencia del panel
    #  de verdad- y se exige que este muy por encima del suelo. Medido en Xvfb,
    #  que no declara frecuencia y por eso JUCE cae a su valor de reserva de
    #  100 Hz: 592 cuadros en 6 s, contra los ~100 que daria el suelo.
    print()
    env = dict (os.environ)
    env["DISPLAY"] = DISPLAY
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_DEMO": "1", "ZATI_OPEN": "mix", "ZATI_SPIN": str (SEGUNDOS)})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=SEGUNDOS + 90).stdout
        panel = next ((json.loads (l.strip()) for l in out.splitlines()
                       if l.strip().startswith ('{') and '"spin"' in l), None)
    except Exception:
        panel = None
    if panel is None:
        print ("la cadencia del panel no contesto");  malas.append ("vblank")
    else:
        hz = int (panel.get ("cuadros", 0)) / float (SEGUNDOS)
        #  El suelo es `DeviceTier::relojMs`, que en el peor aparato son 10
        #  cuadros por segundo y en el mejor 30. Con el vblank vivo esto tiene
        #  que dar la frecuencia del panel; treinta y cinco deja pasar
        #  cualquier panel real y no deja pasar la red.
        #
        #  Y ES LA CADENCIA DE DIBUJO, que desde el techo de 60 ya no es la del
        #  panel: en uno de 120 Hz la app pinta uno de cada dos a proposito -el
        #  mismo trabajo dos veces no se ve mejor y le roba nucleo al hilo de
        #  audio- asi que 60 contra 120 aqui es lo correcto y no una perdida.
        #  Lo que esta linea sigue cazando es el vblank que NO llega. Ver
        #  `Tests/fluidez.py`, que es quien juzga si esa cadencia aletea.
        print ("la cadencia de dibujo: %.1f cuadros por segundo%s"
               % (hz, "   <-- el vblank no llega, dibuja el reloj" if hz < 35.0 else ""))
        if hz < 35.0: malas.append ("el vblank no llega")

    #  --- APAGAR EL MOVIMIENTO, CON DOS CIFRAS -----------------------------
    #
    #  La cara late: la lampara de un efecto encendido respira, los pads
    #  destellan, el cristal y el analizador corren treinta veces por segundo. Y
    #  no habia forma de pararlo — para alguien con sensibilidad vestibular o
    #  con epilepsia fotosensible, «no hay forma de apagarlo» es «no hay forma
    #  de usarlo».
    #
    #  Lo que se apaga es lo que se mueve SOLO. Un cabezal parado no es una app
    #  mas tranquila: es una app que ha dejado de decir por donde va el
    #  transporte. Por eso son DOS cifras y no una — solo la primera la cumple
    #  una app congelada, y solo la segunda una que no apaga nada.
    print()
    caraSin  = corre ("",    sonando=True, quieta=True)
    secSin   = corre ("sec", sonando=True, quieta=True)
    if caraSin is None or secSin is None:
        print ("la corrida con el movimiento apagado no contesto")
        malas.append ("movimiento")
    else:
        cs_ = ventanas (caraSin)
        ss_ = ventanas (secSin)
        print ("con el MOVIMIENTO apagado: la cara %.3f ventanas por cuadro "
               "(latiendo %.3f) y el cabezal de la rejilla sigue en %.3f"
               % (cs_, caraLate, ss_))
        #  El liston de la cara sale de su propia medida latiendo: apagarlo
        #  tiene que dejarla una decima parte de lo que costaba, y ahi no hay
        #  numero redondo que inventar.
        if cs_ > caraLate * 0.10:
            malas.append ("apagar el movimiento no para la cara: %.3f contra %.3f latiendo"
                          % (cs_, caraLate))
        if ss_ <= 0.0:
            malas.append ("apagar el movimiento se lleva tambien el cabezal de la "
                          "rejilla de pasos, que es lo que dice por donde va el transporte")

    print()
    cs = cabezal()
    if not cs:
        print ("el cabezal no contesto");  malas.append ("cabezal")
    else:
        #  Los dos cabezales que se repintan acotados: la rejilla de pasos y la
        #  ONDA. En la onda ademas va un rastro que crece detras, asi que la
        #  union de las dos marcas tiene que cubrirlo: no se juzga leyendo el
        #  codigo. Roto a proposito estrechando la marca: 3105 pixeles fuera.
        for quien, nombre in (("1", "rejilla de pasos"), ("onda", "onda del pad")):
            c = cs.get (quien)
            if c is None:
                print ("el cabezal de %s no contesto" % nombre)
                malas.append ("cabezal " + nombre)
                continue
            fuera = int (c.get ("fuera_de_la_zona", -1))
            print ("cabezal (%s): %d pixeles comparados, %d fuera de la zona repintada"
                   % (nombre, int (c.get ("pixeles", 0)), fuera))
            if fuera != 0:
                malas.append ("el cabezal de %s deja rastro" % nombre)

    print()
    if malas:
        print ("FALLA:", ", ".join (malas))
        return 1
    print ("ninguna ficha se repinta sola (tope %d fotogramas en %d s)" % (TOPE, SEGUNDOS))
    return 0


if __name__ == "__main__":
    sys.exit (main())

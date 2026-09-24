#!/usr/bin/env python3
"""EL HILO DE MENSAJES PARADO, QUE ES LO QUE ANDROID LLAMA «NO RESPONDE».

Dos veces salio en el telefono el cartel «Zati Sampler no responde · Esperar /
Aceptar», y la segunda con el AUDIO SONANDO -medidor a -11 dB, forma de onda
viva, 94 BPM- o sea con el hilo de audio intacto y el de mensajes parado. La
tanda anterior dedujo la causa leyendo codigo a partir de una captura de
pantalla: encontro un fallo real -la sonda del carril rapido se salia del bufer
de AAudio- y no era el sintoma. Una captura no es una medida, y esta casa tiene
banco justamente para eso.

Aqui hay DOS reglas, y las dos hacen falta:

  1. NINGUNA OPERACION DEL HILO DE MENSAJES PASA DEL PRESUPUESTO. La sonda
     ZATI_ESTADO monta la app con trabajo dentro y cronometra, una por una, las
     cosas que este hilo hace enteras: la fabrica, el maquetado, capturar el
     estado, guardarlo, la carcasa, el idioma, y guardar y abrir un proyecto.
     Lo que se mide es el TROZO MAS LARGO y no el total, porque lo que cuelga
     una app no es que una tarea dure dos segundos: es que este hilo se pase dos
     segundos sin volver al bucle.

     Fue esta regla la que encontro lo de esta tanda: la fabrica -los 64 sonidos
     del primer arranque- costaba **1691 ms de un tiron**, contra 6 de guardar
     el estado entero, 5 de traducir la app y 4 de maquetarla. Se rendian con
     cuatro hebras y se las esperaba con un `join` DENTRO del hilo de mensajes,
     asi que el paralelismo no compraba nada aqui. Hoy son 1 ms: rendir se fue a
     una hebra propia y colocar -que si es de este hilo- se reparte por el
     temporizador. El reloj de pared no ha cambiado; lo que ha cambiado es lo
     unico que Android mira.

  2. Y LA CAJA NEGRA APUNTA EL ATASCO, CON SU CIFRA Y CON SU NOMBRE. La regla 1
     solo puede cazar lo que esta maquina sabe ejecutar, y el escritorio no es
     un telefono: la mitad de lo que puede atascar a este hilo en Android -abrir
     el dispositivo, JNI, el almacenamiento compartido- aqui no existe. Asi que
     la app lleva un vigilante propio: el hilo de mensajes deja un latido y una
     etiqueta con lo que esta haciendo, y un hilo aparte mira el reloj cada
     decima de segundo y escribe el parte EN CUANTO se pasa del plazo.

     Mientras el atasco dura, no despues: si la persona pulsa «Aceptar» el
     sistema mata el proceso y lo que no estuviera en disco no existe. Por eso
     se comprueban las dos lineas -la de «>=» en curso y la definitiva- y por
     eso se comprueba tambien que al arranque siguiente la app se acuerda.

  3. Y QUE NO CANTE CUANDO NO HAY NADA. Un vigilante que avisa siempre es el
     mismo fallo que uno que no avisa nunca, y esta casa ya lo pago una vez con
     «La vez anterior se cerro en: ...» saliendo en cada arranque.

  4. Y LO MISMO PARA TODO LO QUE SE PUEDE APRETAR, QUE NO ES UNA LISTA. La
     regla 1 mide una lista ESCRITA A MANO -la fabrica, maquetar, capturar,
     guardar, la carcasa, el idioma, el proyecto- y con esa lista se encontro
     el ANR de la tanda anterior. Con esa lista no se encuentra el siguiente:
     una lista a mano solo cubre lo que alguien se acordo de apuntar, y la
     fabrica estuvo SIETE tandas colgando este hilo sin que ninguna prueba la
     mirara, porque nadie la habia apuntado.

     ZATI_TAPAS no enumera: monta la app con trabajo dentro, abre las 39
     fichas una por una, recorre el arbol entero y aprieta con su reloj al lado
     CADA tapa y CADA mando que este visible y encendido. Son 2442 apretadas y
     el mismo presupuesto de la regla 1, escrito una sola vez.

     Encontro esto: **DESHACER y REHACER bloqueaban el hilo de mensajes hasta
     8805 ms**, contra los 42 que cuesta la misma operacion sobre la pila
     recien nacida. Mil setecientos sesenta veces el presupuesto y casi el
     doble de lo que Android tarda en dar la app por colgada. Ninguna de las
     ocho operaciones de la lista a mano pasaba de 26.

     Y la regla lleva SU PROPIO CONTROL DE POBLACION, porque una prueba que
     recorre un arbol y no encuentra nada sale en verde con cero lineas: es la
     forma mas barata que hay de vaciar una medida sin que se note.

ZATI_ATASCO=ms es la entrada: el hilo de mensajes se queda ocupado ese rato a
proposito, bajo la etiqueta `banco/atasco`. Es la misma figura que ZATI_XRUN y
ZATI_LASTRE - lo que no pasa en el escritorio se convierte en una ENTRADA, para
que la regla que lo caza pueda verse fallar.
"""
import json, os, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  EL PRESUPUESTO DE UN TRAMO DEL HILO DE MENSAJES, EN ESTA MAQUINA.
#
#  250 ms y no 5000, que es lo que Android cuenta para dar la app por colgada.
#  El banco corre en un escritorio de cuatro nucleos y un telefono es varias
#  veces mas lento, asi que medir contra el limite de Android seria medir con la
#  regla puesta justo donde ya es tarde. Con 250 quedan veinte veces de margen,
#  y aun asi la fabrica se pasaba SIETE veces.
TOPE_MS = 250

#  El atasco que se inyecta, y el plazo con el que se vigila. 2600 porque es del
#  orden de lo que se vio en el telefono, y 800 para que el vigilante conteste
#  sin que cada corrida cueste cinco segundos de banco.
ATASCO_MS = 2600
PLAZO_MS  = 800


def corre (casa, ticks=40, atasco=0, plazo=0, estado=False, tapas=False, revive=False):
    """Un arranque en `casa`, que NO se borra: la caja negra vive ahi."""
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_LANG="es",
                ZATI_SIZE="412x915", DISPLAY=PANTALLA,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"))
    if   revive:
        env["ZATI_REVIVE"] = "1"
        env["ZATI_SALIDA_PREVIA"] = MUESTRA_ANR
    elif tapas:  env["ZATI_TAPAS"]  = "1"
    elif estado: env["ZATI_ESTADO"] = "1"
    else:        env["ZATI_ARRANQUE"] = str (ticks)
    if atasco: env["ZATI_ATASCO"]    = str (atasco)
    if plazo:  env["ZATI_ATASCO_MS"] = str (plazo)
    try:
        #  ZATI_TAPAS abre 39 fichas y aprieta 2442 controles, y cada apretada
        #  vuelve a abrir su ficha antes -ver auditTapas, cuidado 3-, asi que la
        #  corrida entera esta del orden de diez minutos. Con los 600 de las
        #  demas, esta prueba salia SIEMPRE por el `except` de abajo diciendo
        #  «la sonda no contesto», que es una prueba roja que no mide nada.
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=1800 if tapas else 600).stdout
    except subprocess.TimeoutExpired:
        return []
    filas = []
    for l in out.splitlines():
        l = l.strip()
        if not l.startswith ('{'): continue
        try:    filas.append (json.loads (l))
        except Exception: pass
    return filas


def caja (casa):
    """Las lineas de la caja negra de esa casa."""
    #  La biblioteca es `Music/ZATI` en el escritorio y la carpeta publica de
    #  Android en el telefono, y quien lo decide es `ProjectStore::home()`. Se
    #  busca en vez de escribir la ruta: una ruta copiada aqui se queda vieja el
    #  dia que la otra cambie, y esta prueba diria «no hay atascos» en vez de
    #  «no encuentro el fichero», que es la peor forma de aprobar.
    for raiz, _, ficheros in os.walk (casa):
        if "zati-bitacora.txt" in ficheros:
            with open (os.path.join (raiz, "zati-bitacora.txt"),
                       encoding="utf8", errors="replace") as fh:
                return [l.strip() for l in fh if l.strip()]
    return []


def atascos (lineas):
    """(en_curso, definitivos) como pares (ms, donde)."""
    curso, fin = [], []
    for l in lineas:
        if not l.startswith ("ATASCO"): continue
        resto = l[len ("ATASCO"):].strip()
        enCurso = resto.startswith (">=")
        if enCurso: resto = resto[2:].strip()
        try:
            ms = int (resto.split (" ", 1)[0])
        except Exception:
            continue
        donde = resto.split (" en ", 1)[1] if " en " in resto else "?"
        (curso if enCurso else fin).append ((ms, donde))
    return curso, fin


def regla_presupuesto (malas):
    """1. Ningun tramo del hilo de mensajes pasa de TOPE_MS."""
    casa = tempfile.mkdtemp (prefix="zati-atasco-op-")
    try:
        filas = [d for d in corre (casa, estado=True) if d.get ("atasco") == "op"]
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    if not filas:
        malas.append ("la sonda ZATI_ESTADO no contesto: no hay nada que juzgar")
        return

    for d in filas:
        reloj = d.get ("reloj")
        print ("  %-10s %5d ms%s" % (d["que"], d["ms"],
                                     "" if reloj is None else "   (reloj de pared %d)" % reloj))
    print ("  presupuesto %d ms por tramo" % TOPE_MS)

    for d in filas:
        if d["ms"] > TOPE_MS:
            malas.append ("%s bloquea el hilo de mensajes %d ms y el tope son %d"
                          % (d["que"], d["ms"], TOPE_MS))

    #  Y QUE LA FABRICA SIGA COSTANDO LO QUE CUESTA. Sin esto, dejar de
    #  rendirla -o rendir cuatro sonidos en vez de sesenta y cuatro- pasaria
    #  esta regla con un cero, que es la forma mas barata de poner en verde una
    #  medida vaciandola.
    fab = next ((d for d in filas if d["que"] == "fabrica"), None)
    if fab is None:
        malas.append ("la sonda no midio la fabrica")
    elif fab.get ("reloj", 0) < 200:
        malas.append ("la fabrica entera dice tardar %s ms de reloj: no esta rindiendo nada"
                      % fab.get ("reloj"))


def regla_vigilante (malas):
    """2. La caja negra apunta el atasco, mientras dura y al terminar."""
    casa = tempfile.mkdtemp (prefix="zati-atasco-caja-")
    try:
        corre (casa, ticks=60, atasco=ATASCO_MS, plazo=PLAZO_MS)
        lineas = caja (casa)
        curso, fin = atascos (lineas)
        for l in lineas: print ("  %s" % l)

        mio = [x for x in curso if x[1] == "banco/atasco"]
        if not mio:
            malas.append ("un atasco de %d ms no dejo linea EN CURSO: si el sistema mata "
                          "la app ahi, no queda rastro" % ATASCO_MS)
        elif mio[0][0] < PLAZO_MS:
            malas.append ("la linea en curso dice %d ms y el plazo son %d"
                          % (mio[0][0], PLAZO_MS))

        suyo = [x for x in fin if x[1] == "banco/atasco"]
        if not suyo:
            malas.append ("un atasco de %d ms no dejo la linea definitiva" % ATASCO_MS)
        else:
            ms = suyo[0][0]
            #  Banda ancha a proposito: lo que se comprueba es que la cifra sea
            #  LA DEL ATASCO y no un numero cualquiera. Por abajo no puede bajar
            #  del que se inyecto; por arriba cabe una vuelta del temporizador
            #  y lo que el sistema le robe al proceso.
            if not (ATASCO_MS - 100 <= ms <= ATASCO_MS + 1400):
                malas.append ("el atasco apuntado son %d ms y se inyectaron %d"
                              % (ms, ATASCO_MS))

        #  Y AL ARRANQUE SIGUIENTE, LA APP SE ACUERDA. Es la mitad que importa:
        #  despues de «Aceptar» el proceso muere y vuelve a abrir sin memoria.
        segunda = corre (casa, ticks=4, plazo=PLAZO_MS)
        linea = next ((d.get ("atasco_previo", "") for d in segunda
                       if d.get ("arranque") == "cara"), None)
        print ("  al volver a abrir, la app dice %r" % linea)
        if linea is None:
            malas.append ("el segundo arranque no contesto")
        elif "banco/atasco" not in (linea or ""):
            malas.append ("la app no recuerda el atasco de la vez anterior: %r" % linea)
    finally:
        shutil.rmtree (casa, ignore_errors=True)


def regla_silencio (malas):
    """3. Y sin atasco no se apunta nada."""
    casa = tempfile.mkdtemp (prefix="zati-atasco-mudo-")
    try:
        corre (casa, ticks=40, plazo=5000)
        curso, fin = atascos (caja (casa))
        print ("  sin atasco inyectado y con 5000 ms de plazo: %d lineas"
               % (len (curso) + len (fin)))
        for ms, donde in curso + fin:
            malas.append ("se apunta un atasco que no existe: %d ms en %s" % (ms, donde))
    finally:
        shutil.rmtree (casa, ignore_errors=True)


#  EL PRESUPUESTO DE LA REGLA 4, QUE NO ES EL DE LA REGLA 1, Y POR QUE.
#
#  La regla 1 mide OCHO operaciones perseguidas una por una, y con 250 ms le
#  sobra: la peor cuesta 26. La regla 4 mide 2442, o sea que es un inventario y
#  no una lista corta, y el inventario dice esto despues de arreglar lo de esta
#  tanda: 2318 apretadas por debajo de 250 ms y **124 entre 250 y 710**, todas
#  ellas DESHACER y REHACER sobre la app llena.
#
#  Lo que falta para bajarlas es el CUERPO de `applyState` -548 ms medidos, de
#  los que la cola conocida (seleccionar, refrescar los 64 pads y maquetar) solo
#  explica 300-, y trocear `applyState` no se puede hacer a medias: o repone el
#  estado entero o deja la app contando dos verdades. Es una tanda propia y esta
#  escrito aqui para que no se olvide, en vez de un liston puesto donde caiga.
#
#  Asi que hay DOS listones y cada uno mide una cosa distinta:
#
#    TOPE_ANR es el duro. 1000 ms contra los 5000 que Android cuenta para dar la
#    app por colgada: cinco veces de margen sobre una maquina que es varias
#    veces mas rapida que un telefono. Lo que esta regla existe para que no
#    vuelva a pasar es lo que se acaba de arreglar -deshacer bloqueaba **8805
#    ms**, casi el DOBLE del limite de Android- y eso lo caza con sitio de
#    sobra.
#
#    TOPE_CUANTAS es el de poblacion, y es el que impide que la banda de 250 a
#    1000 se vaya llenando sin que nadie lo note. 200 sobre las 124 de hoy: hay
#    hueco para que una ficha nueva traiga sus tapas sin poner el banco en rojo,
#    y no lo hay para que una tanda meta setenta y seis operaciones lentas.
TOPE_ANR     = 1000
TOPE_CUANTAS = 200


#  CUANTAS APRETADAS TIENE QUE HABER PARA QUE ESTO SEA UNA MEDIDA.
#
#  2442 fueron las medidas la primera vez, sobre 39 fichas. El liston se pone
#  MUY por debajo -2000- porque el numero exacto se mueve con cada tapa que la
#  app gane o pierda y un liston pegado al ultimo valor convierte cada tanda de
#  diseno en una prueba roja. Lo que este numero tiene que cazar es el desplome:
#  que las fichas dejen de abrirse, que `tapaDeBanco` se vuelva falso para
#  todas, que el recorrido se quede en el primer piso. Eso no baja de 2442 a
#  2300: baja a decenas.
MINIMO_TAPAS  = 2000
MINIMO_FICHAS = 30


def regla_todo_lo_que_se_aprieta (malas):
    """4. Ninguna TAPA ni MANDO pasa del presupuesto, y se aprietan todos."""
    casa = tempfile.mkdtemp (prefix="zati-atasco-tapas-")
    try:
        filas = corre (casa, tapas=True)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    total = next ((d for d in filas if d.get ("tapas") == "total"), None)
    ops   = [d for d in filas if d.get ("atasco") == "op" and d.get ("tapa")]

    if total is None:
        malas.append ("la sonda ZATI_TAPAS no contesto: no hay nada que juzgar")
        return

    lentas = [d for d in ops if d["ms"] > TOPE_MS]
    print ("  %d apretadas en %d fichas, %d saltadas"
           % (total["apretadas"], total["fichas"], total["saltadas"]))
    print ("  la peor: %s a %d ms" % (total["peor_en"], total["peor"]))
    print ("  %d por encima de %d ms, tope de poblacion %d"
           % (len (lentas), TOPE_MS, TOPE_CUANTAS))
    print ("  y ninguna puede pasar de %d ms" % TOPE_ANR)

    #  EL CONTROL DE POBLACION VA PRIMERO. Sin el, todo lo de abajo aprueba
    #  con una lista vacia.
    if total["apretadas"] < MINIMO_TAPAS:
        malas.append ("solo se apretaron %d controles y el minimo son %d: la sonda no "
                      "esta recorriendo la app" % (total["apretadas"], MINIMO_TAPAS))
    if total["fichas"] < MINIMO_FICHAS:
        malas.append ("solo se abrieron %d fichas y el minimo son %d"
                      % (total["fichas"], MINIMO_FICHAS))
    if len (ops) != total["apretadas"]:
        malas.append ("el recuento dice %d apretadas y hay %d lineas: una de las dos miente"
                      % (total["apretadas"], len (ops)))

    #  Y LAS QUE SE PASAN, POR SU NOMBRE. Agrupadas por control y no una linea
    #  por apretada: la misma tapa sale en varias fichas y un listado de ciento
    #  veinticuatro renglones esconde que son dos controles.
    peor = {}
    for d in lentas:
        quien = d["que"].split ("/", 1)[-1]
        if d["ms"] > peor.get (quien, (0, ""))[0]:
            peor[quien] = (d["ms"], d["que"])

    for quien, (ms, donde) in sorted (peor.items(), key=lambda kv: -kv[1][0])[:8]:
        print ("    %-28s %5d ms   (la peor de %d apretadas suyas)"
               % (quien, ms, len ([d for d in lentas
                                   if d["que"].split ("/", 1)[-1] == quien])))

    for quien, (ms, donde) in sorted (peor.items(), key=lambda kv: -kv[1][0]):
        if ms > TOPE_ANR:
            malas.append ("apretar %s bloquea el hilo de mensajes %d ms: con %d Android "
                          "da la app por colgada" % (donde, ms, 5000))

    if len (lentas) > TOPE_CUANTAS:
        malas.append ("%d apretadas pasan de %d ms y el tope de poblacion son %d"
                      % (len (lentas), TOPE_MS, TOPE_CUANTAS))


#  5. ABRIR EL DISPOSITIVO. El «no responde» volvio con la caja negra diciendo
#  «reanudada» y detras nada. Tres cosas del codigo lo multiplicaban en el
#  telefono, y las tres se cuentan igual aqui con un dispositivo falso que
#  pregunta y abre como Oboe (ver BancoDevice). Medido con el codigo de antes:
#
#    preguntas_pintar   62   cada una, en Oboe, un flujo exclusivo temporal
#    aperturas_volver    2   con su espera de hasta un segundo cada una
#    intentos_60s       60   contra un HAL que no abre, uno por segundo
#    vigilante_suelto    0   el atasco al volver no lo apuntaba nadie
#
#  Los topes son los del codigo de ahora, no un numero que quepa: una pregunta
#  -la primera de la ruta-, una apertura, y la serie 1-2-4-8-16 s da siete
#  intentos en sesenta segundos; se deja uno de holgura por el redondeo del
#  reloj.
TOPE_PREGUNTAS_PINTAR = 1
TOPE_APERTURAS_VOLVER = 1
TOPE_INTENTOS_60S     = 8
TOPE_MS_PEDIR         = 250
#  La traza de muestra de un ANR, con el hilo principal leyendo de FUSE: lo
#  que Android guarda y la app lee al arrancar (Source/SalidaPrevia.h).
MUESTRA_ANR           = os.path.join (os.path.dirname (os.path.abspath (__file__)), "anr_muestra.txt")


def regla_abrir_el_dispositivo (malas):
    """5. Volver del fondo abre UNA vez, pintar no pregunta, y un HAL muerto no ocupa el hilo."""
    casa = tempfile.mkdtemp (prefix="zati-atasco-revive-")
    try:
        filas = corre (casa, revive=True)
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    r = next ((f for f in filas if f.get ("revive") == 1), None)
    if r is None:
        malas.append ("abrir: la sonda ZATI_REVIVE no contesto"); return
    if r.get ("dispositivo") != 1:
        malas.append ("abrir: el dispositivo de banco no se abrio, y sin el todo sale cero")
        return
    print ("  %d preguntas al driver pintando 60 veces AJUSTES - AUDIO, tope %d"
           % (r["preguntas_pintar"], TOPE_PREGUNTAS_PINTAR))
    print ("  %d aperturas al volver del fondo, tope %d; bufer %d -> %d"
           % (r["aperturas_volver"], TOPE_APERTURAS_VOLVER, r["bloque"], r["bloque_vuelta"]))
    print ("  %d intentos en 60 s con el HAL muerto, tope %d; esperas si cuesta 3 s: %s"
           % (r["intentos_60s"], TOPE_INTENTOS_60S, r["esperas_caras"]))
    print ("  la caja negra vuelve a mirar al reanudar: %s"
           % ("si" if r["vigilante_suelto"] else "NO"))
    if r["preguntas_pintar"] > TOPE_PREGUNTAS_PINTAR:
        malas.append ("abrir: %d preguntas al driver pintando, tope %d"
                      % (r["preguntas_pintar"], TOPE_PREGUNTAS_PINTAR))
    if r["aperturas_volver"] > TOPE_APERTURAS_VOLVER:
        malas.append ("abrir: %d aperturas al volver del fondo, tope %d"
                      % (r["aperturas_volver"], TOPE_APERTURAS_VOLVER))
    if r["bloque_vuelta"] != r["bloque"]:
        malas.append ("abrir: vuelve con bufer %d y se fue con %d"
                      % (r["bloque_vuelta"], r["bloque"]))
    if r["intentos_60s"] > TOPE_INTENTOS_60S:
        malas.append ("abrir: %d intentos en 60 s contra un HAL muerto, tope %d"
                      % (r["intentos_60s"], TOPE_INTENTOS_60S))
    if not r["recupera"] or r["espera_tras"] != 1000:
        malas.append ("abrir: cuando el HAL vuelve no se recoge, o la espera no vuelve a 1 s")
    if max (r["esperas_caras"]) > r["tope"]:
        malas.append ("abrir: una espera pasa del tope de %d ms" % r["tope"])
    if not r["vigilante_suelto"]:
        malas.append ("abrir: tras reanudar la caja negra sigue enganchada en el aviso de antes")

    #  6. UN SERVIDOR DE AUDIO QUE TARDA SEIS SEGUNDOS. Lo que el hilo de
    #  mensajes pasa dentro de la peticion tiene que ser nada: la apertura va
    #  por su propio hilo. Y tiene que acabar abriendo igual.
    print ("  %d ms del hilo de mensajes pidiendo una apertura que tarda 6000, tope %d; abre: %s"
           % (r["ms_pedir_lento"], TOPE_MS_PEDIR, "si" if r["abre_lento"] else "NO"))
    if r["ms_pedir_lento"] > TOPE_MS_PEDIR:
        malas.append ("abrir: %d ms del hilo de mensajes esperando al audio, tope %d - eso es el cartel"
                      % (r["ms_pedir_lento"], TOPE_MS_PEDIR))
    if not r["abre_lento"]:
        malas.append ("abrir: la apertura lenta no llego a abrir el dispositivo")

    #  7. UN PAD QUE TARDA TRES SEGUNDOS EN LEERSE. La ultima linea de la caja
    #  negra del telefono antes del cartel era «ATASCO 1081 ms en pads/cargar»:
    #  la vuelta mas larga de stepPadJob tiene que ser nada, porque leer va en
    #  su hebra, y el trabajo tiene que acabar igual.
    print ("  %d ms la vuelta mas larga cargando un pad que tarda 3000, tope %d; acaba: %s"
           % (r["peor_paso_pads"], TOPE_MS_PEDIR, "si" if r["pads_acaban"] else "NO"))
    if r["peor_paso_pads"] > TOPE_MS_PEDIR:
        malas.append ("pads: %d ms del hilo de mensajes leyendo un pad, tope %d - eso es el cartel"
                      % (r["peor_paso_pads"], TOPE_MS_PEDIR))
    if not r["pads_acaban"]:
        malas.append ("pads: la carga con un pad lento no termina")

    #  8. EL PARTE DE ANDROID. Sobre la traza de muestra: el renglon dice ANR y
    #  la frase del sistema, y la cabeza es la del hilo "main" -su primera
    #  linea es la de FUSE, no la del vigilante ni la del Signal Catcher-, de
    #  ocho lineas como mucho.
    cabeza = [l.strip() for l in r["salida_cabeza"].split ("|") if l.strip()]
    print ("  renglon: %s" % r["salida_renglon"])
    print ("  cabeza del main: %d lineas, la primera: %s" % (len (cabeza), cabeza[0] if cabeza else "-"))
    if not r["salida_hay"] or not r["salida_fallo"]:
        malas.append ("android: el parte de muestra no se leyo, o no cuenta como fallo")
    if not r["salida_renglon"].startswith ("ANR · Input dispatching timed out"):
        malas.append ("android: el renglon no dice ANR y la frase: %r" % r["salida_renglon"])
    if not cabeza or "read+8" not in cabeza[0] or len (cabeza) > 8:
        malas.append ("android: la cabeza no es la del hilo main o pasa de 8 lineas: %r" % cabeza[:3])
    if any ("write+8" in l or "Object.wait" in l for l in cabeza):
        malas.append ("android: la cabeza se mete en otro hilo")


def main():
    if not os.path.isfile (APP):
        print ("no hay binario en %s" % APP); return 1

    malas = []

    print ("1. lo que cada operacion bloquea el hilo de mensajes")
    regla_presupuesto (malas)

    print()
    print ("2. la caja negra apunta el atasco")
    regla_vigilante (malas)

    print()
    print ("3. y no lo apunta cuando no lo hay")
    regla_silencio (malas)

    print()
    print ("4. y lo mismo para cada tapa y cada mando de la app")
    regla_todo_lo_que_se_aprieta (malas)

    print()
    print ("5. y abrir el dispositivo al volver del fondo")
    regla_abrir_el_dispositivo (malas)

    print()
    for m in malas: print ("FALLA ", m)
    print ("el hilo de mensajes no se para, y si se para queda escrito" if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

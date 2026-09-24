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


def corre (casa, ticks=40, atasco=0, plazo=0, estado=False):
    """Un arranque en `casa`, que NO se borra: la caja negra vive ahi."""
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_LANG="es",
                ZATI_SIZE="412x915", DISPLAY=PANTALLA,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"))
    if estado: env["ZATI_ESTADO"] = "1"
    else:      env["ZATI_ARRANQUE"] = str (ticks)
    if atasco: env["ZATI_ATASCO"]    = str (atasco)
    if plazo:  env["ZATI_ATASCO_MS"] = str (plazo)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=600).stdout
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
    for m in malas: print ("FALLA ", m)
    print ("el hilo de mensajes no se para, y si se para queda escrito" if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

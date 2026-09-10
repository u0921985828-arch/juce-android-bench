#!/usr/bin/env python3
# ============================================================================
#  DIECISEIS CANALES: la mesa entre los pads y los efectos.
#
#  «Cuando pasas de un pad a otro, los huecos de los efectos sigue igual.» Era
#  verdad y estaba medido: `selectPad` refrescaba quince cosas y ninguna era
#  del lado de los efectos, y las seis ranuras de la cara eran GLOBALES. Lo
#  unico que era del pad -cuanto manda a cada efecto- son 1344 numeros que
#  nadie gestiona.
#
#  Ahora hay dieciseis canales. El pad elige el suyo en los ajustes del pad, y
#  el canal se lleva sus seis ranuras, sus envios y sus sesenta y tres numeros;
#  el reparto no se eligio: de los veintiun tipos, dieciseis SUSTITUYEN y cinco
#  SUMAN, asi que hay un INSERTO por canal -dieciseis compresores, dieciseis
#  ecualizadores- y un ENVIO de TODOS, que es literalmente lo que un envio
#  significa.
#
#  Y esa es la queja que abrio la tanda, con sus palabras: «solo es posible que
#  un ecualizador funcione y sea colocado en un canal solo, deberia de haber un
#  plugin disponible de cada tipo para cada canal».
#
#  NINGUNA DE LAS ONCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Son fallos
#  de INDICE y de ESTADO: un pad que manda al canal equivocado se maqueta
#  perfecto -no solapa, no se sale, no corta un rotulo, no mide cero y esta
#  traducido-. Es la familia de los cinco fallos del compas del piano.
#
#  Y SE MIDE POR EL GESTO Y NO POR EL CALLBACK. Llamar a `setPadCanal` por
#  dentro se salta justo el codigo que decide si la fila de la cara sigue al
#  pad, que es donde vive todo lo que esta tanda anade: la app pulsa las tapas
#  de verdad -`canalBtns[c]->onClick`, `PadButton::mouseDown`,
#  `canFaders[c]`- y aqui solo se juzga lo que quedo.
#
#  Lo que NO esta aqui y esta en `Tests/StressTest.cpp`, porque es AUDIO y no
#  estado: que el envio salga del canal y no del pad -cola del delay 0.35355
#  contra 0.00000-, que el fader del canal baje el seco Y la cola -−6.02 dB
#  las dos- y la fila de control, que es la que sostiene todo lo demas: los 64
#  pads en el canal 0 con los envios de ayer suenan BIT A BIT igual.
#
#      python3 Tests/canales.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import display_alive


def corre (extra=None):
    """Una corrida con HOME propio: con el de verdad la app restaura la sesion
    que hubiera y no pasa por el camino que se quiere medir."""
    casa = tempfile.mkdtemp (prefix="zati-canales-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_CANALES="1")
    if extra: env.update (extra)
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "canales" in r: return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes")
        return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea de canales")
        return 1

    malas = []

    #  1. LA REJILLA DE CANALES MUEVE EL PAD, Y LA FILA DE LA CARA VA CON EL.
    #
    #     Con DOS cifras: a que canal fue el pad *y* que la fila de la cara sea
    #     la de ese canal. Solo la primera la cumple un `setPadCanal` al que no
    #     le sigue nadie -que es exactamente como estaba la app antes de esta
    #     tanda, con las seis ranuras globales- y solo la segunda la cumple una
    #     cara que cambia de fila sin mover el pad.
    print ("elegir   el pad 5 va al canal %-2d  y la fila de la cara dice %s"
           % (r["canal_del_pad"], r["fila_tras_mover"]))
    if r["canal_del_pad"] != 3:
        malas.append ("tocar el canal 3 en la rejilla dejo el pad en el canal %d"
                      % r["canal_del_pad"])
    if r["fila_tras_mover"] != [4, -1, -1, -1, -1, -1]:
        malas.append ("el pad se movio al canal 3 y la fila de la cara se quedo "
                      "en %s: los huecos de los efectos siguen igual"
                      % r["fila_tras_mover"])

    #  2. CAMBIAR DE PAD CAMBIA LA FILA — que es la queja, con sus palabras.
    #
    #     Por el GESTO: se construye un `MouseEvent` y se llama a
    #     `PadButton::mouseDown`, no a `selectPad`, que es justo donde el fallo
    #     no existe. Y con DOS pads en DOS canales, porque «la fila dice algo»
    #     lo cumple igual una fila que no se mueve nunca: lo que hay que ver es
    #     que diga COSAS DISTINTAS.
    print ("tocar    el pad 2 -> canal %d fila %s   el pad 5 -> canal %d fila %s"
           % (r["canal_pad2"], r["fila_pad2"], r["canal_pad5"], r["fila_pad5"]))
    if r["canal_pad2"] != 0 or r["fila_pad2"] != [0, -1, -1, -1, -1, -1]:
        malas.append ("tocar el pad 2 -del canal 0- dejo canal %d y fila %s"
                      % (r["canal_pad2"], r["fila_pad2"]))
    if r["canal_pad5"] != 3 or r["fila_pad5"] != [4, -1, -1, -1, -1, -1]:
        malas.append ("tocar el pad 5 -del canal 3- dejo canal %d y fila %s"
                      % (r["canal_pad5"], r["fila_pad5"]))
    if r["fila_pad2"] == r["fila_pad5"]:
        malas.append ("los dos pads viven en canales distintos y la cara enseña "
                      "la MISMA fila %s: cambiar de pad no cambia los efectos"
                      % r["fila_pad2"])

    #  3a. UN INSERTO ESTA EN LOS DIECISEIS CANALES A LA VEZ — que es la queja
    #      con la que empezo esta tanda, dicha al derecho: «solo es posible que
    #      un ecualizador funcione y sea colocado en un canal solo».
    #
    #      Esta cifra se INVIERTE respecto a la tanda anterior. Antes se exigia
    #      `[-1,…] / [7,…]` porque `ponEnRanura` recorria los dieciseis canales
    #      vaciando la ranura donde ese tipo estuviera, y eso era CORRECTO el
    #      dia que se escribio: su premisa era «su estado en el motor es uno
    #      solo, asi que dos canales serian dos ventanas al mismo aparato».
    #      Ahora hay dieciseis `Inserto`, uno por canal, asi que la premisa ya
    #      no existe y la rotura a proposito es DEJAR EL BUCLE — o sea el verde
    #      de ayer.
    print ("inserto  CMP en el 0 y luego en el 4:  %s / %s"
           % (r["inserto0"], r["inserto4"]))
    print ("envio    DLY en el 0 y luego en el 4:  %s / %s"
           % (r["envio0"], r["envio4"]))
    lleno = [7, -1, -1, -1, -1, -1]
    if r["inserto0"] != lleno or r["inserto4"] != lleno:
        malas.append ("poner CMP en el canal 4 dejo el 0 en %s y el 4 en %s: un "
                      "inserto es de CADA canal, asi que ponerlo en uno no puede "
                      "quitarselo al otro" % (r["inserto0"], r["inserto4"]))

    #  3b. Y UN ENVIO SIGUE SIENDO DE TODOS. La cifra NO cambia: una linea de
    #      retardo existe para que varias fuentes entren en la misma cola, y
    #      restringirla a un canal es exactamente lo contrario de lo que un
    #      envio significa. Los cinco que SUMAN se quedan globales.
    if r["envio0"] == [-1, -1, -1, -1, -1, -1] or r["envio0"] != r["envio4"]:
        malas.append ("poner DLY en el canal 4 dejo el 0 en %s y el 4 en %s: una "
                      "linea de retardo existe para que varias fuentes entren en "
                      "la misma cola" % (r["envio0"], r["envio4"]))

    #  3c. Y EL AJUSTE ES DEL CANAL, que es la que hace falta de verdad.
    #
    #      Con 3a y 3b imprimiendo ya la MISMA forma —`[7,…]` en los dos y
    #      `[3,…]` en los dos— solas no separan nada: las dos las cumple igual
    #      una app en la que los sesenta y tres numeros siguen siendo globales
    #      y lo unico por canal es la fila de tapas. Lo que las separa es si el
    #      NUMERO viaja con la fila.
    #
    #      TRES cifras y no dos, y la tercera es la que impide que «en el canal
    #      4 sale otro» lo cumpla un codigo que BORRA el ajuste al cambiar de
    #      canal: se vuelve al 0 y tiene que estar el que se puso. El defecto
    #      de RATIO de CMP es 4.0, asi que el canal que nadie ha tocado dice
    #      4.00 — y el 6.00 no puede salir de ahi por casualidad.
    #
    #      Medido por el MANDO —`macroCtrl2` con `sendNotificationSync`, que es
    #      el equivalente de `->onClick` en una tapa— y no llamando a
    #      `setFxParam`: lo que se prueba es la VENTANA de sesenta y tres
    #      deslizadores sobre el canal, y la ventana vive en el callback.
    print ("ajuste   RATIO de CMP: canal 0 %.2f -> canal 4 %.2f -> canal 0 %.2f"
           % (r["ajuste0"], r["ajuste4"], r["ajuste_vuelve"]))
    if abs (r["ajuste0"] - 6.0) > 0.01:
        malas.append ("el mando dejo %.2f en el canal 0 y se puso 6.00"
                      % r["ajuste0"])
    if abs (r["ajuste4"] - 6.0) <= 0.01:
        malas.append ("el canal 4 dice %.2f, lo mismo que el 0: los sesenta y "
                      "tres numeros siguen siendo globales y la fila de tapas es "
                      "lo unico que cambia" % r["ajuste4"])
    if abs (r["ajuste_vuelve"] - 6.0) > 0.01:
        malas.append ("al volver al canal 0 el ajuste vale %.2f y valia 6.00: "
                      "cambiar de canal no lee la ventana, la BORRA"
                      % r["ajuste_vuelve"])

    #  4. VACIAR UNA RANURA APAGA SU EFECTO **SOLO SI NO LE QUEDA OTRA**. Un
    #     envio puede vivir en tres canales, y quitarlo de uno no lo deja sin
    #     tapa: apagarlo ahi seria callar un delay que se sigue viendo. Con las
    #     dos cifras, o «se apago» lo cumple tambien quitarlo siempre.
    print ("vaciar   quitando una %s  ->  quitando la ultima %s"
           % (r["tras_quitar_una"], r["tras_quitar_ultima"]))
    if r["tras_quitar_una"] != 1:
        malas.append ("quitar el DLY de un canal lo apago estando puesto en otro: "
                      "un delay que se sigue viendo no puede quedarse mudo")
    if r["tras_quitar_ultima"] != 0:
        malas.append ("quitar la ULTIMA ranura del DLY lo dejo sonando y sin tapa "
                      "donde tocarlo")
    #     Y la otra mitad, que es la que la partio en dos: un INSERTO se apaga
    #     SIEMPRE. Puede estar en dos canales a la vez —eso es 3a— y el que se
    #     va es el de ESTE canal, con su propia instancia en el motor: con la
    #     guardia del envio puesta tambien aqui, el compresor del canal 0 se
    #     queda comprimiendo sin una tapa donde tocarlo.
    print ("         y quitando un INSERTO que sigue en otro canal: %s"
           % r["inserto_tras_quitar"])
    if r["inserto_tras_quitar"] != 0:
        malas.append ("quitar el CMP del canal 0 lo dejo encendido porque sigue "
                      "puesto en el 4: un inserto tiene UNA instancia por canal, "
                      "asi que la del 0 se queda sonando sin tapa")

    #  5. EL FADER Y EL MUTE DEL CANAL LLEGAN AL MOTOR, por la TIRA de la mesa
    #     y no llamando a `setCanalGain`: lo que se prueba es el camino.
    #     −6 dB son 0.5012 de ganancia lineal.
    print ("mesa     el fader del canal 6 deja %.4f en el motor y el mute %s"
           % (r["gan_canal6"], r["mute_canal6"]))
    if abs (r["gan_canal6"] - 0.5012) > 0.002:
        malas.append ("el fader del canal 6 a −6 dB dejo %.4f en el motor"
                      % r["gan_canal6"])
    if r["mute_canal6"] != 1:
        malas.append ("el mute del canal 6 no llego al motor")

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("los dieciseis canales: la rejilla mueve el pad, cambiar de pad "
           "cambia la fila, un inserto es de CADA canal con su propio ajuste y "
           "un envio de todos, vaciar apaga al inserto siempre y al envio solo "
           "si no le queda otra, y la tira llega al motor")
    return 0


if __name__ == "__main__":
    sys.exit (main())

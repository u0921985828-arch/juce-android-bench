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
from expo import PANTALLA, display_alive


def corre (extra=None):
    """Una corrida con HOME propio: con el de verdad la app restaura la sesion
    que hubiera y no pasa por el camino que se quiere medir."""
    casa = tempfile.mkdtemp (prefix="zati-canales-")
    #  DISPLAY va PUESTA y sale de `PANTALLA`: `display_alive` cae a ":99"
    #  cuando el entorno no la trae, asi que sin esta linea la comprobacion dice
    #  que si contra una pantalla y la app arranca sin ninguna. Ver `kits.py`.
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
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

    #  3b. Y EL DELAY TAMBIEN ESTA EN LOS DOS. Esta regla decia «un envio
    #      sigue siendo de todos» y pedia la misma fila por la razon contraria:
    #      una sola linea para todas las fuentes. La forma no cambia -DLY en
    #      los dos-, pero desde la Tanda 30 son DOS delays, y eso lo dicen 3d y
    #      4, que tocan uno y miran el otro.
    if r["envio0"] != [3, -1, -1, -1, -1, -1] or r["envio4"] != [3, -1, -1, -1, -1, -1]:
        malas.append ("poner DLY en el canal 4 dejo el 0 en %s y el 4 en %s: cada "
                      "canal tiene su delay" % (r["envio0"], r["envio4"]))

    #  3d. EL CASO DE LA PERSONA: REV en el canal 10 -el Skank por envio- y en
    #      el 4 -la caja-. «Cuando yo desactivo el del canal 10, se desactiva
    #      el otro, se cambia el otro.» Las dos cosas, por el MANDO y por el
    #      interruptor: el TAMANO del 4 a 0.20 y el del 10 a 0.95 tienen que
    #      quedarse cada uno en el suyo, y apagar el del 10 tiene que dejar el
    #      del 4 encendido y con su mezcla. Con la reverb de la mesa los dos
    #      leen el 0.95 que se puso el ultimo y el 4 se apaga con el 10.
    print ("REV      tamano 4 %.2f / 10 %.2f   apagado el 10: mezcla 4 %.2f / 10 %.2f  luz 4 %d"
           % (r["rev_tam4"], r["rev_tam10"], r["rev_mix4"], r["rev_mix10"], r["rev_on4"]))
    if abs (r["rev_tam4"] - 0.20) > 0.01 or abs (r["rev_tam10"] - 0.95) > 0.01:
        malas.append ("el TAMANO de la reverb del canal 4 dice %.2f y el del 10 %.2f; "
                      "se pusieron 0.20 y 0.95: mover una mueve la otra"
                      % (r["rev_tam4"], r["rev_tam10"]))
    if r["rev_on4"] != 1 or r["rev_mix4"] <= 0.0 or r["rev_mix10"] != 0.0:
        malas.append ("apagar la reverb del canal 10 dejo la del 4 con luz %d y "
                      "mezcla %.2f (y la del 10 en %.2f): apagar una apaga la otra"
                      % (r["rev_on4"], r["rev_mix4"], r["rev_mix10"]))

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

    #  4. VACIAR UNA RANURA APAGA SU EFECTO, SIEMPRE, Y SOLO EL DE ESE CANAL.
    #
    #     Esta cifra se INVIERTE en la Tanda 30. Pedia `tras_quitar_una` a UNO
    #     -«un delay que se sigue viendo no puede quedarse mudo»- porque el DLY
    #     del 0 y el del 4 eran el mismo y apagarlo en uno callaba el otro. Ahora
    #     son dos: el del 0 se apaga -o se queda sonando sin tapa- y el del 4
    #     sigue, que es `sigue_en_4` y la cifra que dice que son dos.
    print ("vaciar   quitando una %s (el del 4 sigue %s)  ->  quitando la ultima %s"
           % (r["tras_quitar_una"], r["sigue_en_4"], r["tras_quitar_ultima"]))
    if r["tras_quitar_una"] != 0:
        malas.append ("quitar el DLY del canal 0 lo dejo encendido sin tapa donde "
                      "tocarlo: el del 0 es suyo")
    if r["sigue_en_4"] != 1:
        malas.append ("quitar el DLY del canal 0 apago el del canal 4: siguen "
                      "siendo el mismo aparato")
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

    #  5b. Y EL SOLO DEL CANAL, por la misma puerta y con TRES cifras.
    #
    #      Llego del telefono —«modo solo por canal en el Mixer de canales
    #      tambien»— y hasta esta tanda el solo era del PAD y solo del pad. Un
    #      canal es un GRUPO: aislar la bateria con el solo del pad pide acertar
    #      los once pads que la forman y apagarlos de uno en uno.
    #
    #      Las tres, y ninguna sobra. `solo_canal7` dice que el bit se guardo;
    #      `hay_solo` dice que el bit CACHEADO —el que el hilo de audio lee de
    #      verdad, una vez por pad y por bloque— se entero. Sin la segunda, la
    #      primera la cumple un `setCanalSolo` que se olvida de
    #      `refreshCanalSolo`, y entonces la tapa se enciende y no calla a nadie:
    #      la app diciendo que si por fuera y muda por dentro. Y `solo_tras_apagar`
    #      es la vuelta, que es donde vive el fallo contrario: un solo que no se
    #      puede quitar deja los otros treinta y uno callados para siempre.
    print ("mesa     solo del canal 7: bit %s, hay_solo %s, y al apagarlo %s"
           % (r["solo_canal7"], r["hay_solo"], r["solo_tras_apagar"]))
    if r["solo_canal7"] != 1:
        malas.append ("la tapa SOLO del canal 7 no llego al motor")
    if r["hay_solo"] != 1:
        malas.append ("el solo del canal 7 se guardo pero anyCanalSolo dice que no "
                      "hay ninguno: el hilo de audio lee ESE bit, asi que la tapa "
                      "se enciende y no calla a nadie")
    if r["solo_tras_apagar"] != 0:
        malas.append ("apagar el solo del canal 7 dejo anyCanalSolo en %s: los otros "
                      "treinta y uno se quedan callados" % r["solo_tras_apagar"])

    #  5b-bis. SIN SOLO EN LAS DOS PAGINAS, Y CADA UNA EN SU AMBITO.
    #
    #      Del telefono: «la opcion SIN SOLO que hay en el Mixer de pads
    #      deberia estar tambien en el de canales; funcionaria por separado, una
    #      los canales y otra los pads». Estaba a medias desde que la mesa gano
    #      el solo por canal: las treinta y dos tapas de solo se pusieron y la
    #      forma de VACIARLAS no, asi que `AudioEngine::clearCanalSolo` existia
    #      y no la llamaba nadie desde la cara. Con la lista desplazandose, el
    #      canal encendido puede estar fuera de la pantalla: la mesa muda y lo
    #      que lo explica sin verse.
    #
    #      CUATRO cifras y ninguna sobra, porque «por separado» son DOS
    #      direcciones. Se encienden un solo de canal y uno de pad a la vez y se
    #      vacia desde CANALES: el de canal cae y el de pad SIGUE. Luego la
    #      vuelta desde PADS. Con una sola direccion, lo pedido lo cumple un
    #      `clearSolo()` que vacia los dos ambitos — que es justo lo contrario.
    #
    #      Y la quinta, `vaciar_visible_canales`, porque un gesto que funciona y
    #      no se ve no lo usa nadie: es el fallo que se esta arreglando, no uno
    #      distinto.
    print ("mesa     SIN SOLO en canales: visible %s, canal %s, y el pad sigue %s "
           "| desde pads: pad %s, canal %s"
           % (r["vaciar_visible_canales"], r["canal_tras_vaciar_canales"],
              r["pad_tras_vaciar_canales"], r["pad_tras_vaciar_pads"],
              r["canal_tras_vaciar_pads"]))
    if r["vaciar_visible_canales"] != 1:
        malas.append ("la tapa SIN SOLO no sale en la pagina de CANALES")
    if r["canal_tras_vaciar_canales"] != 0:
        malas.append ("SIN SOLO en CANALES dejo anyCanalSolo en %s: no vacio nada"
                      % r["canal_tras_vaciar_canales"])
    if r["pad_tras_vaciar_canales"] != 1:
        malas.append ("SIN SOLO en CANALES se llevo por delante el solo del PAD: "
                      "son dos ambitos y se pidieron por separado")
    if r["pad_tras_vaciar_pads"] != 0:
        malas.append ("SIN SOLO en PADS dejo anySolo en %s: no vacio nada"
                      % r["pad_tras_vaciar_pads"])
    if r["canal_tras_vaciar_pads"] != 1:
        malas.append ("SIN SOLO en PADS se llevo por delante el solo del CANAL: "
                      "son dos ambitos y se pidieron por separado")

    #  5c. UN EFECTO ENTRA SONANDO, que es la otra peticion de la misma tanda:
    #      «el envio predeterminado al mixer del efecto debe ser al 100 como
    #      Default, pero que este activado tambien el efecto cuando se mete en el
    #      Slot».
    #
    #      Poner un efecto en una ranura dejaba el interruptor apagado y
    #      `canalSend` en su cero de fabrica, o sea que abrir el menu, elegir DRV
    #      y cerrar no movia un decibelio. Es de los que no fallan: la tapa se
    #      pinta LLENA y suena igual que vacia.
    #
    #      TRES cifras. El envio y el interruptor son las dos mitades —encenderlo
    #      sin envio deja un efecto al que no le llega nada, y para un ENVIO eso
    #      es silencio literal; el envio sin encenderlo deja el bus alimentando un
    #      aparato parado—. Y la tercera, el VECINO, es la que impide que «entra
    #      al maximo» lo cumpla un codigo que lo sube en los treinta y dos: eso
    #      meteria en la reverb treinta y un canales que nadie mando.
    print ("ranura   DRV al entrar en el canal 2: envio %.2f, encendido %s, "
           "y el canal 3 en %.2f"
           % (r["envio_al_entrar"], r["encendido_al_entrar"], r["envio_del_vecino"]))
    if abs (r["envio_al_entrar"] - 1.0) > 0.001:
        malas.append ("poner DRV en una ranura dejo su envio en %.2f y no en 1.00: "
                      "la tapa se pinta llena y no suena" % r["envio_al_entrar"])
    if r["encendido_al_entrar"] != 1:
        malas.append ("poner DRV en una ranura lo dejo APAGADO: el gesto entero no "
                      "mueve un decibelio")
    if r["envio_del_vecino"] > 0.001:
        malas.append ("poner DRV en el canal 2 subio tambien el envio del canal 3 a "
                      "%.2f: un envio es de todos pero canalSend es por canal"
                      % r["envio_del_vecino"])

    #      Y LA CUARTA, QUE ES LA QUE FALTABA Y LA QUE SE PAGO: que el PAD acabe
    #      mandando de verdad.
    #
    #      Las tres de arriba salian en verde y el efecto NO SONABA. El envio se
    #      escribe en el CANAL y quien tiene que mandar es el pad, que nace SIN
    #      canal -los sesenta y cuatro-: `refrescaSendMask` lo dice entero, «un
    #      pad SIN canal no manda a ningun bus». O sea interruptor encendido,
    #      envio al maximo, y un bus al que no llega una muestra.
    #
    #      Llego del telefono como «el EQ no funciona o el envio no termina en el
    #      efecto», y las dos mitades de la frase eran la misma cosa.
    #
    #      Se mide sobre `padSendMask` —lo que el hilo de audio lee— y no sobre
    #      el canal del pad: tener canal es el MEDIO, tener el bit puesto es el
    #      fin. Preguntando por el canal, esto lo cumpliria un `setPadCanal` a un
    #      canal que no manda a ningun efecto.
    #
    #      Rota a proposito quitando la linea que mete el pad en el canal:
    #      `envio 1, encendido 1, MANDA 0`.
    print ("ranura   y el pad elegido manda a algun bus: %s" % r["manda_al_entrar"])
    if r["manda_al_entrar"] != 1:
        malas.append ("poner DRV en una ranura dejo al pad elegido SIN mandar a "
                      "ningun bus: el envio y el interruptor estan puestos y la "
                      "senal no llega, que es «el efecto no hace nada»")

    #      Y LA SECUENCIA DEL TELEFONO ENTERA, con sus cinco cifras.
    #
    #      «Meto un sonido, pongo una caja en el pad 2, que esta sin canal. Lo
    #      linkeo al 3, pongo el EQ en el 3, en el slot 1, y ese EQ ni analiza
    #      nada ni modifica nada.»
    #
    #      Con una sola cifra no se sabe DONDE se rompe, asi que van las cinco
    #      del camino: en que canal quedo el pad, cual edita la cara, en que
    #      canal acabo el EQ, si el pad manda, y si la mezcla y el envio
    #      llegaron al canal donde el efecto esta puesto. La primera corrida las
    #      dio TODAS bien —3, 3, 3, manda, 1.00, 1.00— y por eso se supo que el
    #      fallo estaba mas abajo, en el hilo de audio y no en el enrutado.
    print ("ranura   la secuencia del telefono: pad en %s, cara en %s, EQ en %s, "
           "manda %s, mezcla %s, envio %s"
           % (r["tf_canal_pad"], r["tf_canal_cara"], r["tf_canal_eq"],
              r["tf_manda"], r["tf_mezcla"], r["tf_envio"]))
    if (r["tf_canal_pad"] != 3 or r["tf_canal_cara"] != 3 or r["tf_canal_eq"] != 3
            or r["tf_manda"] != 1 or r["tf_mezcla"] < 0.99 or r["tf_envio"] < 0.99):
        malas.append ("la secuencia del telefono no acaba con el EQ enrutado: "
                      "pad %s, cara %s, EQ %s, manda %s, mezcla %.2f, envio %.2f"
                      % (r["tf_canal_pad"], r["tf_canal_cara"], r["tf_canal_eq"],
                         r["tf_manda"], r["tf_mezcla"], r["tf_envio"]))

    #      Y EL PAN DEL CANAL VUELVE DEL FICHERO.
    #
    #      `cpan` y `canc` son propiedades NUEVAS, y una propiedad que nadie
    #      mide es una que se pierde en la primera tanda que toque el guardado
    #      -acaba de pasar con la cifra de pads recuperados, que llevaba una
    #      tanda rota-. Se guarda -0.75 y 1.60 porque ninguno de los dos puede
    #      salir de un defecto: el pan nace centrado y el ancho en uno.
    print ("canal    el pan del canal vuelve del fichero: %s y ancho %s"
           % (r["pan_vuelve"], r["anc_vuelve"]))
    if abs (r["pan_vuelve"] + 0.75) > 0.01 or abs (r["anc_vuelve"] - 1.60) > 0.01:
        malas.append ("el pan del canal no vuelve del proyecto: %s y %s en vez de "
                      "-0.75 y 1.60" % (r["pan_vuelve"], r["anc_vuelve"]))

    #  6. EL BANCO DE LA REJILLA: que se llegue a los de detras.
    #
    #     Con treinta y dos canales la rejilla sigue siendo de cuatro por cuatro
    #     —dieciseis en fila estan medidos y no caben, 26 px en 280— y lo que
    #     crece es el numero de bancos, exactamente como los pads llegan a
    #     sesenta y cuatro con A B C D.
    #
    #     Con DOS cifras, que una se engaña: el chip MUEVE la rejilla *y* NO
    #     cambia el canal del pad. Solo la primera la cumple un chip que ademas
    #     reasigna —o sea pasear seria tocar, que es lo contrario de la fila A B
    #     C D— y solo la segunda la cumple un chip muerto, que deja los
    #     dieciseis de detras inalcanzables. Mas la tercera, que es la que dice
    #     que la celda sigue cumpliendo el dedo: la rejilla no crece.
    #     Y la CUARTA, que es la que el chip no puede decir: la rejilla se abre
    #     DONDE ESTA EL PAD. Con el pad ya en el canal 21, volver a abrirla
    #     tiene que empezar en el 17 —o sea banco B— y no en el 01 con ninguna
    #     tapa encendida, que es un menu que no dice donde estas.
    #     Y la QUINTA, que es la que ninguna de las once reglas de `expo.py`
    #     puede ver y por la que la app publica esta cifra: son treinta y dos
    #     tapas para dieciseis celdas, asi que volver del banco B al A tiene que
    #     dejar DIECISEIS vivas y no treinta y dos. El banco de maqueta no llega:
    #     su pantalla `canal` abre el selector en el banco A y no lo mueve, y
    #     cerrarlo vacia los limites — medido, quitar el `setBounds ({})` deja
    #     las 1400 corridas con CERO y RESIDUO a cero.
    print ("banco    %d bancos de %d; el chip B deja la primera celda en el canal "
           "%d (%s), el pad sigue en el %d, al reabrir empieza en el %d y al "
           "volver al A quedan %d tapas vivas"
           % (r["bancos"], r["canales"] // r["bancos"], r["banco_primera"] + 1,
              r["banco_celda"], r["banco_pasear"], r["banco_reabre"] + 1,
              r["banco_vivas"]))
    if r["bancos"] * 16 != r["canales"]:
        malas.append ("%d canales en %d bancos de dieciseis no cuadran"
                      % (r["canales"], r["bancos"]))
    if r["banco_primera"] != 16:
        malas.append ("el chip B deja la rejilla empezando en el canal %d y tiene "
                      "que empezar en el 17: los dieciseis de detras no se alcanzan"
                      % (r["banco_primera"] + 1))
    if r["banco_pasear"] != 0:
        malas.append ("pasear por los bancos movio el pad al canal %d: elegir un "
                      "banco es MIRAR y tocar una celda es elegir"
                      % r["banco_pasear"])
    if r["banco_elegir"] != 20:
        malas.append ("tocar el canal 21 del banco B dejo el pad en el canal %d"
                      % (r["banco_elegir"] + 1))
    if r["banco_vivas"] != 16:
        malas.append ("volver del banco B al A deja %d tapas visibles y con "
                      "limites para %d celdas: las del banco de atras se quedan "
                      "con las coordenadas de la pasada anterior"
                      % (r["banco_vivas"], r["canales"] // r["bancos"]))
    if r["banco_reabre"] != 16:
        malas.append ("con el pad en el canal 21, reabrir el selector empieza en "
                      "el canal %d: la rejilla no se abre donde esta el pad"
                      % (r["banco_reabre"] + 1))
    try:
        cw, ch = [int (v) for v in r["banco_celda"].split ("x")]
    except Exception:
        cw = ch = 0
    if cw < 40 or ch < 40:
        malas.append ("la celda de la rejilla de canales mide %s y el dedo pide 40"
                      % r["banco_celda"])

    #  8. EL FADER DE LA MESA SE VE ENTERO Y DICE SUS DECIBELIOS.
    #
    #     Llego del telefono: «el fader tampoco es muy especifico con los
    #     decibelios que tocas, ni tactil». Era el carril de JUCE: cuatro
    #     pixeles de pista en una celda de cuarenta y dos, y la cifra en una
    #     casilla aparte que la maqueta tiraba en un movil de 412 -la pagina
    #     de PADS no ensenaba NINGUN decibelio-. La app PINTA el fader del pad
    #     0 solo y cuenta: a 0 dB que parte de la celda tiene tinta, y a -60 dB
    #     que parte de la mitad derecha no es fondo, que es la cifra. Y cuantos
    #     faders conservan casilla aparte, que tiene que ser ninguno.
    #
    #     El binario de antes no volcaba estas claves (-1 en las tres, cuatro
    #     FALLA). Medido despues, 110x40 px: tinta 0.78, letra 0.40, casillas
    #     0. Roto a proposito -quitando la marca "fader" y dejando el carril
    #     de JUCE sin casilla-: tinta 0.137 y letra 0.119, que es la pista de
    #     cuatro pixeles cruzando la mitad derecha; por eso la letra pide 0.2
    #     y no un «algo»: con 0.03 el carril pelado pasaba.
    #
    #     Y LA BARRA ES MAS FINA QUE SU CELDA, del telefono tras verla a toda
    #     la fila: «las barras mas finas no?». La app mide la BANDA pintada
    #     -filas de pixeles con tinta- y cuenta tinta y cifra dentro de ella.
    #     La celda sigue en 40 (lo que se toca); la banda tiene que ser por lo
    #     menos 12 px mas baja que la celda (se ve fina) y de 16 para arriba
    #     (cuatro carriles de JUCE: se ve). Roto a proposito -grosorFader a la
    #     celda entera- la banda vale 40 y esta regla FALLA; la de la tinta
    #     no, que una barra gorda tambien esta llena.
    #
    #     Y MAS FINA TODAVIA, del telefono tras verla de 22: «no tan altos los
    #     faders, son muy gruesos; nada mas grueso de lo normativo». La barra
    #     pasa a `Metrics::md` (12) con la aguja de 6, y la cifra ya no cabe
    #     DENTRO de la barra: va centrada en la celda, encima de la barra,
    #     como en cualquier mesa. La banda se mide por las filas con tinta, y
    #     la cifra a 0 dB cae a la izquierda del pomo, asi que con la letra
    #     por medio la banda puede leer uno o dos pixeles mas que la barra:
    #     el techo es la MITAD de la celda (fina) y el suelo 10 (se ve: dos
    #     carriles de JUCE y medio, y la aguja de 6 dentro con un pixel de
    #     margen por lado). Medido con la barra de 22: banda 22; con la de
    #     12: la cifra de abajo.
    #
    #     Y A LA MITAD DE LA CELDA, del telefono tras verla de 16: «te he
    #     dicho que lo hagas mas alto, no menos ancho; el alto es de abajo
    #     arriba». Veinte, el techo de esta regla, y la cifra cabe dentro con
    #     tres de aire, como en el master. El techo se escribe como 20 -la
    #     mitad de `hit`- y no como la mitad de lo que mide el Slider, que
    #     son 38 por el aire denso de la fila: 19 habria tirado la barra de
    #     20 por un pixel que no es de la barra.
    tinta  = r.get ("fader_tinta", -1.0)
    letra  = r.get ("fader_letra", -1.0)
    cajas  = r.get ("fader_cajas", -1)
    alto   = r.get ("fader_alto", 0)
    ancho  = r.get ("fader_ancho", 0)
    banda  = r.get ("fader_banda", -1)
    print ("fader    %dx%d px, banda de %d: tinta a 0 dB %.2f, letra a -60 dB %.2f, casillas aparte %d"
           % (ancho, alto, banda, tinta, letra, cajas))
    if tinta < 0.6:
        malas.append ("el fader del pad 0 a 0 dB solo pinta el %.0f%% de su banda: "
                      "sigue siendo un carril de cuatro pixeles" % (tinta * 100.0))
    techo = 20   # la mitad de la celda de `hit` (40); los bordes del Slider miden 38
    if banda < 10 or banda > techo:
        malas.append ("la banda del fader del pad 0 mide %d px en una celda de %d: tiene que "
                      "verse fina (hasta %d) y verse (desde 10)" % (banda, alto, techo))
    if letra < 0.2:
        malas.append ("el fader del pad 0 a -60 dB no lleva cifra dentro (%.3f de la "
                      "mitad derecha no es fondo)" % letra)
    if cajas != 0:
        malas.append ("%d faders de la mesa conservan una casilla de texto aparte: "
                      "la cifra va dentro de la barra" % cajas)
    #     Y EL ALTO DE LA CELDA ES EL QUE LA TARJETA DA, ya no 36: desde que
    #     las dieciseis filas entran en la ficha -la regla quince- la fila
    #     mide lo que toca a cada una, 36 en 412x915 y 32 en 393x851, y el
    #     fader cuatro menos. Lo que no puede faltar es la banda entera con
    #     su aire: una celda mas baja que la banda la recorta.
    if alto < techo + 4:
        malas.append ("el fader del pad 0 mide %d px de alto: la banda de %d no cabe con su aire"
                      % (alto, techo))

    #  10. CADA FADER LLEVA DENTRO SU AGUJA, Y LA AGUJA DICE LO QUE SUENA.
    #
    #     Del telefono, con las barras ya gordas: «que se vea el relleno menos,
    #     con menos opacidad y que se vea constante el vumetro en cada canal,
    #     como pega». La app abre la mesa, dispara el pad 0, bombea audio por
    #     el aparato del banco y mide: el pico que el motor dice del pad 0 y
    #     del pad 1 -que no ha sonado y tiene que dar CERO, o la mesa estaria
    #     ensenando el mismo nivel en las dieciseis barras-, a cuantos pixeles
    #     de su barra queda la aguja del pad 0, y que parte de esos pixeles
    #     cambia de color al pintarla: el fader a -60 dB con la aguja puesta
    #     contra el mismo fader con la aguja a cero.
    #
    #     Roto a proposito -sin dibujar la aguja en el ramal "fader"- la tinta
    #     cae a 0.000 con el pico y la aguja intactos: la regla de la tinta es
    #     la que ve el dibujo, las otras dos ven el motor y la cuenta.
    vuPad0 = r.get ("vu_pad0", -1.0)
    vuPad1 = r.get ("vu_pad1", -1.0)
    vuAguja = r.get ("vu_aguja", -1)
    vuTinta = r.get ("vu_tinta", -1.0)
    print ("aguja    pad 0 a %.3f y pad 1 a %.3f: aguja de %d px, tinta %.3f"
           % (vuPad0, vuPad1, vuAguja, vuTinta))
    if vuPad0 < 0.05:
        malas.append ("el pad 0 suena y el motor dice un pico de %.3f para la mesa" % vuPad0)
    if vuPad1 != 0.0:
        malas.append ("el pad 1 no ha sonado y el motor le da un pico de %.3f" % vuPad1)
    if vuAguja < 8:
        malas.append ("la aguja del fader del pad 0 queda a %d px: no se ve" % vuAguja)
    if vuTinta < 0.5:
        malas.append ("la aguja del fader del pad 0 solo cambia el %.0f%% de sus pixeles: "
                      "no se dibuja" % (vuTinta * 100.0))

    #  9. LA BARRA DE LA MESA ES DE LA CASA, Y NO SE COBRA DE LAS FILAS.
    #
    #     Del telefono, dos veces. Primero «que no se coma espacio a la
    #     derecha»: el Viewport se restaba los ocho pixeles de su barra del
    #     ancho de las filas. Luego, con la de JUCE a cuatro en el margen: «la
    #     barra que aparece en el mixer no deberia ser la de JUCE, deberia ser
    #     hecha en la app, como alguna barra mas que aparece por ahi». Ahora
    #     es `BarraVista`, la del piano y la cancion, en vertical y contando
    #     desde arriba: un dedo de ancho al final de las filas, a `halfGap` de
    #     la S de cada fila, y acaba donde acaba la cruz del titulo.
    #
    #     Se miden cinco cosas: que la de JUCE mida CERO, que la de la casa
    #     este (hay dieciseis filas en una tarjeta de siete) y mida un dedo,
    #     el hueco entre la S y la barra, el borde contra la cruz, y que la
    #     barra MUEVA: un toque en su pie pasa una pagina de filas y uno en la
    #     cabeza vuelve a la primera, por el mismo `mouseDown` que el dedo.
    #     Medido en 412x915: JUCE 0, barra 40, hueco 4, borde 0, salta 13
    #     filas, vuelve a 0. Roto a proposito -la barra contando desde abajo
    #     como el piano- el toque en el pie no mueve nada: salta 0.
    #
    #     Y SE MIDE EN 360x640, no en 412x915: desde que las dieciseis filas
    #     entran en la ficha -la regla quince- en 412x915 no hay nada que
    #     arrastrar y la barra no sale, que es lo correcto y lo que la quince
    #     pide. Donde ni a 24 por fila entran, la mesa vuelve a ser la de
    #     antes: filas de 44 con la barra. Medido en 360x640: el Viewport
    #     ensena 308 y las filas miden 704; JUCE 0, barra 40 a 4 de la S,
    #     borde 0, salta 7 filas, vuelve a 0.
    vista = r.get ("mesa_vista", -1)
    filas = r.get ("mesa_filas", -2)
    barra = r.get ("mesa_barra", -1)
    borde = r.get ("mesa_borde", -1)
    print ("mesa     en 412x915 el Viewport ensena %d px y las filas miden %d; barra de JUCE %d px; "
           "la S acaba a %d px de la cruz" % (vista, filas, barra, borde))
    if barra != 0:
        malas.append ("la mesa sigue ensenando la barra de JUCE, de %d px" % barra)
    if borde != 0:
        malas.append ("la S de la primera fila acaba %d px antes que la cruz del titulo" % borde)
    r360 = corre ({"ZATI_SIZE": "360x640"})
    if r360 is None:
        malas.append ("la app no publico la linea de canales en 360x640")
    else:
        vista = r360.get ("mesa_vista", -1)
        filas = r360.get ("mesa_filas", -2)
        barra = r360.get ("mesa_barra", -1)
        borde = r360.get ("mesa_borde", -1)
        barraApp = r360.get ("mesa_barra_app", -1)
        hueco = r360.get ("mesa_hueco", -1)
        salta = r360.get ("mesa_salta", -1)
        vuelve = r360.get ("mesa_vuelve", -1)
        print ("mesa     en 360x640 el Viewport ensena %d px de alto y las filas miden %d; barra de JUCE %d px, "
               "barra de la casa %d px a %d de la S; acaba a %d px de la cruz; un toque en el pie salta %d "
               "filas y uno en la cabeza vuelve a %d"
               % (r360.get ("mesa_vista_alto", -1), r360.get ("mesa_filas_alto", -1), barra, barraApp,
                  hueco, borde, salta, vuelve))
        if barra != 0:
            malas.append ("en 360x640 la mesa sigue ensenando la barra de JUCE, de %d px" % barra)
        if barraApp < 36:
            malas.append ("en 360x640 la barra de la casa de la mesa mide %d px: tiene que estar (hay "
                          "dieciseis filas en una tarjeta de siete) y medir un dedo" % barraApp)
        if not 1 <= hueco <= 8:
            malas.append ("en 360x640 la S de la primera fila queda a %d px de la barra: pide aire, y no "
                          "mas de sm" % hueco)
        if borde != 0:
            malas.append ("en 360x640 la barra acaba %d px antes que la cruz del titulo" % borde)
        if salta < 1:
            malas.append ("en 360x640 un toque en el pie de la barra no mueve la mesa (salta %d filas)" % salta)
        if vuelve != 0:
            malas.append ("en 360x640 un toque en la cabeza de la barra no vuelve a la primera fila "
                          "(queda en %d px)" % vuelve)

    #  11. EN LA FILA DE UN PAD ENTRA TODO: pan, fader, M y S.
    #
    #     Del telefono, a 400 dp de ancho y con la foto delante: la fila
    #     traia fader, M y S, y el pan no estaba -la escalera lo tira donde
    #     al fader no le quedan 96 con el pan puesto- y «haz los botones
    #     menos anchos, tiene que entrar todo en una fila». Ahora S y M ceden
    #     antes que el pan: se estrechan lo justo, nunca por debajo de 32, y
    #     solo si con eso el pan entra. Se mide en 400x900 -la pantalla de la
    #     foto, que no es ninguna de las nueve de expo.py-: el pan esta y
    #     mide un dedo, M mide al menos 32 y el fader conserva su suelo: 96
    #     de fila, que son 92 de bordes con `aireTapa` a cada lado, los
    #     mismos 92 que mide en 412x915. Y en 412x915, donde el pan ya
    #     entraba, M sigue en 40: estrechar donde no hace falta es pagar sin
    #     cobrar (la primera version de la escalera lo hacia: M a 39 en 412
    #     por contar 144 donde la cuenta del pan dice 143).
    #     Medido: 400x900 pan 44, M 35, fader 92; 412x915 M 40. Roto a
    #     proposito -la escalera quitada, M y S en 40 siempre- el pan de
    #     400x900 mide 0.
    #     Y DESDE QUE EL PAN ES UN KNOB su celda es un dedo y su aire -44,
    #     que son 40 de bordes- y no un tercio de la fila: un knob es
    #     redondo y el ancho de mas era hueco entre el fader y el pan. La
    #     cuenta de la escalera pasa a 96 + 44. Medido: 400x900 pan 40, M
    #     37, fader 92; 393x851 pan 40, M 34, fader 93; 412x915 M 40 y el
    #     fader 95, tres mas que con el pan a tercios.
    #
    #     Y LA CELDA DEL PAN MIDE LO QUE SU TINTA, con tres switches en la
    #     fila y el fader con un suelo de 64 -un dedo y medio- en vez de 96.
    #     Del telefono: «la distancia que quiero que guarden es la que hay
    #     entre los botones M y S»: la celda de 44 con el dial de 20 dentro
    #     era doce pixeles de hueco a cada lado del dial, y «tres switch, de
    #     hecho; que no sean botones, el mute y el solo; y uno que sea mono
    #     o estereo» son tres celdas donde habia dos. Con el suelo de 96 los
    #     tres entraban en 393x851 a 34 con el fader en 97 -la cuenta sobre
    #     la fila medida- pero bajo 384 se caia el ST y en 360x640 tambien
    #     el pan; con 64 entran los tres y el pan hasta 360x800, y el fader
    #     del movil mide 79 en vez de 97. La escalera: los tres switches
    #     se estrechan juntos hasta 32, luego se cae el ST, luego el pan
    #     (regla diecisiete, en 360x640). Aqui: el pan esta y mide su tinta
    #     (regla dieciseis), M no baja de 32, el fader conserva su suelo, y
    #     en 412x915 M sigue en 40. Medido: 400x900 pan 34, M 40, fader 84;
    #     393x851 pan 34, M 40, fader 79; 412x915 M 40 y el fader 92.
    r400 = corre ({"ZATI_SIZE": "400x900"})
    if r400 is None:
        malas.append ("la app no publico la linea de canales en 400x900")
    else:
        print ("fila     en 400x900 el pan del pad 0 mide %d (tinta %d), M %d y el fader %d; en 412x915 M mide %d"
               % (r400.get ("mesa_pan", -1), r400.get ("mesa_pan_tinta", -1), r400.get ("mesa_ms", -1),
                  r400.get ("fader_ancho", -1), r.get ("mesa_ms", -1)))
        if r400.get ("mesa_pan", 0) < 30 or r400.get ("mesa_pan", 0) != r400.get ("mesa_pan_tinta", -1):
            malas.append ("en 400x900 el pan del pad 0 no esta en la fila o su celda no mide su tinta (celda %d, "
                          "tinta %d): los switches tienen que ceder antes que el pan y la celda es el bloque"
                          % (r400.get ("mesa_pan", 0), r400.get ("mesa_pan_tinta", -1)))
        if r400.get ("mesa_ms", 0) < 32:
            malas.append ("en 400x900 M mide %d: nunca por debajo de 32" % r400.get ("mesa_ms", 0))
        if r400.get ("fader_ancho", 0) < 64:
            malas.append ("en 400x900 el fader del pad 0 mide %d: el pan no se paga con el fader (suelo 64)"
                          % r400.get ("fader_ancho", 0))
        if r.get ("mesa_ms", 0) != 40:
            malas.append ("en 412x915 M mide %d y la fila entera entraba con 40: estrechar donde no hace "
                          "falta es pagar sin cobrar" % r.get ("mesa_ms", 0))

    #  12. EL FADER PINTA SU CELDA, LA CIFRA CABE Y EL PAN ES UN KNOB.
    #
    #     Del telefono, con la foto de la tanda anterior delante: «te he
    #     dicho que lo hagas mas alto, no menos ancho», «eso no es un knob,
    #     es un joystick: a la izquierda la L y a la derecha la R»,
    #     «demasiado hueco entre el titulo y el fader, y entre el fader y el
    #     knob» y «tiene que ser algo mas grande, que si no, no entra». Lo
    #     medido dijo por que: la barra se pintaba en 68 de sus 92 px -doce
    #     de pomo de JUCE por cada lado que nadie habia pedido-, el pan era
    #     un deslizador con 19 px de recorrido, y la cifra se cortaba
    #     («-5.», «+», «-») cuando no cabia a ningun lado del pomo.
    #     Ahora: lo pintado llega a los bordes (a dos pixeles, el filo
    #     redondeado), a -10.3 dB -el pomo por el medio- la cifra que se
    #     pinta cabe entera en su lado (se acorta: sin unidad, y luego el
    #     entero), y el pan del pad 0 es rotatorio.
    #     Medido en 412x915: pintado 95 de 95, banda 20, a -10.3 dB pinta
    #     «-10.3» (sin unidad) y cabe, knob; en 800x1280 cabe «-10.3 dB». La
    #     tinta a 0 dB pasa de 0.74 -que era exactamente 68 de 92- a 1.00, y
    #     la letra a -60 dB baja de 0.59 a 0.28 porque la misma cifra es
    #     menos parte de una barra mas ancha. Roto -el binario de la tanda
    #     anterior, sin cambiar nada-: no publica lo pintado ni `cabe`, y el
    #     pan no es rotatorio: tres FALLA.
    pintado = r.get ("fader_pintado", -1)
    cabe    = r.get ("fader_cabe", -1)
    cifra   = r.get ("fader_cifra", "")
    knob    = r.get ("mesa_knob", 0)
    print ("fader    pintado %d de %d px de ancho; a -10.3 dB pinta «%s» y %s; el pan del pad 0 %s"
           % (pintado, ancho, cifra, "cabe" if cabe == 1 else "NO cabe",
              "es un knob" if knob == 1 else "NO es un knob"))
    if pintado < 0:
        malas.append ("la app no publica cuanto pinta a lo ancho el fader del pad 0")
    elif pintado < ancho - 2:
        malas.append ("el fader del pad 0 pinta %d de sus %d px: la barra tiene que llegar a los bordes"
                      % (pintado, ancho))
    if cabe < 0:
        malas.append ("la app no publica si la cifra del fader del pad 0 cabe")
    elif cabe != 1:
        malas.append ("a -10.3 dB la cifra del fader del pad 0 no cabe en su lado (pinta «%s»): "
                      "se acorta antes que cortarse" % cifra)
    if knob != 1:
        malas.append ("el pan del pad 0 no es un knob")

    # 13. Y EL DIAL DEL KNOB MIDE LO QUE LA BANDA DEL FADER. Del telefono, con
    #     la foto del dial de 28 en la celda de 38: «muy grande el knob, no?
    #     como que no pega las proporciones». La proporcion de la fila la da
    #     la barra del fader, `grosorFader` (20) de alto: el dial pintado -el
    #     ancho mayor con tinta de las filas de arriba de su celda, que es el
    #     dial; la L y la R van abajo- no es mas grueso que esa banda, y no
    #     baja de 16 (`lg`), que con menos no se le ve la aguja.
    #     Medido en 412x915: dial 20 con banda 20. Roto -el binario del dial
    #     de 28, con esta medida puesta y el dibujo sin tocar-: pinta 32 (el
    #     aro de 1.6 y su suavizado caen por fuera del radio) contra 20:
    #     FALLA.
    knob_d = r.get ("mesa_knob_d", -1)
    print ("knob     el pan del pad 0 pinta un dial de %d px; la banda del fader mide %d" % (knob_d, banda))
    if knob_d < 0:
        malas.append ("la app no publica lo que mide el dial del pan del pad 0")
    elif knob_d > banda:
        malas.append ("el dial del pan del pad 0 mide %d y la banda del fader %d: el knob no es mas "
                      "grueso que el fader de al lado" % (knob_d, banda))
    elif knob_d < 16:
        malas.append ("el dial del pan del pad 0 mide %d: con menos de 16 no se le ve la aguja" % knob_d)

    # 14. Y EL KNOB Y SUS LETRAS SON UN BLOQUE CENTRADO. Del telefono, con la
    #     foto del dial de 20 y las letras en las esquinas de abajo de la
    #     celda: «no esta centrado el texto LR o el knob» y «no hay necesidad
    #     de tanto hueco: donde termina el circulo del knob podria ser la
    #     linea de arriba de la L y la R»; y con las letras pegadas: «asi me
    #     gusta, pero un pelin: el grosor que tiene la linea del circulo, eso
    #     quiero que sea de aire». Medido por la tinta del pad 0 -la app
    #     publica donde pinta el dial, cuanto mide su linea en el ecuador y
    #     las cajas de las dos letras-: el dial centrado en su celda (a un
    #     pixel), las letras debajo a tanto aire del circulo como mide su
    #     linea (ni mas ni menos), la L al ras del borde izquierdo del dial y
    #     la R del derecho (a un pixel), y el bloque entero -dial, aire y
    #     letras- centrado de arriba abajo en la celda (a dos).
    #     Medido en 412x915: dial en x 10..29 e y 5..24 de 40x40 con la
    #     linea de 2, la L en 10..14 y la R en 25..29 desde la fila 27: dos
    #     de aire. Rotos, con la medida puesta y el dibujo sin tocar: el
    #     binario de las letras en las esquinas da seis de aire y la L desde
    #     el 4 con el dial en el 10, y el de las letras pegadas, cero de aire
    #     con la linea de 2: FALLA los dos.
    #
    #     Y LAS LETRAS VAN A LOS LADOS, la L a la izquierda del dial y la R
    #     a la derecha -«a la izquierda la L y a la derecha la R», lo que el
    #     telefono dijo la primera vez-, desde que las dieciseis filas
    #     entran en la ficha: en una fila de 32 no caben debajo del dial. El
    #     aire entre cada letra y el circulo sigue siendo lo que mide su
    #     linea, cada letra con su tinta a la altura del centro del dial, el
    #     dial centrado de arriba abajo, y el bloque -de la L a la R- es la
    #     celda entera: la L empieza en el borde izquierdo y la R acaba en el
    #     derecho. Medido en 412x915: dial en x 7..26 e y 6..25 de 34x32 con
    #     la linea de 2, la L en 0..4 y la R en 29..33, las dos en las filas
    #     12..19. Roto -el binario de la tanda anterior, con las letras
    #     debajo-: la L y la R a los lados no estan, FALLA.
    kw, kh = r.get ("mesa_knob_w", 0), r.get ("mesa_knob_h", 0)
    kx0, ky0, ky1 = r.get ("mesa_knob_x0", -1), r.get ("mesa_knob_y0", -1), r.get ("mesa_knob_y1", -1)
    aro = r.get ("mesa_knob_aro", -1)
    tl, tr = r.get ("mesa_l", [-1] * 4), r.get ("mesa_r", [-1] * 4)
    kx1 = kx0 + knob_d - 1
    print ("knob     dial en x %d..%d, y %d..%d de una celda de %dx%d, linea de %d; L en %s, R en %s"
           % (kx0, kx1, ky0, ky1, kw, kh, aro, tl, tr))
    if kx0 < 0 or kw <= 0:
        malas.append ("la app no publica donde pinta el dial del pan del pad 0")
    elif tl[1] < 0 or tr[1] < 0 or tl[1] >= kx0 or tr[0] <= kx1:
        malas.append ("a los lados del dial del pan del pad 0 falta la L o la R (L %s, R %s, dial %d..%d)"
                      % (tl, tr, kx0, kx1))
    else:
        if abs ((ky0 + ky1) - (kh - 1)) > 1:
            malas.append ("el dial del pan del pad 0 no esta centrado de arriba abajo en su celda: va de %d a "
                          "%d en %d" % (ky0, ky1, kh))
        aireL = kx0 - tl[1] - 1
        aireR = tr[0] - kx1 - 1
        if aro < 1 or aro > 3:
            malas.append ("la linea del circulo del pan del pad 0 mide %d px: no se lee como linea" % aro)
        elif aireL != aro or aireR != aro:
            malas.append ("entre el circulo del pan del pad 0 y sus letras hay %d px de aire a la izquierda y "
                          "%d a la derecha, y la linea del circulo mide %d: ese aire es el grosor de la linea"
                          % (aireL, aireR, aro))
        if abs ((tl[2] + tl[3]) - (ky0 + ky1)) > 2 or abs ((tr[2] + tr[3]) - (ky0 + ky1)) > 2:
            malas.append ("la L y la R del pan del pad 0 no van a la altura del dial: la L en las filas %d..%d y "
                          "la R en %d..%d con el dial en %d..%d" % (tl[2], tl[3], tr[2], tr[3], ky0, ky1))
        if tl[0] > 1 or tr[1] < kw - 2:
            malas.append ("el bloque del pan del pad 0 no es su celda: la L empieza en %d y la R acaba en %d de "
                          "una celda de %d" % (tl[0], tr[1], kw))

    # 15. LAS DIECISEIS FILAS ENTRAN EN LA FICHA. Del telefono: «tienen que
    #     entrar todos los canales en el pop up del mixer, o si no es una
    #     jodienda para girar los knobs de paneo». Era una jodienda medida:
    #     en 393x851 la tarjeta ensenaba once filas de 44 y las otras cinco
    #     se arrastraban, y el Viewport arrastra la lista con el mismo gesto
    #     con el que se gira el knob. Ahora la fila mide lo que la tarjeta
    #     da -hasta 44, nunca menos de 24- y las dieciseis entran sin barra:
    #     las filas miden lo que el Viewport ensena o menos, la fila no baja
    #     de 24 y la barra de la casa no esta. En 412x915 y en 393x851, que
    #     es la del movil. Medido: 412x915 filas de 36, 576 en 583, sin
    #     barra; 393x851 filas de 32, 512 en 519. Roto -el binario de la
    #     tanda anterior-: filas de 44, 704 en 583 con la barra: FALLA.
    r393 = corre ({"ZATI_SIZE": "393x851"})
    if r393 is None:
        malas.append ("la app no publico la linea de canales en 393x851")
    for nombre, rr in (("412x915", r), ("393x851", r393)):
        if rr is None: continue
        fila = rr.get ("mesa_fila", -1)
        vistaAlto = rr.get ("mesa_vista_alto", -1)
        filasAlto = rr.get ("mesa_filas_alto", -1)
        barraApp = rr.get ("mesa_barra_app", -1)
        print ("filas    en %s cada fila mide %d: las dieciseis miden %d y el Viewport ensena %d; barra de la casa %d"
               % (nombre, fila, filasAlto, vistaAlto, barraApp))
        if fila < 24:
            malas.append ("en %s la fila de la mesa mide %d: nunca por debajo de 24" % (nombre, fila))
        if filasAlto < 0 or vistaAlto < 0 or filasAlto > vistaAlto:
            malas.append ("en %s las dieciseis filas miden %d y el Viewport ensena %d: no entran todas"
                          % (nombre, filasAlto, vistaAlto))
        if barraApp != 0:
            malas.append ("en %s la mesa lleva barra de %d px con las dieciseis filas a la vista: no hay nada "
                          "que arrastrar" % (nombre, barraApp))

    # 16. Y LOS HUECOS DE LA FILA SON TODOS EL MISMO. Del telefono, con la
    #     foto del knob en su celda de 44: «la distancia que quiero que
    #     guarden es la que hay entre los botones M y S». La app mide los
    #     huecos por la TINTA de cada control del pad 0 -su foto, de la
    #     primera a la ultima columna pintada- y no por los limites de las
    #     celdas: fader a pan, pan a ST, ST a M y M a S tienen que ser los
    #     cuatro `halfGap`, 4, el que separaba M de S. Y la celda del pan
    #     mide lo que su tinta, que es lo que lo hace posible. Medido en
    #     412x915 y 393x851: [4, 4, 4, 4], celda 34 y tinta 34. Roto -el
    #     binario de la tanda anterior-: no publica los huecos, FALLA.
    for nombre, rr in (("412x915", r), ("393x851", r393)):
        if rr is None: continue
        huecos = rr.get ("mesa_huecos", [-1] * 4)
        pan, tinta = rr.get ("mesa_pan", -1), rr.get ("mesa_pan_tinta", -1)
        print ("huecos   en %s, por la tinta: fader-pan, pan-ST, ST-M y M-S son %s; la celda del pan mide %d y "
               "su tinta %d" % (nombre, huecos, pan, tinta))
        if len (huecos) != 4 or any (h != 4 for h in huecos):
            malas.append ("en %s los huecos de la fila del pad 0 son %s y tienen que ser los cuatro de 4, el "
                          "que hay entre M y S" % (nombre, huecos))
        if pan < 30 or pan != tinta:
            malas.append ("en %s la celda del pan del pad 0 mide %d y su tinta %d: la celda es el bloque, sin "
                          "aire propio" % (nombre, pan, tinta))

    # 17. TRES SWITCHES, Y EL DE ESTEREO MUEVE EL ANCHO. Del telefono: «tres
    #     switch, de hecho; que no sean botones, el mute y el solo; y uno
    #     que sea mono o estereo, por la abertura». La fila del pad 0 lleva
    #     tres tapas pintadas como switch -ranura y paleta-, y el ST es una
    #     ventana de dos posiciones al ancho del pad: tocarlo lo pone a cero
    #     y volver a tocarlo lo devuelve a lo que tenia, leido del MOTOR y
    #     por el gesto (`pulsaTapa`). Activo con la muestra que sea, y la
    #     app lo MIDE con una de UN canal -el izquierdo de la de fabrica,
    #     puesto por `assignSampleToPad`-: del telefono, «con una mono,
    #     todas son mono» y «que abra los canales mono»; la de un canal se
    #     ABRE (regla 20), asi que un ST apagado por mono es la version
    #     anterior, la que lo gateaba por los canales del fichero. Con la de
    #     dos canales la medida no distingue nada -alli siempre estuvo
    #     activo-, por eso la regla exige que sea de uno. Y la escalera: en
    #     360x640 -con la barra, que se cobra un dedo- el ST se cae ANTES
    #     que el pan, que es el que se gira en cada mezcla: dos switches y el
    #     pan puesto. Medido en 412x915 y 393x851: 3 switches, la muestra de
    #     1 canal, activo, 1.00 -> 0.00 -> 1.00; en 360x640, 2 switches y el
    #     pan de 34. Roto -el binario de la tanda anterior-: mide con la de
    #     dos canales, FALLA.
    for nombre, rr in (("412x915", r), ("393x851", r393)):
        if rr is None: continue
        sw = rr.get ("mesa_sw", -1)
        canales = rr.get ("mesa_st_canales", -1)
        activo = rr.get ("mesa_st_activo", -1)
        antes, despues, vuelve = (rr.get ("mesa_st_antes", -1.0), rr.get ("mesa_st_despues", -1.0),
                                  rr.get ("mesa_st_vuelve", -1.0))
        print ("switch   en %s la fila del pad 0 lleva %d switches; la muestra tiene %d canales y el ST esta %s: "
               "el ancho va %.2f -> %.2f -> %.2f" % (nombre, sw, canales, "activo" if activo == 1 else "apagado",
                                                      antes, despues, vuelve))
        if sw != 3:
            malas.append ("en %s la fila del pad 0 lleva %d switches y son tres: S, M y ST" % (nombre, sw))
        if canales != 1:
            malas.append ("en %s el ST del pad 0 se midio con una muestra de %d canales y tiene que ser de UNO: "
                          "con dos la medida no distingue nada" % (nombre, canales))
        if activo != 1:
            malas.append ("en %s el ST del pad 0 esta %s con una muestra de %d canales: activo con la que sea, "
                          "que la de un canal se abre" % (nombre, "activo" if activo == 1 else "apagado", canales))
        if abs (antes - 1.0) > 0.001 or abs (despues) > 0.001 or abs (vuelve - 1.0) > 0.001:
            malas.append ("en %s el ST del pad 0 deja el ancho en %.2f -> %.2f -> %.2f y tiene que ir de 1.00 a "
                          "0.00 y volver a 1.00" % (nombre, antes, despues, vuelve))
    if r360 is not None:
        print ("switch   en 360x640 la fila del pad 0 lleva %d switches y el pan mide %d"
               % (r360.get ("mesa_sw", -1), r360.get ("mesa_pan", -1)))
        if r360.get ("mesa_sw", -1) != 2 or r360.get ("mesa_pan", 0) < 30:
            malas.append ("en 360x640 la fila del pad 0 lleva %d switches y el pan mide %d: el ST se cae antes "
                          "que el pan, y el pan se queda" % (r360.get ("mesa_sw", -1), r360.get ("mesa_pan", 0)))

    # 18. LA PAGINA DE CANALES, con la misma gramatica que la de PADS. Del
    #     telefono, con la foto de MEZCLA · CANALES en 412x915: «¿por que la
    #     pantalla de canales sigue siendo asi?». Asi era: S y M tapas, la M
    #     en el borde y la S a un pixel -al reves que en la fila de un pad-,
    #     y una franja vacia de una fila entre la ultima y el MASTER, lo que
    #     el cajon tiraba por encajar filas plenas (hasta 43 px). Ahora: S y
    #     M switches, la S en el borde y la M a `halfGap` (4). Y DOS BANCOS,
    #     del telefono con las dos fotos: «como en el de pads, que aparezca
    #     una A y una B arriba separando los 32 canales en 16 y 16, asi si
    #     que entraria en una pantalla como esta en los pads». Asi que la
    #     pagina lleva dos chips -A y B- y dieciseis filas, con la MISMA
    #     regla de la de PADS: donde las dieciseis entran -412x915, 393x851-
    #     la fila es alto/16 (suelo 24) y no hay barra; tocar B pone delante
    #     la fila del canal 17 y quita la del 1, con las mismas dieciseis
    #     filas. Donde no entran -360x640- la fila no baja de 44, la barra de
    #     la casa esta y el sobrante se reparte, asi que lo que queda al aire
    #     es menos que filas a la vista; y la pagina de PADS hace lo mismo.
    #     La app lo mide por el gesto -el interruptor de vista, los chips- y
    #     vuelve. Medido: 412x915 dos chips, filas de 36, 576 en 583 sin
    #     barra, el B ensena el 17 con 576; 393x851 filas de 32, 512 en 519.
    #     Roto -el binario de la tanda anterior-: ningun chip en CANALES y
    #     treinta y dos filas de 45 (1440 px en 630) con barra de 40, FALLA.
    for nombre, rr in (("412x915", r), ("393x851", r393)):
        if rr is None: continue
        fila, ven, sobra = rr.get ("mesa_c_fila", -1), rr.get ("mesa_c_ven", -1), rr.get ("mesa_c_sobra", -1)
        sw, orden, hueco = rr.get ("mesa_c_sw", -1), rr.get ("mesa_c_orden", -1), rr.get ("mesa_c_hueco_sm", -1)
        barraApp = rr.get ("mesa_c_barra_app", -1)
        vistaAlto, filasAlto = rr.get ("mesa_c_vista_alto", -1), rr.get ("mesa_c_filas_alto", -1)
        bancos, bancoB, filasB = rr.get ("mesa_c_bancos", 0), rr.get ("mesa_c_banco_b", -1), rr.get ("mesa_c_filas_b", -1)
        print ("canales  en %s la pagina de CANALES: %d chips, filas de %d, las dieciseis miden %d y el Viewport "
               "ensena %d, barra de la casa %d; el B pone delante el canal 17: %d, con %d; %d switches, la S %s "
               "y a %d de la M"
               % (nombre, bancos, fila, filasAlto, vistaAlto, barraApp, bancoB, filasB, sw,
                  "en el borde" if orden == 1 else "DENTRO", hueco))
        if bancos != 2:
            malas.append ("en %s la pagina de CANALES lleva %d chips de banco y son dos: A y B, dieciseis y dieciseis"
                          % (nombre, bancos))
        if fila < 24:
            malas.append ("en %s la fila de CANALES mide %d: nunca por debajo de 24" % (nombre, fila))
        if filasAlto < 0 or vistaAlto < 0 or filasAlto > vistaAlto:
            malas.append ("en %s las dieciseis filas de CANALES miden %d y el Viewport ensena %d: no entran todas"
                          % (nombre, filasAlto, vistaAlto))
        if barraApp != 0:
            malas.append ("en %s la pagina de CANALES lleva barra de %d px con las dieciseis filas a la vista: no "
                          "hay nada que arrastrar" % (nombre, barraApp))
        if bancoB != 1 or filasB != filasAlto:
            malas.append ("en %s tocar el B de CANALES %s la fila del canal 17 delante (filas de %d contra %d en "
                          "el A): el banco no cambia de filas" % (nombre, "pone" if bancoB == 1 else "NO pone",
                                                                 filasB, filasAlto))
        if sw != 2:
            malas.append ("en %s la fila del canal 1 lleva %d switches y son dos: S y M" % (nombre, sw))
        if orden != 1 or hueco != 4:
            malas.append ("en %s la S del canal 1 %s y queda a %d de la M: en el borde y a 4, como en la fila de "
                          "un pad" % (nombre, "esta en el borde" if orden == 1 else "esta DENTRO", hueco))
    if r360 is not None:
        for pagina, kf, ks, kv, kb in (("PADS", "mesa_fila", "mesa_sobra", "mesa_vista_alto", "mesa_barra_app"),
                                       ("CANALES", "mesa_c_fila", "mesa_c_sobra", "mesa_c_vista_alto", "mesa_c_barra_app")):
            fila, sobra, barraApp = r360.get (kf, -1), r360.get (ks, -1), r360.get (kb, -1)
            ven = r360.get (kv, 0) // max (1, fila)
            print ("canales  en 360x640 la pagina de %s, con la barra de %d: filas de %d, %d a la vista y %d px al aire"
                   % (pagina, barraApp, fila, ven, sobra))
            if fila < 44 or ven < 1 or sobra < 0 or sobra >= ven:
                malas.append ("en 360x640 la pagina de %s deja %d px al aire con %d filas de %d a la vista"
                              % (pagina, sobra, ven, fila))
            if barraApp < 36:
                malas.append ("en 360x640 la pagina de %s no lleva la barra de la casa (%d px) con filas que no "
                              "entran" % (pagina, barraApp))

    # 19. EL GESTO SOBRE UN MANDO ES DEL MANDO, NO DE LA LISTA. Del telefono:
    #     «es una jodienda para girar los knobs de paneo». Estaba escrito -los
    #     mandos llevan `setViewportIgnoreDragFlag`- y sin medir. La app mete
    #     un DEDO por la ventana (no un raton: el Viewport solo arrastra con lo
    #     que no flota) que sube sesenta pixeles sobre el pan del pad 0 y luego
    #     sobre el nombre de la fila, y publica cuanto se movio la lista en
    #     cada caso y cuanto el pan. Lo que vale: sobre el knob la lista se
    #     queda a 0 y el pan se mueve; sobre el nombre la lista se mueve -eso
    #     prueba que el dedo llega-. Donde hay barra: 360x640, en PADS y en
    #     CANALES -con el fader del canal 0-; desde los dos bancos CANALES ya
    #     no la lleva en 412x915 ni 393x851, y sin barra el nombre deja 0
    #     tambien. `*_dio` dice que bajo el dedo estaba el mando y no una
    #     tapa. Roto -el flag quitado-: la lista se mueve con el knob, FALLA.
    if r360 is not None:
        knob, nombre = r360.get ("mesa_arr_knob", -1), r360.get ("mesa_arr_nombre", -1)
        dio, pan = r360.get ("mesa_arr_dio", 0), float (r360.get ("mesa_arr_pan", 0))
        print ("dedo     en 360x640, sesenta px de dedo sobre el pan del pad 0: la lista se mueve %d px y el pan "
               "%.3f (bajo el dedo el knob: %d); sobre el nombre la lista se mueve %d px" % (knob, pan, dio, nombre))
        if dio != 1:
            malas.append ("en 360x640 bajo el dedo no estaba el knob del pan: la medida no vale")
        if knob != 0 or abs (pan) < 0.05:
            malas.append ("en 360x640 el dedo sobre el knob del pan mueve la lista %d px y el pan %.3f: el giro es "
                          "del knob y la lista se queda" % (knob, pan))
        if nombre < 40:
            malas.append ("en 360x640 el dedo sobre el nombre mueve la lista %d px: por el nombre SI se arrastra, "
                          "o el dedo no llega" % nombre)
    for nombre_p, rr in (("360x640", r360),):
        if rr is None: continue
        fader, nombre, dio = rr.get ("mesa_c_arr_fader", -1), rr.get ("mesa_c_arr_nombre", -1), rr.get ("mesa_c_arr_dio", 0)
        print ("dedo     en %s, CANALES: sesenta px de dedo sobre el fader del canal 0 mueven la lista %d px "
               "(bajo el dedo el fader: %d); sobre el nombre, %d px" % (nombre_p, fader, dio, nombre))
        if dio != 1:
            malas.append ("en %s bajo el dedo no estaba el fader del canal 0: la medida no vale" % nombre_p)
        if fader != 0:
            malas.append ("en %s el dedo sobre el fader del canal 0 mueve la lista %d px: el arrastre es del fader"
                          % (nombre_p, fader))
        if nombre < 40:
            malas.append ("en %s el dedo sobre el nombre del canal mueve la lista %d px: por el nombre SI se "
                          "arrastra, o el dedo no llega" % (nombre_p, nombre))

    # 20. LA MUESTRA MONO SE ABRE. Del telefono: «con una mono, todas son
    #     mono» y «dale, que abra los canales mono». Hasta aqui el ST y el
    #     ANCHO no podian hacer nada con una muestra de un canal: escalaban
    #     un lado que era cero. Ahora el motor FABRICA el lado -la misma
    #     senal retrasada 10 ms, por `kLadoMono` (0.5) y por `ancho`- y la
    #     app lo mide en el motor y no en el mando: un ruido de UN canal en
    #     el pad 0, directo al master, renderizado con ancho 0 y con 1. Lo
    #     que vale: con ancho 0 el lado es CERO (cerrada es mono); con 1 el
    #     lado es k veces el centro (±5 %) y el centro no se mueve (±0.5 %);
    #     la suma L+R es la MISMA muestra a muestra en las dos tiradas
    #     -mono-compatible: en un altavoz no cambia nada-; y la correlacion
    #     L/R con ancho 1 es (1-k^2)/(1+k^2) = 0.60 (±0.05), abierto sin ser
    #     dos fuentes. Medido en 412x915: lado 0.000000 -> 0.050705 (k por
    #     el centro: 0.051000), centro 0.102000 las dos, suma 0.00000001,
    #     corr 0.6039. Roto -el binario de la tanda anterior-: no publica la
    #     apertura, FALLA. (La 17 cae tambien desde que la auditoria mide
    #     el ST con una muestra de UN canal: ese binario la mide con la de
    #     dos y dice canales=2. Esta es la que mide lo que el ST HACE con
    #     la mono; la 17, que esta encendido.)
    canalesM = r.get ("abre_canales", 0)
    lado0, lado1 = float (r.get ("abre_lado0", -1)), float (r.get ("abre_lado1", -1))
    centro0, centro1 = float (r.get ("abre_centro0", -1)), float (r.get ("abre_centro1", -1))
    corr1, suma, k = float (r.get ("abre_corr1", -2)), float (r.get ("abre_suma", -1)), float (r.get ("abre_k", 0))
    corrTeor = (1.0 - k * k) / (1.0 + k * k) if k > 0 else 2.0
    print ("abre     la muestra de %d canal en el pad 0: con ancho 0 lado %.6f y centro %.6f; con ancho 1 lado %.6f "
           "(k %.3f -> %.6f) y centro %.6f; la suma L+R cambia %.8f; correlacion L/R %.4f (teorica %.2f)"
           % (canalesM, lado0, centro0, lado1, k, k * centro1, centro1, suma, corr1, corrTeor))
    if canalesM != 1:
        malas.append ("la app no midio la apertura con una muestra de UN canal (%d): la medida no vale" % canalesM)
    if lado0 < 0 or lado0 > 1.0e-6:
        malas.append ("con ancho 0 la muestra mono deja lado %.6f: cerrada tiene que ser mono, lado cero" % lado0)
    if centro0 <= 0.01:
        malas.append ("con ancho 0 el centro es %.6f: el pad no sono, la medida no vale" % centro0)
    if k <= 0 or lado1 <= 0 or abs (lado1 - k * centro1) > 0.05 * k * centro1:
        malas.append ("con ancho 1 la muestra mono deja lado %.6f y tiene que ser k=%.3f veces el centro (%.6f): "
                      "no se abre" % (lado1, k, k * centro1))
    if centro0 <= 0 or abs (centro1 - centro0) > 0.005 * centro0:
        malas.append ("abrir la muestra mono mueve el centro de %.6f a %.6f: el centro no se toca" % (centro0, centro1))
    if suma < 0 or suma > 1.0e-5:
        malas.append ("abrir la muestra mono cambia la suma L+R en %.8f rms: tiene que ser mono-compatible, la "
                      "misma suma" % suma)
    if abs (corr1 - corrTeor) > 0.05:
        malas.append ("con ancho 1 la correlacion L/R de la muestra mono es %.4f y tiene que ser %.2f (±0.05): "
                      "ni dos fuentes ni mono" % (corr1, corrTeor))

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("los %d canales: la rejilla mueve el pad, cambiar de pad cambia la "
           "fila, un inserto y un envio son de CADA canal con su propio ajuste, "
           "apagar la reverb del 10 deja sonando la del 4, vaciar apaga el efecto "
           "del canal, la tira llega al motor y los %d bancos alcanzan los %d y se "
           "abren donde esta el pad"
           % (r["canales"], r["bancos"], r["canales"]))
    return 0


if __name__ == "__main__":
    sys.exit (main())

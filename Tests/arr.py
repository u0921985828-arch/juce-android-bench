#!/usr/bin/env python3
# ============================================================================
#  LAS HERRAMIENTAS DE ARREGLO, JUZGADAS.
#
#  Con la paleta y la rejilla sola, una pagina de cancion sirve para escribir
#  y para borrar, y eso no es arreglar: arreglar es meter un compas donde
#  falta, quitar el que sobra y repetir el trozo que funciona. Esas
#  operaciones mueven decenas de celdas de golpe, y una que se equivoca de
#  fila o de direccion sigue "funcionando" - algo se mueve - hasta que un dia
#  descubres que tu estribillo esta un compas corrido.
#
#  Asi que la app monta una cancion que se lee de un vistazo -el carril n
#  lleva el valor n+1 en el compas 2n y nada mas-, ejecuta UNA operacion y
#  saca lo que quedo. Las respuestas estan aqui y no dentro del codigo que se
#  prueba: una prueba que se juzga a si misma cambia de opinion a la vez que
#  el fallo.
#
#  Y la prueba mas fuerte de las seis no comprueba un resultado sino una
#  IDENTIDAD: desplazar el patron un paso adelante y otro atras tiene que
#  devolver exactamente el patron de partida, con sus notas, sus fuerzas y sus
#  repeticiones. Una rotacion que pierde la nota pasa cualquier prueba que
#  solo mire que casillas suenan.
#
#      python3 Tests/arr.py
# ============================================================================
import json, os, subprocess, sys

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def display_alive():
    d = PANTALLA
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre(size="412x915"):
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": size, "ZATI_LANG": "es",
                 "ZATI_OPEN": "song", "ZATI_ARR": "1"})
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    song, pat, vuelta, golpe, celda = {}, {}, None, None, None
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "arr" in d: song[d["arr"]] = d
        if "pat" in d: pat[d["pat"]] = d
        if "vuelta" in d: vuelta = d
        if "golpe" in d: golpe = d
        if "celda" in d: celda = d
    return song, pat, vuelta, golpe, celda


#  La cancion de partida: carril n, compas 2n, valor n+1.
INICIAL = [[1,0,0,0,0,0,0,0],
           [0,0,2,0,0,0,0,0],
           [0,0,0,0,3,0,0,0],
           [0,0,0,0,0,0,4,0]]

#  Metido un compas en el 2 (indice 2): lo del compas 0 se queda donde estaba
#  -esta ANTES del cursor- y todo lo demas se corre uno a la derecha.
INSERTADO = [[1,0,0,0,0,0,0,0,0],
             [0,0,0,2,0,0,0,0,0],
             [0,0,0,0,0,3,0,0,0],
             [0,0,0,0,0,0,0,4,0]]

#  Pegado el compas 2 -que lleva un 2 en el carril 1- en el compas 5.
PEGADO = [[1,0,0,0,0,0,0,0],
          [0,0,2,0,0,2,0,0],
          [0,0,0,0,3,0,0,0],
          [0,0,0,0,0,0,4,0]]

#  Recortar y alargar un bloque. 1000 es la marca de "continuacion".
BLOQUE = {
    "bloque inicial":     [[0,1,1000,0,2,0,0,0]] + [[0]*8]*3,
    "acortado":           [[0,1,   0,0,2,0,0,0]] + [[0]*8]*3,
    "alargado":           [[0,1,1000,0,2,0,0,0]] + [[0]*8]*3,
    "alargado otra vez":  [[0,1,1000,1000,2,0,0,0]] + [[0]*8]*3,
    #  Contra el vecino no pasa nada: comerse el bloque de al lado seria
    #  borrar algo que nadie ha pedido borrar.
    "y contra el vecino": [[0,1,1000,1000,2,0,0,0]] + [[0]*8]*3,
}

#  UN PASO SON NUEVE CAMPOS, y hasta aqui esto medi­a tres.
#
#  [paso, pad, nota, fuerza, repeticion, largo, empujon, corte, acorde, bloqueos]
#
#  La queja llego del telefono con las dos mitades: «cuando copio un patron y
#  lo pego, la velocidad no se copia» y «de un acorde de tres notas solo se
#  pega la tonica». Las dos son la misma causa -COPIAR PATRON llevaba el
#  «suena», la nota raiz y el largo del patron, y nada mas- y ninguna se podia
#  ver desde aqui, porque el volcado sacaba nota, fuerza y repeticion.
#
#  Los nueve van DISTINTOS de su defecto a proposito: con un campo en su valor
#  de fabrica, «se copio» y «el destino ya valia eso» dan el mismo numero.
#  Defectos: vel 127, roll 1, largo 0, empujon 0, corte -1, acorde [] y los
#  cuatro bloqueos en -1.
PASO_A       = [5,90,3,9,-25,33,[4,7],[11,22,44,88]]
PASO_B       = [0,127,4,0,0,-1,[],[-1,-1,-1,-1]]
PAT_INICIAL  = [[0,0] + PASO_A, [3,1] + PASO_B]
PAT_ADELANTE = [[1,0] + PASO_A, [4,1] + PASO_B]
PAT_DOBLADO  = [[0,0] + PASO_A, [3,1] + PASO_B, [16,0] + PASO_A, [19,1] + PASO_B]

#  El paso que se ensucia a mano en el patron DESTINO antes de pegar, en una
#  casilla donde el origen esta APAGADO, y lo que tiene que quedar despues.
BASURA       = [5,0,0,40,1,7,0,-1,[2,9],[-1,-1,-1,-1]]
LIMPIO       = [5,0] + [0,127,1,0,0,-1,[],[-1,-1,-1,-1]]


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada:", APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo");  return 1

    song, pat, vuelta, golpe, celda = corre()
    malas = []

    def mira (nombre, tengo, quiero, largo=None, quieroLargo=None):
        ok = (tengo == quiero) and (largo is None or largo == quieroLargo)
        print ("%-22s %s" % (nombre, "correcto" if ok else "MAL"))
        if not ok:
            print ("   sale  ", tengo, "largo", largo)
            print ("   deberia", quiero, "largo", quieroLargo)
            malas.append (nombre)

    if not song or not pat:
        print ("la app no contesto");  return 1

    mira ("cancion de partida", song["inicial"]["carriles"], INICIAL,
          song["inicial"]["largo"], 8)
    mira ("insertar un compas", song["insertar en 2"]["carriles"], INSERTADO,
          song["insertar en 2"]["largo"], 9)
    #  Quitar el compas que se acaba de meter tiene que devolver la cancion
    #  EXACTA de partida. Es la identidad, y es mas fuerte que cualquier
    #  resultado escrito a mano: una de las dos podria estar mal a la vez que
    #  la respuesta, las dos juntas no.
    mira ("quitar lo insertado", song["quitar el 2"]["carriles"], INICIAL,
          song["quitar el 2"]["largo"], 8)
    #  Y LO QUE MUEVE LA LINEA DE TIEMPO MUEVE LAS DOS COSAS.
    #
    #  Con las dos vistas fundidas, un carril lleva bloques de patron Y clips de
    #  audio, asi que meter un compas tiene que correr los dos: si no, la toma
    #  de voz suena un compas antes de la parte que acompaña — desincronizada,
    #  sin que nada falle y sin verse hasta que suena. El clip esta en el compas
    #  4 y el cursor en el 2, o sea DETRAS, que es donde la operacion actua.
    #
    #  Con DOS cifras y no una: solo «se corre al insertar» lo cumple igual un
    #  codigo que corre siempre, y solo «vuelve al quitar» uno que no mueve
    #  nada. Es la misma identidad que las dos filas de arriba.
    mira ("insertar corre el clip", song["clip tras insertar"]["clip"], 5)
    mira ("y quitar lo devuelve",   song["clip tras quitar"]["clip"],   4)

    mira ("copiar y pegar", song["pegar el 2 en el 5"]["carriles"], PEGADO,
          song["pegar el 2 en el 5"]["largo"], 8)

    for k, v in BLOQUE.items():
        mira (k, song[k]["carriles"], v, song[k]["largo"], 8)

    mira ("patron de partida", pat["inicial"]["pasos"], PAT_INICIAL,
          pat["inicial"]["largo"], 16)
    mira ("desplazar adelante", pat["adelante"]["pasos"], PAT_ADELANTE,
          pat["adelante"]["largo"], 16)
    #  La otra identidad: adelante y atras devuelven el patron entero, notas y
    #  golpes incluidos. Una rotacion que solo mueve el "suena" pasaria la
    #  fila de arriba y fallaria esta.
    mira ("y volver atras", pat["atras"]["pasos"], PAT_INICIAL,
          pat["atras"]["largo"], 16)
    mira ("doblar el patron", pat["doblado"]["pasos"], PAT_DOBLADO,
          pat["doblado"]["largo"], 32)

    #  COPIAR Y PEGAR EL PATRON, con DOS cifras y no una.
    #
    #  La primera dice lo que VIAJA: el patron pegado tiene que ser el de
    #  partida, los nueve campos. Sin ella, «pega» lo cumple un codigo que solo
    #  mueve el encendido - que es lo que habia, y por eso la velocidad no se
    #  copiaba y del acorde solo llegaba la tonica.
    #
    #  La segunda dice lo que se LIMPIA, y es la mitad invisible: quien pegaba
    #  escribia ENCIMA sin vaciar lo que no copiaba, asi que el destino se
    #  quedaba con su fuerza y su acorde viejos. El paso 5 del pad 0 se ensucia
    #  a mano y el origen lo tiene APAGADO: despues de pegar tiene que valer su
    #  defecto y no lo que habia. Sin esta, «los nueve viajan» lo cumple el
    #  codigo de antes con una capa nueva encima de la basura vieja.
    mira ("el destino, ensuciado", song["basura en el destino"]["paso"], BASURA)
    mira ("pegar el patron",       pat["pegado"]["pasos"], PAT_INICIAL,
          pat["pegado"]["largo"], 16)
    mira ("y limpia lo que habia", song["tras pegar"]["paso"], LIMPIO)

    #  Y COPIAR / PEGAR LA FILA DE UN PAD: el unico camino que ya se llevaba
    #  los nueve y que no ejecutaba NINGUNA prueba.
    mira ("copiar y pegar la fila", song["fila pegada"]["paso"], [0,2] + PASO_A)

    #  Lo que no se guarda se pierde sin avisar. El silenciado de carriles y
    #  el tramo en bucle van por el mismo arbol que escribe el proyecto.
    if vuelta is None:
        print ("%-22s %s" % ("guardar y volver", "MAL - sin respuesta")); malas.append ("vuelta")
    else:
        #  Y el silencio por BLOQUE con ellos. Dos bloques y en carriles
        #  distintos: una mascara que vuelve con un solo bit puesto la cumple
        #  igual un lector que se quedo con el primer numero de la lista.
        mira ("guardar y volver", [vuelta["mudos"], vuelta["bucle"],
                                   vuelta["bloques mudos"]],
              [[1,0,0,1], [2,5], [[0,1],[2,6]]])

    #  --- UN GOLPE SUELTO DE UN PAD QUE NO ESTA EN LA REJILLA -------------
    #
    #  El pincel de un solo golpe guarda -(pad+1) con el pad de los SESENTA Y
    #  CUATRO, y a Playlist se le pasaba la tabla de colores del banco que se
    #  VE: dieciseis huecos. Cualquier golpe de los bancos B, C o D leia fuera
    #  del array en cada repintado de la ficha CANCION.
    #
    #  Ninguna de las ocho reglas del banco puede verlo — es un fallo de INDICE
    #  y no de geometria: el bloque se maqueta perfecto, no solapa, no se sale
    #  y no lleva texto — y ademas no se CAE, porque lo leido acaba en
    #  Zati::colour, que envuelve con un modulo. El sintoma era un bloque del
    #  color de otro pad, y una lectura fuera de rango que un ASan o MTE
    #  convierten en un cierre.
    #
    #  Se mide PINTANDO y por el contador que lleva quien indexa, no
    #  preguntandole a la tabla su tamano: eso seria repetir la constante.
    print()
    if golpe is None:
        print ("%-22s %s" % ("golpe suelto", "MAL - sin respuesta")); malas.append ("golpe")
    else:
        print ("%-22s celdas %s, tabla de %d zatis, %d fuera de rango"
               % ("golpe suelto", golpe["celdas"], golpe["zatis"], golpe["fuera"]))
        if golpe["fuera"] != 0:
            malas.append ("pintar la cancion pidio %d veces un pad fuera de la tabla de colores"
                          % golpe["fuera"])
        #  Y la corrida de control: si la tabla no llegara a los 64, las celdas
        #  de arriba no serian el caso que esto existe para cazar.
        if golpe["zatis"] < 64:
            malas.append ("la tabla de colores tiene %d huecos y un golpe suelto usa hasta 64"
                          % golpe["zatis"])
        if golpe["celdas"] != [-34, -64]:
            malas.append ("las celdas del golpe suelto salieron %s" % (golpe["celdas"],))

    #  Y LO QUE EL FICHERO DE PROYECTO PUEDE METER EN UNA CELDA. setSongCell
    #  comprobaba los indices y guardaba el valor tal cual, y ese valor sale de
    #  toks[b].getIntValue(): un project.xml corrupto metia cualquier entero y
    #  cada consumidor tenia que volver a validarlo. Con las dos mitades — que
    #  lo malo se rechace Y que lo bueno pase — porque una puerta que dice que
    #  no a todo pasa la primera sola.
    if celda is None:
        print ("%-22s %s" % ("celda acotada", "MAL - sin respuesta")); malas.append ("celda")
    else:
        print ("%-22s -9999 y 999999 -> %s, y el pad 63 -> %d"
               % ("celda acotada", celda["puestas"][:2], celda["puestas"][2]))
        if celda["puestas"][:2] != [0, 0]:
            malas.append ("una celda acepta valores que ningun consumidor sabe leer: %s"
                          % (celda["puestas"][:2],))
        if celda["puestas"][2] != -64:
            malas.append ("la puerta rechaza un golpe suelto valido (el pad 63): %d"
                          % celda["puestas"][2])

    #  LA MINIATURA: el bloque dibuja sus pasos, y los dibuja DONDE TOCA.
    #
    #  Dos cifras, y la segunda es la que hace falta. «Dibuja algo» lo cumple
    #  igual un bloque que enseña SIEMPRE el compas cero del patron: en un
    #  bloque de cuatro compases con un patron de dos, eso son cuatro copias
    #  del primer compas y suena otra cosa. `giro` compara el mismo bloque con
    #  el patron declarado de dos compases y de uno: con la vuelta bien, las
    #  celdas 1 y 3 cambian de contenido y las dos imagenes difieren; con el
    #  fallo puesto son identicas y sale CERO — mientras `pasos` sigue
    #  diciendo que si.
    mini = song.get ("miniatura")
    if not mini:
        print ("%-22s %s" % ("miniatura", "MAL - sin respuesta")); malas.append ("miniatura")
    else:
        print ("%-22s pasos %d px   giro %d px"
               % ("miniatura", mini["pasos"], mini["giro"]))
        if mini["pasos"] <= 0:
            malas.append ("el bloque no dibuja sus pasos: sigue siendo un color")
        if mini["giro"] <= 0:
            malas.append ("el bloque enseña siempre el compas cero del patron: no da la vuelta")

    #  EL ZOOM DE LA LINEA DE TIEMPO: cuantos compases se ven de una vez.
    #
    #  DOS cifras, y la segunda es la que hace falta. «El ciclo pasa por 8, 16
    #  y 4» lo cumple igual un zoom que salta al compas cero en cada toque - y
    #  entonces mirar la cancion mas ancha te deja mirando OTRA PARTE de la
    #  cancion. El compas que tenias delante tiene que seguir delante.
    #
    #  Y LA ESCALERA se mide en la pantalla donde el paso NO CABE, que es la
    #  unica en la que existe: en un movil grande los tres pasos entran y un
    #  ciclo sin suelo saldria igual de verde. En 280x653 la celda a dieciseis
    #  compases mide 12 px contra un suelo de 20, asi que ese paso se salta y
    #  el ciclo es 8 -> 4 -> 8.
    z = song.get ("zoom")
    if not z:
        print ("%-22s %s" % ("zoom", "MAL - sin respuesta")); malas.append ("zoom")
    else:
        print ("%-22s vistas %s   anchos %s   primer compas %s   suelo %d"
               % ("zoom", z["vistas"], z["anchos"], z["primeros"], z["suelo"]))
        if len (set (z["vistas"][:3])) < 2:
            malas.append ("la tapa de zoom no cicla: %s" % (z["vistas"],))
        flacas = [a for a in z["anchos"] if a < z["suelo"]]
        if flacas:
            malas.append ("el zoom ofrece una celda de %s px con el suelo en %d"
                          % (flacas, z["suelo"]))
        if len (set (z["primeros"])) != 1:
            malas.append ("el zoom se lleva la vista a otro compas: %s" % (z["primeros"],))

        #  La escalera, en la pantalla estrecha.
        songE, _, _, _, _ = corre ("280x653")
        ze = songE.get ("zoom")
        if not ze:
            malas.append ("el zoom no contesto en 280x653")
        else:
            print ("%-22s vistas %s   anchos %s" % ("  y en 280x653", ze["vistas"], ze["anchos"]))
            if 16 in ze["vistas"]:
                malas.append ("en 280x653 el zoom ofrece 16 compases y la celda no cabe: %s"
                              % (ze["anchos"],))
            flacas = [a for a in ze["anchos"] if a < ze["suelo"]]
            if flacas:
                malas.append ("en 280x653 el zoom ofrece una celda de %s px" % (flacas,))

    #  EL FILO DE UN BLOQUE LO ESTIRA.
    #
    #  TRES cifras, y ninguna sobra. «Se estira» lo cumple igual un asa que
    #  ademas MUEVE el bloque -y desde el dedo eso es un bloque que se escapa
    #  mientras lo recortas- asi que se mira la cabeza Y el largo. «Una entrada
    #  de deshacer» solo significa algo con el arrastre partido en cuatro
    #  eventos, que es como lo emite un dedo: de un salto la cifra sale igual
    #  con el fallo y sin el. Y la tercera es el candado que hace que esto no
    #  cueste el pincel: apoyar sobre el filo no escribe nada -puede ser el
    #  principio de un estiron- pero un TOQUE sigue pintando, o el filo de un
    #  bloque largo seria una celda en la que la rejilla no responde.
    asa = song.get ("asa")
    if not asa:
        print ("%-22s %s" % ("asa del bloque", "MAL - sin respuesta")); malas.append ("asa")
    else:
        print ("%-22s %s -> %s   %d entrada(s) de deshacer   tras el toque %s"
               % ("asa del bloque", asa["antes"], asa["estirado"],
                  asa["entradas"], asa["tras el toque"]))
        if asa["antes"] != [2, 3]:
            malas.append ("el bloque de partida no es el que se puso: %s" % (asa["antes"],))
        if asa["estirado"][0] != asa["antes"][0]:
            malas.append ("el asa MUEVE el bloque en vez de estirarlo: %s -> %s"
                          % (asa["antes"], asa["estirado"]))
        if asa["estirado"][1] != 6:
            malas.append ("el asa no estira: el bloque mide %d compases y tenia que medir 6"
                          % asa["estirado"][1])
        if asa["entradas"] != 1:
            malas.append ("un solo estiron deja %d entradas de deshacer" % asa["entradas"])
        if asa["tras el toque"] != [-1, 0]:
            malas.append ("con la goma armada el bloque no se borra: queda %s"
                          % (asa["tras el toque"],))

    #  LAS CUATRO HERRAMIENTAS: MOVER, LAPIZ, GOMA y SILENCIAR.
    #
    #  Medidas POR LA TAPA -que es lo que arma el modo- y POR EL GESTO. Con DOS
    #  cifras donde una se engana: «mover mueve» lo cumple igual un codigo que
    #  ademas pinta por el camino, asi que se mira que la cabeza llegue Y que el
    #  largo siga siendo el mismo; y «silenciar silencia» lo cumple una tapa que
    #  escribe el bit y no lo lee nadie, asi que se mira que el MISMO toque con
    #  el lapiz armado PINTE - o sea que la herramienta es la que decide.
    her = song.get ("herramientas")
    if not her:
        print ("%-22s %s" % ("herramientas", "MAL - sin respuesta")); malas.append ("herramientas")
    else:
        print ("%-22s movido %s   %d entrada(s)   mudo %s   con lapiz pinta %d"
               % ("herramientas", her["movido"], her["entradas"],
                  her["mudo"], her["con lapiz pinta"]))
        #  El bloque de tres compases empieza en el 2, se agarra por el 3 -o sea
        #  por su SEGUNDO compas- y se suelta en el 6: tiene que quedar en el 5,
        #  el dedo MENOS el agarre. Sin el agarre saldria en el 6.
        if her["movido"] != [5, 3]:
            malas.append ("MOVER no lleva el bloque donde el dedo lo suelta: %s, y tenia que ser [5, 3]"
                          % (her["movido"],))
        if her["entradas"] != 1:
            malas.append ("un solo arrastre de MOVER deja %d entradas de deshacer" % her["entradas"])
        if her["mudo"] != [1, 0]:
            malas.append ("SILENCIAR no alterna el bloque: %s" % (her["mudo"],))
        if her["con lapiz pinta"] == 0:
            malas.append ("con el LAPIZ armado el mismo toque ya no pinta: la herramienta no decide")

    #  ------------------------------------------------------------------
    #  LA RED DEBAJO DE LO QUE SE ESCRIBE CON EL DEDO
    #  ------------------------------------------------------------------
    #
    #  `Tests/deshacer.py` mide la otra mitad -todo lo que ocupa un PAD toma
    #  foto- y no puede ver esta: un paso encendido no ocupa ningun pad, asi que
    #  aquella regla daba verde con las dos acciones mas frecuentes de la app
    #  sin red. Las tapas del secuenciador -PEGAR, DOBLAR, EUCLIDES- si la
    #  tenian desde el principio, que es lo que hacia el agujero invisible:
    #  deshacer respondia, solo que saltandose todo lo tocado con el dedo.
    #
    #  Y las cifras van en pares por el motivo de siempre. «Se deshace» lo
    #  cumple igual una rejilla que no escribe nada, asi que se mira lo escrito
    #  Y lo que queda al deshacer; y una foto por celda tambien deshace, solo
    #  que dieciseis veces, asi que se mira ademas cuantas entradas dejo UN
    #  gesto.
    pas = song.get ("deshacer pasos")
    if not pas:
        print ("%-22s %s" % ("deshacer pasos", "MAL - sin respuesta")); malas.append ("deshacer pasos")
    else:
        print ("%-22s el arrastre pinta %d, deja %d foto(s), deshacer %d, rehacer %d"
               % ("pasos", pas["pintados"], pas["entradas"],
                  pas["tras deshacer"], pas["tras rehacer"]))
        #  Cuatro eventos -un mouseDown y tres mouseDrag- sobre cuatro columnas.
        if pas["pintados"] != 4:
            malas.append ("el arrastre por cuatro columnas enciende %d pasos" % pas["pintados"])
        if pas["entradas"] != 1:
            malas.append ("un arrastre por la rejilla deja %d entradas de deshacer y tiene que dejar 1"
                          % pas["entradas"])
        if pas["tras deshacer"] != 0:
            malas.append ("deshacer deja %d pasos encendidos: la foto no es de antes del gesto"
                          % pas["tras deshacer"])
        if pas["tras rehacer"] != 4:
            malas.append ("rehacer devuelve %d pasos de 4" % pas["tras rehacer"])

    nts = song.get ("deshacer notas")
    if not nts:
        print ("%-22s %s" % ("deshacer notas", "MAL - sin respuesta")); malas.append ("deshacer notas")
    else:
        print ("%-22s el toque escribe %d, deja %d foto(s), deshacer %d"
               % ("notas", nts["escritas"], nts["entradas"], nts["tras deshacer"]))
        if nts["escritas"] != 1:
            malas.append ("el toque en el piano escribe %d notas" % nts["escritas"])
        if nts["entradas"] != 1:
            malas.append ("escribir una nota deja %d entradas de deshacer" % nts["entradas"])
        if nts["tras deshacer"] != 0:
            malas.append ("deshacer deja %d notas escritas" % nts["tras deshacer"])

    rej = song.get ("deshacer rejilla")
    if not rej:
        print ("%-22s %s" % ("deshacer rejilla", "MAL - sin respuesta")); malas.append ("deshacer rejilla")
    else:
        #  Y SE JUZGA LA VISTA. Aqui se comparaban negras por paso, que era lo
        #  mismo mientras la rejilla ERA el paso guardado; ahora ensanchar la
        #  rejilla no mueve el paso ni un bit -ese es el arreglo- y la regla
        #  vieja habria dado MAL por funcionar bien.
        print ("%-22s vista %d -> %d, %d foto(s), deshacer %d   paso %s   pila %s"
               % ("rejilla", rej["antes"], rej["tras el mando"], rej["entradas"],
                  rej["tras deshacer"], rej["paso"], rej["pila"]))
        if rej["tras el mando"] == rej["antes"]:
            malas.append ("mover la REJILLA no cambia la vista: sigue en %d" % rej["antes"])
        if rej["entradas"] != 1:
            malas.append ("cambiar la REJILLA deja %d entradas de deshacer" % rej["entradas"])
        if rej["tras deshacer"] != rej["antes"]:
            malas.append ("deshacer deja la REJILLA en %d y tenia que volver a %d"
                          % (rej["tras deshacer"], rej["antes"]))
        #  Y el paso guardado tambien vuelve: si el cambio lo afino, deshacer
        #  tiene que engordarlo otra vez o el patron se queda reescrito.
        if abs (rej["paso"][0] - rej["paso"][1]) > 1e-6:
            malas.append ("deshacer deja el paso guardado en %.4f y tenia que volver a %.4f"
                          % (rej["paso"][1], rej["paso"][0]))
        #  Y la pila no puede crecer al deshacer: `applyState` repone el mando
        #  CON aviso, asi que sin la bandera la foto se apila sobre la pila que
        #  se esta desapilando y deshacer no termina nunca.
        if rej["pila"][1] > rej["pila"][0]:
            malas.append ("deshacer la REJILLA apila otra foto: la pila pasa de %d a %d"
                          % (rej["pila"][0], rej["pila"][1]))

    #  LA REJILLA MIDE EL CUADRADITO Y NO TOCA EL PATRON.
    #
    #  Esta es la regla que no se puede escribir contando golpes: al cambiar de
    #  rejilla los tres seguian encendidos y en los mismos pasos, y lo que
    #  cambiaba era lo que VALE un paso - o sea que el patron entero se oia al
    #  doble de velocidad y ninguna cuenta de notas lo veia. La cifra es el
    #  PULSO en el que cae cada golpe, antes y despues, y las dos listas tienen
    #  que ser IGUALES. La queja fue literal: "eso que cambias es la medida del
    #  cuadradito, con lo cual no deberia cambiarse ni el tiempo, ni los BPM,
    #  ni nada del proyecto, solo lo visual".
    #
    #  Y LA SEGUNDA MITAD, QUE ES LA QUE CAMBIO ESTA TANDA: lo que TIENE que
    #  moverse son las CASILLAS. Antes la prueba pedia que se moviera el paso
    #  guardado -"sin esto, una app que ignorase el mando saldria verde"- y
    #  ese era justamente el fallo: mover el paso guardado es reescribir el
    #  patron. Ahora se exige lo contrario, que el paso NO se mueva, y el
    #  guardia de "el mando hace algo" se muda a las casillas, que es donde de
    #  verdad se ve el cuadradito.
    zoom = song.get ("la rejilla es un zoom")
    if not zoom:
        print ("%-22s %s" % ("rejilla zoom", "MAL - sin respuesta")); malas.append ("la rejilla es un zoom")
    else:
        print ("%-22s paso %.4f -> %.4f   celdas %s   pulsos %s -> %s   bpm %s   largo %s"
               % ("rejilla zoom", zoom["paso antes"], zoom["paso despues"], zoom["celdas"],
                  zoom["pulsos antes"], zoom["pulsos despues"], zoom["bpm"], zoom["largo"]))
        #  UNO · el cuadradito se hace mas gordo de verdad. Sin esto, un mando
        #  desconectado saldria verde con todo lo demas igual.
        if zoom["celdas"][1] >= zoom["celdas"][0]:
            malas.append ("ensanchar la REJILLA deja las casillas en %d de %d: el mando no hace nada"
                          % (zoom["celdas"][1], zoom["celdas"][0]))
        #  DOS · y el paso GUARDADO no se mueve, que es lo contrario de lo que
        #  esta misma prueba pedia antes de la tanda.
        if abs (zoom["paso despues"] - zoom["paso antes"]) > 1e-6:
            malas.append ("ensanchar la REJILLA mueve el paso guardado de %.4f a %.4f: eso es "
                          "reescribir el patron" % (zoom["paso antes"], zoom["paso despues"]))
        if len (zoom["pulsos antes"]) != 3:
            malas.append ("la medida escribio %d golpes y tenian que ser 3"
                          % len (zoom["pulsos antes"]))
        if len (zoom["pulsos antes"]) != len (zoom["pulsos despues"]):
            malas.append ("cambiar la REJILLA deja %d golpes de los %d que habia"
                          % (len (zoom["pulsos despues"]), len (zoom["pulsos antes"])))
        else:
            for k, (a1, b1) in enumerate (zip (zoom["pulsos antes"], zoom["pulsos despues"])):
                if abs (a1 - b1) > 1e-6:
                    malas.append ("cambiar la REJILLA mueve el golpe %d del pulso %.4f al %.4f"
                                  % (k, a1, b1))
        if abs (zoom["bpm"][0] - zoom["bpm"][1]) > 1e-6:
            malas.append ("cambiar la REJILLA toca el tempo: %.2f -> %.2f"
                          % (zoom["bpm"][0], zoom["bpm"][1]))
        if zoom["largo"][0] != zoom["largo"][1]:
            malas.append ("cambiar la REJILLA cambia el bucle: %d pasos -> %d"
                          % (zoom["largo"][0], zoom["largo"][1]))

    #  EL TRESILLO, QUE ES DONDE LA REJILLA DEJA DE SALIR REDONDA.
    #
    #  La regla de arriba mide 1/16 -> 1/8, donde una rejilla y la otra son
    #  multiplos, y una app que solo supiera con eso saldria verde. Aqui van
    #  los dos casos que faltan, y el segundo es el que cambio de veredicto en
    #  esta tanda: los golpes que SOLO existen en el tresillo ya no se pierden.
    tres = song.get ("la rejilla y el tresillo")
    if not tres:
        print ("%-22s %s" % ("rejilla tresillo", "MAL - sin respuesta"))
        malas.append ("la rejilla y el tresillo")
    else:
        print ("%-22s recto %s -> tresillo %s -> vuelta %s   largo %s"
               % ("rejilla tresillo", tres["pulsos recto"], tres["pulsos tresillo"],
                  tres["pulsos vuelta"], tres["largo tresillo"]))
        print ("%-22s del tresillo %s -> recto %s   bucle %.4f de %d   dice: %s"
               % ("rejilla aprieta", tres["pulsos solo tresillo"], tres["pulsos apretados"],
                  tres["bucle"], tres["compas"], tres["dicho"]))

        #  UNO · IDA Y VUELTA POR EL TRESILLO. Entre una rejilla y su tresillo
        #  no hay factor entero, y los tres golpes tienen que volver a su pulso
        #  clavados las dos veces.
        if len (tres["pulsos recto"]) != 3:
            malas.append ("la medida del tresillo escribio %d golpes y tenian que ser 3"
                          % len (tres["pulsos recto"]))
        else:
            for cual in ("pulsos tresillo", "pulsos vuelta"):
                if len (tres[cual]) != 3:
                    malas.append ("pasar por el TRESILLO deja %d golpes de los 3 que habia (%s)"
                                  % (len (tres[cual]), cual))
                    continue
                for k, (a1, b1) in enumerate (zip (tres["pulsos recto"], tres[cual])):
                    if abs (a1 - b1) > 1e-6:
                        malas.append ("pasar por el TRESILLO mueve el golpe %d del pulso "
                                      "%.4f al %.4f (%s)" % (k, a1, b1, cual))

        #  DOS · Y LOS QUE SOLO EXISTEN EN EL TRESILLO TAMPOCO SE PIERDEN.
        #
        #  Aqui la prueba decia lo contrario: los pulsos 0.0833 y 0.3333 no se
        #  pueden decir con casillas de 0.25, asi que uno caia encima de otro y
        #  lo unico que se exigia era que el renglon de estado LLEVARA LA
        #  CUENTA. Desde que la casilla es solo lo que se dibuja y el patron se
        #  guarda con su propio paso, no hay nada que contar: los tres golpes
        #  siguen en su pulso mirandolos por donde se mire. Una prueba que
        #  sigue pidiendo que se pierdan notas es una prueba que defiende el
        #  fallo.
        if len (tres["pulsos solo tresillo"]) != 3:
            malas.append ("la medida de lo que no cabe escribio %d golpes y tenian que ser 3"
                          % len (tres["pulsos solo tresillo"]))
        elif len (tres["pulsos apretados"]) != 3:
            malas.append ("mirar el tresillo con la rejilla recta pierde %d de los 3 golpes: %s"
                          % (3 - len (tres["pulsos apretados"]), tres["pulsos apretados"]))
        else:
            for k, (a1, b1) in enumerate (zip (tres["pulsos solo tresillo"],
                                               tres["pulsos apretados"])):
                if abs (a1 - b1) > 1e-6:
                    malas.append ("mirar el tresillo con la rejilla recta mueve el golpe %d "
                                  "del pulso %.4f al %.4f" % (k, a1, b1))
        #  Y el renglon dice el cuadradito y no un aviso de que algo no cupo.
        if "1/16" not in tres["dicho"]:
            malas.append ("el renglon no dice que cuadradito hay puesto: %r" % tres["dicho"])

        #  Y EL BUCLE SIGUE SIENDO COMPASES ENTEROS. Se mide en PULSOS y no en
        #  pasos: "16 pasos" solo era un compas con la rejilla en 1/16, que es
        #  el numero escrito a mano que deformaba los patrones. Un compas son
        #  cuatro pulsos en las siete rejillas y con cualquier paso guardado.
        if abs (tres["bucle"] / 4.0 - round (tres["bucle"] / 4.0)) > 1e-4 or tres["bucle"] <= 0:
            malas.append ("el bucle dura %.4f pulsos y no son compases enteros" % tres["bucle"])
        if tres["largo apretado"] % tres["compas"] != 0:
            malas.append ("el largo apretado es %d pasos y un compas son %d"
                          % (tres["largo apretado"], tres["compas"]))

    #  Y QUE EL PASO GUARDADO NO SE ATASQUE FINO.
    #
    #  Las 42 parejas salen de un compas limpio cada una, asi que no ven lo que
    #  pasa tocando el mando siete veces seguidas. El paso guardado se AFINA
    #  para poder decir cada rejilla; si no se volviera a ENGORDAR cuando los
    #  datos lo permiten, acabaria en 1/48 de pulso -un compas son entonces los
    #  192 pasos enteros que la maquina guarda- y el siguiente cambio ya no
    #  cabria. No se pierde ni un golpe por el camino, asi que las reglas de
    #  pulsos dan verde mientras la app se queda sin mando.
    atasco = song.get ("la rejilla no se atasca")
    if not atasco:
        print ("%-22s %s" % ("rejilla atasco", "MAL - sin respuesta"))
        malas.append ("la rejilla no se atasca")
    else:
        print ("%-22s pasos %s   largos %s   %d negadas   vuelve a %s   pulsos %s"
               % ("rejilla atasco", atasco["pasos"], atasco["largos"], atasco["negadas"],
                  atasco["paso final"], atasco["pulsos"]))
        #  12 unidades de 1/48 de pulso son 1/16, que es donde empezo la ruta.
        if atasco["paso final"] != 12:
            malas.append ("tras recorrer las siete rejillas el paso guardado se queda en %d/48 "
                          "de pulso y tenia que volver a 12" % atasco["paso final"])
        if atasco["negadas"]:
            malas.append ("recorrer las siete rejillas seguidas niega %d cambios" % atasco["negadas"])
        if [round (x, 4) for x in atasco["pulsos"]] != [0.0, 0.25, 0.5]:
            malas.append ("recorrer las siete rejillas deja los golpes en %s y estaban en "
                          "[0.0, 0.25, 0.5]" % atasco["pulsos"])

    #  Y EL COMPAS DE LA CANCION SIGUE SIENDO UN COMPAS.
    #
    #  Todo lo de arriba mide el PATRON. La CANCION no la tocaba nadie, y es
    #  donde la correccion del compas se habia quedado a medias: el motor
    #  llevaba un `kBarSteps = 16` escrito a mano AL LADO de `pasosPorCompas()`,
    #  que lo deriva -dos tablas que dicen lo mismo son dos reglas-. Con el paso
    #  guardado en 1/16 las dos coinciden, y por eso ninguna prueba lo vio en
    #  siete tandas. Afinando el paso, el "compas" de la linea de tiempo valia
    #  un cuarto de compas: `muestrasPorCompas()` devolvia un cuarto, o sea el
    #  clip sonaba donde no se dibuja.
    #
    #  Se pregunta lo unico que la rejilla NO puede cambiar: cuanto dura un
    #  compas. Las siete cifras tienen que ser la MISMA, y no hace falta el
    #  tempo ni la frecuencia de muestreo para decirlo - que es lo que separa
    #  esto de repetir aqui la cuenta del C++.
    comp = song.get ("el compas de la cancion")
    if not comp:
        print ("%-22s %s" % ("compas cancion", "MAL - sin respuesta"))
        malas.append ("el compas de la cancion")
    else:
        print ("%-22s paso %s   pasos/compas %s   muestras/compas %s   clip en %s"
               % ("compas cancion", comp["pasos"], comp["compas pasos"],
                  comp["compas muestras"], comp["clip compas"]))
        distintas = sorted (set (comp["compas muestras"]))
        if len (distintas) != 1:
            malas.append ("un compas dura %s muestras segun la rejilla que se mire, "
                          "y el tempo no lo toca ninguna" % distintas)
        #  Y la otra mitad: pasos por compas es el paso guardado al reves. 48
        #  unidades son un pulso y un compas son cuatro, o sea 192 / paso.
        for u, pc in zip (comp["pasos"], comp["compas pasos"]):
            if pc != 192 // u:
                malas.append ("con el paso en %d/48 de pulso el compas dice %d pasos "
                              "y son %d" % (u, pc, 192 // u))
        if any (c != 3 for c in comp["clip compas"]):
            malas.append ("el clip estaba en el compas 3 y las siete rejillas lo "
                          "dejan en %s" % comp["clip compas"])

    #  Y EL CLIP SUENA DONDE SE DIBUJA, CON EL COMPAS EMPEZADO.
    #
    #  La regla de arriba mide el COMPAS, que es lo que habia. Desde que un
    #  clip lleva desfase dentro del compas eso deja de bastar: un motor que
    #  ignore el paso pinta la toma a un cuarto del compas 3 y la toca en el
    #  filo, o sea medio compas de error -1000 ms a 120- sin que nada falle y
    #  sin que se vea hasta que suena. Es la misma figura que "el clip suena
    #  donde no se dibuja" que costo la tanda anterior, en un sitio nuevo.
    #
    #  TRES cifras. La primera es el CONTROL: la misma corrida sin clip tiene
    #  que ser silencio (-1), porque si no cualquier cosa que sonara -un patron
    #  que quedo puesto, la cola de un pad- se leeria como "el clip". La
    #  segunda es donde la rejilla lo DIBUJA, que sale de la tabla que la
    #  rejilla recibe y no de los campos del clip: comparar el clip consigo
    #  mismo no compara nada. Y la tercera es donde SUENA, buscando la primera
    #  muestra que no es silencio en el bus.
    son = song.get ("el clip suena donde se dibuja")
    if son is None:
        print ("%-22s %s" % ("clip sub-compas", "MAL - sin respuesta"))
        malas.append ("el clip suena donde se dibuja")
    else:
        print ("%-22s dibuja %s   suena %s   sin clip %s   paso %s muestras"
               % ("clip sub-compas", son["dibuja paso"], son["suena paso"],
                  son["sin clip"], son["muestras paso"]))
        if son["sin clip"] != -1:
            malas.append ("sin clip la cancion no esta en silencio: suena en la "
                          "muestra %s, asi que la medida no mide el clip"
                          % son["sin clip"])
        elif abs (float (son["suena paso"]) - float (son["dibuja paso"])) > 0.05:
            malas.append ("el clip se dibuja en el paso %s y suena en el %s, o sea "
                          "%.2f pasos de donde se ve"
                          % (son["dibuja paso"], son["suena paso"],
                             float (son["suena paso"]) - float (son["dibuja paso"])))

    #  LAS SIETE REJILLAS CONTRA LAS SIETE, IDA Y VUELTA.
    #
    #  Las dos reglas de arriba miden tres parejas escogidas a mano, y con las
    #  tres en verde la queja siguio siendo "se deforman los patrones". Lo que
    #  no median es la ida y vuelta COMPLETA de las 42 parejas: la primera vez
    #  que se midieron fallaban 41, y con el suelo del largo arreglado seguian
    #  fallando 21, todas al ENGORDAR la rejilla.
    #
    #  Se mide lo que se OYE y no los pasos: el pulso de cada golpe y el largo
    #  del bucle en pulsos, que es lo unico que no depende de la rejilla con la
    #  que se mire. Y el veredicto es CERO: una sola pareja que no devuelva el
    #  patron es un patron que se deforma.
    siete = song.get ("las siete rejillas")
    NOMBRES = ["1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"]
    if not siete:
        print ("%-22s %s" % ("siete rejillas", "MAL - sin respuesta"))
        malas.append ("las siete rejillas")
    else:
        vueltas = siete["vueltas"]
        malPulsos, malBucle, negadas = [], [], []
        for v in vueltas:
            antes, vuelta = v["antes"], v["vuelta"]
            a, _, c = v["bucle"]
            par = "%s->%s" % (NOMBRES[v["de"]], NOMBRES[v["a"]])
            if v.get ("negada"): negadas.append (par)
            if len (antes) != len (vuelta) or any (abs (x - y) > 1e-6
                                                   for x, y in zip (antes, vuelta)):
                malPulsos.append ("%s %s -> %s" % (par, antes, vuelta))
            if abs (a - c) > 1e-6:
                malBucle.append ("%s bucle %.4f -> %.4f" % (par, a, c))

        print ("%-22s %d parejas, %d con pulsos malos, %d con bucle malo, %d negadas"
               % ("siete rejillas", len (vueltas), len (malPulsos), len (malBucle), len (negadas)))

        #  Las 42 tienen que estar: 7x7 menos las 7 de una rejilla consigo
        #  misma. Una medida que se deje parejas fuera es una medida que
        #  aprueba lo que no mira.
        if len (vueltas) != 42:
            malas.append ("la medida de las siete rejillas trae %d parejas y son 42" % len (vueltas))
        for m in malPulsos[:6]:
            malas.append ("ida y vuelta de rejilla que deforma el patron: %s" % m)
        if len (malPulsos) > 6:
            malas.append ("...y %d parejas mas con los golpes movidos" % (len (malPulsos) - 6))
        for m in malBucle[:6]:
            malas.append ("ida y vuelta de rejilla que cambia el bucle: %s" % m)
        if len (malBucle) > 6:
            malas.append ("...y %d parejas mas con el bucle cambiado" % (len (malBucle) - 6))
        #  Y NINGUNA SE NIEGA. Negarse no deforma nada -es la salida honrada
        #  cuando el patron no cabe en los pasos que la maquina guarda- pero
        #  saliendo de UN compas limpio tienen que caber las 42, que es para lo
        #  que el patron paso de 64 pasos a 192. Si vuelve a haber negativas,
        #  alguien bajo ese numero.
        if negadas:
            malas.append ("saliendo de un compas limpio se niegan %d cambios de rejilla: %s"
                          % (len (negadas), ", ".join (negadas[:6])))

    print()
    if malas:
        print ("FALLA:", ", ".join (malas))
        return 1
    print ("las operaciones de arreglo hacen lo que dicen, y lo que se escribe con el dedo se deshace")
    return 0


if __name__ == "__main__":
    sys.exit (main())

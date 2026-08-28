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

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre():
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
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

#  El patron: [paso, pad, nota, fuerza, repeticiones].
PAT_INICIAL  = [[0,0,5,90,1], [3,1,0,127,4]]
PAT_ADELANTE = [[1,0,5,90,1], [4,1,0,127,4]]
PAT_DOBLADO  = [[0,0,5,90,1], [3,1,0,127,4], [16,0,5,90,1], [19,1,0,127,4]]


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

    #  Lo que no se guarda se pierde sin avisar. El silenciado de carriles y
    #  el tramo en bucle van por el mismo arbol que escribe el proyecto.
    if vuelta is None:
        print ("%-22s %s" % ("guardar y volver", "MAL - sin respuesta")); malas.append ("vuelta")
    else:
        mira ("guardar y volver", [vuelta["mudos"], vuelta["bucle"]],
              [[1,0,0,1], [2,5]])

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

    print()
    if malas:
        print ("FALLA:", ", ".join (malas))
        return 1
    print ("las operaciones de arreglo hacen lo que dicen")
    return 0


if __name__ == "__main__":
    sys.exit (main())

#!/usr/bin/env python3
# ============================================================================
#  EL JUEGO DE ICONOS: lo que la app DIBUJA.
#
#  Las seis reglas del banco recorren el arbol de COMPONENTES, asi que de un
#  icono no ven absolutamente nada: es un dibujo dentro de una tapa que ya
#  median antes. Un icono puede salirse de su caja y pintar encima del rotulo,
#  puede quedar en cuatro trazos grises que no se leen, y sobre todo pueden
#  ser DOS EL MISMO - y las 812 corridas darian cero hallazgos en los tres
#  casos.
#
#  El tercero es el que obliga a que esta prueba exista. Es la misma leccion
#  que el banco de la fabrica: alli se median los sesenta y cuatro sonidos de
#  uno en uno -que no esten mudos, que igualen en sonoridad, que no saturen- y
#  con eso SESENTA Y CUATRO COPIAS DEL MISMO BOMBO sacaban sobresaliente en
#  las cinco. Con iconos pasa igual y ademas se cae solo: INSERTAR y QUITAR
#  salen naturalmente como el mismo dibujo con un signo cambiado, y en la fila
#  de herramientas de CANCION van uno al lado del otro.
#
#  QUE SE MIDE
#   - CABE: los limites del trazo, con su grosor, dentro de la rejilla de 24.
#   - TINTA: que porcion de la caja se pinta. Muy poca es una mancha gris; el
#     tope de arriba caza el otro extremo, un icono que es un cuadrado negro.
#   - LLENA: que el dibujo OCUPE su caja. Uno centrado al 50% se lee como si
#     estuviera mas lejos que sus vecinos, y en una fila se nota antes que
#     cualquier otra cosa.
#   - DISTINTOS: la distancia entre cada par de los rasterizados. Se comparan
#     DESENFOCADOS a proposito: dos dibujos que se diferencian en que un trazo
#     esta un pixel mas alla son el mismo icono para el ojo, y la distancia a
#     pelo diria que no.
#
#  El rasterizado lo hace la APP (UiAudit::volcadoIconos) y el juicio vive
#  aqui: las respuestas no pueden estar dentro del codigo que se prueba.
#
#      python3 Tests/iconos.py
# ============================================================================
import json, math, os, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (
        ROOT, "build", "Zati_artefacts", "Release", "Zati")

N = 24                     # lado del rasterizado para la GEOMETRIA
#  Y EL DE LA PRUEBA DE PARES ES OTRO, que lo dice la app.
#
#  Los dos numeros existen porque son dos preguntas. La caja, la tinta y lo
#  que llena se miden en la rejilla de 24, que es donde el dibujo se ESCRIBE.
#  Que dos iconos sean distintos se mide donde se LEE, y eso es
#  `Iconos::kLadoMin` - trece pixeles, el lado mas pequeño al que `reparteTapa`
#  saca un dibujo.
#
#  Y no es lo mismo: con la prueba puesta a 24 pasaban dos pares que a trece
#  son el MISMO dibujo. `insEp` era `piano` con una tecla mas -0.3676 a 24,
#  0.1987 a trece- y `vaciar` era `pad` con una equis en vez de un punto
#  -0.3676 y 0.2378-. Los dos redibujados: el piano electrico es la varilla que
#  el martillo golpea, y VACIAR se queda sin su caja.
N_LEE = None               # lo publica la app: Iconos::kLadoMin

#  LOS LISTONES. Salen de la primera medida y no de una opinion: se corrio el
#  juego entero, se miraron los numeros y se puso el liston donde separa lo
#  que hay de lo que no puede pasar. El de DISTINTOS es el unico que importa
#  de verdad, y esta puesto por debajo del par mas cercano que se acepto a
#  proposito - ver la tabla que imprime al final.
CABE_TOL   = 0.30          # px de la rejilla de 24
TINTA_MIN  = 0.045
TINTA_MAX  = 0.55
LLENA_MIN  = 0.70          # el lado mayor del dibujo, en fraccion de la caja
DISTINTO   = 0.25


def corre (lado = None):
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_OPEN": "pads", "ZATI_ICONOS": str (lado or N)})
    r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)

    iconos = []
    for linea in r.stdout.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "px" in d and "icono" in d:
            iconos.append (d)
    return iconos, r


def mapa (d):
    hex_ = d["px"]
    n = d["n"]
    return [int (hex_[i*2:i*2+2], 16) / 255.0 for i in range (n * n)], n


def desenfoca (m, n):
    #  Una caja de 3x3. Sin esto, dos dibujos identicos con un trazo movido un
    #  pixel salen mas lejos que dos que se parecen de verdad: la distancia por
    #  pixel castiga el desplazamiento, que es lo unico que el ojo perdona.
    out = [0.0] * (n * n)
    for y in range (n):
        for x in range (n):
            s = c = 0.0
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < n and 0 <= xx < n:
                        s += m[yy * n + xx]; c += 1.0
            out[y * n + x] = s / c
    return out


def distancia (a, b):
    #  RELATIVA A LA TINTA QUE HAY, no por pixel de la caja.
    #
    #  El primer intento era la distancia euclidea normalizada por el numero de
    #  pixeles, y no servia: la mayor parte de una caja de 24x24 esta vacia en
    #  los DOS iconos, asi que dos dibujos compactos salian siempre cerca
    #  aunque no se parecieran. Un cuadrado relleno y un circulo relleno del
    #  mismo tamano daban 0.079 -por debajo del liston, o sea "el mismo
    #  dibujo"- cuando lo unico que comparten es el sitio que ocupan.
    #
    #  Dividido por la UNION de la tinta, el numero dice lo que se queria
    #  preguntar: que parte de lo que se pinta es distinta. Identicos, 0.
    #  Es el mismo tropiezo que el banco de la fabrica, donde el coseno sobre
    #  bandas normalizadas daba 0.995 para cualquier par y hubo que pasarse a
    #  decibelios para que "parecido" y "identico" dejaran de ser el mismo
    #  numero. Primero se duda de la prueba.
    dif  = sum ((x - y) ** 2 for x, y in zip (a, b))
    union = sum (max (x, y) ** 2 for x, y in zip (a, b))
    return math.sqrt (dif / union) if union > 0.0 else 0.0


def caja (m, n, umbral=0.15):
    xs = [x for y in range (n) for x in range (n) if m[y*n+x] > umbral]
    ys = [y for y in range (n) for x in range (n) if m[y*n+x] > umbral]
    if not xs:
        return 0, 0
    return (max (xs) - min (xs) + 1), (max (ys) - min (ys) + 1)


#  ==========================================================================
#  EL DIBUJO Y LA PALABRA COMPARTEN RENGLON.
#
#  La queja llego mirando el telefono - "los sprites deben estar centrados en
#  altura con el texto" - y ninguna de las once reglas de `expo.py` puede
#  verla: un icono descentrado se maqueta perfecto, no solapa, no se sale, no
#  corta el rotulo y esta traducido.
#
#  Y LA PRIMERA MEDIDA SE EQUIVOCO, que a estas alturas es el patron. Se
#  miraron los LIMITES NOMINALES del camino - los `x,y,w,h` que esta misma
#  prueba imprime - y salieron DIECIOCHO descentrados, con `deshacer` a 2.73
#  unidades de 24. No lo estaban: `Iconos::dibuja` centra por la TINTA desde
#  que todos ocupan la misma caja, asi que lo nominal no es lo que se pinta.
#  Se mide lo PINTADO y contra el ROTULO, que es la pregunta que se hizo.
#
#  Con DOS cifras, que es lo que separa las dos formas de que esto se lea mal:
#  que compartan renglon Y que guarden la misma proporcion de una fila a otra.
#  Un icono perfectamente centrado que pesa el doble en una pestana que en una
#  tapa se lee igual de mal, y es lo que habia: 1.80 alturas de letra en una
#  pestana de 26 px contra 1.32 en una tapa de 48.
TAPA_DESVIO = 0.50         # px entre el centro de tinta del dibujo y el del rotulo
#
#  Y LA PROPORCION DEJA DE SER LA PREGUNTA: lo es el LADO.
#
#  Aqui habia un `TAPA_SPREAD = 0.25` que exigia que el lado del icono valiese
#  las mismas alturas de letra en todas las filas. Era un RODEO: lo que el
#  parrafo de arriba quiere -«todos ocupan la misma caja, o se leen como si el
#  pequeno estuviera mas lejos»- es que el dibujo mida lo mismo, y la
#  proporcion era la forma indirecta de pedirlo mientras el lado se sacaba del
#  alto de cada tapa. Desde que el lado es UNO -ver ZatiLookAndFeel::iconoLado-
#  la proporcion ya NO puede ser constante, porque la letra no lo es: en una
#  tapa de 26 px la letra vale 10 px por su propio suelo y en una de 48 vale
#  14.5 por su tope, asi que el mismo dibujo de 17 px pesa 1.70 y 1.24. Las dos
#  reglas se contradicen y la que vale es la directa.
#
#  El lado no se escribe aqui: lo dice la app en cada fila del volcado.


def tapas():
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_OPEN": "pads", "ZATI_TAPA": "8"})
    r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    out = []
    for linea in r.stdout.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"tapa"' in linea:
            try:    out.append (json.loads (linea))
            except Exception: pass
    return out


def juzgaTapas (fallos):
    filas = tapas()
    if not filas:
        fallos.append ("la app no volco ninguna tapa: no se mide el renglon")
        return

    print ("\n== el dibujo y la palabra, en %d tapas ==" % len (filas))
    print ("%-9s %8s %6s %8s %8s %8s" % ("tapa", "wxh", "letra", "lado", "desvio", "lado/letra"))
    razones = []
    for f in filas:
        ci = (f["icono"][0] + f["icono"][1]) / 2.0
        ct = (f["texto"][0] + f["texto"][1]) / 2.0
        d  = ci - ct
        razon = f["lado"] / max (1.0, f["letra"])
        razones.append (razon)
        marca = ""
        if abs (d) > TAPA_DESVIO:
            marca = "  DESCENTRADO"
            fallos.append ("%s: el dibujo va %+.2f px del rotulo" % (f["tapa"], d))
        print ("%-9s %4dx%-3d %6.2f %8d %+8.2f %8.2f%s"
               % (f["tapa"], f["w"], f["h"], f["letra"], f["lado"], d, razon, marca))

    lados = sorted (set (f["lado"] for f in filas))
    print ("   lados: %s   proporcion %.2f a %.2f"
           % (", ".join (str (l) for l in lados), min (razones), max (razones)))
    if len (lados) != 1:
        fallos.append ("hay %d lados de icono en la app: %s"
                       % (len (lados), ", ".join (str (l) for l in lados)))


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build --target Zati)")

    iconos, r = corre()
    if not iconos:
        print (r.stdout[-2000:])
        print (r.stderr[-2000:])
        sys.exit ("FALLA  la app no volco ningun icono")

    fallos = []
    print ("== %d iconos, rasterizados a %dx%d ==" % (len (iconos), N, N))
    print ("%-14s %6s %6s   %s" % ("icono", "tinta", "llena", "caja en la rejilla de 24"))

    mapas, borrosos = {}, {}
    for d in iconos:
        m, n = mapa (d)
        nombre = d["icono"]
        mapas[nombre] = m
        borrosos[nombre] = desenfoca (m, n)

        tinta = sum (m) / len (m)
        w, h = caja (m, n)
        llena = max (w, h) / float (n)

        x0, y0 = d["x"], d["y"]
        x1, y1 = x0 + d["w"], y0 + d["h"]
        se_sale = (x0 < -CABE_TOL or y0 < -CABE_TOL
                   or x1 > 24.0 + CABE_TOL or y1 > 24.0 + CABE_TOL)

        marca = ""
        if se_sale:            marca += " FUERA"; fallos.append ("%s se sale de su caja" % nombre)
        if tinta < TINTA_MIN:  marca += " SIN_TINTA"; fallos.append ("%s casi no pinta (%.3f)" % (nombre, tinta))
        if tinta > TINTA_MAX:  marca += " MANCHA"; fallos.append ("%s es una mancha (%.3f)" % (nombre, tinta))
        if llena < LLENA_MIN:  marca += " PEQUENO"; fallos.append ("%s no llena su caja (%.2f)" % (nombre, llena))

        print ("%-14s %6.3f %6.2f   %5.1f %5.1f %5.1f %5.1f%s"
               % (nombre, tinta, llena, x0, y0, d["w"], d["h"], marca))

    #  --- los que se parecen, AL TAMANO AL QUE SE LEEN ----------------------
    lee = iconos[0].get ("min") or 13
    leidos, _ = corre (lee)
    borrosos = {}
    for d in leidos:
        m, n = mapa (d)
        borrosos[d["icono"]] = desenfoca (m, n)
    print ("\n== y comparados al lado mas pequeno al que la app los dibuja: %d px ==" % lee)

    nombres = sorted (borrosos)
    pares = []
    for i in range (len (nombres)):
        for j in range (i + 1, len (nombres)):
            a, b = nombres[i], nombres[j]
            pares.append ((distancia (borrosos[a], borrosos[b]), a, b))
    pares.sort()

    print ("\n== los cinco pares mas parecidos de %d ==" % len (pares))
    for d, a, b in pares[:5]:
        print ("   %-14s %-14s  %.4f%s" % (a, b, d, "   MISMO DIBUJO" if d < DISTINTO else ""))
    mediana = pares[len (pares) // 2][0]
    print ("   mediana de los %d pares: %.4f   liston: %.4f" % (len (pares), mediana, DISTINTO))

    #  DOS ESTADOS DE UN MISMO MANDO SI PUEDEN PARECERSE: el candado abierto y
    #  el cerrado son la misma tapa en sus dos posiciones -nunca se ven a la
    #  vez- y lo que tienen que compartir es justamente la forma, o dejarian de
    #  leerse como el mismo control. Es lo mismo que el banco de la fabrica hace
    #  con el shaker y la maraca: se acepta, se escribe por que, y se sigue
    #  imprimiendo la distancia para que no baje sin que nadie se entere.
    ESTADOS = { ("fijo", "momentaneo") }
    for d, a, b in pares:
        if d < DISTINTO and (a, b) not in ESTADOS and (b, a) not in ESTADOS:
            fallos.append ("%s y %s son el mismo dibujo (%.4f)" % (a, b, d))

    juzgaTapas (fallos)

    print()
    if fallos:
        for f in fallos:
            print ("FALLA  " + f)
        sys.exit (1)
    print ("OK  %d iconos, %d pares" % (len (iconos), len (pares)))


if __name__ == "__main__":
    main()

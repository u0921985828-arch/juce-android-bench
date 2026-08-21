#!/usr/bin/env python3
# ============================================================================
#  EL JUEGO DE ICONOS Y EL GRANO DEL CHASIS: lo que la app DIBUJA.
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
#   - GRANO: cuanto mueve la textura del chasis lo que Tests/skins.py
#     certifico. Esa prueba mide la TABLA, y desde que el cuerpo lleva textura
#     la tabla ya no es lo que se pinta. Sin esta linea se podria subir el
#     grano hasta romper el contraste sin que nada fallase.
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

N = 24                     # lado del rasterizado

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
#  EL GRANO NO SE JUZGA POR CUANTO MUEVE EL RATIO SINO POR DONDE LO DEJA.
#  La primera version pedia que el desvio fuera menor que 0.20 y era un numero
#  inventado: los cuatro salieron entre 1.07 y 1.82 y los cuatro estaban bien -
#  un contraste de 13.79 que baja a 12.14 no ha dejado de leerse. Lo que si
#  puede pasar es que el peor pixel del chasis se lleve la tinta por debajo del
#  4.50 que Tests/skins.py exige para lo que hay que leer, y que la textura
#  crezca sin que nadie se entere. Las dos cosas, y con el numero de la otra
#  prueba, no con uno nuevo.
GRANO_MIN  = 4.50          # = MIN_TEXT de Tests/skins.py
GRANO_MOV  = 0.15          # fraccion del ratio de la tabla


def corre():
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_OPEN": "pads", "ZATI_ICONOS": str (N)})
    r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)

    iconos, granos = [], []
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
        elif "grano" in d:
            granos.append (d)
    return iconos, granos, r


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


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build --target Zati)")

    iconos, granos, r = corre()
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

    #  --- los que se parecen ------------------------------------------------
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

    for d, a, b in pares:
        if d < DISTINTO:
            fallos.append ("%s y %s son el mismo dibujo (%.4f)" % (a, b, d))

    #  --- el grano ----------------------------------------------------------
    print ("\n== el grano del chasis, contra lo que Tests/skins.py certifico ==")
    print ("%-10s %6s %8s %8s %8s %7s" % ("carcasa", "amp", "tabla", "mas claro", "mas oscuro", "mueve"))
    for g in granos:
        peor = min (g["claro"], g["oscuro"])
        mov  = g["desvio"] / g["base"] if g["base"] > 0 else 0.0
        malo = (peor < GRANO_MIN) or (mov > GRANO_MOV)
        print ("%-10s %6.3f %8.2f %8.2f %8.2f %6.1f%%%s"
               % (g["grano"], g["amp"], g["base"], g["claro"], g["oscuro"], mov * 100.0,
                  "   ILEGIBLE" if peor < GRANO_MIN else ("   SE PASA" if malo else "")))
        if peor < GRANO_MIN:
            fallos.append ("el grano deja la tinta en %.2f sobre %s, por debajo de %.2f"
                           % (peor, g["grano"], GRANO_MIN))
        elif mov > GRANO_MOV:
            fallos.append ("el grano mueve el contraste un %.0f%% en %s" % (mov * 100.0, g["grano"]))
    if len (granos) != 4:
        fallos.append ("faltan carcasas en el volcado del grano: %d de 4" % len (granos))

    print()
    if fallos:
        for f in fallos:
            print ("FALLA  " + f)
        sys.exit (1)
    print ("OK  %d iconos, %d pares, 4 carcasas" % (len (iconos), len (pares)))


if __name__ == "__main__":
    main()

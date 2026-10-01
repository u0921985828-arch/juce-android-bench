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
#   - MARCO: que el dibujo OCUPE su caja POR LOS DOS LADOS. Uno centrado al
#     50% se lee como si estuviera mas lejos que sus vecinos, y en una fila se
#     nota antes que cualquier otra cosa.
#   - GROSOR: que el pelo con el que esta dibujado sea UNO para todos. No se
#     juzga en el raster -ver el bloque de la regla- sino en el FUENTE: toda
#     anchura de trazo y toda banda rellena que sea una linea salen de
#     `Iconos::kBanda`, y el raster solo imprime el numero para poder comparar
#     una corrida con la de antes.
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

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

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
LLENA_MIN  = 0.70          # el lado MENOR del dibujo, en fraccion de la caja
DISTINTO   = 0.25
#  Y EL VECINDARIO DE UNA LINEA, en pixeles de la rejilla de 24. `Iconos::kBanda`
#  mide 1.73; una banda rellena por debajo de esto es una linea y tiene que salir
#  de ahi. Ver la regla del grosor, en `main`.
BANDA_MAX  = 2.5
#  Y CUANTOS RECTANGULOS RELLENOS SE LE PERMITE NO LEER: NINGUNO. De los dos
#  lados, al menos uno tiene que estar escrito con un numero, y ese es el grosor
#  -el otro es el largo, que es lo que un bucle hace variar-. Se cuenta igual,
#  que es la cadena de control de esta regla: si sube de cero, la regla esta
#  dejando pasar bandas. Ver la regla del grosor.
BANDAS_ILEGIBLES = 0


def corre (lado = None):
    env = dict (os.environ, DISPLAY=PANTALLA)
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


def grosorBandas (m, n, umbral=0.15, tope=6.0):
    """El ancho de la BANDA de tinta mas comun, en pixeles de la rejilla.

    Para cada pixel con tinta se mide la racha de tinta que pasa por el en las
    cuatro direcciones -las dos diagonales contadas en pixeles de verdad, o sea
    cada paso por raiz de dos- y se queda la MENOR, que es el ancho de la banda
    por su lado corto. Una linea de dos pixeles da dos vaya por donde vaya.

    Se dejan fuera los pixeles cuya banda pasa de `tope`, que a veinticuatro son
    tres veces y media una linea de esta cara: eso ya no es una linea sino el
    interior de una MANCHA -el cuadrado de `stop`, el cuerpo de `fijo`- y un
    grosor de linea no dice nada de una mancha. Devuelve el modo redondeado a
    medio pixel y que parte de la tinta era banda.
    """
    dirs = [(1, 0, 1.0), (0, 1, 1.0), (1, 1, math.sqrt (2.0)), (1, -1, math.sqrt (2.0))]
    anchos = []
    for y in range (n):
        for x in range (n):
            if m[y * n + x] <= umbral:
                continue
            peor = None
            for dx, dy, paso in dirs:
                c = 1
                i, j = x + dx, y + dy
                while 0 <= i < n and 0 <= j < n and m[j * n + i] > umbral:
                    c += 1; i += dx; j += dy
                i, j = x - dx, y - dy
                while 0 <= i < n and 0 <= j < n and m[j * n + i] > umbral:
                    c += 1; i -= dx; j -= dy
                a = c * paso
                if peor is None or a < peor:
                    peor = a
            anchos.append (peor)
    if not anchos:
        return 0.0, 0.0
    banda = [a for a in anchos if a <= tope]
    if not banda:
        return 0.0, 0.0
    cuenta = {}
    for a in banda:
        k = round (a * 2.0) / 2.0
        cuenta[k] = cuenta.get (k, 0) + 1
    modo = max (cuenta.items(), key=lambda kv: (kv[1], -kv[0]))[0]
    return modo, len (banda) / float (len (anchos))


def centroTinta (m, n, umbral=0.15):
    """El centro de la CAJA de tinta, en pixeles de la rejilla de `n`."""
    xs = [x for y in range (n) for x in range (n) if m[y*n+x] > umbral]
    ys = [y for y in range (n) for x in range (n) if m[y*n+x] > umbral]
    if not xs:
        return n / 2.0, n / 2.0
    return ((min (xs) + max (xs) + 1) / 2.0, (min (ys) + max (ys) + 1) / 2.0)


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
#  Y EL DESVIO DENTRO DE LA PROPIA CAJA, en pixeles de la rejilla de 24.
#
#  Se imprime en la primera corrida antes de fijarlo, que es como esta casa
#  pone un liston: la cifra sale de la poblacion y no de una idea. Ver el
#  bloque de `centroTinta`.
CENTRO_MAX  = 1.50
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
    env = dict (os.environ, DISPLAY=PANTALLA)
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

    #  ==========================================================================
    #  EL ANCHO DE UNA CIFRA LO DICE LA FUENTE, y esto lo lee del FUENTE.
    #
    #  Las cifras dibujadas tienen una regla que ninguna medida de pixeles puede
    #  ver: cada una ocupa la celda que la fuente le habria dado, para que lo que
    #  `expo.py` mide con `getStringWidth` y lo que se pinta sean el mismo numero.
    #  Con un avance propio, un rotulo de cifras cabria en la cuenta y no en la
    #  pantalla —o al reves— y las reglas TRUNC, SQUEEZE y CORTADO estarian
    #  midiendo un ancho que no existe.
    #
    #  Y SALIO MEDIDO que hacia falta escribirlo: con `Cifras::dibuja` cambiado a
    #  un ancho fijo de 9 px por cifra, las 1456 corridas de `expo.py` siguen
    #  dando **los mismos 3346 TOUCH y cero en las diecinueve duras**, porque el
    #  banco mide el rotulo con la fuente y la fuente no cambio. Una regla que
    #  solo vive en un comentario no protege nada, asi que se lee del codigo —la
    #  misma figura que `maqueta.py` con los tokens de `Metrics` y `suministro.py`
    #  con los SHA de las acciones.
    ico = os.path.join (ROOT, "Source", "Iconos.h")
    try:
        fuente = open (ico, encoding="utf-8").read()
    except OSError:
        sys.exit ("FALLA  no puedo leer Source/Iconos.h: la regla del ancho no mide nada")
    cuerpo = fuente.split ("namespace Cifras", 1)[-1]
    if "GlyphArrangement::getStringWidth" not in cuerpo:
        fallos.append ("Cifras::dibuja no saca su ancho de la fuente: lo medido y lo "
                       "dibujado dejan de ser el mismo numero")

    #  ==========================================================================
    #  EL GROSOR DE LINEA ES UNO, Y ESO TAMBIEN SE LEE DEL FUENTE.
    #
    #  Llego del telefono, pegado a lo del marco: «que todos los sprites esten
    #  dentro del mismo marco de altura y de anchura como tambien de grosor de
    #  linea». Lo del marco se arreglo en el dibujo y se vigila con el raster
    #  -ver `llena`-. Esto NO se puede vigilar con el raster, y conviene
    #  escribir por que antes de que a alguien le parezca una dejadez.
    #
    #  En el raster, el grosor de una linea y el ancho de una mancha son el
    #  mismo numero. `grosorBandas` mide la banda mas comun de cada icono y
    #  sirve para comparar una corrida con la de antes, pero no puede ser un
    #  liston: `stop` es un cuadrado de diecisiete pixeles y `trm` dos cunas
    #  macizas, y pedirles una banda fina es pedirles que dejen de ser lo que
    #  son. Medido antes de esta tanda, veintiuno de los 140 tenian su banda
    #  mas comun en cuatro pixeles o mas con la mayoria de su tinta en banda, y
    #  de esos solo una parte eran lineas gordas: el resto eran formas.
    #
    #  Lo que SI es exacto es de donde sale el numero. `Iconos::kBanda` es el
    #  unico grosor de linea del fichero -el mismo que `grosorPara` le da a la
    #  rejilla de 24- y lo que esta regla comprueba es que nadie vuelva a
    #  teclear el suyo:
    #
    #    1. la constante existe, y `bandaH`/`bandaV` salen de ella;
    #    2. ninguna anchura de trazo del fichero es un numero tecleado;
    #    3. ninguna banda RELLENA que sea una linea se escribe con su propio
    #       ancho. "Que sea una linea" son dos cosas a la vez: que mida POR SU
    #       LADO CORTO menos de 2.5 px de la rejilla de 24 -o sea en el
    #       vecindario de `kBanda`, que mide 1.73- y que sea al menos el DOBLE
    #       de larga que de ancha. Lo primero sin lo segundo cazaria el talon
    #       del arco de `insCello`, que es un taco de 2.6 x 2.4, y un taco no
    #       tiene grosor de linea; lo segundo sin lo primero cazaria los
    #       bloques de `rep`, que son de 4.3 y son bloques a proposito.
    #
    #  Habia VEINTICINCO anchos distintos entre 0.7 y 5.0 repartidos por
    #  treinta y tres iconos, mas tres anchuras de trazo tecleadas -la escalera
    #  de `bit` a 3.4, la espiral de `insCuerdas` a 1.75 y la Z de la marca a
    #  24*0.086, que era el `grosorPara` de hace dos versiones-, mas las cifras,
    #  que iban a 0.115 de su alto contra 0.072 de un icono: un numero al lado
    #  de un icono del mismo tamano salia un sesenta por ciento mas gordo.
    #
    #  Lo que esta regla NO juzga, y se dice para que no se confunda con un
    #  olvido: un rectangulo TRAZADO mas estrecho que dos veces `kBanda` -las
    #  cuatro de `mano`, las tres de `dly`, las tres de `rep`- se pinta macizo
    #  porque sus dos paredes se solapan, y lo que se ve es una banda de cuatro
    #  pixeles. Es un bloque y no una linea, y quien lo quiera cambiar tendra
    #  que decidir si una mano son cuatro dedos o cuatro rayas. Se deja escrito.
    import re                                                      # noqa: E402
    if not re.search (r"constexpr float kBanda\s*=", fuente):
        fallos.append ("no hay `Iconos::kBanda`: el grosor de linea vuelve a ser "
                       "un numero por icono")
    for ayuda in ("bandaH", "bandaV"):
        cuerpoAyuda = fuente.split ("inline void %s " % ayuda, 1)
        if len (cuerpoAyuda) < 2:
            fallos.append ("no hay `detalle::%s`: una banda rellena no tiene de "
                           "donde sacar el grosor" % ayuda)
        elif "kBanda" not in cuerpoAyuda[1].split ("}", 1)[0]:
            fallos.append ("`detalle::%s` no usa `kBanda`" % ayuda)

    #  Cadena de control: si el barrido no ve ni un trazo, no mide nada.
    trazos = re.findall (r"PathStrokeType\s*\(\s*([^,]+),", fuente)
    if len (trazos) < 3:
        fallos.append ("solo veo %d anchuras de trazo en Iconos.h: el barrido del "
                       "grosor no esta midiendo nada" % len (trazos))
    for t in trazos:
        if "kBanda" not in t and "grosor" not in t:
            fallos.append ("hay una anchura de trazo tecleada en Iconos.h: `%s` "
                           "(sale de `kBanda` o de `grosorPara`)" % t.strip())

    #  Y las bandas rellenas alargadas. `R` es el camino que se RELLENA; lo que
    #  va a `L` es un contorno y su pared ya la pone `grosorPara`.
    #  Los parentesis se CIERRAN contando, que `(float) i * 4.2f` lleva los
    #  suyos dentro y un `[^)]*` corta la llamada por la mitad.
    def argumentos (texto, desde):
        prof, i = 0, desde
        while i < len (texto):
            if texto[i] == "(": prof += 1
            elif texto[i] == ")":
                prof -= 1
                if prof == 0: return texto[desde + 1:i]
            i += 1
        return ""

    def parte (texto):
        #  Por comas de primer nivel, o `(float) i` partiria mal.
        prof, act, out = 0, "", []
        for c in texto:
            if c == "(": prof += 1
            elif c == ")": prof -= 1
            if c == "," and prof == 0:
                out.append (act); act = ""
            else:
                act += c
        out.append (act)
        return [a.strip() for a in out]

    bandas = [argumentos (fuente, m.end() - 1)
              for m in re.finditer (r"\bR\.add(?:Rounded)?Rectangle\s*\(", fuente)]
    if not bandas:
        fallos.append ("no veo ni un rectangulo relleno en Iconos.h: el barrido de "
                       "las bandas no esta midiendo nada")
    #  LOS CUATRO NUMEROS SE LEEN DE UNO EN UNO, y lo que no sea un numero se
    #  CUENTA en vez de descartarse en silencio: un `R.addRoundedRectangle (x, y,
    #  2.6f, 10.0f, 0.9f)` con la esquina escrita en una variable corre los
    #  argumentos y haria que el ancho leido fuese el radio. Es la cadena de
    #  control de esta regla: si la cuenta de ilegibles sube, la regla esta
    #  dejando pasar bandas y hay que venir a mirar.
    sueltas, ilegibles = [], 0
    for b in bandas:
        if "kBanda" in b:
            continue
        arg = [a.rstrip ("f") for a in parte (b)]
        #  Solo el ANCHO y el ALTO: donde se pone la banda da igual para su
        #  grosor, y pedir que las cuatro fuesen numeros dejaba sin leer las que
        #  van en un bucle -las de `sonido`, los golpes de `humanizar`-, que son
        #  justo las que se escriben una vez y salen diez.
        if len (arg) < 4:
            ilegibles += 1
            continue
        lado = [a if re.fullmatch (r"-?\d+\.?\d*", a) else None for a in arg[2:4]]
        if lado[0] is None and lado[1] is None:
            ilegibles += 1
            continue
        if None in lado:
            #  UNO DE LOS DOS LADOS EN UNA EXPRESION ES UNA BARRA: lo que varia
            #  es su LARGO -la altura de cada barra de `sonido`, de `humanizar`,
            #  de `niveles`- y el que esta escrito es el GROSOR. Si ese esta por
            #  debajo del vecindario de `kBanda` es una linea y no hay que
            #  mirarle la proporcion, que la pone el bucle.
            ancho = float (lado[0] if lado[0] is not None else lado[1])
            if 0.0 < ancho < BANDA_MAX:
                sueltas.append ("%.1f de ancho y el largo en una variable" % ancho)
            continue
        w, h = float (lado[0]), float (lado[1])
        corto, largo = min (w, h), max (w, h)
        if 0.0 < corto < BANDA_MAX and largo >= 2.0 * corto:
            sueltas.append ("%.1fx%.1f" % (w, h))
    print ("== el grosor sale de un sitio: %d anchuras de trazo, %d rectangulos "
           "rellenos, %d sin poder leer ==" % (len (trazos), len (bandas), ilegibles))
    if ilegibles > BANDAS_ILEGIBLES:
        fallos.append ("hay %d rectangulos rellenos con las medidas en una "
                       "expresion y la regla del grosor no los ve (tope %d)"
                       % (ilegibles, BANDAS_ILEGIBLES))
    for x in sueltas:
        fallos.append ("hay una banda rellena de %s escrita con su propio ancho: "
                       "por debajo de %.1f px y el doble de larga que de ancha es "
                       "una LINEA, y una linea sale de `bandaH`/`bandaV`"
                       % (x, BANDA_MAX))

    print ("== %d iconos, rasterizados a %dx%d ==" % (len (iconos), N, N))
    print ("%-14s %6s %6s %6s %5s   %s"
           % ("icono", "tinta", "marco", "grosor", "centro", "caja en la rejilla de 24"))

    mapas, borrosos = {}, {}
    for d in iconos:
        m, n = mapa (d)
        nombre = d["icono"]
        mapas[nombre] = m
        borrosos[nombre] = desenfoca (m, n)

        tinta = sum (m) / len (m)
        w, h = caja (m, n)
        #  EL MARCO TIENE DOS LADOS, Y ESTA REGLA MIRABA UNO.
        #
        #  Miraba `max (w, h)`, o sea el lado LARGO, y el largo lo llena
        #  siempre: `Iconos::dibuja` escala cada dibujo hasta que su lado mayor
        #  toca el 96 % de la caja. Lo que no veia es el OTRO lado, y por ahi se
        #  colaba el juego entero. El escalado es uniforme —y tiene que serlo,
        #  que deformar saca la rueda de AJUSTES ovalada— asi que el lado corto
        #  cae donde lo deje la PROPORCION del dibujo. Medido sobre los 140: la
        #  mediana del lado corto estaba en 0.83 de la caja y TREINTA Y SIETE
        #  iconos por debajo de 0.70, con `fla` y `pit` en 0.42 —una banda de
        #  diez pixeles de alto en una caja de veinticuatro—. En una fila de
        #  tapas eso se lee como iconos de dos tamanos distintos, que es de
        #  donde vino: «que todos los sprites esten dentro del mismo marco de
        #  altura y anchura».
        #
        #  El liston no es nuevo: es el MISMO, aplicado a los dos lados.
        llena = min (w, h) / float (n)
        #  Y DONDE CAE LA TINTA DENTRO DE SU CAJA, que es lo que ninguna de las
        #  cuatro miraba.
        #
        #  Llego del telefono -«asegurate tambien que todos los sprites de los
        #  efectos estan centrados, hay algunos que falla»- y al ir a buscarlo
        #  salio que el banco no podia verlo: `SPRITE` en `expo.py` da CERO en
        #  las 1456 corridas y las cuatro de aqui pasan, porque CABE mira si el
        #  trazo se sale, TINTA cuanto pinta, LLENA cuanto ocupa y DISTINTOS si
        #  dos se parecen. Un dibujo que llena su caja y esta desplazado dos
        #  pixeles a la derecha las cumple las cuatro.
        #
        #  Y se ve enseguida, porque un icono no se mira solo: `reparteTapa`
        #  centra la CAJA en la tapa, asi que lo que el ojo compara en una fila
        #  es donde cae la tinta DENTRO de esa caja. Dos iconos con la misma
        #  caja y la tinta en distinto sitio se leen como una fila torcida.
        #
        #  Se mide con el centro de la caja de tinta y no con el centroide: el
        #  centroide lo mueve un relleno grande en una esquina -REC es un anillo
        #  y STOP un bloque- y eso es peso, no posicion. Lo que el ojo alinea son
        #  los filos.
        #  Y EL PELO CON EL QUE ESTA DIBUJADO, que se imprime y no se juzga:
        #  ver el bloque de la regla del grosor, mas arriba.
        pelo, enBanda = grosorBandas (m, n)
        cx, cy = centroTinta (m, n)
        desvio = max (abs (cx - n / 2.0), abs (cy - n / 2.0)) * 24.0 / n

        x0, y0 = d["x"], d["y"]
        x1, y1 = x0 + d["w"], y0 + d["h"]
        se_sale = (x0 < -CABE_TOL or y0 < -CABE_TOL
                   or x1 > 24.0 + CABE_TOL or y1 > 24.0 + CABE_TOL)

        marca = ""
        if se_sale:            marca += " FUERA"; fallos.append ("%s se sale de su caja" % nombre)
        #  EL MENOS NO PUEDE PAGAR MAS TINTA, y es una excepcion declarada como
        #  la del marco. `TINTA_MIN` existe para que un DIBUJO no sea cuatro
        #  trazos grises que no se leen: un icono dice una cosa entera y si la
        #  dice con poca tinta es que la dice mal. El menos es UN TRAZO, y lo que
        #  pinta es lo que un menos pinta —una barra de 11 px en una caja de 24,
        #  o sea 0.032—. Cumplia el liston solo porque las cifras iban con un
        #  trazo un sesenta por ciento mas gordo que los iconos; igualado el
        #  grosor, el que se queda corto es el liston y no el glifo. Los diez
        #  digitos siguen entre 0.097 y 0.188 y no hacen falta excepciones.
        if tinta < TINTA_MIN and nombre != "cifra -":
            marca += " SIN_TINTA"; fallos.append ("%s casi no pinta (%.3f)" % (nombre, tinta))
        if tinta > TINTA_MAX:  marca += " MANCHA"; fallos.append ("%s es una mancha (%.3f)" % (nombre, tinta))
        #  EL MARCO NO APLICA A LAS CIFRAS, y es una excepcion declarada y no
        #  una rebaja del liston. Eran una -el menos- cuando la regla miraba el
        #  lado largo, y son las once desde que mira el corto, por la misma
        #  razon y no por una nueva: la PROPORCION de una cifra es parte de la
        #  cifra. Un 1 es estrecho, un menos es una barra corta y los diez
        #  digitos salen a 0.67 x 0.92 porque es la caja de un digito; pedirles
        #  que llenen un cuadrado es pedirles que dejen de parecer numeros, y
        #  entre ellas —que es con quien se ven, en una fila de pasos— la
        #  proporcion ya es la misma para todas. La regla existe porque un
        #  dibujo pequeno se lee como si estuviera mas lejos que sus vecinos, y
        #  eso vale para un icono, que dice una cosa entera.
        if llena < LLENA_MIN and not nombre.startswith ("cifra"):
            marca += " PEQUENO"
            fallos.append ("%s no llena su marco por el lado corto (%.2f)" % (nombre, llena))
        if desvio > CENTRO_MAX:
            marca += " DESCENTRADO"
            fallos.append ("%s tiene la tinta a %.1f px del centro de su caja (tope %.1f)"
                           % (nombre, desvio, CENTRO_MAX))

        print ("%-14s %6.3f %6.2f %6s %5.1f   %5.1f %5.1f %5.1f %5.1f%s"
               % (nombre, tinta, llena,
                  "%.1f/%.0f%%" % (pelo, enBanda * 100.0), desvio,
                  x0, y0, d["w"], d["h"], marca))

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

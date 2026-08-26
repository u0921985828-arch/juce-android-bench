#!/usr/bin/env python3
# ============================================================================
#  EL ICONO DEL LANZADOR: LA REJILLA CON LA Z ENCENDIDA.
#
#  Esta maquina tenia DOS logos y ninguno miraba al otro. `Iconos::marca()` es
#  un pad con la Z recortada dentro y vive en la cabecera y en el banner; el
#  icono de la app era `ci/icon.png`, un PNG de 1024 HECHO A MANO - placa
#  blanca, dieciseis pads grises y cuatro de colores sueltos - que no generaba
#  nadie, no media nadie y no compartia un solo token con la app.
#
#  Ahora es la rejilla 4x4 con la Z dibujada por los pads ENCENDIDOS: los dos
#  logos a la vez. Y se genera, por lo mismo que el banner de la tienda - un
#  dibujo hecho fuera se queda con la paleta del dia que se hizo.
#
#      ZATI_ICONO=ci/icon.png ./build/Zati_artefacts/Release/Zati
#
#  El PNG se COMMITEA, igual que los .flac de Tools/fabrica.py: Projucer lo
#  necesita en tiempo de compilacion, y el codigo esta para que se sepa de
#  donde sale. Este fichero lo regenera y lo juzga.
#
#  Cuatro cosas, y ninguna se puede juzgar mirando el dibujo a 1024:
#
#  1. QUE LA Z SE LEA A 48 PX, que es lo unico que importa de un icono - 48 dp
#     es lo que mide en el cajon de aplicaciones. Se reduce a las cuatro
#     densidades que JUCE escribe, se muestrea el centro de las dieciseis
#     celdas y se separan por el punto medio. Tienen que salir EXACTAMENTE los
#     diez de la Z.
#  2. CONTRASTE entre encendido y apagado, con el liston de skins.py y no con
#     uno nuevo: el liston de una prueba no se reinventa en la de al lado.
#  3. QUE LA REJILLA SE LEA COMO REJILLA: los apagados contra el cuerpo en dE.
#     Sin eso el icono no son dieciseis pads con diez encendidos, son diez
#     cuadrados flotando, y el juego con la rejilla se pierde.
#  4. Y QUE SOBREVIVA A LA MASCARA. JUCE escribe el icono como LEGACY, asi que
#     de Android 8 en adelante el lanzador lo recorta con la suya. La esquina
#     de la tapa mas exterior tiene que caer dentro de la circunferencia
#     inscrita. El icono de hoy la tenia FUERA.
#
#  Mas la que la app contesta ella, porque el PNG no puede: que la Z del icono
#  y la Z de la marca de la cabecera sigan siendo la misma (desacuerdo 0).
#
#      python3 Tests/icono.py
# ============================================================================
import os, subprocess, sys, tempfile, shutil, json, math

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (
        ROOT, "build", "Zati_artefacts", "Release", "Zati")
DEST = os.path.join (ROOT, "ci", "icon.png")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
#  Las cuentas de color salen de skins.py y no se reescriben aqui: dos copias
#  de la misma formula son dos formulas.
from skins import lum, ratio, dE, croma, MIN_WELL

#  Las cuatro densidades que JUCE escribe (3/8, 4/8, 6/8 y 8/8 del original) se
#  ven a 36, 48, 72 y 96 dp. La que manda es 48.
TAMANOS = [36, 48, 72, 96]
LADO    = 4
#  Una fila invisible POR CADA LADO, o sea 6x6: ver StoreArt::appIcon.
TOTAL   = LADO + 2

hechas, fallos = [], []

def mide (nombre, ok, detalle=""):
    hechas.append (nombre)
    if not ok: fallos.append (nombre)
    print ("  %-6s %s  %s" % ("OK" if ok else "FALLA", nombre, detalle))


def genera (destino=None, estilo=None, mascara=None, recta=False):
    casa = tempfile.mkdtemp (prefix="zati-icono-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "ZATI_ICONO": destino or DEST})
        if estilo  is not None: env["ZATI_ICONO_ESTILO"]  = str (estilo)
        if mascara is not None: env["ZATI_ICONO_MASCARA"] = mascara
        if recta: env["ZATI_Z_RECTA"] = "1"
        r = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    for linea in r.stdout.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"icono"' in linea:
            try: return json.loads (linea)
            except Exception: pass
    return None


def pixeles (path, lado):
    """El PNG reducido a `lado`, como lista de filas de (r,g,b)."""
    tmp = tempfile.mktemp (suffix=".rgb")
    try:
        subprocess.run (["convert", path, "-resize", "%dx%d!" % (lado, lado),
                         "-depth", "8", "RGB:" + tmp], check=True)
        d = open (tmp, "rb").read()
    finally:
        if os.path.exists (tmp): os.remove (tmp)
    return [[tuple (d[(y * lado + x) * 3 : (y * lado + x) * 3 + 3])
             for x in range (lado)] for y in range (lado)]


def v (c):  return (c[0] << 16) | (c[1] << 8) | c[2]

#  QUE CELDAS DIBUJAN LA Z, LEIDAS DE LA APP y no escritas aqui otra vez.
#
#  Estaban duplicadas -el mismo patron a mano en Python- y eso son dos reglas:
#  el dia que la diagonal de `Iconos::marca()` paso a ondular, `marcaCelda`
#  cambio a dos celdas por fila y esta copia se quedo en una, asi que el banco
#  saco cinco FALLA contra un icono que estaba bien. La app imprime `celdas`
#  desde la MISMA funcion con la que dibuja la rejilla.
def celdasDeLaZ (d):
    s = d.get ("celdas", "")
    if len (s) != LADO * LADO: sys.exit ("la app no dijo que celdas dibujan la Z")
    return set ((f, c) for f in range (LADO) for c in range (LADO)
                if s[f * LADO + c] == "1")


# --- se genera --------------------------------------------------------------
#
#  DOS ICONOS Y NO UNO, y esa es la unica forma de que este fichero mida lo que
#  se publica. `ci/icon.png` sale con el estilo DE FABRICA -el que la app elige
#  cuando nadie dice nada, ver StoreArt::kEstiloDeFabrica- y las cinco primeras
#  reglas de aqui abajo dan por hecho una REJILLA de 4x4: muestrean el centro
#  de dieciseis celdas. El dia que el de fabrica dejo de ser la rejilla, esas
#  cinco habrian medido celdas que no existen.
#
#  Asi que la rejilla se genera aparte y sigue midiendose con sus reglas -que
#  es lo correcto: un estilo que no se publica hoy se publica manana- y el que
#  se commitea se juzga con las que le tocan segun lo que la app diga que
#  dibujo. Quien decide es la app, no una copia del numero aqui.
d = genera()
REJ = tempfile.mktemp (suffix="-rejilla.png")
dr = genera (REJ, 0)
if d is None:
    print ("la app no escribio el icono"); sys.exit (1)
if not os.path.exists (DEST):
    print ("no hay %s" % DEST); sys.exit (1)

ESTILOS = ["rejilla", "invertida", "unColor", "conTira", "marcaSola",
           "marcaVentana", "marcaTapa", "marcaBanda", "marcaOnda"]
fab = d.get ("estilo", 0)
print ("icono: %s, %d bytes, estilo de fabrica: %d (%s)"
       % (os.path.relpath (DEST, ROOT), os.path.getsize (DEST), fab,
          ESTILOS[fab] if 0 <= fab < len (ESTILOS) else "?"))
print()

#  --- LAS DOS Z SON LA MISMA -------------------------------------------------
#  Lo unico que el PNG no puede decir. Sin esto son dos escrituras de la misma
#  idea que se separan en cuanto alguien toque una, y el juego entero se
#  deshace sin que falle nada.
mide ("la Z del icono es la de la marca", d.get ("desacuerdo") == 0,
      "%s desacuerdos de 16  celdas %s" % (d.get ("desacuerdo"), d.get ("celdas")))

#  --- LA Z SE LEE A CADA TAMANO ---------------------------------------------
for lado in TAMANOS:
    px = pixeles (REJ, lado)
    paso = lado / float (TOTAL)

    #  El centro de cada celda visible: la primera empieza en `paso`, que es la
    #  fila invisible.
    celdas = {}
    for f in range (LADO):
        for c in range (LADO):
            x = int (paso * (1 + c) + paso * 0.5)
            y = int (paso * (1 + f) + paso * 0.5)
            celdas[(f, c)] = px[min (y, lado - 1)][min (x, lado - 1)]

    #  EL CORTE VA DONDE ESTA EL HUECO MAS GRANDE, no en el punto medio del
    #  rango. La primera version partia por la mitad entre la celda mas clara y
    #  la mas oscura, y eso castiga al ambar por ser brillante: con el ambar
    #  arriba del todo el punto medio sube y el violeta -que es un encendido
    #  perfectamente separable de un apagado- cae del lado equivocado. La
    #  pregunta es si las dieciseis caen en DOS grupos y si son los dos que
    #  tocan, asi que el corte es el salto mas grande. Primero se duda de la
    #  prueba.
    ls = sorted (lum (v (p)) for p in celdas.values())
    salto, corte = -1.0, 0.0
    for i in range (len (ls) - 1):
        if ls[i + 1] - ls[i] > salto:
            salto, corte = ls[i + 1] - ls[i], (ls[i] + ls[i + 1]) / 2.0
    leidas = set (k for k, p in celdas.items() if lum (v (p)) > corte)
    debe   = celdasDeLaZ (dr)

    sobran = sorted (leidas - debe)
    faltan = sorted (debe - leidas)
    mide ("la Z se lee a %d px" % lado, leidas == debe,
          "%d encendidas de %d%s" % (len (leidas), len (debe),
              "" if leidas == debe else "  sobran %s  faltan %s" % (sobran, faltan)))

#  --- CONTRASTE, CON LOS LISTONES DE skins.py --------------------------------
px = pixeles (REJ, 1024)
paso = 1024 / float (TOTAL)
enc, apa = [], []
for f in range (LADO):
    for c in range (LADO):
        x = int (paso * (1 + c) + paso * 0.5); y = int (paso * (1 + f) + paso * 0.5)
        (enc if (f, c) in celdasDeLaZ (dr) else apa).append (v (px[y][x]))

cuerpo = v (px[int (paso * 0.5)][int (paso * 0.5)])

#  EL LISTON ES LA CARA, no un numero prestado.
#
#  Esta comprobacion se equivoco DOS veces antes de acertar, y las dos por lo
#  mismo: pedirle al icono un liston de otra pregunta.
#
#  1. Primero MIN_TEXT (4.50), que es lo que skins.py exige para LEER una
#     palabra sobre su tapa. Saco 3.44. Pero esto no es texto.
#  2. Luego MIN_STATE (3.00), que es «apagado contra encendido» de una TAPA.
#     Con las tapas ya identicas a las de la app saco 2.00 - y resulta que la
#     CARA tampoco llega a 3.00 en LACA, porque el cuerpo de un pad es su zati
#     al 62 % de ALFA sobre una placa oscura. O sea que el liston suspendia a
#     la maquina entera, no al icono.
#
#  La pregunta buena es comparativa, que es como esta casa mide casi todo: el
#  icono no puede leerse PEOR que la cara que abre. Los colores los da la app
#  con sus propios tokens y la cuenta se hace aqui una sola vez.
cargados = [int (h.lstrip ("#"), 16) for h in d.get ("cara_cargados", [])]
vacios   = [int (h.lstrip ("#"), 16) for h in d.get ("cara_vacios", [])]
cara = min (ratio (a, b) for a in cargados for b in vacios) if cargados and vacios else 0.0

peor = min (ratio (e, a) for e in enc for a in apa)
mide ("un encendido contra un apagado", peor >= cara,
      "el icono %.2f  la cara %.2f" % (peor, cara))

#  La rejilla tiene que LEERSE como rejilla: si los apagados se funden con el
#  cuerpo, esto no son dieciseis pads con diez encendidos, son diez cuadrados
#  flotando - y entonces no hay juego con el icono que habia.
peorApa = min (dE (a, cuerpo) for a in apa)
mide ("un apagado contra el cuerpo", peorApa >= MIN_WELL,
      "dE %.1f  (liston %.1f)" % (peorApa, MIN_WELL))

#  --- Y NO PUEDE IR MAS SATURADO QUE LA CARA -----------------------------
#
#  La misma pregunta comparativa que la de aqui arriba, en la otra dimension.
#  Un icono no compite en contraste solamente: los ocho zatis A PLENO miden
#  C* 59.4 de croma y esos mismos ocho, en un pad cargado de la cara, 39.3 -
#  porque el cuerpo de un pad es su zati al 62 % (PadArt::cuerpoDe). Pintar el
#  icono con el color crudo lo saca x1.51 mas saturado que la maquina que abre,
#  que es exactamente lo que hacia la onda antes de medirlo.
#
#  Y NO ES UNA REGLA NUEVA, es una que ya existia y que el icono se salto: en
#  la app el zati a pleno vive en la BANDA de 5 px de un pad y en el pad que
#  suena, y donde los ocho salen juntos solo lo activo va a pleno -la tira de
#  la cabecera pinta el resto a 0.45 y el muestrario del pad a 0.38-.
#
#  Los colores de la cara los da la app con sus propios tokens, igual que en la
#  regla de contraste: aqui no se inventa ninguno.
cromaIcono = sum (croma (e) for e in enc) / len (enc)
cromaCara  = sum (croma (c) for c in cargados) / len (cargados) if cargados else 0.0
mide ("no va mas saturado que la cara", cromaIcono <= cromaCara + 0.5,
      "el icono C* %.1f  la cara C* %.1f" % (cromaIcono, cromaCara))

#  --- LA MASCARA DEL LANZADOR ------------------------------------------------
#
#  JUCE escribe el icono como legacy, asi que de Android 8 en adelante el
#  lanzador lo mete en SU mascara. Lo que importa es lo mas lejos del centro
#  que hay algo dibujado, contra la circunferencia inscrita.
#
#  Y SE MIDE EN EL PNG, no se deduce del reparto. La primera version calculaba
#  la esquina desde la misma constante con la que la app la dibuja, asi que al
#  quitar la fila invisible a proposito -que es justo el fallo que esta regla
#  existe para cazar- siguio saliendo verde: no estaba midiendo, estaba
#  repitiendo el numero. Un banco que repite la constante del codigo no prueba
#  el codigo.
#  El cuerpo lleva DEGRADADO, asi que el fondo no es un color: es un color por
#  fila. Comparar contra la esquina de arriba daba 723 px - o sea que el propio
#  degradado contaba como «algo dibujado» y la regla suspendia siempre. La
#  referencia de cada fila es su propio borde izquierdo, que ahi solo hay
#  chasis: la placa empieza mucho mas adentro.
#  Y SOBRE EL QUE SE COMMITEA, no sobre la rejilla: es el PNG que Projucer
#  mete en la APK y el unico que un lanzador va a recortar.
def mascaraLanzador (path, etiqueta):
    q = pixeles (path, 1024)
    media = 1024 / 2.0
    lejos = 0.0
    for y in range (0, 1024, 2):
        fila = v (q[y][2])
        for x in range (0, 1024, 2):
            if dE (v (q[y][x]), fila) > 6.0:
                dd = math.hypot (x + 0.5 - media, y + 0.5 - media)
                if dd > lejos: lejos = dd
    mide (etiqueta, lejos <= media,
          "lo mas lejos dibujado a %.0f px del centro, radio %.0f" % (lejos, media))

mascaraLanzador (DEST, "las esquinas sobreviven a la mascara")
mascaraLanzador (REJ,  "la rejilla tambien sobrevive a la mascara")


# ============================================================================
#  LAS MARCAS, que llevaban una tanda sin que nadie las midiera.
#
#  Las cinco reglas de arriba dan por hecho una REJILLA de 4x4 -muestrean el
#  centro de dieciseis celdas- asi que los estilos que dibujan la marca a lo
#  grande (`marcaSola`, `marcaVentana`, `marcaTapa`, `marcaBanda`, `marcaOnda`)
#  pasaban sin que ninguna les aplicara. Que es exactamente lo que este
#  proyecto llama una linea que imprime OK.
#
#  Lo que se les pide es la misma pregunta con otro cuerpo: en la rejilla la Z
#  la dibujan diez celdas encendidas y aqui la dibuja un HUECO, asi que lo que
#  se mide es el hueco contra la tapa. Y el hueco no se deduce de las
#  constantes del dibujo -ese es el fallo que ya cometio la regla de la mascara
#  del lanzador- sino que lo dice quien lo dibuja: la app escribe la mascara de
#  `Iconos::marca()` a la misma resolucion, en tres valores (tapa, hueco,
#  fuera).
MARCAS = [(i, ESTILOS[i]) for i in range (4, 9)]

#  CUANTO DEL HUECO ES ONDA. Ni ~0 -no esta- ni ~100 -es un bloque de color y
#  la Z deja de leerse-. La banda sale de la poblacion medida y no de un numero
#  redondo: un golpe corto (SNARE) deja el hueco al 4.2 %, la onda a pleno alto
#  lo llena al 63.9 % y ahi la letra ya no se lee, y los dos candidatos buenos
#  caen entre 24.8 y 32.1.
ONDA_MIN, ONDA_MAX = 12.0, 55.0

def clases (m, lado):
    """La mascara en tres valores: 2 tapa, 1 hueco, 0 fuera del pad."""
    return [[2 if m[y][x][0] > 200 else (1 if m[y][x][0] > 60 else 0)
             for x in range (lado)] for y in range (lado)]

def puros (cl, lado, k):
    """Los pixeles de clase k con sus cuatro vecinos tambien de clase k.

    El filo entre tapa y hueco esta suavizado, asi que un pixel de borde es una
    MEZCLA de los dos: contarlo daria un contraste de 1.0 en cualquier icono y
    la regla no sabria decir que no."""
    out = []
    for y in range (1, lado - 1):
        for x in range (1, lado - 1):
            if (cl[y][x] == k and cl[y-1][x] == k and cl[y+1][x] == k
                and cl[y][x-1] == k and cl[y][x+1] == k):
                out.append ((y, x))
    return out

def media (ps):
    if not ps: return None
    n = len (ps)
    return ((sum (p >> 16 for p in ps) // n) << 16) | \
           ((sum ((p >> 8) & 255 for p in ps) // n) << 8) | (sum (p & 255 for p in ps) // n)

print()
for est, nombre in MARCAS:
    #  Si es el estilo de fabrica se mide el PNG QUE SE COMMITEA y no una copia
    #  recien hecha: son el mismo dibujo hoy, y el dia que no lo sean -alguien
    #  toca el codigo y no regenera- lo que se publica es el fichero.
    propio = (est == fab)
    ico = DEST if propio else tempfile.mktemp (suffix=".png")
    msk = tempfile.mktemp (suffix="-m.png")
    dm = genera (ico, est, msk)
    if dm is None or not os.path.exists (ico) or not os.path.exists (msk):
        mide ("%s se dibuja" % nombre, False, "la app no escribio el icono")
        continue

    zatis  = [int (h, 16) for h in dm.get ("marca_zatis", [])]
    cuerpo = [int (h, 16) for h in dm.get ("marca_cuerpo", [])]
    esOnda = lambda p: (min (dE (p, z) for z in zatis) < min (dE (p, c) for c in cuerpo)
                        if zatis and cuerpo else False)

    #  LA Z SE LEE A CADA TAMANO, por el hueco contra la tapa.
    #
    #  Con DOS numeros y no uno, que es la leccion que este banco ya tiene
    #  escrita para la tira del medidor: dos superficies de color se separan
    #  por CROMA tanto como por luminancia, y un ratio WCAG no ve el croma. La
    #  primera version pedia solo el ratio y suspendia a `marcaVentana` con
    #  1.07 - la Z ahi la dibujan los ocho zatis contra el ambar, o sea una
    #  letra que se separa por color y no por luz, que es exactamente el caso
    #  que un ratio no sabe leer. Los dos listones son los que este proyecto ya
    #  usa: el comparativo de la cara para la luz y MIN_WELL para el color.
    peorR, peorE, detalle = 1e9, 1e9, ""
    for lado in TAMANOS:
        a = pixeles (ico, lado); m = pixeles (msk, lado)
        cl = clases (m, lado)
        hueco = [v (a[y][x]) for y, x in puros (cl, lado, 1)]
        tapa  = [v (a[y][x]) for y, x in puros (cl, lado, 2)]
        if not hueco or not tapa:
            peorR = peorE = 0.0; detalle = "sin hueco medible a %d px" % lado; break
        #  El FONDO del hueco, que es lo que dibuja la letra: la onda es
        #  contenido dentro de la ventana, no la ventana.
        ct = media (tapa)
        fondo = media ([p for p in hueco if not esOnda (p)]) or media (hueco)
        r, e = ratio (ct, fondo), dE (ct, fondo)
        if max (r / cara, e / MIN_WELL) < max (peorR / cara, peorE / MIN_WELL):
            peorR, peorE, detalle = r, e, "el peor a %d px" % lado
    mide ("%s: la Z se lee" % nombre, peorR >= cara or peorE >= MIN_WELL,
          "ratio %.2f (cara %.2f)  dE %.1f (liston %.1f)  (%s)"
          % (peorR, cara, peorE, MIN_WELL, detalle))

    #  Y LA ONDA, con TRES cifras: cuanta hay, si se ve y como de saturada.
    if est == 8:
        a = pixeles (ico, 1024); m = pixeles (msk, 1024)
        cl = clases (m, 1024)
        hueco = [v (a[y][x]) for y, x in puros (cl, 1024, 1)]
        onda  = [p for p in hueco if esOnda (p)]
        frac  = 100.0 * len (onda) / max (1, len (hueco))
        mide ("%s: cuanto del hueco es onda" % nombre,
              ONDA_MIN <= frac <= ONDA_MAX,
              "%.1f %%  (banda %.0f-%.0f)" % (frac, ONDA_MIN, ONDA_MAX))

        #  Solo la anterior la cumple tambien una onda del color del fondo:
        #  estaria ahi y no se veria. Con el liston del hueco de una celda
        #  contra su tarjeta, que es la misma pregunta -dos superficies de
        #  color que tienen que separarse- y el liston de una prueba no se
        #  reinventa en la de al lado.
        co = media (onda); cf = media ([p for p in hueco if not esOnda (p)])
        sep = dE (co, cf) if (co is not None and cf is not None) else 0.0
        mide ("%s: la onda contra el fondo del hueco" % nombre, sep >= MIN_WELL,
              "dE %.1f  (liston %.1f)" % (sep, MIN_WELL))

        #  Y NO MAS SATURADA QUE LA CARA, el mismo liston comparativo que la
        #  rejilla y en la misma dimension: la onda es un CAMPO de color, o sea
        #  lo que en un pad es el cuerpo, y el cuerpo de un pad es su zati al
        #  62 %. Pintada con el color crudo salia a C* 59.4 contra los 39.3 de
        #  la cara - x1.51 mas saturada que la maquina que abre.
        #
        #  Se mide sobre los pixeles de onda y no sobre el icono entero: el
        #  acento de la tapa y el chasis son de la CARCASA y los juzga
        #  skins.py, que tiene su propia regla de croma para el acento.
        cOnda = sum (croma (p) for p in onda) / max (1, len (onda))
        mide ("%s: la onda no va mas saturada que la cara" % nombre,
              cOnda <= cromaCara + 0.5,
              "C* %.1f  la cara C* %.1f" % (cOnda, cromaCara))

    for f in ([msk] if propio else [ico, msk]):
        if os.path.exists (f): os.remove (f)

# ============================================================================
#  Y LA DIAGONAL NO ES RECTA, que es lo unico que dice si la onda de la Z esta
#  en la pantalla o solo en el codigo.
#
#  Se mide contra una REFERENCIA que genera la app con la diagonal recta
#  (`ZATI_Z_RECTA=1`, ver Iconos::rectaParaElBanco) y no deduciendo la forma de
#  las constantes del dibujo: ese es el fallo que esta misma prueba ya cometio
#  dos veces -la mascara del lanzador y el hueco de la Z- y que se resume en que
#  un banco que repite la constante del codigo no prueba el codigo.
#
#  Y a los tamanos a los que la marca se dibuja de VERDAD, que no son solo los
#  del lanzador: la cabecera de la app la pinta a unos 40 px y la fila de
#  modulos a 26. Una onda que solo existe a 1024 no la ve nadie.
print()
recto = tempfile.mktemp (suffix="-recto.png")
mrec  = tempfile.mktemp (suffix="-recto-m.png")
mond  = tempfile.mktemp (suffix="-onda-m.png")
genera (recto, 4, mrec, recta=True)
onda  = tempfile.mktemp (suffix="-onda.png")
genera (onda, 4, mond)

#  A LOS CUATRO TAMANOS DEL LANZADOR y no a los de la app.
#
#  Medida antes en los tres tamanos a los que la marca se dibuja: 3.4 % a 26 px
#  -la fila de modulos- y 6.6 % a 40 -la cabecera-. O sea que a esos dos la onda
#  practicamente no esta, y eso NO es un fallo que arreglar subiendo la
#  amplitud: a 2.2 el trazo ya se lee desigual a 26 px, que es peor. Es la
#  escalera de siempre - la onda es una funcion del ICONO, que es donde la marca
#  se dibuja grande, y en la cabecera queda como un matiz.
#
#  Asi que la regla pregunta donde tiene que cumplirse: los cuatro tamanos que
#  JUCE escribe. El liston sale de la poblacion -10.9 % es el peor de los
#  cuatro- y se pone por debajo, no por encima.
MIN_ONDA_Z = 8.0
for lado in TAMANOS:
    a = clases (pixeles (mond, lado), lado)
    b = clases (pixeles (mrec, lado), lado)
    hueco = sum (1 for y in range (lado) for x in range (lado)
                 if a[y][x] == 1 or b[y][x] == 1)
    dif = sum (1 for y in range (lado) for x in range (lado)
               if (a[y][x] == 1) != (b[y][x] == 1))
    pc = 100.0 * dif / max (1, hueco)
    mide ("la diagonal no es recta a %d px" % lado, pc >= MIN_ONDA_Z,
          "%.1f %% del hueco cambia  (liston %.1f)" % (pc, MIN_ONDA_Z))

for f in (recto, mrec, mond, onda):
    if os.path.exists (f): os.remove (f)

if os.path.exists (REJ): os.remove (REJ)

print()
print ("icono: %d comprobaciones, %d FALLA" % (len (hechas), len (fallos)))
sys.exit (1 if fallos else 0)

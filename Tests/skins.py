#!/usr/bin/env python3
"""ZATI — el banco de las carcasas.

Las cuatro reglas de expo.py miden GEOMETRIA y la quinta mide TRADUCCION.
Ninguna mira si dos superficies se distinguen la una de la otra, y por eso
sobrevivio esto: en LACA, una tapa apagada tenia 1.10 a 1 de contraste contra
la tarjeta sobre la que se apoya - por debajo de lo que el ojo separa como dos
superficies - y una ficha entera se leia como un plano liso con manchas ambar
encima. La captura de pantalla lo enseñaba y nadie lo veia, porque en una
captura eso parece un estilo.

Peor todavia: el bloque de profundidad bajo cada tapa y su borde se dibujaban
con ZatiColours::ink, que es la TINTA de la carcasa - y en un chasis oscuro la
tinta es CLARA. En GRAFITO y en LACA la "sombra" era un halo crema al 42% con
3.42 de contraste contra la tarjeta, o sea, lo que mas destacaba de un boton
apagado era su sombra. Un boton encendido no podia competir con eso.

Se mide sobre la TABLA, no sobre una captura: los colores son la fuente, y un
umbral que se comprueba en cuatro carcasas x cinco pares es una linea de
salida en vez de una tarde mirando pantallazos.
"""
import re, sys, os, math

HERE = os.path.dirname (os.path.abspath (__file__))
LNF  = os.path.join (HERE, "..", "Source", "ZatiLookAndFeel.h")

FIELDS = ['top','mid','bot','panel','panelHi','panelLo','key','keyLit','screw',
          'plate','plateEdge','ink','inkDim','inkLight','white','padTop','padBg2',
          'padBorder','lcd','lcdFg','lcdDim','accent','accentBright','accentDim']
SKINS = ['PAPEL', 'GRAFITO', 'ACERO', 'LACA']

#  El alfa con el que ZatiLookAndFeel dibuja el bloque de profundidad y la
#  ranura de una tapa apagada. Si cambia alli, cambia aqui: la prueba mide lo
#  que la app dibuja, no lo que a la prueba le gustaria que dibujase.
SHADOW_ALPHA = 0.42

#  Umbrales. No son WCAG - una tapa no es texto - sino lo que hizo falta para
#  que las cuatro carcasas se leyeran igual de bien, con LACA como el caso que
#  los fijo.
MIN_STATE   = 3.00   # apagado contra encendido
MIN_STEP    = 1.30   # el escalon entre la cara de la tapa y su propia sombra
MIN_TEXT    = 4.50   # AA: la tinta sobre la tapa que la lleva
#  EN dE, NO EN RATIO DE LUMINANCIA.
#
#  El primer intento midio "paso puesto contra celda vacia" con el mismo ratio
#  WCAG que todo lo demas y dio 1.03 en PAPEL: suspenso. Era la PRUEBA la que
#  estaba mal. Un fragmento amarillo sobre un hueco crema tiene casi la misma
#  luminancia y el ojo los separa sin esfuerzo, porque lo que los separa es el
#  croma - y un ratio de luminancia no mide croma. Un contraste de TEXTO si se
#  mide asi (leer es detectar bordes de luminancia); dos superficies de color,
#  no. dE mira las tres dimensiones.
MIN_CELL    = 25.0   # dE: un paso puesto contra el hueco de una celda vacia
MIN_WELL    = 6.0    # dE: el hueco contra la tarjeta en la que esta
#  Texto de verdad, no tapas: aqui si manda WCAG. 4.5 para lo que hay que leer
#  seguido - la pantalla - y 3.0 para los rotulos de seccion, que van en
#  mayusculas grandes y espaciadas y entran en la excepcion de texto grande.
MIN_LCD     = 4.50   # la tinta de la pantalla sobre el cristal
MIN_SECTION = 3.00   # el rotulo de una seccion sobre su tarjeta
#  El anillo del mando contra el chasis. El mando es HUECO a proposito - la
#  cara deja ver el chasis - asi que lo unico que lo dibuja es su borde: si ese
#  borde no se separa del chasis, no hay mando, hay un numero flotando.
MIN_KNOB    = 3.00

#  EL CROMA DEL ACENTO, contra el zati mas cromatico de los ocho.
#
#  La regla escrita de esta casa es que *el color pertenece al sistema de
#  zatis* y que el chasis es acromatico salvo LACA, que es la excepcion
#  declarada. Nadie habia medido si esa excepcion se pasaba: el ambar de LACA
#  daba C* 69.7 contra el rojo, que es el zati mas cromatico de los ocho, con
#  67.5 - o sea que el acento de una carcasa era la nota de color mas fuerte
#  del producto, por encima de la paleta a la que el color pertenece.
#
#  Es un TOPE y no un suelo, asi que se escribe como razon para que entre en
#  la misma tabla que todo lo demas: el zati mas cromatico dividido por el
#  acento, que tiene que ser 1.00 o mas. Las otras tres carcasas tienen el
#  acento acromatico y pasan de sobra.
MIN_CHROMA  = 1.00

#  Los alfas con los que la app dibuja cada una de esas tres cosas.
SECTION_ALPHA = 0.55   # paintPadSheetContent
KNOB_ALPHA    = 0.85   # drawRotarySlider
#  ...y el del PANEL que agrupa varios controles. Ver MainComponent::pintaPaneles.
PANEL_ALPHA   = 0.16
#  ...y el de su BORDE, que se pinta ENCIMA del relleno y en la misma direccion.
#  Un panel relleno y nada mas se lee como una mancha; con un filo se lee como
#  una placa. Si esto se queda corto, el borde existe en la tabla y no en la
#  pantalla - que es la clase de fallo que no falla, se publica.
BORDE_ALPHA   = 0.12

#  Los ocho fragmentos, LEIDOS DE Zati.h. No dependen de la carcasa - el color
#  es del sistema de zatis y de nada mas - asi que un paso puesto lleva siempre
#  uno de estos ocho y lo que cambia con la carcasa es el hueco de detras.
#
#  Se leen del fichero y no se copian aqui porque la primera version los copio
#  de memoria y los ocho estaban mal - parecidos, pero mal. Una prueba que mide
#  colores que la app no dibuja no esta midiendo la app.
def zati_colours():
    src = open (os.path.join (HERE, "..", "Source", "Zati.h"), encoding="utf8").read()
    blk = src[src.index ("juce::Colour (0x"):]
    vals = [int (x, 16) & 0xffffff for x in re.findall (r'juce::Colour \(0x([0-9a-fA-F]{8})\)', blk)][:8]
    if len (vals) != 8: sys.exit ("Zati.h no tiene ocho fragmentos")
    return vals

#  LA TIRA DEL MEDIDOR. Verde, amarillo y rojo son las tres unicas notas de
#  color del chasis y NO se mueven con la carcasa - son semanticas - pero el
#  cristal sobre el que se pintan SI, asi que hay que medirlas contra cada uno.
#
#  Dos cosas distintas, y hacen falta las dos: que un segmento encendido se
#  separe del cristal (ratio, es una barra de 5 px y se lee por su borde de
#  luz) y que cada zona se separe de la siguiente (dE, porque verde, amarillo
#  y rojo se distinguen por CROMA y un ratio WCAG no ve el croma - tres tonos
#  con la misma luminancia darian tres ratios estupendos y una tira que parece
#  de un solo color).
MIN_METER = 3.00    # un segmento encendido contra el cristal
MIN_ZONE  = 20.0    # dE de una zona contra la de al lado


def signal_colours():
    """red / yellow / green, LEIDOS de ZatiLookAndFeel.h. Van fuera de la
    tabla de carcasas porque no dependen de la carcasa."""
    src = open (LNF, encoding="utf8").read()
    src = re.sub (r'//[^\n]*', '', src)          # mismo motivo que en parse()
    out = {}
    for n in ("red", "yellow", "green"):
        m = re.search (r'juce::Colour\s+' + n + r'\s*\{\s*0x([0-9a-fA-F]{8})\s*\}', src)
        if m is None: sys.exit ("no encuentro el color %s" % n)
        out[n] = int (m.group (1), 16) & 0xffffff
    return out


#  Los alfas con los que StepGrid dibuja el hueco de una celda vacia. Mismo
#  contrato que SHADOW_ALPHA: si cambian alli, cambian aqui.
CELL_EMPTY   = 0.28
CELL_BEAT    = 0.48


def parse():
    src = open (LNF, encoding="utf8").read()
    blk = src[src.index ("static const Skin table[4]"):]
    #  SIN LOS COMENTARIOS. Un comentario de esta tabla cita colores - es el
    #  formato de la casa, cada decision explica contra que numero se tomo - y
    #  el primer intento se trago un 0xff235055 citado dentro de una frase y
    #  desplazo la tabla entera un campo: LACA salia con la tinta donde va la
    #  placa y el banco dio por malos dos pares que estaban bien. Una prueba
    #  que no se ha corregido antes de creerla no es una medida.
    blk = re.sub (r'//[^\n]*', '', blk)
    nums = [int (x, 16) for x in re.findall (r'0x([0-9a-fA-F]{8})', blk)][:4 * len (FIELDS)]
    if len (nums) < 4 * len (FIELDS):
        sys.exit ("la tabla de carcasas no tiene 4 x %d colores" % len (FIELDS))
    return [dict (zip (FIELDS, nums[i * len (FIELDS):(i + 1) * len (FIELDS)])) for i in range (4)]


def rgb (v):  return ((v >> 16) & 255, (v >> 8) & 255, v & 255)
def _lin (c):
    c /= 255.0
    return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
def lum (v):
    r, g, b = rgb (v)
    return 0.2126 * _lin (r) + 0.7152 * _lin (g) + 0.0722 * _lin (b)
def ratio (a, b):
    la, lb = lum (a), lum (b)
    hi, lo = max (la, lb), min (la, lb)
    return (hi + 0.05) / (lo + 0.05)
def lab (v):
    """CIE L*a*b*, D65. Hace falta porque dos superficies de color se separan
    por croma tanto como por luminancia, y el ratio WCAG solo ve lo segundo."""
    r, g, b = [_lin (c) for c in rgb (v)]
    X = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047
    Y = (0.2126 * r + 0.7152 * g + 0.0722 * b)
    Z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883
    f = lambda t: t ** (1 / 3) if t > 0.008856 else 7.787 * t + 16 / 116
    fx, fy, fz = f (X), f (Y), f (Z)
    return (116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz))
def dE (a, b):
    la, lb = lab (a), lab (b)
    return math.sqrt (sum ((x - y) ** 2 for x, y in zip (la, lb)))
def croma (v):
    """C* de Lab: cuanto color tiene, aparte de cuanta luz. Es la dimension que
    ni `lum` ni `ratio` ven, y la unica en la que se puede preguntar si algo
    esta demasiado saturado."""
    _, a, b = lab (v)
    return math.hypot (a, b)
def over (fg, bg, alpha):
    """fg sobre bg con ese alfa — lo que el pincel deja realmente en pantalla."""
    f, b = rgb (fg), rgb (bg)
    return sum (int (round (f[i] * alpha + b[i] * (1 - alpha))) << (16 - 8 * i) for i in range (3))
def brillo (v):
    """juce::Colour::getPerceivedBrightness, escrito aqui desde la definicion.
    Es la funcion con la que la app decide hacia que lado se hunde una cosa
    (ZatiColours::recess y groupOn), y usar `lum` en su lugar seria medir una
    regla distinta de la que se dibuja: en LACA las dos coinciden por poco
    -0.26 contra 0.057, las dos por debajo de 0.5- y esa clase de coincidencia
    es la que hace que un cambio de tabla pase la prueba y falle en pantalla."""
    r, g, b = [c / 255.0 for c in rgb (v)]
    return math.sqrt (0.241 * r * r + 0.691 * g * g + 0.068 * b * b)
def ink_on (d, surface):
    """La misma eleccion que ZatiColours::textOn: se MIDE, no se supone."""
    return d['ink'] if ratio (d['ink'], surface) >= ratio (d['inkLight'], surface) else d['inkLight']


def main():
    skins = parse()
    ZATI = zati_colours()
    SIG  = signal_colours()
    bad = []
    print (f"{'carcasa':9} {'apag/enc':>9} {'escalon':>8} {'tinta/tapa':>11} "
           f"{'tinta/acento':>13} {'paso/hueco':>10} {'hueco/tarj':>11} "
           f"{'panel/tarj':>11} {'borde/pan':>10} "
           f"{'pantalla':>9} {'seccion':>8} {'mando':>7} {'tira':>6} {'zonas':>7} "
           f"{'zati/acento':>12}")
    for name, d in zip (SKINS, skins):
        #  La sombra cae sobre la superficie que hay detras de la tapa, que es
        #  la tarjeta o el chasis: los dos son chassisTop.
        #  La misma regla que ZatiColours::recess: el hueco se separa hacia
        #  donde queda sitio. El cuerpo de GRAFITO ya esta casi en negro y no le
        #  queda recorrido hacia abajo - un hueco negro sobre el separaba 4 de
        #  dE, que es "no se ve".
        into   = 0xffffff if lum (d['top']) < 0.03 else 0x000000
        #  EL PANEL DE UN GRUPO NO ES UNA SOMBRA y no escoge su lado igual.
        #  Ver ZatiColours::groupOn: una sombra va debajo de un objeto y tiene
        #  que ser mas oscura, y un panel va detras de varios y solo tiene que
        #  separarse - asi que se va hacia el lado que tenga MAS SITIO, con el
        #  corte en la mitad.
        haciaDonde = 0xffffff if brillo (d['top']) < 0.5 else 0x000000
        panel  = over (haciaDonde, d['top'], PANEL_ALPHA)
        #  El borde compone sobre el RELLENO, no sobre la tarjeta: se dibuja
        #  despues y encima. Medirlo contra la tarjeta diria que se ve
        #  estupendamente cuando lo que hay que separar es del relleno.
        bordeP = over (haciaDonde, panel, BORDE_ALPHA)
        shadow = over (into, d['top'], SHADOW_ALPHA)
        empty  = over (into, d['top'], CELL_EMPTY)
        beat   = over (into, d['top'], CELL_BEAT)
        m = {
            "apagado contra encendido": (ratio (d['key'], d['accent']), MIN_STATE),
            #  Lo que dibuja el objeto no es la sombra contra la tarjeta - en
            #  GRAFITO el cuerpo ya es casi negro y una sombra negra encima no
            #  se ve - sino el ESCALON entre la cara de la tapa y su sombra.
            #  Ese escalon es el que existe en las cuatro carcasas, y es el que
            #  hace que una tapa se lea como un objeto y no como una mancha.
            "escalon cara/sombra":      (ratio (d['key'],  shadow),        MIN_STEP),
            "tinta sobre la tapa":      (ratio (ink_on (d, d['key']),    d['key']),    MIN_TEXT),
            "tinta sobre el acento":    (ratio (ink_on (d, d['accent']), d['accent']), MIN_TEXT),
            #  LA REJILLA DE PASOS. Un paso puesto lleva el color de su
            #  fragmento y uno vacio es un hueco en la tarjeta; lo que hay que
            #  garantizar es que se distingan, y el caso dificil es el fragmento
            #  MENOS separado de ese hueco de los ocho. Con el hueco escrito en
            #  padBorder salia mas CLARO que la tarjeta en las dos carcasas
            #  oscuras - del lado equivocado - y la rejilla se leia como si
            #  todos los pasos estuvieran puestos a medias.
            "paso puesto contra vacio": (min (dE (z, empty) for z in ZATI), MIN_CELL),
            "hueco contra la tarjeta":  (dE (empty, d['top']),   MIN_WELL),
            #  EL PANEL DE UN GRUPO contra la tarjeta en la que esta. Es la
            #  MISMA pregunta que la de arriba -una superficie hundida contra
            #  la que hay detras- asi que lleva el MISMO liston: el de una
            #  prueba no se reinventa en la de al lado.
            #
            #  Con paneles en una ficha sola esto se toleraba sin medirlo; con
            #  cuatro, no. Es el caso de EXPORTAR otra vez - los numeros
            #  existian y no los miraba nadie, asi que una regresion ahi no
            #  fallaba, se publicaba. Roto a proposito (groupOn escogiendo el
            #  lado como recess) sale FALLA en LACA con 5.34.
            "panel contra la tarjeta":  (dE (panel, d['top']),   MIN_WELL),
            #  EL BORDE DEL PANEL contra su propio relleno. Misma pregunta que
            #  las dos de arriba -dos superficies contiguas- asi que el MISMO
            #  liston. A 0.10 de alfa LACA se quedaba en 6.39, pasando raspando;
            #  a 0.12 son 7.69. Roto a proposito con el alfa a 0.04, fallan las
            #  cuatro carcasas.
            "borde contra el panel":    (dE (bordeP, panel),    MIN_WELL),
            #  LA PANTALLA. Es lo unico de la app que se lee seguido - nombre
            #  del fichero, frecuencia, recorte, tempo - y va sobre un cristal
            #  que NO es el chasis, asi que ninguna de las medidas de arriba
            #  la cubre.
            "texto de la pantalla":     (ratio (d['lcdFg'], d['lcd']), MIN_LCD),
            #  EL ROTULO DE UNA SECCION, que se pinta con la tinta al 55%
            #  sobre la tarjeta. Un alfa es una decision de contraste
            #  disfrazada de decision de estilo, y nadie la habia medido.
            "rotulo de seccion":        (ratio (over (d['ink'], d['top'], SECTION_ALPHA), d['top']),
                                         MIN_SECTION),
            #  EL ANILLO DEL MANDO contra el chasis que se ve por dentro.
            "anillo del mando":         (ratio (over (d['ink'], d['panel'], KNOB_ALPHA), d['top']),
                                         MIN_KNOB),
            #  LA TIRA DEL MEDIDOR sobre el cristal. Se mide el PEOR de los
            #  tres: una tira en la que el verde no se ve es una tira que solo
            #  dice cosas cuando ya vas mal.
            "segmento de la tira":      (min (ratio (c, d['lcd']) for c in SIG.values()),
                                         MIN_METER),
            #  ...y que las tres zonas se separen entre si. La peor de las dos
            #  fronteras, que es la que decide si la tira tiene tres colores.
            "zona contra zona":         (min (dE (SIG['green'],  SIG['yellow']),
                                              dE (SIG['yellow'], SIG['red'])),
                                         MIN_ZONE),
            #  EL ACENTO NO PUEDE SER LA NOTA DE COLOR MAS FUERTE DEL
            #  PRODUCTO. Ver MIN_CHROMA. El liston sale de la POBLACION -los
            #  ocho zatis, leidos de Zati.h- y no de un numero redondo.
            "croma del acento":         (max (croma (z) for z in ZATI)
                                         / max (1e-6, croma (d['accent'])),
                                         MIN_CHROMA),
        }
        vals = list (m.values())
        print (f"{name:9} {vals[0][0]:9.2f} {vals[1][0]:8.2f} {vals[2][0]:11.2f} "
               f"{vals[3][0]:13.2f} {vals[4][0]:10.2f} {vals[5][0]:11.2f} "
               f"{vals[6][0]:11.2f} {vals[7][0]:10.2f} "
               f"{vals[8][0]:9.2f} {vals[9][0]:8.2f} {vals[10][0]:7.2f} "
               f"{vals[11][0]:6.2f} {vals[12][0]:7.2f} {vals[13][0]:12.2f}")
        for what, (v, floor) in m.items():
            if v < floor:
                bad.append (f"{name}: {what} {v:.2f} < {floor:.2f}")

    print()
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("las cuatro carcasas pasan")


main()

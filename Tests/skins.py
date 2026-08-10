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
def over (fg, bg, alpha):
    """fg sobre bg con ese alfa — lo que el pincel deja realmente en pantalla."""
    f, b = rgb (fg), rgb (bg)
    return sum (int (round (f[i] * alpha + b[i] * (1 - alpha))) << (16 - 8 * i) for i in range (3))
def ink_on (d, surface):
    """La misma eleccion que ZatiColours::textOn: se MIDE, no se supone."""
    return d['ink'] if ratio (d['ink'], surface) >= ratio (d['inkLight'], surface) else d['inkLight']


def main():
    skins = parse()
    ZATI = zati_colours()
    bad = []
    print (f"{'carcasa':9} {'apag/enc':>9} {'escalon':>8} {'tinta/tapa':>11} "
           f"{'tinta/acento':>13} {'paso/hueco':>10} {'hueco/tarj':>11}")
    for name, d in zip (SKINS, skins):
        #  La sombra cae sobre la superficie que hay detras de la tapa, que es
        #  la tarjeta o el chasis: los dos son chassisTop.
        #  La misma regla que ZatiColours::recess: el hueco se separa hacia
        #  donde queda sitio. El cuerpo de GRAFITO ya esta casi en negro y no le
        #  queda recorrido hacia abajo - un hueco negro sobre el separaba 4 de
        #  dE, que es "no se ve".
        into   = 0xffffff if lum (d['top']) < 0.03 else 0x000000
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
        }
        vals = list (m.values())
        print (f"{name:9} {vals[0][0]:9.2f} {vals[1][0]:8.2f} {vals[2][0]:11.2f} "
               f"{vals[3][0]:13.2f} {vals[4][0]:10.2f} {vals[5][0]:11.2f}")
        for what, (v, floor) in m.items():
            if v < floor:
                bad.append (f"{name}: {what} {v:.2f} < {floor:.2f}")

    print()
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("las cuatro carcasas pasan")


main()

#!/usr/bin/env python3
"""ZATI - el banco de la TABLA de idiomas.

expo.py compara lo que se DIBUJA en dos idiomas, y por eso no ve nada de esto:
una clave que no existe en la tabla sale igual en los cuatro idiomas y parece
"identica a proposito", y una fila duplicada nunca llega a usarse. Esto mira la
tabla y el codigo, no la pantalla.

Encontro tres cosas la primera vez que se ejecuto: la fila MODO estaba dos
veces, T("KIT") habia quedado apuntando a una clave que se renombro, y el texto
que explica MASTER contra PISTAS en la ficha de exportacion no estaba en la
tabla - o sea, en espanol en las cuatro compilaciones.
"""
import re, sys, os, glob, collections

HERE = os.path.dirname (os.path.abspath (__file__))
SRC  = os.path.join (HERE, "..", "Source")

def sin_comentarios (text):
    """Quita los comentarios respetando los literales.

    Hace falta porque esta prueba se caza a si misma: el comentario que explica
    que se recogen los literales de dentro de un `T ("...")` esta escrito ASI,
    con las comillas, en `Lang.cpp` y en `MainComponent_Audit.cpp` — o sea que
    la expresion de abajo lo lee como una clave y sale `FALLA ... '...'`. Una
    clave nombrada en prosa no es una clave usada.

    Se escribe a mano y no con una regex porque `//` vive tambien dentro de una
    cadena (una URL, por ejemplo) y borrar desde ahi partiria el literal."""
    out, i, n = [], 0, len (text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":                      # literal: se copia entero
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append (text[i:j + 1]); i = j + 1
        elif text.startswith ("//", i):
            j = text.find ("\n", i);  i = n if j < 0 else j
        elif text.startswith ("/*", i):
            j = text.find ("*/", i);  i = n if j < 0 else j + 2
        else:
            out.append (c); i += 1
    return "".join (out)

def joined_literals (text):
    """Une los literales que C++ concatena solos: T("a" "b") es la clave "ab".
    Sin esto, el comprobador da falsos positivos en cada texto largo."""
    return re.sub (r'"\s*\n\s*"', "", text)

def literales_en_T (text):
    """Todos los literales que hay DENTRO de una llamada a T().

    La expresion de siempre —`T \\("...`— solo ve el literal pegado al
    parentesis, asi que se perdia todo lo que llega por un ternario:
    `T (cancion ? "MODO CANCION|modo" : "MODO PATRON|modo")`, los tres `SI|chip`
    / `NO|chip` y una docena mas. Quince claves que estaban usadas y esta prueba
    daba por muertas — lo que ademas hacia inutil la cuenta de huerfanas de mas
    abajo, que es para lo que se escribio esto.

    Se equilibran parentesis a mano y respetando los literales, por lo mismo que
    `sin_comentarios`: un `)` dentro de una cadena cerraria la llamada antes de
    tiempo."""
    out, i, n = set(), 0, len (text)
    while True:
        i = text.find ("T (", i)
        if i < 0: break
        j, depth, clave = i + 2, 0, True
        while j < n:
            c = text[j]
            if c == '"':
                k = j + 1
                while k < n and text[k] != '"':
                    k += 2 if text[k] == '\\' else 1
                if clave: out.add (text[j + 1:k])
                j = k + 1; continue
            if   c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0: break
            #  Y SOLO EL PRIMER ARGUMENTO. `T (clave, valor)` sustituye %1 por
            #  el segundo, que a veces es un literal: contarlo como clave saco
            #  «/», «0.0» y «ZATI/Instrumentos» como filas que faltaban. La coma
            #  de un `juce::String (a, b)` de dentro va a profundidad 2 y no
            #  cuenta.
            elif c == ',' and depth == 1: clave = False
            j += 1
        i = j + 1
    return out

def main():
    lang = joined_literals (open (os.path.join (SRC, "Lang.cpp"), encoding="utf8").read())
    blk  = lang[lang.index ("const Row kTable[]"):]
    blk  = blk[:blk.index ("\n    };")]
    rows = re.findall (r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                       r'\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                       r'\s*"((?:[^"\\]|\\.)*)"\s*\}', blk, re.S)
    keys = [r[0] for r in rows]
    bad  = []

    for k, c in collections.Counter (keys).items():
        if c > 1: bad.append ("fila duplicada: %r (la segunda no se usa nunca)" % k)

    for r in rows:
        for i, lg in ((2, "en"), (3, "zh"), (4, "ar")):
            if not r[i].strip():
                bad.append ("fila %r sin %s" % (r[0], lg))

    code = ""
    for f in sorted (glob.glob (os.path.join (SRC, "*.cpp")) + glob.glob (os.path.join (SRC, "*.h"))):
        code += joined_literals (sin_comentarios (open (f, encoding="utf8").read()))
    used = literales_en_T (code)

    #  Y LAS CLAVES QUE LLEGAN POR VARIABLE, que esta prueba no puede ver.
    #
    #  Los nombres de los parametros de un efecto pasan por `T()` desde
    #  `fxDefs[f].param[pi]`, o sea que aqui no hay literal que recoger: la
    #  expresion de arriba busca lo que esta escrito DENTRO de un `T ("...")`.
    #  Asi estuvo «TONE» -CTRL 2 de DRV- sin fila en la tabla desde el dia que
    #  ese efecto existe, diciendo lo mismo en las cuatro compilaciones, y con
    #  veinte nombres de parametro mas por venir eso deja de ser un descuido.
    #
    #  Se leen de `fxDefs` en el fuente y no de una lista escrita aqui: una
    #  tabla copiada en dos sitios son dos tablas.
    fx = joined_literals (open (os.path.join (SRC, "MainComponent.cpp"), encoding="utf8").read())
    i  = fx.index ("const MainComponent::FxDef MainComponent::fxDefs")
    blkfx = fx[i:fx.index ("\n};", i)]
    params = set()
    for trio in re.findall (r'\{\s*"[A-Z]{2,3}"\s*,\s*\{([^}]*)\}', blkfx):
        params.update (x.strip().strip ('"') for x in trio.split (",") if x.strip())
    used |= params

    #  Y LOS QUINCE PASOS DEL TOUR, por lo mismo y con el mismo agujero.
    #
    #  `ZatiTour::titulos` y `ZatiTour::cuerpos` son dos tablas de literales que
    #  se pasan a `T()` POR INDICE, asi que la expresion de arriba no ve ni uno.
    #  Se pago: la tanda de las ranuras reescribio la fila del paso 5 en
    #  `Lang.cpp` y dejo la CLAVE con el texto viejo -«Filtro, paso alto,
    #  saturacion, eco, reduccion y reverberacion»-, o sea que ese paso salia en
    #  espanol en las cuatro compilaciones y la fila nueva no la usaba nadie. Y
    #  las dos mitades pasaban todas las reglas: la clave no estaba duplicada y
    #  la fila no estaba vacia.
    #  SIN COMENTARIOS, que es lo que ya hace el barrido de arriba y por lo
    #  mismo: los parrafos que explican estas tablas citan claves entre comillas
    #  -«la pestana PASO»- y una clave nombrada en prosa no es una clave usada.
    interno = joined_literals (sin_comentarios (
        open (os.path.join (SRC, "MainComponentInterno.h"), encoding="utf8").read()))
    tour = interno
    pasos = set()
    for nombre in ("titulos", "cuerpos"):
        j = tour.index ("* " + nombre + "[MainComponent::kTourPasos]")
        blk = tour[j:tour.index ("};", j)]
        pasos.update (re.findall (r'"((?:[^"\\]|\\.)+)"', blk))
    #  El titulo del primer paso es el NOMBRE de la app, que es el mismo en los
    #  cuatro idiomas por definicion: pedirle una fila seria pedir que la marca
    #  se traduzca. Es la misma clase de excepcion que `UNTRANSLATED_OK`.
    pasos.discard ("ZATI")
    used |= pasos

    #  Y EL MANUAL, que es la TERCERA tabla que llega por indice y la unica que
    #  seguia sin mirar nadie.
    #
    #  `kManual` son diez capitulos con su titulo y sus lineas, y el pintor las
    #  pasa por `T()` una a una: exactamente el mismo agujero que los pasos del
    #  tour, con la misma consecuencia -una linea cuya clave no esta en la tabla
    #  sale en espanol en las cuatro compilaciones- y ademas INVISIBLE para la
    #  regla comparativa de `expo.py`, porque el manual se PINTA y no es un
    #  componente. Se pago: la tanda de las ranuras reescribio la fila del RACK
    #  en `Lang.cpp` y dejo la clave del manual con el texto viejo -«EL PAD: los
    #  seis envios de uno»-, que llevaba desde entonces sin traducirse.
    man = interno
    m = man.index ("kManual[kManualChapterCount]")
    blkman = man[m:man.index ("\n    };", m)]
    manual = set (re.findall (r'"((?:[^"\\]|\\.)+)"', blkman))
    used |= manual

    #  Y LOS GESTOS, que es la CUARTA tabla por indice — y la que mas duele,
    #  porque esa pagina es la unica de la app cuyo trabajo entero es decir la
    #  verdad. `paintGesturesPage` lleva un `Row rows[kNumGestures]` y las pasa
    #  por `T (rows[i].how)`, asi que ninguna de sus dieciseis cadenas la ve el
    #  barrido de arriba. Cambiar el texto de una fila sin cambiar la clave la
    #  deja en espanol en las cuatro compilaciones, que es lo que ya paso dos
    #  veces con el tour y una con el manual.
    pin = joined_literals (sin_comentarios (
        open (os.path.join (SRC, "MainComponent_Paint.cpp"), encoding="utf8").read()))
    gi = pin.index ("const Row rows[kNumGestures]")
    blkg = pin[gi:pin.index ("\n    };", gi)]
    gestos = set (re.findall (r'"((?:[^"\\]|\\.)+)"', blkg))
    used |= gestos

    for k in sorted (used - set (keys)):
        bad.append ("clave usada y NO en la tabla (sale en espanol en los cuatro): %r" % k)

    #  Y LA DIRECCION CONTRARIA, que se imprime y NO se juzga.
    #
    #  `used - keys` caza la fila que falta; `keys - used` seria la fila que
    #  sobra —una que se reescribio dejando la vieja detras, que es como el
    #  paso 5 del tour se quedo sin traducir dos veces—. Medido antes de
    #  escribir la regla: 127 huerfanas y casi todas legitimas, porque media
    #  tabla llega por VARIABLE —`tablaDeMandos`, los nombres de los ocho
    #  zatis, las dieciseis familias de `Sintes::tabla()`, los `Named` de las
    #  casillas— y ahi no hay literal dentro de un `T()` que recoger. Un numero
    #  que no separa el fallo del caso legitimo no puede ser un veredicto: se
    #  publica, como TOUCH y como TARJETA, para que una fila muerta se vea como
    #  un numero que sube. `ZATI_LANG_HUERFANAS=1` las lista.
    huerfanas = sorted (set (keys) - used)
    print ("%d filas, %d claves usadas en el codigo (%d parametros de efecto, "
           "%d pasos del tour, %d lineas de manual, %d gestos), %d filas sin "
           "cliente visible"
           % (len (rows), len (used), len (params), len (pasos), len (manual),
              len (gestos), len (huerfanas)))
    if os.environ.get ("ZATI_LANG_HUERFANAS"):
        for k in huerfanas: print ("  sin cliente visible  %r" % k)
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("la tabla esta completa")

main()

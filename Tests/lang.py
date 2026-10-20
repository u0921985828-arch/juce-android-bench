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
import re, sys, os, glob, collections, unicodedata

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

_VISTOS = collections.Counter()

def _linea (f, lit):
    """La linea del literal en el fichero de verdad: `joined_literals` se come
    los saltos y la cuenta sobre el texto unido sale corrida. Se cuenta la
    aparicion -el tercer « · » de un fichero es el tercero que se encuentra-."""
    trozo = '"' + lit[:12] + ('"' if len (lit) <= 12 else "")
    n = _VISTOS[(f, trozo)]; _VISTOS[(f, trozo)] += 1
    for i, l in enumerate (open (f, encoding="utf8"), 1):
        c = l.split ("//")[0].count (trozo)
        if n < c: return i
        n -= c
    return 0

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

    #  UN CONCEPTO, UNA PALABRA EN CHINO (textos#15 y #16): patron=乐句,
    #  SEC=音序器, banco=音色库, kit=套件, proyecto=工程, pad=音垫,
    #  master=总输出, biblioteca=素材库. Los sinonimos no pasan.
    SINONIMOS_ZH = ("图案", "音型", "样式", "乐句库", "音垫组", "音色组", "项目",
                    "打击垫", "主输出", "主控", "总线", "资料库")
    for r in rows:
        for w in SINONIMOS_ZH:
            if w in r[3]:
                bad.append ("zh %r usa %s, que el glosario no admite" % (r[0][:40], w))
        if re.search (r"(?<![色材])库", r[3]) or (r[0] in ("SEC", "PATRONES") and r[3] == "音序"):
            bad.append ("zh %r: banco/patron fuera del glosario (%s)" % (r[0][:40], r[3][:20]))

    #  EL CASTELLANO TAMBIEN LLEVA TILDES.
    #
    #  La CLAVE se escribe sin tildes a proposito -es un identificador y vive
    #  en el codigo-, pero una celda `es` vacia significa «la clave ya lo dice
    #  en castellano», y eso dejo la app entera diciendo «telefono», «senala»,
    #  «cancion», «ATRAS», «MANTEN» y «ESPANOL»: unas doscientas cincuenta filas
    #  en las que la clave hacia de texto sin que nadie lo decidiera. El ingles,
    #  el chino y el arabe tenian su celda; el castellano se quedaba con la
    #  forma de identificador.
    #
    #  Se mira lo que SALE en castellano -la celda `es` si la hay, y si no la
    #  clave sin su `|contexto`- contra palabras que SIN tilde no existen. Solo
    #  palabras sin otra lectura: «mas», «esta», «aun», «solo», «como»,
    #  «donde», «cuanto», «fabrica» o «ultimo» son correctas sin tilde en otra
    #  frase, y una regla que acierta a medias acaba con una lista de
    #  excepciones que nadie mantiene.
    sin_tilde = """
        cancion patron sesion telefono microfono metronomo senala senalar
        ensena ensenar ensenamelo aqui alli ahi tambien numero pagina menu
        automatizacion pulsacion saturacion modulacion duracion afinacion
        repeticion maquina musica capitulo capitulos compas vacio vacia atras
        manten dinamica nucleos bufer espanol tamano pestana dieciseis guia
        demas reves rapido rapida minimo maximo movil basica acustica lapiz
        organos acordeon sintesis ambar indice razon ningun
    """.split()
    sin_tilde_re = re.compile (r"(?<![\w])(" + "|".join (sin_tilde) + r")(?![\w])", re.I)
    for r in rows:
        es = r[1] if r[1] else r[0].split ("|")[0]
        for m in sin_tilde_re.finditer (es):
            bad.append ("fila %r: «%s» sin tilde en castellano (pon la celda es)"
                        % (r[0], m.group (1)))

    #  Y LOS NOMBRES DE LOS PRESETS, que en castellano salen tal cual (textos#3).
    inc = open (os.path.join (SRC, "FxPresets.inc"), encoding="utf8").read()
    for nom in re.findall (r'\{\s*\{\s*"([^"]+)"', inc):
        for m in re.finditer (r"(?<![\w])(" + "|".join (sin_tilde + ["mas", "avion", "telefono"]) + r")(?![\w])", nom, re.I):
            bad.append ("preset %r: «%s» sin tilde" % (nom, m.group (1)))

    #  EL ARABE: UN CONCEPTO, UNA PALABRA, Y LA PALABRA LATINA NO VA PRIMERO.
    #
    #  La columna ar la escribio nadie que la leyera entera: el pad tenia cuatro
    #  nombres -باد, الوسادات (cojines), اللوح (que ademas es el plato)-, PRESET
    #  y AJUSTES eran los dos «إعداد», REMUESTREAR y REHACER los dos «إعادة», el
    #  fader era un cerrojo y el metronomo un acelerador (auditoria 2026-10,
    #  textos #6, #7, #10 y #11). Ninguna regla lo veia: cada celda es legal por
    #  separado. El glosario vive en un comentario de Lang.cpp -una lista, un
    #  dueño- y esto lo lee: la palabra prohibida no sale en ninguna celda ar, y
    #  la fila cuya clave nombra el concepto lleva la palabra buena. Medido
    #  antes de escribir una sola celda: 143 celdas contra el glosario en 1015 filas.
    #
    #  Y LA DIRECCION. Una celda ar que empieza por palabra latina -«REC
    #  يسجل…», «TAP…», «AUTO…»- toma base LTR, porque en JUCE 8 el nivel del
    #  parrafo lo decide siempre el primer caracter fuerte (SheenBidi, regla
    #  P2) y `ReadingDirection::rightToLeft` solo manda cuando no hay ninguno.
    #  La palabra latina se pinta en el extremo IZQUIERDO, o sea al FINAL de
    #  la lectura: «يسجل ما يسمعه الميكروفون في الباد المختار REC» (textos #4,
    #  medido con la foto del manual en ar a 393x851, ZATI_OPEN=manual). Se
    #  exige que la celda empiece por palabra arabe; las celdas enteramente
    #  latinas (XY, AUTO, mmap) no entran, porque sin arabe dentro no hay nada
    #  que reordenar. Medido antes: 13 celdas.
    def ar_de (lit):
        """La celda ar como texto. La fila de la cuenta atras esta escrita con
        escapes \\xNN y el barrido de arriba la lee con los escapes puestos."""
        def bytes_de (m):
            return bytes (int (h, 16) for h in re.findall (r"\\x([0-9a-fA-F]{2})", m.group (0))).decode ("utf8", "replace")
        return re.sub (r"(\\x[0-9a-fA-F]{2})+", bytes_de, lit)

    letra_ar = u"ء-يٱ-ۓ"
    glos = re.findall (r"^\s*//\s+([a-z][a-z ]*?)\s+=\s+(\S.*?)\s+\|\s+no:[ \t]*(.*?)[ \t]*$",
                       lang[:lang.index ("const Row kTable[]")], re.M)
    if len (glos) < 15:
        bad.append ("Lang.cpp: no encuentro el glosario del arabe (%d entradas)" % len (glos))
    #  La prohibida se busca como palabra: pegada por delante admite el
    #  clitico (والرف, للخلاط) y por detras no puede seguir letra, para que
    #  «اللوح» (pad) no cace «اللوحة» (plato).
    prohibidas = [(c, p, re.compile (p + "(?![" + letra_ar + "])")) for c, _, nos in glos for p in nos.split()]
    #  concepto -> la clave que lo nombra, lo que la celda ar tiene que llevar,
    #  lo que no puede llevar, y la salvedad (una clave con TONO y NOTA a la
    #  vez lleva las dos palabras a proposito).
    conceptos = [
        ("pad",          r"(?i)(?<![a-z])pads?(?![a-z])",                         "باد",           None,    None),
        ("mezclador",    r"(?i)(?<![a-z])(mezclador|mesa)(?![a-z])|^(MIX|MEZCLA Y CANALES)$", "مازج", None, None),
        ("rack",         r"(?<![A-Za-z])RACK(?![A-Za-z])",                        "الراك",         None,    None),
        ("ranura",       r"(?i)(?<![a-z])ranuras?(?![a-z])",                      "خان",           None,    None),
        ("casilla",      r"(?i)(?<![a-z])(casillas?|celdas?)(?![a-z])",           "خلي",           None,    None),
        ("nota",         r"(?i)(?<![a-z])notas?(?![a-z])",                        "نوت",           "نغم",   r"(?i)(?<![a-z])tonos?(?![a-z])"),
        ("carcasa",      r"(?i)(?<![a-z])carcasas?(?![a-z])",                     "هيكل|هياكل",    "غلاف",  None),
        ("envolvente",   r"(?i)(?<![a-z])(envolvente|ENV)(?![a-z])",              "غلاف",          None,    None),
        ("solo",         r"(?<![A-Za-z])(SOLO|Solo|solos)(?![A-Za-z])",           "منفرد",         None,    None),
        ("bloque",       r"(?i)(?<![a-z])bloques?(?![a-z])",                      "كتل",           None,    None),
        ("preset",       r"(?i)(?<![a-z|])presets?(?![a-z])",                      "مسبق",          None,    None),
        ("remuestrear",  r"(?i)remuestr",                                         "ريسامبل",       "إعادة", None),
        ("fader",        r"(?i)(?<![a-z])faders?(?![a-z])",                       "فيدر",          None,    None),
        ("metronomo",    r"(?i)metronomo",                                        "مترونوم",       None,    None),
        ("semitono",     r"(?i)semitonos?|(?<![A-Z])SEMIS(?![A-Z]|\|preset)",              "نغم",           None,    None),
        ("semicorchea",  r"(?i)semicorchea",                                      "نوتة",          None,    None),
        ("cuenta atras", r"(?i)cuenta atras",                                     u"(?<![" + letra_ar + "])(ال)?عد(?![" + letra_ar + "])", None, None),
        ("compas",       r"(?i)(?<![a-z])compas(es)?(?![a-z])",                   "مازور",         None,    None),
    ]
    conceptos = [(c, re.compile (k), re.compile (si), no and re.compile (no), salvo and re.compile (salvo))
                 for c, k, si, no, salvo in conceptos]
    glosario = 0
    for r in rows:
        ar = ar_de (r[4])
        if not ar: continue
        clave = r[0]
        for c, p, rx in prohibidas:
            if rx.search (ar):
                glosario += 1
                bad.append ("fila %r: «%s» en arabe es %s y el glosario lo prohibe" % (clave[:50], p, c))
        for c, k, si, no, salvo in conceptos:
            if not k.search (clave): continue
            if not si.search (ar):
                glosario += 1
                bad.append ("fila %r: nombra %s y la celda ar no dice «%s»" % (clave[:50], c, si.pattern))
            if no and no.search (ar) and not (salvo and salvo.search (clave)):
                glosario += 1
                bad.append ("fila %r: nombra %s y la celda ar dice «%s», que es otra cosa" % (clave[:50], c, no.pattern))
            #  PRESET lleva siempre el adjetivo: un «إعداد» sin «مسبق» detras
            #  es AJUSTES, que es la confusion que esto existe para parar.
            if c == "preset":
                for m in re.finditer (u"إعداد", ar):
                    if not re.match (u"[^\\s]*\\s+(?:ال)?مسبق", ar[m.end():]):
                        glosario += 1
                        bad.append ("fila %r: «إعداد» sin «مسبق» en una fila de PRESET" % clave[:50]); break
    bidi = 0
    for r in rows:
        ar = ar_de (r[4])
        if not re.search ("[" + letra_ar + "]", ar): continue
        for ch in ar:
            b = unicodedata.bidirectional (ch)
            if b in ("R", "AL"): break
            if b == "L":
                bidi += 1
                bad.append ("fila %r: la celda ar empieza por palabra latina y se pinta LTR: %r"
                            % (r[0][:50], ar[:40])); break
    print ("glosario arabe: %d entradas, %d celdas contra el glosario, %d celdas ar que empiezan en latino"
           % (len (glos), glosario, bidi))

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
    #  verdad. La tabla es `kGestos` -la pintan la pagina AYUDA y el capitulo
    #  GESTOS de la guia, la misma y no una copia- y llega por `T (gf.how)`,
    #  asi que ninguna de sus cadenas la ve el barrido de arriba. Cambiar el
    #  texto de una fila sin cambiar la clave la deja en espanol en las cuatro
    #  compilaciones, que es lo que ya paso dos veces con el tour y una con el
    #  manual. Vivia en Paint.cpp como `Row rows[kNumGestures]`; se mudo con la
    #  guia unica y esta lectura se mudo con ella.
    pin = interno
    gi = pin.index ("const GestoFila kGestos[]")
    blkg = pin[gi:pin.index ("\n    };", gi)]
    gestos = set (re.findall (r'"((?:[^"\\]|\\.)+)"', blkg))
    used |= gestos

    #  Y LOS DOCE MANDOS DE UN INSTRUMENTO, que es la QUINTA por indice.
    #
    #  `Sintes::mando (familia, i)` devuelve la clave de una tabla de 16x4 mas
    #  otra de cuatro comunes, y la cara la pasa por `T()`: mismo agujero que
    #  los `param[]` de un efecto, con la misma consecuencia -un mando sin fila
    #  dice lo mismo en las cuatro compilaciones- y ademas cincuenta claves de
    #  una vez, o sea cincuenta oportunidades de que se cuele una.
    inc = joined_literals (sin_comentarios (
        open (os.path.join (SRC, "SintesMandos.inc"), encoding="utf8").read()))
    mandos = set()
    for nombre in ("kMandosDeForma[kFamilias][4]", "kMandosComunes[8]"):
        j = inc.index (nombre)
        blkm = inc[j:inc.index ("};", j)]
        mandos.update (re.findall (r'"((?:[^"\\]|\\.)+)"', blkm))
    used |= mandos

    #  UN NOMBRE, UNA COSA.
    #
    #  Las 384 tapas de sonido y las 24 de familia salen de la misma tabla y
    #  se leen en el mismo menu, y habia VEINTICINCO nombres puestos dos
    #  veces: un CELLO en CUERDAS y otro en CELLOS, un CLARINET en VIENTOS y
    #  otro en CANAS, un SITAR dentro de CUERDA PULS cuando SITAR es ademas
    #  una familia entera. Ninguna prueba lo veia -las dos filas son legales
    #  por separado- y en el telefono son dos cosas distintas que se llaman
    #  igual, que es lo que costo la queja: "ya habia un nombre para dos
    #  cosas".
    #
    #  La excepcion es la tapa que lleva el nombre de SU PROPIA familia -el
    #  SITAR de SITAR-, que no son dos cosas: es la voz canonica de la
    #  familia y asi lo escribe cualquier sampler.
    tab = sin_comentarios (open (os.path.join (SRC, "SintesTabla.inc"), encoding="utf8").read())
    familia, tapas, familias = None, [], []
    for linea in tab.splitlines():
        m = re.match (r'\s*\{\s*"([^"]+)",\s*f[A-Z]', linea)
        if m:
            familia = m.group (1); familias.append (familia); continue
        m = re.match (r'\s*\{\s*"([^"]+)",\s*-?[0-9]', linea)
        if m and familia is not None:
            tapas.append ((familia, m.group (1)))

    vistas = {}
    for f, n in tapas:
        if n in vistas:
            bad.append ("el nombre %r esta en dos sonidos: %s y %s" % (n, vistas[n], f))
        vistas[n] = f
    for f, n in tapas:
        if n in familias and n != f:
            bad.append ("el sonido %s de %s se llama como la familia %s" % (n, f, n))
    print ("%d familias y %d sonidos, un nombre cada uno" % (len (familias), len (tapas)))

    #  NINGUN LITERAL CON LETRAS FUERA DE ASCII ENTRA POR `String (const char*)`.
    #
    #  Ese constructor lee cada byte como un caracter -es CharPointer_ASCII,
    #  ver juce_String.cpp-, asi que el «+» del paso LOS EFECTOS de la guia
    #  salia «Â«+Â»» en pantalla, y en ingles, chino y arabe salia ESO MISMO en
    #  espanol: la clave mal leida ya no es la de la tabla -que si se lee con
    #  fromUTF8- y la busqueda falla. Lo vio una foto, no un banco. Se admite
    #  el literal que va derecho a `fromUTF8 (`, a `CharPointer_UTF8 (` o a
    #  `T (` -que tiene su sobrecarga de `const char*` que lee UTF-8-, y los
    #  pasos de la guia, que llegan a `T` por `guiaTexto (const char*)`.
    #  La tabla de Lang.cpp no entra: se lee con fromUTF8 fila a fila.
    sobrecarga = re.search (r"\bT\s*\(\s*const\s+char\s*\*",
                            open (os.path.join (SRC, "Lang.h"), encoding="utf8").read())
    crudos = 0
    for f in sorted (glob.glob (os.path.join (SRC, "*.cpp")) + glob.glob (os.path.join (SRC, "*.h"))):
        txt = joined_literals (sin_comentarios (open (f, encoding="utf8").read()))
        libres = []
        if f.endswith ("Lang.cpp"):
            a = txt.index ("const Row kTable[]"); libres.append ((a, txt.index ("\n    };", a)))
        #  `Lang::nativeName` devuelve `const char*` y su unico cliente lo
        #  lee con fromUTF8 (la fila de idioma de ASPECTO).
        for m in re.finditer (r"const char\*\s*Lang::nativeName[^{]*\{", txt):
            libres.append ((m.end(), txt.index ("\n}", m.end())))
        for m in (re.finditer (r"cuerpos\s*\[[^\]]*\]\s*=\s*\{", txt) if sobrecarga else ()):
            libres.append ((m.end(), txt.index ("};", m.end())))
        for m in re.finditer (r'"((?:[^"\\\n]|\\.)*)"', txt):
            if all (ord (c) < 128 for c in m.group (1)): continue
            if any (a <= m.start() < b for a, b in libres): continue
            antes   = txt[:m.start()].rstrip()
            despues = txt[m.end():].lstrip()
            if re.search (r"(fromUTF8|CharPointer_UTF8)\s*\($", antes): continue
            if sobrecarga and re.search (r"\bT\s*\($", antes): continue
            #  Pegado A LA DERECHA de un String va por `+=`, que si lee UTF-8:
            #  `s + " · "` y `s << " · "` estan bien. A la IZQUIERDA no:
            #  `" · " + s` construye primero el String con los bytes.
            #  Y la suma va de izquierda a derecha: en `s + " · " + t` el
            #  literal se suma a `s`, ya String.
            if re.search (r"(\+|<<|\+=)$", antes): continue
            #  Una rama de ternario hereda el sitio del parentesis que la abre.
            if antes.endswith (("?", ":")) and not despues.startswith ("+"):
                prof, k = 0, len (antes) - 1
                while k >= 0:
                    if antes[k] == ")": prof += 1
                    elif antes[k] == "(":
                        if prof == 0: break
                        prof -= 1
                    k -= 1
                if re.search (r"(\+|<<)$", antes[:max (k, 0)].rstrip()): continue
            crudos += 1
            bad.append ("%s:%d literal fuera de ASCII leido como bytes (sale con Â): %r"
                        % (os.path.basename (f), _linea (f, m.group (1)), m.group (1)[:50]))
    print ("literales fuera de ASCII sin pasar por UTF-8: %d" % crudos)

    #  Y LAS CLAVES QUE VIAJAN EN UN ARRAY. La tira del paso pinta sus
    #  rotulos con `T (lb.key)` desde `const char* nombres[4] = {...}`, y ahi
    #  no hay `T ("...")` que recoger: «ATAQUE», «CAIDA» e «INICIO» salieron
    #  sin tilde y sin traducir en los cuatro idiomas sin que esta prueba lo
    #  viera (auditoria 2026-10, textos#0). Se recogen los literales de esos
    #  arrays, y ademas todo literal con forma de clave con contexto
    #  (`CORTE|filtro`), que no puede ser otra cosa que una clave.
    for f in glob.glob (os.path.join (SRC, "*.cpp")) + glob.glob (os.path.join (SRC, "*.h")):
        if os.path.basename (f) in ("Lang.cpp", "MainComponent_Audit.cpp"): continue
        t = sin_comentarios (open (f, encoding="utf8").read())
        for arr in re.findall (r'const\s+char\s*\*\s*nombres\s*\[\s*\d*\s*\]\s*=\s*\{([^}]*)\}', t):
            for lit in re.findall (r'"((?:[^"\\]|\\.)*)"', arr):
                if lit: used.add (lit)
        for lit in re.findall (r'"([A-Z][A-Z0-9 ]*\|[a-z]+)"', t):
            used.add (lit)

    #  Y LOS 150 NOMBRES DE PRESET, que la cara pasa por `T (nombre + "|preset")` (textos#3).
    tabla_fx = open (os.path.join (SRC, "FxPresets.inc"), encoding="utf8").read()
    presets_fx = set (n + "|preset" for n in re.findall (r'\{\s*"([^"]+)"\s*,\s*-?[0-9]', tabla_fx))
    used |= presets_fx
    used.discard ("|preset")   # el sufijo que la cara pega, no una clave

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
           "%d pasos del tour, %d lineas de manual, %d gestos, %d mandos de "
           "instrumento), %d filas sin cliente visible"
           % (len (rows), len (used), len (params), len (pasos), len (manual),
              len (gestos), len (mandos), len (huerfanas)))
    if os.environ.get ("ZATI_LANG_HUERFANAS"):
        for k in huerfanas: print ("  sin cliente visible  %r" % k)
    if bad:
        for b in bad: print ("FALLA  " + b)
        sys.exit (1)
    print ("la tabla esta completa")

main()

#!/usr/bin/env python3
"""CUANTOS TOQUES DESDE LA CARA HASTA CADA FICHA.

Es la cifra que define «intuitivo» y no existia. El banco tenia diecinueve
reglas de veredicto y todas de GEOMETRIA -dedo, solapes, ventana, celda, rotulo
cortado-: una ficha al fondo de la app las pasa las diecinueve y aun asi no la
encuentra nadie. Con 46 pantallas eso deja de ser una opinion y pasa a ser un
reparto que se puede mirar.

SE PUBLICA Y NO SE JUZGA, que es como entro el histograma de aire y por la
misma razon: un numero sin poblacion detras no separa el fallo del caso
legitimo. Con el reparto delante se vera si hay cola larga y ENTONCES se pone
liston.

    python3 Tests/profundidad.py


## POR QUE ESTO NO SE MIDE CORRIENDO LA APP, que era el primer plan

La instrumentacion existe -`UiAudit::apertura`, que `openSheet` rellena- y se
probo en las 51 corridas del ciclado. Dio **1 en las catorce**, y no porque la
app sea plana: porque **el banco abre cada ficha en frio**. Cada corrida
arranca la app, pone `ZATI_OPEN=loquesea` y la abre desde la cara, asi que «que
habia abierto antes» es siempre nada. El numero medido era el del arnes y no el
de la app.

Antes de eso se intento con la capa de la TAPA que `openSheet` recibe, y dio
cero en las catorce por otra razon: los treinta y tantos `openSheet` de la app
pasan todos una tapa de la CARA -`setButton`, `mixButton`, `padsButton`,
`secButton`, `songButton`- incluso cuando la ficha se abre desde dentro de
otra. Dos formas de preguntar y las dos contestando una constante: es la misma
figura que el camino «asoma un pad» de `desglose.py`, que valia para las nueve.

Asi que la hondura se saca de DONDE ESTA CABLEADA, que es donde de verdad vive:
la tapa que abre una ficha esta *dentro* de otra ficha o de la cara, y eso lo
dice `addAndMakeVisible`. Sigue **derivandose y no declarandose** -no hay una
lista de fichas en este fichero-: una ficha nueva con su tapa aparece sola.

La instrumentacion de `UiAudit::apertura` se queda puesta y publicada: el dia
que el banco navegue de una ficha a otra -y no en frio- dira la hondura REAL
recorrida, que es la comprobacion que a esta le falta.
"""
import collections, os, re, sys

ROOT   = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
FUENTE = os.path.join (ROOT, "Source", "MainComponent.cpp")

#  Lo que cuenta como «esta ficha se pone delante».
#  El `\*?` no es cosmetico: la fila de modulos llama `openSheet (*s, *b)`
#  dentro de su bucle, y sin el, PAD y SEC -las dos pestañas que mas se tocan-
#  se quedaban sin puerta y sus fichas salian a dos toques en vez de a uno.
ABRE = (re.compile (r"\bopenSheet\s*\(\s*\*?(\w+)\s*,"),
        re.compile (r"\b(\w+)\s*(?:\.|->)\s*setVisible\s*\(\s*true\s*\)"))

#  A partir de aqui una ficha esta LEJOS. No se juzga todavia: se imprime al
#  lado para que el reparto se lea sin contar a mano.
HONDO = 3


def sinComentarios (texto):
    """Fuera los comentarios, conservando los saltos de linea.

    Este fichero explica cada decision con el fallo que la motivo, asi que sus
    comentarios NOMBRAN fichas y tapas continuamente -«sin esto, cerrar el
    secuenciador dejaba flotando su selector»- y leerlos como codigo inventa
    cableado que no existe. Es la misma razon por la que `deshacer.py` los quita
    antes de buscar `pushUndo`.
    """
    def fuera (m):
        s = m.group (0)
        return s if s[0] in "\"'" else re.sub (r"[^\n]", " ", s)
    return re.sub (r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'"
                   r"|//[^\n]*" r"|/\*.*?\*/", fuera, texto, flags=re.S)


def bloqueDe (texto, p):
    """Las llaves mas interiores que envuelven la posicion p.

    Hace falta para que un alias valga solo donde se escribe: ver el comentario
    de `alias`. Se cuenta hacia atras hasta la llave que queda abierta, y desde
    ahi hacia delante hasta la que la cierra.
    """
    n, i = 0, p
    while i > 0:
        i -= 1
        if   texto[i] == "}": n += 1
        elif texto[i] == "{":
            if n == 0: break
            n -= 1
    n, j = 0, i
    while j < len (texto):
        if   texto[j] == "{": n += 1
        elif texto[j] == "}":
            n -= 1
            if n == 0: return i, j
        j += 1
    return i, len (texto)


def bloque (texto, i):
    """El cuerpo que empieza en la primera llave a partir de i."""
    a = texto.find ("{", i)
    if a < 0: return ""
    n, j = 0, a
    while j < len (texto):
        if   texto[j] == "{": n += 1
        elif texto[j] == "}":
            n -= 1
            if n == 0: return texto[a:j + 1]
        j += 1
    return texto[a:]


def fichasQueAbre (cuerpo, porFuncion, resuelve):
    """Que fichas pone delante este trozo de codigo: directas y por ayudante."""
    out = set()
    for pat in ABRE:
        for m in pat.finditer (cuerpo):
            for f in resuelve (m.group (1)):
                if f.lower().endswith ("sheet"):
                    out.add (f)
    for fn, fichas in porFuncion.items():
        if re.search (r"\b" + re.escape (fn) + r"\s*\(", cuerpo):
            out |= fichas
    return out


def main():
    if not os.path.isfile (FUENTE):
        print ("FALLA  no esta %s" % FUENTE)
        return 1
    texto = sinComentarios (open (FUENTE, encoding="utf8").read())

    #  1. QUE FICHA PONE DELANTE CADA FUNCION. Hace falta porque media docena
    #     de fichas no se abren con `openSheet` sino con un ayudante con nombre
    #     -`openInstSheet`, `openBrowseForPad`, `abreMidiSheet`- y una tapa
    #     llama al ayudante, no a `openSheet`.
    porFuncion = {}
    for m in re.finditer (r"^[\w:<>,&*\s]*?\bMainComponent::(\w+)\s*\(", texto, re.M):
        cuerpo = bloque (texto, m.end())
        fichas = set()
        for pat in ABRE:
            for q in pat.finditer (cuerpo):
                if q.group (1).lower().endswith ("sheet"):
                    fichas.add (q.group (1))
        #  `closeAllSheets` las nombra TODAS para esconderlas: es lo contrario
        #  de abrir una, y sin esta linea seria la madre de la app entera.
        if fichas and m.group (1) not in ("closeAllSheets", "apuntaApertura"):
            porFuncion[m.group (1)] = fichas

    #  2. DONDE VIVE CADA TAPA. `xSheet.addAndMakeVisible (t)` la mete en la
    #     ficha x; `xSheet.cuerpo.addAndMakeVisible (t)` tambien -el cuerpo es
    #     la parte desplazable de la misma ficha-; a secas, la cara.
    #     Y LAS QUE SE COLOCAN EN LOTE. La fila de modulos de la cara —PAD,
    #     SEC, MEZCLA, CANCION— no se coloca por su nombre sino por un array
    #     -`juce::TextButton* mb[4] = { &padsButton, ... }` y luego
    #     `addAndMakeVisible (mb[i])`- que es justo como se coloca lo que se
    #     repite. Sin resolver el array, las CUATRO tapas mas usadas de la app
    #     salian «sin camino» y con ellas `padSheet` y `seqSheet`: la regla
    #     habria dicho que las dos pantallas que mas se abren no se abren.
    lote = {}
    for m in re.finditer (r"\*\s*(\w+)\s*\[\s*\d*\s*\]\s*=\s*\{([^}]*)\}", texto):
        lote[m.group (1)] = re.findall (r"&(\w+)", m.group (2))
    #     Y EL ALIAS DEL BUCLE, que es la otra mitad: no se coloca `mb[i]` sino
    #     `auto* b = mb[i];` y luego `addAndMakeVisible (b)`. Sin seguir el
    #     alias el array resuelto no sirve de nada, que es lo que paso en la
    #     primera corrida con el lote ya leido y las cuatro tapas aun sueltas.
    #
    #     CON SU BLOQUE, Y NO PARA TODO EL FICHERO. Puesto global, la letra `b`
    #     -que es el nombre del que tira CUALQUIER bucle de este fichero- se
    #     quedaba valiendo «las cuatro pestañas de la cara» hasta el final:
    #     `browseSheet` salio abierta por `mixButton`, `padsButton`, `secButton`
    #     y `songButton` a la vez, que es un cableado que no existe. Un alias
    #     vale dentro de las llaves donde se escribe y ni una mas.
    alias = []          # (ini, fin, nombre, miembros)
    for m in re.finditer (r"auto\s*\*\s*(\w+)\s*=\s*(\w+)\s*\[", texto):
        if m.group (2) not in lote:
            continue
        a, b = bloqueDe (texto, m.start())
        alias.append ((a, b, m.group (1), lote[m.group (2)]))

    def resuelve (nombre, pos):
        for a, b, n, miembros in alias:
            if n == nombre and a <= pos <= b:
                return miembros
        return lote.get (nombre) or [nombre]

    vive = {}
    for m in re.finditer (r"(?:(\w+)\s*\.\s*(?:cuerpo\s*\.\s*)?)?addAndMakeVisible\s*\(\s*\*?&?(\w+)", texto):
        duenyo, hijo = m.group (1), m.group (2)
        if duenyo and not duenyo.lower().endswith ("sheet"):
            continue                       # un panel dentro de una ficha: no cambia la capa
        for h in resuelve (hijo, m.start()):
            vive.setdefault (h, duenyo or "")

    #  3. QUE FICHA ABRE CADA TAPA: el cuerpo de su `onClick`.
    quienAbre = collections.defaultdict (set)
    for m in re.finditer (r"\b(\w+)\s*(?:\.|->)\s*onClick\s*=", texto):
        tapa = m.group (1)
        cuerpo = bloque (texto, m.end())
        #     Y LA TAPA TAMBIEN SE RESUELVE POR EL LOTE, que es la simetrica de
        #     lo de arriba y hacia falta igual: el bucle escribe `b->onClick` y
        #     coloca `addAndMakeVisible (b)`, asi que `vive` guarda los nombres
        #     de verdad -padsButton, secButton- y `quienAbre` se quedaba con la
        #     letra `b`, que no vive en ningun sitio. `seqSheet` salia a dos
        #     toques siendo una pestaña de la cara.
        for f in fichasQueAbre (cuerpo, porFuncion, lambda n: resuelve (n, m.start())):
            for t in resuelve (tapa, m.start()):
                quienAbre[f].add (t)

    if not quienAbre:
        print ("FALLA  no se encontro ni una tapa que abra una ficha: el"
               " cableado se lee de otra forma y esto no mide nada")
        return 1

    #  4. LA HONDURA. Una ficha esta a los toques de la ficha donde vive su
    #     tapa, mas uno; una tapa de la cara son los de la cara, que es cero.
    def hondura (ficha, vistas=frozenset()):
        if ficha in vistas: return None
        mejor = None
        for tapa in quienAbre.get (ficha, ()):
            donde = vive.get (tapa)
            if donde is None:              # una tapa que nadie coloca no se toca
                continue
            if donde == "":
                h = 1
            else:
                arriba = hondura (donde, vistas | {ficha})
                h = None if arriba is None else arriba + 1
            if h is not None and (mejor is None or h < mejor):
                mejor = h
        return mejor

    hond = {f: hondura (f) for f in quienAbre}
    hist = collections.Counter (h for h in hond.values() if h is not None)
    sueltas = sorted (f for f, h in hond.items() if h is None)

    print ("%d fichas con tapa que las abre, cableadas en %s"
           % (len (quienAbre), os.path.relpath (FUENTE, ROOT)))
    print()
    print ("== TOQUES DESDE LA CARA ==")
    for h in sorted (hist):
        print ("  %d toque%s  x%-3d  %s"
               % (h, " " if h == 1 else "s", hist[h],
                  ", ".join (sorted (f for f, v in hond.items() if v == h))))
    hondas = sorted (f for f, v in hond.items() if v is not None and v > HONDO)
    print()
    print ("a mas de %d toques: %s" % (HONDO, ", ".join (hondas) or "ninguna"))
    #  Las que ninguna tapa colocada abre. Hoy son DOS y las dos por una razon
    #  medida, no por un hueco de la lectura:
    #
    #   - `browseSheet` no la abre ningun `onClick`: la abre un GESTO -tocar un
    #     pad con LOAD armado, `padClicked`- y esta prueba lee tapas. Un gesto
    #     no tiene tapa que colocar, asi que no tiene capa.
    #   - `instSheet` cuelga de `browseFactoryButton`, que vive DENTRO de
    #     `browseSheet`: sin hondura para la de arriba no hay hondura para la
    #     de abajo.
    #
    #  Se imprimen enteras y no se juzgan: saber CUALES es la mitad del trabajo
    #  de la tanda que ponga el liston, y las dos apuntan al mismo sitio —los
    #  gestos, que es lo que `plano.py` tampoco cubre entero—.
    if sueltas:
        print ("sin camino a la cara: %s" % ", ".join (sueltas))
    print()
    print ("IMPRESO, NO JUZGADO: un liston sobre un reparto que se ve por"
           " primera vez seria un numero elegido a ojo.")
    return 0


if __name__ == "__main__":
    sys.exit (main())

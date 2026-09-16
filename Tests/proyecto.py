#!/usr/bin/env python3
"""LA RED DEL PROYECTO: LO QUE SE LLEVA TU TRABAJO POR DELANTE AVISA ANTES.

`Tests/deshacer.py` cubre lo que se lleva un PAD. Esto cubre lo que se lleva el
proyecto ENTERO, que es la version grande del mismo fallo y no la miraba nadie:

  - `newProject` vacia los sesenta y cuatro pads y los ocho patrones, y
    preguntaba «BORRA TODO?» desde siempre.
  - `loadProject` hace lo mismo con otro nombre —lo que tenias delante se va— y
    **no preguntaba nada**: un doble toque en una fila de la lista y adios.

La asimetria castiga justo a quien no sabe lo que va a pasar, que es quien hace
doble toque en una lista para ver que hay. Lo salva la sesion invisible, pero la
persona no lo sabe, asi que para ella el trabajo se perdio.

DESHACER NO SIRVE AQUI, y por eso el liston es `armConfirm` y no `pushUndo`: la
pila de deshacer es del proyecto abierto, asi que abrir otro no es una accion
que se pueda deshacer — es cambiar de mundo. Lo unico que protege es preguntar
antes.

SE DERIVA Y NO SE DECLARA, igual que `deshacer.py`: se buscan las llamadas a las
dos funciones que sustituyen el proyecto, se mira en que funcion cae cada una, y
se pide que esa funcion pregunte. Una puerta nueva al proyecto aparece sola.

    python3 Tests/proyecto.py
"""
import os, re, sys

ROOT   = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
FUENTE = os.path.join (ROOT, "Source", "MainComponent.cpp")

#  Las dos que sustituyen el proyecto entero.
EMBUDOS  = ("loadProject", "newProject")
#  Y lo que cuenta como preguntar. `armConfirm` es la forma de esta casa: pone
#  la tapa roja y le cambia el rotulo, y el segundo toque es el que va.
PREGUNTA = "armConfirm"

#  LAS QUE NO PREGUNTAN, con la razon por la que no.
#
#  Igual que en `deshacer.py`: una excepcion sin razon escrita es la regla
#  rebajada; con la razon al lado es la regla completa.
SIN_AVISO = {
    "loadProject": "ES el embudo: pregunta quien lo llama, porque por aqui entra"
                   " tambien la recuperacion de sesion al arrancar — y preguntarle"
                   " a alguien si quiere recuperar lo que acaba de perder es la"
                   " peor version de esto",
    "newProject":  "igual: es el embudo, y la tapa NUEVO ya pregunta antes",
}


def sinComentarios (texto):
    """Fuera los comentarios, conservando los saltos de linea.

    Hace falta por lo mismo que en `deshacer.py`: este fichero explica cada
    decision nombrando las funciones de las que habla —«y NO va dentro de
    `loadProject`»— asi que leer los comentarios como codigo inventa llamadas
    que no existen. Alli costo una rotura que decia OK con el `pushUndo`
    comentado.
    """
    def fuera (m):
        s = m.group (0)
        return s if s[0] in "\"'" else re.sub (r"[^\n]", " ", s)
    return re.sub (r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'"
                   r"|//[^\n]*" r"|/\*.*?\*/", fuera, texto, flags=re.S)


def funciones (texto):
    """Los limites de cada funcion miembro, por la llave de apertura."""
    out = []
    for m in re.finditer (r"^[\w:<>,&*\s]*?\bMainComponent::(\w+)\s*\(", texto, re.M):
        i = texto.find ("{", m.end())
        if i < 0: continue
        nivel, j = 0, i
        while j < len (texto):
            if   texto[j] == "{": nivel += 1
            elif texto[j] == "}":
                nivel -= 1
                if nivel == 0: break
            j += 1
        out.append ((m.group (1), m.start(), j))
    return out


def bloqueDe (texto, p):
    """Las llaves mas interiores que envuelven la posicion p.

    EL GRANO ES EL BLOQUE Y NO LA FUNCION, y esa es la diferencia entre una
    regla y una linea que imprime OK. Las TRES puertas al proyecto viven dentro
    del constructor —son `onClick` y `onChosen` cableados ahi— asi que
    preguntando por la funcion, la respuesta era «MainComponent» y bastaba con
    que el constructor tuviera UN `armConfirm` en cualquier parte, para
    cualquier cosa, para dar verde a las tres. Es el mismo error que ya se
    corrigio en `profundidad.py` con el alias `b`: acotar al bloque donde de
    verdad vive.
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


def main():
    if not os.path.isfile (FUENTE):
        print ("FALLA  no esta %s" % FUENTE)
        return 1
    texto = sinComentarios (open (FUENTE, encoding="utf8").read())

    #  LA CADENA DE CONTROL, delante: si los embudos se renombraron, «ninguna
    #  sin aviso» seria no haber mirado. Misma figura que el `if not cruces` de
    #  `desglose.py` y el `if llamadas == 0` de `deshacer.py`.
    llamadas = sum (len (re.findall (r"\b" + e + r"\s*\(", texto)) for e in EMBUDOS)
    if llamadas == 0:
        print ("FALLA  no hay ni una llamada a %s: la regla no mide nada"
               " (se habran renombrado los embudos)" % " ni a ".join (EMBUDOS))
        return 1

    #  Cada funcion miembro, para poder perdonar a los embudos por su nombre...
    dentroDe = []
    for nombre, ini, fin in funciones (texto):
        dentroDe.append ((ini, fin, nombre))

    def duenyo (pos):
        for ini, fin, nombre in dentroDe:
            if ini <= pos <= fin: return nombre
        return "(fuera de una funcion)"

    #  ...y cada LLAMADA con su bloque, que es donde se juzga.
    pide, perdonadas, sinRed = [], [], []
    vistas = set()
    for e in EMBUDOS:
        for m in re.finditer (r"\b" + e + r"\s*\(", texto):
            fn = duenyo (m.start())
            if fn in SIN_AVISO:
                vistas.add (fn)
                if fn not in perdonadas: perdonadas.append (fn)
                continue
            a, b = bloqueDe (texto, m.start())
            #  CON LA LINEA, que es lo que separa «una puerta no pregunta» de
            #  «cual». Las DOS puertas de ABRIR viven en el constructor y
            #  llaman al mismo embudo, asi que el par (funcion, embudo) las
            #  nombra igual: quitando el `armConfirm` de una, la misma etiqueta
            #  salia en «preguntan» y en «SIN AVISO» a la vez. Falla igual
            #  —el veredicto es correcto— pero no dice a cual ir.
            quien = "%s / %s:%d" % (fn, e, texto[:m.start()].count ("\n") + 1)
            vistas.add (fn)
            if (PREGUNTA + " (") in texto[a:b]:
                if quien not in pide: pide.append (quien)
            elif quien not in sinRed:
                sinRed.append (quien)

    print ("%d llamadas a %s en %d funciones"
           % (llamadas, " / ".join (EMBUDOS), len (vistas)))
    print ("preguntan:   %s" % (", ".join (sorted (pide)) or "ninguna"))
    print ("perdonadas:  %s" % (", ".join (sorted (perdonadas)) or "ninguna"))
    for n in sorted (perdonadas):
        print ("   %-18s %s" % (n, SIN_AVISO[n]))
    print ("SIN AVISO:   %s" % (", ".join (sorted (sinRed)) or "ninguna"))

    mal = 0
    if sinRed:
        mal = 1
        print ("FALLA  %d funciones sustituyen el proyecto entero sin preguntar:"
               " %s" % (len (sinRed), ", ".join (sorted (sinRed))))
    #  Y la otra mitad: un perdon que ya no corresponde a nadie protege a quien
    #  no existe, y el dia que alguien escriba una funcion con ese nombre se la
    #  salta sin que nadie lo decida.
    huerfanas = sorted (set (SIN_AVISO) - vistas)
    if huerfanas:
        mal = 1
        print ("FALLA  %d perdones no corresponden a ninguna funcion que llame a"
               " los embudos: %s" % (len (huerfanas), ", ".join (huerfanas)))
    if mal:
        return 1
    print ("VEREDICTO: OK  las %d puertas al proyecto preguntan antes, y las %d"
           " que no dicen por que" % (len (pide), len (perdonadas)))
    return 0


if __name__ == "__main__":
    sys.exit (main())

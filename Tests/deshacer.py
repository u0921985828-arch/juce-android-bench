#!/usr/bin/env python3
"""LA RED: TODO LO QUE PISA UN PAD PASA POR `pushUndo`, O DICE POR QUE NO.

`pushUndo` tenia veinticinco clientes y cubria bien el secuenciador -PEGAR,
MOVER, VACIAR, DOBLAR, HUMANIZAR, EUCLIDES-. Lo que se sale de ahi no: cargar
un fichero encima de un pad -que es de las acciones mas frecuentes de la app y
de las mas destructivas- entraba por TRES puertas -el navegador, el doble toque
y el selector del sistema- y ninguna tomaba la foto. Grabar del micro tampoco,
teniendola su gemela REMUESTREAR, que aterriza exactamente igual. Y la rama de
FABRICA de la ficha de instrumentos se llevaba dieciseis pads por delante
mientras la rama de ficheros de la MISMA funcion la tomaba en su primera linea.

Nadie lo medía. `plano.py` sabe que tapas hay y `desglose.py` que controles
tiene cada ficha, pero «esto se puede deshacer» no se ve en la pantalla: es una
propiedad del camino y solo se nota el dia que la pierdes.

SE DERIVA Y NO SE DECLARA, que es lo que la hace util el segundo año. El embudo
por el que entra TODO lo que ocupa un pad es `assignSampleToPad` -lo dice su
propio comentario: «por donde entran TODOS los...»- asi que la regla busca sus
llamadas, mira en que funcion cae cada una, y pide `pushUndo` en esa misma
funcion. Una puerta nueva a un pad aparece sola, sin que nadie se acuerde de
apuntarla — la misma razon por la que las tapas de mantener «se buscan, no se
enumeran».

Y las excepciones se ESCRIBEN CON SU RAZON, no se perdonan en silencio: abajo,
en `SIN_RED`, una linea por funcion. Una excepcion sin razon escrita es la
regla rebajada; con la razon al lado es la regla completa.

    python3 Tests/deshacer.py
"""
import os, re, sys

ROOT   = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
FUENTE = os.path.join (ROOT, "Source", "MainComponent.cpp")

#  EL EMBUDO. Si algun dia cambia de nombre, esta prueba se queda midiendo una
#  funcion que no existe y diria OK para siempre: por eso se comprueba que
#  aparece, abajo, antes de dar ningun veredicto.
EMBUDO = "assignSampleToPad"
FOTO   = "pushUndo"

#  LAS QUE NO LLEVAN FOTO, con la razon por la que no. Cuatro familias:
#
#   - las que RESTAURAN: tomar una foto al deshacer seria apilar sobre lo que
#     se esta desapilando, o sea deshacer que ya no vuelve.
#   - las que ESCUCHAN: la escucha del navegador es reversible por su propio
#     camino -la x devuelve `preAuditionSample`- y su foto la toma CARGAR, con
#     el contenido de ANTES de la escucha. Ver `loadBrowserSelection`.
#   - las que ARRANCAN la app: la fabrica de la primera vez y la sesion que
#     vuelve no tienen nada detras que deshacer.
#   - el ANDAMIO del banco, que no es la app.
SIN_RED = {
    "assignSampleToPad":    "ES el embudo: la foto la toma quien lo llama, porque"
                            " por aqui pasan tambien deshacer y la sesion",
    "restorePads":          "restaura: la foto la tomo quien lo mando deshacer",
    "stepPadJob":           "es la bomba del reparto asincrono de la sesion y del"
                            " proyecto: pone lo que ya era tuyo",
    "cancelAudition":       "la escucha se deshace por su camino, ver preAuditionSample",
    "selectionChanged":     "es la escucha del navegador: un toque en la lista carga"
                            " y dispara, y la foto -con lo de ANTES de oir- la toma"
                            " CARGAR. Ver loadBrowserSelection",
    "cargaFabricaEnBanco":  "la foto la toma quien llama: loadFactoryKits -primera"
                            " vez que se abre la app- no la quiere y la ficha de"
                            " instrumentos si",
    #  Se llamaba `ponInstrumentoEnPad` hasta que la sintesis se fue a su
    #  propio hilo -473 ms de mediana congelaban la cara-. Ahora esa funcion
    #  encola y NO pisa el pad; quien lo pisa es el que vacia el buzon, y la
    #  foto se sigue tomando en el mismo sitio: ANTES de encolar.
    "montaInstrumentoRendido": "lo saca del buzon en el hilo de mensajes:"
                            " onInstElegido y eligePreset toman la foto antes"
                            " de encolar la sintesis, y el banco llama sin cara"
                            " que deshacer",
    "resintetizaInstrumento": "la toman los dos que llaman: el arrastre de un mando"
                              " en onDragStart -una sola por arrastre- y VOLVER",
}


def sinComentarios (texto):
    """Fuera los comentarios, conservando los saltos de linea.

    Hace falta porque la primera version leia el fichero tal cual y una llamada
    COMENTADA seguia contando: al romper a proposito -poner `//` delante del
    `pushUndo` del micro- la regla siguio diciendo OK, porque el texto
    `pushUndo (` sigue ahi. Y este fichero esta lleno de comentarios que
    nombran las dos funciones -es el formato de la casa: contar el fallo con
    sus cifras- asi que no es un caso raro, es el caso normal. Se rompio de
    verdad -borrando la linea- y entonces si: `SIN RED: toggleMicSampling`.

    Los saltos se conservan para que las posiciones sigan cuadrando con las
    lineas, que es lo unico que hace falta de este texto.
    """
    def fuera (m):
        s = m.group (0)
        return s if s[0] in "\"'" else re.sub (r"[^\n]", " ", s)
    #  Las cadenas van PRIMERO en la alternancia para que un `//` dentro de una
    #  -«https://...»- no abra un comentario.
    return re.sub (r'"(?:\\.|[^"\\])*"' r"|'(?:\\.|[^'\\])*'"
                   r"|//[^\n]*" r"|/\*.*?\*/", fuera, texto, flags=re.S)


def funciones (texto):
    """Los limites de cada funcion miembro, por la llave de apertura.

    Basta con `void MainComponent::loQueSea (` a principio de linea y contar
    llaves hasta cerrar: este fichero no anida definiciones de miembro, y los
    lambdas de dentro cuentan como parte de la funcion que los escribe —que es
    justo lo que se quiere, porque la foto de una carga asincrona se toma en el
    cuerpo y se gasta en el lambda.
    """
    out = []
    pat = re.compile (r"^[\w:<>,&*\s]*?\bMainComponent::(\w+)\s*\(", re.M)
    for m in pat.finditer (texto):
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


def main():
    if not os.path.isfile (FUENTE):
        print ("FALLA  no esta %s" % FUENTE)
        return 1
    texto = sinComentarios (open (FUENTE, encoding="utf8").read())

    #  LA CADENA DE CONTROL, antes que nada: si el embudo ya no se llama asi,
    #  «ninguna sin red» seria no haber mirado. Es la misma figura que el
    #  `if not cruces` de `desglose.py`.
    llamadas = texto.count (EMBUDO + " (")
    if llamadas == 0:
        print ("FALLA  no hay ni una llamada a %s: la regla no mide nada"
               " (se habra renombrado el embudo)" % EMBUDO)
        return 1

    fs = funciones (texto)
    sinRed, conRed, perdonadas = [], [], []
    vistas = set()
    for nombre, ini, fin in fs:
        cuerpo = texto[ini:fin]
        if (EMBUDO + " (") not in cuerpo:
            continue
        vistas.add (nombre)
        if nombre in SIN_RED:
            perdonadas.append (nombre)
        elif (FOTO + " (") in cuerpo:
            conRed.append (nombre)
        else:
            sinRed.append (nombre)

    print ("%d llamadas a %s en %d funciones"
           % (llamadas, EMBUDO, len (vistas)))
    print ("con red:      %s" % (", ".join (sorted (conRed)) or "ninguna"))
    print ("perdonadas:   %s" % (", ".join (sorted (perdonadas)) or "ninguna"))
    for n in sorted (perdonadas):
        print ("   %-24s %s" % (n, SIN_RED[n]))
    print ("SIN RED:      %s" % (", ".join (sorted (sinRed)) or "ninguna"))

    mal = 0
    if sinRed:
        mal = 1
        print ("FALLA  %d funciones pisan un pad y no toman foto: %s"
               % (len (sinRed), ", ".join (sorted (sinRed))))
    #  Y LA OTRA MITAD: un perdon que ya no corresponde a nada es una
    #  excepcion que protege a quien no existe, y el dia que alguien escriba
    #  una funcion con ese nombre se la salta sin que nadie lo decida. Un
    #  numero que nadie mira se publica; una excepcion que nadie mira se cae.
    huerfanas = sorted (set (SIN_RED) - vistas)
    if huerfanas:
        mal = 1
        print ("FALLA  %d perdones no corresponden a ninguna funcion que llame"
               " a %s: %s" % (len (huerfanas), EMBUDO, ", ".join (huerfanas)))
    if mal:
        return 1
    print ("VEREDICTO: OK  las %d funciones que ocupan un pad toman foto, y las"
           " %d que no dicen por que" % (len (conRed), len (perdonadas)))
    return 0


if __name__ == "__main__":
    sys.exit (main())

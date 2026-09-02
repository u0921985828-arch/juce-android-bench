#!/usr/bin/env python3
# ============================================================================
#  LA AUTOMATIZACION — lo que separa «toco los efectos» de «el rebote suena
#  como lo toque».
#
#  La fila de efectos existe para TOCAR: esa es la mitad de por que las seis
#  tapas son ranuras. Y todo lo que se tocaba se perdia al exportar, porque el
#  rebote salia con el numero que estuviera puesto al final — asi que la unica
#  forma de que un barrido de filtro llegara al fichero era no tocarlo.
#
#  NINGUNA de las nueve reglas de `expo.py` puede ver nada de esto: es ESTADO.
#  Una app que se olvida de lo que tocaste se maqueta perfecta, no solapa, no
#  corta un rotulo y esta traducida. Es la familia de los cinco fallos del
#  compas del piano, otra vez.
#
#  SE MIDE POR LA TAPA Y POR EL MANDO —`autoBtn.onClick`, `macroCtrl1` con su
#  `onValueChange`— y no llamando a `anotaAutomacion` por dentro, que es justo
#  donde no existe ninguno de los fallos: si el evento no llega a
#  `pushFxParam`, o si el paso que se lee no es el del transporte, llamar a la
#  funcion por dentro pasa igual.
#
#      python3 Tests/auto.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  Lo que se escribe a mano antes de guardar. Dos eventos y no uno, y con
#  valores DISTINTOS entre si y de distinto signo: con dos iguales, un cruce de
#  campos dentro de la fila pasaria desapercibido.
ESPERADO = "17:3:2:0.42;48:0:0:-0.75;"


def corre():
    casa = tempfile.mkdtemp (prefix="zati-auto-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es", ZATI_AUTO="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("auto"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea de la automatizacion")
        return 1

    malas = []

    #  1. PARADO NO SE ESCRIBE, RODANDO SI. Con DOS cifras y no una: «escribe»
    #     lo cumple igual una app que escribe siempre, y entonces un evento sin
    #     paso acabaria en cualquier sitio - el sintoma seria un barrido que
    #     suena al principio de la cancion en vez de donde lo tocaste.
    print ("armar    la tapa arma: %d" % r["armado_tras_tocar"])
    print ("parado   eventos escritos con el transporte quieto: %d" % r["parado_escribe"])
    print ("rodando  eventos: %d   en el paso %d   (el transporte iba por el %d)"
           % (r["rodando_escribe"], r["paso_escrito"], r["paso_visto"]))
    if r["armado_tras_tocar"] != 1:
        malas.append ("la tapa AUTO no arma")
    if r["parado_escribe"] != 0:
        malas.append ("con el transporte parado se escribieron %d eventos: un evento "
                      "sin paso cae en cualquier sitio" % r["parado_escribe"])
    if r["rodando_escribe"] < 1:
        malas.append ("con la cancion rodando no se escribio nada")
    if r["paso_escrito"] != r["paso_visto"]:
        malas.append ("el evento se anoto en el paso %d y el transporte iba por el %d"
                      % (r["paso_escrito"], r["paso_visto"]))

    #  2. UN EVENTO POR PASO Y POR PARAMETRO, Y EL ULTIMO GANA. Tres valores
    #     seguidos en el mismo paso son UN evento con el ultimo, no tres: sin
    #     esto un arrastre de dos segundos escribe cientos de eventos en el
    #     mismo sitio y cuatro mil huecos se llenan con una sola frase.
    print ("mismo    tras tres valores en el mismo paso: %d evento(s), el ultimo %d"
           % (r["tras_tres"], int (r["ultimo"])))
    if r["tras_tres"] != 1:
        malas.append ("tres valores en el mismo paso dejaron %d eventos" % r["tras_tres"])
    if int (r["ultimo"]) != 800:
        malas.append ("gano el valor %d y no el ultimo (800)" % int (r["ultimo"]))

    #  3. PARAR DESARMA. Un modo de escritura que se queda puesto es como se
    #     borra una automatizacion buena en la pasada siguiente sin haber
    #     tocado nada a proposito.
    print ("parar    armado despues de parar: %d" % r["armado_tras_parar"])
    if r["armado_tras_parar"] != 0:
        malas.append ("parar el transporte no desarmo AUTO")

    #  4. Y VUELVE DEL FICHERO DE PROYECTO, con DOS cifras: los eventos que
    #     vuelven al espejo Y los que tiene el MOTOR. Solo lo primero lo cumple
    #     una lista que se lee del XML y no se publica nunca — la
    #     automatizacion volveria escrita y muda, que es exactamente el fallo
    #     que ya se cazo con los clips.
    print ("vuelve   %s   motor %d" % (r["vuelta"] or "(nada)", r["motor"]))
    if r["vuelta"] != ESPERADO:
        malas.append ("del fichero volvio «%s» y se guardo «%s»" % (r["vuelta"], ESPERADO))
    if r["motor"] != 2:
        malas.append ("el motor se quedo con %d eventos: la tabla se lee y no se publica"
                      % r["motor"])

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("se escribe solo rodando, un evento por paso y parametro, parar desarma, "
           "y vuelve entera del proyecto")
    return 0


if __name__ == "__main__":
    sys.exit (main())

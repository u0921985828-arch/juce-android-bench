#!/usr/bin/env python3
# ============================================================================
#  EL BANCO DE TOMAS: DONDE CAE LO QUE SE GRABA.
#
#  Llego del telefono con estas palabras: «cuando grabo se sobrescribe el pad
#  uno». Era exacto y era total, no intermitente. `grabaAlArreglo` pedia
#  `firstEmptyPad()` y, si no habia ninguno, caia en el pad ELEGIDO -justo
#  debajo del comentario que promete que una toma nueva no pisa un sonido que
#  la persona haya puesto-. Y en una maquina de fabrica esa caida es la unica
#  rama que corre: los sesenta y cuatro pads vienen llenos, asi que
#  `firstEmptyPad()` devuelve -1 SIEMPRE. `Tests/carga.py` ya lo medi­a por el
#  otro lado -64 pads con sonido en una instalacion limpia- y nadie habia
#  cruzado las dos cifras.
#
#  Y hay una segunda mitad que no se ve: un clip apunta al PAD, asi que todas
#  las tomas caian en el mismo hueco y la segunda REESCRIBIA el audio de la
#  primera - el clip ya puesto en la linea de tiempo pasaba a sonar otra cosa.
#  Dos tomas al arreglo no se podian hacer.
#
#  NINGUNA DE LAS TRECE REGLAS DE `expo.py` PUEDE VERLO. Es un fallo de INDICE
#  y de estado: una toma que cae en el pad equivocado se maqueta perfecta -no
#  solapa, no se sale, no corta un rotulo, no mide cero y esta traducida-. Es
#  la familia de los cinco fallos del compas del piano.
#
#  SE MIDE POR LA TAPA -`songRecBtn.onClick`- y no llamando a `grabaAlArreglo`
#  por dentro, que es justo donde no vive ninguno de estos fallos.
#
#      python3 Tests/tomas.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


#  SIN PANTALLA NO SE MIDE NADA, y hay que DECIRLO: sin esta guarda un Xvfb
#  muerto sale como «la app no publico la linea», o sea como un fallo de la
#  app, y se pierde media tarde buscando un cambio que no era.
def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre():
    casa = tempfile.mkdtemp (prefix="zati-tomas-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_TOMAS="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=240)
    except subprocess.TimeoutExpired:
        return {}
    filas = {}
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "tomas" in r: filas[r["tomas"]] = r
    return filas


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    if not display_alive():
        print ("FALLA  no hay DISPLAY vivo: esta prueba maqueta la app")
        return 1

    f = corre()
    if 1 not in f or 2 not in f or 3 not in f:
        print ("FALLA  la app no publico las tres lineas de las tomas")
        return 1

    malas = []
    a, b, c = f[1], f[2], f[3]
    banco = chr (ord ("A") + a["banco"])

    #  1. LA TOMA CAE EN EL BANCO DE TOMAS *Y EL PAD 01 SIGUE INTACTO*.
    #
    #     Dos cifras porque una se engaña: «cae en C01» lo cumple tambien un
    #     codigo que escribe siempre el mismo pad, y «el 01 sigue intacto» lo
    #     cumple una toma que no llego a grabarse. La segunda es la queja tal
    #     cual llego.
    print ("destino  primera %s   segunda %s   nombre %s   (banco de tomas %s)"
           % (a["primera"], a["segunda"], a["nombre"], banco))
    print ("pad 01   antes %-10s ahora %s" % (a["pad01antes"], a["pad01"]))
    if not a["primera"].startswith (banco):
        malas.append ("la toma no cae en el banco de tomas: %s" % a["primera"])
    if a["pad01"] != a["pad01antes"]:
        malas.append ("la toma se comio el pad 01: era «%s» y es «%s»"
                      % (a["pad01antes"], a["pad01"]))

    #  2. Y LA SEGUNDA CAE EN OTRO PAD. Es la mitad que no se ve: un clip apunta
    #     al PAD, asi que dos tomas en el mismo hueco reescriben el audio de la
    #     primera.
    if a["primera"] == a["segunda"]:
        malas.append ("las dos tomas caen en el mismo pad (%s): la segunda "
                      "reescribe el audio de la primera y su clip pasa a sonar "
                      "otra cosa" % a["primera"])
    if not a["nombre"].startswith ("TOMA"):
        malas.append ("la toma no se llama TOMA n: «%s»" % a["nombre"])

    #  3. CON EL BANCO LLENO DE LO QUE PUSO LA PERSONA, NO GRABA Y LO DICE.
    #
    #     Con dos cifras: sin la segunda, «dice que esta lleno» lo cumple
    #     tambien una app que lo dice y graba igual.
    print ("lleno    lo dice %d   arranco %d" % (b["lleno"], b["grabando"]))
    if b["lleno"] != 1:
        malas.append ("con los dieciseis puestos por la persona, la puerta "
                      "sigue encontrando sitio")
    if b["grabando"] != 0:
        malas.append ("el banco esta lleno y la toma arranca igual: se va a "
                      "comer algo que pusiste tu")

    #  4. Y LA MARCA VUELVE DEL FICHERO. Sin ella la regla cambiaria entre el
    #     primer arranque y el segundo -la sesion devuelve los sesenta y cuatro
    #     desde sus WAV- y la toma siguiente se comeria la anterior. Se borra a
    #     mano entre guardar y abrir: si al volver sigue puesta no es que se
    #     haya guardado, es que nadie la quito.
    print ("marca    de fabrica antes %d   vuelven %d" % (c["antes"], c["vuelven"]))
    if c["vuelven"] != c["antes"]:
        malas.append ("la marca de fabrica no vuelve del fichero: %d de %d"
                      % (c["vuelven"], c["antes"]))

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("las tomas caen en el banco %s, en orden, y nada de lo tuyo se pisa" % banco)
    return 0


sys.exit (main())

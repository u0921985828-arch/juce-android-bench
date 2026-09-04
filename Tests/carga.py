#!/usr/bin/env python3
# ============================================================================
#  ZATI - LO QUE CADA PANTALLA LE PIDE A QUIEN LA MIRA.
#
#  Las once reglas de `expo.py` miden que TODO QUEPA: que nada solape, que nada
#  se salga, que ningun rotulo se corte, que ninguna celda baje de su suelo. Una
#  ficha con setenta controles perfectamente colocados las pasa las once, y es
#  exactamente la ficha que nadie entiende la primera vez.
#
#  Aqui se cuenta lo que ninguna de ellas mira: CUANTO hay en cada pantalla.
#  Controles propios -los de la ficha, no los de la cara que se ve debajo-,
#  rotulos pintados y palabras que hay que leer. No se juzga, se imprime: un
#  mezclador de dieciseis canales tiene setenta y dos controles porque es un
#  mezclador de dieciseis canales, y un tope inventado lo suspenderia por
#  hacer su trabajo. Es la misma decision que TOUCH y que el porcentaje de
#  iconos - la cifra existe para que una pantalla no engorde sin que nadie se
#  entere.
#
#  Y SI SE JUZGA UNA COSA, que es la unica de esta casa que mide el PRODUCTO y
#  no una pieza: entre instalar la app y oir algo no puede haber mas que UN
#  TOQUE. Con la maquina recien instalada -HOME limpio, sin sesion- y con la
#  BIENVENIDA DELANTE, que es el estado de la primera vez y no el de abrirla
#  dos veces.
#
#  Y la tarjeta la levanta el propio gancho, porque con `ZATI_AUDIT` la app no
#  la enseña NUNCA a proposito -una tarjeta encima serian diecinueve fichas
#  medidas a traves de ella-: esperar a que salga sola seria esperar a algo que
#  el banco apaga. Se pone el estado que se quiere medir, igual que `ZATI_DLC`
#  planta los packs.
#
#      python3 Tests/carga.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile
import concurrent.futures as cf

HERE = os.path.dirname (os.path.abspath (__file__))
ROOT = os.path.dirname (HERE)
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, HERE)
from expo import SHEETS          # la lista de pantallas vive en UN sitio


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (env_extra, size="412x915"):
    #  HOME propio: sin el, la app restaura la sesion que dejara la prueba
    #  anterior y la cuenta de controles cambia con lo que hubiera puesto.
    casa = tempfile.mkdtemp (prefix="zati-carga-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_SIZE=size, ZATI_LANG="es")
    env.update (env_extra)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=240).stdout
    except Exception:
        return []
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    filas = []
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ("{"):
            try: filas.append (json.loads (l))
            except Exception: pass
    return filas


def mide (ficha):
    rows = corre ({"ZATI_AUDIT": "1", "ZATI_OPEN": ficha})
    if not rows:
        return None
    comps = [r for r in rows if "path" in r]
    ctrl  = [r for r in comps if r.get ("kind") in ("button", "slider", "editor")
             and r.get ("w", 0) > 0 and r.get ("h", 0) > 0]
    #  SOLO LO SUYO. La cara sigue debajo de una ficha abierta con todas sus
    #  tapas maquetadas, asi que contar todo daria la cara mas la ficha en cada
    #  fila. Cada ficha lleva su numero de capa y todo lo que cuelga de ella lo
    #  hereda - la misma pieza que hizo falta para la regla del rotulo tapado.
    capa  = max ([r.get ("capa", 0) for r in comps] or [0])
    suyos = [r for r in ctrl if r.get ("capa", 0) == capa]
    pint  = [r.get ("rotulo", "") for r in rows
             if r.get ("rotulo") and r.get ("capa", 0) == capa]
    #  Palabras: lo que hay que LEER para saber que hace cada cosa. El rotulo de
    #  una tapa cuenta aunque lleve dibujo al lado, que el dibujo es el adorno.
    textos = [r.get ("text", "") for r in suyos if r.get ("text")] + pint
    #  Y CUANTOS TIENEN NOMBRE para quien no ve la pantalla: `getTitle` es lo
    #  que lee TalkBack, y sin el un control se anuncia por su clase.
    return dict (ficha = ficha or "cara",
                 ctrl = len (suyos), rotulos = len (pint),
                 nombre = sum (1 for r in suyos if r.get ("nombre")),
                 palabras = sum (len (t.split()) for t in textos))


def primer_sonido():
    for r in corre ({"ZATI_AUDIT": "1", "ZATI_PRIMER": "1"}):
        if r.get ("primer"):
            return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes -esto no mide nada sin "
               "pantalla, y sin decirlo saldria como un fallo de la app-");   return 1

    with cf.ProcessPoolExecutor (max_workers=4) as ex:
        res = [r for r in ex.map (mide, SHEETS) if r]
    if len (res) != len (SHEETS):
        print ("FALLA  %d de %d pantallas no contestaron" % (len (SHEETS) - len (res), len (SHEETS)))
        return 1

    res.sort (key = lambda r: -r["ctrl"])
    print ("%-11s %6s %8s %9s %8s" % ("pantalla", "ctrl", "rotulos", "palabras", "nombre"))
    for r in res:
        print ("%-11s %6d %8d %9d %7d%%"
               % (r["ficha"], r["ctrl"], r["rotulos"], r["palabras"],
                  100 * r["nombre"] // max (1, r["ctrl"])))
    ctrl = sorted (r["ctrl"] for r in res)
    print()
    conNombre = sum (r["nombre"] for r in res)
    print ("%d pantallas   %d controles propios   mediana %d   la mas cargada %s con %d"
           % (len (res), sum (ctrl), ctrl[len (ctrl) // 2], res[0]["ficha"], res[0]["ctrl"]))
    print ("con nombre para un lector de pantalla: %d de %d  (%d%%)"
           % (conNombre, sum (ctrl), 100 * conNombre // max (1, sum (ctrl))))

    #  --- EL RENGLON DE CONTINUIDAD ---------------------------------------
    #
    #  La banda de la cabecera decia el nombre del proyecto y nada mas. Lo que
    #  hace volver a un instrumento no es un premio: es la sensacion de que hay
    #  algo empezado, y para eso la app tiene que decir QUE proyecto, CUANTO
    #  trabajo hay dentro y DE CUANDO es.
    #
    #  Con DOS cifras, que es lo que separa las dos formas de escribirlo mal:
    #  que diga el proyecto Y que diga cuanto trabajo hay. La primera la cumple
    #  la linea de siempre; la segunda, una que se olvida del nombre.
    #
    #  Se mide con la app LLENA -la pantalla `llena` de la lista- porque vacia
    #  esta linea no tiene nada que contar: es el estado al que le falta justo
    #  lo que se mide. Y es una PANTALLA y no una variable aparte, o seria un
    #  estado que solo esta prueba sabe abrir.
    print()
    cont = ""
    for r in corre ({"ZATI_AUDIT": "1", "ZATI_OPEN": "llena"}, "800x1280"):
        if r.get ("tipo") == "proyecto": cont = r.get ("rotulo", "")
    print ("el renglon de continuidad dice: %s" % (cont or "(nada)"))
    faltan = []
    if "SESION NOCTURNA LARGA" not in cont: faltan.append ("que proyecto es")
    if "PADS" not in cont:                  faltan.append ("cuanto trabajo hay dentro")
    if "HACE" not in cont:                  faltan.append ("de cuando es")
    if faltan:
        for f in faltan:
            print ("FALLA  el renglon de continuidad no dice %s" % f)
        return 1

    #  --- Y LO UNICO QUE SE JUZGA ------------------------------------------
    p = primer_sonido()
    print()
    if p is None:
        print ("FALLA  la app no publico la linea del primer sonido");  return 1
    print ("recien instalada: %d pads con sonido, bienvenida %s, y UN toque en el "
           "pad 1 deja %d voz viva"
           % (p["pads_con_sonido"], "puesta" if p["bienvenida"] else "NO puesta", p["voces"]))
    malas = []
    #  Las DOS mitades. Solo la segunda la cumple tambien una maquina que abre
    #  sin la bienvenida -y entonces esto no estaria midiendo la primera vez-;
    #  solo la primera la cumple una que enseña la tarjeta y se traga el toque.
    if not p["bienvenida"]:
        malas.append ("la bienvenida no quedo puesta: esto no mide la primera vez")
    if p["pads_con_sonido"] < 16:
        malas.append ("la rejilla abre con %d pads con sonido de 16" % p["pads_con_sonido"])
    if p["voces"] < 1:
        malas.append ("un toque en el pad 1 no suena con la bienvenida delante")
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("la maquina suena al primer toque")
    return 0


sys.exit (main())

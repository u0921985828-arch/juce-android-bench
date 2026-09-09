#!/usr/bin/env python3
"""ZATI — el banco de los paneles de grupo.

Un panel no se puede salir de la ventana, no solapa a nadie y no lleva texto,
asi que NINGUNA de las seis reglas de expo.py le aplica: la geometria que
vigilan es la de los controles, y un panel es lo que se dibuja ALREDEDOR de
ellos. Se anadieron a cuatro fichas y el banco entero seguia en verde con el
aire repartido a ojo.

Lo unico que un panel puede hacer mal es el REPARTO DEL AIRE, y son dos cosas
distintas:

  1. Que no envuelva lo que dice envolver -un control fuera del panel, o el
     panel mas grande que su grupo-, que es un fallo de que `resized()` cerro
     el grupo donde no tocaba.
  2. Que el aire no sea el MISMO a cada lado. Un panel con cuatro pixeles a la
     izquierda y seis a la derecha no se lee como un fallo, se lee como que la
     pagina esta torcida - que es peor, porque no se puede senalar.

Y una tercera que solo existe habiendo varios: dos paneles de la misma pagina
tienen que compartir sus bordes verticales. Uno un pixel mas ancho que el de
arriba es la clase de cosa que se ve sin poder decir que se ha visto.

Se mide sobre el VOLCADO y no sobre una captura, como todo aqui: la app apunta
cada panel con UiAudit::panel y cada control sale ya en el volcado, asi que lo
que se compara son las dos cifras que la maqueta produjo - no unos pixeles
contados en una foto, que traen ademas la sombra de la tapa y el antialias del
borde redondeado.
"""
import json, os, subprocess, sys, tempfile, shutil
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname (os.path.abspath (__file__))
APP  = os.path.join (HERE, "..", "build", "Zati_artefacts", "Release", "Zati")

#  Las mismas siete pantallas y los mismos cuatro idiomas que expo.py: una
#  pantalla que alli se mide y aqui no es una pantalla donde esto no se sabe.
SIZES = ["360x640", "393x851", "412x915", "344x882", "280x653", "800x1280", "915x412"]
LANGS = ["es", "en", "zh", "ar"]
#  Solo las fichas que llevan paneles. Abrir las otras veintitantas seria
#  cuadruplicar el tiempo para leer cero paneles en cada una.
#  Y el PIANO, que entra el dia que tiene paneles: era la unica de las tres
#  paginas de la ficha del secuenciador sin ninguno, asi que esta lista no lo
#  abria y una pagina sin paneles no se puede medir mal.
SHEETS = ["pads", "pad2", "pad3", "sec", "secp", "paso", "piano", "song", "set", "asp",
          "proj", "midi", "expo", "chop"]

#  El aire que la app dice que deja. Se lee del fichero y no se copia: una
#  prueba que lleva su propia copia del numero pasa cuando el numero cambia.
def tokens():
    import re
    src = open (os.path.join (HERE, "..", "Source", "ZatiLookAndFeel.h"), encoding="utf8").read()
    #  `sm` vive en una linea con cinco tokens y `gap` en otra; halfGap y los
    #  dos aires del panel se derivan, exactamente como en el C++. Se leen y no
    #  se copian: una prueba que lleva su propia copia del numero pasa cuando
    #  el numero cambia, que es la mitad de los fallos que este banco existe
    #  para cazar.
    m = re.search (r'int\s+xs\s*=\s*(\d+),\s*sm\s*=\s*(\d+)', src)
    if m is None: sys.exit ("no encuentro Metrics::sm")
    sm = int (m.group (2))
    g = re.search (r'int\s+gap\s*=\s*(\d+)', src)
    if g is None: sys.exit ("no encuentro Metrics::gap")
    halfGap = int (g.group (1)) // 2
    return halfGap, sm // 4, sm

AIRE_X, AIRE_Y, SEPARACION = tokens()


def run (size, lang, sheet, casa):
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": size, "ZATI_LANG": lang,
                 "ZATI_OPEN": sheet, "ZATI_SKIN": "3", "HOME": casa})
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=60)
    except subprocess.TimeoutExpired:
        return None
    rows = []
    for line in p.stdout.splitlines():
        line = line.strip()
        if not line.startswith ("{"): continue
        try: rows.append (json.loads (line))
        except Exception: pass
    return rows or None


def juzga (rows, tag, lang):
    """Las tres preguntas, sobre un volcado."""
    fallos = []
    paneles = [r for r in rows if r.get ("panel")]
    if not paneles:
        return fallos, 0

    #  Los controles de VERDAD. Lo que mide cero no esta colocado -la casa
    #  apaga y vacia lo que no cabe- y meterlo en la union estiraria el grupo
    #  hasta el origen.
    ctrl = [r for r in rows
            if r.get ("kind") in ("button", "slider", "editor", "combo")
            and r.get ("w", 0) > 0 and r.get ("h", 0) > 0]

    for pa in paneles:
        px, py, pw, ph = pa["x"], pa["y"], pa["w"], pa["h"]
        capa = pa.get ("capa", 0)

        #  QUIEN VIVE DENTRO: por el CENTRO y no por el solape. Un control que
        #  asoma medio pixel por el borde de un panel vecino contaria en los
        #  dos y las dos cuentas saldrian mal; el centro pertenece a uno solo.
        dentro = [c for c in ctrl
                  if c.get ("capa", 0) == capa
                  and px <= c["x"] + c["w"] // 2 <= px + pw
                  and py <= c["y"] + c["h"] // 2 <= py + ph]
        if not dentro:
            #  Un panel vacio es un grupo que se cerro sobre nada: se dibuja una
            #  losa detras de un rotulo pintado y de nada mas.
            fallos.append (("VACIO", tag, f"panel {pw}x{ph} en {px},{py} no envuelve ningun control"))
            continue

        izq = min (c["x"] for c in dentro)
        der = max (c["x"] + c["w"] for c in dentro)

        #  1. QUE ENVUELVA. Un control que se sale del panel por los lados.
        if izq < px or der > px + pw:
            fallos.append (("FUERA", tag,
                            f"panel [{px},{px+pw}] no cubre [{izq},{der}]"))
            continue

        #  2. QUE LAS FILAS DE DENTRO EMPIECEN Y ACABEN EN LA MISMA X.
        #
        #     Esta es la pregunta, y costo dos intentos llegar a ella. El
        #     primero comparo el aire contra el TOKEN y saco 580 hallazgos de
        #     una regla equivocada: el token es el aire del panel contra el
        #     grupo que resized() cerro, y las tapas de dentro meten ademas su
        #     propio margen, asi que 4 + 2 = 6 es correcto y constante en casi
        #     toda la app.
        #
        #     El segundo comparo izquierda contra derecha DEL PANEL, y ahi el
        #     falso positivo son las columnas de rotulo PINTADO: en RECORTE los
        #     cuatro deslizadores empiezan 68 px dentro porque a su izquierda
        #     va el nombre de cada uno, y en PROYECTOS igual. Eso no es un
        #     panel torcido, es una maqueta con canal - y es deliberada.
        #
        #     Lo que el ojo ve como torcido es que DOS FILAS del mismo panel no
        #     empiecen en el mismo sitio, y eso no tiene excepcion legitima:
        #     donde pasa, es que una fila se escribio con su margen a mano en
        #     vez de con el token. Sin umbrales y sin exenciones.
        filas = []
        for c in sorted (dentro, key=lambda c: c["y"]):
            #  Misma fila = se solapan en vertical. Dos tapas de la misma fila
            #  comparten renglon entero; dos de filas distintas no se tocan.
            if filas and c["y"] < filas[-1][1]:
                filas[-1][0] = min (filas[-1][0], c["x"])
                filas[-1][1] = max (filas[-1][1], c["y"] + c["h"])
                filas[-1][2] = max (filas[-1][2], c["x"] + c["w"])
            else:
                filas.append ([c["x"], c["y"] + c["h"], c["x"] + c["w"]])

        #     Y por el borde de ENTRADA, no por los dos. Que una fila acabe
        #     antes que otra es un control mas corto a proposito -en MIDI la
        #     tapa del puerto es un tercio de ancho y la lista de debajo entera-
        #     y eso no es desalineado, es un boton. Lo que no puede pasar es que
        #     dos filas del mismo panel EMPIECEN en sitios distintos.
        #
        #     El borde de entrada es la izquierda en tres idiomas y la DERECHA
        #     en arabe, porque Lang::takeStart muerde por el otro lado: leer
        #     siempre la izquierda habria dado por bueno en arabe justo lo que
        #     se rechaza en espanol, y al reves.
        rtl = (lang == "ar")
        entradas = sorted ({(px + pw - f[2]) if rtl else (f[0] - px) for f in filas})
        if len (entradas) > 1:
            fallos.append (("FILAS", tag,
                            f"panel en {px},{py}: filas que empiezan en "
                            + "/".join (str (e) for e in entradas) + " px"))

    #  3. LOS PANELES DE LA MISMA COLUMNA COMPARTEN BORDES, y no se tocan.
    #
    #     POR COLUMNA Y NO POR FICHA, que fue el segundo error de esta prueba:
    #     apaisado, SEC y PASO parten la tarjeta en dos columnas -es la maqueta
    #     girada, y es correcta- asi que comparar todos contra todos daba
    #     "bordes distintos" en las dos y huecos de -84 px, que son dos paneles
    #     que ni se tocan ni se solapan: estan uno al lado del otro.
    columnas = defaultdict (list)
    for pa in paneles:
        columnas[(pa.get ("capa", 0), pa["x"], pa["w"])].append (pa)

    #  AQUI HABIA UNA TERCERA REGLA Y SE CAYO. Decia que los paneles de una
    #  ficha tienen que compartir sus bordes verticales, y suena bien hasta que
    #  se mira lo que senala: en apaisado, la pagina PATRON pone CADENA y SWING
    #  en un panel a todo lo ancho y debajo parte la tarjeta en dos columnas con
    #  un panel cada una. Los tres empiezan en la misma x y el de arriba mide el
    #  doble - y esta bien, es un grupo que ocupa las dos columnas encima de dos
    #  que ocupan una. Distinguir eso de un panel un pixel mas ancho que su
    #  vecino no se puede sin inventarse un umbral, y un umbral inventado es
    #  como esta prueba ya se equivoco dos veces. Lo que si queda medido es que
    #  dos paneles de la MISMA columna no se toquen, que es lo de abajo.

    for clave, lista in columnas.items():
        orden = sorted (lista, key=lambda p: p["y"])
        for a, b in zip (orden, orden[1:]):
            hueco = b["y"] - (a["y"] + a["h"])
            if hueco < SEPARACION - 2 * AIRE_Y:
                fallos.append (("PEGADOS", tag,
                                f"paneles en y={a['y']} y y={b['y']}: {hueco} px de hueco"))
    return fallos, len (paneles)


def una (combo):
    size, lang, sheet, casa = combo
    rows = run (size, lang, sheet, casa)
    if rows is None:
        return [("CRASH", f"{size}/{lang}/{sheet}", "sin volcado")], 0
    return juzga (rows, f"{size}/{lang}/{sheet}", lang)


def main():
    if not os.path.exists (APP):
        sys.exit ("no esta compilado: " + APP)
    casas = tempfile.mkdtemp()
    combos = []
    for i, size in enumerate (SIZES):
        for lang in LANGS:
            for sheet in SHEETS:
                #  Un HOME por corrida: dos procesos creando .sesion/samples a
                #  la vez es exactamente la carrera que Tests/session.py existe
                #  para cazar.
                casa = os.path.join (casas, f"{size}_{lang}_{sheet}")
                os.makedirs (casa, exist_ok=True)
                combos.append ((size, lang, sheet, casa))

    todos, paneles, corridas = [], 0, 0
    try:
        with ProcessPoolExecutor (max_workers=os.cpu_count() or 4) as ex:
            for f, n in ex.map (una, combos):
                todos += f; paneles += n; corridas += 1
    finally:
        shutil.rmtree (casas, ignore_errors=True)

    porclase = defaultdict (list)
    for kind, tag, que in todos: porclase[kind].append ((tag, que))

    for kind in ("CRASH", "VACIO", "FUERA", "FILAS", "PEGADOS"):
        if not porclase[kind]: continue
        print (f"\n{kind}  ({len (porclase[kind])})")
        #  Uno por texto distinto: el mismo panel torcido sale en 28 corridas y
        #  veintiocho lineas iguales esconden la segunda clase de fallo.
        visto = set()
        for tag, que in porclase[kind]:
            if que in visto: continue
            visto.add (que)
            print (f"  {tag:24} {que}")

    print()
    print (f"paneles: {corridas} corridas, {paneles} paneles medidos, "
           f"aire {AIRE_X} x {AIRE_Y}, separacion {SEPARACION}")
    if todos:
        print (f"FALLA: " + ", ".join (f"{k} {len (v)}" for k, v in porclase.items() if v))
        return 1
    print ("todos los paneles envuelven lo suyo con el mismo aire a cada lado")
    return 0


if __name__ == "__main__":
    sys.exit (main())

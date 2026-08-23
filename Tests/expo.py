#!/usr/bin/env python3
"""ZATI expo bench — runs the self-measuring build across the matrix and
judges every dump against the rules a stand at a music-tech show gets judged on:
a finger fits, nothing overlaps, no caption is clipped, in any language, on any
phone anyone will bring to the stand."""
import subprocess, os, json, sys, itertools, collections, re
import concurrent.futures, shutil, tempfile

#  RELATIVO AL SCRIPT, no una ruta absoluta al repositorio.
#
#  Estaba clavado a /home/user/FX-404/build/..., asi que copiar Tests/ a otro
#  sitio para que un reseteo del repositorio no matara la tanda aislaba los
#  SCRIPTS y no el binario: se seguia midiendo el del repositorio. Los numeros
#  eran validos -siempre se compila antes de medir- pero el aislamiento no
#  existia, y una compilacion lanzada a la vez tira la tanda entera con
#  PermissionError a mitad.
BIN = os.environ.get ("ZATI_BIN") or os.path.join (
    os.path.dirname (os.path.dirname (os.path.abspath (__file__))),
    "build", "Zati_artefacts", "Release", "Zati")

# Real devices, not round numbers. dp at the density the app actually lays out in.
SIZES = [
    ("360x640",  "small 16:9 (Galaxy A-series, Redmi 9A)"),
    ("393x851",  "Pixel 8 / modern 19.5:9"),
    ("412x915",  "Pixel 8 Pro / big phone"),
    ("344x882",  "narrow tall (Z Flip cover-open, Xperia)"),
    ("280x653",  "Galaxy Fold FRONT screen — the worst case anyone ships"),
    ("800x1280", "tablet portrait"),
    ("915x412",  "LANDSCAPE — the orientation nobody tests"),
]
LANGS = ["es", "en", "zh", "ar"]
SHEETS = ["", "pads", "pad2", "pad3", "sec", "secp", "paso", "song", "piano", "pianod", "mix", "xy", "set", "asp", "proj", "gest", "midi", "manual", "rack", "chop", "inst", "instd", "instg", "vst", "expo", "tour", "tour1", "tour6", "tour10", "tourf", "browse", "browsedir"]

MIN_TOUCH = 40   # Metrics::hit — Android's own guideline is 48dp, this is the floor
#  LO QUE SE DIBUJA Y SE TOCA IGUAL.
#
#  La rejilla de pasos y la lista de la cancion no son 256 botones: son UN
#  componente que se pinta y se acierta con el raton, y por eso la regla del
#  dedo minimo pasaba de largo por encima de ellas. Medido: en 280x653 una
#  celda de paso son 12x12 px - la tercera parte del dedo -, y el banco daba
#  esa pantalla por buena.
#
#  No es un fallo que se arregle agrandando la celda: son dieciseis pistas por
#  dieciseis pasos, y esa es la ficha. Lo que sirve es que no EMPEORE sin que
#  nadie se entere, y para eso hace falta que este medido. El suelo es lo que
#  hay hoy, y el gesto que lo hace usable - pintar arrastrando el dedo - ya
#  esta puesto.
MIN_CELL = 12
MIN_NOTE = 16   # la fila del piano roll: ver el bloque 0 de juzga()

#  LA PANTALLA VIRTUAL SE COMPRUEBA ANTES DE EMPEZAR.
#
#  Xvfb se muere solo cada cierto tiempo en este entorno, y cuando se muere la
#  app no arranca: segmentation fault en Component::centreWithSize, las 448
#  corridas devuelven cero filas y el banco anuncia que la interfaz esta rota.
#  Ha pasado cuatro veces, y cada una costo un rato de mirar codigo que estaba
#  bien. Un banco que no distingue "la app falla" de "no hay donde dibujarla"
#  no es un banco, es una fuente de sustos.
def display_alive():
    d = os.environ.get("DISPLAY", ":99")
    try:
        return subprocess.run(["xdpyinfo", "-display", d],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


#  EN PARALELO, Y CADA UNA CON SU CASA.
#
#  Las 644 corridas se lanzaban de una en una: cuarenta minutos de reloj con
#  catorce nucleos parados, y un banco que tarda cuarenta minutos se deja de
#  pasar. Lo que impedia lanzarlas juntas no era el servidor X -acepta tantos
#  clientes como haga falta- sino que TODAS escriben la misma sesion: la app
#  crea .sesion/samples y sus 64 WAV la primera vez que abre, y dos procesos
#  haciendolo a la vez es exactamente la carrera que Tests/session.py existe
#  para cazar. Cada corrida se lleva su HOME propio, asi que ProjectStore les
#  da carpetas distintas y no se pisan.
def run(size, lang, sheet, casa=None):
    env = dict(os.environ, ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG=lang,
               ZATI_OPEN=sheet, DISPLAY=":99")
    if casa:
        env["HOME"] = casa
        env["XDG_DATA_HOME"] = casa
        #  LA CAJA NEGRA SE VACIA ANTES DE CADA CORRIDA.
        #
        #  Cada trabajador reutiliza su HOME para sus corridas, y la app dice en
        #  su renglon de estado si la vez anterior no acabo bien - que es
        #  exactamente para lo que existe. Pero aqui "la vez anterior" es OTRA
        #  corrida del banco, asi que un tropiezo del andamio -la pantalla
        #  virtual cayendose un segundo- aparecia como un hallazgo de rotulo en
        #  la corrida siguiente: "SQUEEZE ... CAIDA senal 06 en ficha ... needs
        #  273 has 248". Un banco que informa de su propio andamio ensena a no
        #  leerlo, que es lo que ya paso con la caja de ruta del navegador.
        try:
            os.remove(os.path.join(casa, "Music", "ZATI", "zati-bitacora.txt"))
        except OSError:
            pass
    try:
        out = subprocess.run([BIN], env=env, capture_output=True, timeout=180).stdout.decode("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    rows = []
    for line in out.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try: rows.append(json.loads(line))
            except Exception: pass
    return rows or None

def judge(rows, size, lang, sheet):
    findings = []
    #  LA RAIZ SE BUSCA, no se da por hecho que es la primera linea.
    #
    #  Era `rows[0]`, y eso hacia que CUALQUIER linea JSON impresa antes del
    #  volcado dejara W y H en cero - y con la ventana a cero, los cuarenta
    #  componentes de cada corrida caen "fuera de ventana": 45 521 hallazgos de
    #  golpe, todos falsos, tapando los de verdad. Lo pago una linea de
    #  arranque de tres campos.
    head = next((r for r in rows if r.get("root")), {})
    W, H = head.get("w", 0), head.get("h", 0)
    comps = [r for r in rows if "path" in r]

    for r in comps:
        tag = f"{size}/{lang}/{sheet or 'face'}"
        #  0. Las rejillas que se pintan enteras: su celda tambien se toca.
        #  Y la TERCERA, que faltaba. El piano roll se pinta entero igual que
        #  las otras dos y su fila no la media nadie: salia a 7.4 px apaisado
        #  y a 10.8 en un 360x640. Con suelo propio y mas alto que el de las
        #  otras porque la fila de una nota es un blanco de ARRASTRE y fallarla
        #  no falla el toque - escribe la nota de al lado, y sin decirlo.
        #  Y el piano lleva DOS suelos y no uno, que es lo que las otras dos no
        #  necesitan. A lo ancho se elige el PASO -la misma pregunta que hace
        #  la rejilla de pasos, con el mismo suelo-; a lo alto se elige la
        #  NOTA, y fallar de fila no falla el toque: escribe otro tono, suena,
        #  y no lo dice nadie. Por eso el suelo vertical es mas alto.
        for grid, cols, lanes, gutter, sx, sy in (("StepGrid",  16, 16, 30, MIN_CELL, MIN_CELL),
                                                  ("Playlist",   8,  4, 26, MIN_CELL, MIN_CELL),
                                                  ("PianoRoll", 16, 13, 26, MIN_CELL, MIN_NOTE)):
            if grid in r["path"] and r["w"] > gutter and r["h"] > 0:
                cw = (r["w"] - gutter) / cols
                ch = r["h"] / lanes
                if cw < sx or ch < sy:
                    findings.append(("CELDA", tag,
                                     f'{grid} {cw:.0f}x{ch:.0f} px por celda', min(cw, ch)))
        #  0. VISIBLE Y DE CERO PIXELES, que es el punto ciego de todas las
        #     demas: las seis reglas de abajo se saltan lo que mide 0x0 -y con
        #     razon, porque la casa APAGA lo que no cabe *y* le vacia los
        #     limites-, asi que un control que se pone visible y se queda sin
        #     colocar pasa las seis. Lo pago la vista previa del troceado, que
        #     salio a 0x0 en las tres pantallas porque se anadio el componente
        #     sin sumar su alto a lo que se pide, y las 756 corridas dieron
        #     cero hallazgos. Y lo pago antes SEGUIR, que llevaba desde el
        #     primer dia visible y de 0x0.
        #
        #     El volcado solo trae lo VISIBLE -UiAudit::walk se rinde con
        #     isVisible() falso-, asi que una linea aqui con w o h en cero es
        #     exactamente eso: encendido y sin sitio. Media regla -apagar sin
        #     vaciar- la ven las otras; esta ve la otra media.
        #
        #     Y LA LISTA VACIA DE JUCE NO ES NUESTRA, igual que la caja de ruta
        #     del navegador: un juce::ListBox sin filas deja su ListViewport y
        #     el componente de contenido con alto cero, y eso pasa en PROYECTOS
        #     -sin proyectos guardados- y en el navegador. Son 99 hallazgos de
        #     tres clases de JUCE, y de paso son la prueba de fuego de esta
        #     regla: dispara, lee bien el volcado y llega a componentes que
        #     miden 0 - que era justo lo que hacia falta saber, porque lo que
        #     esta regla existe para cazar -la vista previa del troceado a 0x0-
        #     ya no esta ahi para verlo fallar.
        juceLista = r["path"].split("/")[-1].endswith(("ListBoxE", "ListViewportE",
                                                       "oredComponent", "oredComponentE"))
        if (r["w"] <= 0 or r["h"] <= 0) and not juceLista:
            findings.append(("CERO", tag,
                             f'{r.get("text") or r["path"].split("/")[-1]} {r["w"]}x{r["h"]}', 0))
        # 1. A finger has to fit.
        if r.get("hit") and r.get("on"):
            if r["w"] < MIN_TOUCH or r["h"] < MIN_TOUCH:
                findings.append(("TOUCH", tag, f'{r.get("text","?")} {r["w"]}x{r["h"]}', min(r["w"], r["h"])))
        # 2. Nothing may be laid out off the window.
        if r["w"] > 0 and r["h"] > 0 and not r.get("scrolled"):
            if r["x"] < -1 or r["y"] < -1 or r["x"] + r["w"] > W + 1 or r["y"] + r["h"] > H + 1:
                findings.append(("OFFSCREEN", tag, f'{r.get("text",r["path"].split("/")[-1])} @{r["x"]},{r["y"]} {r["w"]}x{r["h"]}', 0))
        # 3. Type that has to be squeezed to fit.
        #    drawFittedText squeezes to 0.9 horizontal scale and may wrap to a
        #    second line before it gives up, so this is not "invisible text" —
        #    it is a cap whose lettering no longer matches the cap beside it,
        #    which is precisely what reads as amateur on a stand.
        #  LA CAJA DE RUTA DEL NAVEGADOR NO ES NUESTRA. Es la de
        #  juce::FileBrowserComponent, mide 22 px de alto y ensena la ruta
        #  entera; con el banco en paralelo cada corrida tiene su HOME temporal
        #  y la ruta pasa de 24 caracteres a 47, asi que el hallazgo crecia con
        #  el nombre del directorio de pruebas y no con la app. Un banco que
        #  informa de su propio andamio ensena a no leerlo.
        if "needW" in r and r["haveW"] > 0 and not r.get("text", "").startswith("/"):
            over = r["needW"] - r["haveW"]
            if over > 0.5:
                kind = "TRUNC" if r["needW"] > r["haveW"] / 0.9 else "SQUEEZE"
                findings.append((kind, tag, f'"{r.get("text","")}" needs {r["needW"]:.0f} has {r["haveW"]:.0f}', -over))

    # 4. Interactive siblings must not overlap. Only siblings: a sheet sits
    #    over the face by design, and comparing across layers reports the
    #    design as a bug a thousand times over.
    hits = [r for r in comps if r.get("hit") and r["w"] > 0 and r["h"] > 0]
    par = lambda r: r["path"].rsplit("/", 1)[0]
    #  AGRUPADOS POR PADRE ANTES DE COMBINAR, no despues. Comparar todos contra
    #  todos y tirar el 99% por no ser hermanos es O(n^2) sobre el arbol entero
    #  -en la ficha de la mezcla son medio millon de parejas para mirar unas
    #  pocas miles- y era lo que hacia que juzgar costase mas que correr la app.
    familias = collections.defaultdict(list)
    for r in hits: familias[par(r)].append(r)
    for hermanos in familias.values():
        for a, b in itertools.combinations(hermanos, 2):
            ox = min(a["x"]+a["w"], b["x"]+b["w"]) - max(a["x"], b["x"])
            oy = min(a["y"]+a["h"], b["y"]+b["h"]) - max(a["y"], b["y"])
            if ox > 1 and oy > 1:
                findings.append(("OVERLAP", f"{size}/{lang}/{sheet or 'face'}",
                                 f'{a.get("text",a["path"].split("/")[-1])} x {b.get("text",b["path"].split("/")[-1])} by {ox}x{oy}', 0))
    return findings


# ---------------------------------------------------------------------------
#  5. UNTRANSLATED TEXT.
#
#  Two readouts printed the Spanish word in all four languages ("recto", the
#  bar count) and the three tabs of the SETTINGS card - the card that CONTAINS
#  the language selector - were never retranslated at all. None of it was
#  caught, because every rule above judges GEOMETRY: a Spanish caption in an
#  English build fits its cap perfectly.
#
#  The test is comparative and needs no dictionary: lay out the same component
#  in Spanish and in English and compare the string at the same PATH. If it did
#  not change, either it went through T() and the two languages agree - which
#  is legitimate and lives in OK below - or it never went through T() at all.
#  A row that is genuinely identical is a one-line entry here; a row that is
#  not is a bug, and it is one line of output instead of nobody noticing.
#
#  PUNTO CIEGO CONOCIDO: esto compara el TEXTO DE LOS COMPONENTES, porque es lo
#  que UiAudit vuelca. El texto pintado a mano en un paint() no tiene componente
#  y por lo tanto no se mide. Asi sobrevivieron los dieciocho nombres de
#  parametro de los seis efectos - CUTOFF, RESO, FREQ, DRIVE, TIME, FBK, BITS,
#  RATE, SIZE, DAMP - en espanol y en las cuatro compilaciones, en la cara de la
#  maquina. Se encontraron leyendo fxDefs, no corriendo esto. Mientras el
#  volcado no lleve tambien lo que se pinta, esta prueba cubre los rotulos de
#  los controles y no los de la pintura.
UNTRANSLATED_OK = {
    # A number, a unit, a symbol, a path, a file name.
    # (handled by the regex below)
    "ZATI",                                    # the wordmark
    "L", "R", "C", "M", "S", "A", "B", "D",    # channel, pan and bank letters
    "FLT", "ISO", "HPF", "DRV", "DLY", "BIT", "REV",  # effect abbreviations
    "TAP", "KIT",                              # universales en cualquier sampler
    "MANUAL",                                  # se escribe igual en las dos lenguas
    "MIDI",                                    # es una sigla, y es la misma en todo el mundo
    "XY",                                      # los dos ejes se llaman igual en todas partes
    "PADS", "PAD", "SEC", "MIX", "SET", "SONG", "REC", "PLAY", "STOP", "LOAD",
    "RACK", "TEST", "AUDIO", "AUTOCUT", "AUTO CHOP", "SWING", "off",
    "PIANO",                                   # el instrumento se llama igual en las dos
    "PAD -", "PAD +",                          # PAD pasa por T() y coincide de verdad en es/en
    "OCT -", "OCT +",                          # la abreviatura de octava es la misma
    "MONO",                                    # se dice igual en las dos lenguas
    "PRESETS",                                 # se dice igual en las dos lenguas
    "PACK -", "PACK +",                        # la palabra es la misma en las dos lenguas
    "MASTER",                                  # la mezcla final se llama igual en las dos
    "WAV", "OGG",                              # los dos formatos, que son extensiones de fichero
    "TOUR",                                    # la palabra es la misma en las dos lenguas
    "SINTES",                                  # el pack se llama asi, como ZATI
    "OFF",                                     # el extremo apagado de un mando, universal en un aparato
    "PAPEL", "GRAFITO", "ACERO", "LACA",       # the four chassis, named not translated
    "ESPANOL", "ENGLISH",                      # each language names itself
    "file:",
    "\u4e2d\u6587", "\u0627\u0644\u0639\u0631\u0628\u064a\u0629",   # each language names itself, in itself
}
#  A number with a unit welded to it - "0 st", "120 bpm", "2 ms", "0 c" - is
#  the same string in every language and always will be.
UNTRANSLATED_UNIT = re.compile(r'^[+\-]?[0-9][0-9.,]*\s*(st|c|ms|s|bpm|dB|Hz|kHz|%|x)?$', re.I)
UNTRANSLATED_SAFE = re.compile(r'^[\s0-9%.,:;+\-/|×xX\u00b7\u00b0"\'()\[\]_@#]*$')

#  UN ROTULO PINTADO NO PUEDE CAER DEBAJO DE UN CONTROL.
#
#  Las reglas de geometria recorren el arbol de COMPONENTES y un titulo no es
#  un componente: se dibuja. Por eso "AJUSTES - AUDIO" podia pasar por debajo de
#  la tapa de CUADRAR y por debajo de la x de cerrar sin que 812 corridas
#  dijeran nada. La app apunta el rectangulo que el texto OCUPA -no la banda que
#  se le dio, que suele ser el ancho entero- asi que un solape aqui es un solape
#  de verdad.
def judge_tapado(rows, size, lang, sheet):
    out = []
    ctrl = [r for r in rows
            if r.get("kind") in ("button", "slider", "editor")
            and r.get("w", 0) > 0 and r.get("h", 0) > 0]
    for r in rows:
        if not r.get("rotulo"):
            continue
        rx, ry, rw, rh = r["x"], r["y"], r["w"], r["h"]
        if rw <= 0 or rh <= 0:
            continue
        for c in ctrl:
            #  Solo dentro de la MISMA ficha. La cara sigue debajo de una
            #  ficha abierta con todas sus tapas maquetadas, asi que comparar
            #  todo contra todo daba 417 hallazgos y casi todos eran una
            #  tarjeta opaca encima de la maquina.
            if c.get("capa", 0) != r.get("capa", 0):
                continue
            ix = min(rx + rw, c["x"] + c["w"]) - max(rx, c["x"])
            iy = min(ry + rh, c["y"] + c["h"]) - max(ry, c["y"])
            #  Dos pixeles de tolerancia: una banda que acaba justo donde
            #  empieza una tapa no es un rotulo tapado, es un rotulo pegado.
            if ix > 2 and iy > 2:
                out.append(("TAPADO", f"{size}/{lang}/{sheet or 'face'}",
                            f'"{r["rotulo"]}" debajo de "{c.get("text","?")}"', 0))
                break
    return out


def judge_lang(rows_es, rows_en, size, sheet):
    if not rows_es or not rows_en: return []
    def m(rows):
        return {r["path"]: r["text"] for r in rows if r.get("path") and r.get("text")}
    es, en = m(rows_es), m(rows_en)
    datos = {r["path"] for r in rows_es if r.get("dato")}
    out = []
    for path, t in es.items():
        #  LO QUE VIENE DE FUERA NO SE TRADUCE. El nombre de un instrumento
        #  sale de una carpeta del disco: es identico en los cuatro idiomas por
        #  definicion, y contarlo daba 168 hallazgos falsos que tapaban los de
        #  verdad - entre ellos uno REAL, los cuatro bancos de fabrica, que se
        #  listaban sin pasar por T(). La app marca el que es dato.
        if path in datos: continue
        if t in UNTRANSLATED_OK or UNTRANSLATED_SAFE.match(t): continue
        if UNTRANSLATED_UNIT.match(t): continue
        if "/" in t or t.startswith("P") and t[1:].isdigit(): continue
        if en.get(path) == t:
            out.append(("UNTRANSLATED", f"{size}/{sheet or 'face'}", f'"{t}" identical in es and en', 0))
    return out

def una_pagina(combo):
    """Un recorrido de paginas. Devuelve el culpable, o None si esta limpio."""
    size, lang = combo
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": size, "ZATI_LANG": lang,
                 "ZATI_DEMO": "1", "ZATI_PAGES": "1"})
    try:
        out = subprocess.run([BIN], env=env, capture_output=True,
                              text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        return "no contesto"
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"paginas"' in linea:
            d = json.loads (linea)
            if d.get ("solapes", 0) or d.get ("fuera", 0):
                return d.get ("culpable", "?")
    return None


#  Una corrida entera dentro de UN proceso: lanzar la app, leer su volcado y
#  juzgarlo. Juzgar es lo que cuesta -es O(n^2) en hermanos por la regla de los
#  solapes- y por eso viaja con la corrida en vez de volver al proceso padre.
#  Solo se devuelven las filas cuando hacen falta para la prueba comparativa de
#  idioma, que es la unica que necesita el volcado entero de vuelta.
def corre_y_juzga(combo, casa):
    size, lang, sheet = combo
    rows = run(size, lang, sheet, casa)
    if rows is None:
        return [], None, (0, 0)
    #  CUANTAS TAPAS LLEVAN DIBUJO Y CUANTAS LO ENSENAN.
    #
    #  El icono es el adorno y la palabra la funcion, asi que donde no caben
    #  los dos sale la palabra (ver ZatiLookAndFeel::reparteTapa). Eso esta
    #  bien y es invisible: sin contarlo, "los iconos no salen en el movil
    #  estrecho" y "los iconos no salen" son la misma corrida en verde.
    puestos = sum (1 for r in rows if "icono" in r)
    pintados = sum (1 for r in rows if r.get ("icono"))
    return (judge(rows, size, lang, sheet) + judge_tapado(rows, size, lang, sheet),
            (rows if lang in ("es", "en") else []), (puestos, pintados))


def paginas():
    """CAMBIAR DE PAGINA, que es lo que ningun arranque limpio hace.

    Este banco abre UNA ficha por proceso, y por eso no vio que las tapas de
    la pagina PASO se quedaban con sus coordenadas al volver a PASOS -
    dibujadas encima de la fila de COMPAS, comiendose sus toques. Un control
    que no se maqueta en la pagina que se ve conserva el sitio de la anterior,
    y eso solo aparece si CAMBIAS de pagina.

    La app recorre la lista entera tres veces, ida y vuelta, y mira las dos
    reglas que no dependen del idioma despues de cada cambio. Ida y vuelta
    porque el fallo es de RESIDUO: solo se ve al volver a una pagina que ya se
    habia dejado."""
    peor = []
    trabajos = int(os.environ.get("ZATI_TRABAJOS", 0)) or max(1, min(16, os.cpu_count() or 4))
    combos = [(size, lang) for size, _ in SIZES for lang in LANGS]
    with concurrent.futures.ThreadPoolExecutor(max_workers=trabajos) as pool:
        for (size, lang), culpa in zip(combos, pool.map(una_pagina, combos)):
            if culpa is not None:
                peor.append ((size, lang, culpa))
    return peor


def main():
    if not os.path.exists(BIN):
        sys.exit("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit("la pantalla virtual %s no responde: sin ella las 448 corridas "
                 "salen vacias y parece que la app esta rota.\n"
                 "    Xvfb :99 -screen 0 1920x1080x24 &" % os.environ.get("DISPLAY", ":99"))

    only = sys.argv[1:]
    allf = []
    pairs = collections.defaultdict(dict)
    iconos = collections.defaultdict(lambda: [0, 0])
    runs = fails = 0

    combos = [(size, lang, sheet)
              for size, _ in SIZES for lang in LANGS for sheet in SHEETS
              if not only or any(o in f"{size}{lang}{sheet}" for o in only)]

    #  Tantas a la vez como nucleos. No menos: cada corrida se pasa la mayor
    #  parte de su vida arrancando -sintetizar los 64 sonidos, montar el arbol,
    #  hablar con el servidor X- y eso no satura un nucleo, asi que dejar uno
    #  libre "por si acaso" es dejar el banco a la mitad de velocidad por nada.
    #  ZATI_TRABAJOS lo fuerza, para poder medir el banco contra si mismo.
    trabajos = int(os.environ.get("ZATI_TRABAJOS", 0)) or max(1, min(16, os.cpu_count() or 4))
    casas = tempfile.mkdtemp(prefix="zati-banco-")
    try:
        #  PROCESOS y no hilos: medido, el tiempo no se iba en la app -una
        #  corrida entera son 0.9 s- sino en JUZGARLA aqui, y el reparto por
        #  hilos deja eso en un solo nucleo por el GIL. Con hilos, 92 corridas
        #  tardaban 5 min 36 con 5 min 58 de CPU en un nucleo: el banco estaba
        #  esperandose a si mismo. Cada proceso corre la app Y la juzga.
        with concurrent.futures.ProcessPoolExecutor(max_workers=trabajos) as pool:
            futuros = {}
            for i, c in enumerate(combos):
                casa = os.path.join(casas, "c%02d" % (i % trabajos))
                os.makedirs(casa, exist_ok=True)
                futuros[pool.submit(corre_y_juzga, c, casa)] = c
            for fut in concurrent.futures.as_completed(futuros):
                size, lang, sheet = futuros[fut]
                findings, rows, ico = fut.result()
                iconos[size][0] += ico[0]
                iconos[size][1] += ico[1]
                runs += 1
                if rows is None:
                    fails += 1
                    #  Y SE DICE QUIEN FUE, DONDE SE CUENTA Y NO DONDE SE JUZGA.
                    #
                    #  Sin servidor X, JUCE se cae en Component::centreWithSize
                    #  antes de que exista una ventana: la corrida no vuelca, y
                    #  "una corrida vacia" se lee como un fallo de la app. El
                    #  Xvfb de este contenedor se muere solo -seis veces en una
                    #  sesion- y han costado tres tandas de mirar codigo que
                    #  estaba bien.
                    #
                    #  El primer intento lo puso en corre_y_juzga y no servia
                    #  de nada: esta rama devuelve rows None y el que llama
                    #  hace `continue` antes de mirar los hallazgos. Un aviso
                    #  que nadie lee es lo mismo que no ponerlo. Roto a
                    #  proposito con la pantalla abajo: "no dump - SIN PANTALLA
                    #  VIRTUAL en :99".
                    quien = ("no dump — crash or hang" if display_alive()
                             else "no dump — SIN PANTALLA VIRTUAL en :99: JUCE se cae "
                                  "en centreWithSize y esto NO es un fallo de la app")
                    allf.append(("CRASH", f"{size}/{lang}/{sheet or 'face'}", quien, 0))
                    continue
                allf += findings
                if lang in ("es", "en"): pairs[(size, sheet)][lang] = rows
    finally:
        shutil.rmtree(casas, ignore_errors=True)
    for (size, sheet), d in pairs.items():
        allf += judge_lang(d.get("es"), d.get("en"), size, sheet)
    by = collections.Counter(f[0] for f in allf)
    print(f"\n=== {runs} runs, {fails} produced nothing ===")
    print("findings:", dict(by))
    # Group identical messages across the matrix so one bug is one line.
    grouped = collections.defaultdict(list)
    for kind, tag, msg, sev in allf: grouped[(kind, msg)].append(tag)
    for (kind, msg), tags in sorted(grouped.items(), key=lambda kv: (kv[0][0], -len(kv[1]))):
        print(f"{kind:9} x{len(tags):<3} {msg}   [{tags[0]}{' +'+str(len(tags)-1) if len(tags)>1 else ''}]")

    #  Los iconos de las tapas, por pantalla. No es una regla -que un icono no
    #  quepa no es un fallo, es la escalera de siempre- pero sin el numero no
    #  hay forma de distinguir "aqui no caben" de "no hay iconos".
    print()
    print("iconos dibujados por pantalla (de las tapas que llevan uno asignado):")
    for size, _ in SIZES:
        p, d = iconos[size]
        if p:
            print("  %-9s %4d de %4d   %3.0f%%" % (size, d, p, 100.0 * d / p))

    #  Y el residuo al cambiar de pagina, que ninguna de las 476 corridas de
    #  arriba puede ver porque cada una abre una ficha y se va.
    resto = paginas()
    print()
    if resto:
        print("RESIDUO AL CAMBIAR DE PAGINA:")
        for size, lang, quien in resto:
            print(f"  {size}/{lang}  {quien}")
    else:
        print("cambiar de pagina no deja nada colocado donde no toca")

main()

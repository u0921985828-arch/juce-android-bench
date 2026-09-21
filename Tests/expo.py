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
    #  Y LOS DOS DE EN MEDIO, que es donde el apaisado se parte en dos ramas.
    #  `wideFace` pide ancho >= padCol + faceColumn + aire, o sea ~556 px de
    #  area segura: por encima hay segunda cara y por debajo se cae a la rama
    #  vertical con 360 px de alto. El barrido tenia 915x412 -holgado- y nada
    #  entre las dos ramas, asi que el borde no lo medía nadie.
    ("640x360",  "LANDSCAPE de un telefono pequeño — justo sobre el umbral"),
    ("412x480",  "pantalla partida, la que el propio codigo cita"),
]
LANGS = ["es", "en", "zh", "ar"]
SHEETS = ["", "plato", "songm", "pads", "pad2", "pad3", "sec", "secp", "paso", "eq", "eqb", "song", "piano", "pianod", "pianosel", "pick", "mix", "xy", "set", "asp", "proj", "gest", "midi", "midf", "lang", "manual", "mixc", "canal", "rack", "rackf", "ranura", "ranural", "preset", "preseteq", "chop", "inst", "instd", "instg", "vst", "vstm", "expo", "tour", "tour1", "tour3", "tour6", "tour10", "tourf", "browse", "browsedir",
#  Y LA MISMA MAQUINA CON TRABAJO DENTRO. Todo lo de arriba se mide con
#  un proyecto vacio o con el kit de fabrica, y casi todo lo que un
#  rotulo puede romper solo aparece lleno: un nombre de pad que es el
#  del fichero que cargaste, una linea de tiempo de sesenta y cuatro
#  compases repartiendo la misma celda, una lista de PROYECTOS con
#  filas de verdad y el renglon de continuidad con sus tres campos.
#  Es un ESTADO y no una regla nueva, como `ZATI_SKIN` con la carcasa.
                    "llena", "llena-song", "llena-sec",
                    "llena-piano", "llena-proj", "llena-mix"]

#  EL CONTRATO DE LA ANATOMIA, importado de quien lo lee. `Tests/maqueta.py`
#  parsea `Tests/maqueta.md` y lo resuelve contra la tabla de `Metrics`; aqui
#  solo se usa. Escribir los valores otra vez serian dos contratos, que es lo
#  que ya costo la Z de `marcaCelda` saliendo en rojo contra un icono correcto.
from maqueta import contrato, tokens as _tokensMetrics                  # noqa: E402

CONTRATO = {}    # lo llena main() antes de la primera corrida

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
#  Y LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: la constante vive en
#  `kits.py`, al lado de `display_alive`, y no se vuelve a escribir aqui. Este
#  fichero llevaba `DISPLAY=":99"` clavado en `run()` mientras la comprobacion
#  leia el entorno — la misma regla con dos numeros, que es como `cpu.py` y
#  `instr.py` acabaron dando el veredicto entero en rojo con la app perfecta.
from kits import PANTALLA, display_alive                           # noqa: E402


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
               ZATI_OPEN=sheet, DISPLAY=PANTALLA)
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
            #  Y SOLO SE TRAGA LO QUE NO ES JSON.
            #
            #  Era `except Exception`, asi que una clave repetida -que es lo que
            #  el hook levanta- se habria comido la linea entera en silencio: la
            #  cadena de control se habria tragado a si misma. Lo que se ignora
            #  es una linea que no es un objeto; lo demas sube y para la tanda.
            try: rows.append(json.loads(line, object_pairs_hook=_sin_repetir))
            except json.JSONDecodeError: pass
    return rows or None


#  NINGUNA LINEA DEL VOLCADO REPITE UNA CLAVE.
#
#  El espacio de claves de este volcado es PLANO y no lo vigilaba nadie, y se
#  pago TRES VECES en una tarde al anadir la fila de radio: `on` ya significaba
#  «habilitado», `lit` es la marca de `litAccent`, y `fila` es una LINEA entera
#  del volcado. `json.loads` no protesta -al parsear gana la ultima- asi que las
#  dos primeras habrian funcionado dejando a la regla de al lado midiendo otra
#  cosa, en silencio. La tercera si reventaba, y solo porque `judge_fila`
#  reconoce sus lineas por la presencia de la clave.
#
#  Una clave, un significado. Se comprueba al parsear, que es donde se puede.
class ClaveRepetida(Exception):
    pass


def _sin_repetir(pares):
    d = {}
    for k, v in pares:
        if k in d:
            raise ClaveRepetida(
                f"FALLA el volcado repite la clave «{k}» en una linea: "
                "una clave, un significado (ver UiAudit::walk)")
        d[k] = v
    return d

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
    LADO = head.get("icoLado", 0)
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
        #  Y LAS TRES CIFRAS LAS DICE LA APP, no este fichero.
        #
        #  Estaban escritas a mano -16x16, 8x4 y 16x13- aqui y otra vez en
        #  planos.py, y eso valia mientras ninguna rejilla pudiera cambiar de
        #  tamano sola. Desde que las tres tienen ventana continua y zoom,
        #  medir con la cuenta de ayer es medir OTRA rejilla: la de pasos
        #  ensena las columnas que entren a celda cuadrada, el piano va de
        #  ocho a treinta y dos y la linea de tiempo de cuatro a dieciseis.
        #  Es la misma decision que `Iconos::kLadoMin` y la marca `valor`.
        for grid, sx, sy in (("StepGrid",  MIN_CELL, MIN_CELL),
                             ("Playlist",  MIN_CELL, MIN_CELL),
                             ("PianoRoll", MIN_CELL, MIN_NOTE)):
            if grid not in r["path"] or "cols" not in r:
                continue
            #  Y LA CELDA ES LA DIBUJADA, no `(ancho - canal) / columnas`: la
            #  rejilla de pasos pinta cuadrado y deja lo que sobra sin usar
            #  -para eso esta la barra-, asi que la division da un numero que
            #  nadie pinta. Lo dice la app.
            cw, ch = float (r["cw"]), float (r["ch"])
            if cw > 0 and ch > 0:
                if cw < sx or ch < sy:
                    findings.append(("CELDA", tag,
                                     f'{grid} {cw:.0f}x{ch:.0f} px por celda', min(cw, ch)))

        #  Y LA CELDA DE PASOS ES CUADRADA, que es lo que se pidio y lo unico
        #  que ninguna otra regla puede ver: una celda de 20x36 se maqueta
        #  perfecta -no solapa, no se sale, no lleva rotulo, esta traducida- y
        #  se falla al tocarla porque el dedo apunta a un cuadrado.
        #
        #  Es el estado de ARRANQUE: los dos zooms son sueltos a proposito, asi
        #  que la cuadratura se exige con el zoom sin tocar. Un pixel de margen
        #  porque el ancho sale de una division entera.
        if "StepGrid" in r["path"] and "cw" in r:
            cw, ch = float (r["cw"]), float (r["ch"])
            if cw > 0 and ch > 0 and abs (cw - ch) > 1.0:
                findings.append(("CUADRADA", tag,
                                 f'la celda de pasos es {cw:.1f}x{ch:.1f}', abs (cw - ch)))

        #  Y LO QUE SOBRA POR EL FILO DERECHO. SE IMPRIME Y NO SE JUZGA.
        #
        #  Llego en una foto -«la rejilla deja una columna partida a la
        #  derecha»- y que asome es DELIBERADO: es lo unico que dice que la
        #  ventana no llega al final del patron, y `paint` dibuja `numCols()+1`
        #  justo para eso. Lo que no decidia nadie es CUANTO, porque
        #  `stepGrid.setBounds (inner)` no acota `inner` a un multiplo del lado
        #  de la celda.
        #
        #  Medido en las siete pantallas: la del piano y la de la cancion
        #  reparten su ancho entre un numero fijo de columnas y les sobran
        #  0.08 px; la de pasos es la unica con celda CUADRADA y le sobra de
        #  1.00 a 14.70 px, o sea del 8% al 96% de una celda.
        #
        #  Y no se juzga porque no hay nada que este mal: el gesto acota a
        #  `numCols()`, asi que esa columna partida se DIBUJA y se TOCA con el
        #  mismo indice y escribe el paso que enseña. Acotarla costaria la unica
        #  señal de que hay mas a la derecha - y esa señal ya tiene dueño, la
        #  barra que va justo debajo. Lo que hacia falta es que no pueda cambiar
        #  sin que nadie se entere.
        if "StepGrid" in r["path"] and "cw" in r and "canal" in r:
            sobra = r["w"] - r["canal"] - r["cols"] * float (r["cw"])
            findings.append(("FILO", tag,
                             "a la rejilla de pasos le sobran %.0f px por el filo "
                             "derecho (%.0f%% de celda)"
                             % (sobra, 100.0 * sobra / max (1.0, float (r["cw"]))),
                             sobra))
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
        # 1b. TODOS LOS DIBUJOS MIDEN LO MISMO.
        #
        #     Ninguna de las once reglas anteriores puede verlo: un icono de
        #     catorce pixeles dentro de una tapa de cuarenta no solapa, no se
        #     sale, no corta su rotulo, no mide cero y esta traducido. Y habia
        #     SIETE tamanos a la vez -13, 14, 15, 16, 17, 18 y 26-, o sea el
        #     mismo trazo leyendose mas cerca o mas lejos segun la fila.
        #
        #     El lado NO se escribe aqui: lo publica la app en su linea raiz.
        #     Escrito en los dos sitios seria el numero de ayer el dia que suba,
        #     que es exactamente el fallo que esta regla existe para cazar.
        if LADO and r.get("icono") and r.get("icoW", 0) > 0:
            if r["icoW"] != LADO or r["icoH"] != LADO:
                findings.append(("SPRITE", tag,
                                 f'{r["icono"]} {r["icoW"]}x{r["icoH"]} y el lado es {LADO}', 0))
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
    "FLT", "ISO", "HPF", "DRV", "DLY", "BIT", "REV", "EQ",  # effect abbreviations
    "CMP", "GTE", "DSS", "LIM",                # la familia de dinamica, tres letras cada una
    "CHO", "FLA", "PHA", "TRM",                # la de modulacion, igual: tres letras
    "RNG", "PIT", "WID", "EXC", "TRN", "FRZ",  # la de caracter, igual
    "WAH", "OCT",   # y los dos que se pidieron, por lo mismo: son tres letras
    "AMB",          # el ambiente, el que cerro el reparto por tipos: tres letras
    "FRM", "FLD", "ROT", "PNG", "DUC", "REP",   # los seis que llevan el
                    # catalogo a cinco por familia, por lo mismo: tres letras
    "EXTRAS",       # la puerta del contenido: la palabra es la misma en las dos
    "RATIO",                                   # se escribe igual en las dos lenguas
    "FREQ",                                    # la abreviatura de frecuencia, la misma
    "AUTO",                                    # la abreviatura de automatizacion, igual en las cuatro
    "TAP", "KIT",                              # universales en cualquier sampler
    "MANUAL",                                  # se escribe igual en las dos lenguas
    "MIDI",                                    # es una sigla, y es la misma en todo el mundo
    "XY",                                      # los dos ejes se llaman igual en todas partes
    "PADS", "PAD", "SEC", "MIX", "SET", "SONG", "REC", "PLAY", "STOP", "LOAD",
    "RACK", "TEST", "AUDIO", "AUTOCUT", "AUTO CHOP", "SWING", "off",
    "SOLO",                                    # la palabra que lleva escrita cualquier mesa
    "PIANO",                                   # el instrumento se llama igual en las dos
    "PAD -", "PAD +",                          # PAD pasa por T() y coincide de verdad en es/en
    "OCT -", "OCT +",                          # la abreviatura de octava es la misma
    "MONO",                                    # se dice igual en las dos lenguas
    "PRESETS",                                 # se dice igual en las dos lenguas
    "PACK -", "PACK +",                        # la palabra es la misma en las dos lenguas
    "SEL",                                     # la abreviatura de seleccion/select es la misma
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
#  Y LOS NOMBRES DE PRESET DE EFECTO, que NO se traducen a proposito: son
#  nombres propios, igual que `Sintes::Preset::nombre` y que los veintitres
#  nombres de efecto que ya estan aqui arriba. Traducir «PLACA» lo convierte en
#  otro preset.
#
#  SE LEEN DEL FUENTE Y NO SE ESCRIBEN AQUI. Ciento quince nombres copiados a
#  mano son ciento quince sitios donde esta lista se queda vieja el dia que uno
#  se renombra, y entonces la regla dice que hay un rotulo sin traducir donde
#  solo hay un nombre cambiado. Es lo mismo que `marcas.py` hace con `fxDefs`:
#  se parsean, **se cuentan contra lo que el fuente declara**, y si la cuenta no
#  sale la prueba FALLA en vez de seguir con media lista.
def _presetsDeFabrica ():
    raiz = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
    inc  = os.path.join (raiz, "Source", "FxPresets.inc")
    hdr  = os.path.join (raiz, "Source", "FxPresets.h")
    eng  = os.path.join (raiz, "Source", "AudioEngine.h")
    try:
        texto = open (inc, encoding="utf8").read()
        cab   = open (hdr, encoding="utf8").read()
        mot   = open (eng, encoding="utf8").read()
    except OSError:
        return None

    #  `{ "NOMBRE", numero, ...` — el mismo ancla que marcas.py usa con fxDefs,
    #  y con el espacio admitido dentro del nombre: «OCHO BITS» y «MAS GOLPE»
    #  son dos palabras.
    nombres = re.findall (r'\{\s*"([A-Z0-9 ]{1,16})"\s*,\s*[-0-9]', texto)

    mEsc = re.search (r'kEscritos\s*=\s*(\d+)', cab)
    mFx  = re.search (r'kNumFx\s*=\s*(\d+)', mot)
    if mEsc is None or mFx is None:
        return None
    esperados = int (mEsc.group (1)) * int (mFx.group (1))
    if len (nombres) != esperados:
        return None            # la cuenta no sale: que lo diga el veredicto

    #  Y el cero, que no esta en la tabla porque se deriva de `kFxDef`.
    return set (nombres) | {"DEFECTO"}


#  Y LOS QUE `Lang.cpp` DECLARA IGUALES EN LAS DOS LENGUAS, leidos de la tabla
#  y no copiados aqui. El comentario de arriba ya parte el caso en dos: o el
#  texto paso por `T()` y las dos lenguas coinciden -legitimo- o no paso por
#  `T()` -fallo-. Lo unico que sabe cual de los dos es, es la propia tabla de
#  traduccion, asi que se le pregunta a ella.
#
#  Hizo falta al entrar las ocho familias nuevas: `FM`, `SYNC` y `PIANOS` se
#  escriben igual en espanol y en ingles y salieron 39 veces como sin traducir
#  con la app perfecta. Escribirlas a mano aqui habria sido la tercera tabla
#  que dice lo mismo que `Lang.cpp` -y la que se queda vieja el dia que a
#  `PIANOS` se le ponga un ingles propio, callando un fallo de verdad-.
def _declaradosIguales ():
    raiz = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
    try:
        texto = open (os.path.join (raiz, "Source", "Lang.cpp"), encoding="utf8").read()
    except OSError:
        return None

    #  `{ "CLAVE", "es o vacio", "en", "zh", "ar" }`. Con el segundo campo
    #  vacio el espanol ES la clave, que es como esta escrita casi toda la
    #  tabla.
    fila = re.compile (r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,'
                       r'\s*"((?:[^"\\]|\\.)*)"\s*,')
    iguales = set()
    for clave, es, en in fila.findall (texto):
        if en and en == (es or clave): iguales.add (en)
    return iguales or None


DECLARADOS_IGUALES = _declaradosIguales ()
if DECLARADOS_IGUALES is None:
    print ("FALLA: no se pudo leer Lang.cpp; esta regla no mide nada asi")
    sys.exit (1)
UNTRANSLATED_OK |= DECLARADOS_IGUALES


PRESETS_FABRICA = _presetsDeFabrica ()
if PRESETS_FABRICA is None:
    print ("FALLA: no se pudieron leer los presets de FxPresets.inc; "
           "esta regla no mide nada asi")
    sys.exit (1)
UNTRANSLATED_OK |= PRESETS_FABRICA

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

    #  Y DOS COSAS MAS QUE UN ROTULO PINTADO PUEDE HACER Y NADIE MEDIA.
    #
    #  TAPADO mira un rotulo contra un CONTROL. Faltaban las dos preguntas que
    #  se le hacen a cualquier rotulo que sea un componente desde hace tandas
    #  -TRUNC y SQUEEZE- y que a estos no se les podia hacer, porque `apunta`
    #  apuntaba el ancho que el texto OCUPA y ese va acotado a la banda: un
    #  texto que no cabe salia con el mismo `w` que uno que cabe justo.
    #
    #   - CORTADO: pide mas de lo que tiene. `pide` es el minimo al que se
    #     sigue leyendo entero, ya con el apreton del dibujo aplicado, y CERO
    #     es «se elide a proposito, no lo juzgues» - lo dice la app y no una
    #     lista de textos en el script, que solo sabria medir una de las cuatro
    #     compilaciones.
    #   - PISADO: dos rotulos pintados de la MISMA capa, uno encima de otro. La
    #     regla de solapes recorre componentes y un rotulo no lo es, asi que el
    #     renglon de ayuda de CANCION llevaba metido 46x16 px debajo de su
    #     propio titulo en las cuatro lenguas sin que nada fallara.
    #  Y EL FILTRO PEDIA `w > 0`, QUE ES JUSTO EL CASO QUE HAY QUE CAZAR.
    #
    #  Un rotulo con ancho CERO se caia de la lista antes de llegar a CORTADO,
    #  asi que la unica regla que pregunta «¿cabe?» no veia al que no cabe NADA.
    #  Medido: **89 rotulos pintados con ancho cero** en cuatro de los siete
    #  tamaños y los cuatro idiomas — el titulo de las cuatro pantallas de SEC,
    #  su renglon de cadena, y el del PIANO en chino. En el telefono la ficha
    #  SEC se abre SIN TITULO y ninguna de las diecinueve reglas duras podia
    #  verlo: un rectangulo de ancho cero no solapa, no se sale, no se corta a
    #  media palabra y no encoge la letra. Se publicaba, y `plano.py` contestaba
    #  que la ficha tiene titulo porque el rotulo existe en el volcado.
    #
    #  El filtro se escribio para saltarse lo que no se pinta, y de paso se
    #  tragaba lo que se pinta en cero pixeles. Lo que dice «esto no es un
    #  rotulo» es el ALTO; el ancho es lo que se esta midiendo. La exencion de
    #  elidir sigue valiendo sola: `pide == 0` nunca dispara `pide > w`.
    pin = [r for r in rows if r.get("rotulo") and r.get("h", 0) > 0]
    for r in pin:
        if r.get("pide", 0) > r["w"]:
            out.append(("CORTADO", f"{size}/{lang}/{sheet or 'face'}",
                        f'"{r["rotulo"]}" pide {r["pide"]} tiene {r["w"]}', r["w"] - r["pide"]))
    for i, a in enumerate(pin):
        for b in pin[i + 1:]:
            if a.get("capa", 0) != b.get("capa", 0):
                continue
            ix = min(a["x"] + a["w"], b["x"] + b["w"]) - max(a["x"], b["x"])
            iy = min(a["y"] + a["h"], b["y"] + b["h"]) - max(a["y"], b["y"])
            if ix > 2 and iy > 2:
                out.append(("PISADO", f"{size}/{lang}/{sheet or 'face'}",
                            f'"{a["rotulo"]}" sobre "{b["rotulo"]}" {ix}x{iy} px', 0))
    return out


#  EL AIRE ENTRE FILAS HERMANAS. Se imprime, no se juzga.
#
#  «Que todos los apartados de todas las pestanas guarden la relacion de aire
#  entre componentes logica con el demas aire y distancia que hay» — y eso hoy
#  no lo mide nadie: las once reglas miden que todo QUEPA y ninguna mira a que
#  distancia queda de lo de al lado.
#
#  Medido antes de escribir un liston, que es lo que separa esto de inventarse
#  uno: **227 huecos verticales entre hermanos y quince valores distintos** —0,
#  4, 6, 8, 10, 13, 14, 16, 19, 22, 24, 25, 28, 29, 33—. Los de la escala de
#  `Metrics` (4, 8, 12, 16, 24) son mayoria y el resto es lo que deja un
#  `removeFromTop` despues de repartir, que no es un fallo: una fila elastica
#  acaba donde acaba.
#
#  Un numero que no separa el fallo del caso legitimo NO puede ser un veredicto
#  —es la leccion de TARJETA, y antes la del porcentaje de iconos— asi que se
#  publica para que una regresion se vea como un numero que cambia.
#
#  Y DICE QUIEN, que es lo que faltaba para poder hacer algo con el.
#
#  «Sigo viendo espacio de mas entre ciertas secciones, y luego otras que no hay
#  espacio entre ellas» — y el histograma daba la razon con 15 073 huecos en 24
#  valores, con el tercer monton mas grande en CERO (x1312). Pero un monton no se
#  puede arreglar: hay que saber CUALES. Es la leccion que esta casa ya pago con
#  el residuo al cambiar de pagina, que paso de «8 solapes» a `1 @35,320 39x36`.
#
#  Y AGREGADO POR PAR, que el mismo par sale en siete pantallas por cuatro
#  idiomas y son un solo sitio del fuente.
#
#  ------------------------------------------------------------------------
#  Y DOS NUMEROS Y NO UNO, que es lo que hacia que estos 27 valores no se
#  pudieran arreglar.
#
#  Aqui decia «los dos extremos y no todos: el CERO y lo que pase de un
#  `Metrics::lg`. La banda de en medio son los dos valores de la escala y no hay
#  nada que mirar». Es una AFIRMACION SIN MEDIDA y medida es falsa: de los
#  16 489 huecos de la corrida, **7 357 -el 45 %- caen en esa banda y NO son de
#  la escala**, y el monton mas grande de todo el histograma vive justo ahi
#  -14 px x4883, el 30 % del aire de la app-. O sea que la lista de culpables
#  era ciega precisamente donde estaba la masa.
#
#  Y la causa de los 27 valores es aritmetica. Lo que se publicaba era
#      hueco_de_maqueta + aire_de_pintado_de_arriba + aire_de_pintado_de_abajo
#  porque el descuento de abajo -que existe por una buena razon, ver mas
#  abajo- se aplicaba ANTES de contar. Una fila de 40 px con una tapa deja
#  (40 - 40*0.75)/2 = 5 px por lado, asi que:
#
#      14 x4883 = maqueta  4 (Metrics::xs) + 5 + 5      dos tapas de 40
#      18 x1046 = maqueta  8 (Metrics::sm) + 5 + 5
#      10 x944  = maqueta  0                + 5 + 5
#       8 x3850 = maqueta  8                + 0 + 0      dos filas de <= 26
#       4 x1748 = maqueta  4                + 0 + 0
#      13/15/5/9 = maqueta 8/10/0/4         + 5 + 0      UNA tapa y la otra no
#
#  Los impares no eran un `/ 2` perdiendo un pixel: son los pares en los que
#  solo un lado es una tapa. Y ninguna de las dos preguntas se podia contestar
#  con la suma:
#
#    - **CUANTO SE SEPARO** es una decision de MAQUETA, la escribe una linea de
#      `resized()`, tiene un dueño por sitio y se puede contrastar contra la
#      escala de `Metrics`. Es `crudo`.
#    - **CUANTO SE VE** es lo que el ojo nota en el telefono y es donde vive la
#      queja. Es `visto`, el de siempre.
#
#  Es la misma figura que `ensureDirectory` y que `origin`: *lo que importa no
#  es lo que devuelve la orden sino donde acabo el fichero*. Mezcladas en un
#  numero, 27 valores que parecian caos y son cuatro huecos de maqueta vistos a
#  traves de un descuento que depende del alto de cada vecino.
#
#  Y LA LISTA DEJA DE ESTAR CORTADA. Estaba en `[:24]`, y las 24 que salian eran
#  todas `x28` -7 pantallas por 4 idiomas, o sea un solo sitio del fuente cada
#  una-: 672 huecos mostrados de los 3 226 que cumplian la condicion, el **79 %
#  oculto**. Un monton no se puede arreglar: hay que saber CUALES.
def quienEs(r):
    #  EL INDICE DE HERMANO Y LA CLASE — `root/64:e10TextButtonE` sale «64
    #  TextButton» — que es lo unico que nombra un control y aguanta la
    #  agregacion.
    #
    #  Y NO el rotulo, que fue el primer intento y salio medido: el rotulo esta
    #  TRADUCIDO, asi que el mismo par del fuente sale con cuatro nombres y su
    #  cuenta se parte en cuatro. Con eso, el monton de CERO dejaba ver 56 de
    #  1312 y los otros 1256 caian por debajo del corte de la lista. Es la
    #  leccion que esta casa ya pago con la marca `valor` de los iconos: una
    #  regla escrita sobre ROTULOS solo sabe medir una de las cuatro
    #  compilaciones.
    #
    #  Y EL INDICE SE QUEDA, que la segunda version lo tiraba «porque cambia
    #  con la maqueta» — una AFIRMACION SIN MEDIDA, y medida es falsa: es la
    #  posicion en el array de hijos del padre, o sea el orden de
    #  `addAndMakeVisible` en el constructor, asi que no lo mueven ni el tamano
    #  ni el idioma. Comprobado con `pick` en 412x915 y 280x653 por `es` y
    #  `ar`: las cuatro corridas dan `64 65 66 67` para A B C D. Sin el, las
    #  dos mitades de un par salen las dos como «TextButton» y la lista dice
    #  que hay dos filas de tapas sin decir CUALES.
    hoja = r["path"].rsplit("/", 1)[-1]
    m = re.match(r"^(\d+):", hoja)
    idx = m.group(1) if m else "?"
    #  El nombre de la clase viene MANGLED (`e10TextButtonE`, `N4juce6SliderE`):
    #  cada tramo es su largo seguido del identificador. Se quedan los tramos,
    #  que es lo legible.
    #  Se lee en ORDEN y no con un `findall`, que fue el primer intento y
    #  salio medido: `[A-Za-z_]\w*` es codicioso, asi que en `N4juce6SliderE`
    #  se traga `juce6SliderE` entero y el segundo tramo se pierde — «juce» en
    #  vez de «juce::Slider».
    resto, tramos = hoja[len(idx) + 1:], []
    i = 0
    while i < len(resto):
        j = i
        while j < len(resto) and resto[j].isdigit(): j += 1
        if j == i: i += 1; continue
        n = int(resto[i:j])
        tramos.append(resto[j:j + n])
        i = j + n
    return ("%s %s" % (idx, "::".join(tramos) or hoja))[:22]


#  LA ESCALA DE `Metrics` -xs, sm, md, lg, xl- mas el cero, que es pegar dos
#  filas a proposito. Un hueco de MAQUETA que no este aqui lo escribio alguien a
#  mano. Se LEE de `Metrics` y no se copia, que es lo que `ANATOMIA` ya hace con
#  `Tests/maqueta.md`: dos listas de numeros son dos contratos, y la segunda es
#  la que un dia se queda con la escala de ayer.
#
#  Y con CADENA DE CONTROL: si la tabla no se puede leer, esto se queda en `[0]`
#  y entonces TODO hueco saldria «fuera de la escala» — una lista de mil
#  culpables que no mide nada. `main()` lo comprueba y para, igual que hace con
#  el contrato de la anatomia.
def _escala():
    met, _ = _tokensMetrics()
    if not met:
        return None
    vals = [met.get(k) for k in ("xs", "sm", "md", "lg", "xl")]
    return None if any(v is None for v in vals) else sorted({0} | set(vals))


ESCALA = _escala() or [0]


def mide_aire(rows, quien=None, ficha="", size=""):
    #  DOS contadores: `visto` es lo que el ojo ve -con el descuento del
    #  pintado- y `crudo` es lo que la maqueta separo. Ver el bloque de arriba.
    aire  = collections.Counter()
    crudo = collections.Counter()
    comps = [r for r in rows if "path" in r and r.get("hit") and r["w"] > 0 and r["h"] > 0]
    #  Y LO QUE SE PINTA EN MEDIO, QUE NO ES AIRE.
    #
    #  Tercera vez que la misma leccion sale en esta funcion: *se mide lo que se
    #  DIBUJA y no lo que se reserva*. Un hueco de 22 px entre dos filas de `sec`
    #  parecia un numero a mano fuera de la escala y salio en las cuatro
    #  pantallas medidas -393x851, 412x915, 360x640 y 800x1280- identico, o sea
    #  escrito. Y esta escrito, pero no es aire: es
    #      inner.removeFromTop (Metrics::sm);                 //  8
    #      inner.removeFromTop (Metrics::bandaSubtitulo);     // 14  <- rotulo
    #  o sea ocho de aire y catorce de un ROTULO PINTADO, que no es un
    #  componente y por eso no estaba en la cuenta. Medir eso como «22 px de
    #  aire» y mandarlo a la escala habria movido un rotulo para cuadrar un
    #  numero. Eran 680 huecos, el 18 % de todo lo que salia fuera de escala.
    #
    #  Asi que un hueco con algo pintado dentro NO se cuenta: no es aire, esta
    #  ocupado. Se mira en la MISMA capa, que la cara sigue maquetada debajo de
    #  cada ficha.
    #
    #  Y SOLO POR LA VERTICAL, que el primer intento pedia ademas solape
    #  HORIZONTAL con el par y salio medido: en `xy` el hueco de 22 px queda
    #  entre MOMENTANEO -al final de la fila del titulo, en x 231..319- y la
    #  ultima de las seis tapas de efecto -x 309..361-, y el renglon que lo
    #  llena, «entra al tocar y sale al soltar», se pinta en x 30..202. El
    #  rotulo llena la banda de lado a lado como concepto y su TINTA no llega
    #  hasta la derecha, asi que el solape horizontal daba -29 y el hueco salia
    #  como aire. Una banda reservada para un rotulo no es aire para ninguna
    #  pareja de esa franja, llegue la tinta donde llegue: el aire se mide por
    #  la vertical y la ocupacion tambien.
    pintado = [r for r in rows
               if not r.get("hit") and r.get("h", 0) > 0 and r.get("w", 0) > 0
               and "x" in r and "y" in r]
    porCapa = collections.defaultdict(list)
    for r in pintado:
        porCapa[r.get("capa", 0)].append(r)
    fam = collections.defaultdict(list)
    for r in comps:
        fam[r["path"].rsplit("/", 1)[0]].append(r)
    for hermanos in fam.values():
        if len(hermanos) < 2:
            continue
        #  Una FILA son los hermanos que comparten banda de y: el aire entre dos
        #  tapas de la misma fila es horizontal y lo decide `layoutModuleBar`
        #  repartiendo por el texto, asi que no es el aire del que se habla.
        #  Y EL FILO DE UNA FILA ES EL DE LO QUE SE DIBUJA, no el de lo que se
        #  reserva. Una tapa se pinta tres cuartos de alta y centrada, asi que
        #  deja `aire` px vacios por arriba y otros tantos por abajo dentro de
        #  su rectangulo — la app lo publica, ver UiAudit::walk. Sin descontarlo
        #  esta cuenta mide la RESERVA: dos filas de tapas pegadas salian a CERO
        #  con diez pixeles a la vista, y esa sola pareja de la cara era 1232 de
        #  los 1312 huecos «a cero» del histograma. Es el mismo descuento que
        #  `ctrlSeamTop` hace para colocar las palabras grabadas.
        #  Cada fila lleva CUATRO filos y no dos: los de lo dibujado -con el
        #  descuento- y los del rectangulo que la maqueta reservo. El agrupado
        #  en filas se hace por lo DIBUJADO, que es como estaba: dos controles
        #  se leen como una fila si sus tintas comparten banda, no si sus
        #  reservas se rozan.
        filas = []
        for r in sorted(hermanos, key=lambda r: (r["y"], r["x"])):
            ar = r.get("aire", 0)
            arriba, abajo = r["y"] + ar, r["y"] + r["h"] - ar
            if filas and arriba < filas[-1][1]:
                filas[-1][1] = max(filas[-1][1], abajo)
                filas[-1][3] = max(filas[-1][3], r["y"] + r["h"])
                filas[-1][5] = r
            else:
                filas.append([arriba, abajo, r["y"], r["y"] + r["h"], r, r])
        for a, b in zip(filas, filas[1:]):
            hueco = b[0] - a[1]
            #  Por encima de un dedo ya no es aire entre filas, es una fila que
            #  falta o una banda pintada en medio.
            if not 0 <= hueco <= 48:
                continue
            #  El de MAQUETA sale de los filos sin descontar. Puede ser
            #  negativo -dos reservas que se pisan mientras sus tintas no-, y
            #  entonces no es aire: es un solape que ya cazan OVERLAP y PISADO,
            #  asi que no se cuenta aqui como si fuera un hueco.
            seco = b[2] - a[3]
            if seco < 0:
                continue
            #  ¿Hay algo PINTADO dentro del hueco? Entonces no es aire, y no
            #  entra en NINGUNO de los dos histogramas: un rotulo entre dos
            #  filas no lo ve el ojo como espacio ni lo escribio nadie como tal.
            if any(p["y"] >= a[3] and p["y"] + p["h"] <= b[2]
                   for p in porCapa.get(a[4].get("capa", 0), ())):
                continue
            aire[hueco] += 1
            crudo[seco] += 1
            #  Y AHORA SE APUNTAN TODOS, que la condicion de antes
            #  -`hueco == 0 or hueco > 16`- dejaba sin nombre los 7 357 de la
            #  banda de en medio. El corte se hace al IMPRIMIR, donde se sabe
            #  contra que; aqui se recoge.
            #
            #  Y CON LA PANTALLA EN LA CLAVE, que es lo que separa un numero
            #  ESCRITO de un sobrante ELASTICO sin inventarse un liston: un
            #  hueco que vale lo mismo en las siete pantallas lo escribio
            #  alguien; uno que cambia con la ventana es lo que quedo despues de
            #  repartir, y *una fila elastica acaba donde acaba*.
            if quien is not None:
                quien[(ficha or "cara", quienEs(a[4]), quienEs(b[5]),
                       seco, size)] += 1
    return aire, crudo


#  LO QUE UNA TARJETA PIDE Y LO QUE HAY. Se imprime, no se juzga.
#
#  Las nueve anteriores miden el SINTOMA. `sheetFromBottom` recorta al tope de
#  la tarjeta con un `jmin` que no se queja, asi que lo que falta se lo come en
#  silencio lo ULTIMO que se maqueta: la fila de CADENA, la REJILLA de la pagina
#  PATRON saliendo a 217x0, las cuatro tapas de CARCASA a 4 px de alto y la
#  celda de la linea de tiempo cayendo de 20.2 px a 9. Cuando lo que se cae es
#  un control, lo cazan CERO o TOUCH o CELDA; cuando es un texto pintado o el
#  aire entre dos filas, no lo caza nadie.
#
#  Aqui se mide la CAUSA, que la app publica desde el sitio donde se decide. Una
#  ficha que se DESPLAZA -o que trae su propia lista dentro- puede pedir lo que
#  quiera; una que no, no.
#
#  Y NO SE JUZGA, que es lo que la primera corrida obligo a decir en voz alta.
#  Salio con NOVENTA Y DOS y ni uno era un fallo: son CINCO fichas -el piano,
#  las dos paginas del pad, CANCION y el troceado- que piden su deseo entero y
#  dejan que el recorte se lo coma un elemento ELASTICO con suelo propio. Lo
#  prueban las otras nueve reglas en la misma corrida: cero CERO, cero CELDA y
#  cero solapes, o sea que nada acabo en cero ni por debajo de su suelo. Un
#  numero que no separa el fallo del deseo no puede ser un veredicto - se
#  imprime, como TOUCH y como el porcentaje de iconos, para que una regresion se
#  vea como un numero que cambia. Lo que si vale de esto es que las cinco cifras
#  no las habia visto nadie: el piano pide 672 px donde la tarjeta da 663 en un
#  movil grande y 370 apaisado.
def judge_tarjeta(rows, size, lang, sheet):
    out = []
    for r in rows:
        if not r.get("tarjeta"):
            continue

        #  Y LO QUE LA TARJETA DEJA VER DEBAJO, que ESTA SI SE JUZGA.
        #
        #  El tope de pie dejo de ser un porcentaje y pasa a derivarse de una
        #  condicion medida: que por debajo asome un PAD ENTERO, que desde la
        #  tanda de `onFuera` ademas se puede tocar -`Sheet::onFuera` lo dispara
        #  y lo selecciona-. Con el 0.90 clavado en 412x915 quedan 29 px de pad
        #  asomando y `tocaPadDetras` deja de poder acertarse: se cambiaria una
        #  funcion medida por unos pixeles, y ninguna de las once reglas lo veria
        #  porque el pad esta colocado, entero y en su sitio — lo que le falta es
        #  estar TAPADO por la tarjeta, y una tarjeta encima no es un solape.
        #
        #  Las dos cifras las dice la APP: el hueco que quedo con la tarjeta ya
        #  colocada -y no «ventana menos alto», que la tarjeta se centra- y el
        #  suelo que ese hueco tiene que cumplir, que apaisado vale cero porque
        #  alli no hay pad debajo que proteger.
        libre, suelo = r.get("libre", -1), r.get("suelo", 0)
        if libre >= 0 and suelo > 0 and libre < suelo:
            out.append(("ASOMA", f"{size}/{lang}/{sheet or 'face'}",
                        f'la tarjeta deja {libre} px por debajo y el pad pide {suelo}',
                        suelo - libre))

        if r.get("desplaza"):
            continue
        if r["pedido"] > r["tope"]:
            out.append(("TARJETA", f"{size}/{lang}/{sheet or 'face'}",
                        f'pide {r["pedido"]} px y la tarjeta da {r["tope"]}',
                        r["pedido"] - r["tope"]))
    return out


#  EL PRESUPUESTO DE LA CARA CONTRA LO QUE LA CARA COLOCA.
#
#  Ninguna de las catorce reglas de arriba puede verlo, y por una razon de
#  fondo: un presupuesto que pide de mas no solapa, no se sale, no corta un
#  rotulo y no mide cero. Lo unico que hace es restarse de `freeH`, o sea
#  quitarle alto al CRISTAL -la unica banda de esa columna que puede dar- y
#  regalarselo a la rejilla de pads, que se lleva lo que sobre. El error se
#  cobra en la unica banda que la cara tiene para MIRAR.
#
#  Y se ha encontrado a mano dos veces, las dos leyendo el fichero: el
#  `Metrics::xs` entre las pestañas y el transporte, y el `Metrics::sm` de la
#  costura de EFECTOS. Con la tercera ya hay patron.
#
#  UNA CIFRA Y NO CATORCE, porque el elastico es el testigo: todo lo demas de
#  la columna tiene alto fijo, asi que un termino de mas o de menos en
#  CUALQUIER banda aterriza en los pads y en ningun otro sitio. `apretada` dice
#  si el cristal se quedo clavado en su suelo -la cara sobre-suscrita-, y ahi
#  los pads absorben el deficit A PROPOSITO: esa cifra es la escalera y no un
#  descuadre, asi que se imprime y no se juzga.
def judge_cara(rows, size, lang, sheet):
    out = []
    for r in rows:
        if not r.get("cara"):
            continue
        #  Y LA CARA SOBRE-SUSCRITA SE IMPRIME Y NO SE JUZGA: ahi el cristal
        #  se queda clavado en su suelo y los pads absorben el deficit A
        #  PROPOSITO, o sea que esa cifra es la escalera y no un descuadre. Sin
        #  imprimirla, «cero CARA» no distingue una cara cuadrada de una que no
        #  se mide nunca.
        if r.get("apretada"):
            out.append(("APRETADA", f"{size}/{lang}/{sheet or 'face'}",
                        (f'el cristal se queda en su suelo y los pads absorben '
                         f'{-r["sobra"]} px de deficit' if r["sobra"] < 0 else
                         f'el cristal se queda en su suelo y a los pads les '
                         f'sobran {r["sobra"]} px'),
                        0))
            continue
        if r["sobra"] != 0:
            out.append(("CARA", f"{size}/{lang}/{sheet or 'face'}",
                        (f'la cara reserva {r["sobra"]} px de mas: se los come la rejilla'
                         if r["sobra"] > 0 else
                         f'la cara coloca {-r["sobra"]} px mas de los que reserva'),
                        abs(r["sobra"])))
    return out


#  EL AIRE DEL MARCO: NADA DE UNA FICHA SE METE EN EL.
#
#  Es la tercera mitad de la queja -«o se olvida el aire del marco»- y la unica
#  de las tres que no medía nadie. Las otras dos resultaron ser la misma cosa:
#  el histograma medía la BANDA reservada y no lo dibujado, y con el aire de la
#  tapa descontado los 1312 «huecos a cero» se quedaron en CERO.
#
#  `sheetFromBottom` es la unica puerta: mete el contenido `margenFichaX` por
#  `margenFichaY` y devuelve ESE rectangulo, asi que la invariante se cumple por
#  construccion... mientras nadie expanda hacia fuera. Y la app expande en nueve
#  sitios -`row.expanded (Metrics::aireTapa, 0)` y sus hermanos- para que el
#  filo de una fila caiga donde el de sus vecinas, que es lo que pide la regla
#  FILA. Hoy las nueve se compensan con el `reduced` de cada tapa y no se comen
#  un pixel; una que se descompense no la ve ninguna de las otras doce, porque
#  dos pixeles de marco no solapan, no se salen de la ventana, no cortan un
#  rotulo, no miden cero y estan traducidos.
#
#  POR CAPA, que es lo que costo la primera medida: el tour NO pasa por
#  `sheetFromBottom` -su muelle se reduce contra su propio rectangulo, con los
#  mismos dos tokens- asi que sus tres tapas se comparaban con la tarjeta de la
#  ficha que el paso hubiera abierto y salian 60 px «por dentro del marco».
#  Catorce hallazgos y ninguno de la app. La tarjeta dice de que capa es y solo
#  juzga a los suyos, que es la misma pieza que hizo falta para el rotulo
#  tapado.
#
#  Y solo lo que NO se desplaza: dentro de un Viewport el contenido se sale a
#  proposito -para eso es un desplazamiento- que es lo que ya distingue
#  `scrolled` en la regla de fuera de ventana.
def judge_marco(rows, size, lang, sheet):
    out = []
    for t in rows:
        if not t.get("tarjeta") or t.get("w", 0) <= 0:
            continue
        capa = t.get("capa", 0)
        if capa <= 0:
            continue
        x0, y0 = t["x"] + t["marcoX"], t["y"] + t["marcoY"]
        x1, y1 = t["x"] + t["w"] - t["marcoX"], t["y"] + t["h"] - t["marcoY"]
        for r in rows:
            if "path" not in r or not r.get("hit") or r.get("scrolled"):
                continue
            if r.get("capa", 0) != capa or r["w"] <= 0 or r["h"] <= 0:
                continue
            #  El contenedor de la ficha no: `Sheet` cubre la ventana y dibuja
            #  la tarjeta dentro, asi que preguntarle si respeta su propio marco
            #  no significa nada.
            if r["w"] >= t["w"] or r["h"] >= t["h"]:
                continue
            for lado, v, marco in (("izq", r["x"] - x0, t["marcoX"]),
                                   ("der", x1 - (r["x"] + r["w"]), t["marcoX"]),
                                   ("arr", r["y"] - y0, t["marcoY"]),
                                   ("aba", y1 - (r["y"] + r["h"]), t["marcoY"])):
                if v < 0:
                    out.append(("MARCO", f"{size}/{lang}/{sheet or 'face'}",
                                f'"{(r.get("text") or quienEs(r))}" se mete {-v} px en el marco '
                                f'{lado} de la ficha, que declara {marco}', 0))
    return out


# ============================================================================
#  TODAS LAS FICHAS TIENEN LA MISMA ANATOMIA.
#
#  De las diecinueve reglas duras, NINGUNA comparaba una ficha con OTRA:
#  `MARCO`, `CABECERA`, `TARJETA`, `ASOMA`, `FILA` y los cuatro criterios de
#  `paneles.py` miden cada ficha contra si misma, asi que veintiuna anatomias
#  distintas -cada una coherente consigo misma- las pasaban las diecinueve. La
#  queja llego con esas palabras: «quiero que todos los pop ups guarden aire y
#  demas factores en proporciones muy similares».
#
#  El contrato lo dice `Tests/maqueta.md` y lo que vale cada token lo dice
#  `Metrics`; aqui se compara con lo que la app DIBUJO. Censado antes de poner
#  el liston, que es la regla de la casa y no una formalidad -`paneles.py` tiro
#  tres reglas por saltarsela-: el papel `titulo` salia con CUATRO alturas y no
#  porque la maqueta estuviera mal, sino porque tres cosas distintas
#  compartian el papel -el titulo de la ficha, la CHAPA de familia del
#  instrumento y el rotulo de un grupo de AUTO CHOP-. Un papel que significa
#  tres cosas no se puede juzgar: es la marca `valor` de los iconos contada
#  desde el otro lado. Con los tres papeles honestos, las cinco piezas salen a
#  UN valor cada una.
#
#  LA CARA NO ES UNA FICHA y se queda fuera por la capa, no por una lista: su
#  banda es la marca serigrafiada en el chasis -«ZATI SAMPLER», 24 px- y
#  compararla con el titulo de una tarjeta seria comparar dos cosas distintas.
#
#  Y EL ROTULO DE SECCION SE IMPRIME Y NO SE JUZGA. Tiene dos formas legitimas
#  y el volcado no las separa: encima de un grupo es una banda de
#  `bandaSubtitulo`, y al LADO de una fila de chips ocupa el renglon entero
#  -son los 40 px de las siete filas de AJUSTES-. Un numero que no separa el
#  fallo del caso legitimo no puede ser un veredicto, que es la leccion de
#  TARJETA y antes la del porcentaje de iconos.
# ============================================================================
#  Que papel del volcado es cada pieza del contrato.
PAPEL = {"titulo": "titulo", "subtitulo": "subtitulo", "dato": "pie"}


def judge_anatomia(rows, size, lang, sheet, piezas):
    out = []
    donde = f"{size}/{lang}/{sheet or 'face'}"
    for r in rows:
        capa = r.get("capa", 0)
        if capa <= 0:
            continue
        if r.get("tarjeta") and r.get("w", 0) > 0:
            for pieza in ("marcoX", "marcoY"):
                quiere = piezas[pieza][1]
                if r[pieza] != quiere:
                    out.append(("ANATOMIA", donde,
                                f'el marco {pieza[-1].lower()} de la ficha mide {r[pieza]} px '
                                f'y el contrato dice {quiere} (Metrics::{piezas[pieza][0]})', 0))
        if "rotulo" in r:
            pieza = PAPEL.get(r.get("tipo"))
            if pieza is None:
                continue
            tok, quiere = piezas[pieza]
            #  Y POR LINEA: el pie de la ficha del instrumento son tres
            #  renglones, o sea tres bandas. Cuantas son lo dice la app -lo
            #  publica `pintaAyuda`, que es quien las dibuja- y no una cuenta
            #  aqui: un banco que adivina el reparto no prueba el reparto.
            quiere *= max(1, r.get("lineas", 1))
            if r["h"] != quiere:
                out.append(("ANATOMIA", donde,
                            f'la banda de {pieza} mide {r["h"]} px y el contrato dice '
                            f'{quiere} (Metrics::{tok} x{max(1, r.get("lineas", 1))}): '
                            f'"{r["rotulo"][:28]}"', 0))
    return out


#  UNA FILA DE TAPAS LLENA EL RECTANGULO QUE SE LE DIO.
#
#  Cada tapa de una fila se recorta por los lados -es el hueco que la separa de
#  su hermana- y ese recorte SOBRA en los dos extremos, asi que la fila entera
#  acababa dos pixeles dentro. Medido en la cara a 412x915 antes del arreglo: el
#  cristal, los cuatro bancos y los dieciseis pads de 14 a 398, y las pestanas de
#  modulo, el transporte y los seis efectos de 16 a 396. Tres filos izquierdos en
#  la pantalla que no se puede evitar, y la queja llego con esas palabras -«hay
#  varios ligeros fallos de colocacion, revisalo en la barra de efectos».
#
#  NINGUNA de las once anteriores puede verlo: dos pixeles de margen no solapan,
#  no se salen, no cortan un rotulo, no miden cero y estan traducidos.
#
#  Y NO se pregunta «que todas las filas de la app compartan filo», que es lo
#  primero que sale y tiene excepciones legitimas -en RECORTE los cuatro
#  deslizadores empiezan 68 px dentro porque a su izquierda va el nombre de cada
#  uno, PINTADO, que es el falso positivo que `paneles.py` ya se comio-. Lo que
#  no tiene excepcion es esto: quien coloca una fila de tapas recibe un
#  rectangulo y tiene que llenarlo. Lo dice la APP -el maquetado apunta el que
#  se dio y la union de lo que puso- y no un script adivinando que filas son
#  hermanas.
def judge_fila(rows, size, lang, sheet):
    out = []
    for r in rows:
        if not r.get("fila"):
            continue
        dx, dr = r["dadaX"], r["dadaR"]
        px, pr = r["puestaX"], r["puestaR"]
        if px != dx or pr != dr:
            out.append(("FILA", f"{size}/{lang}/{sheet or 'face'}",
                        f'la fila y={r["y"]} recibio {dx}..{dr} y ocupa {px}..{pr}',
                        max(abs(px - dx), abs(pr - dr))))
    return out


#  UNA FILA DE CHIPS: UN GRUPO DE RADIO, Y UNA ENCENDIDA.
#
#  Llego en una foto del telefono y eran DOS fallos a la vez, los dos de la
#  misma raiz y ninguno visible para las catorce reglas de geometria: una fila
#  de chips con dos encendidos -o con ninguno- se maqueta perfecta, no solapa,
#  no se sale, no corta su rotulo, no mide cero y esta traducida. Es la familia
#  de los cinco fallos del compas del piano.
#
#  La pieza de JUCE que los explica, leida y no supuesta: `setToggleState (true)`
#  llama a `turnOffOtherButtonsInGroup` AUNQUE la notificacion sea
#  `dontSendNotification`, y lo hace ANTES de escribir su propio estado; y ese
#  barrido pasa la MISMA notificacion a las hermanas, o sea que con un dedo
#  -`sendNotification`- apagar a una hermana DISPARA su `onClick`.
#
#    - AJUSTES · AUDIO salia con los cuatro chips de TOMAS apagados: compartian
#      id 7312 Y padre con los dos del MONITOR, asi que para JUCE eran una fila
#      de seis, y el monitor -que siempre enciende uno- apagaba los cuatro siete
#      lineas mas abajo.
#    - MEZCLA salia con dos bancos encendidos: `showMixBank` escribia UNA tapa y
#      dejaba el resto al grupo, asi que el `onClick` espurio de la que se apaga
#      la volvia a encender antes de que la nueva tuviera su estado escrito.
#
#  DOS PREGUNTAS Y NO UNA, porque cada fallo pasa la del otro: con el 7312
#  puesto, el grupo tiene SEIS tapas y UNA encendida -o sea que «exactamente una
#  encendida» daba verde-, y con la mesa rota las dos filas son la misma. Asi
#  que se pregunta ademas de QUIEN es cada tapa, y eso lo dice la app
#  (`filaDeRadio`): es la marca `valor` de los iconos otra vez -una lista de
#  rotulos en Python solo sabria medir una de las cuatro compilaciones-.
#
#  Y EL LISTON SALE DE LA POBLACION. Censadas las 1400 corridas antes de
#  escribir el veredicto: dieciseis grupos, y TODOS con exactamente una
#  encendida salvo uno - las seis ranuras de efecto del XY, con CERO en las 28
#  corridas de esa ficha. Eso es correcto y no una excepcion inventada: desde
#  que las ranuras nacen vacias, un tipo que no esta puesto no tiene tapa que
#  encender. Lo marca la app y no una lista aqui.
#
#  Con cadena de control: si el barrido no encuentra ni un grupo con dos o mas
#  tapas visibles, FALLA en vez de dar verde sin haber mirado.
def judge_chips(rows, size, lang, sheet):
    out = []
    donde = f"{size}/{lang}/{sheet or 'face'}"
    grupos = collections.defaultdict(list)
    for r in rows:
        if "grupo" not in r:
            continue
        padre = r["path"].rsplit("/", 1)[0] if "/" in r["path"] else ""
        grupos[(padre, r["grupo"])].append(r)

    #  El grupo solo mira a los HERMANOS, asi que la clave es (padre, id): tres
    #  juegos de chips comparten el 5151 con tres padres distintos y eso es
    #  correcto por construccion.
    vistos = 0
    for (padre, grupo), chips in sorted(grupos.items()):
        if len(chips) < 2:
            continue
        vistos += 1
        filas = sorted({c.get("radio", "") for c in chips})
        if len(filas) > 1:
            out.append(("CHIPS", donde,
                        "las filas " + " y ".join(f"«{f}»" for f in filas)
                        + f" comparten el grupo {grupo}: JUCE las trata como UNA",
                        len(filas)))
            continue
        on = sum(c.get("toggle", 0) for c in chips)
        if on == 0 and all(c.get("radioVacia") for c in chips):
            continue
        if on != 1:
            out.append(("CHIPS", donde,
                        f'la fila «{filas[0]}» tiene {on} encendidas de {len(chips)}',
                        abs(on - 1)))
    return out, vistos


#  LA CABECERA DE UNA FICHA COMPARTE RENGLON CON SUS TAPAS.
#
#  Llego mirando la foto de la ficha de un pad: «el espacio entre el texto de
#  PAD 1 y el primer menu -donde pone 01, OIR y la x- esta mas abajo que la
#  parte de arriba del texto». Medido, no era de esa ficha: **DOCE PIXELES en
#  veintiuna**, mas 18 en XY y 9 en EXPORTAR.
#
#  El maquetado abre todas con `inner.removeFromTop (Metrics::hit)` y centra
#  sus tapas ahi -centro en y+20-; el PINTOR se inventaba su banda del MISMO
#  rectangulo con `removeFromTop (16)` -centro en y+8-. Cuatro numeros escritos
#  a mano (16, 14, 18 y 12) para un renglon que ya estaba reservado.
#
#  Y NINGUNA DE LAS TRECE REGLAS PODIA VERLO: el titulo no solapa a nadie
#  -`antesDe` lo aparta-, no se sale, no se corta, no mide cero y esta
#  traducido. Lo unico que le pasa es que no esta a la altura de su fila.
#
#  Se mide el BLOQUE de cabecera y no solo el titulo: donde hay subtitulo los
#  dos se mueven juntos y la pareja queda centrada, asi que cada linea cae
#  siete pixeles a un lado - que es lo que hace una cabecera de dos lineas en
#  cualquier aparato y no un desvio.
def judge_cabecera(rows, size, lang, sheet):
    out = []
    #  El titulo de la ficha es el de su CAPA, como en plano.py: la cabecera de
    #  la cara -«ZATI SAMPLER»- es un rotulo de tipo titulo y ademas el primero
    #  de la lista, asi que sin la capa toda ficha saldria comparada con el.
    rot = [r for r in rows if r.get("rotulo")]
    capa = max((r.get("capa", 0) for r in rot), default=0)
    #  Y SOLO DE UNA FICHA. La cara no tiene cabecera de ficha: su banda es la
    #  marca serigrafiada con su raya debajo, y apaisado la columna de la
    #  derecha empieza a la misma altura -los cuatro chips A B C D, 10..50-
    #  asi que «las tapas que cruzan la banda del titulo» los coge a ellos y
    #  saca veinte hallazgos de una fila que no es su fila. La cabecera de la
    #  cara ya la miran TAPADO y CORTADO.
    if capa == 0:
        return out
    tit = [r for r in rot if r.get("tipo") == "titulo" and r.get("capa", 0) == capa]
    if not tit:
        return out
    t = tit[0]
    t0, t1 = t["y"], t["y"] + t["h"]
    if t["h"] <= 0:
        return out

    #  Las tapas de SU renglon: las de la misma capa cuya banda vertical cruza
    #  la del titulo. Coger «la mas cercana» daba la de dos filas mas abajo en
    #  las fichas que no tienen ninguna en la cabecera.
    caps = [r for r in rows if r.get("path") and r.get("capa", 0) == capa
            and r.get("hit") and r.get("h", 0) > 0
            and r["y"] < t1 and (r["y"] + r["h"]) > t0]
    if not caps:
        return out
    fila0 = min(c["y"] for c in caps)
    fila1 = max(c["y"] + c["h"] for c in caps)

    #  Y EL BLOQUE ENTERO: los rotulos de la capa que caen dentro de ese
    #  renglon. El subtitulo de una ficha vive ahi y baja el centro de la
    #  pareja; medir solo el titulo pediria que la primera linea estuviese
    #  centrada, que es lo contrario de lo que una cabecera de dos lineas hace.
    bloque = [r for r in rot if r.get("capa", 0) == capa and r.get("h", 0) > 0
              and r["y"] < fila1 and (r["y"] + r["h"]) > fila0]
    b0 = min(r["y"] for r in bloque)
    b1 = max(r["y"] + r["h"] for r in bloque)

    desvio = abs((b0 + b1) / 2.0 - (fila0 + fila1) / 2.0)
    if desvio > 1.0:
        out.append(("CABECERA", f"{size}/{lang}/{sheet or 'face'}",
                    f'"{t["rotulo"]}" ocupa {b0}..{b1} y su fila de tapas {fila0}..{fila1}'
                    f' ({desvio:.0f} px)', int(desvio)))
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
    env.update ({"DISPLAY": PANTALLA,
                 "ZATI_AUDIT": "1", "ZATI_SIZE": size, "ZATI_LANG": lang,
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
    #  Y SI NO PUBLICO LA LINEA, NO ESTA LIMPIA: NO SE HA MEDIDO.
    #
    #  Esta funcion devolvia None -o sea «limpio»- por el mismo camino con el
    #  que sale una corrida buena y con el que sale una que no llego a abrir la
    #  ventana. Con la pantalla sin poner, las corridas del ciclado de paginas
    #  daban cero solapes sin haber recorrido una sola ficha: la regla del
    #  RESIDUO -la unica que caza un control que conserva las coordenadas de la
    #  pagina anterior- imprimia OK y no miraba nada. Es la cadena de control
    #  que `marcas.py` y `apk.py` ya tienen: si el barrido no ve lo que tiene
    #  que ver, FALLA.
    return "no publico paginas"


#  Una corrida entera dentro de UN proceso: lanzar la app, leer su volcado y
#  juzgarlo. Juzgar es lo que cuesta -es O(n^2) en hermanos por la regla de los
#  solapes- y por eso viaja con la corrida en vez de volver al proceso padre.
#  Solo se devuelven las filas cuando hacen falta para la prueba comparativa de
#  idioma, que es la unica que necesita el volcado entero de vuelta.
def corre_y_juzga(combo, casa):
    size, lang, sheet = combo
    #  LO QUE PLANTA ESTADO SE LLEVA SU PROPIA CASA.
    #
    #  Cada trabajador reutiliza su HOME -de ahi el paso de 5 min 36 a 1 min
    #  57- y eso vale mientras una corrida no deje nada escrito. `llena` deja:
    #  el hilo de sesion escribe treinta pads, ocho patrones y una cancion de
    #  sesenta y cuatro compases, y la corrida siguiente del mismo trabajador
    #  los RESTAURA. La primera tirada con la app llena lo canto en rojo:
    #  `SQUEEZE 43` con la cara diciendo «Sesion recuperada - sesion nocturna
    #  larga» en pantallas donde nadie habia abierto nada, y `CELDA 40` con la
    #  rejilla de pasos a 11 px en `sec` -no en `llena-sec`-. Ni un hallazgo
    #  era de la app: eran corridas midiendo el estado de la de antes.
    #
    #  Cuesta los 400 ms de la primera apertura -la fabrica se sintetiza otra
    #  vez- y son 224 corridas de 1456.
    #  Y `mixc` por lo mismo: para medir la pagina de CANALES hay que repartir
    #  los sesenta y cuatro pads por los dieciseis canales -si no, las dieciseis
    #  tiras dicen «0 PADS» y el renglon que mas puede romper un rotulo no se
    #  mide nunca-, y eso lo escribe la sesion igual que lo de `llena`. Sin casa
    #  propia, la corrida siguiente del mismo trabajador abriria con los pads
    #  repartidos y midiendo un estado que nadie puso.
    propia = None
    if sheet.startswith ("llena") or sheet == "mixc":
        propia = tempfile.mkdtemp (prefix="zati-llena-")
        casa = propia
    try:
        return _corre_y_juzga (combo, casa)
    finally:
        if propia:
            shutil.rmtree (propia, ignore_errors=True)


def _corre_y_juzga(combo, casa):
    size, lang, sheet = combo
    rows = run(size, lang, sheet, casa)
    if rows is None:
        return [], None, (0, 0), collections.Counter(), collections.Counter(), 0
    #  CUANTAS TAPAS LLEVAN DIBUJO Y CUANTAS LO ENSENAN.
    #
    #  El icono es el adorno y la palabra la funcion, asi que donde no caben
    #  los dos sale la palabra (ver ZatiLookAndFeel::reparteTapa). Eso esta
    #  bien y es invisible: sin contarlo, "los iconos no salen en el movil
    #  estrecho" y "los iconos no salen" son la misma corrida en verde.
    puestos = sum (1 for r in rows if "icono" in r)
    pintados = sum (1 for r in rows if r.get ("icono"))
    quien = collections.Counter()
    chips, chipsVistos = judge_chips(rows, size, lang, sheet)
    return (judge(rows, size, lang, sheet) + judge_tapado(rows, size, lang, sheet)
                                           + judge_anatomia(rows, size, lang, sheet, CONTRATO)
                                           + judge_tarjeta(rows, size, lang, sheet)
                                           + judge_marco(rows, size, lang, sheet)
                                           + judge_cara(rows, size, lang, sheet)
                                           + judge_fila(rows, size, lang, sheet)
                                           + judge_cabecera(rows, size, lang, sheet)
                                           + chips,
            (rows if lang in ("es", "en") else []), (puestos, pintados),
            mide_aire(rows, quien, sheet, size), quien, chipsVistos,   # (visto, crudo)
            collections.Counter((r["h"], sheet or "cara") for r in rows
                                if "rotulo" in r and r.get("tipo") == "seccion"
                                and r.get("capa", 0)))


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
                 "    Xvfb :99 -screen 0 1920x1080x24 &" % PANTALLA)

    #  EL CONTRATO, ANTES DE LAS 1456 CORRIDAS. Si no se puede leer o nombra
    #  un token que `Metrics` no tiene, `ANATOMIA` compararia contra un
    #  diccionario vacio y las 1456 saldrian verdes sin haber preguntado nada.
    global CONTRATO
    if _escala() is None:
        sys.exit("no puedo leer la escala de espaciado de Metrics: "
                 "el histograma de maqueta no mide nada")
    _met, _ = _tokensMetrics()
    piezas, huerfanos = contrato(_met) if _met else (None, None)
    if not piezas or huerfanos:
        sys.exit("no puedo leer el contrato de Tests/maqueta.md%s: ANATOMIA no mide nada"
                 % ("" if not huerfanos else
                    " (nombra tokens que Metrics no tiene: %s)"
                    % ", ".join(t for _p, t in huerfanos)))
    CONTRATO = piezas

    only = sys.argv[1:]
    allf = []
    pairs = collections.defaultdict(dict)
    iconos = collections.defaultdict(lambda: [0, 0])
    aire   = collections.Counter()
    crudo  = collections.Counter()
    quien  = collections.Counter()
    secciones = collections.Counter()
    runs = fails = chipsVistos = 0

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
                findings, rows, ico, aireRun, quienRun, chipsRun, secRun = fut.result()
                chipsVistos += chipsRun
                secciones += secRun
                iconos[size][0] += ico[0]
                iconos[size][1] += ico[1]
                aire  += aireRun[0]
                crudo += aireRun[1]
                quien += quienRun
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

    #  LA CADENA DE CONTROL DE CHIPS.
    #
    #  «Cero filas mal» y «no he mirado ninguna fila» dan la misma corrida en
    #  verde, y esta regla es la mas facil de dejar muda sin querer: basta con
    #  que alguien renombre la clave del volcado. Es lo mismo que hacen
    #  `marcas.py` cuando no puede leer sus reglas y `apk.py` cuando no ve el
    #  texto de la app.
    if not only and chipsVistos == 0:
        allf.append(("CHIPS", "control", "ni una fila de radio con dos o mas "
                     "tapas visibles: la regla no mide nada", 1))
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

    #  EL ROTULO DE SECCION, que se imprime y no se juzga. Tiene DOS formas
    #  legitimas y el volcado no las separa: encima de un grupo es una banda de
    #  `bandaSubtitulo`, y al LADO de una fila de chips ocupa el renglon entero
    #  -los 40 px de las siete filas de AJUSTES-. Un liston ahi suspenderia a la
    #  mitad de la app por maquetar bien, que es la leccion de TARJETA.
    if secciones:
        porAlto = collections.Counter()
        quienSec = collections.defaultdict(set)
        for (h, sh), k in secciones.items():
            porAlto[h] += k
            quienSec[h].add(sh)
        print()
        print("banda del rotulo de seccion (%d rotulos, %d alturas; se imprime, no se juzga):"
              % (sum(porAlto.values()), len(porAlto)))
        for h in sorted(porAlto):
            print("  %3d px x%-5d %s" % (h, porAlto[h], ", ".join(sorted(quienSec[h]))[:70]))

    #  Y EL AIRE ENTRE FILAS HERMANAS. DOS histogramas y no uno -ver mide_aire-:
    #  el de MAQUETA, que es el judiciable porque cada hueco lo escribe una
    #  linea de `resized()`, y el VISTO, que es lo que el ojo nota y sigue
    #  imprimiendose sin juzgarse porque depende del alto de los dos vecinos.
    def pinta(nombre, cont):
        print()
        print("aire vertical %s (%d huecos, %d valores distintos):"
              % (nombre, sum(cont.values()), len(cont)))
        print("  " + "   ".join("%d px x%d" % (h, n) for h, n in sorted(cont.items())))

    pinta("de MAQUETA entre filas hermanas", crudo)
    fuera = sum(n for h, n in crudo.items() if h not in ESCALA)
    total = max(1, sum(crudo.values()))
    print("  en la escala %s: %d de %d (%.0f%%); fuera: %d"
          % (ESCALA, total - fuera, total, 100.0 * (total - fuera) / total, fuera))
    pinta("VISTO entre filas hermanas (maqueta + lo que cada tapa deja al pintarse)", aire)

    #  LA LISTA, con el corte hecho AQUI y no al recoger -el `[:24]` de antes
    #  ocultaba el 79 %- y con los pares partidos en DOS FAMILIAS, que es lo
    #  unico que separa el fallo del caso legitimo sin inventarse un liston:
    #
    #    ESCRITO   el mismo hueco en las SIETE pantallas. Lo puso una linea de
    #              `resized()`, tiene un dueño, y tiene que valer un token.
    #    ELASTICO  el hueco cambia con la ventana. Es lo que quedo despues de
    #              repartir -la rejilla de pads centrada con
    #              `withSizeKeepingCentre`, el sobrante del presupuesto de la
    #              cara- y *una fila elastica acaba donde acaba*.
    #
    #  Medido, y por eso se escribe: el par `64 TextButton | 15 PadButton` sale
    #  a 9 px en unas pantallas y a 10 en otras -750 y 300 huecos- porque el
    #  resto de centrar la rejilla es impar; el par de `sec` sale a 22 en
    #  393x851, 412x915, 360x640 y 800x1280, identico, porque esta escrito. Sin
    #  esta division las dos salian en la misma lista y la primera no se puede
    #  arreglar: forzar el residuo a un token es dejar de centrar la rejilla.
    porPar = collections.defaultdict(lambda: [collections.Counter(), set()])
    for (fic, a, b, seco, size), n in quien.items():
        e = porPar[(fic, a, b)]
        e[0][seco] += n
        e[1].add(size)
    #  ESCRITO pide las DOS cosas: un solo valor Y haber salido en las SIETE
    #  pantallas. La primera version pedia solo lo primero y salio medido: el
    #  par `88 juce::Slider | 98 HoldButton` aparecia con «40 px, 2 de 7» en
    #  once fichas y se clasificaba como escrito, cuando es el sobrante elastico
    #  de la cara que en las otras cinco pantallas vale 33, 37 o 43 — no se ve
    #  cambiar porque en esas cinco el par ni siquiera llega a existir. Un valor
    #  constante medido en dos pantallas no es una constante: es una muestra de
    #  dos.
    escritos = {k: v for k, v in porPar.items()
                if len(v[0]) == 1 and len(v[1]) == len(SIZES)}
    elasticos = len(porPar) - len(escritos)
    malos = {k: v for k, v in escritos.items() if next(iter(v[0])) not in ESCALA}
    print()
    print("  %d pares: %d ESCRITOS (el mismo hueco en las %d pantallas)"
          " y %d sin demostrar constantes (cambian con la ventana, o no salen en"
          " las siete; no se juzgan)"
          % (len(porPar), len(escritos), len(SIZES), elasticos))
    if malos:
        print("  ESCRITOS fuera de la escala — %d huecos en %d pares:"
              % (sum(sum(v[0].values()) for v in malos.values()), len(malos)))
        print("    %-6s %-7s %-11s %-22s %-22s %s"
              % ("huecos", "maqueta", "pantallas", "arriba", "abajo", "ficha"))
        for (fic, a, b), (cnt, sizes) in sorted(
                malos.items(), key=lambda kv: (-sum(kv[1][0].values()), kv[0])):
            print("    x%-5d %3d px   %2d de %-4d %-22s %-22s %s"
                  % (sum(cnt.values()), next(iter(cnt)), len(sizes), len(SIZES),
                     a, b, fic))
    else:
        print("  todos los huecos ESCRITOS valen un token de la escala")

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

    #  Y EL VEREDICTO, QUE ES LO QUE ESTE FICHERO NO TENIA.
    #
    #  Ochocientas noventa y seis corridas, siete contadores que SKILL.md
    #  declara "cero es cero"... y `main()` terminaba sin `sys.exit`, o sea que
    #  el banco principal de esta app devolvia CERO pasara lo que pasara. Lo
    #  juzgaba un ojo humano leyendo texto, que es exactamente lo que esta casa
    #  llama "una linea que imprime OK" - y lo llevaba escrito el fichero que
    #  mas cita esa regla.
    #
    #  TOUCH no cuenta: el dedo por debajo del minimo es una escalera conocida
    #  -seis efectos por cuarenta no caben en un Fold cerrado- y esta medido con
    #  su cifra. Lo que no puede pasar de cero es lo demas.
    duros = [k for k in ("TRUNC", "SQUEEZE", "OVERLAP", "OFFSCREEN", "CELDA",
                         "UNTRANSLATED", "CERO", "TAPADO", "SPRITE", "CORTADO", "PISADO",
                         "FILA", "CUADRADA", "ASOMA", "CABECERA", "MARCO", "CARA",
                         "CHIPS", "ANATOMIA", "CRASH") if by.get(k)]
    if resto:
        duros.append("RESIDUO")
    print()
    if duros:
        print("FALLA: " + ", ".join("%s %d" % (k, by.get(k, len(resto))) for k in duros))
        return 1
    print("expo: %d corridas, %d TOUCH conocidos, cero en las demas reglas" % (runs, by.get("TOUCH", 0)))
    return 0

#  Con guarda, que sin ella IMPORTAR este fichero corre el banco entero. La
#  lista de pantallas vive aqui y Tests/planos.py la lee de aqui -escrita dos
#  veces se queda corta, que es como plano.py se dejo doce claves atras-, asi
#  que un `from expo import SHEETS` lanzaba las 812 corridas y se colgaba.
if __name__ == "__main__":
    sys.exit (main())

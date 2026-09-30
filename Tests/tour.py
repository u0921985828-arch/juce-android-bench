#!/usr/bin/env python3
"""QUE CADA PASO DEL TOUR SEÑALE ALGO.

El tour agujerea el velo sobre un control de verdad y le pone un anillo. Si el
objetivo sale vacio no hay agujero ni anillo: queda la maquina entera
oscurecida y un parrafo, o sea el folleto que este tour existe para no ser.

Y eso paso en DOS pasos sin que nadie se enterara hasta que llego una captura:

  - "LO QUE HACE UN PASO" apuntaba a `stepStripArea`, una variable que solo se
    ASIGNA en un sitio -`vuArea = stepStripArea = {}`- desde que la tira del
    paso dejo la cara. Valia vacio SIEMPRE.
  - "Y LO DEMAS", el ultimo, caia en el `default` de la tabla: los casos
    llegaban al 13 y los pasos son quince. La unica pantalla donde se explica
    como cambiar de idioma se enseñaba sin señalar el selector de idioma.

Los dos abrian su ficha, la oscurecian entera y no marcaban nada. Ninguna de
las seis reglas de expo.py los veia, porque el foco no es un componente: es un
rectangulo que se dibuja.

Se mide el rectangulo y no un si/no: un objetivo de 3x3 px tampoco señala nada
aunque no este vacio. El liston es el dedo minimo, que es lo que ya define un
blanco en esta casa.

El paso 0 es la portada y NO tiene objetivo a proposito - se comprueba que
siga siendo el unico, que es la otra mitad: un `default` que atiende un caso
correcto y otro olvidado es como se esconde el segundo.
"""
import json, os, re, shutil, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import display_alive

ROOT  = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP   = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
#  LOS PASOS SALEN DEL FUENTE, no de un numero escrito aqui: eran quince
#  escritos a mano y la guia paso a veinticuatro, que es exactamente como una
#  regla deja de mirar los pasos nuevos sin decir nada.
import re as _re
PASOS = int (_re.search (r"kTourPasos\s*=\s*(\d+)",
                         open (os.path.join (ROOT, "Source", "MainComponent.h"),
                               encoding="utf8").read()).group (1))
MIN   = 40          # Metrics::hit: el mismo liston que un blanco tocable
PORTADA = 0         # el unico sin objetivo

#  LOS PASOS CUYO OBJETIVO ES TEXTO PINTADO Y NO UN COMPONENTE, con el `tipo`
#  con el que el volcado publica sus filas. Son los unicos que pueden llevar
#  el foco mal COLOCADO sin que se note en su tamano: los demas salen de
#  `deComponente`, que convierte coordenadas con `getLocalArea`.
#
#  El paso sale del fuente y no de un numero escrito aqui: la guia ya paso de
#  quince a veinticuatro una vez, y una regla clavada a un indice deja de
#  mirar lo que mira en cuanto alguien mete un capitulo en medio.
_TITULOS = _re.findall (
    r'"([^"]*)"',
    _re.search (r"titulos\s*\[[^\]]*\]\s*=\s*\{(.*?)\};",
                open (os.path.join (ROOT, "Source", "MainComponentInterno.h"),
                      encoding="utf8").read(), _re.S).group (1))
TIPO_DEL_PASO = {_TITULOS.index ("GESTOS"): "gesto"} if "GESTOS" in _TITULOS else {}


def foco (paso, size):
    casa = tempfile.mkdtemp (prefix="zati-tour-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", ZATI_OPEN="tour%d" % paso,
                DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    ult, rots = None, []
    for l in out.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        if '"tour"' in l:
            try: ult = json.loads (l)
            except Exception: pass
        elif '"rotulo"' in l:
            try: rots.append (json.loads (l))
            except Exception: pass
    #  Los rotulos PINTADOS del paso, en coordenadas de ventana -el volcado ya
    #  corrige el origen del cuerpo desplazable, ver UiAudit::origenPintado-.
    #  Van con el foco porque la regla de abajo los contrasta: el anillo tiene
    #  que CONTENER lo que el paso explica, y eso no se puede saber mirando
    #  solo su tamano.
    if ult is not None: ult["_rotulos"] = rots
    return ult


# ============================================================================
#  LA GUIA DICE LA VERDAD, y la dice entera.
#
#  Hasta la tanda 31 esta prueba solo miraba el anillo. Y la guia tenia TRES
#  cosas falsas que ninguna regla podia ver:
#
#   - el paso de la mesa decia «dieciseis PADS y dieciseis CANALES» desde que
#     el motor tiene treinta y dos (`kNumCanales`), escrito en letra;
#   - GESTOS listaba «GOLPEA ARRIBA O ABAJO · toca mas fuerte o mas flojo»,
#     que es falso desde el tapeo plano;
#   - el paso del secuenciador llamaba a `stepCellToggled (0, 4)`: si el paso
#     5 del pad 1 estaba encendido te lo APAGABA y apilaba un deshacer.
#
#  Y lo que no explicaba nadie -REMUESTREAR, el EQ, AUTO, TAP, la CANCION, los
#  proyectos- no lo veia nadie porque no hay regla que pregunte por lo que
#  falta. Ahora se pregunta, contra las tapas de la app y no contra una lista.
# ============================================================================
GUIA = {"capitulos": []}

#  Los numeros que la guia escribe en letra, y el sustantivo que los ata a una
#  constante del motor. Solo los que tienen dueño: «tres mandos» es el plato y
#  no una constante, y un numero sin dueño no se juzga.
LETRAS = {"uno": 1, "una": 1, "dos": 2, "tres": 3, "cuatro": 4, "cinco": 5,
          "seis": 6, "siete": 7, "ocho": 8, "nueve": 9, "diez": 10, "doce": 12,
          "dieciseis": 16, "veinte": 20, "treinta": 30, "treinta y dos": 32,
          "sesenta y cuatro": 64}
DUENO = {"canales": ("canales",), "efectos": ("efectos",), "bancos": ("bancos",),
         "patrones": ("patrones",), "ranuras": ("ranuras",), "carriles": ("carriles",),
         "bandas": ("bandas",), "pads": ("pads", "padsbanco")}

#  Lo que la guia ya dijo y era falso. Si vuelve, es que alguien lo devolvio.
FALSOS = ["GOLPEA ARRIBA O ABAJO", "toca mas fuerte o mas flojo",
          "dieciseis CANALES"]

#  Las fichas cuyas tapas tienen que estar explicadas. Son las que la guia
#  abre y las que se alcanzan desde la cara.
COBERTURA = ["", "pads", "pad2", "pad3", "sec", "paso", "piano", "song", "mix",
             "mixc", "rack", "inst", "vst", "expo", "proj", "set", "asp", "midi",
             "eq", "chop", "mandos"]
#  Lo que se escribe con su razon, como `SIN_RED` en deshacer.py: tapas cuyo
#  rotulo es CONTENIDO y no una funcion. El resto se deriva del volcado: una
#  tapa de un grupo de radio de idioma o carcasa es una opcion, y una con
#  icono de familia es un nombre de instrumento.
SIN_EXPLICAR = {
    "DLY": "el nombre del efecto enfocado: la ficha de mandos abre el que haya",
}
RADIOS_DE_CONTENIDO = ("idioma", "carcasa")


def _norm (t):
    import unicodedata
    t = unicodedata.normalize ("NFKD", t).encode ("ascii", "ignore").decode()
    return re.sub (r"\s+", " ", t.upper()).strip()


def _corre (env_extra, size="412x915"):
    casa = tempfile.mkdtemp (prefix="zati-guia-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", DISPLAY=PANTALLA, **env_extra)
    try:
        return subprocess.run ([APP], env=env, capture_output=True,
                               timeout=300).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return ""
    finally:
        shutil.rmtree (casa, ignore_errors=True)


def _json (out):
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ("{"):
            try: yield json.loads (l)
            except Exception: pass


def guia (malas):
    ds = [d for d in _json (_corre ({"ZATI_GUIA": "1", "ZATI_OPEN": ""})) if "guia" in d]
    cif = next ((d for d in ds if d["guia"] == "cifras"), None)
    if cif is None:
        malas.append ("la guia no contesto"); return
    pasos = [d for d in ds if d["guia"] == "paso"]
    caps  = [d for d in ds if d["guia"] == "capitulo"]
    gest  = [d for d in ds if d["guia"] == "gesto"]
    pat   = next ((d for d in ds if d["guia"] == "patron"), {})
    GUIA["capitulos"] = caps

    #  1. CADA PASO ES DE UN CAPITULO, Y SOLO DE UNO. Un paso huerfano es un
    #     anillo que ENSENAMELO no alcanza nunca; uno en dos capitulos, un
    #     tramo que se pisa; un capitulo sin pasos, un ENSENAMELO que no ensena.
    duenos = {n: [] for n in range (cif["pasos"])}
    for c in caps:
        if c["desde"] > c["hasta"]:
            malas.append ("el capitulo %s no tiene pasos (%d..%d)"
                          % (c["titulo"], c["desde"], c["hasta"]))
        for n in range (c["desde"], c["hasta"] + 1):
            duenos.setdefault (n, []).append (c["titulo"])
    sin  = [n for n, v in duenos.items() if not v]
    dobl = [n for n, v in duenos.items() if len (v) > 1]
    print ("%d capitulos, %d pasos: %d sin capitulo, %d en dos"
           % (len (caps), cif["pasos"], len (sin), len (dobl)))
    for n in sin:  malas.append ("el paso %d no es de ningun capitulo" % n)
    for n in dobl: malas.append ("el paso %d es de %s" % (n, " y ".join (duenos[n])))

    #  2. LAS CIFRAS SON LAS DEL MOTOR.
    textos = [p["titulo"] + ". " + p["texto"] for p in pasos] \
           + [c["titulo"] + ". " + c["texto"] for c in caps] \
           + [g["como"] + " " + g["que"] for g in gest]
    todo = "\n".join (textos)
    num = r"(treinta y dos|sesenta y cuatro|" + "|".join (
        k for k in LETRAS if " " not in k) + r"|\d+)"
    juzgadas = 0
    for m in re.finditer (num + r"\s+(\w+)", todo, re.I):
        n, sust = m.group (1).lower(), m.group (2).lower()
        if sust not in DUENO: continue
        v = int (n) if n.isdigit() else LETRAS[n]
        buenos = [cif[k] for k in DUENO[sust]]
        juzgadas += 1
        if v not in buenos:
            malas.append ("la guia dice «%s %s» y el motor tiene %s"
                          % (m.group (1), m.group (2), " o ".join (map (str, buenos))))
    print ("cifras juzgadas contra el motor: %d" % juzgadas)

    #  3. LO QUE YA FUE FALSO NO VUELVE, y GESTOS tiene las filas que dice.
    for f in FALSOS:
        if _norm (f) in _norm (todo):
            malas.append ("la guia vuelve a decir «%s», que es falso" % f)
    if len (gest) != cif["gestos"]:
        malas.append ("GESTOS pinta %d filas y kNumGestures dice %d"
                      % (len (gest), cif["gestos"]))

    #  4. RECORRERLA NO TOCA TU PATRON, ni deja un deshacer que no pediste.
    print ("el patron tras recorrer la guia: %d celdas antes, %d despues, "
           "identico %s; deshacer %d -> %d"
           % (pat.get ("celdasAntes", -1), pat.get ("celdasDespues", -1),
              "si" if pat.get ("igual") else "NO",
              pat.get ("undoAntes", -1), pat.get ("undoDespues", -1)))
    if not pat.get ("igual"):
        malas.append ("recorrer la guia cambia el patron: %d celdas antes y %d despues"
                      % (pat.get ("celdasAntes", -1), pat.get ("celdasDespues", -1)))
    if pat.get ("undoDespues") != pat.get ("undoAntes"):
        malas.append ("recorrer la guia apila %d deshacer"
                      % (pat.get ("undoDespues", 0) - pat.get ("undoAntes", 0)))

    #  5. COBERTURA: toda tapa con nombre esta explicada en algun sitio.
    import concurrent.futures as cf
    with cf.ThreadPoolExecutor (8) as ex:
        salidas = list (ex.map (lambda f: (f, _corre ({"ZATI_OPEN": f})), COBERTURA))
    guiaN = _norm (todo)
    faltan, contadas = {}, set()
    for ficha, out in salidas:
        for d in _json (out):
            t = d.get ("text", "")
            if d.get ("kind") != "button" or d.get ("on") != 1 or not t: continue
            if re.fullmatch (r"[A-H\d]|P\d|[\d.:%x+\-]+.*|MANDO \d+", t): continue
            if d.get ("radio") in RADIOS_DE_CONTENIDO: continue
            if str (d.get ("icono", "")).startswith ("ins"): continue
            if t in SIN_EXPLICAR: continue
            contadas.add (t)
            if _norm (t) not in guiaN:
                faltan.setdefault (t, set()).add (ficha or "cara")
    print ("tapas con nombre en %d fichas: %d, sin explicar en la guia: %d"
           % (len (COBERTURA), len (contadas), len (faltan)))
    for t, fs in sorted (faltan.items()):
        malas.append ("la tapa %s (%s) no sale en la guia" % (t, ", ".join (sorted (fs))))
    print()


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario: compila primero")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    size = sys.argv[1] if len (sys.argv) > 1 else "412x915"
    VENT = [int (v) for v in size.lower().split ("x")]
    malas = []
    guia (malas)
    pags  = {}
    print ("%-6s %10s   %s" % ("paso", "objetivo", "que pasa"))
    for n in range (PASOS):
        d = foco (n, size)
        if d is None:
            malas.append ("el paso %d no contesto" % n); continue
        w, h = d.get ("focoW", 0), d.get ("focoH", 0)
        vacio = (w <= 0 or h <= 0)
        pags[n] = d.get ("mixpag", -1)

        if n == PORTADA:
            estado = "portada, sin objetivo a proposito"
            if not vacio:
                estado = "TIENE objetivo y no deberia"
                malas.append ("el paso %d es la portada y señala %dx%d" % (n, w, h))
        elif vacio:
            estado = "NO SEÑALA NADA"
            malas.append ("el paso %d no señala nada" % n)
        elif min (w, h) < MIN:
            estado = "objetivo por debajo del dedo"
            malas.append ("el paso %d señala %dx%d, bajo el minimo de %d" % (n, w, h, MIN))
        else:
            estado = "correcto"

        #  --- Y DENTRO DE LA VENTANA, Y SOBRE LO QUE EXPLICA -----------
        #
        #  UN RECTANGULO BIEN MEDIDO Y MAL COLOCADO PASABA EN VERDE. El
        #  paso 24 -GESTOS- senalaba 347x326, el tamano exacto de la lista
        #  de gestos, y lo pintaba en (0,128): sobre la cabecera de
        #  AJUSTES y escapandose por el filo izquierdo, con la primera
        #  fila de gestos empezando 224 px mas abajo. Llego por una
        #  captura, igual que los dos de arriba, porque las tres reglas
        #  anteriores solo miran `focoW` y `focoH`.
        #
        #  La causa es de coordenadas y no de maquetado: `gesturesArea`
        #  sale de `sheetFromBottom`, que en una ficha DESPLAZABLE
        #  devuelve `cuerpo.getLocalBounds()` -origen (0,0)- mientras el
        #  velo y el anillo se pintan en coordenadas de ventana.
        #
        #  Son DOS reglas y no una porque cazan dos cosas distintas, y la
        #  primera sola no bastaba: (0,128) esta DENTRO de la ventana.
        if not vacio:
            x, y = d.get ("focoX", 0), d.get ("focoY", 0)

            #  CABE EN LA PANTALLA. Se mide contencion y no «x >= 0»: un
            #  foco medio fuera por abajo es igual de inutil que uno medio
            #  fuera por la izquierda, y una sola cuenta caza las cuatro.
            #  Lo saco el paso de INSTRUMENTOS, 337x596 desde y=420 en una
            #  ventana de 915.
            if (x < 0 or y < 0 or x + w > VENT[0] or y + h > VENT[1]):
                estado = "FUERA DE LA VENTANA"
                malas.append ("el paso %d senala %dx%d en (%d,%d): se sale de %dx%d"
                              % (n, w, h, x, y, VENT[0], VENT[1]))

            #  Y CUBRE LO QUE EXPLICA, contrastado contra lo que la pagina
            #  PINTA -que el volcado ya publica en coordenadas de ventana-.
            #  Es la unica forma de cazar un origen equivocado sin escribir
            #  aqui el rectangulo correcto a mano, que seria copiar la
            #  maqueta y quedarse viejo a la vuelta siguiente.
            filas = [r for r in d.get ("_rotulos", [])
                     if r.get ("tipo") == TIPO_DEL_PASO.get (n)]
            if filas:
                fx0 = min (r["x"] for r in filas)
                fy0 = min (r["y"] for r in filas)
                fx1 = max (r["x"] + r["w"] for r in filas)
                fy1 = max (r["y"] + r["h"] for r in filas)
                if not (x <= fx0 and y <= fy0 and x + w >= fx1 and y + h >= fy1):
                    estado = "NO CUBRE LO QUE EXPLICA"
                    malas.append ("el paso %d senala (%d,%d)+%dx%d y las %d filas "
                                  "de '%s' ocupan (%d,%d)..(%d,%d)"
                                  % (n, x, y, w, h, len (filas), TIPO_DEL_PASO[n],
                                     fx0, fy0, fx1, fy1))
            print ("%-6d %14s   %s" % (n, "%dx%d@%d,%d" % (w, h, x, y), estado))
        else:
            print ("%-6d %14s   %s" % (n, "%dx%d" % (w, h), estado))

    #  --- Y EL PASO ABRE LA PAGINA QUE NOMBRA -----------------------------
    #
    #  El paso 11 dice «la mesa tiene dieciseis PADS y dieciseis CANALES» y
    #  abria la mesa sin tocar `mixPage`, o sea en la que hubiera - por defecto
    #  PADS, asi que la palabra CANALES no se veia por ningun sitio. Es el
    #  residuo de siempre, el mismo que hizo que los pasos 6 a 9 llamen a
    #  `showSeqPage` explicitamente.
    #
    #  Ninguna de las reglas de arriba puede verlo: el objetivo del paso es
    #  `rackButton`, que se maqueta en las DOS paginas, asi que el anillo sale
    #  igual de bien con la mesa abierta donde no toca.
    #
    #  Es una CIFRA y no un si/no -dice QUE pagina quedo- asi que caza tambien
    #  el caso contrario, que otro paso deje la mesa en CANALES, sin escribir
    #  una segunda regla.
    print()
    #  El paso que habla de los canales es el tramo del capitulo MEZCLA Y
    #  CANALES de la guia, no un numero: con la guia unica paso del 11 al 15.
    CANALES = next ((c["desde"] for c in GUIA["capitulos"]
                     if "CANALES" in c["titulo"]), -1)
    print ("la mesa queda en: %s" % ", ".join ("%d:%s" % (n, "CANALES" if v == 1
                                                          else "PADS" if v == 0 else "?")
                                               for n, v in sorted (pags.items())
                                               if v != 0))
    if pags.get (CANALES) != 1:
        malas.append ("el paso %d habla de los canales y abre la mesa en PADS" % CANALES)
    for n, v in sorted (pags.items()):
        if n != CANALES and v != 0:
            malas.append ("el paso %d deja la mesa en la pagina %d" % (n, v))

    #  --- LA BIENVENIDA SON CUATRO PASOS Y UNA PUERTA ---------------------
    #
    #  Quince tarjetas la primera vez son el manual otra vez, y este proyecto
    #  ya lo tiene escrito a otra escala: «un parrafo largo encima de una
    #  maquina oscurecida no se lee, se salta». Los cuatro primeros -los pads,
    #  los bancos, CARGAR/REC/PLAY y el transporte- son LA APP; los otros once
    #  son el recorrido, y se piden.
    #
    #  Con DOS cifras, que es lo que separa las dos formas de escribirlo mal:
    #  que el cuarto OFREZCA la puerta -y no encadene- y que tomarla LLEGUE al
    #  quinto. Solo la primera la cumple una tapa que cambia de rotulo y no
    #  hace nada; solo la segunda, un tour que no se corta en ninguna parte.
    print()
    d3 = foco (3, size)
    d2 = foco (2, size)
    if d3 is None or d2 is None:
        malas.append ("el paso de la puerta no contesto")
    else:
        print ("paso 3 (la puerta):  avanzar dice %-14s  la tercera dice %s"
               % ('"%s"' % d3.get ("sig", ""), '"%s"' % d3.get ("tercera", "")))
        print ("paso 2 (antes):      avanzar dice %-14s  la tercera dice %s"
               % ('"%s"' % d2.get ("sig", ""), '"%s"' % d2.get ("tercera", "")))
        if d3.get ("tercera") == d2.get ("tercera"):
            malas.append ("el cuarto paso no ofrece la puerta: la tercera tapa sigue "
                          "diciendo \"%s\"" % d3.get ("tercera"))
        if d3.get ("sig") == d2.get ("sig"):
            malas.append ("el cuarto paso encadena al quinto en vez de cerrar: "
                          "avanzar sigue diciendo \"%s\"" % d3.get ("sig"))

    #  Y TOMARLA LLEGA AL QUINTO, pulsando la tapa DE VERDAD: llamar a
    #  `showTour(4)` por dentro se salta justo el codigo que decide si esa tapa
    #  salta o sigue.
    dp = None
    casaP = tempfile.mkdtemp (prefix="zati-puerta-")
    try:
        env = dict (os.environ, HOME=casaP, ZATI_AUDIT="1", ZATI_SIZE=size,
                    ZATI_LANG="es", ZATI_OPEN="tourpuerta",
                    DISPLAY=PANTALLA)
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180)\
                        .stdout.decode ("utf8", "replace")
        for l in out.splitlines():
            l = l.strip()
            if l.startswith ("{") and '"tour"' in l:
                try: dp = json.loads (l)
                except Exception: pass
    finally:
        shutil.rmtree (casaP, ignore_errors=True)

    paso = dp.get ("tour", -1) if dp else -1
    print ("tomando la puerta:   el tour queda en el paso %d" % paso)
    if paso != 4:
        malas.append ("tomar la puerta deja el tour en el paso %d y tenia que "
                      "llevar al 4" % paso)

    #  --- Y QUE SOLO SALGA LA PRIMERA VEZ ---------------------------------
    #
    #  Es la otra mitad del tour y la que no miraba nadie: los quince pasos
    #  pueden señalar perfectamente lo que explican y aun asi la bienvenida
    #  aparecer EN CADA ARRANQUE, que es como se lee una app rota. Pasaba: la
    #  marca de visto se escribia al acabarlo, asi que salir por el boton ATRAS
    #  de Android -o cerrar la app- no marcaba nada.
    #
    #  Se mide con DOS arranques y el MISMO HOME, que es la unica forma: con un
    #  HOME por corrida las dos serian la primera vez y la prueba diria que si
    #  a cualquier cosa. Y por el sitio donde se DECIDE, no por una copia de la
    #  regla: la app imprime lo que acaba de resolver.
    casa = tempfile.mkdtemp (prefix="zati-tour2-")
    try:
        vistas, puestas = [], []
        for _ in range (2):
            env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                        ZATI_LANG="es", ZATI_OPEN="",
                        XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                        DISPLAY=PANTALLA)
            out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                                  timeout=300).stdout
            v = p = None
            for linea in out.splitlines():
                linea = linea.strip()
                if not linea.startswith ('{'): continue
                try:    d = json.loads (linea)
                except Exception: continue
                if d.get ("arranque") == "tour":
                    v = d["primera"]; p = d.get ("puesto")
            vistas.append (v); puestas.append (p)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    print()
    print ("arranques con el mismo HOME: %s" % vistas)
    if vistas != [1, 0]:
        malas.append ("la bienvenida sale %s en dos arranques y tenia que salir [1, 0]"
                      % (vistas,))

    #  Y LA MITAD QUE FALTABA: que la marca decida algo.
    #
    #  Lo de arriba mide que se ESCRIBE bien y salia [1, 0] con la bienvenida
    #  apareciendo igual en cada arranque, porque hay un segundo camino que la
    #  levanta y no la mira: retranslateUi -que corre en el constructor y al
    #  tocar un idioma- llamaba a showTour, y showTour termina en
    #  `tourSheet.setVisible (true)` sin condicion. El bloque que si mira la
    #  marca corre despues y solo puede AÑADIR.
    #
    #  Con ZATI_AUDIT la tarjeta no se enseña nunca a proposito, asi que
    #  cualquier uno aqui es exactamente eso: puesta sin que nadie la pida.
    #  Salia [1, 1].
    print ("la bienvenida quedo puesta:    %s" % puestas)
    if puestas != [0, 0]:
        malas.append ("la bienvenida queda puesta %s sin que nadie la pida" % (puestas,))

    #  --- Y QUE CAMBIAR DE IDIOMA NO CIERRE LA FICHA ----------------------
    #
    #  El mismo fallo por la otra puerta, y la mas absurda de las dos: el idioma
    #  se cambia desde AJUSTES, y tocarlo llamaba a retranslateUi -> showTour ->
    #  tourPrepara, cuyo caso por defecto empieza por closeAllSheets. Cerraba la
    #  ficha que tenias delante y levantaba la bienvenida encima.
    #
    #  La app pulsa la tapa DE VERDAD (langButtons[1]->onClick), que es donde el
    #  fallo existe: llamar a retranslateUi por dentro se salta el callback que
    #  lo encadena todo.
    casa = tempfile.mkdtemp (prefix="zati-tour3-")
    try:
        env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                    ZATI_LANG="es", ZATI_OPEN="lang",
                    XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                    DISPLAY=PANTALLA)
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
        idi = None
        for linea in out.splitlines():
            linea = linea.strip()
            if not linea.startswith ('{'): continue
            try:    d = json.loads (linea)
            except Exception: continue
            if "idioma" in d: idi = d
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    print()
    if idi is None:
        malas.append ("el cambio de idioma no contesto")
    else:
        print ("tras tocar un idioma:  ajustes %d  tour %d"
               % (idi.get ("ajustes", -1), idi.get ("tour", -1)))
        if idi.get ("ajustes") != 1:
            malas.append ("cambiar de idioma cierra AJUSTES, que es de donde se cambia")
        if idi.get ("tour") != 0:
            malas.append ("cambiar de idioma levanta la bienvenida")

    print()
    for m in malas: print ("FALLA ", m)
    print ("los %d pasos de la guia señalan lo que explican, sus cifras son las del "
           "motor, no tocan el patron y todas las tapas estan explicadas" % PASOS if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

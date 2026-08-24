#!/usr/bin/env python3
"""EL PLANO DE CADA PANTALLA, DIBUJADO CON LAS COORDENADAS DE VERDAD.

Para decidir donde falta un dibujo -o donde una fila se ha quedado con un
hueco- hay que ver la app entera, y verla entera son treinta y dos pantallas
abiertas una por una en el telefono. El proyecto ya tenia las dos mitades y
nadie las habia juntado:

  - `ZATI_AUDIT=1` imprime el rectangulo, el texto y el ICONO de cada control,
    mas los rotulos que se PINTAN (que no son componentes y son media pantalla);
  - `ZATI_SHOT` saca el PNG de esa misma pantalla.

Las dos son excluyentes en el mismo arranque -es una cadena if/else en
Main.cpp- asi que son dos corridas por pantalla. Con eso sale un SVG a escala
1:1 al lado de la foto: el plano dice DONDE esta cada cosa y QUE lleva, y la
foto dice COMO se ve. Con una sola no se puede comprobar que la otra no miente.

Y las tres rejillas de LIENZO -la de pasos, el piano y la cancion- se dibujan
por dentro con la misma cuenta que ya hace Tests/expo.py, o el secuenciador y
el piano roll salen como una caja vacia, que es justo lo que hay que ver.

    python3 Tests/planos.py            # las 32, a 412x915 en espanol
    python3 Tests/planos.py sec piano  # solo esas
    ZATI_PLANOS_LANG=en python3 Tests/planos.py

Sale en `planos/`: un SVG y un PNG por pantalla, y un index.html con todo
dentro (las fotos en base64) para poder mirarlo de una vez. La carpeta va al
.gitignore por lo mismo que /store/: la fuente es el script.
"""
import base64, json, os, shutil, subprocess, sys, tempfile
import concurrent.futures

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (
        ROOT, "build", "Zati_artefacts", "Release", "Zati")
SALIDA = os.path.join (ROOT, "planos")

#  LA MISMA LISTA QUE expo.py, no una copia mas corta.
#
#  Tests/plano.py lleva la suya y cubre 17 de las 32 -sin pad2, sin pad3, sin
#  secp, sin el piano escrito, sin ASPECTO, sin los cuatro de instrumentos, sin
#  el instrumento del pad, sin los cinco del tour y sin el navegador de
#  carpetas-. Una lista de pantallas escrita dos veces es media app sin medir:
#  quince pantallas que nadie mide y nadie echa de menos.
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import SHEETS, display_alive

#  Como se llama cada una y desde donde se llega. Lo segundo importa tanto como
#  lo primero: un plano que no dice como se abre esa pantalla es un dibujo.
NOMBRES = {
    "":         ("LA CARA",                  "la maquina, sin ninguna ficha abierta"),
    "pads":     ("EL PAD · SONIDO",          "pestana PAD"),
    "pad2":     ("EL PAD · RECORTE",         "pestana PAD, pagina RECORTE"),
    "pad3":     ("EL PAD · ENVIOS",          "pestana PAD, pagina EL PAD"),
    "sec":      ("SEC · PASOS",              "pestana SEC"),
    "secp":     ("SEC · PASOS, con un paso tocado", "tocar una celda de la rejilla"),
    "paso":     ("SEC · PATRON",             "pestana SEC, pagina PATRON"),
    "piano":    ("SEC · PIANO ROLL",         "pestana SEC, pagina PIANO"),
    "pianod":   ("SEC · PIANO con notas",    "el piano con cinco notas y un acorde escritos"),
    "song":     ("CANCION",                  "pestana CANCION"),
    "mix":      ("MEZCLA",                   "pestana MEZCLA"),
    "xy":       ("XY",                       "pestana XY"),
    "set":      ("AJUSTES · AUDIO",          "pestana AJUSTES"),
    "asp":      ("AJUSTES · ASPECTO",        "AJUSTES, pestana ASPECTO"),
    "midi":     ("AJUSTES · MIDI",           "AJUSTES, pestana MIDI"),
    "proj":     ("AJUSTES · PROYECTOS",      "AJUSTES, tapa PROYECTOS"),
    "gest":     ("AJUSTES · GESTOS",         "AJUSTES, tapa GESTOS"),
    "manual":   ("MANUAL",                   "AJUSTES · GESTOS, tapa MANUAL"),
    "rack":     ("RACK DE ENVIOS",           "EL PAD, tapa RACK"),
    "chop":     ("TROCEAR",                  "EL PAD, tapa CHOP"),
    "inst":     ("INSTRUMENTOS · fabrica",   "navegador, tapa FABRICA"),
    "instd":    ("INSTRUMENTOS · pack de disco", "INSTRUMENTOS con dos packs instalados"),
    "instg":    ("INSTRUMENTOS · rejilla",   "INSTRUMENTOS con un destino elegido"),
    "vst":      ("EL INSTRUMENTO",           "un pad con instrumento, pestana PAD"),
    "expo":     ("EXPORTAR",                 "AJUSTES · PROYECTOS, tapa EXPORTAR"),
    "browse":   ("NAVEGADOR · cargar",       "tapa CARGAR"),
    "browsedir":("NAVEGADOR · elegir carpeta", "EXPORTAR, tapa CARPETA"),
    "tour":     ("TOUR · paso 1",            "AJUSTES · GESTOS, tapa TOUR"),
    "tour1":    ("TOUR · paso 2",            "el tour, siguiente"),
    "tour6":    ("TOUR · paso 7",            "el tour, dentro de SEC"),
    "tour10":   ("TOUR · paso 11",           "el tour, dentro de EL PAD"),
    "tourf":    ("TOUR · ultimo paso",       "el tour, al final"),
}

#  Las tres que se pintan enteras, con la cuenta de su celda. Es la MISMA de
#  Tests/expo.py -si se separan, el plano dibuja una rejilla que el banco mide
#  de otra forma-.
REJILLAS = (("StepGrid",  16, 16, 30),
            ("Playlist",   8,  4, 26),
            ("PianoRoll", 16, 13, 26))

#  Los colores del plano no son los de la app: un plano es un plano. Lo unico
#  que se hereda es que lo que se TOCA se distingue de lo que solo se lee.
COLOR = {
    "button": ("#1d4e63", "#63b6d6"),
    "slider": ("#3a2f5c", "#9b8cd6"),
    "editor": ("#14323d", "#5fa9bd"),
    "label":  ("#232323", "#8a8a8a"),
    "other":  ("#1a1a1a", "#4a4a4a"),
}


def corre (clave, tam, lang, extra=None):
    """Una corrida, con HOME propio. Devuelve las lineas JSON del volcado."""
    casa = tempfile.mkdtemp (prefix="zati-plano-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": tam, "ZATI_LANG": lang,
                     "ZATI_OPEN": clave, "DISPLAY": os.environ.get ("DISPLAY", ":99")})
        if extra:
            env.update (extra)
        out = subprocess.run ([APP], env=env, capture_output=True,
                              timeout=300).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return []
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = []
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ("{") and linea.endswith ("}"):
            try:
                filas.append (json.loads (linea))
            except Exception:
                pass
    return filas


def esc (t):
    return (str (t).replace ("&", "&amp;").replace ("<", "&lt;")
                   .replace (">", "&gt;").replace ('"', "&quot;"))


def svg (clave, filas, tam):
    """El plano: un rectangulo por control, en coordenadas de ventana."""
    raiz = next ((r for r in filas if r.get ("root")), None)
    if raiz is None:
        return None, (0, 0)
    W, H = raiz["w"], raiz["h"]
    comps = [r for r in filas if "path" in r and r["w"] > 0 and r["h"] > 0]
    rots  = [r for r in filas if "rotulo" in r]

    #  De menos a mas profundo: un hijo se dibuja ENCIMA de su padre, que es
    #   como esta en pantalla.
    comps.sort (key=lambda r: (r.get ("depth", 0), r["y"], r["x"]))

    p = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d">' % (W, H, W, H)]
    p.append ('<rect width="%d" height="%d" fill="#0d1414"/>' % (W, H))

    conIcono = sinIcono = 0
    for r in comps:
        k = r.get ("kind", "other")
        relleno, borde = COLOR.get (k, COLOR["other"])
        x, y, w, h = r["x"], r["y"], r["w"], r["h"]
        p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="2" fill="%s" stroke="%s" stroke-width="1"/>'
                  % (x, y, w, h, relleno, borde))

        #  Las rejillas de lienzo, por dentro. Sin esto el secuenciador, el
        #   piano y la cancion son una caja vacia.
        for nombre, cols, carriles, canal in REJILLAS:
            if nombre in r["path"] and w > canal and h > 0:
                cw = (w - canal) / float (cols)
                ch = h / float (carriles)
                for c in range (cols + 1):
                    gx = x + canal + c * cw
                    p.append ('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="#2b4a52" stroke-width="0.5"/>'
                              % (gx, y, gx, y + h))
                for c in range (carriles + 1):
                    gy = y + c * ch
                    p.append ('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="#2b4a52" stroke-width="0.5"/>'
                              % (x, gy, x + w, gy))
                p.append ('<text x="%d" y="%d" font-family="monospace" font-size="8" fill="#7fd4c8">%s %dx%d  celda %.1fx%.1f</text>'
                          % (x + 2, y + h - 3, nombre, cols, carriles, cw, ch))

        txt = str (r.get ("text", "") or "")
        ico = r.get ("icono")
        if k == "button" and (txt or ico is not None):
            if ico:
                conIcono += 1
                #  El dibujo, como un cuadrado lleno a la izquierda del rotulo.
                p.append ('<rect x="%d" y="%.1f" width="7" height="7" fill="#f0b429"/>'
                          % (x + 3, y + h / 2.0 - 3.5))
            elif txt:
                sinIcono += 1
                p.append ('<rect x="%d" y="%.1f" width="7" height="7" fill="none" stroke="#e05252" stroke-width="1"/>'
                          % (x + 3, y + h / 2.0 - 3.5))
        if txt:
            fs = min (11, max (6, h - 4))
            p.append ('<text x="%.1f" y="%.1f" font-family="monospace" font-size="%d" fill="#dfe8e8" text-anchor="middle">%s</text>'
                      % (x + w / 2.0, y + h / 2.0 + fs / 3.0, fs, esc (txt[:22])))

    #  Y los rotulos PINTADOS, que no son componentes: el titulo de la ficha y
    #   los encabezados de seccion. Media pantalla es esto.
    for r in rots:
        p.append ('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#f0b429" stroke-width="1" stroke-dasharray="3 2"/>'
                  % (r["x"], r["y"], max (1, r["w"]), max (1, r["h"])))
        p.append ('<text x="%d" y="%d" font-family="monospace" font-size="10" fill="#f0b429">%s</text>'
                  % (r["x"] + 2, r["y"] + min (11, r["h"]), esc (r["rotulo"][:30])))

    p.append ('</svg>')
    return "\n".join (p), (conIcono, sinIcono)


#  LO QUE NO LLEVA DIBUJO A PROPOSITO LO DICE LA APP, no esta lista.
#
#  La primera version llevaba los ROTULOS aqui -"1 OCTAVA", "PAD -", "PACK +"-
#  y esa prueba solo sabe medir una de las cuatro compilaciones: en ingles el
#  mismo boton dice "1 OCTAVE", la excepcion deja de encajar y la fila sale
#  como un hueco que no existe. Un banco que no reconoce lo que mira da verde
#  o rojo por el motivo equivocado, que es peor que no medir.
#
#  Ahora la marca `valor` viaja en el volcado (ver ponIconos y UiAudit.h): la
#  pone quien reparte los iconos, que es quien sabe cuales son las dos clases
#  -el SIGNO de un par que sube y baja, y el rotulo que dice el estado en vez
#  de la accion-. Aqui solo queda la cruz de cerrar, que es el mismo caracter
#  en los cuatro idiomas y ya es un simbolo.
SIN_DIBUJO = {"×"}


def huecos (filas):
    """Las FILAS donde unas tapas llevan dibujo y otras no.

    Es la regla que ninguna de las seis del banco puede ver, porque un hueco
    de dibujo se maqueta perfecto: no solapa, no se sale, no corta el rotulo y
    esta traducido. Y no se lee como "aqui no cabia" sino como una tapa a la
    que le falta algo, que es exactamente por lo que `filaDeIconos` decide la
    fila entera de golpe y no tapa por tapa.

    Una fila es el mismo PADRE y la misma banda de y. Sin el padre, la fila de
    efectos de la cara se agrupa con los botones de una ficha que flota encima
    -misma y, otro sitio- y salen huecos que no existen.
    """
    b = [r for r in filas
         if r.get ("kind") == "button" and r.get ("w", 0) > 0 and r.get ("h", 0) > 0
         and str (r.get ("text", "")).strip()]
    grupos = []
    for r in b:
        padre = "/".join (r["path"].split ("/")[:-1])
        for g in grupos:
            if (g[0][0] == padre and abs (g[0][1]["y"] - r["y"]) <= 6
                    and abs (g[0][1]["h"] - r["h"]) <= 6):
                g.append ((padre, r)); break
        else:
            grupos.append ([(padre, r)])

    fuera = []
    for g in grupos:
        #  Las celdas de una rejilla de pads llevan el numero por rotulo, y su
        #  dibujo es un DATO -la familia del instrumento que hay dentro-: una
        #  vacia al lado de una llena no es un hueco, es que esta vacia.
        caps = [r for _, r in g if not str (r["text"]).strip().isdigit()
                and not r.get ("valor")
                and str (r["text"]).strip() not in SIN_DIBUJO]
        if len (caps) < 2:
            continue
        con = [r for r in caps if r.get ("icono")]
        sin = [r for r in caps if not r.get ("icono")]
        if con and sin:
            fuera.append ((g[0][1]["y"],
                           [str (r["text"]) for r in con],
                           [str (r["text"]) for r in sin]))
    return fuera


def una (clave):
    tam  = os.environ.get ("ZATI_PLANOS_SIZE", "412x915")
    lang = os.environ.get ("ZATI_PLANOS_LANG", "es")

    filas = corre (clave, tam, lang)
    dibujo, cuenta = svg (clave, filas, tam)
    if dibujo is None:
        return clave, None, None, (0, 0), []
    rotos = huecos (filas)

    #  Y la foto, en OTRA corrida: ZATI_SHOT y el volcado son excluyentes.
    png = os.path.join (SALIDA, "%s.png" % (clave or "cara"))
    corre (clave, tam, lang, {"ZATI_SHOT": png, "ZATI_SHOT_SCALE": "0.5"})

    with open (os.path.join (SALIDA, "%s.svg" % (clave or "cara")), "w", encoding="utf8") as f:
        f.write (dibujo)
    datos = None
    if os.path.exists (png):
        with open (png, "rb") as f:
            datos = base64.b64encode (f.read()).decode ("ascii")
    return clave, dibujo, datos, cuenta, rotos


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")

    solo = sys.argv[1:]
    claves = [s for s in SHEETS if not solo or (s or "cara") in solo]
    os.makedirs (SALIDA, exist_ok=True)

    trabajos = max (1, min (16, os.cpu_count() or 4))
    hechos = {}
    with concurrent.futures.ThreadPoolExecutor (max_workers=trabajos) as pool:
        for clave, dibujo, png, cuenta, rotos in pool.map (una, claves):
            hechos[clave] = (dibujo, png, cuenta, rotos)

    #  El indice, con todo dentro: se abre en el telefono sin nada al lado.
    doc = ['<meta charset="utf-8"><title>ZATI · planos</title>',
           '<style>body{background:#0b1010;color:#dfe8e8;font-family:system-ui,sans-serif;margin:0;padding:24px}'
           'h1{font-size:20px;letter-spacing:.1em}h2{font-size:15px;margin:32px 0 4px;letter-spacing:.08em}'
           'p.d{color:#8fa3a3;font-size:13px;margin:0 0 10px}'
           '.par{display:flex;gap:16px;align-items:flex-start;flex-wrap:wrap}'
           'svg,img{max-width:100%;height:auto;border:1px solid #24343a;border-radius:4px}'
           '.leg{color:#8fa3a3;font-size:12px;margin:12px 0 24px}'
           'b.si{color:#f0b429}b.no{color:#e05252}</style>',
           '<h1>ZATI · el plano de cada pantalla</h1>',
           '<p class="d">Dibujado con las coordenadas que la app imprime, no a mano. '
           'Cuadrado <b class="si">lleno</b> = la tapa lleva dibujo; cuadrado '
           '<b class="no">hueco</b> = tapa con rotulo y sin dibujo. '
           'La linea de puntos amarilla es un rotulo PINTADO, que no es un componente.</p>']

    print ("%-11s %-34s %s" % ("clave", "pantalla", "tapas con dibujo / sin el"))
    faltan = 0
    rotas = []
    for clave in claves:
        dibujo, png, (si, no), rotos = hechos.get (clave, (None, None, (0, 0), []))
        nombre, comose = NOMBRES.get (clave, (clave.upper(), ""))
        if dibujo is None:
            print ("%-11s %-34s SIN VOLCADO" % (clave or "cara", nombre))
            rotas.append ((clave or "cara", -1, [], ["SIN VOLCADO"]))
            continue
        faltan += no
        print ("%-11s %-34s %3d / %3d%s" % (clave or "cara", nombre, si, no,
                                            "   HUECO" if rotos else ""))
        for y, con, sin in rotos:
            rotas.append ((clave or "cara", y, con, sin))
        doc.append ('<h2>%s</h2><p class="d">%s &nbsp;·&nbsp; <b class="si">%d</b> con dibujo, '
                    '<b class="no">%d</b> sin el</p><div class="par">%s%s</div>'
                    % (esc (nombre), esc (comose), si, no, dibujo,
                       ('<img src="data:image/png;base64,%s">' % png) if png else ""))

    with open (os.path.join (SALIDA, "index.html"), "w", encoding="utf8") as f:
        f.write ("\n".join (doc))

    print()
    print ("%d pantallas en %s/index.html — %d tapas con rotulo y sin dibujo"
           % (len (claves), os.path.relpath (SALIDA, ROOT), faltan))

    #  Y el veredicto, que es lo que separa este fichero de un generador de
    #  dibujos: sin el, una fila que se queda con un hueco no falla, se
    #  publica. Es lo mismo que le pasaba a EXPORTAR -los numeros existian y no
    #  los miraba nadie- y lo que ya obligo a poner veredicto en expo.py.
    if rotas:
        print()
        for clave, y, con, sin in rotas:
            print ("  HUECO  %-10s y=%-4s  con dibujo: %-38s sin el: %s"
                   % (clave, y if y >= 0 else "?",
                      ",".join (c[:12] for c in con), ",".join (s[:16] for s in sin)))
        print ("FALLA: %d filas con hueco" % len (rotas))
        return 1

    print ("planos: %d pantallas, ninguna fila con un hueco de dibujo" % len (claves))
    return 0


if __name__ == "__main__":
    sys.exit (main())

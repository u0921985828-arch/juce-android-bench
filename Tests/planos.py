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


#  LA CABECERA DE LA PAGINA, entera y aparte.
#
#  Se escribe con comillas triples y sin formateo: el CSS lleva `100%` y un
#  `%` de formateo en esta cadena lo parte por la mitad. Y los colores se
#  declaran como fichas en `:root` -nunca dentro del bloque de tema-, o el
#  visor que no marca nada se queda con la tinta de un tema sobre el fondo del
#  otro. El plano SI se queda oscuro en los dos: es la app, y la app es oscura.
ENCABEZADO = """<meta charset="utf-8">
<title>Planos de ZATI</title>
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@500;600&family=Barlow:wght@400;500&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
:root{
  --papel:#eceae4; --tarjeta:#f6f5f1; --filo:#d3d0c7;
  --tinta:#1b2224; --apagado:#5f6b6d;
  --plano:#0e1517; --planofilo:#243237;
  --azul:#1c6a85; --ambar:#9a6b06; --rojo:#a8352f;
  --rejilla:rgba(28,106,133,.10);
}
@media (prefers-color-scheme:dark){ :root:not([data-theme="light"]){
  --papel:#0b1113; --tarjeta:#121a1d; --filo:#222f34;
  --tinta:#dde7e8; --apagado:#8b9fa2;
  --plano:#0e1517; --planofilo:#22333a;
  --azul:#5fb4d1; --ambar:#f0b429; --rojo:#e07070;
  --rejilla:rgba(95,180,209,.07);
}}
:root[data-theme="dark"]{
  --papel:#0b1113; --tarjeta:#121a1d; --filo:#222f34;
  --tinta:#dde7e8; --apagado:#8b9fa2;
  --plano:#0e1517; --planofilo:#22333a;
  --azul:#5fb4d1; --ambar:#f0b429; --rojo:#e07070;
  --rejilla:rgba(95,180,209,.07);
}
*{box-sizing:border-box}
body{
  margin:0; background:var(--papel); color:var(--tinta);
  font-family:"Barlow",system-ui,-apple-system,sans-serif; font-size:15px; line-height:1.55;
  background-image:linear-gradient(var(--rejilla) 1px,transparent 1px),
                   linear-gradient(90deg,var(--rejilla) 1px,transparent 1px);
  background-size:32px 32px;
}
.hoja{display:grid; grid-template-columns:236px minmax(0,1fr); gap:36px;
      max-width:1320px; margin:0 auto; padding:40px 28px 96px}
@media (max-width:900px){ .hoja{grid-template-columns:minmax(0,1fr); gap:20px; padding:24px 16px 64px} }

/*  LA PORTADA, que es una ficha tecnica y no un heroe: lo que hay que saber
    antes de mirar treinta y dos dibujos es de donde salen los numeros.       */
.portada{grid-column:1/-1; border-top:2px solid var(--tinta); padding-top:14px;
         display:flex; flex-wrap:wrap; gap:20px 40px; align-items:flex-end; justify-content:space-between}
h1{font-family:"Barlow Condensed",sans-serif; font-weight:600; font-size:clamp(30px,5vw,46px);
   letter-spacing:.02em; margin:0; text-wrap:balance; text-transform:uppercase}
.sub{margin:6px 0 0; color:var(--apagado); max-width:62ch}
.sello{font-family:"IBM Plex Mono",ui-monospace,monospace; font-size:12px; color:var(--apagado);
       text-align:right; line-height:1.7; font-variant-numeric:tabular-nums}
.sello b{color:var(--tinta); font-weight:500}

/*  La leyenda dice que significa cada marca del dibujo. Sin ella el plano es
    un monton de rectangulos de colores.                                     */
.leyenda{grid-column:1/-1; display:flex; flex-wrap:wrap; gap:8px 22px;
         font-family:"IBM Plex Mono",ui-monospace,monospace; font-size:12px; color:var(--apagado);
         border:1px solid var(--filo); background:var(--tarjeta); border-radius:3px; padding:12px 16px}
.leyenda span{display:inline-flex; align-items:center; gap:7px}
.marca{width:11px; height:11px; border-radius:2px; flex:none}
.m-si{background:var(--ambar)}
.m-no{border:1.5px solid var(--rojo)}
.m-rot{border:1.5px dashed var(--ambar)}
.m-b{background:var(--azul)}

.indice{align-self:start; position:sticky; top:24px}
@media (max-width:900px){ .indice{position:static} }
.indice .rot{font-family:"IBM Plex Mono",ui-monospace,monospace; font-size:11px; letter-spacing:.14em;
             text-transform:uppercase; color:var(--apagado); margin:0 0 8px}
.indice ol{list-style:none; margin:0; padding:0; display:flex; flex-direction:column;
           border-left:1px solid var(--filo)}
.indice a{display:flex; justify-content:space-between; gap:10px; align-items:baseline;
          padding:5px 10px; color:var(--apagado); text-decoration:none; font-size:13.5px;
          border-left:2px solid transparent; margin-left:-1px}
.indice a:hover,.indice a:focus-visible{color:var(--tinta); border-left-color:var(--azul);
                                        background:var(--tarjeta); outline:none}
.indice em{font-family:"IBM Plex Mono",ui-monospace,monospace; font-style:normal; font-size:11.5px;
           font-variant-numeric:tabular-nums; opacity:.75; flex:none}

main{display:flex; flex-direction:column; gap:44px; min-width:0}
section{min-width:0; scroll-margin-top:20px}
.cab{display:flex; flex-wrap:wrap; gap:8px 20px; align-items:flex-end; justify-content:space-between;
     border-bottom:1px solid var(--filo); padding-bottom:8px; margin-bottom:16px}
h2{font-family:"Barlow Condensed",sans-serif; font-weight:600; text-transform:uppercase;
   letter-spacing:.05em; font-size:21px; margin:0; color:var(--tinta)}
.ruta{margin:2px 0 0; font-size:13px; color:var(--apagado)}
.cifras{display:flex; gap:8px; flex:none}
.chip{font-family:"IBM Plex Mono",ui-monospace,monospace; font-size:11px; letter-spacing:.04em;
      padding:3px 9px; border-radius:2px; border:1px solid var(--filo); color:var(--apagado);
      font-variant-numeric:tabular-nums; white-space:nowrap}
.chip b{font-weight:500}
.chip.si b{color:var(--ambar)} .chip.no b{color:var(--rojo)}

.par{display:flex; gap:22px; align-items:flex-start; flex-wrap:wrap}
figure{margin:0; display:flex; flex-direction:column; gap:7px; min-width:0; max-width:100%}
.lienzo{background:var(--plano); border:1px solid var(--planofilo); border-radius:3px;
        padding:6px; overflow-x:auto; max-width:100%}
svg,img{display:block; max-width:100%; height:auto}
figcaption{font-family:"IBM Plex Mono",ui-monospace,monospace; font-size:11px; color:var(--apagado);
           letter-spacing:.03em}
a:focus-visible{outline:2px solid var(--azul); outline-offset:2px}
@media (prefers-reduced-motion:no-preference){ .indice a{transition:color .12s,border-color .12s} }
</style>
<div class="hoja"><header class="portada">
<div><h1>El plano de cada pantalla</h1>
<p class="sub">Las treinta y dos pantallas de ZATI dibujadas a escala 1:1 con las
coordenadas que la app imprime de si misma, no a mano y no de una captura. Al lado
de cada plano, la foto de esa misma pantalla en otra corrida.</p></div>
<p class="sello">ARTiFACTS · ZATI<br><b>32</b> pantallas · <b>412&times;915</b><br>SVG del volcado + PNG</p>
</header>
<div class="leyenda">
<span><i class="marca m-b"></i>control que se toca</span>
<span><i class="marca m-si"></i>tapa CON dibujo</span>
<span><i class="marca m-no"></i>tapa con rotulo y SIN dibujo</span>
<span><i class="marca m-rot"></i>rotulo pintado, que no es un componente</span>
</div>"""


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
    #
    #  La pagina es un documento de taller y se viste como tal -mesa de dibujo,
    #  no folleto-: el plano ya trae su propio fondo oscuro porque la app es
    #  oscura, asi que lo que se disena aqui es el MARCO, y se disena para los
    #  dos temas del visor, que son tres estados y no dos (claro, oscuro, y el
    #  de por defecto que no marca nada).
    doc = [ENCABEZADO]

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
        doc.append (
            '<section id="s-%s"><header class="cab"><div><h2>%s</h2>'
            '<p class="ruta">%s</p></div><div class="cifras">'
            '<span class="chip si"><b>%d</b> con dibujo</span>'
            '<span class="chip no"><b>%d</b> sin el</span></div></header>'
            '<div class="par"><figure><div class="lienzo">%s</div>'
            '<figcaption>plano · %s px, del volcado</figcaption></figure>%s</div></section>'
            % (clave or "cara", esc (nombre), esc (comose), si, no, dibujo,
               os.environ.get ("ZATI_PLANOS_SIZE", "412x915"),
               ('<figure><div class="lienzo"><img alt="%s" src="data:image/png;base64,%s"></div>'
                '<figcaption>foto · la misma pantalla</figcaption></figure>'
                % (esc (nombre), png)) if png else ""))

    #  El indice lateral se escribe al final porque necesita las cifras, y va
    #  DELANTE en el documento: una lista de treinta y dos pantallas detras de
    #  treinta y dos pantallas no es un indice.
    indice = ['<nav class="indice" aria-label="las pantallas"><p class="rot">32 pantallas</p><ol>']
    for clave in claves:
        dibujo, _png, (si, no), _r = hechos.get (clave, (None, None, (0, 0), []))
        if dibujo is None:
            continue
        nombre, _ = NOMBRES.get (clave, (clave.upper(), ""))
        indice.append ('<li><a href="#s-%s"><span>%s</span><em>%d/%d</em></a></li>'
                       % (clave or "cara", esc (nombre), si, si + no))
    indice.append ('</ol></nav>')

    with open (os.path.join (SALIDA, "index.html"), "w", encoding="utf8") as f:
        f.write (doc[0] + "\n" + "\n".join (indice)
                 + '\n<main>\n' + "\n".join (doc[1:]) + "\n</main>\n</div>\n")

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

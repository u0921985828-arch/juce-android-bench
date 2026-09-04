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
    "plato":    ("LA CARA CON UN EFECTO PUESTO", "cara, ranura con DLY"),
    "rack":     ("RACK, RANURAS VACIAS",       "EL PAD, tapa RACK"),
    "rackf":    ("RACK CON LAS SEIS LLENAS",   "EL PAD, tapa RACK"),
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

#  UN PLANO ES LA PAGINA, LITERAL. La paleta no se inventa: es la de la app,
#  fila por fila de ZatiLookAndFeel::skinTable, y la letra tambien -Oswald para
#  las tapas y los numeros de pad, JetBrains Mono para las lecturas-, que son
#  las dos que la app lleva dentro y las dos estan en Google Fonts.
#
#  La primera version pintaba un rectangulo de color por control y no era un
#  plano: los dieciseis pads, los trece mandos y las seis pestanas salian
#  iguales, o sea la lista de numeros otra vez pero en colores. Un plano se
#  lee como la pagina o no sirve para decidir nada sobre la pagina.
CARCASAS = {
  0: dict (nombre="PAPEL", top="#f4efe3", mid="#ece6d8", bot="#e0d9c8",
           panel="#eee9dc", panelHi="#fffdf7", panelLo="#c7bfac",
           key="#ded7c6", keyLit="#cfc7b4", screw="#b3ab98",
           plate="#b3aa93", plateEdge="#8f8774",
           ink="#26221b", inkDim="#6b6355", inkLight="#f4efe3", white="#fffdf7",
           padTop="#ded7c6", padBg2="#e9e3d4", padBorder="#cdc5b2",
           lcd="#14120f", lcdFg="#ece7d9", lcdDim="#8b8375",
           accent="#26221b", accentBright="#4a4438", accentDim="#14120e"),
  1: dict (nombre="GRAFITO", top="#23211e", mid="#1b1a17", bot="#141311",
           panel="#262421", panelHi="#35322d", panelLo="#0f0e0d",
           key="#302d29", keyLit="#3d3934", screw="#4a4640",
           plate="#383430", plateEdge="#4d4842",
           ink="#e8e3d7", inkDim="#9a9384", inkLight="#1b1a17", white="#f4efe3",
           padTop="#302d29", padBg2="#2a2724", padBorder="#45403a",
           lcd="#0c0b0a", lcdFg="#e8e3d7", lcdDim="#8b8375",
           accent="#e8e3d7", accentBright="#fffdf7", accentDim="#bdb7a8"),
  2: dict (nombre="ACERO", top="#eef0f2", mid="#e2e5e9", bot="#d2d6db",
           panel="#e8ebee", panelHi="#fbfcfd", panelLo="#b4bac1",
           key="#d6dae0", keyLit="#c6cbd2", screw="#a3a9b1",
           plate="#a8aeb6", plateEdge="#868c94",
           ink="#1e2328", inkDim="#5c646d", inkLight="#eef0f2", white="#ffffff",
           padTop="#d6dae0", padBg2="#e0e4e8", padBorder="#bcc2ca",
           lcd="#10141a", lcdFg="#e4e9ee", lcdDim="#828b95",
           accent="#1e2328", accentBright="#3d454e", accentDim="#11151a"),
  3: dict (nombre="LACA", top="#1b4a4f", mid="#14393d", bot="#0e2c30",
           panel="#1a4247", panelHi="#2a5b61", panelLo="#081f22",
           key="#33666f", keyLit="#3d7580", screw="#4d8a95",
           plate="#11201f", plateEdge="#2a4a48",
           ink="#f2e8d5", inkDim="#9ab5b3", inkLight="#0e2c30", white="#fffdf7",
           padTop="#1c2b2b", padBg2="#172525", padBorder="#35514f",
           lcd="#08191c", lcdFg="#e9f2ee", lcdDim="#7fa39f",
           accent="#f0a830", accentBright="#ffc35c", accentDim="#c1811f"),
}

#  QUE ES CADA COSA, por su clase de C++: el volcado trae el `path` con el
#  nombre del tipo, asi que un pad se dibuja como un pad y un mando como un
#  mando.
def clase (r):
    return r.get ("path", "").split ("/")[-1].split (":")[-1]


#  LOS ICONOS DE VERDAD, no un cuadradito que los represente.
#
#  `ZATI_ICONOS=n` vuelca cada dibujo como una rejilla de alfas, que es la
#  misma que juzga Tests/iconos.py. Se convierte en tiras horizontales -un
#  <rect> por racha de pixeles seguidos, que es lo que hace que 24x24 no sean
#  576 rectangulos- y se define UNA vez por pagina en <defs>. Sin esto el plano
#  no puede contestar la pregunta para la que se hizo: cual es el dibujo que
#  falta.
_ICONOS = None

def iconos_del_binario (n=24):
    global _ICONOS
    if _ICONOS is not None:
        return _ICONOS
    _ICONOS = {}
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_OPEN": "pads", "ZATI_ICONOS": str (n),
                 "DISPLAY": os.environ.get ("DISPLAY", ":99")})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True,
                              timeout=300).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return _ICONOS
    for linea in out.splitlines():
        linea = linea.strip()
        if not (linea.startswith ("{") and '"px"' in linea):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        m, k = d["px"], d["n"]
        tiras = []
        for y in range (k):
            x = 0
            while x < k:
                if int (m[(y * k + x) * 2:(y * k + x) * 2 + 2], 16) > 96:
                    x0 = x
                    while x < k and int (m[(y * k + x) * 2:(y * k + x) * 2 + 2], 16) > 96:
                        x += 1
                    tiras.append ((x0, y, x - x0, 1))
                else:
                    x += 1
        _ICONOS[d["icono"]] = (k, tiras)
    return _ICONOS


def esc (t):
    return (str (t).replace ("&", "&amp;").replace ("<", "&lt;")
                   .replace (">", "&gt;").replace ('"', "&quot;"))


#  CUANTO CUERPO DE LETRA CABE. Oswald es condensada -0.46 del cuerpo por
#  caracter, medido contra los rotulos del volcado- y el mono 0.60. Se encoge
#  hasta que cabe en vez de cortar la palabra: un plano con "AJUST..." no dice
#  si el rotulo cabia.
def cuerpo (texto, ancho, tope, mono=False):
    if not texto:
        return tope
    razon = 0.60 if mono else 0.46
    return max (4.5, min (tope, ancho / (len (texto) * razon + 0.6)))


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
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Oswald:wght@400;500;600&family=Barlow:wght@400;500&family=JetBrains+Mono:wght@400;500&display=swap">
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
h1{font-family:"Oswald",sans-serif; font-weight:600; font-size:clamp(30px,5vw,46px);
   letter-spacing:.02em; margin:0; text-wrap:balance; text-transform:uppercase}
.sub{margin:6px 0 0; color:var(--apagado); max-width:62ch}
.sello{font-family:"JetBrains Mono",ui-monospace,monospace; font-size:12px; color:var(--apagado);
       text-align:right; line-height:1.7; font-variant-numeric:tabular-nums}
.sello b{color:var(--tinta); font-weight:500}

/*  La leyenda dice que significa cada marca del dibujo. Sin ella el plano es
    un monton de rectangulos de colores.                                     */
.leyenda{grid-column:1/-1; display:flex; flex-wrap:wrap; gap:8px 22px;
         font-family:"JetBrains Mono",ui-monospace,monospace; font-size:12px; color:var(--apagado);
         border:1px solid var(--filo); background:var(--tarjeta); border-radius:3px; padding:12px 16px}
.leyenda span{display:inline-flex; align-items:center; gap:7px}
.marca{width:11px; height:11px; border-radius:2px; flex:none}
.m-si{background:var(--ambar)}
.m-no{border:1.5px solid var(--rojo)}
.m-rot{border:1.5px dashed var(--ambar)}
.m-b{background:var(--azul)}

.indice{align-self:start; position:sticky; top:24px}
@media (max-width:900px){ .indice{position:static} }
.indice .rot{font-family:"JetBrains Mono",ui-monospace,monospace; font-size:11px; letter-spacing:.14em;
             text-transform:uppercase; color:var(--apagado); margin:0 0 8px}
.indice ol{list-style:none; margin:0; padding:0; display:flex; flex-direction:column;
           border-left:1px solid var(--filo)}
.indice a{display:flex; justify-content:space-between; gap:10px; align-items:baseline;
          padding:5px 10px; color:var(--apagado); text-decoration:none; font-size:13.5px;
          border-left:2px solid transparent; margin-left:-1px}
.indice a:hover,.indice a:focus-visible{color:var(--tinta); border-left-color:var(--azul);
                                        background:var(--tarjeta); outline:none}
.indice em{font-family:"JetBrains Mono",ui-monospace,monospace; font-style:normal; font-size:11.5px;
           font-variant-numeric:tabular-nums; opacity:.75; flex:none}

main{display:flex; flex-direction:column; gap:44px; min-width:0}
section{min-width:0; scroll-margin-top:20px}
.cab{display:flex; flex-wrap:wrap; gap:8px 20px; align-items:flex-end; justify-content:space-between;
     border-bottom:1px solid var(--filo); padding-bottom:8px; margin-bottom:16px}
h2{font-family:"Oswald",sans-serif; font-weight:600; text-transform:uppercase;
   letter-spacing:.05em; font-size:21px; margin:0; color:var(--tinta)}
.ruta{margin:2px 0 0; font-size:13px; color:var(--apagado)}
.cifras{display:flex; gap:8px; flex:none}
.chip{font-family:"JetBrains Mono",ui-monospace,monospace; font-size:11px; letter-spacing:.04em;
      padding:3px 9px; border-radius:2px; border:1px solid var(--filo); color:var(--apagado);
      font-variant-numeric:tabular-nums; white-space:nowrap}
.chip b{font-weight:500}
.chip.si b{color:var(--ambar)} .chip.no b{color:var(--rojo)}

.par{display:flex; gap:22px; align-items:flex-start; flex-wrap:wrap}
figure{margin:0; display:flex; flex-direction:column; gap:7px; min-width:0; max-width:100%}
.lienzo{background:var(--plano); border:1px solid var(--planofilo); border-radius:3px;
        padding:6px; overflow-x:auto; max-width:100%}
svg,img{display:block; max-width:100%; height:auto}
figcaption{font-family:"JetBrains Mono",ui-monospace,monospace; font-size:11px; color:var(--apagado);
           letter-spacing:.03em}
/*  LAS COTAS, apagadas por defecto. Cada SVG las lleva dentro en su propio
    grupo; el interruptor vive en la pagina y no en el dibujo, asi que se ven
    las treinta y dos a la vez o ninguna -comparar dos pantallas con una
    acotada y la otra no es como no acotar-.                                 */
/*  El interruptor es hermano de .hoja, asi que la cascada baja por ahi: con
    `~ main` no encuentra nada porque main vive DENTRO de .hoja.             */
.cotas{display:none}
#vercotas:checked ~ .hoja .cotas{display:inline}
#vercotas{position:absolute; opacity:0; width:0; height:0}
.interruptor{grid-column:1/-1; display:flex; gap:14px; align-items:center; flex-wrap:wrap;
  font-family:"JetBrains Mono",ui-monospace,monospace; font-size:11.5px; color:var(--apagado)}
.interruptor label{cursor:pointer; border:1px solid var(--filo); background:var(--tarjeta);
  border-radius:3px; padding:7px 13px; letter-spacing:.06em; color:var(--tinta); user-select:none}
#vercotas:checked ~ .hoja .interruptor label{border-color:var(--azul); color:var(--azul)}
#vercotas:focus-visible ~ .hoja .interruptor label{outline:2px solid var(--azul); outline-offset:2px}
a:focus-visible{outline:2px solid var(--azul); outline-offset:2px}
@media (prefers-reduced-motion:no-preference){ .indice a{transition:color .12s,border-color .12s} }
</style>
<input type="checkbox" id="vercotas">
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
</div>
<div class="interruptor"><label for="vercotas">COTAS · medidas, regla de 8 px y lo que no llega al dedo</label>
<span>40 px es Metrics::hit; la regla marca gordo cada 40 y fino cada 8, que es Metrics::gap</span></div>"""


def corre (clave, tam, lang, extra=None):
    """Una corrida, con HOME propio. Devuelve las lineas JSON del volcado."""
    casa = tempfile.mkdtemp (prefix="zati-plano-")

    #  EL TOUR DE BIENVENIDA, VISTO YA.
    #
    #  Cada corrida estrena HOME, asi que para la app siempre es la primera vez
    #  y sale el tour. Con ZATI_AUDIT no se enseña pero con ZATI_SHOT si, o sea
    #  que el plano salia la pagina y la foto de al lado la MISMA pagina bajo el
    #  velo del tour: dos dibujos del mismo sitio contando cosas distintas, que
    #  es justo lo que este fichero existe para no hacer. La marca es la que
    #  escribe MainComponent::tourFile.
    marca = os.path.join (casa, ".config", "zati-tour.txt")
    try:
        os.makedirs (os.path.dirname (marca), exist_ok=True)
        with open (marca, "w") as f:
            f.write ("1")
    except OSError:
        pass

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


def svg (clave, filas, tam, piel=0, marcados=frozenset()):
    """LA PAGINA, DIBUJADA. Cada control como lo que es y no como un rectangulo.

    El volcado trae la clase de C++ en el `path`, el estado en `on`, el rotulo
    en `text` y el dibujo en `icono`, asi que hay de sobra para redibujar la
    pagina: un pad se dibuja como un pad, un mando giratorio como un anillo con
    su aguja, una rejilla de pasos con sus 256 celdas y una ficha como una
    tarjeta encima de la cara oscurecida, que es exactamente lo que es.
    """
    C = CARCASAS.get (piel, CARCASAS[0])
    raiz = next ((r for r in filas if r.get ("root")), None)
    if raiz is None:
        return None, (0, 0)
    W, H = raiz["w"], raiz["h"]

    comps = [r for r in filas if "path" in r and r["w"] > 0 and r["h"] > 0]
    rots  = [r for r in filas if "rotulo" in r]
    ICO   = iconos_del_binario()

    #  POR CAPAS Y LUEGO POR PROFUNDIDAD.
    #
    #  El volcado trae `capa`: la cara es la 0 y cada ficha lleva la suya. La
    #  primera version pintaba el velo y la tarjeta ANTES de todo y las tapas
    #  de la cara caian encima, o sea la ficha debajo de la maquina que viene a
    #  tapar - justo al reves de lo que se ve. Se pinta la cara entera, luego
    #  el velo y la tarjeta, y luego lo que vive en esa ficha.
    comps.sort (key=lambda r: (r.get ("capa", 0), r.get ("depth", 0), r["y"], r["x"]))

    #  EL NUMERO DE UN PAD SALE DE SU SITIO. El volcado trae `pad` y vale 1 en
    #  los dieciseis -no es el indice- y `on` vale 1 en casi todo, que no es el
    #  estado tampoco: la primera version pinto los dieciseis pads encendidos y
    #  numerados "02". La rejilla se numera de ABAJO ARRIBA, como la cara.
    pads = sorted ([r for r in comps if "PadButton" in clase (r)],
                   key=lambda r: (-r["y"], r["x"]))
    numPad = {r["path"]: i + 1 for i, r in enumerate (pads)}

    p = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" height="%d" '
         'font-family="Oswald, Arial Narrow, sans-serif">' % (W, H, W, H)]

    #  EL CHASIS: el degradado de verdad, de `top` a `bot`. Es lo primero que
    #  se ve de la maquina y lo unico que no es un componente.
    p.append ('<defs><linearGradient id="ch" x1="0" y1="0" x2="0" y2="1">'
              '<stop offset="0" stop-color="%s"/><stop offset=".5" stop-color="%s"/>'
              '<stop offset="1" stop-color="%s"/></linearGradient>' % (C["top"], C["mid"], C["bot"]))

    #  Los iconos que salen en ESTA pagina, definidos una vez cada uno.
    usados = sorted ({r["icono"] for r in comps if r.get ("icono")})
    for nom in usados:
        if nom not in ICO:
            continue
        n, tiras = ICO[nom]
        cuerpo_ = "".join ('<rect x="%d" y="%d" width="%d" height="1"/>' % (a, b, c) for a, b, c, _ in tiras)
        p.append ('<symbol id="i-%s" viewBox="0 0 %d %d">%s</symbol>' % (nom, n, n, cuerpo_))
    p.append ('</defs>')
    p.append ('<rect width="%d" height="%d" fill="url(#ch)"/>' % (W, H))

    def texto (t, x, y, fs, color, anchor="middle", mono=False, peso=500, cabe=None):
        """Un rotulo. Con `cabe`, cenido a ese ancho y nunca mas ancho.

        La razon de avance de Oswald se estimaba en 0.46 del cuerpo y se queda
        corta segun las letras: "CANCION" salia cortada por el filo de su
        pestana en el plano mientras la app la dibuja entera -y el banco dice
        0 TRUNC en las 896 corridas-. Un plano que corta lo que la pantalla no
        corta miente sobre lo unico que se mira en el. Con textLength el
        navegador aprieta hasta que cabe, que es ademas lo que hace
        drawFittedText."""
        lim = ''
        if cabe is not None and len (t) * fs * 0.46 > cabe:
            lim = ' textLength="%.1f" lengthAdjust="spacingAndGlyphs"' % max (4.0, cabe)
        return ('<text x="%.1f" y="%.1f" font-size="%.1f" fill="%s" text-anchor="%s"%s'
                ' font-weight="%d" letter-spacing=".02em"%s>%s</text>'
                % (x, y, fs, color, anchor,
                   ' font-family="JetBrains Mono, ui-monospace, monospace"' if mono else "",
                   peso, lim, esc (t)))

    #  LA FICHA, que es una tarjeta encima de la cara OSCURECIDA. Es la unica
    #  pieza que no sale del volcado tal cual -el componente Sheet ocupa la
    #  ventana entera y la tarjeta la pinta el- asi que se deduce de la caja
    #  que ocupan sus hijos, que es de donde sale en la app.
    #  UN PLANO ENSEÑA LA CARA Y LA FICHA QUE ESA CLAVE ABRE. Ni una mas.
    #
    #  Con un HOME nuevo el tour de bienvenida esta MAQUETADO aunque con
    #  ZATI_AUDIT no se enseñe, asi que su capa sale en el volcado: el plano de
    #  LA CARA salia con las tapas ATRAS / SIGUIENTE / SALTAR de una ficha que
    #  no esta en la pantalla.
    #
    #  Y el primer intento -tirar las capas que ocupan la ventana entera- se
    #  llevo por delante la ficha de la MEZCLA: la caja de una capa se sacaba
    #  incluyendo su propio `Sheet`, y un Sheet SIEMPRE mide la ventana entera
    #  porque es quien pinta el velo. Medir una tarjeta incluyendo el velo que
    #  la rodea da siempre la pantalla.
    capas = sorted ({r.get ("capa", 0) for r in comps if r.get ("capa", 0)},
                    key=lambda c: -sum (1 for r in comps if r.get ("capa", 0) == c))
    quedan = set() if not clave else set (capas[:1])
    comps = [r for r in comps if r.get ("capa", 0) in quedan or not r.get ("capa", 0)]
    esTour = (clave or "").startswith ("tour")

    def tarjeta (capa):
        """El velo del 45% y la tarjeta, cuando empieza una ficha.

        La tarjeta no sale del volcado tal cual -el componente Sheet ocupa la
        ventana entera y la dibuja el- asi que se deduce de la caja que ocupan
        sus hijos, que es de donde sale en la app."""
        hijos = [r for r in comps if r.get ("capa", 0) == capa
                 and "Sheet" not in clase (r) and "XyPanel" not in clase (r)]
        if not hijos or esTour:
            return
        x0 = max (0, min (r["x"] for r in hijos) - 14)
        x1 = min (W, max (r["x"] + r["w"] for r in hijos) + 14)
        y0 = max (0, min (r["y"] for r in hijos) - 42)
        y1 = min (H, max (r["y"] + r["h"] for r in hijos) + 14)
        p.append ('<rect width="%d" height="%d" fill="#000" opacity=".45"/>' % (W, H))
        p.append ('<rect x="%.0f" y="%.0f" width="%.0f" height="%.0f" rx="10" fill="%s" '
                  'stroke="%s" stroke-width="1.5"/>'
                  % (x0, y0, x1 - x0, y1 - y0, C["panel"], C["plateEdge"]))

    capaVista = 0
    for r in comps:
        if r.get ("capa", 0) != capaVista:
            capaVista = r.get ("capa", 0)
            tarjeta (capaVista)
        k    = r.get ("kind", "other")
        cl   = clase (r)
        x, y, w, h = r["x"], r["y"], r["w"], r["h"]
        cx, cy = x + w / 2.0, y + h / 2.0
        t    = str (r.get ("text", "") or "")

        #  LOS LIENZOS SON DE TIPO `other`, asi que el salto de los contenedores
        #  va DESPUES de mirar si esto es uno. La primera version saltaba todo
        #  `other` de entrada y la ficha SEC salia sin su rejilla de pasos, que
        #  es justo lo que esa pagina es: 256 celdas y catorce tapas alrededor.
        lienzo = (next ((q for q in REJILLAS if q[0] in cl), None) is not None
                  or any (q in cl for q in ("Spectrum", "Wave", "Display", "Preview",
                                            "Meter", "Keyboard", "Teclado")))
        if "Sheet" in cl or "XyPanel" in cl:
            continue    # la tarjeta se dibuja aparte, de la caja de sus hijos

        #  EL ROTULO QUE VIVE DENTRO DE UN MANDO es el nombre y la lectura que
        #  la persona ve -SWING, TEMPO, "120 BPM"- y se saltaba entero: los
        #  mandos salian como carriles pelados sin decir de que son.
        if r.get ("inSlider"):
            if t:
                p.append (texto (t, cx, cy + 3.5, cuerpo (t, w, min (10.5, max (7.0, h))),
                                 C["inkDim"], mono=any (ch.isdigit() for ch in t)))
            continue
        if k == "other" and not lienzo:
            continue    # un contenedor no se ve

        # ---- EL PAD ---------------------------------------------------------
        if "PadButton" in cl:
            #  EL COLOR DEL PAD, que es como se encuentra un sonido en esta
            #  maquina: un pad CARGADO lleva su zati -relleno flojo, borde a
            #  tope- y uno vacio no lleva ninguno. Dibujar los dieciseis del
            #  mismo color es borrar la unica cosa que los distingue de lejos.
            #  `toDisplayString(false)` devuelve el hex PELADO -"E8544A"- y sin
            #  la almohadilla no es un color: los dieciseis pads salian grises,
            #  o sea exactamente el fallo que este dato venia a arreglar.
            frag = r.get ("color")
            if frag and not frag.startswith ("#"):
                frag = "#" + frag
            if r.get ("loaded") and frag:
                p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="5" fill="%s"/>'
                          % (x, y, w, h, C["padBg2"]))
                p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="5" fill="%s" '
                          'fill-opacity=".30" stroke="%s" stroke-width="1.5"/>'
                          % (x, y, w, h, frag, frag))
            else:
                p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="5" fill="%s" stroke="%s"/>'
                          % (x, y, w, h, C["padTop"], C["padBorder"]))
            tinta = C["ink"]
            p.append (texto ("%02d" % numPad.get (r["path"], 0), x + 5, y + 15, 13,
                             tinta if r.get ("loaded") else C["inkDim"], "start", peso=600))
            #  Un pad CON sonido lleva su onda; uno vacio esta vacio, que es la
            #  diferencia que se mira al abrir la rejilla.
            if r.get ("loaded"):
                pasos_ = max (6, int (w // 5))
                d = []
                for i in range (pasos_):
                    a = (0.20 + 0.62 * abs (((i * 7) % 11) / 11.0 - 0.5) * 2) * (h * 0.20)
                    d.append ('<rect x="%.1f" y="%.1f" width="2" height="%.1f" fill="%s"/>'
                              % (x + 5 + i * (w - 10) / pasos_, y + h - 10 - a, a * 2, C["inkDim"]))
                p.append ("".join (d))
            if t:
                fs = cuerpo (t, w - 8, 9.5)
                p.append (texto (t, cx, y + h - 4, fs, tinta, cabe=w - 8))
            continue

        # ---- UN MANDO -------------------------------------------------------
        if k == "slider":
            lectura = next ((c for c in filas if c.get ("inSlider")
                             and c.get ("path", "").startswith (r["path"] + "/")
                             and str (c.get ("text", "")).strip()), None)
            val = str (lectura.get ("text", "")) if lectura else ""
            #  GIRATORIO O CARRIL lo dice el volcado (`estilo`), no la
            #  proporcion de la caja: la caja de un mando incluye su rotulo, asi
            #  que los tres mandos de la cara -que son redondos- salian
            #  dibujados como carriles por medir mas de ancho que de alto.
            est = r.get ("estilo")
            if est == "rot" if est else (0.65 < (w / float (h)) < 1.6 and h >= 24):
                #  GIRATORIO: cuerpo oscuro, anillo y aguja, como lo dibuja
                #  ZatiLookAndFeel. El hueco de abajo es el del mando de verdad.
                rad = min (w, h) * 0.36
                p.append ('<circle cx="%.1f" cy="%.1f" r="%.1f" fill="%s" stroke="%s"/>'
                          % (cx, cy - h * 0.06, rad, C["lcd"], C["plateEdge"]))
                p.append ('<path d="M %.1f %.1f A %.1f %.1f 0 1 1 %.1f %.1f" fill="none" '
                          'stroke="%s" stroke-width="2" stroke-linecap="round"/>'
                          % (cx - rad * 0.78, cy - h * 0.06 + rad * 0.62, rad * 1.0, rad * 1.0,
                             cx + rad * 0.78, cy - h * 0.06 + rad * 0.62, C["accent"]))
                p.append ('<line x1="%.1f" y1="%.1f" x2="%.1f" y2="%.1f" stroke="%s" stroke-width="2"/>'
                          % (cx, cy - h * 0.06, cx + rad * 0.55, cy - h * 0.06 - rad * 0.55, C["lcdFg"]))
                if val:
                    p.append (texto (val, cx, y + h - 1, cuerpo (val, w, 9, True), C["inkDim"], mono=True))
            else:
                #  DESLIZADOR: carril hundido y su puno.
                if (est == "linh") if est else (w >= h):
                    p.append ('<rect x="%d" y="%.1f" width="%d" height="4" rx="2" fill="%s"/>'
                              % (x, cy - 2, w, C["plate"]))
                    p.append ('<rect x="%.1f" y="%.1f" width="9" height="%.1f" rx="2" fill="%s" stroke="%s"/>'
                              % (x + w * 0.62 - 4, y + 2, max (8.0, h - 4), C["key"], C["plateEdge"]))
                else:
                    p.append ('<rect x="%.1f" y="%d" width="4" height="%d" rx="2" fill="%s"/>'
                              % (cx - 2, y, h, C["plate"]))
                    p.append ('<rect x="%.1f" y="%.1f" width="%.1f" height="9" rx="2" fill="%s" stroke="%s"/>'
                              % (x + 2, y + h * 0.38, max (8.0, w - 4), C["key"], C["plateEdge"]))
            continue

        # ---- UNA CAJA DE TEXTO ---------------------------------------------
        if k == "editor":
            p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="3" fill="%s" stroke="%s"/>'
                      % (x, y, w, h, C["lcd"], C["plateEdge"]))
            if t:
                p.append (texto (t, x + 6, cy + 4, cuerpo (t, w - 12, min (12.0, h - 6), True),
                                 C["lcdFg"], "start", mono=True))
            continue

        # ---- UN ROTULO ------------------------------------------------------
        if k == "label":
            if t:
                p.append (texto (t, x, cy + 3.5, cuerpo (t, w, min (11.0, h)), C["inkDim"], "start"))
            continue

        # ---- LAS TRES REJILLAS DE LIENZO -----------------------------------
        rej = next (((n, c, l, ca) for n, c, l, ca in REJILLAS if n in cl), None)
        if rej is not None:
            n, cols, carriles, canal = rej
            p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="3" fill="%s" stroke="%s"/>'
                      % (x, y, w, h, C["lcd"], C["plateEdge"]))
            if w > canal and h > 0:
                cw = (w - canal) / float (cols); ch = h / float (carriles)
                #  El canal de la izquierda, que en las tres dice de que fila es.
                p.append ('<rect x="%d" y="%d" width="%d" height="%d" fill="%s" opacity=".5"/>'
                          % (x, y, canal, h, C["plate"]))
                for c in range (cols + 1):
                    gx = x + canal + c * cw
                    p.append ('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="%s" stroke-width="%s" opacity="%s"/>'
                              % (gx, y, gx, y + h, C["lcdDim"], "1" if c % 4 == 0 else "0.5",
                                 ".9" if c % 4 == 0 else ".35"))
                for c in range (carriles + 1):
                    gy = y + c * ch
                    p.append ('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" stroke-width="0.5" opacity=".35"/>'
                              % (x, gy, x + w, gy, C["lcdDim"]))
                #  La cota de la celda, que es lo que este banco mide de ellas.
                p.append (texto ("%s  celda %.1f x %.1f" % (n, cw, ch), x + canal + 2, y + h - 3,
                                 8, C["lcdDim"], "start", mono=True))
            continue

        # ---- EL ESPECTRO / LO QUE ES UN CRISTAL ----------------------------
        if "Spectrum" in cl or "Wave" in cl or "Display" in cl or "Preview" in cl:
            p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="3" fill="%s" stroke="%s"/>'
                      % (x, y, w, h, C["lcd"], C["plateEdge"]))
            barras = max (8, int (w // 6))
            for i in range (barras):
                a = (0.25 + 0.7 * abs (((i * 5) % 13) / 13.0 - 0.5) * 2) * (h - 8)
                p.append ('<rect x="%.1f" y="%.1f" width="3" height="%.1f" fill="%s" opacity=".8"/>'
                          % (x + 3 + i * (w - 6) / barras, y + h - 4 - a, a, C["lcdFg"]))
            continue

        # ---- UNA TAPA -------------------------------------------------------
        relleno, tinta = C["key"], C["ink"]
        p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="%s" stroke="%s" stroke-width="1"/>'
                  % (x, y, w, h, relleno, C["padBorder"]))

        ico = r.get ("icono")
        lado = min (float (r.get ("icoW") or 0), float (r.get ("icoH") or 0))
        if ico and lado >= 6 and ico in ICO:
            #  El dibujo va a la IZQUIERDA del rotulo y con su tinta, que es
            #  como lo coloca reparteTapa.
            fs  = cuerpo (t, w - lado - 12, min (13.0, max (8.0, h * 0.42)))
            anc = len (t) * fs * 0.46
            ix  = cx - (anc + lado + 4) / 2.0
            p.append ('<use href="#i-%s" x="%.1f" y="%.1f" width="%.1f" height="%.1f" fill="%s"/>'
                      % (ico, ix, cy - lado / 2.0, lado, lado, tinta))
            if t:
                p.append (texto (t, ix + lado + 4, cy + fs * 0.35, fs, tinta, "start",
                                 cabe=w - lado - 12))
        elif t:
            fs = cuerpo (t, w - 8, min (13.0, max (8.0, h * 0.42)))
            p.append (texto (t, cx, cy + fs * 0.35, fs, tinta, cabe=w - 8))

        #  LA MARCA DEL BANCO: una tapa con rotulo y SIN dibujo lleva un punto
        #  rojo en la esquina. Es lo unico del plano que no esta en la pantalla
        #  -es la pregunta para la que se hizo- y por eso es lo unico que no
        #  usa la paleta de la carcasa.
        if r["path"] in marcados:
            p.append ('<circle cx="%.1f" cy="%.1f" r="3" fill="#e8756a" '
                      'stroke="%s" stroke-width="1"/>' % (x + w - 4, y + 4, C["panel"]))

    #  Y LOS ROTULOS QUE SE PINTAN, que no son componentes -el titulo de una
    #  ficha, el nombre de una seccion- y son media pantalla.
    for r in rots:
        t = r["rotulo"]
        grande = r.get ("tipo") == "titulo"
        p.append (texto (t, r["x"], r["y"] + min (14, max (10, r["h"] * 0.8)),
                         cuerpo (t, max (20, r["w"]), 15 if grande else 11),
                         C["ink"] if grande else C["inkDim"], "start", peso=600 if grande else 500))

    #  LA CAPA DE COTAS, apagada por defecto y encendida desde la pagina.
    #
    #  Un plano para MIRAR y un plano para REHACER no son el mismo dibujo: para
    #  mover algo hace falta saber cuanto mide, cuanto aire tiene al lado y
    #  contra que topa. Va en su propio grupo para que no ensucie el primero.
    cot = ['<g class="cotas">']

    #  La regla, cada Metrics::gap -8 px- con marca gorda cada 40, que es el
    #  dedo minimo: los margenes se leen contando, sin medir a ojo.
    for gx in range (0, W + 1, 8):
        gordo = (gx % 40 == 0)
        cot.append ('<line x1="%d" y1="0" x2="%d" y2="%d" stroke="%s" stroke-width="0.5" opacity="%s"/>'
                    % (gx, gx, 7 if gordo else 4, C["accent"], ".55" if gordo else ".28"))
    for gy in range (0, H + 1, 8):
        gordo = (gy % 40 == 0)
        cot.append ('<line x1="0" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="0.5" opacity="%s"/>'
                    % (gy, 7 if gordo else 4, gy, C["accent"], ".55" if gordo else ".28"))

    for r in comps:
        cl = clase (r)
        if "Sheet" in cl or "XyPanel" in cl or r.get ("inSlider"):
            continue
        x, y, w, h = r["x"], r["y"], r["w"], r["h"]
        if w < 26 or h < 12:
            continue    # en una celda de rejilla la cota tapa la celda

        #  POR DEBAJO DEL DEDO, marcado. Es la restriccion que un rediseno no
        #  puede saltarse: 40 px es Metrics::hit, y una tapa mas estrecha se
        #  falla al tocarla por mucho que se lea bien.
        if r.get ("kind") in ("button", "slider") and min (w, h) < 40:
            cot.append ('<rect x="%.1f" y="%.1f" width="%d" height="%d" rx="4" fill="none" '
                        'stroke="#e8756a" stroke-width="1" stroke-dasharray="3 2"/>'
                        % (x + 0.5, y + 0.5, w - 1, h - 1))
        cot.append ('<text x="%d" y="%d" font-size="7.5" fill="%s" '
                    'font-family="JetBrains Mono, ui-monospace, monospace" opacity=".85">%dx%d</text>'
                    % (x + 2, y + 8, C["accent"], w, h))
    cot.append ('</g>')
    p.append ("".join (cot))

    p.append ('</svg>')

    #  Las dos cifras del banco: cuantas tapas llevan dibujo y cuantas no.
    conIcono = sum (1 for r in comps if r.get ("kind") == "button" and r.get ("icono")
                    and str (r.get ("text", "")).strip())
    sinIcono = sum (1 for r in comps if r.get ("kind") == "button" and not r.get ("icono")
                    and str (r.get ("text", "")).strip())
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
                           [str (r["text"]) for r in sin],
                           [r["path"] for r in sin]))
    return fuera


def una (clave):
    tam  = os.environ.get ("ZATI_PLANOS_SIZE", "412x915")
    lang = os.environ.get ("ZATI_PLANOS_LANG", "es")

    #  LA CARCASA ES UNA ENTRADA, no lo que hubiera en las preferencias: el
    #  plano se dibuja con la MISMA con la que corrio la app, o el dibujo y la
    #  foto de al lado cuentan dos historias. Es lo que ZATI_SKIN existe para
    #  hacer, y por defecto es la 0 -PAPEL-, que es con la que la app abre.
    piel = int (os.environ.get ("ZATI_PLANOS_SKIN", "0"))
    filas = corre (clave, tam, lang, {"ZATI_SKIN": str (piel)})

    #  Los huecos, ANTES de dibujar: el plano marca en rojo las tapas que la
    #  regla senala y no "toda tapa sin dibujo", que son 369 y casi todas estan
    #  bien -un chip de banco o un mando de valor no lleva dibujo a proposito-.
    #  Un plano que marca en rojo lo que esta bien no se puede leer.
    rotos = huecos (filas)
    marcados = frozenset (q for _, _, _, rutas in rotos for q in rutas)
    dibujo, cuenta = svg (clave, filas, tam, piel, marcados)
    if dibujo is None:
        return clave, None, None, (0, 0), []

    #  Y la foto, en OTRA corrida: ZATI_SHOT y el volcado son excluyentes.
    png = os.path.join (SALIDA, "%s.png" % (clave or "cara"))
    corre (clave, tam, lang, {"ZATI_SHOT": png, "ZATI_SHOT_SCALE": "0.5",
                              "ZATI_SKIN": str (piel)})

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

    #  LOS ICONOS SE SACAN ANTES DE ABRIR LOS HILOS.
    #
    #  La cache se ponia a {} y se llenaba despues, asi que un segundo hilo
    #  veia "ya esta" y se llevaba el diccionario VACIO: media tanda de planos
    #  salia sin un solo dibujo, que es exactamente lo que estos planos existen
    #  para enseñar. Una cache perezosa con hilos es una carrera.
    iconos_del_binario()

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
        for y, con, sin, _rutas in rotos:
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

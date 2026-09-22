#!/usr/bin/env python3
"""ZATI — el banco de los paneles de grupo.

Un panel no lleva texto y no es un componente, asi que NINGUNA de las reglas
de expo.py le aplica: la geometria que vigilan es la de los controles, y un
panel es lo que se dibuja ALREDEDOR de ellos. Se anadieron a cuatro fichas y el
banco entero seguia en verde con el aire repartido a ojo.

AQUI DECIA «lo unico que un panel puede hacer mal es el REPARTO DEL AIRE» Y ERA
FALSO, y lo desmintieron dos quejas del telefono con dos capturas: «sigue
habiendo ese error de diseno en pad settings» -el panel de CINTA pisando los
rotulos de CORTE, RESON y ANCHO- y «como tambien en el apartado de ayuda» -la
pagina de GESTOS con dos paneles de OTRA pagina encima de la lista-. Las dos
llevaban meses a la vista, y las dos eran invisibles porque la premisa de esta
prueba decia que no habia nada mas que preguntar. Un panel SI puede solapar -lo
que NO envuelve, que es distinto de lo que envuelve- y SI puede salirse -del
marco de su ficha, que no es la ventana-. Son las reglas AJENO y MARCO de mas
abajo.

El reparto del aire son dos cosas distintas:

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

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

HERE = os.path.dirname (os.path.abspath (__file__))
APP  = os.path.join (HERE, "..", "build", "Zati_artefacts", "Release", "Zati")

#  Las mismas siete pantallas y los mismos cuatro idiomas que expo.py: una
#  pantalla que alli se mide y aqui no es una pantalla donde esto no se sabe.
#  Y los dos de en medio, por lo mismo que en `expo.py`: `wideFace` se decide
#  en ~556 px de area segura y el barrido no tenia nada a los dos lados de esa
#  raya. 640x360 cae justo encima -segunda cara- y 412x480 debajo.
SIZES = ["360x640", "393x851", "412x915", "344x882", "280x653", "800x1280",
         "915x412", "640x360", "412x480"]
LANGS = ["es", "en", "zh", "ar"]
#  Solo las fichas que llevan paneles. Abrir las otras veintitantas seria
#  cuadruplicar el tiempo para leer cero paneles en cada una.
#  Y el PIANO, que entra el dia que tiene paneles: era la unica de las tres
#  paginas de la ficha del secuenciador sin ninguno, asi que esta lista no lo
#  abria y una pagina sin paneles no se puede medir mal.
#  Y `vst`, que entra por lo mismo: sus tres paneles se rellenaban a mano con
#  `groove (0.16f)` -una SOMBRA, que en LACA deja el panel a 5.3 de dE contra la
#  tarjeta- y no publicaban `UiAudit::panel`, asi que eran invisibles para la
#  unica prueba que mide un panel. Desde que pasan por `pintaPaneles` se pueden
#  medir, y son cuatro y no tres: la cabecera y el preset son UNO.
#  Y `gest`, la pagina de AYUDA de AJUSTES, que faltaba: la app la abre desde
#  siempre -ZATI_OPEN=gest- y esta lista no la nombraba, asi que los dos
#  paneles que se le pintan encima no los medía nadie. Una ficha que no se
#  abre no puede fallar.
SHEETS = ["pads", "pad2", "pad3", "sec", "secp", "paso", "piano", "song", "set", "asp",
          "proj", "midi", "expo", "chop", "vst", "gest"]

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
    #  Y EL AIRE DEL PANEL SE LEE, no se deriva. Estaba escrito aqui como
    #  `gap // 2` porque asi lo definia el C++, y el dia que `panelAireX` paso
    #  a cero -para que la losa dejara de cruzar el margen de la tarjeta- esta
    #  prueba habria seguido midiendo contra un 4 que ya no existe, que es
    #  exactamente la forma de mentir que su propia cabecera dice evitar.
    px = re.search (r'int\s+panelAireX\s*=\s*(\d+)', src)
    if px is None: sys.exit ("no encuentro Metrics::panelAireX")
    halfGap = int (px.group (1))
    #  Y `lg`, que es el aire que la casa deja en el filo de una tarjeta. Sale
    #  de la misma linea de cinco tokens que `sm`.
    l = re.search (r'int\s+xs\s*=\s*\d+,\s*sm\s*=\s*\d+,\s*md\s*=\s*\d+,\s*lg\s*=\s*(\d+)', src)
    if l is None: sys.exit ("no encuentro Metrics::lg")
    return halfGap, sm // 4, sm, int (l.group (1))

AIRE_X, AIRE_Y, SEPARACION, FILO_TARJETA = tokens()


def run (size, lang, sheet, casa):
    env = dict (os.environ, DISPLAY=PANTALLA)
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

    #  EL RECTANGULO DE CADA FICHA, por capa. `sheetFromBottom` publica la
    #  tarjeta YA COLOCADA, asi que no hay que repetir ninguna cuenta del C++.
    #
    #  Y CONTRA LA TARJETA Y NO CONTRA EL HUECO UTIL, que es lo que la primera
    #  version preguntaba y saco **644 hallazgos sin un solo fallo**: un panel
    #  se dibuja con un `expanded (panelAireX, panelAireY)` incondicional, asi
    #  que un grupo que ocupa el ancho del contenido deja el panel 4 px dentro
    #  del margen de la ficha A PROPOSITO -es el aire del panel, y el margen de
    #  la tarjeta existe justo para que quepa-. Preguntar eso es preguntar si un
    #  `expanded` expande, que es la misma forma de mentir que ya costo el aire
    #  vertical retirado mas abajo. Lo que no tiene lectura legitima es salirse
    #  de la TARJETA: eso ya no esta en la ficha.
    #
    #  Y SOLO LAS QUE NO SE DESPLAZAN. En una ficha desplazable el contenido
    #  mide lo que pidio y lo que sobra se alcanza arrastrando, asi que un
    #  panel por debajo del filo es el funcionamiento y no el fallo.
    marcos = {}
    for t in rows:
        if not t.get ("tarjeta"): continue
        if t.get ("desplaza"): continue
        if t.get ("w", 0) <= 0 or t.get ("h", 0) <= 0: continue
        marcos[t.get ("capa", 0)] = (t["x"], t["y"], t["w"], t["h"])

    #  Y LOS MISMOS RECTANGULOS SIN LA EXENCION DE DESPLAZAR, para la pregunta
    #  de los LADOS. Una ficha que se desplaza lo hace en vertical: por abajo un
    #  panel puede asomar y es el funcionamiento; por los costados no se
    #  desplaza nada, asi que alli la exencion no tiene lectura.
    costados = {}
    for t in rows:
        if not t.get ("tarjeta"): continue
        if t.get ("w", 0) <= 0 or t.get ("h", 0) <= 0: continue
        costados[t.get ("capa", 0)] = (t["x"], t["w"])

    for pa in paneles:
        px, py, pw, ph = pa["x"], pa["y"], pa["w"], pa["h"]
        capa = pa.get ("capa", 0)
        quien = pa.get ("nombre") or f"panel en {px},{py}"

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
            fallos.append (("VACIO", tag, f"{quien} {pw}x{ph} en {px},{py} no envuelve ningun control"))
            continue

        izq = min (c["x"] for c in dentro)
        der = max (c["x"] + c["w"] for c in dentro)

        #  1. QUE ENVUELVA. Un control que se sale del panel por los lados.
        if izq < px or der > px + pw:
            fallos.append (("FUERA", tag,
                            f"{quien} [{px},{px+pw}] no cubre [{izq},{der}]"))
            continue

        #  1 bis. QUE NO PISE LO QUE NO ENVUELVE.
        #
        #     Esta es la queja «sigue habiendo ese error de diseno en pad
        #     settings» y estuvo meses a la vista. Un panel se dibuja ALREDEDOR
        #     de su grupo, asi que solapar a los suyos es lo que hace; lo que
        #     no tiene lectura legitima ninguna es tocar a un control cuyo
        #     centro cae FUERA, que es un grupo que se cerro sobre un
        #     rectangulo mas grande que lo que hay dentro. La pertenencia se
        #     decide por el centro, igual que arriba, y por eso la pregunta se
        #     puede hacer sin umbral: o el centro esta dentro o esta fuera.
        #
        #     Y contra OVERLAP de expo.py, que seria la forma perezosa de
        #     medirlo: alli un solape entre dos cosas que se tocan es un fallo
        #     SIEMPRE, y un panel solapa a los suyos por definicion. Dar `hit`
        #     a las filas de panel habria sacado un hallazgo por cada control
        #     de cada panel de la app - una medida que miente por el otro lado,
        #     que es la que da tantos numeros que nadie los lee.
        ajenos = []
        for c in ctrl:
            if c.get ("capa", 0) != capa: continue
            cx, cy = c["x"] + c["w"] // 2, c["y"] + c["h"] // 2
            if px <= cx <= px + pw and py <= cy <= py + ph: continue
            if c["x"] >= px + pw or c["x"] + c["w"] <= px: continue
            if c["y"] >= py + ph or c["y"] + c["h"] <= py: continue
            ajenos.append (c)
        if ajenos:
            c = ajenos[0]
            fallos.append (("AJENO", tag,
                            f"{quien} [{px},{py},{pw}x{ph}] pisa {len (ajenos)} "
                            f"control(es) que no envuelve, el primero "
                            f"{c.get ('path', c.get ('kind', '?'))} en "
                            f"{c['x']},{c['y']} de {c['w']}x{c['h']}"))

        #  1 ter. QUE NO SE SALGA DEL MARCO DE SU FICHA.
        #
        #     La otra mitad de la misma tanda: un panel puede caber en la
        #     ventana -asi que OFFSCREEN no lo ve- y aun asi salirse del hueco
        #     util de su tarjeta, porque `pintaPaneles` hace un `expanded`
        #     incondicional de panelAireX x panelAireY y el presupuesto de la
        #     ficha no cuenta esos cuatro pixeles. Se mide contra lo que la app
        #     publica -el rectangulo de la tarjeta- y no contra una constante
        #     copiada aqui.
        marco = marcos.get (capa)
        if marco is not None:
            mx, my, mw, mh = marco
            if px < mx or py < my or px + pw > mx + mw or py + ph > my + mh:
                fallos.append (("MARCO", tag,
                                f"{quien} [{px},{py},{px+pw},{py+ph}] se sale del marco "
                                f"[{mx},{my},{mx+mw},{my+mh}]"))

        #  1 quater. QUE POR LOS LADOS DEJE EL AIRE DEL FILO.
        #
        #     Caber dentro de la tarjeta no es estar bien puesto. `MARCO` mide
        #     lo primero -si el panel se SALE- y por eso dejaba pasar la queja
        #     «el area donde esta el otro color no es el correcto, por los lados
        #     tiene que tener mas aire sino queda feo»: la losa cabia de sobra y
        #     aun asi era el UNICO elemento de la app que cruzaba el margen de
        #     su propia tarjeta. Medido en 412x915, ficha del pad: tarjeta
        #     17..396, contenido 33..380 -dieciseis de margen- y panel 29..384,
        #     o sea a DOCE del filo contra los dieciseis de todo lo demas.
        #     `pintaPaneles` expande `panelAireX` sin condicion y el margen no
        #     lo llevaba dentro, asi que no era un caso: eran 225 de 225.
        #
        #     Y ESTA PRUEBA LO DIJO Y SE RETRACTO. Mas arriba esta escrito que
        #     preguntar esto «es preguntar si un expanded expande». Era cierto
        #     mientras el margen valia `lg` pelado -la respuesta estaba
        #     forzada- y dejo de serlo en cuanto el margen paso a
        #     `lg + halfGap`: ahora la respuesta puede salir mal, que es la
        #     unica condicion para que una pregunta sea una prueba.
        lado = costados.get (capa)
        if lado is not None:
            mx2, mw2 = lado
            izqT, derT = px - mx2, (mx2 + mw2) - (px + pw)
            if min (izqT, derT) < FILO_TARJETA:
                fallos.append (("FILO", tag,
                                f"{quien} deja {izqT}/{derT} px contra la tarjeta "
                                f"[{mx2},{mx2+mw2}] y el filo pide {FILO_TARJETA}"))

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
                            f"{quien}: filas que empiezan en "
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

    #  Y DESPUES SE CAYERON OTRAS DOS, las dos de la misma tanda y las dos
    #  medidas antes de creerlas. Llego «espacio de mas entre ciertas secciones,
    #  y luego otras que no hay espacio entre ellas», que se lee como que al
    #  minimo de abajo le falta su maximo. Ni ese maximo ni el aire vertical
    #  sobreviven a mirar lo que senalan:
    #
    #  - EL MAXIMO -«dos paneles de la misma columna quedan a `sm - 2 *
    #    panelAireY` y a nada mas»- saco **124 hallazgos y ni uno era un
    #    fallo**: 268 px en `sec` y 522 en tableta son la REJILLA DE PASOS
    #    sentada entre dos paneles, y 92 en AJUSTES son el recuadro de AUDIO.
    #    Dos paneles de una columna no son dos grupos ADYACENTES, y adyacente
    #    no se puede decidir desde el volcado: es la misma frase que la tercera
    #    regla de aqui arriba, con otra pieza. El minimo no tiene ese problema
    #    porque por debajo del token los dos paneles SE TOCAN, y eso no tiene
    #    lectura legitima ninguna.
    #
    #  - EL AIRE VERTICAL -que el contenido empiece `panelAireY` por debajo del
    #    filo y acabe otro tanto por encima- saco **464**, y la razon por la que
    #    no vale es peor: `pintaPaneles` hace UN `expanded (panelAireX,
    #    panelAireY)` sin condiciones, asi que preguntar eso es preguntar si un
    #    `expanded` expande — la prueba repitiendo la constante del codigo, que
    #    es como `Tests/icono.py` dio verde dos veces con la mascara del
    #    lanzador rota. Y lo unico que SI puede ver es otra cosa: un panel se
    #    deduce de las BANDAS de rotulo que `resized()` publica y lo que el
    #    volcado trae es el TEXTO, que dentro de su banda va centrado — de ahi
    #    los «19 px arriba» de `sec`. Eso no es un panel torcido.
    #
    #    Y contar los rotulos pintados como contenido, que fue el intento de
    #    salvarla, cuesta **21 FILAS falsos** en RECORTE: alli los cuatro
    #    deslizadores empiezan 68 px dentro porque a su izquierda va su nombre
    #    PINTADO, que es la excepcion que esta misma prueba ya tiene escrita
    #    arriba. Se fue con la regla que lo pedia.

    #  Y SIGUE SIENDO UN MINIMO Y NO UN VALOR EXACTO, que se probo y salio
    #  medido — ver el parrafo de arriba, que es donde viven las reglas que
    #  esta prueba ha tenido que retirar.
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
    casas = tempfile.mkdtemp (prefix="zati-paneles-")
    #  UN HOME POR TRABAJADOR, NO POR CORRIDA, y es una medida y no un gusto.
    #
    #  Lo que hay que evitar es que DOS PROCESOS A LA VEZ escriban el mismo
    #  `.sesion/samples`, que es la carrera que Tests/session.py existe para
    #  cazar; para eso basta con que no haya dos corridas simultaneas en la
    #  misma casa, y el reparto por indice de trabajador ya lo garantiza -es lo
    #  que hace Tests/expo.py:1442 desde que se escribio-.
    #
    #  Una casa por corrida costaba lo que nadie habia sumado: la app siembra
    #  la biblioteca de fabrica en cada HOME nuevo -64 WAV, ~18 MB- asi que las
    #  612 combinaciones de nueve pantallas pedian ~11 GB de disco de usar y
    #  tirar, y no se liberaba ni uno hasta el `rmtree` del final. Con siete
    #  pantallas eran 476 casas y entraba raspando; los dos tamanos que entran
    #  en esta tanda lo pasaron de largo y el contenedor se quedo SIN DISCO a
    #  mitad de corrida -«No space left on device», 0 B libres de 252 GB-, que
    #  no se lee como un fallo del banco sino como que todo deja de funcionar.
    #  Con una casa por trabajador son `nproc` casas y el pico no depende del
    #  numero de pantallas.
    #  Y el reparto vale porque el pool despacha EN ORDEN y con un trabajo por
    #  trabajador: las corridas vivas a la vez son una ventana de `trabajos`
    #  indices consecutivos, o sea `trabajos` casas distintas.
    trabajos = max (1, os.cpu_count() or 4)
    for i in range (trabajos):
        os.makedirs (os.path.join (casas, "c%02d" % i), exist_ok=True)
    combos = []
    for size in SIZES:
        for lang in LANGS:
            for sheet in SHEETS:
                casa = os.path.join (casas, "c%02d" % (len (combos) % trabajos))
                combos.append ((size, lang, sheet, casa))

    todos, paneles, corridas = [], 0, 0
    try:
        with ProcessPoolExecutor (max_workers=trabajos) as ex:
            for f, n in ex.map (una, combos):
                todos += f; paneles += n; corridas += 1
    finally:
        shutil.rmtree (casas, ignore_errors=True)

    porclase = defaultdict (list)
    for kind, tag, que in todos: porclase[kind].append ((tag, que))

    for kind in ("CRASH", "VACIO", "FUERA", "AJENO", "MARCO", "FILAS", "PEGADOS"):
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

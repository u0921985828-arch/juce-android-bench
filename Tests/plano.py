#!/usr/bin/env python3
"""EL PLANO DE CADA PANTALLA: titulos, secciones y controles en orden de lectura.

expo.py mide GEOMETRIA -dedo, solapes, ventana, celda, rotulo cortado- y no
sabe nada de ESTRUCTURA: si una ficha tiene titulo, si su titulo coincide con
la tapa que la abre, si las secciones van en un orden que se pueda aprender, o
si hay un encabezado sin nada debajo. Eso no se puede juzgar sin ver la
pantalla escrita, y una captura no se puede comparar con la de la semana que
viene.

Y hasta ahora ni siquiera era posible mirarlo: los titulos y los nombres de
seccion SE PINTAN, y las seis reglas recorren el arbol de COMPONENTES. Un
rotulo dibujado no es un componente. Por eso la mesa llevaba desde el primer
dia titulada "MIX" a mano -sin pasar por T(), con la fila MIX->MEZCLA en la
tabla y la pestana que la abre usandola- y no lo vio nadie. Ver
UiAudit::rotulo.

El plano se lee de arriba abajo, que es como se lee la pantalla:

    MEZCLA                                    412x915
      titulo    MEZCLA                         31,113  351x16
      seccion   ...
      [control] fader 01                       ...

    python3 Tests/plano.py              todas las fichas
    python3 Tests/plano.py set mix      solo esas
"""
import json, os, re, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import display_alive

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  La ficha, y la tapa que la abre. La segunda columna es lo que hay que poder
#  comparar con el titulo: tocas MEZCLA y tienes que aterrizar en MEZCLA.
FICHAS = [
    ("",       "la cara"),
    ("pads",   "PADS"),
    ("sec",    "SEC"),
    ("piano",  "SEC"),
    ("paso",   "SEC"),
    ("song",   "CANCION"),
    ("mix",    "MEZCLA"),
    ("xy",     "XY"),
    ("set",    "AJUSTES"),
    ("proj",   "AJUSTES"),
    ("gest",   "AJUSTES"),
    ("midi",   "AJUSTES"),
    ("manual", "AJUSTES"),
    ("expo",   "AJUSTES"),
    ("rack",   "MEZCLA"),
    ("chop",   "PADS"),
    ("browse", "CARGAR"),
    #  Y LAS TRES QUE FALTABAN, que es donde esta regla no podia decir que no.
    #
    #  El tour es la que vale: era la UNICA ficha de la app cuyo titulo se
    #  dibujaba con `drawText` a pelo, o sea sin pasar por `pintaTitulo` y por
    #  tanto sin publicar su banda. Invisible para el volcado de rotulos y por
    #  tanto para esta prueba y para la regla de «ningun rotulo pintado debajo
    #  de un control» — y como ademas no estaba en esta lista, «toda ficha
    #  publica su titulo» era una afirmacion que nadie comprobaba en la unica
    #  ficha donde era falsa. Roto a proposito devolviendo su drawText:
    #  `tour no tiene titulo: se abre y no dice donde estas`.
    ("tour",   "AJUSTES"),
    ("vst",    "INSTRUMENTO"),
    ("inst",   "INSTRUMENTOS"),
]


def corre (sheet, size, lang):
    casa = tempfile.mkdtemp (prefix="zati-plano-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG=lang, ZATI_OPEN=sheet,
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None, None
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    rot, comp, raiz = [], [], None
    for l in out.splitlines():
        l = l.strip()
        if not (l.startswith ("{") and l.endswith ("}")): continue
        try: d = json.loads (l)
        except Exception: continue
        if "root" in d:      raiz = d
        elif "rotulo" in d:  rot.append (d)
        elif "path" in d and d.get ("w", 0) > 0 and d.get ("h", 0) > 0:
            comp.append (d)
    return raiz, (rot, comp)


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario: compila primero")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    size = os.environ.get ("ZATI_SIZE", "412x915")
    lang = os.environ.get ("ZATI_LANG", "es")
    filtro = [a.lower() for a in sys.argv[1:]]
    avisos = []

    for sheet, tapa in FICHAS:
        if filtro and sheet.lower() not in filtro: continue
        raiz, datos = corre (sheet, size, lang)
        if datos is None:
            avisos.append ("%s no contesto" % (sheet or "cara")); continue
        rot, comp = datos

        nombre = sheet or "cara"
        print ("\n%s   %dx%d   se abre desde %s"
               % (nombre.upper(), raiz.get ("w", 0), raiz.get ("h", 0), tapa))

        #  Titulos y secciones, en orden de lectura.
        #
        #  EL TITULO DE LA FICHA ES EL DE SU CAPA, no el primero de la lista.
        #  Desde que la cabecera de la cara se apunta -«ZATI SAMPLER»- es un
        #  «titulo» mas, y ademas el primero, asi que CANCION y XY salian con
        #  «la tapa dice CANCION y el titulo ZATI SAMPLER»: dos hallazgos
        #  falsos, y de la clase peor, porque aparecieron el dia que esta
        #  prueba empezo a devolver codigo. Cada ficha lleva su numero de capa
        #  y todo lo que cuelga de ella lo hereda; la cara es la 0.
        #
        #  Y LA CAPA DE UNA FICHA NO PUEDE SER LA CERO, que es donde esta regla
        #  decia que si estando mal. `max(...)` se cae a 0 cuando la ficha no
        #  publico ningun rotulo -o sea justo en el caso que hay que cazar- y
        #  entonces coge el de la CARA, «ZATI SAMPLER», y lo da por su titulo:
        #  con el `drawText` del tour devuelto a proposito la prueba seguia
        #  saliendo verde. Una ficha sin rotulos propios no tiene titulo, y eso
        #  es exactamente lo que hay que decir.
        capa = max ((r.get ("capa", 0) for r in rot), default=0) if sheet else 0
        titulos = ([] if (sheet and capa == 0)
                   else [r for r in rot if r["tipo"] == "titulo" and r.get ("capa", 0) == capa])
        for r in sorted (rot, key=lambda r: (r["y"], r["x"])):
            print ("  %-8s %-28s %4d,%-4d %dx%d"
                   % (r["tipo"], r["rotulo"][:28], r["x"], r["y"], r["w"], r["h"]))

        #  EL MANUAL DICE CUANTOS CAPITULOS TIENE Y DIBUJA OTROS TANTOS.
        #
        #  Habia un `kManualChapters = 9` en el .h y una tabla de DIEZ filas, y
        #  manualContentHeight las recorria todas: SI ALGO NO SUENA estaba
        #  traducido a los cuatro idiomas, reservaba su alto en el
        #  desplazamiento y no se pintaba nunca. Ninguna de las ocho reglas de
        #  expo.py puede verlo -un capitulo que no se dibuja no es un
        #  componente-, y el subtitulo prometia OCHO, que no era ninguno de los
        #  dos numeros.
        #
        #  Se pregunta COMPARANDO y sin ninguna cifra escrita aqui: el numero
        #  que el subtitulo dice contra los titulos de capitulo que se pintaron
        #  de verdad. Asi la prueba no hay que tocarla el dia que entre un
        #  capitulo, que es justo lo que no paso con los siete sitios donde
        #  estaba escrito «ocho».
        if sheet == "manual":
            caps = [r for r in rot if r["tipo"] == "capitulo"]
            sub  = [r for r in rot if r["tipo"] == "subtitulo"]
            dice = None
            if sub:
                m = re.search (r"\d+", sub[0]["rotulo"])
                if m: dice = int (m.group (0))
            print ("  el subtitulo dice %s capitulos y se dibujan %d"
                   % (dice if dice is not None else "?", len (caps)))
            if dice is None:
                avisos.append ("manual: el subtitulo no dice cuantos capitulos hay")
            elif dice != len (caps):
                avisos.append ("manual: el subtitulo dice %d capitulos y se dibujan %d"
                               % (dice, len (caps)))

        #  LA PREGUNTA QUE ESTO EXISTE PARA CONTESTAR.
        if sheet and not titulos:
            avisos.append ("%s no tiene titulo: se abre y no dice donde estas" % nombre)
        elif sheet and tapa not in ("la cara",):
            t0 = titulos[0]["rotulo"].split ("  ")[0].strip() if titulos else ""
            if t0 and tapa not in ("AJUSTES", "SEC", "PADS", "MEZCLA", "CARGAR") and t0 != tapa:
                avisos.append ("%s: la tapa dice \"%s\" y el titulo \"%s\"" % (nombre, tapa, t0))

        print ("  %d controles" % len (comp))

    print()
    #  Y DEVUELVE CODIGO. Esto imprimia AVISO y salia con cero pasara lo que
    #  pasara, que es lo mismo que le pasaba a expo.py, session.py y apk.py: el
    #  veredicto lo daba un ojo humano leyendo texto. Por eso «sec no tiene
    #  titulo» y «browse no tiene titulo» duraron tandas enteras.
    #
    #  Y la ultima linea estaba mal escrita ademas: `"%d..." % 0 if avisos else
    #  "..."` se agrupa como `("%d..." % 0) if avisos else "..."`, asi que con
    #  avisos imprimia «0 fichas sin aviso» — el mensaje de que todo esta bien
    #  justo cuando algo no lo esta.
    for a in avisos: print ("FALLA ", a)
    if avisos:
        return 1
    print ("el plano no encuentra desajustes")
    return 0


if __name__ == "__main__":
    sys.exit (main())

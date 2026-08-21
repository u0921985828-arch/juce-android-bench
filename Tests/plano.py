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
import json, os, shutil, subprocess, sys, tempfile

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
        titulos = [r for r in rot if r["tipo"] == "titulo"]
        for r in sorted (rot, key=lambda r: (r["y"], r["x"])):
            print ("  %-8s %-28s %4d,%-4d %dx%d"
                   % (r["tipo"], r["rotulo"][:28], r["x"], r["y"], r["w"], r["h"]))

        #  LA PREGUNTA QUE ESTO EXISTE PARA CONTESTAR.
        if sheet and not titulos:
            avisos.append ("%s no tiene titulo: se abre y no dice donde estas" % nombre)
        elif sheet and tapa not in ("la cara",):
            t0 = titulos[0]["rotulo"].split ("  ")[0].strip() if titulos else ""
            if t0 and tapa not in ("AJUSTES", "SEC", "PADS", "MEZCLA", "CARGAR") and t0 != tapa:
                avisos.append ("%s: la tapa dice \"%s\" y el titulo \"%s\"" % (nombre, tapa, t0))

        print ("  %d controles" % len (comp))

    print()
    for a in avisos: print ("AVISO ", a)
    print ("%d fichas sin aviso" % 0 if avisos else "el plano no encuentra desajustes")
    return 0


if __name__ == "__main__":
    sys.exit (main())

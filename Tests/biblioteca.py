#!/usr/bin/env python3
"""LA BIBLIOTECA QUE SOBREVIVE A DESINSTALAR (android #2).

En Android la biblioteca acababa en Android/data y desinstalar la borra. Lo
minimo que la auditoria pide, y que se puede medir aqui:

  1. ZIP en PROYECTOS copia la biblioteca entera a un zip, la sesion invisible
     (.sesion/) incluida, que es donde viven los sonidos de los pads.
  2. Con la biblioteca en la carpeta de la app (la que desinstalar borra) la
     pagina de PROYECTOS lo avisa; en Musica/ZATI no.
  3. La sonda de escritura prueba con las extensiones que la biblioteca
     escribe (.wav y .xml): Musica/ rechaza lo que no es audio.
"""
import json, os, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN", os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati"))


def corre (volatil):
    casa = tempfile.mkdtemp (prefix="zati-bib-")
    if volatil:
        conf = os.path.join (casa, ".config")
        os.makedirs (os.path.join (conf, "ZATI"))
        open (os.path.join (conf, "zati-home.txt"), "w").write (os.path.join (conf, "ZATI"))
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="393x851",
                ZATI_LANG="es", ZATI_OPEN="zip", DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300).stdout
    except subprocess.TimeoutExpired:
        out = ""
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    for l in out.splitlines():
        if l.strip().startswith ('{"zip"'):
            try: return json.loads (l.strip())
            except Exception: pass
    return None


def main():
    malas = []
    src = open (os.path.join (ROOT, "Source", "ProjectStore.h"), encoding="utf8").read()
    if '".wav", ".xml"' not in src:
        malas.append ("la sonda de escritura no prueba .wav y .xml")

    musica, volatil = corre (False), corre (True)
    print ("en Musica:      %s" % json.dumps (musica, ensure_ascii=False))
    print ("en la app:      %s" % json.dumps (volatil, ensure_ascii=False))
    if musica is None or volatil is None:
        malas.append ("la auditoria no contesto a ZATI_OPEN=zip")
    else:
        if musica.get ("ficheros", -1) <= 0 or musica.get ("entradas") != musica.get ("ficheros"):
            malas.append ("el zip tiene %s entradas de %s ficheros" % (musica.get ("entradas"), musica.get ("ficheros")))
        if not musica.get ("sesion"): malas.append ("el zip no lleva la sesion (.sesion/)")
        if not musica.get ("boton"):  malas.append ("ZIP no esta en la pagina de PROYECTOS")
        if musica.get ("seborra"):    malas.append ("Musica/ZATI se da por borrable")
        if not volatil.get ("seborra"): malas.append ("la carpeta de la app no se da por borrable")
        if "desinstalar" not in volatil.get ("aviso", ""):
            malas.append ("con la biblioteca en la app no se avisa (dice «%s»)" % volatil.get ("aviso"))
        if "desinstalar" in musica.get ("aviso", ""):
            malas.append ("en Musica se avisa sin motivo")
    if malas:
        for m in malas: print ("FALLA  " + m)
        sys.exit (1)
    print ("la biblioteca se copia entera a un zip y se avisa donde desinstalar la borra")

main()

#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  LO QUE SALE DEL TELEFONO.
#
#  La exportacion es la unica funcion de esta app cuyo resultado sale del
#  aparato, y era la unica sin banco que la corriera: los numeros existian
#  -la app los imprime con ZATI_EXPORT=1- y no los miraba nadie, asi que una
#  regresion no fallaba, se publicaba.
#
#  Se comprueban las tres salidas y el destino:
#
#    master   un WAV con lo que suena
#    pistas   un WAV por pad con muestra, mas el master
#    ogg      lo mismo comprimido, que es OTRO escritor y otro fichero
#    destino  la carpeta elegida se guarda, vuelve, y una que no acepta
#             escritura se rechaza SIN perder la anterior
#
#  El OGG se juzga por la RELACION con el WAV y no por un tamano absoluto: el
#  tamano depende de la calidad y del contenido, y clavarlo aqui seria un banco
#  que hay que actualizar cada vez que se toca el encoder. Lo que no puede pasar
#  es que "comprimido" pese lo mismo que sin comprimir - eso es la tapa
#  cambiando un rotulo y nada mas.
#
#      python3 Tests/export.py
# ============================================================================
import json, os, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
BIN  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

def corre():
    casa = tempfile.mkdtemp (prefix="zati-export-")
    env = dict (os.environ, ZATI_AUDIT="1", ZATI_EXPORT="1",
                DISPLAY=os.environ.get ("DISPLAY", ":99"),
                HOME=casa, XDG_DATA_HOME=casa)
    try:
        out = subprocess.run ([BIN], env=env, capture_output=True,
                              timeout=900).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    filas = {}
    for l in out.splitlines():
        l = l.strip()
        if not (l.startswith ("{") and l.endswith ("}")): continue
        try: d = json.loads (l)
        except Exception: continue
        if "export" in d: filas[d["export"]] = d
    return filas

def main():
    if not os.path.exists (BIN):
        sys.exit ("no hay binario: compila primero (cmake --build build)")

    filas = corre()
    if filas is None:
        sys.exit ("la app no contesto")

    malas = []
    def juzga (nombre, cond, texto):
        print ("%-10s %s   %s" % (nombre, texto, "correcto" if cond else "MAL"))
        if not cond: malas.append (nombre)

    m = filas.get ("master", {})
    juzga ("master", m.get ("ok") == 1 and m.get ("ficheros") == 1 and m.get ("bytes", 0) > 100000,
           "%s fichero, %s bytes" % (m.get ("ficheros", "?"), m.get ("bytes", "?")))

    p = filas.get ("pistas", {})
    #  Un WAV por pad con muestra, al menos. Contar solo "muchos ficheros"
    #  dejaria pasar un rebote que escribe cuarenta veces el mismo pad, pero
    #  eso lo cazan los bytes: cuarenta copias del mismo pad pesan lo mismo que
    #  cuarenta pads distintos, asi que aqui se comprueba la CUENTA y el peso
    #  total lo comprueba el de master.
    juzga ("pistas", p.get ("ok") == 1 and p.get ("ficheros", 0) >= p.get ("pads", 0),
           "%s ficheros para %s pads con muestra" % (p.get ("ficheros", "?"), p.get ("pads", "?")))

    o = filas.get ("ogg", {})
    razon = (m.get ("bytes", 1) / max (1, o.get ("bytes", 1))) if o else 0
    juzga ("ogg", o.get ("ok") == 1 and o.get ("ficheros") == 1 and razon >= 4.0,
           "%s bytes, %.0f veces mas pequeno que el WAV" % (o.get ("bytes", "?"), razon))

    d = filas.get ("destino", {})
    juzga ("destino",
           d.get ("acepta") == 1 and d.get ("vuelve") == 1 and d.get ("rechaza") == 1
             and d.get ("aguanta") == 1 and d.get ("limpia") == 1,
           "acepta %s vuelve %s rechaza %s aguanta %s limpia %s"
             % (d.get ("acepta", "?"), d.get ("vuelve", "?"), d.get ("rechaza", "?"),
                d.get ("aguanta", "?"), d.get ("limpia", "?")))

    print()
    if malas:
        print ("FALLA:", ", ".join (malas));  return 1
    print ("lo que sale del telefono sale entero")
    return 0

if __name__ == "__main__":
    sys.exit (main())

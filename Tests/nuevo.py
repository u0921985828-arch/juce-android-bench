#!/usr/bin/env python3
# ============================================================================
#  CON QUE ABRE LA MAQUINA.
#
#  Un proyecto vacio no es un proyecto sin nada. Es lo primero que ve quien
#  acaba de instalar esto, y se llega a el por DOS codigos distintos que nadie
#  obliga a decir lo mismo: restoreSession cuando no hay sesion, y newProject
#  cuando se pulsa NUEVO. Ya se ha pagado que uno de los dos se dejara medio
#  estado sin tocar -NUEVO vaciaba los pads y los ocho patrones y se dejaba la
#  linea de tiempo del proyecto anterior encima-, asi que se miden los dos y se
#  comparan.
#
#  Tres cosas definen ese estado:
#
#  - LOS SONIDOS. Al instalar, los 64 de fabrica; despues de NUEVO, ninguno.
#    Es la unica de las tres en la que los dos caminos tienen que diferir.
#  - LA CANCION. El patron 1 en el primer hueco. Sin el, la pagina CANCION abre
#    con cuatro carriles vacios y no se explica sola, y PLAY en modo cancion
#    recorre ocho compases mudos.
#  - LOS ENVIOS. Los 64 pads a cero en los seis efectos. El uno de antes
#    obligaba a BAJAR cinco envios por cada uno que querias.
#
#  Con HOME temporal, que es la mitad que hace que esto mida algo: con el HOME
#  de verdad la app restaura la sesion que hubiera y no pasa nunca por el
#  camino de la primera vez.
#
#      python3 Tests/nuevo.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre():
    casa = tempfile.mkdtemp (prefix="zati-nuevo-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": "song", "ZATI_NUEVO": "1"})
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "nuevo" in d:
            filas[d["nuevo"]] = d
    return filas


def main():
    if not os.path.exists (APP):
        print ("no hay binario: cmake --build build"); return 1

    filas = corre()
    if "arranque" not in filas or "nuevo" not in filas:
        print ("la app no volco los dos estados"); return 1

    fallos = []
    for que, pads in (("arranque", 64), ("nuevo", 0)):
        d = filas[que]

        if d["pads"] != pads:
            fallos.append ("%s: %d pads con sonido, esperados %d" % (que, d["pads"], pads))

        #  El maximo Y la suma. Solo el maximo dejaria pasar un getter que
        #  devuelve cero siempre, y solo la suma dejaria pasar un envio a 1
        #  compensado por otro a -1, que el rango no permite pero la prueba no
        #  sabe.
        if d["envmax"] != 0 or d["envsuma"] != 0:
            fallos.append ("%s: envios max %.3f suma %.3f, esperado 0"
                           % (que, d["envmax"], d["envsuma"]))

        carriles = d["carriles"]
        esperado = [[0] * d["largo"] for _ in carriles]
        esperado[0][0] = 1
        if carriles != esperado:
            fallos.append ("%s: la cancion es %s y se esperaba el patron 1 en el primer hueco"
                           % (que, carriles))

        print ("%-10s %2d pads   envios max %.2f suma %.2f   carril 0 %s"
               % (que, d["pads"], d["envmax"], d["envsuma"], carriles[0]))

    #  Y QUE LOS DOS CAMINOS DIGAN LO MISMO en todo menos en los sonidos, que
    #  es la comprobacion que caza el fallo de verdad: no que cada uno este
    #  bien por separado, sino que no se separen.
    a, n = filas["arranque"], filas["nuevo"]
    if a["carriles"] != n["carriles"] or a["envmax"] != n["envmax"]:
        fallos.append ("los dos caminos a un proyecto vacio no coinciden")

    print()
    if fallos:
        for f in fallos: print ("FALLA  " + f)
        return 1
    print ("la maquina abre igual por los dos caminos")
    return 0


sys.exit (main())

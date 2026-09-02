#!/usr/bin/env python3
# ============================================================================
#  LA BANDA DE AUDIO DE LA CANCION, medida por el GESTO.
#
#  Los clips ya tenian dos medidas -que suenen donde se pusieron (el motor) y
#  que vuelvan del project.xml (session.py)- y ninguna de las dos toca la unica
#  parte que una persona usa: el dedo. Llamar a ponClip o a mueveClip por dentro
#  se salta exactamente el codigo que decide QUE pista y QUE compas caen bajo el
#  dedo, que es donde vivian los cinco fallos del compas del piano.
#
#  Y ninguna de las ocho reglas de expo.py puede verlo, porque no es geometria:
#  una banda que escribe el clip en el compas de al lado se maqueta perfecta, no
#  solapa, no se sale, no corta ningun rotulo y esta traducida.
#
#      python3 Tests/clips.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  El dedo minimo de la casa. Un clip se ARRASTRA, asi que su celda no puede
#  medir menos que una tapa: fallar el agarre no es fallar un toque, es mover
#  otra cosa.
DEDO = 40


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (size="412x915"):
    casa = tempfile.mkdtemp (prefix="zati-clips-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", ZATI_CLIPS="1",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    out = subprocess.run ([APP], env=env, capture_output=True, timeout=300)
    for l in out.stdout.decode ("utf8", "replace").splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: d = json.loads (l)
        except Exception: continue
        if "clipsui" in d: return d
    return None


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    d = corre()
    if d is None: sys.exit ("la app no dijo nada")

    malas = 0

    def juzga (titulo, ok, dice):
        nonlocal malas
        if not ok: malas += 1
        print ("%-34s %-22s %s" % (titulo, dice, "correcto" if ok else "FALLA"))

    #  1. PONER. Un toque en un hueco deja el clip en ESA pista y ESE compas, y
    #  no en el (0,0) que saldria de no leer el punto del dedo.
    juzga ("un toque pone el clip ahi", d.get ("puesto") == [2, 3],
           "puesto %s" % (d.get ("puesto"),))

    #  2. MOVER. Arrastrar lo lleva a la pista y al compas de destino: es el
    #  gesto que separa esta banda de la vista de patrones, donde arrastrar
    #  PINTA. Dos gestos incompatibles en el mismo dedo, y por eso son dos
    #  vistas.
    juzga ("arrastrar lo mueve", d.get ("movido") == [1, 5],
           "movido %s" % (d.get ("movido"),))

    #  3. EL AGARRE, que es la cifra que de verdad hace falta. El clip mide tres
    #  compases y se coge por el TERCERO para soltarlo en el 2: tiene que quedar
    #  en el 0, o sea el dedo MENOS por donde se agarro. Sin esta, «arrastrar
    #  mueve» lo cumple igual un codigo que pega el bloque por su principio de un
    #  salto - el bloque se va un trozo que nadie pidio, y es lo primero que se
    #  nota.
    juzga ("respeta por donde se agarro",
           d.get ("largo_compases") == 3 and d.get ("agarrado") == [1, 0],
           "agarrado %s de %s compases" % (d.get ("agarrado"), d.get ("largo_compases")))

    #  4. BORRAR con la brocha VACIAR, que es la MISMA que borra en la vista de
    #  patrones: un gesto nuevo para borrar seria una segunda forma de lo mismo.
    juzga ("vaciar lo quita", d.get ("tras_borrar") == 0,
           "quedan %s" % (d.get ("tras_borrar"),))

    #  5. Y LA CELDA, contra el dedo. Esta banda existe porque ocho carriles no
    #  caben -20.2 px en 280x653, medido- asi que la cifra que la justifica hay
    #  que mirarla: si un dia vuelve a bajar del dedo, la decision se cae.
    celda = d.get ("celda") or [0, 0]
    juzga ("la celda se puede agarrar", celda[0] >= DEDO and celda[1] >= DEDO,
           "%dx%d px" % (celda[0], celda[1]))

    print()
    print ("la banda de audio hace lo que dice" if malas == 0
           else "%d de 5 no" % malas)
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

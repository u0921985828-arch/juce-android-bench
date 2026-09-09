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
#  Y ninguna de las once reglas de expo.py puede verlo, porque no es geometria:
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
    #
    #  CON DOS CIFRAS, y la primera es un control: que la brocha llegase de
    #  verdad a CLIP. Se cicla por la TAPA -PATRON, SONIDO, CLIP- porque
    #  escribir el estado a mano se salta la traduccion a lo que la rejilla lee,
    #  y ahi es exactamente donde estaba el fallo: el campo se escribia en la
    #  cara y el de la rejilla no lo ponia nadie, asi que la brocha se veia
    #  armada y no hacia nada. Sin el control, «puesto [2,3]» lo cumpliria
    #  tambien un toque que cayera en la familia equivocada.
    juzga ("un toque pone el clip ahi",
           d.get ("pincel") == 2 and d.get ("puesto") == [2, 3],
           "brocha %s  puesto %s" % (d.get ("pincel"), d.get ("puesto")))

    #  2. MOVER, CON LA MANO ARMADA. Arrastrar lo lleva a la pista y al compas
    #  de destino. Antes esto se medi­a en la vista de audio, donde arrastrar
    #  solo podia significar mover; con las dos familias en la misma rejilla
    #  arrastrar YA significa pintar, asi que mover es un MODO -la misma
    #  decision que el piano tomo con LAPIZ, GOMA, TIJERAS y SEL- y sin armarlo
    #  el gesto pinta, que es lo correcto.
    juzga ("arrastrar lo mueve", d.get ("movido") == [1, 5],
           "movido %s" % (d.get ("movido"),))

    #  3. EL AGARRE, que es la cifra que de verdad hace falta. El clip mide tres
    #  compases y se coge por el TERCERO para soltarlo en el 2: tiene que quedar
    #  en el 0, o sea el dedo MENOS por donde se agarro. Sin esta, «arrastrar
    #  mueve» lo cumple igual un codigo que pega el bloque por su principio de un
    #  salto - el bloque se va un trozo que nadie pidio, y es lo primero que se
    #  nota.
    juzga ("respeta por donde se agarro",
           d.get ("agarre_compases") == 4 and d.get ("agarrado") == [1, 0],
           "agarrado %s de %s compases" % (d.get ("agarrado"), d.get ("agarre_compases")))

    #  4. EL RECORTE HEREDADO. El clip nace con LO QUE SUENA en el pad y no con
    #  el buffer entero: es la misma regla que GUARDAR KIT, y sin ella un pad de
    #  un break de cuatro minutos recortado a un golpe entra en la cancion como
    #  cuatro minutos. Dos cifras, porque si el clip midiera lo mismo que la
    #  fuente la prueba diria que si a no hacer nada.
    fuente, clip = d.get ("largo_fuente", 0), d.get ("largo_clip", 0)
    juzga ("el clip toma el recorte del pad",
           fuente > 0 and clip > 0 and abs (clip * 2 - fuente) <= 2,
           "%d de %d muestras" % (clip, fuente))

    #  5. LAS ASAS. Un clip de tres compases en (1,0): se coge su ultimo compas
    #  y se lleva dos mas alla, y tiene que quedar de CINCO compases SIN moverse
    #  de sitio. Las dos cifras, porque un asa que ademas mueve pasa cualquier
    #  prueba que solo mire el largo.
    juzga ("el asa alarga y no mueve",
           d.get ("tras_asa") == [1, 0] and d.get ("compases_tras_asa") == 5,
           "%s de %s compases" % (d.get ("tras_asa"), d.get ("compases_tras_asa")))

    #  6. Y UN CLIP CORTO NO TIENE ASAS: con un compas, las dos asas serian el
    #  clip entero y no quedaria medio que agarrar para moverlo. Arrastrar su
    #  filo tiene que MOVERLO y dejarlo de un compas. Sin esta cifra, «aqui no
    #  caben asas» y «no hay asas» son la misma corrida en verde.
    juzga ("un clip corto no tiene asas",
           d.get ("corto_tras_filo") == [1, 3] and d.get ("corto_compases") == 1,
           "%s de %s compases" % (d.get ("corto_tras_filo"), d.get ("corto_compases")))

    #  7. BORRAR con la GOMA, que es la MISMA herramienta que borra un bloque de
    #  patron: una funcion, un dueño. La tapa VACIAR se retiro justo por eso -
    #  eran dos dueños de lo mismo, y la que se queda es la que ademas se VE
    #  armada.
    juzga ("la goma lo quita", d.get ("tras_borrar") == 0,
           "quedan %s" % (d.get ("tras_borrar"),))

    #  8. LA CANALETA SILENCIA LAS DOS COSAS.
    #
    #  Un carril lleva bloques de patron Y clips desde que las dos vistas se
    #  fundieron, asi que su MUTE tiene que callar los dos. Las dos mascaras son
    #  distintas a proposito -carril y pista de audio no eran el mismo numero
    #  cuando eran dos paginas- y aqui son el mismo carril, asi que la canaleta
    #  escribe las dos. Con una sola, se calla el carril y el audio sigue
    #  sonando: no falla nada, y la persona oye la toma sobre el silencio.
    #
    #  Medido POR EL GESTO -un toque a la izquierda del canalon- y con las DOS
    #  cifras, que cada una sola la cumple media maquina.
    juzga ("la canaleta calla las dos",
           d.get ("carril_mudo") == 1 and d.get ("pista_muda") == 1,
           "carril %s  pista %s" % (d.get ("carril_mudo"), d.get ("pista_muda")))

    #  9. Y LA CELDA, contra el dedo. Esta banda existe porque ocho carriles no
    #  caben -20.2 px en 280x653, medido- asi que la cifra que la justifica hay
    #  que mirarla: si un dia vuelve a bajar del dedo, la decision se cae.
    celda = d.get ("celda") or [0, 0]
    juzga ("la celda se puede agarrar", celda[0] >= DEDO and celda[1] >= DEDO,
           "%dx%d px" % (celda[0], celda[1]))

    print()
    print ("la banda de audio hace lo que dice" if malas == 0
           else "%d de 9 no" % malas)
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

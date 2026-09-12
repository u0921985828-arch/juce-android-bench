#!/usr/bin/env python3
# ============================================================================
#  EL MIDI DEL PIANO ROLL: LA IDA Y LA VUELTA.
#
#  Se pidio asi: «en el piano roll se podria tanto exportar como importar midi
#  para que se lean y se apliquen bien en las cuadriculas, y lo mismo para los
#  midis creados poder aplicarlo».
#
#  NINGUNA DE LAS QUINCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO: un
#  importador que deja las notas una octava mas abajo, o media casilla tarde, se
#  maqueta perfecto -no solapa, no se sale, no corta un rotulo, no mide cero y
#  esta traducido-. Es la familia de los cinco fallos del compas del piano.
#
#  Y LA CIFRA QUE MANDA ES UNA IDENTIDAD y no un contador: lo que sale y vuelve
#  a entrar tiene que ser EL MISMO patron, nota a nota. «Se exporta» lo cumple
#  un fichero con la raiz sola y «se importa» lo cumple uno que cae una octava
#  abajo; solo la vuelta entera dice que las dos mitades hablan el mismo idioma.
#  Es la figura de las dos filas que mas valen de `Tests/arr.py` -quitar el
#  compas que se acaba de insertar devuelve la cancion exacta- y la del troceado
#  que volvia siendo N copias: sonaba igual y habia dejado de ser un troceado.
#
#      python3 Tests/midi.py
# ============================================================================
import json, os, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en `kits.py`,
#  al lado de `display_alive`, y quien arranca la app la escribe en su entorno.
#  Sin esta linea la comprobacion dice que si contra :99 y el arranque se va sin
#  ventana — el veredicto entero en rojo con la app perfecta, que es lo que ya
#  costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre():
    casa = tempfile.mkdtemp (prefix="zati-midi-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_MIDI="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return {}
    filas = {}
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "midi" in r: filas[r["midi"]] = r
    return filas


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1

    filas = corre()
    if not filas:
        print ("FALLA  la app no publico ninguna linea de midi")
        return 1

    malas = []

    # ------------------------------------------------------------------
    #  1. LA IDA Y LA VUELTA, con CUATRO cifras porque cada una sola se engaña:
    #
    #     - `bytes`  dice que hubo fichero. Sin el, «vuelven siete» lo cumple un
    #       importador que no leyo nada y un patron que nadie borro.
    #     - `tras_borrar` dice que el pad se vacio A MANO entre medias, que es
    #       lo unico que separa «se ha escrito» de «nadie lo quito» - la misma
    #       linea que la prueba de la sesion y la del troceado.
    #     - `despues` dice cuantas volvieron.
    #     - `iguales` dice cuantas volvieron EXACTAS: paso, semitono, fuerza y
    #       largo. Comparar por cuenta seria decir que si a un importador que
    #       las pone todas en el paso cero.
    # ------------------------------------------------------------------
    v = filas.get ("vuelta")
    if v is None:
        malas.append ("la app no publico la ida y vuelta")
    else:
        print ("vuelta   antes %d  bytes %d  tras borrar %d  despues %d  iguales %d"
               % (v["antes"], v["bytes"], v["tras_borrar"], v["despues"], v["iguales"]))
        if v["antes"] != 7:
            malas.append ("el patron de partida no son las siete notas que se "
                          "escribieron: %d" % v["antes"])
        if v["bytes"] <= 0:
            malas.append ("no se escribio ningun .mid: la ida no existe")
        if v["tras_borrar"] != 0:
            malas.append ("el pad no se vacio entre medias: lo que vuelva no dice "
                          "nada, porque nadie lo habia quitado")
        if v["despues"] != v["antes"]:
            malas.append ("vuelven %d de %d notas" % (v["despues"], v["antes"]))
        if v["iguales"] != v["antes"]:
            malas.append ("solo %d de %d vuelven EXACTAS: la ida y la vuelta no son "
                          "una identidad" % (v["iguales"], v["antes"]))

    # ------------------------------------------------------------------
    #  2. LO QUE NO CABE SE CUENTA Y SE DICE. Un fichero de fuera no tiene por
    #     que caber: notas a tres octavas del pad -`setStepNote` acota en ±24-,
    #     compases mas alla del patron, y una quinta voz en la misma columna
    #     -el motor guarda una raiz y TRES notas de mas-. Una importacion que se
    #     come la mitad en silencio es la peor forma de funcionar: la que parece
    #     que funciona.
    #
    #     Con las DOS mitades: que las que caben ENTREN y que las que no se
    #     CUENTEN. Solo la primera la cumple un lector que no acota nada -y deja
    #     una nota del compas noventa escrita en el paso cero-, y solo la
    #     segunda la cumple uno que no deja pasar ninguna.
    # ------------------------------------------------------------------
    h = filas.get ("hostil")
    if h is None:
        malas.append ("la app no publico la lectura del fichero hostil")
    else:
        print ("hostil   puestas %d  fuera %d  tarde %d  apiladas %d  leidas %d"
               % (h["puestas"], h["fuera"], h["tarde"], h["apiladas"], h["leidas"]))
        #  Una que cabe, mas cuatro de las seis de la misma columna.
        if h["puestas"] != 5 or h["leidas"] != 5:
            malas.append ("de las diez del fichero hostil tenian que entrar 5: "
                          "puestas %d, leidas %d" % (h["puestas"], h["leidas"]))
        if h["fuera"] != 2:
            malas.append ("las dos notas a +40 y -40 semitonos no se contaron "
                          "como fuera: %d" % h["fuera"])
        if h["tarde"] != 1:
            malas.append ("la nota del paso 200 no se conto como tarde: %d" % h["tarde"])
        if h["apiladas"] != 2:
            malas.append ("la quinta y la sexta voz de la misma columna no se "
                          "contaron como apiladas: %d" % h["apiladas"])

    # ------------------------------------------------------------------
    #  3. LAS TRES CONVENCIONES, medidas y no supuestas. Son ELECCIONES -no hay
    #     nada en el formato que las imponga- asi que las tres tienen que poder
    #     fallar:
    #
    #       do central = semitono 0   ·   una negra = cuatro pasos
    #       y un paso tiene UNA ortografia, el CERO
    #
    #     La tercera es la que hace que la ida y vuelta sea una identidad: cero
    #     y cuatro son el mismo sonido y el mismo dibujo, y el fichero solo
    #     puede llevar una duracion. Sin ella, importar lo que acabas de
    #     exportar escribe un cuatro en cada casilla que nadie habia tocado.
    # ------------------------------------------------------------------
    c = filas.get ("convencion")
    if c is None:
        malas.append ("la app no publico las convenciones")
    else:
        print ("convenc  do central %d  la negra cae en el paso %d  y dura %d "
               "cuartos   un paso vuelve como %d"
               % (c["do_central"], c["paso_de_la_negra"], c["largo_de_una_negra"],
                  c["largo_de_un_paso"]))
        if c["do_central"] != 0:
            malas.append ("el do central no es el semitono cero: %d" % c["do_central"])
        if c["paso_de_la_negra"] != 4:
            malas.append ("una negra no ocupa cuatro pasos: cae en el %d"
                          % c["paso_de_la_negra"])
        if c["largo_de_una_negra"] != 16:
            malas.append ("una negra no vuelve midiendo cuatro pasos: %d cuartos"
                          % c["largo_de_una_negra"])
        if c["largo_de_un_paso"] != 0:
            malas.append ("una nota de UN PASO vuelve como %d y no como cero: son "
                          "dos ortografias del mismo sonido y el fichero solo lleva "
                          "una" % c["largo_de_un_paso"])

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("lo que sale y vuelve a entrar es el MISMO patron nota a nota, lo que "
           "no cabe se cuenta, y las tres convenciones se cumplen")
    return 0


if __name__ == "__main__":
    sys.exit (main())

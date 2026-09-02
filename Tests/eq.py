#!/usr/bin/env python3
# ============================================================================
#  EL EQ DE CINCO BANDAS Y SU CURVA — la primera cara propia de un efecto.
#
#  Los seis efectos de esta maquina se tocan con CTRL 1, CTRL 2 y CTRL 3, y el
#  tercero es siempre MIX. O sea DOS mandos libres por efecto, y con dos mandos
#  un ecualizador de cinco bandas no cabe: hacen falta diez numeros. Este es el
#  efecto que obliga a que un efecto pueda traer su propia superficie y ocupar
#  el plato en vez de pedir prestados los mandos de todos.
#
#  NINGUNA de las nueve reglas de `expo.py` puede ver nada de esto. Una curva
#  es un LIENZO -se pinta entera y se acierta con el dedo, como la rejilla de
#  pasos, el piano y la linea de tiempo- asi que un nodo que escribe la banda de
#  al lado se maqueta perfecto: no solapa, no se sale, no lleva rotulo, no mide
#  cero y esta traducido. Es la familia de los cinco fallos del compas del
#  piano, otra vez.
#
#  SE MIDE POR EL GESTO EN PIXELES y no llamando al callback: `ponBandaEq` se
#  salta exactamente el codigo que decide QUE nodo cae bajo el dedo y hasta
#  donde puede llegar. La app construye un `MouseEvent` y llama a
#  `EqCurve::mouseDown` / `mouseDrag`, igual que `clips.py` con la linea de
#  tiempo y `piano.py` con el arrastre del piano roll.
#
#      python3 Tests/eq.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  El dedo minimo. Es `Metrics::hit`, el mismo numero con el que el banco de
#  maqueta juzga cualquier control: el liston de una prueba no se reinventa en
#  la de al lado.
DEDO = 40

#  La mas ancha y la mas estrecha que nadie fabrica. La segunda es la que
#  decide: es donde el plato mide 232 px y donde se comprobo que la curva tiene
#  que llevarselo ENTERO -a cinco nodos les tocan 46 px, y en dos tercios 31-.
PANTALLAS = ["412x915", "280x653"]


def corre (size):
    casa = tempfile.mkdtemp (prefix="zati-eq-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG="es", ZATI_EQ="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("eq"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1

    malas = []

    for size in PANTALLAS:
        r = corre (size)
        if r is None:
            print ("FALLA  %s: la app no publico la linea del EQ" % size)
            return 1

        #  1. EL PLATO ES DE QUIEN LO OCUPA, con las DOS mitades: un efecto de
        #     los de siempre se queda con los tres mandos y el EQ con la curva.
        #     Solo lo segundo lo cumple una curva que se ve siempre, y entonces
        #     los mandos no volverian nunca; solo lo primero lo cumple una
        #     curva que no se enseña nunca, y entonces no hay EQ que tocar.
        print ("%-9s plato   con FLT curva %d mandos %d   con EQ curva %d mandos %d"
               % (size, r["plato_con_flt"], r["mandos_con_flt"],
                  r["plato_con_eq"], r["mandos_con_eq"]))
        if r["plato_con_flt"] != 0 or r["mandos_con_flt"] != 1:
            malas.append ("%s: con un efecto de tres mandos el plato no es suyo" % size)
        if r["plato_con_eq"] != 1 or r["mandos_con_eq"] != 0:
            malas.append ("%s: con el EQ delante el plato no pasa a la curva" % size)

        #  2. Y CADA NODO SE PUEDE AGARRAR. Este es el numero que decidio que la
        #     curva se lleva el plato entero: un nodo por debajo del dedo no es
        #     un control, y un deslizador que se ajusta ARRASTRANDO es el que
        #     menos puede permitirselo -fallar el agarre no es fallar un toque,
        #     es mover otra cosa-.
        print ("%-9s nodo    %d px de ancho   (dedo %d)" % (size, r["por_nodo"], DEDO))
        if r["por_nodo"] < DEDO:
            malas.append ("%s: a cada nodo le tocan %d px, por debajo del dedo"
                          % (size, r["por_nodo"]))

        #  3. UN NODO ESCRIBE SU BANDA Y NO LA DE AL LADO, con un TESTIGO en
        #     otra: «escribio» lo cumple igual un codigo que escribe siempre en
        #     la misma. El testigo vale -7 y lo movido tiene que subir.
        print ("%-9s arrastre  banda 2 %+.2f dB   testigo en la 4 %+.2f   espejo %+.2f"
               % (size, r["g2"], r["g4"], r["espejo2"]))
        if r["g2"] < 3.0:
            malas.append ("%s: el arrastre no escribio la banda: %+.2f dB" % (size, r["g2"]))
        if abs (r["g4"] + 7.0) > 0.01:
            malas.append ("%s: el arrastre se llevo por delante otra banda: la 4 quedo en %+.2f"
                          % (size, r["g4"]))
        #  Y el ESPEJO -de donde se pinta- tiene que decir lo mismo que el
        #  motor. Si solo se escribiera el espejo, la curva subiria y no sonaria
        #  nada; si solo el motor, sonaria y la curva se quedaria plana. Las dos
        #  formas de escribirlo mal, y las dos parecen bien.
        if abs (r["espejo2"] - r["g2"]) > 0.01:
            malas.append ("%s: la curva dibuja %+.2f y el motor tiene %+.2f"
                          % (size, r["espejo2"], r["g2"]))

        #  4. DOS BANDAS NO SE CRUZAN. La 2 se arrastra hasta fuera por la
        #     derecha y tiene que quedarse por debajo de la 3 con su tercio de
        #     octava de guarda: un nodo que salta al otro lado de su vecino no
        #     se puede arrastrar, y la curva dibujada y la que suena dejan de
        #     estar de acuerdo.
        print ("%-9s guarda  banda 2 en %.0f Hz   banda 3 en %.0f" % (size, r["f2"], r["f3"]))
        if r["cruza"]:
            malas.append ("%s: la banda 2 (%.0f Hz) adelanto a la 3 (%.0f)"
                          % (size, r["f2"], r["f3"]))
        if r["f2"] < r["f3"] * 0.5:
            malas.append ("%s: la banda 2 no llego ni cerca de su tope: %.0f Hz de %.0f"
                          % (size, r["f2"], r["f3"] / 1.26))

        #  5. UN TOQUE EN EL AIRE NO ARRASTRA NADA. Se coge el nodo mas cercano
        #     SOLO dentro de un dedo, que es la misma regla que las asas del
        #     recorte: sin el limite, un toque arriba del todo se lleva la banda
        #     que quede mas cerca en horizontal y la curva salta sola.
        print ("%-9s lejos   la peor banda quedo en %+.2f dB" % (size, r["lejos"]))
        if r["lejos"] > 0.01:
            malas.append ("%s: un arrastre fuera de todo nodo movio una banda: %+.2f dB"
                          % (size, r["lejos"]))
        print()

    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("el plato pasa a la curva, cada nodo se agarra, escribe su banda, no "
           "cruza a la vecina y no responde en el aire")
    return 0


if __name__ == "__main__":
    sys.exit (main())

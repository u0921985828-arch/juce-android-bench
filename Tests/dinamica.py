#!/usr/bin/env python3
# ============================================================================
#  LA FAMILIA DE DINAMICA EN LA CARA: CMP · GTE · DSS · LIM.
#
#  Lo que SUENA lo mide el banco del motor (`Tests/StressTest.cpp`), con cuatro
#  filas y DOS cifras cada una — que es lo unico que separa un efecto de un
#  fader: la primera dice que hace algo y la segunda que hace SOLO lo que dice.
#
#  Lo que se mide aqui es lo que ninguna de las nueve reglas de `expo.py` puede
#  ver y el motor tampoco: que los cuatro tipos LLEGUEN a la fila, que su
#  reduccion se LEA, y que un proyecto vuelva con sus numeros. Son fallos de
#  INDICE y de estado — una tapa que enciende el efecto de al lado se maqueta
#  perfecta, no solapa, no se sale, no corta un rotulo y esta traducida. Es la
#  familia de los cinco fallos del compas del piano, otra vez.
#
#  SE MIDE POR LA TAPA Y POR EL MANDO. Llamar a `setDynP0` por dentro se salta
#  el camino de verdad —del mando a `pushFxParam`, de ahi a
#  `AudioEngine::setFxParam` y de ahi al atomico— y es ese switch de treinta y
#  tres casos el que se equivoca de una fila.
#
#      python3 Tests/dinamica.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  El dedo minimo, `Metrics::hit`. El liston de una prueba no se reinventa en
#  la de al lado.
DEDO = 40

#  CUANTOS TIPOS HAY lo dice la APP (`AudioEngine::kNumFx`, en la linea del
#  volcado) y no una cuenta escrita aqui: escrito en los dos sitios son dos
#  reglas, y el dia que entre el doce la que se queda vieja es esta. Lo que si
#  se comprueba aqui es que el menu OFREZCA todos los que hay.

#  Los indices de los cuatro, que son los de `AudioEngine::kFxCmp` y
#  siguientes. Escritos aqui porque es lo que la prueba comprueba: que la tapa
#  del menu ponga el tipo que dice y no el de al lado.
CMP, GTE, DSS, LIM = 7, 8, 9, 10

#  La mas ancha y la mas estrecha que nadie fabrica. La segunda es la que
#  decide si once celdas caben en tres columnas.
PANTALLAS = ["412x915", "280x653"]


#  SIN PANTALLA NO SE MIDE NADA, y hay que DECIRLO. Sin esta guarda, un Xvfb
#  muerto sale como «la app no publico la linea» —o sea como un fallo de la
#  app— y se pierde media tarde buscando un cambio que no era. Es la misma
#  guarda que ya abre `expo.py`, `kits.py` y las demas.
def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (size):
    casa = tempfile.mkdtemp (prefix="zati-dyn-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG="es", ZATI_DYN="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("dyn"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes -esto no mide nada sin "
               "pantalla, y sin decirlo saldria como un fallo de la app-")
        return 1

    malas = []

    for size in PANTALLAS:
        r = corre (size)
        if r is None:
            print ("FALLA  %s: la app no publico la linea de dinamica" % size)
            return 1

        #  1. TODOS LOS TIPOS ESTAN EN EL MENU. Con mas tipos que ranuras el
        #     menu es la UNICA puerta a los que no caben en la fila: si la rejilla se
        #     hubiera quedado en siete celdas, CMP, GTE, DSS y LIM sonarian y
        #     no habria forma de ponerlos — un efecto al que no se llega es un
        #     efecto que no esta.
        tipos = r["tipos"]
        print ("%-9s menu    %d de %d tipos ofrecidos   celda %dx%d px   (dedo %d)"
               % (size, r["en_menu"], r["tipos"], r["celda_w"], r["celda_h"], DEDO))
        if r["en_menu"] != r["tipos"]:
            malas.append ("%s: el menu ofrece %d tipos de %d"
                          % (size, r["en_menu"], r["tipos"]))
        #  Y la celda contra el dedo, que es lo que decidio tres columnas y no
        #  dos: en dos serian seis filas y la tarjeta no da apaisado.
        if r["celda_w"] < DEDO or r["celda_h"] < DEDO:
            malas.append ("%s: la celda del menu mide %dx%d, por debajo del dedo"
                          % (size, r["celda_w"], r["celda_h"]))

        #  2. CADA TIPO LLEGA A SU RANURA POR EL GESTO, con DOS: con uno solo,
        #     un switch corrido de una fila pasa la prueba si la ranura y el
        #     tipo coinciden por casualidad.
        print ("%-9s ranuras   la 0 lleva el tipo %d   la 1 el %d"
               % (size, r["ranura0"], r["ranura1"]))
        if r["ranura0"] != CMP or r["ranura1"] != LIM:
            malas.append ("%s: se puso CMP (%d) y LIM (%d) y quedaron %d y %d"
                          % (size, CMP, LIM, r["ranura0"], r["ranura1"]))

        #  3. LOS TRES MANDOS ESCRIBEN EN SU EFECTO Y NO EN EL DE AL LADO, con
        #     un TESTIGO: a CMP se le da un umbral y a LIM un techo DISTINTOS.
        #     `AudioEngine::setFxParam` es un switch de treinta y tres casos y
        #     equivocarse de una fila es exactamente como se escribe mal.
        print ("%-9s mandos  CMP umbral %+.2f dB ratio %.2f:1   LIM techo %+.2f dB soltar %.0f ms"
               % (size, r["cmp_umbral"], r["cmp_ratio"], r["lim_techo"], r["lim_soltar"]))
        if abs (r["cmp_umbral"] + 30.0) > 0.01 or abs (r["cmp_ratio"] - 6.0) > 0.01:
            malas.append ("%s: CMP quedo en umbral %+.2f y ratio %.2f, pedidos -30.00 y 6.00"
                          % (size, r["cmp_umbral"], r["cmp_ratio"]))
        if abs (r["lim_techo"] + 12.0) > 0.01 or abs (r["lim_soltar"] - 200.0) > 0.5:
            malas.append ("%s: LIM quedo en techo %+.2f y soltar %.0f, pedidos -12.00 y 200"
                          % (size, r["lim_techo"], r["lim_soltar"]))

        #  4. LA REDUCCION SE LEE, Y SOLO CON EL DEDO FUERA. Un compresor que
        #     no dice cuanto comprime es invisible — los tres mandos dicen lo
        #     que le has PEDIDO y ninguno lo que esta haciendo. Y las DOS
        #     mitades: una casilla que dice la reduccion mientras se mueve el
        #     mando es un control contando otra cosa que su propio numero, que
        #     es el fallo que ya costo una medida con el corte del pad.
        print ("%-9s lectura   suelto «%s»   tocado «%s»"
               % (size, r["lee_suelto"], r["lee_tocado"]))
        if "dB" not in r["lee_suelto"] or not r["lee_suelto"].lstrip ("‎").startswith ("-"):
            malas.append ("%s: la casilla no dice la reduccion con el dedo fuera: «%s»"
                          % (size, r["lee_suelto"]))
        if "%" not in r["lee_tocado"]:
            malas.append ("%s: la casilla no vuelve al valor del mando al tocarlo: «%s»"
                          % (size, r["lee_tocado"]))

        #  4b. Y LA REDUCCION ES DEL CANAL. Hay DIECISEIS limitadores, uno por
        #      canal, y el pad 0 vive en el canal cero: el del cuatro no ha
        #      visto una muestra en su vida. Con DOS cifras, o «el limitador
        #      reduce» lo cumple igual un motor con UNO compartido —que es
        #      exactamente lo que habia antes de los canales— y entonces dos
        #      canales enseñarian el mismo medidor de reduccion.
        print ("%-9s canal   reduccion en el canal 0 %.2f dB   en el 4 %.2f"
               % (size, r["red_c0"], r["red_c4"]))
        #  `getDynReduccion` devuelve CUANTO se quita, o sea positivo: es la
        #  casilla la que lo escribe con signo menos.
        if r["red_c0"] < 0.5:
            malas.append ("%s: el limitador del canal 0 quita %.2f dB con el tono "
                          "entrando: no esta limitando" % (size, r["red_c0"]))
        if abs (r["red_c4"]) > 0.01:
            malas.append ("%s: el limitador del canal 4 quita %.2f dB y por ese "
                          "canal no pasa nada: los dieciseis comparten uno"
                          % (size, r["red_c4"]))

        #  5. Y VUELVEN DEL FICHERO DE PROYECTO. Se escribe con el MISMO arbol
        #     que escribe el fichero, se BORRA a mano —si al volver sigue
        #     puesto no es que se haya guardado, es que nadie lo quito— y se
        #     abre. Con los DOS: los parametros y la fila, que son dos
        #     propiedades distintas y solo una de las dos existia antes.
        print ("%-9s proyecto  CMP vuelve en %+.2f   LIM en %+.2f   ranuras %d y %d"
               % (size, r["cmp_vuelve"], r["lim_vuelve"], r["r0_vuelve"], r["r1_vuelve"]))
        if abs (r["cmp_vuelve"] + 30.0) > 0.01 or abs (r["lim_vuelve"] + 12.0) > 0.01:
            malas.append ("%s: del proyecto vuelven CMP %+.2f y LIM %+.2f, guardados -30 y -12"
                          % (size, r["cmp_vuelve"], r["lim_vuelve"]))
        if r["r0_vuelve"] != CMP or r["r1_vuelve"] != LIM:
            malas.append ("%s: del proyecto vuelven las ranuras %d y %d, guardadas %d y %d"
                          % (size, r["r0_vuelve"], r["r1_vuelve"], CMP, LIM))
        print()

    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("los %d tipos caben en el menu, cada tapa pone el suyo, los mandos "
           "escriben en su efecto, la reduccion se lee con el dedo fuera y todo "
           "vuelve del proyecto" % tipos)
    return 0


if __name__ == "__main__":
    sys.exit (main())

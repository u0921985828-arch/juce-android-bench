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

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

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


#  SIN PANTALLA NO SE MIDE NADA, y hay que DECIRLO. Sin esta guarda, un Xvfb
#  muerto sale como «la app no publico la linea» —o sea como un fallo de la
#  app— y se pierde media tarde buscando un cambio que no era. Es la misma
#  guarda que ya abre `expo.py`, `kits.py` y las demas.
def display_alive():
    d = PANTALLA
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (size):
    casa = tempfile.mkdtemp (prefix="zati-eq-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
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
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes -esto no mide nada sin "
               "pantalla, y sin decirlo saldria como un fallo de la app-")
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

        #  6. LA CURVA QUE SE DIBUJA NO ES PLANA CUANDO LAS BANDAS NO LO ESTAN.
        #     Es la regla que faltaba, y la que habria cazado el fallo que llego
        #     en una foto del telefono: los cinco nodos movidos y una RAYA
        #     RECTA. La causa era exacta y ninguna de las cinco de arriba podia
        #     verla — miran las GANANCIAS y la guarda, no la FORMA de lo que se
        #     pinta. `Eq5::recalcula` solo se llamaba desde `procesa`, o sea
        #     desde el hilo de audio, y el ESPEJO del que se dibuja no procesa
        #     audio nunca: su bandera `sucio` se quedaba puesta para siempre y
        #     `respuestaEnDb` evaluaba la tabla de coeficientes de la curva
        #     plana. El banco del motor tampoco: mide sobre una instancia que
        #     acaba de procesar.
        #
        #     La app lo mide PINTANDO -quien pone los coeficientes al dia es
        #     `EqCurve::paint`- y saca el recorrido entre el maximo y el minimo
        #     de la respuesta en las cinco frecuencias de fabrica, con
        #     +5/-6/+3.5/-4/+8 dB escritos. Roto a proposito quitando el
        #     `refresca()`: 0.00 dB.
        print ("%-9s curva   recorrido %.2f dB con las cinco bandas escritas"
               % (size, r["recorrido"]))
        if r["recorrido"] < 6.0:
            malas.append ("%s: la curva sale PLANA con las bandas escritas: %.2f dB "
                          "de recorrido" % (size, r["recorrido"]))

        #  7. MANTENER SOBRE UN NODO ABRE SU FICHA Y ARRASTRAR NO, que son DOS
        #     cifras porque una sola se engaña por los dos lados: «abre» lo
        #     cumple igual un gesto que abre siempre, y entonces cada arrastre
        #     acabaria con la ficha delante al soltar; «no abre al arrastrar» lo
        #     cumple igual un gesto muerto. Medido POR EL GESTO -mouseDown,
        #     mouseDrag y el reloj vencido como lo venceria el sistema- y no
        #     llamando a `onNodo`, que es justo donde el fallo no existe.
        print ("%-9s mantener  armado al apoyar %d   tras arrastrar %d   ficha %d banda %d"
               % (size, r["armado_apoyar"], r["armado_arrastre"],
                  r["ficha_mantener"], r["banda_abierta"] + 1))
        if r["armado_apoyar"] != 1 or r["ficha_mantener"] != 1:
            malas.append ("%s: mantener sobre un nodo no abre su ficha "
                          "(armado %d ficha %d)"
                          % (size, r["armado_apoyar"], r["ficha_mantener"]))
        if r["banda_abierta"] != 2:
            malas.append ("%s: se mantuvo sobre la banda 3 y se abrio la %d"
                          % (size, r["banda_abierta"] + 1))
        if r["armado_arrastre"] != 0:
            malas.append ("%s: arrastrar no cancela el mantener: el menu se abriria "
                          "al soltar" % size)

        #  8. EL RECORRIDO ES LA BANDA AUDIBLE ENTERA, y la banda mas aguda
        #     LLEGA a donde dice el mando. Va con DOS cifras porque una sola se
        #     engaña: «donde queda» lo cumple igual un numero guardado y no
        #     aplicado, y solo la respuesta DIBUJADA ahi arriba dice que el
        #     filtro esta de verdad en esos 20 kHz.
        #
        #     El tope interno de `recalcula` era `fs * 0.45` —19 845 Hz a
        #     44.1 kHz— asi que con el techo en 20 000 la banda se habria
        #     quedado 156 Hz por debajo de donde el nodo la dibuja, en silencio:
        #     el mando diciendo una cosa y el filtro haciendo otra, que es el
        #     fallo que ya costo una medida con el corte del pad.
        print ("%-9s rango   %.0f Hz a %.0f   la aguda a tope queda en %.0f Hz "
               "y dibuja %+.2f dB"
               % (size, r["f_min"], r["f_max"], r["f_tope"], r["db_tope"]))
        if abs (r["f_min"] - 20.0) > 0.5 or abs (r["f_max"] - 20000.0) > 0.5:
            malas.append ("%s: el recorrido va de %.0f a %.0f y tiene que ir de "
                          "20 a 20000" % (size, r["f_min"], r["f_max"]))
        if abs (r["f_tope"] - r["f_max"]) > 1.0:
            malas.append ("%s: la banda aguda a tope quedo en %.0f Hz de los %.0f "
                          "que dice el mando" % (size, r["f_tope"], r["f_max"]))
        #  Y NO «MAS DE TANTO», sino una IDENTIDAD: un estante alto sube la
        #  MITAD de su ganancia justo en su propia frecuencia —esa es la
        #  definicion de la frecuencia de un estante— asi que con +10 dB
        #  escritos en 20 kHz lo que se dibuja ahi son **5.00 clavados**. Un
        #  liston de «mas de 3» lo cumple igual un filtro arrastrado a otro
        #  sitio: por encima de su esquina un estante tiende a su ganancia
        #  entera, o sea que un tope que se llevara la banda a 16.8 kHz daria
        #  +9 y pasaria. La identidad no.
        #
        #  El banco corre a 48 kHz, donde el `fs * 0.45` de antes no llegaba a
        #  morder -21 600 > 20 000-: el fallo es de 44.1 kHz. Se rompe a
        #  proposito bajando el tope a `fs * 0.35`, que es exactamente lo que
        #  44.1 kHz le haria a una banda de 20 kHz.
        if abs (r["db_tope"] - 5.0) > 0.5:
            malas.append ("%s: la banda aguda esta escrita en %.0f Hz con +10 dB y "
                          "la curva dibuja %+.2f dB ahi -un estante en su propia "
                          "frecuencia sube 5.00-"
                          % (size, r["f_max"], r["db_tope"]))

        #  9. EL TIPO Y LA Q, por la TAPA y por el MANDO. Un tipo de PASO no
        #     tiene ganancia — corta, no realza — asi que su nodo se queda
        #     clavado en la linea de cero: con +8 dB escritos, pasar la banda a
        #     PASO ALTO tiene que dejar `gainVisible` en 0. Y la Q escribe en
        #     los DOS sitios, espejo y motor, que es lo unico que separa «la
        #     curva se estrecha» de «suena mas estrecho»: con uno solo, uno de
        #     los dos se queda contando otra cosa.
        print ("%-9s banda   tipo tras el chip %d   ganancia visible %+.2f   "
               "Q motor %.2f espejo %.2f"
               % (size, r["tipo_chip"], r["visible_paso"], r["q_motor"], r["q_espejo"]))
        if r["tipo_chip"] != 3:
            malas.append ("%s: el chip PASO ALTO dejo la banda en el tipo %d"
                          % (size, r["tipo_chip"]))
        if abs (r["visible_paso"]) > 0.01:
            malas.append ("%s: un tipo de paso dibuja %+.2f dB de ganancia"
                          % (size, r["visible_paso"]))
        if abs (r["q_motor"] - 4.5) > 0.02 or abs (r["q_espejo"] - 4.5) > 0.02:
            malas.append ("%s: la Q quedo en motor %.2f y espejo %.2f, pedida 4.50"
                          % (size, r["q_motor"], r["q_espejo"]))

        #  9. Y LA CURVA ES DEL CANAL, que es la queja con la que empezo la
        #     tanda dicha en su efecto: «solo es posible que un ecualizador
        #     funcione y sea colocado en un canal solo». Hay DIECISEIS `Eq5`,
        #     uno por canal.
        #
        #     Con CUATRO cifras porque el EQ tiene dos lados y cada uno puede
        #     mentir por su cuenta: lo que el MOTOR guarda en el canal 0 y en
        #     el 4, y lo que la curva DIBUJA al llegar al 4 y al volver al 0.
        #     Solo las dos primeras las cumple una app con dieciseis
        #     ecualizadores y un espejo que no se recarga —la curva enseñaria
        #     la del canal anterior sobre el filtro nuevo— y solo las dos
        #     ultimas una que recarga el espejo de un motor con uno solo. Y la
        #     cuarta es la que impide que «en el 4 sale plana» lo cumpla un
        #     codigo que BORRA la curva al cambiar de canal.
        print ("%-9s canal   motor c0 %+.2f dB  c4 %+.2f   curva en c4 %+.2f  "
               "al volver %+.2f"
               % (size, r["curva_c0"], r["curva_c4"], r["espejo_c4"],
                  r["espejo_vuelve"]))
        if abs (r["curva_c0"] - 9.0) > 0.05:
            malas.append ("%s: se escribieron +9 dB en el canal 0 y el motor "
                          "guardo %+.2f" % (size, r["curva_c0"]))
        if abs (r["curva_c4"]) > 0.05:
            malas.append ("%s: escribir la banda 2 en el canal 0 dejo %+.2f dB en "
                          "el canal 4: un ecualizador es de CADA canal"
                          % (size, r["curva_c4"]))
        if abs (r["espejo_c4"]) > 0.05:
            malas.append ("%s: al ir al canal 4 la curva sigue dibujando %+.2f dB: "
                          "el espejo se quedo con el canal anterior"
                          % (size, r["espejo_c4"]))
        if abs (r["espejo_vuelve"] - 9.0) > 0.05:
            malas.append ("%s: al volver al canal 0 la curva dibuja %+.2f dB y se "
                          "escribieron +9: cambiar de canal no lee la curva, la "
                          "BORRA" % (size, r["espejo_vuelve"]))
        print()

    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("el plato pasa a la curva, cada nodo se agarra, escribe su banda, no "
           "cruza a la vecina, no responde en el aire, la curva sigue a las "
           "bandas, va de 20 Hz a 20 kHz y llega, mantener abre la ficha y el "
           "tipo y la Q escriben en los dos")
    return 0


if __name__ == "__main__":
    sys.exit (main())

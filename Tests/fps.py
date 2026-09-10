#!/usr/bin/env python3
# ============================================================================
#  QUE LA APP SE VEA IGUAL A 60 QUE A 120.
#
#  Esta es la regla que faltaba, y no es preventiva: el fallo que caza estaba
#  puesto HOY, antes de que el dibujo colgara del vblank. Todas las constantes
#  de tiempo visuales estaban escritas como un factor POR CUADRO y documentadas
#  contra treinta cuadros por segundo:
#
#      peak     *= 0.72f          la aguja del medidor
#      hold     *= 0.985f         la retencion de pico
#      clipHold  = 90             «~3 s a 30 cuadros»
#      padFlash *= 0.8f           el destello de un pad
#      s        += 0.25f * (dB-s) la caida del analizador del EQ
#
#  Y treinta cuadros por segundo es lo que tenia la gama ALTA. La tabla de
#  `DeviceTier` repartia 100, 60, 40 y 33 ms —10, 16.7, 25 y 30 fps— asi que el
#  mismo aviso de clip duraba **3 s en un movil bueno y 9 en uno de gama
#  basica**, y la aguja caia a cuatro velocidades distintas segun el telefono.
#  Con el vblank eso habria pasado de cuatro velocidades a una por panel: 60,
#  90 o 120.
#
#  SE MIDE EN MILISEGUNDOS DE RELOJ Y NO EN CUADROS, que es lo unico que separa
#  las dos formas de escribirlo: contando cuadros, el fallo y el arreglo dan
#  exactamente el mismo numero.
#
#  Y por el camino de verdad —`SpectrumDisplay::setSamples` y
#  `MainComponent::pintaCuadro`—, no repitiendo la formula aqui: *un banco que
#  repite la constante del codigo no prueba el codigo*, que es lo que esta casa
#  ya pago con la mascara del lanzador.
#
#  Roto a proposito devolviendo los factores por cuadro: sale el DOBLE de
#  rapido a 120 que a 60, que es exactamente el fallo que existia entre un
#  movil de gama alta y uno de gama basica.
#
#      python3 Tests/fps.py
# ============================================================================
import json, os, subprocess, sys

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  EL MARGEN, y sale de la cuenta y no de un numero redondo: lo que se mide es
#  un decaimiento muestreado, asi que la respuesta se redondea hacia arriba al
#  siguiente cuadro. A 60 Hz eso son 16.7 ms de cuantizacion y a 120, 8.3, o
#  sea que las dos cadencias pueden diferir legitimamente en un cuadro de 60.
#  El 8 % deja sitio para eso sobre los 230 ms de la aguja —el mas corto de los
#  tres— y esta muy por debajo del 100 % que da el fallo.
MARGEN = 0.08

#  Lo que cada una tiene que durar, resuelto de la constante de tiempo y no
#  copiado del codigo: la aguja son 100 ms de tau y se mide hasta la decima
#  parte, o sea 100 * ln(10); el aviso de clip son tres segundos enteros; el
#  destello son 150 ms de tau hasta 0.02, o sea 150 * ln(50).
ESPERADO = {"aguja": 230.0, "clip": 3000.0, "destello": 587.0}


def display_alive():
    d = PANTALLA
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre():
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_BALISTICA": "1"})
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"balistica"' in linea:
            return json.loads (linea)
    return None


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada:", APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo");  return 1

    d = corre()
    if d is None:
        print ("FALLA: la app no publico la linea de balistica");  return 1

    malas = []
    print ("            60 Hz      120 Hz    esperado")
    for quien in ("aguja", "clip", "destello"):
        a, b = d.get (quien + "60", 0.0), d.get (quien + "120", 0.0)
        esp  = ESPERADO[quien]
        peor = max (a, b)
        rel  = abs (a - b) / max (1.0, peor)
        mal  = rel > MARGEN
        #  Y las DOS mitades, que es lo que separa las dos formas de fallar:
        #  «60 y 120 coinciden» lo cumple tambien una constante que se ha
        #  quedado clavada en el numero equivocado -y entonces el aviso de clip
        #  dura un segundo en los dos-, y «dura tres segundos» lo cumple una
        #  cadencia sola sin comparar con la otra.
        fuera = abs (peor - esp) / esp > 0.10
        if fuera: mal = True
        print ("%-10s %8.1f ms %8.1f ms %8.1f ms%s"
               % (quien, a, b, esp,
                  "   <-- %.0f%% de diferencia" % (100.0 * rel) if rel > MARGEN else
                  ("   <-- no es lo que dura" if fuera else "")))
        if mal: malas.append (quien)

    print()
    if malas:
        print ("FALLA: se ve distinto a 60 que a 120 —", ", ".join (malas))
        return 1
    print ("las tres duran lo mismo a las dos cadencias")
    return 0


if __name__ == "__main__":
    sys.exit (main())

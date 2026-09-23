#!/usr/bin/env python3
# ============================================================================
#  QUE LA LATENCIA VUELVA SOLA.
#
#  La app pide el buffer mas pequeno que ofrece el driver -un burst del
#  hardware- porque la latencia es su argumento entero, y cuenta los under-runs
#  para subirlo si el telefono no llega. Eso ya estaba. Lo que NO estaba es la
#  vuelta: `burstMult` solo subia. Subia por cuatro chasquidos, se escribia en
#  `buffer.txt` y de ahi no se movia ni en esa sesion ni en ninguna de las
#  siguientes, porque el arranque lee el fichero.
#
#  O sea que UN mal rato -otra app comiendose el telefono, una llamada, el
#  sistema indexando- dejaba la app en cuatro bursts PARA SIEMPRE. Con un burst
#  de 256 a 48 kHz eso es pasar de 5.3 ms de buffer a 21.3, y de ~16 ms de
#  salida a ~64: cuatro veces la latencia por un chasquido de hace tres
#  semanas, sin que nada lo dijera.
#
#  LOS CHASQUIDOS SON UNA ENTRADA. Toda esta ley depende de `getXRunCount`, y
#  en el escritorio no hay aparato que lo cuente: sin `ZATI_XRUN` no habria
#  forma de medirla y por eso llevaba desde el primer dia sin medir.
#  `ZATI_XRUN="ms:cuantos,..."` los inyecta por el MISMO camino por el que
#  llegan los de verdad -la ley corre una vez y no dos, que es lo unico que
#  hace que esto pruebe algo- y `ZATI_XRUN_ESCALA` acelera el reloj, porque
#  medir cuarenta y cinco segundos de tramo limpio costaria cuarenta y cinco
#  segundos de banco.
#
#  Tres ramas y las tres se miden:
#
#    1. SUBE. Cuatro chasquidos seguidos y el multiplicador pasa de 1 a 2.
#    2. BAJA. Cuarenta y cinco segundos limpios y vuelve a 1. Antes: nunca.
#    3. Y APRENDE. Si al bajar vuelve a crepitar, ese nivel queda descartado en
#       esta sesion (`suelo` sube) y la app deja de viajar entre dos buffers
#       con un corte de sonido en cada viaje.
#
#  Roto a proposito quitando la rama de bajada: el multiplicador se queda en 2
#  para siempre, que es exactamente lo que la app hacia.
#
#      python3 Tests/buffer.py
# ============================================================================
import json, os, subprocess, sys

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  Veinte veces el reloj: un tick de mantenimiento son 60 ms, asi que cada uno
#  cubre 1.2 s de los de la ley y los cuarenta y cinco segundos de tramo limpio
#  caben en treinta y ocho ticks.
ESCALA = 20


def display_alive():
    try:
        return subprocess.run (["xdpyinfo", "-display", PANTALLA],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def limpia_preferencia():
    #  Y SE PARTE DEL MISMO SITIO. La app RECUERDA el multiplicador en
    #  `buffer.txt` -es lo que hace que el proximo arranque no vuelva a
    #  crepitar para aprender lo mismo- asi que la corrida anterior deja el
    #  estado puesto y la siguiente empezaria en 2 en vez de en 1. Se borra: lo
    #  que esta regla mide es la LEY, y la memoria es otra cosa.
    import glob
    for d in ("Music/ZATI", ".config/ZATI", "ZATI"):
        for f in glob.glob (os.path.join (os.path.expanduser ("~"), d, "buffer.txt")):
            try:
                os.remove (f)
            except OSError:
                pass


def corre (guion, ticks):
    limpia_preferencia()
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_XRUN": guion, "ZATI_XRUN_ESCALA": str (ESCALA),
                 "ZATI_ARRANQUE": str (ticks)})
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    #  El camino: cada cambio de (mult, suelo) con el instante en el que paso.
    pasos, ant = [], None
    for l in out.splitlines():
        if '"buffer"' not in l:
            continue
        try:
            r = json.loads (l)
        except Exception:
            continue
        k = (r["mult"], r["suelo"])
        if k != ant:
            pasos.append ((r["ms"], r["mult"], r["suelo"]))
            ant = k
    return pasos


def escribe (linea, pasos):
    print ("%-46s %s" % (linea,
                         "  ".join ("%ds:%d/%d" % (ms // 1000, m, s)
                                    for ms, m, s in pasos)))


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada en %s" % APP);  return 1
    if not display_alive():
        print ("no hay pantalla en %s" % PANTALLA);  return 1

    malas = []
    print ("caso                                           camino (segundo:mult/suelo)")

    #  1 y 2. Cuatro chasquidos de golpe, y luego nada.
    pasos = corre ("6000:4", 140)
    escribe ("cuatro chasquidos, y luego limpio", pasos)
    mult = [m for _, m, _ in pasos]
    if mult[:3] != [1, 2, 1]:
        print ("   <-- se esperaba 1 -> 2 -> 1 y salio %s" % mult);  malas.append ("sube y baja")
    else:
        #  Y CUANDO. Bajar antes de tiempo seria reabrir el stream cada dos por
        #  tres, que corta el sonido; el plazo es de cuarenta y cinco segundos.
        baja = pasos[2][0] - pasos[1][0]
        if not (40000 <= baja <= 70000):
            print ("   <-- bajo a los %d ms, y el plazo son 45000" % baja)
            malas.append ("el plazo de bajada")

    #  3. Y si al bajar vuelve a crepitar, ese nivel se descarta.
    pasos = corre ("6000:4,56000:4", 260)
    escribe ("y si al bajar vuelve a crepitar", pasos)
    suelo = [s for _, _, s in pasos]
    if suelo[-1] <= 1:
        print ("   <-- el suelo sigue en %d: la app volveria a bajar y a subir "
               "para siempre" % suelo[-1])
        malas.append ("el suelo aprendido")

    print ()
    if malas:
        print ("VEREDICTO: FALLA (%s)" % ", ".join (malas));  return 1
    print ("VEREDICTO: OK");  return 0


if __name__ == "__main__":
    sys.exit (main())

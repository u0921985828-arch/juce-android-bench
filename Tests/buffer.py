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
#  Y DOS RAMAS MAS, que son los TIRONES Y LOS CRISPEOS POR BLUETOOTH:
#
#    4. EL APARATO QUE NO CUENTA NO ES EL APARATO LIMPIO. `getXRunCount` tiene
#       tres respuestas y no dos: un numero, cero, y «aqui no hay contador»
#       -Oboe contesta `ErrorUnimplemented` en el legado de OpenSL y en AAudio
#       fuera del carril MMAP, que por radio es SIEMPRE-. JUCE se comia ese
#       error y devolvia cero, o sea la respuesta de un aparato limpio, asi que
#       la ley sumaba tramo limpio y a los 45 s BAJABA el bloque -reabriendo el
#       flujo, que corta el sonido- hasta dejarlo en el minimo, que es donde
#       cruje, y sin poder volver a subirlo porque para subir hace falta el
#       contador que no hay. `ci/patch_juce_oboe.py` hace que el error llegue
#       como -1 y `ZATI_XRUN` lo inyecta con una cuenta NEGATIVA.
#
#    5. Y POR BLUETOOTH EL SUELO SON DOS BURST, a proposito. El enlace mete
#       entre 100 y 250 ms que no los quita ningun ajuste: los milisegundos que
#       se ahorran pidiendo el burst minimo no se oyen ahi y el corte que ese
#       burst no absorbe si. `ZATI_RUTA=bluetooth` es la entrada, la misma
#       figura que en `Tests/cuenta.py` con la guarda del monitor.
#
#       Y lo que se aprende por radio se guarda APARTE (`buffer-bt.txt`): un
#       numero compartido significaria que los cascos de ayer le cuestan
#       latencia al altavoz de hoy. Se comprueba el fichero, no la intencion.
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


#  Los dos nombres: la ruta directa y la de radio. Ver MainComponent::rutaBt.
PREFS = ("buffer.txt", "buffer-bt.txt")


def ficheros (nombre):
    """Los sitios donde la app puede tener su `home()`. Ver ProjectStore."""
    import glob
    sitios = []
    for d in ("Music/ZATI", ".config/ZATI", "ZATI"):
        sitios += glob.glob (os.path.join (os.path.expanduser ("~"), d, nombre))
    return sitios


def carpeta_casa():
    """La `home()` que la app esta usando de verdad, si ya hay alguna."""
    for d in ("Music/ZATI", ".config/ZATI", "ZATI"):
        ruta = os.path.join (os.path.expanduser ("~"), d)
        if os.path.isdir (ruta):
            return ruta
    return None


def limpia_preferencia():
    #  Y SE PARTE DEL MISMO SITIO. La app RECUERDA el multiplicador en
    #  `buffer.txt` -es lo que hace que el proximo arranque no vuelva a
    #  crepitar para aprender lo mismo- asi que la corrida anterior deja el
    #  estado puesto y la siguiente empezaria en 2 en vez de en 1. Se borra: lo
    #  que esta regla mide es la LEY, y la memoria es otra cosa.
    for nombre in PREFS:
        for f in ficheros (nombre):
            try:
                os.remove (f)
            except OSError:
                pass


def siembra_preferencia (nombre, valor):
    """Un telefono que ya aprendio su nivel llega con esto puesto.

    Hace falta para medir la BAJADA: sin preferencia el multiplicador arranca en
    el suelo y una regla sobre la bajada no podria fallar nunca.
    """
    casa = carpeta_casa()
    if casa is None:
        return False
    with open (os.path.join (casa, nombre), "w") as f:
        f.write (str (valor))
    return True


def lee_preferencia (nombre):
    for f in ficheros (nombre):
        try:
            with open (f) as fh:
                return fh.read().strip()
        except OSError:
            pass
    return None


def corre (guion, ticks, ruta=None, siembra=None):
    limpia_preferencia()
    if siembra is not None:
        if not siembra_preferencia (*siembra):
            return None                      # cadena de control: ver main
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_XRUN": guion, "ZATI_XRUN_ESCALA": str (ESCALA),
                 "ZATI_ARRANQUE": str (ticks)})
    if ruta is not None:
        env["ZATI_RUTA"] = ruta
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    #  El camino: cada cambio de (mult, suelo) con el instante en el que paso.
    pasos, ant, bt = [], None, None
    for l in out.splitlines():
        if '"buffer"' not in l:
            continue
        try:
            r = json.loads (l)
        except Exception:
            continue
        #  CADENA DE CONTROL DE LA RUTA: la app publica por donde cree que sale
        #  el sonido. Si `ZATI_RUTA` no llega a la ley, las reglas de abajo
        #  medirian la ruta directa creyendo medir la de radio.
        bt = r.get ("bt")
        k = (r["mult"], r["suelo"])
        if k != ant:
            pasos.append ((r["ms"], r["mult"], r["suelo"]))
            ant = k
    return pasos, bt


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
    pasos, _ = corre ("6000:4", 140)
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
    pasos, _ = corre ("6000:4,56000:4", 260)
    escribe ("y si al bajar vuelve a crepitar", pasos)
    suelo = [s for _, _, s in pasos]
    if suelo[-1] <= 1:
        print ("   <-- el suelo sigue en %d: la app volveria a bajar y a subir "
               "para siempre" % suelo[-1])
        malas.append ("el suelo aprendido")

    #  4a. EL CONTRASTE, que es lo que hace que 4b pruebe algo: un aparato que
    #      SI cuenta, que llega con el nivel 2 aprendido y que va limpio, baja.
    #      Sin esta mitad, «se queda en 2» podria ser que la bajada no funciona.
    r = corre ("0:0", 140, siembra=("buffer.txt", 2))
    if r is None:
        print ("no se pudo sembrar la preferencia: no hay carpeta de la app")
        malas.append ("la siembra de la preferencia")
    else:
        pasos, _ = r
        escribe ("cuenta y va limpio, desde el 2", pasos)
        mult = [m for _, m, _ in pasos]
        if mult[0] != 2 or 1 not in mult:
            print ("   <-- se esperaba empezar en 2 y bajar a 1, y salio %s" % mult)
            malas.append ("la bajada desde la preferencia")

    #  4b. Y EL APARATO QUE NO CUENTA SE QUEDA DONDE ESTA. Mismo punto de
    #      partida, mismo plazo, y la unica diferencia es la respuesta del
    #      contador: -1 en vez de 0. Antes del arreglo las dos corridas de
    #      arriba y de aqui eran LA MISMA, porque JUCE devolvia cero para las
    #      dos, y esta bajaba igual: el bloque minimo por una ruta que no lo
    #      aguanta, y una reapertura -un corte de sonido- para llegar ahi.
    r = corre ("0:-1", 140, siembra=("buffer.txt", 2))
    if r is None:
        malas.append ("la siembra de la preferencia")
    else:
        pasos, _ = r
        escribe ("no lleva la cuenta, desde el 2", pasos)
        mult = [m for _, m, _ in pasos]
        if len (mult) != 1 or mult[0] != 2:
            print ("   <-- se esperaba quedarse en 2 y salio %s: un bloque que "
                   "no se puede corregir no se mueve" % mult)
            malas.append ("el aparato que no cuenta")

    #  5a. POR RADIO, EL SUELO SON DOS. Sin preferencia ninguna y con el
    #      contador limpio: la ruta directa arrancaria en 1 -lo dice el caso 1-
    #      y esta tiene que arrancar en 2 y no bajar de ahi en todo el plazo.
    r = corre ("0:0", 140, ruta="bluetooth")
    pasos, bt = r
    escribe ("por bluetooth, limpio y sin preferencia", pasos)
    if bt != 1:
        print ("   <-- la app no se cree en una ruta de radio (bt=%r): "
               "ZATI_RUTA no llega a la ley y esta regla no mide nada" % (bt,))
        malas.append ("ZATI_RUTA=bluetooth no llega")
    else:
        mult = [m for _, m, _ in pasos]
        if mult != [2]:
            print ("   <-- se esperaba quedarse en 2 y salio %s" % mult)
            malas.append ("el suelo por bluetooth")

    #  5b. Y LO QUE SE APRENDE POR RADIO NO LO PAGA EL ALTAVOZ. Cuatro
    #      chasquidos por Bluetooth suben a 3; cuando el tramo limpio vuelve, la
    #      bajada respeta el suelo de la ruta y para en 2, no en 1. Y lo
    #      aprendido se escribe en SU fichero: `buffer.txt` sigue sin existir.
    pasos, bt = corre ("6000:4", 90, ruta="bluetooth")
    escribe ("por bluetooth, cuatro chasquidos", pasos)
    directo, radio = lee_preferencia ("buffer.txt"), lee_preferencia ("buffer-bt.txt")
    print ("%-46s buffer.txt=%s  buffer-bt.txt=%s" % ("", directo, radio))
    mult = [m for _, m, _ in pasos]
    if bt != 1:
        malas.append ("ZATI_RUTA=bluetooth no llega (5b)")
    elif mult != [2, 3, 2]:
        print ("   <-- se esperaba 2 -> 3 -> 2 y salio %s: por radio la bajada "
               "no puede pasarse del suelo" % mult)
        malas.append ("la bajada por bluetooth")
    elif radio is None:
        print ("   <-- no hay buffer-bt.txt: lo aprendido por radio no se guarda")
        malas.append ("la memoria de la ruta de radio")
    elif directo is not None:
        print ("   <-- buffer.txt dice %r: lo aprendido por radio se le esta "
               "cobrando al altavoz" % (directo,))
        malas.append ("la memoria compartida")

    print ()
    if malas:
        print ("VEREDICTO: FALLA (%s)" % ", ".join (malas));  return 1
    print ("VEREDICTO: OK");  return 0


if __name__ == "__main__":
    sys.exit (main())

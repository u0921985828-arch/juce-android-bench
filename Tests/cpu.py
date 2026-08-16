#!/usr/bin/env python3
# ============================================================================
#  LO QUE GASTA LA APP SIN QUE NADIE LA TOQUE.
#
#  El motor se mide corriendo bloques (Tests/Cpu.cpp) y la respuesta de ese
#  banco fue que el motor no es el problema: una sesion entera -secuencia
#  rodando, seis efectos abiertos, filtros por pad, cuatro voces en modo
#  TONO- se lleva el 2 % del presupuesto de un bloque. La CPU de esta app se
#  iba por el otro lado, y por un sitio que ningun perfilador de audio mira:
#  la cara repintandose para no cambiar nada.
#
#  Las fichas de esta app ocupan la VENTANA ENTERA y llevan un velo al 45 %.
#  Eso quiere decir que un repaint() de ficha no repinta la ficha: repinta el
#  chasis, los dieciseis pads, el espectro y los cuarenta controles que hay
#  debajo del velo. Tres sitios lo pedian treinta veces por segundo -el
#  renglon de la cadena en SEC, la rejilla de CANCION y el recuadro de AUDIO
#  en AJUSTES- para mover un texto que no habia cambiado.
#
#  Asi que la prueba no cuenta milisegundos, que dependen de la maquina.
#  Cuenta FOTOGRAMAS COMPLETOS: cuantas veces se ha pintado el fondo de la
#  cara mientras la app estaba abierta y quieta. Ese numero no depende del
#  ordenador, y con la app quieta tiene que ser practicamente cero.
#
#      python3 Tests/cpu.py [segundos]
# ============================================================================
import json, os, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

SEGUNDOS = int (sys.argv[1]) if len (sys.argv) > 1 else 8

#  Las fichas que se abren solas y se quedan abiertas. Las que piden un
#  dialogo del sistema quedan fuera: en un banco sin pantalla no vuelven.
FICHAS = ["", "pads", "sec", "song", "mix", "set", "proj", "midi", "gest",
          "xy", "rack", "chop", "browse"]

#  EL TOPE. Un arranque pinta el fondo una vez, y una ficha que se abre puede
#  pedir otro; a partir de ahi, con nadie tocando nada, no hay motivo para
#  ninguno mas. Tres deja sitio para el asentamiento de los margenes del
#  sistema sin dejar pasar un repintado periodico, que es lo que se busca:
#  a treinta por segundo, ocho segundos son doscientos cuarenta.
TOPE = 3


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (ficha):
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_DEMO": "1", "ZATI_OPEN": ficha, "ZATI_SPIN": str (SEGUNDOS)})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=SEGUNDOS + 90).stdout
    except subprocess.TimeoutExpired:
        return None

    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"spin"' in linea:
            try:
                return json.loads (linea)
            except Exception:
                pass
    return None


def cabezal():
    """El repintado parcial del cabezal, comprobado pixel a pixel.

    La rejilla de pasos ya no se repinta entera cuando el cabezal se mueve:
    se repinta la union de donde estaba y donde esta. Eso es correcto solo si
    TODO lo que cambia de un fotograma al siguiente cae dentro de esa union,
    y eso no se juzga leyendo el codigo. Se pinta la rejilla dos veces entera
    y se comparan los pixeles: un fallo aqui deja un rastro de marcas por la
    rejilla, que es el tipo de fallo que solo se ve en un video."""
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_HEAD": "1"})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=900).stdout
    except subprocess.TimeoutExpired:
        return None
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"cabezal"' in linea:
            try:
                return json.loads (linea)
            except Exception:
                pass
    return None


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada:", APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo");  return 1

    print ("ficha    fotogramas   CPU ms / %d s" % SEGUNDOS)
    malas = []
    for f in FICHAS:
        r = corre (f)
        if r is None:
            print ("%-8s  --   no contesto" % (f or "(cara)"));  malas.append (f or "(cara)")
            continue
        fondos = int (r.get ("fondos", -1))
        print ("%-8s %6d %12.0f%s" % (f or "(cara)", fondos, r.get ("cpu_ms", 0.0),
                                      "   <-- se repinta sola" if fondos > TOPE else ""))
        if fondos > TOPE:
            malas.append (f or "(cara)")

    print()
    c = cabezal()
    if c is None:
        print ("el cabezal no contesto");  malas.append ("cabezal")
    else:
        fuera = int (c.get ("fuera_de_la_zona", -1))
        print ("cabezal: %d pixeles comparados, %d fuera de la zona repintada"
               % (int (c.get ("pixeles", 0)), fuera))
        if fuera != 0:
            malas.append ("cabezal deja rastro")

    print()
    if malas:
        print ("FALLA:", ", ".join (malas))
        return 1
    print ("ninguna ficha se repinta sola (tope %d fotogramas en %d s)" % (TOPE, SEGUNDOS))
    return 0


if __name__ == "__main__":
    sys.exit (main())

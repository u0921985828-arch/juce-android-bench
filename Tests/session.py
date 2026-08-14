#!/usr/bin/env python3
# ============================================================================
#  QUE LA SESION VUELVA ENTERA.
#
#  Las otras pruebas miran la cara y el motor. Esta mira lo unico que la
#  persona no puede recuperar si se pierde: los sonidos que tenia puestos.
#
#  Existe porque habia un pad que no volvia, y no volvia SIEMPRE el mismo -el
#  primero- y no siempre: cuatro arranques de cada cinco. Ninguna prueba podia
#  verlo, porque las dos mitades del banco miran una sola corrida y este es un
#  fallo de carrera entre dos hilos. La causa era que juce::File::createDirectory
#  se da por vencido cuando el mkdir devuelve EEXIST -o sea cuando OTRO hilo
#  acaba de crear ese mismo componente del camino-, asi que el hilo de sesion
#  se iba sin crear .sesion/samples mientras el de mensajes creaba .sesion. El
#  stream se abria con ENOENT, writeSample devolvia false, y nadie mira ese
#  false.
#
#  La prueba es de repeticion y no de una vez: se arranca N veces con una casa
#  limpia y se cuenta. Una sola corrida verde no dice nada de una carrera.
#
#      python3 Tests/session.py [veces]
# ============================================================================
import os, shutil, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
TMP  = os.path.join (ROOT, "build", ".session-test")

#  SESENTA Y CUATRO, no dieciseis. Desde que la app trae sonidos de fabrica
#  (Kits.h) el arranque llena los cuatro bancos, y ZATI_DEMO se limita a
#  reescribir el primero. Mejor para esta prueba: cuatro veces mas ficheros
#  escritos a la vez es cuatro veces mas probable pillar la carrera que la
#  motivo.
EXPECTED = 64
RUNS = int (sys.argv[1]) if len (sys.argv) > 1 else 8


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def one (home, extra_env):
    env = dict (os.environ, HOME=home, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    env.update (extra_env)
    out = subprocess.run ([APP], env=env, capture_output=True, timeout=180)
    return out.stdout.decode ("utf8", "replace")


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")

    bad = 0
    for r in range (RUNS):
        home = os.path.join (TMP, "run%d" % r)
        shutil.rmtree (home, ignore_errors=True)
        os.makedirs (home, exist_ok=True)

        #  Primera corrida: se monta el kit y se cierra. Escribir la sesion es
        #  cosa del hilo de sesion, y el destructor le da hasta tres segundos.
        one (home, {"ZATI_DEMO": "1"})

        samples = os.path.join (home, "Music", "ZATI", ".sesion", "samples")
        wavs = sorted (f for f in os.listdir (samples)) if os.path.isdir (samples) else []
        #  Y que no quede ningun temporal a medias: si queda, un guardado se
        #  fue por el camino de error sin limpiar.
        leftovers = [f for f in wavs if not f.endswith (".wav")]
        wavs = [f for f in wavs if f.endswith (".wav")]

        #  Segunda corrida sobre la misma casa: la que de verdad importa, que
        #  es si la app se encuentra a si misma al volver.
        back = one (home, {})
        restored = -1
        for line in back.splitlines():
            if "recuperada" in line or "restored" in line or "已恢复" in line:
                for tok in line.replace ("[", " ").replace ("]", " ").split():
                    if tok.isdigit(): restored = int (tok); break

        ok = (len (wavs) == EXPECTED and not leftovers and restored == EXPECTED)
        if not ok:
            bad += 1
            missing = sorted (set ("pad%02d.wav" % (i + 1) for i in range (EXPECTED)) - set (wavs))
            print ("FALLA corrida %d: %d/%d WAV%s%s  recuperados=%d"
                   % (r + 1, len (wavs), EXPECTED,
                      "  faltan " + ",".join (missing) if missing else "",
                      "  sobran " + ",".join (leftovers) if leftovers else "",
                      restored))
        else:
            print ("corrida %d: %d WAV, %d recuperados" % (r + 1, len (wavs), restored))

    shutil.rmtree (TMP, ignore_errors=True)
    print()
    print ("las %d corridas devuelven la sesion entera" % RUNS if bad == 0
           else "%d de %d corridas pierden algo" % (bad, RUNS))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit (main())

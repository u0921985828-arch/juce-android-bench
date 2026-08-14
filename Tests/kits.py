#!/usr/bin/env python3
# ============================================================================
#  LOS SESENTA Y CUATRO SONIDOS DE FABRICA.
#
#  Se sintetizan en el arranque (Source/Kits.h), asi que no hay ficheros que
#  mirar en el repositorio: lo que se mide es lo que la app deja escrito en su
#  sesion la primera vez que abre. Eso, ademas, comprueba el camino entero -
#  generar, asignar al pad, escribir el WAV - y no solo la sintesis.
#
#  Lo que un kit tiene que cumplir, y por que cada cosa:
#
#    - NINGUNO MUDO. Un pad que no suena parece un pad roto, y con sesenta y
#      cuatro nadie va a ir descubriendo cual es.
#    - TODOS AL MISMO NIVEL. Sin normalizar, un charles queda diez decibelios
#      por debajo de un bombo y la persona cree que el pad esta mal.
#    - NINGUNO SATURADO. Un sonido que llega a 1.0 recorta en cuanto le subes
#      la ganancia, que es lo primero que se hace.
#    - QUE CUBRAN LA BANDA. Cuatro bancos de graves no son cuatro bancos.
#    - QUE ACABEN EN CERO. Una muestra que termina en un valor distinto de
#      cero da un CLICK en cada golpe: el fallo que mas se oye y menos se ve.
#
#  Y el brillo se mide por CRUCES POR CERO y no con una transformada corta.
#  El primer intento uso una DFT de 2048 puntos sumando solo las 63 primeras
#  bandas - o sea que no veia por encima de 1.36 kHz - y declaro que los
#  sesenta y cuatro sonidos estaban entre 116 y 859 Hz, con un charles de 8000
#  entre ellos. Una medida que no llega a donde vive lo que mide no es una
#  medida.
#
#      python3 Tests/kits.py
# ============================================================================
import glob, math, os, shutil, struct, subprocess, sys, wave

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
TMP  = os.path.join (ROOT, "build", ".kits-test")

EXPECTED = 64
MIN_PEAK = 0.50      # ninguno mudo ni escondido
MAX_PEAK = 0.98      # margen antes de recortar
LEVEL_SPREAD = 0.10  # entre el mas alto y el mas bajo
MIN_BRIGHT = 3000.0  # tiene que haber agudos de verdad
MAX_EDGE   = 0.02    # ultimo valor de la muestra: casi cero


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def load (path):
    w = wave.open (path, "rb")
    n, sw, ch = w.getnframes(), w.getsampwidth(), w.getnchannels()
    raw = w.readframes (n); w.close()
    out = []
    for i in range (0, len (raw), sw * ch):
        b = raw[i:i + sw]
        if len (b) < sw: break
        out.append (int.from_bytes (b, "little", signed=True) / float (1 << (8 * sw - 1)))
    return out


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")

    shutil.rmtree (TMP, ignore_errors=True)
    os.makedirs (TMP, exist_ok=True)

    #  Casa limpia: sin sesion, la app carga la fabrica y la escribe.
    env = dict (os.environ, HOME=TMP, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    subprocess.run ([APP], env=env, capture_output=True, timeout=180)

    files = sorted (glob.glob (os.path.join (TMP, "Music", "ZATI", ".sesion", "samples", "*.wav")))
    if len (files) != EXPECTED:
        print ("FALLA  la fabrica dejo %d sonidos y tienen que ser %d" % (len (files), EXPECTED))
        shutil.rmtree (TMP, ignore_errors=True)
        return 1

    bad, rows = [], []
    for f in files:
        x = load (f)
        name = os.path.basename (f)
        if not x:
            bad.append ("%s vacio" % name); continue

        peak = max (abs (v) for v in x)
        rms  = math.sqrt (sum (v * v for v in x) / len (x))
        seg  = x[:4410]
        zc   = sum (1 for i in range (1, len (seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
        bright = zc * 44100.0 / max (1, len (seg)) / 2.0
        edge = abs (x[-1])
        rows.append ((name, peak, rms, bright, len (x) / 44100.0))

        if peak < MIN_PEAK: bad.append ("%s mudo (pico %.3f)" % (name, peak))
        if peak > MAX_PEAK: bad.append ("%s al borde de recortar (pico %.3f)" % (name, peak))
        if edge > MAX_EDGE: bad.append ("%s acaba en %.3f: CLICK" % (name, edge))

    peaks = [r[1] for r in rows]
    spread = max (peaks) - min (peaks)
    brights = [r[3] for r in rows]
    durs = [r[4] for r in rows]

    if spread > LEVEL_SPREAD:
        bad.append ("los niveles bailan %.3f entre el mas alto y el mas bajo" % spread)
    if max (brights) < MIN_BRIGHT:
        bad.append ("no hay agudos: el mas brillante son %.0f Hz" % max (brights))

    print ("%-22s %d sonidos" % ("fabrica", len (rows)))
    print ("%-22s %.3f a %.3f  (margen %.3f)" % ("pico", min (peaks), max (peaks), spread))
    print ("%-22s %.0f Hz a %.0f Hz" % ("brillo", min (brights), max (brights)))
    print ("%-22s %.3f s a %.2f s" % ("duracion", min (durs), max (durs)))
    for label, lo, hi in (("graves", 0, 700), ("medios", 700, 3000), ("agudos", 3000, 1e9)):
        print ("%-22s %d" % ("  " + label, sum (1 for b in brights if lo <= b < hi)))

    shutil.rmtree (TMP, ignore_errors=True)
    print()
    if bad:
        for b in bad: print ("FALLA  " + b)
        return 1
    print ("los %d sonidos de fabrica pasan" % len (rows))
    return 0


if __name__ == "__main__":
    sys.exit (main())

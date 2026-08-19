#!/usr/bin/env python3
# ============================================================================
#  LOS SONIDOS DE FABRICA QUE VIENEN DE UNA MAQUINA DE VERDAD.
#
#  Kits.h sintetiza los sesenta y cuatro y eso sigue siendo verdad para la
#  mitad de ellos: TEXTURA y TONOS no existen en ningun banco de percusion, asi
#  que se quedan sintetizados. Los otros dos bancos si:
#
#    A  ACUSTICA  bateria muestreada de verdad (Akai MPC2000 + Yamaha RY-30)
#    B  MAQUINA   las dieciseis voces de una Roland TR-808
#
#  Una 808 sintetizada nunca va a ganarle a una 808. Lo que se sintetiza es lo
#  que no se puede muestrear porque nadie lo ha grabado - un riser, un impacto,
#  un colchon - y ahi la sintesis no es un sucedaneo sino la unica forma.
#
#  ESTE SCRIPT NO CORRE EN LA COMPILACION. Los .flac que produce estan en el
#  repositorio; esto esta aqui para que se sepa DE DONDE sale cada uno y para
#  poder rehacerlos si cambia el criterio. Los bancos de origen no estan en el
#  repositorio y no tienen por que estar.
#
#      python3 Tools/fabrica.py <carpeta-con-los-bancos>
#
#  Lo que hace con cada uno, y por que:
#
#  - A MONO. El motor lee las muestras de fabrica en mono -Voice reparte a los
#    dos canales- y un estereo aqui es el doble de APK para un canal que nadie
#    lee. Se suman los dos, no se coge el izquierdo: coger uno tira la mitad
#    del platillo.
#  - A 48 kHz, que es a lo que abre el aparato en casi cualquier movil. A 44.1
#    cada golpe pasaria por el remuestreador de Voice - interpolacion sobre un
#    transitorio, que es donde peor se porta.
#  - SIN EL SILENCIO DE DELANTE. Un WAV de fabrica trae a veces cinco
#    milisegundos de nada antes del golpe, y cinco milisegundos son cinco
#    milisegundos de latencia REGALADOS despues de todo lo que cuesta
#    quitarlos en el motor.
#  - Y SIN LA COLA MUDA de detras, que es APK que no suena.
#  - EN FLAC Y NO EN WAV: es sin perdidas -un transitorio de caja no admite un
#    codec con perdidas al 100% de zoom- y ocupa poco mas de la mitad. Y no en
#    OGG por lo mismo: lo que se pierde en un codec con perdidas es
#    exactamente el ataque, que es lo unico que tiene un golpe de bateria.
#
#  Lo que NO hace: igualar el volumen. Eso lo hace Kits::render al cargar, con
#  la misma curva K y el mismo objetivo que los sintetizados - si se hiciera
#  aqui, los muestreados y los sintetizados se igualarian por caminos
#  distintos y sonarian a volumenes distintos.
# ============================================================================
import os, subprocess, sys, wave, struct

#  QUE SONIDO ES CADA UNO. Elegido por nombre donde el banco lo dice y
#  comprobado con la medida: duracion, centro espectral y reparto grave/medio/
#  agudo de cada candidato.
#
#  Y donde no hay un candidato claro, NO SE PONE NINGUNO: el hueco se queda
#  sintetizado. Un cencerro que en realidad es una castanuela es peor que un
#  cencerro sintetico, porque el sintetico al menos es lo que dice ser.
MAPA = [
    # (destino,        banco,                 fichero)
    ("a01_kick",   "Akai_MPC2000",          "Kick_F.wav"),
    ("a02_snare",  "Akai_MPC2000",          "St_Ambsn7.wav"),
    ("a03_hat",    "Akai_MPC2000",          "HH_Thin.wav"),
    ("a04_open",   "Akai_MPC2000",          "HH_Thin_Op.wav"),
    ("a05_rim",    "Akai_MPC2000",          "P_Sn_Rim.wav"),
    ("a06_tomlo",  "Akai_MPC2000",          "Nr_Tom_L.wav"),
    ("a07_tommi",  "Akai_MPC2000",          "Nr_Tom_M.wav"),
    ("a08_tomhi",  "Akai_MPC2000",          "Nr_Tom_H.wav"),
    ("a09_clap",   "Akai_MPC2000",          "F_Clap_1.wav"),
    ("a10_ride",   "Akai_MPC2000",          "Thin_Ride.wav"),
    ("a11_crash",  "Akai_MPC2000",          "Crash_1.wav"),
    ("a12_shake",  "Yamaha_RY30",           "Titt Shaker.wav"),
    ("a13_conga",  "Akai_MPC2000",          "Houc_Tom_Sa.wav"),
    #  a14 COWBEL se queda sintetizado: lo mas parecido en estos bancos es una
    #  castanuela, y un cencerro que es otra cosa es peor que uno sintetico.
    ("a15_tamb",   "Yamaha_RY30",           "House Tamb.wav"),
    ("a16_splash", "Akai_MPC2000",          "Nr_Splash.wav"),

    #  LAS DIECISEIS VOCES DE LA 808, que son exactamente dieciseis y caben
    #  clavadas en un banco. El nombre del fichero es <voz><tono><caida>: se
    #  coge el punto medio de los mandos, que es como suena la maquina cuando
    #  la enciendes.
    ("b01_bd",     "Roland_TR808_hifi_set", "BD5050.WAV"),
    ("b02_sd",     "Roland_TR808_hifi_set", "SD2550.WAV"),
    ("b03_ch",     "Roland_TR808_hifi_set", "CH.WAV"),
    ("b04_oh",     "Roland_TR808_hifi_set", "OH50.WAV"),
    ("b05_rs",     "Roland_TR808_hifi_set", "RS.WAV"),
    ("b06_cp",     "Roland_TR808_hifi_set", "CP.WAV"),
    ("b07_lt",     "Roland_TR808_hifi_set", "LT50.WAV"),
    ("b08_mt",     "Roland_TR808_hifi_set", "MT50.WAV"),
    ("b09_ht",     "Roland_TR808_hifi_set", "HT50.WAV"),
    ("b10_lc",     "Roland_TR808_hifi_set", "LC50.WAV"),
    ("b11_mc",     "Roland_TR808_hifi_set", "MC50.WAV"),
    ("b12_hc",     "Roland_TR808_hifi_set", "HC50.WAV"),
    ("b13_cy",     "Roland_TR808_hifi_set", "CY5050.WAV"),
    ("b14_cb",     "Roland_TR808_hifi_set", "CB.WAV"),
    ("b15_cl",     "Roland_TR808_hifi_set", "CL.WAV"),
    ("b16_ma",     "Roland_TR808_hifi_set", "MA.WAV"),
]

RATE   = 48000
MAXSEC = 3.0
#  -60 dBFS relativo al pico. Mas alto se come el ataque de un platillo, que
#  empieza suave; mas bajo no quita nada porque el ruido de suelo de una cinta
#  de los ochenta ya esta por encima.
UMBRAL = 10.0 ** (-60.0 / 20.0)
#  Un milisegundo de guarda ANTES del primer cruce del umbral: cortar justo en
#  el es cortar el primer flanco, que es un click y ademas el ataque.
GUARDA = int (RATE * 0.001)


def busca (raiz, banco, fichero):
    for dirpath, _, files in os.walk (os.path.join (raiz, banco)):
        for f in files:
            if f.lower() == fichero.lower():
                return os.path.join (dirpath, f)
    return None


def a_mono_48k (src, dst):
    #  ffmpeg y no un remuestreador escrito aqui: swr hace un sinc con ventana
    #  de verdad, y un remuestreador casero sobre un transitorio es como se
    #  metio el alias que Kits.h tardo una version en quitar.
    subprocess.run (["ffmpeg", "-y", "-v", "error", "-i", src,
                     "-ac", "1", "-ar", str (RATE), "-sample_fmt", "s16", dst],
                    check=True)


def recorta (path):
    w = wave.open (path, "rb")
    n = w.getnframes()
    raw = w.readframes (n); w.close()
    x = list (struct.unpack ("<%dh" % (len (raw) // 2), raw[: (len (raw) // 2) * 2]))
    if not x: return 0, 0
    pico = max (abs (v) for v in x) or 1
    lim = pico * UMBRAL
    ini = next ((i for i, v in enumerate (x) if abs (v) > lim), 0)
    fin = next ((i for i in range (len (x) - 1, -1, -1) if abs (x[i]) > lim), len (x) - 1)
    ini = max (0, ini - GUARDA)
    fin = min (len (x), fin + GUARDA + 1, ini + int (RATE * MAXSEC))
    y = x[ini:fin]
    w = wave.open (path, "wb")
    w.setnchannels (1); w.setsampwidth (2); w.setframerate (RATE)
    w.writeframes (struct.pack ("<%dh" % len (y), *y)); w.close()
    return len (x), len (y)


def main():
    if len (sys.argv) < 2:
        print (__doc__ or "uso: python3 Tools/fabrica.py <carpeta-con-los-bancos>")
        return 1
    raiz = sys.argv[1]
    salida = os.path.join (os.path.dirname (os.path.dirname (os.path.abspath (__file__))),
                           "Source", "kits")
    os.makedirs (salida, exist_ok=True)

    total = 0
    faltan = []
    for destino, banco, fichero in MAPA:
        src = busca (raiz, banco, fichero)
        if src is None:
            faltan.append ("%s: no esta %s/%s" % (destino, banco, fichero)); continue
        tmp = os.path.join (salida, destino + ".wav")
        a_mono_48k (src, tmp)
        antes, despues = recorta (tmp)
        flac = os.path.join (salida, destino + ".flac")
        subprocess.run (["ffmpeg", "-y", "-v", "error", "-i", tmp,
                         "-compression_level", "12", flac], check=True)
        os.remove (tmp)
        tam = os.path.getsize (flac)
        total += tam
        print ("%-12s %-24s %5.2f s -> %5.2f s   %6d B" %
               (destino, fichero[:24], antes / RATE, despues / RATE, tam))

    print ()
    for f in faltan: print ("FALTA  " + f)
    print ("%d ficheros, %.2f MB en total" % (len (MAPA) - len (faltan), total / 1e6))
    return 1 if faltan else 0


sys.exit (main())

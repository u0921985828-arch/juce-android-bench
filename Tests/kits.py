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

#  SE MIDE LA SONORIDAD, NO EL PICO. Y esta prueba nacio midiendo el pico, que
#  es exactamente por que no vio el fallo: los sesenta y cuatro salian a 0.890
#  clavado - "margen 0.000", un sobresaliente - y sonaban a volumenes
#  completamente distintos. Un charles dura 40 ms y un bombo 400; al mismo pico
#  el bombo mete diez veces mas energia al oido.
#
#  La ponderacion es la misma idea que la curva K de la norma de sonoridad: un
#  paso alto de cabeza, porque el oido casi no cuenta 40 Hz, y una repisa por
#  encima de 2 kHz, porque cuenta de mas. No es la norma entera - no hace falta
#  para comparar sesenta y cuatro sonidos entre si - pero si es lo que separa
#  "mismo pico" de "misma sonoridad".
MAX_LOUD_SPREAD_DB = 6.0   # entre el mas y el menos sonoro
#  EL PICO YA NO DICE SI ALGO SUENA. Con la sonoridad igualada, el pico es una
#  CONSECUENCIA: la curva K atenua 55 Hz y realza 3 kHz, asi que un bombo
#  necesita mucha mas amplitud que un timbre para llegar al mismo volumen al
#  oido. Medido, ese es todo el rango entre 0.07 y 0.69, y los tres primeros no
#  estan mudos - suenan igual de fuerte que los demas. Quien dice si algo suena
#  es la sonoridad; el pico solo dice si queda margen.
MIN_PEAK = 0.03            # solo caza el silencio de verdad
MIN_LOUD = 0.030           # y esto es lo que dice si SUENA
MAX_PEAK = 0.80            # techo: cuatro pads a la vez sin llegar al master
MIN_BRIGHT = 3000.0        # tiene que haber agudos de verdad
MAX_EDGE   = 0.02          # ultimo valor de la muestra: casi cero
MAX_START  = 0.02          # y el PRIMERO: un flanco de entrada es un click

#  Y QUE NO SEAN EL MISMO SONIDO DOS VECES.
#
#  Esta es la comprobacion que faltaba y la que encontro el fallo gordo. Todo
#  lo de arriba mira cada sonido POR SEPARADO, asi que sesenta y cuatro copias
#  del mismo bombo pasarian las cinco con sobresaliente. Y algo muy parecido
#  era lo que habia: la cabecera de Kits.h promete "cada banco una maquina
#  distinta" y el banco A y el B eran el mismo kit con los numeros movidos -
#  SNARE y SD 808, HAT y CH 808, OPEN y OH 808, RIDE y CYM 808 -, mas seis
#  sonidos (SHAKE, MARACA, TAMB, HISS, STATIC, SCRAPE) que eran literalmente el
#  mismo generador: ruido por un filtro.
#
#  El primer intento comparo los espectros con un COSENO sobre bandas
#  normalizadas y no sirvio: dos vectores no negativos y repartidos dan siempre
#  un coseno altisimo, asi que una caja y un siseo salian a 0.995 y no habia
#  forma de separar "parecido" de "identico". En decibelios y con distancia
#  euclidea el mismo par se separa: la mediana de los 2016 pares es 27 dB, o
#  sea que la escala tiene sitio de sobra para decir que dos cosas se parecen.
#
#  Y el descriptor lleva ESPECTRO Y ENVOLVENTE, que hacen falta los dos: solo
#  con espectro un charles cerrado y uno abierto son identicos, y solo con
#  envolvente lo es cualquier par de golpes secos.
MIN_PAIR_DB = 4.0          # dos sonidos por debajo de esto son el mismo sonido
BANDS   = 28               # bandas logaritmicas de 30 Hz a 20 kHz
NFFT    = 16384            # 341 ms: el golpe entero y potencia de dos
ENVBINS = 8                # tramos de tiempo, tambien logaritmicos
FLOOR   = -60.0            # por debajo de esto ya es silencio y no forma


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def biquad (x, b, a):
    """Un biquado directo I. Los coeficientes se escriben desde la norma, no se
    copian del C++: si los dos lados leyeran el mismo sitio, la prueba diria
    que si a cualquier cosa."""
    b0, b1, b2 = b; a1, a2 = a
    y = []; x1 = x2 = y1 = y2 = 0.0
    for v in x:
        o = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1 = x1, v; y2, y1 = y1, o
        y.append (o)
    return y


def loudness (x):
    """PONDERADA CON LA CURVA K DE BS.1770, y sobre la VENTANA DE 400 ms MAS
    SONORA - no sobre el fichero entero.

    Las dos cosas nacieron de un fallo. La primera version media el PICO: los
    sesenta y cuatro salian a 0.890 clavado, "margen 0.000", un sobresaliente,
    y sonaban a volumenes completamente distintos. La segunda pondero a mano
    con dos filtros de un polo que no eran los del C++, asi que los dos lados
    discrepaban varios decibelios y la prueba acusaba al codigo de un desajuste
    que era suyo.

    Y medir el fichero ENTERO castiga a lo largo y disperso: un vinilo de tres
    segundos con cuatro chasquidos tiene una energia media ridicula, asi que se
    le subia el volumen hasta que los chasquidos pegaban. Lo que se compara es
    como suena EL GOLPE.

    Coeficientes de la norma para 48 kHz, que es a lo que se genera aqui."""
    SHELF_B = (1.53512485958697, -2.69169618940638, 1.19839281085285)
    SHELF_A = (-1.69065929318241, 0.73248077421585)
    HP_B    = (1.0, -2.0, 1.0)
    HP_A    = (-1.99004745483398, 0.99007225036621)

    k = biquad (biquad (x, SHELF_B, SHELF_A), HP_B, HP_A)
    win = min (len (k), int (48000 * 0.400))
    if win <= 0: return 0.0
    run = sum (v * v for v in k[:win]); best = run
    for n in range (win, len (k)):
        run += k[n] * k[n] - k[n - win] * k[n - win]
        if run > best: best = run
    return math.sqrt (best / win)


def fft (re, im):
    """FFT de radio 2, en el sitio. Escrita aqui y no importada de ninguna
    parte por lo mismo que los coeficientes del biquado: el banco no puede
    depender de un paquete que puede no estar en la maquina que lo corre."""
    n = len (re)
    j = 0
    for i in range (1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit; bit >>= 1
        j |= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    largo = 2
    while largo <= n:
        ang = -2.0 * math.pi / largo
        wr, wi = math.cos (ang), math.sin (ang)
        for i in range (0, n, largo):
            cr, ci = 1.0, 0.0
            for k in range (i, i + largo // 2):
                m = k + largo // 2
                tr = re[m] * cr - im[m] * ci
                ti = re[m] * ci + im[m] * cr
                re[m] = re[k] - tr; im[m] = im[k] - ti
                re[k] += tr;        im[k] += ti
                cr, ci = cr * wr - ci * wi, cr * wi + ci * wr
        largo <<= 1


def descriptor (x):
    """Espectro en bandas y envolvente en tramos, los dos en decibelios
    relativos a su propio maximo. Relativos y no absolutos porque el nivel ya
    lo iguala la sonoridad: aqui lo que se compara es la FORMA."""
    y = list (x[:NFFT])
    y += [0.0] * (NFFT - len (y))
    #  Ventana de Hann: sin ella el corte a los 341 ms mete un flanco y su
    #  chorro de armonicos se reparte por todas las bandas, que es justo lo que
    #  hace que dos sonidos distintos se parezcan.
    re = [y[i] * (0.5 - 0.5 * math.cos (2.0 * math.pi * i / (NFFT - 1))) for i in range (NFFT)]
    im = [0.0] * NFFT
    fft (re, im)

    edges = [30.0 * (20000.0 / 30.0) ** (i / float (BANDS)) for i in range (BANDS + 1)]
    pot = [1e-20] * BANDS
    for k in range (NFFT // 2):
        f = k * 48000.0 / NFFT
        if f < edges[0] or f >= edges[-1]: continue
        b = int (BANDS * math.log (f / edges[0]) / math.log (edges[-1] / edges[0]))
        b = min (BANDS - 1, max (0, b))
        pot[b] += re[k] * re[k] + im[k] * im[k]
    sp = [10.0 * math.log10 (v) for v in pot]
    top = max (sp)
    sp = [max (FLOOR, v - top) for v in sp]

    cortes = [0] + [int (48000 * 3.0 * (2 ** i) / 128.0) for i in range (ENVBINS)]
    ev = []
    for i in range (ENVBINS):
        a, b = cortes[i], min (cortes[i + 1], len (x))
        ev.append (math.sqrt (sum (v * v for v in x[a:b]) / (b - a)) if b > a else 0.0)
    ev = [20.0 * math.log10 (v + 1e-9) for v in ev]
    top = max (ev)
    ev = [max (FLOOR, v - top) for v in ev]
    return sp + ev


def distancia (a, b):
    """Decibelios medios de diferencia entre dos descriptores."""
    return math.sqrt (sum ((p - q) ** 2 for p, q in zip (a, b)) / len (a))


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

    bad, rows, descs = [], [], []
    for f in files:
        x = load (f)
        name = os.path.basename (f)
        if not x:
            bad.append ("%s vacio" % name); continue
        descs.append ((name, descriptor (x)))

        peak = max (abs (v) for v in x)
        loud = loudness (x)
        seg  = x[:4800]
        zc   = sum (1 for i in range (1, len (seg)) if (seg[i - 1] < 0) != (seg[i] < 0))
        bright = zc * 48000.0 / max (1, len (seg)) / 2.0
        rows.append ((name, peak, loud, bright, len (x) / 48000.0))

        if peak < MIN_PEAK: bad.append ("%s vacio (pico %.3f)" % (name, peak))
        if loud < MIN_LOUD: bad.append ("%s no suena (sonoridad %.4f)" % (name, loud))
        if peak > MAX_PEAK: bad.append ("%s sin margen (pico %.3f)" % (name, peak))
        if abs (x[-1]) > MAX_EDGE:  bad.append ("%s acaba en %.3f: CLICK" % (name, abs (x[-1])))
        if abs (x[0])  > MAX_START: bad.append ("%s empieza en %.3f: CLICK" % (name, abs (x[0])))

    peaks   = [r[1] for r in rows]
    louds   = [r[2] for r in rows if r[2] > 1e-9]
    brights = [r[3] for r in rows]
    durs    = [r[4] for r in rows]

    spread_db = 20.0 * math.log10 (max (louds) / min (louds)) if louds else 0.0
    if spread_db > MAX_LOUD_SPREAD_DB:
        bad.append ("la sonoridad baila %.1f dB entre el mas y el menos sonoro" % spread_db)
    if max (brights) < MIN_BRIGHT:
        bad.append ("no hay agudos: el mas brillante son %.0f Hz" % max (brights))

    #  Y LOS PARES. 2016 comparaciones, y se ensenan siempre las cinco mas
    #  cercanas aunque pasen: un banco que se acerca al limite ano tras ano se
    #  ve venir aqui y no el dia que falla.
    pares = []
    for i in range (len (descs)):
        for j in range (i + 1, len (descs)):
            pares.append ((distancia (descs[i][1], descs[j][1]), descs[i][0], descs[j][0]))
    pares.sort()
    for d, a, b in pares:
        if d < MIN_PAIR_DB:
            bad.append ("%s y %s son el mismo sonido (%.2f dB de diferencia)" % (a, b, d))

    print ("%-22s %d sonidos" % ("fabrica", len (rows)))
    print ("%-22s %.1f dB entre el mas y el menos sonoro" % ("sonoridad", spread_db))
    print ("%-22s %.3f a %.3f" % ("pico", min (peaks), max (peaks)))
    print ("%-22s %.0f Hz a %.0f Hz" % ("brillo", min (brights), max (brights)))
    print ("%-22s %.3f s a %.2f s" % ("duracion", min (durs), max (durs)))
    for label, lo, hi in (("graves", 0, 700), ("medios", 700, 3000), ("agudos", 3000, 1e9)):
        print ("%-22s %d" % ("  " + label, sum (1 for b in brights if lo <= b < hi)))
    if pares:
        medio = pares[len (pares) // 2][0]
        print ("%-22s %.1f dB de mediana, %.2f el par mas parecido" % ("distancia", medio, pares[0][0]))
        for d, a, b in pares[:5]:
            print ("  %-20s %s  %.2f dB" % (a.replace (".wav", ""), b.replace (".wav", ""), d))

    shutil.rmtree (TMP, ignore_errors=True)
    print()
    if bad:
        for b in bad: print ("FALLA  " + b)
        return 1
    print ("los %d sonidos de fabrica pasan" % len (rows))
    return 0


if __name__ == "__main__":
    sys.exit (main())

#!/usr/bin/env python3
"""QUE ES CADA SONIDO DE FABRICA, contra lo que su receta DICE que es.

Da las cifras con las que se afina una receta: la afinacion de verdad, cuanto
tarda en caer, donde tiene el centro de masa espectral y cuanto de lo que suena
es ruido y cuanto tono.

Y las pone AL LADO de lo que la fila de Kits.h declara, porque el fallo que se
busca no es "este numero es raro" sino "este numero no es el que la receta
dice". Un bombo cuya receta dice 55 Hz y da 90 no suena a lo que se escribio.

Sirvio para lo mas caro que ha hecho: cuando se decidio sacar del binario las
treinta y una grabaciones sin licencia, esta tabla dio el T60 de cada maquina y
con el se ajustaron los `decay` de sus recetas -x3.36 de desvio mediano a
x1.00-. Las grabaciones ya no estan; la tabla sigue siendo con lo que se afina.

La tabla se lee del propio Kits.h con una expresion regular en vez de pedirle
a la app que la vuelque: es texto plano y una fila de C++ no cambia de forma.

Los WAV salen de la sesion que la app escribe al abrir por primera vez, que es
la MISMA fuente que usa kits.py - asi los dos hablan de los mismos bytes.
"""
import glob, math, os, re, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import load, display_alive, fft   # la MISMA FFT, no otra a mano

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
SR   = 48000.0


def tabla():
    """Las 64 filas de Kits.h: slot -> (nombre, forma, hz, decay).

    EL SLOT SE CUENTA SOBRE TODAS LAS FILAS, no sobre las que casan.

    La primera version enumeraba con una expresion que exigia los NUEVE campos,
    y treinta y tres filas de la tabla estan escritas en forma corta -{ "COWBEL",
    metal, 2.4f, 0.28f, 2400.0f, 3.0f }, sin juego, brillo ni muestra, que una
    inicializacion de agregado deja a su defecto-. Esas no casaban, el contador
    no avanzaba, y CADA SLOT a partir de la primera quedaba corrido: 31 filas
    vistas de 64 reales. COWBEL es la catorceava del banco A, asi que los slots
    0..12 salian bien y de ahi en adelante cada grabacion se comparaba con la
    receta de su vecina - el banco B entero desplazado uno.

    No fallaba: daba numeros. Se descubrio porque un RS con `decay` 0.01 -o sea
    un fichero de 0.05 s- seguia midiendo 1.8 s de T60, que es imposible. Una
    tabla que empareja mal no dice que no, dice otra cosa.
    """
    txt = open (os.path.join (ROOT, "Source", "Kits.h"), encoding="utf8").read()
    #  Toda fila es { "NOMBRE", forma, ...campos... }. Se enumeran TODAS y los
    #  campos se parten despues, que es lo que separa contar de leer.
    fila   = re.compile (r'\{\s*"([^"]+)"\s*,\s*(\w+)\s*,([^{}]*?)\}')
    numero = re.compile (r'(-?[\d.]+)f')
    out = {}
    for i, m in enumerate (fila.finditer (txt)):
        cuerpo = m.group (3)
        n = [float (x) for x in numero.findall (cuerpo)]
        if len (n) < 4: continue
        out[i] = dict (nombre=m.group (1).strip(), forma=m.group (2),
                       hz=n[0], decay=n[1], p1=n[2], p2=n[3])
    return out


def fabrica():
    casa = tempfile.mkdtemp (prefix="zati-anal-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    subprocess.run ([APP], env=env, capture_output=True, timeout=300)
    fich = sorted (glob.glob (os.path.join (casa, "Music", "ZATI", ".sesion", "samples", "*.wav")))
    out = [load (f) for f in fich]
    shutil.rmtree (casa, ignore_errors=True)
    return out


def afinacion (x):
    """La fundamental por AUTOCORRELACION sobre la senal FILTRADA.

    Un golpe de membrana tiene la afinacion cayendo y sus modos altos suelen
    pegar mas fuerte que el primero, asi que el pico del espectro devuelve un
    parcial y no la nota. La autocorrelacion busca el periodo que se repite,
    que es lo que el oido llama la afinacion.

    Y SE FILTRA ANTES. El primer intento corria sobre la senal cruda y encima
    decimaba de cuatro en cuatro para ir mas rapido: en un bombo eso se
    engancha al CLICK del ataque en vez de a la nota, y devolvia 1200 Hz - que
    ademas es el tope del rango de busqueda, o sea la medida diciendo "no he
    encontrado nada" con la voz de un resultado. Un paso bajo de un polo a
    1500 Hz deja la nota y se lleva el golpe de baqueta.

    Sobre los primeros 120 ms, que es donde un golpe tiene tono; despues es
    cola. Rango 30..1200 Hz: por debajo de 30 no hay nota y por encima de 1200
    ya no es una membrana. Si el mejor periodo cae en un extremo del rango se
    devuelve 0 en vez del extremo: un tope no es una medida.
    """
    n = min (len (x), int (0.120 * SR))
    if n < 800: return 0.0

    #  Paso bajo de un polo a 1500 Hz, ida y vuelta para no correr la fase.
    a = 1.0 - math.exp (-2.0 * math.pi * 1500.0 / SR)
    z = 0.0; f1 = []
    for v in x[:n]:
        z += a * (v - z); f1.append (z)
    z = 0.0; seg = [0.0] * n
    for i in range (n - 1, -1, -1):
        z += a * (f1[i] - z); seg[i] = z

    med = sum (seg) / n
    seg = [v - med for v in seg]
    if sum (v * v for v in seg) < 1e-12: return 0.0

    lo, hi = int (SR / 1200.0), min (n - 1, int (SR / 30.0))
    mejor, mejorR = 0, 0.0
    for lag in range (lo, hi):
        r = sum (seg[i] * seg[i + lag] for i in range (n - lag))
        r /= float (n - lag)
        if r > mejorR: mejorR, mejor = r, lag
    if mejor <= lo or mejor >= hi - 1: return 0.0   # pegado al tope: no es una medida
    return SR / mejor


def caida (x, db):
    """Segundos hasta que la envolvente baja `db` desde su maximo."""
    win = int (0.005 * SR)
    env, i = [], 0
    while i + win <= len (x):
        env.append (math.sqrt (sum (v * v for v in x[i:i+win]) / win))
        i += win
    if not env: return 0.0
    top = max (env)
    if top <= 1e-9: return 0.0
    umbral = top * (10.0 ** (-db / 20.0))
    pico = env.index (top)
    for j in range (pico, len (env)):
        if env[j] < umbral:
            return (j - pico) * win / SR
    return len (env) * win / SR


def centroide_y_ruido (x):
    """Centro de masa espectral en Hz, y la planitud -0 tono puro, 1 ruido-.

    La planitud es la media geometrica partida por la aritmetica, que es la
    forma estandar y la que separa "esto es un seno" de "esto es un siseo" sin
    tener que elegir un umbral.

    Con la FFT de kits.py y no con una DFT escrita aqui. El primer intento la
    escribio a mano y para que no tardara una eternidad recorria las muestras
    DE DOS EN DOS - o sea a media frecuencia de muestreo, que aliasa todo: el
    centroide salia clavado cerca de 12000 Hz en media tabla, que es SR/4, y
    eso no es un sonido, es el sintoma.
    """
    N = 4096
    y = list (x[:N]); y += [0.0] * (N - len (y))
    y = [v * (0.5 - 0.5 * math.cos (2.0 * math.pi * i / (N - 1))) for i, v in enumerate (y)]
    re, im = y, [0.0] * N
    fft (re, im)

    sw = sfw = 0.0
    pot = []
    for k in range (1, N // 2):
        p = re[k] * re[k] + im[k] * im[k] + 1e-20
        f = k * SR / N
        pot.append (p); sw += p * f; sfw += p
    if sfw <= 0: return 0.0, 0.0
    cen = sw / sfw
    lg = sum (math.log (p) for p in pot) / len (pot)
    plana = math.exp (lg) / (sum (pot) / len (pot))
    return cen, plana


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    filtro = [a.lower() for a in sys.argv[1:]]
    t = tabla()
    if not t: sys.exit ("no se pudo leer la tabla de Kits.h")
    son = fabrica()

    print ("%-9s %-6s %8s %8s   %7s %7s   %8s %7s %6s" %
           ("sonido", "forma", "hz dice", "hz ES", "dec dice", "dec ES", "centroide", "-20dB", "ruido"))
    for slot in sorted (t):
        if slot >= len (son): continue
        r = t[slot]
        if filtro and not any (f in r["nombre"].lower() for f in filtro): continue
        x = son[slot]
        hz = afinacion (x)
        d60 = caida (x, 60.0)
        d20 = caida (x, 20.0)
        cen, plana = centroide_y_ruido (x)
        print ("%-9s %-6s %8.1f %8.1f   %7.3f %7.3f   %8.0f %7.3f %6.2f" %
               (r["nombre"], r["forma"], r["hz"], hz, r["decay"], d60, cen, d20, plana))
    return 0


if __name__ == "__main__":
    sys.exit (main())

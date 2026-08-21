#!/usr/bin/env python3
"""CUANTO SE PARECE CADA RECETA A LA MAQUINA DE LA QUE SALIO.

La mitad de la fabrica son grabaciones de aparatos reales -una TR-808, un
MPC2000, un RY-30- y eso es contenido de terceros dentro de una app de pago.
La salida limpia no es quitarlos: es SINTETIZARLOS. Un timbre no es una
grabacion, y lo que esta protegido es el fichero, no que un bombo tenga la
afinacion cayendo.

Y las recetas YA ESTAN ESCRITAS. Cada una de las 31 filas con muestra lleva
sus parametros de sintesis - son lo que suena si el recurso no esta, que en
escritorio pasa de verdad-. Lo que nadie ha medido nunca es CUANTO se parece
cada una a su grabacion, y sin ese numero afinar la sintesis es adivinar de
oido.

Se mide con el MISMO descriptor que Tests/kits.py usa para decir si dos
sonidos de la fabrica son el mismo sonido dos veces: espectro en bandas
logaritmicas mas envolvente en tramos, los dos en decibelios relativos a su
propio maximo. Se importa de alli en vez de copiarlo: una regla escrita dos
veces son dos reglas.

La escala ya tiene referencia, que es lo que la hace legible: en kits.py la
MEDIANA de los 2016 pares de la fabrica esta en 28 dB -dos sonidos que no
tienen nada que ver- y el liston de "son el mismo sonido" esta en 4. O sea que
una copia que mida por debajo de esos 4 dB contra su original es
indistinguible por esta medida.

    python3 Tests/clon.py            todas
    python3 Tests/clon.py kick snare  solo las que casen con esos nombres

ESTE BANCO CERTIFICA, NO GUIA. Escrito aqui porque es la clase de cosa que se
vuelve a intentar.

La medida SATURA. Medido con dos senos que decaen: 124 Hz contra 201 dan 16.57
dB y 124 contra 1000 dan 16.25 - o sea MENOS estando diez veces mas lejos. En
cuanto dos sonidos dejan de compartir bandas, la distancia se planta cerca de
16 y deja de decir cuanto peor.

La consecuencia practica costo un experimento: se corrigio la afinacion de
MC 808 de 124 a 201 Hz -la medida de su grabacion- y su distancia se quedo en
16.95, la MISMA hasta la centesima. No fue que el cambio no llegara a correr
-el binario era nuevo y `skin` usa r.hz en cuatro sitios-: fue que los dos
valores estan al otro lado de la saturacion.

Asi que afinar 31 recetas contra este numero es imposible por construccion: no
baja cuando te acercas. El gradiente tiene que salir de Tests/analiza.py, que
mide MAGNITUDES de la grabacion -afinacion, caida, centroide, planitud- y esas
si se pueden perseguir una a una. Este banco es la PUERTA del final: por debajo
de 4 dB kits.py llamaria a los dos el mismo sonido, y ahi es cuando los FLAC
pueden salir del APK.
"""
import glob, os, re, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import descriptor, distancia, load, display_alive   # la MISMA regla

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  El liston: por debajo de esto, kits.py diria que son el mismo sonido.
CERCA = 4.0
#  Y por encima de esto no se parece en nada util. La mediana de dos sonidos
#  distintos de la fabrica esta en 28.
LEJOS = 12.0


def slots_con_grabacion():
    """De Source/kits/a07_tommi.flac -> (slot 6, "a07 tommi")."""
    d = {}
    for f in sorted (glob.glob (os.path.join (ROOT, "Source", "kits", "*.flac"))):
        m = re.match (r"^([ab])(\d{2})_(.+)\.flac$", os.path.basename (f))
        if not m: continue
        banco = 0 if m.group (1) == "a" else 1
        n = int (m.group (2))
        d[banco * 16 + n - 1] = "%s%s %s" % (m.group (1), m.group (2), m.group (3))
    return d


def fabrica (sinMuestra):
    """Los 64 WAV que la app deja escritos la primera vez que abre."""
    casa = tempfile.mkdtemp (prefix="zati-clon-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    if sinMuestra: env["ZATI_SIN_MUESTRA"] = "1"
    subprocess.run ([APP], env=env, capture_output=True, timeout=300)
    fich = sorted (glob.glob (os.path.join (casa, "Music", "ZATI", ".sesion", "samples", "*.wav")))
    salida = [load (f) for f in fich]
    shutil.rmtree (casa, ignore_errors=True)
    return salida


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")

    filtro = [a.lower() for a in sys.argv[1:]]
    conGrab = slots_con_grabacion()
    if not conGrab:
        sys.exit ("no hay grabaciones en Source/kits")

    grab = fabrica (False)
    sint = fabrica (True)
    if len (grab) != len (sint) or not grab:
        sys.exit ("las dos corridas no dejaron el mismo numero de sonidos: %d y %d"
                  % (len (grab), len (sint)))

    #  LA COMPROBACION QUE HACE QUE ESTO NO MIENTA. Si el interruptor no
    #  funcionara, las dos corridas serian identicas y TODAS las distancias
    #  saldrian cero - un sobresaliente perfecto que significa lo contrario de
    #  lo que parece. Los slots SIN grabacion tienen que salir a cero, y los
    #  que la tienen, distintos de cero.
    sinGrab = [i for i in range (len (grab)) if i not in conGrab]
    control = max ((distancia (descriptor (grab[i]), descriptor (sint[i]))
                    for i in sinGrab), default=0.0)
    if control > 0.01:
        print ("FALLA  un sonido SIN grabacion cambia entre las dos corridas (%.2f dB): "
               "la sintesis no es determinista y nada de lo de abajo vale" % control)
        return 1

    filas = []
    for slot, nombre in sorted (conGrab.items()):
        if slot >= len (grab): continue
        if filtro and not any (f in nombre.lower() for f in filtro): continue
        d = distancia (descriptor (grab[slot]), descriptor (sint[slot]))
        filas.append ((d, slot, nombre))

    if not filas:
        sys.exit ("ningun sonido casa con %s" % " ".join (filtro))

    if control <= 0.01 and all (d < 0.01 for d, _, _ in filas):
        print ("FALLA  todas las distancias son cero: ZATI_SIN_MUESTRA no hizo nada")
        return 1

    filas.sort (reverse=True)
    print ("%-14s %8s   %s" % ("sonido", "dB", "que tan lejos de su maquina"))
    for d, slot, nombre in filas:
        marca = "IGUAL" if d < CERCA else ("cerca" if d < LEJOS else "LEJOS")
        print ("%-14s %8.2f   %s" % (nombre, d, marca))

    ds = sorted (d for d, _, _ in filas)
    med = ds[len (ds) // 2]
    print ("\n%d sonidos   peor %.2f dB (%s)   mediana %.2f   mejor %.2f"
           % (len (filas), ds[-1], filas[0][2], med, ds[0]))
    print ("referencia: bajo %.0f dB kits.py los llamaria el mismo sonido; "
           "dos sonidos distintos de la fabrica median 28" % CERCA)
    return 0


if __name__ == "__main__":
    sys.exit (main())

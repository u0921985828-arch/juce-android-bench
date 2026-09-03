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
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

SEGUNDOS = int (sys.argv[1]) if len (sys.argv) > 1 else 8

#  Las fichas que se abren solas y se quedan abiertas. Las que piden un
#  dialogo del sistema quedan fuera: en un banco sin pantalla no vuelven.
#  «eq» ES LA CARA CON EL ANALIZADOR PUESTO, y hace falta que este: la curva
#  vive en el PLATO -o sea en la cara, no en una ficha- y solo se enseña con el
#  EQ en una ranura y con el foco. Sin esta entrada, las dos FFT de 1024 por
#  tick no las corre nadie en el unico banco que mide por RELOJ, que es
#  exactamente publicar un numero sin mirarlo.
FICHAS = ["", "eq", "pads", "sec", "song", "mix", "set", "proj", "midi", "gest",
          "xy", "rack", "chop", "browse"]

#  EL TOPE, en VENTANAS y no en llamadas. Un arranque pinta el fondo una vez y
#  una ficha que se abre puede pedir otro; a partir de ahi, con nadie tocando
#  nada, no hay motivo para ninguno mas.
#
#  Y en ventanas porque contar LLAMADAS no separa un fotograma de una banda -la
#  leccion que la tabla de abajo ya tenia y esta no-: medido, una ficha abierta
#  sobre una maquina con dos efectos encendidos entra 370 veces en
#  `MainComponent::paint` con un recorte de 2622 pixeles, que son **0.007**
#  ventanas. Tres ventanas dejan sitio al asentamiento de los margenes del
#  sistema sin dejar pasar un repintado periodico: a treinta por segundo, ocho
#  segundos son doscientas cuarenta.
TOPE = 3

#  Y LA MISMA PREGUNTA CON LA MAQUINA SONANDO, que es el estado que este banco
#  no medi­a.
#
#  En un escritorio no hay tarjeta de sonido, asi que el motor no renderiza, el
#  osciloscopio ve silencio y `SpectrumDisplay::setSamples` se rinde en su
#  guardia: la pieza mas grande de la cara -su propio comentario la llama «el
#  coste en reposo mas grande de la app»- no se repintaba NUNCA aqui. En un
#  telefono con algo sonando se repinta treinta veces por segundo, se vea o no.
#  `ZATI_SONANDO` bombea los bloques que le tocan a cada tick y el camino
#  entero -motor, colas, osciloscopio, VU, destellos- corre de verdad.
#
#  Y AQUI SE CUENTAN PIXELES Y NO LLAMADAS, que es lo que separa las dos cosas
#  que estaban pasando a la vez: el cabezal de la rejilla de pasos entra en
#  `MainComponent::paint` treinta veces por segundo y esta BIEN, porque pide su
#  banda; la ficha que tapa la cara entraba las mismas veces y pedia la
#  ventana. Contando llamadas los dos salen igual. Medido en MEZCLA, en 8 s:
#  42 346 644 pixeles antes -112 fotogramas equivalentes, o sea pintando la
#  maquina entera con una tarjeta delante- y 380 660 despues, uno.
#
#  Y LA CIFRA QUE VIAJA PASA A SER POR CUADRO, que es lo que la separacion del
#  reloj y el dibujo obliga a decir en voz alta.
#
#  Hasta esta tanda el dibujo colgaba de UN temporizador a `uiIntervalMs`, o sea
#  entre 10 y 30 cuadros por segundo segun la gama, y ocho segundos eran ~133
#  cuadros CLAVADOS: un total y un por-cuadro eran el mismo numero con otra
#  escala. Desde que el repintado cuelga del vblank la cadencia la pone el panel
#  -60, 90 o 120- o el `ZATI_VBLANK` de aqui abajo, asi que **los pixeles
#  totales suben por definicion** y el numero de la tanda anterior deja de ser
#  comparable. Lo que no puede empeorar es el coste POR CUADRO, y eso es lo que
#  se juzga.
#
#  Y la cadencia se fija, que si no esta medida depende de la maquina: X11
#  entrega vblanks a la frecuencia que declare el display —100 Hz en Xvfb, que
#  no declara ninguna— asi que sin `ZATI_VBLANK` el mismo binario daria un
#  numero distinto en el portatil y en el runner. Es la misma razon por la que
#  esta prueba corre SOLA.
VBLANK_HZ = 60

#  El tope sale de la POBLACION y no de un numero redondo. Medido a 60 Hz con
#  la maquina sonando: la ficha que mas pinta CON RAZON es SEC con **0.007**
#  ventanas por cuadro —el cabezal de la rejilla de pasos, que pide su banda—
#  y una ficha que repinta lo que no se ve mide como la CARA, **0.50**. Cinco
#  centesimas estan siete veces por encima de la primera y diez por debajo de
#  la segunda.
TOPE_SONANDO = 0.05


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (ficha, sonando=False):
    #  CON SU PROPIO HOME, que es lo que le faltaba. Sin el, la app restaura la
    #  SESION que dejara la ultima prueba que corriera - y `Tests/ranuras.py` y
    #  `Tests/dinamica.py` dejan DOS efectos encendidos, cuyas lamparas laten a
    #  proposito («nothing lit means nothing repainted»). Un veredicto que
    #  depende de lo que dejara el de antes no es un veredicto: es el mismo
    #  fallo que `Tests/session.py` acaba de pagar en `proyecto()`.
    casa = tempfile.mkdtemp (prefix="zati-cpu-")
    env = dict (os.environ)
    env.update ({"HOME": casa,
                 "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                 "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_DEMO": "1", "ZATI_OPEN": ficha, "ZATI_SPIN": str (SEGUNDOS),
                 "ZATI_VBLANK": str (VBLANK_HZ)})
    if sonando: env["ZATI_SONANDO"] = "1"
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=SEGUNDOS + 90).stdout
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)

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
    fuera = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith ('{') and '"cabezal"' in linea:
            try:
                d = json.loads (linea)
                fuera[str (d["cabezal"])] = d
            except Exception:
                pass
    return fuera or None


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada:", APP);  return 1
    if not display_alive():
        print ("no hay DISPLAY vivo");  return 1

    print ("ficha    entradas  ventanas   CPU ms / %d s" % SEGUNDOS)
    malas = []
    for f in FICHAS:
        r = corre (f)
        if r is None:
            print ("%-8s  --   no contesto" % (f or "(cara)"));  malas.append (f or "(cara)")
            continue
        #  EN PIXELES Y NO EN LLAMADAS, que es la leccion que la tabla de
        #  abajo aprendio y esta no: «contar llamadas no separa un fotograma de
        #  una banda». Medido, una ficha abierta sobre una maquina con DOS
        #  efectos encendidos entra 370 veces en `MainComponent::paint` con un
        #  recorte de **2622 pixeles** -las dos lamparas, que laten a
        #  proposito- y eso salia como «se repinta sola» al lado de una que
        #  repinta la ventana entera. Son 0.007 ventanas: dos ordenes de
        #  magnitud.
        fondos = int (r.get ("fondos", -1))
        vent = max (1, int (r.get ("ventana", 1)))
        equi = int (r.get ("pixeles", 0)) / float (vent)
        print ("%-8s %6d  %7.2f %10.0f%s"
               % (f or "(cara)", fondos, equi, r.get ("cpu_ms", 0.0),
                  "   <-- se repinta sola" if equi > TOPE else ""))
        if equi > TOPE:
            malas.append (f or "(cara)")

    print()
    print ("y con la maquina SONANDO, en ventanas repintadas POR CUADRO (%d Hz)"
           % VBLANK_HZ)
    print ("ficha    por cuadro  cuadros   CPU ms")
    for f in FICHAS:
        r = corre (f, sonando=True)
        if r is None:
            print ("%-8s  --   no contesto" % (f or "(cara)"));  malas.append (f or "(cara) sonando")
            continue
        vent = max (1, int (r.get ("ventana", 1)))
        cuad = max (1, int (r.get ("cuadros", 1)))
        equi = int (r.get ("pixeles", 0)) / float (vent) / float (cuad)
        #  La cara no se juzga: ahi el cristal y los destellos de los pads SE
        #  VEN, asi que repintarlos es el trabajo. Se imprime porque es el
        #  techo contra el que se leen las demas, y porque el dia que suba hay
        #  que enterarse.
        #
        #  Y «eq» es la CARA CON EL EQ PUESTO y no una ficha: la curva vive en
        #  el plato, o sea en la cara. Se lee CONTRA la cara y no contra el tope
        #  de las fichas, que es lo que hace de esta fila una medida y no un
        #  rojo: lo que separa a las dos es lo que cuesta el analizador —dos FFT
        #  de 1024 por cuadro, la mancha de entrada y la linea de salida—.
        cara = (f in ("", "eq"))
        mal  = (not cara) and equi > TOPE_SONANDO
        print ("%-8s %8.3f %8d %9.0f%s" % (f or "(cara)", equi, cuad, r.get ("cpu_ms", 0.0),
                                        "   (se ve: no se juzga)" if cara else
                                        ("   <-- pinta lo que no se ve" if mal else "")))
        if mal:
            malas.append ((f or "(cara)") + " sonando")
    #  Y QUE EL VBLANK ESTE VIVO, que es la comprobacion sin la cual todo lo
    #  de arriba puede salir verde con el dibujo cayendose al reloj.
    #
    #  `pintaCuadro` cuelga del vblank y tiene una red debajo: si el peer no
    #  entrega ninguno, lo llama el temporizador de mantenimiento a la cadencia
    #  del SUELO. Esa red es correcta —«ningun camino puede dejar la app en
    #  silencio»— y hace que un vblank roto no falle nada: la app seguiria
    #  pintando, a 16.7 cuadros por segundo, con las veintiocho pruebas en
    #  verde. Es *una linea que imprime OK* aplicada a la tanda entera.
    #
    #  Asi que se corre UNA sin `ZATI_VBLANK` -o sea con la cadencia del panel
    #  de verdad- y se exige que este muy por encima del suelo. Medido en Xvfb,
    #  que no declara frecuencia y por eso JUCE cae a su valor de reserva de
    #  100 Hz: 592 cuadros en 6 s, contra los ~100 que daria el suelo.
    print()
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_DEMO": "1", "ZATI_OPEN": "mix", "ZATI_SPIN": str (SEGUNDOS)})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=SEGUNDOS + 90).stdout
        panel = next ((json.loads (l.strip()) for l in out.splitlines()
                       if l.strip().startswith ('{') and '"spin"' in l), None)
    except Exception:
        panel = None
    if panel is None:
        print ("la cadencia del panel no contesto");  malas.append ("vblank")
    else:
        hz = int (panel.get ("cuadros", 0)) / float (SEGUNDOS)
        #  El suelo es `DeviceTier::relojMs`, que en el peor aparato son 10
        #  cuadros por segundo y en el mejor 30. Con el vblank vivo esto tiene
        #  que dar la frecuencia del panel; treinta y cinco deja pasar
        #  cualquier panel real y no deja pasar la red.
        print ("la cadencia del panel: %.1f cuadros por segundo%s"
               % (hz, "   <-- el vblank no llega, dibuja el reloj" if hz < 35.0 else ""))
        if hz < 35.0: malas.append ("el vblank no llega")

    print()
    cs = cabezal()
    if not cs:
        print ("el cabezal no contesto");  malas.append ("cabezal")
    else:
        #  Los dos cabezales que se repintan acotados: la rejilla de pasos y la
        #  ONDA. En la onda ademas va un rastro que crece detras, asi que la
        #  union de las dos marcas tiene que cubrirlo: no se juzga leyendo el
        #  codigo. Roto a proposito estrechando la marca: 3105 pixeles fuera.
        for quien, nombre in (("1", "rejilla de pasos"), ("onda", "onda del pad")):
            c = cs.get (quien)
            if c is None:
                print ("el cabezal de %s no contesto" % nombre)
                malas.append ("cabezal " + nombre)
                continue
            fuera = int (c.get ("fuera_de_la_zona", -1))
            print ("cabezal (%s): %d pixeles comparados, %d fuera de la zona repintada"
                   % (nombre, int (c.get ("pixeles", 0)), fuera))
            if fuera != 0:
                malas.append ("el cabezal de %s deja rastro" % nombre)

    print()
    if malas:
        print ("FALLA:", ", ".join (malas))
        return 1
    print ("ninguna ficha se repinta sola (tope %d fotogramas en %d s)" % (TOPE, SEGUNDOS))
    return 0


if __name__ == "__main__":
    sys.exit (main())

#!/usr/bin/env python3
# ============================================================================
#  EL BANCO DE TOMAS: DONDE CAE LO QUE SE GRABA.
#
#  Llego del telefono con estas palabras: «cuando grabo se sobrescribe el pad
#  uno». Era exacto y era total, no intermitente. `grabaAlArreglo` pedia
#  `firstEmptyPad()` y, si no habia ninguno, caia en el pad ELEGIDO -justo
#  debajo del comentario que promete que una toma nueva no pisa un sonido que
#  la persona haya puesto-. Y en una maquina de fabrica esa caida es la unica
#  rama que corre: los sesenta y cuatro pads vienen llenos, asi que
#  `firstEmptyPad()` devuelve -1 SIEMPRE. `Tests/carga.py` ya lo medi­a por el
#  otro lado -64 pads con sonido en una instalacion limpia- y nadie habia
#  cruzado las dos cifras.
#
#  Y hay una segunda mitad que no se ve: un clip apunta al PAD, asi que todas
#  las tomas caian en el mismo hueco y la segunda REESCRIBIA el audio de la
#  primera - el clip ya puesto en la linea de tiempo pasaba a sonar otra cosa.
#  Dos tomas al arreglo no se podian hacer.
#
#  NINGUNA DE LAS TRECE REGLAS DE `expo.py` PUEDE VERLO. Es un fallo de INDICE
#  y de estado: una toma que cae en el pad equivocado se maqueta perfecta -no
#  solapa, no se sale, no corta un rotulo, no mide cero y esta traducida-. Es
#  la familia de los cinco fallos del compas del piano.
#
#  SE MIDE POR LA TAPA -`songRecBtn.onClick`- y no llamando a `grabaAlArreglo`
#  por dentro, que es justo donde no vive ninguno de estos fallos.
#
#      python3 Tests/tomas.py
# ============================================================================
import json, os, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_BIN") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


#  SIN PANTALLA NO SE MIDE NADA, y hay que DECIRLO: sin esta guarda un Xvfb
#  muerto sale como «la app no publico la linea», o sea como un fallo de la
#  app, y se pierde media tarde buscando un cambio que no era.
def display_alive():
    d = PANTALLA
    try:
        return subprocess.run (["xdpyinfo", "-display", d],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre():
    casa = tempfile.mkdtemp (prefix="zati-tomas-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_TOMAS="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=240)
    except subprocess.TimeoutExpired:
        return {}
    filas = {}
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "tomas" in r: filas[r["tomas"]] = r
    return filas


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    if not display_alive():
        print ("FALLA  no hay DISPLAY vivo: esta prueba maqueta la app")
        return 1

    f = corre()
    if any (k not in f for k in (1, 2, 3, 5, 8, 9)):
        print ("FALLA  la app no publico las lineas de las tomas (%s)" % sorted (f))
        return 1

    malas = []
    a, b, c = f[1], f[2], f[3]

    #  1. tomas#lista: LAS TOMAS VAN A LA LISTA DE LA CANCION Y NINGUN PAD CAMBIA.
    #     Dos tomas grabadas por la tapa, dos en la lista, dos clips que suenan
    #     de ellas, y los sesenta y cuatro pads con el mismo audio que antes.
    print ("lista    grabadas %d   en la lista %d   clips de toma %d (con audio %d)   pads cambiados %d"
           % (a["grabadas"], a["lista"], a["clips_toma"], a["con_audio"], a["pads_cambiados"]))
    print ("pad 01   antes %-10s ahora %s" % (a["pad01antes"], a["pad01"]))
    if a["grabadas"] != 2 or a["lista"] != 2:
        malas.append ("dos tomas por la tapa y la lista tiene %d" % a["lista"])
    if a["con_audio"] != 2:
        malas.append ("los clips de toma no suenan de su toma: %d de 2" % a["con_audio"])
    if a["pads_cambiados"] != 0 or a["pad01"] != a["pad01antes"]:
        malas.append ("grabar al arreglo toco %d pads" % a["pads_cambiados"])

    #  2. tomas#capas: LA NUEVA ENCIMA, LA VIEJA DEBAJO, Y MUTE ELIGE.
    e5 = f[5]
    print ("pila     debajo %d   suena T%d   tras MUTE suena T%d"
           % (a["debajo"], a["arriba"] + 1, e5["suena_despues"] + 1))
    if a["debajo"] != 1 or a["arriba"] != 1:
        malas.append ("la segunda toma no queda encima de la primera (debajo %d, suena %d)"
                      % (a["debajo"], a["arriba"]))
    if e5["suena_despues"] != 0 or e5["debajo"] != 1:
        malas.append ("MUTE sobre la pila no pasa a la otra toma (suena %d)" % e5["suena_despues"])

    #  3. tomas#onda: LA PISTA VE LA TOMA MIENTRAS ENTRA, a su nivel.
    print ("onda     picos en la pista %d   pico %.3f" % (a["picos_vistos"], a["pico"]))
    if a["picos_vistos"] <= 0 or a["pico"] <= 0.0:
        malas.append ("la pista no dibuja la toma mientras se graba (%d picos)" % a["picos_vistos"])

    #  4. LAS TOMAS VUELVEN: WAV de sesion, arbol y deshacer.
    e9 = f[9]
    print ("vuelven  wav de sesion %d   clips con audio %d   deshacer deja %d de %d"
           % (e9["wav_sesion"], e9["vuelven"], e9["tras_deshacer"], e9["lista"]))
    if e9["wav_sesion"] != 1:
        malas.append ("la sesion no escribe el WAV de la toma")
    if e9["vuelven"] != 2:
        malas.append ("al volver de la sesion los clips de toma no tienen audio (%d de 2)" % e9["vuelven"])
    print ("proyecto clips de toma con audio tras guardar y abrir %d" % e9.get ("proyecto", -1))
    if e9.get ("proyecto") != 2:
        malas.append ("al abrir el proyecto los clips de toma no tienen audio (%s de 2)" % e9.get ("proyecto"))
    if e9["tras_deshacer"] != e9["lista"] - 1:
        malas.append ("deshacer no quita la ultima toma (%d de %d)" % (e9["tras_deshacer"], e9["lista"]))

    #  5. tomas#vacia: CON CUENTA ATRAS, UN CUADRO NO PARA LA TOMA QUE ESPERA.
    #     Era el «No se grabo nada» del telefono: el cuadro veia el motor sin
    #     grabar -la cuenta, o el micro abriendo- y cerraba la toma vacia.
    #     Y parar en la cuenta dice por que.
    e8 = f[8]
    print ("cuenta   armada %d   sigue tras un cuadro %d   «%s»"
           % (e8["en_cuenta"], e8["sigue"], e8["estado"]))
    if e8["en_cuenta"] != 1:
        malas.append ("la toma no quedo armada en la cuenta atras")
    if e8["sigue"] != 1:
        malas.append ("un cuadro con la cuenta en marcha cierra la toma vacia: «No se grabo nada»")
    if "cuenta" not in e8["estado"].lower():
        malas.append ("parar en la cuenta no dice por que no hay toma: «%s»" % e8["estado"])

    #  6. CON EL BANCO DE TOMAS LLENO SE GRABA AL ARREGLO IGUAL: no ocupa pads.
    print ("lleno    arranco %d" % b["grabando"])
    if b["grabando"] != 1:
        malas.append ("con el banco de tomas lleno, grabar al arreglo no arranca")

    #  4. Y LA MARCA VUELVE DEL FICHERO. Sin ella la regla cambiaria entre el
    #     primer arranque y el segundo -la sesion devuelve los sesenta y cuatro
    #     desde sus WAV- y la toma siguiente se comeria la anterior. Se borra a
    #     mano entre guardar y abrir: si al volver sigue puesta no es que se
    #     haya guardado, es que nadie la quito.
    print ("marca    de fabrica antes %d   vuelven %d" % (c["antes"], c["vuelven"]))
    if c["vuelven"] != c["antes"]:
        malas.append ("la marca de fabrica no vuelve del fichero: %d de %d"
                      % (c["vuelven"], c["antes"]))

    #  7. estabilidad#8: REMUESTREAR tras el micro espera a que el audio reabra.
    g = f.get (7, {})
    print ("remuestreo  armado con la apertura en vuelo: %s (se espera 0)" % g.get ("armado_en_vuelo"))
    if g.get ("armado_en_vuelo") != 0:
        malas.append ("REMUESTREAR arma la toma con la reapertura del audio en vuelo: la vacia")

    #  5. Un remuestreo no se pierde al ir al fondo ni al llenarse (estabilidad#6 y #7).
    d = f.get (4, {})
    print ("remuestreo  al fondo %s   buffer lleno %s   (activo:pad cambio, se espera 0:1)"
           % (d.get ("fondo"), d.get ("lleno")))
    if d.get ("fondo") != "0:1":
        malas.append ("ir al fondo remuestreando deja %s: la toma se pierde al volver"
                      % d.get ("fondo"))
    if d.get ("lleno") != "0:1":
        malas.append ("con el buffer lleno el remuestreo queda en %s: la tapa sigue en "
                      "PARAR y el pad sin la toma" % d.get ("lleno"))

    #  6. La toma del micro no se toca ya publicada (estabilidad#2).
    e = f.get (6, {})
    print ("micro    toma compartida con el motor al recortarla: %s (se espera 0)" % e.get ("compartida"))
    if e.get ("compartida") != 0:
        malas.append ("la toma del micro se recorta despues de publicarla al hilo de audio (%s)" % e.get ("compartida"))

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("las tomas van a la cancion, se apilan, se ven entrar y vuelven")
    return 0


sys.exit (main())

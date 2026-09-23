#!/usr/bin/env python3
# ============================================================================
#  QUE LA APP NO VAYA A TIRONES.
#
#  «Cincuenta y tres cuadros por segundo» y «va a tirones» son compatibles, y
#  esa es la razon de que esta regla exista aparte de `cpu.py`: una app que
#  pinta 100, 50, 100, 50 da la misma MEDIA que una que pinta 75 siempre, y
#  solo la primera se ve dar saltos. La media no puede verlo por construccion.
#
#  Lo que se mide es la VARIACION, en dos numeros que no se pueden falsear
#  promediando:
#
#   · `cadencia_cambios` — cuantas veces la app ha cambiado de cadencia. Cada
#     cambio es un hueco que dura el doble o la mitad que el anterior, y el ojo
#     los ve uno a uno. ESTE es el numero del tiron.
#   · `huecos` — el reparto de los huecos entre cuadros pintados, en cubos de
#     4 ms. Una cadencia sana es UNA columna; una a tirones son dos separadas.
#
#  EL FALLO QUE CAZA ESTABA PUESTO. `cuadroSaltar` se decidia con
#  `floor (coste / periodo)`, recalculado desde cero en cada cuadro y sin
#  memoria. Con un cuadro que cuesta cerca de un periodo -el caso NORMAL en un
#  telefono, que es para lo que se elige el presupuesto- ese `floor` cae a un
#  lado y a otro con el temblor de la media movil. Medido con ZATI_LASTRE=16 a
#  60 Hz: **56 cambios de cadencia en 10 segundos**, con el histograma partido
#  en dos columnas -179 huecos de 16-20 ms y 137 de 32-36-. La media decia 41
#  cuadros por segundo y lo que se veia era un tiron cada dos decimas.
#
#  Y LA CONDICION ES UNA ENTRADA Y NO UNA ESPERANZA. En el escritorio la cara
#  sale a 0.3 ms de fotograma contra los 8.3 de un panel a 120 Hz, o sea que la
#  rama que decide la cadencia no se ejecutaba ni una vez en las corridas del
#  banco. `ZATI_LASTRE=ms` pone el cuadro a costar lo que cuesta en un telefono
#  -con temblor de +-18 %, que es lo que hace cambiar de opinion a un `floor`
#  sin memoria- y `ZATI_VBLANK=hz` pone el panel. Misma figura que ZATI_INSETS.
#
#  Roto a proposito volviendo a `floor (coste / periodo)` sin memoria: salen 56
#  cambios donde ahora sale 1.
#
#      python3 Tests/fluidez.py
# ============================================================================
import json, os, subprocess, sys

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

SEGUNDOS = 10

#  EL LISTON, y sale de lo que el ojo distingue y no de un numero redondo. La
#  app tiene un guardia de medio segundo entre cambios de cadencia (ver
#  `kCadenciaEsperaMs`), asi que el peor caso LEGAL en diez segundos son veinte
#  cambios. Se pide cinco: por encima de ahi la banda muerta no esta haciendo
#  su trabajo y lo unico que sujeta la cadencia es el reloj, que es el ultimo
#  recurso y no el mecanismo.
TOPE_CAMBIOS = 5

#  Los casos. Cada uno es un telefono: el panel que trae y lo que le cuesta un
#  cuadro. El de 16 ms a 60 Hz es EL caso -un cuadro que cuesta justo un
#  periodo- y es el que daba 56.
CASOS = [
    ("panel de 60 Hz, cara barata",        60,  0),
    ("panel de 90 Hz, cara barata",        90,  0),
    ("panel de 120 Hz, cara barata",      120,  0),
    ("panel de 60 Hz, cuadro de 16 ms",    60, 16),
    ("panel de 60 Hz, cuadro de 24 ms",    60, 24),
    ("panel de 60 Hz, cuadro de 40 ms",    60, 40),
]


def display_alive():
    try:
        return subprocess.run (["xdpyinfo", "-display", PANTALLA],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre (hz, lastre):
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_SONANDO": "1", "ZATI_SPIN": str (SEGUNDOS),
                 "ZATI_VBLANK": str (hz), "ZATI_LASTRE": str (lastre)})
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    for l in out.splitlines():
        if '"spin"' in l:
            return json.loads (l)
    return None


def columnas (huecos):
    #  CUANTAS COLUMNAS TIENE EL HISTOGRAMA, que es el dibujo del tiron sin
    #  pasar por la media. Un cubo cuenta como columna si se lleva al menos el
    #  10 % de los huecos; dos columnas separadas por un hueco vacio es la app
    #  alternando entre dos cadencias.
    total = sum (huecos)
    if total <= 0:
        return 0
    gordos = [i for i, n in enumerate (huecos) if n >= 0.10 * total]
    if not gordos:
        return 0
    grupos, ant = 1, gordos[0]
    for i in gordos[1:]:
        if i > ant + 1:
            grupos += 1
        ant = i
    return grupos


def main():
    if not os.path.exists (APP):
        print ("no hay app compilada en %s" % APP);  return 1
    if not display_alive():
        print ("no hay pantalla en %s" % PANTALLA);  return 1

    malas = []
    print ("caso                             cuadros/vblanks  cambios  columnas")
    for nombre, hz, lastre in CASOS:
        r = corre (hz, lastre)
        if r is None:
            print ("%-32s  no contesto" % nombre);  malas.append (nombre);  continue

        cambios = int (r.get ("cadencia_cambios", 999))
        cols    = columnas (r.get ("huecos", []))
        cuad    = int (r.get ("cuadros", 0))
        vbl     = int (r.get ("vblanks", 0))

        mal = []
        if cambios > TOPE_CAMBIOS:
            mal.append ("cadencia inestable")
        #  Y LA SEGUNDA MITAD: una sola columna. Dos columnas con pocos cambios
        #  serian la app partida en dos cadencias de forma estable, que se ve
        #  igual de mal y que la cuenta de cambios no ve.
        if cols > 1:
            mal.append ("histograma partido en %d" % cols)
        #  Y QUE NO SE RINDA: pintar dos cuadros en diez segundos tambien da
        #  cero cambios y una sola columna.
        if cuad < 10 * SEGUNDOS:
            mal.append ("solo %d cuadros" % cuad)

        print ("%-32s  %5d / %5d      %3d      %2d%s"
               % (nombre, cuad, vbl, cambios, cols,
                  "   <-- " + ", ".join (mal) if mal else ""))
        if mal:
            malas.append (nombre)

    print ()
    if malas:
        print ("VEREDICTO: FALLA (%s)" % ", ".join (malas));  return 1
    print ("VEREDICTO: OK");  return 0


if __name__ == "__main__":
    sys.exit (main())

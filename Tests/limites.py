#!/usr/bin/env python3
"""ZATI — los limites de las 55 fichas, medidos en la TINTA.

POR QUE EXISTE, y es una frase: las once reglas de geometria de esta casa le
preguntan al MAQUETADO, y el maquetado es el que miente.

`TARJETA` compara el pedido con el tope. `SOBRA` compara lo pedido con lo
colocado. `MARCO`, `AJENO` y `VACIO` comparan rectangulos con rectangulos.
`FILA` compara el rectangulo que se dio con la union de lo que se puso. Las
cinco leen el MISMO numero, asi que cuando el maquetado miente de forma
coherente -pide 500 y coloca 490- ninguna se entera: le preguntan al que miente.

Eso es literalmente lo que paso. La pagina SONIDO de la ficha del pad dejaba
diez pixeles de aire muerto DENTRO del panel de abajo; 412x915/es/pads se mide
1980 veces por barrido; y salio verde hasta que lo enseno la foto de un
telefono. Y la regla que acabo cazandolo -`SOBRA`- esta escrita A MANO y para
UNA ficha: la publica `padSheet` porque alguien la escribio ahi, y las otras
cincuenta y cuatro no publican nada.

Esto pregunta lo mismo a las cincuenta y cinco sin escribirlo cincuenta y cinco
veces, y lo pregunta a la TINTA: `UiAudit::mideTinta` busca, dentro de cada
tarjeta y de cada panel, el RECTANGULO VACIO MAS GRANDE de la imagen que la app
acaba de pintar. Es el segundo numero que esta casa exige y que `SOBRA` no
puede dar: uno dice lo que el maquetado cree que coloco y el otro dice donde
acabo la pintura. Los dos se pueden enganar por separado; los dos a la vez, no.

Y ADEMAS DIBUJA. `ZATI_CONTORNOS=1` pinta encima el contorno de cada tarjeta,
panel, fila y control, y en rojo macizo el hueco encontrado. Con `--laminas` se
sacan las cincuenta y cinco fichas en tres pantallas para MIRARLAS, que es lo
que un numero no hace: la ficha del pad la encontro un ojo y no una cifra.

LO QUE ESTA PRUEBA NO PUEDE VER, escrito para que nadie lo descubra otra vez:
una caja tan llena que su color mas frecuente no sea el del fondo sino el de un
control. El hueco sale entonces en cero y esa caja no dice nada - un fallo
hacia el SILENCIO, que es el peor que puede tener una prueba. Por eso la rotura
a proposito de este fichero no vale hecha en una ficha cualquiera: tiene que
salir con la cifra de la ficha del pad, que es la unica de la que ya se sabe la
respuesta.
"""
import json, os, subprocess, sys, tempfile, shutil
from collections import defaultdict
from concurrent.futures import ProcessPoolExecutor

#  La pantalla que se comprueba es la que se usa. Ver el comentario de
#  `paneles.py`: escribir `:99` aqui a mano ya costo una tarde en `cpu.py`.
from kits import PANTALLA                                          # noqa: E402
#  Y LAS DOS LISTAS SE IMPORTAN. Una ficha que `expo.py` barre y esta no seria
#  una ficha donde esto no se sabe, y escribirlas otra vez son dos listas.
from expo import SHEETS, SIZES                                     # noqa: E402

HERE = os.path.dirname (os.path.abspath (__file__))
APP  = os.path.join (HERE, "..", "build", "Zati_artefacts", "Release", "Zati")

#  Las tres que se fotografian: el movil normal, el apaisado -la orientacion
#  que nadie prueba- y el peor caso que alguien vende. Las nueve se MIDEN; solo
#  tres se dibujan, porque 55 x 9 laminas son 495 imagenes que nadie mira.
LAMINAS = ["412x915", "915x412", "280x653"]


def corre (sheet, size, lamina=None):
    casa = tempfile.mkdtemp (prefix="zati-limites-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", DISPLAY=PANTALLA)
    #  `llena` y `llena-*` no son fichas: son un ESTADO de datos, igual que
    #  `ZATI_SKIN` con la carcasa. La ficha sale de quitarle el prefijo.
    if sheet.startswith ("llena"):
        env["ZATI_LLENA"] = "1"
        env["ZATI_OPEN"]  = sheet[6:] if sheet != "llena" else ""
    elif sheet:
        env["ZATI_OPEN"] = sheet
    if lamina:
        env["ZATI_CONTORNOS"] = "1"
        env["ZATI_SHOT"] = lamina
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=300)
    except Exception:
        shutil.rmtree (casa, ignore_errors=True)
        return []
    filas = []
    for l in out.stdout.decode ("utf8", "replace").splitlines():
        l = l.strip()
        if not l.startswith ('{"tinta"'): continue
        try: filas.append (json.loads (l))
        except Exception: pass
    shutil.rmtree (casa, ignore_errors=True)
    return filas


def una (arg):
    sheet, size = arg
    return sheet, size, corre (sheet, size)


def _lamina (arg):
    sheet, size, d = arg
    corre (sheet, size, os.path.join (d, "%s-%s.png" % (sheet or "face", size)))
    return 1


def main():
    laminas = None
    if "--laminas" in sys.argv:
        laminas = sys.argv[sys.argv.index ("--laminas") + 1]
        os.makedirs (laminas, exist_ok=True)
        #  CON `--laminas` NO SE BARRE, y se dice por que: el barrido son 55 x 9
        #  = 495 arranques de app y las laminas otros 165, y juntos en la misma
        #  corrida son mas de dos horas con la maquina de cuatro nucleos que
        #  esto tiene. Son dos entregables distintos -el ranking y las imagenes-
        #  y se piden por separado. Sin la bandera se barre y se juzga, que es
        #  lo que corre el banco.
        tareas = [(sh, sz, laminas) for sh in SHEETS for sz in LAMINAS]
        with ProcessPoolExecutor (max_workers=min (8, os.cpu_count() or 4)) as ex:
            hechas = sum (ex.map (_lamina, tareas))
        print ("laminas: %d imagenes en %s" % (hechas, laminas))
        return 0

    combos = [(sh, sz) for sh, _ in [(s, 0) for s in SHEETS] for sz, _ in SIZES]
    datos = defaultdict (list)
    with ProcessPoolExecutor (max_workers=min (8, os.cpu_count() or 4)) as ex:
        for sheet, size, filas in ex.map (una, combos):
            for r in filas:
                datos[(sheet or "face", r["tinta"])].append ((size, r))

    #  EL HUECO SE DICE EN PARTE DE LA CAJA Y NO EN PIXELES. Un hueco de 200 px
    #  dentro de un panel de 210 es el panel entero vacio, y dentro de una
    #  tarjeta de tableta es la separacion normal entre dos grupos. Comparar
    #  pixeles entre pantallas de 280 y de 1280 no compara nada.
    filas = []
    for (sheet, quien), obs in datos.items():
        for size, r in obs:
            caja = r["w"] * r["h"]
            if caja <= 0: continue
            filas.append ((r["area"] / caja, r["area"], sheet, quien, size,
                           "%dx%d en %d,%d" % (r["hw"], r["hh"], r["hx"], r["hy"])))
    filas.sort (reverse=True)

    paneles = [f for f in filas if not f[3].startswith ("tarjeta")]
    tarjs   = [f for f in filas if f[3].startswith ("tarjeta")]

    print ("limites: %d fichas x %d pantallas, %d cajas medidas"
           % (len (SHEETS), len (SIZES), len (filas)))
    print ()
    print ("los veinte PANELES con mas hueco (parte de la caja que esta vacia):")
    for p in paneles[:20]:
        print ("  %5.1f%%  %-22s %-18s %-9s %s" % (p[0] * 100, p[2], p[3], p[4], p[5]))
    print ()
    print ("las diez TARJETAS con mas hueco (una tarjeta separa grupos, asi que")
    print ("su hueco es sobre todo la separacion: se imprime y no se juzga):")
    for t in tarjs[:10]:
        print ("  %5.1f%%  %-22s %-18s %-9s %s" % (t[0] * 100, t[2], t[3], t[4], t[5]))

    #  =================== ESTO NO JUZGA, Y SE DICE POR QUE ==================
    #
    #  No por falta de ganas: se intento, se midio y no salio. Queda escrito
    #  entero para que la proxima tanda no repita los dos callejones.
    #
    #  LO QUE EL BARRIDO ENSEÑA. Los primeros veinte de 1437 cajas son de
    #  CUATRO clases distintas y solo dos son un fallo:
    #
    #    · `pads/padGrupos[0]` 36.7 % -211x63 pegado al filo de abajo y al de
    #      la derecha-: ES el fallo de la tanda 9, aire muerto dentro del panel.
    #    · `midi/setGrupos[0]` y `[1]` 36.8 %, iguales en las nueve pantallas:
    #      la fila de MANDAR y la de RECIBIR llevan UNA tapa y el resto del
    #      renglon vacio, con el desplegable de abajo llegando al filo.
    #    · `paso/seqBloques[2]` 46.2 %, el MAS GRANDE de los 1437 y NO es un
    #      fallo: el hueco esta encima de la barra de SWING, y una barra
    #      horizontal tiene el carril fino por diseño. Aire legitimo.
    #    · `tour10/padGrupos[0]` 43.3 %: el panel del pad con la tarjeta del
    #      tour tapandolo. Ni fallo ni aire: otra pagina encima.
    #
    #  CALLEJON UNO, el liston en tanto por ciento. No separa esas cuatro, y no
    #  es cuestion de afinarlo: las dos que SI son fallo -36.7 y 36.8- estan POR
    #  DEBAJO de las dos que no lo son -46.2 y 43.3-, asi que NO HAY CORTE que
    #  se quede con unas y no con otras. Ponerlo igualmente seria la regla del
    #  MAXIMO de `paneles.py` otra vez: 124 hallazgos, ninguno real, retirada.
    #
    #  CALLEJON DOS, el caso sin ambiguedad. La idea era juzgar solo lo que no
    #  admite discusion -un panel VACIO DEL TODO, que es el fallo de GESTOS
    #  heredando los paneles de PROYECTOS- con el liston DERIVADO: `dw` y `dh`
    #  son el interior que `mideTinta` mide, asi que un panel sin nada dentro
    #  vale 1.000 clavado. Y no se puede ver fallar, que es lo que la descarta:
    #    · No hay en esta app una sola banda de pixeles de un color unico. Se
    #      busco a lo ancho de la pantalla entera, de y=150 a y=760, en alturas
    #      de 24, 40 y 60: CERO. El cromo -el filo de la tarjeta, los
    #      separadores de una lista, la rejilla- cruza absolutamente todo.
    #    · Se publico un panel de rotura encima del hueco entre la tarjeta y la
    #      rejilla de pads, y `UiAudit` NI LO VE: `pintaPaneles` recorta a la
    #      tarjeta, asi que un panel fuera de ella no llega a medirse.
    #    · Y el fallo historico tampoco habria dado 1.000: los paneles de
    #      PROYECTOS caian sobre la LISTA de gestos, que tiene renglones.
    #  O sea que el liston derivado es inalcanzable por construccion. Una regla
    #  que no se puede ver fallar no es una regla; esta casa lo dice de las
    #  pruebas y vale igual para esta.
    #
    #  LO QUE SI ESTA VALIDADO, y es lo que hace que esta tabla se pueda creer:
    #  la SENSIBILIDAD de la medida, con la rotura de la tanda 9 puesta otra
    #  vez. Devolviendo los diez pixeles a `wantH`, el hueco de
    #  `pads/padGrupos[0]` pasa de 211x63 a **211x73** - exactamente los diez -
    #  sin que nadie le haya escrito a esta prueba cual era la cifra. Eso es lo
    #  que ninguna de las once reglas de maquetado podia dar.
    #
    #  ASI QUE EL ENTREGABLE SON LAS LAMINAS Y EL RANKING, y el veredicto se
    #  queda para cuando haya una pregunta que la tinta sepa contestar sola.

    return 0


if __name__ == "__main__":
    sys.exit (main())

#!/usr/bin/env python3
# ============================================================================
#  LAS DOS SELECCIONES DE RANGO: la del SECUENCIADOR y la de la CANCION.
#
#  Las dos son la misma funcion en dos lienzos -arrastras una banda, salen
#  cuatro acciones, el portapapeles guarda lo de dentro en coordenadas
#  relativas- y por eso se miden en UNA corrida y no en dos: dos arranques del
#  binario para juzgar el mismo contrato cuestan el doble y miden lo mismo.
#
#  Lo que cada mitad aporta, y que ninguna captura de pantalla puede dar:
#
#   - SEC: los NUEVE campos del paso. Esta casa ya pago dos veces la cuenta de
#     copiar tres de nueve -«la velocidad no se copia», «de un acorde de tres
#     notas solo se pega una»- y la unica forma de cazarlo es escribir los
#     nueve fuera de su defecto, pegarlos EN OTRO PATRON y volver a leerlos.
#     Pegar en el mismo patron no vale: con el original debajo, un campo que no
#     viaje se encuentra ya puesto y la prueba sale verde con el fallo dentro.
#
#   - CANCION: el `offset`. Cortar un bloque de 64 pasos por el 24 tiene que
#     dejar uno de 24 con offset 0 y otro de 40 con offset 8 -que es
#     (0 + 24) % 16, el largo del patron-, o sea el trozo de atras sigue
#     sonando por donde iba. Sin `offset` los dos empezarian por el paso 0 del
#     patron, que es exactamente lo que el modelo de celdas no podia ni
#     representar y lo que esta tanda vino a arreglar.
#
#      python3 Tests/sel.py
# ============================================================================
import json, os, subprocess, sys

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def display_alive():
    try:
        return subprocess.run (["xdpyinfo", "-display", PANTALLA],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def corre():
    env = dict (os.environ, DISPLAY=PANTALLA)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_OPEN": "sec", "ZATI_SEL": "1"})
    out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                          timeout=300).stdout
    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "sel" in d:
            filas[d["sel"]] = d
    return filas


#  EL PASO CON LOS NUEVE CAMPOS FUERA DE SU DEFECTO.
#
#  [on, nota, empujon, corte, vel, roll, largo, acorde, bloqueos]. El acorde es
#  la celda cruda de 64 bits con dos notas extra puestas (+4 y +7): si viajara
#  solo la nota raiz -que es el fallo que ya se pago- este numero saldria 0 y
#  los otros ocho campos seguirian cuadrando.
#
#  Y el largo es 40 cuartos, o sea diez pasos: por encima de los 63 del uint8
#  no, pero si por encima de lo que cabe en la banda, que es lo que obliga a
#  que el campo viaje entero y no recortado al hueco.
NUEVE = [1, 5, 3, 40, 51, 4, 40, 216172782113785604, 0]

#  Y EL PASO VACIADO, que es la otra mitad de BORRAR: apagar el «suena» deja
#  detras la nota, la fuerza, el acorde y el largo del que habia, y eso no se
#  ve hasta que alguien vuelve a encender ese paso y le sale con los
#  parametros de otro. Estos son los defectos de `vaciaPaso`.
VACIO = [0, 0, 0, -1, 127, 1, 0, 0, 0]

fallos = []


def juzga(nombre, visto, quiere):
    ok = visto == quiere
    if not ok:
        fallos.append (nombre)
    print ("  %-34s %-46s %s" % (nombre, str (visto),
                                 "correcto" if ok else "MAL, se esperaba %s" % (quiere,)))


def main():
    if not display_alive():
        print ("FALLA  no hay pantalla en %s" % PANTALLA)
        return 1

    f = corre()
    faltan = [k for k in ("sec original", "sec copiado", "sec pegado", "sec borrado",
                          "cancion inicial", "cancion copiado", "cancion pegado",
                          "cancion borrado", "cancion corte") if k not in f]
    if faltan:
        print ("FALLA  la sonda no dijo: %s" % ", ".join (faltan))
        return 1

    #  EL GUARDIA DE ESCALA, Y POR QUE NO ES UN NUMERO A MANO.
    #
    #  Los pasos por compas los decide la REJILLA de la pagina abierta, asi que
    #  la primera version de esta prueba esperaba 16 -que es lo que sale sin
    #  abrir nada- y veia 64 al abrir SEC: cuatro reglas en rojo con la app
    #  perfecta. La cifra que importa no es 16 ni 64 sino que la banda de 24
    #  pasos CORTE el bloque en vez de cubrirlo, o sea que el bloque de cuatro
    #  compases mida mas de 24; y que el patron siga midiendo 16 pasos, que es
    #  de donde sale el offset 8 = 24 % 16. Las dos se comprueban, y el resto
    #  de las cifras se derivan de `pc` en vez de repetirlo.
    pc = f["cancion inicial"]["pc"]
    juzga ("la banda corta y no cubre", [4 * pc > 24, 24 % 16], [True, 8])

    print ("\nSEC: la banda se lleva los nueve campos, y a otro patron")
    juzga ("el paso escrito",        f["sec original"]["paso"], NUEVE)
    juzga ("copiados de la banda",   [f["sec copiado"]["entradas"],
                                      f["sec copiado"]["pads"],
                                      f["sec copiado"]["pasos"]], [2, 3, 8])
    #  LA REGLA FUERTE: identidad, no parecido. El paso que sale del patron 1
    #  tiene que ser el MISMO que entro en el 0, los nueve campos.
    juzga ("pegado en el patron 1",  f["sec pegado"]["paso"], NUEVE)
    juzga ("borrado dentro y fuera", [f["sec borrado"]["dentro"],
                                      f["sec borrado"]["fuera"]], [0, 1])
    juzga ("y sin cola detras",      f["sec borrado"]["resto"], VACIO)

    print ("\nCANCION: la banda recorta por sus filos y el offset se corre")
    juzga ("bloque de cuatro compases", f["cancion inicial"]["bloques"],
                                        [[0, 0, 0, 4 * pc, 0], [1, 1, 2 * pc, pc, 0]])
    #  COPIAR SE LLEVA SOLO LO SELECCIONADO: 24 y no 64, que es lo que «copiar
    #  medio patron y no el bucle entero» significa.
    juzga ("copiado, recortado a 24",   f["cancion copiado"]["bloques"],
                                        [[0, 0, 0, 24, 0]])
    juzga ("y el tamano de la banda",   [f["cancion copiado"]["carriles"],
                                         f["cancion copiado"]["pasos"]], [2, 24])
    #  PEGAR CAE DONDE SE MARCA, y el sitio marcado NO es multiplo de compas:
    #  compas 5 mas 8 pasos, que es justo lo que el modelo de celdas no podia
    #  ni escribir -una celda por compas no tiene donde guardar «y ocho pasos».
    juzga ("pegado donde se marca",     f["cancion pegado"]["bloques"],
                                        [[0, 0, 0, 4 * pc, 0], [1, 1, 2 * pc, pc, 0],
                                         [0, 0, 5 * pc + 8, 24, 0]])
    #  BORRAR PARTE POR EL FILO: lo que asoma se queda, con su offset corrido.
    #  Un BORRAR que se lleve el bloque entero deja dos en la lista y no tres.
    juzga ("borrado, parte con offset",  f["cancion borrado"]["bloques"],
                                         [[0, 0, 24, 4 * pc - 24, 8], [1, 1, 2 * pc, pc, 0],
                                          [0, 0, 5 * pc + 8, 24, 0]])
    #  CORTE ES UNA SOLA ENTRADA DE DESHACER. Si BORRAR metiera la suya,
    #  deshacer una vez dejaria el trozo copiado pero no borrado.
    juzga ("corte, una entrada de undo", f["cancion corte"]["undo"], 1)

    print ()
    if fallos:
        print ("FALLA  %d de %d: %s" % (len (fallos), 13, ", ".join (fallos)))
        return 1
    print ("las dos bandas recortan por sus filos, y lo que copian viaja entero")
    return 0


if __name__ == "__main__":
    sys.exit (main())

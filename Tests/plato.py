#!/usr/bin/env python3
# ============================================================================
#  EL PLATO QUE SE ELIGE — los tres mandos de la cara dejan de ser fijos.
#
#  Llego del telefono: «que cada efecto ya no se modifique solo con 3 knobs».
#  El motor paso de cuatro huecos por efecto a ocho y la cara no tiene alto
#  para un mando mas, asi que la personalizacion es ELEGIR que mueven los
#  tres: mantener un mando abre la ficha con todos, y tocar uno lo lleva al
#  plato. Y tocar el nombre del efecto cambia el plato a PAD: los tres van al
#  pad elegido -o a su instrumento-.
#
#  NINGUNA regla de maqueta ve nada de esto: un plato que escribe el
#  parametro del vecino se pinta perfecto. Se mide POR EL MANDO
#  -`macroCtrl2.setValue` con su aviso- y contando lo que cambio en el motor
#  entero: 32 canales x 30 tipos x 8 parametros.
#
#      python3 Tests/plato.py
# ============================================================================
import json, os, subprocess, sys, tempfile

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre():
    casa = tempfile.mkdtemp (prefix="zati-plato-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es", ZATI_PLATO="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("plato"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea del plato")
        return 1

    malas = []

    #  1. DE FABRICA, LOS DE SIEMPRE: un proyecto que no dice nada del plato
    #     toca p0 p1 p2, que es lo que tocaba antes de poder elegir.
    print ("fabrica  el plato del CMP toca p%s" % " p".join (r["fabrica"]))
    if r["fabrica"] != "012":
        malas.append ("de fabrica el plato toca %s y no 012" % r["fabrica"])

    #  2. LA FICHA SE ABRE Y LLEVA UN MANDO AL PLATO. El CMP tiene siete con
    #     mando -ocho menos el enganche, que va en su fila de SYNC-.
    print ("ficha    abierta %d con %d mandos; el segundo del plato toca ahora p%d"
           % (r["ficha"], r["en_ficha"], r["elegido"]))
    if r["ficha"] != 1:
        malas.append ("mantener el mando no abrio la ficha")
    if r["en_ficha"] != 7:
        malas.append ("la ficha del CMP ensena %d mandos y son 7" % r["en_ficha"])
    if r["elegido"] != 4:
        malas.append ("tocar el ATAQUE dejo el mando en p%d" % r["elegido"])

    #  3. Y ESE MANDO ESCRIBE SU PARAMETRO Y NINGUNO MAS, contado en el motor
    #     entero: un cruce de indices escribe al vecino.
    print ("escribe  movidos %d: el suyo %d, ajenos %d; el motor dice lo que el mando %d"
           % (r["movidos"], r["suyo"], r["ajenos"], r["mando_igual"]))
    if r["suyo"] != 1 or r["mando_igual"] != 1:
        malas.append ("el mando no llego a su parametro (suyo %d, igual %d)"
                      % (r["suyo"], r["mando_igual"]))
    if r["ajenos"] != 0:
        malas.append ("mover el mando escribio %d parametros ajenos" % r["ajenos"])

    #  4. LA ELECCION VUELVE DEL PROYECTO, borrada entre medias: sin borrarla,
    #     «vuelve» lo cumple no haberla guardado.
    print ("vuelve   borrada a p%d, del proyecto vuelve p%d" % (r["borrada"], r["vuelve"]))
    if r["borrada"] == 4:
        malas.append ("la medida no borro la eleccion antes de abrir: no prueba nada")
    if r["vuelve"] != 4:
        malas.append ("del proyecto volvio p%d y se guardo p4" % r["vuelve"])

    #  5. EN PAD, AL PAD: el primer mando es el CORTE y llega al motor, sin
    #     tocar ni un efecto.
    print ("pad      modo %d; corte mando %.2f, ficha %.2f, motor %.2f; efectos movidos %d"
           % (r["modo_pad"], r["corte_mando"], r["corte_ficha"], r["corte_motor"], r["ajenos_pad"]))
    if r["modo_pad"] != 1:
        malas.append ("el plato no paso a PAD")
    if abs (r["corte_mando"] - r["corte_ficha"]) > 0.01 or abs (r["corte_mando"] - r["corte_motor"]) > 0.01:
        malas.append ("en PAD el corte no sigue al mando (%.2f / %.2f / %.2f)"
                      % (r["corte_mando"], r["corte_ficha"], r["corte_motor"]))
    if r["ajenos_pad"] != 0:
        malas.append ("en PAD el mando movio %d parametros de efecto" % r["ajenos_pad"])

    #  6. Y SI EL PAD ES UN INSTRUMENTO, a SUS mandos: el primero es el BRILLO
    #     -el septimo de doce-.
    print ("inst     es instrumento %d; el mando va al %d de la ficha; mando %.3f, valor %.3f"
           % (r["es_inst"], r["inst_idx"], r["inst_mando"], r["inst_valor"]))
    if r["es_inst"] != 1 or r["inst_idx"] != 7:
        malas.append ("el plato de un instrumento no va a su BRILLO (inst %d, idx %d)"
                      % (r["es_inst"], r["inst_idx"]))
    if abs (r["inst_mando"] - r["inst_valor"]) > 0.001:
        malas.append ("el mando del instrumento no llega (%.3f contra %.3f)"
                      % (r["inst_mando"], r["inst_valor"]))

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("el plato se elige, escribe solo lo suyo, vuelve del proyecto, y en PAD toca el pad o su instrumento")
    return 0


if __name__ == "__main__":
    sys.exit (main())

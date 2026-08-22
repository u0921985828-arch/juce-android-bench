#!/usr/bin/env python3
# ============================================================================
#  DIECISEIS NIVELES.
#
#  Un pad toca su sonido a la fuerza que sea. Con 16 NIVELES encendido, los
#  dieciseis pads de la rejilla tocan EL MISMO sonido a dieciseis fuerzas: es
#  como se toca un bombo a mano en una MPC y es lo que separa un golpe puesto
#  de un golpe tocado.
#
#  La cuenta cabe en cuatro lineas (MainComponent::disparoDe) y por eso mismo
#  hay que medirla: lo dificil aqui no es la formula, es CUANDO se decide el
#  pad. Se captura al ENCENDER el modo y no al disparar, porque si se leyera
#  selectedPad en cada golpe, tocar el pad 5 para oirlo cambiaria el sonido que
#  los otros quince estan tocando -o sea que el modo se destruiria solo en
#  cuanto lo usaras-.
#
#  Cuatro estados y no uno, que es lo que separa "la cuenta esta bien" de "el
#  modo esta bien":
#
#      off                  cada pad el suyo, a fuerza 1
#      on                   los 32 el capturado, a 1/16..16/16
#      tras cambiar de pad   IGUAL que el anterior
#      apagado              vuelve cada pad al suyo
#
#  Las fuerzas se comprueban MONOTONAS y con los dos extremos puestos, no solo
#  "que sean distintas": dieciseis numeros distintos y desordenados dan una
#  rejilla en la que el 03 pega mas que el 11, que es exactamente lo que este
#  modo existe para que no pase.
#
#      python3 Tests/niveles.py
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  Los mismos que pone auditNiveles. Escritos aqui y no leidos de la app: una
#  respuesta que la propia app elige no comprueba nada.
CAPTURADO = 21          # selectedPad al encender
DESPUES   = 3           # selectedPad despues, que NO tiene que mover nada
PADS      = 32          # dos bancos, para ver que la cuenta se repite por banco
PORBANCO  = 16


def corre():
    casa = tempfile.mkdtemp (prefix="zati-niveles-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": "pads", "ZATI_NIVELES": "1"})
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "niveles" in d:
            filas[d["niveles"]] = d
    return filas


def main():
    if not os.path.exists (APP):
        print ("FALLA  no hay binario: %s" % APP)
        return 1

    filas = corre()
    faltan = [q for q in ("off", "on", "tras cambiar de pad", "apagado") if q not in filas]
    if faltan:
        print ("FALLA  el volcado no trae %s" % ", ".join (faltan))
        return 1

    fallos = []

    #  --- APAGADO: cada pad el suyo, a tope. Es la linea de control, y hace
    #  falta: sin ella, un disparoDe que devolviera siempre el pad capturado
    #  pasaria las otras tres.
    for que in ("off", "apagado"):
        d = filas[que]
        recto = list (range (PADS))
        if d["pads"] != recto:
            fallos.append ("%s: los pads son %s y tenian que ser cada uno el suyo"
                           % (que, d["pads"][:8]))
        if any (abs (v - 1.0) > 1e-4 for v in d["vels"]):
            fallos.append ("%s: hay fuerzas distintas de 1 (%s)" % (que, d["vels"][:4]))

    #  --- ENCENDIDO, y las dos lineas dicen lo MISMO. La segunda es la que
    #  mide la captura: entre una y otra se movio selectedPad.
    for que in ("on", "tras cambiar de pad"):
        d = filas[que]

        malos = [i for i, p in enumerate (d["pads"]) if p != CAPTURADO]
        if malos:
            fallos.append ("%s: %d de %d pads no van al %d (el primero, el %d, va al %d)"
                           % (que, len (malos), PADS, CAPTURADO,
                              malos[0], d["pads"][malos[0]]))

        vels = d["vels"]
        for banco in range (PADS // PORBANCO):
            tramo = vels[banco * PORBANCO : (banco + 1) * PORBANCO]

            #  MONOTONA y con los dos extremos. Solo los extremos lo cumple una
            #  rampa desordenada por dentro; solo la monotonia lo cumple una
            #  rampa de 0.9 a 1.0, que no es una rejilla de niveles.
            if any (b <= a for a, b in zip (tramo, tramo[1:])):
                fallos.append ("%s: el banco %d no sube de izquierda a derecha: %s"
                               % (que, banco, [round (v, 3) for v in tramo]))
            if abs (tramo[0] - 1.0 / PORBANCO) > 1e-3 or abs (tramo[-1] - 1.0) > 1e-3:
                fallos.append ("%s: el banco %d va de %.4f a %.4f y tenia que ir de %.4f a 1"
                               % (que, banco, tramo[0], tramo[-1], 1.0 / PORBANCO))

    if filas["on"]["pads"] != filas["tras cambiar de pad"]["pads"]:
        fallos.append ("mover el pad elegido (%d -> %d) cambio el destino: %d -> %d"
                       % (CAPTURADO, DESPUES,
                          filas["on"]["pads"][0], filas["tras cambiar de pad"]["pads"][0]))

    for que in ("off", "on", "tras cambiar de pad", "apagado"):
        d = filas[que]
        print ("%-20s pads %-6s vels %.4f .. %.4f"
               % (que, "%d..%d" % (d["pads"][0], d["pads"][-1]),
                  d["vels"][0], d["vels"][PORBANCO - 1]))

    print()
    if fallos:
        for f in fallos: print ("FALLA  " + f)
        return 1
    print ("16 niveles: un pad, dieciseis fuerzas, y el pad se captura al encender")
    return 0


sys.exit (main())

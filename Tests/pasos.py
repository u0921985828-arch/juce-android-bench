#!/usr/bin/env python3
# ============================================================================
#  LOS PASOS NO SE PISAN, Y EL DEDO NO PESA POR DONDE CAE.
#
#  «Que revises los steps del secuenciador, que cada parametro que tiene cada
#  uno es independiente al otro. Hay mas cosas que estan de raro
#  funcionamiento.» El almacen `[patron][paso][pad]` ya lo era; lo que no lo
#  era es la cara. La tira de debajo de la rejilla escribe en `selectedStep` x
#  `selectedPad`, y cambiar de pad dejaba la tira apuntando a un paso APAGADO
#  del pad nuevo: el primer mando que se tocaba le escribia una fuerza a un
#  golpe que no existia, y ese golpe la sacaba el dia que se encendiera. Y
#  encender una casilla que se habia borrado resucitaba los nueve campos del
#  golpe borrado, porque `setStep` solo mueve el bit.
#
#  La app fotografia los 8 x 192 x 64 pasos, los nueve campos de cada uno,
#  antes y despues de cada gesto -tocar, mover los ocho mandos, cambiar de pad,
#  borrar, volver a encender, la rejilla a 1/8- y cuenta las celdas que
#  cambiaron fuera de la que el gesto nombra. Tiene que salir CERO en todas.
#
#  Y el tapeo: «asi los golpes son de la misma intensidad al tapear solamente…
#  no tiene base fisica correcta». La fuerza salia de la ALTURA del dedo -1.0
#  arriba, 0.35 abajo, 9 dB-; sin presion medida, ahora es la misma en los tres.
#
#      python3 Tests/pasos.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import PANTALLA, display_alive


def corre():
    casa = tempfile.mkdtemp (prefix="zati-pasos-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_PASOS="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "pasos" in r: return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes")
        return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea de pasos")
        return 1

    malas = []

    #  S8 · EL TAPEO ES PLANO. Tres alturas y una sola fuerza, la del tope. Con
    #  `byPosition` de vuelta salen 0.97 / 0.68 / 0.38.
    vy = r["vel_y"]
    print ("tapeo    fuerza en y=0.05/0.50/0.95: %s" % " / ".join ("%.2f" % v for v in vy))
    if len (vy) != 3 or any (abs (v - 1.0) > 1e-3 for v in vy):
        malas.append ("el tapeo depende de la altura del dedo: %s" % vy)

    #  S7.1 · Tocar una casilla vacia y mover los ocho mandos: el paso tocado
    #  lleva los ocho, y ninguno mas cambia.
    print ("tocar    ajenas %d  destino %d  tira viva %d"
           % (r["ajenas_tocar"], r["destino"], r["tira_viva"]))
    if r["ajenas_tocar"] != 0:
        malas.append ("tocar y mover la tira cambio %d pasos ajenos" % r["ajenas_tocar"])
    if r["destino"] != 1:
        malas.append ("los ocho mandos no llegaron al paso tocado")
    if r["tira_viva"] != 1:
        malas.append ("la tira de un paso encendido esta apagada")

    #  S7.2 · Cambiar de pad con la tira en un paso que en el pad nuevo esta
    #  apagado: la tira se apaga y mover el mando no escribe nada.
    print ("pad      ajenas %d  tira muerta %d" % (r["ajenas_cambiar_pad"], r["tira_muerta"]))
    if r["ajenas_cambiar_pad"] != 0:
        malas.append ("cambiar de pad y mover la tira escribio %d pasos apagados"
                      % r["ajenas_cambiar_pad"])
    if r["tira_muerta"] != 1:
        malas.append ("la tira sigue viva sobre un paso apagado")

    #  S7.3 · Borrar mueve el bit y nada mas; el mando despues no reescribe.
    print ("borrar   ajenas %d" % r["ajenas_borrado"])
    if r["ajenas_borrado"] != 0:
        malas.append ("borrar un paso y mover la tira cambio %d celdas" % r["ajenas_borrado"])

    #  S7.4 · Volver a encender: nace limpio. Sin `vaciaPaso` hereda los ocho.
    print ("hereda   %d campos del golpe borrado" % r["hereda"])
    if r["hereda"] != 0:
        malas.append ("un paso re-encendido heredo %d campos del borrado" % r["hereda"])

    #  S7.5 · La rejilla a 1/8 sobre un patron de 1/16: dos pasos por casilla,
    #  y la casilla 6 es el paso 12.
    print ("1/8      %d pasos por casilla  casilla 6 -> paso %d  encendido %d  ajenas %d"
           % (r["ppc"], r["paso_1_8"], r["en_1_8"], r["ajenas_1_8"]))
    if r["ppc"] != 2:
        malas.append ("la rejilla a 1/8 no dio dos pasos por casilla sino %d" % r["ppc"])
    if r["paso_1_8"] != 12 or r["en_1_8"] != 1:
        malas.append ("la casilla 6 a 1/8 no encendio el paso 12 (dio %d)" % r["paso_1_8"])
    if r["ajenas_1_8"] != 0:
        malas.append ("tocar a 1/8 cambio %d pasos ajenos" % r["ajenas_1_8"])

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("OK  los pasos no se pisan y el tapeo es plano")
    return 0


if __name__ == "__main__":
    sys.exit (main())

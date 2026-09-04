#!/usr/bin/env python3
# ============================================================================
#  LOS MODOS ARMADOS: SOLO DESDE LA CARA, Y EL ESTADO VISTO DONDE SE ACTUA.
#
#  Aislar un sonido mientras tocas es el gesto mas repetido de una sesion y
#  estaba en la MESA — a un toque, y la mesa TAPA la rejilla: para oir un pad
#  solo habia que cerrar, mirar y volver.
#
#  Es un MODO y no un gesto porque en un pad no queda ninguno: tocar dispara -al
#  APOYAR, desde que se midio que el `onClick` le sumaba al golpe todo el tiempo
#  del dedo- y mantener abre la ficha. Y un modo mas es exactamente lo que la
#  feria senalo como mal resuelto: «el estado tiene que verse DONDE SE ACTUA, no
#  donde se armo» — la tapa encendida esta en una fila y el dedo esta en la
#  rejilla. Por eso las dos mitades entran juntas o no entran.
#
#  NINGUNA DE LAS ONCE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Una rejilla
#  que dispara donde deberia aislar se maqueta perfecta: no solapa, no se sale,
#  no corta un rotulo, no mide cero y esta traducida. Es la familia de los cinco
#  fallos del compas del piano.
#
#      python3 Tests/modos.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import display_alive                                    # noqa: E402


def corre (tam="412x915"):
    """Con HOME propio: con el de verdad la app restaura la sesion que hubiera
    y los solos no serian los que esta prueba pone."""
    casa = tempfile.mkdtemp (prefix="zati-modos-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=tam, ZATI_LANG="es",
                ZATI_MODOS="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return {}
    out = {}
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if "modos" in r: out[r["modos"]] = r
    return out


def main ():
    if not os.path.exists (APP):
        print ("no hay binario: cmake --build build --target Zati"); return 1
    if not display_alive():
        print ("no hay DISPLAY vivo"); return 1

    r = corre()
    if not r:
        print ("FALLA  la app no publico las lineas de los modos"); return 1

    malo = []

    #  1. CON EL MODO ARMADO, TOCAR AISLA Y NO SUENA — con DOS cifras.
    #
    #  Solo la primera la cumple un modo que ademas dispara, y entonces el golpe
    #  que quieres oir aislado te lo comes tu en el mismo toque; solo la segunda
    #  la cumple una tapa muerta, que no aisla nada.
    a = r.get (1, {})
    print ("solo       armado %d   aislado el pad %d   voces %d"
           % (a.get ("armado", -1), a.get ("aislado", -99), a.get ("voces", -1)))
    if not a.get ("armado"):
        malo.append ("la tapa SOLO no arma el modo")
    if a.get ("aislado") != 5:
        malo.append ("con SOLO armado, tocar el pad 5 no lo aisla (aislado %s)"
                     % a.get ("aislado"))
    if a.get ("voces", -1) != 0:
        malo.append ("con SOLO armado, tocar el pad ademas lo DISPARA: %d voces"
                     % a.get ("voces", -1))

    #  2. Y MANTENER LOS QUITA TODOS. Vaciar los solos es lo unico de esta
    #     funcion que no se deshace tocando otra vez, asi que no puede compartir
    #     gesto con armarla — es la misma pareja que AUTO en la cancion.
    b = r.get (2, {})
    print ("           tras mantener SOLO, solos puestos: %d" % b.get ("trasMantener", -1))
    if b.get ("trasMantener", -1) != 0:
        malo.append ("mantener SOLO no quita los solos")

    #  3. EL LIENZO DICE QUE HAY UN MODO PUESTO, medido en PIXELES.
    #
    #  Un booleano que el codigo se pone a si mismo no mide nada: es el fallo de
    #  `caraLista`, que decia «tapada» con la cara entera a la vista. Se pinta el
    #  mismo componente con el modo quitado y con el modo puesto y se cuentan
    #  los pixeles que cambian.
    c = r.get (3, {})
    print ("lienzo     el pad cambia en %d px   el piano en %d px"
           % (c.get ("pad", -1), c.get ("piano", -1)))
    if c.get ("pad", 0) <= 0:
        malo.append ("la rejilla de pads no dice que hay un modo armado: 0 px cambian")
    if c.get ("piano", 0) <= 0:
        malo.append ("la rejilla del piano no dice que herramienta esta armada: 0 px cambian")

    #  4. Y UN PAD CALLADO POR EL SOLO DE OTRO SE VE. Es la mitad que la mesa no
    #     podia dar: el solo se pone alli y alli la rejilla no se ve.
    d = r.get (4, {})
    print ("callado    un pad mudo por el solo de otro cambia en %d px" % d.get ("mudo", -1))
    if d.get ("mudo", 0) <= 0:
        malo.append ("un pad callado por el solo de otro se dibuja igual que uno que suena")

    if malo:
        for m in malo: print ("FALLA  " + m)
        return 1
    print ("SOLO se arma desde la cara y el lienzo dice en que modo esta")
    return 0


if __name__ == "__main__":
    sys.exit (main())

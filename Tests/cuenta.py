#!/usr/bin/env python3
# ============================================================================
#  LA CUENTA ATRAS Y EL METRONOMO.
#
#  Las dos existian A MEDIAS, que es peor que no existir porque parece que
#  estan. El clic tenia tapa SOLO en la vista de audio de CANCION y grabar lo
#  FORZABA -lo mirases o no-, y `armaCuentaAtras (1)` estaba escrito UNA vez en
#  toda la app: un compas, clavado, sin opcion, y solo en el camino de grabar
#  al arreglo. Grabar de normal -el microfono de la cara- no tenia ninguna de
#  las dos, y una toma que entra a ojo entra corrida la grabes sobre el arreglo
#  o sola.
#
#  NINGUNA DE LAS NUEVE REGLAS DE `expo.py` PUEDE VER ESTO. Son fallos de
#  ESTADO: una app que fuerza el metronomo se maqueta perfecta, no solapa, no
#  corta un rotulo y esta traducida. Es la familia de los cinco fallos del
#  compas del piano.
#
#  SE MIDE POR LA TAPA y no poniendo el numero por dentro: `cuentaButtons[i]->
#  onClick` es lo que escribe la preferencia, y llamar a `saveCuentaPref` a
#  mano se salta justo el camino que la persona recorre.
#
#      python3 Tests/cuenta.py
# ============================================================================
import json, os, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre (ruta=None):
    casa = tempfile.mkdtemp (prefix="zati-cuenta-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_CUENTA="1")
    #  LA RUTA DE SALIDA COMO ENTRADA DEL BANCO. En un escritorio no hay
    #  altavoz de telefono que realimentar, asi que sin esto la guarda del
    #  monitor solo existiria en el aparato y no la mediria nadie. Es lo mismo
    #  que hacen ZATI_SKIN con la carcasa y ZATI_DLC con los packs.
    if ruta is not None: env["ZATI_RUTA"] = ruta
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("cuenta"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea de la cuenta")
        return 1

    malas = []

    #  1. LOS TRES VALORES, con DOS cifras y no una: lo que la preferencia dice
    #     Y si el motor se queda esperando de verdad. Solo la primera la cumple
    #     tambien una tapa que escribe el numero y no lo usa - que es
    #     exactamente como estaba el camino del microfono antes de esto.
    print ("chips    compases %-6s   espera %-6s" % (r["compases"], r["espera"]))
    if r["compases"] != "0,1,2":
        malas.append ("las tres tapas no escriben 0, 1 y 2: %s" % r["compases"])
    if r["espera"] != "0,1,1":
        malas.append ("con la cuenta a %s el motor no espera lo que debe: la tapa "
                      "escribe el numero y no lo usa" % r["espera"])

    #  2. GRABAR YA NO FUERZA EL CLIC. Se apaga a mano y se arma la cuenta: con
    #     el `engine.setClick (true)` de antes esto sale 1, y la persona se
    #     encuentra el metronomo colandose en la toma por los cascos.
    print ("clic     apagado a mano, tras armar la cuenta: %d" % r["clic_tras_armar"])
    if r["clic_tras_armar"] != 0:
        malas.append ("grabar volvio a FORZAR el metronomo: se apago a mano y salio "
                      "encendido")

    #  3. Y LAS DOS SE RECUERDAN. El fichero es de la PERSONA y no del proyecto
    #     -como el idioma, la carcasa y el master- asi que lo que se comprueba
    #     es que la siguiente lectura devuelva lo que se dejo. La app borra el
    #     valor en memoria antes de leer: si al volver sigue puesto no es que se
    #     haya guardado, es que nadie lo quito.
    print ("vuelve   compases %d   clic %d" % (r["vuelve"], r["clic_vuelve"]))
    if r["vuelve"] != 2:
        malas.append ("la cuenta no vuelve del disco: %s" % r["vuelve"])
    if r["clic_vuelve"] != 1:
        malas.append ("el metronomo no vuelve del disco: %s" % r["clic_vuelve"])

    #  4. EL MONITOR: oirte por los cascos mientras grabas. Es la otra mitad de
    #     «como se prepara una toma» y por eso vive aqui.
    #
    #     Con DOS cifras y por la TAPA, igual que la cuenta: lo que la
    #     preferencia dice Y lo que el motor acaba teniendo. Solo la primera la
    #     cumple una casilla que escribe un booleano y no lo empuja.
    print ("monitor  apagado %d   puesto %d   vuelve %d   (altavoz %d)"
           % (r["mon_apagado"], r["mon_puesto"], r["mon_vuelve"], r["mon_altavoz"]))
    if r["mon_altavoz"] != 0:
        malas.append ("la corrida normal cree que la salida es el altavoz: "
                      "la guarda mide otra cosa")
    if r["mon_apagado"] != 0:
        malas.append ("con el monitor APAGADO el motor lo tiene puesto")
    if r["mon_puesto"] != 1:
        malas.append ("con el monitor ENCENDIDO el motor no lo tiene: la tapa "
                      "escribe la preferencia y no la empuja")
    if r["mon_vuelve"] != 1:
        malas.append ("el monitor no vuelve del disco: %s" % r["mon_vuelve"])

    #  5. Y LA GUARDA, que es lo que separa un monitor de un acople: sin cascos
    #     la produccion sale por el altavoz, se cuela en la toma y el microfono
    #     cierra el lazo. Se pregunta AL APARATO -`AudioManager.getDevices`- y
    #     no a una casilla de buena fe, que es la regla de `canReallyWriteInto`.
    #
    #     La preferencia sigue diciendo que si; lo que no puede es SONAR. Sin
    #     las dos cifras, «no suena» lo cumple igual una tapa muerta.
    a = corre ("altavoz")
    if a is None:
        malas.append ("la app no publico la linea con la ruta forzada")
    else:
        print ("altavoz  puesto %d   vuelve %d   (altavoz %d)"
               % (a["mon_puesto"], a["mon_vuelve"], a["mon_altavoz"]))
        if a["mon_altavoz"] != 1:
            malas.append ("ZATI_RUTA=altavoz no llega a la guarda")
        if a["mon_puesto"] != 0:
            malas.append ("con la salida en el ALTAVOZ el monitor suena igual: "
                          "eso es un acople y la toma se lleva la produccion dentro")
        if a["mon_vuelve"] != 1:
            malas.append ("la guarda se llevo por delante la preferencia: "
                          "no suena Y ademas se apaga sola")

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("la cuenta atras es una opcion, el metronomo no se fuerza, el monitor "
           "sale por los cascos y no por el altavoz, y las cuatro se recuerdan")
    return 0


if __name__ == "__main__":
    sys.exit (main())

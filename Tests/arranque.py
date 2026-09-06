#!/usr/bin/env python3
"""QUE LA CARA NO SE ENSEÑE HASTA QUE HA DEJADO DE MOVERSE.

«Cuando abres la app hace un ajuste a la pantalla.» Con targetSdk 36 la app va
edge to edge y tiene que restarse las barras del sistema, y en el constructor no
se pueden pedir -getRootWindowInsets no contesta hasta que la ventana esta
enganchada- asi que el primer fotograma se maquetaba sin ajustar y el ajuste
llegaba en el primer o segundo latido, 60 ms por tick. Justo despues de que
Android retirara su splash, que se va en cuanto la app dibuja: el suyo no lo
puede tapar por diseno.

ESTO NO PASA EN EL ESCRITORIO. SystemInsets::get() devuelve {} siempre, asi que
sin un andamio no habria nada que medir: ZATI_INSETS con su tick simula la
respuesta tardia -y el SILENCIO de antes, que es lo que la primera version de
esta prueba no simulaba- igual que ZATI_SKIN convierte la carcasa en entrada.

TRES cifras y no una, que es la regla de la casa:

  1. La cara no se ve antes de tiempo.
  2. Y SE ACABA VIENDO. Sin esta, una portada que no se levanta nunca aprueba
     la primera - la misma trampa que ya costo una medida en QUITAR RUIDO y en
     el candado de los packs.
  3. Y lo que se ve al destapar es lo DEFINITIVO: el rectangulo de la placa de
     pads en el primer fotograma visible es el mismo que al final. Eso es lo
     que la persona llama «no hay ajuste».

Mas el tope: si el aparato no contesta nunca, la portada se levanta igual.

Y LA CAJA NEGRA, que es el otro arranque y no la medi­a nadie: «La vez anterior
se cerro en: ficha MEZCLA» salia en CADA arranque -un parte de caida que sale
siempre no es un parte, es ruido, y ademas entrena a no leer el que importe-.
Con las dos mitades: una salida normal de Android no avisa, y una caida si, con
su prefijo. Ver la funcion `bitacora` de abajo.
"""
import json, os, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import display_alive

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

TICKS   = 10        # cuantos ticks deja correr la app
LLEGAN  = 4         # y en cual contestan los margenes
#  EL TOPE LO DICE LA APP y no se escribe aqui: el plazo esta en milisegundos
#  -kPortadaTopeMs- y cuantos ticks son depende de `DeviceTier::uiIntervalMs`,
#  que vale 33, 40, 60 o 100 segun el aparato. Escrito en el banco seria la
#  misma regla en dos sitios, y la de aqui se quedaria vieja el dia que cambie
#  la otra. Este es solo el respaldo por si la linea no trae el campo.
TOPE    = 30


def corre (casa, ticks, muere=0, senal=0):
    """Un arranque en `casa`, que NO se borra: la bitacora vive ahi.

    `muere` es el tick en el que la app se va como se va en Android -onPause y
    el proceso desaparece, sin pasar por `shutdown()`- y `senal` la que levanta
    en vez de eso. Ver ZATI_MUERE en MainComponent.h."""
    env = dict (os.environ, HOME=casa, ZATI_SIZE="412x915",
                ZATI_ARRANQUE=str (ticks),
                ZATI_INSETS="100,0,50,0", ZATI_INSETS_TICK="2",
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    if muere: env["ZATI_MUERE"] = str (muere)
    if senal: env["ZATI_SENAL"] = str (senal)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    except subprocess.TimeoutExpired:
        return []
    filas = []
    for l in out.splitlines():
        l = l.strip()
        if not l.startswith ('{'): continue
        try:    d = json.loads (l)
        except Exception: continue
        if d.get ("arranque") == "cara": filas.append (d)
    return filas


def bitacora (malas):
    """LA CAJA NEGRA, con las DOS mitades.

    Un mecanismo que no avisa NUNCA pasa la primera comprobacion solo, y el de
    ayer -que avisaba SIEMPRE- pasaba la segunda: «La vez anterior se cerro en:
    ficha MEZCLA» salia en cada arranque, en un telefono donde no se habia
    caido nada. La causa es que `finLimpio()` tenia un solo llamante,
    `Main.cpp::shutdown()`, y en Android ese camino no corre: ATRAS manda la
    tarea al fondo y lo que la mata despues es un SIGKILL.

    Y no lo miraba nadie: el unico `grep` de `zati-bitacora.txt` en `Tests/`
    era `expo.py` BORRANDOLA.

    Se arranca DOS VECES CON EL MISMO HOME, que es lo unico que hace medible
    «la vez anterior» - con un HOME por corrida las dos serian la primera. Es
    lo mismo que ya hace la marca del tour."""
    #  1. Salida normal de Android: onPause y el proceso se va. NO avisa.
    casa = tempfile.mkdtemp (prefix="zati-bit-")
    try:
        corre (casa, 30, muere=4)
        segunda = corre (casa, 3)
        limpia = segunda[0].get ("previa", "?") if segunda else "?"
        print ("tras una salida normal de Android, la vez anterior dice %r" % limpia)
        if not segunda:
            malas.append ("bitacora: el segundo arranque no contesto")
        elif limpia:
            malas.append ("bitacora: parte de caida tras una salida limpia: %r" % limpia)

        #  2. Y UNA CAIDA DE VERDAD SI AVISA, con su prefijo. Sin esta, apagar
        #     el aviso del todo pasaria la primera.
        corre (casa, 30, muere=4, senal=11)
        tercera = corre (casa, 3)
        rota = tercera[0].get ("previa", "?") if tercera else "?"
        print ("tras una caida, la vez anterior dice %r" % rota)
        if not tercera:
            malas.append ("bitacora: el arranque tras la caida no contesto")
        elif not rota.startswith ("CAIDA senal 11"):
            malas.append ("bitacora: una caida no deja parte: %r" % rota)
    finally:
        shutil.rmtree (casa, ignore_errors=True)


def arranca (ticks, tick_insets, size="412x915"):
    """Los ticks de un arranque, con los margenes contestando en tick_insets."""
    casa = tempfile.mkdtemp (prefix="zati-arr-")
    env = dict (os.environ, HOME=casa, ZATI_SIZE=size,
                ZATI_ARRANQUE=str (ticks),
                ZATI_INSETS="100,0,50,0", ZATI_INSETS_TICK=str (tick_insets),
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    except subprocess.TimeoutExpired:
        return []
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = []
    for l in out.splitlines():
        l = l.strip()
        if not l.startswith ('{'): continue
        try:    d = json.loads (l)
        except Exception: continue
        if d.get ("arranque") == "cara": filas.append (d)
    return filas


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario: compila primero")
    if not display_alive():      sys.exit ("la pantalla virtual no responde")

    malas = []
    filas = arranca (TICKS, LLEGAN)
    if not filas:
        print ("FALLA  el arranque no contesto")
        return 1

    print ("%-6s %9s   %s" % ("tick", "cara", "placa de pads"))
    for d in filas:
        print ("%-6d %9s   %s  pintadas %d" % (d["tick"], "TAPADA" if d["cubierta"] else "a la vista",
                                              d["placa"], d.get ("pintadas", 0)))

    #  1. Tapada mientras la geometria puede moverse.
    antes = [d for d in filas if d["tick"] < LLEGAN]
    tapadas = [d["tick"] for d in antes if not d["cubierta"]]
    print()
    print ("los margenes contestan en el tick %d" % LLEGAN)
    if tapadas:
        malas.append ("la cara se ve en los ticks %s, antes de que la geometria sea definitiva"
                      % (tapadas,))

    #  1b. Y TAPADA DE VERDAD, no solo segun la bandera. Esta comprobacion
    #      existe porque la primera version NO la tenia: publicaba caraLista, o
    #      sea la bandera, y al quitar la portada a proposito siguio saliendo
    #      VERDE - la bandera decia «tapada» con la cara entera a la vista. Se
    #      cuenta dentro de pintaPortada, que es quien tapa.
    if antes and antes[-1].get ("pintadas", 0) < 1:
        malas.append ("la portada no se pinto ni una vez: la cara estaba a la vista")

    #  2. Y se acaba viendo.
    if filas[-1]["cubierta"]:
        malas.append ("la portada no se levanta nunca: sigue tapada en el tick %d"
                      % filas[-1]["tick"])

    #  3. Y lo primero que se ve es lo definitivo.
    visibles = [d for d in filas if not d["cubierta"]]
    if not visibles:
        malas.append ("la cara no llego a verse en %d ticks" % TICKS)
    else:
        primera, ultima = visibles[0]["placa"], visibles[-1]["placa"]
        print ("la placa al destapar %s   y al final %s" % (primera, ultima))
        if primera != ultima:
            malas.append ("la placa se mueve DESPUES de enseñarse: %s -> %s"
                          % (primera, ultima))
        #  Y que el andamio este midiendo algo: si los margenes no movieran la
        #  maqueta, las tres comprobaciones de arriba pasarian sin que existiera
        #  el fallo. Es la corrida de control.
        tapada = [d["placa"] for d in filas if d["cubierta"]]
        if tapada and tapada[0] == primera:
            malas.append ("los margenes simulados no mueven la maqueta: la prueba no mide nada")
        elif tapada:
            print ("y sin ajustar era %s, o sea %d px de salto"
                   % (tapada[0], abs (tapada[0][1] - primera[1])))

    #  4. EL TOPE. Un aparato que no conteste nunca no puede dejar la portada
    #     puesta: es la hermana de «ningun camino puede dejar la app en
    #     silencio». Se piden margenes que llegan mas alla del plazo.
    print()
    lentas = arranca (TOPE + 4, TOPE + 99)
    if not lentas:
        malas.append ("el arranque sin margenes no contesto")
    else:
        tope = lentas[0].get ("tope", TOPE)
        if tope != TOPE:
            print ("el tope que dice la app son %d ticks" % tope)
            lentas = arranca (tope + 4, tope + 99)
        print ("sin margenes nunca, la portada se levanta en el tick %s"
               % next ((d["tick"] for d in lentas if not d["cubierta"]), "NUNCA"))
        if lentas[-1]["cubierta"]:
            malas.append ("sin respuesta del sistema la portada se queda puesta para siempre")

    print()
    bitacora (malas)

    print()
    for m in malas: print ("FALLA ", m)
    print ("la cara no se enseña hasta que ha dejado de moverse" if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

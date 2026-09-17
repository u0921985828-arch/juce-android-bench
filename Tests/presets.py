#!/usr/bin/env python3
# ============================================================================
#  LOS PRESETS DE CADA EFECTO.
#
#  Llego del telefono: «1 tipo de cada, si que podriamos preparar presets para
#  cada efecto, estudialo». Hasta esta tanda un efecto arrancaba en su fila de
#  `AudioEngine::kFxDef` y a partir de ahi se movia a mano, tipo por tipo y
#  canal por canal: veintitres tipos por cuatro parametros por treinta y dos
#  canales.
#
#  NINGUNA DE LAS DIECINUEVE REGLAS DURAS DE `expo.py` PUEDE VER NADA DE ESTO.
#  Una tabla con un numero fuera de rango, un preset que no cambia el audio o
#  uno repetido se maquetan perfectos: no solapan, no se salen, no cortan un
#  rotulo y estan traducidos. Es la misma familia que los cinco fallos del
#  compas del piano.
#
#  ESTA PRUEBA ES LA HERMANA DE `instr.py` y se lee igual: la app RINDE -
#  `auditFxPresets` pone cada preset, mide lo que sale y publica una linea por
#  caso- y el juicio vive aqui. Escribir los listones en C++ seria la misma
#  regla en dos sitios, que es lo que esta casa lleva quince veces pagando.
#
#      python3 Tests/presets.py
# ============================================================================
import json, os, subprocess, sys, tempfile, shutil

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA. Ver `kits.py`: este fichero
#  llevaba `:99` clavado en tres bancos distintos mientras la comprobacion leia
#  el entorno, y el veredicto entero salia en rojo con la app perfecta.
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import PANTALLA, display_alive                           # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  EL MISMO LISTON QUE `un inserto cambia el audio` de `StressTest`, y no uno
#  nuevo: alli se decidio que por debajo del 2 % de muestras distintas un
#  efecto no esta haciendo nada que se pueda oir, y un preset es un efecto con
#  sus numeros puestos. *El liston de una prueba no se reinventa en la de al
#  lado.*
MIN_CAMBIA = 2.0        # % de muestras que tienen que cambiar

#  Y LA MISMA TOLERANCIA CON LA QUE LA AUDITORIA YA CUADRA LOS DOS DEFECTOS
#  -`defectos_cruzados`, en `Tests/ranuras.py`-: 0.001 en absoluto.
TOL_DEF = 0.001


def corre ():
    """Arranca la app en modo presets y clasifica sus lineas."""
    if not os.path.exists (APP):
        print ("no hay binario: compila con cmake --build build")
        return None
    if not display_alive ():
        print ("no hay DISPLAY vivo")
        return None

    casa = tempfile.mkdtemp (prefix="zati-presets-")
    try:
        env = dict (os.environ,
                    DISPLAY=PANTALLA, HOME=casa,
                    XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                    ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                    ZATI_PRESETS="1")
        out = subprocess.run ([APP], env=env, capture_output=True,
                              timeout=900).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        shutil.rmtree (casa, ignore_errors=True)
        print ("la app no termino")
        return None

    filas = {"preset": [], "kfxdef": [], "spec": [], "tuyo": []}
    for line in out.splitlines ():
        line = line.strip ()
        if not (line.startswith ("{") and line.endswith ("}")):
            continue
        #  Y SOLO SE TRAGA LO QUE NO ES JSON. Un `except Exception` se comeria
        #  una clave repetida -que es lo que el hook levanta- en silencio.
        try:
            d = json.loads (line)
        except json.JSONDecodeError:
            continue
        for k in filas:
            if k in d:
                filas[k].append (d)
                break

    shutil.rmtree (casa, ignore_errors=True)
    return filas


def main ():
    r = corre ()
    if r is None:
        return 1

    fallos = []
    ps = r["preset"]
    tipos = sorted ({d["fx"] for d in ps})

    # ------------------------------------------------------------------
    #  1. ESTAN TODOS. Seis por tipo contando DEFECTO.
    # ------------------------------------------------------------------
    porTipo = {}
    for d in ps:
        porTipo.setdefault (d["fx"], []).append (d)
    esperados = len (tipos) * 6
    print ("tabla      %d presets en %d tipos" % (len (ps), len (tipos)))
    if len (ps) != esperados:
        fallos.append ("salieron %d presets y son %d" % (len (ps), esperados))
    for f, lista in porTipo.items ():
        if len (lista) != 6:
            fallos.append ("el tipo %s trae %d presets y son 6"
                           % (lista[0]["tipo"], len (lista)))

    # ------------------------------------------------------------------
    #  2. NINGUNO SIN NOMBRE, Y NINGUNO REPETIDO DENTRO DE SU TIPO.
    #
    #  Dentro de su tipo y no en toda la tabla: «SUAVE» esta en CMP, en DSS,
    #  en PHA y en WAH y las cuatro veces dice lo mismo. Lo que no puede pasar
    #  es que dos celdas de la MISMA lista digan lo mismo, porque entonces una
    #  de las dos no se puede elegir a proposito.
    # ------------------------------------------------------------------
    repes = 0
    for f, lista in porTipo.items ():
        nombres, numeros = set (), set ()
        for d in lista:
            if not d["nombre"].strip ():
                fallos.append ("%s preset %d sin nombre" % (d["tipo"], d["k"]))
            if d["nombre"] in nombres:
                fallos.append ("%s repite el nombre %s" % (d["tipo"], d["nombre"]))
                repes += 1
            nombres.add (d["nombre"])
            clave = tuple (round (v, 6) for v in d["p"]) + (d["bandas"],)
            if clave in numeros:
                fallos.append ("%s repite los numeros de %s" % (d["tipo"], d["nombre"]))
                repes += 1
            numeros.add (clave)
    print ("nombres    %d nombres, %d repetidos" % (len (ps), repes))

    # ------------------------------------------------------------------
    #  3. NINGUNO FUERA DE SU RANGO.
    #
    #  Los rangos NO se copian aqui: la app los publica desde
    #  `fxDefs[f].spec[pi]`, que es donde el numero esta razonado. Una segunda
    #  copia en Python seria la misma tabla en dos sitios y la que se quedara
    #  vieja diria que todo cabe.
    # ------------------------------------------------------------------
    rango = {(d["fx"], d["p"]): (d["lo"], d["hi"]) for d in r["spec"]}
    fuera = 0
    for d in ps:
        for pi in range (3):
            lo, hi = rango.get ((d["fx"], pi), (None, None))
            if lo is None:
                continue
            v = d["p"][pi]
            #  Con el mismo epsilon del acotado: un preset escrito en el tope
            #  exacto no puede fallar por el ultimo bit del float.
            if v < lo - 1e-4 or v > hi + 1e-4:
                fallos.append ("%s %s: p%d vale %.4f y su rango es %.4f..%.4f"
                               % (d["tipo"], d["nombre"], pi, v, lo, hi))
                fuera += 1
    print ("rangos     %d valores medidos, %d fuera de su spec"
           % (len (ps) * 3, fuera))

    # ------------------------------------------------------------------
    #  4. EL PRESET CERO ES `kFxDef`, y no una tercera tabla de defectos.
    #
    #  Esta casa ya tiene DOS -la del motor y el `spec[pi].def` de la cara- y
    #  una comprobacion que las cuadra sobre 69 casillas. La tercera es la que
    #  se queda vieja sin que nadie se entere, asi que el preset DEFECTO no
    #  escribe numeros: los LEE.
    # ------------------------------------------------------------------
    defs = {d["fx"]: d["d"] for d in r["kfxdef"]}
    descuadran = 0
    for d in ps:
        if d["k"] != 0:
            continue
        esperado = defs.get (d["fx"])
        if esperado is None:
            continue
        for pi in range (3):
            if abs (d["p"][pi] - esperado[pi]) > TOL_DEF:
                fallos.append ("%s DEFECTO p%d vale %.4f y kFxDef dice %.4f"
                               % (d["tipo"], pi, d["p"][pi], esperado[pi]))
                descuadran += 1
        #  Y el cuarto -el enganche del modulador- nace LIBRE. `kFxDef` no
        #  tiene cuarta columna a proposito: el cero significa algo por si
        #  mismo y no hace falta inventarle una fila al motor.
        if abs (d["p"][3]) > TOL_DEF:
            fallos.append ("%s DEFECTO deja el enganche en %.4f y nace libre"
                           % (d["tipo"], d["p"][3]))
            descuadran += 1
    print ("defectos   el preset cero contra kFxDef: %d de %d descuadran"
           % (descuadran, len (tipos) * 4))

    # ------------------------------------------------------------------
    #  5. NINGUNO MUDO — Y ESTA ES LA QUE HACE HONESTA LA TABLA.
    #
    #  Sin ella, un preset puesto en neutro pasa las cuatro de arriba sin hacer
    #  absolutamente nada. Se mide en un canal que NO ES EL CERO, que es la
    #  leccion que costo dieciocho efectos mudos: el banco medía el camino de
    #  cada efecto en el canal 0 -que era justo el unico que funcionaba- y las
    #  dos medidas salian verdes con el fallo dentro.
    #
    #  El preset CERO queda fuera a proposito y no por comodidad: es la fila de
    #  fabrica, y la fila de fabrica trae la MEZCLA en cero — un efecto recien
    #  puesto nace apagado. Pedirle que cambie el audio seria pedirle que
    #  incumpla lo que es.
    # ------------------------------------------------------------------
    mudos = []
    for d in ps:
        if d["k"] == 0:
            continue
        pc = d["distintas"] * 100.0 / max (1, d["muestras"])
        if pc < MIN_CAMBIA:
            mudos.append ("%s %s (%.1f %%)" % (d["tipo"], d["nombre"], pc))
    escritos = len (ps) - len (tipos)
    print ("audio      %d de %d presets cambian el audio en el canal 4"
           % (escritos - len (mudos), escritos))
    if mudos:
        print ("           mudos %s" % mudos[:12])
        fallos.append ("%d presets no cambian el audio" % len (mudos))

    # ------------------------------------------------------------------
    #  6. LOS TUYOS: donde acabo el fichero, y la vuelta.
    #
    #  Lo que se juzga NO es lo que devolvio la orden de guardar sino DONDE
    #  ACABO EL FICHERO — la misma leccion de `ensureDirectory` y de
    #  `canReallyWriteInto`. Y un nombre con `../` no puede escribir fuera de
    #  `ZATI/Presets`: `getChildFile` resuelve `..` subiendo un nivel.
    # ------------------------------------------------------------------
    if not r["tuyo"]:
        fallos.append ("la app no publico la linea de los presets tuyos")
    else:
        t = r["tuyo"][0]
        print ("tuyos      guardado %d, existe %d, dentro de ZATI/Presets %d, "
               "fuera de sitio %d" % (t["guardado"], t["existe"], t["dentro"],
                                      t["fuera_de_sitio"]))
        print ("           el preset puesto era %d y mover un mando lo deja en %d"
               % (t["puesto_antes"], t["marca_tras_mando"]))
        print ("           vuelve: %.2f -> %.2f -> %.2f   marca %d   ficha %s"
               % (t["antes"], t["lejos"], t["vuelve"], t["movido"], t["nombre"]))
        if not t["guardado"] or not t["existe"] or not t["dentro"]:
            fallos.append ("un preset tuyo no acabo en ZATI/Presets")
        if t["fuera_de_sitio"] != 0:
            fallos.append ("un nombre con ../ escribio %d ficheros fuera de su carpeta"
                           % t["fuera_de_sitio"])
        #  MOVER UN MANDO LO MARCA. Sin esto la ficha dice «MI ECO» con el
        #  audio ya cambiado, que es mentir sobre lo que suena.
        #  Y SE MIDE SOBRE UN PRESET DE FABRICA, no despues de guardar uno
        #  tuyo: guardar deja la casilla en MOVIDO por su cuenta, asi que la
        #  cifra salia -1 con el codigo roto y con el bueno. Hacen falta las
        #  dos: que ANTES hubiera un indice de verdad y que despues sea -1.
        if t["puesto_antes"] < 0:
            fallos.append ("el andamio no dejo puesto un preset de fabrica (%d)"
                           % t["puesto_antes"])
        if t["marca_tras_mando"] != -1:
            fallos.append ("mover un mando no marco el preset como MOVIDO (%d)"
                           % t["marca_tras_mando"])
        if t["movido"] != -1:
            fallos.append ("guardar uno tuyo no dejo la casilla en MOVIDO (%d)"
                           % t["movido"])
        #  Y LA VUELTA, con el mando movido al extremo MAS LEJANO del valor de
        #  hoy: mover a un tope escrito sale verde si ya estaba en el tope.
        if abs (t["vuelve"] - t["antes"]) > 0.01:
            fallos.append ("el preset tuyo no volvio: %.4f contra %.4f"
                           % (t["vuelve"], t["antes"]))
        if t["nombre"] != "MI ECO":
            fallos.append ("la ficha dice «%s» y el preset puesto es MI ECO"
                           % t["nombre"])

    # ------------------------------------------------------------------
    if fallos:
        for f in fallos[:40]:
            print ("FALLA  " + f)
        if len (fallos) > 40:
            print ("...y %d mas" % (len (fallos) - 40))
        return 1

    print ("\n%d presets de efecto: ninguno sin nombre, ninguno repetido, "
           "ninguno fuera de rango, ninguno mudo, y los tuyos caen dentro"
           % len (ps))
    return 0


if __name__ == "__main__":
    sys.exit (main ())

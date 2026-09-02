#!/usr/bin/env python3
# ============================================================================
#  LAS SEIS RANURAS DE LA FILA DE EFECTOS.
#
#  Hasta esta tanda el indice de una tapa ERA su efecto: la tapa 3 tocaba el
#  DLY y no habia forma de que tocara otra cosa. Eso hace dos cosas mal a la
#  vez. Una maquina recien abierta enseñaba seis efectos que nadie habia
#  puesto -seis tapas de las que no sabes cuales vas a usar, con el mismo
#  argumento que ya costo una medida en los envios: «una mezcla se hace
#  subiendo lo que quieres, no apagando lo que no»- y el dia que haya mas de
#  seis tipos no hay donde meterlos.
#
#  Ahora la fila son seis RANURAS. Una vacia dibuja «+» y al tocarla o
#  mantenerla abre el menu; llena se comporta como siempre -tocar enciende,
#  mantener afina- y ya no vuelve a abrir el menu: lo que se pidio es ACCION
#  UNICA, y cambiar o quitar lo que hay se hace desde el RACK.
#
#  NINGUNA DE LAS NUEVE REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Son fallos
#  de INDICE y de estado, no de geometria: una fila que enciende el efecto
#  equivocado se maqueta perfecta, no solapa, no se sale, no corta un rotulo,
#  no mide cero y esta traducida. Es la familia de los cinco fallos del compas
#  del piano, otra vez.
#
#  SE MIDE POR EL GESTO Y NO POR EL CALLBACK. Llamar a `ponEnRanura` por dentro
#  se salta justo el codigo que decide si una tapa abre el menu o enciende un
#  efecto, que es donde vive todo lo que esta tanda anade. La app pulsa las
#  tapas de verdad -`fxButtons[s]->onClick`, `ranuraBtns[f]->onClick`- y aqui
#  solo se juzga lo que quedo.
#
#      python3 Tests/ranuras.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre (extra=None):
    """Una corrida con HOME propio: con el de verdad la app restaura la sesion
    que hubiera y no pasa por el camino que se quiere medir."""
    casa = tempfile.mkdtemp (prefix="zati-ranuras-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
                ZATI_RANURAS="1")
    if extra: env.update (extra)
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("ranuras"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes -cmake --build build-" % APP)
        return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea de ranuras")
        return 1

    malas = []

    #  1. EL MENU SE ABRE SOBRE UNA RANURA VACIA Y NO SOBRE UNA LLENA, con las
    #     DOS cifras: «se abrio» lo cumple igual un menu que se abre siempre, y
    #     entonces no habria forma de encender un efecto desde la cara. Y la
    #     tercera dice que la tapa llena hace lo OTRO, que es lo que separa «no
    #     abrio el menu» de «no hizo nada».
    print ("menu     sobre vacia %-3s  sobre llena %-3s  y la llena enciende %s"
           % (r["menu_tras_vacia"], r["menu_tras_llena"], r["enciende_al_tocar"]))
    if r["menu_tras_vacia"] != 0:
        malas.append ("tocar el «+» de la ranura 0 no abrio su menu (%s)"
                      % r["menu_tras_vacia"])
    if r["menu_tras_llena"] != -1:
        malas.append ("tocar una ranura LLENA abrio el menu (%s): entonces no hay "
                      "forma de encender un efecto desde la cara"
                      % r["menu_tras_llena"])
    if r["enciende_al_tocar"] != 1:
        malas.append ("tocar una ranura llena no encendio su efecto")

    #  2. ACCION UNICA. Elegir en el menu llena la ranura y la cierra, y a
    #     partir de ahi esa tapa ENCIENDE: no vuelve a ofrecer el menu nunca.
    #     Sin la ultima cifra, «ya no abre el menu» lo cumple tambien una tapa
    #     que se ha quedado muerta.
    print ("elegir   %s  menu %-3s  y despues enciende %s  con el mapa %s"
           % (r["tras_elegir"], r["menu_tras_elegir"],
              r["enciende_despues"], r["mapa_despues"]))
    if r["tras_elegir"] != "[-1,-1,4,-1,-1,-1]":
        malas.append ("elegir BIT en el menu de la ranura 2 dejo %s"
                      % r["tras_elegir"])
    if r["menu_tras_elegir"] != -1:
        malas.append ("el menu no se cerro al elegir (%s)" % r["menu_tras_elegir"])
    if r["enciende_despues"] != 1:
        malas.append ("la ranura recien llena no enciende su efecto al tocarla")
    if r["mapa_despues"] != r["tras_elegir"]:
        malas.append ("volver a tocar la ranura cambio lo que hay dentro: %s -> %s"
                      % (r["tras_elegir"], r["mapa_despues"]))

    #  3. UN TIPO, UNA RANURA. Su estado en el motor es uno solo -un filtro, una
    #     linea de retardo, una reverb- asi que dos ranuras del mismo tipo
    #     serian dos ventanas al mismo aparato con dos interruptores que se
    #     contradicen. Poner en la 0 el tipo que estaba en la 3 tiene que dejar
    #     la 3 VACIA, y eso es lo que dice el -1 del medio.
    print ("mover    %s   (el tipo 3 se fue de su ranura)" % r["tras_mover"])
    if r["tras_mover"] != "[3,1,2,-1,4,5]":
        malas.append ("poner en la ranura 0 un tipo que ya estaba en la 3 dejo %s: "
                      "un tipo tiene que estar en UNA ranura" % r["tras_mover"])

    #  4. VACIAR UNA RANURA APAGA SU EFECTO, con las dos mitades: hace falta
    #     verlo encendido ANTES, o «esta apagado» lo cumple tambien un efecto
    #     que no se encendio nunca.
    print ("vaciar   encendido antes %s  ->  despues %s"
           % (r["antes_de_vaciar"], r["tras_vaciar"]))
    if r["antes_de_vaciar"] != 1:
        malas.append ("el efecto no llego a encenderse: la medida no mide nada")
    if r["tras_vaciar"] != 0:
        malas.append ("vaciar la ranura dejo su efecto SONANDO y sin tapa donde "
                      "tocarlo")

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("las seis ranuras: el menu, la accion unica, un tipo una ranura, y "
           "vaciar apaga")
    return 0


if __name__ == "__main__":
    sys.exit (main())

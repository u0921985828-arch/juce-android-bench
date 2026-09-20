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

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


#  Las siete pantallas del banco, importadas y no escritas otra vez: una lista
#  de pantallas escrita dos veces es media app sin medir.
sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import SIZES as _SIZES
SIZES = [w for w, _ in _SIZES]
DEDO  = 40
#  EL MINIMO DE «CAMBIA EL AUDIO», heredado y no inventado: es el 2 % de
#  muestras distintas que `StressTest` usa en `un inserto cambia el audio`.
#  Dos pruebas con dos listones para la misma pregunta son dos reglas, y la
#  que se quede vieja deja de proteger nada.
MIN_CAMBIA = 2.0


def leeForma (s):
    """«3x7:460:115» -> columnas, filas, lo que pide de alto, ancho de celda."""
    cf, pide, celda = s.split (":")
    c, f = cf.split ("x")
    return int (c), int (f), int (pide), int (celda)


def corre (extra=None):
    """Una corrida con HOME propio: con el de verdad la app restaura la sesion
    que hubiera y no pasa por el camino que se quiere medir."""
    casa = tempfile.mkdtemp (prefix="zati-ranuras-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
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
    print ("         y al ENTRAR ya venia encendida: %s   (al elegir en el menu: %s)"
           % (r["enciende_al_entrar"], r["enciende_al_elegir"]))
    if r["menu_tras_vacia"] != 0:
        malas.append ("tocar el «+» de la ranura 0 no abrio su menu (%s)"
                      % r["menu_tras_vacia"])
    if r["menu_tras_llena"] != -1:
        malas.append ("tocar una ranura LLENA abrio el menu (%s): entonces no hay "
                      "forma de encender un efecto desde la cara"
                      % r["menu_tras_llena"])
    #  LAS DOS CIFRAS, Y NO UNA. Esto pedia «tocar una ranura llena la
    #  enciende» y era cierto mientras un efecto entrara APAGADO. Desde que se
    #  pidio que entre sonando -«que este activado tambien el efecto cuando se
    #  mete en el Slot»- el primer toque lo APAGA, que es lo que un interruptor
    #  hace, y esta linea daba FALLA sobre lo correcto: la prueba se quedo
    #  vieja al cambiar lo que mide. Se pregunta entero -entra encendida, Y la
    #  tapa conmuta- porque «entra encendida» sola la cumple tambien una tapa
    #  muerta, y «conmuta» sola la cumple una que entra apagada.
    if r["enciende_al_entrar"] != 1:
        malas.append ("poner un efecto en una ranura no lo dejo encendido: la "
                      "tapa se pinta LLENA y no mueve un decibelio")
    if r["enciende_al_tocar"] != 0:
        malas.append ("tocar una ranura llena y encendida no la apago (%s): esa "
                      "tapa tiene que conmutar" % r["enciende_al_tocar"])

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
    if r["enciende_al_elegir"] != 1:
        malas.append ("elegir un efecto en el menu no lo dejo encendido")
    if r["enciende_despues"] != 0:
        malas.append ("volver a tocar la ranura recien llena no la apago (%s)"
                      % r["enciende_despues"])
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

    #  5. EL MOTOR Y SU MANDO ARRANCAN EN EL MISMO NUMERO.
    #
    #     `AudioEngine::kFxDef` y `MainComponent::fxDefs[f].spec[p].def` son la
    #     misma regla escrita dos veces, y el mando se construye con
    #     `dontSendNotification` a proposito -no hay motor al que empujar
    #     todavia- asi que nadie los iguala nunca. Cuando se puso esta linea
    #     habia TRES que no cuadraban: DRV arrancaba con el drive en 0.0 y el
    #     tono en 20 kHz mientras su mando decia 0.55 y 8 kHz, y la FUERZA del
    #     de-esser en 0.0 con el mando en 0.5 — o sea un mando que se movia y
    #     no hacia nada, que es un fallo que esta casa ya ha pagado.
    print ("defectos el motor y la cara discrepan en %d de %d"
           % (r["defectos_cruzados"], r["tipos"] * 3))
    if r["defectos_cruzados"] != 0:
        malas.append ("%d parametros arrancan con un numero en el motor y otro en "
                      "el mando" % r["defectos_cruzados"])

    #  5b. Y LOS DIECISEIS CANALES NACEN EN EL MISMO SITIO.
    #
    #      La regla 3 -«un tipo, una ranura»- NO cambia con los canales: dentro
    #      de un canal sigue habiendo una ranura por tipo, y eso va escrito
    #      aqui para que nadie la «arregle» leyendo la tanda al reves. Lo que
    #      esa tanda anade es su segunda mitad: desde que un inserto es de cada
    #      canal, `fxP` son dieciseis filas de sesenta y tres numeros y las
    #      quince de detras no las mira nadie hasta que alguien abre ese canal.
    #
    #      Un `std::array` con `{}` las deja a CERO, y cero es un valor valido
    #      en casi todos los parametros: el compresor del canal 7 abriria con
    #      umbral 0 dB y ratio 1 -o sea sin comprimir- mientras la ficha dice
    #      lo que dice el canal 0. Es el fallo de `notaViva` y el del cero de
    #      `padAncho`, contado en 1008 casillas.
    print ("nacer    de %d (canal, tipo) arrancan distintos del canal 0: %d"
           % (r["canales"] * r["tipos"], r["canales_raros"]))
    if r["canales_raros"] != 0:
        malas.append ("%d de %d (canal, tipo) arrancan con otro numero que el "
                      "canal 0: los quince que nadie ha tocado tienen que nacer "
                      "donde nace el cero"
                      % (r["canales_raros"], r["canales"] * r["tipos"]))

    #  5c. EL GESTO ENTERO CONTRA EL AUDIO — la medida que no tenia nadie.
    #
    #      Del telefono: «cuando inserto un efecto en uno de los slots, hasta
    #      que no voy al mixer, a rack, y toco el fader de 0 a 100, no puedo
    #      tocar los parametros; es como que estan bloqueados». Y el banco
    #      entero dijo que si con el fallo dentro, por una razon que se puede
    #      poner en columnas:
    #
    #        medida                       gesto     mide audio   abre el envio
    #        auditCanales 5c + telefono   casi        NO            no
    #        auditRanuras 1-4             SI          NO            no
    #        auditRack capa viva          casi        si            SI
    #        StressTest un inserto        no          si            SI
    #        auditFxPresets               casi        si            SI
    #
    #      «Mide audio» y «no abre el envio» no coinciden en NINGUNA fila. Las
    #      tres que rinden bloques se ponen el envio y el canal del pad a mano
    #      -el andamio de `enCanalCero`- y por eso no pueden ver un camino que
    #      nace cerrado: *una prueba que se adapta al defecto deja de medirlo.*
    #      Y las cuatro cifras de arriba son de ESTADO: la 1 dice «no mueve un
    #      decibelio» y lo que mira es un booleano de la cara.
    #
    #      Esta junta las dos mitades: dos toques -el «+» de la ranura 0 y la
    #      celda de DRV- y a partir de ahi NADA a mano. Por eso `gesto_envio` y
    #      `gesto_canal_pad` son veredictos y no adorno: son lo que el gesto
    #      tiene que sembrar solo.
    print ()
    print ("gesto    foco %s (DRV %s)  mandos %s/3  pad al canal %s  envio %.2f"
           % (r["gesto_foco"], r["gesto_drv"], r["gesto_mandos"],
              r["gesto_canal_pad"], r["gesto_envio"]))
    print ("         el audio cambia en %.2f%% de las muestras  (rms %.4f -> %.4f)"
           % (r["gesto_cambia"], r["gesto_rms_antes"], r["gesto_rms_despues"]))
    print ("         y mover CTRL 1 lo cambia otro %.2f%%" % r["mando_cambia"])
    if r["gesto_foco"] != r["gesto_drv"]:
        malas.append ("poner un efecto en una ranura dejo los tres mandos "
                      "apuntando al tipo %s y no al %s que se acaba de poner"
                      % (r["gesto_foco"], r["gesto_drv"]))
    if r["gesto_mandos"] != 3:
        malas.append ("despues del gesto solo %d de los 3 mandos se pueden "
                      "tocar: el efecto esta puesto y sus parametros estan "
                      "bloqueados" % r["gesto_mandos"])
    if r["gesto_canal_pad"] != 4:
        malas.append ("poner un efecto no metio el pad elegido en el canal "
                      "(%s): el envio esta al 100 y no le llega una muestra"
                      % r["gesto_canal_pad"])
    if r["gesto_envio"] < 0.99:
        malas.append ("poner un efecto dejo su envio en %.2f y no al maximo"
                      % r["gesto_envio"])
    #  EL LISTON ES EL DE `un inserto cambia el audio` de `StressTest` -2 %- y
    #  no uno nuevo: *el liston de una prueba no se reinventa en la de al lado*.
    if r["gesto_cambia"] < MIN_CAMBIA:
        malas.append ("el gesto entero -poner DRV en la ranura 0 y nada mas- "
                      "cambio el %.2f%% de las muestras, por debajo del %.1f%%: "
                      "la tapa se pinta LLENA y no mueve un decibelio"
                      % (r["gesto_cambia"], MIN_CAMBIA))
    if r["mando_cambia"] < MIN_CAMBIA:
        malas.append ("mover CTRL 1 despues del gesto cambio el %.2f%% de las "
                      "muestras, por debajo del %.1f%%: el mando se mueve y no "
                      "llega al efecto que acabas de poner"
                      % (r["mando_cambia"], MIN_CAMBIA))

    #      Y LA SEGUNDA GRIETA, que el gesto de arriba NO puede ver: el
    #      `setEnabled` de los tres mandos vivia en `refrescaRanuras` y
    #      `focusFx` no la llama. Poniendo un efecto eso no se nota -
    #      `ponEnRanura` termina en `refrescaRanuras` de todos modos- asi que
    #      hace falta un camino que mueva el foco sin pasar por ella: MANTENER
    #      pulsada una ranura llena. Y hace falta llegar con los mandos
    #      apagados, o la cifra no puede fallar; de ahi `manten_apagados`, que
    #      es la mitad sin la que esto seria una linea que imprime OK.
    print ("manten   se llega con %s/3 apagados  ->  foco %s  mandos %s/3"
           % (3 - r["manten_apagados"], r["manten_foco"], r["manten_mandos"]))
    if r["manten_apagados"] != 0:
        malas.append ("la medida de mantener no llego con los tres mandos "
                      "apagados (%s/3 encendidos): no mide nada"
                      % r["manten_apagados"])
    if r["manten_foco"] != r["gesto_drv"]:
        malas.append ("mantener una ranura llena no movio el foco (%s)"
                      % r["manten_foco"])
    if r["manten_mandos"] != 3:
        malas.append ("mantener una ranura llena movio el foco y dejo %d de 3 "
                      "mandos tocables: el efecto esta puesto, es el que tiene "
                      "los mandos, y estan bloqueados" % r["manten_mandos"])

    #  6. LA REJILLA DEL MENU, EN LAS SIETE PANTALLAS — Y NO SOLO LA DE HOY.
    #
    #     Con once tipos la rejilla son tres columnas y cuatro filas en las
    #     siete, o sea que la regla que la decide no se puede VER fallar. El
    #     dia que sean veintiuno son SIETE filas, y siete filas piden 460 px
    #     con VACIAR contra los **370** que da una tarjeta apaisada: las filas
    #     reales salen 40,40,40,40,40,22,0 — la sexta por debajo del dedo y la
    #     septima de 0x0—. Y esta ficha NO se desplaza, asi que lo que no cabe
    #     no se alcanza arrastrando.
    #
    #     Se pregunta a la funcion de verdad -la misma que llama `resized()`,
    #     no una formula repetida aqui- por el numero de hoy y por veintiuno,
    #     con la geometria de cada pantalla. Lo que se exige de las dos: que lo
    #     pedido quepa en la tarjeta y que la celda llegue al dedo.
    print()
    #  Y LA SEGUNDA COLUMNA ES «CON UNO MAS» Y NO «A 21»: un numero clavado
    #  en la prueba vale lo mismo que uno clavado en el codigo, y este valia
    #  `kNumFx` el dia que se escribio. Lo que hay que saber es si el menu
    #  sigue cabiendo el dia que entre el siguiente tipo.
    tipos = r["tipos"]
    print ("pantalla   hoy (%d)       con uno mas (%d)  tope" % (tipos, tipos + 1))
    for size in SIZES:
        g = corre ({"ZATI_SIZE": size})
        if g is None:
            malas.append ("%s no contesto" % size);  continue
        tope = g["tope_tarjeta"]
        linea = "%-10s %-14s %-14s %4d" % (size, g["forma"], g["formaMas"], tope)
        for cual in ("forma", "formaMas"):
            cols, filas, pide, celda = leeForma (g[cual])
            if pide > tope:
                malas.append ("%s: %s pide %d px y la tarjeta da %d"
                              % (size, g[cual], pide, tope));  linea += "  <--"
            if celda < DEDO:
                malas.append ("%s: %s deja la celda en %d px de ancho"
                              % (size, g[cual], celda));  linea += "  <--"
            if filas < 2:
                malas.append ("%s: %s es UNA fila, que es lo que este menu "
                              "existe para no ser" % (size, g[cual]));  linea += "  <--"
        print (linea)

    print()
    #  ============ LOS SEIS ROTULOS DE FAMILIA ============
    #
    #  El menu son treinta tapas y el orden YA las agrupaba -seis familias de
    #  cinco, `MainComponent::ordenFx`- pero los nombres vivian en un
    #  comentario del codigo fuente, o sea en el unico sitio donde no los lee
    #  quien tiene el telefono en la mano.
    #
    #  Y no basta con contar cuantos hay: un rotulo sobre la fila equivocada
    #  pasa esa cuenta y ademas MIENTE -diria que FLT es una saturacion-, asi
    #  que lo que se mide es que cada banda quede por encima de la primera tapa
    #  de SU fila. Los seis nombres van literales: una prueba que lee la tabla
    #  que juzga cambia de opinion a la vez que el fallo.
    ESPERADAS = ["FILTRO", "SATURACION", "MODULACION", "ESPACIO", "DINAMICA", "TIEMPO"]
    fam = [t.split (":") for t in r.get ("familias", "").split (",") if ":" in t]
    nombres = [t[0] for t in fam]
    sobre = r.get ("bandas_sobre", 0)
    if nombres != ESPERADAS:
        malas.append ("las familias del menu son %s y no %s" % (nombres, ESPERADAS))
    if sobre != len (ESPERADAS):
        malas.append ("solo %d de %d rotulos de familia estan sobre su fila"
                      % (sobre, len (ESPERADAS)))
    #  Y CERRADO EL MENU, NINGUNA BANDA SOBREVIVE. Son bandas pintadas: sin
    #  componente que apagar, una que se quede con los limites de la ultima vez
    #  se pinta encima de lo que haya debajo. Es la septima regla del banco.
    if r.get ("bandas_zombis", -1) != 0:
        malas.append ("%s bandas de familia siguen puestas con el menu cerrado"
                      % r.get ("bandas_zombis", "?"))
    print ("familias del menu de efectos: %s   %d de %d sobre su fila, %s zombis"
           % (", ".join (nombres) if nombres else "NINGUNA",
              sobre, len (ESPERADAS), r.get ("bandas_zombis", "?")))

    print()
    if malas:
        for m in malas: print ("FALLA  " + m)
        return 1
    print ("las seis ranuras: el menu, la accion unica, un tipo una ranura, "
           "vaciar apaga, los defectos cuadran y la rejilla cabe a %d y a %d"
           % (tipos, tipos + 1))
    return 0


if __name__ == "__main__":
    sys.exit (main())

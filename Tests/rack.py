#!/usr/bin/env python3
# ============================================================================
#  LO QUE LA APP DICE DE UN EFECTO: de que familia es, y que hay dentro.
#
#  De los veintiun tipos, DIECISEIS son insertos: `renderNextBlock` hace
#  `if (fxSustituye[f]) dry *= (1.0f - g)`, o sea que subir ese fader le QUITA
#  senal seca al pad. Solo DLY y REV suman encima. El rack titulaba su fila
#  «cuanto de este pad entra en cada efecto», que describe un envio: con DRV al
#  50 % oyes mitad sucio y mitad limpio y con DLY al 50 % oyes el pad ENTERO
#  mas un eco.
#
#  Y ninguno decia lo que hay dentro. Tres mandos dicen lo que le has PEDIDO al
#  efecto y ninguno lo que esta HACIENDO; el EQ contesta esa pregunta con su
#  curva en el plato, y los otros diez no contestaban nada. La miniatura vive
#  donde ya vive esa respuesta: en el PLATO, al lado de los tres mandos. Estuvo
#  una tanda en la fila del rack y se deshizo mirando la foto — flotaba encima
#  del fader y desordenaba la fila.
#
#  NINGUNA DE LAS DIEZ REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Un visor que
#  dibuja siempre lo mismo se maqueta perfecto: no solapa, no se sale, no corta
#  el rotulo, no mide cero y esta traducido. Es la familia de los cinco fallos
#  del compas del piano, otra vez.
#
#      python3 Tests/rack.py
# ============================================================================
import json, os, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import display_alive                                    # noqa: E402

#  El dedo minimo, importado y no escrito otra vez.
from expo import MIN_TOUCH                                        # noqa: E402


def corre (tam="412x915"):
    """Con HOME propio: con el de verdad la app restaura la sesion que hubiera
    y las ranuras no serian las que esta prueba pone."""
    casa = tempfile.mkdtemp (prefix="zati-rack-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=tam, ZATI_LANG="es",
                ZATI_RACK="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return None
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("rack"): return r
    return None


def main ():
    if not os.path.exists (APP):
        print ("no hay binario: cmake --build build --target Zati"); return 1
    if not display_alive():
        print ("no hay DISPLAY vivo"); return 1

    r = corre()
    if r is None:
        print ("FALLA  la app no publico la linea del rack"); return 1

    malo = []

    #  1. LA FAMILIA, EN LOS ONCE TIPOS.
    #
    #  Lo que se compara es lo que la fila del rack ACABA diciendo -su nombre
    #  accesible, que es lo que lee TalkBack y lo unico que la app afirma sobre
    #  esto- contra lo que el MOTOR dice. Preguntarle dos veces a
    #  `AudioEngine::sustituye` compararia la tabla consigo misma y saldria
    #  verde con la fila diciendo lo que le diera la gana.
    print ("familia    %d de %d filas mal   dibujo %s"
           % (r["mal"], r["tipos"], r["dibujo"]))
    if r["mal"]:
        malo.append ("%d de %d filas dicen lo que no son" % (r["mal"], r["tipos"]))

    #  Y que no sean las once iguales, que es la otra forma de escribirlo mal:
    #  «cero desacuerdos» lo cumple tambien una tabla que dice que si a todo si
    #  el dibujo tambien dice que si a todo.
    fam = set (r["dibujo"])
    print ("           %d insertos y %d envios"
           % (r["dibujo"].count (1), r["dibujo"].count (0)))
    if len (fam) < 2:
        malo.append ("las once filas dicen lo mismo: no distinguen nada")

    #  1b. Y LA FORMA DEL MOTOR SALE DE ESA MISMA TABLA.
    #
    #  Los dieciseis canales no eligieron su numero: `fxSustituye` marca
    #  DIECISEIS insertos de los veintiuno, y de ahi salen los dieciseis
    #  `Inserto` del motor -uno por canal- y los `kNumFx + kNumIns *
    #  kNumCanales` buses. Los tres los DERIVA el codigo con `static_assert`
    #  detras, asi que lo unico que hace falta aqui es que la fila del rack y
    #  el reparto del motor cuenten lo mismo: el dia que un tipo nuevo entre
    #  como envio y alguien lo sume a `kNumIns` a mano, esto lo dice.
    print ("forma      %d insertos · %d envios · %d canales · %d buses"
           % (r["insertos"], r["tipos"] - r["insertos"], r["canales"], r["buses"]))
    if r["insertos"] != r["dibujo"].count (1):
        malo.append ("el motor reserva %d insertos y la fila del rack cuenta %d"
                     % (r["insertos"], r["dibujo"].count (1)))
    if r["buses"] != r["tipos"] + r["insertos"] * r["canales"]:
        malo.append ("el motor dice %d buses y %d + %d x %d son %d"
                     % (r["buses"], r["tipos"], r["insertos"], r["canales"],
                        r["tipos"] + r["insertos"] * r["canales"]))

    #  2. EL VISOR LEE LOS NUMEROS DE AHORA, con DOS cifras.
    #
    #  Solo la primera la cumple una miniatura que dibuja ruido; solo la
    #  segunda, un icono fijo — que es exactamente lo que habia antes.
    #
    #  Y SE MIDE MOVIENDO EL MANDO, que es lo que esta comprobacion no hacia:
    #  escribia el parametro y llamaba a la funcion que rehace la curva, o sea
    #  a la unica que no le faltaba nada. Con un dedo, la curva se quedaba en
    #  la de antes hasta que salias del efecto y volvias. Ver
    #  MainComponent::refrescaVisorPlato.
    print ("visor      %d de %d cambian al mover un mando, %d de %d quietos sin tocar nada"
           % (r["cambian"], r["tipos"], r["quietos"], r["tipos"]))
    if r["cambian"] != r["tipos"]:
        malo.append ("%d de %d visores no se actualizan al mover el mando"
                     % (r["tipos"] - r["cambian"], r["tipos"]))
    if r["quietos"] != r["tipos"]:
        malo.append ("%d de %d visores cambian sin que nadie toque nada"
                     % (r["tipos"] - r["quietos"], r["tipos"]))

    #  3. Y MANDO A MANDO, contra la lista que la app DECLARA.
    #
    #  La comprobacion de arriba movia los tres y le bastaba con que uno
    #  cambiara el dibujo, asi que CINCO mandos que no mueven nada llevaban ahi
    #  desde el primer dia: el TONE de DRV, el RATE de BIT, el FREQ del
    #  de-esser, el CIERRE de la puerta y el SOLTAR del limitador. Lo que se
    #  compara es lo MEDIDO contra `FxVisor::mandosDe`, que es donde la app
    #  dice cuales caben en el eje de cada visor y cuales no — escribir esa
    #  lista aqui serian dos reglas, y la del script solo sabria medir una de
    #  las cuatro compilaciones.
    NOM = ("ninguno", "el 1", "el 2", "los dos")
    print ("mandos     medido %s" % r["medidos"])
    print ("           dicho  %s" % r["dichos"])
    for f, (me, di) in enumerate (zip (r["medidos"], r["dichos"])):
        if me == -1:
            continue                       # el EQ trae su cara y no un visor
        if me != di:
            malo.append ("el tipo %d dice que mueven %s y mueven %s"
                         % (f, NOM[di], NOM[me]))
    if not r["discrepan"]:
        print ("           %d tipos: lo que mueve el visor es lo que la app declara"
               % (r["tipos"] - r["medidos"].count (-1)))

    #  4. LA CAPA VIVA, con DOS cifras o no vale.
    #
    #  Lo de arriba dice que el dibujo sale de la formula que suena. Lo que no
    #  decia es si por ese bus esta pasando algo AHORA: un visor perfecto de un
    #  bus mudo se lee igual que uno de un bus trabajando.
    #
    #  Y las dos cifras porque separan las dos formas de escribirlo mal, y las
    #  dos se dibujan preciosas: una capa que pinta RUIDO se mueve con señal y
    #  tambien sin ella, y una que es un adorno no se mueve con ninguna.
    print ("vivo       %d de %d se mueven con señal, %d de %d quietos sin ella"
           % (r["mueven"], r["medibles"], r["quietos_sin"], r["medibles"]))
    if r["mueven"] != r["medibles"]:
        malo.append ("%d de %d capas vivas no se mueven con señal: son un adorno"
                     % (r["medibles"] - r["mueven"], r["medibles"]))
    if r.get ("vivo_mudo") or r.get ("vivo_ruido"):
        print ("           mudos %s   ruidosos %s"
               % (r.get ("vivo_mudo"), r.get ("vivo_ruido")))
    if r["quietos_sin"] != r["medibles"]:
        malo.append ("%d de %d capas vivas se mueven sin señal: dibujan ruido"
                     % (r["medibles"] - r["quietos_sin"], r["medibles"]))

    #  5. Y LO QUE EL VISOR CUESTA, EN LA PANTALLA MAS ESTRECHA.
    #
    #  Vive en el PLATO, al lado de los tres mandos, que son lo unico que se
    #  toca ahi: no puede costarles un pixel de dedo. Y se mide en 280x653 y no
    #  en un movil grande, que es donde la regla puede FALLAR — con 412 px de
    #  ancho el tope de 120 protege al mando el solo, asi que el suelo no
    #  decide nada y romperlo a proposito seguia saliendo verde. Una regla que
    #  no puede fallar donde se mide es una linea que imprime OK.
    for tam in ("412x915", "280x653"):
        g = r if tam == "412x915" else corre (tam)
        if g is None:
            malo.append ("la app no publico la linea del rack en %s" % tam); continue
        fw, fh = g["fader"]
        mw, mh = g["mini"]
        kw, kh = g["mando"]
        print ("%-9s visor %dx%d   mando %dx%d   fader del rack %dx%d"
               % (tam, mw, mh, kw, kh, fw, fh))
        if fh < MIN_TOUCH:
            malo.append ("%s: el fader del rack queda en %d px de alto, por debajo del dedo de %d"
                         % (tam, fh, MIN_TOUCH))
        if min (kw, kh) < MIN_TOUCH:
            malo.append ("%s: el mando del plato queda en %dx%d, el visor le come el dedo"
                         % (tam, kw, kh))
        if mh < 24 or mw < 48:
            malo.append ("%s: el visor queda en %dx%d y no se lee" % (tam, mw, mh))
        #  Y LA TAPA DE APAGAR, que es lo que esta fila acaba de ganar. Lo que
        #  cuesta sale del FADER -de 163 px a 117 en la tarjeta mas estrecha- y
        #  eso es lo que se comprueba: que la tapa mida un dedo y que el fader
        #  siga midiendo uno. Un `Metrics::hit` clavado la dejaba en 38 px de
        #  ancho, que el `reduced (1, 4)` de la fila se lleva dos.
        uw, uh = g["mute"]
        if min (uw, uh) < MIN_TOUCH:
            malo.append ("%s: la tapa de apagar del rack queda en %dx%d" % (tam, uw, uh))
        if fw < MIN_TOUCH:
            malo.append ("%s: el fader del rack queda en %d px de ancho" % (tam, fw))

    #  6. Y LA MARCA QUE LO DICE MIRANDO, que es lo que faltaba de verdad.
    #
    #  Lo de arriba es el nombre ACCESIBLE — lo que lee TalkBack — y hasta hoy
    #  era lo unico: un ojo humano no tenia nada en la fila que le dijera si
    #  esa fila resta seco o lo suma encima. Se intento dos veces y las dos se
    #  deshicieron mirando la foto (la pista del fader pintada entera y el
    #  visor dentro de la fila), asi que esta vez es una MARCA en el sitio del
    #  dibujo del tipo: el nombre del canalon ya dice CUAL es el efecto.
    #
    #  Y son dos caminos distintos de la app — el titulo lo escribe
    #  `refreshRack` y la marca `refrescaRanuras` — asi que compararlos es lo
    #  que separa «la fila lo dice» de «una de las dos ventanas se quedo
    #  vieja». Que las dos marcas no sean el MISMO dibujo lo mide `iconos.py`
    #  con el listón de siempre.
    print ("marca      %s" % r["marcas"])
    if -1 in r["marcas"]:
        malo.append ("hay filas del rack sin marca de familia: %s" % r["marcas"])
    elif r["marcas"] != r["dibujo"]:
        malo.append ("la marca del canalon y lo que la fila DICE no coinciden:\n"
                     "           marca  %s\n           dice   %s"
                     % (r["marcas"], r["dibujo"]))

    #  7. MUTEAR DESDE EL RACK, con DOS cifras y por la TAPA.
    #
    #  Se pidio «al lado del boton del plugin, una opcion para sustituirlo o
    #  MUTEARLO tambien»: sustituir y vaciar ya se hacian desde el canalon, y
    #  apagar seguia siendo exclusivo de la fila de la cara y del XY — el rack
    #  PINTABA el estado (el canalon con el acento, el fader al 50 % de alfa) y
    #  no dejaba tocarlo.
    #
    #  Y las DOS mitades, porque una sola se engaña: que la tapa APAGUE y que
    #  el canalon SIGA abriendo el menu. Convertir el canalon en interruptor
    #  cumple la primera y se lleva por delante la unica puerta que hay para
    #  cambiar o vaciar una ranura.
    print ("mute       %d -> %d -> %d   sobre vacia %s   el canalon abre el menu %d"
           % (r["mute_antes"], r["mute_despues"], r["mute_vuelve"],
              r["mute_vacia"], r["menu_abre"]))
    if not (r["mute_antes"] == 1 and r["mute_despues"] == 0 and r["mute_vuelve"] == 1):
        malo.append ("la tapa del rack no apaga el efecto: %d -> %d -> %d"
                     % (r["mute_antes"], r["mute_despues"], r["mute_vuelve"]))
    if r["mute_vacia"][0] != r["mute_vacia"][1]:
        malo.append ("sobre una ranura VACIA la tapa apaga algo: %s" % r["mute_vacia"])
    if r["menu_abre"] != 1:
        malo.append ("el canalon dejo de abrir el menu: el rack se queda sin la "
                     "unica puerta para cambiar o vaciar una ranura")

    #  6. EL CRISTAL DEL PLATO SIN EFECTO, en PIXELES.
    #
    #  `FxMini::paint` se rendia arriba del todo con `fx < 0` -o sea en una
    #  instalacion limpia, con las seis ranuras vacias- asi que el visor
    #  quedaba VISIBLE, colocado en 112x74 y sin pintar un pixel: al lado de
    #  los tres mandos se veia el plato pelado. Un rectangulo vacio se lee como
    #  una pieza que falta, y esa fue la queja: «aunque sea una pantalla negra,
    #  para que no este el hueco ese ahi».
    #
    #  DOS CIFRAS, que una sola se engaña por los dos lados: solo «vacio > 0»
    #  lo cumple tambien un visor que se quedo dibujando la curva del efecto
    #  anterior, y solo «vacio distinto de puesto» lo cumple el fallo de hoy,
    #  que es cero contra algo. Juntas dicen que hay cristal y que el cristal
    #  no es la curva.
    print ("cristal    sin efecto %d px pintados, y con un efecto puesto cambian %d"
           % (r["visor_vacio"], r["visor_dif"]))
    if r["visor_vacio"] <= 0:
        malo.append ("el plato sin efecto no pinta un pixel: donde va el visor "
                     "queda un hueco (%d px)" % r["visor_vacio"])
    if not (0 < r["visor_dif"] < r["visor_vacio"]):
        malo.append ("poner un efecto cambia %d px de %d: el cristal vacio no es "
                     "un cristal o es ya la curva" % (r["visor_dif"], r["visor_vacio"]))

    if malo:
        for m in malo: print ("FALLA  " + m)
        return 1
    print ("cada efecto dice de que familia es, el plato ensena lo que hay dentro "
           "y desde el rack se cambia Y se apaga")
    return 0


if __name__ == "__main__":
    sys.exit (main())

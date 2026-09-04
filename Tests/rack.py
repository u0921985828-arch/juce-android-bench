#!/usr/bin/env python3
# ============================================================================
#  LO QUE LA APP DICE DE UN EFECTO: de que familia es, y que hay dentro.
#
#  De los once tipos, NUEVE son insertos: `renderNextBlock` hace
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

    #  2. EL VISOR LEE LOS NUMEROS DE AHORA, con DOS cifras.
    #
    #  Solo la primera la cumple una miniatura que dibuja ruido; solo la
    #  segunda, un icono fijo — que es exactamente lo que habia antes.
    print ("visor      %d de %d cambian al mover un mando, %d de %d quietos sin tocar nada"
           % (r["cambian"], r["tipos"], r["quietos"], r["tipos"]))
    if r["cambian"] != r["tipos"]:
        malo.append ("%d de %d visores no cambian: son un icono"
                     % (r["tipos"] - r["cambian"], r["tipos"]))
    if r["quietos"] != r["tipos"]:
        malo.append ("%d de %d visores cambian sin que nadie toque nada"
                     % (r["tipos"] - r["quietos"], r["tipos"]))

    #  3. Y LO QUE EL VISOR CUESTA, EN LA PANTALLA MAS ESTRECHA.
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

    if malo:
        for m in malo: print ("FALLA  " + m)
        return 1
    print ("cada efecto dice de que familia es y el plato ensena lo que hay dentro")
    return 0


if __name__ == "__main__":
    sys.exit (main())

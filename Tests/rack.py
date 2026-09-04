#!/usr/bin/env python3
# ============================================================================
#  LA FILA DEL RACK: de que familia es cada una, y que hay dentro.
#
#  De los once tipos, NUEVE son insertos: `renderNextBlock` hace
#  `if (fxSustituye[f]) dry *= (1.0f - g)`, o sea que subir ese fader le QUITA
#  senal seca al pad. Solo DLY y REV suman encima. El rack dibujaba las once
#  filas identicas y las titulaba «cuanto de este pad entra en cada efecto»,
#  que describe un envio: dos gestos identicos con dos significados, y con DRV
#  al 50 % oyes mitad sucio y mitad limpio mientras que con DLY al 50 % oyes el
#  pad ENTERO mas un eco.
#
#  Y la fila no decia nada de lo que hay dentro. El EQ trae su propia cara
#  -curva, analizador, cinco tipos de banda- y en el rack era un fader
#  identico al de BIT. La pregunta que una fila de rack tiene que contestar es
#  «¿que le estoy mandando a esto?» y contestaba «un numero».
#
#  NINGUNA DE LAS DIEZ REGLAS DE `expo.py` PUEDE VER NADA DE ESTO. Una fila que
#  dibuja un envio donde hay un inserto se maqueta perfecta: no solapa, no se
#  sale, no corta el rotulo, no mide cero y esta traducida. Es la familia de
#  los cinco fallos del compas del piano, otra vez.
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


def corre ():
    """Con HOME propio: con el de verdad la app restaura la sesion que hubiera
    y las ranuras no serian las que esta prueba pone."""
    casa = tempfile.mkdtemp (prefix="zati-rack-")
    env = dict (os.environ, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="412x915", ZATI_LANG="es",
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
    #  Lo que se compara es lo que la fila ACABA teniendo puesto -la propiedad
    #  que lee el pintor- contra lo que el MOTOR dice. Preguntarle dos veces a
    #  `AudioEngine::sustituye` compararia la tabla consigo misma y saldria
    #  verde con el rack dibujando lo que le diera la gana.
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
        malo.append ("las once filas se dibujan igual: no distinguen nada")

    #  2. LA MINIATURA LEE LOS NUMEROS DE AHORA, con DOS cifras.
    #
    #  Solo la primera la cumple una miniatura que dibuja ruido; solo la
    #  segunda, un icono fijo — que es exactamente lo que habia antes.
    print ("miniatura  %d de %d cambian al mover un mando, %d de %d quietas sin tocar nada"
           % (r["cambian"], r["tipos"], r["quietos"], r["tipos"]))
    if r["cambian"] != r["tipos"]:
        malo.append ("%d de %d miniaturas no cambian: son un icono"
                     % (r["tipos"] - r["cambian"], r["tipos"]))
    if r["quietos"] != r["tipos"]:
        malo.append ("%d de %d miniaturas cambian sin que nadie toque nada"
                     % (r["tipos"] - r["quietos"], r["tipos"]))

    #  3. Y LO QUE LA FILA PAGA POR DIBUJAR. El alto sale del desplazamiento
    #     -`rackSheet.hazDesplazable`- asi que la miniatura no puede costarle un
    #     pixel al fader, que es lo que se arrastra.
    fw, fh = r["fader"]
    mw, mh = r["mini"]
    print ("fila       fader %dx%d   miniatura %dx%d" % (fw, fh, mw, mh))
    if fh < MIN_TOUCH:
        malo.append ("el fader queda en %d px de alto, por debajo del dedo de %d" % (fh, MIN_TOUCH))
    if mh < 12 or mw < 40:
        malo.append ("la miniatura queda en %dx%d: no se lee" % (mw, mh))

    if malo:
        for m in malo: print ("FALLA  " + m)
        return 1
    print ("el rack dice de que familia es cada fila y ensena lo que hay dentro")
    return 0


if __name__ == "__main__":
    sys.exit (main())

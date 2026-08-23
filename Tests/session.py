#!/usr/bin/env python3
# ============================================================================
#  QUE LA SESION VUELVA ENTERA.
#
#  Las otras pruebas miran la cara y el motor. Esta mira lo unico que la
#  persona no puede recuperar si se pierde: los sonidos que tenia puestos.
#
#  Existe porque habia un pad que no volvia, y no volvia SIEMPRE el mismo -el
#  primero- y no siempre: cuatro arranques de cada cinco. Ninguna prueba podia
#  verlo, porque las dos mitades del banco miran una sola corrida y este es un
#  fallo de carrera entre dos hilos. La causa era que juce::File::createDirectory
#  se da por vencido cuando el mkdir devuelve EEXIST -o sea cuando OTRO hilo
#  acaba de crear ese mismo componente del camino-, asi que el hilo de sesion
#  se iba sin crear .sesion/samples mientras el de mensajes creaba .sesion. El
#  stream se abria con ENOENT, writeSample devolvia false, y nadie mira ese
#  false.
#
#  La prueba es de repeticion y no de una vez: se arranca N veces con una casa
#  limpia y se cuenta. Una sola corrida verde no dice nada de una carrera.
#
#      python3 Tests/session.py [veces]
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
TMP  = os.path.join (ROOT, "build", ".session-test")

#  SESENTA Y CUATRO, no dieciseis. Desde que la app trae sonidos de fabrica
#  (Kits.h) el arranque llena los cuatro bancos, y ZATI_DEMO se limita a
#  reescribir el primero. Mejor para esta prueba: cuatro veces mas ficheros
#  escritos a la vez es cuatro veces mas probable pillar la carrera que la
#  motivo.
EXPECTED = 64
RUNS = int (sys.argv[1]) if len (sys.argv) > 1 else 8


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def one (home, extra_env):
    env = dict (os.environ, HOME=home, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    env.update (extra_env)
    out = subprocess.run ([APP], env=env, capture_output=True, timeout=180)
    return out.stdout.decode ("utf8", "replace")


def proyecto():
    """GUARDAR UN PROYECTO Y VOLVER A ABRIRLO, que es lo que hace la persona.

    El fallo de los bancos altos clonados -la mascara de pasos era de 32 bits
    con 64 pads, asi que el pad 32 escribia el bit del pad 0- se arreglo en
    captureState/applyState, y la prueba que lo cubria pasaba por el
    autoguardado de SESION, que es otro camino. Si el arreglo se hubiera caido
    solo en el del proyecto, la prueba habria seguido en verde y la persona
    habria seguido viendo su banco A clonado en el C. Se mide el camino que se
    usa: un paso en el pad 0 de cada banco, guardar, vaciar, abrir."""
    env = dict (os.environ)
    env.update ({"ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                 "ZATI_PROJ": "1"})
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    except subprocess.TimeoutExpired:
        return None
    #  DOS lineas y cada una con su clave. La segunda -lo disperso- se llamaba
    #  tambien "proyecto" y esta funcion devolvia la primera que encontrara: la
    #  del acorde, que no lleva paso0, asi que el banco anunciaba que se perdian
    #  los bancos altos mientras la app los devolvia perfectos. La prueba mintio
    #  antes de acertar, otra vez.
    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "paso0" in d:  filas["pasos"] = d
        if "disperso" in d: filas["disperso"] = d
    return filas or None


def viejos():
    """PROYECTOS DE OTRA EPOCA, abiertos con el binario de hoy.

    La regla esta escrita ocho veces en applyState -"lo que no esta en el
    fichero vale su defecto ANTIGUO y no el de hoy"- y no la comprobaba nadie:
    todos los caminos del banco guardan y leen con el mismo binario, asi que un
    defecto que cambie hoy se lleva por delante los proyectos de ayer sin que
    nada falle.

    Los tres ficheros viven en Tests/proyectos y son texto plano a proposito:
    congelados, no generados, porque generarlos con la app de hoy seria volver
    a medir la app de hoy contra si misma.

    Y la app pone ANTES un proyecto "de ayer" con todo movido -ganancia 0.2,
    pan 0.9, corte 300, reves, envios 0.75 y una cancion escrita- que es lo
    unico que separa "puso el defecto" de "no habia nada que heredar".
    """
    casa = tempfile.mkdtemp (prefix="zati-viejo-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_VIEJO": os.path.join (ROOT, "Tests", "proyectos")})
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'):
            continue
        try:
            d = json.loads (linea)
        except Exception:
            continue
        if "viejo" in d:
            filas[d["viejo"]] = d
    return filas or None


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")

    bad = 0
    for r in range (RUNS):
        home = os.path.join (TMP, "run%d" % r)
        shutil.rmtree (home, ignore_errors=True)
        os.makedirs (home, exist_ok=True)

        #  Primera corrida: se monta el kit y se cierra. Escribir la sesion es
        #  cosa del hilo de sesion, y el destructor le da hasta tres segundos.
        one (home, {"ZATI_DEMO": "1"})

        samples = os.path.join (home, "Music", "ZATI", ".sesion", "samples")
        wavs = sorted (f for f in os.listdir (samples)) if os.path.isdir (samples) else []
        #  Y que no quede ningun temporal a medias: si queda, un guardado se
        #  fue por el camino de error sin limpiar.
        leftovers = [f for f in wavs if not f.endswith (".wav")]
        wavs = [f for f in wavs if f.endswith (".wav")]

        #  Segunda corrida sobre la misma casa: la que de verdad importa, que
        #  es si la app se encuentra a si misma al volver.
        back = one (home, {})
        restored = -1
        for line in back.splitlines():
            if "recuperada" in line or "restored" in line or "已恢复" in line:
                for tok in line.replace ("[", " ").replace ("]", " ").split():
                    if tok.isdigit(): restored = int (tok); break

        ok = (len (wavs) == EXPECTED and not leftovers and restored == EXPECTED)
        if not ok:
            bad += 1
            missing = sorted (set ("pad%02d.wav" % (i + 1) for i in range (EXPECTED)) - set (wavs))
            print ("FALLA corrida %d: %d/%d WAV%s%s  recuperados=%d"
                   % (r + 1, len (wavs), EXPECTED,
                      "  faltan " + ",".join (missing) if missing else "",
                      "  sobran " + ",".join (leftovers) if leftovers else "",
                      restored))
        else:
            print ("corrida %d: %d WAV, %d recuperados" % (r + 1, len (wavs), restored))

    #  ------------------------------------------------------------------
    #  Y QUE UN TROCEADO VUELVA SIENDO UN TROCEADO.
    #
    #  Lo de arriba cuenta ficheros y pads, y con eso un AUTO CHOP pasaba: los
    #  dieciseis trozos volvian y sonaban bien. Lo que no volvia era la
    #  RELACION. Un troceado son N pads apuntando al MISMO buffer, y el disco no
    #  guarda punteros: se escribia un WAV por pad, asi que al volver eran N
    #  buffers distintos con el mismo contenido. Seguia sonando igual y habia
    #  dejado de ser un troceado - la onda sin hermanos que ensenar, N copias
    #  del break en memoria y N en disco.
    #
    #  Se mide con las dos cifras que lo delatan: cuantos WAV deja en disco un
    #  troceado de ocho (ocho menos, no ocho mas) y de que pad dice la app que
    #  sale cada uno al volver.
    home = os.path.join (TMP, "chop")
    shutil.rmtree (home, ignore_errors=True)
    os.makedirs (home, exist_ok=True)

    SLICES = 8
    one (home, {"ZATI_DEMO": "1", "ZATI_CHOPGO": str (SLICES)})

    samples = os.path.join (home, "Music", "ZATI", ".sesion", "samples")
    wavs = [f for f in os.listdir (samples) if f.endswith (".wav")] if os.path.isdir (samples) else []

    back = one (home, {})
    fuentes = []
    for line in back.splitlines():
        if line.startswith ('{"fuentes"'):
            fuentes = json.loads (line)["fuentes"]

    #  Los ocho primeros del banco salen del pad 0; el resto de si mismos.
    esperado_wavs = EXPECTED - (SLICES - 1)
    compartidos = [i for i in range (1, SLICES)] if len (fuentes) == 64 else []
    bien_compartidos = all (fuentes[i] == 0 for i in compartidos) if compartidos else False
    bien_propios     = all (fuentes[i] == i for i in range (SLICES, EXPECTED)) if len (fuentes) == 64 else False

    chop_ok = (len (wavs) == esperado_wavs and bien_compartidos and bien_propios)
    print()
    print ("troceado en %d: %d WAV (esperados %d)   fuentes %s"
           % (SLICES, len (wavs), esperado_wavs,
              "correctas" if (bien_compartidos and bien_propios) else "PERDIDAS"))
    if not chop_ok:
        print ("   fuentes leidas:", fuentes[:SLICES + 2] if fuentes else "ninguna")

    #  ------------------------------------------------------------------
    #  Y QUE EL PATRON NO SE COPIE SOLO DE UN BANCO A OTRO.
    #
    #  Una secuencia escrita en el banco A volvia TAMBIEN en el banco C al
    #  reabrir el proyecto. No era una copia: el fichero guardaba la mascara de
    #  pasos en un int de 32 bits con `1 << p`, y con sesenta y cuatro pads
    #  desplazar mas de 32 se toma modulo 32 - o sea que el pad 32 escribia el
    #  bit del pad 0. Los pads 0..15 son el banco A y los 32..47 el banco C:
    #  compartian los mismos dieciseis bits.
    #
    #  Se mide con un paso en el pad 0 y otro en el 32 - uno de A y uno de C -
    #  y se comprueba que al volver estan ESOS DOS y ningun otro. Con el fallo
    #  puesto, los dos pads salen encendidos en los cuatro sitios.
    home = os.path.join (TMP, "pat")
    shutil.rmtree (home, ignore_errors=True)
    os.makedirs (home, exist_ok=True)

    one (home, {"ZATI_DEMO": "1", "ZATI_STEPS": "0,32"})
    back = one (home, {})
    puestos = []
    for line in back.splitlines():
        if line.startswith ('{"pasos"'):
            puestos = json.loads (line)["pasos"]

    pat_ok = sorted (puestos) == [0, 32]
    print()
    print ("patron entre bancos: pads con paso %s   %s"
           % (sorted (puestos) if puestos else "ninguno",
              "correcto" if pat_ok else "SE COPIA SOLO"))

    #  Y EL CAMINO DEL PROYECTO, que es otro. Lo de arriba pasa por el
    #  autoguardado de sesion; guardar un proyecto y volver a abrirlo es una
    #  llamada distinta, y el fallo de los bancos clonados vivia justo ahi.
    pr = proyecto() or {}
    pasos = pr.get ("pasos", {})
    proj_ok = pasos.get ("paso0") == [0, 16, 32, 48] and pasos.get ("paso5") == [48]
    print()
    print ("guardar proyecto y abrirlo: paso 0 en %s, paso 5 en %s   %s"
           % (pasos.get ("paso0", "?"), pasos.get ("paso5", "?"),
              "correcto" if proj_ok else "SE PIERDEN O SE CLONAN LOS BANCOS ALTOS"))

    #  Y LO QUE NO SE GUARDABA: acorde, empujon, bloqueo y largo. Cuatro cosas
    #  que la app sabia escribir y no sabia recordar - un acorde de cuatro notas
    #  volvia siendo una - y que hasta ahora solo se comprobaban a mano.
    d = pr.get ("disperso", {})
    disp_ok = (d.get ("nota") == 7 and d.get ("acorde") == [4, 12, -128]
               and d.get ("empujon") == -25 and d.get ("bloqueo") == 33
               and d.get ("largo") == 9
               #  Y los otros cuatro bloqueos, que viven EMPAQUETADOS en un
               #  uint32: cuatro valores distintos entre si, porque con cuatro
               #  iguales un cruce de bytes pasaria desapercibido.
               and d.get ("plock") == [11, 22, 44, 88])
    print ("acorde, empujon, bloqueo y largo: nota %s acorde %s empujon %s bloqueo %s largo %s"
           " bloqueos %s   %s"
           % (d.get ("nota", "?"), d.get ("acorde", "?"), d.get ("empujon", "?"),
              d.get ("bloqueo", "?"), d.get ("largo", "?"), d.get ("plock", "?"),
              "correcto" if disp_ok else "NO VUELVEN"))

    shutil.rmtree (TMP, ignore_errors=True)

    #  --- Y LOS PROYECTOS DE OTRA EPOCA ------------------------------------
    vj = viejos()
    viejo_ok = vj is not None and len (vj) == 3
    print()
    if vj:
        for nombre, d in sorted (vj.items()):
            #  Lo que el fichero NO trae vale su defecto ANTIGUO: un proyecto
            #  sin `sends` es anterior a que los envios existieran -cada pad iba
            #  entero a los seis- asi que vuelve con UNO y no con el cero de
            #  hoy. Igual el autocorte: sin la propiedad, puesto.
            bien = (abs (d["envio0"] - 1.0) < 0.01 and d["autocorte0"] == 1
                    and d["vel0"] == 127 and d["roll0"] == 1
                    #  Y lo que el fichero no menciona no se HEREDA del proyecto
                    #  anterior: el pad 20 no esta en ninguno de los tres, asi
                    #  que vuelve como nace -0.85, centrado, sin filtrar, del
                    #  derecho y sin envios- y no con el 0.2, el 0.9 y el 300
                    #  que la corrida anterior le dejo puestos.
                    and abs (d["gain20"] - 0.85) < 0.01 and abs (d["pan20"]) < 0.01
                    and d["corte20"] >= 19999 and d["reves20"] == 0
                    and abs (d["envio20"]) < 0.01
                    #  Y la cancion: sin <song> tiene que quedar VACIA. Con el
                    #  fallo, abrir un proyecto sin linea de tiempo dejaba
                    #  sonando el arreglo del que estuviera abierto.
                    and d["cancion"] == (1 if nombre.startswith ("02") else 0))
            viejo_ok = viejo_ok and bien
            print ("  %-20s envio0 %.2f  pad20 g%.2f p%.2f c%.0f r%d e%.2f  cancion %d  %s"
                   % (nombre, d["envio0"], d["gain20"], d["pan20"], d["corte20"],
                      d["reves20"], d["envio20"], d["cancion"],
                      "correcto" if bien else "HEREDA DEL ANTERIOR"))
    else:
        print ("  los proyectos congelados no volvieron")

    print()
    print ("las %d corridas devuelven la sesion entera" % RUNS if bad == 0
           else "%d de %d corridas pierden algo" % (bad, RUNS))
    #  Y LO DISPERSO CUENTA. Estaba calculado, impreso -"NO VUELVEN"- y FUERA
    #  del veredicto, asi que las nueve propiedades de paso que mas tarde se
    #  anadieron al fichero de proyecto -acorde, empujon, bloqueo de corte,
    #  largo y los cuatro empaquetados- podian dejar de volver sin que el banco
    #  dijera nada. Una comprobacion que no puede suspender es una linea que
    #  imprime OK.
    return 1 if (bad or not chop_ok or not pat_ok or not proj_ok or not disp_ok
                 or not viejo_ok) else 0


if __name__ == "__main__":
    sys.exit (main())

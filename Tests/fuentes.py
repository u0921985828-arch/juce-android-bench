#!/usr/bin/env python3
# ============================================================================
#  LA LISTA DE FUENTES ESTA ESCRITA DOS VECES.
#
#  El escritorio se compila con CMakeLists.txt y Android con Zati.jucer, que es
#  de donde Projucer genera el proyecto de Gradle en el CI. Son dos listas de
#  los mismos ficheros y NADIE las contrastaba, que es el fallo que este banco
#  lleva encontrando desde el principio con otras piezas -moduleBarFits contra
#  layoutModuleBar, captionOf contra drawButtonText-.
#
#  Se pago con Sintes.cpp: anadido a CMake, el escritorio compilaba, las 868
#  corridas del banco salian en verde y la APK moria en el enlazado veinte
#  minutos despues, con `undefined symbol: Sintes::tabla()`. Es la peor forma de
#  fallar de todas -local verde, remoto rojo- y ademas la mas cara, porque el CI
#  tarda lo que tarda.
#
#  Esta prueba cuesta un segundo y se corre ANTES de disparar la APK. No mide
#  nada del sonido ni de la pantalla: mide que los dos ficheros digan lo mismo.
#
#
#  Y LA LISTA DE MODULOS ESTA ESCRITA DOS VECES TAMBIEN, y esa costo una
#  corrida entera del CI.
#
#  Al subir JUCE de 8.0.4 a 8.0.15 el escritorio compilo limpio y las 25
#  pruebas salieron en verde, y ocho minutos despues Projucer se nego a
#  guardar: "At least one of your modules has missing dependencies!".
#  `juce_audio_processors` paso a depender de `juce_audio_processors_headless`
#  -un modulo que NO existe en 8.0.4- y el .jucer lleva las dependencias
#  escritas A MANO. CMake resuelve el cierre solo, asi que el escritorio no
#  puede ver el fallo: es exactamente el caso de Sintes.cpp de arriba con otra
#  pieza. Local verde, remoto rojo, ocho minutos.
#
#  Se contrasta el cierre TRANSITIVO que declaran los propios modulos de JUCE
#  contra la lista del .jucer, y ademas que MODULE y MODULEPATH digan lo mismo
#  -un modulo sin su ruta no se encuentra, y una ruta sin su modulo es basura
#  que nadie borra-.
#
#
#  Y UN NUMERO ESCRITO DONDE NADIE LO LEE, que es el mismo salto y la misma
#  clase de fallo. El .jucer decia `androidPluginVersion="8.9.1"` y ese
#  atributo NO EXISTE para Projucer: en jucer_PresetIDs.h, identico en 8.0.4 y
#  en 8.0.15, esta escrito
#
#      const Identifier androidPluginVersion ("gradleWrapperVersion");
#      // old name is very confusing, but we need to remain backward compatible
#
#  o sea que el AGP se pide con el atributo `gradleWrapperVersion` y lo que
#  hubiera en `androidPluginVersion` se ignora. El AGP de verdad era el DEFECTO
#  de JUCE - 8.4.1 con 8.0.4 y 8.13.2 con 8.0.15 -, y el 8.13.2 exige Gradle
#  8.13 mientras el .jucer pedia 8.11.1. Ahi murio la segunda corrida.
#
#  Lo que hace que esto sea de esta casa y no mala suerte: el numero llevaba
#  desde que se escribio sin efecto ninguno, y GOOGLE-PLAY.md y el comentario
#  del workflow lo citaban como si mandara. Un numero que nadie lee es una
#  afirmacion sin medida, que es lo que este banco lleva cazando desde el
#  principio.
#
#      python3 Tests/fuentes.py
# ============================================================================
import os, re, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))


def de_cmake():
    """Lo que target_sources(Zati PRIVATE ...) enumera."""
    t = open (os.path.join (ROOT, "CMakeLists.txt")).read()
    m = re.search (r"target_sources\(Zati PRIVATE(.*?)\)", t, re.S)
    if m is None: return None
    return set (re.findall (r"(Source/[\w./]+\.cpp)", m.group (1)))


def de_jucer():
    """Lo que Zati.jucer marca con compile=1. Los .h y los recursos no cuentan:
    un header no se compila y un FLAC es un recurso incrustado."""
    t = open (os.path.join (ROOT, "Zati.jucer")).read()
    out = set()
    for linea in t.splitlines():
        if "<FILE" not in linea or 'compile="1"' not in linea: continue
        m = re.search (r'file="(Source/[^"]+)"', linea)
        if m: out.add (m.group (1))
    return out



#  Donde esta el arbol de JUCE. En el escritorio lo baja FetchContent; en el CI
#  de la APK es el clon de la raiz. Si no esta, esta prueba NO puede medir el
#  cierre y lo dice: dar verde sin haber mirado es lo que este fichero existe
#  para no hacer.
JUCE = [os.path.join (ROOT, "build", "_deps", "juce-src", "modules"),
        os.path.join (ROOT, "JUCE", "modules")]


def arbol_juce():
    for d in JUCE:
        if os.path.isdir (d): return d
    return None


def dependencias (mods_dir, mod):
    """Lo que el propio modulo declara en su cabecera. La linea puede llevar
    comas y la seccion trae ademas OSXFrameworks y compañia, que no son
    modulos: se filtra por prefijo juce_."""
    h = os.path.join (mods_dir, mod, mod + ".h")
    if not os.path.exists (h): return None
    txt = open (h, encoding="utf-8", errors="replace").read()
    blo = re.search (r"BEGIN_JUCE_MODULE_DECLARATION(.*?)END_JUCE_MODULE_DECLARATION",
                     txt, re.S)
    if not blo: return []
    d = re.search (r"^\s*dependencies:\s*(.*)$", blo.group (1), re.M)
    if not d: return []
    return [x for x in re.split (r"[,\s]+", d.group (1).strip()) if x.startswith ("juce_")]


def de_jucer_modulos():
    """Los dos sitios del .jucer donde vive la lista, que tienen que coincidir."""
    t = open (os.path.join (ROOT, "Zati.jucer")).read()
    mods  = set (re.findall (r'<MODULE id="([^"]+)"', t))
    rutas = set (re.findall (r'<MODULEPATH id="([^"]+)"', t))
    return mods, rutas


def modulos():
    mods, rutas = de_jucer_modulos()
    fallos = []

    for m in sorted (mods - rutas):
        fallos.append ("el modulo %s no tiene MODULEPATH: Projucer no lo "
                       "encuentra" % m)
    for m in sorted (rutas - mods):
        fallos.append ("hay un MODULEPATH para %s y ese modulo no esta en "
                       "MODULES" % m)

    d = arbol_juce()
    if d is None:
        print ("modulos     no hay arbol de JUCE en disco: esta mitad no mide nada")
        print ("            (compila una vez -cmake -B build- y vuelve)")
        return fallos + ["sin arbol de JUCE no se puede comprobar el cierre"]

    faltan = {}
    for m in sorted (mods):
        dep = dependencias (d, m)
        if dep is None:
            fallos.append ("el modulo %s no existe en el JUCE de disco" % m)
            continue
        for x in dep:
            if x not in mods: faltan.setdefault (x, []).append (m)

    print ("modulos     %2d en Zati.jucer, cierre comprobado contra %s"
           % (len (mods), os.path.relpath (d, ROOT)))
    for x in sorted (faltan):
        fallos.append ("falta el modulo %s en Zati.jucer: lo pide %s. CMake "
                       "resuelve el cierre solo y Projucer NO"
                       % (x, ", ".join (faltan[x])))
    return fallos



#  Los atributos del .jucer que Projucer lee de verdad, y los que NO existen y
#  por tanto no pueden estar puestos: uno ahi es un numero que nadie lee.
JUCER_VIVOS  = ("gradleVersion", "gradleWrapperVersion")
JUCER_MUERTOS = {
    "androidPluginVersion":
        "el AGP se pide con gradleWrapperVersion; androidPluginVersion "
        "no existe para Projucer (ver jucer_PresetIDs.h) y se ignora",
}


def atributos():
    t = open (os.path.join (ROOT, "Zati.jucer")).read()
    fallos = []
    for a, porque in JUCER_MUERTOS.items():
        if re.search (r'\b%s="' % a, t):
            fallos.append ("Zati.jucer trae %s= y no sirve para nada: %s" % (a, porque))
    for a in JUCER_VIVOS:
        if not re.search (r'\b%s="' % a, t):
            fallos.append ("a Zati.jucer le falta %s=: Projucer usara SU defecto "
                           "y el fichero dira otra cosa" % a)
    v = dict (re.findall (r'\b(gradleVersion|gradleWrapperVersion)="([^"]+)"', t))
    if len (v) == 2:
        print ("gradle      wrapper %s, AGP %s"
               % (v["gradleVersion"], v["gradleWrapperVersion"]))
    return fallos


def main():
    cm, ju = de_cmake(), de_jucer()
    if cm is None:
        print ("FALLA  no encuentro target_sources(Zati PRIVATE ...) en CMakeLists.txt")
        return 1

    fallos = []
    #  Los DOS lados, que es lo que separa "falta uno" de "sobra uno": un
    #  fichero solo en el jucer compila en Android y no en el escritorio, y
    #  entonces el banco no lo mide nunca.
    for f in sorted (cm - ju):
        fallos.append ("%s se compila en el escritorio y NO en Android: "
                       "falta en Zati.jucer" % f)
    for f in sorted (ju - cm):
        fallos.append ("%s se compila en Android y NO en el escritorio: "
                       "falta en CMakeLists.txt, o sea que el banco no lo mide" % f)

    fallos += modulos()
    fallos += atributos()

    print ("CMakeLists  %2d fuentes" % len (cm))
    print ("Zati.jucer  %2d fuentes" % len (ju))
    print ("en comun    %2d" % len (cm & ju))

    print()
    if fallos:
        for f in fallos: print ("FALLA  " + f)
        return 1
    print ("las fuentes, los modulos y los atributos del .jucer dicen lo mismo")
    return 0


if __name__ == "__main__":
    sys.exit (main())

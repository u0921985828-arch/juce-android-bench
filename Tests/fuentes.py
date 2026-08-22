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

    print ("CMakeLists  %2d fuentes" % len (cm))
    print ("Zati.jucer  %2d fuentes" % len (ju))
    print ("en comun    %2d" % len (cm & ju))

    print()
    if fallos:
        for f in fallos: print ("FALLA  " + f)
        return 1
    print ("las dos listas de fuentes dicen lo mismo")
    return 0


if __name__ == "__main__":
    sys.exit (main())

#!/usr/bin/env python3
# ============================================================================
#  NINGUNA MARCA AJENA EN LO QUE SE PUBLICA.
#
#  GOOGLE-PLAY.md §1.4 lo AFIRMA -«se ha comprobado que no hay ni una mencion a
#  SP-404, Roland, Akai, MPC ni Koala en Source/»- y §1.5 lo prohibe «ni en
#  codigo, ni en comentarios, ni en la ficha, ni en capturas». Las dos frases
#  llevaban ahi desde el principio y no las comprobaba nadie, asi que eran
#  falsas: el banco B tenia TRECE nombres de pad visibles con `808` -BD 808,
#  SD 808, CH 808...-, un preset «808 SUB» en la tabla de instrumentos, dos
#  comentarios que nombraban la MPC y diecinueve mas que nombraban el modelo.
#  Una afirmacion que nadie mide es una linea que imprime OK, y esta ademas es
#  la de mas riesgo que queda: el mecanismo no es una demanda sino un
#  formulario que retira la ficha sin juicio.
#
#  LA LISTA SALE DEL DOCUMENTO, no de aqui. Escribirla otra vez serian dos
#  reglas: el dia que alguien anadiera una marca a GOOGLE-PLAY.md, este banco
#  seguiria mirando las de ayer. Se leen las reglas 1 y 2 de §1.5 y se saca de
#  ellas lo que esta prohibido; si el documento cambia de forma y no se puede
#  leer, esto FALLA en vez de dar verde con la lista vacia -que es como una
#  prueba de esta clase se muere sin que nadie se entere-.
#
#  Los numeros se buscan con guardas de digito: `1786902180894` -un nombre de
#  fichero de Instagram que aparece en un comentario- lleva `808` dentro y no
#  es una marca. Y las palabras con guardas de letra, o `MPC` saldria dentro de
#  cualquier identificador que la contenga.
#
#      python3 Tests/marcas.py
# ============================================================================
import os, re, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))

#  Lo que se publica: el codigo que entra en el binario y el proyecto de
#  Android, que es donde viven el nombre de la app y el del paquete.
ARBOL = ["Source", "Zati.jucer"]

#  NINGUNA EXCEPCION, y eso es nuevo. Aqui vivian Tools/fabrica.py -de donde
#  salia cada grabacion- y Tests/clon.py -cuanto se parecia cada receta a la
#  suya-: los dos nombraban los aparatos y los dos hacian falta mientras las
#  grabaciones estuvieran dentro del binario. Desde que la fabrica se sintetiza
#  entera no hay procedencia que documentar, los dos se retiraron, y el
#  repositorio no nombra una marca ajena en ningun sitio.
FUERA = ()


def prohibido():
    """Las reglas 1 y 2 de GOOGLE-PLAY.md §1.5, leidas del documento.

    Dos regex ANCLADAS en las palabras del propio documento y no un barrido de
    mayusculas: el primer intento cogia la seccion entera y se traia la regla 3
    -que enumera los seis efectos NUESTROS- y media docena de palabras de
    prosa, o sea 249 hallazgos y ni uno de verdad. Si el documento cambia de
    forma, esto devuelve vacio y la prueba FALLA en vez de dar verde con la
    lista corta, que es como una prueba de esta clase se muere sin ruido.
    """
    t = open (os.path.join (ROOT, "GOOGLE-PLAY.md"), encoding="utf-8").read()

    r1 = re.search (r"\*\*Nombres de modelo ajenos\.\*\*(.*?)\n\s*\d\. \*\*", t, re.S)
    r2 = re.search (r"Nada de ([^.]*?) en el nombre", t, re.S)
    if r1 is None or r2 is None: return None, None

    cuerpo = r1.group (1)
    #  Los modelos: dos o tres letras, guion, tres cifras.
    modelos = set (re.findall (r"\b([A-Z]{2,3}-\d{3})\b", cuerpo))
    #  Y las marcas, de la enumeracion que el propio texto encabeza.
    marcas = set()
    m = re.search (r"marcas ([^.]*)\.", cuerpo)
    if m: marcas = {x.strip() for x in re.split (r",| y ", m.group (1)) if x.strip()}
    #  MPC va suelto en la primera lista, sin guion ni cifras.
    marcas |= set (re.findall (r"\bMPC\b", cuerpo))

    nums = set (re.findall (r"\b(\d{3})\b", r2.group (1)))
    return (modelos | marcas), nums


def ficheros():
    for base in ARBOL:
        p = os.path.join (ROOT, base)
        if os.path.isfile (p):
            yield base, p
            continue
        for d, _, fs in os.walk (p):
            for f in sorted (fs):
                if not f.endswith ((".h", ".cpp", ".inc", ".jucer")): continue
                full = os.path.join (d, f)
                yield os.path.relpath (full, ROOT), full


def main():
    palabras, nums = prohibido()
    if not palabras or not nums:
        print ("FALLA  no puedo leer las reglas 1 y 2 de GOOGLE-PLAY.md §1.5: "
               "sin lista no hay prueba")
        return 1

    #  Guardas de letra y de digito alrededor de cada nombre: `MPC` saldria
    #  dentro de cualquier identificador que la contenga.
    pats = [(p, re.compile (r"(?<![A-Za-z0-9])" + re.escape (p) + r"(?![A-Za-z0-9])"))
            for p in sorted (palabras)]
    #  Y los numeros SOLO con forma de modelo -letras, guion opcional, las tres
    #  cifras- porque la regla 2 dice «en el NOMBRE» y no «en ningun sitio»:
    #  404 es tambien una coordenada y 808 vive dentro de
    #  `1786902180894`, que es un nombre de fichero de Instagram citado en un
    #  comentario. Asi la regla caza `FX-404` -el nombre viejo del proyecto, y
    #  el mayor riesgo que ha tenido segun el propio documento- sin inventarse
    #  hallazgos con los pixeles.
    #
    #  Y el separador es opcional Y puede ser un ESPACIO, que es donde la
    #  primera version no valia nada: pedia guion, asi que al devolver «SD 808»
    #  a proposito -uno de los trece nombres que esta prueba existe para cazar-
    #  seguia saliendo verde. Una prueba que no caza el fallo por el que se
    #  escribio es una linea que imprime OK.
    pats += [("*" + n, re.compile (r"(?<![A-Za-z0-9])[A-Za-z]{1,3}[- ]?" + n + r"(?![0-9])"))
             for n in sorted (nums)]

    hallazgos = []
    nf = 0
    for rel, full in ficheros():
        if rel.replace (os.sep, "/") in FUERA: continue
        nf += 1
        for i, linea in enumerate (open (full, encoding="utf-8", errors="replace"), 1):
            for nombre, rx in pats:
                if rx.search (linea):
                    hallazgos.append ((rel, i, nombre, linea.strip()[:78]))

    #  Y LOS NOMBRES DE LOS EFECTOS, por las MISMAS dos reglas.
    #
    #  La regla 3 de §1.5 enumeraba los nuestros a mano y se quedo vieja DOS
    #  veces -dijo ISO y CRSH cuando el codigo decia FLT y BIT, y luego se
    #  quedo en seis con once en la tabla-. Una copia de `fxDefs` escrita en un
    #  documento es una copia que se queda vieja; el criterio no. Asi que el
    #  documento dice el criterio y esto pasa los nombres DE VERDAD por las
    #  reglas 1 y 2: un efecto llamado `LOOPER` o `TB3` falla en vez de
    #  publicarse.
    #
    #  Y se leen del fuente y no de una lista aqui, que es la misma razon por
    #  la que `Tests/lang.py` los parsea: dos listas son dos reglas.
    fx = open (os.path.join (ROOT, "Source", "MainComponent.cpp"),
               encoding="utf-8").read()
    k = fx.index ("const MainComponent::FxDef MainComponent::fxDefs")
    nombresFx = re.findall (r'\{\s*"([A-Z0-9]{2,8})"\s*,\s*\{', fx[k:fx.index ("\n};", k)])

    #  Y SE CUENTAN CONTRA `kNumFx`, que es lo que hizo falta al romperlo a
    #  proposito. El ancla de `lang.py` es `"[A-Z]{2,3}"` y se copio aqui: un
    #  nombre CON UN NUMERO —o sea justo el que esta regla existe para cazar—
    #  no casa, la fila no se parsea y el barrido sale verde con catorce
    #  nombres de quince. Un nombre que no se lee no es un nombre limpio.
    ae = open (os.path.join (ROOT, "Source", "AudioEngine.h"), encoding="utf-8").read()
    m = re.search (r"kNumFx\s*=\s*(\d+)", ae)
    cuantos = int (m.group (1)) if m else -1
    if cuantos < 0 or len (nombresFx) != cuantos:
        print ("FALLA  lei %d nombres de fxDefs y kNumFx dice %d: esto no mide nada"
               % (len (nombresFx), cuantos))
        return 1
    for nom in nombresFx:
        for etiqueta, rx in pats:
            if rx.search (nom):
                hallazgos.append (("Source/MainComponent.cpp", 0,
                                   etiqueta, "el efecto se llama «%s»" % nom))

    print ("prohibido   %s" % "  ".join (sorted (palabras) + sorted (nums)))
    print ("mirados     %d ficheros de %s" % (nf, ", ".join (ARBOL)))
    print ("efectos     %d nombres: %s" % (len (nombresFx), " ".join (nombresFx)))
    print()

    if hallazgos:
        for rel, i, nombre, linea in hallazgos[:40]:
            print ("FALLA  %s:%d  «%s»  %s" % (rel, i, nombre, linea))
        if len (hallazgos) > 40:
            print ("FALLA  ... y %d mas" % (len (hallazgos) - 40))
        return 1

    print ("ni una marca ajena en lo que se publica")
    return 0


if __name__ == "__main__":
    sys.exit (main())

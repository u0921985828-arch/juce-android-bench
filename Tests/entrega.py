#!/usr/bin/env python3
#  DONDE ACABO EL EMPUJON.
#
#  La regla de la casa dice «commit -> push -> comprobar que el publico tiene lo
#  que se ha medido», y la comprobacion estaba escrita como
#
#      git ls-remote origin refs/heads/main
#
#  o sea PASANDO POR EL NOMBRE DEL REMOTO. Eso vale mientras `.git/config` diga
#  la verdad, y medido no la dice: el contenedor lo reescribe entre sesiones y
#  `origin` vuelve a la mitad -fetch en el repositorio privado y push en el
#  publico-. Con eso, la postcondicion devuelve el `main` del privado, dos
#  tandas por detras, y sale VERDE por el motivo equivocado. Una postcondicion
#  que mide el repositorio que no es no mide nada.
#
#  Ya costo DOS TANDAS enteras publicadas en el sitio equivocado. El arreglo de
#  entonces fue apuntar el remoto, y ese arreglo se deshace solo: es el mismo
#  patron que las contrasenas de la firma -arregladas el 28, deshechas el 29 al
#  reescribir el paso, y CLAUDE.md las dio por hechas trece tandas-. Sin la
#  regla, la sesion siguiente lo repite.
#
#  Y LA FRASE QUE LO DEJABA SIN MEDIR ERA CIERTA A MEDIAS. CLAUDE.md decia que
#  un remoto vive en `.git/config`, que no esta en el repositorio, «asi que no
#  hay regla de banco que pueda medirlo». No la hay POR EL NOMBRE. Por URL no
#  depende de `.git/config` en absoluto — y entonces deja de importar que el
#  nombre se desapunte.
#
#  LAS DOS PREGUNTAS, y la segunda no es redundante:
#
#    1. El publico tiene lo que se ha medido: `git ls-remote <URL>` contra
#       `git rev-parse HEAD`. Por URL y no por `origin`, que es preguntarle al
#       fichero que acaba de mentir.
#    2. Y `origin` apunta ahi por los DOS lados. La 1 dice si ESTA entregado; la
#       2, si el proximo `git push` sin argumentos va a ir al sitio. Con las dos
#       URL en el mensaje: «origin esta mal» sin decir a donde apunta no se
#       arregla.
#
#  Y LA PRUEBA NO ARREGLA LO QUE MIDE. `git remote set-url` es una linea y
#  sale gratis ponerla aqui; una prueba que repara lo que mide deja de poder
#  decir que no.
#
#  FUERA DE banco.yml a proposito: la pregunta 1 compara con el HEAD LOCAL y el
#  CI hace checkout de un SHA suelto, asi que alli la respuesta no significa
#  nada. Va en el renglon de la entrega, al lado de Tests/apk.py, que esta
#  fuera por lo mismo.

import pathlib
import re
import subprocess
import sys

RAIZ = pathlib.Path(__file__).resolve().parent.parent
DATOS = RAIZ / "Tests" / "entrega.md"


def canonica():
    """La URL del publico, leida de su unico dueño.

    Escrita aqui seria la misma regla en dos sitios, que es lo que ya paso con
    la Z de `marcaCelda` y con el tope de la tarjeta en `maquetas.py`. Y si el
    fichero cambia de forma, FALLA: la cadena de control que `marcas.py` y
    `suministro.py` ya tienen -si el barrido no ve lo que tiene que ver, no
    mide nada-.
    """
    if not DATOS.exists():
        return None
    for linea in DATOS.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^publico:\s*(\S+)\s*$", linea)
        if m:
            return m.group(1)
    return None


def git(*args):
    r = subprocess.run(["git", "-C", str(RAIZ), *args],
                       capture_output=True, text=True)
    return r.returncode, r.stdout.strip(), r.stderr.strip()


def remoto_main(url):
    rc, out, err = git("ls-remote", url, "refs/heads/main")
    if rc != 0 or not out:
        return None, err or "sin respuesta"
    return out.split()[0], None


def urls_de_origin():
    """fetch y push por separado: `set-url --push` mueve UNA sola."""
    _, fetch, _ = git("remote", "get-url", "origin")
    _, push, _ = git("remote", "get-url", "--push", "origin")
    return fetch, push


def main():
    fallos = []

    url = canonica()
    if url is None:
        print(f"FALLA  no puedo leer la URL canonica de {DATOS.relative_to(RAIZ)}: "
              f"no mide nada")
        return 1
    print(f"publico    {url}")

    #  1 . EL PUBLICO TIENE LO QUE SE HA MEDIDO.
    _, head, _ = git("rev-parse", "HEAD")
    alla, err = remoto_main(url)
    print(f"HEAD       {head}")
    if alla is None:
        print(f"FALLA  no contesta el publico ({err}): no mide nada")
        return 1
    print(f"alli       {alla}")
    if alla != head:
        fallos.append(f"el publico esta en {alla[:7]} y aqui HEAD es {head[:7]}: "
                      f"lo que el CI compile no es lo que se ha medido")

    #  2 . Y EL PROXIMO EMPUJON VA A IR AHI.
    fetch, push = urls_de_origin()
    print(f"origin     fetch {fetch or '(ninguno)'}")
    print(f"           push  {push or '(ninguno)'}")
    for lado, tiene in (("fetch", fetch), ("push", push)):
        if tiene != url:
            fallos.append(
                f"origin {lado} apunta a {tiene or '(ninguno)'} y el publico es "
                f"{url}: arreglalo con  git remote set-url origin {url}")

    for f in fallos:
        print("FALLA  " + f)
    print("VEREDICTO:", "FALLA" if fallos else "OK")
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())

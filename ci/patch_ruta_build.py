#!/usr/bin/env python3
"""Que la ruta de compilacion no viaje dentro de la .so.

El binario que se instala llevaba **320 veces** la ruta absoluta del runner
-`/home/runner/work/FX-404/FX-404/Builds/Android/app/../../../JUCE/...`- en su
`.rodata`. Son los `__FILE__` de las macros de asercion de JUCE, que en release
siguen ahi: la libreria ya sale *stripped* -no hay una sola seccion `.debug_*`-
asi que no es informacion de depuracion que se pueda quitar despues, son
literales de verdad.

No es un fallo de funcionamiento y por eso duro: nadie miraba lo que va ESCRITO
dentro del paquete. Lo que filtra es el nombre viejo del proyecto, que
`GOOGLE-PLAY.md` 1.5 regla 2 llama «el mayor riesgo que ha tenido: misma
categoria, mismo numero, mismo comprador que el SP-404», dentro del fichero que
se publica. `Tests/marcas.py` no puede verlo porque mira `Source/`, y `Source/`
esta limpio.

`-ffile-prefix-map=<raiz>=.` deja esos `__FILE__` en ruta relativa sin tocar una
linea de codigo ni cambiar lo que hace el programa.

VA EN EL PROYECTO GENERADO Y NO EN EL .jucer por dos razones. La bandera
necesita la ruta ABSOLUTA de la raiz, que solo se conoce cuando el runner ya ha
hecho el checkout - escribirla en `Zati.jucer` seria clavar
`/home/runner/work/...` en un fichero del repositorio, o sea meter el nombre
justo donde se esta intentando quitarlo. Y ademas el fichero pasaria a decir la
ruta de la maquina de otro.

Y falla ruidosamente si el CMakeLists generado cambia de forma, igual que
`patch_juce_oboe.py`: un parche que no se aplica se lee exactamente igual que
uno que si, y el sintoma seria una cifra en `Tests/apk.py` que nadie sabria
explicar.

La medida que lo cierra es esa misma prueba, que corre sobre el binario ya
firmado y cuenta lo escrito dentro: si esto no se aplica, el APK no pasa.
"""

import os
import re
import sys
from pathlib import Path

CM = Path(sys.argv[1] if len(sys.argv) > 1
          else "Builds/Android/app/CMakeLists.txt")
RAIZ = Path(sys.argv[2] if len(sys.argv) > 2
            else os.environ.get("GITHUB_WORKSPACE", os.getcwd())).resolve()

MARCA = "# --- Zati: la ruta de compilacion no viaja dentro de la .so ---"

if not CM.is_file():
    sys.exit("FALLA: no encuentro el CMakeLists generado en %s" % CM)

texto = CM.read_text(encoding="utf-8")

if MARCA in texto:
    print("ya estaba parcheado")
    sys.exit(0)

#  El nombre del objetivo lo pone Projucer y no se da por sabido: se lee del
#  `add_library(... SHARED ...)` que el propio fichero declara. Si manana lo
#  llama de otra forma, esto FALLA en vez de escribir una linea que no aplica a
#  nada.
m = re.search(r"add_library\s*\(\s*([$\{\}\w]+)\s+SHARED", texto)
if m is None:
    sys.exit("FALLA: no encuentro el add_library(... SHARED ...) en %s: "
             "Projucer ha cambiado de forma" % CM)

objetivo = m.group(1)

#  Dos mapeos y no uno: la raiz del repositorio -de donde salen las rutas de
#  Source/ y las de JUCE, que se citan como `../../../JUCE/...`- y la carpeta
#  del proyecto generado, que es el directorio de compilacion y el prefijo con
#  el que el compilador ve casi todo.
app = (RAIZ / "Builds" / "Android" / "app").resolve()
banderas = [
    "-ffile-prefix-map=%s=." % app,
    "-ffile-prefix-map=%s=." % RAIZ,
]

texto += "\n%s\n#  Ver ci/patch_ruta_build.py y la cuarta comprobacion de Tests/apk.py.\ntarget_compile_options(%s PRIVATE %s)\n" % (
    MARCA, objetivo, " ".join(banderas))

CM.write_text(texto, encoding="utf-8")

#  Postcondicion y no valor de retorno, que es la regla de la casa: lo que
#  importa no es que la escritura dijera que si, sino que la linea este.
vuelto = CM.read_text(encoding="utf-8")
if MARCA not in vuelto or "-ffile-prefix-map=" not in vuelto:
    sys.exit("FALLA: la bandera no quedo escrita en %s" % CM)

print("objetivo: %s" % objetivo)
for b in banderas:
    print("  %s" % b)

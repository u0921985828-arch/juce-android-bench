#!/usr/bin/env python3
#  LA CREDENCIAL QUE NO SE PUEDE DESHACER DESDE LA APP.
#
#  De todo lo que este proyecto guarda, la clave de subida es la unica cuya
#  perdida no tiene arreglo desde aqui: un APK firmado con otra clave no es una
#  actualizacion de esta app, es otra app. Y es lo unico del repositorio que no
#  esta en el repositorio - vive en los secretos de la cuenta - asi que lo que
#  se puede medir no es la clave sino COMO la maneja el workflow.
#
#  Y hace falta medirlo porque YA SE DESHIZO SOLO. El 28 de agosto `2d243ef`
#  paso las tres contrasenas de la linea de ordenes al entorno y a la entrada
#  estandar; el 29, `d4206d5` -cuyo asunto es la etiqueta `latest` y no toca la
#  firma- reescribio el paso entero y las devolvio al argv, comentario
#  explicativo incluido. `CLAUDE.md` siguio contandolo como hecho durante trece
#  tandas. Un arreglo que nada comprueba es un arreglo que vuelve: es la regla
#  de la casa -«un numero que nadie mira se publica»- aplicada a lo que no es un
#  numero.
#
#  LAS DOS PREGUNTAS, y ninguna repite una constante del workflow:
#
#    1. Ninguna contrasena llega al argv de un programa EXTERNO. No se pregunta
#       «¿usa env:?», que seria la respuesta escrita dos veces: se pregunta si
#       la variable se expande en una orden que crea un proceso. Un builtin
#       -`[`, `printf`, `echo`- no tiene /proc/<pid>/cmdline que leer, asi que
#       `printf ... | jarsigner -storepass:file /dev/stdin` pasa por lo que ES
#       y no por como esta escrito.
#    2. El keystore se borra pase lo que pase. Por `trap ... EXIT` y no rama a
#       rama: un `rm` por camino cubre los caminos que alguien enumero, y deja
#       fuera el que importa -que apksigner falle y el `bash -e` de Actions se
#       vaya sin pasar por ninguna rama-.
#
#  Y LOS NOMBRES SE DERIVAN. Cuales son las contrasenas lo dice el bloque `env:`
#  que las mapea desde `${{ secrets.* }}`, asi que un secreto que alguien anada
#  manana entra en la regla sin tocar este fichero. Escribir aqui la lista seria
#  medir la lista de ayer.
#
#  Y NO TODO SECRETO ES UNA CREDENCIAL, que es donde la primera version de esta
#  regla se equivoco: saco dos FALLA por `KEY_ALIAS`, y un alias es un NOMBRE
#  -apksigner ni siquiera ofrece una forma fuera del argv para `--ks-key-alias`-
#  asi que verlo en la tabla de procesos no le sirve a nadie para firmar nada.
#  Lo que no puede salir es el MATERIAL: los bytes del keystore y las dos
#  contrasenas. Se clasifica por el nombre del secreto de GitHub -PASSWORD,
#  PASS, TOKEN, SECRET, BASE64- y no por el de la variable local ni por una
#  lista de este proyecto, asi que un `ANDROID_UPLOAD_PASSWORD` que alguien
#  anada manana entra solo y un `..._ALIAS` se queda fuera con razon.
#
#  CADENA DE CONTROL: si no se encuentra ni un solo paso que consuma secretos,
#  esto FALLA en vez de dar verde sin haber mirado - lo mismo que hace
#  `marcas.py` cuando no puede leer sus reglas.

import os, re, sys, glob

RAIZ = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))

#  Ordenes que NO crean un proceso, o sea que no publican su argv. La lista es
#  de bash y no de este proyecto, asi que no se queda vieja con la app.
BUILTINS = {"[", "[[", "test", "printf", "echo", "read", "export", "local",
            "declare", "set", "unset", "eval", "exit", "return", "shift",
            "source", ".", "cd", "trap", "true", "false", ":"}
PALABRAS = {"if", "then", "elif", "else", "fi", "while", "until", "do", "done",
            "for", "case", "esac", "!", "{", "}", "("}


def ordenes (script):
    """Cada orden del script, ya unidas las continuaciones y partidas las
    tuberias y las listas. Devuelve (primera_palabra, texto_entero)."""
    #  Se unen las continuaciones con `\` primero: una orden partida en seis
    #  lineas es UNA orden, y mirarla linea a linea diria que `--ks-pass ...`
    #  es una orden cuyo primer termino es una opcion.
    unido, acc = [], ""
    for linea in script.splitlines():
        sin = re.sub (r"(?<!\\)#.*$", "", linea)           # comentario fuera
        acc += " " + sin.rstrip()
        if sin.rstrip().endswith ("\\"):
            acc = acc.rstrip()[:-1]
            continue
        if acc.strip():
            unido.append (acc.strip())
        acc = ""
    if acc.strip():
        unido.append (acc.strip())

    for orden in unido:
        for trozo in re.split (r"\|\||&&|(?<!\|)\|(?!\|)|;", orden):
            t = trozo.strip()
            while t:
                p = t.split (maxsplit = 1)[0]
                if p in PALABRAS:
                    t = t.split (maxsplit = 1)[1] if " " in t else ""
                    continue
                break
            if t:
                yield t.split (maxsplit = 1)[0], t


def pasos_con_secretos (texto):
    """(nombres de variable secretas, script) de cada paso que mapea secretos."""
    #  Un paso es un bloque `env:` con `${{ secrets.X }}` y su `run: |`. Se
    #  busca por la forma y no por el nombre del paso, que es prosa.
    for m in re.finditer (r"^(\s+)env:\s*$", texto, re.M):
        sangria, ini = len (m.group (1)), m.end()
        resto = texto[ini:]
        varsec, run, dentro = [], [], None
        for linea in resto.splitlines():
            if not linea.strip():
                (run.append ("") if dentro is not None else None)
                continue
            s = len (linea) - len (linea.lstrip())
            if dentro is not None:
                if s <= dentro:
                    break
                run.append (linea)
                continue
            if s <= sangria and not re.match (r"^\s+run:\s*\|", linea):
                if s < sangria or not re.match (r"^\s+\w[\w-]*:", linea):
                    break
            if re.match (r"^\s+run:\s*\|", linea):
                dentro = s
                continue
            v = re.match (r"^\s+([A-Z_][A-Z0-9_]*):\s*\$\{\{\s*secrets\.([A-Z0-9_]+)", linea)
            if v and re.search (r"PASS|PASSWORD|TOKEN|SECRET|BASE64|_B64", v.group (2)):
                varsec.append (v.group (1))
        if varsec and run:
            yield varsec, "\n".join (run)


fallos, pasos, revisadas = [], 0, 0

for wf in sorted (glob.glob (os.path.join (RAIZ, ".github", "workflows", "*.yml"))):
    texto = open (wf, encoding = "utf-8").read()
    corto = os.path.relpath (wf, RAIZ)
    for varsec, script in pasos_con_secretos (texto):
        pasos += 1
        #  Las que llevan CONTRASENA, que son las que un argv publica. El
        #  keystore en base64 tambien es secreto, asi que entra igual.
        print (f"  {corto}: un paso con {len (varsec)} credenciales -> {', '.join (varsec)}")

        for primera, orden in ordenes (script):
            revisadas += 1
            base = primera.strip ('"').strip ("'")
            base = os.path.basename (base.split ("/")[-1])
            if base in BUILTINS or base.startswith ("$") or base.startswith ("${"):
                #  `$APKSIGNER` y `$JAVA_HOME/bin/jarsigner` SI son externos:
                #  lo que empieza por `$` puede ser cualquier cosa, asi que se
                #  mira igual. Solo se salta un builtin con nombre.
                if base in BUILTINS:
                    continue
            for v in varsec:
                if re.search (r"\$\{?" + v + r"\}?\b", orden):
                    fallos.append (f"{corto}: «{v}» se expande en el argv de "
                                   f"`{primera}` — legible en /proc/<pid>/cmdline\n"
                                   f"      {orden[:120]}")

        #  Y EL KEYSTORE SE BORRA PASE LO QUE PASE.
        clave = re.search (r">\s*(\S+\.jks|\S+\.keystore)\b", script)
        if clave:
            k = clave.group (1)
            trap = re.search (r"trap\s+'[^']*rm[^']*" + re.escape (k) + r"[^']*'\s+EXIT", script)
            if not trap:
                fallos.append (f"{corto}: «{k}» se escribe en el runner y no hay "
                               f"`trap ... rm ... EXIT`: un fallo antes del borrado "
                               f"deja la clave de subida en el disco del runner")

print()
if pasos == 0:
    print ("FALLA  ningun workflow consume secretos: la regla no mide nada")
    sys.exit (1)

for f in fallos:
    print ("  FALLA  " + f)

print (f"\nfirma: {pasos} paso(s) con secretos, {revisadas} ordenes revisadas, "
       f"{len (fallos)} FALLA")
sys.exit (1 if fallos else 0)

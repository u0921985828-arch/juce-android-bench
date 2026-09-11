#!/usr/bin/env python3
#  LO QUE EL CI SE TRAE, Y SI VA CLAVADO.
#
#  Esta app no tiene red -cero modulos de red, cero sockets, ningun permiso de
#  INTERNET- asi que su unica cadena de suministro es la del CI: las acciones
#  que los tres workflows ejecutan en el mismo runner donde se compila y se
#  firma, y el arbol de JUCE, que ademas se compila DENTRO del binario que se
#  instala.
#
#  Una `uses:` por ETIQUETA es un puntero movible: quien pueda mover `@v4`
#  aguas arriba ejecuta codigo en el runner que tiene las cuatro credenciales
#  de subida delante. Y una etiqueta de JUCE que se mueva cambia lo que se
#  publica sin que falle nada -- se publica, que es la clase de fallo que este
#  banco lleva contando.
#
#  Y HACE FALTA LA REGLA Y NO SOLO EL ARREGLO. Es literalmente lo que acaba de
#  pasar con las contrasenas de la firma: se arreglaron el 28 de agosto y se
#  deshicieron el 29 al reescribir el paso, y `CLAUDE.md` lo dio por hecho
#  trece tandas. Un pin que nada comprueba vuelve a ser una etiqueta el dia que
#  alguien pegue un bloque de ejemplo de la documentacion.
#
#  LAS DOS PREGUNTAS:
#
#    1. Toda `uses:` va por SHA de cuarenta hexadecimales. LAS DIEZ y no solo
#       las dos de terceros: una lista blanca -«actions/* se perdona»- es una
#       lista que hay que mantener y que un dia deja pasar lo que no toca.
#    2. Todo `git clone` se contrasta contra un SHA declarado en el propio
#       workflow. No se comprueba QUE SHA -eso seria la constante del codigo
#       copiada en el banco, el fallo que `Tests/icono.py` ya cometio dos veces
#       con la mascara del lanzador- sino que exista el numero y que el paso lo
#       compare con lo que ha bajado.
#
#  Y es fichero propio y no una tercera pregunta de `Tests/firma.py`: aquella
#  mide la credencial que no se puede deshacer desde la app y esta mide lo que
#  el CI se trae. Dos preguntas, dos dueños.
#
#  CADENA DE CONTROL: si no encuentra ni una `uses:` -otro layout, otro
#  directorio- FALLA en vez de dar verde sin haber mirado, lo mismo que
#  `marcas.py` cuando no puede leer sus reglas.

import os, re, sys, glob

RAIZ = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
SHA  = re.compile (r"^[0-9a-f]{40}$")


def bloques_run (texto):
    """(linea, script) de cada bloque `run: |` del fichero."""
    lineas = texto.splitlines()
    i = 0
    while i < len (lineas):
        m = re.match (r"^(\s*)-?\s*run:\s*[|>]", lineas[i])
        if not m:
            i += 1
            continue
        sangria, cuerpo, ini = len (m.group (1)), [], i + 1
        j = ini
        while j < len (lineas):
            l = lineas[j]
            if l.strip() and len (l) - len (l.lstrip()) <= sangria:
                break
            cuerpo.append (l)
            j += 1
        yield ini + 1, "\n".join (cuerpo)
        i = j


fallos, acciones, clavadas, clones = [], 0, 0, 0

for wf in sorted (glob.glob (os.path.join (RAIZ, ".github", "workflows", "*.yml"))):
    texto = open (wf, encoding = "utf-8").read()
    corto = os.path.relpath (wf, RAIZ)

    #  Los SHA que el workflow DECLARA en su `env:`, para contrastar clones.
    declarados = {v: s for v, s in
                  re.findall (r"^\s+([A-Z_][A-Z0-9_]*):\s*\"?([0-9a-f]{40})\"?\s*$",
                              texto, re.M)}

    #  1 · TODA `uses:` POR SHA.
    for n, linea in enumerate (texto.splitlines(), 1):
        m = re.match (r"^\s*-?\s*uses:\s*([^\s#]+)", linea)
        if not m:
            continue
        ref = m.group (1).strip ('"').strip ("'")
        if ref.startswith ("./") or ref.startswith ("docker://"):
            continue          # una accion local no se trae de ningun sitio
        acciones += 1
        if "@" not in ref:
            fallos.append (f"{corto}:{n}  `{ref}` no lleva referencia ninguna")
            continue
        repo, punta = ref.rsplit ("@", 1)
        if SHA.match (punta):
            clavadas += 1
        else:
            fallos.append (f"{corto}:{n}  `{repo}` va por «{punta}», que es un "
                           f"puntero movible aguas arriba")

    #  2 · TODO `git clone` CONTRASTADO CONTRA UN SHA DECLARADO.
    for n, script in bloques_run (texto):
        if not re.search (r"\bgit\s+clone\b", script):
            continue
        clones += 1
        destino = re.search (r"\bgit\s+clone\b.*?([\w.-]+?)(?:\.git)?\s*$",
                             script, re.M)
        quien = destino.group (1) if destino else "un arbol"
        usados = [v for v in declarados
                  if re.search (r"\$\{?" + v + r"\}?\b", script)]
        if not declarados:
            fallos.append (f"{corto}:{n}  `git clone` de {quien} y el workflow no "
                           f"declara ningun SHA: se trae por etiqueta y nadie "
                           f"comprueba el commit")
        elif not usados:
            fallos.append (f"{corto}:{n}  `git clone` de {quien} y no usa ninguno "
                           f"de los SHA declarados ({', '.join (sorted (declarados))})")
        elif not re.search (r"rev-parse\s+HEAD", script):
            fallos.append (f"{corto}:{n}  `git clone` de {quien} nombra {usados[0]} "
                           f"y no contrasta el commit bajado (`rev-parse HEAD`)")
        else:
            print (f"  {corto}:{n}  {quien} contrastado contra {usados[0]}")

print()
if acciones == 0:
    print ("FALLA  ninguna accion que mirar: no mide nada")
    sys.exit (1)

for f in fallos:
    print ("  FALLA  " + f)

print (f"\nsuministro: {clavadas} de {acciones} acciones clavadas por SHA, "
       f"{clones} clone(s) contrastado(s), {len (fallos)} FALLA")
sys.exit (1 if fallos else 0)

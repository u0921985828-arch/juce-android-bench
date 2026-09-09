#!/usr/bin/env python3
# ============================================================================
#  TODO NUMERO DE MAQUETADO VIVE EN `Metrics`, Y NADIE LO COMPROBABA.
#
#  Es la regla mas antigua de esta casa -esta escrita en CLAUDE.md desde el
#  principio: *«Todos los numeros de maquetado viven en Metrics y
#  ZatiLookAndFeel - nada de constantes a mano»*- y era una AFIRMACION SIN
#  MEDIDA, que es como se publica lo que deja de ser verdad.
#
#  Llego pidiendo «una revision a todas las pantallas para maquetar los
#  espacios entre secciones, lineas de botones y demas», y lo primero que se
#  midio reencuadro la peticion: el aire ENTRE FILAS ya cumplia -3 literales de
#  103, el 97.1 %- y el que estaba escrito a mano era el de una CELDA, o sea el
#  margen que cada control se deja dentro de su fila: ~51 de ~97, el 53 %. Y los
#  tres peores casos no eran entre filas sino DENTRO DE UNA: cuatro inquilinos
#  del plato de la cara a 4, 6, 4 y 10; tres celdas contiguas de un canal de la
#  mesa a 0, 2 y 4; el canalon del rack a 1 con el fader que lo toca a 2.
#
#  Ninguna de las catorce reglas de `expo.py` puede ver nada de eso: dos pixeles
#  de margen no solapan, no se salen de la ventana, no cortan un rotulo, no
#  miden cero y estan traducidos. Se mide en el FUENTE, que es donde vive la
#  causa, igual que `fuentes.py` mide dos listas de ficheros y no un binario.
#
#
#  Y JUZGA EL AIRE Y NO EL TAMANO, que es lo que la deja sin falsos positivos.
#
#  Un `reduced`, un `expanded` o un `translated` son SIEMPRE un margen: el
#  numero que llevan es aire y nada mas, asi que si vale lo que un token vale,
#  esta mal escrito por definicion -el numero ya tiene nombre y el nombre esta a
#  un include-. Igual una linea que solo separa: `x.removeFromTop (8);` sin
#  asignar a nadie es un hueco.
#
#  Un TAMANO no. `removeFromTop (26)` puede ser un token, puede ser el alto de
#  dos renglones de texto medidos contra su parrafo y puede ser el ancho de una
#  tapa; forzarle el token que casualmente vale 26 -`canalPiano`, el canalon de
#  una rejilla- no seria arreglar nada, seria escribir una mentira que ademas
#  se lee como si alguien la hubiera pensado. La primera version de esta prueba
#  los juzgaba y sacaba noventa y nueve donde el aire eran cincuenta y seis. Se
#  IMPRIMEN con el token que tendrian, que es lo que hace falta para moverlos a
#  mano cuando toca, y no se juzgan — *un numero que no separa el fallo del caso
#  legitimo no puede ser un veredicto*, que es la leccion de TARJETA.
#
#  Asi la prueba no necesita lista de excepciones y se mantiene sola: el dia que
#  alguien anada un token de aire, lo que valga ese numero pasa a estar mal
#  escrito.
#
#  Y LA TABLA SE LEE DEL FUENTE. Escribirla aqui serian dos tablas, que es lo
#  que ya costo la Z de `marcaCelda` saliendo en rojo contra un icono correcto.
#  Si no se puede leer, FALLA: dar verde con la tabla vacia es una linea que
#  imprime OK, y es lo mismo que hace `marcas.py` cuando no encuentra sus
#  reglas.
# ============================================================================
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CABECERA = os.path.join(RAIZ, "Source", "ZatiLookAndFeel.h")

#  Los ficheros que MAQUETAN. Se dejan fuera los tres que dibujan un simbolo
#  dentro de la caja que les dan -`Iconos.h`, `PadArt.h`, `StoreArt.h`-: alli
#  los numeros son coordenadas de un dibujo en su propia rejilla de 24 unidades
#  y no el aire de una fila. Es una clase de fichero y no una lista de
#  excepciones.
DIBUJAN = {"Iconos.h", "PadArt.h", "StoreArt.h"}

#  Las llamadas que colocan algo. `setBounds` con un rectangulo ya calculado no
#  lleva numero; los que lo llevan son estas.
LLAMADAS = ("reduced", "expanded", "withTrimmedTop", "withTrimmedBottom",
            "withTrimmedLeft", "withTrimmedRight", "withHeight", "withWidth",
            "removeFromTop", "removeFromBottom", "removeFromLeft",
            "removeFromRight", "withSizeKeepingCentre", "translated")

#  Con UN nivel de parentesis dentro, que si no se pierden justo los que llevan
#  una cuenta al lado: `reduced (4, (Metrics::btn - Metrics::hit) / 2)` -el
#  fader del master- no casaba, y ese 4 es el que su propio comentario dice que
#  tiene que valer lo que el del rotulo de al lado.
CALL = re.compile(r"\.(" + "|".join(LLAMADAS) + r")\s*\(((?:[^()]|\([^()]*\))*)\)")
#  El aire: un inset, un outset y un empujon. Mas la linea que SOLO separa.
AIRE = ("reduced", "expanded", "translated")
SEPARADOR = re.compile(r"^\s*\w+\.removeFrom(?:Top|Bottom|Left|Right)\s*\(\s*-?\d+\s*\)\s*;\s*$")
LIT = re.compile(r"^-?\d+$")


def sin_comentarios(txt):
    #  A mano y no con una regex, que `//` vive tambien dentro de una cadena:
    #  es el mismo cuidado que ya costo un falso positivo en `lang.py`.
    out, i, n = [], 0, len(txt)
    while i < n:
        c = txt[i]
        if c == '"':
            j = i + 1
            while j < n and not (txt[j] == '"' and txt[j - 1] != "\\"):
                j += 1
            out.append(txt[i:j + 1]); i = j + 1
        elif txt.startswith("//", i):
            j = txt.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif txt.startswith("/*", i):
            j = txt.find("*/", i)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in txt[i:j])); i = j
        else:
            out.append(c); i += 1
    return "".join(out)


def tokens():
    #  `static constexpr int a = 4, b = 8;` y `static constexpr int x = lg;`.
    #  Se evalua en orden con lo que ya hay en el ambito, porque la mitad de la
    #  tabla se define en funcion de la otra mitad -`halfGap = gap / 2`,
    #  `aireTapa = halfGap / 2`, `margenFichaX = lg`-.
    if not os.path.exists(CABECERA):
        return None, None
    txt = sin_comentarios(open(CABECERA, encoding="utf-8").read())
    #  Anclado: sin el, un `namespace MetricsX` seguia casando y la rotura a
    #  proposito de "la tabla ilegible" salia verde.
    anc = re.search(r"\bnamespace\s+Metrics\b", txt)
    if anc is None:
        return None, None
    corte = anc.start()
    fin = txt.find("\nclass ZatiLookAndFeel", corte)
    fin = len(txt) if fin < 0 else fin
    met, laf = {}, {}
    for ambito, trozo in ((met, txt[corte:fin]), (laf, txt[fin:])):
        for m in re.finditer(r"static\s+constexpr\s+(int|float)\s+([^;]+);", trozo):
            tipo = m.group(1)
            for decl in m.group(2).split(","):
                if "=" not in decl:
                    continue
                nombre, expr = decl.split("=", 1)
                nombre, expr = nombre.strip(), expr.strip()
                if not re.match(r"^\w+$", nombre):
                    continue
                #  El sufijo de un float se quita del NUMERO y no de la
                #  cadena: `expr.replace ("f", "")` convertia `halfGap` en
                #  `halGap` y se perdian la mitad de los tokens -y con ellos,
                #  los literales que valen lo que ellos-. Una prueba que se come
                #  su propia tabla da verde por el motivo equivocado.
                expr = re.sub(r"(\d)f\b", r"\1", expr).replace("Metrics::", "")
                try:
                    val = eval(expr, {"__builtins__": {}}, dict(met, **ambito))  # noqa: S307
                except Exception:
                    continue
                if not isinstance(val, (int, float)):
                    continue
                #  `gap / 2` en C++ es division ENTERA y en Python da 4.0, asi
                #  que `halfGap` y con el `aireTapa` y los dos `panelAire`
                #  salian float y se caian del filtro de mas abajo: la tabla
                #  perdia justo los tokens de aire, que son los que esta prueba
                #  existe para vigilar. Se coacciona por el TIPO declarado.
                ambito[nombre] = int(val) if tipo == "int" else val
    return (met or None), (laf or None)


def coma(args):
    #  Solo las comas de primer nivel: `(a, (b - c) / 2)` son DOS argumentos.
    out, hondo, act = [], 0, ""
    for ch in args:
        if ch == "(":
            hondo += 1
        elif ch == ")":
            hondo -= 1
        if ch == "," and hondo == 0:
            out.append(act); act = ""
        else:
            act += ch
    out.append(act)
    return out


def barre(met, laf):
    #  El valor -> los nombres que ya lo tienen. Solo enteros: un token float
    #  (`apretonAyuda`, `panelBorde`) no es un numero de maquetado que alguien
    #  pueda escribir a mano en un `reduced`.
    porValor = {}
    for pref, tabla in (("Metrics::", met), ("ZatiLookAndFeel::", laf)):
        for k, v in tabla.items():
            if isinstance(v, int) and v > 0:
                porValor.setdefault(v, []).append(pref + k)

    fallas, sueltos, tam = [], {}, {}
    src = os.path.join(RAIZ, "Source")
    ficheros = [f for f in sorted(os.listdir(src))
                if (f.endswith(".h") or f.startswith("MainComponent"))
                and f.endswith((".h", ".cpp")) and f not in DIBUJAN]
    for f in ficheros:
        ruta = os.path.join(src, f)
        crudo = open(ruta, encoding="utf-8").readlines()
        limpio = sin_comentarios("".join(crudo)).split("\n")
        for n, linea in enumerate(limpio, 1):
            #  Un `repaint` no maqueta nada: pide que se vuelva a pintar un
            #  trozo, y el margen que se le anade es de dibujo. Son tres en toda
            #  la app y las tres caben en su linea.
            if "repaint" in linea:
                continue
            for m in CALL.finditer(linea):
                for arg in coma(m.group(2)):
                    arg = arg.strip()
                    if not LIT.match(arg):
                        continue
                    v = int(arg)
                    if v <= 0:
                        continue
                    if v not in porValor:
                        sueltos[v] = sueltos.get(v, 0) + 1
                    elif m.group(1) in AIRE or SEPARADOR.match(linea):
                        fallas.append((f, n, m.group(1), v, porValor[v],
                                       crudo[n - 1].strip()[:90]))
                    else:
                        tam.setdefault(v, []).append("%s:%d" % (f, n))
    return fallas, sueltos, tam, porValor


def main():
    met, laf = tokens()
    if not met:
        print("FALLA  no puedo leer la tabla de Metrics de Source/ZatiLookAndFeel.h:"
              " no mide nada")
        return 1
    print("tabla de tokens: %d en Metrics, %d en ZatiLookAndFeel"
          % (len(met), len(laf or {})))

    fallas, sueltos, tam, porValor = barre(met, laf or {})

    #  Los TAMANOS que coinciden con un token: se imprimen y no se juzgan.
    print()
    print("tamanos literales que coinciden con un token (se imprimen, no se juzgan): %d en %d valores"
          % (sum(len(v) for v in tam.values()), len(tam)))
    for v in sorted(tam):
        print("  %-3d x%-3d  seria %s" % (v, len(tam[v]), " / ".join(porValor[v][:3])))

    #  Los que no coinciden con ninguno, igual. Ver la cabecera.
    print()
    print("literales de maquetado SIN token (se imprimen, no se juzgan): %d en %d valores"
          % (sum(sueltos.values()), len(sueltos)))
    if sueltos:
        print("  " + "   ".join("%d x%d" % (v, n) for v, n in sorted(sueltos.items())))

    print()
    if not fallas:
        print("VEREDICTO: OK  ningun literal de AIRE vale lo que un token")
        return 0
    print("FALLA  %d literales de AIRE tienen ya un token con ese valor:" % len(fallas))
    for f, n, call, v, nombres, linea in fallas:
        print("  Source/%s:%d  %s(%d)  es %s" % (f, n, call, v, " / ".join(nombres)))
        print("      %s" % linea)
    return 1


if __name__ == "__main__":
    sys.exit(main())

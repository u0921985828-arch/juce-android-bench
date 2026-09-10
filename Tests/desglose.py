#!/usr/bin/env python3
"""EL DESGLOSE: QUE CONTROLES TIENE CADA PANTALLA, QUE ESTA REPETIDO Y QUE FALTA.

Los planos (Tests/planos.py) dicen DONDE esta cada cosa. Esto dice QUE hay, y
sobre todo las dos preguntas que un plano no contesta porque hacen falta las
treinta y dos pantallas a la vez:

  - **que esta en dos sitios**, que es lo que la regla de la casa llama *una
    funcion, un dueno*;
  - **que le falta a un pop-up** para poder usarse sin salir de el.

Se lee del mismo volcado que los planos -asi que no puede quedarse viejo: si
alguien mueve un control, el desglose cambia solo- y no vuelve a escribir la
lista de pantallas ni la lectura del volcado: las toma de `planos`, que a su
vez toma las pantallas de `expo`. Escribir eso tres veces es como Tests/plano.py
se quedo cubriendo 17 de las 32.

    python3 Tests/desglose.py                 # 412x915
    ZATI_PLANOS_SIZE=280x653 python3 Tests/desglose.py

Sale `desglose/controles.csv` -una fila por control, para ordenar en una hoja
de calculo- y el informe por pantalla en la salida.
"""
import csv, os, sys, collections
import concurrent.futures

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from planos import corre, clase, NOMBRES, SALIDA as SALIDA_PLANOS, iconos_del_binario
from expo import SHEETS, PANTALLA, display_alive

ROOT   = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
SALIDA = os.path.join (ROOT, "desglose")

DEDO = 40    # Metrics::hit

#  LOS MINIMOS DE UN POP-UP. Cada uno con la razon por la que esta, que es lo
#  que separa una regla de una mania.
MINIMOS = [
    ("titulo",     "sin el no se sabe donde estas"),
    ("cerrar",     "salir no puede depender de tocar fuera de la tarjeta"),
    ("dedo",       "un control por debajo de 40 px se falla al tocarlo"),
    ("transporte", "si la pagina escribe algo que suena, PLAY tiene que estar a mano"),
]

#  LAS PAGINAS QUE ESCRIBEN ALGO QUE SUENA, o sea donde no tener PLAY al lado
#  obliga a cerrar la ficha para oir lo que acabas de escribir. No es una lista
#  de gustos: son las tres rejillas de LIENZO mas las dos que editan un sonido.
SUENAN = {"sec", "secp", "paso", "piano", "pianod", "song", "pads", "pad2", "vst"}


def filas_de (comps):
    """Agrupa por PADRE y banda de y, que es lo que hace una fila de verdad.

    Sin el padre, la fila de efectos de la cara se agrupa con los botones de la
    ficha que flota encima -misma y, otro sitio-. Es la misma cuenta que
    planos.huecos, y por eso el desglose y el plano dicen lo mismo.
    """
    grupos = []
    for r in comps:
        padre = "/".join (r["path"].split ("/")[:-1])
        for g in grupos:
            if (g[0][0] == padre and abs (g[0][1]["y"] - r["y"]) <= 6
                    and abs (g[0][1]["h"] - r["h"]) <= 6):
                g.append ((padre, r)); break
        else:
            grupos.append ([(padre, r)])
    grupos.sort (key=lambda g: (g[0][1]["y"], g[0][1]["x"]))
    return grupos


def una (clave):
    """Una pantalla, en los DOS idiomas.

    En espanol y en ingles y emparejado por la RUTA del arbol, que es lo unico
    que separa «dos tapas que se llaman igual» de «dos tapas que se llaman
    igual SOLO en espanol» - que es exactamente el caso de CANCION: songButton
    dice SONG y songModeBtn dice SONG MODE en ingles, y las dos dicen CANCION
    aqui. Una prueba en un solo idioma no puede ver eso.
    """
    tam = os.environ.get ("ZATI_PLANOS_SIZE", "412x915")
    piel = int (os.environ.get ("ZATI_PLANOS_SKIN", "0"))
    es = corre (clave, tam, "es", {"ZATI_SKIN": str (piel)})
    en = corre (clave, tam, "en", {"ZATI_SKIN": str (piel)})
    if not es:
        return clave, None

    ingles = {r["path"]: str (r.get ("text", "") or "") for r in en if "path" in r}
    raiz = next ((r for r in es if r.get ("root")), {})

    #  Solo la cara y la ficha que esta clave abre: la misma regla que el plano.
    comps = [r for r in es if "path" in r and r["w"] > 0 and r["h"] > 0]
    capas = sorted ({r.get ("capa", 0) for r in comps if r.get ("capa", 0)},
                    key=lambda c: -sum (1 for r in comps if r.get ("capa", 0) == c))
    quedan = set() if not clave else set (capas[:1])
    capaFicha = capas[0] if (clave and capas) else 0
    comps = [r for r in comps if r.get ("capa", 0) in quedan or not r.get ("capa", 0)]

    #  Lo de la FICHA es lo que se juzga; la cara de debajo esta en su propia
    #  pantalla y contarla aqui seria contarla treinta y dos veces.
    dentro = [r for r in comps if r.get ("capa", 0) == capaFicha]
    utiles = [r for r in dentro
              if r.get ("kind") in ("button", "slider", "editor")
              and not r.get ("inSlider")
              and "Sheet" not in clase (r) and "XyPanel" not in clase (r)]

    rotulos = [r for r in es if "rotulo" in r and r.get ("capa", 0) == capaFicha]

    filas = []
    for n, g in enumerate (filas_de (utiles)):
        for orden, (_padre, r) in enumerate (sorted (g, key=lambda q: q[1]["x"])):
            filas.append ({
                "pantalla": clave or "cara",
                "nombre":   NOMBRES.get (clave, (clave.upper(), ""))[0],
                "fila":     n,
                "orden":    orden,
                "rotulo_es": str (r.get ("text", "") or ""),
                "rotulo_en": ingles.get (r["path"], ""),
                "icono":    r.get ("icono") or "",
                "tipo":     r.get ("kind", ""),
                "clase":    clase (r),
                "x": r["x"], "y": r["y"], "w": r["w"], "h": r["h"],
                "bajo_el_dedo": int (r.get ("kind") in ("button", "slider")
                                     and min (r["w"], r["h"]) < DEDO),
                "es_valor": int (bool (r.get ("valor"))),
                "ruta": r["path"],
            })

    #  Los minimos.
    txt = {f["rotulo_es"] for f in filas}
    falta = []
    if clave:
        if not any (r.get ("tipo") == "titulo" for r in rotulos):
            falta.append ("titulo")
        #  LA CRUZ SE ESCRIBE DE DOS FORMAS, y la primera version de esta
        #  regla solo conocia una: buscaba «×» -el signo de multiplicar- y
        #  MANUAL y RACK la escriben con una «x» normal, asi que salieron como
        #  «no tiene cruz de cerrar» teniendola. Primero se duda de la prueba.
        #  Que sean dos caracteres distintos para el mismo control es un
        #  hallazgo aparte, y se cuenta: ver `cruces`.
        if not ({"×", "x", "X"} & txt):
            falta.append ("cerrar")
        if clave in SUENAN and not ({"PLAY", "STOP"} & txt):
            falta.append ("transporte")
    bajos = [f for f in filas if f["bajo_el_dedo"]]
    if bajos:
        falta.append ("dedo (%d)" % len (bajos))

    titulo = next ((r["rotulo"] for r in rotulos if r.get ("tipo") == "titulo"), "")
    cruz = next ((t for t in ("×", "x", "X") if t in txt), "")
    return clave, {"filas": filas, "falta": falta, "titulo": titulo, "cruz": cruz,
                   "w": raiz.get ("w", 0), "h": raiz.get ("h", 0)}


def main():
    if not display_alive():
        sys.exit ("la pantalla virtual no responde:  Xvfb :99 -screen 0 1920x1080x24 &")
    os.makedirs (SALIDA, exist_ok=True)
    iconos_del_binario()     # antes de los hilos: la cache perezosa es una carrera

    solo = sys.argv[1:]
    claves = [s for s in SHEETS if not solo or (s or "cara") in solo]

    hechos = {}
    with concurrent.futures.ThreadPoolExecutor (
            max_workers=max (1, min (16, os.cpu_count() or 4))) as pool:
        for clave, d in pool.map (una, claves):
            hechos[clave] = d

    todas = [f for k in claves for f in (hechos.get (k) or {}).get ("filas", [])]

    with open (os.path.join (SALIDA, "controles.csv"), "w", newline="", encoding="utf8") as fh:
        w = csv.DictWriter (fh, fieldnames=list (todas[0].keys()) if todas else ["pantalla"])
        w.writeheader()
        w.writerows (todas)

    # ---- el inventario --------------------------------------------------
    print ("%-11s %-32s %5s %5s  %s" % ("clave", "pantalla", "ctrl", "filas", "le falta"))
    for k in claves:
        d = hechos.get (k)
        if d is None:
            print ("%-11s %-32s  SIN VOLCADO" % (k or "cara", NOMBRES.get (k, ("?", ""))[0]))
            continue
        nf = len ({f["fila"] for f in d["filas"]})
        print ("%-11s %-32s %5d %5d  %s"
               % (k or "cara", NOMBRES.get (k, (k.upper(), ""))[0], len (d["filas"]), nf,
                  ", ".join (d["falta"]) or "-"))

    # ---- lo que esta en dos sitios --------------------------------------
    #  Un control se identifica por su RUTA, asi que el mismo boton visto desde
    #  dos pantallas no cuenta como dos: lo que se busca son rotulos o dibujos
    #  IGUALES en controles DISTINTOS.
    porRuta = {}
    for f in todas:
        porRuta.setdefault (f["ruta"] + "|" + f["pantalla"], f)
    unicos = {}
    for f in todas:
        unicos.setdefault ((f["rotulo_es"], f["icono"], f["clase"], f["w"], f["h"]), f)

    porRotulo = collections.defaultdict (list)
    for f in unicos.values():
        if f["rotulo_es"] and f["rotulo_es"] != "×" and not f["es_valor"]:
            porRotulo[f["rotulo_es"]].append (f)

    print()
    print ("== EL MISMO ROTULO EN CONTROLES DISTINTOS ==")
    print ("%-14s %-26s %s" % ("rotulo ES", "en ingles", "donde"))
    for rot in sorted (porRotulo):
        g = porRotulo[rot]
        if len (g) < 2:
            continue
        ings = sorted ({f["rotulo_en"] for f in g if f["rotulo_en"]})
        #  Si en ingles se separan, el homonimo es SOLO de esta compilacion, y
        #  eso es peor que un duplicado: en una lengua se distinguen y en otra
        #  no, asi que nadie que lea el codigo en ingles lo ve.
        marca = "  <-- solo en espanol" if len (ings) > 1 else ""
        print ("%-14s %-26s %s%s" % (rot[:14], " / ".join (ings)[:26],
                                     ", ".join (sorted ({f["pantalla"] for f in g}))[:44], marca))

    porIcono = collections.defaultdict (set)
    for f in unicos.values():
        if f["icono"]:
            porIcono[f["icono"]].add ((f["rotulo_es"], f["pantalla"]))
    print()
    print ("== EL MISMO DIBUJO EN ROTULOS DISTINTOS ==")
    for ico in sorted (porIcono):
        rots = {r for r, _ in porIcono[ico]}
        if len (rots) < 2:
            continue
        print ("%-14s %s" % (ico, ", ".join (sorted (rots))[:78]))

    # ---- veredicto -------------------------------------------------------
    #  Una medida que no puede decir que no vale tan poco como una que no se ha
    #  corregido antes de creerla: si ninguna pantalla falla ningun minimo, o
    #  esta todo bien o la comprobacion no esta enchufada.
    sinTitulo = [k for k in claves if hechos.get (k) and "titulo" in hechos[k]["falta"]]
    sinCerrar = [k for k in claves if hechos.get (k) and "cerrar" in hechos[k]["falta"]]
    sinPlay   = [k for k in claves if hechos.get (k) and "transporte" in hechos[k]["falta"]]
    print()
    print ("%d pantallas, %d controles en %s/controles.csv"
           % (len (claves), len (todas), os.path.relpath (SALIDA, ROOT)))
    print ("sin titulo: %s" % (", ".join (k or "cara" for k in sinTitulo) or "ninguna"))
    print ("sin cruz de cerrar: %s" % (", ".join (k or "cara" for k in sinCerrar) or "ninguna"))
    #  Y CON QUE CARACTER. El mismo control escrito de dos formas es la version
    #  mas pequena de «una funcion, un dueno»: se lee igual y no es el mismo.
    cruces = collections.defaultdict (list)
    for k in claves:
        d = hechos.get (k)
        if d and d.get ("cruz"):
            cruces[d["cruz"]].append (k or "cara")
    if len (cruces) > 1:
        print ("la cruz de cerrar se escribe de %d formas:" % len (cruces))
        for c, ks in sorted (cruces.items(), key=lambda q: -len (q[1])):
            print ("   %r  en %d fichas: %s" % (c, len (ks), ", ".join (ks)[:60]))
    print ("escriben sonido y no tienen PLAY a mano: %s"
           % (", ".join (k or "cara" for k in sinPlay) or "ninguna"))
    return 0


if __name__ == "__main__":
    sys.exit (main())

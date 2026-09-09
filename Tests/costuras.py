#!/usr/bin/env python3
# ============================================================================
#  LAS TRES PALABRAS GRABADAS DE LA CARA, CENTRADAS EN EL HUECO QUE SE VE.
#
#  CONTROL, EFECTOS y PADS son silkscreen: la palabra parte el rayado y las dos
#  mitades entran desde los bordes. `engraveIn` la pone en el punto medio de
#  los dos numeros que recibe, y esos dos eran los de la banda que `resized()`
#  RESERVA — que no es el hueco que se PINTA. Una fila de tapas deja
#  `aireTapaVertical` vacio dentro de su propio rectangulo (la tapa se pinta
#  tres cuartos) y un plato pinta hasta su borde, asi que la banda y el hueco
#  se separan por cinco pixeles y la palabra se va con ellos.
#
#  `ctrlSeamTop` ya descontaba ese aire para CONTROL — su comentario lo cuenta —
#  y las otras dos costuras se quedaron sin el, que es media regla. Medido en
#  412x915 antes del arreglo: el hueco de EFECTOS se ve de 367 a 401 (centro
#  384) y el rayado caia en 380.5; el de PADS de 431 a 487 (centro 459) y caia
#  en 462.5. Tres pixeles y medio cada uno y en direcciones CONTRARIAS, porque
#  EFECTOS tiene el plato encima y la fila debajo y PADS al reves.
#
#  Y NINGUNA DE LAS TRECE REGLAS DE `expo.py` PUEDE VERLO: el rayado y la
#  palabra se PINTAN, no son componentes, y ademas estan perfectamente dentro
#  de la ventana, sin solapar a nadie y traducidos.
#
#  LAS DOS MITADES, y la segunda es la que hace que esto no sea una linea que
#  imprime OK:
#
#    - la app DICE donde dibujo el rayado (`UiAudit::costura`), que es la mitad
#      que se juzga;
#    - y la FOTO dice donde esta el hueco, contando pixeles hacia arriba y
#      hacia abajo hasta que el color deja de ser el del chasis.
#
#  Publicar tambien los dos bordes que `engraveIn` recibio seria repetir la
#  constante del codigo y el banco daria verde con el fallo puesto — que es
#  exactamente lo que `Tests/icono.py` ya hizo dos veces con la mascara del
#  lanzador. Aqui el juez es la imagen.
#
#      python3 Tests/costuras.py
# ============================================================================
import json, os, subprocess, sys, tempfile, zlib, struct

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
sys.path.insert (0, os.path.join (ROOT, "Tests"))
from expo import SIZES                      # una lista de pantallas, un dueño

#  Lo que separa el chasis de lo que hay encima. El cuerpo lleva un degradado
#  vertical, asi que dos filas seguidas de chasis se diferencian en uno o dos
#  niveles: por debajo de tres esto contaria el propio degradado como «algo
#  dibujado», que es el mismo falso positivo que ya costo una medida en la
#  mascara del icono.
TOL   = 3
#  Hasta donde se busca el borde. Mas alla no hay costura que valga: son
#  cuarenta px de fila mas su aire.
VENTANA = 80
#  Cuanto puede desviarse la palabra del centro del hueco. El rayado ocupa dos
#  filas -tinta y brillo- asi que medio pixel es el redondeo y no un desvio.
LISTON = 1.0


def display_alive():
    d = os.environ.get ("DISPLAY", ":99")
    try:
        return subprocess.run (["xdpyinfo", "-display", d], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, timeout=10).returncode == 0
    except Exception:
        return False


def lee_png (ruta):
    d = open (ruta, "rb").read()
    i, idat, w, h, ct = 8, b"", 0, 0, 6
    while i < len (d):
        ln = struct.unpack (">I", d[i:i+4])[0]
        t  = d[i+4:i+8]
        c  = d[i+8:i+8+ln]
        if   t == b"IHDR": w, h, _bd, ct = struct.unpack (">IIBB", c[:10])
        elif t == b"IDAT": idat += c
        i += 12 + ln
    raw = zlib.decompress (idat)
    bpp = 4 if ct == 6 else 3
    stride = w * bpp
    out, prev, o = bytearray(), bytearray (stride), 0
    for _y in range (h):
        f = raw[o]; o += 1
        line = bytearray (raw[o:o+stride]); o += stride
        for x in range (stride):
            a = line[x-bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x-bpp] if x >= bpp else 0
            if   f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs (p - a), abs (p - b), abs (p - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        out += line; prev = line
    return w, h, bpp, bytes (out)


def corre (size, shot):
    casa = tempfile.mkdtemp (prefix="zati-costura-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG="es",
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
    if shot: env["ZATI_SHOT"] = shot
    out = subprocess.run ([APP], env=env, capture_output=True, timeout=300)
    filas = []
    for l in out.stdout.decode ("utf8", "replace").splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: filas.append (json.loads (l))
        except Exception: pass
    return filas


def borde (px, w, bpp, x, y0, paso):
    """Primera fila, desde y0 y en la direccion `paso`, cuyo color no es el
       del chasis. Devuelve None si no aparece ninguna dentro de la ventana."""
    def at (yy):
        s = (yy * w + x) * bpp
        return px[s], px[s+1], px[s+2]
    ref = at (y0)
    for k in range (1, VENTANA):
        y = y0 + paso * k
        if y < 0: return None
        c = at (y)
        if max (abs (c[i] - ref[i]) for i in range (3)) > TOL:
            return y
        ref = c                                # el degradado se sigue, no se acumula
    return None


def hueco (px, w, bpp, x0, x1, y):
    """El hueco que se VE alrededor del rayado: la tinta mas CERCANA por
       arriba y por abajo de todo el tramo. Con una sola columna, la de
       CONTROL caia entre dos tapas del transporte y el hueco salia sesenta
       pixeles mas alto — el ojo centra contra las tapas, no contra el aire
       que queda entre ellas."""
    arriba, abajo = None, None
    for x in range (max (0, x0), min (w, x1 + 1), 2):
        a = borde (px, w, bpp, x, y - 3, -1)
        b = borde (px, w, bpp, x, y + 3, +1)
        if a is not None: arriba = a if arriba is None else max (arriba, a)
        if b is not None: abajo  = b if abajo  is None else min (abajo,  b)
    return arriba, abajo


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario")
    if not display_alive():      sys.exit ("la pantalla virtual no responde")

    malas, medidas, sin_medir = 0, 0, 0
    for size, _nombre in SIZES:
        dump = corre (size, None)
        cost = [d for d in dump if "costura" in d]
        png  = os.path.join (tempfile.mkdtemp (prefix="zati-shot-"), "cara.png")
        corre (size, png)
        if not os.path.exists (png):
            print ("%-9s no salio la foto" % size); malas += 1; continue
        w, h, bpp, px = lee_png (png)

        for c in cost:
            y, x0, x1 = c["y"], c["x0"], c["x1"]
            if not (4 < y < h - 4): sin_medir += 1; continue
            #  Desde DENTRO del hueco y no desde el rayado: la tinta del propio
            #  rayado seria el primer borde y esto mediria cero siempre.
            arriba, abajo = hueco (px, w, bpp, x0, x1, y)
            if arriba is None or abajo is None:
                sin_medir += 1
                continue
            centro = (arriba + 1 + abajo) / 2.0
            desvio = abs (centro - y)
            medidas += 1
            if desvio > LISTON:
                malas += 1
                print ("%-9s FALLA  hueco %d..%d centro %.1f  rayado en %d  (%.1f px)"
                       % (size, arriba + 1, abajo - 1, centro, y, desvio))

    #  CADENA DE CONTROL. Un banco que no encuentra un solo hueco daria verde
    #  sin haber mirado nada, que es la peor forma de pasar. La cara tiene tres
    #  costuras y siete pantallas: si no se miden al menos dos por pantalla, lo
    #  que falla es la medida y no la app.
    if medidas < 2 * len (SIZES):
        print ("la foto no encuentra los huecos: %d costuras medidas de %d"
               % (medidas, medidas + sin_medir))
        malas += 1

    print()
    print ("%d costuras medidas, %d sin borde a la vista" % (medidas, sin_medir))
    print ("las palabras grabadas van centradas en su hueco" if malas == 0
           else "%d costuras descentradas" % malas)
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

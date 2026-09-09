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
#  Y LA SEGUNDA REGLA DE ESTE FICHERO: NINGUNA FILA DEL MEDIDOR SE SALE DEL
#  CRISTAL. Es lo mismo por el otro lado -algo que se PINTA en la cara y que
#  ninguna de las trece reglas de `expo.py` puede ver, porque no es un
#  componente- y llevaba una tanda entera roto: la tercera fila, la del canal,
#  se dibujaba MEDIO CORTADA por el filo de abajo y por eso su «01» no se
#  entendia. `kMeterBand` valia 22 con un comentario al lado afirmando que «la
#  tercera cabe entera»: la banda util son 22 - 2*kMeterAire = 16 px y a paso
#  de 7 la tercera fila pide de la 14 a la 21. Roto a proposito devolviendo el
#  22: «la fila canal del medidor va de 106 a 113 y el cristal mide 110: se
#  sale 3 px», en las SIETE pantallas. Una afirmacion sin medida, la enesima.
#
#  Aqui el juez NO es la foto sino los dos rectangulos que la app publica -el
#  del cristal y el de cada fila- y eso no es repetir la constante: los
#  publica la MISMA funcion que los dibuja (`SpectrumDisplay::row`), asi que
#  la pregunta es «lo dibujado cabe en lo dibujado». Un `apunta` al lado
#  calculando el rectangulo otra vez si habria sido la regla escrita dos
#  veces, y por eso se fusionaron.
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


def espectro (size):
    """EL ESPECTRO DEL CRISTAL, con las DOS cifras.

       Son las dos formas de escribir esto mal y las dos se dibujan preciosas:
       una capa que pinta RUIDO se mueve con señal y tambien sin ella, y una
       que es un ADORNO no se mueve con ninguna. Con una sola cifra las dos
       pasan — es la misma pareja que `Tests/rack.py` le pide a la capa viva de
       los visores del plato.

       La app publica el pico del ANALIZADOR y no una bandera que se ponga a si
       misma, que es el fallo de `caraLista`: un booleano decia «tapada» con la
       cara entera a la vista."""
    def corrida (sonando):
        env = dict (os.environ, HOME=tempfile.mkdtemp (prefix="zati-esp-"),
                    ZATI_AUDIT="1", ZATI_SIZE=size, ZATI_LANG="es", ZATI_DEMO="1",
                    ZATI_SPIN="3", DISPLAY=os.environ.get ("DISPLAY", ":99"))
        if sonando: env["ZATI_SONANDO"] = "1"
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=300)
        for l in out.stdout.decode ("utf8", "replace").splitlines():
            l = l.strip()
            if l.startswith ("{") and "\"spin\"" in l:
                try: return json.loads (l).get ("espectro_db")
                except Exception: return None
        return None
    return corrida (False), corrida (True)


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario")
    if not display_alive():      sys.exit ("la pantalla virtual no responde")

    malas, medidas, sin_medir, filas, rotulos = 0, 0, 0, 0, []
    for size, _nombre in SIZES:
        dump = corre (size, None)
        cost = [d for d in dump if "costura" in d]

        #  Y EL ROTULO DE LA TERCERA FILA CABE EN LA CAJA DONDE CAYO. La app
        #  publica lo que DIBUJA y su caja, no la palabra larga y la celda
        #  ancha pase lo que pase: la rama corta mete dos cifras en un canalon
        #  de diez pixeles, que es la unica de las dos que de verdad puede no
        #  caber. Se imprime ademas por cual salio, que hoy es siempre la
        #  larga -de 24 px en chino a 37 en ingles contra los 68 de la celda-
        #  y sin eso la escalera no se ha visto caer.
        for r in [d for d in dump if d.get ("vurot")]:
            rotulos.append ((size, r["texto"], r["pide"], r["tiene"]))
            if r["pide"] > r["tiene"]:
                malas += 1
                print ("%-9s FALLA  el rotulo del medidor dice %s, pide %d px y tiene %d"
                       % (size, r["texto"], r["pide"], r["tiene"]))

        #  EL MEDIDOR CABE ENTERO. Sin el cristal no hay contra que medir, asi
        #  que su ausencia es un fallo y no un caso que se salta.
        vus  = [d for d in dump if d.get ("vu")]
        cris = next ((v for v in vus if v["que"] == "cristal"), None)
        if cris is None:
            print ("%-9s el cristal no se publica: el medidor no se mide" % size)
            malas += 1
        else:
            for v in vus:
                if v["que"] == "cristal": continue
                filas += 1
                fuera = (v["y"] + v["h"]) - (cris["y"] + cris["h"])
                if fuera > 0 or v["y"] < cris["y"]:
                    malas += 1
                    print ("%-9s FALLA  la fila %s del medidor va de %d a %d y el cristal"
                           " mide %d: se sale %d px"
                           % (size, v["que"], v["y"], v["y"] + v["h"], cris["h"], fuera))
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

    #  Y LA MISMA CADENA PARA EL MEDIDOR: son tres filas por pantalla, asi que
    #  con menos de tres por pantalla lo que falla es la medida.
    if filas < 3 * len (SIZES):
        print ("solo %d filas de medidor de %d: el medidor no se esta midiendo"
               % (filas, 3 * len (SIZES)))
        malas += 1

    #  Y EL ESPECTRO DEL CRISTAL. Una pantalla basta: lo que se mide no es
    #  geometria sino si la capa viva sale del DSP, y eso no cambia con el
    #  tamano de la ventana. El suelo lo dice el propio analizador (-78 dB).
    SUELO = -78.0
    quieto, sonando = espectro (SIZES[0][0])
    if quieto is None or sonando is None:
        print ("el cristal no publica su espectro: no se mide"); malas += 1
    else:
        if sonando <= SUELO + 6.0:
            print ("FALLA  el espectro no se mueve con señal: %.1f dB con el suelo en %.0f"
                   % (sonando, SUELO)); malas += 1
        if quieto > SUELO + 1.0:
            print ("FALLA  el espectro dibuja ruido con la maquina callada: %.1f dB"
                   % quieto); malas += 1
        print ("el espectro del cristal: %.1f dB sonando y %.1f callada"
               % (sonando, quieto))

    print()
    print ("%d costuras medidas, %d sin borde a la vista, %d filas de medidor"
           % (medidas, sin_medir, filas))
    if rotulos:
        cortos = [r for r in rotulos if len (r[1]) <= 2]
        print ("el rotulo de la tercera fila: %d con la palabra entera, %d con las dos"
               " cifras en el canalon  (pide %d..%d px)"
               % (len (rotulos) - len (cortos), len (cortos),
                  min (r[2] for r in rotulos), max (r[2] for r in rotulos)))
    print ("las palabras grabadas van centradas y el medidor cabe entero" if malas == 0
           else "%d hallazgos" % malas)
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

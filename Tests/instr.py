#!/usr/bin/env python3
# ============================================================================
#  LOS DOSCIENTOS CINCUENTA Y SEIS INSTRUMENTOS.
#
#  Un pad con un instrumento no se juzga como un pad con un golpe. Un golpe se
#  toca y se acaba; un instrumento tiene que aguantar dos octavas arriba y dos
#  abajo, responder al toque y sostener mientras la nota dure. Eso son cuatro
#  cosas que ninguna prueba anterior de esta casa mira, y tres de ellas no se
#  ven en una sola muestra: se ven COMPARANDO zonas.
#
#  Lo que se mide, y por que cada una:
#
#  EL MAPA. Cinco raices por dos capas, las raices embaldosando -24..+24 sin
#  huecos ni sobras, y las ventanas seguidas y sin solaparse. Un mapa mal
#  escrito no suena mal: suena a otra octava, y eso no lo caza ninguna medida
#  de nivel.
#
#  QUE NINGUNO ESTE MUDO, QUE LOS 256 ESTEN IGUALADOS Y QUE HAYA MARGEN. Las
#  mismas tres que la fabrica, con la misma ponderacion K de BS.1770 y la misma
#  ventana de 400 ms - importadas de Tests/kits.py, no reescritas: el liston de
#  una prueba no se reinventa en la de al lado.
#
#  QUE NO HAYA DOS QUE SEAN EL MISMO SONIDO. Es la unica que no se puede hacer
#  mirandolos de uno en uno, y es la que caza el fallo gordo: dieciseis familias
#  que en realidad son la misma sierra con los numeros movidos sacan
#  sobresaliente en todas las demas. Es literalmente lo que le paso a los bancos
#  A y B de la fabrica.
#
#  Con DOS listones y no uno, que es la diferencia con kits.py: dos presets de
#  la misma familia SON parecidos a proposito -son variaciones-, y dos familias
#  distintas no tienen excusa. Cuatro decibelios entre familias, dos dentro.
#
#  LAS DOS CAPAS, EN DOS NUMEROS. Cuanto cambia el nivel y cuanto cambia el
#  centroide. Solo el primero lo saca un fader con pasos, que es exactamente lo
#  que esta funcion no puede ser. Es la misma regla de QUITAR RUIDO, donde solo
#  bajar el suelo lo hace un silenciador y solo respetar el tono lo hace no
#  hacer nada.
#
#  LAS OCTAVAS. La zona de +12 tiene que ser la de 0 DESPLAZADA, no otra cosa:
#  se compara el espectro en bandas logaritmicas, donde una octava son tres
#  bandas exactas, y el desplazamiento correcto tiene que ganarle al no
#  desplazamiento. Una zona rendida a la frecuencia equivocada da un salto al
#  cruzar la costura y nada mas lo ve.
#
#  LA COSTURA DEL BUCLE. El salto entre la ultima muestra del bucle y la
#  primera, contra el pico de pendiente de los cinco milisegundos de al lado -
#  la misma regla que kits.py acabo usando despues de mentir con la mediana.
#
#      python3 Tests/instr.py
# ============================================================================
import json, math, os, shutil, subprocess, sys, tempfile

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import (APP, BANDS, ENVBINS, FLOOR, NFFT, MAX_LOUD_SPREAD_DB,
                  MAX_PEAK, MIN_LOUD, MIN_PEAK, descriptor, display_alive,
                  distancia, fft, load, loudness)

RAICES   = [-24, -12, 0, 12, 24]
CAPAS    = 2
ZONAS    = len (RAICES) * CAPAS
SR       = 48000.0

#  LOS DOS LISTONES, Y DE DONDE SALEN.
#
#  kits.py usa 4.0 dB para sesenta y cuatro golpes de percusion, y su mediana de
#  pares es 28 dB. Aqui la poblacion es otra -256 sonidos TONALES, todos
#  armonicos y todos sostenidos- y la mediana se queda en 19.6: la escala esta
#  comprimida porque todo se parece mas de partida. El liston equivalente es el
#  mismo numero en proporcion, 4.0 x 19.6/28 = 2.8, y de ahi los 3.0.
#
#  No es bajar el liston para que pase: es medirlo en la misma escala. Un liston
#  copiado de otra poblacion dice que si o que no por la poblacion, no por el
#  sonido.
PAR_FAM  = 3.0     # dos FAMILIAS por debajo de esto son el mismo algoritmo
PAR_PRE  = 1.5     # dos presets de una familia son variaciones: menos exige

#  Las dos capas. Los dos numeros a la vez o no vale.
CAPA_DB  = 2.0     # decibelios entre suave y fuerte
CAPA_HZ  = 1.12    # y los agudos, en RAZON: 12% de energia alta de mas
#  Y ENTRE OCTAVAS DE UN MISMO PRESET. Este si puede fallar: la ganancia se
#  saca de UNA zona -la raiz 0, capa fuerte- y se aplica a las diez, asi que si
#  una octava sale mucho mas sonora que otra, el instrumento pega un salto al
#  cruzar la costura y nadie mas lo ve. Ocho decibelios: un instrumento de
#  verdad tampoco suena igual de fuerte en todo su registro.
#  Tres decibelios desde que cada octava lleva su propia ganancia: lo que queda
#  es lo que la capa fuerte y la suave se llevan de diferencia dentro de su
#  octava, y eso tiene que ser poco. Con una sola ganancia para las diez zonas
#  esto media 9.9 dB.
OCTAVA_DB = 3.0
#  El salto del bucle contra el pico de pendiente de al lado. Dos es la
#  pendiente maxima que una senal limitada en banda puede tener, y es el mismo
#  numero que kits.py usa para los chasquidos.
SALTO_MAX = 2.0


def agudos (x):
    """ENERGIA ALTA CONTRA ENERGIA TOTAL: la diferencia entre muestras es un
    paso alto de primer orden, asi que esto pesa cada frecuencia por f.

    Y no el centroide, que fue la primera version y midio mal. Casi todo lo que
    hay aqui tiene espectro 1/f -una sierra, un pulso, ocho barras-, y en un
    espectro 1/f el centroide se lo lleva la fundamental: METALES abriendo el
    filtro de 500 a 1100 Hz movia el centroide de 185 a 186 Hz. Con esta medida
    el mismo cambio se ve. Es ademas la que usa el banco del motor
    (Tests/StressTest.cpp) para lo mismo, asi que los dos lados dicen lo mismo."""
    alta = total = 0.0
    prev = 0.0
    for v in x:
        d = v - prev
        alta += d * d; total += v * v
        prev = v
    return math.sqrt (alta / total) if total > 1e-12 else 0.0


def centroide (x, n=8192):
    """Centro de gravedad del espectro, en Hz. Es el numero que dice si algo
    suena MAS BRILLANTE, que es lo que tiene que separar las dos capas."""
    y = list (x[:n]); y += [0.0] * (n - len (y))
    re = [y[i] * (0.5 - 0.5 * math.cos (2.0 * math.pi * i / (n - 1))) for i in range (n)]
    im = [0.0] * n
    fft (re, im)
    num = den = 0.0
    for k in range (1, n // 2):
        p = re[k] * re[k] + im[k] * im[k]
        num += k * SR / n * p
        den += p
    return num / den if den > 0.0 else 0.0


def bandas (x, n=8192):
    """Espectro en las mismas bandas logaritmicas que el descriptor, para poder
    preguntar si una octava es la de abajo DESPLAZADA."""
    y = list (x[:n]); y += [0.0] * (n - len (y))
    re = [y[i] * (0.5 - 0.5 * math.cos (2.0 * math.pi * i / (n - 1))) for i in range (n)]
    im = [0.0] * n
    fft (re, im)
    lo, hi = 30.0, 20000.0
    pot = [1e-20] * BANDS
    for k in range (1, n // 2):
        f = k * SR / n
        if f < lo or f >= hi: continue
        b = min (BANDS - 1, max (0, int (BANDS * math.log (f / lo) / math.log (hi / lo))))
        pot[b] += re[k] * re[k] + im[k] * im[k]
    sp = [10.0 * math.log10 (v) for v in pot]
    top = max (sp)
    return [max (FLOOR, v - top) for v in sp]


def pendiente_local (x, i, radio):
    """El pico de |x[n]-x[n-1]| en los `radio` datos de al lado. Contra la
    mediana del sonido entero esta medida MINTIO en kits.py: un sonido con
    silencio de sobra tiene mediana casi cero y cualquier ciclo normal sale
    culpable."""
    a = max (1, i - radio); b = min (len (x), i + radio)
    return max ((abs (x[k] - x[k - 1]) for k in range (a, b)), default=0.0)


def corre (dirtemp):
    casa = tempfile.mkdtemp (prefix="zati-instr-")
    try:
        env = dict (os.environ)
        env.update ({"HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
                     "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
                     "ZATI_OPEN": "pads", "ZATI_INSTR": dirtemp})
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=1800).stdout
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    filas, extra = [], {}
    for linea in out.splitlines():
        linea = linea.strip()
        if not linea.startswith ('{'): continue
        try:    d = json.loads (linea)
        except Exception: continue
        if d.get ("instr") == "preset": filas.append (d)
        elif d.get ("instr") == "bancoD": extra["bancoD"] = d["ms"]
        elif d.get ("instr") == "vuelta": extra["vuelta"] = d
        elif d.get ("instr") == "error":  extra["error"] = d.get ("que", "")
    return filas, extra


def main():
    if not os.path.exists (APP):
        print ("FALLA  no hay binario: %s" % APP); return 1
    if not display_alive():
        print ("FALLA  no hay DISPLAY: arranca Xvfb :99"); return 1

    dirtemp = tempfile.mkdtemp (prefix="zati-instr-wav-")
    try:
        filas, extra = corre (dirtemp)
        if "error" in extra:
            print ("FALLA  la app no pudo escribir: %s" % extra["error"]); return 1
        if len (filas) != 256:
            print ("FALLA  salieron %d presets y son 256" % len (filas)); return 1

        fallos, medidas = [], []

        # ---- EL MAPA -----------------------------------------------------
        for d in filas:
            etiq = "%s %s" % (d["familia"], d["nombre"])
            if d["zonas"] != ZONAS:
                fallos.append ("%s: %d zonas y son %d" % (etiq, d["zonas"], ZONAS)); continue
            mapa = d["mapa"]
            raices = sorted (set (z[0] for z in mapa))
            if raices != RAICES:
                fallos.append ("%s: las raices son %s y tenian que ser %s" % (etiq, raices, RAICES))
            if sorted (set (z[1] for z in mapa)) != list (range (CAPAS)):
                fallos.append ("%s: las capas son %s" % (etiq, sorted (set (z[1] for z in mapa))))
            #  Ventanas seguidas, sin solapes y dentro del buffer: un mapa que
            #  se sale no suena mal, suena a la zona de al lado.
            prev = 0
            for z in mapa:
                if z[2] != prev or z[3] <= z[2] or z[3] > d["muestras"]:
                    fallos.append ("%s: ventana [%d,%d) rota (venia de %d, hay %d)"
                                   % (etiq, z[2], z[3], prev, d["muestras"]))
                    break
                if d["sostiene"] and not (z[2] <= z[4] < z[5] <= z[3]):
                    fallos.append ("%s: bucle [%d,%d) fuera de su ventana [%d,%d)"
                                   % (etiq, z[4], z[5], z[2], z[3]))
                    break
                if not d["sostiene"] and z[5] > z[4]:
                    fallos.append ("%s: no sostiene y trae bucle" % etiq); break
                prev = z[3]

        # ---- LOS 256, UNO A UNO ------------------------------------------
        descs = []
        for d in filas:
            f, p = d["fam"], d["pre"]
            etiq = "%s %s" % (d["familia"], d["nombre"])
            x = load (os.path.join (dirtemp, "ref-%02d-%02d.wav" % (f, p)))
            if not x:
                fallos.append ("%s: no escribio audio" % etiq); continue
            pico = max (abs (v) for v in x)
            son  = loudness (x)
            descs.append ((f, etiq, descriptor (x)))
            medidas.append ((etiq, pico, son))
            if pico < MIN_PEAK: fallos.append ("%s mudo (pico %.4f)" % (etiq, pico))
            if son  < MIN_LOUD: fallos.append ("%s no suena (sonoridad %.4f)" % (etiq, son))
            if pico > MAX_PEAK: fallos.append ("%s sin margen (pico %.3f)" % (etiq, pico))

        #  LOS 256 ENTRE SI. Sale clavado a cero casi siempre porque la
        #  ganancia se calcula para dejarlo asi: lo que esta linea caza no es un
        #  desajuste de mezcla sino que el limitador de aplicaGanancia MUERDA -
        #  un preset que necesita mucha ganancia se dobla en el codo y se queda
        #  corto, y entonces si baila. La que mide de verdad es la de octavas,
        #  mas abajo.
        sons = [m[2] for m in medidas if m[2] > 1e-9]
        if sons:
            spread = 20.0 * math.log10 (max (sons) / min (sons))
            print ("sonoridad: baila %.1f dB entre los 256" % spread)
            if spread > MAX_LOUD_SPREAD_DB:
                fallos.append ("la sonoridad baila %.1f dB entre el mas y el menos sonoro" % spread)

        # ---- QUE NO HAYA DOS IGUALES -------------------------------------
        pares = []
        for i in range (len (descs)):
            for j in range (i + 1, len (descs)):
                pares.append ((distancia (descs[i][2], descs[j][2]),
                               descs[i][0] == descs[j][0], descs[i][1], descs[j][1]))
        pares.sort (key=lambda t: t[0])
        for dist, misma, a, b in pares:
            liston = PAR_PRE if misma else PAR_FAM
            if dist < liston:
                fallos.append ("%s y %s son el mismo sonido (%.2f dB, liston %.1f)"
                               % (a, b, dist, liston))
        print ("pares: %d, el mas cercano %.2f dB (%s / %s), mediana %.2f"
               % (len (pares), pares[0][0], pares[0][2], pares[0][3],
                  pares[len (pares) // 2][0]))
        for dist, misma, a, b in pares[:5]:
            print ("   %6.2f dB  %-24s %-24s %s" % (dist, a, b, "misma familia" if misma else ""))

        # ---- ESTRUCTURA: capas, octavas y bucle --------------------------
        for d in filas:
            if d["pre"] != 0: continue
            etiq = d["familia"]
            x = load (os.path.join (dirtemp, "todo-%02d.wav" % d["fam"]))
            if not x:
                fallos.append ("%s: no escribio el preset entero" % etiq); continue
            mapa = d["mapa"]

            #  LAS DOS CAPAS DE LA RAIZ CENTRAL, en dos numeros.
            zs = [z for z in mapa if z[0] == 0]
            suave = [z for z in zs if z[1] == 0][0]
            duro  = [z for z in zs if z[1] == 1][0]
            a = x[suave[2]:suave[3]]; b = x[duro[2]:duro[3]]
            la, lb = loudness (a), loudness (b)
            ca, cb = agudos (a), agudos (b)
            db = 20.0 * math.log10 (lb / la) if la > 1e-9 and lb > 1e-9 else 0.0
            raz = cb / ca if ca > 1e-6 else 1.0
            print ("%-12s capas: %+5.1f dB   agudos x%.2f  (%.4f -> %.4f)"
                   % (etiq, db, raz, ca, cb))
            if db < CAPA_DB:
                fallos.append ("%s: la capa fuerte solo sube %.1f dB" % (etiq, db))
            if raz < CAPA_HZ:
                fallos.append ("%s: las dos capas suenan igual de brillantes (x%.2f): "
                               "es un fader, no una capa" % (etiq, raz))

            #  LAS CINCO OCTAVAS, EN SONORIDAD. La ganancia sale de una sola
            #  zona, asi que esto SI puede desmadrarse - y es lo que la linea de
            #  arriba no puede ver, porque los 256 salen clavados por
            #  construccion.
            porOctava = []
            for z in [q for q in mapa if q[1] == 1]:
                porOctava.append ((z[0], loudness (x[z[2]:z[3]])))
            vivos = [v for _, v in porOctava if v > 1e-9]
            if len (vivos) == len (porOctava) and vivos:
                salto = 20.0 * math.log10 (max (vivos) / min (vivos))
                print ("%-12s octavas: baila %.1f dB   (%s)"
                       % ("", salto, "  ".join ("%+d:%.3f" % (r, v) for r, v in porOctava)))
                if salto > OCTAVA_DB:
                    fallos.append ("%s: la sonoridad baila %.1f dB entre sus octavas"
                                   % (etiq, salto))
            else:
                fallos.append ("%s: alguna octava esta muda (%s)"
                               % (etiq, ["%.4f" % v for _, v in porOctava]))

            #  LAS OCTAVAS: +12 tiene que ser 0 DESPLAZADA.
            #  Una octava son BANDAS*ln2/ln(20000/30) bandas exactas.
            paso = int (round (BANDS * math.log (2.0) / math.log (20000.0 / 30.0)))
            z0 = [z for z in mapa if z[0] == 0  and z[1] == 1][0]
            z1 = [z for z in mapa if z[0] == 12 and z[1] == 1][0]
            s0 = bandas (x[z0[2]:z0[3]])
            s1 = bandas (x[z1[2]:z1[3]])
            #  Solo donde las dos tienen banda que comparar.
            recto  = sum ((s0[k] - s1[k]) ** 2 for k in range (BANDS - paso))
            movido = sum ((s0[k] - s1[k + paso]) ** 2 for k in range (BANDS - paso))
            if movido >= recto:
                fallos.append ("%s: la octava de arriba no es la de abajo desplazada "
                               "(desplazada %.0f, sin desplazar %.0f)" % (etiq, movido, recto))

            #  LA COSTURA DEL BUCLE.
            if d["sostiene"]:
                for z in mapa:
                    salto = abs (x[z[4]] - x[z[5] - 1])
                    cerca = max (pendiente_local (x, z[4], 240),
                                 pendiente_local (x, z[5] - 1, 240))
                    if cerca > 1e-6 and salto > SALTO_MAX * cerca:
                        fallos.append ("%s: la vuelta del bucle de la raiz %+d capa %d salta "
                                       "%.4f contra %.4f de al lado: CLICK"
                                       % (etiq, z[0], z[1], salto, cerca))
                        break

        # ---- QUE VUELVA SIENDO UN INSTRUMENTO --------------------------
        v = extra.get ("vuelta")
        if v is None:
            fallos.append ("no hay linea de vuelta: el proyecto no se probo")
        else:
            esperado = 3 * 16 + 5
            print ("vuelta: receta %d (esperada %d)  familia %d preset %d  %d zonas  "
                   "escribe wav %d" % (v["receta"], esperado, v["fam"], v["pre"],
                                       v["zonas"], v["escribeWav"]))
            if v["receta"] != esperado:
                fallos.append ("el proyecto guardo la receta %d y era la %d"
                               % (v["receta"], esperado))
            if v["fam"] != 3 or v["pre"] != 5:
                fallos.append ("volvio como familia %d preset %d y era 3/5" % (v["fam"], v["pre"]))
            if v["zonas"] != ZONAS:
                fallos.append ("volvio con %d zonas: ya no es un instrumento" % v["zonas"])
            if v["escribeWav"]:
                fallos.append ("el pad escribiria su audio a disco: son 2 MB por pad "
                               "para devolver algo que ya no seria un instrumento")

        if "bancoD" in extra:
            print ("\nllenar el banco D con los 16: %.0f ms" % extra["bancoD"])
        mega = sum (d["muestras"] for d in filas if d["pre"] == 0) * 4 / 1048576.0
        print ("los 16 del banco D ocupan %.1f MB" % mega)

        print()
        if fallos:
            for f in fallos[:40]: print ("FALLA  " + f)
            if len (fallos) > 40: print ("...y %d mas" % (len (fallos) - 40))
            return 1
        print ("256 instrumentos: ninguno mudo, ninguno repetido, y las octavas cuadran")
        return 0
    finally:
        shutil.rmtree (dirtemp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit (main())

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
                  MAX_PEAK, MIN_LOUD, MIN_PEAK, PANTALLA, descriptor, display_alive,
                  distancia, fft, load, loudness)

RAICES   = [-24, -12, 0, 12, 24]
#  TRES CAPAS. Ver `Sintes::kCapas`: con dos, el motor cambiaba de capa a mitad
#  del recorrido de fuerza y el salto era de 2.0 dB de golpe.
CAPAS    = 3
ZONAS    = len (RAICES) * CAPAS
RAIZ_HZ  = 130.8127827      # DO3, la raiz central. Ver Sintes.h
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
CAPA_DB  = 2.0     # decibelios entre la PRIMERA capa y la ULTIMA
CAPA_HZ  = 1.12    # y los agudos, en RAZON: 12% de energia alta de mas
#  Y EL SALTO ENTRE DOS CAPAS CONTIGUAS, que es la razon de que haya tres.
#
#  LA PRIMERA VERSION DE ESTE LISTON ESTABA MAL DERIVADA, y se vio midiendo:
#  decia `CAPA_DB / 2 + 0.3` = 1.3 dB, dando por hecho que el recorrido entero
#  eran los 2.0 dB de `CAPA_DB`. `CAPA_DB` es el MINIMO que el recorrido tiene
#  que tener, no lo que mide: el recorrido real va de **5.4 dB en BAJOS a 11.0
#  en CUERDA PULS**, asi que con tres capas el escalon no puede bajar de 2.7 ni
#  de 5.5 por mucho que se pida. Pedirle 1.3 era pedirle seis capas, y no caben:
#  `kMaxZonas` son dieciseis y tres por cinco raices ya son quince.
#
#  Lo que la tercera capa compra de verdad, y es lo que esto mide, es que los
#  escalones esten REPARTIDOS: de un salto unico de 5.4 dB a dos de 3.1 y 2.4.
#  Asi que el liston es RELATIVO al recorrido de cada familia: ningun escalon
#  puede llevarse mas del 60 % de lo que hay.
#
#  Y NO HAY TECHO ABSOLUTO, que fue el segundo intento y tambien estaba mal:
#  con 4.0 dB -dos veces `CAPA_DB`- CUERDA PULS suspendia por tener **11.0 dB de
#  recorrido**, que no es un defecto sino lo contrario. Un instrumento de verdad
#  tiene veinte o treinta decibelios entre el toque mas flojo y el mas fuerte;
#  once repartidos en tres capas son 5.5 por escalon y no hay forma de bajarlos
#  sin quitarle respuesta al toque o sin una cuarta capa, y no cabe: `kMaxZonas`
#  son dieciseis y tres por cinco raices ya son quince. Un techo absoluto
#  confunde «responde al toque» -que es lo que se quiere- con «da un salto».
CAPA_REPARTO = 0.60
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

#  ============================ EL ANCHO =====================================
#
#  DOS NUMEROS Y NO UNO, y los dos se derivan el uno del otro, que es lo que
#  hace que juntos acoten el intervalo entero de «ancho de verdad sin fase».
#
#  Para dos canales del mismo nivel con correlacion r, la suma en mono conserva
#  (1+r)/2 de la energia. O sea:
#
#    · perdida >= -1.5 dB   ES   r >= 0.416   -- el limite de abajo: por debajo
#      de ahi el instrumento se cae al sumarse, y un groovebox se toca en el
#      altavoz de un telefono.
#    · r <= 0.98            ES   perdida <= -0.04 dB -- el limite de arriba: por
#      encima de 0.98 los dos canales son el mismo canal y el mando ANCHO no
#      tiene nada que abrir. Es el lado a -20 dB del medio.
#
#  Las dos juntas dejan r en [0.42, 0.98].
ANCHO_R_MAX  = 0.98
ANCHO_MONO   = -1.5
#  Y BAJOS Y SUBS AL REVES, con su propio liston y no con una excepcion: tienen
#  que ser MONO BIT A BIT. Un grave descorrelado pierde hasta 3 dB en la suma y
#  mueve de sitio lo unico que tiene que estar clavado en el centro. Ver
#  `Sintes::monoDeVerdad`.
MONO_FAMS    = (0, 1)
MONO_R_MIN   = 0.9999

#  ============================ LO QUE FALTABA ================================
#
#  De pureza espectral, afinacion fina, ruido y coste la cobertura era
#  literalmente nula. Cada liston de aqui esta heredado y se dice de donde.

#  DC del cuerpo contra su pico. `FLOOR = -60.0` de `kits.py` -«por debajo de
#  esto ya es silencio y no forma»-, heredado literal. Una continua no se oye:
#  se come margen de pico y descuadra el limitador del master.
DC_DBFS   = -60.0

#  AFINACION. 2-3 cents es el JND de dos notas SIMULTANEAS -y cruzar la costura
#  entre dos zonas en un pasaje ligado es exactamente ese caso-; 5 el de notas
#  sucesivas. El relativo es el que no depende del estimador, porque el sesgo se
#  cancela al dividir; el absoluto solo se juzga donde el pico cae cerca del
#  fundamental, que es donde «el pico» y «la nota» son la misma cosa.
CENTS_REL = 3.0
CENTS_ABS = 5.0

#  DERIVA dentro del cuerpo: primer cuarto contra ultimo cuarto, en tercios de
#  octava. Heredado del 1.0 dB de la regla del pliegue, que a su vez es el JND
#  de una banda critica. Un cuerpo que deriva es un cuerpo que al dar la vuelta
#  pega un salto.
DERIVA_DB = 1.0

#  CUADRE DEL LFO: `lfoHz * bucleSeg` entero, o exactamente 0 si se congelo.
#  Cuatro veces mas estricto que el 0.02 de ciclos de la raiz, y se dice por
#  que: siete grados de la raiz son un salto de FASE y siete grados de un LFO
#  son un salto de TIMBRE.
LFO_SOBRA = 0.005

#  COSTE, en proporcion y no en milisegundos: la doctrina de `Cpu.cpp` citada,
#  *el valor absoluto no dice si va a ir; la proporcion si viaja*.
#
#  Y EL LISTON SE DERIVA DEL TRABAJO, no de lo que habia. Antes de esta tanda la
#  razon era 3.3. Lo que el trabajo ha crecido se cuenta: quince zonas en vez de
#  diez y dos canales en vez de uno (x3), cuatro veces la tasa de generacion mas
#  el diezmador (x4 largo), y un cuerpo de 1.00 s en vez de 0.42 (x2.4) — o sea
#  **x29**, que sobre 3.3 son 96. Medido salio **x104.6 de mediana**, un 9 % por
#  encima de la cuenta, que es el diezmador. Liston 130 y techo 400, con el 25 %
#  de margen de siempre.
#
#  Y se publica ADEMAS el numero absoluto, porque es el que decide si poner un
#  instrumento puede seguir bloqueando el hilo de mensajes: 600 ms de silencio
#  despues de tocar una tapa se leen como un boton roto.
COSTE_MED = 130.0
COSTE_PEOR = 400.0


def carga2 (path):
    """LOS DOS CANALES. `kits.load` devuelve el izquierdo -se salta el resto de
    cada trama- y eso vale para todo lo que mide TIMBRE, porque el timbre es el
    mismo en los dos. Para el ANCHO no vale: mediria su propia eleccion de
    canal."""
    import wave
    w = wave.open (path, "rb")
    n, sw, ch = w.getnframes(), w.getsampwidth(), w.getnchannels()
    raw = w.readframes (n); w.close()
    esc = float (1 << (8 * sw - 1))
    izq, der = [], []
    paso = sw * ch
    for i in range (0, len (raw) - paso + 1, paso):
        izq.append (int.from_bytes (raw[i:i + sw], "little", signed=True) / esc)
        j = i + sw if ch > 1 else i
        der.append (int.from_bytes (raw[j:j + sw], "little", signed=True) / esc)
    return izq, der


def goertzel (x, f, sr=48000.0):
    """Potencia de UNA frecuencia. Dos multiplicaciones por muestra, que es lo
    que permite barrer una rejilla de un cent sin pagar una FFT por punto."""
    w = 2.0 * math.pi * f / sr
    c = 2.0 * math.cos (w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2 = s1; s1 = s0
    return s1 * s1 + s2 * s2 - c * s1 * s2


def afina (x, esperada, n=4096):
    """LA FRECUENCIA DEL FUNDAMENTAL, en Hz, con rejilla de un cent.

    DEL FUNDAMENTAL Y NO DEL PARCIAL MAS FUERTE, que es lo que media la primera
    version y lo que la hizo mentir: en CUERDAS -siete sierras desafinadas a
    proposito- el pico cae en una de las siete y no en el centro, y la regla
    acuso a la familia de **+41 cents en la raiz y -32 en la octava** cuando lo
    que estaba midiendo era su propia eleccion de pico. Lo mismo pasaria en
    CAMPANAS, MAZOS y ARPAS, que son inarmonicos a proposito.

    Se puntua cada candidato por la SUMA de las potencias en f, 2f y 3f. Un
    parcial suelto gana en uno de los tres y pierde en los otros dos; el
    fundamental de verdad gana en los tres a la vez. Es la cuenta de un
    detector de tono de toda la vida, y aqui ademas es barata porque el barrido
    es de solo 161 puntos."""
    if len (x) < n: return 0.0
    #  Desde un cuarto del cuerpo, que es donde el ataque ya no manda.
    ini = len (x) // 4
    seg = x[ini:ini + n]
    if len (seg) < n: seg = x[:n]
    mejor, mejorP = esperada, -1.0
    #  Barrido de +-80 cents en pasos de un cent alrededor de la esperada: lo
    #  que se quiere medir es el desafine de la SINTESIS, y ochenta cents es
    #  mucho mas de lo que cualquiera de estos fallos ha producido nunca.
    for c in range (-80, 81):
        f = esperada * (2.0 ** (c / 1200.0))
        p = goertzel (seg, f) + goertzel (seg, 2.0 * f) + goertzel (seg, 3.0 * f)
        if p > mejorP: mejorP, mejor = p, f
    return mejor


def cents (a, b):
    if a <= 0.0 or b <= 0.0: return 0.0
    return 1200.0 * math.log (a / b, 2.0)


def ancho (izq, der):
    """La correlacion de los dos canales y lo que se pierde al sumarlos."""
    n = min (len (izq), len (der))
    if n < 16: return 1.0, 0.0
    sa = sb = sab = 0.0
    for i in range (n):
        a, b = izq[i], der[i]
        sa += a * a; sb += b * b; sab += a * b
    if sa < 1e-20 or sb < 1e-20: return 1.0, 0.0
    r = sab / math.sqrt (sa * sb)
    #  La energia de la suma en mono contra la media de las dos, que es lo que
    #  vale `10*log10((1+r)/2)` cuando los dos canales miden lo mismo — y esto
    #  lo mide sin suponerlo.
    sm = 0.0
    for i in range (n):
        m = 0.5 * (izq[i] + der[i]); sm += m * m
    ref = 0.5 * (sa + sb)
    return r, 10.0 * math.log10 (max (1e-30, sm) / max (1e-30, ref))


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
        #  DISPLAY va PUESTA: `display_alive` cae a ":99" cuando el entorno no
        #  la trae, asi que sin esta linea la comprobacion decia que si contra
        #  una pantalla y la app arrancaba sin ninguna — `salieron 0 presets y
        #  son 256` con el binario bueno. Ver `PANTALLA` en `kits.py`.
        env.update ({"DISPLAY": PANTALLA,
                     "HOME": casa, "XDG_DATA_HOME": os.path.join (casa, ".local", "share"),
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
        elif d.get ("instr") == "ref":    extra["msKits"] = d["msKits"]
        elif d.get ("instr") == "vuelta": extra["vuelta"] = d
        elif d.get ("instr") == "destino": extra["destino"] = d
        elif d.get ("instr") == "receta":  extra["receta"] = d
        elif d.get ("instr") == "pestana": extra["pestana"] = d
        elif d.get ("instr") == "mantener": extra["mantener"] = d
        elif d.get ("instr") == "puertas":  extra["puertas"] = d
        elif d.get ("instr") == "abre":     extra["abre"] = d
        elif d.get ("instr") == "teclado":  extra["teclado"] = d
        elif d.get ("instr") == "piano":    extra["piano"] = d
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
            #
            #  Y CON LAS DOS MUESTRAS DE GUARDA EN MEDIO. Detras del final de una
            #  zona que da vueltas van escritas las dos primeras muestras de su
            #  cuerpo: `hermite4` mira una por delante y dos por detras, asi que
            #  sin ellas la ultima fraccion antes de dar la vuelta se interpolaba
            #  contra la octava de al lado. Ver `Sintes::kGuardas`. O sea que
            #  `ini` de la siguiente NO es `fin` de la anterior: son dos mas.
            #  Se comprueba que la separacion sea EXACTAMENTE esa, que es lo que
            #  distingue una guarda de un hueco.
            GUARDAS = 2
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

                #  Y EL BUCLE MIDE UN NUMERO ENTERO DE CICLOS DE SU RAIZ.
                #
                #  Es la comprobacion que faltaba y la que caza lo que se OIA.
                #  La costura ya se medi­a -y salia continua, porque el fundido
                #  cruzado la tapa- pero el fundido mezcla el final con el
                #  principio, y si el bucle no cae en un numero entero de ciclos
                #  esos dos trozos estan DESFASADOS: a 0.42 s clavados, un bajo
                #  de 32.7 Hz daba 13.735 ciclos, o sea 265 grados, y el
                #  fundamental se cancelaba. Medido: 13.8 dB de bache 21 ms
                #  despues de cada vuelta.
                if d["sostiene"]:
                    hz = 130.8127827 * (2.0 ** (z[0] / 12.0))
                    ciclos = (z[5] - z[4]) * hz / SR
                    sobra = abs (ciclos - round (ciclos))
                    if sobra > 0.02:
                        fallos.append ("%s: el bucle de la raiz %+d mide %.3f ciclos "
                                       "(%.0f grados de desfase al dar la vuelta)"
                                       % (etiq, z[0], ciclos, 360.0 * sobra))
                        break
                #  Y EL LFO CUADRA CON EL BUCLE, o esta congelado.
                #
                #  El LFO solo existe mientras se rinde: una vez la zona es un
                #  buffer, su movimiento esta horneado y dar vueltas lo repite
                #  igual. Si no cae en un numero entero de vueltas, la costura
                #  parte el movimiento por la mitad y eso se oye como un salto
                #  de TIMBRE una vez por vuelta. Ver `Sintes::largoBucle`.
                #
                #  Cero significa CONGELADO y es una respuesta: un LFO que no da
                #  ni una vuelta dentro del cuerpo no es deriva, es una rampa, y
                #  una rampa en bucle es un diente de sierra.
                if d["sostiene"] and len (z) >= 8:
                    lfoHz, bucleSeg = z[6], z[7]
                    if lfoHz > 0.0 and bucleSeg > 0.0:
                        vueltas = lfoHz * bucleSeg
                        sobra = abs (vueltas - round (vueltas))
                        if sobra > LFO_SOBRA:
                            fallos.append ("%s: el LFO de la raiz %+d da %.4f vueltas por bucle "
                                           "(sobra %.4f, liston %.3f)"
                                           % (etiq, z[0], vueltas, sobra, LFO_SOBRA))
                            break

                prev = z[3] + GUARDAS

        # ---- LOS 256, UNO A UNO ------------------------------------------
        descs = []
        peorDC = (-999.0, "")
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

            #  DC DEL CUERPO, contra su propio pico. Se mide DESDE el punto de
            #  bucle: el ataque de una campana tiene un desplazamiento legitimo
            #  mientras el filtro se asienta, y medirlo entero seria acusar a la
            #  fisica. Ver DC_DBFS.
            zref = [z for z in d["mapa"] if z[1] == CAPAS - 1 and z[0] == 0]
            ini  = (zref[0][4] - zref[0][2]) if (zref and d["sostiene"]) else 0
            cuerpo = x[ini:] if ini < len (x) - 16 else x
            media = sum (cuerpo) / float (len (cuerpo))
            dc = 20.0 * math.log10 (max (1e-12, abs (media)) / max (1e-12, pico))
            if dc > DC_DBFS:
                fallos.append ("%s: %.1f dBFS de continua (liston %.0f)" % (etiq, dc, DC_DBFS))
            if dc > peorDC[0]: peorDC = (dc, etiq)

        print ("continua: la peor %.1f dBFS (%s)" % (peorDC[0], peorDC[1]))

        # ---- EL COSTE, EN PROPORCION -------------------------------------
        msRef = extra.get ("msKits", 0.0)
        mss = sorted (d["ms"] for d in filas)
        if msRef > 0.01 and mss:
            med  = mss[len (mss) // 2] / msRef
            peor = mss[-1] / msRef
            print ("coste: mediana x%.1f (%.0f ms)   peor x%.1f (%.0f ms)   "
                   "(un golpe de fabrica %.1f ms)"
                   % (med, mss[len (mss) // 2], peor, mss[-1], msRef))
            if med > COSTE_MED:
                fallos.append ("sintetizar cuesta x%.1f la mediana (liston x%.0f)" % (med, COSTE_MED))
            if peor > COSTE_PEOR:
                fallos.append ("el preset mas caro cuesta x%.1f (liston x%.0f)" % (peor, COSTE_PEOR))

        # ---- EL ANCHO DE LOS 256 -----------------------------------------
        peorR, peorMono, quienR, quienMono = -1.0, 99.0, "", ""
        for d in filas:
            f, p = d["fam"], d["pre"]
            etiq = "%s %s" % (d["familia"], d["nombre"])
            if d.get ("canales", 1) < 2:
                fallos.append ("%s: la muestra sale con %d canal" % (etiq, d.get ("canales", 1)))
                continue
            izq, der = carga2 (os.path.join (dirtemp, "ref-%02d-%02d.wav" % (f, p)))
            r, mono = ancho (izq, der)
            if f in MONO_FAMS:
                if r < MONO_R_MIN:
                    fallos.append ("%s tenia que ser mono bit a bit y mide r=%.6f" % (etiq, r))
                if mono < -0.001:
                    fallos.append ("%s pierde %.3f dB al sumarse en mono" % (etiq, mono))
                continue
            #  Y EL LISTON DE ARRIBA NO SE LE PIDE A LO QUE ES UN SENO.
            #
            #  `r <= 0.98` dice «los dos canales no pueden ser el mismo canal», y
            #  eso solo tiene sentido donde hay ALGO PLURAL que repartir. MAZOS
            #  SOFT MAL es un seno con dos inarmonicos al 28 % y un golpe de
            #  baqueta del 5 %: los inarmonicos ya salen uno a cada lado del todo
            #  y aun asi mide **r = 0.9804**, porque lo que manda es el
            #  fundamental, que esta centrado. Un seno no se puede ensanchar sin
            #  moverle la fase, y moverle la fase es justo lo que esta casa no
            #  hace -se cancela al sumar en mono-.
            #
            #  El guardia se MIDE, no se escribe una lista de familias: si una
            #  sola banda de tercio de octava se lleva mas del 80 % de la energia,
            #  el preset es un tono y no tiene nada que repartir.
            esp = bandas (izq)
            top = max (esp)
            conc = sum (1.0 for v in esp if v > top - 7.0) <= 1
            if r > ANCHO_R_MAX and not conc:
                fallos.append ("%s no tiene ancho: r=%.4f (liston %.2f)" % (etiq, r, ANCHO_R_MAX))
            if mono < ANCHO_MONO:
                fallos.append ("%s se cae %.2f dB al sumarse en mono (liston %.1f)"
                               % (etiq, mono, ANCHO_MONO))
            if r > peorR:       peorR, quienR = r, etiq
            if mono < peorMono: peorMono, quienMono = mono, etiq
        print ("ancho: r peor %.4f (%s)   mono peor %+.2f dB (%s)"
               % (peorR, quienR, peorMono, quienMono))

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
            duro  = [z for z in zs if z[1] == CAPAS - 1][0]
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

            #  LA AFINACION, EN CENTS Y NO EN BANDAS.
            #
            #  La regla de octavas compara bandas logaritmicas y CATORCE CENTS
            #  CABEN DENTRO DE UNA: el desafine del Karplus-Strong -que valia
            #  -14.4 cents en toda la octava alta de CUERDA PULS- paso por
            #  delante de ella sin que se moviera. Se mide la frecuencia del
            #  parcial dominante con rejilla de un cent.
            #
            #  El RELATIVO es el que manda -el sesgo del estimador se cancela al
            #  dividir- y el ABSOLUTO solo se juzga donde el pico cae cerca del
            #  fundamental: en CAMPANAS, MAZOS y ARPAS el parcial que mas suena
            #  no es la nota a proposito, y exigirselo seria inventarse una regla
            #  que la tabla no dice.
            zc = [z for z in mapa if z[1] == CAPAS - 1]
            z0  = [z for z in zc if z[0] == 0]
            z12 = [z for z in zc if z[0] == 12]
            if z0 and z12:
                f0  = afina (x[z0[0][2]:z0[0][3]],  RAIZ_HZ)
                f12 = afina (x[z12[0][2]:z12[0][3]], RAIZ_HZ * 2.0)
                rel = cents (f12 / 2.0, f0)
                abs0 = cents (f0, RAIZ_HZ)
                abs12 = cents (f12, RAIZ_HZ * 2.0)
                #  EL RELATIVO TAMBIEN NECESITA EL GUARDIA, y esa fue la segunda
                #  correccion. El sesgo del estimador se cancela al dividir SOLO
                #  si las dos octavas se han enganchado a la misma cosa; en un
                #  conjunto desafinado -siete sierras- o en un inarmonico a
                #  proposito -CAMPANAS- cada una se engancha a un parcial
                #  distinto, y entonces la resta no mide afinacion sino cual de
                #  los dos parciales gano. Salieron -32 cents en CUERDAS con la
                #  sintesis intacta.
                #  DIEZ CENTS Y NO VEINTICINCO: con veinticinco, CAMPANAS
                #  -inarmonica a proposito- colaba el guardia y la regla juzgaba
                #  la resta de dos parciales distintos, +6 cents con la sintesis
                #  intacta. Si el pico esta a mas de diez cents del fundamental es
                #  que el pico NO es el fundamental.
                cerca = abs (abs0) < 10.0 and abs (abs12) < 10.0
                print ("%-12s afina: raiz 0 %+.1f cents   la octava %+.1f cents%s"
                       % (etiq, abs0, rel, "" if cerca else "  (el pico no es el fundamental)"))
                #  SE IMPRIME Y NO SE JUZGA, todavia. Ver el bloque de arriba:
                #  la regla ha cambiado de metodo tres veces -parcial mas fuerte,
                #  suma armonica, guardia a 25 y a 10 cents- y en las familias de
                #  conjunto desafinado sigue sin poder separar «el instrumento
                #  esta plano» de «el estimador se ha enganchado al saw de al
                #  lado». Hasta que una rotura a proposito la haga fallar con la
                #  cifra esperada no es una regla, es una linea que imprime un
                #  numero, y esta casa no cuelga un veredicto de eso.
                if cerca and (abs (rel) > CENTS_REL or abs (abs0) > CENTS_ABS):
                    print ("   ojo: %s fuera de los listones de afinacion "
                           "(relativo %.0f, absoluto %.0f) - sin veredicto"
                           % (etiq, CENTS_REL, CENTS_ABS))

            #  LA DERIVA DENTRO DEL CUERPO: primer cuarto contra ultimo cuarto.
            #
            #  Lo que se congela en el punto de bucle son las envolventes, y esta
            #  es la regla que lo comprueba desde fuera: si algo sigue cayendo
            #  ahi dentro, cada vuelta lo reinicia y eso es un «wah» a la
            #  velocidad del bucle.
            if d["sostiene"] and z0:
                cb = x[z0[0][4]:z0[0][5]]
                #  LA VENTANA ES UNA VUELTA ENTERA DEL LFO, no un cuarto del
                #  cuerpo. Con el cuarto, ORGANOS salia a **1.00 dB clavado en
                #  el liston** y no era deriva: su leslie cuadra en seis vueltas
                #  por bucle, asi que un cuarto contiene 1.5 y el primero y el
                #  ultimo ven el LFO en fases distintas. Lo que esta regla busca
                #  es una TENDENCIA -una envolvente que sigue cayendo dentro del
                #  bucle- y un LFO no es una tendencia, es una oscilacion: con
                #  una vuelta entera a cada lado se cancela sola.
                q = len (cb) // 4
                if len (z0[0]) >= 8 and z0[0][6] > 0.0 and z0[0][7] > 0.0:
                    vueltas = max (1, int (round (z0[0][6] * z0[0][7])))
                    q = len (cb) // vueltas
                #  Y LA PRIMERA VENTANA NO EMPIEZA EN EL PRINCIPIO DEL CUERPO,
                #  que es donde vive el FUNDIDO CRUZADO: hasta 150 ms en las
                #  cuatro familias de conjunto desafinado, y ahi la señal es la
                #  suma de dos trozos y no el regimen. Con la ventana corta de
                #  una vuelta de LFO caia casi entera dentro y la regla acusaba
                #  de **11 a 20 dB de deriva** a CUERDAS, COLCHONES, METALES,
                #  LEADS, COROS y VIENTOS, que es justo el grupo que lleva
                #  fundido largo. Medía el fundido, no la deriva.
                #  Y LAS DOS VENTANAS SON UNA VUELTA ENTERA CADA UNA, colocadas
                #  en un MULTIPLO de la vuelta. Recortar «un poco despues» no
                #  vale: si las ventanas no caen en fase con el LFO, lo que se
                #  mide es el LFO -salieron de 8 a 18 dB en las seis familias que
                #  lo llevan, con la sintesis intacta-.
                #  Y LAS DOS VENTANAS ALINEADAS AL MISMO MULTIPLO de la vuelta:
                #  con la ultima pegada al final del cuerpo, el resto de la
                #  division la dejaba en otra fase del LFO y la regla volvia a
                #  medir el LFO -de 8 a 15 dB en las seis familias que lo llevan-.
                salta = q * max (1, int (math.ceil (0.150 * SR / q)))
                fin   = (len (cb) // q) * q
                if q > 2048 and fin - salta >= 2 * q:
                    ba, bb = bandas (cb[salta:salta + q]), bandas (cb[fin - q:fin])
                    #  Y SOLO LAS BANDAS QUE SUENAN. `bandas` devuelve dB
                    #  relativos al pico y con suelo en FLOOR, asi que una banda
                    #  cuarenta decibelios por debajo es un hueco entre parciales:
                    #  ahi la razon entre dos ventanas es enorme y no significa
                    #  nada. Sin este suelo la regla acusaba de 8 a 15 dB a las
                    #  familias con LFO, que son las que mueven un filtro y por lo
                    #  tanto las que tienen huecos que se mueven.
                    dif = max ((abs (a - b) for a, b in zip (ba, bb)
                                if a > -40.0 or b > -40.0), default=0.0)
                    print ("%-12s deriva: %.2f dB entre el primer cuarto y el ultimo" % (etiq, dif))
                    #  SE IMPRIME Y NO SE JUZGA, todavia. `bandas` trunca a
                    #  NFFT = 8192 muestras, asi que de una ventana de una vuelta
                    #  de LFO -que en un cuerpo de un segundo son entre 6000 y
                    #  16000- lo que entra en la FFT es solo el principio, y dos
                    #  principios a distinta fase del LFO se diferencian en los 8
                    #  a 15 dB que salen. La regla mide su ventana y no la deriva.
                    #  Se arregla alineando las dos ventanas a la misma fase del
                    #  LFO y en la tanda que viene, con su rotura.
                    if dif > DERIVA_DB:
                        print ("   ojo: %s deriva %.2f dB (liston %.1f) - sin veredicto"
                               % (etiq, dif, DERIVA_DB))

            #  Y EL TERCER NUMERO: NINGUN ESCALON ENTRE CAPAS CONTIGUAS.
            #
            #  Los dos de arriba miden el RECORRIDO entero y saldrian iguales
            #  con dos capas que con tres: lo que la tercera capa arregla no es
            #  cuanto se abre el instrumento, es que no se abra de golpe.
            sons = []
            for c in range (CAPAS):
                zc = [z for z in zs if z[1] == c]
                if not zc: continue
                sons.append (loudness (x[zc[0][2]:zc[0][3]]))
            pasos = []
            for i in range (1, len (sons)):
                if sons[i - 1] > 1e-9 and sons[i] > 1e-9:
                    pasos.append (20.0 * math.log10 (sons[i] / sons[i - 1]))
            if pasos:
                total = sum (pasos)
                peor  = max (abs (v) for v in pasos)
                cuota = (peor / total) if total > 1e-6 else 1.0
                print ("%-12s escalones: %s dB   (recorrido %.1f, el mayor se lleva %.0f%%)"
                       % (etiq, " ".join ("%+.2f" % v for v in pasos), total, 100.0 * cuota))
                if cuota > CAPA_REPARTO:
                    fallos.append ("%s: un escalon se lleva el %.0f%% del recorrido de fuerza "
                                   "(liston %.0f%%): las capas no estan repartidas"
                                   % (etiq, 100.0 * cuota, 100.0 * CAPA_REPARTO))

            #  LAS CINCO OCTAVAS, EN SONORIDAD. La ganancia sale de una sola
            #  zona, asi que esto SI puede desmadrarse - y es lo que la linea de
            #  arriba no puede ver, porque los 256 salen clavados por
            #  construccion.
            porOctava = []
            #  LA CAPA DE LA QUE SALE LA GANANCIA, y no la numero 1.
            #
            #  Con dos capas eran la misma cosa; con tres, `q[1] == 1` es la del
            #  MEDIO y la ganancia se mide en la de arriba. La regla acusaba a
            #  CUERDA PULS de **12.4 dB entre sus octavas** y lo que comparaba era
            #  una capa que nadie habia igualado: medidas las de arriba, las cinco
            #  raices caen dentro de 1.4 dB. Que el escalon de una capa a otra sea
            #  parejo ya lo mira `CAPA_REPARTO`, que es su sitio.
            for z in [q for q in mapa if q[1] == CAPAS - 1]:
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

        # ---- EL DESTINO SE ELIGE ---------------------------------------
        #
        #  Era el pad del mismo numero que la familia dentro del banco D, asi
        #  que todo lo demas sale verde con el destino clavado: el instrumento
        #  carga, suena, vuelve del proyecto y ocupa un pad. Lo unico que lo
        #  separa es pedir un pad que NO sea el suyo.
        de = extra.get ("destino")
        if de is None:
            fallos.append ("no hay linea de destino: no se probo elegir el pad")
        else:
            print ("destino: pedido %d, fue al %d (clavado daria %d)"
                   % (de["pedido"], de["fue"], de["clavado"]))
            if de["fue"] != de["pedido"]:
                fallos.append ("se pidio el pad %d y el instrumento fue al %d"
                               % (de["pedido"], de["fue"]))
            #  Y la otra mitad: que el pedido no coincida con el clavado, o la
            #  comprobacion de arriba pasaria tambien con el destino fijo.
            if de["pedido"] == de["clavado"]:
                fallos.append ("la prueba pide justo el pad clavado (%d): no mide nada"
                               % de["pedido"])

        # ---- LA RECETA ES DEL PAD --------------------------------------
        #
        #  Los dieciseis por dieciseis eran una tabla de SOLO LECTURA: la ficha
        #  podia pasar de un preset al siguiente y no habia una sola forma de
        #  tocar ninguno. Un instrumento que no se toca es un sample con
        #  nombre.
        #
        #  CON DOS CIFRAS Y NO UNA. «Mover un mando cambia el audio» lo cumple
        #  tambien un codigo que rinde algo distinto cada vez -y entonces
        #  VOLVER no devolveria nada-, y «volver deja el mismo audio» lo cumple
        #  un mando que no esta conectado. Bit a bit y no por nivel: la
        #  sintesis iguala la sonoridad por octava, asi que «casi el mismo
        #  pico» es justo lo que dejaria pasar una receta que no llega al
        #  oscilador.
        rc = extra.get ("receta")
        if rc is None:
            fallos.append ("no hay linea de receta: nadie mide si los ocho mandos hacen algo")
        else:
            print ("\nreceta movida: %d muestras cambian al mover el mando, "
                   "%d al volver a la fila" % (rc["suena"], rc["vuelve"]))
            print ("y del fichero vuelven %s mandos, audio %d muestras de diferencia"
                   % (rc["mandos"], rc["audio"]))
            if rc["suena"] <= 0:
                fallos.append ("mover un mando de la receta no cambia una sola muestra: "
                               "los ocho no llegan al oscilador")
            if rc["vuelve"] != 0:
                fallos.append ("VOLVER no devuelve la fila de la tabla: %d muestras "
                               "de diferencia" % rc["vuelve"])
            #  Y LA IDA Y VUELTA POR EL FICHERO, por el camino de verdad -el
            #  trabajo troceado, que es quien rinde los pads al abrir-. Las dos
            #  mitades: los ocho numeros y el AUDIO. Solo los numeros lo cumple
            #  un lector que los guarda y sintetiza la fila igualmente, que es
            #  exactamente lo que pasaba mientras la receta se leia en
            #  `applyState` -o sea DESPUES de rendir los pads-.
            a, b = rc["mandos"].split ("/")
            if a != b:
                fallos.append ("del fichero vuelven %s mandos de la receta" % rc["mandos"])
            if not rc["movida"]:
                fallos.append ("la receta vuelve del fichero y el pad no se sabe movido: "
                               "VOLVER no aparecera y el proyecto siguiente no la guardara")
            if rc["audio"] != 0:
                fallos.append ("el pad vuelve del fichero sonando distinto: %d muestras "
                               "-se rindio la fila de la tabla y no la receta-" % rc["audio"])

        # ---- LA PESTAÑA PAD ABRE LO QUE EL PAD ES ----------------------
        #
        #  Ninguna de las catorce reglas de expo.py puede verlo: una pestaña
        #  que abre la ficha equivocada se maqueta perfecta. Y con DOS cifras,
        #  que una se engaña: QUE ficha queda abierta *y* si la pestaña se
        #  queda ENCENDIDA - «abre la del instrumento» lo cumple igual un
        #  camino que se salta `openSheet`, y entonces la fila de la cara dice
        #  que no hay ninguna ficha abierta con una delante.
        pe = extra.get ("pestana")
        if pe is None:
            fallos.append ("no hay linea de pestana: nadie mide que abre PAD")
        else:
            print ("\nla pestana PAD: con una muestra abre %s (tapa %d), con un "
                   "instrumento %s (tapa %d), y la puerta vuelve a %s"
                   % (pe["normal"], pe["tapaN"], pe["instrumento"], pe["tapaI"], pe["vuelta"]))
            if pe["normal"] != "pad":
                fallos.append ("con una muestra la pestana PAD abre %s" % pe["normal"])
            if pe["instrumento"] != "vst":
                fallos.append ("con un instrumento la pestana PAD abre %s y no su ficha"
                               % pe["instrumento"])
            if not pe["tapaN"] or not pe["tapaI"]:
                fallos.append ("la pestana se queda apagada con su ficha delante: "
                               "la cara dice que no hay ninguna abierta")
            if pe["vuelta"] != "pad":
                fallos.append ("la puerta de la ficha del instrumento no lleva a EL PAD: "
                               "la ganancia, el pan y el filtro se quedan sin camino")

        # ---- Y MANTENER ABRE LA MISMA FICHA -----------------------------
        #
        #  «Cuando mantienes el pad para entrar en ajustes, como en pad cuando
        #  es un sonido, pero cuando es un instrumento, no funciona»: quince
        #  pads hacian una cosa y el del instrumento otra. Ninguna de las
        #  quince reglas de expo.py puede verlo — un pad al que le falta un
        #  gesto se maqueta perfecto.
        #
        #  CUATRO cifras, que una sola se engaña por los dos lados: que el pad
        #  este en modo TECLA -o sea que se mide el caso que fallaba-, que el
        #  reloj se ARME -y no que alguien llame a onHold, que es donde el
        #  fallo no existe-, que abra la ficha del instrumento, y que la NOTA
        #  no se pague por ello: suena con la ficha delante y se suelta al
        #  levantar el dedo.
        ma = extra.get ("mantener")
        if ma is None:
            fallos.append ("no hay linea de mantener: nadie mide el gesto en un instrumento")
        else:
            print ("\nmantener un pad de instrumento: tecla %d, reloj armado %d, abre %s, "
                   "suena %d voces con la ficha delante y %d al soltar"
                   % (ma["tecla"], ma["armado"], ma["ficha"], ma["sonando"], ma["al_soltar"]))
            if not ma["tecla"]:
                fallos.append ("el pad del instrumento no esta en modo tecla: "
                               "esta prueba no mide el caso que fallaba")
            if not ma["armado"]:
                fallos.append ("mantener un pad de instrumento no arma el reloj: "
                               "el gesto no existe ahi")
            if ma["ficha"] != "vst":
                fallos.append ("mantener un pad de instrumento abre %s y no su ficha"
                               % ma["ficha"])
            if ma["sonando"] < 1:
                fallos.append ("abrir la ficha corta la nota: %d voces con el dedo encima"
                               % ma["sonando"])
            if ma["al_soltar"] != 0:
                fallos.append ("al soltar quedan %d voces: la ficha se comio el suelta"
                               % ma["al_soltar"])

        # ---- Y LAS TRES PUERTAS DEL REPARTO -----------------------------
        #
        #  «La mayoria de opciones y botones de envios o cosas que hay en pad
        #  settings y no hay en la pantalla del plugin instrumento». Son
        #  PUERTAS y no copias: los nueve deslizadores siguen teniendo un dueño
        #  -EL PAD- y su puerta en la cabecera. Lo que no tenia camino es el
        #  reparto, que estaba a TRES toques.
        pu = extra.get ("puertas")
        if pu is None:
            fallos.append ("no hay linea de puertas: nadie mide el reparto desde el instrumento")
        else:
            print ("las tres puertas: %s, RACK -> %s, PIANO -> %s, y el canal abre su rejilla %d"
                   % (pu["canal"], pu["rack"], pu["piano"], pu["picker"]))
            if not pu["hay"]:
                fallos.append ("la ficha del instrumento no trae las tres puertas del reparto")
            if pu["rack"] != "rack":
                fallos.append ("la puerta RACK de la ficha del instrumento abre %s" % pu["rack"])
            if pu["piano"] != "piano":
                fallos.append ("la puerta PIANO de la ficha del instrumento abre %s" % pu["piano"])
            if not pu["picker"]:
                fallos.append ("la puerta CANAL no abre la rejilla de canales")
            #  Y LAS DOS DICEN LO MISMO, que es lo que separa dos puertas de dos
            #  reglas: el rotulo del canal se escribe en un sitio o un dia una
            #  de las dos se queda vieja.
            if pu["canal"] != pu["canalPad"]:
                fallos.append ("las dos tapas de canal no dicen lo mismo: «%s» en el "
                               "instrumento y «%s» en EL PAD" % (pu["canal"], pu["canalPad"]))

        # ---- LA FICHA ABRE EN EL PAD DEL QUE VIENES ---------------------
        #
        #  Llego del telefono: «seleccionas el pad seis y le das a CARGAR; se
        #  abre la pestaña, le das INSTRUMENTO y se te abre automaticamente en
        #  el cuarenta y nueve». Era exacto y de una linea: `openInstSheet` no
        #  leia `selectedPad` en ninguna, asi que la ficha abria con lo que
        #  `instDestPad` llevara dentro, y su unico valor de arranque era el
        #  literal 48 -que se pinta «PAD 49»-.
        #
        #  CON CUATRO CIFRAS, que cada una sola se engaña:
        #   - `destino == vengoDe` es lo que se pidio;
        #   - `vengoDe != 48` porque pedir justo el pad clavado sale verde con
        #     el fallo puesto;
        #   - `lleno == 1` porque la frase dice *este libre o no*: con un pad
        #     vacio, sembrar con `firstEmptyPad` pasaria igual;
        #   - y `banco == destino / 16`, que es la invariante que `instg`
        #     rompia cuando el banco se guardaba aparte.
        ab = extra.get ("abre")
        if ab is None:
            fallos.append ("no hay linea de abre: nadie mide con que pad abre la ficha")
        else:
            print ("\nvengo del pad %d (lleno %d) y la ficha abre en el %d, banco %d (ficha %d)"
                   % (ab["vengoDe"], ab["lleno"], ab["destino"], ab["banco"], ab["abierta"]))
            if not ab["abierta"]:
                fallos.append ("el gesto no dejo abierta la ficha de instrumentos: no mide nada")
            if ab["destino"] != ab["vengoDe"]:
                fallos.append ("vengo del pad %d y la ficha abre en el %d"
                               % (ab["vengoDe"] + 1, ab["destino"] + 1))
            if ab["vengoDe"] == 48:
                fallos.append ("la prueba viene justo del pad clavado: no mide nada")
            if not ab["lleno"]:
                fallos.append ("el pad del que se viene esta vacio: «este libre o no» "
                               "no se estaria midiendo")
            if ab["banco"] != ab["destino"] // 16:
                fallos.append ("banco %d con destino %d: el banco no se deriva del pad"
                               % (ab["banco"], ab["destino"]))

        # ---- OIR UNA TECLA SUENA EL PAD COMO ESTA AFINADO ---------------
        #
        #  `abreVst` ponia la base del teclado en la octava de `padPitch` con
        #  este argumento: «abrir siempre en el cero dejaria un bajo afinado dos
        #  octavas abajo sonando en un sitio que no es el suyo». Leido el camino
        #  entero, la frase esta del reves:
        #
        #     Teclado::notaEn   ->  base + blancas[i]
        #     vstTeclado.onNota ->  postNoteOnAt (vstPad, semis, …)
        #     AudioEngine       ->  semis = padPitch[slot] + extraSemis
        #
        #  `postNoteOnAt` es RELATIVO al pad por diseño medido -es la puerta que
        #  existe para que oir una tecla no AFINE el pad- asi que la base se
        #  SUMABA al pitch que el motor ya aplica: con el pad a +12 la tecla C
        #  sonaba +24, y con el pad a −24, −48.
        #
        #  SE MIDE POR IDENTIDAD Y BIT A BIT, que es lo unico que hay: no existe
        #  un accesor al semitono que una voz acabo usando. La tecla cero tiene
        #  que sonar EXACTAMENTE igual que `postNoteOnAt (pad, 0)`, y con OCTAVA
        #  subida una vez, igual que `postNoteOnAt (pad, 12)` — o sea que la tapa
        #  sigue haciendo su trabajo, que es la mitad que una sola cifra no ve.
        #  Y con el pad a +12 y no en cero, que ahi las dos formas dan lo mismo.
        #
        #  Con una cifra de CONTROL: las dos referencias tienen que DIFERIR
        #  entre si, o «bit a bit igual» lo cumpliria tambien un motor que
        #  ignora el semitono del comando.
        te = extra.get ("teclado")
        if te is None:
            fallos.append ("no hay linea de teclado: nadie mide lo que suena una tecla")
        else:
            print ("teclado: base %d, tras OCTAVA + %d; la tecla cero difiere de "
                   "postNoteOnAt en %d muestras y tras subir en %d (control %d)"
                   % (te["base"], te["baseArriba"], te["difA"], te["difB"], te["control"]))
            if te["control"] == 0:
                fallos.append ("las dos referencias del teclado son identicas: "
                               "el semitono del comando no llega, no se mide nada")
            if te["base"] != 0:
                fallos.append ("la ficha abre el teclado en la base %d: con el pad afinado "
                               "eso se SUMA al pitch y la tecla suena el doble" % te["base"])
            if te["difA"] != 0:
                fallos.append ("la tecla cero no suena el pad como esta afinado: "
                               "%d muestras de diferencia" % te["difA"])
            if te["baseArriba"] != 12:
                fallos.append ("la tapa OCTAVA + deja la base en %d y no en 12"
                               % te["baseArriba"])
            if te["difB"] != 0:
                fallos.append ("con OCTAVA subida la tecla cero no suena +12: "
                               "%d muestras de diferencia" % te["difB"])

        # ---- LA PUERTA AL PIANO ES EL PIANO Y NO UN CLON ----------------
        #
        #  La otra mitad de lo que se pidio: *«mejor no clonar esa pestaña —
        #  que sea el piano roll, conectado y sincronizado, en los pads que
        #  selecciones»*. Medido, ya lo era: hay UNA sola `pianoGrid` y
        #  `abrePianoDelPad` es una PUERTA. Lo que no habia era una cifra —se
        #  podia romper para que abriera el piano de otro pad y las 1428
        #  corridas seguian en verde.
        #
        #  Se mide POR EL GESTO -`pianoGrid.gesto` en pixeles- y no llamando a
        #  `pianoCellToggled`, que es justo donde ninguno de los cinco fallos
        #  del compas existia. Y con un TESTIGO en el pad 0 en la MISMA
        #  columna: es lo unico que separa «escribio» de «escribio donde
        #  tocaba», porque un piano apuntando al pad de por defecto borraria la
        #  nota del testigo en vez de dejar de escribir.
        pi = extra.get ("piano")
        if pi is None:
            fallos.append ("no hay linea de piano: nadie mide que pad edita la puerta")
        else:
            print ("la puerta al piano: la ficha edita el pad %d y el piano el %d; "
                   "la nota cayo en el patron (%d, nota %d) y el testigo sigue en %d"
                   % (pi["vstPad"], pi["pianoPad"], pi["puesto"], pi["nota"], pi["testigo"]))
            if pi["vstPad"] == 0:
                fallos.append ("la ficha edita el pad 0: el piano por defecto daria "
                               "verde sin haber medido nada")
            if pi["pianoPad"] != pi["vstPad"]:
                fallos.append ("la ficha del instrumento edita el pad %d y el piano el %d"
                               % (pi["vstPad"], pi["pianoPad"]))
            if not pi["puesto"]:
                fallos.append ("la nota escrita desde esa puerta no esta en el patron "
                               "del pad del instrumento")
            if pi["testigo"] != 9:
                fallos.append ("el testigo del pad 0 quedo en %d y se escribio con 9: "
                               "el piano escribio en el pad equivocado" % pi["testigo"])

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

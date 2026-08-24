#!/usr/bin/env python3
"""MAQUETAS: dibujar una pantalla PROPUESTA antes de escribirla.

`Tests/planos.py` dibuja lo que la app HACE, leyendo su volcado. Esto dibuja lo
que se propone que haga, leyendo una especificacion. Las dos con el mismo
pincel y la misma paleta, o la maqueta se halaga sola: una propuesta dibujada
con otro lapiz siempre parece mejor que la pantalla de verdad.

Lo que separa esto de un dibujo bonito es que HACE LA CUENTA y se NIEGA a
dibujar lo que no cabe. Los numeros no se inventan: salen de donde los saca la
app.

  tarjeta   ancho  = ancho de ventana x 0.92        (anchoTarjeta)
            alto   = min(pedido, alto x 0.78)       (sheetFromBottom)
                     x 0.90 apaisado, que es la rama que ya existe
            dentro = la tarjeta menos (16, 12)      (Metrics::lg, md)
  fila de tapas     Metrics::btn + xs = 48
  fila de pestanas  Metrics::tab = 44
  dedo minimo       Metrics::hit = 40
  celda de pasos    12 px (16 con la tira del paso puesta)
  fila de nota      16 px

    python3 Tests/maquetas.py                # dibuja las propuestas
    python3 Tests/maquetas.py --contraste    # comprueba la cuenta contra la app
"""
import os, sys

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from planos import CARCASAS, iconos_del_binario, esc, cuerpo, corre, clase

ROOT   = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
SALIDA = os.path.join (ROOT, "maquetas")

XS, SM, MD, LG, XL = 4, 8, 12, 16, 24
BTN, TAB, HIT, GAP = 44, 44, 40, 8
FILA   = BTN + XS      # 48: lo que cuesta una fila de tapas con su aire
CELDA_PASO, CELDA_NOTA = 12, 16


def tarjeta (W, H, pedido):
    """El rectangulo interior de una ficha, con la cuenta de la app.

    No es una aproximacion: es `sheetFromBottom` copiada, y por eso el modo
    --contraste existe. Si las dos se separan, todas las maquetas heredan el
    error y ninguna de las cuentas de abajo vale nada.
    """
    tope = 0.90 if W > H else 0.78
    h = min (pedido, int (H * tope))
    w = int (W * 0.92)
    x = (W - w) // 2
    y = (H - h) // 2
    return (x + LG, y + MD, w - 2 * LG, h - 2 * MD), (x, y, w, h)


# ---------------------------------------------------------------------------
#  LAS PROPUESTAS.
#
#  Una fila es (nombre, alto, cosas). El alto es un numero o "resto"; "resto"
#  se lleva lo que quede y es donde vive el hallazgo: si una fila nueva empuja,
#  se ve aqui y no en el telefono dos semanas despues.
#
#  Cada cosa es ("tapa", rotulo, dibujo) | ("mando", rotulo) | ("rejilla",
#  clase, columnas, carriles) | ("hueco",) | ("texto", lo que sea).
TAPA, MANDO, REJILLA, HUECO, TEXTO = "tapa", "mando", "rejilla", "hueco", "texto"

PROPUESTAS = [
 # ---- SEC · PATRON, en 280x653, que es donde esta el defecto -------------
 #
 #  Medido: a 412 la ficha tiene diez filas y las tres de la CADENA -1..8 mas
 #  QUITAR CADENA- son 120 px de ellas. A 280 la tarjeta da 485 px y esas tres
 #  filas se caen ENTERAS: nueve controles que desaparecen, no que encogen.
 ("paso-a", "SEC · PATRON — (a) la cadena, su propia pagina", 280, 653, 485, [
   ("titulo",   40, [(TEXTO, "SEC · PATRON"), (HUECO,), (TAPA, "×", None)]),
   ("pestanas", TAB, [(TAPA, "PASOS", "sec"), (TAPA, "PIANO", "piano"),
                      (TAPA, "PATRON", "patron"), (TAPA, "CADENA", "cadena")]),
   ("mover",    40, [(TAPA, "ATRAS", "atras"), (TAPA, "ADELANTE", "adelante")]),
   ("mover2",   40, [(TAPA, "DOBLAR", "doblar"), (TAPA, "HUMANIZAR", "humanizar")]),
   ("copiar",   40, [(TAPA, "COPIAR", "copiar"), (TAPA, "PEGAR", "pegar")]),
   ("copiar2",  40, [(TAPA, "COPIAR FILA", "copiar"), (TAPA, "PEGAR FILA", "pegar")]),
   ("euclid",   40, [(MANDO, "EUCLIDES")]),
   ("rejilla",  40, [(MANDO, "REJILLA")]),
   ("swing",    40, [(MANDO, "SWING")]),
   ("resto", "resto", [(HUECO,)]),
 ], "La cadena deja de competir por el alto: se va a una cuarta pestana y "
    "PATRON se queda con lo que actua sobre el patron. Lo que hay que mirar "
    "aqui es la fila de pestanas -cuatro donde habia tres, a 55 px cada una en "
    "280- y si el dibujante no avisa, cabe."),

 ("paso-b", "SEC · PATRON — (b) la cadena, un mando y no ocho tapas", 280, 653, 485, [
   ("titulo",   40, [(TEXTO, "SEC · PATRON"), (HUECO,), (TAPA, "×", None)]),
   ("pestanas", TAB, [(TAPA, "PASOS", "sec"), (TAPA, "PIANO", "piano"),
                      (TAPA, "PATRON", "patron")]),
   ("cadena",   40, [(TAPA, "-", None), (TEXTO, "P1 · P3 · P4"), (TAPA, "+", None)]),
   ("mover",    40, [(TAPA, "ATRAS", "atras"), (TAPA, "ADELANTE", "adelante"),
                     (TAPA, "DOBLAR", "doblar"), (TAPA, "HUMANIZAR", "humanizar")]),
   ("copiar",   40, [(TAPA, "COPIAR", "copiar"), (TAPA, "PEGAR", "pegar"),
                     (TAPA, "COPIAR FILA", "copiar"), (TAPA, "PEGAR FILA", "pegar")]),
   ("euclid",   40, [(MANDO, "EUCLIDES")]),
   ("rejilla",  40, [(MANDO, "REJILLA")]),
   ("swing",    40, [(MANDO, "SWING")]),
   ("resto", "resto", [(HUECO,)]),
 ], "Tres filas pasan a una: la cadena se lee como una lista y se edita con "
    "menos y mas, que es el idioma que esta app ya usa para PAD y PACK. Cuesta "
    "que deja de verse cuales de los ocho estan puestos, o sea que hay que "
    "recorrerla para saberlo."),

 ("paso-c", "SEC · PATRON — (c) la cadena se va a CANCION", 280, 653, 485, [
   ("titulo",   40, [(TEXTO, "SEC · PATRON"), (HUECO,), (TAPA, "×", None)]),
   ("pestanas", TAB, [(TAPA, "PASOS", "sec"), (TAPA, "PIANO", "piano"),
                      (TAPA, "PATRON", "patron")]),
   ("mover",    40, [(TAPA, "ATRAS", "atras"), (TAPA, "ADELANTE", "adelante"),
                     (TAPA, "DOBLAR", "doblar"), (TAPA, "HUMANIZAR", "humanizar")]),
   ("copiar",   40, [(TAPA, "COPIAR", "copiar"), (TAPA, "PEGAR", "pegar"),
                     (TAPA, "COPIAR FILA", "copiar"), (TAPA, "PEGAR FILA", "pegar")]),
   ("euclid",   40, [(MANDO, "EUCLIDES")]),
   ("rejilla",  40, [(MANDO, "REJILLA")]),
   ("swing",    40, [(MANDO, "SWING")]),
   ("puerta",   40, [(TAPA, "CADENA EN CANCION", "cancion")]),
   ("resto", "resto", [(HUECO,)]),
 ], "La de mas fondo, y la unica que no es un truco de sitio: una cadena de "
    "patrones y una linea de tiempo de bloques son LA MISMA funcion -tocar "
    "patrones en orden- en dos sitios, y esta casa tiene una regla para eso. "
    "Se queda la que sabe mas -la linea de tiempo, que ademas tiene largo por "
    "bloque y carriles- y PATRON deja una PUERTA, nunca una copia. Lo que hay "
    "que decidir antes: si `PLAY toca el patron / la cadena` y `PLAY toca la "
    "cancion` deben seguir siendo dos modos o uno."),

 # ---- CANCION -----------------------------------------------------------
 ("song-a", "CANCION — (a) la de la cara cambia de nombre", 412, 915, 640, [
   ("titulo",   44, [(TEXTO, "ARREGLO"), (HUECO,), (TAPA, "×", None)]),
   ("brochas", FILA, [(TAPA, "SONIDO", "sonido"), (TAPA, "CANCION", "cancion"),
                      (TAPA, "BUCLE", "loop"), (TAPA, "VACIAR", "vaciar")]),
   ("paleta1", FILA, [(TAPA, "P1", None), (TAPA, "P2", None), (TAPA, "P3", None),
                      (TAPA, "P4", None)]),
   ("paleta2", FILA, [(TAPA, "P5", None), (TAPA, "P6", None), (TAPA, "P7", None),
                      (TAPA, "P8", None)]),
   ("arreglo", FILA, [(TAPA, "INSERTAR", "insertar"), (TAPA, "QUITAR", "quitar"),
                      (TAPA, "ACORTAR", "acortar"), (TAPA, "ALARGAR", "alargar")]),
   ("linea", "resto", [(REJILLA, "Playlist", 8, 4)]),
   ("pie",    FILA, [(TAPA, "PLAY", "play"), (MANDO, "8 compases")]),
 ], "Cero px y cero maquetado: la pestana de la cara pasa a decir ARREGLO -o "
    "LINEA- y el choque se acaba. Lo que cuesta es renombrar una pestana de "
    "primer nivel que la gente ya conoce, y que en ingles no habia problema "
    "-SONG contra SONG MODE- asi que se estaria cambiando el idioma bueno para "
    "arreglar el otro."),

 ("song-b", "CANCION — (b) la de dentro sobra", 412, 915, 640, [
   ("titulo",   44, [(TEXTO, "CANCION"), (HUECO,), (TAPA, "×", None)]),
   ("brochas", FILA, [(TAPA, "SONIDO", "sonido"), (TAPA, "BUCLE", "loop"),
                      (TAPA, "VACIAR", "vaciar")]),
   ("paleta1", FILA, [(TAPA, "P1", None), (TAPA, "P2", None), (TAPA, "P3", None),
                      (TAPA, "P4", None)]),
   ("paleta2", FILA, [(TAPA, "P5", None), (TAPA, "P6", None), (TAPA, "P7", None),
                      (TAPA, "P8", None)]),
   ("arreglo", FILA, [(TAPA, "INSERTAR", "insertar"), (TAPA, "QUITAR", "quitar"),
                      (TAPA, "ACORTAR", "acortar"), (TAPA, "ALARGAR", "alargar")]),
   ("linea", "resto", [(REJILLA, "Playlist", 8, 4)]),
   ("pie",    FILA, [(TAPA, "PLAY", "play"), (TAPA, "PATRON", "patron"),
                     (MANDO, "8 compases")]),
 ], "El interruptor de modo ya esta en las tres filas de transporte -cara, SEC "
    "y esta-, asi que la de la fila de brochas es la cuarta y ademas la unica "
    "que esta lejos de un PLAY. Baja al pie, junto al PLAY que ya hay ahi. "
    "Cuesta CERO px y no gana ninguno: la primera version de esta nota decia "
    "que el carril ganaba sitio y era FALSO -quitar una tapa de una fila no "
    "quita la fila-, y el dibujante lo canto con el numero."),

 ("song-c", "CANCION — (c) el estado lo dice el titulo", 412, 915, 640, [
   ("titulo",   44, [(TEXTO, "CANCION ▸ tocando el patron"), (HUECO,), (TAPA, "×", None)]),
   ("brochas", FILA, [(TAPA, "SONIDO", "sonido"), (TAPA, "BUCLE", "loop"),
                      (TAPA, "VACIAR", "vaciar")]),
   ("paleta1", FILA, [(TAPA, "P1", None), (TAPA, "P2", None), (TAPA, "P3", None),
                      (TAPA, "P4", None)]),
   ("paleta2", FILA, [(TAPA, "P5", None), (TAPA, "P6", None), (TAPA, "P7", None),
                      (TAPA, "P8", None)]),
   ("arreglo", FILA, [(TAPA, "INSERTAR", "insertar"), (TAPA, "QUITAR", "quitar"),
                      (TAPA, "ACORTAR", "acortar"), (TAPA, "ALARGAR", "alargar")]),
   ("linea", "resto", [(REJILLA, "Playlist", 8, 4)]),
   ("pie",    FILA, [(TAPA, "PLAY", "play"), (MANDO, "8 compases")]),
 ], "El titulo de la ficha dice el estado y se toca para cambiarlo. Quita una "
    "tapa y pone la respuesta donde ya estas mirando al abrir. El riesgo esta "
    "medido en esta casa: un titulo que ademas es un boton no se lee como un "
    "boton, y este banco tiene una regla entera sobre rotulos PINTADOS que no "
    "son componentes y que por eso no los ve nadie."),
]

#  LAS TRES QUE SE CAYERON DE LA PRIMERA RONDA, con su motivo. Vale mas esto
#  que las maquetas que se quedaron: una propuesta para una pantalla que no
#  tiene ningun defecto medido es ruido con dibujo.
CAIDAS = [
  ("MEZCLA", "72 controles en 20 filas, que es el numero mas grande de la app "
   "y NO es un defecto: en el desglose su columna de minimos dice «-». Cero "
   "controles bajo el dedo, titulo, cruz, y dentro de un Viewport. Una mesa de "
   "dieciseis canales es eso. Y la propuesta era esconder MUTE y SOLO -lo que "
   "mas se toca mezclando- detras de un toque largo, o sea un gesto invisible "
   "para un estado que hay que ver de un vistazo."),
  ("EL PAD · SONIDO y RECORTE", "«escribe sonido y no tiene PLAY a mano» era "
   "una regla MIA -yo elegi que fichas entraban en esa lista- y no una medida. "
   "Comprobado en el codigo: OIR dispara por `postNoteOn`, y `Voice::start` lee "
   "`padStart`, asi que OIR YA suena el recorte. La herramienta para oir lo que "
   "estas recortando ya esta en esa ficha."),
  ("EL INSTRUMENTO", "Lo mismo: el teclado de la ficha suena al tocarlo y elige "
   "la nota, que es mas de lo que da un PLAY. Lo que quedaba era «oirlo con el "
   "patron rodando», que es un deseo y no un defecto medido."),
]


def dibuja (clave, titulo, W, H, pedido, filas, nota, piel=0):
    C   = CARCASAS.get (piel, CARCASAS[0])
    ICO = iconos_del_binario()
    (dx, dy, dw, dh), (cx, cy, cw, ch) = tarjeta (W, H, pedido)

    fijo  = sum (f[1] for f in filas if f[1] != "resto")
    aire  = SM * (len (filas) - 1)
    nresto = sum (1 for f in filas if f[1] == "resto")
    resto = (dh - fijo - aire) // max (1, nresto)

    #  UN RESTO NEGATIVO ES QUE NO CABE, y la comprobacion de abajo NO lo ve:
    #  una fila de alto negativo no hace avanzar la `y`, asi que `y` acaba
    #  DENTRO de la tarjeta y el dibujante daba «0 avisos» para dos propuestas
    #  que se pasaban en 23 px. Lo cazaron los numeros de la tabla y no la
    #  regla, que es exactamente lo que esta regla existe para que no pase.
    desborde = fijo + aire - dh

    p = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %d %d" width="%d" '
         'height="%d" font-family="Oswald, Arial Narrow, sans-serif">' % (W, H, W, H)]
    usados = sorted ({c[2] for f in filas for c in f[2]
                      if c[0] == TAPA and len (c) > 2 and c[2]})
    p.append ('<defs>')
    for nom in usados:
        if nom in ICO:
            n, tiras = ICO[nom]
            p.append ('<symbol id="m-%s" viewBox="0 0 %d %d">%s</symbol>'
                      % (nom, n, n, "".join ('<rect x="%d" y="%d" width="%d" height="1"/>'
                                             % (a, b, c) for a, b, c, _ in tiras)))
    p.append ('</defs>')
    p.append ('<rect width="%d" height="%d" fill="%s"/>' % (W, H, C["mid"]))
    p.append ('<rect width="%d" height="%d" fill="#000" opacity=".45"/>' % (W, H))
    p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="10" fill="%s" stroke="%s" '
              'stroke-width="1.5"/>' % (cx, cy, cw, ch, C["panel"], C["plateEdge"]))

    avisos, y = ([] if desborde <= 0 else
                 ["las filas fijas se pasan en %d px de la tarjeta (%d pedidos, %d hay)"
                  % (desborde, fijo + aire, dh)]), dy
    for nombre, alto, cosas in filas:
        h = resto if alto == "resto" else alto
        anchos = [1.0] * len (cosas)
        libre  = dw - GAP * (len (cosas) - 1)
        for i, cosa in enumerate (cosas):
            x = dx + int (sum (anchos[:i]) / sum (anchos) * libre) + GAP * i
            w = int (anchos[i] / sum (anchos) * libre)
            tipo = cosa[0]
            if tipo == HUECO:
                continue
            if tipo == TEXTO:
                fs = cuerpo (cosa[1], w, min (15.0, max (9.0, h * 0.55)))
                p.append ('<text x="%d" y="%.1f" font-size="%.1f" fill="%s" font-weight="600">%s</text>'
                          % (x, y + h / 2.0 + fs * 0.35, fs, C["ink"], esc (cosa[1])))
                continue
            if tipo == REJILLA:
                _, cl, cols, carriles = cosa
                p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="3" fill="%s" stroke="%s"/>'
                          % (x, y, w, h, C["lcd"], C["plateEdge"]))
                canal = 26 if carriles > 1 else 0
                cwid = (w - canal) / float (cols); chi = h / float (carriles)
                for c2 in range (cols + 1):
                    gx = x + canal + c2 * cwid
                    p.append ('<line x1="%.1f" y1="%d" x2="%.1f" y2="%d" stroke="%s" '
                              'stroke-width="0.5" opacity=".45"/>' % (gx, y, gx, y + h, C["lcdDim"]))
                for c2 in range (carriles + 1):
                    gy = y + c2 * chi
                    p.append ('<line x1="%d" y1="%.1f" x2="%d" y2="%.1f" stroke="%s" '
                              'stroke-width="0.5" opacity=".45"/>' % (x, gy, x + w, gy, C["lcdDim"]))
                p.append ('<text x="%d" y="%d" font-size="8" fill="%s" '
                          'font-family="JetBrains Mono, monospace">%s  celda %.1f x %.1f</text>'
                          % (x + canal + 3, y + h - 4, C["lcdDim"], cl, cwid, chi))
                if cl == "StepGrid" and chi < CELDA_PASO:
                    avisos.append ("la celda de pasos queda en %.1f px (suelo %d)" % (chi, CELDA_PASO))
                if cl == "PianoRoll" and chi < CELDA_NOTA:
                    avisos.append ("la fila de nota queda en %.1f px (suelo %d)" % (chi, CELDA_NOTA))
                if cl == "Playlist" and chi < CELDA_PASO:
                    avisos.append ("el carril de la cancion queda en %.1f px (suelo %d)" % (chi, CELDA_PASO))
                continue
            if tipo == MANDO:
                p.append ('<rect x="%d" y="%.1f" width="%d" height="4" rx="2" fill="%s"/>'
                          % (x, y + h / 2.0 - 2, w, C["plate"]))
                p.append ('<rect x="%.1f" y="%d" width="9" height="%d" rx="2" fill="%s" stroke="%s"/>'
                          % (x + w * 0.6, y + 2, max (8, h - 4), C["key"], C["plateEdge"]))
                fs = cuerpo (cosa[1], w, 9.5)
                p.append ('<text x="%d" y="%d" font-size="%.1f" fill="%s">%s</text>'
                          % (x, y + h - 1, fs, C["inkDim"], esc (cosa[1])))
                continue
            rot, ico = cosa[1], (cosa[2] if len (cosa) > 2 else None)
            p.append ('<rect x="%d" y="%d" width="%d" height="%d" rx="4" fill="%s" stroke="%s"/>'
                      % (x, y, w, h, C["key"], C["padBorder"]))
            fs = cuerpo (rot, w - 8, min (13.0, max (8.0, h * 0.42)))
            #  Cenido al hueco que hay, como en planos.py: la razon de avance
            #  de Oswald se estima y se queda corta segun las letras, y una
            #  maqueta que corta "INSERTAR" por el filo de su tapa miente
            #  justo sobre lo que se esta decidiendo.
            def apreta (t, cabe):
                return (' textLength="%.1f" lengthAdjust="spacingAndGlyphs"' % max (4.0, cabe)
                        if len (t) * fs * 0.46 > cabe else '')
            if ico and ico in ICO and min (w, h) >= 34:
                lado = min (18, max (13, int (h * 0.42)))
                anc  = len (rot) * fs * 0.46
                ix   = x + w / 2.0 - (anc + lado + 4) / 2.0
                p.append ('<use href="#m-%s" x="%.1f" y="%.1f" width="%d" height="%d" fill="%s"/>'
                          % (ico, ix, y + h / 2.0 - lado / 2.0, lado, lado, C["ink"]))
                p.append ('<text x="%.1f" y="%.1f" font-size="%.1f" fill="%s"%s>%s</text>'
                          % (ix + lado + 4, y + h / 2.0 + fs * 0.35, fs, C["ink"],
                             apreta (rot, w - lado - 12), esc (rot)))
            else:
                p.append ('<text x="%.1f" y="%.1f" font-size="%.1f" fill="%s" '
                          'text-anchor="middle"%s>%s</text>'
                          % (x + w / 2.0, y + h / 2.0 + fs * 0.35, fs, C["ink"],
                             apreta (rot, w - 8), esc (rot)))
            if min (w, h) < HIT and rot != "×":
                avisos.append ("%s queda en %dx%d (dedo %d)" % (rot, w, h, HIT))
        y += h + SM

    if y - SM > dy + dh:
        avisos.append ("la tarjeta se pasa en %d px de lo que da sheetFromBottom" % (y - SM - dy - dh))
    p.append ('</svg>')
    return "\n".join (p), avisos, {"tarjeta": (cw, ch), "dentro": (dw, dh),
                                   "pedido": pedido, "resto": resto}


def contraste():
    """La cuenta del dibujante contra la de la app, ficha por ficha.

    Si mi `tarjeta()` y `sheetFromBottom` no dan lo mismo, todas las maquetas
    heredan el error y ninguna de las cuentas de arriba vale nada. Se compara
    con lo que la app DIBUJA: la caja que ocupan los controles de la ficha.
    """
    print ("%-10s %-16s %-16s %s" % ("ficha", "dibujante", "la app", ""))
    malos = 0
    for k in ("pads", "pad2", "song", "mix", "paso", "vst"):
        filas = corre (k, "412x915", "es", {"ZATI_SKIN": "0"})
        c = [r for r in filas if "path" in r and r["w"] > 0 and r["h"] > 0]
        capas = sorted ({r.get ("capa", 0) for r in c if r.get ("capa", 0)},
                        key=lambda q: -sum (1 for r in c if r.get ("capa", 0) == q))
        if not capas:
            continue
        g = [r for r in c if r.get ("capa", 0) == capas[0]
             and "Sheet" not in clase (r) and "XyPanel" not in clase (r)]
        if not g:
            continue
        x0 = min (r["x"] for r in g); x1 = max (r["x"] + r["w"] for r in g)
        (dx, dy, dw, dh), _ = tarjeta (412, 915, 10000)
        mal = abs ((x1 - x0) - dw) > 8
        malos += mal
        print ("%-10s ancho %-10d ancho %-10d %s"
               % (k, dw, x1 - x0, "<-- NO CUADRA" if mal else "ok"))
    print()
    print ("la cuenta del dibujante coincide con la app" if not malos
           else "FALLA: %d fichas no cuadran" % malos)
    return 1 if malos else 0


#  Que maquetas son de la misma pantalla, para poder ponerlas al lado. El
#  orden es hoy primero: una propuesta sin el "antes" al lado no se puede
#  juzgar, que es de lo que iba todo esto.
#  El "hoy" NO es una maqueta mia: es el plano de verdad, dibujado del volcado
#  por Tests/planos.py. Mockear tambien el antes es como se cuela un antes peor
#  de lo que es, y ese es el error mas facil de cometer en un documento cuyo
#  trabajo entero es comparar.
GRUPOS = [
  ("SEC · PATRON — en 280x653", "/tmp/hoy-paso-280.svg", ["paso-a", "paso-b", "paso-c"],
   "A 412 esta ficha tiene diez filas y la CADENA son tres de ellas -1..8 mas "
   "QUITAR CADENA-. A 280 la tarjeta da 485 px y esas tres se caen ENTERAS: "
   "medido, nueve controles que desaparecen. No encogen: no estan."),
  ("CANCION — en 412x915", "/tmp/hoy-song-412.svg", ["song-a", "song-b", "song-c"],
   "Dos tapas dicen CANCION con el mismo dibujo: `songButton`, que abre esta "
   "ficha, y la tapa de modo, que cambia lo que toca PLAY. En ingles se separan "
   "-SONG y SONG MODE- asi que el choque es solo de la compilacion espanola."),
]

CSS = """<meta charset="utf-8">
<title>Maquetas de ZATI</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Oswald:wght@400;500;600&family=Barlow:wght@400;500&family=JetBrains+Mono:wght@400;500&display=swap">
<style>
:root{--papel:#eceae4;--tarjeta:#f6f5f1;--filo:#d3d0c7;--tinta:#1b2224;--apagado:#5f6b6d;
 --azul:#1c6a85;--ambar:#8a5f05;--verde:#1f6b4a;--rej:rgba(28,106,133,.10)}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
 --papel:#0b1113;--tarjeta:#121a1d;--filo:#222f34;--tinta:#dde7e8;--apagado:#8b9fa2;
 --azul:#5fb4d1;--ambar:#f0b429;--verde:#7fd4a8;--rej:rgba(95,180,209,.07)}}
:root[data-theme="dark"]{--papel:#0b1113;--tarjeta:#121a1d;--filo:#222f34;--tinta:#dde7e8;
 --apagado:#8b9fa2;--azul:#5fb4d1;--ambar:#f0b429;--verde:#7fd4a8;--rej:rgba(95,180,209,.07)}
*{box-sizing:border-box}
body{margin:0;background:var(--papel);color:var(--tinta);font-family:"Barlow",system-ui,sans-serif;
 font-size:15px;line-height:1.55;
 background-image:linear-gradient(var(--rej) 1px,transparent 1px),linear-gradient(90deg,var(--rej) 1px,transparent 1px);
 background-size:32px 32px}
.hoja{max-width:1280px;margin:0 auto;padding:40px 24px 90px}
header.portada{border-top:2px solid var(--tinta);padding-top:14px;margin-bottom:26px}
h1{font-family:"Oswald",sans-serif;font-weight:600;font-size:clamp(28px,5vw,44px);margin:0;
 text-transform:uppercase;letter-spacing:.02em}
.sub{margin:8px 0 0;color:var(--apagado);max-width:70ch}
section{margin:44px 0 0}
h2{font-family:"Oswald",sans-serif;font-weight:600;text-transform:uppercase;letter-spacing:.05em;
 font-size:21px;margin:0 0 3px;border-bottom:1px solid var(--filo);padding-bottom:7px}
.porque{margin:8px 0 14px;color:var(--apagado);max-width:74ch}
.par{display:flex;gap:18px;flex-wrap:wrap;align-items:flex-start}
figure{margin:0;width:330px;max-width:100%;display:flex;flex-direction:column;gap:7px}
figure.hoy .lienzo{border-color:var(--apagado)}
.lienzo{background:#0e1517;border:1px solid var(--filo);border-radius:3px;padding:5px;overflow:hidden}
svg{display:block;width:100%;height:auto}
figcaption{font-family:"Oswald",sans-serif;font-size:13.5px;letter-spacing:.04em;text-transform:uppercase}
figure.hoy figcaption{color:var(--apagado)}
.coste{font-family:"JetBrains Mono",monospace;font-size:11px;color:var(--ambar);
 font-variant-numeric:tabular-nums}
.nota{font-size:13px;color:var(--apagado);margin:0}
.avisos{font-family:"JetBrains Mono",monospace;font-size:11px;color:#c0392b}
.regla{margin:34px 0 0;border:1px solid var(--filo);background:var(--tarjeta);border-radius:3px;
 padding:14px 16px;font-size:13.5px;color:var(--apagado)}
.regla b{color:var(--tinta)}
code{font-family:"JetBrains Mono",monospace;font-size:12.5px}
/*  Las cotas viven dentro del plano de HOY -las mete Tests/planos.py- y aqui
    estorban: lo que se compara es la disposicion, no las medidas de cada
    control. Se apagan igual que en el informe.                             */
.cotas{display:none}
</style>"""


def documento (hechas):
    doc = [CSS, '<div class="hoja"><header class="portada">',
      '<h1>Maquetas, segunda vuelta</h1>',
      '<p class="sub">La primera ronda tenia cinco pantallas y tres no tenian ningun defecto '
      'medido: estan abajo, con el motivo. Y las siete propuestas eran la misma idea siete '
      'veces &mdash; donde pongo PLAY. Quedan las DOS que si tienen un defecto contado, con tres '
      'ideas de clases distintas cada una: cambiar de sitio, cambiar de control, o quitar la '
      'funcion de aqui porque ya vive en otro lado. El &laquo;hoy&raquo; es el plano de verdad, '
      'dibujado del volcado.</p></header>']

    for titulo, hoyf, claves, porque in GRUPOS:
        doc.append ('<section><h2>%s</h2><p class="porque">%s</p><div class="par">' % (esc (titulo), esc (porque)))
        try:
            with open (hoyf, encoding="utf8") as fh:
                doc.append ('<figure class="hoy"><div class="lienzo">%s</div>'
                            '<figcaption>hoy</figcaption>'
                            '<span class="coste">medido, no maquetado</span>'
                            '<p class="nota">Dibujado del volcado por Tests/planos.py: '
                            'esto es lo que la app hace hoy, no una version mia de lo que '
                            'hace.</p></figure>' % fh.read())
        except OSError:
            pass
        for k in claves:
            svg, nombre, nota, avisos, cuenta = hechas[k]
            hoy = False
            doc.append (
              '<figure class="%s"><div class="lienzo">%s</div>'
              '<figcaption>%s</figcaption>'
              '<span class="coste">dentro %d px &middot; resto %d px%s</span>'
              '<p class="nota">%s</p>%s</figure>'
              % ("hoy" if hoy else "", svg, esc (nombre.split ("&mdash;")[-1]),
                 cuenta["dentro"][1], cuenta["resto"],
                 "" if hoy else " &middot; pedido %d" % cuenta["pedido"],
                 esc (nota),
                 ('<p class="avisos">%s</p>' % esc ("; ".join (avisos))) if avisos else ""))
        doc.append ('</div></section>')

    doc.append ('<section><h2>Las tres que se cayeron</h2>'
      '<p class="porque">De la primera ronda, y esto vale mas que las maquetas que '
      'se quedaron: una propuesta para una pantalla que no tiene ningun defecto '
      'medido es ruido con dibujo.</p>')
    for nombre, motivo in CAIDAS:
        doc.append ('<div class="regla"><b>%s</b><br>%s</div>' % (esc (nombre), esc (motivo)))
    doc.append ('</section>')

    doc.append (
      '<div class="regla"><b>Lo que el dibujante NO deja pasar</b>, y por que se puede creer: '
      'las cuatro reglas se han roto a proposito y las cuatro saltan con su numero &mdash; '
      'una tapa de 30 px sale <code>PLAY queda en 169x30 (dedo 40)</code>; una rejilla '
      'aplastada, <code>la celda de pasos queda en 6.0 px (suelo 12)</code>; una ficha con '
      'diecinueve filas, <code>la tarjeta se pasa en 367 px de lo que da sheetFromBottom</code>. '
      'Y la cuenta de la tarjeta se contrasta contra la app en seis fichas: '
      '<code>python3 Tests/maquetas.py --contraste</code>.</div>')
    doc.append ('</div>')
    return "\n".join (doc)


def main():
    if "--contraste" in sys.argv:
        return contraste()
    os.makedirs (SALIDA, exist_ok=True)
    iconos_del_binario()
    total = 0
    hechas = {}
    print ("%-10s %-46s %6s %6s  %s" % ("clave", "maqueta", "dentro", "resto", "avisos"))
    for clave, titulo, W, H, pedido, filas, nota in PROPUESTAS:
        svg, avisos, cuenta = dibuja (clave, titulo, W, H, pedido, filas, nota)
        with open (os.path.join (SALIDA, clave + ".svg"), "w", encoding="utf8") as f:
            f.write (svg)
        hechas[clave] = (svg, titulo, nota, avisos, cuenta)
        total += len (avisos)
        print ("%-10s %-46s %6d %6d  %s"
               % (clave, titulo[:46], cuenta["dentro"][1], cuenta["resto"],
                  "; ".join (avisos)[:60] or "-"))
    print()
    with open (os.path.join (SALIDA, "maquetas.html"), "w", encoding="utf8") as f:
        f.write (documento (hechas))
    print()
    print ("%d maquetas en %s/, %d avisos  ->  %s/maquetas.html"
           % (len (PROPUESTAS), os.path.relpath (SALIDA, ROOT), total,
              os.path.relpath (SALIDA, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit (main())

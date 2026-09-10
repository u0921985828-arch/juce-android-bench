#!/usr/bin/env python3
"""QUE CADA PASO DEL TOUR SEÑALE ALGO.

El tour agujerea el velo sobre un control de verdad y le pone un anillo. Si el
objetivo sale vacio no hay agujero ni anillo: queda la maquina entera
oscurecida y un parrafo, o sea el folleto que este tour existe para no ser.

Y eso paso en DOS pasos sin que nadie se enterara hasta que llego una captura:

  - "LO QUE HACE UN PASO" apuntaba a `stepStripArea`, una variable que solo se
    ASIGNA en un sitio -`vuArea = stepStripArea = {}`- desde que la tira del
    paso dejo la cara. Valia vacio SIEMPRE.
  - "Y LO DEMAS", el ultimo, caia en el `default` de la tabla: los casos
    llegaban al 13 y los pasos son quince. La unica pantalla donde se explica
    como cambiar de idioma se enseñaba sin señalar el selector de idioma.

Los dos abrian su ficha, la oscurecian entera y no marcaban nada. Ninguna de
las seis reglas de expo.py los veia, porque el foco no es un componente: es un
rectangulo que se dibuja.

Se mide el rectangulo y no un si/no: un objetivo de 3x3 px tampoco señala nada
aunque no este vacio. El liston es el dedo minimo, que es lo que ya define un
blanco en esta casa.

El paso 0 es la portada y NO tiene objetivo a proposito - se comprueba que
siga siendo el unico, que es la otra mitad: un `default` que atiende un caso
correcto y otro olvidado es como se esconde el segundo.
"""
import json, os, shutil, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import display_alive

ROOT  = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP   = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
PASOS = 15
MIN   = 40          # Metrics::hit: el mismo liston que un blanco tocable
PORTADA = 0         # el unico sin objetivo


def foco (paso, size):
    casa = tempfile.mkdtemp (prefix="zati-tour-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG="es", ZATI_OPEN="tour%d" % paso,
                DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    ult = None
    for l in out.splitlines():
        l = l.strip()
        if l.startswith ("{") and '"tour"' in l:
            try: ult = json.loads (l)
            except Exception: pass
    return ult


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario: compila primero")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    size = sys.argv[1] if len (sys.argv) > 1 else "412x915"
    malas = []
    pags  = {}
    print ("%-6s %10s   %s" % ("paso", "objetivo", "que pasa"))
    for n in range (PASOS):
        d = foco (n, size)
        if d is None:
            malas.append ("el paso %d no contesto" % n); continue
        w, h = d.get ("focoW", 0), d.get ("focoH", 0)
        vacio = (w <= 0 or h <= 0)
        pags[n] = d.get ("mixpag", -1)

        if n == PORTADA:
            estado = "portada, sin objetivo a proposito"
            if not vacio:
                estado = "TIENE objetivo y no deberia"
                malas.append ("el paso %d es la portada y señala %dx%d" % (n, w, h))
        elif vacio:
            estado = "NO SEÑALA NADA"
            malas.append ("el paso %d no señala nada" % n)
        elif min (w, h) < MIN:
            estado = "objetivo por debajo del dedo"
            malas.append ("el paso %d señala %dx%d, bajo el minimo de %d" % (n, w, h, MIN))
        else:
            estado = "correcto"
        print ("%-6d %10s   %s" % (n, "%dx%d" % (w, h), estado))

    #  --- Y EL PASO ABRE LA PAGINA QUE NOMBRA -----------------------------
    #
    #  El paso 11 dice «la mesa tiene dieciseis PADS y dieciseis CANALES» y
    #  abria la mesa sin tocar `mixPage`, o sea en la que hubiera - por defecto
    #  PADS, asi que la palabra CANALES no se veia por ningun sitio. Es el
    #  residuo de siempre, el mismo que hizo que los pasos 6 a 9 llamen a
    #  `showSeqPage` explicitamente.
    #
    #  Ninguna de las reglas de arriba puede verlo: el objetivo del paso es
    #  `rackButton`, que se maqueta en las DOS paginas, asi que el anillo sale
    #  igual de bien con la mesa abierta donde no toca.
    #
    #  Es una CIFRA y no un si/no -dice QUE pagina quedo- asi que caza tambien
    #  el caso contrario, que otro paso deje la mesa en CANALES, sin escribir
    #  una segunda regla.
    print()
    CANALES = 11
    print ("la mesa queda en: %s" % ", ".join ("%d:%s" % (n, "CANALES" if v == 1
                                                          else "PADS" if v == 0 else "?")
                                               for n, v in sorted (pags.items())
                                               if v != 0))
    if pags.get (CANALES) != 1:
        malas.append ("el paso %d habla de los canales y abre la mesa en PADS" % CANALES)
    for n, v in sorted (pags.items()):
        if n != CANALES and v != 0:
            malas.append ("el paso %d deja la mesa en la pagina %d" % (n, v))

    #  --- LA BIENVENIDA SON CUATRO PASOS Y UNA PUERTA ---------------------
    #
    #  Quince tarjetas la primera vez son el manual otra vez, y este proyecto
    #  ya lo tiene escrito a otra escala: «un parrafo largo encima de una
    #  maquina oscurecida no se lee, se salta». Los cuatro primeros -los pads,
    #  los bancos, CARGAR/REC/PLAY y el transporte- son LA APP; los otros once
    #  son el recorrido, y se piden.
    #
    #  Con DOS cifras, que es lo que separa las dos formas de escribirlo mal:
    #  que el cuarto OFREZCA la puerta -y no encadene- y que tomarla LLEGUE al
    #  quinto. Solo la primera la cumple una tapa que cambia de rotulo y no
    #  hace nada; solo la segunda, un tour que no se corta en ninguna parte.
    print()
    d3 = foco (3, size)
    d2 = foco (2, size)
    if d3 is None or d2 is None:
        malas.append ("el paso de la puerta no contesto")
    else:
        print ("paso 3 (la puerta):  avanzar dice %-14s  la tercera dice %s"
               % ('"%s"' % d3.get ("sig", ""), '"%s"' % d3.get ("tercera", "")))
        print ("paso 2 (antes):      avanzar dice %-14s  la tercera dice %s"
               % ('"%s"' % d2.get ("sig", ""), '"%s"' % d2.get ("tercera", "")))
        if d3.get ("tercera") == d2.get ("tercera"):
            malas.append ("el cuarto paso no ofrece la puerta: la tercera tapa sigue "
                          "diciendo \"%s\"" % d3.get ("tercera"))
        if d3.get ("sig") == d2.get ("sig"):
            malas.append ("el cuarto paso encadena al quinto en vez de cerrar: "
                          "avanzar sigue diciendo \"%s\"" % d3.get ("sig"))

    #  Y TOMARLA LLEGA AL QUINTO, pulsando la tapa DE VERDAD: llamar a
    #  `showTour(4)` por dentro se salta justo el codigo que decide si esa tapa
    #  salta o sigue.
    dp = None
    casaP = tempfile.mkdtemp (prefix="zati-puerta-")
    try:
        env = dict (os.environ, HOME=casaP, ZATI_AUDIT="1", ZATI_SIZE=size,
                    ZATI_LANG="es", ZATI_OPEN="tourpuerta",
                    DISPLAY=PANTALLA)
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180)\
                        .stdout.decode ("utf8", "replace")
        for l in out.splitlines():
            l = l.strip()
            if l.startswith ("{") and '"tour"' in l:
                try: dp = json.loads (l)
                except Exception: pass
    finally:
        shutil.rmtree (casaP, ignore_errors=True)

    paso = dp.get ("tour", -1) if dp else -1
    print ("tomando la puerta:   el tour queda en el paso %d" % paso)
    if paso != 4:
        malas.append ("tomar la puerta deja el tour en el paso %d y tenia que "
                      "llevar al 4" % paso)

    #  --- Y QUE SOLO SALGA LA PRIMERA VEZ ---------------------------------
    #
    #  Es la otra mitad del tour y la que no miraba nadie: los quince pasos
    #  pueden señalar perfectamente lo que explican y aun asi la bienvenida
    #  aparecer EN CADA ARRANQUE, que es como se lee una app rota. Pasaba: la
    #  marca de visto se escribia al acabarlo, asi que salir por el boton ATRAS
    #  de Android -o cerrar la app- no marcaba nada.
    #
    #  Se mide con DOS arranques y el MISMO HOME, que es la unica forma: con un
    #  HOME por corrida las dos serian la primera vez y la prueba diria que si
    #  a cualquier cosa. Y por el sitio donde se DECIDE, no por una copia de la
    #  regla: la app imprime lo que acaba de resolver.
    casa = tempfile.mkdtemp (prefix="zati-tour2-")
    try:
        vistas, puestas = [], []
        for _ in range (2):
            env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                        ZATI_LANG="es", ZATI_OPEN="",
                        XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                        DISPLAY=PANTALLA)
            out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                                  timeout=300).stdout
            v = p = None
            for linea in out.splitlines():
                linea = linea.strip()
                if not linea.startswith ('{'): continue
                try:    d = json.loads (linea)
                except Exception: continue
                if d.get ("arranque") == "tour":
                    v = d["primera"]; p = d.get ("puesto")
            vistas.append (v); puestas.append (p)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    print()
    print ("arranques con el mismo HOME: %s" % vistas)
    if vistas != [1, 0]:
        malas.append ("la bienvenida sale %s en dos arranques y tenia que salir [1, 0]"
                      % (vistas,))

    #  Y LA MITAD QUE FALTABA: que la marca decida algo.
    #
    #  Lo de arriba mide que se ESCRIBE bien y salia [1, 0] con la bienvenida
    #  apareciendo igual en cada arranque, porque hay un segundo camino que la
    #  levanta y no la mira: retranslateUi -que corre en el constructor y al
    #  tocar un idioma- llamaba a showTour, y showTour termina en
    #  `tourSheet.setVisible (true)` sin condicion. El bloque que si mira la
    #  marca corre despues y solo puede AÑADIR.
    #
    #  Con ZATI_AUDIT la tarjeta no se enseña nunca a proposito, asi que
    #  cualquier uno aqui es exactamente eso: puesta sin que nadie la pida.
    #  Salia [1, 1].
    print ("la bienvenida quedo puesta:    %s" % puestas)
    if puestas != [0, 0]:
        malas.append ("la bienvenida queda puesta %s sin que nadie la pida" % (puestas,))

    #  --- Y QUE CAMBIAR DE IDIOMA NO CIERRE LA FICHA ----------------------
    #
    #  El mismo fallo por la otra puerta, y la mas absurda de las dos: el idioma
    #  se cambia desde AJUSTES, y tocarlo llamaba a retranslateUi -> showTour ->
    #  tourPrepara, cuyo caso por defecto empieza por closeAllSheets. Cerraba la
    #  ficha que tenias delante y levantaba la bienvenida encima.
    #
    #  La app pulsa la tapa DE VERDAD (langButtons[1]->onClick), que es donde el
    #  fallo existe: llamar a retranslateUi por dentro se salta el callback que
    #  lo encadena todo.
    casa = tempfile.mkdtemp (prefix="zati-tour3-")
    try:
        env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                    ZATI_LANG="es", ZATI_OPEN="lang",
                    XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                    DISPLAY=PANTALLA)
        out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                              timeout=300).stdout
        idi = None
        for linea in out.splitlines():
            linea = linea.strip()
            if not linea.startswith ('{'): continue
            try:    d = json.loads (linea)
            except Exception: continue
            if "idioma" in d: idi = d
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    print()
    if idi is None:
        malas.append ("el cambio de idioma no contesto")
    else:
        print ("tras tocar un idioma:  ajustes %d  tour %d"
               % (idi.get ("ajustes", -1), idi.get ("tour", -1)))
        if idi.get ("ajustes") != 1:
            malas.append ("cambiar de idioma cierra AJUSTES, que es de donde se cambia")
        if idi.get ("tour") != 0:
            malas.append ("cambiar de idioma levanta la bienvenida")

    print()
    for m in malas: print ("FALLA ", m)
    print ("los %d pasos del tour señalan lo que explican" % PASOS if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

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
                DISPLAY=os.environ.get ("DISPLAY", ":99"))
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
    print ("%-6s %10s   %s" % ("paso", "objetivo", "que pasa"))
    for n in range (PASOS):
        d = foco (n, size)
        if d is None:
            malas.append ("el paso %d no contesto" % n); continue
        w, h = d.get ("focoW", 0), d.get ("focoH", 0)
        vacio = (w <= 0 or h <= 0)

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
        vistas = []
        for _ in range (2):
            env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE="412x915",
                        ZATI_LANG="es", ZATI_OPEN="",
                        XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                        DISPLAY=os.environ.get ("DISPLAY", ":99"))
            out = subprocess.run ([APP], env=env, capture_output=True, text=True,
                                  timeout=300).stdout
            v = None
            for linea in out.splitlines():
                linea = linea.strip()
                if not linea.startswith ('{'): continue
                try:    d = json.loads (linea)
                except Exception: continue
                if d.get ("arranque") == "tour": v = d["primera"]
            vistas.append (v)
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    print()
    print ("arranques con el mismo HOME: %s" % vistas)
    if vistas != [1, 0]:
        malas.append ("la bienvenida sale %s en dos arranques y tenia que salir [1, 0]"
                      % (vistas,))

    print()
    for m in malas: print ("FALLA ", m)
    print ("los %d pasos del tour señalan lo que explican" % PASOS if not malas
           else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

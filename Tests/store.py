#!/usr/bin/env python3
# ============================================================================
#  LAS FOTOS DE LA FICHA DE GOOGLE PLAY, hechas por la propia app.
#
#  Una captura de pantalla tomada a mano en un movil trae tres cosas que no se
#  quieren: la barra de estado con la hora y la bateria de quien la saco, la
#  resolucion del movil que hubiera a mano, y el estado en el que estuviera la
#  app ese dia. Y hay que rehacerlas las catorce cada vez que se mueve un
#  boton.
#
#  Esto las saca del componente, a la escala que pida la tienda, con doce pads
#  cargados y un patron escrito, en los idiomas que se publiquen, y tarda
#  menos de un minuto. Lo que Play pide:
#
#    icono            512x512 PNG de 32 bits
#    grafico          1024x500, y NO puede ser una captura
#    telefono         2 a 8, lado corto >= 320, largo <= 3840, relacion <= 2:1
#    tablet 7"        opcional, misma regla
#    tablet 10"       opcional, misma regla
#
#  La relacion 2:1 es la que decide el tamano logico: 412x915 - la pantalla del
#  Pixel - es 2.22:1 y la tienda LO RECHAZA. Por eso se maqueta a 412x824, que
#  es exactamente 2:1, y se pinta a escala 2.6214 para llegar a 1080 de ancho.
#  Pintar a escala no estira una imagen: vuelve a dibujar vectores y fuentes.
#
#      python3 Tests/store.py [carpeta]
# ============================================================================
import os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")
OUT  = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "store")

#  Que se enseña y en que orden. El orden importa: en la ficha se ven las dos
#  primeras sin desplazar, asi que van la maquina entera y el secuenciador.
SHOTS = [
    ("",      "01-maquina"),
    ("sec",   "02-secuenciador"),
    ("pad2",  "03-recorte"),
    ("pads",  "04-sonido"),
    ("xy",    "05-xy"),
    ("mix",   "06-mezcla"),
    ("pad3",  "07-envios"),
    ("chop",  "08-chop"),
]

#  nombre, ancho logico, alto logico, escala. El alto logico no pasa nunca del
#  doble del ancho.
FORMATS = [
    ("telefono",  412,  824, 2.6214),   # 1080 x 2160
    ("tablet7",   600,  960, 2.0),      # 1200 x 1920
    ("tablet10",  800, 1280, 2.0),      # 1600 x 2560
]

TABLET_SHOTS = {"", "sec", "pad2", "mix"}
#  LOS CUATRO IDIOMAS QUE LA APP HABLA, y no dos.
#
#  Esto generaba capturas en es/en mientras la app se compila en cuatro
#  (Source/Lang.h). Publicar la ficha en chino o en arabe era publicarla SIN
#  capturas, y son justo los dos mercados que ESTUDIO-2026.md llama «lo mas
#  barato y lo mas infravalorado de toda la lista»: cuatro idiomas es la unica
#  ventaja de catalogo que este producto tiene sobre el que manda.
#
#  Y en arabe la maqueta va espejada, asi que no son las mismas capturas con
#  otro rotulo: son otra pantalla.
LANGS = ["es", "en", "zh", "ar"]


def png_size (path):
    with open (path, "rb") as f:
        d = f.read (32)
    return struct.unpack (">II", d[16:24])


def run (env_extra, home):
    env = dict (os.environ)
    env["DISPLAY"] = env.get ("DISPLAY", ":99")
    env["HOME"] = home
    env.update (env_extra)
    subprocess.run ([APP], env=env, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL, timeout=180)


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build)")

    os.makedirs (OUT, exist_ok=True)
    home = os.path.join (OUT, ".home")

    made = []
    for lang in LANGS:
        for fmt, lw, lh, scale in FORMATS:
            for sheet, name in SHOTS:
                if fmt != "telefono" and sheet not in TABLET_SHOTS:
                    continue

                #  Casa limpia en cada corrida. Con una sesion guardada dentro,
                #  la app la restaura y la foto sale con los pads de la corrida
                #  anterior y un "Sesion recuperada [40 pads]" en la barra.
                shutil.rmtree (home, ignore_errors=True)
                os.makedirs (home, exist_ok=True)

                d = os.path.join (OUT, lang, fmt)
                os.makedirs (d, exist_ok=True)
                path = os.path.join (d, name + ".png")

                run ({"ZATI_AUDIT": "1", "ZATI_DEMO": "1",
                      "ZATI_LANG": lang, "ZATI_OPEN": sheet,
                      "ZATI_SIZE": "%dx%d" % (lw, lh),
                      "ZATI_SHOT": path, "ZATI_SHOT_SCALE": "%.4f" % scale}, home)

                if not os.path.exists (path):
                    print ("FALLA   %s/%s/%s" % (lang, fmt, name)); continue
                made.append (path)

    #  El grafico destacado, dibujado por la app con la paleta de la app.
    feature = os.path.join (OUT, "grafico-destacado-1024x500.png")
    run ({"ZATI_BANNER": feature, "ZATI_BANNER_SIZE": "1024x500"}, home)
    if os.path.exists (feature): made.append (feature)

    #  El icono. El del repositorio es de 1024 y la tienda lo quiere de 512: se
    #  reduce, no se vuelve a dibujar, para que el de la ficha y el del cajon
    #  de aplicaciones sean el MISMO dibujo.
    icon = os.path.join (OUT, "icono-512x512.png")
    src = os.path.join (ROOT, "ci", "icon.png")
    if os.path.exists (src):
        subprocess.run (["convert", src, "-resize", "512x512", "-strip", icon], check=False)
        if os.path.exists (icon): made.append (icon)

    shutil.rmtree (home, ignore_errors=True)

    #  Y se comprueba lo que pide la tienda, que es la unica parte que no se
    #  puede mirar a ojo: relacion de aspecto y limites de tamano.
    bad = 0
    for p in made:
        w, h = png_size (p)
        rel = os.path.relpath (p, OUT)
        short, long_ = min (w, h), max (w, h)
        problems = []
        if "grafico-destacado" in rel:
            if (w, h) != (1024, 500): problems.append ("no es 1024x500")
        elif "icono" in rel:
            if (w, h) != (512, 512): problems.append ("no es 512x512")
        else:
            if short < 320:  problems.append ("lado corto %d < 320" % short)
            if long_ > 3840: problems.append ("lado largo %d > 3840" % long_)
            if long_ > 2 * short: problems.append ("relacion %.2f:1 > 2:1" % (long_ / short))
        if problems:
            bad += 1
            print ("RECHAZA %-46s %dx%d  %s" % (rel, w, h, "; ".join (problems)))

    print ("%d imagenes en %s" % (len (made), OUT))
    print ("las %d pasan lo que pide Play" % len (made) if bad == 0
           else "%d NO pasan" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit (main())

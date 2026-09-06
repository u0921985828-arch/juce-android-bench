#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ============================================================================
#  LO QUE SALE DEL TELEFONO.
#
#  La exportacion es la unica funcion de esta app cuyo resultado sale del
#  aparato, y era la unica sin banco que la corriera: los numeros existian
#  -la app los imprime con ZATI_EXPORT=1- y no los miraba nadie, asi que una
#  regresion no fallaba, se publicaba.
#
#  Se comprueban las tres salidas y el destino:
#
#    master   un WAV con lo que suena
#    pistas   un WAV por pad con muestra, mas el master
#    ogg      lo mismo comprimido, que es OTRO escritor y otro fichero
#    destino  la carpeta elegida se guarda, vuelve, y una que no acepta
#             escritura se rechaza SIN perder la anterior
#
#  El OGG se juzga por la RELACION con el WAV y no por un tamano absoluto: el
#  tamano depende de la calidad y del contenido, y clavarlo aqui seria un banco
#  que hay que actualizar cada vez que se toca el encoder. Lo que no puede pasar
#  es que "comprimido" pese lo mismo que sin comprimir - eso es la tapa
#  cambiando un rotulo y nada mas.
#
#      python3 Tests/export.py
# ============================================================================
import json, os, subprocess, sys, tempfile, shutil

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
BIN  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  LOS METADATOS SE LEEN DEL FICHERO, no del que dice haberlos escrito.
#
#  El diccionario iba vacio y el rebote salia sin titulo ni artista. Al ponerlo
#  hay dos formas de equivocarse que un `assert` sobre el codigo no ve: las
#  claves NO son las mismas en los dos formatos -el WAV usa un trozo INFO y el
#  OGG comentarios Vorbis- y JUCE ignora EN SILENCIO la clave que no reconoce.
#  O sea que escribir el juego del WAV en el OGG compila, corre, no se queja y
#  deja el fichero mudo. Por eso esto abre los bytes.
def infoWav (ruta):
    """El trozo LIST INFO de un RIFF: {'INAM': 'titulo', 'IART': ...}."""
    d = {}
    b = open (ruta, "rb").read (1 << 20)
    if b[:4] != b"RIFF": return d
    i = 12
    while i + 8 <= len (b):
        cid, n = b[i:i+4], int.from_bytes (b[i+4:i+8], "little")
        cuerpo = b[i+8 : i+8+n]
        if cid == b"LIST" and cuerpo[:4] == b"INFO":
            j = 4
            while j + 8 <= len (cuerpo):
                k, m = cuerpo[j:j+4], int.from_bytes (cuerpo[j+4:j+8], "little")
                d[k.decode ("ascii", "replace")] = cuerpo[j+8 : j+8+m].split (b"\0")[0].decode ("utf8", "replace")
                j += 8 + m + (m & 1)
        i += 8 + n + (n & 1)
    return d


def infoOgg (ruta):
    """Los comentarios Vorbis, que van en texto plano dentro de la cabecera."""
    b = open (ruta, "rb").read (1 << 16)
    d = {}
    for clave in (b"TITLE=", b"ARTIST=", b"ALBUM=", b"ENCODER=", b"TRACKNUMBER="):
        k = b.find (clave)
        if k < 0: continue
        v, i = b"", k + len (clave)
        while i < len (b) and 32 <= b[i] < 127 or (i < len (b) and b[i] > 127):
            v += b[i:i+1]; i += 1
        d[clave[:-1].decode()] = v.decode ("utf8", "replace")
    return d


def corre():
    casa = tempfile.mkdtemp (prefix="zati-export-")
    env = dict (os.environ, ZATI_AUDIT="1", ZATI_EXPORT="1",
                DISPLAY=os.environ.get ("DISPLAY", ":99"),
                HOME=casa, XDG_DATA_HOME=casa)
    try:
        out = subprocess.run ([BIN], env=env, capture_output=True,
                              timeout=900).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        shutil.rmtree (casa, ignore_errors=True)
        return None, {}
    #  Y SE LEEN ANTES DE BORRAR LA CASA. La version anterior tenia el rmtree en
    #  un `finally`, asi que los ficheros ya no existian cuando alguien quisiera
    #  mirarlos - por eso esta prueba solo sabia contar bytes.
    marcas = {}
    for raiz, _, ficheros in os.walk (casa):
        for f in sorted (ficheros):
            r = os.path.join (raiz, f)
            if   f.endswith (".wav"): marcas.setdefault ("wav", []).append ((f, infoWav (r)))
            elif f.endswith (".ogg"): marcas.setdefault ("ogg", []).append ((f, infoOgg (r)))
    shutil.rmtree (casa, ignore_errors=True)

    filas = {}
    for l in out.splitlines():
        l = l.strip()
        if not (l.startswith ("{") and l.endswith ("}")): continue
        try: d = json.loads (l)
        except Exception: continue
        if "export" in d: filas[d["export"]] = d
    return filas, marcas

#  EL REBOTE EN VIVO va en OTRA corrida: `ZATI_VIVO` y `ZATI_EXPORT` son
#  excluyentes en `Main.cpp` -una cadena if/else- y ademas este bombea audio en
#  tiempo casi real, asi que meterlo en la de arriba le sumaria su medio minuto
#  a una prueba que ya tarda.
def corre_vivo():
    casa = tempfile.mkdtemp (prefix="zati-vivo-")
    env = dict (os.environ, ZATI_AUDIT="1", ZATI_VIVO="1",
                DISPLAY=os.environ.get ("DISPLAY", ":99"),
                HOME=casa, XDG_DATA_HOME=casa)
    try:
        out = subprocess.run ([BIN], env=env, capture_output=True,
                              timeout=600).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        out = ""
    shutil.rmtree (casa, ignore_errors=True)
    return out


def vivo (out):
    for l in out.splitlines():
        l = l.strip()
        if not (l.startswith ("{") and l.endswith ("}")): continue
        try: d = json.loads (l)
        except Exception: continue
        if d.get ("vivo"): return d
    return None


def main():
    if not os.path.exists (BIN):
        sys.exit ("no hay binario: compila primero (cmake --build build)")

    filas, marcas = corre()
    if filas is None:
        sys.exit ("la app no contesto")

    malas = []
    def juzga (nombre, cond, texto):
        print ("%-10s %s   %s" % (nombre, texto, "correcto" if cond else "MAL"))
        if not cond: malas.append (nombre)

    m = filas.get ("master", {})
    juzga ("master", m.get ("ok") == 1 and m.get ("ficheros") == 1 and m.get ("bytes", 0) > 100000,
           "%s fichero, %s bytes" % (m.get ("ficheros", "?"), m.get ("bytes", "?")))

    p = filas.get ("pistas", {})
    #  Un WAV por pad con muestra, al menos. Contar solo "muchos ficheros"
    #  dejaria pasar un rebote que escribe cuarenta veces el mismo pad, pero
    #  eso lo cazan los bytes: cuarenta copias del mismo pad pesan lo mismo que
    #  cuarenta pads distintos, asi que aqui se comprueba la CUENTA y el peso
    #  total lo comprueba el de master.
    juzga ("pistas", p.get ("ok") == 1 and p.get ("ficheros", 0) >= p.get ("pads", 0),
           "%s ficheros para %s pads con muestra" % (p.get ("ficheros", "?"), p.get ("pads", "?")))

    o = filas.get ("ogg", {})
    razon = (m.get ("bytes", 1) / max (1, o.get ("bytes", 1))) if o else 0
    juzga ("ogg", o.get ("ok") == 1 and o.get ("ficheros") == 1 and razon >= 4.0,
           "%s bytes, %.0f veces mas pequeno que el WAV" % (o.get ("bytes", "?"), razon))

    #  LOS METADATOS, LEIDOS DEL FICHERO.
    #
    #  El master pesa 130 bytes mas que antes de esto, y ese numero es una
    #  PISTA y no una prueba: 130 bytes de relleno tambien pesan 130. Lo que
    #  vale es que las etiquetas esten dentro y digan lo que tienen que decir.
    #
    #  Se juzgan los DOS formatos por separado a proposito. Las claves no son
    #  las mismas -el WAV usa un trozo INFO con INAM/IPRD/ISFT y el OGG
    #  comentarios Vorbis con TITLE/ALBUM/ENCODER- y JUCE ignora en silencio la
    #  clave que no reconoce, asi que escribir el juego del WAV en el OGG
    #  compila, corre, no se queja y deja el fichero mudo. Un solo formato
    #  comprobado habria dejado pasar exactamente ese fallo.
    wavs = marcas.get ("wav", [])
    oggs = marcas.get ("ogg", [])

    #  El master es el WAV sin numero de pista; una pista lo lleva. Se busca uno
    #  de cada, que es lo que separa "escribe metadatos" de "escribe los mismos
    #  metadatos dieciseis veces".
    #  IPRT y no ITRK, que fue el primer intento y sacaba "meta pista MAL" con
    #  el codigo bien: JUCE llama a la clave riffInfoTrackNo y el codigo de
    #  cuatro letras que escribe es IPRT. Primero se duda de la prueba.
    maestro = next ((d for f, d in wavs if d and "IPRT" not in d), None)
    pista   = next ((d for f, d in wavs if d and "IPRT" in d), None)

    juzga ("meta wav",
           bool (maestro) and maestro.get ("INAM", "") != ""
             and maestro.get ("ISFT", "") == "ZATI Sampler",
           "titulo \"%s\"  album \"%s\"  software \"%s\""
             % (maestro.get ("INAM", "-") if maestro else "-",
                maestro.get ("IPRD", "-") if maestro else "-",
                maestro.get ("ISFT", "-") if maestro else "-"))

    #  Y el titulo de una pista es el nombre del PAD, no el del fichero: el
    #  fichero va saneado -sin espacios ni acentos, porque es una ruta- y el
    #  metadato no tiene esa limitacion. Si los dos coincidieran siempre, seria
    #  que el titulo se saco del nombre del fichero.
    juzga ("meta pista",
           bool (pista) and pista.get ("INAM", "") != "" and pista.get ("IPRT", "") != "",
           "titulo \"%s\"  pista %s de %d WAV"
             % (pista.get ("INAM", "-") if pista else "-",
                pista.get ("IPRT", "-") if pista else "-", len (wavs)))

    ogg1 = next ((d for f, d in oggs if d), None)
    juzga ("meta ogg",
           bool (ogg1) and ogg1.get ("TITLE", "") != "" and ogg1.get ("ENCODER", "") == "ZATI Sampler",
           "titulo \"%s\"  album \"%s\"  encoder \"%s\""
             % (ogg1.get ("TITLE", "-") if ogg1 else "-",
                ogg1.get ("ALBUM", "-") if ogg1 else "-",
                ogg1.get ("ENCODER", "-") if ogg1 else "-"))

    d = filas.get ("destino", {})
    juzga ("destino",
           d.get ("acepta") == 1 and d.get ("vuelve") == 1 and d.get ("rechaza") == 1
             and d.get ("aguanta") == 1 and d.get ("limpia") == 1,
           "acepta %s vuelve %s rechaza %s aguanta %s limpia %s"
             % (d.get ("acepta", "?"), d.get ("vuelve", "?"), d.get ("rechaza", "?"),
                d.get ("aguanta", "?"), d.get ("limpia", "?")))

    #  EL REBOTE EN VIVO, que es el tercer modo y el que no existia.
    #
    #  Se pidio «opcion de exportar en Live, con un count in para no perder el
    #  tiempo»: MASTER y PISTAS son los dos OFFLINE -un motor clonado, tres
    #  minutos de musica en un par de segundos- y REMUESTREAR es un rebote en
    #  vivo pero a un PAD, no a un fichero.
    #
    #  CON TRES CIFRAS, y la del medio es la que hace falta:
    #    - sale un fichero y pesa lo suyo;
    #    - durante la CUENTA ATRAS no se escribe ni una muestra, o sea que el
    #      clic no acaba dentro del fichero que mandas — que es la clase de
    #      cosa que solo se descubre escuchando lo que ya has mandado;
    #    - y con cuenta y sin cuenta sale el MISMO fichero, que es lo unico que
    #      separa «la cuenta no se escribe» de «con cuenta se escribe menos».
    v = vivo (corre_vivo())
    if v is None:
        malas.append ("vivo")
        print ("%-14s %s" % ("vivo", "la app no publico la linea"))
    else:
        #  Un cuarto de segundo a 48 kHz, 24 bits y dos canales son 72 KB: por
        #  debajo de eso no hay cancion, hay una cabecera. No se compara contra
        #  un numero exacto porque lo que se escribe depende de cuantos bloques
        #  bombee el banco, o sea del reloj — y esta casa ya tiene escrito que
        #  una medida que depende de la maquina no es un veredicto.
        pesa  = v["bytes"] > 72000
        limpio = v["tras_cuenta"] == 0
        igual = v["bytes_cuenta"] > 72000 and abs (v["bytes_cuenta"] - v["bytes"]) < v["bytes"] // 10
        juzga ("vivo",
               pesa and limpio and igual,
               "%s %d bytes   con cuenta %d   escrito durante la cuenta %d   tiradas %d"
                 % (v["nombre"], v["bytes"], v["bytes_cuenta"], v["tras_cuenta"], v["tiradas"]))

    print()
    if malas:
        print ("FALLA:", ", ".join (malas));  return 1
    print ("lo que sale del telefono sale entero, y ahora tambien mientras suena")
    return 0

if __name__ == "__main__":
    sys.exit (main())

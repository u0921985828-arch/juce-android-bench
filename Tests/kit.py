#!/usr/bin/env python3
"""GUARDAR KIT: que lo que queda en el disco sea un kit y no una carpeta.

Esta prueba mira FICHEROS y no la funcion. Un guardado que no se queja y deja
dieciseis WAV de cabecera sola es exactamente el fallo que ya costo un proyecto
mudo en esta app -writeFromAudioSampleBuffer devolviendo true sobre un flujo que
todavia no habia tocado el disco-, y desde aqui "no se quejo" y "esta escrito"
se parecen demasiado.

Cuatro preguntas, y las cuatro tienen respuesta en el disco:

  1. Estan los dieciseis.
  2. Van numerados 01..16, con las DOS cifras. Sin ellas "10" se ordena antes
     que "2" y el kit vuelve barajado, que es como CARGAR KIT lo va a leer.
  3. Ninguno es una cabecera sola.
  4. Y el que llevaba el recorte a la mitad pesa LA MITAD. Esta es la que separa
     "escribio el trozo" de "escribio el fichero entero", y sin mover un
     recorte antes las dos respuestas darian el mismo numero - por eso lo mueve
     auditKit y no la prueba.
"""
import json, os, re, shutil, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

APP = os.path.join(os.path.dirname(__file__), "..", "build",
                   "Zati_artefacts", "Release", "Zati")
NOMBRE = "BANCO DE PRUEBA"

#  Cabecera WAV de 24 bits tal y como la escribe JUCE, mas un frame. Por debajo
#  de esto no hay audio, haya lo que haya en el nombre.
MIN_BYTES = 128


def pantalla():
    d = PANTALLA
    if subprocess.run(["xdpyinfo"], env={**os.environ, "DISPLAY": d},
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode != 0:
        print("FALLA  no hay pantalla virtual en", d,
              "- JUCE se cae en centreWithSize y parece un fallo del codigo")
        sys.exit(2)
    return d


def corre():
    d = pantalla()
    casa = tempfile.mkdtemp(prefix="zati-kit-")
    env = {**os.environ, "HOME": casa, "DISPLAY": d,
           "ZATI_AUDIT": "1", "ZATI_SIZE": "412x915", "ZATI_LANG": "es",
           "ZATI_KIT": NOMBRE}
    out = subprocess.run([APP], env=env, capture_output=True, text=True,
                         timeout=600).stdout
    for linea in out.splitlines():
        linea = linea.strip()
        if linea.startswith("{") and '"kit"' in linea:
            return json.loads(linea), casa
    print("FALLA  la app no volco nada. Salida:", out[-400:])
    shutil.rmtree(casa, ignore_errors=True)
    sys.exit(1)


def main():
    d, casa = corre()
    fallos = []
    lista = d["lista"]

    print(f'kit "{d["kit"]}"  {d["ficheros"]} ficheros  en {d["carpeta"]}')

    # 1. Los dieciseis.
    if d["ficheros"] != 16:
        fallos.append(f'FALLA  se esperaban 16 ficheros y hay {d["ficheros"]}')

    # 2. Numerados con dos cifras y en orden.
    for i, f in enumerate(lista):
        m = re.match(r"^(\d{2}) ", f["n"])
        if not m:
            fallos.append(f'FALLA  "{f["n"]}" no empieza por dos cifras y un espacio')
        elif int(m.group(1)) != i + 1:
            fallos.append(f'FALLA  "{f["n"]}" esta en el sitio {i+1} y dice {m.group(1)}')

    #  Y que el orden por NOMBRE -que es el que usa CARGAR KIT- sea el mismo:
    #  la comprobacion de arriba mira el numero y esta mira lo que hara la
    #  maquina al leerlos.
    if [f["n"] for f in lista] != sorted(f["n"] for f in lista):
        fallos.append("FALLA  ordenados por nombre no salen en el orden de los pads")

    # 3. Ninguno vacio.
    for f in lista:
        if f["bytes"] < MIN_BYTES:
            fallos.append(f'FALLA  "{f["n"]}" son {f["bytes"]} bytes: cabecera sola, no suena')

    # 4. El recorte.
    #
    #  auditKit deja el primer pad en 0.25..0.75, o sea la mitad. A 24 bits el
    #  audio son 3 bytes por muestra y por canal; la cabecera de JUCE ronda los
    #  cien, asi que se compara con holgura y en RATIO, que es lo que no depende
    #  de cuantos canales tenga la fabrica.
    if lista:
        fuente = d["fuente_muestras"]
        bytes0 = lista[0]["bytes"] - 120
        canales = d.get("fuente_canales", 0)
        if fuente > 0 and bytes0 > 0 and canales > 0:
            #  Con los canales DICHOS y no probados: probar uno y dos hasta que
            #  alguno cuadrara dejaba pasar el fichero entero en mono como si
            #  fuera la mitad en estereo, y salio OK con el recorte roto a
            #  proposito. Una regla con dos respuestas posibles acierta por
            #  accidente.
            esc = bytes0 / (3.0 * canales)
            ratio = esc / fuente
            if 0.47 <= ratio <= 0.53:
                print(f"recorte: {esc:.0f} de {fuente} muestras "
                      f"({ratio*100:.1f}%) en {canales} canal(es)  OK")
            else:
                fallos.append(f'FALLA  el primer pad iba recortado a la mitad y pesa '
                              f'{lista[0]["bytes"]} bytes: {esc:.0f} muestras de las '
                              f'{fuente} de la fuente ({ratio*100:.1f}%), no la mitad')

    # 5. Y que salga de DONDE tiene que salir.
    #
    #  El tamano dice cuantas muestras se escribieron y no cuales: un fichero
    #  del largo correcto sacado del sitio equivocado pesa igual. La primera
    #  version de esta prueba se quedaba en el tamano, se rompio el recorte a
    #  proposito -copiar desde cero en vez de desde el inicio del recorte- y
    #  dijo OK. Asi que se comparan dos numeros que vienen por caminos
    #  distintos: el RMS leido DEL FICHERO y el del trozo que la app tenia en
    #  memoria. Solo coinciden si el trozo salio de su sitio.
    dd, de_ = d.get("rms_disco", 0.0), d.get("rms_esperado", 0.0)
    if de_ <= 1e-6:
        fallos.append("FALLA  el trozo esperado esta mudo: la prueba no puede juzgar nada")
    else:
        rel = abs(dd - de_) / de_
        if rel > 0.02:
            fallos.append(f"FALLA  lo escrito no es el trozo recortado: RMS en disco {dd:.5f} "
                          f"contra {de_:.5f} esperado ({rel*100:.1f}% de diferencia)")
        else:
            print(f"origen : RMS disco {dd:.5f}  memoria {de_:.5f}  ({rel*100:.2f}%)  OK")

    shutil.rmtree(casa, ignore_errors=True)

    for f in fallos:
        print(f)
    print("VEREDICTO:", "OK" if not fallos else f"{len(fallos)} FALLA")
    sys.exit(0 if not fallos else 1)


if __name__ == "__main__":
    main()

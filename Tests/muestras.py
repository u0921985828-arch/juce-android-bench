#!/usr/bin/env python3
#  Lo que entra del disco no rompe el audio (auditoria 2026-10, estabilidad#3,
#  #13, #14, #15, #16, #26). Escribe ficheros hostiles, los pasa por Carga
#  (los dos lectores reales) y juzga:
#    - ninguna muestra cargada lleva NaN/Inf, ni QUITAR RUIDO las fabrica
#    - ninguna pasa de +24 dBFS (1e30 o basura decodificada)
#    - como mucho dos canales
#    - la frecuencia del detalle es la que se reproduce, nunca negativa
#    - MP3 registrado una vez
#      python3 Tests/muestras.py [ruta/a/Carga]
import json, math, os, struct, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
CARGA = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "build-bench", "Carga_artefacts", "Release", "Carga")

def wav (ruta, datos, canales, sr, bits=16, fmt=1):
    blk = canales * bits // 8
    cab = struct.pack ("<4sI4s4sIHHIIHH4sI", b"RIFF", 36 + len (datos), b"WAVE", b"fmt ", 16, fmt,
                       canales, sr & 0xffffffff, (sr * blk) & 0xffffffff, blk, bits, b"data", len (datos))
    open (ruta, "wb").write (cab + datos)

def seno (n, canales, amp=0.5):
    return b"".join (struct.pack ("<h", int (amp * 32767 * math.sin (0.05 * i))) * canales for i in range (n))

def f32 (vals, canales):
    return b"".join (struct.pack ("<f", v) * canales for v in vals)

def escribe (d):
    n = 48000
    wav (os.path.join (d, "00-control.wav"), seno (n, 2), 2, 48000)
    vals = [float ("nan") if i % 4 == 0 else (float ("inf") if i % 7 == 0 else 0.5 * math.sin (0.05 * i)) for i in range (n)]
    wav (os.path.join (d, "01-nan-inf.wav"), f32 (vals, 1), 1, 48000, 32, 3)
    wav (os.path.join (d, "02-todo-nan.wav"), f32 ([float ("nan")] * n, 1), 1, 48000, 32, 3)
    wav (os.path.join (d, "03-1e30.wav"), f32 ([1e30 * math.sin (0.05 * i) for i in range (n)], 1), 1, 48000, 32, 3)
    wav (os.path.join (d, "04-sr-enorme.wav"), seno (n, 1), 1, 4000000000)
    wav (os.path.join (d, "05-sr-cero.wav"), seno (n, 1), 1, 0)
    wav (os.path.join (d, "06-16canales.wav"), seno (4800, 16), 16, 48000)
    wav (os.path.join (d, "07-255canales.wav"), seno (480, 255), 255, 48000)
    open (os.path.join (d, "08-mp3-falso.mp3"), "wb").write (bytes ((i * 2654435761) >> 13 & 0xff for i in range (200000)))
    #  Un .wav no se decodifica como MP3 (estabilidad#4).
    wav (os.path.join (d, "09-f64.wav"), b"".join (struct.pack ("<d", 0.1 * math.sin (0.05 * i)) for i in range (20736)), 1, 48000, 64, 3)
    open (os.path.join (d, "10-wav-basura.wav"), "wb").write (bytes ((i * 40503) >> 7 & 0xff for i in range (200000)))

#  Lo unico que puede salir de esos dos si entran: lo que su cabecera declara.
DECLARA = { "09-f64.wav": (1, 48000), "10-wav-basura.wav": None }

def main():
    if not os.path.exists (CARGA):
        sys.exit ("no hay Carga en %s: cmake -B build-bench -DZATI_BENCH=ON && cmake --build build-bench --target Carga" % CARGA)
    d = tempfile.mkdtemp (prefix="zati-muestras-")
    escribe (d)
    p = subprocess.run ([CARGA, d], capture_output=True, text=True, timeout=600)
    filas = []
    for l in p.stdout.splitlines():
        l = l.strip()
        if l.startswith ("{"):
            try: filas.append (json.loads (l))
            except Exception: pass
    malas = []
    mp3 = next ((r for r in filas if "mp3_cargador" in r), {})
    print ("mp3      formatos registrados en el cargador: %s" % mp3.get ("mp3_cargador"))
    if mp3.get ("mp3_cargador") != 1:
        malas.append ("MP3 registrado %s veces" % mp3.get ("mp3_cargador"))
    corte = next ((r for r in filas if "cerrar_ms" in r), {})
    print ("cerrar   a media carga: %s ms, la carga entera %s ms" % (corte.get ("cerrar_ms"), corte.get ("carga_entera_ms")))
    if not corte or corte["cerrar_ms"] * 4 > corte["carga_entera_ms"]:   # estabilidad#24
        malas.append ("cerrar a media carga espera %s ms de %s" % (corte.get ("cerrar_ms"), corte.get ("carga_entera_ms")))
    for r in filas:
        if "f" not in r: continue
        if r["via"] == "detalle":
            print ("%-22s detalle  %s %s" % (r["f"], "OK" if r["ok"] else "NO", r["detalle"]))
            for t in r["detalle"].split():
                if t.endswith ("Hz"):
                    hz = int (t[:-2]) if t[:-2].lstrip ("-").isdigit() else -1
                    if not 4000 <= hz <= 768000:
                        malas.append ("%s: el detalle dice %s" % (r["f"], t))
            continue
        if not r["ok"]:
            print ("%-22s %-8s rechazado" % (r["f"], r["via"]))
            continue
        if r["f"] in DECLARA and (DECLARA[r["f"]] is None or (r["ch"], int (r["sr"])) != DECLARA[r["f"]]):
            malas.append ("%s por %s: entra como %dch %.0fHz, no lo que declara" % (r["f"], r["via"], r["ch"], r["sr"]))
        print ("%-22s %-8s ch %-3d sr %-7.0f no finitas %-6d pico %-8.3g ruido no finitas %d"
               % (r["f"], r["via"], r["ch"], r["sr"], r["no_finitas"], r["pico"], r["ruido_no_finitas"]))
        if r["no_finitas"] or r["ruido_no_finitas"] > 0:
            malas.append ("%s por %s: %d no finitas, QUITAR RUIDO %d" % (r["f"], r["via"], r["no_finitas"], r["ruido_no_finitas"]))
        if r["pico"] > 16.0:
            malas.append ("%s por %s: pico %.3g" % (r["f"], r["via"], r["pico"]))
        if r["ch"] > 2:
            malas.append ("%s por %s: %d canales" % (r["f"], r["via"], r["ch"]))
        if not 4000 <= r["sr"] <= 768000:
            malas.append ("%s por %s: frecuencia %.0f" % (r["f"], r["via"], r["sr"]))
    ctl = [r for r in filas if r.get ("f") == "00-control.wav" and r.get ("via") in ("cargador", "sesion") and r.get ("ok")]
    if len (ctl) != 2:
        malas.append ("el control no entro por los dos lectores")
    print()
    for m in malas: print ("FALLA ", m)
    print ("VEREDICTO:", "OK" if not malas else "%d FALLA" % len (malas))
    return 1 if malas else 0

if __name__ == "__main__":
    sys.exit (main())

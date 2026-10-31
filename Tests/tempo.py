#!/usr/bin/env python3
# ============================================================================
#  AL TEMPO (tempo#sincro). Del telefono: «no puede sincronizar el sample al
#  tempo que este el proyecto». En RECORTE, AL TEMPO pone los tiempos que mas
#  se parecen a lo que dura la ventana (1, 2, 4, 8...), el motor los recibe y
#  estira la muestra al tempo sin desafinar (StressTest, «al tempo»), el
#  proyecto los guarda y apagarlo los quita.
#
#      python3 Tests/tempo.py [ruta/al/Zati]
# ============================================================================
import json, os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = sys.argv[1] if len (sys.argv) > 1 else os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from expo import PANTALLA, display_alive, MIN_TOUCH                        # noqa: E402


def corre (tam):
    casa = tempfile.mkdtemp (prefix="zati-tempo-")
    env = dict (os.environ, DISPLAY=PANTALLA, HOME=casa,
                XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE=tam, ZATI_LANG="es", ZATI_TEMPO="1")
    try:
        p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=180)
    except subprocess.TimeoutExpired:
        return None
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    for l in p.stdout.splitlines():
        l = l.strip()
        if not l.startswith ("{"): continue
        try: r = json.loads (l)
        except Exception: continue
        if r.get ("tempo"): return r
    return None


def main():
    if not os.path.exists (APP):
        print ("no existe %s: compila antes" % APP)
        return 1
    if not display_alive():
        print ("no hay DISPLAY vivo: arranca Xvfb antes")
        return 1
    malo = []
    for tam in ("412x915", "280x653"):
        r = corre (tam)
        if r is None:
            malo.append ("en %s la auditoria no contesto" % tam)
            continue
        print ("%-8s tapa %s vista %d   ventana %.3f s a %.0f bpm -> espera %.2f   antes %.2f puesto %.2f «%s»   apagado %.2f «%s»   proyecto %.2f"
               % (tam, r["ancho"], r["vista"], r["seg"], r["bpm"], r["espera"], r["antes"], r["puesto"], r["texto"],
                  r["quitado"], r["texto_off"], r["vuelve"]))
        if r["vista"] != 1 or r["ancho"][1] < MIN_TOUCH:
            malo.append ("en %s AL TEMPO no esta a la vista en RECORTE o no mide un dedo: %s" % (tam, r["ancho"]))
        if r["espera"] <= 0 or r["antes"] != 0 or abs (r["puesto"] - r["espera"]) > 1e-3 or r["encendido"] != 1:
            malo.append ("en %s tocar AL TEMPO no pone los tiempos de la ventana en el motor: espera %.2f, puso %.2f"
                         % (tam, r["espera"], r["puesto"]))
        if "TIEMPOS" not in r["texto"] or r["texto_off"] != "AL TEMPO":
            malo.append ("en %s la tapa no dice los tiempos encendida o AL TEMPO apagada: «%s» / «%s»" % (tam, r["texto"], r["texto_off"]))
        mt = r.get ("mantiene", [-1, -1, -1]); e = r["espera"]   # tempo#doble
        print ("         mantener: %s «%s»" % (mt, r.get ("texto_doble", "")))
        if [round (x, 2) for x in mt] != [round (2 * e, 2), round (e / 2, 2), round (e, 2)] or "TIEMPO" not in r.get ("texto_doble", ""):
            malo.append ("en %s mantener AL TEMPO no va al doble, a la mitad y vuelta: %s (ventana %.2f)" % (tam, mt, e))
        if r["quitado"] != 0 or abs (r["vuelve"] - r["puesto"]) > 1e-3:
            malo.append ("en %s apagar no quita los tiempos (%.2f) o el proyecto no los guarda (%.2f)" % (tam, r["quitado"], r["vuelve"]))
    for m in malo: print ("FALLA  " + m)
    if not malo: print ("AL TEMPO pone los tiempos de la ventana, el motor los recibe, el proyecto los guarda y apagarlo los quita")
    return 1 if malo else 0


if __name__ == "__main__":
    sys.exit (main())

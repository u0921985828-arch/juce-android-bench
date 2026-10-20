#!/usr/bin/env python3
# ============================================================================
#  LA FERIA, HALLAZGO POR HALLAZGO.
#
#  Cinco jueces jugaron con la app sin saber nada de ella (feria 2026-10) y
#  escribieron lo que les fallo. Cada regla es uno de esos fallos medido por
#  el camino que tomo el juez, con ZATI_FERIA (ver auditFeria):
#
#   a. RUTEO. El primer efecto sin reparto entra en los 64 pads; con reparto
#      solo en el elegido (J1, J2, J5: la reverb no llegaba al fichero).
#   b. XY. Lo que encendio la persona sigue encendido al soltar; lo que
#      encendio el toque se apaga al soltar (J3).
#   c. DESHACER no se mueve cuando aparece REHACER (J3).
#   d. LA REJILLA no salta al primer toque (J2, fotos 03 -> 04).
#   e. EL PAD ELEGIDO vuelve con el estado (J4).
#   f. TEXTO BASURA no mueve el mando: «abc» y vacio dejan el valor (J4).
#   g. EL SEGURO? que caduca desarma y lo dice (J4, fotos 29-33).
#   h. ATRAS cierra lo de arriba y despues no hace nada (J3).
#   l. UN BOTON QUE ASOMA bajo una ficha la cierra y hace lo suyo (J4 M9).
#   i. EL LIMITADOR se ve: master +12 y pads x4 escriben LIM en el cristal (J2).
#   j. BORRAR otro proyecto con uno abierto no tira la app (B1, J4). Con
#      ZATI_FERIA_APP apuntando al binario ASan, ademas, cero errores.
#   k. UN CAMBIO SE GUARDA AL SOLTAR: el tempo llega a disco 1.5 s despues del
#      ultimo toque, y sin toque no (el control), que el estado entero era
#      cada 20 s y J4 espero 8.
#   m. LO TECLEADO TAMBIEN (auditoria 2026-10, persistencia#4): un tempo
#      escrito en la caja del mando con Return llega a disco a los 2 s sin
#      ningun dedo levantado, y el Return del nombre de un canal deja el
#      estado encargado.
#   n. EL PROYECTO CON CAMBIOS LO DICE (auditoria 2026-10, persistencia#6,
#      ux-flujos#3 y #4): tras mover el tempo la banda dice SIN GUARDAR y no
#      A SALVO -en los cuatro idiomas y con un nombre largo-, ABRIR y NUEVO
#      avisan y nombran el proyecto, GUARDAR o DESHACER hasta lo guardado lo
#      limpian, y la sesion recuperada sigue diciendolo.
#
#      python3 Tests/feria.py
# ============================================================================
import json, os, re, shutil, subprocess, sys, tempfile

from kits import PANTALLA                                          # noqa: E402

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.environ.get ("ZATI_FERIA_APP") or os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")


def corre (casa):
    env = dict (os.environ, HOME=casa, XDG_DATA_HOME=os.path.join (casa, ".local", "share"),
                ZATI_AUDIT="1", ZATI_SIZE="393x851", ZATI_LANG="es", DISPLAY=PANTALLA,
                ZATI_FERIA="1", ASAN_OPTIONS="detect_leaks=0:halt_on_error=0")
    p = subprocess.run ([APP], env=env, capture_output=True, text=True, timeout=900)
    fila = None
    for l in p.stdout.splitlines():
        l = l.strip()
        if l.startswith ('{"feria"'):
            fila = json.loads (l)
    asan = p.stderr.count ("ERROR: AddressSanitizer")
    return fila, asan


def main():
    if not os.path.exists (APP):
        sys.exit ("no hay binario: compila primero (cmake --build build-rel)")
    casa = tempfile.mkdtemp (prefix="zati-feria-")
    #  Una sesion rota de antes, como la deja un apagado a mitad de escribir:
    #  el ancla apunta a una biblioteca con un state.xml truncado.
    lib = os.path.join (casa, "ZATILIB")
    os.makedirs (os.path.join (lib, ".sesion"))
    with open (os.path.join (lib, ".sesion", "state.xml"), "w") as f:
        f.write ('<?xml version="1.0"?>\n<ZATI bpm="131"><PADS><PAD i="0" na')
    os.makedirs (os.path.join (casa, ".config"))
    with open (os.path.join (casa, ".config", "zati-home.txt"), "w") as f:
        f.write (lib)
    try:
        r, asan = corre (casa)
    finally:
        shutil.rmtree (casa, ignore_errors=True)
    if r is None:
        print ("FALLA  la sonda ZATI_FERIA no contesto (errores ASan: %d)" % asan); return 1

    malas = []
    def regla (nombre, ok, texto):
        print ("  %-8s %s  %s" % (nombre, "ok   " if ok else "FALLA", texto))
        if not ok: malas.append (nombre)

    regla ("ruteo", r["ruteo_todos"] == 64 and r["ruteo_uno"] == 2 and r["ruteo_elegido"] == 1,
           "sin reparto %d pads al canal (64); con reparto %d (2: el que habia y el elegido)"
           % (r["ruteo_todos"], r["ruteo_uno"]))
    regla ("xy", r["xy_sigue"] == 1 and r["xy_entra"] == 1 and r["xy_sale"] == 1,
           "encendido por la persona sigue %d; el toque enciende %d y al soltar apaga %d"
           % (r["xy_sigue"], r["xy_entra"], r["xy_sale"]))
    regla ("deshacer", r["undo_con"] == r["undo_sin"],
           "DESHACER %s con REHACER, %s sin" % (r["undo_con"], r["undo_sin"]))
    regla ("rejilla", r["grid_con"] == r["grid_sin"],
           "rejilla %s sin paso elegido, %s con" % (r["grid_sin"], r["grid_con"]))
    # Un compas puede estrecharse hasta un 25 % para entrar entero sin barra (secuenciador#16); nunca crecer.
    regla ("celda", r["celda_1c"] <= r["celda_4c"] + 0.01 and r["celda_1c"] >= 0.75 * r["celda_4c"]
                    and r["celda_8p"] >= 2 * r["celda_4c"] - 0.01
                    and r["celda_8p"] >= 36,
           "celda de paso %.1f px con 1 compas y %.1f con 4; con 1-8 pistas %.1f de alto"
           % (r["celda_1c"], r["celda_4c"], r["celda_8p"]))
    regla ("pad", r["pad_vuelve"] == 9, "pad elegido tras deshacer el estado: %d (9)" % r["pad_vuelve"])
    regla ("texto", r["bpm_basura"] == 127 and r["bpm_vacio"] == 127 and r["bpm_cifra"] == 90
                    and abs (r["master_basura"] - 0.5) < 1e-9,
           "BPM 127 + abc = %g, + vacio = %g, + 90 = %g; master 0.5 + abc = %g"
           % (r["bpm_basura"], r["bpm_vacio"], r["bpm_cifra"], r["master_basura"]))
    regla ("caduca", r["caduca_desarma"] == 1 and r["caduca_dice"] == 1,
           "SEGURO? caducado: desarma %d, lo dice %d" % (r["caduca_desarma"], r["caduca_dice"]))
    regla ("atras", r["atras1"] == 1 and r["atras2"] == 0 and r["atras_cerro"] == 1,
           "atras con SET abierto %d y cerrado %d; despues %d"
           % (r["atras1"], r["atras_cerro"], r["atras2"]))
    regla ("asoma", r["asoma_undo"] == 1 and r["asoma_cierra"] == 1 and r["asoma_deshace"] == 1,
           "DESHACER asoma bajo SEC %d; tocarlo cierra %d y deshace %d"
           % (r["asoma_undo"], r["asoma_cierra"], r["asoma_deshace"]))
    regla ("lim", r["sin_aparato"] == 1 and r["recorte"] > 1.0012 and r["lim_db"] > 0.01,
           "recorte %.3f (sin aparato %d), el cristal dice LIM -%.1f dB"
           % (r["recorte"], r["sin_aparato"], r["lim_db"]))
    regla ("borrar", r["fila_uno"] >= 0 and r["borra_fuera"] == 1 and r["borra_dice"] == 1
                     and r["borra_abierto"] == 1 and asan == 0,
           "UNO borrado con DOS abierto: carpeta fuera %d, lo dice %d, DOS sigue %d, errores ASan %d"
           % (r["borra_fuera"], r["borra_dice"], r["borra_abierto"], asan))
    regla ("punto", r.get("punto_fuera", -1) == 1 and r.get("punto_vecino", -1) == 1,
           "BORRAR \"FERIA.PUNTO\": su carpeta fuera %d, \"FERIAPUNTO\" sigue %d"
           % (r.get("punto_fuera", -1), r.get("punto_vecino", -1)))
    regla ("pila", r.get("nombre_vuelve", -1) == 1 and r.get("abrir_vacia", -1) == 1,
           "DESHACER devuelve el nombre del pad %d; ABRIR deja la pila vacia %d"
           % (r.get("nombre_vuelve", -1), r.get("abrir_vacia", -1)))
    regla ("corrupto", r.get("corruptos", 0) == 1,
           "state.xml roto apartado antes de pisarlo: %d copias" % r.get("corruptos", 0))
    regla ("fondo", r.get("fondo_cuadros", -1) == 0 and r.get("frente_cuadros", 0) > 0,
           "cuadros pintados con la app al fondo %d (delante, control: %d)"
           % (r.get("fondo_cuadros", -1), r.get("frente_cuadros", 0)))
    regla ("guardado", r["bpm_antes"] == 120 and r["bpm_sin_soltar"] == 120 and r["bpm_tras_soltar"] == 131,
           "tempo en disco: %g antes, %g a los 2 s sin soltar (control), %g a los 2 s de soltar"
           % (r["bpm_antes"], r["bpm_sin_soltar"], r["bpm_tras_soltar"]))

    regla ("tecleado", r.get("bpm_antes_tecla", -1) == 131 and r.get("bpm_tecleado_memoria", -1) == 140
                       and r.get("bpm_tecleado", -1) == 140 and r.get("nombre_encarga", -1) == 1,
           "tempo tecleado 140 con Return (en memoria %g): en disco %g antes y %g a los 2 s;"
           " el Return del nombre de canal encarga el estado %d"
           % (r.get("bpm_tecleado_memoria", -1), r.get("bpm_antes_tecla", -1),
              r.get("bpm_tecleado", -1), r.get("nombre_encarga", -1)))

    regla ("escucha", r.get ("escucha_suena", -1) == 1 and r.get ("escucha_atras", -1) == 1
                      and r.get ("escucha_tarde", -1) == 1,
           "explorador: escucha suena %d, ATRAS devuelve %d, carga tardia no pisa %d"
           % (r.get ("escucha_suena", -1), r.get ("escucha_atras", -1), r.get ("escucha_tarde", -1)))

    regla ("rojo", r.get ("rojo_tinta", 0) >= 4.5,
           "texto sobre tapa roja, peor de las cuatro carcasas: %.2f (AA 4.5)" % r.get ("rojo_tinta", 0))

    regla ("cristal", r.get ("cristal_trazo", 0) >= 3.0,
           "curva del EQ y cursor del XY sobre el cristal, peor carcasa: %.2f (1.4.11 pide 3)" % r.get ("cristal_trazo", 0))
    regla ("armada", r.get ("armada_nombre", 0) == 1 and r.get ("armada_anuncia", 0) == 1,
           "la tapa armada se llama como lo que pregunta %d y se anuncia %d veces"
           % (r.get ("armada_nombre", 0), r.get ("armada_anuncia", 0)))
    regla ("pulgar", r.get ("pulgar_filo", 0) >= 3.0,
           "filo del pulgar de la barra contra la ficha, peor carcasa: %.2f (1.4.11 pide 3)" % r.get ("pulgar_filo", 0))

    regla ("mantener", r.get ("manten_total", 0) > 0 and r.get ("manten_accion", 0) == r.get ("manten_total", 0)
           and r.get ("manten_dispara", 0) == 1,
           "botones con mantener %d, con accion para el lector %d, la accion dispara %d"
           % (r.get ("manten_total", 0), r.get ("manten_accion", 0), r.get ("manten_dispara", 0)))
    regla ("ficha modal", r.get ("cara_tapada", -1) == 0 and r.get ("cara_vuelve", 0) > 0,
           "con la ficha abierta el lector ve %d controles de la cara (al cerrar %d)"
           % (r.get ("cara_tapada", -1), r.get ("cara_vuelve", 0)))
    regla ("paso marcado", r.get ("paso_marca", 0) >= 3.0,
           "marca de un paso puesto contra su relleno, peor carcasa y color: %.2f (1.4.1 y 1.4.11)" % r.get ("paso_marca", 0))
    regla ("pista", r.get ("pista_slider", 0) >= 3.0,
           "pista de un deslizador contra la tarjeta, peor carcasa: %.2f (1.4.11 pide 3)" % r.get ("pista_slider", 0))
    regla ("rejilla", r.get ("bloque_antes", 0) > 0 and r.get ("bloque_despues", 0) == r.get ("compas_despues", -1),
           "un bloque de un compas (%d pasos) mide %d tras pasar a 1/32, y el compas son %d (secuenciador#1)"
           % (r.get ("bloque_antes", 0), r.get ("bloque_despues", 0), r.get ("compas_despues", 0)))
    regla ("paso16", r.get ("sec_barra", 0) == 1 or r.get ("sec_cols", 0) >= r.get ("sec_largo", 99),
           "sin barra la rejilla ensena %d columnas enteras de un patron de %d (secuenciador#16)"
           % (r.get ("sec_cols", 0), r.get ("sec_largo", 0)))
    parte = r.get ("parte_en", "")
    jerga = [w for w in ("ATASCO", "CAIDA", "senal", " en ", "/", "se paro", "ficha") if w in parte]
    regla ("parte", parte != "" and not jerga,
           "el parte de la vez anterior en ingles: %r%s (textos#2)"
           % (parte, (" lleva " + ", ".join (jerga)) if jerga else ""))
    #  visual#2: ruta, subir y NOMBRE cambian con la carcasa y no son el V4 oscuro.
    pc = [t.split() for t in r.get ("piel_cajas", "").split ("|") if t.strip()]
    v4 = {"FF323E44", "FF263238", "323E44", "263238"}
    malpiel = [] if len (pc) == 3 and all (len (f) == 4 for f in pc) else ["sin medida"]
    if not malpiel:
        for i, n in enumerate (("ruta", "subir", "nombre canal", "nombre preset")):
            col = [f[i].upper() for f in pc]
            if any (c in v4 for c in col) or len (set (col)) < 3:
                malpiel.append ("%s %s" % (n, "/".join (col)))
    regla ("piel cajas", not malpiel and r.get ("nombre_fuente", -1) == 1,
           "ruta, subir y cajas NOMBRE siguen a la carcasa%s; fuente de NOMBRE como la del proyecto: %s (visual#2)"
           % ((": " + ", ".join (malpiel)) if malpiel else "", "si" if r.get ("nombre_fuente", -1) == 1 else "NO"))
    regla ("talkback", r.get ("deslizadores", 0) > 20 and r.get ("pasos_finos", -1) == 0 and r.get ("cifra_cruda", -1) == 0,
           "de %d deslizadores, %d piden mas de 100 gestos de punta a punta y %d leen una cifra cruda%s (accesibilidad#20, #21)"
           % (r.get ("deslizadores", 0), r.get ("pasos_finos", -1), r.get ("cifra_cruda", -1),
              (": " + r.get ("crudos", "")) if r.get ("crudos") else ""))
    regla ("foco", r.get ("foco_boton", -1) > 20 and r.get ("foco_mando", -1) > 20 and r.get ("foco_dedo", -1) == 0,
           "el foco de teclado cambia %d pixeles en un boton y %d en un mando (y %d con el dedo abajo) (accesibilidad#7)"
           % (r.get ("foco_boton", -1), r.get ("foco_mando", -1), r.get ("foco_dedo", -1)))
    regla ("negras", r.get ("negra_min", 0) >= 24,
           "la tecla negra mas estrecha se toca en %d px, con 24 de minimo (accesibilidad#18)" % r.get ("negra_min", 0))
    regla ("cifras", r.get ("sin_letra", -1) == 0,
           "%d botones se leen como una cifra o un signo, sin decir de que son (accesibilidad#23)" % r.get ("sin_letra", -1))
    regla ("on/off", r.get ("en_ingles", -1) == 0,
           "%d conmutadores le dicen On u Off al lector en cualquier idioma (accesibilidad#32)" % r.get ("en_ingles", -1))
    regla ("toque", r.get ("toque_sube", 0) == 3 and r.get ("toque_baja", 0) == 3,
           "un toque sin arrastre sube %d/3 y baja %d/3 mandos (giro, barra, fader) (accesibilidad#19)"
           % (r.get ("toque_sube", 0), r.get ("toque_baja", 0)))
    regla ("bitacora", r.get ("bitacora_abre", "") == "proyecto/fin",
           "tras abrir un proyecto la bitacora dice «%s» (estabilidad#27)" % r.get ("bitacora_abre", ""))
    with open (os.path.join (os.path.dirname (os.path.dirname (os.path.abspath (__file__))), "Source", "MainComponent.cpp")) as fuente:
        crudos_async = sum (1 for l in fuente if re.search (r"(callAsync|loadAsync) \(.*\[this[,\]]", l))
    regla ("async", crudos_async == 0,
           "%d callAsync/loadAsync capturan this crudo y no un SafePointer (estabilidad#32)" % crudos_async)
    raiz_src = os.path.join (os.path.dirname (os.path.dirname (os.path.abspath (__file__))), "Source")
    with open (os.path.join (raiz_src, "Main.cpp")) as fuente: sueltos = fuente.read().count (".detach()")
    with open (os.path.join (raiz_src, "MidiIo.h")) as fuente: midi_io = fuente.read()
    ciego = len (re.findall (r"\n\s*stopThread \(\d+\);\s*\n\s*allNotesOff\(\);", midi_io))
    regla ("hilos", sueltos == 0 and ciego == 0,
           "%d hilos sueltos en Main.cpp y %d allNotesOff tras un stopThread sin mirar (estabilidad#29, #30)" % (sueltos, ciego))
    regla ("x carga", r.get ("x_tapada", -1) == 0,
           "con Cargando encima la X del navegador %s (estabilidad#11)"
           % ("se toca" if r.get ("x_tapada", -1) == 0 else "queda TAPADA"))
    regla ("con nombre", r.get ("sin_nombre", -1) == 0 and r.get ("cruces", -1) == 0,
           "cajas sin nombre %d, cruces que se leen «×» %d (botones sin letra en total: %d)"
           % (r.get ("sin_nombre", -1), r.get ("cruces", -1), r.get ("simbolos", -1)))
    regla ("pad elegido", r.get ("pads_marcables", 0) == 64 and r.get ("pad_elegido", 0) == 1,
           "pads marcables %d de 64, el elegido se dice %d" % (r.get ("pads_marcables", 0), r.get ("pad_elegido", 0)))
    regla ("midi ajeno", r.get ("midi_ajeno", -1) == 0 and r.get ("midi_gordo", -1) == 0,
           "IMPORTAR MIDI con un .wav trae %d notas y con un .mid de 5 MB %d"
           % (r.get ("midi_ajeno", -1), r.get ("midi_gordo", -1)))
    regla ("estado al lector", r.get ("estado_anuncia", 0) == 1,
           "la linea de estado se anuncia %d veces (una)" % r.get ("estado_anuncia", 0))
    regla ("regla del piano", r.get ("regla_tinta", 0) >= 4.5,
           "cifras de la regla del piano, peor carcasa: %.2f (1.4.3 pide 4.5)" % r.get ("regla_tinta", 0))
    regla ("rotulo del EQ", r.get ("eq_rotulo", 0) >= 4.5 and r.get ("eq_cuerpo", 0) >= 11,
           "rotulos de frecuencia del EQ: %.2f a %g px (4.5 y 11)" % (r.get ("eq_rotulo", 0), r.get ("eq_cuerpo", 0)))
    regla ("filo de nota", r.get ("nota_filo", 0) >= 3.0 and r.get ("nota_grosor", 0) >= 1.5,
           "filo de una nota del piano contra su celda: %.2f a %g px (3 y 1.5)" % (r.get ("nota_filo", 0), r.get ("nota_grosor", 0)))
    regla ("titulos", r.get ("titulos_copiados", 0) > 0 and r.get ("titulos_viejos", -1) == 0,
           "nombres accesibles copiados del rotulo que cambian con el idioma: %d, se quedan en el anterior: %d"
           % (r.get ("titulos_copiados", 0), r.get ("titulos_viejos", -1)))

    regla ("singuardar", r.get ("limpio_avisa", -1) == 0 and r.get ("sucio_avisa", -1) == 1
                         and r.get ("sucio_salvo", -1) == 0 and r.get ("aviso_idiomas", -1) == 4
                         and r.get ("aviso_largo", -1) == 4 and r.get ("abrir_avisa", -1) == 1
                         and r.get ("abrir_dice", -1) == 1 and r.get ("nuevo_avisa", -1) == 1
                         and r.get ("guardar_limpia", -1) == 1 and r.get ("abrir_limpio", -1) == 1
                         and r.get ("deshacer_limpia", -1) == 1 and r.get ("sesion_lleva", -1) == 1,
           "con el tempo movido la banda dice SIN GUARDAR %d (A SALVO %d; recien abierto %d), en %d/4 idiomas"
           " y %d/4 con nombre largo; ABRIR avisa %d y nombra el proyecto %d, NUEVO avisa %d;"
           " GUARDAR limpia %d, ABRIR vuelve a secas %d, DESHACER limpia %d; la sesion recuperada lo trae %d"
           % (r.get ("sucio_avisa", -1), r.get ("sucio_salvo", -1), r.get ("limpio_avisa", -1),
              r.get ("aviso_idiomas", -1), r.get ("aviso_largo", -1), r.get ("abrir_avisa", -1),
              r.get ("abrir_dice", -1), r.get ("nuevo_avisa", -1), r.get ("guardar_limpia", -1),
              r.get ("abrir_limpio", -1), r.get ("deshacer_limpia", -1), r.get ("sesion_lleva", -1)))

    for m in malas: print ("FALLA ", m)
    print ("VEREDICTO:", "OK" if not malas else "%d FALLA" % len (malas))
    return 1 if malas else 0


if __name__ == "__main__":
    sys.exit (main())

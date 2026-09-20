#!/usr/bin/env python3
"""EL PLANO DE CADA PANTALLA: titulos, secciones y controles en orden de lectura.

expo.py mide GEOMETRIA -dedo, solapes, ventana, celda, rotulo cortado- y no
sabe nada de ESTRUCTURA: si una ficha tiene titulo, si su titulo coincide con
la tapa que la abre, si las secciones van en un orden que se pueda aprender, o
si hay un encabezado sin nada debajo. Eso no se puede juzgar sin ver la
pantalla escrita, y una captura no se puede comparar con la de la semana que
viene.

Y hasta ahora ni siquiera era posible mirarlo: los titulos y los nombres de
seccion SE PINTAN, y las seis reglas recorren el arbol de COMPONENTES. Un
rotulo dibujado no es un componente. Por eso la mesa llevaba desde el primer
dia titulada "MIX" a mano -sin pasar por T(), con la fila MIX->MEZCLA en la
tabla y la pestana que la abre usandola- y no lo vio nadie. Ver
UiAudit::rotulo.

El plano se lee de arriba abajo, que es como se lee la pantalla:

    MEZCLA                                    412x915
      titulo    MEZCLA                         31,113  351x16
      seccion   ...
      [control] fader 01                       ...

    python3 Tests/plano.py              todas las fichas
    python3 Tests/plano.py set mix      solo esas
"""
import glob, json, os, re, shutil, subprocess, sys, tempfile

#  LA PANTALLA QUE SE COMPRUEBA ES LA QUE SE USA: `PANTALLA` vive en
#  `kits.py`, al lado de `display_alive`, y quien arranca la app la escribe
#  en su entorno. Sin esta linea la comprobacion dice que si contra :99 y el
#  arranque se va sin ventana — el veredicto entero en rojo con la app
#  perfecta, que es lo que ya costo una tarde en `cpu.py` y otra en `instr.py`.
from kits import PANTALLA                                          # noqa: E402

sys.path.insert (0, os.path.dirname (os.path.abspath (__file__)))
from kits import display_alive

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
APP  = os.path.join (ROOT, "build", "Zati_artefacts", "Release", "Zati")

#  La ficha, y la tapa que la abre. La segunda columna es lo que hay que poder
#  comparar con el titulo: tocas MEZCLA y tienes que aterrizar en MEZCLA.
FICHAS = [
    ("",       "la cara"),
    ("pads",   "PADS"),
    ("sec",    "SEC"),
    ("piano",  "SEC"),
    ("paso",   "SEC"),
    ("song",   "CANCION"),
    ("mix",    "MEZCLA"),
    ("xy",     "XY"),
    ("set",    "AJUSTES"),
    ("proj",   "AJUSTES"),
    ("gest",   "AJUSTES"),
    ("midi",   "AJUSTES"),
    ("manual", "AJUSTES"),
    ("expo",   "AJUSTES"),
    ("rack",   "MEZCLA"),
    ("chop",   "PADS"),
    ("browse", "CARGAR"),
    #  Y LAS TRES QUE FALTABAN, que es donde esta regla no podia decir que no.
    #
    #  El tour es la que vale: era la UNICA ficha de la app cuyo titulo se
    #  dibujaba con `drawText` a pelo, o sea sin pasar por `pintaTitulo` y por
    #  tanto sin publicar su banda. Invisible para el volcado de rotulos y por
    #  tanto para esta prueba y para la regla de «ningun rotulo pintado debajo
    #  de un control» — y como ademas no estaba en esta lista, «toda ficha
    #  publica su titulo» era una afirmacion que nadie comprobaba en la unica
    #  ficha donde era falsa. Roto a proposito devolviendo su drawText:
    #  `tour no tiene titulo: se abre y no dice donde estas`.
    ("tour",   "AJUSTES"),
    #  Y EL MENU DE LA RANURA, que desde que tiene cabeceras de seccion pinta
    #  seis rotulos que NADIE miraba. `Tests/ranuras.py` mide la BANDA -donde
    #  cae, que este sobre su fila, que no quede ninguna al cerrar- y eso es
    #  maquetado: quitando la llamada a `pintaTitulo` y dejando las bandas
    #  puestas, ranuras.py seguia en verde y el menu salia sin una sola
    #  palabra. Medir el hueco no es medir el rotulo.
    ("ranura", "MEZCLA"),
    #  Y LA PAGINA DE CANALES DE LA MESA: la misma ficha con otro contenido no
    #  es el mismo estado —su fila de chips lleva dos tapas en vez de cinco y
    #  sus tiras pierden el pan y el solo—, y una pagina que el banco no abre es
    #  una pagina sin medir.
    ("mixc",   "MEZCLA"),
    ("vst",    "INSTRUMENTO"),
    ("inst",   "EXTRAS"),
    #  Y LA CARA CON LA SESION YA ESCRITA, que es el unico estado donde la banda
    #  de continuidad puede decir «A SALVO». Ver la regla de abajo.
    ("salvo",  "la cara"),
]


#  «A SALVO» en las cuatro lenguas, copiado de Lang.cpp. Es la unica forma de
#  preguntar por el campo en las cuatro compilaciones: el volcado da el rotulo
#  YA traducido, que es lo correcto -es lo que se ve- y obliga a esta tabla.
SALVO = { "es": "A SALVO", "en": "SAFE",
          "zh": "\u5df2\u4fdd\u5b58", "ar": "\u0645\u062d\u0641\u0648\u0638" }


def corre (sheet, size, lang):
    casa = tempfile.mkdtemp (prefix="zati-plano-")
    env = dict (os.environ, HOME=casa, ZATI_AUDIT="1", ZATI_SIZE=size,
                ZATI_LANG=lang, ZATI_OPEN=sheet,
                DISPLAY=PANTALLA)
    try:
        out = subprocess.run ([APP], env=env, capture_output=True, timeout=180).stdout.decode ("utf8", "replace")
    except subprocess.TimeoutExpired:
        return None, None
    finally:
        shutil.rmtree (casa, ignore_errors=True)

    rot, comp, mant, raiz = [], [], [], None
    for l in out.splitlines():
        l = l.strip()
        if not (l.startswith ("{") and l.endswith ("}")): continue
        try: d = json.loads (l)
        except Exception: continue
        if "root" in d:        raiz = d
        elif "mantener" in d:  mant.append (d)
        elif "rotulo" in d:    rot.append (d)
        elif "path" in d and d.get ("w", 0) > 0 and d.get ("h", 0) > 0:
            comp.append (d)
    return raiz, (rot, comp, mant)


def main():
    if not os.path.exists (APP): sys.exit ("no hay binario: compila primero")
    if not display_alive(): sys.exit ("la pantalla virtual no responde")

    #  UN SOLO VISOR DE ONDA EN TODO EL ARBOL.
    #
    #  Habia DOS y el bueno no lo usaba CORTAR: `WaveformDisplay` (818 lineas)
    #  con zoom x128, pellizco, arrastre de la vista y audicion al tocar, y
    #  `ChopPreview` (201) sin NINGUNA de las cuatro, dibujando solo el canal 0.
    #  Ajustar una marca en una muestra de 20 s en un movil era imposible. Una
    #  funcion, dos duenos — la misma figura que `assignSampleToPad` ya cerro.
    #
    #  Se deriva y no se declara: se busca QUIEN dibuja una envolvente de
    #  muestra, o sea quien declara el par `mins, maxs`, que es lo que un visor
    #  de onda tiene y ninguna otra cosa de esta app necesita. Preguntar por el
    #  NOMBRE del fichero no serviria: el segundo visor se llamaba ChopPreview y
    #  el tercero se llamara de otra forma.
    fuentes = sorted (glob.glob (os.path.join (ROOT, "Source", "*.h"))
                      + glob.glob (os.path.join (ROOT, "Source", "*.cpp")))
    visores = [os.path.basename (f) for f in fuentes
               if re.search (r"juce::Array\s*<\s*float\s*>\s*mins\s*,\s*maxs", 
                             open (f, encoding="utf8", errors="replace").read())]
    print ("visores de onda en el arbol: %s" % (", ".join (visores) or "(ninguno)"))
    if len (visores) != 1:
        print ("FALLA  %d componentes dibujan la envolvente de una muestra, y tiene "
               "que ser UNO" % len (visores))
        return 1

    #  Y COMPARTIR SOLO EXISTE CUANDO HAY ALGO QUE MANDAR.
    #
    #  Es la regla de la tira del paso, la que ya gobierna PEGAR y las cuatro de
    #  la seleccion del piano: *un control que no puede hacer nada no es
    #  informacion, es ruido*. La tapa manda el `content://` que `publicarExport`
    #  captura, asi que sin rebote publicado no tiene nada que mandar y apretarla
    #  no haria nada.
    #
    #  ESTO SE DERIVA Y NO SE MIDE ABRIENDO LA FICHA, y se dice por que: para
    #  ver la tapa PUESTA haria falta un rebote de verdad -un hilo, un WAV y una
    #  fila en el almacen de medios- dentro de una corrida del banco. Lo que si
    #  se puede medir sin eso es la unica forma en que la regla se rompe: que
    #  alguien escriba `setVisible (true)` a pelo. Se pide que TODA visibilidad
    #  de la tapa la decida `exportUri`, o sea `false` o la pregunta por la URI.
    #
    #  Lo que esto NO cubre, dicho: que el selector de Android se abra. Un
    #  `Intent` no existe en el escritorio y ninguna regla de este banco puede
    #  verlo. Es la excepcion declarada de esta tanda y se comprueba a mano.
    mc = open (os.path.join (ROOT, "Source", "MainComponent.cpp"),
               encoding="utf8", errors="replace").read()
    vis = re.findall (r"exportShareBtn\.setVisible\s*\(([^)]*)\)", mc)
    print ("exportShareBtn.setVisible: %s" % (", ".join (v.strip() for v in vis) or "(nunca)"))
    malas = [v.strip() for v in vis
             if v.strip() != "false" and "exportUri" not in v]
    if not vis:
        print ("FALLA  nadie decide si COMPARTIR se ve: la regla no mide nada")
        return 1
    if malas:
        print ("FALLA  %d veces se ensena COMPARTIR sin preguntar por exportUri: %s"
               % (len (malas), ", ".join (malas)))
        return 1

    size = os.environ.get ("ZATI_SIZE", "412x915")
    lang = os.environ.get ("ZATI_LANG", "es")
    filtro = [a.lower() for a in sys.argv[1:]]
    avisos = []

    for sheet, tapa in FICHAS:
        if filtro and sheet.lower() not in filtro: continue
        raiz, datos = corre (sheet, size, lang)
        if datos is None:
            avisos.append ("%s no contesto" % (sheet or "cara")); continue
        rot, comp, mant = datos

        nombre = sheet or "cara"
        print ("\n%s   %dx%d   se abre desde %s"
               % (nombre.upper(), raiz.get ("w", 0), raiz.get ("h", 0), tapa))

        #  Titulos y secciones, en orden de lectura.
        #
        #  EL TITULO DE LA FICHA ES EL DE SU CAPA, no el primero de la lista.
        #  Desde que la cabecera de la cara se apunta -«ZATI SAMPLER»- es un
        #  «titulo» mas, y ademas el primero, asi que CANCION y XY salian con
        #  «la tapa dice CANCION y el titulo ZATI SAMPLER»: dos hallazgos
        #  falsos, y de la clase peor, porque aparecieron el dia que esta
        #  prueba empezo a devolver codigo. Cada ficha lleva su numero de capa
        #  y todo lo que cuelga de ella lo hereda; la cara es la 0.
        #
        #  Y LA CAPA DE UNA FICHA NO PUEDE SER LA CERO, que es donde esta regla
        #  decia que si estando mal. `max(...)` se cae a 0 cuando la ficha no
        #  publico ningun rotulo -o sea justo en el caso que hay que cazar- y
        #  entonces coge el de la CARA, «ZATI SAMPLER», y lo da por su titulo:
        #  con el `drawText` del tour devuelto a proposito la prueba seguia
        #  saliendo verde. Una ficha sin rotulos propios no tiene titulo, y eso
        #  es exactamente lo que hay que decir.
        capa = max ((r.get ("capa", 0) for r in rot), default=0) if sheet else 0
        titulos = ([] if (sheet and capa == 0)
                   else [r for r in rot if r["tipo"] == "titulo" and r.get ("capa", 0) == capa])
        for r in sorted (rot, key=lambda r: (r["y"], r["x"])):
            print ("  %-8s %-28s %4d,%-4d %dx%d"
                   % (r["tipo"], r["rotulo"][:28], r["x"], r["y"], r["w"], r["h"]))

        #  EL MANUAL DICE CUANTOS CAPITULOS TIENE Y DIBUJA OTROS TANTOS.
        #
        #  Habia un `kManualChapters = 9` en el .h y una tabla de DIEZ filas, y
        #  manualContentHeight las recorria todas: SI ALGO NO SUENA estaba
        #  traducido a los cuatro idiomas, reservaba su alto en el
        #  desplazamiento y no se pintaba nunca. Ninguna de las ocho reglas de
        #  expo.py puede verlo -un capitulo que no se dibuja no es un
        #  componente-, y el subtitulo prometia OCHO, que no era ninguno de los
        #  dos numeros.
        #
        #  Se pregunta COMPARANDO y sin ninguna cifra escrita aqui: el numero
        #  que el subtitulo dice contra los titulos de capitulo que se pintaron
        #  de verdad. Asi la prueba no hay que tocarla el dia que entre un
        #  capitulo, que es justo lo que no paso con los siete sitios donde
        #  estaba escrito «ocho».
        if sheet == "manual":
            caps = [r for r in rot if r["tipo"] == "capitulo"]
            sub  = [r for r in rot if r["tipo"] == "subtitulo"]
            dice = None
            if sub:
                m = re.search (r"\d+", sub[0]["rotulo"])
                if m: dice = int (m.group (0))
            print ("  el subtitulo dice %s capitulos y se dibujan %d"
                   % (dice if dice is not None else "?", len (caps)))
            if dice is None:
                avisos.append ("manual: el subtitulo no dice cuantos capitulos hay")
            elif dice != len (caps):
                avisos.append ("manual: el subtitulo dice %d capitulos y se dibujan %d"
                               % (dice, len (caps)))

        #  CADA TAPA DE MANTENER DE LA CARA TIENE SU FILA EN GESTOS.
        #
        #  Un `HoldButton` hace dos cosas y solo una deja marca: la de mantener
        #  no se descubre tocando, asi que la unica forma de saber que existe es
        #  que AJUSTES · GESTOS la enumere. Esa lista se escribio a mano y se
        #  quedo vieja dos veces -MANTEN SOLO y MANTEN AUTO llevaban dos tandas
        #  existiendo sin fila- y ninguna de las once reglas de expo.py puede
        #  verlo: un gesto que no esta en una lista se maqueta perfecto.
        #
        #  Y LA LISTA NO SE ESCRIBE AQUI. La publica la app recorriendo los
        #  hijos de la cara (UiAudit::gestoDe), con el rotulo YA traducido: dos
        #  listas serian dos reglas y la de este script se quedaria vieja
        #  igual, que es exactamente el fallo que esta regla existe para cazar.
        #
        #  Se pide que alguna fila NOMBRE la tapa y no que exista una fila en
        #  su indice: quitando la de SOLO, las de abajo se corren y una
        #  comprobacion por indice seguiria saliendo en verde. La excepcion es
        #  declarada por la app -las ranuras de efecto comparten «MANTEN UN
        #  EFECTO», que no las nombra una por una-.
        if sheet == "gest":
            filas = sorted ({r["y"] for r in rot if r["tipo"] == "gesto"})
            texto = " ".join (r["rotulo"].upper() for r in rot if r["tipo"] == "gesto")
            print ("  %d filas de gesto, %d tapas de mantener en la cara"
                   % (len (filas), len (mant)))
            if not mant:
                avisos.append ("gest: la app no publica ni una tapa de mantener: no mide nada")
            for m in mant:
                #  `quien` y no `tapa`: la del bucle de arriba es la tapa que
                #  ABRE la ficha, y pisarla hacia que la comprobacion del
                #  titulo comparase «AJUSTES» contra el rotulo de la ultima
                #  tapa de mantener - «+», que es una ranura vacia.
                quien = m["mantener"].upper()
                if m.get ("familia", 0):
                    continue
                if quien not in texto:
                    avisos.append ("gest: MANTENER %s no tiene fila en GESTOS" % quien)

        #  LA BANDA DICE QUE TU TRABAJO ESTA A SALVO.
        #
        #  `SessionKeeper` escribe los pads cada dos segundos y el estado entero
        #  cada veinte, y la interfaz no lo contaba en ningun sitio: ni
        #  indicador, ni punto, ni estado sucio. El unico mensaje que existia
        #  salia al RECUPERAR, o sea cuando ya te habias llevado el susto.
        #
        #  Se pregunta por el ROTULO PINTADO -tipo «proyecto», el renglon de
        #  continuidad- y no por un componente: esta linea se dibuja con
        #  `drawText` a pelo, asi que ninguna de las 19 reglas de expo.py la ve.
        #
        #  Y se pregunta en `salvo` y NO en la cara: en la cara recien abierta
        #  el campo no puede salir -no se ha escrito nada todavia- y pedirlo
        #  alli seria una regla que falla siempre. Al reves tampoco vale: si se
        #  pidiera en todas, las 53 pantallas irian en rojo por decir la verdad.
        #  Una pantalla, un estado, una pregunta.
        #
        #  La palabra se pide TRADUCIDA a la lengua de la corrida, que es la
        #  leccion de las tres pestanas de AJUSTES: comparar contra el literal
        #  espanol da verde en es y rojo en las otras tres, o al reves.
        if sheet == "salvo":
            cont = [r for r in rot if r["tipo"] == "proyecto"]
            quiere = SALVO.get (lang, "A SALVO")
            dicen  = " ".join (r["rotulo"] for r in cont)
            print ("  la banda de continuidad dice: %s" % (dicen or "(nada)"))
            if not cont:
                avisos.append ("salvo: la banda de continuidad no se publica")
            elif quiere not in dicen:
                avisos.append ("salvo: la sesion esta escrita y la banda no dice «%s»"
                               % quiere)

        #  LAS SEIS SECCIONES DEL MENU DE EFECTOS, PINTADAS Y EN ORDEN.
        #
        #  `Tests/ranuras.py` ya mide las BANDAS: que haya seis, que cada una
        #  caiga sobre su fila y que no quede ninguna al cerrar el menu. Eso es
        #  maquetado. Roto a proposito -quitando la llamada a `pintaTitulo` y
        #  dejando las bandas puestas- ranuras.py seguia en VERDE y el menu se
        #  abria con seis huecos en blanco y treinta tapas sin agrupar: *medir
        #  el hueco no es medir el rotulo*. Aqui se pregunta por el rotulo.
        #
        #  Y EN ORDEN, que es la mitad que importa: seis nombres presentes en
        #  cualquier orden serian seis cabeceras puestas sobre la familia
        #  equivocada, que es peor que no ponerlas -un nombre mal puesto se cree-.
        #
        #  Los nombres se piden en ESPANOL porque `plano.py` corre en los cuatro
        #  idiomas y esta lista es la clave de `T()`, no su traduccion; la
        #  comparacion se hace solo en la corrida espanola por lo mismo que
        #  `salvo` pide la palabra traducida: una pantalla, un estado, una
        #  pregunta.
        if sheet == "ranura" and lang == "es":
            QUIERE = ["FILTRO", "SATURACION", "MODULACION", "ESPACIO", "DINAMICA", "TIEMPO"]
            secc = [r["rotulo"] for r in rot if r["tipo"] == "seccion"]
            print ("  las secciones del menu: %s" % (", ".join (secc) or "(ninguna)"))
            if secc != QUIERE:
                avisos.append ("ranura: las secciones pintadas son %s y tenian que ser %s"
                               % (secc or "(ninguna)", QUIERE))

        #  LA PREGUNTA QUE ESTO EXISTE PARA CONTESTAR.
        #
        #  Se pregunta por FICHAS abiertas y no por «tiene clave»: `salvo` es un
        #  ESTADO de la cara -la sesion ya escrita- y no una ficha, asi que no
        #  tiene titulo propio y no puede tenerlo. Quien lo dice es la tabla de
        #  arriba, que ya declara de donde se abre cada una: «la cara» es la
        #  respuesta para las dos. Lo anadi con clave y salio «salvo no tiene
        #  titulo: se abre y no dice donde estas», que es cierto por la letra y
        #  falso por lo que se quiere saber.
        if sheet and tapa == "la cara":
            pass
        elif sheet and not titulos:
            avisos.append ("%s no tiene titulo: se abre y no dice donde estas" % nombre)
        elif sheet and tapa not in ("la cara",):
            t0 = titulos[0]["rotulo"].split ("  ")[0].strip() if titulos else ""
            if t0 and tapa not in ("AJUSTES", "SEC", "PADS", "MEZCLA", "CARGAR") and t0 != tapa:
                avisos.append ("%s: la tapa dice \"%s\" y el titulo \"%s\"" % (nombre, tapa, t0))

        print ("  %d controles" % len (comp))

    print()
    #  Y DEVUELVE CODIGO. Esto imprimia AVISO y salia con cero pasara lo que
    #  pasara, que es lo mismo que le pasaba a expo.py, session.py y apk.py: el
    #  veredicto lo daba un ojo humano leyendo texto. Por eso «sec no tiene
    #  titulo» y «browse no tiene titulo» duraron tandas enteras.
    #
    #  Y la ultima linea estaba mal escrita ademas: `"%d..." % 0 if avisos else
    #  "..."` se agrupa como `("%d..." % 0) if avisos else "..."`, asi que con
    #  avisos imprimia «0 fichas sin aviso» — el mensaje de que todo esta bien
    #  justo cuando algo no lo esta.
    for a in avisos: print ("FALLA ", a)
    if avisos:
        return 1
    print ("el plano no encuentra desajustes")
    return 0


if __name__ == "__main__":
    sys.exit (main())

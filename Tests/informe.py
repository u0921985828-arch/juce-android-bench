#!/usr/bin/env python3
"""EL INFORME: el plano, lo que hay dentro, lo que falta y por donde tocarlo.

Junta lo que ya existe -el plano de `planos.py`, el inventario de
`desglose.py`, la lista curada de `duplicados.md`- en un documento que se abre
en Chrome sin nada al lado, y le anade lo unico que no se puede generar: las
OPCIONES de disposicion, que son una decision y no una medida.

Tres por pantalla y no una, porque no hay una respuesta correcta:
conservadora (mover lo minimo), agresiva (aplicar «una funcion, un dueno» y
decir que se pierde) y de cero (repensar la pantalla para el gesto que sirve).
Cada una con su coste en pixeles de ALTO, que en esta app es lo unico que
escasea.

    python3 Tests/informe.py      # necesita planos/ y desglose/ ya generados
"""
import csv, os, sys, collections

ROOT = os.path.dirname (os.path.dirname (os.path.abspath (__file__)))
sys.path.insert (0, os.path.join (ROOT, "Tests"))
from planos import NOMBRES
from expo import SHEETS

PLANOS = os.path.join (ROOT, "planos")
SALIDA = os.path.join (ROOT, "desglose")

#  LAS OPCIONES. Solo para las pantallas con un hallazgo: proponer tres formas
#  de recolocar una pantalla que esta bien es ruido, y ruido en un documento
#  que se lee para decidir es lo que hace que no se lea.
#
#  El coste en alto sale de la cuenta de la casa: una fila de tapas es
#  Metrics::btn (44) mas su aire (4), o sea 48; una fila de pestanas es
#  Metrics::tab (44). Lo que se gana o se pierde va contra el alto de la
#  tarjeta, que es de donde salen las celdas de las rejillas.
PROPUESTAS = {
 "pads": [
  ("conservadora", "+0 px",
   "El PLAY de la cara esta debajo y tocarlo cierra la ficha. Poner OIR "
   "-que ya existe en esta ficha- a hacer las dos cosas no vale: OIR es el pad "
   "y PLAY es el transporte. Se deja como esta y se acepta el viaje."),
  ("agresiva", "-48 px de la fila de recorte",
   "Meter PLAY en la fila del titulo, al lado de la cruz, que es donde ya vive "
   "el transporte en SEC. Cuesta ancho en la cabecera y no alto."),
  ("de cero", "+48 px",
   "Una fila de transporte propia y comun a las tres paginas del pad, con "
   "PLAY, el modo y OIR. Es lo que ya se hizo en SEC y funciono; aqui cuesta "
   "una fila que la pagina RECORTE no tiene."),
 ],
 "pad2": [
  ("conservadora", "+0 px", "Igual que EL PAD · SONIDO: comparten cabecera."),
  ("agresiva", "+0 px",
   "REV y QUITAR RUIDO no son del recorte sino del sonido: bajarlos a la "
   "pagina SONIDO deja sitio para el transporte sin pedir alto."),
  ("de cero", "+48 px",
   "La fila de transporte comun de la propuesta de arriba, compartida por las "
   "tres paginas."),
 ],
 "paso": [
  ("conservadora", "+0 px",
   "PATRON es la pagina de lo que le pasa al patron entero, no de escribirlo: "
   "se puede argumentar que no necesita PLAY."),
  ("agresiva", "+0 px",
   "El transporte de SEC ya esta en la pagina PASOS. Ponerlo en la fila de "
   "pestanas -PASOS · PIANO · PATRON- lo deja a mano en las tres sin coste."),
  ("de cero", "-48 px",
   "EUCLIDES y la CADENA son del patron y ya se caen en 280 px. Si la pagina "
   "se parte en dos -herramientas / cadena- entra el transporte y ademas deja "
   "de caerse nada."),
 ],
 "vst": [
  ("conservadora", "+0 px",
   "El teclado de la ficha ya suena al tocarlo, asi que oir el preset no pide "
   "el transporte. Pero escribir con el piano y oirlo en contexto, si."),
  ("agresiva", "+0 px",
   "PLAY en la fila de la cabecera, al lado de la cruz."),
  ("de cero", "+44 px",
   "Esta ficha tiene forma de aparato (tres paneles). Un cuarto panel de "
   "transporte debajo del teclado seria coherente y cuesta una fila."),
 ],
 "song": [
  ("conservadora", "+0 px",
   "Dejar los dos CANCION y confiar en que el sitio los distinga. Es lo que "
   "hay hoy y es lo que motivo esta revision."),
  ("agresiva", "+0 px",
   "La tapa de modo dice el ESTADO y la de la cara dice el DESTINO. Cambiar "
   "la clave de `songButton` a algo que no colisione en espanol -«ARREGLO», "
   "«LINEA»- separa las dos sin tocar el maquetado ni el motor."),
  ("de cero", "-48 px",
   "El modo no es de la ficha CANCION: es de lo que toca PLAY, y ya esta en "
   "las cuatro filas de transporte. Quitarlo de dentro de la ficha deja una "
   "fila entera para la linea de tiempo, que es lo unico para lo que existe "
   "esa pagina."),
 ],
 "mix": [
  ("conservadora", "+0 px",
   "72 controles en 20 filas. Es una mesa y una mesa es eso."),
  ("agresiva", "+0 px",
   "M y S por canal son 32 tapas de las 72. Un solo toque largo sobre el "
   "fader podria hacer el mute, como el toque largo del pad abre su ficha: "
   "el gesto ya existe en esta app."),
  ("de cero", "+0 px",
   "Cuatro canales a la vez con paginas A-B-C-D, como los bancos de pads. "
   "Cada canal pasa de una tira estrecha a una columna que se puede tocar."),
 ],
 "manual": [
  ("conservadora", "+0 px", "La cruz se escribe con «x» y en las otras 26 fichas con «×»."),
  ("agresiva", "+0 px", "Un solo caracter para las 28."),
  ("de cero", "+0 px", "La cruz sale de la cabecera comun, como el titulo."),
 ],
 "rack": [
  ("conservadora", "+0 px", "Igual que MANUAL: la cruz es «x» y no «×»."),
  ("agresiva", "+0 px", "Un solo caracter."),
  ("de cero", "+0 px", "Cabecera comun."),
 ],
}


def esc (t):
    return (str (t).replace ("&", "&amp;").replace ("<", "&lt;")
                   .replace (">", "&gt;").replace ('"', "&quot;"))


def lee_csv (tam):
    f = os.path.join (SALIDA, "controles-%s.csv" % tam)
    if not os.path.exists (f):
        return {}
    por = collections.defaultdict (list)
    with open (f, encoding="utf8") as fh:
        for r in csv.DictReader (fh):
            por[r["pantalla"]].append (r)
    return por


CSS = """<meta charset="utf-8">
<title>Desglose de ZATI</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Oswald:wght@400;500;600&family=Barlow:wght@400;500&family=JetBrains+Mono:wght@400;500&display=swap">
<style>
:root{--papel:#eceae4;--tarjeta:#f6f5f1;--filo:#d3d0c7;--tinta:#1b2224;--apagado:#5f6b6d;
 --azul:#1c6a85;--ambar:#8a5f05;--rojo:#a8352f;--verde:#1f6b4a;--rej:rgba(28,106,133,.10)}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){
 --papel:#0b1113;--tarjeta:#121a1d;--filo:#222f34;--tinta:#dde7e8;--apagado:#8b9fa2;
 --azul:#5fb4d1;--ambar:#f0b429;--rojo:#e07070;--verde:#7fd4a8;--rej:rgba(95,180,209,.07)}}
:root[data-theme="dark"]{--papel:#0b1113;--tarjeta:#121a1d;--filo:#222f34;--tinta:#dde7e8;
 --apagado:#8b9fa2;--azul:#5fb4d1;--ambar:#f0b429;--rojo:#e07070;--verde:#7fd4a8;--rej:rgba(95,180,209,.07)}
*{box-sizing:border-box}
body{margin:0;background:var(--papel);color:var(--tinta);font-family:"Barlow",system-ui,sans-serif;
 font-size:15px;line-height:1.55;
 background-image:linear-gradient(var(--rej) 1px,transparent 1px),linear-gradient(90deg,var(--rej) 1px,transparent 1px);
 background-size:32px 32px}
.hoja{display:grid;grid-template-columns:226px minmax(0,1fr);gap:34px;max-width:1340px;margin:0 auto;padding:40px 26px 90px}
@media (max-width:920px){.hoja{grid-template-columns:minmax(0,1fr);gap:18px;padding:22px 14px 60px}}
.portada{grid-column:1/-1;border-top:2px solid var(--tinta);padding-top:14px;display:flex;
 flex-wrap:wrap;gap:18px 40px;align-items:flex-end;justify-content:space-between}
h1{font-family:"Oswald",sans-serif;font-weight:600;font-size:clamp(28px,5vw,44px);margin:0;
 text-transform:uppercase;letter-spacing:.02em;text-wrap:balance}
.sub{margin:6px 0 0;color:var(--apagado);max-width:66ch}
.sello{font-family:"JetBrains Mono",monospace;font-size:12px;color:var(--apagado);text-align:right;line-height:1.7}
.sello b{color:var(--tinta);font-weight:500}
.avisos{grid-column:1/-1;display:grid;gap:10px;grid-template-columns:repeat(auto-fit,minmax(280px,1fr))}
.aviso{border:1px solid var(--filo);border-left:3px solid var(--rojo);background:var(--tarjeta);
 border-radius:3px;padding:12px 14px}
.aviso.ok{border-left-color:var(--verde)}
.aviso h3{margin:0 0 4px;font-family:"Oswald",sans-serif;font-size:14px;letter-spacing:.05em;text-transform:uppercase}
.aviso p{margin:0;font-size:13.5px;color:var(--apagado)}
nav{align-self:start;position:sticky;top:22px}
@media (max-width:920px){nav{position:static}}
nav .rot{font-family:"JetBrains Mono",monospace;font-size:11px;letter-spacing:.14em;text-transform:uppercase;
 color:var(--apagado);margin:0 0 8px}
nav ol{list-style:none;margin:0;padding:0;border-left:1px solid var(--filo)}
nav a{display:flex;justify-content:space-between;gap:8px;padding:5px 10px;color:var(--apagado);
 text-decoration:none;font-size:13.5px;border-left:2px solid transparent;margin-left:-1px}
nav a:hover,nav a:focus-visible{color:var(--tinta);border-left-color:var(--azul);background:var(--tarjeta);outline:none}
nav em{font-family:"JetBrains Mono",monospace;font-style:normal;font-size:11px;opacity:.7}
nav a.mal em{color:var(--rojo);opacity:1}
main{display:flex;flex-direction:column;gap:42px;min-width:0}
section{min-width:0;scroll-margin-top:18px}
.cab{display:flex;flex-wrap:wrap;gap:6px 18px;align-items:flex-end;justify-content:space-between;
 border-bottom:1px solid var(--filo);padding-bottom:8px;margin-bottom:14px}
h2{font-family:"Oswald",sans-serif;font-weight:600;text-transform:uppercase;letter-spacing:.05em;
 font-size:20px;margin:0}
.ruta{margin:2px 0 0;font-size:13px;color:var(--apagado)}
.chips{display:flex;gap:6px;flex-wrap:wrap}
.chip{font-family:"JetBrains Mono",monospace;font-size:11px;padding:3px 9px;border-radius:2px;
 border:1px solid var(--filo);color:var(--apagado);white-space:nowrap}
.chip.mal{border-color:var(--rojo);color:var(--rojo)}
.cuerpo{display:grid;grid-template-columns:minmax(0,auto) minmax(280px,1fr);gap:22px;align-items:start}
@media (max-width:920px){.cuerpo{grid-template-columns:minmax(0,1fr)}}
.lienzo{background:#0e1517;border:1px solid var(--filo);border-radius:3px;padding:5px;overflow-x:auto}
svg{display:block;max-width:100%;height:auto}
table{border-collapse:collapse;width:100%;font-size:12.5px;font-variant-numeric:tabular-nums}
th{text-align:left;font-family:"JetBrains Mono",monospace;font-size:10.5px;letter-spacing:.08em;
 text-transform:uppercase;color:var(--apagado);border-bottom:1px solid var(--filo);padding:5px 7px 5px 0;font-weight:500}
td{padding:3px 7px 3px 0;border-bottom:1px solid var(--filo)}
td.n{font-family:"JetBrains Mono",monospace;color:var(--apagado)}
tr.fin td{border-bottom:2px solid var(--filo)}
td.dedo{color:var(--rojo)}
.op{margin-top:14px;display:grid;gap:8px;grid-template-columns:repeat(auto-fit,minmax(250px,1fr))}
.op article{border:1px solid var(--filo);background:var(--tarjeta);border-radius:3px;padding:11px 13px}
.op h4{margin:0 0 3px;font-family:"Oswald",sans-serif;font-size:13px;letter-spacing:.06em;text-transform:uppercase}
.op .coste{font-family:"JetBrains Mono",monospace;font-size:11px;color:var(--ambar)}
.op p{margin:5px 0 0;font-size:13px;color:var(--apagado)}
.tam{display:flex;gap:14px;font-family:"JetBrains Mono",monospace;font-size:11.5px;color:var(--apagado);margin-top:8px}
/*  Las cotas viven dentro de cada SVG y se apagan desde AQUI, que es lo unico
    que permite verlas en las treinta y dos a la vez o en ninguna: comparar dos
    pantallas con una acotada y la otra no es como no acotar.               */
.cotas{display:none}
#vercotas:checked ~ .hoja .cotas{display:inline}
#vercotas{position:absolute;opacity:0;width:0;height:0}
.interruptor{grid-column:1/-1;display:flex;gap:14px;align-items:center;flex-wrap:wrap;
 font-family:"JetBrains Mono",monospace;font-size:11.5px;color:var(--apagado)}
.interruptor label{cursor:pointer;border:1px solid var(--filo);background:var(--tarjeta);
 border-radius:3px;padding:7px 13px;letter-spacing:.06em;color:var(--tinta);user-select:none}
#vercotas:checked ~ .hoja .interruptor label{border-color:var(--azul);color:var(--azul)}
a:focus-visible{outline:2px solid var(--azul);outline-offset:2px}
</style>
<input type="checkbox" id="vercotas">"""


def main():
    tams = ["412x915", "280x653", "915x412"]
    datos = {t: lee_csv (t) for t in tams}
    base = datos[tams[0]]
    if not base:
        sys.exit ("faltan los CSV: corre antes Tests/desglose.py")

    doc = [CSS, '<div class="hoja">',
      '<header class="portada"><div><h1>Que tiene cada pantalla</h1>'
      '<p class="sub">Las treinta y dos pantallas y pop-ups de ZATI, con sus controles '
      'contados del volcado que la app imprime de si misma. Para cada una: el plano a 1:1, '
      'la lista de lo que hay agrupada por fila, lo que le falta y tres formas de recolocarla. '
      'Los controles salen medidos; las opciones son una decision.</p></div>'
      '<p class="sello">ARTiFACTS &middot; ZATI<br><b>%d</b> controles &middot; <b>32</b> pantallas'
      '<br>412&times;915, 280&times;653, 915&times;412</p></header>'
      % sum (len (v) for v in base.values()),
      '<div class="interruptor"><label for="vercotas">COTAS &middot; medidas, regla de 8 px y lo que no llega al dedo</label>'
      '<span>40 px es Metrics::hit &middot; la regla marca gordo cada 40 y fino cada 8</span></div>']

    #  Lo que hay que saber antes de leer treinta y dos secciones.
    doc.append ('<div class="avisos">'
      '<div class="aviso"><h3>Cuatro paginas escriben sonido y no tienen PLAY a mano</h3>'
      '<p>EL PAD &middot; SONIDO, EL PAD &middot; RECORTE, SEC &middot; PATRON y EL INSTRUMENTO. '
      'El PLAY de la cara queda debajo de la ficha y tocar ahi la CIERRA, asi que oir lo que '
      'acabas de escribir cuesta cerrar y volver a abrir.</p></div>'
      '<div class="aviso"><h3>CANCION son dos tapas con el mismo dibujo</h3>'
      '<p>La que abre la ficha y la que cambia lo que toca PLAY dicen las dos CANCION en espanol '
      'y llevan las dos <code>Iconos::Id::cancion</code>. En ingles se separan: SONG y SONG MODE.</p></div>'
      '<div class="aviso"><h3>La cruz de cerrar se escribe de dos formas</h3>'
      '<p>&laquo;&times;&raquo; en 26 fichas y &laquo;x&raquo; en MANUAL y RACK. El mismo control '
      'con dos caracteres distintos.</p></div>'
      '<div class="aviso ok"><h3>El piano roll YA tiene PLAY y el cambio de modo</h3>'
      '<p>Medido en el codigo de hoy: la barra del piano lleva PLAY, PATRON/CANCION y la cruz. '
      'Si en tu telefono no estan, la APK instalada es anterior.</p></div>'
      '</div>')

    nav = ['<nav aria-label="pantallas"><p class="rot">32 pantallas</p><ol>']
    cuerpo = ['<main>']

    for k in SHEETS:
        clave = k or "cara"
        filas = base.get (clave, [])
        if not filas:
            continue
        nombre, comose = NOMBRES.get (k, (clave.upper(), ""))
        bajos = [f for f in filas if f["bajo_el_dedo"] == "1"]
        nfilas = len ({f["fila"] for f in filas})
        mal = bool (bajos) or clave in PROPUESTAS
        nav.append ('<li><a class="%s" href="#s-%s"><span>%s</span><em>%s</em></a></li>'
                    % ("mal" if mal else "", clave, esc (nombre),
                       ("%d!" % len (bajos)) if bajos else str (len (filas))))

        svg = ""
        p = os.path.join (PLANOS, "%s.svg" % clave)
        if os.path.exists (p):
            with open (p, encoding="utf8") as fh:
                svg = fh.read()

        chips = ['<span class="chip">%d controles</span>' % len (filas),
                 '<span class="chip">%d filas</span>' % nfilas]
        if bajos:
            chips.append ('<span class="chip mal">%d bajo el dedo</span>' % len (bajos))
        for t in tams[1:]:
            n = len (datos[t].get (clave, []))
            chips.append ('<span class="chip">%s: %d</span>' % (t, n))

        tab = ['<table><thead><tr><th>fila</th><th>rotulo</th><th>en ingles</th>'
               '<th>dibujo</th><th>tipo</th><th>tam</th></tr></thead><tbody>']
        ultima = None
        for f in filas:
            fin = " class=\"fin\"" if (ultima is not None and f["fila"] != ultima) else ""
            ultima = f["fila"]
            dedo = ' class="n dedo"' if f["bajo_el_dedo"] == "1" else ' class="n"'
            tab.append ('<tr%s><td class="n">%s</td><td>%s</td><td class="n">%s</td>'
                        '<td class="n">%s</td><td class="n">%s</td><td%s>%s&times;%s</td></tr>'
                        % (fin, f["fila"], esc (f["rotulo_es"]) or "&mdash;",
                           esc (f["rotulo_en"]) or "", esc (f["icono"]) or "&mdash;",
                           esc (f["tipo"]), dedo, f["w"], f["h"]))
        tab.append ('</tbody></table>')

        ops = ""
        if clave in PROPUESTAS:
            ops = ['<div class="op">']
            for titulo, coste, texto in PROPUESTAS[clave]:
                ops.append ('<article><h4>%s</h4><span class="coste">%s</span>'
                            '<p>%s</p></article>' % (esc (titulo), esc (coste), esc (texto)))
            ops.append ('</div>')
            ops = "".join (ops)

        cuerpo.append (
          '<section id="s-%s"><header class="cab"><div><h2>%s</h2><p class="ruta">%s</p></div>'
          '<div class="chips">%s</div></header>'
          '<div class="cuerpo"><div class="lienzo">%s</div><div>%s</div></div>%s</section>'
          % (clave, esc (nombre), esc (comose), "".join (chips), svg, "".join (tab), ops))

    nav.append ('</ol></nav>')
    cuerpo.append ('</main>')
    doc += nav + cuerpo + ['</div>']

    salida = os.path.join (SALIDA, "informe.html")
    with open (salida, "w", encoding="utf8") as fh:
        fh.write ("\n".join (doc))
    print ("%s  (%.1f MB)" % (os.path.relpath (salida, ROOT),
                              os.path.getsize (salida) / 1048576.0))
    return 0


if __name__ == "__main__":
    sys.exit (main())

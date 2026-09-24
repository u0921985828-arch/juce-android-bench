#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include "Zati.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <typeinfo>
#include <vector>

// ============================================================================
//  UiAudit — the interface measuring itself.
//
//  Everything an interface gets wrong at a size or in a language it was not
//  drawn in is measurable: a cap narrower than a finger, two controls that
//  overlap, a caption wider than the box it was put in. Judging those by eye
//  on a screenshot means one language and one screen at a time, and the two
//  that matter here - Chinese, which is wide per glyph, and Arabic, which is
//  right to left - are exactly the two nobody checks.
//
//  So the app measures itself instead. With ZATI_AUDIT set it lays out, walks
//  its own component tree, prints one JSON line per component and quits.
//  Bounds are in window coordinates, so overlaps and gaps between different
//  parents come out of the same arithmetic.
//
//  The line that earns this file: for anything that draws a caption we print
//  BOTH the pixels the text needs at its real font AND the pixels its box
//  gives it. Truncation stops being something you notice in a screenshot and
//  becomes a number that is negative.
//
//      ZATI_AUDIT=1              dump and quit
//      ZATI_SIZE=412x915         lay out at this size first
//      ZATI_LANG=es|en|zh|ar     in this language
//      ZATI_OPEN=pads|sec|paso|...  with this sheet open
//      ZATI_CYCLE=3              suspend and resume this many times first
//      ZATI_DEMO=1               con doce pads cargados y un patron escrito
//      ZATI_SHOT=x.png           saca una foto en vez de un volcado
//      ZATI_SHOT_SCALE=2.62      a esta escala
//      ZATI_PAINT=60             cuanto cuesta un fotograma, por piezas
//      ZATI_SPIN=12              CPU del proceso con la cara abierta y quieta
//      ZATI_SONANDO=1            y con la maquina SONANDO: sin aparato de
//                                sonido el motor no renderiza y el cristal se
//                                queda en su guardia de silencio, o sea que la
//                                pieza mas grande de la cara no se repinta
//                                nunca en el banco
//      ZATI_AUDIO=1              la decision del carril rapido sobre tablas
//                                sinteticas: la sonda de verdad habla con
//                                libaaudio y no corre aqui, pero lo que se
//                                DECIDE con su respuesta si (ver audio.py)
//      ZATI_KIT=nombre           guarda el banco de delante como kit y lo vuelca
//      ZATI_INSETS=t,l,b,r       margenes del sistema simulados (ver abajo)
//      ZATI_INSETS_TICK=n        y a partir de que tick contestan
//      ZATI_ARRANQUE=n           n ticks con la cara abierta, una linea por tick
//
//  Las tres ultimas son de MainComponent y viven APARTE de ZATI_AUDIT a
//  proposito. En el escritorio SystemInsets::get() devuelve {} siempre, o sea
//  que el salto de maquetado del arranque -la cara colocandose sola cuando
//  Android contesta por fin a los margenes- NO EXISTE aqui y no habria forma de
//  medirlo: ZATI_INSETS lo convierte en una ENTRADA, que es lo mismo que hace
//  ZATI_SKIN con la carcasa y ZATI_DLC con los packs. Y ZATI_ARRANQUE no puede
//  ir con ZATI_AUDIT porque alli no hay portada -las 924 corridas miden la cara
//  y las fotos de ZATI_SHOT saldrian con el chasis vacio-.
// ============================================================================
namespace UiAudit
{
    inline bool enabled()  { return juce::SystemStats::getEnvironmentVariable ("ZATI_AUDIT", {}).isNotEmpty(); }

    inline juce::String env (const char* k) { return juce::SystemStats::getEnvironmentVariable (k, {}); }

    //  CUANTAS VECES SE HA PINTADO EL FONDO DE LA CARA.
    //
    //  El chasis solo se dibuja cuando hay que repintar la ventana entera, asi
    //  que contarlo cuenta fotogramas completos - que es la pregunta que
    //  importa en un telefono. Una ficha translucida que pide repaint() de si
    //  misma suma uno aqui aunque el fondo no haya cambiado, y ese es
    //  exactamente el desperdicio que se buscaba.
    inline int fondosPintados = 0;
    //  Y LOS PIXELES, que es lo que de verdad cuesta.
    //
    //  Contar LLAMADAS no separa un fotograma completo de una banda de treinta
    //  pixeles, y esa diferencia es de 5.2 ms a 0.29 medidos: el cabezal de la
    //  rejilla de pasos entra aqui treinta veces por segundo y esta BIEN,
    //  porque pide su banda. Con la maquina sonando las dos cosas pasan a la
    //  vez y la cuenta de llamadas deja de decir nada.
    inline long long pixelesPintados = 0;
    //  Y CUANTOS CUADROS SE HAN PEDIDO, que desde que el dibujo cuelga del
    //  vblank es la mitad que faltaba: los pixeles TOTALES suben por
    //  definicion al subir la tasa -a 120 Hz se pintan cuatro veces mas
    //  fotogramas que a 30- asi que el numero de este banco dejaria de ser
    //  comparable con el de la tanda anterior sin decirlo. Lo que no puede
    //  empeorar es el coste POR CUADRO.
    inline long long cuadrosPintados = 0;
    //  Y COMO DE REGULARES SON, que es una pregunta DISTINTA de cuantos hay.
    //
    //  «Cincuenta y tres cuadros por segundo» y «va a tirones» son compatibles:
    //  una app que pinta 100, 50, 100, 50 da la misma media que una que pinta 75
    //  siempre, y solo la primera se ve dar saltos. La media no puede verlo por
    //  construccion -es lo que una media hace- asi que lo que se cuenta es la
    //  VARIACION, y en tres numeros que no se pueden falsear promediando:
    //
    //   · `huecoCubos`  — el reparto de los huecos entre cuadros PINTADOS, en
    //     cubos de 4 ms. Una cadencia sana es una sola columna; una a tirones
    //     son dos columnas separadas, que es exactamente el dibujo de alternar
    //     entre pintar y saltarse uno.
    //   · `cadenciaCambios` — cuantas veces la app ha CAMBIADO de cadencia, o
    //     sea cuantas veces `cuadroSaltar` paso a otro nivel. Este es EL numero
    //     del tiron: cada cambio es un hueco que dura el doble o la mitad que el
    //     anterior, y el ojo los ve uno a uno.
    //   · `vblanksVistos` / `cuadrosSaltados` — el denominador, sin el cual «500
    //     cuadros» no dice si la app llego o se rindio.
    inline long long vblanksVistos   = 0;
    inline long long cuadrosSaltados = 0;
    inline long long cadenciaCambios = 0;
    inline double    huecoPeorMs     = 0.0;
    inline std::array<int, 16> huecoCubos {};   // [i] = huecos de [4i, 4i+4) ms
    inline void apuntaHueco (double ms)
    {
        huecoPeorMs = std::max (huecoPeorMs, ms);
        const int c = juce::jlimit (0, 15, (int) (ms / 4.0));
        ++huecoCubos[(size_t) c];
    }
    //  CUANTAS VECES SE HA MOVIDO EL CABEZAL DEL PIANO ROLL. La barra estaba
    //  dibujada desde el primer dia y no estaba viva: el temporizador solo
    //  alimentaba la rejilla de PASOS, asi que en la pagina del piano el
    //  cabezal se quedaba donde estuviera al entrar. Eso no se ve en un
    //  volcado de geometria -el componente esta ahi y mide lo mismo- y se
    //  cuenta aqui, que es la unica forma de que un cero se distinga de un
    //  "no lo he mirado".
    //  EL OBJETIVO DEL PASO DEL TOUR, para que "no señala nada" sea un HALLAZGO
    //  y no una captura de pantalla que alguien mande dos meses despues.
    //
    //  Dos pasos senalaban el vacio y nadie lo vio: el de la tira del paso
    //  apuntaba a `stepStripArea`, que quedo valiendo {} cuando la tira se fue
    //  de la cara, y el ultimo -el que explica el idioma y las carcasas- caia
    //  en el `default` de la tabla porque los casos llegaban al 13 y los pasos
    //  son quince. Los dos abrian su ficha, la oscurecian entera y no ponian ni
    //  agujero ni anillo.
    //
    //  Se vuelca el rectangulo y no un si/no: un objetivo de 3x3 px tampoco
    //  señala nada, y con el numero delante se ve venir.
    //  LOS ROTULOS QUE SE PINTAN, que hasta ahora eran invisibles para el banco.
    //
    //  Las seis reglas recorren el arbol de COMPONENTES, y el titulo de una
    //  ficha, el nombre de una seccion y las palabras grabadas de la cara no
    //  son componentes: se dibujan. Por eso la mesa llevaba desde el primer dia
    //  titulada "MIX" a mano -sin pasar por T(), con la fila MIX->MEZCLA en la
    //  tabla y la pestana que la abre usandola- y no lo caz nadie.
    //
    //  Se apuntan aqui con su rectangulo y su TIPO, que es lo que permite
    //  levantar el plano de una pantalla: titulo, seccion, subseccion. Sin el
    //  tipo serian una lista de palabras sueltas y el orden no se podria juzgar.
    //  Y LO QUE EL TEXTO PIDE, ademas de lo que ocupa.
    //
    //  `w` es lo que el rotulo OCUPA -acotado a la banda que le dieron- asi que
    //  un texto que no cabe sale con el mismo `w` que uno que cabe justo: la
    //  pregunta «¿se lee entero?» no se podia hacer sobre un rotulo PINTADO.
    //  Para los rotulos que son componentes eso lo miden TRUNC y SQUEEZE desde
    //  hace tandas; los pintados -que son media pantalla en esta app- no los
    //  miraba nadie.
    //
    //  Y SOLO EL ANCHO, que es lo que la medida decidio.
    //
    //  Hubo un segundo campo -el ALTO de la letra contra el de la banda, «una
    //  banda mas baja que su texto lo recorta»- y saco 1456 casos con UN SOLO
    //  rotulo distinto, que es la firma de un liston mal puesto. Se probaron
    //  las dos formas baratas de preguntarlo y las dos dan lo mismo:
    //  `Font::getHeight()` es ascendente mas descendente -el hueco que una
    //  linea RESERVA- y `GlyphArrangement::getBoundingBox` devuelve la caja de
    //  LINEA por glifo, no la tinta. Medido donde no se puede discutir, con los
    //  pixeles de una captura: «ZATI SAMPLER» declaraba 26.0 en una banda de 24
    //  y su tinta va de y=11 a y=25, o sea **15 px con cinco de aire por
    //  arriba y cinco por abajo**. No habia nada que arreglar.
    //
    //  Medir la tinta de verdad seria rasterizar cada rotulo, y son cincuenta
    //  por corrida en 1456 corridas. Un campo que nadie juzga es ruido, asi que
    //  se retira y queda escrito para no volver a ponerlo.
    //  `pide` es el ancho MINIMO al que ese texto se sigue leyendo entero, ya
    //  con el apreton aplicado, y CERO significa «no se juzga»: un rotulo que
    //  se elide a proposito -el nombre del proyecto- se corta con puntos
    //  suspensivos y eso se lee como un nombre largo, no como un fallo. Lo dice
    //  quien lo dibuja y no una lista de textos en el script, que solo sabria
    //  medir una de las cuatro compilaciones -es el argumento de la marca
    //  `valor` de los iconos-.
    //  Y EL CUERPO DE LETRA CON EL QUE SE DIBUJO, que es lo que hacia falta
    //  para poder CENSAR cuantos hay. `SPRITE` existe porque habia SIETE lados
    //  de icono en la app y hoy hay uno; el mismo argumento -«dos filas de la
    //  misma ficha dibujando el mismo trazo a dos tamanos»- vale igual para la
    //  LETRA, y aqui no lo mide nadie: cinco tamanos con nombre (`fTiny`
    //  .. `fValue`) mas CATORCE sitios con un literal o una cuenta al vuelo.
    //  Se apunta antes de escribir ninguna regla: un liston inventado antes de
    //  censar es exactamente lo que esta casa no hace.
    //
    //  Y LA CLAVE SE LLAMA `cuerpoLetra` Y NO `cuerpo`, que fue el primer
    //  nombre y duro una tarde: el gancho de INSTRUMENTOS ya publica
    //  `"cuerpo"` para el ALTO del cuerpo de la ficha -736 px-, asi que un
    //  censo que filtre «tiene la clave cuerpo» mezcla las dos lineas y saca
    //  un cuerpo de letra de 736. El espacio de claves del volcado es PLANO,
    //  que es lo que ya costo `on`, `lit` y `fila` en la tanda de los chips.
    struct Rotulo { int x, y, w, h; juce::String texto, tipo; int capa; int pide;
                    float cuerpoLetra; int lineas; };
    inline std::vector<Rotulo> rotulos;

    //  EN QUE CAPA SE ESTA PINTANDO.
    //
    //  Un rotulo pintado y un control que se solapan solo son un fallo si
    //  estan en la MISMA ficha: la cara sigue debajo de una ficha abierta -sus
    //  tapas se maquetan y salen en el volcado- asi que comparar todo contra
    //  todo daba 417 hallazgos y casi todos eran una tarjeta opaca encima de
    //  la maquina. La ficha se identifica con un numero propio y cada control
    //  hereda el de la ficha en la que vive; la cara es la capa 0.
    inline int capaActual = 0;
    inline int siguienteCapa = 1;

    //  Y DONDE ESTA EL ORIGEN DE LO QUE SE PINTA.
    //
    //  El volcado de componentes va en coordenadas de la VENTANA -walk las
    //  convierte- y un rotulo se apunta con el rectangulo que su pintor le
    //  paso, que en una ficha desplazable esta en coordenadas del CUERPO. Los
    //  rotulos de AJUSTES salian todos con x=0 y comparados contra tapas en
    //  x=35: dos sistemas de coordenadas mezclados en la misma prueba.
    inline juce::Point<int> origenPintado { 0, 0 };

    //  No hace nada fuera del banco: una app que no se esta midiendo no tiene
    //  por que llevar la cuenta de lo que dibuja.
    inline bool midiendo = false;

    inline void rotulo (juce::Rectangle<int> r, const juce::String& t, const char* tipo,
                        int pide = 0, float cuerpo = 0.0f, int lineas = 1)
    {
        if (! midiendo || t.isEmpty()) return;
        r += origenPintado;
        rotulos.push_back ({ r.getX(), r.getY(), r.getWidth(), r.getHeight(), t, tipo, capaActual,
                             pide, cuerpo, lineas });
    }

    //  CUANTOS TOQUES DESDE LA CARA, QUE ES LA CIFRA QUE DEFINE «INTUITIVO» Y
    //  NO LA MEDIA NADIE.
    //
    //  El banco tenia diecinueve reglas de GEOMETRIA -dedo, solapes, ventana,
    //  celda, rotulo cortado- y ni una de CAMINO: una ficha a cuatro toques de
    //  la cara pasa las diecinueve y aun asi nadie la encuentra. La app tiene
    //  46 pantallas y hasta esta tanda no habia forma de decir cuantas estan
    //  lejos, ni de que una nueva se fuera al fondo sin que saltara nada.
    //
    //  Y SE DERIVA, NO SE DECLARA, que es lo que la hace util el segundo año:
    //  `openSheet (Sheet& s, juce::TextButton& toggle)` ya recibe las DOS
    //  mitades -la ficha y la tapa que la abrio- asi que apuntar el par cuesta
    //  esta linea, y la capa en la que vive esa tapa la publica ya el volcado.
    //  Una ficha nueva aparece sola, sin que nadie se acuerde de anotarla — la
    //  misma razon por la que las tapas de mantener «se buscan, no se
    //  enumeran» en `auditOpen`.
    //
    //  `tapa` es la capa de la TAPA, no la de la ficha: capa 0 es la cara, o
    //  sea un toque; una tapa que vive dentro de otra ficha son los toques de
    //  aquella mas uno. El grafo lo resuelve `Tests/profundidad.py`, que es
    //  donde hay las 46 corridas a la vez.
    struct Apertura { juce::String ficha; int tapa, capa; };
    inline std::vector<Apertura> aperturas;

    //  Y LA PUERTA ES `enabled()`, NO `midiendo`. Escrito con `midiendo` -que
    //  es lo que usan `rotulo` y `panel`- esto no apunto ni una: esa bandera
    //  solo esta puesta durante la pasada de pintado de `recogeRotulos`, y una
    //  ficha se abre mucho antes de que nadie pinte. Salieron cuatro corridas
    //  con la lista vacia y sin fallar nada, que es la forma que tiene una
    //  instrumentacion de mentir: no da un numero malo, no da ninguno.
    inline void apertura (const juce::String& ficha, int tapa, int capa)
    {
        if (! enabled()) return;
        for (const auto& a : aperturas)
            if (a.ficha == ficha && a.tapa == tapa) return;   // una vez basta
        aperturas.push_back ({ ficha, tapa, capa });
    }

    //  LOS PANELES DE GRUPO, apuntados igual que los rotulos y por lo mismo:
    //  son pintados, no son componentes, y lo que no se apunta no se mide. Un
    //  panel es la unica cosa de esta app que se dibuja ALREDEDOR de otras, asi
    //  que su fallo no es solaparse ni salirse - es que el aire que deja no sea
    //  el mismo por los cuatro lados, y eso solo se ve con las dos cifras al
    //  lado. Ver Tests/paneles.py.
    //
    //  Y ESO ERA FALSO, y costo dos quejas del telefono que ninguna regla podia
    //  ver: «sigue habiendo ese error de diseno en pad settings» y «como
    //  tambien en el apartado de ayuda». Un panel SI puede solaparse -con lo
    //  que NO envuelve- y SI puede salirse -del marco de su ficha-, y las dos
    //  cosas estaban pasando a la vista de todos. El parrafo de arriba se
    //  quedo escrito porque describe por que la prueba nacio; lo que no se
    //  queda es la conclusion, que era que no habia nada mas que preguntar.
    //
    //  Y CON NOMBRE, que es la mitad que faltaba para poder senalar: un fallo
    //  que dice «panel 212x96 en 74,318» obliga a abrir la maqueta y contar
    //  rectangulos. Con `padGrupos[2]` se va al sitio. El nombre es el del
    //  array que `resized()` publica mas su indice, o sea lo que el codigo ya
    //  sabe y la prueba no tenia forma de adivinar.
    struct Panel { juce::String nombre; int x, y, w, h; int capa; };
    inline std::vector<Panel> paneles;

    inline void panel (juce::Rectangle<int> r, const juce::String& nombre = {})
    {
        if (! midiendo) return;
        r += origenPintado;
        paneles.push_back ({ nombre, r.getX(), r.getY(), r.getWidth(), r.getHeight(), capaActual });
    }

    //  LO QUE UNA TARJETA PIDE Y LO QUE HAY.
    //
    //  `sheetFromBottom` recorta al tope de la tarjeta con un `jmin` y NO SE
    //  QUEJA: lo que falta se lo come en silencio lo ULTIMO que se maqueta, que
    //  es como esta casa ha pagado la fila de CADENA, la REJILLA de la pagina
    //  PATRON a 217x0, las cuatro tapas de CARCASA a 4 px y la celda de la
    //  linea de tiempo cayendo de 20.2 a 9. Las nueve reglas ven el SINTOMA
    //  -una celda por debajo de su suelo, un control de 0x0- y solo cuando lo
    //  que se cae es medible: una tarjeta que pide cincuenta pixeles de mas y
    //  se los quita a un texto pintado no la ve ninguna.
    //
    //  Se apunta la CAUSA y no se juzga: la primera corrida saco 92 y ni uno era
    //  un fallo -cinco fichas que piden su deseo entero y dejan que el recorte se
    //  lo coma un elemento elastico con suelo propio, con cero CERO y cero CELDA
    //  en la misma corrida-. Una ficha que se desplaza puede pedir lo que quiera
    //  -para eso se desplaza-; una que no, no. Ver Tests/expo.py.
    //  Y LO QUE LA TARJETA DEJA VER DEBAJO, que es la otra mitad y la que
    //  ninguna de las once reglas tiene: el tope de pie dejo de ser un
    //  porcentaje y pasa a DERIVARSE de que asome un pad entero -que desde la
    //  tanda de `onFuera` ademas se puede tocar-. Una derivacion que nadie
    //  comprueba es una afirmacion, asi que la app publica el hueco que queda
    //  por debajo y el suelo que ese hueco tiene que cumplir.
    //  Y EL RECTANGULO DE LA TARJETA CON EL MARCO QUE DECLARA, que es la
    //  tercera mitad de la queja -«se olvida el aire del marco»- y lo unico de
    //  ella que ninguna regla podia ver: `sheetFromBottom` mete el contenido
    //  `margenFichaX` x `margenFichaY` y devuelve ESE rectangulo, asi que una
    //  ficha que despues expande una fila hacia fuera -como la cara hace con
    //  `aireTapa` para alinear su filo- se come el marco sin que nada falle.
    struct Tarjeta { int pedido, tope, libreAbajo, sueloAbajo; bool desplaza;
                     int x, y, w, h, marcoX, marcoY, capa; };
    inline std::vector<Tarjeta> tarjetas;

    //  UNA COSTURA GRABADA: donde la app DICE que dibujo el rayado, y por que
    //  columna pasa.
    //
    //  Es la mitad que se juzga; la otra -donde esta el hueco de verdad- la
    //  da la FOTO, contando pixeles. Publicar aqui tambien los dos bordes que
    //  `engraveIn` recibio seria repetir la constante del codigo, que es
    //  exactamente el fallo que `Tests/icono.py` ya cometio dos veces con la
    //  mascara del lanzador: un banco que repite el numero no prueba nada.
    //  Con el TRAMO de rayado entero y no una columna: una fila de tapas deja
    //  huecos entre tapa y tapa, y una sola columna puede caer justo en uno —
    //  medido, la de CONTROL caia entre CARGAR y REC y la foto decia que el
    //  hueco empezaba sesenta pixeles mas arriba. Lo que el ojo centra es
    //  contra las TAPAS, asi que se busca la tinta mas cercana de todo el
    //  tramo.
    //  Y LA BANDA QUE LA COSTURA PINTA, que no es lo mismo que los dos bordes
    //  que `engraveIn` recibio: aquellos son la ENTRADA -repetirlos aqui seria
    //  el fallo de la mascara del lanzador- y esta es la SALIDA, el alto que la
    //  palabra y su rayado ocupan de verdad. La publica la misma funcion que
    //  los dibuja, asi que la pregunta que se puede hacer con ella es la de las
    //  filas del medidor: lo dibujado cabe donde lo dibujado deja sitio.
    //  Y EL FILO DERECHO DE LA ZONA, que es lo que hace falta para poder
    //  preguntar si algo cae DEBAJO de esta costura y no de la de al lado.
    //  `x0..x1` es el tramo IZQUIERDO del rayado -lo que la foto recorre para
    //  buscar el hueco- y una costura pinta ademas la palabra y el tramo de la
    //  derecha, asi que su extension real es `x0..x2`. Apaisado la cara son dos
    //  columnas y las tres costuras comparten alturas: sin el filo derecho, una
    //  pregunta por Y sola confundiria la de PADS con la de EFECTOS.
    struct Costura { int y, x0, x1, x2, y0, y1; };
    inline std::vector<Costura> costuras;
    inline void costura (int y, int x0, int x1, int x2, int y0, int y1)
    { if (enabled()) costuras.push_back ({ y, x0, x1, x2, y0, y1 }); }

    //  LA CUÑA QUE APUNTA A LA RANURA ENFOCADA, que no la miraba nadie.
    //
    //  Se PINTA y no reserva alto, asi que ninguna de las catorce reglas de
    //  `expo.py` le aplica: no es un componente, no solapa a un hermano, no se
    //  sale de la ventana y no lleva rotulo. Y estaba anclada a la tapa
    //  mientras el rayado de EFECTOS se movia con su hueco, asi que la barra le
    //  cruzaba por dentro en las seis ranuras.
    struct Cuna { int x, y, w, h; };
    inline std::vector<Cuna> cunas;
    inline void cuna (juce::Rectangle<int> r)
    { if (enabled()) cunas.push_back ({ r.getX(), r.getY(), r.getWidth(), r.getHeight() }); }

    //  LO QUE LA CARA PRESUPUESTA CONTRA LO QUE COLOCA.
    //
    //  Ninguna de las catorce reglas de `expo.py` puede verlo: un presupuesto
    //  que pide de mas no solapa, no se sale, no corta un rotulo y no mide
    //  cero — lo unico que hace es quitarle alto al CRISTAL, que es la unica
    //  banda de esta columna que da, y regalarselo a la rejilla de pads, que
    //  se lleva lo que sobre. Se ha encontrado a mano DOS veces (el
    //  `Metrics::xs` de las pestañas y el `Metrics::sm` de la costura de
    //  EFECTOS) y las dos leyendo el fichero.
    //
    //  Y no hace falta un libro de catorce bandas, porque el elastico es el
    //  testigo: todo lo demas de la columna tiene alto fijo, asi que cualquier
    //  descuadre aterriza aqui. `apretada` dice si el cristal se quedo clavado
    //  en su suelo — ahi los pads absorben el deficit A PROPOSITO y la cifra
    //  mide la escalera, no un fallo.
    inline int caraSobraPx = 0;
    inline bool caraApretada = false, caraSobraVista = false;
    inline void caraSobra (int px, bool apretada)
    { if (enabled()) { caraSobraPx = px; caraApretada = apretada; caraSobraVista = true; } }

    //  EL MEDIDOR DEL CRISTAL, FILA POR FILA Y CON EL CRISTAL QUE LAS TIENE
    //  QUE CONTENER.
    //
    //  La tercera fila -la del canal- se salio del cristal desde el dia que
    //  entro: cuatro pixeles por los segmentos y cinco por el rotulo, asi que
    //  en el telefono el numero se veia partido por la mitad. Ninguna de las
    //  trece reglas del banco podia verlo, y por una razon de fondo: TODAS
    //  recorren el arbol de COMPONENTES, y aqui el que se sale no es un
    //  componente sino una fila que `SpectrumDisplay::paint` dibuja DENTRO de
    //  uno. Desde fuera el cristal esta entero, en su sitio y sin solapar a
    //  nadie.
    //
    //  Se publica el rectangulo que cada fila OCUPA y el del cristal, los dos
    //  en coordenadas del propio cristal, y la regla pregunta si lo dibujado
    //  cabe en lo dibujado. No se publican `kMeterBand` ni el paso de fila: un
    //  banco que repite la constante del codigo no prueba el codigo — es lo
    //  que esta casa ya pago dos veces con la mascara del lanzador.
    struct Vu { int x, y, w, h; juce::String que; };
    inline std::vector<Vu> vus;
    inline void vuFila (juce::Rectangle<int> r, const juce::String& que)
    { if (enabled()) vus.push_back ({ r.getX(), r.getY(), r.getWidth(), r.getHeight(), que }); }

    //  Y POR CUAL DE LAS DOS RAMAS SALIO EL ROTULO DE LA TERCERA FILA. La
    //  palabra cabe hoy en las siete pantallas por los cuatro idiomas, asi que
    //  sin esto la rama corta -las dos cifras en el canalon- no la ve nadie y
    //  la escalera seria una linea que imprime OK. Se publica y no se juzga,
    //  como TOUCH: las dos ramas son correctas y lo que importa es que el
    //  banco pueda decir cual se tomo y verla caer al romperla.
    //  UNA CELDA QUE PIDE Y LO QUE SE LE DIO.
    //
    //  Ninguna de las veinte reglas puede ver a un control que se quedo con lo
    //  que SOBRABA en vez de con lo que pide: no solapa, no se sale, no corta
    //  el rotulo, no mide cero y esta traducido. Se ve de lejos y no se mide de
    //  cerca, que es la peor clase de fallo que tiene esta app.
    //
    //  Lo vivo cuando esto se escribio: con CHOKE solo en su fila, su celda era
    //  la fila ENTERA y la casilla del deslizador salia de 347 px en 412x915,
    //  contra las nueve casillas de 107 de la misma ficha. Un "off" de tres
    //  letras en un campo tres veces mas ancho que cualquier otro.
    //
    //  Y lo dice QUIEN LO SABE -el maquetado, que es el unico que tiene el
    //  numero que pidio- y no un script adivinando desde el volcado cual de dos
    //  anchos era el correcto. Es la misma pieza que `vuRotulo` y que `fila`.
    struct Celda { juce::String quien; int pide, da; };
    inline std::vector<Celda> celdas;
    inline void celda (const juce::String& quien, int pide, int da)
    { if (enabled()) celdas.push_back ({ quien, pide, da }); }

    //  LO QUE HAY DENTRO DE UNA CAJA, MEDIDO EN LA TINTA Y NO EN LA ARITMETICA.
    //
    //  Todas las reglas de geometria de esta casa le preguntan al MAQUETADO:
    //  `TARJETA` compara el pedido con el tope, `SOBRA` compara lo pedido con
    //  lo colocado, `MARCO`/`AJENO`/`VACIO` comparan rectangulos con
    //  rectangulos. Las cinco leen el mismo numero, asi que si el maquetado
    //  miente de forma coherente -pide 500 y coloca 490- ninguna se entera:
    //  preguntan al que miente.
    //
    //  Eso es literalmente lo que paso. La ficha del pad dejaba diez pixeles de
    //  aire muerto DENTRO del panel de abajo, 412x915/es/pads se mide 1980
    //  veces por barrido, y salio verde hasta que lo enseno la foto de un
    //  telefono. Y la regla que lo caza -`SOBRA`- esta escrita A MANO para UNA
    //  ficha: la publica `padSheet` porque yo la escribi ahi, y las otras
    //  cincuenta y cuatro no publican nada.
    //
    //  Esto pregunta lo mismo sin escribirlo cincuenta y cinco veces: donde
    //  acabo la TINTA. `recogeRotulos` ya pinta la app entera sobre una imagen
    //  para recoger los rotulos, asi que la foto ya esta hecha y solo hacia
    //  falta no tirarla. Cuesta un recorrido de pixeles en C++ -milisegundos- y
    //  no obliga a decodificar PNG en Python, que ahi es un bucle por byte
    //  (`Tests/costuras.py:106`) y sobre 495 capturas no se acaba nunca.
    //
    //  LO QUE SE MIDE ES LA BANDA VACIA MAS ALTA de dentro de la caja, y no el
    //  aire de los cuatro lados: el hueco del pad no estaba en un filo sino
    //  ENTRE las dos filas, y un aire de filo lo tiene cualquier panel por
    //  `panelAireY`. Con su altura y donde empieza, para poder senalar.
    //
    //  Y EL FONDO NO SE LE PASA, SE MIDE. Escribir aqui el color con el que
    //  `pintaPaneles` rellena seria la prueba repitiendo la constante del
    //  codigo -el fallo que `Tests/icono.py` cometio dos veces con la mascara
    //  del lanzador-, y ademas se rompe sola el dia que una carcasa mueva el
    //  token. Se toma el color MAS FRECUENTE de dentro de la caja, que en un
    //  panel es el relleno porque un panel es sobre todo hueco entre controles.
    //
    //  LO QUE ESTA REGLA NO PUEDE VER, y se escribe para que nadie lo
    //  descubra otra vez: una caja tan llena que su color mas frecuente sea el
    //  de un control y no el del fondo. Entonces la banda vacia sale en cero y
    //  esta caja no dice nada. Es un fallo hacia el silencio, que es el peor,
    //  y por eso la rotura a proposito de `Tests/limites.py` NO vale hecha en
    //  una ficha cualquiera: tiene que salir con la cifra de la ficha del pad,
    //  que es la unica de la que ya se sabe la respuesta.
    struct Tinta { juce::String quien; int x, y, w, h; int dw, dh;
                   int hx, hy, hw, hh, area; };
    inline std::vector<Tinta> tintas;

    //  El lienzo de `recogeRotulos`, guardado en vez de tirado. Vive aqui y no
    //  en la funcion porque `dump` lo necesita DESPUES de que la pasada de
    //  pintado termine, y volver a pintar por segunda vez seria otra foto que
    //  podria no ser la misma -un visor con temporizador ya ha cambiado-.
    inline juce::Image lienzo;

    //  El radio de la esquina de un panel: dentro de ese cuadrado la esquina es
    //  chasis y no relleno, asi que cuenta como tinta y taparia la medida. Se
    //  excluye de los dos extremos de cada fila, junto con dos pixeles de filo
    //  y suavizado -`drawRoundedRectangle` a 1 px sobre `reduced (0.5f)`-.
    inline void mideTinta (const juce::String& quien, juce::Rectangle<int> caja, int radio)
    {
        if (! enabled() || ! lienzo.isValid()) return;

        //  EL FILO VA CON NOMBRE Y NO CON UN DOS, y no es cosmetica: hay
        //  cuatro tokens de `Metrics` que valen dos -`keyAir`, `aireTapa`,
        //  `centraDedo`, `panelAireY`- y `Tests/maqueta.py` canta cualquier
        //  literal de aire que ya tenga token. Ninguno de esos cuatro significa
        //  esto: esto es el GROSOR DE LA LINEA que `drawRoundedRectangle`
        //  pinta sobre `reduced (0.5f)` mas su suavizado, o sea una propiedad
        //  del dibujo del panel y no una medida de maquetado. Ponerle el nombre
        //  de uno de ellos seria callar la regla mintiendo.
        constexpr int kFilo = 2;
        const auto dentro = caja.getIntersection (lienzo.getBounds())
                                .reduced (juce::jmax (kFilo, radio), kFilo);
        if (dentro.getWidth() < 4 || dentro.getHeight() < 4) return;

        const juce::Image::BitmapData px (lienzo, juce::Image::BitmapData::readOnly);

        //  EL FONDO SE VOTA EN EL BORDE Y NO EN EL INTERIOR, y esto costo una
        //  medida entera que decia lo contrario de lo que pasaba.
        //
        //  Votando TODO el interior, la caja de PROYECTOS -`setGrupos[0]`,
        //  355x58- salia con un hueco de 296x38, el 60.8 % de la caja, la peor
        //  de las cincuenta y cinco fichas en las nueve pantallas. Y no habia
        //  hueco: ese rectangulo es el interior de la caja de escribir el
        //  nombre, que estaba vacia porque no hay ningun proyecto. Medido:
        //  relleno del panel (27,74,79) en 7706 pixeles contra interior del
        //  editor (8,25,28) en 11248. El control ES MAS GRANDE que el fondo
        //  que lo rodea, asi que gana la votacion, y entonces la regla llama
        //  hueco al control y tinta al fondo: da la vuelta a la respuesta.
        //
        //  El borde no tiene esa duda. Un panel es un rectangulo relleno con
        //  el contenido metido para dentro; el anillo de dos pixeles justo por
        //  dentro de su filo es relleno POR CONSTRUCCION. Se vota ahi -y se
        //  vota, no se toma un pixel, porque un rotulo que llegue al filo
        //  ensucia un trozo del anillo pero no su mayoria-.
        //
        //  RGB de 16 bits -5/6/5- para que la tabla quepa y para que dos tonos
        //  a un paso de distancia no se partan el voto: el relleno es un color
        //  plano y el suavizado de sus bordes lo rodea de vecinos a un bit.
        std::vector<int> voto (1 << 16, 0);
        int modo = 0, mejor = -1;
        const auto vota = [&] (int x, int y)
        {
            const auto c = px.getPixelColour (x, y);
            const int k = ((c.getRed() >> 3) << 11) | ((c.getGreen() >> 2) << 5) | (c.getBlue() >> 3);
            if (++voto[(size_t) k] > mejor) { mejor = voto[(size_t) k]; modo = k; }
        };
        for (int d = 0; d < kFilo; ++d)
        {
            for (int x = dentro.getX(); x < dentro.getRight(); ++x)
            {
                vota (x, dentro.getY() + d);
                vota (x, dentro.getBottom() - 1 - d);
            }
            for (int y = dentro.getY(); y < dentro.getBottom(); ++y)
            {
                vota (dentro.getX() + d, y);
                vota (dentro.getRight() - 1 - d, y);
            }
        }

        const int fr = (modo >> 11) << 3, fg = ((modo >> 5) & 63) << 2, fb = (modo & 31) << 3;

        //  EL HUECO ES UN RECTANGULO, Y NO UNA BANDA NI UNA COLUMNA.
        //
        //  Empezo midiendo la fila vacia mas alta, y eso se comio el fallo que
        //  lo motivo: la celda de CHOKE deja medio renglon vacio A LA DERECHA,
        //  y por encima de ella hay una fila -CINTA/NORMALIZAR- que llega al
        //  filo. Ninguna fila entera esta vacia y ninguna columna entera esta
        //  vacia, asi que las dos medidas daban cuatro pixeles y el hueco que
        //  se ve es de doscientos por cuarenta. Medir un eje es media regla.
        //
        //  Se busca el RECTANGULO VACIO MAS GRANDE, que es el histograma de
        //  siempre: por cada fila, cuantos pixeles vacios lleva encima cada
        //  columna, y el rectangulo mayor bajo ese perfil con una pila. Cuesta
        //  un recorrido por pixel, igual que contarlos.
        //
        //  Un pixel esta vacio si no se aparta del fondo mas de ocho niveles.
        //  Ocho y no cero porque el suavizado de un filo cercano tiñe a sus
        //  vecinos un par de niveles, y con cero cada sombra seria tinta y el
        //  hueco saldria en cero SIEMPRE: una regla que no falla nunca.
        const int an = dentro.getWidth(), al = dentro.getHeight();
        std::vector<int> alto ((size_t) an, 0), pila;
        int mejorArea = 0, hx = 0, hy = 0, hw = 0, hh = 0;

        for (int y = 0; y < al; ++y)
        {
            for (int x = 0; x < an; ++x)
            {
                const auto c = px.getPixelColour (dentro.getX() + x, dentro.getY() + y);
                const bool vacio = std::abs ((int) c.getRed()   - fr) <= 8
                                && std::abs ((int) c.getGreen() - fg) <= 8
                                && std::abs ((int) c.getBlue()  - fb) <= 8;
                alto[(size_t) x] = vacio ? alto[(size_t) x] + 1 : 0;
            }

            pila.clear();
            for (int x = 0; x <= an; ++x)
            {
                const int h = (x < an) ? alto[(size_t) x] : 0;
                int izq = x;
                while (! pila.empty() && alto[(size_t) pila.back()] >= h)
                {
                    const int i = pila.back(); pila.pop_back();
                    const int base = pila.empty() ? 0 : pila.back() + 1;
                    const int area = alto[(size_t) i] * (x - base);
                    if (area > mejorArea)
                    {
                        mejorArea = area;
                        hw = x - base;  hh = alto[(size_t) i];
                        hx = dentro.getX() + base;
                        hy = dentro.getY() + y - hh + 1;
                    }
                    izq = base;
                }
                juce::ignoreUnused (izq);
                if (x < an) pila.push_back (x);
            }
        }
        //  Y SE PUBLICA EL INTERIOR MEDIDO -`dw`, `dh`- y no solo la caja.
        //  Sin el, la regla de Python tendria que restarle el borde por su
        //  cuenta, o sea escribir `Metrics::sm` otra vez en otro fichero: la
        //  prueba repitiendo la constante del codigo, que es como `icono.py`
        //  dio verde dos veces con la mascara rota. Con el interior publicado,
        //  un panel VACIO DEL TODO vale exactamente 1.0 y el liston sale de la
        //  aritmetica en vez de elegirse a ojo.
        tintas.push_back ({ quien, caja.getX(), caja.getY(), caja.getWidth(), caja.getHeight(),
                            dentro.getWidth(), dentro.getHeight(),
                            hx, hy, hw, hh, mejorArea });
    }

    struct VuRot { juce::String texto; int pide, tiene; };
    inline std::vector<VuRot> vuRotulos;
    inline void vuRotulo (const juce::String& t, int pide, int tiene)
    { if (enabled()) vuRotulos.push_back ({ t, pide, tiene }); }

    //  Y no se guarda con `midiendo`, que solo esta puesto durante la pasada
    //  de pintado: esto lo escribe `resized()`, que corre antes. La lista la
    //  vacia el propio `resized()` al empezar, o cada maquetado dejaria el
    //  suyo encima del anterior.
    inline void tarjeta (int pedido, int tope, bool desplaza,
                         int libreAbajo = -1, int sueloAbajo = 0,
                         juce::Rectangle<int> r = {}, int marcoX = 0, int marcoY = 0,
                         int capa = 0)
    {
        if (! enabled()) return;
        tarjetas.push_back ({ pedido, tope, libreAbajo, sueloAbajo, desplaza,
                              r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                              marcoX, marcoY, capa });
    }

    //  UNA FILA DE TAPAS LLENA EL RECTANGULO QUE SE LE DIO.
    //
    //  Cada tapa de una fila se recorta por los lados -es el hueco que la
    //  separa de su hermana- y ese recorte sobra en los DOS extremos, asi que
    //  la fila entera acababa dos pixeles dentro. Medido en la cara a 412x915:
    //  el cristal, los cuatro bancos y los dieciseis pads de 14 a 398, y las
    //  pestanas de modulo, el transporte y los seis efectos de 16 a 396. Tres
    //  filos izquierdos en la pantalla que no se puede evitar.
    //
    //  NINGUNA de las once reglas puede verlo: dos pixeles de margen no
    //  solapan, no se salen de la ventana, no cortan un rotulo, no miden cero
    //  y estan traducidos. Y no vale preguntarselo a todas las filas de la
    //  app - en RECORTE los cuatro deslizadores empiezan 68 px dentro porque a
    //  su izquierda va el nombre de cada uno, PINTADO, que es exactamente el
    //  falso positivo que `Tests/paneles.py` ya se comio.
    //
    //  Lo que SI es exacto y no tiene excepcion legitima es esto: quien coloca
    //  una fila de tapas recibe un rectangulo y tiene que llenarlo. Se apunta
    //  el que se dio y la union de lo que se puso, y los dos filos tienen que
    //  coincidir. Lo dice quien lo sabe -el maquetado- y no un script que
    //  tenga que adivinar que filas son hermanas.
    struct Fila { int dadaX, dadaR, puestaX, puestaR, y; };
    inline std::vector<Fila> filas;

    inline void fila (juce::Rectangle<int> dada, juce::Rectangle<int> puesta)
    {
        if (! enabled() || puesta.isEmpty()) return;
        filas.push_back ({ dada.getX(), dada.getRight(),
                           puesta.getX(), puesta.getRight(), dada.getY() });
    }

    //  QUE TAPAS DE LA CARA TIENEN UN GESTO ESCONDIDO.
    //
    //  Un `HoldButton` hace DOS cosas y solo una se ve: la de mantener no deja
    //  marca en la cara, asi que no se descubre tocando. La unica pantalla que
    //  las enumera es AJUSTES · GESTOS, y esa lista se escribio a mano y se
    //  quedo vieja dos veces —MANTEN SOLO y MANTEN AUTO llevaban dos tandas
    //  existiendo sin fila—. Ninguna de las once reglas puede verlo: un gesto
    //  que no esta en una lista se maqueta perfecto.
    //
    //  La lista NO se escribe en el script, que serian dos listas y la del
    //  banco se quedaria vieja igual: la BUSCA la app recorriendo los hijos de
    //  la cara, con el rotulo puesto en el idioma de la corrida. El banco pide
    //  que alguna fila de GESTOS lo NOMBRE, que es lo unico que separa «tiene
    //  fila» de «tiene la fila de otro»: quitando la de SOLO, las de abajo se
    //  corren y la comprobacion por indice seguiria en verde.
    //
    //  `familia` es la excepcion declarada: las ranuras de efecto comparten una
    //  sola fila -«MANTEN UN EFECTO»- que no las nombra una por una, porque
    //  seis filas iguales son el manual otra vez.
    struct Mantener { juce::String tapa; int familia; };
    inline std::vector<Mantener> mantener;

    inline void gestoDe (const juce::String& tapa, int familia)
    {
        if (! enabled()) return;
        mantener.push_back ({ tapa, familia });
    }

    //  EL JUEGO DE ICONOS, RASTERIZADO.
    //
    //  Un icono se juzga por tres cosas y solo la primera se ve mirandolo:
    //  que quepa en su caja, que tenga tinta suficiente para leerse, y que no
    //  se parezca a ningun otro. La tercera es la unica que no se puede juzgar
    //  de uno en uno - es la misma leccion que el banco de la fabrica, donde
    //  sesenta y cuatro copias del mismo bombo sacaban sobresaliente en las
    //  cinco pruebas que miran un sonido por separado.
    //
    //  Se vuelca la rejilla de alfas y el juicio vive en Tests/iconos.py: las
    //  respuestas no pueden estar dentro del codigo que se prueba.
    inline void volcadoIconos (int n)
    {
        for (int i = 1; i < (int) Iconos::Id::kNum; ++i)
        {
            const auto id = (Iconos::Id) i;
            const auto px = Iconos::rasteriza (id, n);
            const auto lim = Iconos::limites (id);

            juce::String hex;
            hex.preallocateBytes ((size_t) (n * n * 2 + 8));
            for (auto v : px) hex << juce::String::toHexString ((int) v).paddedLeft ('0', 2);

            std::cout << "{\"icono\":\"" << Iconos::nombre (id) << "\""
                      << ",\"n\":" << n
                      //  El lado al que la app dibuja un icono. Lo dice ella y
                      //  no el script: es donde la prueba de pares tiene que
                      //  preguntar, y escrito en los dos sitios seria el numero
                      //  de ayer el dia que suba.
                      //
                      //  Y ES `iconoLado` Y NO `kLadoMin`. Mientras el lado se
                      //  pedia del alto de cada tapa, el mas pequeno posible
                      //  era el suelo y ahi habia que preguntar; desde que es
                      //  uno solo, `kLadoMin` es el suelo que ese lado tiene
                      //  que superar y NO un tamano al que se dibuje nada. La
                      //  prueba de pares llevaba una tanda comparando a 13 px
                      //  dibujos que la app pinta a 17.
                      << ",\"min\":" << ZatiLookAndFeel::iconoLado()
                      << ",\"x\":" << juce::String (lim.getX(), 2)
                      << ",\"y\":" << juce::String (lim.getY(), 2)
                      << ",\"w\":" << juce::String (lim.getWidth(), 2)
                      << ",\"h\":" << juce::String (lim.getHeight(), 2)
                      << ",\"px\":\"" << hex << "\"}" << std::endl;
        }

        //  Y LAS ONCE CIFRAS, por la misma puerta y con el mismo formato.
        //
        //  Diez digitos y el signo son once dibujos nuevos, y un dibujo nuevo
        //  sin las cuatro reglas de `iconos.py` es exactamente lo que este
        //  proyecto ya pago con los sesenta y cuatro bombos iguales: que no
        //  esten vacios, que no sean manchas, que llenen su caja y sobre todo
        //  que **no sean dos el mismo** — un 6 y un 9 mal escritos se
        //  distinguen por donde se cierra el bucle y nada mas, y a trece
        //  pixeles eso es un pixel.
        //
        //  Se emiten con la clave `icono` a proposito: la prueba que los juzga
        //  es la misma y no hay que escribirla dos veces. El nombre lleva
        //  prefijo para que un fallo diga «cifra 8» y no «8».
        for (const char* c : { "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "-" })
        {
            const auto px = Cifras::rasteriza ((juce::juce_wchar) c[0], n);
            juce::String hex;
            hex.preallocateBytes ((size_t) (n * n * 2 + 8));
            for (auto v : px) hex << juce::String::toHexString ((int) v).paddedLeft ('0', 2);

            //  Los limites del glifo en SU rejilla de 10x14, llevados a la de
            //  24 en la que la regla de la caja pregunta: el trazo se ensancha
            //  al pintar, asi que se cuenta con su grosor puesto, igual que
            //  `Iconos::limites`.
            auto p = Cifras::glifo ((juce::juce_wchar) c[0]);
            const float m = Cifras::grosor (Cifras::kH) * 0.5f;
            const auto lim = p.isEmpty() ? juce::Rectangle<float>()
                                         : p.getBounds().expanded (m);
            const float esc = 24.0f / Cifras::kH;

            std::cout << "{\"icono\":\"cifra " << c << "\""
                      << ",\"n\":" << n
                      << ",\"min\":" << (int) Cifras::kAltoMin
                      << ",\"x\":" << juce::String (lim.getX() * esc, 2)
                      << ",\"y\":" << juce::String (lim.getY() * esc, 2)
                      << ",\"w\":" << juce::String (lim.getWidth() * esc, 2)
                      << ",\"h\":" << juce::String (lim.getHeight() * esc, 2)
                      << ",\"px\":\"" << hex << "\"}" << std::endl;
        }
    }


    //  ==========================================================================
    //  EL DIBUJO Y LA PALABRA COMPARTEN LINEA, y hasta hoy nadie lo media.
    //
    //  La queja llego mirando el telefono -«los sprites deben estar centrados en
    //  altura con el texto»- y las once reglas de `expo.py` no pueden verla: un
    //  icono dos pixeles alto se maqueta perfecto, no solapa, no se sale, no
    //  corta el rotulo y esta traducido. Es la familia de los cinco fallos del
    //  compas del piano otra vez, en el unico sitio donde el ojo si lo ve.
    //
    //  Y LA PRIMERA MEDIDA SE EQUIVOCO, que a estas alturas es el patron: se
    //  midieron los LIMITES NOMINALES del camino (`Iconos::limites`) y salieron
    //  dieciocho descentrados, con `deshacer` a 2.73 unidades de 24. No lo
    //  estaban: `Iconos::dibuja` ya centra por la TINTA desde que todos ocupan
    //  la misma caja -`dx = caja.getCentreX() - lim.getCentreX() * esc`- asi que
    //  lo nominal no es lo que se pinta. Se mide lo PINTADO, que es la regla de
    //  la casa, y se mide contra el ROTULO y no contra la caja: dos cosas
    //  centradas cada una en su mitad pueden seguir sin compartir renglon.
    //
    //  Se pinta SOLO el primer plano -`drawButtonText` sobre un lienzo
    //  transparente- porque con la tapa debajo «tinta» seria cualquier pixel que
    //  no sea el fondo, y el fondo de una tapa lleva degradado, filo y sombra.
    //  ==========================================================================
    inline void volcadoTapa (int escala)
    {
        struct Caso { const char* rot; Iconos::Id id; int w, h; };
        static const Caso casos[] =
        {
            { "CANCION", Iconos::Id::cancion, 120, 44 },
            { "PLAY",    Iconos::Id::play,     90, 44 },
            { "AJUSTES", Iconos::Id::sistema, 130, 40 },
            { "MEDIR",   Iconos::Id::medir,   120, 32 },
            { "SEC",     Iconos::Id::sec,      80, 26 },
            { "VACIAR",  Iconos::Id::vaciar,  140, 48 },
        };

        ZatiLookAndFeel laf;
        for (const auto& c : casos)
        {
            juce::TextButton b (c.rot);
            b.setLookAndFeel (&laf);
            b.getProperties().set ("icono", (int) c.id);
            b.setSize (c.w, c.h);

            const auto rep = ZatiLookAndFeel::reparteTapa (b);
            if (rep.id == Iconos::Id::ninguno) { b.setLookAndFeel (nullptr); continue; }

            const int S = juce::jlimit (2, 16, escala);
            juce::Image img (juce::Image::ARGB, c.w * S, c.h * S, true);
            {
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale ((float) S));
                laf.drawButtonText (g, b, false, false);
            }

            //  El primero y el ultimo renglon con tinta dentro de una banda de
            //  columnas, en unidades del boton.
            auto banda = [&img, S] (int x0, int x1, float& arriba, float& abajo)
            {
                arriba = -1.0f; abajo = -1.0f;
                juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
                for (int y = 0; y < img.getHeight(); ++y)
                    for (int x = juce::jmax (0, x0 * S); x < juce::jmin (img.getWidth(), x1 * S); ++x)
                        if (bd.getPixelColour (x, y).getAlpha() > 40)
                        {
                            if (arriba < 0.0f) arriba = (float) y / (float) S;
                            abajo = (float) y / (float) S;
                            break;
                        }
            };

            float ia = 0.0f, ib = 0.0f, ta = 0.0f, tb = 0.0f;
            banda (rep.icono.getX(), rep.icono.getRight(), ia, ib);
            banda (rep.texto.getX(), c.w,                  ta, tb);

            std::cout << "{\"tapa\":\"" << c.rot << "\""
                      << ",\"w\":" << c.w << ",\"h\":" << c.h
                      << ",\"letra\":" << juce::String (rep.fuente.getHeight(), 2)
                      << ",\"lado\":" << rep.icono.getWidth()
                      << ",\"icono\":[" << juce::String (ia, 3) << "," << juce::String (ib, 3) << "]"
                      << ",\"texto\":[" << juce::String (ta, 3) << "," << juce::String (tb, 3) << "]"
                      << "}" << std::endl;
            b.setLookAndFeel (nullptr);
        }
    }

    inline int tourPaso = -1;
    inline int tourFocoW = 0, tourFocoH = 0;
    //  Lo que dicen las dos salidas de la tarjeta: la que avanza y la tercera,
    //  que en el paso de la PUERTA deja de decir SALTAR. Ver `tourSkipCaption`.
    inline std::string tourSig, tourTercera;
    //  Y EN QUE PAGINA QUEDO LA MESA. El paso 11 nombra los dieciseis
    //  CANALES y abria la mesa en la pagina que hubiera - por defecto
    //  PADS - asi que la palabra no se veia. Es una CIFRA y no un si/no:
    //  dice QUE pagina quedo, asi que caza tambien el caso contrario, que
    //  otro paso deje la mesa en CANALES, sin escribir una segunda regla.
    inline int tourMixPag = 0;

    inline int cabezalPiano = 0;
    //  Y cuantas veces se ha ALIMENTADO la pagina del piano. Son dos cifras y
    //  no una porque en un escritorio sin tarjeta de sonido el transporte no
    //  avanza -el paso lo mueve la llamada de audio- y cabezalPiano saldria
    //  cero tanto con el fallo puesto como con el quitado. Lo que si se puede
    //  medir aqui es el eslabon que faltaba: que el temporizador llame a
    //  refreshPiano mientras la pagina del piano esta abierta. Cero es el
    //  fallo; el resto lo cuenta el motor, que es donde el paso avanza.
    inline int pianoTicks = 0;

    //  UNA FOTO DEL COMPONENTE, a la escala que se pida.
    //
    //  A escala 1 la foto sale con los pixeles logicos - 412 de ancho - y Play
    //  pide como minimo 320 en el lado corto, asi que pasaria por los pelos y
    //  se veria como un movil de 2012. Pintar a escala 2.6 no estira una
    //  imagen: vuelve a dibujar los vectores, las fuentes y los degradados a
    //  1080 de ancho, que es lo que hace que un rotulo se lea en la ficha.
    inline void snapshot (juce::Component& c, const juce::String& path, float scale)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), false, scale);
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();

        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (img, out);
        out.flush();
    }

    inline juce::String esc (const juce::String& s)
    {
        juce::String o;
        for (auto c : s)
        {
            if (c == '"' || c == '\\') o << '\\' << (juce::juce_wchar) c;
            else if (c == '\n')        o << "\\n";
            else if (c < 32)           o << ' ';
            else                       o << (juce::juce_wchar) c;
        }
        return o;
    }

    //  What a component would need to draw its caption without clipping, and
    //  what it actually has. Sliders are excluded: their caption is a number
    //  in a box the layout sized on purpose.
    //  Y LA ESCALA MINIMA A LA QUE SE VA A APRETAR, que es lo que separa un
    //  rotulo apretado de uno CORTADO y no era el mismo numero para todos: la
    //  tapa la dibuja `drawButtonText` de esta casa con 0.9 y la Label la
    //  dibuja JUCE con `getMinimumHorizontalScale()`, que vale cero y en
    //  `drawFittedText` significa el 0.7 por defecto. `expo.py` usaba 0.9 para
    //  las dos y llamaba CORTADO a lo que la Label todavia aprieta.
    struct Caption { bool has = false; float needW = 0.0f; float haveW = 0.0f;
                     float escala = 0.9f; juce::String text; };

    inline Caption captionOf (juce::Component& c)
    {
        Caption r;

        if (auto* tb = dynamic_cast<juce::TextButton*> (&c))
        {
            //  A pad's caption is its number in the display face and its
            //  sample name in a strip of its own; neither is measured here.
            if (dynamic_cast<PadButton*> (&c) != nullptr)
                return r;

            r.has  = true;
            r.text = tb->getButtonText();

            //  LA MISMA CUENTA QUE DIBUJA, no una copia de ella.
            //
            //  Aqui ponia "EXACTLY what ZatiLookAndFeel::drawButtonText will
            //  use" y no lo era: la letra salia del alto del COMPONENTE y el
            //  dibujo la saca del alto de la TAPA -capaDe, o sea max(26,
            //  alto*0.75)-. En una tapa de 44 px, 14.5 px medidos contra 12.5
            //  dibujados. Y el hueco tampoco: desde que una tapa puede llevar
            //  icono, el rotulo no dispone del ancho entero. Las dos cosas
            //  salen ahora de ZatiLookAndFeel::reparteTapa.
            const auto rep = ZatiLookAndFeel::reparteTapa (*tb);

            //  Y UNA TAPA QUE NO DIBUJA SU ROTULO NO TIENE ROTULO QUE MEDIR.
            //
            //  Lo que se mide es lo que se DIBUJA, que es la regla de esta
            //  funcion entera. Una tapa que solo lleva dibujo no pinta su
            //  rotulo en ningun sitio, y contarlo aqui lo daria por CORTADO:
            //  dieciseis hallazgos por rejilla de un texto que nadie ve.
            if (rep.texto.isEmpty())
            {
                r.has = false;
                return r;
            }

            r.needW = juce::GlyphArrangement::getStringWidth (rep.fuente, r.text);
            r.haveW = (float) rep.texto.getWidth();
        }
        else if (auto* l = dynamic_cast<juce::Label*> (&c))
        {
            r.has  = true;
            r.text = l->getText();

            //  LA FUENTE Y EL BORDE CON LOS QUE SE VA A DIBUJAR, no los que la
            //  Label guarda. `LookAndFeel_V2::drawLabel` no usa `getFont()` de
            //  la etiqueta: pide `getLabelFont (label)`, que aqui devuelve el
            //  mono de `Metrics::fValue` — 13 px — mientras `getFont()` sigue
            //  siendo el sans de 15 que JUCE le pone al construirla y que
            //  nadie ha tocado nunca. Y el hueco util no son cuatro pixeles
            //  menos sino DIEZ: el borde por defecto de una Label es
            //  {1, 5, 1, 5} y `drawLabel` se lo resta antes de escribir.
            //
            //  Los dos errores se tapaban el uno al otro -el sans de 15 pide
            //  de mas, los cuatro pixeles perdonan de mas- y el resultado es
            //  que ninguna caja de cifra de esta app se ha medido nunca de
            //  verdad. Se vio en 280x653: la caja de PATRON salia de 16 px
            //  para un «P1» que el banco decia que pedia 16.4 y que dibujado
            //  mide 12, o sea la prueba mandaba ensanchar una caja que cabia
            //  y callaba en las que no. *Medir con otra fuente es responder
            //  «cabe» a una pregunta que no se ha hecho*, que es lo que ya
            //  decia el comentario de la rama de las tapas dos lineas arriba.
            auto& laf = l->getLookAndFeel();
            r.needW = juce::GlyphArrangement::getStringWidth (laf.getLabelFont (*l), r.text);
            r.haveW = (float) laf.getLabelBorderSize (*l)
                                 .subtractedFrom (l->getLocalBounds()).getWidth();
            //  Cero no es cero: `Graphics::drawFittedText` lo lee como «el
            //  minimo por defecto», que son 0.7.
            const float esc = l->getMinimumHorizontalScale();
            r.escala = esc > 0.0f ? esc : 0.7f;
        }

        return r;
    }

    inline const char* kindOf (juce::Component& c)
    {
        if (dynamic_cast<juce::Slider*>     (&c) != nullptr) return "slider";
        if (dynamic_cast<juce::TextEditor*> (&c) != nullptr) return "editor";
        if (dynamic_cast<juce::Button*>     (&c) != nullptr) return "button";
        if (dynamic_cast<juce::Label*>      (&c) != nullptr) return "label";
        //  Una lista desplegable ES un control y salia como "other", asi que
        //  para todo lo que filtra por tipo no existia: los dos puertos MIDI
        //  quedaban fuera y el panel que los envuelve parecia dejar 5 px por un
        //  lado y 237 por el otro. No entra en `interactive` -esos tres estan
        //  escritos uno a uno mas abajo- asi que las seis reglas de expo.py no
        //  se mueven; lo que gana es precision en el volcado.
        if (dynamic_cast<juce::ComboBox*>   (&c) != nullptr) return "combo";
        return "other";
    }

    //  Set by the app before a dump: how long the ENGINE thinks pad n is.
    inline std::function<int (int)> engineLength;

    //  DE QUE PAD SALE EL AUDIO DE CADA UNO. El troceado no es un componente:
    //  son dieciseis pads apuntando al mismo buffer, y eso no se ve en un
    //  volcado del arbol - que es como se perdio al guardar y volver sin que
    //  ninguna prueba lo notara. Se imprime aparte. -1 = pad vacio.
    inline std::function<int (int)> padSource;

    //  Y que pads llevan un paso puesto. Ver padSource: un patron tampoco es
    //  un componente.
    inline std::function<bool (int)> stepOn;

    inline void walk (juce::Component& c, juce::Component& root, const juce::String& path, int depth,
                      bool underSlider = false, bool underViewport = false, int capa = 0)
    {
        if (! c.isVisible()) return;

        //  Ver capaActual: una ficha lleva su numero en una propiedad y todo
        //  lo que cuelga de ella lo hereda.
        if (c.getProperties().contains ("capa"))
            capa = (int) c.getProperties()["capa"];

        const auto abs = root.getLocalArea (&c, c.getLocalBounds());
        const auto cap = captionOf (c);
        const auto kind = kindOf (c);
        const bool interactive = (juce::String (kind) == "button" || juce::String (kind) == "slider"
                                  || juce::String (kind) == "editor");
        //  A slider's text box and its two keys are the slider; they are not
        //  three targets that happen to overlap, and measuring them as such
        //  buries the real findings under a hundred of its own making.
        const bool insideSlider = underSlider;
        //  Anything inside a viewport is ALLOWED past the window edge - that
        //  is what a scroll is. Reporting it as laid out off screen would
        //  bury the real overflows under the one case that is intentional.
        const bool scrolled = underViewport;

        juce::String line;
        line << "{\"path\":\"" << esc (path) << "\""
             << ",\"kind\":\"" << kind << "\""
             << ",\"depth\":" << depth
             << ",\"x\":" << abs.getX() << ",\"y\":" << abs.getY()
             << ",\"w\":" << abs.getWidth() << ",\"h\":" << abs.getHeight()
             << ",\"on\":" << (c.isEnabled() ? 1 : 0)
             << ",\"hit\":" << (interactive && ! insideSlider ? 1 : 0)
             << ",\"inSlider\":" << (insideSlider ? 1 : 0)
             << ",\"capa\":" << capa
             << ",\"scrolled\":" << (scrolled ? 1 : 0)
             //  Y SI TIENE NOMBRE PARA QUIEN NO LO VE. `Component::getTitle` es
             //  lo que un lector de pantalla lee: sin el, un control se anuncia
             //  por su clase -«boton»- y la app entera suena igual. No es una
             //  regla dura -un chip de banco no necesita mas nombre que su
             //  letra- pero sin la cifra, «esta accesible» y «tiene nombre el
             //  10 %» son la misma corrida en verde. Ver Tests/carga.py.
             << ",\"nombre\":" << (c.getTitle().isNotEmpty() ? 1 : 0)
             //  Y CUANTO AIRE DEJA SU PROPIO DIBUJO, que es lo que separa la
             //  banda RESERVADA del hueco que se VE.
             //
             //  Una tapa se pinta tres cuartos de alta y centrada -`capaDe`-
             //  asi que deja cinco pixeles vacios por arriba y otros cinco por
             //  abajo DENTRO de su rectangulo. El histograma de aire medía el
             //  rectangulo, o sea que dos filas de tapas pegadas salian a CERO
             //  con diez pixeles a la vista: 1232 de los 1312 huecos «a cero»
             //  eran esa misma pareja de la cara, que ademas esta pegada A
             //  PROPOSITO y con su medida escrita al lado. Un banco que mide la
             //  reserva y no lo dibujado da la cifra de otra pantalla.
             //
             //  Lo dice la app y no el script, que es la decision de siempre
             //  -`pide`, la marca `valor`, `Iconos::kLadoMin`, `MIN_CELL`-: la
             //  cuenta la hace `aireTapaVertical`, que es la MISMA que usan
             //  `ctrlSeamTop` y las costuras, y repetirla en Python serian dos
             //  reglas. Cero para todo lo que llena su rectangulo, que es lo
             //  que hace un `PadButton` -deriva de `juce::Button` y pinta el
             //  suyo entero- y lo que hace un deslizador.
             << ",\"aire\":" << (dynamic_cast<juce::TextButton*> (&c) != nullptr
                                     ? ZatiLookAndFeel::aireTapaVertical (abs.getHeight()) : 0);

        //  WHAT IS ON THE PAD. The one piece of state worth carrying in a
        //  layout dump: after leaving the app and coming back, is the sound
        //  still on the pad it was on? That is a question about the session,
        //  not about pixels, and it is the question this app keeps failing.
        if (auto* pb = dynamic_cast<PadButton*> (&c))
            line << ",\"pad\":1,\"loaded\":" << (pb->hasSample() ? 1 : 0)
                 << ",\"sample\":\"" << esc (pb->sampleName()) << "\"";

        //  ...and what the ENGINE thinks, which is the half that was missing.
        //  "loaded" is the tile's own belief; a pad can look perfectly loaded
        //  and be silent, and for every restored session it was. The dump now
        //  carries both, so the two can be compared instead of trusted.
        if (engineLength != nullptr)
            if (auto* pb = dynamic_cast<PadButton*> (&c))
                line << ",\"engine\":" << engineLength (pb->getIndex());

        if (cap.has && cap.text.isNotEmpty())
            line << ",\"text\":\"" << esc (cap.text) << "\""
                 << ",\"needW\":" << juce::String (cap.needW, 1)
                 << ",\"haveW\":" << juce::String (cap.haveW, 1)
                 << ",\"escala\":" << juce::String (cap.escala, 2);

        //  LO QUE VIENE DE FUERA NO SE TRADUCE.
        //
        //  La regla de traduccion es comparativa: se maqueta lo mismo en es y
        //  en en y se compara la cadena en la MISMA ruta del arbol. Eso vale
        //  para todo lo que la app escribe... y no vale para lo que la app LEE.
        //  El nombre de un instrumento sale de una carpeta del disco, asi que
        //  es identico en los cuatro idiomas por definicion, y la regla lo
        //  contaba como sin traducir: 168 hallazgos de una tacada, todos falsos
        //  y todos tapando los de verdad.
        //
        //  Quien pone el texto dice si es dato. Un rotulo que la app decide
        //  -PACK, INSTRUMENTOS- se traduce y se mide; uno que la app copia de
        //  un directorio, no.
        if (c.getProperties().contains ("dato"))
            line << ",\"dato\":1";

        //  LA FILA DE RADIO A LA QUE PERTENECE Y SI ESTA ENCENDIDA.
        //
        //  Las catorce reglas de este banco miden GEOMETRIA, y una fila de
        //  chips con dos encendidos -o con ninguno- se maqueta perfecta: no
        //  solapa, no se sale, no corta su rotulo, no mide cero y esta
        //  traducida. Es la familia de los cinco fallos del compas del piano, y
        //  se pago dos veces a la vez: TOMAS salia con los cuatro apagados
        //  porque compartia id con el MONITOR, y la mesa salia con dos bancos
        //  encendidos porque `showMixBank` escribia una sola tapa.
        //
        //  Se vuelca lo que hace falta para las dos preguntas y el juicio vive
        //  en Python, como con los iconos: el `grupo` dice quienes son hermanas
        //  para JUCE -y el padre sale ya del `path`, que es como la regla del
        //  rotulo tapado deduce la capa-, la `fila` dice quienes lo son de
        //  verdad, y `toggle` dice cual esta encendida. Un grupo de cero es un
        //  control suelto y no se vuelca: seria una columna de ceros por linea.
        //
        //  Y LOS TRES NOMBRES SE ELIGIERON A LA TERCERA, que es lo que vale
        //  escrito: el espacio de claves de este volcado es PLANO y no lo
        //  vigilaba nadie.
        //
        //    - `on` ya existe cien lineas mas arriba y significa otra cosa -si
        //      el control esta HABILITADO-. Dos claves iguales en el mismo
        //      objeto JSON no dan un error: al parsearlo gana la ultima, asi
        //      que la regla nueva habria funcionado y la de al lado habria
        //      empezado a llamar «deshabilitado» a todo chip apagado.
        //    - `lit` es el nombre de la propiedad con la que `litAccent` marca
        //      las tapas que se encienden en el acento.
        //    - `fila` ya es una LINEA entera del volcado -la que `judge_fila`
        //      mide contra el rectangulo que se le dio-, y esa regla reconoce
        //      sus lineas por la presencia de la clave: con una tapa trayendola
        //      tambien, el banco entero reventaba con KeyError.
        //
        //  Una clave, un significado. Y como esto se ha pagado tres veces en
        //  una tarde, `Tests/expo.py` comprueba ahora que ninguna linea repita
        //  una clave, que es la unica forma de que la cuarta falle en vez de
        //  publicarse.
        if (auto* bt = dynamic_cast<juce::Button*> (&c))
            if (bt->getRadioGroupId() != 0)
            {
                line << ",\"grupo\":" << bt->getRadioGroupId()
                     << ",\"toggle\":" << (bt->getToggleState() ? 1 : 0)
                     << ",\"radio\":\""
                     << esc (bt->getProperties().getWithDefault ("fila", "").toString()) << "\"";
                if ((int) bt->getProperties().getWithDefault ("filaVacia", 0) != 0)
                    line << ",\"radioVacia\":1";
            }

        //  EL ICONO QUE LE HA TOCADO A ESTA TAPA Y A QUE TAMANO.
        //
        //  No es un componente, asi que ninguna de las reglas de geometria lo
        //  ve: un dibujo de nueve pixeles dentro de una tapa de cuarenta es
        //  una mancha y el volcado saldria identico al de una tapa sin icono.
        //  Y se dumpea tambien CUAL, porque la otra mitad de la pregunta es si
        //  el icono sobrevive a la pantalla estrecha - donde no cabe, no sale,
        //  y eso tiene que poder contarse.
        if (auto* tb = dynamic_cast<juce::TextButton*> (&c))
        {
            const auto rep = ZatiLookAndFeel::reparteTapa (*tb);
            if (rep.id != Iconos::Id::ninguno)
                line << ",\"icono\":\"" << Iconos::nombre (rep.id) << "\""
                     << ",\"icoW\":" << rep.icono.getWidth()
                     << ",\"icoH\":" << rep.icono.getHeight();
            else if ((int) tb->getProperties().getWithDefault ("icono", 0) != 0)
                line << ",\"icono\":\"\",\"icoW\":0,\"icoH\":0";

            //  Y SI ESA TAPA NO LLEVA DIBUJO A PROPOSITO.
            //
            //  `Tests/planos.py` mide las filas donde unas tapas llevan dibujo
            //  y otras no, y la primera version llevaba las excepciones en una
            //  lista de ROTULOS en espanol: "1 OCTAVA", "PAD -". Eso es una
            //  prueba que solo sabe medir una de las cuatro compilaciones -en
            //  ingles el mismo boton dice "1 OCTAVE" y la excepcion deja de
            //  encajar-, o sea la clase de banco que da verde por no reconocer
            //  lo que mira. Lo dice la app, que es quien lo sabe.
            if ((int) tb->getProperties().getWithDefault ("valor", 0) != 0)
                line << ",\"valor\":1";
        }

        //  EL COLOR DE UN PAD Y EL ESTILO DE UN MANDO, que es lo que hace
        //  falta para volver a DIBUJAR la pagina y no solo para medirla.
        //
        //  Tests/planos.py redibuja las 32 pantallas, y sin estas dos lineas
        //  tiene que adivinarlas: los dieciseis pads salian del mismo color
        //  -cuando el color del pad ES como se encuentra un sonido en esta
        //  maquina- y los mandos se repartian en giratorio o carril por la
        //  PROPORCION de su caja, que falla en cuanto la caja incluye el
        //  rotulo. Lo dice quien lo sabe, que es la misma regla por la que la
        //  marca `valor` la pone ponIconos y no una lista en Python.
        //  Y CUANTAS CELDAS TIENE UNA REJILLA QUE SE PINTA ENTERA.
        //
        //  La unica regla que las mide es la de la CELDA -ancho util entre
        //  columnas, alto entre filas- y el banco llevaba esos numeros escritos
        //  a mano en dos ficheros. Desde que las tres tienen ventana continua y
        //  zoom, la cuenta de ayer mide otra rejilla: lo dice quien lo sabe.
        if (auto* rj = dynamic_cast<Rejilla*> (&c))
            line << ",\"cols\":" << rj->celdasAncho()
                 << ",\"filas\":" << rj->celdasAlto()
                 << ",\"canal\":" << rj->canalIzq()
                 << ",\"cw\":" << juce::String (rj->celdaAnchoPx(), 2)
                 << ",\"ch\":" << juce::String (rj->celdaAltoPx(), 2);

        if (auto* pb = dynamic_cast<PadButton*> (&c))
            line << ",\"zati\":" << pb->getZati()
                 << ",\"color\":\"" << Zati::colour (pb->getZati()).toDisplayString (false) << "\"";

        if (auto* sl = dynamic_cast<juce::Slider*> (&c))
        {
            const auto st = sl->getSliderStyle();
            const char* e = (st == juce::Slider::LinearHorizontal || st == juce::Slider::LinearBar) ? "linh"
                          : (st == juce::Slider::LinearVertical || st == juce::Slider::LinearBarVertical) ? "linv"
                          : (st == juce::Slider::IncDecButtons) ? "incdec" : "rot";
            line << ",\"estilo\":\"" << e << "\"";
        }

        line << "}";
        std::cout << line << std::endl;

        const bool childUnderSlider   = underSlider   || dynamic_cast<juce::Slider*>   (&c) != nullptr;
        const bool childUnderViewport = underViewport || dynamic_cast<juce::Viewport*> (&c) != nullptr;
        int i = 0;
        for (auto* k : c.getChildren())
            walk (*k, root, path + "/" + juce::String (i++) + ":" + juce::String (typeid (*k).name()).getLastCharacters (14),
                  depth + 1, childUnderSlider, childUnderViewport, capa);
    }

    // ========================================================================
    //  LAS MISMAS REGLAS, PERO DENTRO Y SIN VOLCADO.
    //
    //  expo.py mide 476 estados FIJOS: los que a alguien se le ocurrieron. Lo
    //  que rompe una interfaz de verdad es la combinacion - la ficha X abierta
    //  con el banco D, en arabe, en apaisado, despues de haber tocado nueve
    //  cosas - y esa no se enumera, se sortea. Sortearla desde fuera cuesta un
    //  arranque de proceso por estado, o sea un segundo y medio; desde dentro
    //  cuesta un repintado.
    //
    //  Solo dos de las cinco reglas, y son las dos que no dependen del idioma:
    //  hermanos que se pisan y controles que se salen de la ventana. Las de
    //  texto necesitan comparar dos idiomas en la misma ruta, que es cosa del
    //  script.
    //  El CULPABLE, no solo la cuenta. Un "8 solapes" manda a leer el
    //  maquetado entero; "ADELANTE sobre 2" apunta a la linea. Se guarda el
    //  primero, que en la practica es el que arrastra a los demas.
    struct Hallazgos { int solapes = 0, fuera = 0, mirados = 0; juce::String quien; };

    inline void recoge (juce::Component& c, juce::Component& root,
                        juce::Array<juce::Rectangle<int>>& hermanos, Hallazgos& h,
                        bool underSlider = false, bool underViewport = false)
    {
        if (! c.isVisible()) return;

        const juce::String kind = kindOf (c);
        const bool interactive = (kind == "button" || kind == "slider" || kind == "editor");
        const auto abs = root.getLocalArea (&c, c.getLocalBounds());

        if (interactive && ! underSlider && abs.getWidth() > 0 && abs.getHeight() > 0)
        {
            ++h.mirados;
            if (! underViewport && ! root.getLocalBounds().contains (abs)) ++h.fuera;
        }

        //  Los hermanos se comparan entre ellos y no contra el arbol entero:
        //  una ficha encima de la cara es el diseno, no un solape.
        juce::Array<juce::Rectangle<int>> mios;
        for (auto* k : c.getChildren())
            recoge (*k, root, mios, h, underSlider || kind == "slider",
                    underViewport || dynamic_cast<juce::Viewport*> (&c) != nullptr);

        if (interactive && ! underSlider && abs.getWidth() > 0 && abs.getHeight() > 0)
        {
            for (const auto& otro : hermanos)
            {
                const auto in = otro.getIntersection (abs);
                if (in.getWidth() > 1 && in.getHeight() > 1)
                {
                    ++h.solapes;
                    if (h.quien.isEmpty())
                    {
                        const auto cap = captionOf (c);
                        h.quien = (cap.text.isNotEmpty() ? cap.text : juce::String (kind))
                                + " @" + juce::String (abs.getX()) + "," + juce::String (abs.getY())
                                + " " + juce::String (abs.getWidth()) + "x" + juce::String (abs.getHeight());
                    }
                }
            }
            hermanos.add (abs);
        }
    }

    inline Hallazgos check (juce::Component& root)
    {
        Hallazgos h;
        juce::Array<juce::Rectangle<int>> raiz;
        recoge (root, root, raiz, h);
        return h;
    }

    //  LO QUE CUESTA UN FOTOGRAMA, y de quien es la culpa.
    //
    //  El motor se puede medir corriendo bloques; la cara no, porque no la
    //  ejecuta nadie: la pinta el sistema cuando le parece. Asi que aqui se
    //  pinta a mano N veces sobre una imagen del mismo tamano que la ventana
    //  y se cronometra - primero el arbol entero, que es lo que paga el
    //  telefono en un repintado completo, y despues cada hijo directo por
    //  separado, que es lo unico que dice A QUIEN cobrarselo.
    //
    //  Se pinta en escala 1 a proposito: lo que se busca es la PROPORCION
    //  entre componentes, y esa no cambia con la densidad de la pantalla,
    //  mientras que el numero absoluto de un portatil no vale para un movil.
    //
    //  La MEDIANA de los fotogramas y no la media, por lo mismo que en el
    //  banco de CPU: un solo fotograma interrumpido por el sistema mueve una
    //  media lo bastante como para invertir el orden de dos filas.
    inline void paintCost (juce::Component& root, int frames)
    {
        if (frames < 1) frames = 1;

        auto mide = [frames] (juce::Component& c, juce::Rectangle<int> clip = {}) -> double
        {
            const auto b = c.getLocalBounds();
            if (b.getWidth() < 1 || b.getHeight() < 1) return 0.0;

            juce::Image img (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
            std::vector<double> t;
            t.reserve ((size_t) frames);

            for (int i = 0; i < frames; ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                {
                    juce::Graphics g (img);
                    //  Recortado, si se pide: es exactamente lo que hace el
                    //  sistema cuando un componente pide repintarse solo un
                    //  trozo, y por tanto lo que cuesta de verdad un repintado
                    //  parcial - incluido lo que hay DETRAS de ese trozo, que
                    //  con una ficha translucida encima tambien hay que
                    //  volver a dibujar.
                    if (! clip.isEmpty()) g.reduceClipRegion (clip);
                    c.paintEntireComponent (g, false);
                }
                t.push_back (std::chrono::duration<double, std::milli> (
                                 std::chrono::steady_clock::now() - t0).count());
            }

            std::sort (t.begin(), t.end());
            return t[t.size() / 2];
        };

        //  Y el FONDO del propio componente raiz, sin sus hijos: el chasis, las
        //  placas y la rotulacion grabada. Se mide llamando a paint() a secas
        //  en vez de a paintEntireComponent, porque restarlo de la suma de los
        //  hijos daba un numero que incluia todo lo que no supimos atribuir.
        auto soloFondo = [frames] (juce::Component& c) -> double
        {
            juce::Image img (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                             juce::jmax (1, c.getHeight()), true);
            std::vector<double> t;
            t.reserve ((size_t) frames);
            for (int i = 0; i < frames; ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                { juce::Graphics g (img); c.paint (g); }
                t.push_back (std::chrono::duration<double, std::milli> (
                                 std::chrono::steady_clock::now() - t0).count());
            }
            std::sort (t.begin(), t.end());
            return t[t.size() / 2];
        };

        const double total = mide (root);
        std::cout << "{\"pintado\":1,\"w\":" << root.getWidth() << ",\"h\":" << root.getHeight()
                  << ",\"fotogramas\":" << frames
                  << ",\"fondo_ms\":" << soloFondo (root)
                  //  Una banda de 30 px de alto en mitad de la ventana: el
                  //  tamano de lo unico que se mueve en una ficha mientras el
                  //  secuenciador rueda. Si esta fila es mucho mas barata que
                  //  total_ms, cada repaint() de ficha entera esta pagando el
                  //  fotograma completo para animar un renglon.
                  << ",\"banda30_ms\":" << mide (root, { 0, root.getHeight() / 2 - 15, root.getWidth(), 30 })
                  << ",\"total_ms\":" << total << "}" << std::endl;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            auto* c = root.getChildComponent (i);
            if (c == nullptr || ! c->isVisible()) continue;
            std::cout << "{\"pieza\":\"" << esc (c->getName().isNotEmpty() ? c->getName()
                                                                          : juce::String (typeid (*c).name()))
                      << "\",\"w\":" << c->getWidth() << ",\"h\":" << c->getHeight()
                      << ",\"hijos\":" << c->getNumChildComponents()
                      << ",\"ms\":" << mide (*c) << "}" << std::endl;
        }
    }

    //  UNA PASADA DE PINTADO ANTES DE VOLCAR, o los rotulos no existen.
    //
    //  En modo banco la app maqueta y se va: nadie pinta, asi que la lista de
    //  UiAudit::rotulo se queda vacia y el plano saldria sin un solo titulo -
    //  que es exactamente lo que hay que auditar. Se pinta una vez sobre una
    //  imagen que se tira, igual que hace paintCost, y con `midiendo` puesto
    //  solo durante esa pasada: fuera del banco esto no corre nunca.
    inline void recogeRotulos (juce::Component& root)
    {
        const auto b = root.getLocalBounds();
        if (b.getWidth() < 1 || b.getHeight() < 1) return;
        rotulos.clear();
        paneles.clear();
        tintas.clear();
        midiendo = true;
        //  Y LA IMAGEN SE GUARDA EN VEZ DE TIRARSE. Es la unica foto que esta
        //  pasada hace, y `mideTinta` la necesita DESPUES de que el pintado
        //  termine -hasta entonces no hay lista de paneles que recorrer-.
        //  Pintar una segunda vez para medirla seria otra foto, y una ficha con
        //  un visor que late no da dos veces la misma. Ver UiAudit::Tinta.
        lienzo = juce::Image (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
        { juce::Graphics g (lienzo); root.paintEntireComponent (g, true); }
        midiendo = false;
    }

    //  EL EFECTO QUE REVELA LOS LIMITES, y por que es de banco y no de la app.
    //
    //  Las medidas de esta casa contestan con un numero, y un numero dice
    //  CUANTO pero no ENSENA. La ficha del pad la encontro una foto de un
    //  telefono porque a la vista habia una losa con un agujero, y ninguna de
    //  las once reglas podia dibujarla. Esto pinta encima de lo ya pintado el
    //  contorno de cada caja -tarjeta, panel, fila y control con dedo- para que
    //  las cincuenta y cinco fichas se puedan MIRAR de una vez.
    //
    //  Y NO RECALCULA NADA. Los rectangulos salen de los vectores que este
    //  mismo fichero ya llena durante el maquetado y el pintado; si los
    //  recalculara seria un segundo maquetado, y dos maquetados son dos
    //  respuestas -que es la figura del `438` escrito a mano contra la funcion
    //  que coloca-. Lo que se dibuja es lo que se midio, o no vale de nada.
    //
    //  DETRAS DE SU VARIABLE Y FUERA DEL CAMINO NORMAL. `Tests/cpu.py` cuenta
    //  pixeles repintados y no llamadas, asi que una linea de mas en el pintado
    //  de cada fotograma se nota: con `ZATI_CONTORNOS` vacio esto es un `if`
    //  que sale. No viaja a lo que se instala como una funcion que alguien
    //  pueda encender: no hay interruptor en AJUSTES, a peticion.
    inline bool contornosOn()
    {
        static const bool v = env ("ZATI_CONTORNOS").isNotEmpty();
        return v;
    }

    //  Un contorno de un pixel y un tono por tipo. Sin relleno y sin sombra: lo
    //  que se quiere ver es lo que hay DEBAJO, y un velo encima lo escondería
    //  igual que lo esconde no tener contorno.
    inline void contornos (juce::Graphics& g, juce::Component& root)
    {
        if (! contornosOn()) return;

        const auto traza = [&g] (juce::Rectangle<int> r, juce::Colour c, float grosor)
        {
            if (r.getWidth() < 1 || r.getHeight() < 1) return;
            g.setColour (c);
            g.drawRect (r, (int) grosor);
        };

        //  Los controles primero y el resto encima: una tapa dentro de un panel
        //  tiene que dejar ver el filo del panel, y no al reves.
        std::function<void (juce::Component&)> anda = [&] (juce::Component& c)
        {
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                if (auto* h = c.getChildComponent (i))
                {
                    if (! h->isVisible()) continue;
                    const auto r = root.getLocalArea (h, h->getLocalBounds());
                    traza (r.toNearestInt(), juce::Colour (0x5500a0ff), 1.0f);
                    anda (*h);
                }
        };
        anda (root);

        for (const auto& f : filas)
            traza ({ f.dadaX, f.y, f.dadaR - f.dadaX, 2 }, juce::Colour (0xaaffcc00), 1.0f);

        for (const auto& p : paneles)
            traza ({ p.x, p.y, p.w, p.h }, juce::Colour (0xcc00ff88), 1.0f);

        for (const auto& t : tarjetas)
            if (t.w > 0 && t.h > 0)
                traza ({ t.x, t.y, t.w, t.h }, juce::Colour (0xddff3355), 2.0f);

        //  Y LA BANDA VACIA, RELLENA, que es la unica que se pinta maciza: es
        //  lo que se ha venido a ver. Rojo translucido sobre lo que no tiene
        //  dueño. Si aqui no hay nada rojo, la ficha llena lo que pidio.
        for (const auto& t : tintas)
            if (t.area > 0)
            {
                g.setColour (juce::Colour (0x44ff0000));
                g.fillRect (juce::Rectangle<int> (t.hx, t.hy, t.hw, t.hh));
            }
    }

    //  LA PASADA DE TINTA ENTERA, y vive fuera de `dump` porque la piden DOS:
    //  el volcado -que la imprime- y la lamina de contornos, que la pinta. La
    //  foto sale antes que el volcado (`Main.cpp`), asi que si esto viviera
    //  dentro de `dump` la lamina saldria con las bandas vacias sin marcar y
    //  seria una lamina que no enseña lo que se ha venido a ver.
    //
    //  EL RADIO ES EL QUE `pintaPaneles` USA HOY, Y SE LEE DEL TOKEN. Decia
    //  `Metrics::sm` -ocho- y era cierto mientras el panel se redondeaba con un
    //  token de ESPACIADO por no haber ninguno de radio. Desde que lo hay,
    //  `pintaPaneles` redondea con `Metrics::radio` -tres- y esta linea se
    //  quedo midiendo la esquina de antes: `mideTinta` hace
    //  `reduced (jmax (kFilo, radio), kFilo)`, o sea que el banco descontaba
    //  OCHO pixeles por costado donde solo hay tres y miraba la tinta de cada
    //  panel en una caja DIEZ px mas estrecha que el panel. La tinta que se
    //  mete en esa franja no la veia nadie. Escrito con el token y no con un
    //  ocho, mover el radio mueve la medida.
    //
    //  Una tarjeta se dibuja con esquinas cuadradas, asi que ahi no hay esquina
    //  que excluir.
    inline void mideTodaLaTinta (juce::Component& root)
    {
        recogeRotulos (root);

        for (const auto& p : paneles)
            mideTinta (p.nombre.isNotEmpty() ? p.nombre : juce::String ("panel"),
                       { p.x, p.y, p.w, p.h }, (int) Metrics::radio);
        for (size_t i = 0; i < tarjetas.size(); ++i)
        {
            const auto& t = tarjetas[i];
            if (t.w > 0 && t.h > 0)
                mideTinta ("tarjeta[" + juce::String ((int) i) + "]",
                           { t.x, t.y, t.w, t.h }, 0);
        }
    }

    inline void dump (juce::Component& root)
    {
        mideTodaLaTinta (root);
        std::cout << "{\"root\":1,\"w\":" << root.getWidth() << ",\"h\":" << root.getHeight()
                  //  EL LADO QUE LA APP DICE QUE DIBUJA. Lo publica ella y no
                  //  lo escribe el script: escrito en los dos sitios, el banco
                  //  compararia contra el numero de ayer el dia que suba, que
                  //  es justo lo que esta regla existe para cazar.
                  << ",\"icoLado\":" << ZatiLookAndFeel::iconoLado()
                  << ",\"lang\":\"" << env ("ZATI_LANG") << "\""
                  << ",\"open\":\"" << env ("ZATI_OPEN") << "\"}" << std::endl;

        for (const auto& r : rotulos)
            std::cout << "{\"rotulo\":\"" << r.texto.replace ("\"", "'").toRawUTF8() << "\""
                      << ",\"tipo\":\"" << r.tipo << "\""
                      << ",\"x\":" << r.x << ",\"y\":" << r.y
                      << ",\"w\":" << r.w << ",\"h\":" << r.h
                      << ",\"capa\":" << r.capa
                      << ",\"pide\":" << r.pide
                      << ",\"cuerpoLetra\":" << juce::String (r.cuerpoLetra, 2)
                      << ",\"lineas\":" << r.lineas << "}" << std::endl;

        for (const auto& a : aperturas)
            std::cout << "{\"apertura\":\"" << a.ficha.toRawUTF8() << "\""
                      << ",\"tapa\":" << a.tapa
                      << ",\"capa\":" << a.capa << "}" << std::endl;

        for (const auto& p : paneles)
            std::cout << "{\"panel\":1"
                      << ",\"nombre\":\"" << p.nombre.toRawUTF8() << "\""
                      << ",\"x\":" << p.x << ",\"y\":" << p.y
                      << ",\"w\":" << p.w << ",\"h\":" << p.h
                      << ",\"capa\":" << p.capa << "}" << std::endl;

        for (const auto& f : filas)
            std::cout << "{\"fila\":1"
                      << ",\"dadaX\":" << f.dadaX << ",\"dadaR\":" << f.dadaR
                      << ",\"puestaX\":" << f.puestaX << ",\"puestaR\":" << f.puestaR
                      << ",\"y\":" << f.y << "}" << std::endl;

        for (const auto& t : tarjetas)
            std::cout << "{\"tarjeta\":1"
                      << ",\"pedido\":" << t.pedido
                      << ",\"tope\":" << t.tope
                      << ",\"libre\":" << t.libreAbajo
                      << ",\"suelo\":" << t.sueloAbajo
                      << ",\"desplaza\":" << (t.desplaza ? 1 : 0)
                      << ",\"x\":" << t.x << ",\"y\":" << t.y
                      << ",\"w\":" << t.w << ",\"h\":" << t.h
                      << ",\"marcoX\":" << t.marcoX << ",\"marcoY\":" << t.marcoY
                      << ",\"capa\":" << t.capa
                      << "}" << std::endl;

        for (const auto& c : costuras)
            std::cout << "{\"costura\":1,\"y\":" << c.y
                      << ",\"x0\":" << c.x0 << ",\"x1\":" << c.x1
                      << ",\"x2\":" << c.x2
                      << ",\"y0\":" << c.y0 << ",\"y1\":" << c.y1 << "}"
                      << std::endl;

        for (const auto& c : cunas)
            std::cout << "{\"cuna\":1,\"x\":" << c.x << ",\"y\":" << c.y
                      << ",\"w\":" << c.w << ",\"h\":" << c.h << "}"
                      << std::endl;

        if (caraSobraVista)
            std::cout << "{\"cara\":1,\"sobra\":" << caraSobraPx
                      << ",\"apretada\":" << (caraApretada ? 1 : 0) << "}"
                      << std::endl;

        for (const auto& r : vuRotulos)
            std::cout << "{\"vurot\":1,\"texto\":\"" << r.texto.toRawUTF8() << "\""
                      << ",\"pide\":" << r.pide << ",\"tiene\":" << r.tiene << "}" << std::endl;

        for (const auto& c : celdas)
            std::cout << "{\"celda\":\"" << c.quien.toRawUTF8() << "\""
                      << ",\"pide\":" << c.pide << ",\"da\":" << c.da << "}" << std::endl;

        for (const auto& t : tintas)
            std::cout << "{\"tinta\":\"" << t.quien.toRawUTF8() << "\""
                      << ",\"x\":" << t.x << ",\"y\":" << t.y
                      << ",\"w\":" << t.w << ",\"h\":" << t.h
                      << ",\"dw\":" << t.dw << ",\"dh\":" << t.dh
                      << ",\"hx\":" << t.hx << ",\"hy\":" << t.hy
                      << ",\"hw\":" << t.hw << ",\"hh\":" << t.hh
                      << ",\"area\":" << t.area << "}" << std::endl;

        for (const auto& v : vus)
            std::cout << "{\"vu\":1,\"que\":\"" << v.que << "\""
                      << ",\"x\":" << v.x << ",\"y\":" << v.y
                      << ",\"w\":" << v.w << ",\"h\":" << v.h << "}"
                      << std::endl;

        for (const auto& m : mantener)
            std::cout << "{\"mantener\":\"" << m.tapa << "\",\"familia\":" << m.familia << "}"
                      << std::endl;

        if (tourPaso >= 0)
            std::cout << "{\"tour\":" << tourPaso
                      << ",\"focoW\":" << tourFocoW
                      << ",\"focoH\":" << tourFocoH
                      //  Y LO QUE DICEN LAS DOS SALIDAS. La bienvenida son
                      //  CUATRO pasos y los otros once estan detras de una
                      //  PUERTA: en el cuarto, la tapa que decia SALTAR pasa a
                      //  ofrecer seguir. Sin estas dos cadenas, partir el tour
                      //  y no partirlo dan la misma corrida en verde.
                      << ",\"sig\":\"" << tourSig << "\""
                      << ",\"tercera\":\"" << tourTercera << "\""
                      << ",\"mixpag\":" << tourMixPag << "}" << std::endl;
        walk (root, root, "root", 0);

        if (padSource != nullptr)
        {
            std::cout << "{\"fuentes\":[";
            for (int i = 0; i < 64; ++i)
                std::cout << (i ? "," : "") << padSource (i);
            std::cout << "]}" << std::endl;
        }

        if (stepOn != nullptr)
        {
            std::cout << "{\"pasos\":[";
            bool first = true;
            for (int i = 0; i < 64; ++i)
                if (stepOn (i)) { std::cout << (first ? "" : ",") << i; first = false; }
            std::cout << "]}" << std::endl;
        }
    }
}

// ============================================================================
//  PULSAR UNA TAPA COMO LA PULSA UN DEDO, Y SABER CUAL NO SE PUEDE PULSAR.
//
//  Vivia en `MainComponentInterno.h`, que las cuatro unidades de MainComponent
//  incluyen y `Main.cpp` NO -lo dice su propia cabecera: «no se incluye desde
//  fuera»-. Asi que `Main.cpp`, donde vive el fuzz, no la tenia y apretaba con
//  `triggerClick()`, que es `postCommandMessage`: el mensaje se encola y el
//  bucle no vuelve hasta que el fuzz ENTERO ha terminado y la app ha cerrado.
//  Medido leyendo `juce_Button.cpp:359`. O sea que la rama de botones del fuzz
//  -una de cada tres acciones sorteadas- no ha apretado nunca nada, y las dos
//  reglas que comprueba despues juzgaban el estado de antes de la accion.
//  Se muda aqui, que es la cabecera que los dos lados SI comparten.
//
//  Y la otra mitad: `Button::internalClickCallback` hace
//  `setToggleState (true, sendNotification)`, que apaga a las hermanas del
//  grupo de radio Y DISPARA SUS `onClick`. Ese callback espurio es la causa de
//  los dos bancos encendidos a la vez en la mesa, y con `onClick()` a pelo no
//  existe: *un gesto que no se puede llamar es un gesto que no se mide*.
inline void pulsaTapa (juce::Button* b)
{
    if (b == nullptr)
        return;

    if (b->getClickingTogglesState())
    {
        const bool quiere = (b->getRadioGroupId() != 0 || ! b->getToggleState());
        if (quiere != b->getToggleState())
        {
            b->setToggleState (quiere, juce::sendNotification);
            return;
        }
    }

    if (b->onClick)
        b->onClick();
}

//  LA TAPA QUE EL BANCO NO PUEDE APRETAR SE MARCA, NO SE ADIVINA POR EL ROTULO.
//
//  El fuzz descartaba por texto: `! t.contains ("CARGAR") && ! t.contains
//  ("LOAD") && ! t.contains ("EXPORT") && ! t.contains ("MIC") && ! t.contains
//  ("REC")`. Tres fallos medidos en esa linea:
//
//    1. El rotulo esta TRADUCIDO. En chino CARGAR es 加载 y en arabe تحميل, asi
//       que la lista solo protegia dos de los cuatro idiomas y el fuzz en `zh`
//       apretaba justo lo que la lista existe para no apretar.
//    2. Descartaba de mas por subcadena: «RECORTE» y «RECORTAR» contienen
//       «REC», asi que dos tapas inofensivas del recorte llevaban sin medirse
//       desde que la lista existe, y nadie podia saberlo leyendola.
//    3. Y descartaba de menos donde importa: la unica tapa que abre un dialogo
//       NATIVO es SISTEMA (`launchSystemPicker` → `FileChooser::launchAsync`),
//       y «SISTEMA» no contiene ninguna de las cinco palabras. Con
//       `triggerClick()` no pasaba nada porque no se apretaba; en cuanto se
//       aprieta de verdad, esa es la que cuelga el banco.
//
//  Se marca en el constructor, donde se sabe POR QUE, y la marca no depende del
//  idioma ni del rotulo que la tapa lleve ese dia.
inline void sinBanco (juce::Button& b, const char* porque)
{
    b.getProperties().set ("sinBanco", juce::String (porque));
}

inline bool tapaDeBanco (const juce::Button& b)
{
    return ! b.getProperties().contains ("sinBanco");
}

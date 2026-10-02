#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include <array>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>

// ============================================================================
//  PianoRoll — las NOTAS de un pad, en una rejilla de tono contra tiempo.
//
//  Un sampler con un piano dentro es un sampler; sin el es una caja de ritmos.
//  Hasta ahora la unica forma de afinar un paso era abrir la pagina PASO y
//  mover el mando NOTA del paso que tuvieras tocado: un semitono, un paso, un
//  viaje. Escribir tres acordes asi son treinta y seis viajes, y por eso nadie
//  los escribia - la maquina tocaba percusion afinada y nada mas.
//
//  Aqui el eje vertical es el TONO y el horizontal el TIEMPO, que es como se
//  lee una melodia desde que existe el pentagrama. Un toque pone una nota; el
//  mismo toque la quita. Varias notas en la misma columna son un acorde, la
//  misma nota en columnas seguidas es un ritmo, y una escalera de notas por
//  columnas es un arpegio: las tres cosas que se pidieron salen del mismo
//  gesto sin nada que aprender.
//
//  Dibujada y no construida con botones, por lo mismo que la rejilla de pasos:
//  veinticinco tonos por dieciseis pasos son cuatrocientos componentes que
//  maquetar y repintar, y lo que hace falta es un componente que pinta y
//  acierta con el dedo.
//
//  LAS TECLAS NEGRAS SE PINTAN NEGRAS. Sin eso la rejilla son veinticinco
//  filas iguales y no hay forma de saber en que nota estas sin contarlas desde
//  abajo: el patron 2-3 de un teclado es lo unico que orienta la vista, y es
//  gratis - sale de saber si el semitono cae en {1,3,6,8,10}.
// ============================================================================
class PianoRoll : public juce::Component, public Rejilla
{
public:
    //  Las tres cifras del banco. El piano cambia de columnas con el zoom y
    //  de filas con OCTAVA -trece o veinticinco-, asi que escribirlas fuera
    //  seria medir la rejilla de ayer.
    int celdasAncho() const override { return numPasos(); }
    int celdasAlto()  const override { return filas; }
    int canalIzq()    const override { return kGutter; }
    float celdaAnchoPx() const override { return (float) juce::jmax (0, getWidth() - kGutter) / (float) juce::jmax (1, nPasos); }
    float celdaAltoPx()  const override { return (float) altoRejilla() / (float) juce::jmax (1, filas); }

    //  LA REGLA DE TIEMPO, encima de la rejilla y del ancho de las columnas.
    //
    //  Llego del telefono produciendo: «igual se podia hacer la seleccion en
    //  base a la linea de tiempo, como se hace en FL». Hasta hoy seleccionar
    //  era la quinta herramienta y una banda elastica por (paso, semi): coger
    //  DOS COMPASES ENTEROS con todas sus notas exigia armar SEL y barrer la
    //  rejilla de arriba a abajo sin dejarse una fila, y lo que estuviera en
    //  otra octava se quedaba fuera. Un tramo de la regla es «de aqui a aqui,
    //  todo», que es lo que se copia cuando se copia una frase.
    //
    //  Y el mismo sitio sirve para decir DONDE se pega: un toque sin arrastre
    //  deja el cursor, y PEGAR cae ahi y no en la primera columna que se vea.
    //  Dos gestos en la misma banda y ninguno pisa la rejilla: la regla no
    //  escribe notas y la rejilla no mueve el cursor.
    //
    //  Mide lo que la columna del teclado, por lo mismo: es la otra cabecera
    //  de la misma tabla, y la esquina donde se cruzan sale cuadrada. Y SE
    //  DESCUENTA del alto en una sola cuenta -`altoRejilla`- que es de donde
    //  sale el alto de la fila: la ficha que reparte el alto, el banco que mide
    //  la celda y el dedo que apunta tienen que leer el mismo numero.
    static constexpr int kRegla = Metrics::canalPiano;
    int altoRejilla() const noexcept { return juce::jmax (1, getHeight() - kRegla); }

    //  LA GEOMETRIA, EN UN SITIO. El banco apuntaba a una celda repitiendo la
    //  division -`getHeight() / filas`- en tres ficheros; con la regla puesta
    //  esa cuenta se queda vieja en los tres a la vez y la prueba apunta a la
    //  fila de al lado sin que nadie lo diga.
    float xDeColumna (int col) const noexcept { return (float) kGutter + celdaAnchoPx() * (float) col; }
    float yDeFila (int fila)   const noexcept { return (float) kRegla + celdaAltoPx() * (float) fila; }
    void  centroDe (int col, int fila, float& x, float& y) const noexcept
    {
        x = xDeColumna (col) + celdaAnchoPx() * 0.5f;
        y = yDeFila (fila)   + celdaAltoPx()  * 0.5f;
    }
    //  Y UN PUNTO DE LA REGLA, para el banco: el centro de la columna, a media
    //  altura de la banda.
    void  puntoRegla (int col, float& x, float& y) const noexcept
    {
        x = xDeColumna (col) + celdaAnchoPx() * 0.5f;
        y = (float) kRegla * 0.5f;
    }

    //  UNA OCTAVA Y SU RAIZ, trece filas. Eran veinticinco -dos octavas- y esa
    //  cuenta se hizo por el rango del motor y no por el dedo: veinticinco
    //  filas se reparten el alto que quede, y medido ficha por ficha la fila
    //  salia a 18.0 px en un movil grande, 10.8 en un 360x640, 11.2 en el
    //  Fold cerrado y 7.4 APAISADO — la sexta parte de un dedo. Poner una nota
    //  a ojo en 10 px es escribir la de al lado, y por eso "no se pueden
    //  colocar ergonomicamente": el gesto estaba bien, el blanco no.
    //
    //  No se arregla dando mas alto -no lo hay- ni desplazando la rejilla, que
    //  esta es de LIENZO y se pinta con el dedo arrastrado: un arrastre
    //  vertical que a veces escribe una nota y a veces mueve la pagina es un
    //  gesto que no se puede aprender. Se arregla enseñando MENOS a la vez, y
    //  la unidad en la que se enseña menos es la octava, no un numero redondo.
    //
    //  Y sale gratis dos veces mas. Con trece filas el boton de OCTAVA embaldosa
    //  el rango entero sin huecos ni sobras: -24..-12, -12..0, 0..12, 12..24,
    //  que es exactamente lo que setStepNote admite. Con veinticinco, la base
    //  en +12 dibujaba hasta +36 y las doce filas de arriba escribian notas que
    //  el motor recortaba a +24 — doce filas que mentian.
    //  Y CUANTAS SE VEN LO ELIGE QUIEN MIRA. Trece es una octava y su raiz y es
    //  lo que hace que una nota se pueda colocar; veinticinco son dos octavas y
    //  sirven para VER una melodia entera de un vistazo, que es la otra cosa
    //  que se le pide a un piano roll. Las dos cuentas tienen sentido y ninguna
    //  gana siempre: en un movil grande veinticinco filas siguen dando 17 px y
    //  eso se lee, aunque no se acierte comodamente.
    //
    //  Trece y veinticinco y nada en medio, porque el boton de OCTAVA mueve de
    //  doce en doce: con cualquier otra cuenta la ventana deja huecos o solapa,
    //  y una fila que no se puede alcanzar desde ningun paso del boton es una
    //  fila que miente. Con trece: -24..-12, -12..0, 0..12, 12..24. Con
    //  veinticinco: -24..0 y 0..24.
    static constexpr int kFilasMin = 13;   // una octava y su raiz
    static constexpr int kFilasMax = 25;   // dos octavas y su raiz
    //  LO QUE SE ELIGE Y LO QUE CABE SON DOS NUMEROS, Y ERAN UNO.
    //
    //  `filas` valia trece o veinticinco y nada mas, asi que la ficha pedia
    //  `filas * kAltoObjetivo` pasara lo que pasara y `sheetFromBottom`
    //  recortaba con un `jmin` que no se queja: en 640x360 la tarjeta es
    //  588x324, la pagina pedia 588 y las filas salian a CATORCE pixeles; en
    //  412x480 pedia 672 -720 con la tira de seleccion- sobre 368. Catorce es
    //  por debajo del suelo de dieciseis que el banco ya juzga: la nota que
    //  pones no es la que querias.
    //
    //  Ahora `filasPedidas` es la eleccion -la que guarda el fichero de
    //  preferencias y la que dice el rotulo de VER- y `tope` lo que el
    //  maquetado ha medido que cabe a `kAltoMin` por fila. `filas` es el
    //  minimo de los dos, que es lo unico que se dibuja. Ensenar menos filas
    //  no es un estado nuevo: la ventana vertical continua existe desde que
    //  `BarraVista` recorre `pianoBase` semitono a semitono.
    void setFilas (int n) { filasPedidas = (n >= kFilasMax) ? kFilasMax : kFilasMin; ajusta(); }
    //  CUANTAS CABEN, que lo dice quien maqueta y no esto. `jmax (1, ...)`
    //  porque una tarjeta de cero alto existe -es el primer `resized` antes de
    //  que la ventana tenga tamaño- y una rejilla de cero filas divide entre
    //  cero tres lineas mas abajo.
    void acota (int caben) { tope = juce::jmax (1, caben); ajusta(); }
    int  getFilas() const noexcept { return filas; }
    //  La ELECCION, no lo que se ve. La guarda el fichero de preferencias y la
    //  lee el rotulo de VER: con `getFilas()` en una pantalla donde el tope
    //  manda, la tapa diria «2 OCT» estando en once filas y la preferencia se
    //  escribiria con un numero que nadie pidio.
    int  getFilasPedidas() const noexcept { return filasPedidas; }
    //  El semitono mas alto que puede quedar abajo, para que el de arriba caiga
    //  clavado en el +24 que setStepNote admite y ni uno mas.
    int  baseMax()  const noexcept { return 24 - (filas - 1); }
    //  Lo que la ficha PIDE por fila. No es un suelo -la rejilla se queda con
    //  lo que sobre y quien manda es el alto de la tarjeta- sino el objetivo:
    //  trece por 34 son 442 px, que es lo que un movil grande puede dar
    //  (450 medidos en 412x915). Pedir los 40 del dedo serian 520 y ninguna
    //  pantalla los tiene, asi que la tarjeta quedaria clavada en su tope en
    //  las siete y el numero dejaria de decir nada.
    static constexpr int kAltoObjetivo = 34;
    //  Y EL SUELO, QUE NO ES EL MISMO NUMERO Y HASTA AHORA NO EXISTIA.
    //
    //  `kAltoObjetivo` es un DESEO -lo dice el parrafo de arriba: «no es un
    //  suelo ... quien manda es el alto de la tarjeta»- y la ficha lo pedia
    //  entero pasara lo que pasara. Mientras la tarjeta dio para tanto, daba
    //  igual; con las dos pantallas que el barrido gano -640x360 y 412x480- la
    //  tarjeta es 588x324 y 379x368, la ficha pedia 588 y 672, y
    //  `sheetFromBottom` recorta con un `jmin` que no se queja: las filas
    //  salian a CATORCE pixeles.
    //
    //  Dieciseis es el suelo que el banco ya juzga para una fila de nota
    //  -`MIN_NOTE` de Tests/expo.py- y no un numero nuevo: poner una nota a
    //  ojo en catorce pixeles es escribir la de al lado, que es exactamente la
    //  queja que bajo las filas de veinticinco a trece. Pedir el deseo cuando
    //  cabe y el suelo cuando no es lo que separa «pide de mas y que lo recorte
    //  otro» de «pide lo que va a colocar».
    static constexpr int kAltoMin = 16;
    //  VEINTISEIS Y NO TREINTA Y CUATRO. La columna del teclado sale del ancho
    //  de la rejilla, asi que cada pixel suyo es un pixel que no tiene la
    //  casilla del paso: en el Fold cerrado -225 px de tarjeta- con 34 la
    //  columna de un paso quedaba en 11.9, por debajo del suelo de 12 que ya
    //  cumple la rejilla de pasos en esa misma pantalla. Con 26 son 12.4, y en
    //  26 px sigue cabiendo el nombre de la octava, que es lo unico que se
    //  escribe ahi y solo en las filas de DO.
    static constexpr int kGutter   = Metrics::canalPiano;   // la columna del teclado
    //  RAIZ MAS SIETE. Eran cuatro en total y la queja fue literal: "solo se
    //  pueden poner cuatro notas en el mismo acorde, no podemos estar tan
    //  limitados". El tope lo ponia la celda del motor -ver
    //  AudioEngine::kExtraNotes- y no la rejilla; al ensancharla a 64 bits
    //  caben ocho. El numero NO se repite a mano: MainComponent tiene un
    //  static_assert que ata este a kExtraNotes + 1, porque un tope escrito
    //  dos veces son dos topes y el dia que uno suba sin el otro la rejilla
    //  se traga notas que el motor si guarda.
    static constexpr int kMaxNotas = 8;

    //  (paso, semitono) — quien la usa decide si pone o quita.
    //  Con el gesto al lado, por lo mismo que `StepGrid::onCell`: un arrastre
    //  escribe una nota por columna y deshacer tiene que desandar el dedo
    //  entero y no la ultima casilla.
    std::function<void (int paso, int semi, bool arrastrando)> onCelda;
    //  Un toque en el teclado: suena esa nota sin escribir nada, que es como
    //  se busca una melodia antes de escribirla.
    std::function<void (int semi)> onTecla;

    //  EL LARGO DE UNA NOTA, en cuartos de paso. Arrastrar por la MISMA fila
    //  desde una nota la estira; arrastrar cambiando de fila sigue pintando
    //  notas, que es como se escribe un acorde o una escalera. Dos gestos que
    //  no se pisan porque uno es horizontal y el otro no.
    std::function<void (int paso, int semi, int cuartos)> onLargo;

    //  LA HERRAMIENTA. Dibujar es lo normal; la GOMA borra lo que toca -sin
    //  alternar, que arrastrar sobre notas puestas y quitadas encendia unas y
    //  apagaba otras- y las TIJERAS cortan la nota por donde se tocan, que es
    //  lo unico que "cortar" puede significar cuando el largo es del paso.
    //  Y EL LAPIZ, que es la goma por el otro lado: escribe y no alterna. La
    //  pregunta la contesta quien tiene los datos - pianoCellToggled - porque
    //  aqui no se sabe que hay escrito en una casilla.
    //  Y LA SELECCION, que es la QUINTA herramienta y no un gesto nuevo.
    //
    //  Esta rejilla es de LIENZO y se pinta con el dedo arrastrado, y arrastrar
    //  ya significa dos cosas: pintar notas cambiando de fila y estirar el
    //  largo por la misma fila. Meter «seleccionar» y «mover» encima serian
    //  CUATRO significados en un dedo, que es lo que esta casa lleva escrito
    //  que no se puede aprender. Con un modo el gesto es inequivoco, y sin el
    //  modo puesto el piano se comporta exactamente igual que antes.
    enum Herramienta { dibujar = 0, lapiz, goma, tijeras, sel };
    void setHerramienta (int h) { util = juce::jlimit (0, 4, h); }
    int  getHerramienta() const { return util; }

    //  Cuantas columnas se estan dibujando. Lo pide el banco para no repetir la
    //  constante: una prueba que lee el numero que ella misma se da cambia de
    //  opinion a la vez que el fallo.
    int  numPasos() const noexcept { return nPasos; }

    //  Borrar y cortar los resuelve quien tiene los datos, igual que onCelda.
    std::function<void (int paso, int semi)> onBorrar;
    std::function<void (int paso, int cuartos)> onCortar;

    //  LA SELECCION LA GUARDA QUIEN TIENE LOS DATOS, no este componente: aqui
    //  solo se dibuja y se reporta el gesto. Es el mismo reparto que el resto
    //  de la casa - el componente sabe geometria, el anfitrion sabe que hay
    //  escrito - y el que hizo que la tabla de zatis se pase en vez de
    //  preguntarle al motor por cada bloque.
    //
    //  La banda va en (paso, semi) y no en pixeles: quien la recibe no tiene
    //  por que saber cuanto mide una celda.
    std::function<void (int paso0, int semi0, int paso1, int semi1)> onBanda;
    std::function<void (int dPaso, int dSemi)> onMueveSel;
    std::function<void()> onVaciaSel;
    //  Si una celda esta seleccionada. Lo contesta el anfitrion porque es quien
    //  tiene el conjunto; aqui hace falta para dibujarla y para saber si un
    //  arrastre empieza DENTRO de la seleccion, que es lo que separa mover de
    //  volver a seleccionar.
    std::function<bool (int paso, int semi)> estaSel;

    //  LOS GESTOS DE LA REGLA, en columnas de la ventana como todo lo demas:
    //  arrastrar da un tramo -las dos columnas, en el orden del dedo- y tocar
    //  sin arrastrar deja el cursor. Que el tramo seleccione TODAS las notas
    //  de esas columnas, esten o no en la octava que se ve, lo decide quien
    //  tiene los datos; aqui no se sabe que hay escrito.
    std::function<void (int col0, int col1)> onTramo;
    std::function<void (int col)> onCursor;
    //  EL PELLIZCO: +1 abre -menos columnas, celdas mas anchas- y -1 cierra.
    //  Cuantas columnas son «una mas» lo decide el anfitrion, que es quien
    //  sabe cuantas caben por el suelo de la celda; esto solo cuenta dedos.
    std::function<void (int dir)> onZoom;
    //  El segundo dedo llega DESPUES del primero, y el primero ya ha tocado la
    //  rejilla como un toque de verdad: con el lapiz armado ha escrito una
    //  nota. Un pellizco que deja una nota detras es un pellizco que ensucia,
    //  asi que al empezar se avisa y el anfitrion deshace ese toque -y solo
    //  ese: el decide, que es quien tiene la pila.
    std::function<void()> onDeshaceToque;

    //  Lo que se dibuja en la regla, puesto por quien lo guarda. -1 es nada.
    //  Un tramo que empieza antes de la ventana llega con c0 negativo y se
    //  recorta al pintar; «ninguno» es c1 < c0, que es como lo manda quien lo
    //  guarda (0, -1).
    void ponTramo (int c0, int c1)
    {
        const bool hay = c1 >= c0;
        const int a = hay ? c0 : -1, b = hay ? c1 : -1;
        if (a == tramo0 && b == tramo1) return;
        tramo0 = a; tramo1 = b;
        repaint();
    }
    void ponCursor (int c) { if (c == cursor) return; cursor = c; repaint(); }
    int  getCursor() const noexcept { return cursor; }

    //  `notas` trae kMaxNotas semitonos por paso; -128 es "ninguna". `pasos`
    //  es cuantas columnas se dibujan, `base` el semitono de la fila de abajo.
    //
    //  Y `desdePaso`, QUE ES EL PASO DEL PATRON DE LA PRIMERA COLUMNA. Sin el,
    //  la rejilla de fondo se tenia que teñir por la COLUMNA -`c % 4`- y esa
    //  cuenta solo coincide con el pulso cuando la ventana arranca en un
    //  multiplo de cuatro. `StepGrid` lo recibe desde el dia que la ventana es
    //  continua y su parrafo lo cuenta entero: «empezando en el paso 3, una
    //  linea cada cuatro columnas cae en 7 y 11, o sea marca el contratiempo y
    //  borra el pulso». El piano no lo recibio nunca — las notas se desplazan
    //  con la barra y la cuadricula se quedaba quieta, que es exactamente la
    //  queja: «se desplazan las notas pero no las cuadriculas, con lo que puede
    //  dar a confundirse donde pone uno las notas siguiendo los pasos».
    void setSource (const signed char* notas, int pasos, int base,
                    int pasoTocando, int zati, float fase = 0.0f,
                    const unsigned char* largos = nullptr, int desdePaso = 0)
    {
        datos = notas; nPasos = juce::jmax (1, pasos); semiBase = base;
        primerPaso = juce::jmax (0, desdePaso);
        cuartos = largos;
        tocando = pasoTocando; color = zati;
        faseAct = juce::jlimit (0.0f, 1.0f, fase);

        //  REPINTAR SOLO SI HA CAMBIADO ALGO, por lo mismo que las otras dos
        //  rejillas: esto se llama en cada tick del temporizador y la ficha
        //  que lo contiene ocupa la ventana entera con un velo encima.
        const size_t n = (size_t) nPasos * (size_t) kMaxNotas;
        const bool largosIguales = sombraLargos.size() == (size_t) nPasos
                                && (cuartos == nullptr
                                      ? std::all_of (sombraLargos.begin(), sombraLargos.end(),
                                                     [] (unsigned char v) { return v == 0; })
                                      : std::memcmp (sombraLargos.data(), cuartos,
                                                     (size_t) nPasos * sizeof (unsigned char)) == 0);
        //  Y `primerPaso` ENTRA EN LA COMPARACION, que es la trampa de este
        //  atajo: sin el, arrastrar la barra por una zona VACIA no repinta -las
        //  256 celdas salen identicas- y la cuadricula se queda donde estaba.
        //  O sea el mismo fallo mudado de sitio.
        bool igual = datos != nullptr && visto
                  && nPasos == prevPasos && semiBase == prevBase
                  && primerPaso == prevPrimer
                  && tocando == prevTocando && color == prevColor
                  && std::abs (faseAct - prevFase) < 0.004f
                  && sombra.size() == n
                  && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0
                  && largosIguales;
        if (igual) return;

        const auto antes = marcaDe (prevTocando);
        const bool soloCabezal = visto && datos != nullptr
                              && nPasos == prevPasos && semiBase == prevBase
                              && primerPaso == prevPrimer
                              && color == prevColor
                              && sombra.size() == n
                              && std::memcmp (sombra.data(), datos, n * sizeof (signed char)) == 0
                              && largosIguales;

        sombra.resize (n);
        if (datos != nullptr) std::memcpy (sombra.data(), datos, n * sizeof (signed char));
        sombraLargos.assign ((size_t) nPasos, 0);
        if (cuartos != nullptr)
            std::memcpy (sombraLargos.data(), cuartos, (size_t) nPasos * sizeof (unsigned char));
        prevPasos = nPasos; prevBase = semiBase; prevTocando = tocando;
        prevPrimer = primerPaso;
        prevColor = color; prevFase = faseAct;
        const bool primera = ! visto;
        visto = true;

        if (soloCabezal && ! primera)
        {
            auto zona = antes.getUnion (marcaDe (tocando));
            if (! zona.isEmpty()) { repaint (zona); return; }
        }
        repaint();
    }

    //  Donde cae la marca del paso que suena. La columna entera mas seis
    //  pixeles a cada lado, que es donde puede caer para cualquier fase.
    juce::Rectangle<int> marcaDe (int paso) const
    {
        const auto r = getLocalBounds();
        if (r.isEmpty() || paso < 0 || paso >= prevPasos) return {};
        const float ancho = (float) (r.getWidth() - kGutter) / (float) juce::jmax (1, prevPasos);
        const float x = (float) r.getX() + (float) kGutter + ancho * (float) paso;
        return juce::Rectangle<float> (x - 6.0f, (float) r.getY(), ancho + 12.0f, (float) r.getHeight())
                 .getSmallestIntegerContainer().getIntersection (r);
    }

    static bool esNegra (int semi)
    {
        const int n = ((semi % 12) + 12) % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    //  El nombre de la nota, con la raiz del pad como DO. No es la afinacion
    //  real de la muestra -eso no lo sabe nadie- sino la distancia a ella, que
    //  es lo unico que el motor entiende y lo unico que hace falta para tocar
    //  un acorde: la forma es la misma en cualquier tonalidad.
    static juce::String nombreDe (int semi)
    {
        static const char* kNombres[12] = { "C", "C#", "D", "D#", "E", "F",
                                            "F#", "G", "G#", "A", "A#", "B" };
        const int n = ((semi % 12) + 12) % 12;
        const int oct = (int) std::floor ((double) semi / 12.0);
        return juce::String (kNombres[n]) + (oct == 0 ? juce::String()
                                                      : juce::String (oct > 0 ? "+" : "") + juce::String (oct));
    }

    void paint (juce::Graphics& g) override
    {
        if (datos == nullptr) return;

        auto r = getLocalBounds();
        const float altoFila = (float) altoRejilla() / (float) filas;
        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const auto tinta = Zati::colour (color);
        //  La rejilla empieza debajo de la regla; `r` sigue siendo el todo
        //  porque el cabezal y el cursor cruzan las dos bandas.
        const float yRej = (float) r.getY() + (float) kRegla;

        pintaRegla (g, r, anchoCol);

        for (int f = 0; f < filas; ++f)
        {
            //  La fila de arriba es la nota mas AGUDA: un piano roll se lee
            //  como un pentagrama, con lo alto arriba. Dibujarlo al reves es
            //  lo primero que hace que nadie entienda la pantalla.
            const int semi = semiBase + (filas - 1 - f);
            const float y  = yRej + altoFila * (float) f;
            const bool negra = esNegra (semi);

            //  EL TECLADO. Negras negras y blancas blancas, que es lo unico
            //  que orienta la vista sin contar filas desde abajo.
            auto tecla = juce::Rectangle<float> ((float) r.getX(), y, (float) kGutter, altoFila)
                             .reduced (1.0f, 0.5f);
            g.setColour (negra ? ZatiColours::groove (0.72f)
                               : ZatiColours::markOn (ZatiColours::chassisTop, 0.10f));
            g.fillRect (tecla);

            //  El DO de cada octava lleva su nombre SIEMPRE; las blancas, su
            //  letra cuando la fila da alto para leerla.
            //
            //  Aqui solo se rotulaba el DO, con su razon escrita: «veinticinco
            //  rotulos en una columna de 34 px es una mancha». El argumento es
            //  del ANCHO y vale para las NEGRAS —«C#» son dos signos y un
            //  sostenido a ese cuerpo de letra es un borron— pero no para las
            //  blancas, que son UNA letra: C D E F G A B. En 34 px una letra
            //  cabe de sobra.
            //
            //  Lo que costaba no decirlo: con dos octavas a la vista habia TRES
            //  referencias en veinticinco filas -C-1, C y C+1- asi que para
            //  saber donde cae un FA habia que contar filas desde el DO. Quien
            //  ya produce las cuenta sin pensar; quien no, no sabe que se
            //  cuentan.
            //
            //  Y SE PREGUNTA POR EL ALTO DE LA FILA, no por cuantas octavas se
            //  ven: con cuatro octavas la fila baja de ocho pixeles y ahi la
            //  letra si seria la mancha que el comentario de antes describia.
            //  El liston es el cuerpo de letra con su interlinea, o sea lo que
            //  hace falta para LEERLA, derivado y no escrito.
            const bool cabeLetra = altoFila >= (float) Metrics::fTiny * 1.35f;
            if (((semi % 12) + 12) % 12 == 0 || (! negra && cabeLetra))
            {
                g.setColour (ZatiColours::textOn (negra ? ZatiColours::groove (0.72f)
                                                        : ZatiColours::chassisTop));
                g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
                //  El DO dice ademas su octava -es la referencia- y las otras
                //  seis solo su letra: repetir el numero en las siete es
                //  gastar la columna en decir siete veces lo mismo.
                const auto txt = (((semi % 12) + 12) % 12 == 0)
                                   ? nombreDe (semi)
                                   : nombreDe (semi).substring (0, 1);
                g.drawText (txt, tecla, juce::Justification::centred);
            }

            for (int c = 0; c < nPasos; ++c)
            {
                const float x = (float) r.getX() + (float) kGutter + anchoCol * (float) c;
                auto celda = juce::Rectangle<float> (x, y, anchoCol, altoFila).reduced (0.8f);

                //  El hueco de una fila negra se hunde un poco mas: es la
                //  misma pista que da el teclado, repetida a lo ancho para que
                //  no haya que mirar a la izquierda en cada nota.
                //  El pulso se tiñe por el PASO y no por la columna: son la
                //  misma cuenta solo cuando la ventana arranca en un borde de
                //  compas, y desde que hay barra de arrastre eso deja de estar
                //  garantizado.
                const bool pulso = (((primerPaso + c) % 4) == 0);
                g.setColour (ZatiColours::groove (negra ? 0.34f : (pulso ? 0.26f : 0.16f)));
                g.fillRect (celda);

                bool puesta = false;
                for (int k = 0; k < kMaxNotas; ++k)
                    if (datos[c * kMaxNotas + k] != -128 && (int) datos[c * kMaxNotas + k] == semi)
                        { puesta = true; break; }

                if (puesta)
                {
                    //  UNA NOTA ES UNA BARRA, no un cuadrado. El largo se
                    //  guarda en cuartos de paso, asi que una nota puede ocupar
                    //  cuatro casillas o un cuarto de una: sin esto, todo lo
                    //  que se escribe aqui dura lo mismo y da igual lo que
                    //  ponga en el motor - "no se ve" y "no esta" se parecen
                    //  demasiado.
                    const int cu = (cuartos != nullptr) ? (int) cuartos[c] : 0;
                    const float anchoNota = cu > 0
                                              ? juce::jmax (3.0f, anchoCol * (float) cu * 0.25f)
                                              : celda.getWidth();
                    auto barra = celda.withWidth (juce::jmin (anchoNota,
                                                             (float) r.getRight() - celda.getX()));
                    g.setColour (tinta);
                    g.fillRect (barra);
                    g.setColour (ZatiColours::ink.withAlpha (0.35f));
                    g.drawRect (barra, Metrics::filo);
                    //  Y el ARRANQUE marcado, que en una barra de cuatro
                    //  casillas es lo unico que dice donde empieza la nota.
                    if (cu > 4)
                    {
                        g.setColour (ZatiColours::ink.withAlpha (Metrics::alfaSeccion));
                        g.fillRect (barra.withWidth (2.0f));
                    }

                    //  Y LO SELECCIONADO SE VE. Una seleccion que no se dibuja
                    //  no es una seleccion: mover en bloque sin saber que
                    //  bloque se mueve es mover a ciegas. Con la misma marca
                    //  que el clip elegido de la banda de audio - el color de
                    //  cabezal - para no inventar un tercer idioma.
                    //
                    //  SE TIÑE LA NOTA, no se le pone un anillo alrededor.
                    //
                    //  Eran dos anillos de 1.4 y 1.6 px, y llego del telefono
                    //  que no se ve: «esas notas se colorearan temporalmente
                    //  hasta dejar de estar seleccionadas». Tiene su cifra —en
                    //  412x915 con dos octavas una fila de nota mide **15 px**
                    //  y una nota de un paso **21 px de ancho**, asi que tres
                    //  pixeles de anillo por lado son mas de la mitad de la
                    //  barra: lo que se veia era el anillo, no la nota, y dos
                    //  notas vecinas seleccionadas se leian como un bloque
                    //  rayado. Rellena, la marca crece con la nota en vez de
                    //  competir con ella.
                    //
                    //  Y ENCIMA DEL COLOR DEL ZATI, no en su lugar: el tono
                    //  dice de que pad es la nota y eso no se puede perder por
                    //  estar seleccionada. Se mezcla, asi que una nota
                    //  seleccionada sigue siendo reconociblemente suya.
                    if (estaSel != nullptr && estaSel (c, semi))
                    {
                        g.setColour (ZatiColours::playhead.withAlpha (0.55f));
                        g.fillRect (barra);
                        g.setColour (ZatiColours::playhead);
                        g.drawRect (barra, Metrics::filo);
                    }
                }
            }
        }

        //  Y LAS LINEAS DE COMPAS, que es la mitad que de verdad se pidio.
        //
        //  El tinte de una celda vacia es un matiz -0.26 contra 0.16- y a
        //  quince pixeles de fila eso no se cuenta de un vistazo; una linea es
        //  una REFERENCIA. `StepGrid` las lleva desde que su ventana es
        //  continua y el piano se quedo sin ellas, que es la otra mitad de
        //  «puede dar a confundirse donde pone uno las notas siguiendo los
        //  pasos de los beat». Caen en los pasos multiplos de cuatro DEL
        //  PATRON, por lo mismo que el tinte.
        //  `sinCompases` es una entrada del BANCO y no un ajuste: la unica
        //  forma exacta de medir estas lineas es pintar la rejilla dos veces y
        //  restar, porque no hay una sola fila de la imagen donde no pinte
        //  nada mas. Ver `auditPiano`.
        if (! sinCompases)
        {
            g.setColour (ZatiColours::groove (0.28f));
            for (int c = 1; c < nPasos; ++c)
                if (((primerPaso + c) % 4) == 0)
                    g.fillRect ((float) r.getX() + (float) kGutter + anchoCol * (float) c - 0.5f,
                                yRej, 1.0f, (float) altoRejilla());
        }

        //  EL TRAMO SOBRE LA REJILLA, un velo del color del cabezal: dice
        //  «estas columnas» por encima de notas y huecos, que es lo que un
        //  tramo de tiempo es. La regla lo lleva mas denso, ver pintaRegla.
        if (tramo1 >= tramo0 && tramo1 >= 0 && tramo0 < nPasos)
        {
            const int a = juce::jmax (tramo0, 0), b = juce::jmin (tramo1, nPasos - 1);
            g.setColour (ZatiColours::playhead.withAlpha (0.10f));
            g.fillRect ((float) r.getX() + (float) kGutter + anchoCol * (float) a, yRej,
                        anchoCol * (float) (b - a + 1), (float) altoRejilla());
        }

        //  Y EL CURSOR DE PEGADO, una linea del acento en el borde izquierdo
        //  de su columna, de la regla al suelo: ahi cae lo que se pegue.
        if (cursor >= 0 && cursor < nPasos)
        {
            const float xc = (float) r.getX() + (float) kGutter + anchoCol * (float) cursor;
            g.setColour (ZatiColours::accent.withAlpha (0.9f));
            g.fillRect (xc - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
        }

        //  El cabezal, encima de todo y en su color.
        if (tocando >= 0 && tocando < nPasos)
        {
            const float x = (float) r.getX() + (float) kGutter + anchoCol * (float) tocando;
            g.setColour (ZatiColours::groove (0.14f));
            g.fillRect (x, (float) r.getY(), anchoCol, (float) r.getHeight());
            const float xx = x + anchoCol * faseAct;
            g.setColour (ZatiColours::playheadEdge);
            g.fillRect (xx - 2.0f, (float) r.getY(), 4.0f, (float) r.getHeight());
            g.setColour (ZatiColours::playhead);
            g.fillRect (xx - 1.0f, (float) r.getY(), 2.0f, (float) r.getHeight());
        }

        //  Y LA HERRAMIENTA ARMADA, VISTA DONDE SE ACTUA.
        //
        //  LAPIZ, GOMA, TIJERAS y SEL cambian lo que hace arrastrar, y hasta
        //  hoy eso solo se sabia mirando la fila de tapas: la encendida esta
        //  arriba y el dedo esta aqui. Un marco del color del acento alrededor
        //  del LIENZO —no de la ficha— dice «esta rejilla esta en un modo»
        //  justo donde cae el dedo. Sin herramienta no hay marco: dibujar es lo
        //  que la rejilla hace de por si, y un aviso permanente no avisa.
        if (util != dibujar)
        {
            auto marco = r.toFloat().reduced (0.75f);
            marco.setLeft (marco.getX() + (float) kGutter);
            marco.setTop (marco.getY() + (float) kRegla);
            g.setColour (ZatiColours::accent.withAlpha (0.9f));
            g.drawRect (marco, Metrics::filo);
        }
    }

    //  CADA DEDO LLEGA COMO UN RATON DISTINTO. JUCE en Android no entrega
    //  `mouseMagnify`: cada dedo es un MouseInputSource con su indice y sus
    //  propios mouseDown/mouseDrag/mouseUp. Es el mismo reparto que
    //  `WaveformDisplay` lleva desde que la onda se pellizca, y por lo mismo
    //  pasa por `toque`: un gesto que no se puede llamar es un gesto que no se
    //  mide, y el banco pellizca con dos indices y sin pantalla tactil.
    void mouseDown (const juce::MouseEvent& e) override { toque (e.source.getIndex(), (float) e.x, (float) e.y, 0); }
    void mouseDrag (const juce::MouseEvent& e) override { toque (e.source.getIndex(), (float) e.x, (float) e.y, 1); }
    void mouseUp   (const juce::MouseEvent& e) override { toque (e.source.getIndex(), (float) e.x, (float) e.y, 2); }

    //  UN DEDO, TRES FASES: 0 baja, 1 se mueve, 2 se levanta.
    //
    //  Con UN dedo esto es `gesto` y `suelta` de siempre, sin cambiar una coma
    //  de lo que hace. El SEGUNDO dedo convierte el gesto en pellizco: lo que
    //  el primero hubiera empezado se cancela -y si ya habia escrito, se avisa
    //  para deshacerlo-, y desde ahi solo se mira la distancia entre los dos.
    //  Al levantar uno el pellizco acaba, y el dedo que queda NO vuelve a
    //  escribir: un dedo que se queda en la pantalla tras abrir no esta
    //  poniendo una nota, esta terminando de abrir.
    //
    //  El umbral es una RAZON y no pixeles -una vez y media- porque un paso del
    //  zoom es el doble o la mitad de columnas; pedir el doble exacto deja el
    //  gesto corto en un movil, y pedir cualquier cosa lo dispara al apoyar.
    //  Al saltar se vuelve a medir desde ahi, para que un pellizco largo de
    //  cuatro columnas pueda llegar a sesenta y cuatro.
    void toque (int id, float x, float y, int fase)
    {
        if (fase == 0)
        {
            dedoBaja (id, x, y);
            if (dedos() >= 2)
            {
                if (! pellizco)
                {
                    pellizco = true;
                    spanBase = juce::jmax (8.0f, span());
                    if (escribio && onDeshaceToque) onDeshaceToque();
                    //  Y sin dejar cursor: un pellizco que empieza en la
                    //  regla no es un toque en la regla.
                    enRegla = false;
                    suelta();
                    escribio = false;
                }
                return;
            }
            if (ignoraResto) return;
            escribio = false;
            gesto (x, y, false);
            return;
        }

        if (fase == 1)
        {
            dedoMueve (id, x, y);
            if (pellizco)
            {
                if (dedos() < 2) return;
                const float razon = span() / juce::jmax (8.0f, spanBase);
                if (razon >= 1.5f)        { if (onZoom) onZoom (+1); spanBase = span(); }
                else if (razon <= 1.0f / 1.5f) { if (onZoom) onZoom (-1); spanBase = span(); }
                return;
            }
            if (ignoraResto) return;
            gesto (x, y, true);
            return;
        }

        dedoSube (id);
        if (pellizco)
        {
            if (dedos() < 2) { pellizco = false; ignoraResto = dedos() > 0; }
            return;
        }
        if (ignoraResto) { if (dedos() == 0) ignoraResto = false; return; }
        suelta();
    }
    //  Cuantos dedos hay apoyados. Lo lee el banco para probar que el
    //  pellizco empieza con dos y acaba con menos.
    int dedos() const noexcept
    {
        int n = 0;
        for (const auto& t : toques) if (t.abajo) ++n;
        return n;
    }

    //  EL GESTO, en pixeles y sin MouseEvent, para que el banco pueda medirlo.
    //
    //  Los cinco fallos del compas se midieron por `onCelda`, que es el
    //  callback: eso salta justo el codigo que decide QUE celda es, que es
    //  donde vive el arrastre. Un gesto que no se puede llamar es un gesto que
    //  no se mide - y el arrastre que cambiaba de fila escribia una nota por
    //  cada fila por la que pasaba el dedo sin que ninguna regla lo viera.
    void gesto (float x, float y, bool arrastrando)
    {
        if (datos == nullptr) return;
        auto r = getLocalBounds();
        const float altoFila = (float) altoRejilla() / (float) filas;
        const int fila = juce::jlimit (0, filas - 1,
                                       (int) ((y - (float) r.getY() - (float) kRegla) / altoFila));
        const int semi = semiBase + (filas - 1 - fila);

        //  LA REGLA, ANTES QUE NADA Y CON CUALQUIER HERRAMIENTA. Un arrastre
        //  que empieza en la regla es un tramo aunque el dedo baje a la
        //  rejilla, y uno que empieza en la rejilla no se vuelve tramo por
        //  subir: el gesto es de donde se apoya el dedo. Tocar el teclado de la
        //  esquina no es nada.
        if (! arrastrando) enRegla = (y < (float) (r.getY() + kRegla));
        if (enRegla)
        {
            if (x < (float) (r.getX() + kGutter) && ! arrastrando) { enRegla = false; return; }
            const float anchoR = (float) (r.getWidth() - kGutter) / (float) nPasos;
            const int col = juce::jlimit (0, nPasos - 1,
                                          (int) ((x - (float) r.getX() - (float) kGutter) / anchoR));
            if (! arrastrando) { reglaIni = col; reglaMovio = false; return; }
            if (col == reglaIni && ! reglaMovio) return;
            reglaMovio = true;
            if (onTramo) onTramo (reglaIni, col);
            return;
        }

        //  EL TECLADO SUENA, no escribe. Buscar la nota antes de ponerla es la
        //  mitad de escribir una melodia, y sin esto habria que escribirla,
        //  oirla y borrarla.
        if (x < (float) (r.getX() + kGutter))
        {
            if (! arrastrando && onTecla) onTecla (semi);
            return;
        }

        const float anchoCol = (float) (r.getWidth() - kGutter) / (float) nPasos;
        const float dentro = (x - (float) r.getX() - (float) kGutter) / anchoCol;
        const int paso = juce::jlimit (0, nPasos - 1, (int) dentro);

        //  LA SELECCION, antes que el candado de fila: aqui arrastrar SI cruza
        //  filas, que es justo lo que una banda elastica tiene que hacer.
        //
        //  Tres respuestas y no dos, y la del medio es la que hace falta: si el
        //  arrastre empieza DENTRO de la seleccion se mueve el bloque entero, y
        //  si empieza fuera se selecciona de nuevo. Sin esa pregunta, mover
        //  seria un cuarto gesto y no habria forma de decir cual de los dos se
        //  quiso.
        if (util == sel)
        {
            if (! arrastrando)
            {
                pasoIni = paso; filaIni = fila; semiIni = semi;
                moviendo = (estaSel != nullptr && estaSel (paso, semi));
                if (! moviendo)
                {
                    //  Tocar fuera vacia: sin esto no habria forma de soltar la
                    //  seleccion sin seleccionar otra cosa.
                    if (onVaciaSel) onVaciaSel();
                    dPaso = dSemi = 0;
                }
                return;
            }

            if (moviendo)
            {
                //  EN DELTAS Y ACUMULADO, no en absolutos: quien mueve el
                //  bloque necesita cuanto se ha movido DESDE la ultima vez, o
                //  cada evento del raton lo desplazaria otra vez entero.
                const int nd = paso - pasoIni, ns = semi - semiIni;
                if (nd == dPaso && ns == dSemi) return;
                if (onMueveSel) onMueveSel (nd - dPaso, ns - dSemi);
                dPaso = nd; dSemi = ns;
                return;
            }

            if (onBanda) onBanda (pasoIni, semiIni, paso, semi);
            return;
        }

        //  ARRASTRAR NO CAMBIA DE FILA.
        //
        //  Estaba escrito al reves -"si el dedo cambia de fila se esta
        //  escribiendo un acorde o una escalera"- y desde el dedo eso es que
        //  bajar el dedo por la rejilla deja una nota en CADA fila por la que
        //  pasa. Un acorde se escribe levantando y volviendo a tocar, que son
        //  dos notas y dos toques; lo que no se puede es escribir cinco sin
        //  querer con un gesto.
        //
        //  Se pregunta antes que la herramienta, asi que la goma se queda
        //  tambien en su fila: borrar de mas es el mismo accidente.
        if (arrastrando && filaIni >= 0 && fila != filaIni) return;
        if (! arrastrando) { filaIni = fila; pasoIni = paso; ultimoLargo = -1; }

        //  LA GOMA borra y no alterna: pasar el dedo por encima de una fila con
        //  notas puestas y huecos encendia los huecos, que es lo contrario de
        //  borrar. Se repite por celda como el pintado, para no borrar la misma
        //  cincuenta veces por segundo.
        if (util == goma)
        {
            const int clave2 = fila * 1000 + paso;
            if (arrastrando && clave2 == ultima) return;
            ultima = clave2;
            if (onBorrar) { escribio = true; onBorrar (paso, semi); }
            return;
        }

        //  LAS TIJERAS cortan por donde se toca: el largo de la nota pasa a ser
        //  lo que va de su casilla hasta el dedo, en cuartos. Tocar dentro de la
        //  primera casilla la deja en un cuarto, que es lo mas corto que hay.
        if (util == tijeras)
        {
            if (arrastrando) return;
            if (onCortar)
            {
                //  Se busca hacia atras la casilla donde empieza la barra que se
                //  ha tocado: cortar por el dedo sin saber donde empieza la nota
                //  daria un largo medido desde el sitio equivocado.
                int ini = paso;
                for (int c = paso; c >= 0; --c)
                {
                    bool aqui = false;
                    for (int k = 0; k < kMaxNotas; ++k)
                        if (datos[c * kMaxNotas + k] != -128 && (int) datos[c * kMaxNotas + k] == semi)
                            { aqui = true; break; }
                    if (aqui) { ini = c; break; }
                }
                const int cu = juce::jlimit (1, 63, (int) ((dentro - (float) ini) * 4.0f) + 1);
                escribio = true;
                onCortar (ini, cu);
            }
            return;
        }

        if (! onCelda) return;

        //  ARRASTRAR POR LA MISMA FILA ES ESTIRAR LA NOTA.
        //
        //  Es el gesto de cualquier piano roll y no se pisa con el de pintar,
        //  porque uno es horizontal y el otro no: si el dedo cambia de fila se
        //  esta escribiendo un acorde o una escalera, y si se queda en la suya
        //  se esta diciendo cuanto dura la nota que se acaba de poner.
        if (arrastrando && fila == filaIni && pasoIni >= 0 && onLargo != nullptr)
        {
            const int cu = juce::jlimit (1, 63, (paso - pasoIni + 1) * 4);
            if (cu != ultimoLargo)
            {
                ultimoLargo = cu;
                escribio = true;
                onLargo (pasoIni, semi, cu);
            }
            return;
        }

        //  Un arrastre pinta, pero solo al ENTRAR en una celda nueva: moverse
        //  dentro de una la encenderia y apagaria varias veces por segundo.
        const int clave = fila * 1000 + paso;
        if (arrastrando && clave == ultima) return;
        ultima = clave;
        escribio = true;
        onCelda (paso, semi, arrastrando);
    }

    void suelta()
    {
        //  El toque en la regla se resuelve al SOLTAR: hasta entonces no se
        //  sabe si iba a ser un tramo. Un dedo que no se movio es el cursor.
        if (enRegla && ! reglaMovio && reglaIni >= 0 && onCursor) onCursor (reglaIni);
        enRegla = false; reglaIni = -1; reglaMovio = false;
        ultima = -1; filaIni = pasoIni = -1; ultimoLargo = -1; moviendo = false; dPaso = dSemi = 0;
    }

private:
    //  El arrastre de la seleccion: donde empezo y cuanto lleva movido.
    int  semiIni = 0;
    bool moviendo = false;
    int  dPaso = 0, dSemi = 0;

    //  La regla: donde empezo el dedo y si se movio. Ver gesto / suelta.
    bool enRegla = false, reglaMovio = false;
    int  reglaIni = -1;
    //  Lo que se dibuja en la regla, en columnas de la ventana. Ver ponTramo.
    int  tramo0 = -1, tramo1 = -1, cursor = -1;

    //  Los dedos. Dos bastan: el tercero no hace nada en ningun sitio de la app.
    struct Toque { int id = -1; float x = 0.0f, y = 0.0f; bool abajo = false; };
    std::array<Toque, 2> toques {};
    bool  pellizco = false, ignoraResto = false, escribio = false;
    float spanBase = 0.0f;
    void dedoBaja (int id, float x, float y)
    {
        for (auto& t : toques) if (t.abajo && t.id == id) { t.x = x; t.y = y; return; }
        for (auto& t : toques) if (! t.abajo) { t = { id, x, y, true }; return; }
    }
    void dedoMueve (int id, float x, float y)
    {
        for (auto& t : toques) if (t.abajo && t.id == id) { t.x = x; t.y = y; return; }
    }
    void dedoSube (int id)
    {
        for (auto& t : toques) if (t.abajo && t.id == id) { t.abajo = false; t.id = -1; return; }
    }
    float span() const noexcept
    {
        const float dx = toques[0].x - toques[1].x, dy = toques[0].y - toques[1].y;
        return std::sqrt (dx * dx + dy * dy);
    }

    //  LA REGLA PINTADA: el pulso rotulado «compas.pulso» cada cuatro
    //  columnas, el compas solo cuando las cuatro no dan sitio al rotulo, el
    //  tramo denso y el cabezal cruzandola. Los numeros son del PATRON -salen
    //  de `primerPaso`-, por lo mismo que el tinte del pulso: una regla que
    //  empieza en 1 en cada ventana no es una regla.
    void pintaRegla (juce::Graphics& g, juce::Rectangle<int> r, float anchoCol)
    {
        auto banda = juce::Rectangle<float> ((float) r.getX(), (float) r.getY(),
                                             (float) r.getWidth(), (float) kRegla);
        g.setColour (ZatiColours::groove (0.40f));
        g.fillRect (banda);
        //  La esquina del teclado, del color de una tecla blanca: es la
        //  cabecera de las dos cabeceras.
        g.setColour (ZatiColours::markOn (ZatiColours::chassisTop, 0.10f));
        g.fillRect (banda.withWidth ((float) kGutter).reduced (1.0f, 1.0f));

        if (tramo1 >= tramo0 && tramo1 >= 0 && tramo0 < nPasos)
        {
            const int a = juce::jmax (tramo0, 0), b = juce::jmin (tramo1, nPasos - 1);
            g.setColour (ZatiColours::playhead.withAlpha (0.35f));
            g.fillRect ((float) r.getX() + (float) kGutter + anchoCol * (float) a, banda.getY(),
                        anchoCol * (float) (b - a + 1), banda.getHeight());
        }

        g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
        const float anchoPulso = anchoCol * 4.0f;
        //  «1.3» son tres signos a cuerpo pequeno: unos 22 px. Donde el pulso
        //  no los da, se rotula solo el compas, que es una cifra.
        const bool cabePulso = anchoPulso >= (float) Metrics::fTiny * 2.2f;
        for (int c = 0; c < nPasos; ++c)
        {
            const int paso = primerPaso + c;
            if (paso % 4 != 0) continue;
            const float x = (float) r.getX() + (float) kGutter + anchoCol * (float) c;
            const bool compas = (paso % 16) == 0;
            g.setColour (ZatiColours::inkDim.withAlpha (compas ? 0.9f : 0.5f));
            g.fillRect (x - 0.5f, banda.getBottom() - (compas ? 8.0f : 4.0f), 1.0f, compas ? 8.0f : 4.0f);
            if (! compas && ! cabePulso) continue;
            const juce::String txt = compas && ! cabePulso
                                       ? juce::String (paso / 16 + 1)
                                       : juce::String (paso / 16 + 1) + "." + juce::String ((paso % 16) / 4 + 1);
            g.setColour (compas ? ZatiColours::ink : ZatiColours::inkDim);
            g.drawText (txt, juce::Rectangle<float> (x + 2.0f, banda.getY(),
                                                     juce::jmax (anchoPulso - 3.0f, 10.0f), banda.getHeight() - 3.0f),
                        juce::Justification::centredLeft, false);
        }
        g.setColour (ZatiColours::groove (0.55f));
        g.fillRect (banda.getX(), banda.getBottom() - 1.0f, banda.getWidth(), 1.0f);
    }

    const signed char* datos = nullptr;
    //  Un largo por PASO, en cuartos: las notas de un acorde comparten casilla
    //  y comparten largo, que es lo que un acorde es.
    const unsigned char* cuartos = nullptr;
    std::vector<unsigned char> sombraLargos;
    int filas = kFilasMin;              // ver setFilas
    //  La eleccion y el tope que el maquetado midio. Ver setFilas / acota.
    int filasPedidas = kFilasMin, tope = kFilasMax;
    void ajusta()
    {
        const int n = juce::jlimit (1, kFilasMax, juce::jmin (filasPedidas, tope));
        if (n == filas) return;
        filas = n;
        repaint();
    }
    int nPasos = 16, semiBase = -12, tocando = -1, color = 0, ultima = -1;
    //  El paso del patron de la primera columna. Lo mismo que `StepGrid`
    //  llama asi, y por lo mismo.
    int primerPaso = 0;
    //  Solo el banco lo toca: con el puesto la rejilla sale sin sus lineas de
    //  compas, que es la referencia contra la que se restan.
public:
    bool sinCompases = false;
private:
    //  Donde empezo el arrastre, para saber si estira o pinta.
    int filaIni = -1, pasoIni = -1, ultimoLargo = -1;
    int util = 0;                       // 0 dibujar, 1 goma, 2 tijeras
    float faseAct = 0.0f;

    std::vector<signed char> sombra;
    int prevPasos = -1, prevBase = -99, prevTocando = -2, prevColor = -1;
    int prevPrimer = -1;
    float prevFase = -1.0f;
    bool visto = false;
};

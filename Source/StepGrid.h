#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include <array>
#include <cstring>

// ============================================================================
//  StepGrid — the whole beat at once: sixteen pad lanes down, sixteen steps of
//  one bar across.
//
//  The old grid showed the steps of ONE pad and never said which, so the thing
//  you actually compose — how the kick, the snare and the hats sit against each
//  other — was the one thing you could not see. Here every pad is a lane, and
//  an active step is filled with that pad's zati colour, so the pattern reads
//  as the same colours you already learned on the pads.
//
//  Sixty-four steps do not fit across a phone at a usable size, so the grid
//  pages by bar (16 steps) instead of shrinking cells to nothing. The bar
//  selector lives above it.
//
//  Drawn, not built from 256 buttons: one component paints the lot and hit-
//  tests on mouseDown, which keeps layout and repaints cheap.
// ============================================================================
class StepGrid : public juce::Component, public Rejilla
{
public:
    //  LO QUE EL BANCO NECESITA SABER de una rejilla que se pinta entera.
    //  Las tres cifras salen de aqui y no de un numero escrito en Python: las
    //  columnas dependen del zoom y del ancho, y los carriles de si la ventana
    //  ensena dieciseis pads o ocho.
    int celdasAncho() const override { return numCols(); }
    int celdasAlto()  const override { return getCarriles(); }
    int canalIzq()    const override { return kGutter; }
    float celdaAnchoPx() const override { return anchoCelda(); }
    float celdaAltoPx()  const override { return altoCelda(); }

    static constexpr int kLanes    = 16;   // pads
    //  UN COMPAS, que es lo que un paso "vale" y lo que la fila de compas
    //  contaba. Ya no es cuantas columnas se dibujan -eso lo decide la celda-
    //  pero sigue siendo la unidad en la que la vista salta y en la que el
    //  patron se mide.
    static constexpr int kBarSteps = 16;
    //  Lo mas ancho que esta ventana puede llegar a ser, que es el patron
    //  entero: AudioEngine::kNumSteps. Escrito aqui para no arrastrar el motor
    //  a un componente que solo dibuja, y comprobado donde se llenan las
    //  sombras -si un dia el patron crece, esto falla en la primera corrida en
    //  vez de leer fuera del array-.
    static constexpr int kMaxCols  = 64;
    //  La columna de los numeros de pad, a la izquierda de la rejilla. Estaba
    //  escrita a mano TRES veces - dos en el pintado y una en el acierto del
    //  toque - y esas tres tienen que decir lo mismo o los toques caen en una
    //  celda distinta de la que se ve. Un numero, un sitio.
    static constexpr int kGutter   = Metrics::canalPasos;

    //  CUANTAS PISTAS SE VEN A LA VEZ, y desde cual.
    //
    //  Dieciseis por dieciseis es la ficha, y por eso la celda se reparte lo
    //  que hay: medido, 19.8 x 17.9 px en un movil grande y 12.2 x 15.0 en el
    //  Fold cerrado. Por encima del suelo de 12 y muy por debajo de un dedo, y
    //  no hay alto que darle - son dieciseis carriles.
    //
    //  Asi que se ven OCHO y el alto se dobla, que es lo mismo que se hizo con
    //  el piano: no se agranda la celda, se enseña menos a la vez. Ocho y no
    //  seis ni diez porque un banco son dieciseis pads y la mitad de dieciseis
    //  se recorre con UN toque - la de arriba y la de abajo - sin que quede
    //  ningun pad al que no se llegue.
    //
    //  Los datos no se tocan: el espejo sigue siendo de dieciseis carriles y
    //  esto es una VENTANA encima. Un paso escrito en el pad 12 sigue sonando
    //  con la mitad de arriba a la vista, igual que un pad del banco C suena
    //  con el banco A en pantalla.
    void setVentana (int cuantos, int desde)
    {
        const int n = (cuantos >= kLanes) ? kLanes : kLanes / 2;
        const int d = (n >= kLanes) ? 0 : juce::jlimit (0, kLanes - n, desde);
        if (n == carriles && d == medio) return;
        carriles = n; medio = d;
        visto = false;                      // la sombra ya no describe lo que se ve
        repaint();
    }
    int getCarriles() const noexcept { return carriles; }
    int getMedio()    const noexcept { return medio; }

    //  LA CELDA ES CUADRADA, Y EL LADO SALE DEL ALTO.
    //
    //  Los dos ejes se calculaban por separado y sin ninguna relacion entre
    //  ellos: `cellW = (w - 30) / 16` y `laneH = h / 16`. Medido, eso daba
    //  19.8 x 17.9 px en un movil grande y 12.2 x 15.0 en el Fold cerrado - una
    //  celda mas ancha que alta en una pantalla y mas alta que ancha en la
    //  otra, o sea que el blanco cambia de forma segun el telefono y el dedo
    //  tiene que aprender dos.
    //
    //  Con el lado sacado del ALTO entran los dieciseis pads lo mas altos que
    //  la tarjeta permite -que es lo que se pidio- y a lo ancho entran las que
    //  entren. Que sean menos de dieciseis no es un problema desde que hay
    //  barra: antes lo era, porque la unica forma de llegar al paso 15 era que
    //  estuviera dibujado.
    //
    //  Y EL ZOOM MULTIPLICA EL LADO, no lo sustituye. Asi el cuadrado se
    //  conserva en el paso de arranque de las dos ventanas -dieciseis carriles
    //  y ocho- sin escribir la regla dos veces: con ocho carriles el lado se
    //  dobla y el ancho va detras.
    void ponZoomAncho (float z)
    {
        const float nuevo = juce::jlimit (0.5f, 4.0f, z);
        if (std::abs (nuevo - zoomW) < 0.001f) return;
        zoomW = nuevo;
        visto = false;                      // la sombra ya no describe lo que se ve
        repaint();
    }
    float getZoomAncho() const noexcept { return zoomW; }

    //  El lado de una celda, en los dos ejes. Lo pide el banco: una prueba que
    //  repite la formula que juzga cambia de opinion a la vez que el fallo.
    float altoCelda()  const noexcept
    { return (float) getHeight() / (float) juce::jmax (1, carriles); }
    float anchoCelda() const noexcept
    { return juce::jmax (1.0f, altoCelda() * zoomW); }

    //  CUANTAS COLUMNAS SE VEN ENTERAS. La que asoma por el filo derecho se
    //  dibuja igual -es lo que dice que hay mas- pero no cuenta: la ventana que
    //  la barra recorre son las enteras, o el ultimo paso del patron quedaria
    //  siempre a medias.
    int numCols() const noexcept
    {
        const float cw = anchoCelda();
        return juce::jlimit (1, kMaxCols, (int) ((float) (getWidth() - kGutter) / cw));
    }

    //  Si a ese zoom la celda sigue por encima de su suelo. Es la pregunta que
    //  hace la escalera de la tapa, y se contesta con el ancho que la rejilla
    //  TIENE - no con el de la ventana.
    bool cabeZoom (float z) const noexcept
    { return getHeight() > 0 && altoCelda() * z >= (float) Metrics::celdaPaso; }

    // Called with the absolute step index (bar offset already applied).
    std::function<void (int pad, int step)> onCell;

    void setSource (const bool* cells,          // [step][pad] flattened, stride = kLanes
                    const int*  zati,           // per pad
                    const bool* loaded,         // per pad
                    const signed char* notes,   // [step][pad] semitone offset, same stride
                    int patternLength, int desdePaso, int playStep, int selectedPad,
                    float stepPhase = 0.0f, int firstPad = 0)
    {
        data = cells; zatiOf = zati; loadedOf = loaded; noteOf = notes;
        patLen = patternLength;
        //  ACOTADO AQUI Y NO SOLO EN LA MAQUETA. El primer paso lo mueve una
        //  barra y lo mueve SEGUIR, y esta funcion la llama el temporizador
        //  treinta veces por segundo: un patron que encoge de 32 pasos a 16
        //  deja la ventana apuntando fuera de la tabla. Es el mismo fallo que
        //  ya costo cinco compases del piano.
        primerPaso = juce::jlimit (0, juce::jmax (0, patLen - numCols()), desdePaso);
        playing = playStep; selPad = selectedPad;
        //  EL NUMERO QUE SE PINTA EN EL CANALON ES EL DEL PAD, NO EL DEL CARRIL.
        //
        //  Esta rejilla trabaja en carriles - dieciseis, del 0 al 15 - y quien
        //  la usa le suma el banco. Pintar carril+1 daba 01..16 en los cuatro
        //  bancos, asi que en el banco B la cabecera decia "PAD 17 BD" y el
        //  canalon de esa misma pista decia 01: el mismo pad con dos nombres, y
        //  el que la persona lee para saber cual es era el falso.
        laneBase = firstPad;
        phase = juce::jlimit (0.0f, 1.0f, stepPhase);

        //  REPINTAR SOLO SI HA CAMBIADO ALGO.
        //
        //  Esto se llama en CADA tick del temporizador mientras la ficha del
        //  secuenciador esta abierta -treinta veces por segundo- y repintaba
        //  siempre, mirase o no lo que le habian pasado. La rejilla son 256
        //  celdas dibujadas a mano con su color, su nota y su cerco; el
        //  patron, en cambio, cambia cuando lo tocas tu. Lo unico que se
        //  mueve solo es el cabezal, y para saber si se ha movido basta con
        //  compararlo.
        //
        //  Se compara SOLO EL COMPAS QUE SE VE, que es lo unico que se
        //  dibuja: un paso escrito en el compas 3 mientras miras el 1 no
        //  cambia ni un pixel de esta rejilla.
        //
        //  Comparar cuesta 512 bytes de memcmp mas treinta y dos escalares.
        //  Repintar cuesta la rejilla entera Y, como la ficha que la contiene
        //  es translucida y ocupa la ventana, todo lo que hay debajo.
        //  Y SE COMPARA LA VENTANA QUE SE VE, que ya no es un compas: son las
        //  columnas que entran a este zoom, desde el paso que la barra dejo. Se
        //  acota al patron para no leer fuera de la tabla cuando la ventana es
        //  mas ancha que lo que queda.
        const int  cols  = juce::jmin (numCols() + 1, kMaxCols);
        const size_t nCel = (size_t) kLanes
                          * (size_t) juce::jlimit (0, cols, patLen - primerPaso);
        const size_t off  = (size_t) primerPaso * (size_t) kLanes;

        bool igual = data != nullptr && visto
                  && patLen == prevPatLen && primerPaso == prevPaso && playing == prevPlaying
                  && selPad == prevSelPad && laneBase == prevLaneBase
                  && std::abs (phase - prevPhase) < 0.004f
                  && std::memcmp (sombraCeldas.data(), data + off, nCel * sizeof (bool)) == 0
                  && std::memcmp (sombraZati.data(),   zatiOf,   sizeof (sombraZati)) == 0
                  && std::memcmp (sombraCarga.data(),  loadedOf, sizeof (sombraCarga)) == 0
                  && (noteOf == nullptr
                        || std::memcmp (sombraNotas.data(), noteOf + off,
                                        nCel * sizeof (signed char)) == 0);

        if (igual) return;

        //  Y si lo UNICO que ha cambiado es el cabezal, se repinta el cabezal.
        //
        //  Mientras el transporte rueda, la fase avanza en cada tick y esta
        //  comparacion no ahorra nada por si sola: lo que se mueve es una
        //  marca de diez pixeles de ancho, y repintar por ella las 256 celdas
        //  -y, como la ficha que las contiene es translucida, el chasis y los
        //  pads que hay debajo- es el mismo derroche a menor escala.
        //
        //  La union de donde estaba y donde esta: dos columnas como mucho, y
        //  una sola cuando la marca solo se desliza dentro de su paso.
        const bool soloCabezal = visto && data != nullptr
                              && patLen == prevPatLen && primerPaso == prevPaso
                              && selPad == prevSelPad && laneBase == prevLaneBase
                              && std::memcmp (sombraCeldas.data(), data + off, nCel * sizeof (bool)) == 0
                              && std::memcmp (sombraZati.data(),  zatiOf,   sizeof (sombraZati)) == 0
                              && std::memcmp (sombraCarga.data(), loadedOf, sizeof (sombraCarga)) == 0
                              && (noteOf == nullptr
                                    || std::memcmp (sombraNotas.data(), noteOf + off,
                                                    nCel * sizeof (signed char)) == 0);

        const auto antes = marcaDe (prevPlaying);

        if (data != nullptr)   std::memcpy (sombraCeldas.data(), data + off, nCel * sizeof (bool));
        if (noteOf != nullptr) std::memcpy (sombraNotas.data(), noteOf + off, nCel * sizeof (signed char));
        if (zatiOf != nullptr) std::memcpy (sombraZati.data(),  zatiOf,   sizeof (sombraZati));
        if (loadedOf != nullptr) std::memcpy (sombraCarga.data(), loadedOf, sizeof (sombraCarga));

        prevPatLen = patLen; prevPaso = primerPaso; prevPlaying = playing;
        prevSelPad = selPad; prevLaneBase = laneBase; prevPhase = phase;
        const bool primera = ! visto;
        visto = true;

        if (soloCabezal && ! primera)
        {
            //  Y SI NO HAY CABEZAL QUE MOVER, NO SE REPINTA NADA.
            //
            //  `soloCabezal` ya dice que las 256 celdas, los zatis, las cargas
            //  y las notas son identicas a lo pintado: lo unico que podia haber
            //  cambiado es la marca. Con el transporte PARADO no hay marca -
            //  `marcaDe (-1)` es vacio por los dos lados- asi que la union sale
            //  vacia... y esto se caia al `repaint()` de abajo, o sea que
            //  repintaba la rejilla ENTERA treinta veces por segundo para no
            //  cambiar un pixel. Y como la ficha que la contiene es
            //  translucida, con ella el chasis, los dieciseis pads y todo lo
            //  que hay debajo.
            //
            //  Medido con `Tests/cpu.py` y la maquina sonando: la ficha SEC
            //  pintaba **18.5 fotogramas equivalentes** en 8 s contra un tope
            //  de 8.0 -y contra los 5.8 que su propio cabezal cuesta cuando de
            //  verdad se mueve-. Ninguna otra ficha pasaba de 0.5.
            //
            //  Estaba desde que existe el atajo y no lo veia nadie porque
            //  cpu.py mide por RELOJ y por eso no corre en el banco de CI.
            auto zona = antes.getUnion (marcaDe (playing));
            if (! zona.isEmpty()) repaint (zona);
            return;
        }

        repaint();
    }


    //  Donde cae la marca del paso que suena, con dos pixeles de margen para
    //  el suavizado de los bordes. Vacio cuando no hay nada sonando o el paso
    //  cae fuera del compas que se ve, que es justo lo que hace falta para que
    //  la union borre la marca anterior y no pinte ninguna nueva.
    //  La FASE no entra: el rectangulo es la columna entera del paso mas seis
    //  pixeles a cada lado, que es donde puede caer la marca para cualquier
    //  fase de 0 a 1 -incluida la de 1, que asoma media marca en la columna
    //  siguiente-. Una zona por fase seria mas ajustada y volveria a mover el
    //  borde en cada tick, que es justo lo que se venia a quitar.
    juce::Rectangle<int> marcaDe (int step) const
    {
        const auto r = getLocalBounds();
        if (r.isEmpty() || step < 0) return {};

        const int base = prevPaso;
        if (step < base || step >= base + numCols() + 1 || step >= prevPatLen) return {};

        const float cellW = anchoCelda();
        const float col   = (float) r.getX() + (float) kGutter + cellW * (float) (step - base);

        //  La columna entera, no solo la linea: debajo de la marca hay un
        //  sombreado de columna que tambien tiene que borrarse al pasar.
        return juce::Rectangle<float> (col - 6.0f, (float) r.getY(),
                                       cellW + 12.0f, (float) r.getHeight())
                 .getSmallestIntegerContainer().getIntersection (r);
    }

    void paint (juce::Graphics& g) override
    {
        if (data == nullptr) return;

        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH = altoCelda();
        const float cellW = anchoCelda();
        const int   base  = primerPaso;
        //  UNA COLUMNA DE MAS, la que asoma por el filo. No es un adorno: es lo
        //  unico que dice que la ventana no llega al final, y sin ella una
        //  rejilla llena y una a medias se ven igual. Cae fuera de los limites
        //  y la recorta el propio componente.
        const int  cols  = juce::jmin (numCols() + 1, kMaxCols);

        for (int fila = 0; fila < carriles; ++fila)
        {
            //  La FILA es donde se dibuja y el CARRIL es de donde salen los
            //  datos: con media rejilla a la vista no son el mismo numero, y
            //  confundirlos pinta la mitad de arriba con los pasos de la de
            //  abajo. Ver setVentana.
            const int lane = medio + fila;
            // Lane 0 is pad 01 at the top: this is a list of pads, read
            // downwards, not the bottom-up pad grid.
            const int pad = lane;
            const auto frag = Zati::colour (zatiOf[pad]);
            const bool has  = loadedOf[pad];
            const float y   = (float) r.getY() + laneH * (float) fila;

            // Gutter: the pad's colour and number, so a lane is identified the
            // same way the pad is.
            auto gut = juce::Rectangle<float> ((float) r.getX(), y, (float) gutter, laneH).reduced (1.0f, 0.5f);
            g.setColour (has ? frag.withMultipliedAlpha (pad == selPad ? 0.95f : 0.55f)
                             : ZatiColours::markOn (ZatiColours::chassisTop, 0.18f));
            g.fillRect (gut);
            g.setColour (has ? ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white)
                             : ZatiColours::inkDim.withAlpha (0.6f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (laneBase + pad + 1).paddedLeft ('0', 2), gut, juce::Justification::centred);

            for (int c = 0; c < cols; ++c)
            {
                const int step = base + c;
                const float x  = (float) r.getX() + (float) gutter + cellW * (float) c;
                auto cell = juce::Rectangle<float> (x, y, cellW, laneH).reduced (1.0f);

                if (step >= patLen)                       // past this pattern's length
                {
                    //  Fuera del patron: el hueco mas profundo de la rejilla.
                    g.setColour (ZatiColours::groove (0.30f));
                    g.fillRect (cell);
                    continue;
                }

                const bool on = data[step * kLanes + pad];

                if (on)
                {
                    //  Un paso puesto en un carril SIN sonido no lleva color de
                    //  fragmento porque no hay fragmento, pero sigue siendo un
                    //  paso puesto: una marca medida contra la tarjeta, clara
                    //  sobre un cuerpo oscuro y oscura sobre uno claro. Escrito
                    //  con ZatiColours::ink salia crema en LACA - mas llamativo
                    //  que un paso que SI tiene sonido.
                    g.setColour (has ? frag : ZatiColours::markOn (ZatiColours::chassisTop, 0.55f));
                    g.fillRect (cell);

                    //  The pitch of a step used to exist only as a number in a
                    //  field, so a melody was something you had to remember
                    //  rather than see. Drawn inside the cell as a mark whose
                    //  HEIGHT is the semitone (-12 at the floor, +12 at the
                    //  ceiling), a lane becomes a contour you can read.
                    const int semis = noteOf != nullptr ? (int) noteOf[step * kLanes + pad] : 0;
                    if (semis != 0)
                    {
                        const float t = 0.5f - juce::jlimit (-1.0f, 1.0f, (float) semis / 12.0f) * 0.42f;
                        const float y = cell.getY() + cell.getHeight() * t;
                        g.setColour (ZatiColours::bestOn (has ? frag : ZatiColours::ink,
                                                          ZatiColours::ink, juce::Colours::white));
                        g.fillRect (cell.getX() + 1.5f, y - 1.0f, cell.getWidth() - 3.0f, 2.0f);
                    }
                }
                else
                {
                    // Beats stay a shade darker than the off-beats, so the bar
                    // keeps a pulse you can count without reading numbers.
                    //  Una celda vacia es un HUECO, y un hueco es mas oscuro que
                    //  la superficie en la que esta. Con padBorder salia mas
                    //  claro que la tarjeta en las dos carcasas oscuras, o sea
                    //  del lado equivocado: la rejilla se leia como si todos los
                    //  pasos estuvieran puestos a medias.
                    const bool beat = (step % 4) == 0;
                    g.setColour (ZatiColours::groove (beat ? 0.48f : 0.28f));
                    g.fillRect (cell);
                }

            }
        }

        // Bar rules every 4 steps — structure, drawn over the cells.
        //  Y LAS REGLAS CAEN EN LOS PASOS MULTIPLOS DE CUATRO DEL PATRON, no
        //  cada cuatro columnas: con la ventana continua las dos cosas dejaron
        //  de ser la misma: empezando en el paso 3, una linea cada cuatro
        //  columnas cae en 7 y 11 - o sea marca el contratiempo y borra el
        //  pulso, que es justo lo contrario de lo que esta regla existe para
        //  decir.
        g.setColour (ZatiColours::groove (0.28f));
        for (int c = 0; c < cols; ++c)
            if (((base + c) % 4) == 0 && c > 0)
                g.fillRect ((float) r.getX() + gutter + cellW * (float) c - 0.5f,
                            (float) r.getY(), 1.0f, (float) r.getHeight());

        //  The playhead. It used to be a red outline drawn around each of the
        //  sixteen cells of the live column, which is sixteen boxes announcing
        //  one position - the eye reads a stack of empty frames, not a beat.
        //  One bar instead: the column it is over, and a line sliding across it
        //  with the step, so the grid has a hand sweeping over it.
        if (playing >= base && playing < base + cols && playing < patLen)
        {
            const float col = (float) r.getX() + gutter + cellW * (float) (playing - base);
            const float y0  = (float) r.getY();
            const float hh  = (float) r.getHeight();

            //  The column the beat is in, and the line inside it. Both in the
            //  playhead's own colour now: red belongs to RECORDING and to
            //  nothing else on this machine.
            g.setColour (ZatiColours::groove (0.14f));
            g.fillRect (col, y0, cellW, hh);

            const float x = col + cellW * phase;
            //  White on a pale card needs an edge or it disappears. Two
            //  hairlines of ink, one each side, is what a printed marker does.
            g.setColour (ZatiColours::playheadEdge);
            g.fillRect (x - 2.0f, y0, 4.0f, hh);
            g.fillRect (x - 5.0f, y0, 10.0f, 4.0f);
            g.fillRect (x - 5.0f, y0 + hh - 4.0f, 10.0f, 4.0f);

            g.setColour (ZatiColours::playhead);
            g.fillRect (x - 1.0f, y0, 2.0f, hh);
            g.fillRect (x - 4.0f, y0, 8.0f, 3.0f);            // the head, top
            g.fillRect (x - 4.0f, y0 + hh - 3.0f, 8.0f, 3.0f); // ...and bottom
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { hit (e); }
    void mouseDrag (const juce::MouseEvent& e) override { hit (e, true); }

private:
    void hit (const juce::MouseEvent& e, bool dragging = false)
    {
        if (data == nullptr || ! onCell) return;
        auto r = getLocalBounds();
        const int gutter = kGutter;
        if (e.x < r.getX() + gutter) return;

        const float laneH = altoCelda();
        const float cellW = anchoCelda();
        //  Y el toque hace el camino de vuelta: fila a carril. Sin sumar el
        //  medio, con la mitad de abajo a la vista se escribiria en los pads de
        //  arriba - la rejilla ensenaria una cosa y la maquina tocaria otra.
        const int fila = juce::jlimit (0, carriles - 1, (int) ((float) (e.y - r.getY()) / laneH));
        const int lane = medio + fila;
        const int col  = juce::jlimit (0, numCols(), (int) ((float) (e.x - r.getX() - gutter) / cellW));
        const int step = primerPaso + col;
        if (step >= patLen) return;

        // A drag paints, but only across cells it has not already touched this
        // gesture — otherwise moving inside one cell would flip it repeatedly.
        const int key = lane * 1000 + step;
        if (dragging && key == lastKey) return;
        lastKey = key;
        onCell (lane, step);
    }

    const bool* data = nullptr;
    const int*  zatiOf = nullptr;
    const bool* loadedOf = nullptr;
    const signed char* noteOf = nullptr;
    int patLen = 16, primerPaso = 0, playing = -1, selPad = -1, lastKey = -1;
    int laneBase = 0;      // el pad del carril 0: 0, 16, 32 o 48. Ver setSource.
    int carriles = kLanes, medio = 0;   // la ventana de pistas. Ver setVentana.
    float zoomW = 1.0f;                 // el ancho de la celda contra su alto
    float phase = 0.0f;   // how far through the live step, 0..1

    //  La copia de lo ultimo PINTADO, para no volver a pintarlo. Ver setSource.
    std::array<bool, (size_t) kLanes * (size_t) kMaxCols>        sombraCeldas {};
    std::array<signed char, (size_t) kLanes * (size_t) kMaxCols> sombraNotas  {};
    std::array<int,  (size_t) kLanes> sombraZati  {};
    std::array<bool, (size_t) kLanes> sombraCarga {};
    int   prevPatLen = -1, prevPaso = -1, prevPlaying = -2, prevSelPad = -2, prevLaneBase = -1;
    float prevPhase = -1.0f;
    bool  visto = false;      // aun no se ha pintado nunca: la primera vez siempre pasa
};

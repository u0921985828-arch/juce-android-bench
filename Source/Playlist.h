#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include "AudioEngine.h"
#include <array>
#include <cstring>
#include <vector>
#include <utility>

// ============================================================================
//  Playlist — the arrangement, as a timeline you can see.
//
//  The chain was a queue: pattern after pattern, in a straight line, and that
//  is all a track could ever be. Here time runs across and four lanes run down,
//  so a drum pattern, a chopped break and a single vocal hit can all sit on the
//  same bar. That is what makes it an arrangement rather than a loop.
//
//  Two kinds of block, deliberately:
//    · a PATTERN, which occupies as many bars as its own length needs
//    · a ONE-SHOT, a single pad fired at the top of that bar — the thing the
//      chain could never hold, and the reason a crash or a vocal stab had to
//      be baked into a pattern of its own before.
//
//  A block wears the colour of what it holds: the pattern's bank colour, or
//  the pad's zati. Nothing here invents a new colour language.
// ============================================================================
class Playlist : public juce::Component
{
public:
    //  La columna de los nombres de pista. Misma razon que en StepGrid: se
    //  escribia dos veces, y el pintado y el toque tienen que coincidir.
    static constexpr int kGutter = Metrics::canalCancion;

    static constexpr int kLanes    = 4;
    static constexpr int kBarsView = 8;    // bars visible at once; pages beyond

    //  LOS CARRILES DE AUDIO NO SON CUATRO CARRILES MAS DE ESTA REJILLA.
    //
    //  Medido antes de decidirlo, que es lo unico que separa un diseno de una
    //  opinion: la linea de tiempo se lleva hoy 42 px por carril en un movil
    //  grande, 28.8 en un 360x640 y **20.2 en 280x653**. Cuatro carriles mas al
    //  mismo alto piden 160 px que no existen - en la pantalla estrecha los
    //  ocho saldrian a diez pixeles - y un clip se ARRASTRA, que es el control
    //  que menos puede permitirse ser fino: fallar el agarre no es fallar un
    //  toque, es mover otra cosa.
    //
    //  Asi que son dos VISTAS de la misma rejilla y no ocho carriles: los
    //  patrones o el audio, con la misma cuenta de compas a pixel escrita UNA
    //  vez. Es la decision que ya tomo la ficha del secuenciador con PASOS,
    //  PIANO y PATRON, y por lo mismo: dos ventanas para un trabajo es lo que
    //  esta app no permite, y dos maquetados para la misma cuenta son dos
    //  reglas.
    enum Vista { vistaPatrones = 0, vistaAudio };

    //  Un clip, tal y como esta rejilla necesita verlo: EN COMPASES. El motor
    //  lo guarda en muestras -el audio mide lo que mide- y traducirlo pide el
    //  tempo, que es cosa del motor y no de un componente que dibuja. Quien
    //  sabe cuantas muestras son un compas lo traduce y pasa esto, igual que la
    //  tabla de zatis se pasa en vez de preguntarle al motor por cada bloque.
    struct ClipVista
    {
        int pista  = 0;      // 0..kAudioLanes-1
        int desde  = 0;      // primer compas que ocupa
        int hasta  = 1;      // el primero que YA NO ocupa, o sea [desde, hasta)
        int pad    = 0;      // de que pad salio, que es de donde sale su color
    };

    // (lane, bar) — the host decides what to place or whether to clear.
    std::function<void (int lane, int bar)> onCell;
    //  UN HUECO DE LA BANDA DE AUDIO: aqui no habia nada, pon lo que tengas.
    std::function<void (int pista, int compas)> onClipNuevo;
    //  Y UN CLIP QUE SE ARRASTRA. El indice es el de la tabla que se paso, no
    //  una identidad: quien la publica es quien la ordena.
    std::function<void (int indice, int pista, int compas)> onClipMueve;
    std::function<void (int indice)> onClipQuita;
    //  Y EL LARGO, arrastrando un filo. Llega en COMPASES porque es lo que
    //  esta rejilla sabe: quien traduce a muestras es quien tiene el tempo.
    std::function<void (int indice, int desdeCompas, int hastaCompas)> onClipLargo;
    //  Un toque en la canaleta del carril: lo silencia. Ver mouseDown.
    std::function<void (int lane)> onLane;

    //  Las veces que blockColour pidio un pad fuera de la tabla. Ver el
    //  desbordamiento que esto arreglo: es un fallo de INDICE, no de geometria,
    //  asi que ninguna de las ocho reglas del banco puede verlo y hace falta
    //  que la app lo cuente.
    static inline int zatisFueraDeRango = 0;

    //  Lo que de verdad le pasaron, para que el banco no tenga que repetir la
    //  constante: una prueba que lee el numero que juzga cambia de opinion a la
    //  vez que el fallo.
    int numZatis() const noexcept { return zatis; }

    //  LA MISMA CUENTA DE COMPAS A PIXEL, con otra cosa dibujada encima.
    //
    //  Cuantos carriles tiene la banda lo dice el MOTOR y no este fichero: son
    //  las pistas que sabe reproducir, y escribir aqui un cuatro seria la misma
    //  regla en dos sitios - la que ya costo que `kContinued` estuviera
    //  definido dos veces.
    static constexpr int kAudioLanes = AudioEngine::kAudioTracks;
    //  Lo mas corto que puede llevar asas. Con dos compases, las dos asas son
    //  el clip entero y no quedaria medio que agarrar para moverlo.
    static constexpr int kCompasesConAsa = 3;

    void ponVista (int v) noexcept
    {
        const int nueva = (v == vistaAudio) ? (int) vistaAudio : (int) vistaPatrones;
        if (nueva == vista) return;
        vista = nueva;
        visto = false;          // la sombra es de la otra vista: no vale
        repaint();
    }
    int  laVista() const noexcept { return vista; }

    //  La tabla se PRESTA, no se copia: la publica quien la tiene y vive lo que
    //  dure la llamada del temporizador, igual que `data` y `zati`.
    void setAudio (const ClipVista* filas, int cuantas, int elegido, unsigned mudos) noexcept
    {
        clips = filas; numClips = juce::jmax (0, cuantas); clipSel = elegido;
        //  SU PROPIA MASCARA DE SILENCIO y no la de los carriles de patron.
        //  Son cuatro cosas distintas -silenciar el carril 1 no puede callar la
        //  pista de audio 1- y compartir el numero habria hecho justo eso sin
        //  un solo error de compilacion.
        mudoAudio = mudos;
        //  Sin sombra: son unas pocas filas y el repintado ya esta acotado por
        //  la guarda de la ficha. Comparar una tabla de clips fila a fila para
        //  ahorrar un repintado de 168 px seria pagar el ahorro dos veces.
        if (vista == vistaAudio) repaint();
    }

    void setSource (const int* cells,       // [lane][bar] flattened, stride = bars
                    const int* zatiOf,      // un zati por pad...
                    int numZatis,           // ...y CUANTOS, que es la mitad que faltaba
                    int bars, int page, int playBar,
                    int cursorBar = -1,     // el compas sobre el que actuan las herramientas
                    unsigned mudos = 0,     // un bit por carril silenciado
                    int loopA = 0, int loopB = 0)   // el tramo en bucle, [A,B) en compases
    {
        data = cells; zati = zatiOf; zatis = numZatis;
        totalBars = bars; pageIndex = page; playing = playBar;
        cursor = cursorBar; mute = mudos; lA = loopA; lB = loopB;

        //  REPINTAR SOLO SI HA CAMBIADO ALGO. Por lo mismo que la rejilla de
        //  pasos: el temporizador llama aqui treinta veces por segundo
        //  mientras la ficha CANCION esta abierta, y lo unico que se mueve
        //  solo es el compas que suena. La tabla la cambias tu, con el dedo.
        //
        //  Y la ficha es translucida y ocupa la ventana entera, asi que un
        //  repintado de esta rejilla arrastra el chasis, los dieciseis pads y
        //  los cuarenta controles que hay debajo del velo.
        const size_t nCel = (size_t) kLanes * (size_t) juce::jmax (1, totalBars);
        bool igual = data != nullptr && visto
                  && totalBars == prevBars && pageIndex == prevPage && playing == prevPlaying
                  && cursor == prevCursor && mute == prevMute
                  && lA == prevLA && lB == prevLB
                  && sombra.size() == nCel
                  && std::memcmp (sombra.data(), data, nCel * sizeof (int)) == 0
                  && (zati == nullptr
                        || std::memcmp (sombraZati.data(), zati, sizeof (sombraZati)) == 0);

        if (igual) return;

        if (data != nullptr) { sombra.resize (nCel); std::memcpy (sombra.data(), data, nCel * sizeof (int)); }
        if (zati != nullptr) std::memcpy (sombraZati.data(), zati, sizeof (sombraZati));
        prevBars = totalBars; prevPage = pageIndex; prevPlaying = playing;
        prevCursor = cursor; prevMute = mute; prevLA = lA; prevLB = lB;
        visto = true;

        repaint();
    }

    //  EL MODO ARMADO, VISTO DONDE SE ACTUA. AUTO cambia si mover un mando
    //  ESCRIBE en la linea de tiempo, y su tapa esta en la fila de arriba
    //  mientras el dedo esta aqui. Transparente es «ningun modo».
    void setModo (juce::Colour c) { if (modoTinte != c) { modoTinte = c; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        if (vista == vistaAudio) { pintaAudio (g); marcoDelModo (g); return; }
        if (data == nullptr) { marcoDelModo (g); return; }
        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float barW  = (float) (r.getWidth() - gutter) / (float) kBarsView;
        const int   base  = pageIndex * kBarsView;

        for (int lane = 0; lane < kLanes; ++lane)
        {
            const float y = (float) r.getY() + laneH * (float) lane;

            const bool mudo = (mute & (1u << (unsigned) lane)) != 0;

            auto gut = juce::Rectangle<float> ((float) r.getX(), y, (float) gutter, laneH).reduced (1.0f, 1.0f);
            //  Mismo criterio que la rejilla de pasos: la canaleta del carril
            //  es una marca medida contra la tarjeta, no un token de la placa
            //  de pads - que en las dos carcasas oscuras sale mas claro que el
            //  fondo y convierte una fila vacia en una fila que parece llena.
            //
            //  Y LA CANALETA ES EL INTERRUPTOR DE SILENCIO del carril. No hay
            //  sitio para cuatro tapas mas en una tarjeta que ya llega al tope
            //  de altura, y el numero del carril es justo lo que en una mesa
            //  lleva el boton de MUTE al lado. Silenciado se pinta en rojo con
            //  el numero tachado, que es lo unico que se lee de un vistazo en
            //  una casilla de treinta pixeles.
            g.setColour (mudo ? ZatiColours::red.withAlpha (0.85f)
                              : ZatiColours::markOn (ZatiColours::chassisTop, 0.18f));
            g.fillRect (gut);
            g.setColour (mudo ? ZatiColours::bestOn (ZatiColours::red, ZatiColours::ink, juce::Colours::white)
                              : ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (lane + 1), gut, juce::Justification::centred);
            if (mudo)
                g.fillRect (gut.getX() + 3.0f, gut.getCentreY() - 0.5f, gut.getWidth() - 6.0f, 1.4f);

            for (int c = 0; c < kBarsView; ++c)
            {
                const int bar = base + c;
                const float x = (float) r.getX() + (float) gutter + barW * (float) c;
                auto cell = juce::Rectangle<float> (x, y, barW, laneH).reduced (1.5f);

                if (bar >= totalBars)                        // past the end of the song
                {
                    g.setColour (ZatiColours::groove (0.30f));   // pasado el final
                    g.fillRect (cell);
                    continue;
                }

                const int v = data[lane * totalBars + bar];

                if (v == 0)
                {
                    //  Un compas vacio es un hueco, y un hueco oscurece.
                    g.setColour (ZatiColours::groove ((bar % 4 == 0) ? 0.48f : 0.28f));
                    g.fillRect (cell);
                }
                else if (v == kContinued)
                {
                    // The tail of a longer pattern: same colour, no label, so a
                    // four-bar block reads as ONE block instead of four copies.
                    const int startBar = findStart (lane, bar);
                    const int sv = startBar >= 0 ? data[lane * totalBars + startBar] : 0;
                    g.setColour (blockColour (sv).withAlpha (mudo ? 0.18f : 0.55f));
                    g.fillRect (cell.withTrimmedLeft (-1.5f));
                }
                else
                {
                    const auto col = blockColour (v);
                    //  Un carril silenciado ensena sus bloques HUECOS: siguen
                    //  ahi, con su color y su nombre, y no suenan. Borrarlos
                    //  seria otra cosa, y esa ya existe.
                    if (mudo)
                    {
                        g.setColour (ZatiColours::groove (0.30f));
                        g.fillRect (cell);
                        g.setColour (col.withAlpha (0.70f));
                        g.drawRect (cell, 1.4f);
                    }
                    else
                    {
                        g.setColour (col);
                        g.fillRect (cell);
                    }
                    g.setColour (mudo ? col.withAlpha (0.85f)
                                      : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
                    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
                    g.drawText (v > 0 ? "P" + juce::String (v)
                                      : juce::String (-v).paddedLeft ('0', 2),
                                cell, juce::Justification::centred);
                }

                if (bar == playing)
                {
                    //  The bar being played, in the playhead's colour rather
                    //  than in the recording one. Ink outside, white inside,
                    //  so it reads on a pale card and on a filled block alike.
                    g.setColour (ZatiColours::playheadEdge);
                    g.drawRect (cell.expanded (1.0f), 1.4f);
                    g.setColour (ZatiColours::playhead);
                    g.drawRect (cell, 1.6f);
                }
            }
        }

        //  EL TRAMO EN BUCLE, como una barra sobre los compases que repiten.
        //  Dibujado ENCIMA de las celdas y no detras: detras lo tapaba el
        //  primer bloque que cayera dentro, que es siempre - un tramo en bucle
        //  sin bloques dentro no es un tramo, es un descanso.
        if (lB > lA)
        {
            const float x0 = (float) r.getX() + gutter + barW * (float) juce::jmax (0, lA - base);
            const float x1 = (float) r.getX() + gutter + barW * (float) juce::jmin (kBarsView, lB - base);
            if (x1 > x0 && lB > base && lA < base + kBarsView)
            {
                g.setColour (ZatiColours::green.withAlpha (0.85f));
                g.fillRect (x0, (float) r.getY(), x1 - x0, 3.0f);
                g.fillRect (x0, (float) r.getY(), 2.0f, 9.0f);
                g.fillRect (x1 - 2.0f, (float) r.getY(), 2.0f, 9.0f);
            }
        }

        //  EL CURSOR: el compas sobre el que actuan INSERTAR, QUITAR, COPIAR y
        //  PEGAR. Sin el, esas cuatro no tendrian donde actuar y habria que
        //  inventar un gesto para decirselo. Una columna entera marcada, no
        //  una celda: las cuatro herramientas trabajan sobre el COMPAS, con
        //  sus cuatro carriles, y marcar una sola celda diria lo contrario.
        if (cursor >= base && cursor < base + kBarsView && cursor < totalBars)
        {
            const float x = (float) r.getX() + gutter + barW * (float) (cursor - base);
            g.setColour (ZatiColours::ink.withAlpha (0.85f));
            g.drawRect (juce::Rectangle<float> (x, (float) r.getY(), barW, (float) r.getHeight()), 2.0f);
        }

        // Bar numbers along the top edge of the first lane.
        g.setColour (ZatiColours::inkDim.withAlpha (0.7f));
        g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
        for (int c = 0; c < kBarsView; c += 2)
            g.drawText (juce::String (base + c + 1),
                        (int) ((float) r.getX() + gutter + barW * (float) c) + 2, r.getY(),
                        (int) barW, 9, juce::Justification::topLeft);
    }

    //  Un marco alrededor del LIENZO y no de la ficha, por lo mismo que en el
    //  piano: lo que tiene que decir «esto esta en un modo» es la superficie
    //  sobre la que cae el dedo.
    void marcoDelModo (juce::Graphics& g)
    {
        if (modoTinte.isTransparent()) return;
        auto marco = getLocalBounds().toFloat().reduced (0.75f);
        marco.setLeft (marco.getX() + (float) kGutter);
        g.setColour (modoTinte.withAlpha (0.9f));
        g.drawRect (marco, 1.5f);
    }

    //  SE PINTA CON EL DEDO ARRASTRADO, como la rejilla de pasos y como el
    //  piano.
    //
    //  Solo habia mouseDown: escribir ocho compases eran ocho toques, y la
    //  regla de la casa dice que esta ficha no se desplaza
    //  precisamente porque "se pinta con el dedo arrastrado". O faltaba el
    //  gesto o sobraba el argumento; faltaba el gesto.
    //
    //  Con dos candados que la rejilla de pasos ya tiene por el mismo motivo:
    //  la canaleta del MUTE solo responde al TOQUE -arrastrar por ella
    //  silenciaria los cuatro carriles de una pasada- y una celda no se
    //  escribe dos veces seguidas, o pasar el dedo por encima de la misma
    //  casilla la enciende y la apaga a la velocidad de los eventos del raton.
    //  Y EN LA BANDA DE AUDIO, ARRASTRAR MUEVE Y NO PINTA. Son dos gestos
    //  incompatibles en el mismo dedo -uno escribe celdas y el otro desplaza un
    //  bloque- y por eso no comparten vista: aqui no hay ninguna celda que se
    //  pueda encender arrastrando, asi que no hay nada que aprender dos veces.
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (vista == vistaAudio) { tocaAudio (e, false); return; }
        ultima = { -1, -1 }; toca (e, false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (vista == vistaAudio) { tocaAudio (e, true); return; }
        toca (e, true);
    }
    void mouseUp   (const juce::MouseEvent&)   override { ultima = { -1, -1 }; arrastrado = -1; }

    void toca (const juce::MouseEvent& e, bool arrastrando)
    {
        if (data == nullptr) return;
        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH0 = (float) r.getHeight() / (float) kLanes;

        //  La canaleta silencia el carril. Ver el pintado: es el sitio donde
        //  una mesa lleva el MUTE, y no cuesta una fila de tapas en una
        //  tarjeta que ya llega al tope de altura.
        if (e.x < r.getX() + gutter)
        {
            if (onLane && ! arrastrando)
                onLane (juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH0)));
            return;
        }

        if (! onCell) return;
        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float barW  = (float) (r.getWidth() - gutter) / (float) kBarsView;
        const int lane = juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH));
        const int bar  = pageIndex * kBarsView
                       + juce::jlimit (0, kBarsView - 1, (int) ((float) (e.x - r.getX() - gutter) / barW));
        if (bar >= totalBars) return;
        if (arrastrando && lane == ultima.first && bar == ultima.second) return;
        ultima = { lane, bar };
        onCell (lane, bar);
    }

    //  ------------------------------------------------------------------
    //  LA BANDA DE AUDIO
    //  ------------------------------------------------------------------

    //  El rectangulo de un compas en una pista, con la MISMA cuenta que la
    //  vista de patrones. Escrita una vez y usada por el pintado y por el
    //  gesto, que es la leccion de la canaleta: cuando el dibujo y el toque
    //  hacen la cuenta cada uno por su lado, un dia dejan de coincidir.
    juce::Rectangle<float> celdaAudio (int pista, int compas) const
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) kBarsView;
        return { (float) r.getX() + (float) kGutter + barW * (float) (compas - pageIndex * kBarsView),
                 (float) r.getY() + pistaH * (float) pista, barW, pistaH };
    }

    void pintaAudio (juce::Graphics& g)
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) kBarsView;
        const int   base   = pageIndex * kBarsView;

        for (int pista = 0; pista < kAudioLanes; ++pista)
        {
            const float y = (float) r.getY() + pistaH * (float) pista;
            const bool mudo = (mudoAudio & (1u << (unsigned) pista)) != 0;

            //  La canaleta es la MISMA de la otra vista y hace lo mismo:
            //  silenciar la pista. Un numero que en una vista silencia y en la
            //  otra no seria el mismo sitio con dos significados.
            auto gut = juce::Rectangle<float> ((float) r.getX(), y, (float) kGutter, pistaH).reduced (1.0f, 1.0f);
            g.setColour (mudo ? ZatiColours::red.withAlpha (0.85f)
                              : ZatiColours::markOn (ZatiColours::chassisTop, 0.18f));
            g.fillRect (gut);
            g.setColour (mudo ? ZatiColours::bestOn (ZatiColours::red, ZatiColours::ink, juce::Colours::white)
                              : ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            //  La A dice que es una pista de AUDIO y no un carril de patrones,
            //  que es lo unico que las dos vistas comparten en pantalla: sin
            //  ella, "1 2 3 4" en la canaleta es el mismo dibujo en las dos.
            g.drawText ("A" + juce::String (pista + 1), gut, juce::Justification::centred);
            if (mudo)
                g.fillRect (gut.getX() + 3.0f, gut.getCentreY() - 0.5f, gut.getWidth() - 6.0f, 1.4f);

            for (int c = 0; c < kBarsView; ++c)
            {
                const int compas = base + c;
                auto cell = juce::Rectangle<float> ((float) r.getX() + (float) kGutter + barW * (float) c,
                                                    y, barW, pistaH).reduced (1.5f);
                g.setColour (compas >= totalBars
                                 ? ZatiColours::groove (0.30f)
                                 : ZatiColours::groove ((compas % 4 == 0) ? 0.48f : 0.28f));
                g.fillRect (cell);
            }
        }

        //  LOS CLIPS, DESPUES DE LOS HUECOS y no celda a celda: un clip de
        //  cuatro compases es UN bloque y no cuatro copias del mismo, que es
        //  exactamente lo que `kContinued` existe para conseguir en la otra
        //  vista. Aqui sale gratis porque el clip ya sabe donde acaba.
        for (int i = 0; i < numClips; ++i)
        {
            const ClipVista& c = clips[i];
            if (! juce::isPositiveAndBelow (c.pista, kAudioLanes)) continue;
            const int d = juce::jmax (c.desde, base);
            const int h = juce::jmin (c.hasta, base + kBarsView);
            if (h <= d) continue;                       // no cae en esta pagina

            auto caja = celdaAudio (c.pista, d)
                            .withWidth (barW * (float) (h - d)).reduced (1.5f);
            const auto col = (zati != nullptr && juce::isPositiveAndBelow (c.pad, zatis))
                                 ? Zati::colour (zati[c.pad]) : Zati::colour (c.pad);
            const bool mudo = (mudoAudio & (1u << (unsigned) c.pista)) != 0;

            if (mudo)
            {
                g.setColour (ZatiColours::groove (0.30f)); g.fillRect (caja);
                g.setColour (col.withAlpha (0.70f));       g.drawRect (caja, 1.4f);
            }
            else
            {
                g.setColour (col); g.fillRect (caja);
            }

            //  EL QUE SE ESTA MOVIENDO SE VE. Sin marca, arrastrar en una banda
            //  de cuatro pistas es soltar y buscar cual se movio.
            if (i == clipSel)
            {
                g.setColour (ZatiColours::playheadEdge); g.drawRect (caja.expanded (1.0f), 1.4f);
                g.setColour (ZatiColours::playhead);     g.drawRect (caja, 1.6f);
            }

            //  Y LAS ASAS SE DIBUJAN donde se pueden coger: dos filos verticales
            //  en el primer y el ultimo compas. Un asa que existe y no se ve es
            //  un gesto que nadie encuentra, y una que se ve donde no existe
            //  -en un clip corto- es peor.
            if ((c.hasta - c.desde) >= kCompasesConAsa)
            {
                g.setColour (ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white)
                                 .withAlpha (0.55f));
                g.fillRect (caja.getX() + 2.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
                g.fillRect (caja.getRight() - 4.0f, caja.getY() + 3.0f, 2.0f, caja.getHeight() - 6.0f);
            }

            g.setColour (mudo ? col.withAlpha (0.85f)
                              : ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (c.pad + 1).paddedLeft ('0', 2), caja, juce::Justification::centred);
        }

        //  El cabezal, con la misma marca que la otra vista: es el mismo
        //  transporte y el mismo compas.
        if (playing >= base && playing < base + kBarsView)
        {
            auto col = juce::Rectangle<float> ((float) r.getX() + (float) kGutter
                                                   + barW * (float) (playing - base),
                                               (float) r.getY(), barW, (float) r.getHeight());
            g.setColour (ZatiColours::playhead);
            g.drawRect (col, 1.6f);
        }

        g.setColour (ZatiColours::inkDim.withAlpha (0.7f));
        g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
        for (int c = 0; c < kBarsView; c += 2)
            g.drawText (juce::String (base + c + 1),
                        (int) ((float) r.getX() + (float) kGutter + barW * (float) c) + 2, r.getY(),
                        (int) barW, 9, juce::Justification::topLeft);
    }

    //  Que clip cae bajo (pista, compas). El PRIMERO que lo contenga: dos clips
    //  encima no es un estado que esta app produzca, y elegir "el de arriba"
    //  sin que haya arriba seria inventarse una regla.
    int clipEn (int pista, int compas) const
    {
        for (int i = 0; i < numClips; ++i)
            if (clips[i].pista == pista && compas >= clips[i].desde && compas < clips[i].hasta)
                return i;
        return -1;
    }

    void tocaAudio (const juce::MouseEvent& e, bool arrastrando)
    {
        auto r = getLocalBounds();
        const float pistaH = (float) r.getHeight() / (float) kAudioLanes;
        const float barW   = (float) (r.getWidth() - kGutter) / (float) kBarsView;
        if (barW <= 0.0f || pistaH <= 0.0f) return;

        //  La canaleta silencia, y SOLO al toque: arrastrar por ella
        //  silenciaria las cuatro pistas de una pasada. Es el mismo candado que
        //  la otra vista y por el mismo motivo.
        if (e.x < r.getX() + kGutter)
        {
            if (onLane && ! arrastrando)
                onLane (juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH)));
            return;
        }

        const int pista  = juce::jlimit (0, kAudioLanes - 1, (int) ((float) (e.y - r.getY()) / pistaH));
        const int compas = pageIndex * kBarsView
                         + juce::jlimit (0, kBarsView - 1, (int) ((float) (e.x - r.getX() - kGutter) / barW));
        if (compas >= totalBars) return;

        if (! arrastrando)
        {
            arrastrado = clipEn (pista, compas);
            asa = 0;
            if (arrastrado >= 0)
            {
                //  LAS ASAS, con la cuenta hecha. El precedente es
                //  WaveformDisplay -«coge el asa mas cercana, pero solo dentro
                //  de un dedo»- y aqui un dedo entero no vale: la celda mide
                //  40 px por compas (medido), asi que dos asas de 40 se comen
                //  un clip de un compas y no queda nada que arrastrar.
                //
                //  Un TERCIO del clip por lado y como mucho 16 px, y POR DEBAJO
                //  DE TRES COMPASES no hay asas: ahi el gesto solo mueve, que
                //  es lo que se quiere de un clip corto. La escalera de
                //  siempre, y el banco la mide.
                const auto& c = clips[arrastrado];
                const int  ancho = c.hasta - c.desde;
                if (ancho >= kCompasesConAsa)
                {
                    if (compas == c.desde)          asa = -1;
                    else if (compas == c.hasta - 1) asa = +1;
                }
            }
            if (arrastrado < 0)
            {
                //  Un hueco: aqui no hay nada que mover, asi que el gesto solo
                //  puede significar poner algo.
                if (onClipNuevo) onClipNuevo (pista, compas);
                return;
            }
            //  DONDE SE AGARRO, en compases desde el principio del clip. Sin
            //  esto, arrastrar un clip de cuatro compases por su tercer compas
            //  lo pega de un salto por su primero: el bloque se mueve un trozo
            //  que la persona no pidio, y es lo primero que se nota.
            agarre = compas - clips[arrastrado].desde;
            //  Un toque sobre un clip lo elige. Quitarlo es la brocha VACIAR,
            //  que es la misma que borra en la otra vista.
            if (onClipQuita && borrando) { onClipQuita (arrastrado); arrastrado = -1; }
            return;
        }

        if (arrastrado < 0 || arrastrado >= numClips) return;

        //  UN ASA CAMBIA EL LARGO Y NO LA POSICION. Son dos cosas y no una: un
        //  asa que ademas mueve pasa cualquier prueba que solo mire el largo, y
        //  desde el dedo es un clip que se escapa mientras lo recortas.
        if (asa != 0)
        {
            const auto& c = clips[arrastrado];
            int d = c.desde, h = c.hasta;
            if (asa < 0) d = juce::jmin (compas, h - 1);
            else         h = juce::jmax (compas + 1, d + 1);
            if (d == c.desde && h == c.hasta) return;
            if (onClipLargo) onClipLargo (arrastrado, d, h);
            return;
        }

        const int nuevoCompas = juce::jmax (0, compas - agarre);
        if (nuevoCompas == clips[arrastrado].desde && pista == clips[arrastrado].pista) return;
        if (onClipMueve) onClipMueve (arrastrado, pista, nuevoCompas);
    }

    //  Lo que la brocha VACIAR pone: quien la lleva es la ficha, y aqui solo se
    //  lee. Un componente que decidiera solo cual es la brocha seria un segundo
    //  dueno de la misma pregunta.
    bool borrando = false;

    //  El mismo centinela que AudioEngine::kContinued, y por eso se toma de
    //  alli: estaba escrito dos veces con el mismo numero, o sea dos duenos
    //  para una regla - mover uno dejaria la rejilla pintando bloques que el
    //  motor no reconoce, sin un solo error de compilacion.
    static constexpr int kContinued = AudioEngine::kContinued;

private:
    //  La ultima celda escrita por el arrastre en curso: sin esto, pasar el
    //  dedo por la misma casilla la enciende y la apaga a la velocidad de los
    //  eventos del raton.
    std::pair<int, int> ultima { -1, -1 };
    // Pattern banks borrow the fragment palette so a block is recognisable at a
    // glance; a one-shot wears its own pad's zati.
    //  EL COLOR DE UN GOLPE SUELTO SE PEDIA CON UN INDICE DE 0..63 A UNA TABLA
    //  DE DIECISEIS.
    //
    //  El pincel de un solo golpe guarda -(pad+1) y `selectedPad` va de 0 a 63
    //  (cuatro bancos de dieciseis), pero aqui llegaba `gridZati`, que es la
    //  tabla del banco QUE SE VE: dieciseis huecos. Con cualquier pad de los
    //  bancos B, C o D pintado en la linea de tiempo, esto leia fuera del array
    //  en CADA repintado de la ficha CANCION.
    //
    //  Hoy no se cae -lo leido acaba en Zati::colour, que envuelve con un modulo
    //  y devuelve un color cualquiera- pero es lectura fuera de rango: una
    //  compilacion con ASan, un asignador endurecido o MTE en un movil reciente
    //  lo convierten en un cierre. Y el sintoma visible tampoco era ninguno: el
    //  bloque salia del color de OTRO pad.
    //
    //  Se arregla por los dos lados. La tabla que se pasa es la de los sesenta y
    //  cuatro (ver refreshSong), que ademas es lo correcto — un golpe del pad 33
    //  se pinta del color del pad 33 y no del 01 del banco de delante — y el
    //  subindice se acota igual, porque una tabla correcta hoy no impide que
    //  alguien vuelva a pasar una corta manana.
    juce::Colour blockColour (int v) const
    {
        if (v > 0 && v != kContinued) return Zati::colour (v - 1);
        if (v < 0)
        {
            const int pad = -v - 1;
            if (zati == nullptr || ! juce::isPositiveAndBelow (pad, zatis))
            {
                ++zatisFueraDeRango;
                //  Su propio color, que es lo unico honesto cuando la tabla no
                //  lo tiene: Zati::colour ya envuelve con el modulo de los ocho.
                return Zati::colour (pad);
            }
            return Zati::colour (zati[pad]);
        }
        return ZatiColours::padBorder;
    }
    int findStart (int lane, int bar) const
    {
        for (int b = bar - 1; b >= 0; --b)
            if (data[lane * totalBars + b] != kContinued) return b;
        return -1;
    }

    const int* data = nullptr;
    const int* zati = nullptr;
    //  CUANTOS trae esa tabla. Sin este numero, `zati` es un puntero sin
    //  largo — que es literalmente el fallo que costo la lectura fuera de
    //  rango: se le pasaba la tabla de dieciseis del banco de delante y aqui
    //  se indexaba con un pad de 0 a 63. Un puntero sin su largo no se puede
    //  acotar, asi que tampoco se puede medir.
    int         zatis = 0;
    int totalBars = 8, pageIndex = 0, playing = -1;
    int cursor = -1;          // el compas que las herramientas van a tocar
    unsigned mute = 0;        // un bit por carril silenciado
    int lA = 0, lB = 0;       // el tramo en bucle, [A,B)

    //  La copia de lo ultimo PINTADO, para no volver a pintarlo. Ver setSource.
    //  El numero de compases lo elige la persona, asi que la sombra de la tabla
    //  crece con ella; los sesenta y cuatro zatis son fijos.
    std::vector<int> sombra;
    std::array<int, 64> sombraZati {};
    int  prevBars = -1, prevPage = -1, prevPlaying = -2, prevCursor = -2, prevLA = -1, prevLB = -1;
    unsigned prevMute = 0xFFFFFFFFu;
    bool visto = false;

    //  LA BANDA DE AUDIO. La tabla se presta y no se copia, igual que `data`.
    int              vista = vistaPatrones;
    const ClipVista* clips = nullptr;
    int              numClips = 0;
    int              clipSel  = -1;   // el que se esta moviendo, para que se vea
    int              arrastrado = -1; // el que este dedo agarro
    int              agarre = 0;      // por que compas suyo lo agarro
    int              asa = 0;         // -1 filo izquierdo, +1 derecho, 0 el medio
    unsigned         mudoAudio = 0;   // un bit por pista de audio silenciada
    juce::Colour     modoTinte { juce::Colours::transparentBlack };
};

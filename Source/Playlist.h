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

    // (lane, bar) — the host decides what to place or whether to clear.
    std::function<void (int lane, int bar)> onCell;
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

    void paint (juce::Graphics& g) override
    {
        if (data == nullptr) return;
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

    //  SE PINTA CON EL DEDO ARRASTRADO, como la rejilla de pasos y como el
    //  piano.
    //
    //  Solo habia mouseDown: escribir ocho compases eran ocho toques, y la
    //  regla escrita en CLAUDE.md dice que esta ficha no se desplaza
    //  precisamente porque "se pinta con el dedo arrastrado". O faltaba el
    //  gesto o sobraba el argumento; faltaba el gesto.
    //
    //  Con dos candados que la rejilla de pasos ya tiene por el mismo motivo:
    //  la canaleta del MUTE solo responde al TOQUE -arrastrar por ella
    //  silenciaria los cuatro carriles de una pasada- y una celda no se
    //  escribe dos veces seguidas, o pasar el dedo por encima de la misma
    //  casilla la enciende y la apaga a la velocidad de los eventos del raton.
    void mouseDown (const juce::MouseEvent& e) override { ultima = { -1, -1 }; toca (e, false); }
    void mouseDrag (const juce::MouseEvent& e) override { toca (e, true); }
    void mouseUp   (const juce::MouseEvent&)   override { ultima = { -1, -1 }; }

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
};

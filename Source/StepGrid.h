#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"

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
class StepGrid : public juce::Component
{
public:
    static constexpr int kLanes    = 16;   // pads
    static constexpr int kBarSteps = 16;   // steps shown at once
    //  La columna de los numeros de pad, a la izquierda de la rejilla. Estaba
    //  escrita a mano TRES veces - dos en el pintado y una en el acierto del
    //  toque - y esas tres tienen que decir lo mismo o los toques caen en una
    //  celda distinta de la que se ve. Un numero, un sitio.
    static constexpr int kGutter   = 30;

    // Called with the absolute step index (bar offset already applied).
    std::function<void (int pad, int step)> onCell;

    void setSource (const bool* cells,          // [step][pad] flattened, stride = kLanes
                    const int*  zati,           // per pad
                    const bool* loaded,         // per pad
                    const signed char* notes,   // [step][pad] semitone offset, same stride
                    int patternLength, int bar, int playStep, int selectedPad,
                    float stepPhase = 0.0f, int firstPad = 0)
    {
        data = cells; zatiOf = zati; loadedOf = loaded; noteOf = notes;
        patLen = patternLength; barIndex = bar; playing = playStep; selPad = selectedPad;
        //  EL NUMERO QUE SE PINTA EN EL CANALON ES EL DEL PAD, NO EL DEL CARRIL.
        //
        //  Esta rejilla trabaja en carriles - dieciseis, del 0 al 15 - y quien
        //  la usa le suma el banco. Pintar carril+1 daba 01..16 en los cuatro
        //  bancos, asi que en el banco B la cabecera decia "PAD 17 BD 808" y el
        //  canalon de esa misma pista decia 01: el mismo pad con dos nombres, y
        //  el que la persona lee para saber cual es era el falso.
        laneBase = firstPad;
        phase = juce::jlimit (0.0f, 1.0f, stepPhase);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (data == nullptr) return;

        auto r = getLocalBounds();
        const int gutter = kGutter;
        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float cellW = (float) (r.getWidth() - gutter) / (float) kBarSteps;
        const int   base  = barIndex * kBarSteps;

        for (int lane = 0; lane < kLanes; ++lane)
        {
            // Lane 0 is pad 01 at the top: this is a list of pads, read
            // downwards, not the bottom-up pad grid.
            const int pad = lane;
            const auto frag = Zati::colour (zatiOf[pad]);
            const bool has  = loadedOf[pad];
            const float y   = (float) r.getY() + laneH * (float) lane;

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

            for (int c = 0; c < kBarSteps; ++c)
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
                    const bool beat = (c % 4) == 0;
                    g.setColour (ZatiColours::groove (beat ? 0.48f : 0.28f));
                    g.fillRect (cell);
                }

            }
        }

        // Bar rules every 4 steps — structure, drawn over the cells.
        g.setColour (ZatiColours::groove (0.28f));
        for (int c = 4; c < kBarSteps; c += 4)
            g.fillRect ((float) r.getX() + gutter + cellW * (float) c - 0.5f,
                        (float) r.getY(), 1.0f, (float) r.getHeight());

        //  The playhead. It used to be a red outline drawn around each of the
        //  sixteen cells of the live column, which is sixteen boxes announcing
        //  one position - the eye reads a stack of empty frames, not a beat.
        //  One bar instead: the column it is over, and a line sliding across it
        //  with the step, so the grid has a hand sweeping over it.
        if (playing >= base && playing < base + kBarSteps && playing < patLen)
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

        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float cellW = (float) (r.getWidth() - gutter) / (float) kBarSteps;
        const int lane = juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH));
        const int col  = juce::jlimit (0, kBarSteps - 1, (int) ((float) (e.x - r.getX() - gutter) / cellW));
        const int step = barIndex * kBarSteps + col;
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
    int patLen = 16, barIndex = 0, playing = -1, selPad = -1, lastKey = -1;
    int laneBase = 0;      // el pad del carril 0: 0, 16, 32 o 48. Ver setSource.
    float phase = 0.0f;   // how far through the live step, 0..1
};

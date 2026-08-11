#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"

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
    static constexpr int kGutter = 26;

    static constexpr int kLanes    = 4;
    static constexpr int kBarsView = 8;    // bars visible at once; pages beyond

    // (lane, bar) — the host decides what to place or whether to clear.
    std::function<void (int lane, int bar)> onCell;

    void setSource (const int* cells,       // [lane][bar] flattened, stride = bars
                    const int* zatiOf,      // per pad, for one-shot colours
                    int bars, int page, int playBar)
    {
        data = cells; zati = zatiOf; totalBars = bars; pageIndex = page; playing = playBar;
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

            auto gut = juce::Rectangle<float> ((float) r.getX(), y, (float) gutter, laneH).reduced (1.0f, 1.0f);
            //  Mismo criterio que la rejilla de pasos: la canaleta del carril
            //  es una marca medida contra la tarjeta, no un token de la placa
            //  de pads - que en las dos carcasas oscuras sale mas claro que el
            //  fondo y convierte una fila vacia en una fila que parece llena.
            g.setColour (ZatiColours::markOn (ZatiColours::chassisTop, 0.18f));
            g.fillRect (gut);
            g.setColour (ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (juce::String (lane + 1), gut, juce::Justification::centred);

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
                    g.setColour (blockColour (sv).withAlpha (0.55f));
                    g.fillRect (cell.withTrimmedLeft (-1.5f));
                }
                else
                {
                    const auto col = blockColour (v);
                    g.setColour (col);
                    g.fillRect (cell);
                    g.setColour (ZatiColours::bestOn (col, ZatiColours::ink, juce::Colours::white));
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

        // Bar numbers along the top edge of the first lane.
        g.setColour (ZatiColours::inkDim.withAlpha (0.7f));
        g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
        for (int c = 0; c < kBarsView; c += 2)
            g.drawText (juce::String (base + c + 1),
                        (int) ((float) r.getX() + gutter + barW * (float) c) + 2, r.getY(),
                        (int) barW, 9, juce::Justification::topLeft);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (data == nullptr || ! onCell) return;
        auto r = getLocalBounds();
        const int gutter = kGutter;
        if (e.x < r.getX() + gutter) return;
        const float laneH = (float) r.getHeight() / (float) kLanes;
        const float barW  = (float) (r.getWidth() - gutter) / (float) kBarsView;
        const int lane = juce::jlimit (0, kLanes - 1, (int) ((float) (e.y - r.getY()) / laneH));
        const int bar  = pageIndex * kBarsView
                       + juce::jlimit (0, kBarsView - 1, (int) ((float) (e.x - r.getX() - gutter) / barW));
        if (bar >= totalBars) return;
        onCell (lane, bar);
    }

    static constexpr int kContinued = 1000;

private:
    // Pattern banks borrow the fragment palette so a block is recognisable at a
    // glance; a one-shot wears its own pad's zati.
    juce::Colour blockColour (int v) const
    {
        if (v > 0 && v != kContinued) return Zati::colour (v - 1);
        if (v < 0) return Zati::colour (zati != nullptr ? zati[-v - 1] : 0);
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
    int totalBars = 8, pageIndex = 0, playing = -1;
};

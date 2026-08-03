#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ZatiLookAndFeel.h"
#include "Zati.h"

// ============================================================================
//  PadButton — a sample pad tile: big index (Oswald), sample name (mono), and
//  a mini min/max waveform. States: empty / loaded / selected / playing, with
//  a trigger flash. Original ARTiFACTS "arctic" look. UI thread only.
// ============================================================================
class PadButton : public juce::Button
{
public:
    explicit PadButton (int idx) : juce::Button (juce::String (idx + 1)), index (idx) {}

    //  start01/end01 are the pad's own trim window. After an auto-chop all
    //  sixteen pads point at ONE buffer with sixteen windows, so a sparkline
    //  drawn from the whole buffer made every tile identical — half the tile
    //  carrying no information at all. Each pad draws only its slice.
    void setSampleInfo (SampleBuffer::Ptr sb, const juce::String& name,
                        float start01 = 0.0f, float end01 = 1.0f)
    {
        loaded = (sb != nullptr);
        padName = name;
        buildSpark (sb.get(), start01, end01);
        repaint();
    }
    void setZati (int z) { if (zati != z) { zati = z; repaint(); } }
    int  getZati() const { return zati; }
    void setSelected (bool s) { if (selected != s) { selected = s; repaint(); } }
    void setPlaying  (bool p) { if (playing  != p) { playing  = p; repaint(); } }
    void setFlash    (float f) { flash = f; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = 3.0f;   // square, not rounded — matches the flat button caps

        // A loaded pad wears its zati colour: 30% fill, full-strength border,
        // and a solid top stripe. The stripe plus the always-drawn number are
        // the non-chromatic reinforcement the spec requires — the pad must
        // still be readable when the hue is not.
        const juce::Colour frag = Zati::colour (zati);

        juce::Colour base   = loaded ? frag.withMultipliedAlpha (0.30f) : ZatiColours::padBg2;
        juce::Colour edge   = loaded ? frag : ZatiColours::padBorder;
        juce::Colour idxCol = loaded ? ZatiColours::ink.withAlpha (0.92f)
                                     : ZatiColours::ink.withAlpha (0.30f);
        //  inkDim on a 30% fragment fill measured 3.25-4.02:1 across the eight
        //  colours — under the 4.5 needed for 9px text on every one of them.
        //  Ink at 0.75 clears it on the worst (5.84:1) and still reads as
        //  secondary against the pad's own numeral.
        juce::Colour nmCol  = loaded ? ZatiColours::ink.withAlpha (0.75f)
                                     : ZatiColours::inkDim;
        juce::Colour sparkCol = loaded ? frag.darker (0.35f) : ZatiColours::ink.withAlpha (0.30f);
        bool onAccent = false;

        if (playing || flash > 0.55f)
        {
            onAccent = true;
            base   = frag;
            edge   = frag.brighter (0.35f);
            //  Pick by measured contrast, not by a brightness threshold. The
            //  old 0.55 cut put red and violet on white at 3.6:1 and 3.9:1
            //  when ink beats white on all eight fragment colours — the
            //  threshold was simply the wrong test.
            idxCol = ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white);
            nmCol  = idxCol.withAlpha (0.9f);
            sparkCol = idxCol.withAlpha (0.85f);
        }
        else if (flash > 0.0f)
        {
            base = base.interpolatedWith (frag, juce::jlimit (0.0f, 1.0f, flash));
        }
        if (down) base = base.darker (0.06f);

        // Body — flat fill, no gradient/sheen.
        g.setColour (base);
        g.fillRoundedRectangle (r, rad);

        // Top stripe (5px): the zati's identity, independent of the fill.
        if (loaded && ! onAccent)
        {
            g.setColour (frag);
            g.fillRect (r.withHeight (5.0f).reduced (1.0f, 0.0f).withY (r.getY() + 1.0f));
        }

        // Sparkline (behind the labels).
        if (loaded && spark.size() > 2)
        {
            auto wr = r.reduced (9.0f, 0.0f);
            const float cy = r.getCentreY() + 2.0f;
            const float halfH = 15.0f;
            const int n = (int) spark.size() / 2;
            g.setColour (sparkCol.withAlpha (onAccent ? 0.9f : 0.5f));
            juce::Path p;
            for (int i = 0; i < n; ++i)
            {
                const float x = wr.getX() + wr.getWidth() * (float) i / (float) (n - 1);
                const float yTop = cy - spark[(size_t) (i * 2 + 1)] * halfH;
                (i == 0) ? p.startNewSubPath (x, yTop) : p.lineTo (x, yTop);
            }
            for (int i = n - 1; i >= 0; --i)
            {
                const float x = wr.getX() + wr.getWidth() * (float) i / (float) (n - 1);
                const float yBot = cy - spark[(size_t) (i * 2)] * halfH;
                p.lineTo (x, yBot);
            }
            p.closeSubPath();
            g.fillPath (p);
        }

        // Index (Oswald) top-left.
        g.setColour (idxCol);
        g.setFont (ZatiColours::displayFont (juce::jmin (26.0f, r.getHeight() * 0.30f)));
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2),
                    r.reduced (9.0f, 6.0f).removeFromTop (r.getHeight() * 0.42f),
                    juce::Justification::topLeft);

        // Name (mono) bottom.
        g.setColour (nmCol);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta).withExtraKerningFactor (0.06f));
        g.drawText (loaded ? padName.toUpperCase() : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    r.reduced (10.0f, 7.0f).removeFromBottom (12.0f), juce::Justification::bottomLeft, true);

        // Border, then focus. A loaded pad's border is its zati; focus is a
        // second, achromatic ring outside it, so selection never overwrites
        // the fragment's colour with the chassis tone.
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, loaded ? 2.0f : 1.2f);

        if (selected && ! onAccent)
        {
            g.setColour (ZatiColours::ink.withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (2.4f), juce::jmax (1.0f, rad - 1.5f), 1.4f);
        }

        // Playing: brighten the pad's own colour rather than adding another.
        if (playing)
        {
            g.setColour ((loaded ? frag : ZatiColours::ink).brighter (0.45f).withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.8f);
        }
    }

private:
    void buildSpark (const SampleBuffer* sb, float start01, float end01)
    {
        spark.clearQuick();
        if (sb == nullptr) return;
        const int len = sb->buffer.getNumSamples();
        if (len < 4) return;

        const int a = juce::jlimit (0, len - 2, (int) (juce::jlimit (0.0f, 1.0f, start01) * (float) len));
        const int b = juce::jlimit (a + 1, len, (int) (juce::jlimit (0.0f, 1.0f, end01)   * (float) len));
        const int n = b - a;
        if (n < 2) return;

        // Normalise each slice to its own peak: a quiet tail slice would
        // otherwise draw as a flat line next to a loud transient one, and the
        // point of the tile art is telling them apart.
        const float* d = sb->buffer.getReadPointer (0);
        float peak = 0.0f;
        for (int i = a; i < b; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        const float norm = peak > 1.0e-4f ? 0.95f / peak : 1.4f;

        const int cols = 44;
        for (int c = 0; c < cols; ++c)
        {
            const int i0 = a + (int) ((juce::int64) c       * n / cols);
            const int i1 = a + (int) ((juce::int64) (c + 1) * n / cols);
            float mn = 0.0f, mx = 0.0f;
            for (int i = i0; i < i1 && i < b; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
            spark.add (juce::jlimit (-1.0f, 1.0f, mn * norm));
            spark.add (juce::jlimit (-1.0f, 1.0f, mx * norm));
        }
    }

    int index = 0;
    int zati = 0;                 // fragment colour, assigned by cut order
    bool loaded = false, selected = false, playing = false;
    float flash = 0.0f;
    juce::String padName;
    juce::Array<float> spark;   // interleaved min,max per column
};

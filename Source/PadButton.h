#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ShardLookAndFeel.h"
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

    void setSampleInfo (SampleBuffer::Ptr sb, const juce::String& name)
    {
        loaded = (sb != nullptr);
        padName = name;
        buildSpark (sb.get());
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

        juce::Colour base   = loaded ? frag.withMultipliedAlpha (0.30f) : ShardColours::padBg2;
        juce::Colour edge   = loaded ? frag : ShardColours::padBorder;
        juce::Colour idxCol = loaded ? ShardColours::ink.withAlpha (0.92f)
                                     : ShardColours::ink.withAlpha (0.30f);
        juce::Colour nmCol  = ShardColours::inkDim;
        juce::Colour sparkCol = loaded ? frag.darker (0.35f) : ShardColours::accent.withAlpha (0.55f);
        bool onAccent = false;

        if (playing || flash > 0.55f)
        {
            onAccent = true;
            base   = frag;
            edge   = frag.brighter (0.35f);
            const bool darkFrag = frag.getPerceivedBrightness() < 0.55f;
            idxCol = darkFrag ? juce::Colours::white : ShardColours::ink;
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
        g.setFont (ShardColours::displayFont (juce::jmin (26.0f, r.getHeight() * 0.30f)));
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2),
                    r.reduced (9.0f, 6.0f).removeFromTop (r.getHeight() * 0.42f),
                    juce::Justification::topLeft);

        // Name (mono) bottom.
        g.setColour (nmCol);
        g.setFont (ShardColours::monoFont (9.0f).withExtraKerningFactor (0.06f));
        g.drawText (loaded ? padName.toUpperCase() : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    r.reduced (10.0f, 7.0f).removeFromBottom (12.0f), juce::Justification::bottomLeft, true);

        // Border / selection.
        if (selected && ! onAccent)
        {
            g.setColour (ShardColours::accent);
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.6f);
        }
        else
        {
            g.setColour (edge);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
        }

        // Playing glow ring.
        if (playing)
        {
            g.setColour (ShardColours::accentBright.withAlpha (0.5f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.8f);
        }
    }

private:
    void buildSpark (const SampleBuffer* sb)
    {
        spark.clearQuick();
        if (sb == nullptr) return;
        const int len = sb->buffer.getNumSamples();
        if (len < 4) return;
        const float* d = sb->buffer.getReadPointer (0);
        const int cols = 44;
        for (int c = 0; c < cols; ++c)
        {
            const int i0 = (int) ((juce::int64) c       * len / cols);
            const int i1 = (int) ((juce::int64) (c + 1) * len / cols);
            float mn = 0.0f, mx = 0.0f;
            for (int i = i0; i < i1 && i < len; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
            spark.add (juce::jlimit (-1.0f, 1.0f, mn * 1.4f));
            spark.add (juce::jlimit (-1.0f, 1.0f, mx * 1.4f));
        }
    }

    int index = 0;
    int zati = 0;                 // fragment colour, assigned by cut order
    bool loaded = false, selected = false, playing = false;
    float flash = 0.0f;
    juce::String padName;
    juce::Array<float> spark;   // interleaved min,max per column
};

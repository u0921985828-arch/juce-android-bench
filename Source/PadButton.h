#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ShardLookAndFeel.h"

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
    void setSelected (bool s) { if (selected != s) { selected = s; repaint(); } }
    void setPlaying  (bool p) { if (playing  != p) { playing  = p; repaint(); } }
    void setFlash    (float f) { flash = f; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        const float rad = 13.0f;

        juce::Colour base   = loaded ? ShardColours::padTop : ShardColours::padBg2;
        juce::Colour edge   = ShardColours::padBorder;
        juce::Colour idxCol = loaded ? ShardColours::inkLight.withAlpha (0.95f)
                                     : ShardColours::inkLight.withAlpha (0.30f);
        juce::Colour nmCol  = ShardColours::inkDim;
        juce::Colour sparkCol = ShardColours::accent.withAlpha (0.55f);
        bool onAccent = false;

        if (playing || flash > 0.55f)
        {
            onAccent = true;
            base   = ShardColours::accent;
            edge   = ShardColours::accentBright;
            idxCol = juce::Colours::white;
            nmCol  = juce::Colours::white.withAlpha (0.9f);
            sparkCol = juce::Colours::white.withAlpha (0.9f);
        }
        else if (flash > 0.0f)
        {
            base   = base.interpolatedWith (ShardColours::accent, juce::jlimit (0.0f, 1.0f, flash));
        }
        if (down) base = base.darker (0.06f);

        // Body.
        juce::ColourGradient grad (base.brighter (onAccent ? 0.10f : 0.04f), r.getX(), r.getY(),
                                   base.darker (onAccent ? 0.10f : 0.06f),  r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, rad);

        // Top sheen.
        g.setColour (juce::Colours::white.withAlpha (onAccent ? 0.18f : 0.55f));
        g.drawLine (r.getX() + rad, r.getY() + 1.2f, r.getRight() - rad, r.getY() + 1.2f, 1.2f);

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
    bool loaded = false, selected = false, playing = false;
    float flash = 0.0f;
    juce::String padName;
    juce::Array<float> spark;   // interleaved min,max per column
};

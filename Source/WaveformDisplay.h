#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ShardLookAndFeel.h"

// ============================================================================
//  WaveformDisplay — the hero LCD screen: the selected pad's sample drawn as a
//  filled min/max envelope on a dark panel, with a tick ruler, blue trim
//  handles and name / sample-rate readouts. Message-thread only.
// ============================================================================
class WaveformDisplay : public juce::Component
{
public:
    void setSample (SampleBuffer::Ptr sb)
    {
        sample = sb;
        computeMinMax();
        repaint();
    }

    void setInfo (const juce::String& name, double sampleRate, double seconds, int channels)
    {
        infoName = name;
        infoRight = (sampleRate > 0.0)
                      ? juce::String (sampleRate / 1000.0, 1) + "k " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                        + (channels > 1 ? " STEREO " : " MONO ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " "
                        + juce::String (seconds, 2) + "s"
                      : juce::String();
        repaint();
    }

    void clear() { sample = nullptr; mins.clearQuick(); maxs.clearQuick(); infoName = {}; infoRight = {}; repaint(); }

    void setTrim (float startNorm, float endNorm)
    {
        start01 = juce::jlimit (0.0f, 1.0f, startNorm);
        end01   = juce::jlimit (0.0f, 1.0f, endNorm);
        repaint();
    }

    // One slice of the loaded source, owned by a pad. When several pads share
    // one buffer (what auto-chop produces) the display stops being "one pad's
    // sample" and becomes the map of the whole cut: every fragment drawn in
    // its own zati colour, which is the middle link of CUT -> PAD -> WAVEFORM
    // -> KNOBS.
    struct Segment
    {
        float start01 = 0.0f, end01 = 1.0f;
        juce::Colour colour;
        int padNumber = 0;
        bool selected = false;
    };

    void setSegments (juce::Array<Segment> segs)
    {
        segments = std::move (segs);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // LCD panel.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff112232), b.getCentreX(), b.getY(),
                                                 ShardColours::screenBg, b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 2.0f);

        const auto lcdFg = ShardColours::lcdFg, lcdDim = ShardColours::lcdDim, accent = ShardColours::accent;

        if (sample == nullptr || mins.isEmpty())
        {
            g.setColour (lcdDim);
            g.setFont (ShardColours::monoFont (11.0f, true).withExtraKerningFactor (0.18f));
            g.drawText ("TAP A PAD TO LOAD ITS WAVEFORM", getLocalBounds(), juce::Justification::centred);
            g.setColour (ShardColours::knobEdge.withAlpha (0.25f));
            g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
            return;
        }

        // Waveform region (between readout rows).
        auto wave = b.reduced (10.0f, 0.0f);
        wave.removeFromTop (22.0f);
        wave.removeFromBottom (20.0f);
        const float midY = wave.getCentreY();
        const float h    = wave.getHeight() * 0.5f - 2.0f;
        const int   W    = mins.size();

        // baseline
        g.setColour (activeColour (accent).withAlpha (0.28f));
        g.fillRect (wave.getX(), midY - 0.5f, wave.getWidth(), 1.0f);

        // filled envelope
        juce::Path top;
        for (int x = 0; x < W; ++x)
        {
            const float px = wave.getX() + (float) x / (float) (W - 1) * wave.getWidth();
            (x == 0 ? top.startNewSubPath (px, midY - maxs[x] * h) : top.lineTo (px, midY - maxs[x] * h));
        }
        for (int x = W - 1; x >= 0; --x)
        {
            const float px = wave.getX() + (float) x / (float) (W - 1) * wave.getWidth();
            top.lineTo (px, midY - mins[x] * h);
        }
        top.closeSubPath();

        if (segments.size() > 1)
        {
            // Chopped source: paint the envelope once per fragment, clipped to
            // that fragment's x range, so each slice carries its zati colour.
            for (const auto& s : segments)
            {
                const float x0 = wave.getX() + s.start01 * wave.getWidth();
                const float x1 = wave.getX() + s.end01   * wave.getWidth();
                if (x1 - x0 < 0.5f) continue;

                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (juce::Rectangle<float> (x0, wave.getY(),
                                                            x1 - x0, wave.getHeight()).toNearestInt());
                g.setColour (s.colour.withAlpha (s.selected ? 0.30f : 0.16f));
                g.fillPath (top);
                g.setColour (s.colour.withAlpha (s.selected ? 1.0f : 0.75f));
                g.strokePath (top, juce::PathStrokeType (s.selected ? 1.6f : 1.2f));
            }

            // Cut lines, in a neutral colour so they read as structure rather
            // than as another fragment.
            g.setColour (lcdDim.withAlpha (0.5f));
            for (int i = 1; i < segments.size(); ++i)
            {
                const float cx = wave.getX() + segments[i].start01 * wave.getWidth();
                for (float y = wave.getY(); y < wave.getBottom(); y += 5.0f)
                    g.fillRect (cx, y, 1.0f, 2.5f);
            }

            // Non-chromatic reinforcement: a 4px bar per fragment under the
            // wave, plus the pad number. Colour alone is never the signal.
            const float barY = wave.getBottom() + 1.0f;
            g.setFont (ShardColours::monoFont (8.0f, true));
            for (const auto& s : segments)
            {
                const float x0 = wave.getX() + s.start01 * wave.getWidth();
                const float x1 = wave.getX() + s.end01   * wave.getWidth();
                if (x1 - x0 < 1.5f) continue;

                g.setColour (s.colour.withAlpha (s.selected ? 1.0f : 0.7f));
                g.fillRect (x0 + 0.5f, barY, juce::jmax (1.0f, x1 - x0 - 1.0f), 4.0f);

                if (x1 - x0 > 16.0f)
                {
                    g.setColour (s.selected ? lcdFg : lcdDim);
                    g.drawText (juce::String (s.padNumber).paddedLeft ('0', 2),
                                juce::Rectangle<float> (x0, barY + 5.0f, x1 - x0, 9.0f),
                                juce::Justification::centred);
                }
            }
        }
        else
        {
            const juce::Colour one = segments.size() == 1 ? segments[0].colour : accent;
            g.setColour (one.withAlpha (0.18f));
            g.fillPath (top);
            g.setColour (one);
            g.strokePath (top, juce::PathStrokeType (1.3f));

            // dim trimmed-out regions
            const float sx = wave.getX() + start01 * wave.getWidth();
            const float ex = wave.getX() + end01   * wave.getWidth();
            g.setColour (ShardColours::screenBg.withAlpha (0.62f));
            g.fillRect (wave.getX(), wave.getY(), sx - wave.getX(), wave.getHeight());
            g.fillRect (ex, wave.getY(), wave.getRight() - ex, wave.getHeight());

            // trim handles
            g.setColour (one);
            for (float hx : { sx, ex })
            {
                g.drawLine (hx, wave.getY(), hx, wave.getBottom(), 1.6f);
                g.fillRect (hx - 3.0f, wave.getY(), 6.0f, 5.0f);
                g.fillRect (hx - 3.0f, wave.getBottom() - 5.0f, 6.0f, 5.0f);
            }
        }

        // tick ruler — only when the source is not already carrying the
        // per-fragment bars, which occupy the same strip.
        if (segments.size() <= 1)
        {
            g.setColour (lcdDim.withAlpha (0.45f));
            for (int k = 0; k <= 32; ++k)
            {
                const float tx = wave.getX() + wave.getWidth() * (float) k / 32.0f;
                const float th = (k % 4 == 0) ? 4.0f : 2.0f;
                g.fillRect (tx, wave.getBottom() + 6.0f, 1.0f, th);
            }
        }

        // readouts
        g.setFont (ShardColours::monoFont (10.5f, true).withExtraKerningFactor (0.08f));
        auto top2 = b.reduced (11.0f, 7.0f).removeFromTop (13.0f);
        g.setColour (activeColour (accent));
        g.fillEllipse (top2.getX(), top2.getCentreY() - 3.0f, 6.0f, 6.0f);
        g.setColour (lcdFg);
        g.drawText (infoName, top2.withTrimmedLeft (12), juce::Justification::topLeft);
        g.setColour (lcdDim);
        g.drawText (infoRight, top2, juce::Justification::topRight);

        // TRIM only makes sense for a single window; on a chopped source each
        // fragment carries its own, and the text would collide with their
        // numbers in the same strip.
        if (segments.size() <= 1)
        {
            auto bot = b.reduced (11.0f, 6.0f).removeFromBottom (12.0f);
            g.setColour (lcdDim);
            g.setFont (ShardColours::monoFont (9.5f, true));
            g.drawText ("TRIM " + juce::String (start01, 2) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + juce::String (end01, 2),
                        bot, juce::Justification::bottomLeft);
        }

        g.setColour (ShardColours::knobEdge.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
    }

    void resized() override { computeMinMax(); repaint(); }

private:
    void computeMinMax()
    {
        mins.clearQuick(); maxs.clearQuick();
        if (sample == nullptr) return;
        auto& buf = sample->buffer;
        const int len = buf.getNumSamples();
        if (len < 1 || buf.getNumChannels() < 1) return;

        const int W = juce::jmax (1, getWidth() > 0 ? getWidth() - 20 : 320);
        const float* d = buf.getReadPointer (0);
        for (int x = 0; x < W; ++x)
        {
            int a = (int) ((juce::int64) x * len / W);
            int e = (int) ((juce::int64) (x + 1) * len / W);
            if (e <= a) e = a + 1;
            if (e > len) e = len;
            float mn = 1.0f, mx = -1.0f;
            for (int i = a; i < e; ++i) { const float s = d[i]; mn = juce::jmin (mn, s); mx = juce::jmax (mx, s); }
            mins.add (juce::jlimit (-1.0f, 1.0f, mn));
            maxs.add (juce::jlimit (-1.0f, 1.0f, mx));
        }
    }

    juce::Colour activeColour (juce::Colour fallback) const
    {
        for (const auto& s : segments)
            if (s.selected) return s.colour;
        return segments.size() == 1 ? segments[0].colour : fallback;
    }

    juce::Array<Segment> segments;
    SampleBuffer::Ptr sample;
    juce::Array<float> mins, maxs;
    float start01 = 0.0f, end01 = 1.0f;
    juce::String infoName, infoRight;
};

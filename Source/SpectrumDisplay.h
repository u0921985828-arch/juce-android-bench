#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"

// ============================================================================
//  SpectrumDisplay — the "screen": a real-time waveform OSCILLOSCOPE on a dark
//  amber-phosphor LCD panel (flat baseline when idle, the output waveform
//  bulges in when a pad plays). Fed post-FX mono master samples via
//  setSamples() from the message thread. Cosmetic (benign data race ok).
//
//  Chrome mirrors a hardware sampler display: corner labels, centred BPM, a
//  faint tick ruler and a bottom status line. Our own palette and layout.
// ============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay() = default;

    void setSamples (const float* src, int n)
    {
        count = juce::jmin (n, kCap);
        float pk = 0.0f;
        for (int i = 0; i < count; ++i)
        {
            const float s = src[i];
            buf[i] = s;
            pk = juce::jmax (pk, std::abs (s));
        }
        peak = juce::jmax (peak * 0.72f, pk);   // meter with a soft decay

        //  Silence twice running draws the same flat line, and this is the
        //  largest component on the face: repainting it thirty times a second
        //  while nothing is playing is the app's biggest idle cost. The VU and
        //  the step LEDs ask for their own repaints when THEY change, so
        //  nothing is missed by sitting still here.
        const bool silent = (pk <= 0.0f && peak < 0.0005f);
        if (silent && wasSilent) return;

        wasSilent = silent;
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

    //  The two meters that used to live outside, on strips of their own above
    //  and below the panel. A hardware sampler puts them ON the screen: the
    //  level and the playhead are things you read WHILE watching the wave, and
    //  splitting them across three separate boxes made the face taller and the
    //  screen smaller for no gain at all.
    void setVu (float l, float r)
    {
        if (std::abs (l - vuL) < 0.002f && std::abs (r - vuR) < 0.002f) return;
        vuL = l; vuR = r;
        repaint();
    }

    //  step is the absolute step, or negative when the transport is stopped;
    //  the colour is the bank that is playing, so the strip says WHICH pattern
    //  as well as where in it.
    void setStep (int absoluteStep, juce::Colour bankColour)
    {
        if (absoluteStep == step && bankColour == stepColour) return;
        step = absoluteStep;
        stepColour = bankColour;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // LCD panel (square) + subtle top scan glow.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (b, 2.0f);
        juce::ColourGradient glow (ZatiColours::lcdFg.withAlpha (0.05f), b.getCentreX(), b.getY(),
                                   ZatiColours::screenBg.withAlpha (0.0f), b.getCentreX(), b.getY() + b.getHeight() * 0.6f, false);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (b, 2.0f);

        //  Scan lines. A black rectangle on paper reads as a hole cut in the
        //  panel; the same rectangle with a line structure in it reads as a
        //  screen switched on. Three pixels apart and barely there - at full
        //  strength it would be a texture competing with the waveform.
        g.setColour (ZatiColours::lcdFg.withAlpha (0.035f));
        for (float y = b.getY() + 2.0f; y < b.getBottom() - 1.0f; y += 3.0f)
            g.fillRect (b.getX() + 1.0f, y, b.getWidth() - 2.0f, 1.0f);

        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.08f));

        // Corner + centre labels (top row) — cool LCD ink.
        auto top = b.reduced (10.0f, 6.0f).removeFromTop (13.0f);
        g.setColour (ZatiColours::lcdFg.withAlpha (0.9f));
        g.drawText (readout, top, juce::Justification::topLeft);
        g.drawText ("BPM:" + juce::String (bpm, 1), top, juce::Justification::topRight);
        g.setColour (ZatiColours::lcdDim);
        g.drawText (T ("OUT") + " " + peakDb(), top, juce::Justification::centredTop);

        //  Stereo VU, immediately under the labels: two rows of segments in a
        //  gutter narrow enough for the L and the R to sit beside them.
        {
            auto vu = b.reduced (8.0f, 0.0f).withY (b.getY() + 22.0f).withHeight (16.0f);
            auto gutter = vu.removeFromLeft (12.0f);

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (8.0f, true));
            g.drawText ("L", gutter.withHeight (8.0f), juce::Justification::centredLeft);
            g.drawText ("R", gutter.withHeight (8.0f).withY (gutter.getY() + 8.0f), juce::Justification::centredLeft);

            const int nSeg = 32;
            const float segW = vu.getWidth() / (float) nSeg;

            auto row = [&] (float level, juce::Rectangle<float> r)
            {
                const int lit = (int) std::round (std::sqrt (juce::jlimit (0.0f, 1.0f, level)) * (float) nSeg);
                for (int i = 0; i < nSeg; ++i)
                {
                    const bool hot = i >= (int) ((float) nSeg * 0.82f);
                    //  A LIT segment has to be the LCD's ink, not the
                    //  chassis accent. The accent is a near-black - it is what
                    //  a pressed key wears on a paper face - and painting it
                    //  on a near-black screen made a lit segment look exactly
                    //  like an unlit one. The meter has never shown a level.
                    g.setColour (i < lit ? (hot ? ZatiColours::red : ZatiColours::lcdFg)
                                         : ZatiColours::lcdFg.withAlpha (0.10f));
                    g.fillRect (vu.getX() + (float) i * segW + 0.5f, r.getY(), segW - 1.0f, r.getHeight());
                }
            };

            row (vuL, vu.withHeight (5.0f).withY (vu.getY() + 1.0f));
            row (vuR, vu.withHeight (5.0f).withY (vu.getY() + 8.0f));
        }

        // Waveform area (between the meters and the bottom furniture).
        auto wave = b.reduced (8.0f, 0.0f);
        wave.removeFromTop (22.0f + 16.0f);
        wave.removeFromBottom (20.0f + 10.0f);
        const float cy = wave.getCentreY();
        const float halfH = wave.getHeight() * 0.5f - 2.0f;

        //  Flat baseline. Same story as the meter above: this was drawn in
        //  the chassis accent, which is near-black, on a near-black panel.
        g.setColour (ZatiColours::lcdFg.withAlpha (0.30f));
        g.fillRect (wave.getX(), cy - 0.6f, wave.getWidth(), 1.2f);

        // Min/max waveform envelope, one vertical segment per pixel column — accent.
        if (count > 1)
        {
            const int cols = juce::jmax (1, (int) wave.getWidth());
            const float gain = 2.6f;   // lift quiet output into view
            for (int x = 0; x < cols; ++x)
            {
                const int i0 = (int) ((float)  x      / (float) cols * (float) count);
                const int i1 = (int) ((float) (x + 1) / (float) cols * (float) count);
                float mn = 0.0f, mx = 0.0f;
                for (int i = i0; i < i1 && i < count; ++i)
                {
                    mn = juce::jmin (mn, buf[i]);
                    mx = juce::jmax (mx, buf[i]);
                }
                const float yTop = cy - juce::jlimit (-halfH, halfH, mx * gain * halfH);
                const float yBot = cy - juce::jlimit (-halfH, halfH, mn * gain * halfH);
                const float amp  = juce::jlimit (0.0f, 1.0f, (mx - mn) * gain);
                const float fx   = wave.getX() + (float) x;
                g.setColour (ZatiColours::lcdFg.withAlpha (0.4f + 0.55f * amp));
                g.fillRect (fx, yTop, 1.0f, juce::jmax (1.0f, yBot - yTop));
            }
        }

        //  Sixteen step LEDs where the tick ruler used to be. The ruler was
        //  decoration measuring nothing; this measures the bar, and the beats
        //  are the ones that stay lit when the transport is stopped.
        {
            auto strip = juce::Rectangle<float> (wave.getX(), b.getBottom() - 28.0f,
                                                 wave.getWidth(), 8.0f);
            const float segW = strip.getWidth() / 16.0f;
            const int cur = step >= 0 ? step % 16 : -1;

            for (int i = 0; i < 16; ++i)
            {
                auto r = juce::Rectangle<float> (strip.getX() + (float) i * segW + 1.5f, strip.getY(),
                                                 segW - 3.0f, strip.getHeight());
                if (i == cur)
                {
                    g.setColour (stepColour);
                    g.fillRoundedRectangle (r, 1.5f);
                }
                else
                {
                    g.setColour (ZatiColours::lcdFg.withAlpha ((i % 4 == 0) ? 0.30f : 0.12f));
                    g.drawRoundedRectangle (r.reduced (0.5f), 1.5f, 1.0f);
                }
            }
        }

        auto status = b.reduced (10.0f, 5.0f).removeFromBottom (12.0f);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("SCOPE"), status, juce::Justification::bottomLeft);
        g.setColour (peak > 0.0005f ? ZatiColours::lcdFg : ZatiColours::lcdDim);
        g.drawText (peak > 0.0005f ? "SIG" : "--", status, juce::Justification::bottomRight);

        // LCD inner bezel.
        g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
    }

private:
    juce::String peakDb() const
    {
        if (peak < 0.0005f) return juce::String ("-inf");
        return juce::String ((int) juce::Decibels::gainToDecibels (peak)) + "dB";
    }

    static constexpr int kCap = 1024;
    float        buf[kCap] {};
    int          count { 0 };
    float        peak  { 0.0f };
    double       bpm   { 120.0 };
    float        vuL   { 0.0f }, vuR { 0.0f };
    int          step  { -1 };
    bool         wasSilent { false };
    juce::Colour stepColour { ZatiColours::lcdFg };
    juce::String readout { "ZATI" };
};

#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"

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
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

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

        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.08f));

        // Corner + centre labels (top row) — cool LCD ink.
        auto top = b.reduced (10.0f, 6.0f).removeFromTop (13.0f);
        g.setColour (ZatiColours::lcdFg.withAlpha (0.9f));
        g.drawText (readout, top, juce::Justification::topLeft);
        g.drawText ("BPM:" + juce::String (bpm, 1), top, juce::Justification::topRight);
        g.setColour (ZatiColours::lcdDim);
        g.drawText (juce::String ("OUT ") + peakDb(), top, juce::Justification::centredTop);

        // Waveform area (between the top labels and the bottom status line).
        auto wave = b.reduced (8.0f, 0.0f);
        wave.removeFromTop (22.0f);
        wave.removeFromBottom (20.0f);
        const float cy = wave.getCentreY();
        const float halfH = wave.getHeight() * 0.5f - 2.0f;

        // Flat baseline (shows through when idle) — accent.
        g.setColour (ZatiColours::amber.withAlpha (0.30f));
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
                g.setColour (ZatiColours::amber.withAlpha (0.4f + 0.55f * amp));
                g.fillRect (fx, yTop, 1.0f, juce::jmax (1.0f, yBot - yTop));
            }
        }

        // Bottom: faint tick ruler + status line.
        const float ry = b.getBottom() - 17.0f;
        g.setColour (ZatiColours::lcdDim.withAlpha (0.5f));
        for (int k = 0; k <= 32; ++k)
        {
            const float tx = wave.getX() + wave.getWidth() * (float) k / 32.0f;
            const float th = (k % 4 == 0) ? 4.0f : 2.0f;
            g.fillRect (tx, ry - th, 1.0f, th);
        }

        auto status = b.reduced (10.0f, 5.0f).removeFromBottom (12.0f);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText ("SCOPE", status, juce::Justification::bottomLeft);
        g.setColour (peak > 0.0005f ? ZatiColours::amber : ZatiColours::lcdDim);
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
    juce::String readout { "ZATI" };
};

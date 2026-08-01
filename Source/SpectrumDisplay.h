#pragma once

#include <JuceHeader.h>
#include "ShardLookAndFeel.h"

// ============================================================================
//  SpectrumDisplay — the "screen": a real-time FFT spectrum on a dark phosphor
//  panel. Fed mono master samples via setSamples() (message thread), does its
//  own FFT, and draws amber bars with a decay. Cosmetic (benign data race ok).
// ============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay()
        : fft (fftOrder),
          window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
    {}

    void setSamples (const float* src, int n)
    {
        const int c = juce::jmin (n, fftSize);
        for (int i = 0; i < c; ++i)        fftData[i] = src[i];
        for (int i = c; i < 2 * fftSize; ++i) fftData[i] = 0.0f;

        window.multiplyWithWindowingTable (fftData, (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData);

        for (int i = 0; i < numBars; ++i)
        {
            const float prop = (float) i / (float) numBars;
            const int   bin  = juce::jlimit (1, fftSize / 2 - 1,
                                             (int) (std::pow (prop, 2.0f) * (fftSize / 2)));
            const float db   = juce::Decibels::gainToDecibels (fftData[bin])
                             - juce::Decibels::gainToDecibels ((float) fftSize);
            const float norm = juce::jmap (juce::jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, 0.0f, 1.0f);
            mags[i] = juce::jmax (norm, mags[i] * 0.82f);   // peak + decay
        }
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (ShardColours::screenBg);
        g.fillRoundedRectangle (b, 6.0f);

        // faint baseline grid
        g.setColour (ShardColours::amber.withAlpha (0.08f));
        for (int k = 1; k < 4; ++k)
        {
            const float yy = b.getY() + b.getHeight() * (float) k / 4.0f;
            g.drawHorizontalLine ((int) yy, b.getX() + 4.0f, b.getRight() - 4.0f);
        }

        const float bw = (b.getWidth() - 8.0f) / (float) numBars;
        for (int i = 0; i < numBars; ++i)
        {
            const float bh = mags[i] * (b.getHeight() - 22.0f);
            const float bx = b.getX() + 4.0f + i * bw;
            g.setColour (ShardColours::amber.withAlpha (0.35f + 0.55f * mags[i]));
            g.fillRect (bx + 0.5f, b.getBottom() - 6.0f - bh, bw - 1.0f, bh);
        }

        g.setColour (ShardColours::amber.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (12.0f)).withExtraKerningFactor (0.15f));
        g.drawText (readout, getLocalBounds().reduced (10, 6), juce::Justification::topLeft);

        g.setColour (ShardColours::amber.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 6.0f, 1.4f);
    }

private:
    static constexpr int fftOrder = 10;
    static constexpr int fftSize   = 1 << fftOrder;   // 1024
    static constexpr int numBars   = 56;

    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    float fftData[2 * fftSize] {};
    float mags[numBars] {};
    juce::String readout { "SHARD" };
};

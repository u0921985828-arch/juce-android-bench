#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

// ============================================================================
//  WaveformDisplay — draws the selected pad's sample (min/max per column) and
//  the trim window (start/end handles). Message-thread only; holds a
//  ref-counted copy of the buffer so it stays alive while displayed.
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

    void clear()
    {
        sample = nullptr;
        mins.clearQuick();
        maxs.clearQuick();
        repaint();
    }

    // Normalised trim positions (0..1).
    void setTrim (float startNorm, float endNorm)
    {
        start01 = juce::jlimit (0.0f, 1.0f, startNorm);
        end01   = juce::jlimit (0.0f, 1.0f, endNorm);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff11141a));
        auto b = getLocalBounds().toFloat();

        if (sample == nullptr || mins.isEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText ("select a pad to see its waveform", getLocalBounds(),
                        juce::Justification::centred);
            return;
        }

        const int   W    = mins.size();
        const float midY = b.getCentreY();
        const float h    = b.getHeight() * 0.48f;

        g.setColour (juce::Colour (0xff1fb6a6));
        for (int x = 0; x < W; ++x)
        {
            const float px = b.getX() + (float) x / (float) W * b.getWidth();
            g.drawLine (px, midY - maxs[x] * h, px, midY - mins[x] * h, 1.0f);
        }

        // Dim the trimmed-out regions + draw the handles.
        const float sx = b.getX() + start01 * b.getWidth();
        const float ex = b.getX() + end01   * b.getWidth();
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect (b.getX(), b.getY(), sx - b.getX(), b.getHeight());
        g.fillRect (ex, b.getY(), b.getRight() - ex, b.getHeight());
        g.setColour (juce::Colour (0xffe0a13a));
        g.drawLine (sx, b.getY(), sx, b.getBottom(), 2.0f);
        g.drawLine (ex, b.getY(), ex, b.getBottom(), 2.0f);
    }

    void resized() override { computeMinMax(); repaint(); }

private:
    void computeMinMax()
    {
        mins.clearQuick();
        maxs.clearQuick();
        if (sample == nullptr) return;

        auto& buf = sample->buffer;
        const int len = buf.getNumSamples();
        if (len < 1 || buf.getNumChannels() < 1) return;

        const int W = juce::jmax (1, getWidth() > 0 ? getWidth() : 320);
        const float* d = buf.getReadPointer (0);
        for (int x = 0; x < W; ++x)
        {
            int a = (int) ((juce::int64) x * len / W);
            int e = (int) ((juce::int64) (x + 1) * len / W);
            if (e <= a) e = a + 1;
            if (e > len) e = len;
            float mn = 1.0f, mx = -1.0f;
            for (int i = a; i < e; ++i) { const float s = d[i]; mn = juce::jmin (mn, s); mx = juce::jmax (mx, s); }
            mins.add (mn);
            maxs.add (mx);
        }
    }

    SampleBuffer::Ptr sample;
    juce::Array<float> mins, maxs;
    float start01 = 0.0f, end01 = 1.0f;
};

#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"

// ============================================================================
//  SpectrumDisplay — the "screen": a real-time FFT SPECTRUM ANALYSER on a dark
//  LCD panel, rebuilt to behave like the Web Audio AnalyserNode FX-404 drew
//  its spectrum with (see analyse()). Fed post-FX mono master samples via
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

        analyse();

        //  Silence twice running draws the same flat line, and this is the
        //  largest component on the face: repainting it thirty times a second
        //  while nothing is playing is the app's biggest idle cost. The VU and
        //  the step LEDs ask for their own repaints when THEY change, so
        //  nothing is missed by sitting still here.
        //  Bars and peak caps have to keep FALLING after the sound stops, so
        //  the screen can only be left alone once everything has actually
        //  reached the floor - not on the first silent block.
        bool settled = true;
        for (const auto m : mag)      if (m > 1.0e-5f) { settled = false; break; }
        if (settled)
            for (const auto c : capLevel) if (c > 0.02f) { settled = false; break; }

        const bool silent = (pk <= 0.0f && peak < 0.0005f && settled);
        if (silent && wasSilent) return;

        wasSilent = silent;
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

    //  ------------------------------------------------------------------
    //  The analyser, rebuilt from what FX-404 was.
    //
    //  FX-404 drew its spectrum in a WebView, and a WebView has exactly one
    //  way to do that: an AnalyserNode and getByteFrequencyData. So the
    //  behaviour is not a matter of taste, it is a specification - and these
    //  are its numbers, straight out of the Web Audio defaults:
    //
    //      fftSize               2048   (we use 1024; the LCD is 300 px wide)
    //      minDecibels           -100
    //      maxDecibels            -30
    //      smoothingTimeConstant  0.8
    //
    //  The smoothing is the part that makes it LOOK like the browser rather
    //  than like an engineering plot: the running average is taken over the
    //  MAGNITUDES, before the decibel conversion, which is why the bars fall
    //  slowly and rise instantly. Averaging the dB values instead gives a
    //  sluggish, mushy meter that nobody would recognise.
    //  ------------------------------------------------------------------
    void analyse()
    {
        //  A ring, not the batch we were just handed. How much arrives per
        //  tick is a DEVICE decision - DeviceTier hands an entry-level phone
        //  256 scope points and a flagship 1024 - so a window taken from one
        //  batch would be full on one phone and impossible on another, and
        //  the analyser would simply not exist on the cheap ones.
        for (int i = 0; i < count; ++i)
        {
            ring[(size_t) ringPos] = buf[i];
            ringPos = (ringPos + 1) & (kFftSize - 1);
        }
        ringFilled = juce::jmin (kFftSize, ringFilled + count);

        if (ringFilled < kFftSize)
        {
            for (auto& m : mag) m *= kSmoothing;    // not enough history yet
            return;
        }

        //  Hann window over the newest kFftSize samples, oldest first. Without
        //  it every bar leaks into its neighbours and the whole spectrum turns
        //  into one wide blur.
        for (int i = 0; i < kFftSize; ++i)
        {
            const float w = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (kFftSize - 1)));
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (kFftSize - 1))] * w;
        }
        std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        //  Web Audio's smoothing, on the magnitudes, exactly as specified.
        const float norm = 2.0f / (float) kFftSize;
        for (int i = 0; i < kNumBins; ++i)
            mag[(size_t) i] = kSmoothing * mag[(size_t) i]
                            + (1.0f - kSmoothing) * fftData[(size_t) i] * norm;
    }

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

        //  THE SPECTRUM. Bars standing on a floor, not a trace through a
        //  middle: a spectrum has no negative half, so anchoring it to the
        //  centre - which is what the oscilloscope that used to live here did
        //  - would waste half the screen and read as the wrong instrument.
        const float floorY = wave.getBottom();
        const float fullH  = wave.getHeight() - 1.0f;
        juce::ignoreUnused (cy, halfH);

        //  Baseline the bars stand on.
        g.setColour (ZatiColours::lcdFg.withAlpha (0.30f));
        g.fillRect (wave.getX(), floorY - 0.6f, wave.getWidth(), 1.2f);

        {
            //  Linear bins across the band, the way getByteFrequencyData
            //  hands them over. Only the bottom slice is drawn: above about
            //  14 kHz a sampler's output is empty on every phone speaker
            //  there is, and eleven dead bars on the right would only make
            //  the live ones narrower.
            const int   usable = (int) (kNumBins * 0.58f);
            const float barGap = 1.0f;
            const float slotW  = wave.getWidth() / (float) kNumBars;

            for (int bIdx = 0; bIdx < kNumBars; ++bIdx)
            {
                const int i0 = (int) ((float)  bIdx      / (float) kNumBars * (float) usable);
                const int i1 = juce::jmax (i0 + 1,
                              (int) ((float) (bIdx + 1) / (float) kNumBars * (float) usable));

                float m = 0.0f;
                for (int i = i0; i < i1 && i < kNumBins; ++i)
                    m = juce::jmax (m, mag[(size_t) i]);

                //  Magnitude -> dB -> 0..1 over [minDecibels, maxDecibels].
                //  This is byteValue/255 with the division left out.
                const float db  = juce::Decibels::gainToDecibels (m, kMinDb);
                const float lvl = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));

                const float h = lvl * fullH;
                const float x = wave.getX() + (float) bIdx * slotW;

                //  A floor of one pixel so the analyser reads as an analyser
                //  when it is quiet, instead of vanishing into the baseline.
                g.setColour (ZatiColours::lcdFg.withAlpha (0.16f));
                g.fillRect (x, floorY - 1.0f, slotW - barGap, 1.0f);

                if (h > 1.0f)
                {
                    g.setColour (ZatiColours::lcdFg.withAlpha (0.45f + 0.55f * lvl));
                    g.fillRect (x, floorY - h, slotW - barGap, h);
                }

                //  Peak caps: the mark that hangs above a bar and slides
                //  down after it. It is what makes a bar chart read as a
                //  METER - without it you cannot see what you just missed.
                float& cap = capLevel[(size_t) bIdx];
                cap = (lvl >= cap) ? lvl : juce::jmax (0.0f, cap - 0.025f);
                if (cap > 0.02f)
                {
                    g.setColour (ZatiColours::lcdFg.withAlpha (0.85f));
                    g.fillRect (x, floorY - cap * fullH - 1.5f, slotW - barGap, 1.5f);
                }
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
        g.drawText (T ("ESPECTRO"), status, juce::Justification::bottomLeft);
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
    //  Analyser state. kCap is the scope ring the engine fills; the FFT
    //  reads the newest kFftSize of it.
    static constexpr int   kFftSize   = 1024;
    static constexpr int   kNumBins   = kFftSize / 2;
    static constexpr int   kNumBars   = 48;
    static constexpr float kSmoothing = 0.8f;    // Web Audio smoothingTimeConstant
    static constexpr float kMinDb     = -100.0f; // Web Audio minDecibels
    static constexpr float kMaxDb     = -30.0f;  // Web Audio maxDecibels

    juce::dsp::FFT                        fft { 10 };   // 2^10 = kFftSize
    std::array<float, (size_t) kFftSize>     ring {};
    int                                      ringPos    = 0;
    int                                      ringFilled = 0;
    std::array<float, (size_t) kFftSize * 2> fftData {};
    std::array<float, (size_t) kNumBins>     mag {};
    std::array<float, (size_t) kNumBars>     capLevel {};

    float        vuL   { 0.0f }, vuR { 0.0f };
    int          step  { -1 };
    bool         wasSilent { false };
    juce::Colour stepColour { ZatiColours::lcdFg };
    juce::String readout { "ZATI" };
};

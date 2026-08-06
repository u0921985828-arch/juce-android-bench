#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"

// ============================================================================
//  SpectrumDisplay — the "screen", ported from FX-404's own drawSpectrum().
//
//  Despite every name in it, this is NOT a frequency analyser. FX-404 called
//  it a spectrum and then used the FFT for exactly one number: a broadband
//  loudness proxy, the mean of getByteFrequencyData across all bins. What the
//  bars actually show is the SHAPE OF THE LOOP - 28 time-slices of the
//  current pattern, each holding the loudest moment the playhead saw while it
//  sat on that slice. By the time a pass finishes the whole strip reads as
//  the shape of the beat: kick here, snare there, hats running through.
//
//  That is a far better thing to put on a sampler than a frequency plot, and
//  it is why an instantaneous analyser looked wrong. Fed post-FX mono master
//  samples via setSamples() from the message thread (benign data race ok).
//
//  Chrome mirrors a hardware sampler display: corner labels, centred BPM, a
//  faint tick ruler and a bottom status line. Our own palette and layout.
// ============================================================================
class SpectrumDisplay : public juce::Component
{
public:
    SpectrumDisplay() { barLevels.fill (kFloorPct); }

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
        //  The shape has to keep SETTLING after the sound stops - that is the
        //  whole point of the decay curve - so the screen can only be left
        //  alone once every bar has actually reached the floor.
        bool settled = true;
        for (const auto lv : barLevels) if (lv > kFloorPct + 0.01f) { settled = false; break; }

        const bool silent = (pk <= 0.0f && peak < 0.0005f && settled && ! playing);
        if (silent && wasSilent) return;

        wasSilent = silent;
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

    //  ------------------------------------------------------------------
    //  The analyser. FX-404's settings verbatim:
    //
    //      analyser.fftSize              = 2048
    //      analyser.smoothingTimeConstant = 0.35
    //      minDecibels / maxDecibels      = -100 / -30   (Web Audio defaults,
    //                                                     never overridden)
    //
    //  and its output is used for exactly one thing, which its own comment
    //  spells out: "used only as a broadband loudness proxy now". The mean of
    //  getByteFrequencyData over every bin. That single number is what drives
    //  the bars; the shape on screen is TIME, not frequency.
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
            return;                       // not enough history yet

        //  Hann window over the newest kFftSize samples, oldest first.
        for (int i = 0; i < kFftSize; ++i)
        {
            const float w = 0.5f * (1.0f - std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (kFftSize - 1)));
            fftData[(size_t) i] = ring[(size_t) ((ringPos + i) & (kFftSize - 1))] * w;
        }
        std::fill (fftData.begin() + kFftSize, fftData.end(), 0.0f);

        fft.performFrequencyOnlyForwardTransform (fftData.data());

        //  Web Audio's smoothing, on the magnitudes, before the dB conversion.
        const float norm = 2.0f / (float) kFftSize;
        double sum = 0.0;
        for (int i = 0; i < kNumBins; ++i)
        {
            float& m = mag[(size_t) i];
            m = kSmoothing * m + (1.0f - kSmoothing) * fftData[(size_t) i] * norm;

            //  getByteFrequencyData, without the quantisation to a byte:
            //  255 * (dB - minDb) / (maxDb - minDb), clamped.
            const float db = juce::Decibels::gainToDecibels (m, kMinDb);
            sum += juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
        }

        //  const loudnessPct = Math.max(4, (sum/freqData.length/255)*100)
        loudnessPct = juce::jmax (kFloorPct, (float) (sum / (double) kNumBins) * 100.0f);

        advanceBars();
    }

    //  ------------------------------------------------------------------
    //  drawSpectrum()'s bar logic, line for line.
    //
    //  While the transport runs, the bar under the playhead holds the loudest
    //  moment it has seen this pass and keeps that height until the loop comes
    //  round again - which is what turns a flicker-per-hit into the shape of a
    //  whole beat. When it stops, the shape settles back down rather than
    //  freezing forever.
    //  ------------------------------------------------------------------
    void advanceBars()
    {
        //  FX-404 ran this on requestAnimationFrame, so its decay is per
        //  60 fps frame. ZATI's tick is whatever the device tier chose - 100,
        //  60, 40 or 33 ms - so the same decay has to be scaled by the time
        //  that actually passed, or the shape would settle four times faster
        //  on a flagship than on an entry-level phone.
        const auto now = juce::Time::getMillisecondCounter();
        const float dtFrames = (lastTickMs == 0) ? 1.0f
                             : juce::jlimit (0.5f, 8.0f, (float) (now - lastTickMs) / 16.667f);
        lastTickMs = now;

        if (playing && step >= 0 && patternLen > 0)
        {
            if (step < lastStepForWave)
                barLevels.fill (kFloorPct);          // wrapped - this pass starts fresh

            lastStepForWave = step;

            //  FX-404 mapped STEP -> bar. Its patterns were 24 to 64 steps, so
            //  every one of the 28 bars got a step and the strip was solid.
            //  ZATI's start at 16, and that same mapping leaves twelve bars
            //  permanently at the floor - a comb with holes in it rather than
            //  the shape of a beat.
            //
            //  So the mapping runs the other way here: each BAR claims its own
            //  slice of the loop. Long patterns behave exactly as they did in
            //  FX-404, one step per bar; short ones let neighbouring bars share
            //  a step and light together, which reads as a wider block for that
            //  step instead of a gap beside it.
            currentBarIdx = -1;
            for (int i = 0; i < kNumBars; ++i)
                if ((int) ((float) i * (float) patternLen / (float) kNumBars) == step)
                {
                    barLevels[(size_t) i] = juce::jmax (barLevels[(size_t) i], loudnessPct);
                    if (currentBarIdx < 0) currentBarIdx = i;
                    barIsPlayhead[(size_t) i] = true;
                }
                else
                    barIsPlayhead[(size_t) i] = false;
        }
        else
        {
            lastStepForWave = -1;
            currentBarIdx   = -1;
            barIsPlayhead.fill (false);
            for (auto& lv : barLevels)
                lv = juce::jmax (kFloorPct, lv - kDecayPerFrame * dtFrames);
        }
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

    //  step is the step within the playing PATTERN, or negative when the
    //  transport is stopped; patternLength is how long that pattern is, which
    //  is what maps a step onto one of the 28 slices; the colour is the bank
    //  that is playing, so the strip says WHICH pattern as well as where in it.
    void setStep (int stepInPattern, int patternLength, bool isPlaying, juce::Colour bankColour)
    {
        if (stepInPattern == step && patternLength == patternLen
            && isPlaying == playing && bankColour == stepColour) return;

        step       = stepInPattern;
        patternLen = juce::jmax (1, patternLength);
        playing    = isPlaying;
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

        //  THE LOOP SHAPE. FX-404's strip was
        //      .lcd-wave   { height:68px; display:flex; align-items:center; gap:1px }
        //      .lcd-wave i { flex:1; background:var(--lcd-fg); border-radius:1px }
        //  so the bars are CENTRED, not standing on a floor: each one grows
        //  symmetrically out of the middle line. That centring is most of why
        //  the strip reads as a waveform of the bar rather than as a chart.
        juce::ignoreUnused (halfH);
        {
            const float gap   = 1.0f;
            const float slotW = wave.getWidth() / (float) kNumBars;
            const float fullH = wave.getHeight();

            for (int i = 0; i < kNumBars; ++i)
            {
                const float pct = barLevels[(size_t) i];
                const float lvl = juce::jmin (1.0f, pct * 0.01f);
                const bool  isPlayhead = barIsPlayhead[(size_t) i];

                const float h = juce::jmax (1.0f, pct * 0.01f * fullH);
                const float x = wave.getX() + (float) i * slotW;
                const float w = juce::jmax (1.0f, slotW - gap);
                auto r = juce::Rectangle<float> (x, cy - h * 0.5f, w, h);

                //  waveBars[i].style.opacity =
                //      isPlayhead ? 1 : (0.45 + lvl*0.55)
                const float alpha = isPlayhead ? 1.0f : (0.45f + lvl * 0.55f);

                //  boxShadow when isPlayhead || lvl > 0.55. JUCE has no box
                //  shadow, so the halo is drawn as two soft passes behind the
                //  bar - same read, no blur pass.
                if (isPlayhead || lvl > 0.55f)
                {
                    const float rad = juce::jmax (lvl, isPlayhead ? 0.8f : 0.0f) * 6.0f;
                    const auto  glowCol = isPlayhead ? stepColour : ZatiColours::lcdFg;
                    g.setColour (glowCol.withAlpha (0.16f));
                    g.fillRoundedRectangle (r.expanded (rad * 0.5f, rad * 0.5f), 2.0f);
                    g.setColour (glowCol.withAlpha (0.22f));
                    g.fillRoundedRectangle (r.expanded (rad * 0.25f, rad * 0.25f), 1.5f);
                }

                g.setColour ((isPlayhead ? stepColour : ZatiColours::lcdFg).withAlpha (alpha));
                g.fillRoundedRectangle (r, 1.0f);
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
    static constexpr int   kNumBars   = 28;   // waveBars.length
    static constexpr float kFloorPct  = 4.0f; // new Array(...).fill(4)
    static constexpr float kDecayPerFrame = 2.2f;  // DECAY_PER_FRAME, per 60 fps frame
    //  FX-404's own value, and its comment on it: "kept low on purpose -
    //  attack speed lives here, the release/trail character is handled
    //  explicitly in drawSpectrum()'s own decay curve, not by blurring the
    //  raw FFT data further".
    static constexpr float kSmoothing = 0.35f;   // analyser.smoothingTimeConstant
    static constexpr float kMinDb     = -100.0f; // Web Audio minDecibels
    static constexpr float kMaxDb     = -30.0f;  // Web Audio maxDecibels

    juce::dsp::FFT                        fft { 10 };   // 2^10 = kFftSize
    std::array<float, (size_t) kFftSize>     ring {};
    int                                      ringPos    = 0;
    int                                      ringFilled = 0;
    std::array<float, (size_t) kFftSize * 2> fftData {};
    std::array<float, (size_t) kNumBins>     mag {};
    std::array<float, (size_t) kNumBars>     barLevels;   // const barLevels = new Array(28).fill(4)
    float                                    loudnessPct     = kFloorPct;
    int                                      currentBarIdx   = -1;
    std::array<bool, (size_t) kNumBars>      barIsPlayhead {};
    int                                      lastStepForWave = -1;
    int                                      patternLen      = 16;
    bool                                     playing         = false;
    juce::uint32                             lastTickMs      = 0;

    float        vuL   { 0.0f }, vuR { 0.0f };
    int          step  { -1 };
    bool         wasSilent { false };
    juce::Colour stepColour { ZatiColours::lcdFg };
    juce::String readout { "ZATI" };
};

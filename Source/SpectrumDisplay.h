#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"
#include "AudioEngine.h"

// ============================================================================
//  SpectrumDisplay — the "screen", ported from FX-404 v232's drawSpectrum().
//
//  v232 threw out the bar strip ("Removed the now-dead .lcd-wave bar styling")
//  and put a canvas there instead, showing the MASTER WAVEFORM SILHOUETTE:
//  getByteTimeDomainData over the analyser's whole window - fftSize 32768,
//  about 0.74 s at 44.1 kHz - decimated to one min/max column per 1.7 screen
//  pixels and drawn as a filled band between the two envelopes with a crisp
//  stroked edge above and below. In its own words: "the master's real signal
//  shape, not a slow left-to-right sweep".
//
//  There is no FFT in it at all. The window is long on purpose - at nearly a
//  second you watch a whole phrase land and decay, which is what makes it read
//  as an instrument's screen rather than as a level meter.
//
//  Fed decimated min/max columns by the engine (AudioEngine section 5c); the
//  raw 35000-sample window never crosses to the message thread.
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

        //  A flat line twice running is the same picture, and this is the
        //  biggest component on the face: repainting it thirty times a second
        //  with nothing playing is the app's largest idle cost. v232 gates its
        //  own loop the same way (lcdShouldAnimate).
        const bool silent = (pk <= 0.0f && peak < 0.0005f);
        if (silent && wasSilent) return;

        wasSilent = silent;
        repaint();
    }

    void setReadout (const juce::String& s) { readout = s; repaint(); }
    void setBpm     (double b)              { bpm = b; }

    //  The engine hands over min/max columns already decimated. Re-bucketing
    //  them into however many the screen is wide is exact: the minimum of a
    //  group of minima IS the minimum.
    void setColumns (const float* mn, const float* mx, int n)
    {
        colCount = juce::jlimit (0, kMaxCols, n);
        for (int i = 0; i < colCount; ++i) { srcMin[i] = mn[i]; srcMax[i] = mx[i]; }
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
        g.drawText (T ("OUT") + " " + Lang::ltr (peakDb()), top, juce::Justification::centredTop);

        //  Stereo VU, immediately under the labels: two rows of segments in a
        //  gutter narrow enough for the L and the R to sit beside them.
        {
            auto vu = b.reduced (8.0f, 0.0f).withY (b.getY() + 22.0f).withHeight (16.0f);
            auto gutter = vu.removeFromLeft (12.0f);

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
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
        //  Only the status line now. The ten pixels the step strip took go to
        //  the waveform, which is the one thing this screen is for.
        wave.removeFromBottom (20.0f);
        const float cy = wave.getCentreY();
        const float halfH = wave.getHeight() * 0.5f - 2.0f;

        //  THE SILHOUETTE, with v232's numbers:
        //      BAR_PITCH  1.7 px per column
        //      mid = h/2, yamp = mid - 1
        //      baseline   white at 6%, 1 px
        //      band       globalAlpha 0.20
        //      edges      lineWidth 1.4, round joins and caps
        //      no glow    ("cheaper + cleaner")
        //
        //  Drawn in lcdFg rather than in v232's --accent, because on THIS face
        //  the accent is a near-black chassis colour - painting it on a
        //  near-black screen is the exact bug that left the meter invisible.
        juce::ignoreUnused (halfH);
        {
            const float mid  = cy;
            const float yamp = wave.getHeight() * 0.5f - 1.0f;

            g.setColour (ZatiColours::lcdFg.withAlpha (0.06f));
            g.fillRect (wave.getX(), mid - 0.5f, wave.getWidth(), 1.0f);

            const int NB = juce::jlimit (24, kMaxCols, (int) (wave.getWidth() / 1.7f));

            if (colCount > 0 && NB > 1)
            {
                const float pitch = wave.getWidth() / (float) NB;
                const float per   = (float) colCount / (float) NB;

                float xs[kMaxCols], yUp[kMaxCols], yDn[kMaxCols];

                for (int bi = 0; bi < NB; ++bi)
                {
                    const int s0 = (int) ((float)  bi      * per);
                    const int s1 = juce::jmax (s0 + 1, (int) ((float) (bi + 1) * per));

                    float mn = 1.0e9f, mx = -1.0e9f;
                    for (int i = s0; i < s1 && i < colCount; ++i)
                    {
                        mn = juce::jmin (mn, srcMin[i]);
                        mx = juce::jmax (mx, srcMax[i]);
                    }
                    if (mx < mn) { mn = 0.0f; mx = 0.0f; }   // only the degenerate case

                    xs [bi] = wave.getX() + (float) bi * pitch + pitch * 0.5f;
                    yUp[bi] = mid + juce::jlimit (-1.0f, 1.0f, mn) * yamp;
                    yDn[bi] = mid + juce::jlimit (-1.0f, 1.0f, mx) * yamp;
                }

                //  The band: out along one envelope and back along the other.
                juce::Path band;
                band.startNewSubPath (xs[0], yUp[0]);
                for (int bi = 1; bi < NB; ++bi)  band.lineTo (xs[bi], yUp[bi]);
                for (int bi = NB - 1; bi >= 0; --bi) band.lineTo (xs[bi], yDn[bi]);
                band.closeSubPath();
                g.setColour (ZatiColours::lcdFg.withAlpha (0.20f));
                g.fillPath (band);

                //  ...and the two crisp edges over it.
                juce::Path up, dn;
                up.startNewSubPath (xs[0], yUp[0]);
                dn.startNewSubPath (xs[0], yDn[0]);
                for (int bi = 1; bi < NB; ++bi) { up.lineTo (xs[bi], yUp[bi]); dn.lineTo (xs[bi], yDn[bi]); }

                const juce::PathStrokeType stroke (1.4f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded);
                g.setColour (ZatiColours::lcdFg);
                g.strokePath (up, stroke);
                g.strokePath (dn, stroke);
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
    //  Decimated min/max columns from the engine, oldest first.
    static constexpr int kMaxCols = AudioEngine::kMaxScopeColumns;
    float srcMin[kMaxCols] {}, srcMax[kMaxCols] {};
    int   colCount   = 0;

    float        vuL   { 0.0f }, vuR { 0.0f };
    bool         wasSilent { false };
    juce::String readout { "ZATI" };
};

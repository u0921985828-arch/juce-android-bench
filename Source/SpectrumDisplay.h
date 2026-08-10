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

    //  SWIPE THE SCREEN TO CHANGE PATTERN BANK.
    //
    //  Switching bank while playing meant opening SEC, finding the PATRON
    //  stepper, and pressing it - a sheet over the pads, mid-take. The screen
    //  is the biggest thing on the face, it has no other gesture on it, and it
    //  is already where you are looking. A horizontal drag past a third of its
    //  width moves one bank; anything shorter is a tap that missed.
    std::function<void (int)> onSwipe;   // -1 previous, +1 next

    void mouseDown (const juce::MouseEvent& e) override { dragFromX = e.position.x; }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (onSwipe == nullptr || dragFromX < 0.0f) return;
        const float dx = e.position.x - dragFromX;
        dragFromX = -1.0f;

        //  A third of the panel, and never less than 60 px: on a narrow phone
        //  a proportional threshold alone is short enough to fire on a stray
        //  thumb roll.
        const float need = juce::jmax (60.0f, (float) getWidth() * 0.33f);
        if (std::abs (dx) >= need)
            onSwipe (dx > 0.0f ? 1 : -1);
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

        //  THE SCREEN, REORGANISED.
        //
        //  It carried four pieces of text and a meter, and two of the four
        //  said nothing. "ZATI" is the name of the app, printed on an app you
        //  already opened, on the one surface that should only ever show what
        //  the instrument is DOING. "SIG" was a signal-present light nobody
        //  could name - which is the definition of a light that is not worth
        //  its pixels next to a level meter that says the same thing better.
        //
        //  What is left is arranged by what it is: the NUMBER at the top, the
        //  SHAPE in the middle with everything the other two gave back, and
        //  the METER along the bottom edge with the tempo beside it. Reading
        //  down the screen is now reading from the abstract to the physical.
        auto top = b.reduced (10.0f, 6.0f).removeFromTop (13.0f);
        g.setColour (ZatiColours::lcdFg.withAlpha (0.9f));
        g.drawText (T ("OUT") + " " + Lang::ltr (peakDb()), top, Lang::start (juce::Justification::top));

        //  Waveform area: everything between the readout and the meter band.
        auto wave = b.reduced (8.0f, 0.0f);
        wave.removeFromTop (22.0f);
        wave.removeFromBottom (kMeterBand);
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

        //  THE BOTTOM EDGE: the meter, and the tempo beside it.
        //
        //  The meter used to sit under the labels at the top, where it was one
        //  more line of furniture between you and the wave. On the bottom edge
        //  it is where a meter is on every machine that has one, it frames the
        //  screen instead of interrupting it, and the wave gets the sixteen
        //  pixels back.
        {
            auto band = b.reduced (8.0f, 0.0f).removeFromBottom (kMeterBand).reduced (0.0f, 3.0f);

            //  The tempo takes the right end - the corner "SIG" used to
            //  occupy, and the one number you look for without looking away
            //  from what you are playing.
            auto bpmCell = band.removeFromRight (68.0f);
            g.setColour (ZatiColours::lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (Lang::ltr (juce::String (bpm, 1) + " BPM"), bpmCell,
                        juce::Justification::centredRight);

            band.removeFromRight (8.0f);
            auto gutter = band.removeFromLeft (10.0f);

            g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fTiny, true));
            g.drawText ("L", gutter.withHeight (7.0f).withY (band.getY()), juce::Justification::centredLeft);
            g.drawText ("R", gutter.withHeight (7.0f).withY (band.getY() + 7.0f), juce::Justification::centredLeft);

            const int nSeg = 32;
            const float segW = band.getWidth() / (float) nSeg;

            auto row = [&] (float level, juce::Rectangle<float> r)
            {
                const int lit = (int) std::round (std::sqrt (juce::jlimit (0.0f, 1.0f, level)) * (float) nSeg);
                for (int i = 0; i < nSeg; ++i)
                {
                    const bool hot = i >= (int) ((float) nSeg * 0.82f);
                    //  A LIT segment has to be the LCD's ink, not the chassis
                    //  accent: the accent is a near-black, and painting it on a
                    //  near-black screen made a lit segment look exactly like
                    //  an unlit one.
                    g.setColour (i < lit ? (hot ? ZatiColours::red : ZatiColours::lcdFg)
                                         : ZatiColours::lcdFg.withAlpha (0.10f));
                    g.fillRect (band.getX() + (float) i * segW + 0.5f, r.getY(), segW - 1.0f, r.getHeight());
                }
            };

            row (vuL, band.withHeight (5.0f).withY (band.getY() + 1.0f));
            row (vuR, band.withHeight (5.0f).withY (band.getY() + 8.0f));
        }

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

    //  How much of the panel the meter band takes along the bottom: two rows
    //  of segments, their L/R gutter, and air above and below.
    static constexpr float kMeterBand = 22.0f;

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
    float        dragFromX { -1.0f };
};

#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ZatiLookAndFeel.h"
#include "Lang.h"

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

    //  Trim by dragging the handles, which is the gesture the drawn handles
    //  have been promising all along. Editing the start of a sound by typing
    //  0.062 is the wrong instrument: you want to grab it and listen.
    std::function<void (float start01, float end01)> onTrimDragged;

    //  Tap anywhere that is not a handle and hear the sound from there. A
    //  waveform you can only look at is a picture; this is the difference
    //  between finding the downbeat by eye and finding it by ear.
    std::function<void (float pos01)> onAudition;

    //  Where the read head is, 0..1, or negative for nothing sounding.
    void setPlayhead (float pos01)
    {
        const float p = (pos01 >= 0.0f && pos01 <= 1.0f) ? pos01 : -1.0f;
        if (std::abs (p - playhead) < 0.0005f && (p < 0.0f) == (playhead < 0.0f)) return;
        playhead = p;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        const float t  = xToNorm ((float) e.x);
        const float ds = std::abs (t - start01), de = std::abs (t - end01);
        // Grab whichever handle is nearer, but only within a finger's width;
        // a tap in open water should not yank an edge across the sample.
        const float grab = 24.0f / juce::jmax (1.0f, (float) waveArea().getWidth());
        dragging = (juce::jmin (ds, de) > grab) ? 0 : (ds <= de ? 1 : 2);

        //  Open water: audition. On a chopped source the fragments are what is
        //  drawn, and the pad that owns the fragment under the finger is the
        //  one that should speak - so the tap is reported and the owner
        //  decides, rather than being assumed to be the selected pad.
        if (dragging == 0 && onAudition)
            onAudition (t);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging == 0 || sample == nullptr) return;
        const float t = xToNorm ((float) e.x);
        float s = start01, en = end01;
        if (dragging == 1) s  = juce::jlimit (0.0f, en - 0.005f, t);
        else               en = juce::jlimit (s + 0.005f, 1.0f, t);
        setTrim (s, en);
        if (onTrimDragged) onTrimDragged (s, en);
    }

    void mouseUp (const juce::MouseEvent&) override { dragging = 0; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // LCD panel.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff112232), b.getCentreX(), b.getY(),
                                                 ZatiColours::screenBg, b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 2.0f);

        const auto lcdFg = ZatiColours::lcdFg, lcdDim = ZatiColours::lcdDim;
        // Fallback trace when nothing carries a zati yet: the LCD's own
        // foreground, so an un-chopped sample stays achromatic like the rest
        // of the chassis instead of borrowing a hue it has not earned.
        const auto accent = ZatiColours::lcdFg;

        if (sample == nullptr || mins.isEmpty())
        {
            g.setColour (lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.18f));
            g.drawText (T ("TAP A PAD TO LOAD ITS WAVEFORM"), getLocalBounds(), juce::Justification::centred);
            g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
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
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
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
            g.setColour (ZatiColours::screenBg.withAlpha (0.62f));
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

        //  The read head, and behind it the ground it has covered. A line
        //  alone says where; the trail says how far through, which is what
        //  "progress" means and what a bare cursor never showed.
        if (playhead >= 0.0f)
        {
            const float px = wave.getX() + playhead * wave.getWidth();
            const float from = wave.getX() + juce::jmin (start01, playhead) * wave.getWidth();

            g.setColour (lcdFg.withAlpha (0.16f));
            g.fillRect (from, wave.getY(), juce::jmax (0.0f, px - from), wave.getHeight());

            g.setColour (ZatiColours::red);
            g.fillRect (px - 1.0f, wave.getY() - 3.0f, 2.0f, wave.getHeight() + 6.0f);
            g.fillRect (px - 3.5f, wave.getY() - 5.0f, 7.0f, 3.0f);
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
        const auto infoFont = ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.08f);
        g.setFont (infoFont);
        auto top2 = b.reduced (11.0f, 7.0f).removeFromTop (13.0f);
        g.setColour (activeColour (accent));
        g.fillEllipse (top2.getX(), top2.getCentreY() - 3.0f, 6.0f, 6.0f);

        //  These two used to be drawn into the same strip, one flush left and
        //  one flush right, which works right up until the sample is called
        //  something long - and then the file name runs straight through the
        //  channel count and both become unreadable. Measure the right-hand
        //  readout, give it its room, and let the name have what is left.
        auto nameRow = top2.withTrimmedLeft (12.0f);
        if (infoRight.isNotEmpty())
        {
            auto rightRow = nameRow.removeFromRight (juce::GlyphArrangement::getStringWidth (infoFont, infoRight) + 8.0f);
            g.setColour (lcdDim);
            g.drawText (infoRight, rightRow, juce::Justification::topRight);
        }

        g.setColour (lcdFg);
        g.drawFittedText (infoName, nameRow.toNearestInt(), juce::Justification::topLeft, 1, 0.7f);

        // TRIM only makes sense for a single window; on a chopped source each
        // fragment carries its own, and the text would collide with their
        // numbers in the same strip.
        if (segments.size() <= 1)
        {
            auto bot = b.reduced (11.0f, 6.0f).removeFromBottom (12.0f);
            g.setColour (lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (T ("TRIM") + " " + juce::String (start01, 2) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + juce::String (end01, 2),
                        bot, juce::Justification::bottomLeft);
        }

        g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
    }

    void resized() override { computeMinMax(); repaint(); }

private:
    juce::Rectangle<float> waveArea() const
    {
        auto w = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
        w.removeFromTop (22.0f);
        w.removeFromBottom (20.0f);
        return w;
    }
    float xToNorm (float x) const
    {
        auto w = waveArea();
        return juce::jlimit (0.0f, 1.0f, (x - w.getX()) / juce::jmax (1.0f, w.getWidth()));
    }
    int dragging = 0;   // 0 none, 1 start, 2 end

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
    float playhead = -1.0f;
    juce::String infoName, infoRight;
};

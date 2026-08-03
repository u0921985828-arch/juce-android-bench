#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "SampleBuffer.h"

// ============================================================================
//  Voice — one playing sample. Phase accumulator + Hermite interpolation,
//  with a playback window (trim start/end), optional reverse and loop, and a
//  short anti-click gain envelope. POD-ish, header-only.
// ============================================================================
struct Voice
{
    bool   active    = false;
    bool   releasing = false;
    bool   loop      = false;
    bool   reverse   = false;
    double pos       = 0.0;
    double delta     = 0.0;
    int    slot      = -1;
    int    winStart  = 1;      // playback window [winStart, winEnd) in samples
    int    winEnd    = 2;

    float  gain      = 0.0f;
    float  target    = 0.0f;
    float  stepUp    = 0.0f;
    float  stepDown  = 0.0f;
    float  stepCtl   = 0.0f;   // volume-cut rate: fixed ~10 ms, not the attack

    float  panL      = 0.7071f;   // equal-power pan gains, precomputed in start()
    float  panR      = 0.7071f;
    float  panTL     = 0.7071f;   // pan targets — retarget() moves these, render() slews
    float  panTR     = 0.7071f;

    void start (int slotIndex, float semitones, float velocity,
                double fSrc, double fSys,
                int startSamp, int endSamp, bool loopOn, bool rev, int srcLen,
                float pan = 0.0f, float attackMs = 2.0f, float releaseMs = 3.0f) noexcept
    {
        slot     = slotIndex;
        winStart = juce::jlimit (1, juce::jmax (1, srcLen - 3), startSamp);
        winEnd   = juce::jlimit (winStart + 1, juce::jmax (winStart + 1, srcLen - 2), endSamp);
        loop     = loopOn;
        reverse  = rev;

        const double base = (fSrc / fSys) * std::pow (2.0, (double) semitones / 12.0);
        delta = rev ? -base : base;
        pos   = rev ? (double) (winEnd - 1) : (double) winStart;

        // Equal-power pan law: pan in [-1, 1], 0 = centre.
        const float panAngle = (juce::jlimit (-1.0f, 1.0f, pan) * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        panL = panTL = std::cos (panAngle);
        panR = panTR = std::sin (panAngle);

        target    = velocity;
        gain      = 0.0f;
        releasing = false;
        const double fadeIn  = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, attackMs)  * fSys);
        const double fadeOut = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, releaseMs) * fSys);
        stepUp    = (float) (velocity / fadeIn);
        stepDown  = (float) (velocity / fadeOut);
        stepCtl   = (float) (1.0 / juce::jmax (1.0, 0.010 * fSys));
        active    = true;
    }

    void release() noexcept { releasing = true; }
    void kill()    noexcept { active = false; releasing = false; gain = 0.0f; }

    // Voice steal: fast fixed declick fade (~1.5 ms) regardless of the pad's
    // musical release — used when the same pad retriggers and this instance
    // must get out of the way without a click.
    void steal (double fSys) noexcept
    {
        if (! active) return;
        releasing = true;
        stepDown  = (float) (juce::jmax (gain, 0.05f) / juce::jmax (1.0, 0.0015 * fSys));
    }

    // Control-rate update (once per block, audio thread): a looping/long voice
    // keeps following its pad's VOLUME and PAN instead of freezing the values
    // captured at start(). Gain ramps in render(); pan slews there too.
    void retarget (float g, float pan) noexcept
    {
        if (! active || releasing) return;
        target = g;
        const float panAngle = (juce::jlimit (-1.0f, 1.0f, pan) * 0.5f + 0.5f) * juce::MathConstants<float>::halfPi;
        panTL = std::cos (panAngle);
        panTR = std::sin (panAngle);
    }

    void render (juce::AudioBuffer<float>& out, int start, int num,
                 const SampleBuffer* sb) noexcept
    {
        if (! active || sb == nullptr || num <= 0)
            return;

        const int srcLen = sb->buffer.getNumSamples();
        const int srcCh  = sb->buffer.getNumChannels();
        if (srcLen < 4 || srcCh < 1) { active = false; return; }

        //  Clamp the window against THIS buffer once per block. The pad's
        //  sample can be swapped underneath a sounding voice, so the window
        //  captured at start() may no longer fit. Doing it here means the
        //  inner loop needs no per-sample safety test at all: inside
        //  [winStart, winEnd) the Hermite taps idx-1 .. idx+2 are provably in
        //  range, and that test used to run on every single sample.
        if (winStart < 1)          winStart = 1;
        if (winEnd   > srcLen - 2) winEnd   = srcLen - 2;
        if (winStart >= winEnd)    { active = false; return; }
        if (pos < (double) winStart)        pos = (double) winStart;
        if (pos > (double) (winEnd - 1))    pos = (double) (winEnd - 1);

        const float* srcL = sb->buffer.getReadPointer (0);
        const float* srcR = (srcCh > 1) ? sb->buffer.getReadPointer (1) : nullptr;
        const int    outCh = out.getNumChannels();
        const bool   stereoOut = outCh > 1;
        float* dstL = out.getWritePointer (0, start);
        float* dstR = stereoOut ? out.getWritePointer (1, start) : nullptr;

        //  Pan glides to its target across exactly one block. The old version
        //  used a fixed per-sample coefficient, which made the glide twice as
        //  slow at 96 kHz as at 48 — a control whose speed depended on the
        //  sound card.
        const float panIncL = (panTL - panL) / (float) num;
        const float panIncR = (panTR - panR) / (float) num;

        const double step = reverse ? -delta : delta;   // always positive magnitude
        int i = 0;

        while (i < num && active)
        {
            // How far to the edge of the window, and therefore how many
            // samples can run before anything needs deciding again.
            const double dist = reverse ? (pos - (double) winStart)
                                        : ((double) (winEnd - 1) - pos);
            if (dist <= 0.0)
            {
                if (! loop) { active = false; break; }
                pos = reverse ? (double) (winEnd - 1) : (double) winStart;
                continue;
            }

            int run = (int) (dist / step) + 1;
            if (run > num - i) run = num - i;
            if (run < 1)       run = 1;

            for (int k = 0; k < run; ++k, ++i)
            {
                if (releasing)
                {
                    gain -= stepDown;
                    if (gain <= 0.0f) { active = false; gain = 0.0f; break; }
                }
                else if (gain < target)
                {
                    gain += stepUp;
                    if (gain > target) gain = target;
                }
                else if (gain > target)
                {
                    // A volume CUT is not a musical release: it used to fall
                    // at the pad's attack rate, so a pad with a one-second
                    // attack took a second to get quieter.
                    gain -= stepCtl;
                    if (gain < target) gain = target;
                }

                panL += panIncL;
                panR += panIncR;

                const int   idx  = (int) pos;
                const float frac = (float) (pos - (double) idx);

                //  A mono sample is the normal case for a drum hit, and the
                //  old code ran the four-point interpolation twice over the
                //  identical data to fill two identical channels.
                const float l = hermite4 (frac, srcL, idx);
                dstL[i] += gain * panL * l;
                if (stereoOut)
                    dstR[i] += gain * panR * (srcR != nullptr ? hermite4 (frac, srcR, idx) : l);

                pos += delta;
            }
        }

        panL = juce::jlimit (0.0f, 1.0f, panL);
        panR = juce::jlimit (0.0f, 1.0f, panR);
    }

private:
    static inline float hermite4 (float frac, const float* y, int idx) noexcept
    {
        const float ym1 = y[idx - 1];
        const float y0  = y[idx];
        const float y1  = y[idx + 1];
        const float y2  = y[idx + 2];
        const float c0 = y0;
        const float c1 = 0.5f * (y1 - ym1);
        const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
        const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        return ((c3 * frac + c2) * frac + c1) * frac + c0;
    }
};

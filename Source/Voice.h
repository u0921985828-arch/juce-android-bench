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

    float  panL      = 0.7071f;   // equal-power pan gains, precomputed in start()
    float  panR      = 0.7071f;

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
        panL = std::cos (panAngle);
        panR = std::sin (panAngle);

        target    = velocity;
        gain      = 0.0f;
        releasing = false;
        const double fadeIn  = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, attackMs)  * fSys);
        const double fadeOut = juce::jmax (1.0, 0.001 * (double) juce::jmax (0.1f, releaseMs) * fSys);
        stepUp    = (float) (velocity / fadeIn);
        stepDown  = (float) (velocity / fadeOut);
        active    = true;
    }

    void release() noexcept { releasing = true; }
    void kill()    noexcept { active = false; releasing = false; gain = 0.0f; }

    void render (juce::AudioBuffer<float>& out, int start, int num,
                 const SampleBuffer* sb) noexcept
    {
        if (! active || sb == nullptr)
            return;

        const int srcLen = sb->buffer.getNumSamples();
        const int srcCh  = sb->buffer.getNumChannels();
        if (srcLen < 4 || srcCh < 1) { active = false; return; }

        const float* srcL = sb->buffer.getReadPointer (0);
        const float* srcR = (srcCh > 1) ? sb->buffer.getReadPointer (1) : srcL;
        const int    outCh = out.getNumChannels();
        float* dstL = out.getWritePointer (0);
        float* dstR = (outCh > 1) ? out.getWritePointer (1) : dstL;

        for (int i = 0; i < num; ++i)
        {
            // Window bounds / loop.
            if (! reverse)
            {
                if (pos >= (double) (winEnd - 1))
                {
                    if (loop) pos = (double) winStart;
                    else      { active = false; break; }
                }
            }
            else
            {
                if (pos <= (double) winStart)
                {
                    if (loop) pos = (double) (winEnd - 1);
                    else      { active = false; break; }
                }
            }

            const int idx = (int) pos;
            if (idx < 1 || idx + 2 >= srcLen) { active = false; break; }   // safety
            const double frac = pos - (double) idx;

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

            dstL[start + i] += gain * panL * hermite4 ((float) frac, srcL, idx);
            if (outCh > 1)
                dstR[start + i] += gain * panR * hermite4 ((float) frac, srcR, idx);

            pos += delta;
        }
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

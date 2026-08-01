#pragma once

#include <JuceHeader.h>
#include <cmath>
#include "SampleBuffer.h"

// ============================================================================
//  Voice — one playing sample. POD-ish, header-only, inline-friendly.
//
//  DSP: fractional phase accumulator + Hermite 4-point interpolation.
//    delta = (F_src / F_sys) * 2^(semitones / 12)
//    pos  += delta            (per output sample)
//  The integer part of `pos` indexes the source; the fractional part feeds
//  interpolation.
//
//  IMPORTANT — indexing convention (top off-by-one bug site, keep it exact):
//    `idx = (int) pos` is the sample AT/just-before the read position (y0).
//    hermite4() reads y[idx-1], y[idx], y[idx+1], y[idx+2].
//    render() therefore needs `idx-1 >= 0` and `idx+2 < srcLen`. The start
//    boundary (idx == 0) is handled inside hermite4 by clamping ym1 = y[idx];
//    the end boundary is the `idx + 2 >= srcLen` guard in render().
//
//  P0 has no envelope: stop() is a hard flag flip. No heap, no virtuals.
// ============================================================================
struct Voice
{
    bool   active = false;
    double pos    = 0.0;    // fractional read position, in source samples (double: float drifts)
    double delta  = 0.0;    // phase increment per output sample
    float  gain   = 1.0f;   // velocity
    int    slot   = -1;

    void start (int slotIndex, float semitones, float velocity, double fSrc, double fSys) noexcept
    {
        slot   = slotIndex;
        pos    = 0.0;
        delta  = (fSrc / fSys) * std::pow (2.0, (double) semitones / 12.0);   // computed once
        gain   = velocity;
        active = true;
    }

    void stop() noexcept { active = false; }   // P0: hard stop, flag flip only

    // Additive render of this voice into a stereo output buffer over
    // [start, start + num). Does not clear — the engine clears once per block.
    void render (juce::AudioBuffer<float>& out, int start, int num,
                 const SampleBuffer* sb) noexcept
    {
        if (! active || sb == nullptr)
            return;

        const int    srcLen = sb->buffer.getNumSamples();
        const int    srcCh  = sb->buffer.getNumChannels();
        if (srcLen < 4 || srcCh < 1)
        {
            active = false;
            return;
        }

        const float* srcL = sb->buffer.getReadPointer (0);
        const float* srcR = (srcCh > 1) ? sb->buffer.getReadPointer (1) : srcL;

        const int    outCh = out.getNumChannels();
        float*       dstL  = out.getWritePointer (0);
        float*       dstR  = (outCh > 1) ? out.getWritePointer (1) : dstL;

        for (int i = 0; i < num; ++i)
        {
            const int    idx  = (int) pos;
            const double frac = pos - (double) idx;

            // One-shot end guard (hermite reads up to idx+2).
            if (idx + 2 >= srcLen)
            {
                active = false;
                break;
            }

            const float l = gain * hermite4 ((float) frac, srcL, idx);
            const float r = gain * hermite4 ((float) frac, srcR, idx);

            dstL[start + i] += l;
            if (outCh > 1)
                dstR[start + i] += r;

            pos += delta;
        }
    }

private:
    // Hermite 4-point, 3rd-order interpolation (Laurent de Soras coefficients).
    // Reads y[idx-1 .. idx+2]; ym1 is clamped at the start boundary.
    static inline float hermite4 (float frac, const float* y, int idx) noexcept
    {
        const float ym1 = (idx >= 1) ? y[idx - 1] : y[idx];   // start-boundary clamp
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

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
//
//  P1: a short linear gain envelope removes clicks —
//    * fade-IN  (~2 ms) at start (no attack click),
//    * fade-OUT (~3 ms) on stop / retrigger (no release click).
//  The voice frees itself (active=false) once the fade-out reaches 0.
//
//  Indexing convention (keep exact): idx = (int) pos is y0; hermite4 reads
//  y[idx-1..idx+2], so render needs idx+2 < srcLen (end guard) and clamps the
//  start boundary inside hermite4.
// ============================================================================
struct Voice
{
    bool   active    = false;
    bool   releasing = false;
    double pos       = 0.0;    // fractional read position (double: float drifts)
    double delta     = 0.0;    // phase increment per output sample
    int    slot      = -1;

    float  gain      = 0.0f;   // current envelope gain
    float  target    = 0.0f;   // velocity (fade-in destination)
    float  stepUp    = 0.0f;   // per-sample fade-in increment
    float  stepDown  = 0.0f;   // per-sample fade-out decrement (positive magnitude)

    void start (int slotIndex, float semitones, float velocity,
                double fSrc, double fSys) noexcept
    {
        slot      = slotIndex;
        pos       = 0.0;
        delta     = (fSrc / fSys) * std::pow (2.0, (double) semitones / 12.0);
        target    = velocity;
        gain      = 0.0f;
        releasing = false;

        const double fadeIn  = juce::jmax (1.0, 0.002 * fSys);   // ~2 ms
        const double fadeOut = juce::jmax (1.0, 0.003 * fSys);   // ~3 ms
        stepUp    = (float) (velocity / fadeIn);
        stepDown  = (float) (velocity / fadeOut);
        active    = true;
    }

    // Begin a fade-out; the voice frees itself when it reaches 0.
    void release() noexcept { releasing = true; }

    // Hard stop (panic) — immediate, no fade.
    void kill() noexcept { active = false; releasing = false; gain = 0.0f; }

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

        const int outCh = out.getNumChannels();
        float* dstL = out.getWritePointer (0);
        float* dstR = (outCh > 1) ? out.getWritePointer (1) : dstL;

        for (int i = 0; i < num; ++i)
        {
            const int    idx  = (int) pos;
            const double frac = pos - (double) idx;

            if (idx + 2 >= srcLen) { active = false; break; }   // one-shot end

            // Update the envelope.
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

            dstL[start + i] += gain * hermite4 ((float) frac, srcL, idx);
            if (outCh > 1)
                dstR[start + i] += gain * hermite4 ((float) frac, srcR, idx);

            pos += delta;
        }
    }

private:
    // Hermite 4-point, 3rd-order interpolation (Laurent de Soras coefficients).
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

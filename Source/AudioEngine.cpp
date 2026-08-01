#include "AudioEngine.h"

// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::~AudioEngine()
{
    for (auto*& p : padSample)
        if (p != nullptr) { p->decReferenceCount(); p = nullptr; }
    for (auto& slot : pendingPad)
        if (auto* p = slot.exchange (nullptr)) p->decReferenceCount();
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

// ---------------------------------------------------------------------------
//  Audio thread
// ---------------------------------------------------------------------------

void AudioEngine::prepareToPlay (double sampleRate, int maxBlockSize) noexcept
{
    systemSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxBlock         = (maxBlockSize > 0) ? maxBlockSize : 512;

    // ~20 s mono record buffer (allocated here, never in the callback).
    recordBuffer.setSize (1, (int) (20.0 * systemSampleRate));
    recordBuffer.clear();

    juce::dsp::ProcessSpec spec { systemSampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
    masterFilter.prepare (spec);
    masterFilter.reset();

    delayLine.prepare (spec);
    delayLine.setMaximumDelayInSamples (juce::jmax (1, (int) (systemSampleRate * 1.0)));
    delayLine.reset();
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.kill();
}

void AudioEngine::triggerPad (int slot) noexcept
{
    if (slot < 0 || slot >= kNumPads)
        return;
    auto* sb = padSample[(size_t) slot];
    if (sb == nullptr)
        return;

    // Choke group: fade out any other pad's voice sharing this pad's group.
    const int group = padChoke[(size_t) slot].load (std::memory_order_relaxed);
    if (group > 0)
        for (int j = 0; j < kNumPads; ++j)
            if (j != slot && padChoke[(size_t) j].load (std::memory_order_relaxed) == group)
                voices[(size_t) j].release();

    const int len = sb->buffer.getNumSamples();
    int st = padStart[(size_t) slot].load (std::memory_order_relaxed);
    int en = padEnd[(size_t) slot].load (std::memory_order_relaxed);
    if (en <= 0 || en > len) en = len;
    if (st < 0 || st >= en)  st = 0;

    triggeredMask.fetch_or ((std::uint32_t) (1u << slot), std::memory_order_relaxed);
    voices[(size_t) slot].start (slot,
                                 padPitch[(size_t) slot].load (std::memory_order_relaxed),
                                 padGain[(size_t) slot].load (std::memory_order_relaxed),
                                 sb->sourceSampleRate, systemSampleRate,
                                 st, en,
                                 padLoop[(size_t) slot].load (std::memory_order_relaxed),
                                 padReverse[(size_t) slot].load (std::memory_order_relaxed),
                                 len);
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    // 0. Capture mic input BEFORE clearing (input is in channel 0 on entry).
    if (recording.load (std::memory_order_acquire) && out.getNumChannels() > 0)
    {
        const int cap = recordBuffer.getNumSamples();
        int rp = recordPos.load (std::memory_order_relaxed);
        const int n = juce::jmin (numSamples, cap - rp);
        if (n > 0)
        {
            recordBuffer.copyFrom (0, rp, out.getReadPointer (0, startSample), n);
            rp += n;
            recordPos.store (rp, std::memory_order_relaxed);
        }
        if (rp >= cap)
            recording.store (false, std::memory_order_release);   // full -> auto stop
    }

    // 1. Adopt freshly published per-pad samples.
    for (int slot = 0; slot < kNumPads; ++slot)
        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);
            padSample[(size_t) slot] = incoming;
        }

    // 2. Clear output.
    out.clear (startSample, numSamples);

    auto renderVoices = [this, &out] (int s, int nn) noexcept
    {
        for (int i = 0; i < kNumPads; ++i)
            voices[(size_t) i].render (out, s, nn, padSample[(size_t) i]);
    };

    // 3. Drain UI trigger commands.
    constexpr int kMaxCmds = 256;
    Command local[kMaxCmds];
    int n = 0;
    commands.drain ([&local, &n] (const Command& c) noexcept { if (n < kMaxCmds) local[n++] = c; });
    for (int i = 0; i < n; ++i)
        handleCommand (local[i]);

    // 4. Sequencer transport (block-quantised stepping — good enough for P1).
    const bool isPlaying = playing.load (std::memory_order_relaxed);
    if (isPlaying)
    {
        if (! wasPlaying) { currentStep = -1; stepAccum = 0.0; playStep.store (-1, std::memory_order_relaxed); }

        const double beatsPerStep = 0.25;   // 16th notes
        const double secPerStep   = (60.0 / juce::jmax (20.0, bpm.load (std::memory_order_relaxed))) * beatsPerStep;
        const double samplesPerStep = juce::jmax (1.0, secPerStep * systemSampleRate);

        stepAccum += numSamples;
        // Fire the first step immediately on start.
        if (currentStep < 0) { stepAccum = samplesPerStep; }

        while (stepAccum >= samplesPerStep)
        {
            stepAccum -= samplesPerStep;
            currentStep = (currentStep + 1) % kNumSteps;
            const std::uint16_t mask = stepMask[(size_t) currentStep].load (std::memory_order_relaxed);
            for (int p = 0; p < kNumPads; ++p)
                if ((mask & (std::uint16_t) (1u << p)) != 0)
                    triggerPad (p);
            playStep.store (currentStep, std::memory_order_relaxed);
        }
    }
    else if (wasPlaying)
    {
        playStep.store (-1, std::memory_order_relaxed);
    }
    wasPlaying = isPlaying;

    // 5. Render all voices over the whole block.
    renderVoices (startSample, numSamples);

    // 5b. Master FX: filter -> drive -> delay. Each stage is BYPASSED when
    //     neutral, so the default signal path is untouched (a bug here must
    //     never silence the whole output).
    {
        const int   outCh = out.getNumChannels();
        const int   ft   = fxType.load   (std::memory_order_relaxed);
        const float cut  = fxCutoff.load (std::memory_order_relaxed);
        const float reso = fxReso.load   (std::memory_order_relaxed);

        // LPF fully open (high cutoff, low resonance) = transparent -> skip.
        const bool filterActive = ! (ft == 0 && cut >= 19000.0f && reso <= 0.72f);
        if (filterActive)
        {
            juce::dsp::AudioBlock<float> block (out.getArrayOfWritePointers(), (size_t) outCh,
                                                (size_t) startSample, (size_t) numSamples);
            masterFilter.setType (ft == 0 ? juce::dsp::StateVariableTPTFilterType::lowpass
                                          : juce::dsp::StateVariableTPTFilterType::highpass);
            masterFilter.setCutoffFrequency (juce::jlimit (20.0f, (float) (systemSampleRate * 0.45), cut));
            masterFilter.setResonance (juce::jlimit (0.1f, 4.0f, reso));
            juce::dsp::ProcessContextReplacing<float> ctx (block);
            masterFilter.process (ctx);
        }

        const float d = fxDrive.load (std::memory_order_relaxed);
        if (d > 0.0001f)
        {
            const float k  = 1.0f + d * 24.0f;                 // drive gain into tanh
            const float mk = 1.0f / std::tanh (k);             // makeup to keep level
            for (int ch = 0; ch < outCh; ++ch)
            {
                float* w = out.getWritePointer (ch, startSample);
                for (int i = 0; i < numSamples; ++i)
                    w[i] = std::tanh (k * w[i]) * mk;
            }
        }

        // Delay (feedback) after the filter/drive.
        const float mix = dlyMix.load (std::memory_order_relaxed);
        if (mix > 0.001f)
        {
            const float fb = juce::jlimit (0.0f, 0.95f, dlyFb.load (std::memory_order_relaxed));
            const float ds = juce::jlimit (1.0f, (float) (systemSampleRate - 1.0),
                                           dlyTime.load (std::memory_order_relaxed) * (float) systemSampleRate / 1000.0f);
            delayLine.setDelay (ds);

            float* w0 = out.getWritePointer (0, startSample);
            float* w1 = (outCh > 1) ? out.getWritePointer (1, startSample) : w0;
            for (int i = 0; i < numSamples; ++i)
                for (int ch = 0; ch < juce::jmin (2, outCh); ++ch)
                {
                    float* w = (ch == 0) ? w0 : w1;
                    const float in = w[i];
                    const float d  = delayLine.popSample (ch);
                    delayLine.pushSample (ch, in + d * fb);
                    w[i] = in * (1.0f - mix) + d * mix;
                }
        }
    }

    // 5c. Feed the scope ring (post-FX mono sum) for the spectrum display.
    {
        int wi = scopeWrite.load (std::memory_order_relaxed);
        const int outCh = out.getNumChannels();
        const float* l = out.getReadPointer (0, startSample);
        const float* r = (outCh > 1) ? out.getReadPointer (1, startSample) : l;
        for (int i = 0; i < numSamples; ++i)
        {
            scope[(size_t) wi] = 0.5f * (l[i] + r[i]);
            wi = (wi + 1) & (kScopeSize - 1);
        }
        scopeWrite.store (wi, std::memory_order_release);
    }

    // 6. Diagnostic test tone.
    int tt = testToneRemaining.load (std::memory_order_relaxed);
    if (tt > 0)
    {
        const int total = juce::jmax (1, (int) (0.4 * systemSampleRate));
        const int fade  = juce::jmax (1, (int) (0.005 * systemSampleRate));
        const double inc = 2.0 * juce::MathConstants<double>::pi * 440.0 / systemSampleRate;
        const int outCh = out.getNumChannels();
        float* dL = out.getWritePointer (0);
        float* dR = (outCh > 1) ? out.getWritePointer (1) : dL;
        const int nOut = juce::jmin (tt, numSamples);
        for (int i = 0; i < nOut; ++i)
        {
            const int done = total - tt;
            float amp = 0.2f;
            if (done < fade)    amp *= (float) done / (float) fade;
            else if (tt < fade) amp *= (float) tt   / (float) fade;
            const float s = amp * (float) std::sin (testPhase);
            dL[startSample + i] += s;
            if (outCh > 1) dR[startSample + i] += s;
            testPhase += inc;
            if (testPhase > 2.0 * juce::MathConstants<double>::pi) testPhase -= 2.0 * juce::MathConstants<double>::pi;
            --tt;
        }
        testToneRemaining.store (tt, std::memory_order_relaxed);
    }
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::NoteOn:  triggerPad (c.slot); break;
        case Command::Type::NoteOff: if (c.slot >= 0 && c.slot < kNumPads) voices[(size_t) c.slot].release(); break;
        case Command::Type::Panic:   for (auto& v : voices) v.kill(); break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

void AudioEngine::postNoteOn (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOn; c.slot = slot;
    commands.push (c);
}

void AudioEngine::postNoteOff (int slot) noexcept
{
    Command c; c.type = Command::Type::NoteOff; c.slot = slot;
    commands.push (c);
}

void AudioEngine::postPanic() noexcept
{
    Command c; c.type = Command::Type::Panic;
    commands.push (c);
}

void AudioEngine::postTestTone() noexcept
{
    testToneRemaining.store ((int) (0.4 * systemSampleRate), std::memory_order_relaxed);
}

int AudioEngine::getSampleLength (int slot) const noexcept
{
    if (slot < 0 || slot >= kNumPads) return 0;
    auto* sb = padSample[(size_t) slot];
    return sb != nullptr ? sb->buffer.getNumSamples() : 0;
}

void AudioEngine::publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept
{
    if (slot < 0 || slot >= kNumPads || newBuffer == nullptr)
        return;

    // Reset this pad's trim window to the full new sample.
    padStart[(size_t) slot].store (0, std::memory_order_relaxed);
    padEnd[(size_t) slot].store (newBuffer->buffer.getNumSamples(), std::memory_order_relaxed);

    newBuffer->incReferenceCount();
    if (auto* old = pendingPad[(size_t) slot].exchange (newBuffer.get(), std::memory_order_release))
        old->decReferenceCount();
}

void AudioEngine::collectRetiredSamples() noexcept
{
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

void AudioEngine::copyScope (float* dst, int n) noexcept
{
    const int wi = scopeWrite.load (std::memory_order_acquire);
    for (int i = 0; i < n; ++i)
        dst[i] = scope[(size_t) ((wi - n + i) & (kScopeSize - 1))];
}

void AudioEngine::setStep (int step, int pad, bool on) noexcept
{
    if (step < 0 || step >= kNumSteps || pad < 0 || pad >= kNumPads) return;
    const std::uint16_t bit = (std::uint16_t) (1u << pad);
    std::uint16_t cur = stepMask[(size_t) step].load (std::memory_order_relaxed);
    cur = on ? (std::uint16_t) (cur | bit) : (std::uint16_t) (cur & ~bit);
    stepMask[(size_t) step].store (cur, std::memory_order_relaxed);
}

void AudioEngine::clearPattern() noexcept
{
    for (auto& m : stepMask) m.store (0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
//  Recording (message thread)
// ---------------------------------------------------------------------------

void AudioEngine::startRecording (int slot) noexcept
{
    if (slot < 0 || slot >= kNumPads) return;
    recordSlot = slot;
    recordPos.store (0, std::memory_order_relaxed);
    recording.store (true, std::memory_order_release);
}

SampleBuffer::Ptr AudioEngine::finishRecording() noexcept
{
    recording.store (false, std::memory_order_release);
    SampleBuffer::Ptr sb;
    const int len = recordPos.load (std::memory_order_acquire);
    if (len > 4)
    {
        sb = new SampleBuffer();
        sb->buffer.setSize (1, len);
        sb->buffer.copyFrom (0, 0, recordBuffer, 0, 0, len);
        sb->sourceSampleRate = systemSampleRate;
        publishSample (recordSlot, sb);
    }
    return sb;
}

float AudioEngine::getRecordSeconds() const noexcept
{
    return (float) (recordPos.load (std::memory_order_relaxed) / systemSampleRate);
}

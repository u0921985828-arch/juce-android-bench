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

    const int len = sb->buffer.getNumSamples();
    int st = padStart[(size_t) slot].load (std::memory_order_relaxed);
    int en = padEnd[(size_t) slot].load (std::memory_order_relaxed);
    if (en <= 0 || en > len) en = len;
    if (st < 0 || st >= en)  st = 0;

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

#include "AudioEngine.h"

// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::~AudioEngine()
{
    // Owner has already called shutdownAudio(): no audio callback in flight.
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
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.kill();
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    // 1. Adopt any freshly published per-pad samples at the block boundary.
    //    The audio thread only swaps raw pointers and pushes the old one to the
    //    retired queue (never decrements a ref), so it can never delete.
    for (int slot = 0; slot < kNumPads; ++slot)
    {
        if (auto* incoming = pendingPad[(size_t) slot].exchange (nullptr, std::memory_order_acquire))
        {
            retired.push (padSample[(size_t) slot]);   // old buffer -> message thread
            padSample[(size_t) slot] = incoming;       // ref already held by publisher
        }
    }

    // 2. Clear the output region once; voices sum in additively.
    out.clear (startSample, numSamples);

    auto renderVoices = [this, &out] (int s, int n) noexcept
    {
        for (int i = 0; i < kNumPads; ++i)
            voices[(size_t) i].render (out, s, n, padSample[(size_t) i]);
    };

    // 3. Drain trigger commands into a stack buffer (no allocation).
    constexpr int kMaxCmds = 256;
    Command local[kMaxCmds];
    int n = 0;
    commands.drain ([&local, &n] (const Command& c) noexcept
    {
        if (n < kMaxCmds) local[n++] = c;
    });

    // 4. Sample-accurate sub-block render (P0: startFrame always 0).
    int cursor = 0;
    for (int i = 0; i < n; ++i)
    {
        const int frame = juce::jlimit (0, numSamples, local[i].startFrame);
        if (frame > cursor)
        {
            renderVoices (startSample + cursor, frame - cursor);
            cursor = frame;
        }
        handleCommand (local[i]);
    }
    if (cursor < numSamples)
        renderVoices (startSample + cursor, numSamples - cursor);

    // 5. Diagnostic test tone (added on top): a 440 Hz sine, ~0.4 s, with a
    //    quick 5 ms fade at each end so it doesn't click.
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
            const int done = total - tt;                 // samples already played
            float amp = 0.2f;
            if (done < fade)          amp *= (float) done / (float) fade;         // fade in
            else if (tt < fade)       amp *= (float) tt   / (float) fade;         // fade out
            const float s = amp * (float) std::sin (testPhase);

            dL[startSample + i] += s;
            if (outCh > 1) dR[startSample + i] += s;

            testPhase += inc;
            if (testPhase > 2.0 * juce::MathConstants<double>::pi)
                testPhase -= 2.0 * juce::MathConstants<double>::pi;
            --tt;
        }
        testToneRemaining.store (tt, std::memory_order_relaxed);
    }
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::NoteOn:
            if (c.slot >= 0 && c.slot < kNumPads && padSample[(size_t) c.slot] != nullptr)
                voices[(size_t) c.slot].start (c.slot, c.semitones, c.velocity,
                                               padSample[(size_t) c.slot]->sourceSampleRate,
                                               systemSampleRate);
            break;

        case Command::Type::NoteOff:
            if (c.slot >= 0 && c.slot < kNumPads)
                voices[(size_t) c.slot].release();
            break;

        case Command::Type::Panic:
            for (auto& v : voices)
                v.kill();
            break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

void AudioEngine::postNoteOn (int slot, float semitones, float velocity, int startFrame) noexcept
{
    Command c;
    c.type = Command::Type::NoteOn;
    c.slot = slot; c.semitones = semitones; c.velocity = velocity; c.startFrame = startFrame;
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
    // Arm ~0.4 s of tone; the audio thread reads this atomically.
    testToneRemaining.store ((int) (0.4 * systemSampleRate), std::memory_order_relaxed);
}

void AudioEngine::publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept
{
    if (slot < 0 || slot >= kNumPads || newBuffer == nullptr)
        return;

    newBuffer->incReferenceCount();   // survive the raw-pointer trip to audio thread

    if (auto* old = pendingPad[(size_t) slot].exchange (newBuffer.get(), std::memory_order_release))
        old->decReferenceCount();     // a prior publish never consumed — release here
}

void AudioEngine::collectRetiredSamples() noexcept
{
    // The ONLY place a SampleBuffer is deleted — always on the message thread.
    retired.drain ([] (SampleBuffer* p) { if (p) p->decReferenceCount(); });
}

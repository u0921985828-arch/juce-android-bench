#include "AudioEngine.h"

// ============================================================================
//  AudioEngine implementation. See AudioEngine.h for the threading contract.
// ============================================================================

AudioEngine::~AudioEngine()
{
    // The owner (MainComponent) has already called shutdownAudio(), so no
    // audio callback can be in flight. Release every reference we still hold.
    if (audioThreadSample != nullptr)
    {
        audioThreadSample->decReferenceCount();
        audioThreadSample = nullptr;
    }
    if (auto* p = pendingSample.exchange (nullptr)) p->decReferenceCount();
    if (auto* r = retiredSample.exchange (nullptr)) r->decReferenceCount();
}

// ---------------------------------------------------------------------------
//  Audio thread
// ---------------------------------------------------------------------------

void AudioEngine::prepareToPlay (double sampleRate, int maxBlockSize) noexcept
{
    // May be called again on a device / sample-rate change. Nothing here
    // allocates. Active voices keep the delta they computed at start(); a rate
    // change mid-note plays slightly off until retriggered (fine for P0).
    systemSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    maxBlock         = (maxBlockSize > 0) ? maxBlockSize : 512;
}

void AudioEngine::releaseResources() noexcept
{
    for (auto& v : voices)
        v.stop();
}

void AudioEngine::renderNextBlock (juce::AudioBuffer<float>& out,
                                   int startSample, int numSamples) noexcept
{
    juce::ScopedNoDenormals noDenormals;   // FTZ/DAZ: pitched/Hermite tails -> denormals

    // 1. Adopt any freshly published sample at the block boundary. The audio
    //    thread only exchanges raw pointers and never decrements a ref count,
    //    so it can never trigger a delete.
    if (auto* incoming = pendingSample.exchange (nullptr, std::memory_order_acquire))
    {
        SampleBuffer* old = audioThreadSample;
        audioThreadSample = incoming;   // reference already held by the publisher

        // Retire the old buffer (ref intact) for the message thread to delete.
        // Under P0's serialized-reload invariant (Load button disabled until the
        // GC timer drains) this slot is always empty here; a clobber would leak,
        // not crash. P1 replaces the single slot with a small SPSC free-list.
        SampleBuffer* clobbered = retiredSample.exchange (old, std::memory_order_release);
        juce::ignoreUnused (clobbered);
    }

    // 2. Clear the output region once; voices sum in additively.
    out.clear (startSample, numSamples);

    auto renderVoices = [this, &out] (int s, int num) noexcept
    {
        for (auto& v : voices)
            v.render (out, s, num, audioThreadSample);
    };

    // 3. Drain trigger commands into a stack buffer (no allocation).
    constexpr int kMaxCmds = 256;
    Command local[kMaxCmds];
    int n = 0;
    commands.drain ([&local, &n] (const Command& c) noexcept
    {
        if (n < kMaxCmds)
            local[n++] = c;
    });

    // 4. Sample-accurate sub-block render. Commands are non-decreasing in
    //    startFrame (P0: always 0). Render the gap before each command, apply
    //    it at its frame, then render the remainder.
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
}

void AudioEngine::handleCommand (const Command& c) noexcept
{
    switch (c.type)
    {
        case Command::Type::NoteOn:
            if (c.slot >= 0 && c.slot < kNumVoices && audioThreadSample != nullptr)
                voices[(size_t) c.slot].start (c.slot, c.semitones, c.velocity,
                                               audioThreadSample->sourceSampleRate,
                                               systemSampleRate);
            break;

        case Command::Type::NoteOff:
            if (c.slot >= 0 && c.slot < kNumVoices)
                voices[(size_t) c.slot].stop();
            break;

        case Command::Type::Panic:
            for (auto& v : voices)
                v.stop();
            break;
    }
}

// ---------------------------------------------------------------------------
//  Message thread
// ---------------------------------------------------------------------------

void AudioEngine::postNoteOn (int slot, float semitones, float velocity, int startFrame) noexcept
{
    Command c;
    c.type       = Command::Type::NoteOn;
    c.slot       = slot;
    c.semitones  = semitones;
    c.velocity   = velocity;
    c.startFrame = startFrame;
    commands.push (c);   // if full, dropped — acceptable for a trigger
}

void AudioEngine::postNoteOff (int slot) noexcept
{
    Command c;
    c.type = Command::Type::NoteOff;
    c.slot = slot;
    commands.push (c);
}

void AudioEngine::postPanic() noexcept
{
    Command c;
    c.type = Command::Type::Panic;
    commands.push (c);
}

void AudioEngine::publishSample (SampleBuffer::Ptr newBuffer) noexcept
{
    if (newBuffer == nullptr)
        return;

    // Add one explicit reference so the object survives the raw-pointer trip to
    // the audio thread; the local Ptr's own reference is dropped on return.
    newBuffer->incReferenceCount();

    if (auto* old = pendingSample.exchange (newBuffer.get(), std::memory_order_release))
        old->decReferenceCount();   // a prior publish never consumed — release it here
}

void AudioEngine::collectRetiredSamples() noexcept
{
    // The ONLY place a SampleBuffer is deleted — always on the message thread.
    if (auto* r = retiredSample.exchange (nullptr, std::memory_order_acquire))
        r->decReferenceCount();
}

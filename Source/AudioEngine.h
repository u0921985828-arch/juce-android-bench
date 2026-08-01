#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Voice.h"
#include "SampleBuffer.h"
#include "CommandFifo.h"

// ============================================================================
//  AudioEngine — the real-time core. Plain class (not a JUCE component).
//
//  Threading contract:
//    * AUDIO thread   : prepareToPlay / releaseResources / renderNextBlock.
//                       Renders only. ZERO alloc, lock, I/O, std::string, free.
//                       Never calls decReferenceCount (so it can never delete).
//    * MESSAGE thread : postNoteOn / postNoteOff / postPanic (enqueue),
//                       publishSample (atomic hand-off), collectRetiredSamples
//                       (the ONLY place a SampleBuffer is deleted).
//
//  Sample data crosses threads via two atomic single-slot mailboxes:
//    pendingSample  : message -> audio  (a freshly decoded buffer to adopt)
//    retiredSample  : audio   -> message (the old buffer to delete)
//  The audio thread holds the live buffer as a RAW pointer (no automatic Ptr
//  ref ops on the audio thread). Ref-count bookkeeping is done explicitly.
// ============================================================================
class AudioEngine
{
public:
    AudioEngine() = default;
    ~AudioEngine();

    // --- Audio thread ---
    void prepareToPlay (double sampleRate, int maxBlockSize) noexcept;
    void releaseResources() noexcept;
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

    // --- Message thread ---
    void postNoteOn  (int slot, float semitones, float velocity, int startFrame = 0) noexcept;
    void postNoteOff (int slot) noexcept;
    void postPanic() noexcept;

    // Adopt a freshly decoded buffer. Takes a Ptr and keeps the object alive
    // across the raw-pointer trip by adding one explicit reference.
    void publishSample (SampleBuffer::Ptr newBuffer) noexcept;

    // Drain the retiree mailbox and delete (message thread, e.g. on a Timer).
    void collectRetiredSamples() noexcept;

private:
    void handleCommand (const Command& c) noexcept;   // audio thread

    static constexpr int kNumVoices = 2;   // P0: 1-2 pads
    std::array<Voice, kNumVoices> voices {};

    CommandFifo commands;

    // Live sample, audio-thread-owned as a raw pointer (holds one reference).
    SampleBuffer* audioThreadSample = nullptr;

    // Cross-thread single-slot mailboxes.
    std::atomic<SampleBuffer*> pendingSample { nullptr };   // message -> audio
    std::atomic<SampleBuffer*> retiredSample { nullptr };   // audio   -> message

    double systemSampleRate = 44100.0;   // F_sys
    int    maxBlock         = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

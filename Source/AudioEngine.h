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
//  P1 step 1: a pad MATRIX. kNumPads slots, each with its own sample; one voice
//  per pad. Tapping pad i triggers voice i playing slot i's buffer.
//
//  Threading contract (unchanged from P0):
//    * AUDIO thread   : prepareToPlay / releaseResources / renderNextBlock.
//                       Render only. ZERO alloc/lock/IO/std::string/free.
//                       Never calls decReferenceCount (so it can never delete).
//    * MESSAGE thread : postNoteOn/Off/Panic, publishSample(slot,...),
//                       collectRetiredSamples (the ONLY place a buffer is freed).
//
//  Per-pad samples cross threads via a pending mailbox per slot (message->audio)
//  plus a shared lock-free retired queue (audio->message) drained by a timer.
// ============================================================================
class AudioEngine
{
public:
    static constexpr int kNumPads = 16;

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

    // Adopt a freshly decoded buffer into pad `slot` (keeps it alive across the
    // raw-pointer trip with one explicit reference).
    void publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept;

    // Drain the retired queue and delete (message thread, e.g. on a Timer).
    void collectRetiredSamples() noexcept;

private:
    void handleCommand (const Command& c) noexcept;   // audio thread

    // Lock-free retired-pointer queue (single producer: audio thread;
    // single consumer: message thread). Holds buffers awaiting deletion.
    class RetiredQueue
    {
    public:
        void push (SampleBuffer* p) noexcept   // audio thread
        {
            if (p == nullptr) return;
            int s1, z1, s2, z2;
            fifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 + z2 < 1) return;           // full (not expected) — drop
            (z1 > 0 ? store[(size_t) s1] : store[(size_t) s2]) = p;
            fifo.finishedWrite (z1 + z2);
        }
        template <typename Fn>
        void drain (Fn&& fn) noexcept          // message thread
        {
            int s1, z1, s2, z2;
            fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) fn (store[(size_t) (s1 + i)]);
            for (int i = 0; i < z2; ++i) fn (store[(size_t) (s2 + i)]);
            fifo.finishedRead (z1 + z2);
        }
    private:
        static constexpr int cap = 128;
        juce::AbstractFifo fifo { cap };
        std::array<SampleBuffer*, cap> store {};
    };

    std::array<Voice, kNumPads> voices {};
    CommandFifo commands;

    // Per-pad live sample (audio-thread-owned raw pointers, each holds a ref).
    std::array<SampleBuffer*, kNumPads>              padSample {};
    std::array<std::atomic<SampleBuffer*>, kNumPads> pendingPad {};   // message -> audio
    RetiredQueue retired;                                             // audio -> message

    double systemSampleRate = 44100.0;   // F_sys
    int    maxBlock         = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

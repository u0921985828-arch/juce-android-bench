#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "Voice.h"
#include "SampleBuffer.h"
#include "CommandFifo.h"

// ============================================================================
//  AudioEngine — real-time core (P1).
//
//  Features: 16-pad matrix (sample per pad), per-pad params (pitch, gain, trim
//  start/end, loop, reverse), a 16-step sequencer with BPM, and mic recording.
//
//  Threading: audio thread renders only (no alloc/lock/free); message thread
//  posts commands, edits per-pad params (atomics), edits the pattern, and does
//  all memory frees. Per-pad samples cross via a pending mailbox per slot; old
//  buffers go to a lock-free retired queue drained by a timer.
// ============================================================================
class AudioEngine
{
public:
    static constexpr int kNumPads  = 16;
    static constexpr int kNumSteps = 16;

    AudioEngine() = default;
    ~AudioEngine();

    // --- Audio thread ---
    void prepareToPlay (double sampleRate, int maxBlockSize) noexcept;
    void releaseResources() noexcept;
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

    // --- Triggers (message thread) ---
    void postNoteOn  (int slot) noexcept;   // uses the pad's stored params
    void postNoteOff (int slot) noexcept;
    void postPanic() noexcept;
    void postTestTone() noexcept;

    // --- Per-pad params (message thread) ---
    void setPadPitch   (int slot, float semis) noexcept { store (padPitch,   slot, semis); }
    void setPadGain    (int slot, float g)     noexcept { store (padGain,    slot, g); }
    void setPadStart   (int slot, int s)       noexcept { store (padStart,   slot, s); }
    void setPadEnd     (int slot, int e)       noexcept { store (padEnd,     slot, e); }
    void setPadLoop    (int slot, bool b)      noexcept { store (padLoop,    slot, b); }
    void setPadReverse (int slot, bool b)      noexcept { store (padReverse, slot, b); }
    void setPadChoke   (int slot, int group)   noexcept { store (padChoke,   slot, group); }   // 0 = none
    int  getSampleLength (int slot) const noexcept;   // 0 if none

    // --- Samples (message thread) ---
    void publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept;
    void collectRetiredSamples() noexcept;

    // --- Sequencer (message thread) ---
    void setPlaying (bool p) noexcept { playing.store (p, std::memory_order_relaxed); }
    bool isPlaying() const noexcept   { return playing.load (std::memory_order_relaxed); }
    void setBpm (double b) noexcept   { bpm.store (b, std::memory_order_relaxed); }
    void setStep (int step, int pad, bool on) noexcept;
    void clearPattern() noexcept;
    int  getPlayStep() const noexcept { return playStep.load (std::memory_order_relaxed); }

    // --- Master FX: filter + drive (message thread setters) ---
    void setFxType   (int t)     noexcept { fxType.store   (t, std::memory_order_relaxed); }   // 0 LPF, 1 HPF
    void setFxCutoff (float hz)  noexcept { fxCutoff.store (hz, std::memory_order_relaxed); }
    void setFxReso   (float q)   noexcept { fxReso.store   (q, std::memory_order_relaxed); }
    void setFxDrive  (float amt) noexcept { fxDrive.store  (amt, std::memory_order_relaxed); }  // 0..1

    // --- Recording (message thread) ---
    void              startRecording (int slot) noexcept;
    SampleBuffer::Ptr finishRecording() noexcept;   // stop + build + publish; returns the buffer
    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }
    float getRecordSeconds() const noexcept;

private:
    void handleCommand (const Command& c) noexcept;   // audio thread
    void triggerPad (int slot) noexcept;              // audio thread

    template <typename Arr, typename V>
    static void store (Arr& a, int slot, V v) noexcept
    {
        if (slot >= 0 && slot < kNumPads) a[(size_t) slot].store (v, std::memory_order_relaxed);
    }

    class RetiredQueue
    {
    public:
        void push (SampleBuffer* p) noexcept
        {
            if (p == nullptr) return;
            int s1, z1, s2, z2;
            fifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 + z2 < 1) return;
            (z1 > 0 ? store[(size_t) s1] : store[(size_t) s2]) = p;
            fifo.finishedWrite (z1 + z2);
        }
        template <typename Fn>
        void drain (Fn&& fn) noexcept
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

    std::array<SampleBuffer*, kNumPads>              padSample {};
    std::array<std::atomic<SampleBuffer*>, kNumPads> pendingPad {};
    RetiredQueue retired;

    // Per-pad params (message writes, audio reads).
    std::array<std::atomic<float>, kNumPads> padPitch {};
    std::array<std::atomic<float>, kNumPads> padGain {};
    std::array<std::atomic<int>,   kNumPads> padStart {};
    std::array<std::atomic<int>,   kNumPads> padEnd {};
    std::array<std::atomic<bool>,  kNumPads> padLoop {};
    std::array<std::atomic<bool>,  kNumPads> padReverse {};
    std::array<std::atomic<int>,   kNumPads> padChoke {};   // 0 = none, 1..8 = choke group

    // Sequencer.
    std::atomic<bool>   playing { false };
    std::atomic<double> bpm { 120.0 };
    std::array<std::atomic<std::uint16_t>, kNumSteps> stepMask {};
    std::atomic<int>    playStep { -1 };
    double stepAccum = 0.0;      // audio-thread only
    int    currentStep = 0;      // audio-thread only
    bool   wasPlaying = false;   // audio-thread only

    // Recording.
    std::atomic<bool> recording { false };
    std::atomic<int>  recordPos { 0 };
    juce::AudioBuffer<float> recordBuffer;   // mono, allocated in prepareToPlay
    int recordSlot = 0;

    // Master FX: filter + drive.
    juce::dsp::StateVariableTPTFilter<float> masterFilter;
    std::atomic<int>   fxType   { 0 };          // 0 LPF, 1 HPF
    std::atomic<float> fxCutoff { 20000.0f };
    std::atomic<float> fxReso   { 0.707f };
    std::atomic<float> fxDrive  { 0.0f };        // 0..1

    // Diagnostic test tone.
    std::atomic<int> testToneRemaining { 0 };
    double           testPhase = 0.0;

    double systemSampleRate = 44100.0;
    int    maxBlock         = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

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
    static constexpr int kNumPads       = 16;
    static constexpr int kNumSteps      = 48;   // max steps per pattern (length is variable, see below)
    static constexpr int kMinPatLen     = 16;
    static constexpr int kMaxPatLen     = kNumSteps;   // 48 = 6 rows of 8 — keeps step cells readable
    static constexpr int kNumPatterns   = 8;    // pattern banks
    static constexpr int kMaxChain      = 16;   // chain slots (pattern indices, in play order)

    AudioEngine();
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
    void setPadPan     (int slot, float p)     noexcept { store (padPan,     slot, p); }        // -1..1
    void setPadAttack  (int slot, float ms)    noexcept { store (padAttack,  slot, ms); }
    void setPadRelease (int slot, float ms)    noexcept { store (padRelease, slot, ms); }
    int  getSampleLength (int slot) const noexcept;   // 0 if none

    // --- Samples (message thread) ---
    void publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept;
    void collectRetiredSamples() noexcept;

    // --- Sequencer (message thread) ---
    void setPlaying (bool p) noexcept { playing.store (p, std::memory_order_relaxed); }
    bool isPlaying() const noexcept   { return playing.load (std::memory_order_relaxed); }
    void setBpm (double b) noexcept   { bpm.store (b, std::memory_order_relaxed); }
    void setStep (int patternIdx, int step, int pad, bool on) noexcept;
    void clearPattern (int patternIdx) noexcept;
    int  getPlayStep() const noexcept { return playStep.load (std::memory_order_relaxed); }

    // --- Pattern length (message thread) ---
    //  How many steps (kMinPatLen..kMaxPatLen, i.e. 16..64) play before this
    //  bank loops/hands off to the next chain entry — FL-Studio-style
    //  variable pattern length.
    void setPatternLength (int patternIdx, int len) noexcept;
    int  getPatternLength (int patternIdx) const noexcept;

    // --- Pattern chain (message thread) ---
    //  editPattern is the bank the UI edits/steps; when the chain is empty,
    //  playback simply loops editPattern (legacy single-pattern behaviour).
    //  A non-empty chain plays its pattern banks in order, looping the chain.
    void setEditPattern (int p) noexcept { editPattern.store (juce::jlimit (0, kNumPatterns - 1, p), std::memory_order_relaxed); }
    int  getEditPattern() const noexcept { return editPattern.load (std::memory_order_relaxed); }
    bool addToChain (int patternIdx) noexcept;
    void clearChain() noexcept { chainLength.store (0, std::memory_order_relaxed); }
    int  getChainLength() const noexcept { return chainLength.load (std::memory_order_relaxed); }
    int  getChainSlot (int i) const noexcept { return (i >= 0 && i < kMaxChain) ? chainSlots[(size_t) i].load (std::memory_order_relaxed) : 0; }
    int  getPlayingPattern() const noexcept { return playingPattern.load (std::memory_order_relaxed); }

    // --- Piano roll: per-step semitone offset from the pad's own pitch ---
    //  (message thread). Lets one pad's sample play a melody across the
    //  16-step grid instead of one fixed pitch per pad.
    void setStepNote (int patternIdx, int step, int pad, int semis) noexcept;
    int  getStepNote  (int patternIdx, int step, int pad) const noexcept;

    // UI feedback: bitmask of pads triggered since the last call (taps + sequencer).
    std::uint32_t fetchTriggered() noexcept { return triggeredMask.exchange (0, std::memory_order_relaxed); }

    // --- Master FX: filter + drive (message thread setters) ---
    void setFxType   (int t)     noexcept { fxType.store   (t, std::memory_order_relaxed); }   // 0 LPF, 1 HPF
    void setFxCutoff (float hz)  noexcept { fxCutoff.store (hz, std::memory_order_relaxed); }
    void setFxReso   (float q)   noexcept { fxReso.store   (q, std::memory_order_relaxed); }
    void setFxDrive  (float amt) noexcept { fxDrive.store  (amt, std::memory_order_relaxed); }  // 0..1
    void setDlyTime  (float ms)  noexcept { dlyTime.store  (ms,  std::memory_order_relaxed); }
    void setDlyFb    (float f)    noexcept { dlyFb.store    (f,   std::memory_order_relaxed); }
    void setDlyMix   (float m)    noexcept { dlyMix.store   (m,   std::memory_order_relaxed); }

    // --- Scope (message thread): copy the last n post-FX master samples ---
    void copyScope (float* dst, int n) noexcept;

    // --- Output peak meters (message thread): max |sample| since last read ---
    float readOutPeakL() noexcept { return outPeakL.exchange (0.0f, std::memory_order_relaxed); }
    float readOutPeakR() noexcept { return outPeakR.exchange (0.0f, std::memory_order_relaxed); }

    // --- Recording (message thread) ---
    void              startRecording (int slot) noexcept;
    SampleBuffer::Ptr finishRecording() noexcept;   // stop + build + publish; returns the buffer
    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }
    float getRecordSeconds() const noexcept;

private:
    void handleCommand (const Command& c) noexcept;               // audio thread
    void triggerPad (int slot, int extraSemis = 0) noexcept;      // audio thread

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
    std::array<std::atomic<float>, kNumPads> padPan {};      // -1 (L) .. 0 (centre) .. 1 (R)
    std::array<std::atomic<float>, kNumPads> padAttack {};   // ms
    std::array<std::atomic<float>, kNumPads> padRelease {};  // ms

    // Sequencer.
    std::atomic<bool>   playing { false };
    std::atomic<double> bpm { 120.0 };
    std::array<std::array<std::atomic<std::uint16_t>, kNumSteps>, kNumPatterns> patternBank {};
    std::atomic<int>    playStep { -1 };
    std::atomic<std::uint32_t> triggeredMask { 0 };   // pads triggered, read by UI
    double stepAccum = 0.0;      // audio-thread only
    int    currentStep = 0;      // audio-thread only
    bool   wasPlaying = false;   // audio-thread only

    // Pattern chain.
    std::array<std::atomic<int>, kNumPatterns> patternLength {};   // steps, kMinPatLen..kMaxPatLen
    std::atomic<int> editPattern { 0 };                        // bank the UI is editing
    std::array<std::atomic<int>, kMaxChain> chainSlots {};
    std::atomic<int> chainLength { 0 };
    std::atomic<int> playingPattern { 0 };                     // bank actually sounding, for UI
    int chainPos = 0;            // audio-thread only

    // Piano roll: per-(pattern, step, pad) semitone offset from the pad's own pitch.
    std::array<std::array<std::array<std::atomic<std::int8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepNote {};

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

    // Master delay.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    std::atomic<float> dlyTime { 250.0f };       // ms
    std::atomic<float> dlyFb   { 0.35f };        // 0..0.95
    std::atomic<float> dlyMix  { 0.0f };         // 0..1

    // Scope ring (post-FX mono), written by the audio thread.
    static constexpr int kScopeSize = 2048;   // power of two
    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    // Running output peak per channel; UI consumes-and-resets via readOutPeak*.
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };

    // Diagnostic test tone.
    std::atomic<int> testToneRemaining { 0 };
    double           testPhase = 0.0;

    double systemSampleRate = 44100.0;
    int    maxBlock         = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

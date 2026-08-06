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
    static constexpr int kNumFx         = 6;    // ISO, HPF, DRV, DLY, CRSH, REV — the order the UI shows
    static constexpr int kNumSteps      = 64;   // max steps per pattern (length is variable, see below)
    static constexpr int kMinPatLen     = 16;
    static constexpr int kMaxPatLen     = kNumSteps;   // 64 = four bars of 16
    static constexpr int kNumPatterns   = 8;    // pattern banks
    static constexpr int kMaxChain      = 16;   // chain slots (pattern indices, in play order)

    //  --- Song / playlist -------------------------------------------------
    //  The chain was one queue of banks: a track in a straight line. A song is
    //  a TIMELINE — several lanes running at once, each holding either a
    //  pattern (which occupies as many bars as its own length needs) or a
    //  single sample fired at that bar. That is what lets a break, a vocal
    //  one-shot and a drum pattern all land on bar 9 together.
    static constexpr int kSongLanes = 4;
    static constexpr int kSongBars  = 64;
    static constexpr int kBarSteps  = 16;

    //  Cell encoding, kept as one int so the audio thread reads it atomically:
    //     0            empty
    //     1..8         a pattern STARTS here (value = bank + 1)
    //     -(pad+1)     a one-shot: fire this pad at the top of the bar
    //     kContinued   this bar is still covered by a pattern started earlier
    static constexpr int kContinued = 1000;

    AudioEngine();
    ~AudioEngine();

    // --- Audio thread ---
    void prepareToPlay (double sampleRate, int maxBlockSize, int inputChannels = 0) noexcept;
    void releaseResources() noexcept;
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

    // --- Triggers (message thread) ---
    void postNoteOn  (int slot, float vel = 1.0f) noexcept;   // uses the pad's stored params
    //  Audition: same note, but read from a point in the source instead of
    //  from the pad's trim. Nothing about the pad changes.
    void postNoteOnFrom (int slot, float from01, float vel = 1.0f) noexcept;
    void postNoteOff (int slot) noexcept;
    void postPanic() noexcept;
    void postTestTone() noexcept;

    void noteOnByLifeboat (int slot) noexcept;   // see postNoteOn

    //  How many triggers the command queue has refused since the last check.
    //  Reading it clears it. A non-zero answer means taps are reaching the
    //  audio thread by the lifeboat rather than by the queue, which is worth
    //  rebuilding the device over.
    int takeDroppedCommands() noexcept { return droppedCommands.exchange (0, std::memory_order_relaxed); }

    // --- Per-pad params (message thread) ---
    void setPadPitch   (int slot, float semis) noexcept { store (padPitch,   slot, semis); }
    void setPadGain    (int slot, float g)     noexcept { store (padGain,    slot, g); }
    void setPadStart   (int slot, int s)       noexcept { store (padStart,   slot, s); }
    void setPadEnd     (int slot, int e)       noexcept { store (padEnd,     slot, e); }
    void setPadLoop    (int slot, bool b)      noexcept { store (padLoop,    slot, b); }
    void setPadReverse (int slot, bool b)      noexcept { store (padReverse, slot, b); }
    //  Whether pitch drags the length with it (tape) or not (tone).
    void setPadKeepLength (int slot, bool b)   noexcept { store (padKeepLength, slot, b); }
    bool getPadKeepLength (int slot) const noexcept
    { return slot >= 0 && slot < kNumPads && padKeepLength[(size_t) slot].load (std::memory_order_relaxed); }
    void setPadChoke   (int slot, int group)   noexcept { store (padChoke,   slot, group); }   // 0 = none
    //  AUTOCUT: a pad that cuts ITSELF. CHOKE settles arguments between
    //  different pads (the open hat and the closed one); this one is about a
    //  pad retriggering over its own tail, which is what a hardware sampler
    //  does by default and what makes a stab or a vocal sound like one
    //  instrument instead of a chorus of itself.
    void setPadSelfCut (int slot, bool on)     noexcept { store (padSelfCut, slot, on); }
    bool getPadSelfCut (int slot) const noexcept
    { return slot >= 0 && slot < kNumPads && padSelfCut[(size_t) slot].load (std::memory_order_relaxed); }
    void setPadPan     (int slot, float p)     noexcept { store (padPan,     slot, p); }        // -1..1
    void setPadAttack  (int slot, float ms)    noexcept { store (padAttack,  slot, ms); }
    void setPadRelease (int slot, float ms)    noexcept { store (padRelease, slot, ms); }
    //  Mute and solo fold into the SAME gain the voices already follow at
    //  control rate, so they take hold on notes that are already sounding —
    //  a mute you have to wait out is not a mute.
    void setPadMute    (int slot, bool m)      noexcept { store (padMute, slot, m); refreshSolo(); }
    void setPadSolo    (int slot, bool s)      noexcept { store (padSolo, slot, s); refreshSolo(); }
    bool isPadMuted    (int slot) const noexcept { return slot >= 0 && slot < kNumPads && padMute[(size_t) slot].load (std::memory_order_relaxed); }
    bool isPadSoloed   (int slot) const noexcept { return slot >= 0 && slot < kNumPads && padSolo[(size_t) slot].load (std::memory_order_relaxed); }
    bool anySolo() const noexcept { return soloActive.load (std::memory_order_relaxed); }
    void clearSolo() noexcept { for (auto& s : padSolo) s.store (false, std::memory_order_relaxed); refreshSolo(); }

    // Audible gain for a pad = its own level, silenced by mute or by someone
    // else's solo.
    float effectiveGain (int slot) const noexcept
    {
        if (slot < 0 || slot >= kNumPads) return 0.0f;
        if (padMute[(size_t) slot].load (std::memory_order_relaxed)) return 0.0f;
        if (soloActive.load (std::memory_order_relaxed)
            && ! padSolo[(size_t) slot].load (std::memory_order_relaxed)) return 0.0f;
        return padGain[(size_t) slot].load (std::memory_order_relaxed);
    }
    int  getSampleLength (int slot) const noexcept;   // 0 if none

    //  Polyphony, decided by the device rather than by a constant. Called once
    //  before any audio runs; both values are plain ints because nothing reads
    //  them except the audio thread, and it reads them after they are set.
    void setPolyphony (int totalVoices, int perPad) noexcept
    {
        voiceLimit     = juce::jlimit (4, kNumVoices, totalVoices);
        maxVoicesOnPad = juce::jlimit (2, voiceLimit, perPad);
    }
    int getPolyphony() const noexcept { return voiceLimit; }

    //  Where this pad's read head is inside its whole source, 0..1, or -1 when
    //  nothing of it is sounding. Cosmetic: the UI draws it, nobody acts on it,
    //  so a block's worth of staleness is exactly right.
    float getPadPosition01 (int slot) const noexcept
    {
        return (slot >= 0 && slot < kNumPads)
                 ? padPos[(size_t) slot].load (std::memory_order_relaxed) : -1.0f;
    }

    //  How much of the pool is in use. Read by the UI for a polyphony readout
    //  and by the offline checks; a benign race with the audio thread is fine
    //  for both, since neither acts on the number.
    int getActiveVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices) if (v.active) ++n;
        return n;
    }

    // --- Samples (message thread) ---
    void publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept;
    void collectRetiredSamples() noexcept;

    // --- Sequencer (message thread) ---
    void setPlaying (bool p) noexcept { playing.store (p, std::memory_order_relaxed); }
    bool isPlaying() const noexcept   { return playing.load (std::memory_order_relaxed); }
    // float, not double: atomic<double> is NOT lock-free on 32-bit ARM, and
    // this is read inside the audio callback (the Android armeabi-v7a build
    // would otherwise take a runtime lock there).
    void setBpm (double b) noexcept   { bpm.store ((float) b, std::memory_order_relaxed); }
    double getBpm() const noexcept    { return (double) bpm.load (std::memory_order_relaxed); }
    void setStep (int patternIdx, int step, int pad, bool on) noexcept;
    void clearPattern (int patternIdx) noexcept;
    int  getPlayStep() const noexcept { return playStep.load (std::memory_order_relaxed); }
    // How far through the current step we are, 0..1. The UI needs it to
    // quantise a live pad hit to the NEAREST step instead of always the one
    // that happens to be sounding — a hit landing just before the beat would
    // otherwise be written a whole 16th late.
    float getStepPhase() const noexcept { return stepPhase.load (std::memory_order_relaxed); }

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

    // --- Song / playlist (message thread) --------------------------------
    void setSongMode (bool on) noexcept { songMode.store (on, std::memory_order_relaxed); }
    bool isSongMode() const noexcept    { return songMode.load (std::memory_order_relaxed); }
    void setSongCell (int lane, int bar, int value) noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return;
        songCell[(size_t) lane][(size_t) bar].store (value, std::memory_order_relaxed);
    }
    int getSongCell (int lane, int bar) const noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return 0;
        return songCell[(size_t) lane][(size_t) bar].load (std::memory_order_relaxed);
    }
    void clearSong() noexcept
    {
        for (auto& lane : songCell) for (auto& c : lane) c.store (0, std::memory_order_relaxed);
    }
    void setSongLength (int bars) noexcept { songBars.store (juce::jlimit (1, kSongBars, bars), std::memory_order_relaxed); }
    int  getSongLength() const noexcept    { return songBars.load (std::memory_order_relaxed); }
    int  getSongBar() const noexcept       { return songBar.load (std::memory_order_relaxed); }

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

    // --- The six effects -------------------------------------------------
    //  ISO, HPF, DRIVE, DELAY, CRUSH, REVERB. Six independent stages in that
    //  order, three parameters each, and the third is always MIX. MIX is the
    //  switch as well as the amount: at zero the stage is skipped outright,
    //  so an effect you are not using costs nothing and cannot colour the
    //  sound. Nothing is shared between stages, which is what lets ISO and
    //  HPF run at once as a band-pass instead of fighting over one filter.
    void setIsoCutoff (float hz) noexcept { fxCutoff.store (hz, std::memory_order_relaxed); }
    void setIsoReso   (float q)  noexcept { fxReso.store   (q,  std::memory_order_relaxed); }
    void setIsoMix    (float m)  noexcept { fxMix.store    (m,  std::memory_order_relaxed); }

    void setHpFreq (float hz) noexcept { hpFreq.store (hz, std::memory_order_relaxed); }
    void setHpReso (float q)  noexcept { hpReso.store (q,  std::memory_order_relaxed); }
    void setHpMix  (float m)  noexcept { hpMix.store  (m,  std::memory_order_relaxed); }

    void setDrvTone (float hz) noexcept { drvTone.store (hz, std::memory_order_relaxed); }
    void setDrvMix  (float m)  noexcept { drvMix.store  (m,  std::memory_order_relaxed); }

    void setCrushBits (float b) noexcept { crBits.store (b, std::memory_order_relaxed); }
    void setCrushRate (float r) noexcept { crRate.store (r, std::memory_order_relaxed); }
    void setCrushMix  (float m) noexcept { crMix.store  (m, std::memory_order_relaxed); }

    //  How much of one pad reaches one effect. 1 is everything, which is the
    //  default so that switching an effect on still colours the whole kit the
    //  way it always did; pull a pad down and that pad stops being sent.
    void setPadSend (int slot, int fx, float v) noexcept
    {
        if (slot >= 0 && slot < kNumPads && fx >= 0 && fx < kNumFx)
            padSend[(size_t) slot][(size_t) fx].store (juce::jlimit (0.0f, 1.0f, v), std::memory_order_relaxed);
    }
    float getPadSend (int slot, int fx) const noexcept
    {
        if (slot < 0 || slot >= kNumPads || fx < 0 || fx >= kNumFx) return 0.0f;
        return padSend[(size_t) slot][(size_t) fx].load (std::memory_order_relaxed);
    }

    void setRevSize (float s) noexcept { rvSize.store (s, std::memory_order_relaxed); }
    void setRevDamp (float d) noexcept { rvDamp.store (d, std::memory_order_relaxed); }
    void setRevMix  (float m) noexcept { rvMix.store  (m, std::memory_order_relaxed); }
    // How much silence a bounce must keep past the last note so the tail is
    // not guillotined. Only AUDIBLE stages count — a ten-second delay with
    // its mix at zero must not pad every export.
    double getFxTailSeconds() const noexcept
    {
        double t = 0.0;
        if (dlyMix.load (std::memory_order_relaxed) > 0.001f)
            t = juce::jmax (t, 4.0 * (double) dlyTime.load (std::memory_order_relaxed) * 0.001);
        if (rvMix.load (std::memory_order_relaxed) > 0.001f)
            t = juce::jmax (t, 3.0);
        return t;
    }

    // --- Scope (message thread): copy the last n post-FX master samples ---
    void copyScope (float* dst, int n) noexcept;

    //  Min/max columns spanning ~0.74 s of master output, oldest first.
    //  Returns how many were written. See renderNextBlock section 5c.
    int  copyScopeColumns (float* dstMin, float* dstMax, int n) noexcept;
    static constexpr int kMaxScopeColumns = 256;

    // --- Output peak meters (message thread): max |sample| since last read ---
    float readOutPeakL() noexcept { return outPeakL.exchange (0.0f, std::memory_order_relaxed); }
    float readOutPeakR() noexcept { return outPeakR.exchange (0.0f, std::memory_order_relaxed); }

    // --- Offline bounce (message thread) --------------------------------
    //  An export does NOT render through this engine. It builds a SECOND
    //  engine, copies the whole machine into it and drives that one from a
    //  background thread as fast as the CPU allows. Two reasons: the live
    //  audio callback is never disturbed (you can keep playing while a
    //  bounce runs), and the render is not tied to real time — three minutes
    //  of music does not take three minutes to write.
    void setOffline (bool o) noexcept { offlineMode = o; }

    //  Copies every parameter, pattern, note, chain slot and song cell.
    //  Samples are NOT copied here: the caller publishes the same
    //  ref-counted buffers, so a bounce costs no extra audio memory.
    void copyStateFrom (const AudioEngine& src) noexcept;

    //  How long a bounce of the CURRENT mode would be, in 16th-note steps:
    //  the song's bars, or the chain's patterns end to end, or just the
    //  pattern being edited. Zero means there is nothing to render.
    int  lengthInSteps() const noexcept;

    //  Is there actually a note anywhere in what would be rendered? A pattern
    //  bank always has a LENGTH, so length alone would happily export two
    //  bars of silence.
    bool hasContentToRender() const noexcept;

    // --- Latency probe (message thread) ----------------------------------
    //  The only honest way to know what the round trip really is: emit a
    //  click at a frame we chose, record the microphone through the same
    //  callback, and measure how far apart the two ended up. Everything the
    //  device reports is a claim; this is a measurement.
    void  startLatencyProbe() noexcept;
    bool  isProbing() const noexcept { return probing.load (std::memory_order_acquire); }
    //  Milliseconds from emitting to hearing, or -1 if nothing was heard.
    float finishLatencyProbe() const noexcept;

    // --- Recording (message thread) ---
    //  How long a take can be, and how many channels it keeps. Sixty seconds
    //  stereo at 48 kHz is 23 MB, which a phone that is already holding
    //  sixteen samples will not notice, and it is the difference between
    //  sampling a phrase and sampling a hit.
    //  Sixty seconds of stereo float at 48 kHz is 23 MB, which a phone that
    //  is already holding sixteen samples will not notice - unless it is an
    //  entry-level phone, which is why the device decides (see DeviceTier).
    void setRecordLimit (double seconds, bool allowStereo) noexcept
    {
        recordSeconds = juce::jlimit (5.0, 300.0, seconds);
        recordAllowStereo = allowStereo;
    }
    float getRecordLimitSeconds() const noexcept { return (float) recordSeconds; }
    int   getRecordChannels() const noexcept { return juce::jmax (1, recordBuffer.getNumChannels()); }

    void              startRecording (int slot) noexcept;
    SampleBuffer::Ptr finishRecording() noexcept;   // stop + build + publish; returns the buffer
    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }
    float getRecordSeconds() const noexcept;

private:
    void handleCommand (const Command& c) noexcept;               // audio thread
    void triggerPad (int slot, int extraSemis = 0, float vel = 1.0f,
                     float from01 = -1.0f) noexcept;   // audio thread

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

    // Two voices per pad, round-robin: a retrigger steals the previous
    // instance with a fast declick fade instead of hard-resetting it (the
    // single-voice reset produced a waveform discontinuity = audible click).
    //  One shared pool, not two voices bolted to each pad.
    //
    //  Two per pad meant a three second break cut itself off on the third hit
    //  while fifteen silent pads sat on thirty voices nobody was using. A pool
    //  spends the polyphony where it is actually being played, which is what
    //  every sampler does and what anyone reaching for a stand demo will try
    //  within about ten seconds.
    //
    //  The per-pad cap stays, just far higher: without one a single held pad
    //  could take the whole pool and starve the other fifteen.
    //  The array is the ceiling; how much of it is USED is the device's
    //  answer, set once at startup (see DeviceTier). Sized rather than
    //  allocated, so nothing about a tier decision can ever reach the audio
    //  thread as a reallocation.
    static constexpr int kNumVoices     = 64;
    std::array<Voice, kNumVoices>       voices {};
    int voiceLimit      = 48;   // <= kNumVoices, message thread sets, audio reads
    int maxVoicesOnPad  = 8;

    //  Which voices belong to which pad, rebuilt once per render segment.
    //  Without it the block cost was pads x voices - sixteen scans of the
    //  whole pool to answer "does this pad have anything sounding", and
    //  sixteen more to render, so a thousand iterations of pure bookkeeping
    //  whether one voice was playing or sixty-four. One pass over the pool
    //  builds a per-pad chain and every later loop touches only its own.
    //  Audio thread only.
    std::array<int, kNumPads>    padFirstVoice {};
    std::array<int, kNumVoices>  voiceNextInPad {};
    std::uint32_t                       voiceSerial = 0;   // audio-thread only, for oldest-steal
    CommandFifo commands;

    //  THE LIFEBOAT. See postNoteOn / renderNextBlock.
    //
    //  One bit per pad, OR'd by the message thread, exchanged to zero by the
    //  audio thread. It carries no velocity and no start point, so it is a
    //  worse trigger than the queue in every way except the only one that
    //  matters here: a single atomic word cannot be left in a broken state,
    //  so it cannot stop working. It is what the test tone has always used,
    //  and the test tone is the thing that kept sounding when the pads did
    //  not.
    std::atomic<std::uint32_t> fallbackTriggers { 0 };

    //  How many triggers the queue has refused. Nothing in the audio path
    //  reads it; the UI does, because a queue that starts refusing is the
    //  app telling us it is wedged and wants the device rebuilt.
    std::atomic<int> droppedCommands { 0 };

    //  Two audio callbacks must never be inside the drain at once. During a
    //  route change the old stream's thread can still be in here when the new
    //  one arrives, and AbstractFifo is single-consumer BY CONTRACT: two
    //  readers move validStart and validEnd past each other and the queue is
    //  wedged for the rest of the process - every push refused, every drain
    //  empty. That is not a glitch that passes, it is permanent, and it looks
    //  exactly like "the pads stopped working but the test tone still beeps".
    std::atomic<bool> inRender { false };

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
    std::array<std::atomic<bool>,  kNumPads> padKeepLength {};   // true = pitch only, false = tape
    std::array<std::atomic<int>,   kNumPads> padChoke {};   // 0 = none, 1..8 = choke group
    std::array<std::atomic<float>, kNumPads> padPan {};      // -1 (L) .. 0 (centre) .. 1 (R)
    std::array<std::atomic<float>, kNumPads> padAttack {};   // ms
    std::array<std::atomic<float>, kNumPads> padRelease {};  // ms
    std::array<std::atomic<bool>,  kNumPads> padMute {};
    std::array<std::atomic<bool>,  kNumPads> padSolo {};
    std::atomic<bool> soloActive { false };   // cached: is anything soloed
    void refreshSolo() noexcept
    {
        bool any = false;
        for (auto& s : padSolo) any = any || s.load (std::memory_order_relaxed);
        soloActive.store (any, std::memory_order_relaxed);
    }

    // Sequencer.
    std::atomic<bool>   playing { false };
    std::atomic<float>  bpm { 120.0f };   // float: lock-free on 32-bit ARM too
    std::array<std::array<std::atomic<std::uint16_t>, kNumSteps>, kNumPatterns> patternBank {};
    std::atomic<int>    playStep { -1 };
    std::atomic<float>  stepPhase { 0.0f };   // 0..1 within the current step
    std::atomic<std::uint32_t> triggeredMask { 0 };   // pads triggered, read by UI
    std::array<std::atomic<float>, (size_t) kNumPads> padPos {};   // read head, 0..1, -1 = silent
    std::array<std::atomic<bool>,  (size_t) kNumPads> padSelfCut {};   // retrigger cuts its own tail
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

    // Song / playlist.
    std::atomic<bool> songMode { false };
    std::array<std::array<std::atomic<int>, kSongBars>, kSongLanes> songCell {};
    std::atomic<int> songBars { 8 };      // how many bars the song is long
    std::atomic<int> songBar  { -1 };     // live playhead bar, for the UI
    // Audio-thread only: what each lane is currently running.
    int  lanePattern[kSongLanes] { -1, -1, -1, -1 };
    int  laneStartStep[kSongLanes] { 0, 0, 0, 0 };
    int  songStep = 0;                    // absolute step within the song

    // Latency probe. The click is emitted a moment AFTER the stream starts,
    // so the measurement is of a settled stream rather than of its first
    // fumbling blocks.
    std::atomic<bool> probeArm { false };
    std::atomic<bool> probing  { false };
    int probeCounter = 0;            // audio-thread only
    int probeClickAt = 0;
    int probeLength  = 0;

    // Recording.
    std::atomic<bool> recording { false };
    std::atomic<int>  recordPos { 0 };
    juce::AudioBuffer<float> recordBuffer;   // allocated in prepareToPlay, never in the callback
    int    recordChannels = 1;               // how many of the input channels we keep
    double recordSeconds  = 60.0;
    bool   recordAllowStereo = true;
    int recordSlot = 0;

    // Master FX: filter + drive.
    juce::dsp::StateVariableTPTFilter<float> masterFilter;
    std::atomic<int>   fxType   { 0 };          // 0 LPF, 1 HPF
    std::atomic<float> fxCutoff { 20000.0f };
    std::atomic<float> fxReso   { 0.707f };
    std::atomic<float> fxDrive  { 0.0f };        // 0..1

    // Audio-thread-only smoothed FX params (one-pole toward the atomics):
    // knob moves arrive as per-block jumps otherwise — zipper on the filter,
    // crackle on the delay time. ~20 ms time constant.
    float smCutoff  = 20000.0f;
    float smReso    = 0.707f;
    float smDrive   = 0.0f;
    float smDlyMix  = 0.0f;
    float smDlyFb   = 0.35f;
    float smDlySamp = 0.0f;      // delay time in samples, smoothed per sample
    bool  filterWasActive = false;
    bool  hpWasActive     = false;

    // Master delay.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> delayLine { 96000 };
    std::atomic<float> dlyTime { 250.0f };       // ms
    std::atomic<float> dlyFb   { 0.35f };        // 0..0.95
    std::atomic<float> dlyMix  { 0.0f };         // 0..1

    // ISO wet/dry, so the low-pass can be blended rather than only replacing.
    std::atomic<float> fxMix { 0.0f };
    float smFxMix = 0.0f;

    // HPF: its OWN filter, not the ISO one switched to high-pass. Two objects
    // cost a few hundred bytes and buy a band-pass you can sweep from both
    // ends — one shared filter would have made them mutually exclusive.
    juce::dsp::StateVariableTPTFilter<float> hpFilter;
    std::atomic<float> hpFreq { 200.0f };
    std::atomic<float> hpReso { 0.707f };
    std::atomic<float> hpMix  { 0.0f };
    float smHpFreq = 200.0f, smHpReso = 0.707f, smHpMix = 0.0f;

    // Drive tone: a one-pole low-pass after the tanh, because saturation
    // without somewhere for the harmonics to go is just harsh.
    std::atomic<float> drvTone { 20000.0f };
    std::atomic<float> drvMix  { 0.0f };
    float smDrvTone = 20000.0f, smDrvMix = 0.0f;
    float drvLp[2] { 0.0f, 0.0f };

    // Crush: bit depth and sample-and-hold rate, the two halves of lo-fi.
    std::atomic<float> crBits { 8.0f };
    std::atomic<float> crRate { 4.0f };
    std::atomic<float> crMix  { 0.0f };
    float smCrMix = 0.0f;
    float crHold[2] { 0.0f, 0.0f };
    float crPhase = 0.0f;

    // Reverb, last in the chain so everything ahead of it lands in the room.
    juce::dsp::Reverb reverb;
    std::atomic<float> rvSize { 0.55f };
    std::atomic<float> rvDamp { 0.45f };
    std::atomic<float> rvMix  { 0.0f };
    float smRvMix = 0.0f;

    // Dry copy for the wet/dry stages. Sized in prepareToPlay, never here.
    juce::AudioBuffer<float> fxDry;

    // ------------------------------------------------------------------
    //  Sends. Each effect is a bus with its own input, and every pad decides
    //  how much of itself goes into each one. That is what makes an effect
    //  belong to a channel rather than to the whole instrument, and it is
    //  also what makes cutting one behave the way it does on hardware: the
    //  SEND closes, the RETURN stays open, so whatever was already inside a
    //  delay or a reverb rings out instead of being amputated.
    //
    //  The four tone effects also take the pad OFF the dry path by the same
    //  amount they take it on to theirs — a filter you can hear around is
    //  not a filter. Delay and reverb add on top, as sends do.
    // ------------------------------------------------------------------
    static constexpr bool fxIsTone[kNumFx] = { true, true, true, false, true, false };

    std::array<std::array<std::atomic<float>, kNumFx>, kNumPads> padSend {};
    std::array<std::array<float, kNumFx>, kNumPads> smSend {};    // audio thread only
    std::array<juce::AudioBuffer<float>, kNumFx> fxBus;
    std::array<bool, kNumFx> busRinging {};
    juce::AudioBuffer<float> padScratch;

    // Scope ring (post-FX mono), written by the audio thread.
    static constexpr int kScopeSize = 2048;   // power of two
    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    //  The decimated silhouette ring: one min/max column per scopeColLen
    //  frames. Audio thread writes, message thread copies.
    static constexpr int kScopeCols = kMaxScopeColumns;   // power of two
    std::array<float, kScopeCols> scopeColMin {};
    std::array<float, kScopeCols> scopeColMax {};
    std::atomic<int> scopeColWrite { 0 };
    int   scopeColLen = 740;              // set from the rate in prepareToPlay
    float colMin =  1.0e9f, colMax = -1.0e9f;   // audio-thread accumulators
    int   colCount = 0;

    // Running output peak per channel; UI consumes-and-resets via readOutPeak*.
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };

    // Diagnostic test tone.
    std::atomic<int> testToneRemaining { 0 };
    double           testPhase = 0.0;

    double systemSampleRate = 44100.0;
    int    maxBlock         = 512;
    bool   offlineMode      = false;   // a bounce clone: no record buffer, no meters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

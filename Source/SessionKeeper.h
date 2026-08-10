#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

// ============================================================================
//  SessionKeeper — the copy of your work that survives the process dying.
//
//  Android does not ask before reclaiming an app. It pauses it, and some time
//  later the process is simply gone; the next launch is a cold start with an
//  empty machine. Until now ZATI answered that with autosave(), which rewrote
//  the open project's project.xml — and did nothing at all when no project was
//  open, which is exactly the state a sampler spends its first hour in. Chop a
//  break, record four pads off the mic, get a call, come back: nothing.
//
//  So there is a second, invisible project that nobody has to remember to
//  save. It lives outside Projects/ so it never appears in the browser, and it
//  has the same shape as a real one:
//
//      ZATI/.sesion/
//          state.xml          the whole machine, plus the name of the project
//                             that was open (so the header comes back too)
//          samples/pad01.wav  a copy of every loaded pad
//
//  Writing sixteen WAVs is not something onPause has time for — Android gives
//  an app a few seconds there before it calls it a hang. So the audio is
//  written *during* the session instead, by this thread, a couple of seconds
//  after a pad changes. By the time the activity pauses there is normally
//  nothing left to do and the only write is the small XML.
//
//  Nothing here hooks into the places that change a pad. sync() is handed the
//  live array and compares it against what it last wrote, pointer by pointer,
//  which catches every path — load, chop, mic, undo, project open — including
//  the ones written after this file.
// ============================================================================
class SessionKeeper : private juce::Thread
{
public:
    SessionKeeper();
    ~SessionKeeper() override;

    static juce::File folder();
    static juce::File stateFile();
    static juce::File padFile (int pad);

    //  Is there something to come back to?
    static bool exists() { return stateFile().existsAsFile(); }

    //  Message thread. Diffs the live pads against the last written set and
    //  queues whatever moved. Cheap enough to call from the UI timer: with
    //  nothing changed it is sixteen pointer comparisons.
    void sync (const SampleBuffer::Ptr* live, int numPads);

    //  Take the live pads as already-written, without queueing anything. Used
    //  right after a restore: those buffers came off this very folder.
    void adopt (const SampleBuffer::Ptr* live, int numPads);

    //  Message thread, synchronous, small.
    void writeState (const juce::ValueTree& state, const juce::String& projectName);

    //  Wait for the queue to drain. Returns false on timeout, and the caller
    //  carries on either way — a bounded wait is the point.
    bool flush (int timeoutMs);

    //  NUEVO: there is no session any more, and the next launch starts clean.
    void clear();

private:
    void run() override;
    bool isIdle() const;

    juce::CriticalSection lock;

    //  Every array below is guarded by `lock`.
    //
    //  `seen` holds a reference on purpose. Comparing bare addresses would be
    //  wrong without it: free a buffer, load another, and the allocator can
    //  hand back the same address — a changed pad that compares equal. Holding
    //  the reference makes that impossible, and costs nothing, because the UI
    //  is holding the same objects anyway.
    //  Must be at least AudioEngine::kNumPads. It was 32 while the machine had
    //  16, and going to 64 banks would have silently stopped protecting half
    //  of them - sync() and adopt() both clamp with jmin.
    static constexpr int kMaxPads = 64;
    std::array<SampleBuffer::Ptr, kMaxPads> seen, queued;
    std::array<bool, kMaxPads> dirty {};
    bool writing = false;

    //  Set by flush(), read by the writer. Not guarded by `lock`: it is a
    //  hint, and taking the lock to read a hint from the thread that holds it
    //  most of the time is how a flush ends up waiting on itself.
    std::atomic<bool> hurry { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionKeeper)
};

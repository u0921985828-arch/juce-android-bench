#pragma once

#include <JuceHeader.h>
#include <array>
#include <cstdint>

// ============================================================================
//  CommandFifo — lock-free trigger transport, message thread -> audio thread.
//
//  Strictly SINGLE-producer / SINGLE-consumer (juce::AbstractFifo):
//    * Producer: the MESSAGE thread only (UI pad clicks). The background
//      sample-loader must NOT push here — it publishes sample data via the
//      engine's atomic pointer slot instead.
//    * Consumer: the AUDIO thread (drain in renderNextBlock).
//
//  startFrame is the sample offset within the block the command is consumed
//  in. For P0 it is always 0 (fire at block start — human tap jitter dwarfs
//  one block). The field/plumbing is carried so P1's sequencer gets
//  sample-accurate scheduling without a rewrite.
// ============================================================================
struct Command
{
    enum class Type : uint8_t { NoteOn, NoteOff, Panic };

    Type  type       = Type::NoteOn;
    int   slot       = 0;
    float semitones  = 0.0f;
    //  Default full, not 0.8: anything that does not measure a strike - the
    //  sequencer, a chain one-shot, a test tone - means "as loud as this pad
    //  is set to", and scaling those by 0.8 would quietly cost every one of
    //  them 2 dB against a hard tap.
    float velocity   = 1.0f;
    int   startFrame = 0;

    //  Where in the SOURCE to start, 0..1, or negative for "wherever the pad's
    //  trim says". Only the preview uses it: tapping the waveform has to play
    //  from the point that was tapped without moving the pad's own start.
    float from01     = -1.0f;
};

class CommandFifo
{
public:
    // --- Producer (message thread) ---
    bool push (const Command& c) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (1, start1, size1, start2, size2);

        if (size1 + size2 < 1)
            return false;   // full — caller drops (may log on the message thread)

        if (size1 > 0) storage[(size_t) start1] = c;
        else           storage[(size_t) start2] = c;

        fifo.finishedWrite (size1 + size2);
        return true;
    }

    // --- Consumer (audio thread) ---
    template <typename Fn>
    void drain (Fn&& fn) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToRead (fifo.getNumReady(), start1, size1, start2, size2);

        for (int i = 0; i < size1; ++i) fn (storage[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i) fn (storage[(size_t) (start2 + i)]);

        fifo.finishedRead (size1 + size2);
    }

private:
    static constexpr int kCapacity = 256;
    juce::AbstractFifo          fifo { kCapacity };
    std::array<Command, kCapacity> storage {};
};

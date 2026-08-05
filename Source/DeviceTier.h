#pragma once

#include <JuceHeader.h>

// ============================================================================
//  DeviceTier — one honest answer to "what can this phone actually do", asked
//  once at startup, and a handful of numbers derived from it.
//
//  The app used to be built for one machine: forty-eight voices, a sixty
//  millisecond UI tick, a minute of stereo record buffer, a quarter of a
//  gigabyte of sample budget. On the phone it was written on that is right.
//  On a four-core entry-level device with 3 GB it is a stutter, and on a
//  flagship it is leaving half the instrument on the table.
//
//  So the constants become a profile. Nothing here is a setting the user has
//  to find: the device is classified from what the system reports about
//  itself, and every number that costs CPU or memory is read from the result.
//
//  What decides the tier, in order of how much it actually predicts:
//
//    * CORES. The audio thread needs one to itself; the UI thread needs
//      another; the sample loader and the session writer want a third. Four
//      cores is the floor at which those stop fighting.
//    * RAM. Sample buffers are the app's whole memory story - a minute of
//      stereo float is 23 MB and a chopped break is held once and pointed at
//      sixteen times. On 3 GB the budget has to be small enough that the
//      system never has a reason to kill us.
//    * CLOCK. Only as a tie-breaker: it is reported unreliably on Android and
//      says nothing about the big/little split.
//
//  Deliberately NOT used: the model name. A lookup table of phones is a lie
//  that ages badly and needs a release to fix.
// ============================================================================
namespace DeviceTier
{
    enum class Tier { low = 0, mid, high, ultra };

    struct Profile
    {
        Tier tier = Tier::mid;

        //  The voice pool. Every one of these is a Hermite interpolation and,
        //  in TONO mode, two overlapping grains - the single biggest thing the
        //  audio thread does.
        int voices = 32;

        //  How many voices one pad may hold before it starts stealing from
        //  itself. Scales with the pool so a held pad never starves the rest.
        int voicesPerPad = 8;

        //  The UI timer. It drives the meters, the scope, the pad flashes and
        //  the playhead - all of it repainting - and it is the second biggest
        //  cost in the app after the voices.
        int uiIntervalMs = 60;

        //  How many samples of the master go into the scope. Fewer points is
        //  fewer columns to walk and to fill.
        int scopePoints = 1024;

        //  The mic take: seconds, and whether stereo is even attempted.
        double recordSeconds = 60.0;
        bool   recordStereo = true;

        //  Ceiling for one decoded sample, in megabytes. A phone that cannot
        //  hold it is better off refusing the file with a message than being
        //  killed by the system halfway through decoding it.
        int sampleBudgetMB = 192;

        //  Multiplier on the driver's smallest burst. One is the fast path; a
        //  device that cannot render a block in time under-runs, and an
        //  under-run is a click, which is worse than the extra milliseconds.
        int bufferBursts = 1;

        //  Whether pad tiles draw their waveform art. Sixteen envelopes
        //  redrawn on every flash is real work on a slow GPU-less path.
        bool padWaveformArt = true;
    };

    //  Classify once. Subsequent calls return the same answer.
    const Profile& profile();

    //  For the SET panel: "4 nucleos · 3.7 GB · media".
    juce::String describe();
    juce::String tierName (Tier t);
}

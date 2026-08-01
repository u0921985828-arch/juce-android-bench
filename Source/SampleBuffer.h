#pragma once

#include <JuceHeader.h>

// ============================================================================
//  SampleBuffer — the ref-counted PCM payload handed across threads.
//
//  Threading contract:
//    * Created & filled on a BACKGROUND thread (SampleLoader).
//    * Published to the AUDIO thread via an atomic raw-pointer swap
//      (see AudioEngine). The audio thread only ever *reads* buffer data and
//      performs atomic ref-count *increments* — never a decrement, so it can
//      never trigger a delete.
//    * Deleted on the MESSAGE thread (AudioEngine::collectRetiredSamples()).
//
//  sourceSampleRate is F_src — the rate the file was recorded at. The Voice
//  uses it against the device rate F_sys to compute the playback increment.
// ============================================================================
class SampleBuffer : public juce::ReferenceCountedObject
{
public:
    using Ptr = juce::ReferenceCountedObjectPtr<SampleBuffer>;

    juce::AudioBuffer<float> buffer;        // decoded PCM
    double                   sourceSampleRate = 44100.0;   // F_src
};

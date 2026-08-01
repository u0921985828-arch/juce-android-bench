#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

class AudioEngine;

// ============================================================================
//  SampleLoader — decodes an audio file OFF the message/audio threads and
//  publishes the result to the AudioEngine.
//
//  Uses JUCE's ThreadPool so the message thread never blocks on file I/O and
//  the audio thread never touches a decoder. On success it hands a
//  ref-counted SampleBuffer to engine.publishSample(); on completion it
//  notifies (on the message thread) so the UI can re-enable its Load button.
// ============================================================================
class SampleLoader
{
public:
    explicit SampleLoader (AudioEngine& engineToLoadInto);

    // Decode `file` in the background. `onFinished(success)` is invoked on the
    // MESSAGE thread once the job completes (whether or not it succeeded).
    void loadAsync (const juce::File& file, std::function<void (bool)> onFinished);

private:
    AudioEngine&               engine;
    juce::AudioFormatManager   formatManager;
    juce::ThreadPool           pool { 1 };   // one background decode worker

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
};

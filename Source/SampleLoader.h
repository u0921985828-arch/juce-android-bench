#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

class AudioEngine;

// ============================================================================
//  SampleLoader — decodes an audio file OFF the message/audio threads and
//  publishes the result into a specific pad slot of the AudioEngine.
// ============================================================================
class SampleLoader
{
public:
    explicit SampleLoader (AudioEngine& engineToLoadInto);

    // Decode `url` in the background into pad `slot`. On Android the file picker
    // returns a content:// URL opened via AndroidDocument. `onFinished(success,
    // detail)` is invoked on the MESSAGE thread; `detail` describes the result
    // or the exact failure point (for on-screen diagnostics).
    void loadAsync (const juce::URL& url, int slot,
                    std::function<void (bool, juce::String)> onFinished);

private:
    AudioEngine&             engine;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool         pool { 1 };   // one background decode worker

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
};

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
    // detail, buffer)` is invoked on the MESSAGE thread; `detail` describes the
    // result or the exact failure point; `buffer` is the loaded sample (for the
    // UI waveform) or nullptr on failure.
    void loadAsync (const juce::URL& url, int slot,
                    std::function<void (bool, juce::String, SampleBuffer::Ptr)> onFinished);

    int formatosMp3() const   // para Tests/Carga.cpp
    {
        int n = 0;
        for (auto* f : formatManager) if (f->getFileExtensions().contains (".mp3")) ++n;
        return n;
    }

private:
    AudioEngine&             engine;
    juce::AudioFormatManager formatManager;
    juce::ThreadPool         pool { 1 };   // one background decode worker

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLoader)
};

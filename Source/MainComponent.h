#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"

// ============================================================================
//  MainComponent — the app's audio + UI surface.
//
//  * Subclasses AudioAppComponent: owns the audio device, forwards the audio
//    callbacks straight into AudioEngine.
//  * UI: two pad buttons + a "Load sample" button. Pad clicks post NoteOn
//    commands to the engine's lock-free FIFO (no audio work on the message
//    thread). Load opens an async FileChooser and kicks a background decode.
//  * A Timer drives message-thread garbage collection of retired sample
//    buffers (the only place SampleBuffers are deleted).
// ============================================================================
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // AudioAppComponent
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    // Component
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void openFileChooser();

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::TextButton padA   { "PAD A" };        // slot 0, pitch 0
    juce::TextButton padB   { "PAD B (+5)" };   // slot 1, +5 semitones (proves pitch math)
    juce::TextButton loadBtn { "Load sample" };
    juce::Label      status;

    std::unique_ptr<juce::FileChooser> chooser;
    bool sampleLoaded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

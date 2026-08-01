#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"

// ============================================================================
//  MainComponent — audio + UI surface (P1 step 1: 16-pad matrix).
//
//  * AudioAppComponent: owns the device, forwards callbacks to AudioEngine.
//  * 4x4 pad grid. Two modes toggled by the LOAD button:
//      - LOAD off  : tapping a pad triggers its sample.
//      - LOAD on   : tapping a pad opens the file picker to assign a sample.
//  * Loaded pads are lit (accent colour); empty pads are dim.
//  * Android-safe loading: the picker returns a content:// URL, read via stream.
//  * A Timer drives message-thread GC of retired sample buffers.
//
//  Original visual identity (no SP-404 skin) — high-contrast dark + one accent.
// ============================================================================
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void padClicked (int index);
    void openChooserForPad (int index);
    void refreshPad (int index);

    static constexpr int kNumPads = AudioEngine::kNumPads;   // 16

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<juce::TextButton> pads;
    juce::TextButton  loadButton { "REASSIGN" };
    juce::TextButton  testButton { "TEST TONE" };
    juce::Label       status;

    std::array<bool, kNumPads> padHasSample {};   // all false
    bool loadMode = false;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"

// ============================================================================
//  MainComponent — Shard UI (P1): 16-pad matrix, per-pad controls (pitch, vol,
//  trim, reverse, loop), waveform, a 16-step sequencer with BPM, and mic
//  recording. Original high-contrast look (no SP-404 skin).
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
    void stepClicked (int step);
    void openChooserForPad (int index);
    void refreshPad (int index);
    void selectPad (int index);
    void updateControlsFromPad (int index);
    void assignSampleToPad (int index, SampleBuffer::Ptr sb);
    void toggleRecording();
    int  firstEmptyPad() const;

    static constexpr int kNumPads  = AudioEngine::kNumPads;    // 16
    static constexpr int kNumSteps = AudioEngine::kNumSteps;   // 16

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<juce::TextButton> pads;
    juce::OwnedArray<juce::TextButton> stepButtons;

    juce::TextButton loadButton { "REASSIGN" };
    juce::TextButton testButton { "TEST" };
    juce::TextButton recButton  { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton clearButton { "CLR" };
    juce::TextButton reverseButton { "REV" };
    juce::TextButton loopButton { "LOOP" };

    juce::Slider pitchSlider, volSlider, startSlider, endSlider, bpmSlider, chokeSlider;
    juce::TextButton fxTypeButton { "LPF" };
    juce::Slider cutoffSlider, resoSlider, driveSlider;
    juce::Slider dlyTimeSlider, dlyFbSlider, dlyMixSlider;
    juce::Label  status, editLabel, fxLabel;
    WaveformDisplay waveform;

    // Per-pad UI state.
    std::array<bool,  kNumPads> padHasSample {};
    std::array<float, kNumPads> padPitch {};
    std::array<float, kNumPads> padGain {};
    std::array<float, kNumPads> padStart01 {};
    std::array<float, kNumPads> padEnd01 {};
    std::array<bool,  kNumPads> padLoop {};
    std::array<bool,  kNumPads> padReverse {};
    std::array<int,   kNumPads> padChokeUI {};   // 0 = none
    std::array<SampleBuffer::Ptr, kNumPads> uiSample;

    // Pattern mirror [step][pad] for the sequencer UI.
    std::array<std::array<bool, kNumPads>, kNumSteps> pattern {};

    int  selectedPad   = -1;
    bool loadMode      = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

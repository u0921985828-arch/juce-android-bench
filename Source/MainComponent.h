#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "SpectrumDisplay.h"
#include "ShardLookAndFeel.h"
#include "PadButton.h"

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
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    enum class Mode { Perform, Edit, Seq, Fx };
    void setMode (Mode m);

    void padClicked (int index);
    void stepClicked (int step);
    void openChooserForPad (int index);
    void refreshPad (int index);
    void selectPad (int index);
    void updateControlsFromPad (int index);
    void assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name = {});
    void toggleRecording();
    void autoChopSelected();
    int  firstEmptyPad() const;
    void layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap);

    static constexpr int kNumPads  = AudioEngine::kNumPads;    // 16
    static constexpr int kNumSteps = AudioEngine::kNumSteps;   // 16

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<PadButton> pads;
    juce::OwnedArray<juce::TextButton> stepButtons;
    juce::OwnedArray<juce::TextButton> tabButtons;   // TOCAR / EDITAR / SEC / FX

    juce::TextButton loadButton { "LOAD" };
    juce::TextButton testButton { "TEST" };
    juce::TextButton recButton  { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton clearButton { "CLR" };
    juce::TextButton reverseButton { "REV" };
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton chopButton { "AUTO CHOP" };

    juce::Slider pitchSlider, volSlider, startSlider, endSlider, bpmSlider, chokeSlider;
    juce::Slider panSlider, attackSlider, releaseSlider;
    juce::Slider patternSlider, noteSlider, lengthSlider;
    juce::TextButton chainAddButton { "+CHAIN" }, chainClearButton { "CLR CHAIN" };
    juce::TextButton fxTypeButton { "LPF" };
    juce::Slider cutoffSlider, resoSlider, driveSlider;
    juce::Slider dlyTimeSlider, dlyFbSlider, dlyMixSlider;
    juce::Slider macroFilter, macroDrive, macroSend;   // quick FX on the perform screen
    juce::Label  status, editLabel, fxLabel;
    WaveformDisplay waveform;
    SpectrumDisplay spectrum;
    ShardLookAndFeel lnf;
    float scopeTmp[1024] {};

    // Per-pad UI state.
    std::array<bool,  kNumPads> padHasSample {};
    std::array<float, kNumPads> padPitch {};
    std::array<float, kNumPads> padGain {};
    std::array<float, kNumPads> padStart01 {};
    std::array<float, kNumPads> padEnd01 {};
    std::array<bool,  kNumPads> padLoop {};
    std::array<bool,  kNumPads> padReverse {};
    std::array<int,   kNumPads> padChokeUI {};   // 0 = none
    std::array<float, kNumPads> padPan {};        // -1..1, 0 = centre
    std::array<float, kNumPads> padAttack {};     // ms
    std::array<float, kNumPads> padRelease {};    // ms
    std::array<SampleBuffer::Ptr, kNumPads> uiSample;
    std::array<juce::String, kNumPads> padName {};

    // Pattern mirror [bank][step][pad] for the sequencer UI.
    static constexpr int kNumPatterns = AudioEngine::kNumPatterns;   // 8
    std::array<std::array<std::array<bool, kNumPads>, kNumSteps>, kNumPatterns> pattern {};
    int selectedPattern = 0;
    int selectedStep    = -1;

    std::array<float, kNumPads> padFlash {};   // 1.0 on trigger, decays -> lit feedback
    // Chassis layout regions (set in resized(), drawn in paint()).
    juce::Rectangle<int> headerArea, screenBezel, fxPanelArea, seqPanelArea,
                         editPanelArea, tabBarArea, editCtrlArea;

    Mode mode { Mode::Perform };
    int  selectedPad   = -1;
    bool loadArmed     = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

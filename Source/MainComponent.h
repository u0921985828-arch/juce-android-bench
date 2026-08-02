#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "SpectrumDisplay.h"
#include "ShardLookAndFeel.h"
#include "PadButton.h"

// ============================================================================
//  MainComponent — COLORS UI (P1): 16-pad matrix, per-pad controls (pitch, vol,
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

    // One perform screen; every deep feature (pad settings, sequencer,
    // pattern chain, auto chop, FX) opens as a pop-up sheet over it — a dim
    // scrim + a bottom card, closed by tapping outside or the x button.
    // Controls are children of their sheet, not of MainComponent, so an
    // open sheet naturally blocks clicks to the machine face behind it.
    class Sheet : public juce::Component
    {
    public:
        std::function<void()> onDismiss;
        std::function<void (juce::Graphics&)> paintContent;   // titles, readouts, rings
        juce::Rectangle<int> sheetBounds;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! sheetBounds.contains (e.getPosition()) && onDismiss)
                onDismiss();
        }
    };
    Sheet padSheet, seqSheet, chainSheet, chopSheet, fxSheet;
    void openSheet (Sheet& s, juce::TextButton& toggle);
    void closeAllSheets();
    void paintSeqSheetContent (juce::Graphics& g);
    void paintPadSheetContent (juce::Graphics& g);
    void paintChainSheetContent (juce::Graphics& g);
    void paintChopSheetContent (juce::Graphics& g);
    void paintFxSheetContent (juce::Graphics& g);

    void padClicked (int index);
    void stepClicked (int step);
    void openChooserForPad (int index);
    void refreshPad (int index);
    void selectPad (int index);
    void updateControlsFromPad (int index);
    void assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name = {});
    void toggleRecording();
    void autoChopSelected();
    void rebuildChain();
    int  firstEmptyPad() const;
    void layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap);

    static constexpr int kNumPads   = AudioEngine::kNumPads;     // 16
    static constexpr int kNumSteps  = AudioEngine::kNumSteps;    // 64 (max pattern length)
    static constexpr int kMinPatLen = AudioEngine::kMinPatLen;   // 16
    static constexpr int kMaxPatLen = AudioEngine::kMaxPatLen;   // 64
    static constexpr int kStepCols  = 8;                         // step grid is always 8 columns wide

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<PadButton> pads;
    juce::OwnedArray<juce::TextButton> stepButtons;

    // Module bar: each button opens its floating sheet (never a mode switch).
    juce::TextButton padsButton  { "PADS" };
    juce::TextButton secButton   { "SEC" };
    juce::TextButton chainButton { "CHAIN" };
    juce::TextButton chopOpenButton { "CHOP" };
    juce::TextButton fxOpenButton   { "FX" };
    juce::TextButton seqCloseButton   { juce::CharPointer_UTF8 ("\xc3\x97") },
                     padCloseButton   { juce::CharPointer_UTF8 ("\xc3\x97") },
                     chainCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") },
                     chopCloseButton  { juce::CharPointer_UTF8 ("\xc3\x97") },
                     fxCloseButton    { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::OwnedArray<juce::TextButton> patternButtons;  // P1..P8 — chain include toggles

    // Context-sensitive macro strip: the 3 physical CTRL knobs switch banks.
    juce::OwnedArray<juce::TextButton> macroBankBtns;   // FILTRO / DELAY / PAD
    int macroBank = 0;
    void setMacroBank (int bank);
    void refreshMacroValues();
    void macroMoved (int idx);

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
    juce::TextButton chainClearButton { "CLR CHAIN" };
    juce::TextButton fxTypeButton { "LPF" };
    juce::Slider cutoffSlider, resoSlider, driveSlider;
    juce::Slider dlyTimeSlider, dlyFbSlider, dlyMixSlider;
    juce::Slider macroCtrl1, macroCtrl2, macroCtrl3;   // CTRL 1-3, bank-dependent
    juce::Label  status, fxLabel;
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
    std::array<bool, kNumPatterns> patternActiveUI {};   // which banks are in the chain

    std::array<float, kNumPads> padFlash {};   // 1.0 on trigger, decays -> lit feedback
    // Chassis layout regions (set in resized(), drawn in paint()).
    juce::Rectangle<int> headerArea, screenBezel, tabBarArea, fxCurveArea,
                         editInfoArea, vuArea, stepStripArea;
    float vuL = 0.0f, vuR = 0.0f;   // smoothed output peaks for the VU strip

    int  selectedPad   = -1;
    bool loadArmed     = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;

    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

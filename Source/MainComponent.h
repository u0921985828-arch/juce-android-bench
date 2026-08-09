#pragma once

#include <JuceHeader.h>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "SpectrumDisplay.h"
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include "ProjectStore.h"
#include "StepGrid.h"
#include "Playlist.h"
#include "AudioFocus.h"
#include "SessionKeeper.h"
#include "Exporter.h"
#include "AudioPath.h"

// ============================================================================
//  MainComponent — ZATI: a 16-pad matrix whose fragments carry the colour, an
//  achromatic chassis, a hero waveform that maps the whole cut, a variable
//  sequencer with pattern banks and chaining, per-pad settings, master FX and
//  self-contained projects.
// ============================================================================
class MainComponent : public juce::AudioAppComponent,
                      private juce::Timer,
                      private juce::FileBrowserListener,
                      private AudioFocus::Listener
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
    void watchAudioDevice();

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
        //  A click INSIDE the card. Painted controls - things with no
        //  component of their own, like the zati swatches - hang off this.
        std::function<void (juce::Point<int>)> onContentClick;
        juce::Rectangle<int> sheetBounds;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! sheetBounds.contains (e.getPosition()))
            {
                if (onDismiss) onDismiss();
            }
            else if (onContentClick)
            {
                onContentClick (e.getPosition());
            }
        }
    };
    //  ONE card, TWO pages. AJUSTES was doing two unrelated jobs in one long
    //  scroll - what the audio device is doing, and what your projects are
    //  called - and splitting them into two separate popups only turned the
    //  scroll into a journey. They are pages of the same card now: AUDIO is
    //  the machine, PROYECTOS is your work, and the tab row swaps between them
    //  without the card going anywhere.
    Sheet padSheet, seqSheet, browseSheet, setSheet, mixSheet, songSheet,
          exportSheet, rackSheet, chopSheet;
    enum SetPage { pageAudio = 0, pageProjects };
    int setPage = pageAudio;
    juce::TextButton pageAudioBtn { "AUDIO" }, pageProjBtn { "PROYECTOS" };
    void showSetPage (int page);
    void openSheet (Sheet& s, juce::TextButton& toggle);
    void closeAllSheets();
    void paintAudioSheetContent (juce::Graphics& g);
    void paintSeqSheetContent (juce::Graphics& g);
    void paintPadSheetContent (juce::Graphics& g);
    void paintBrowseSheetContent (juce::Graphics& g);
    void paintProjSheetContent (juce::Graphics& g);
    void paintMixSheetContent (juce::Graphics& g);
    void paintSongSheetContent (juce::Graphics& g);
    void paintExportSheetContent (juce::Graphics& g);
    void paintRackSheetContent (juce::Graphics& g);
    void paintChopSheetContent (juce::Graphics& g);

    // --- Projects ---------------------------------------------------------
    //  The whole machine (pads + their samples, the 8 pattern banks, the
    //  chain, BPM, FX and skin) serialises to one folder per project.
    juce::ValueTree captureState() const;
    void applyState (const juce::ValueTree& state);
    void saveProject (const juce::String& name);
    void loadProject (const juce::String& name);
    void deleteProject (const juce::String& name);
    void newProject();
    void refreshProjectList();

    class ProjectList : public juce::ListBoxModel
    {
    public:
        juce::StringArray names;
        std::function<void (int)> onChosen;
        //  Tapping a row once should fill the name box with it: picking a
        //  project from the list IS saying which one you mean.
        std::function<void (int)> onSelected;
        int getNumRows() override { return names.size(); }
        void selectedRowsChanged (int row) override { if (onSelected) onSelected (row); }
        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
        {
            if (onChosen) onChosen (row);
        }
    };
    ProjectList  projModel;
    juce::ListBox projList { "proyectos", &projModel };
    juce::TextButton setCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::TextButton projSaveButton { "GUARDAR" };
    juce::TextButton projLoadButton { "ABRIR" };
    juce::TextButton projNewButton  { "NUEVO" };
    juce::TextButton projDeleteButton { "BORRAR" };
    juce::TextButton projExportButton { "EXPORTAR" };


    juce::String currentProject;

    // --- Export -----------------------------------------------------------
    //  The bounce runs on its own thread through a clone of the engine (see
    //  Exporter.h). The UI only starts it, polls its progress from the timer
    //  that is already running, and reports what came out.
    juce::TextButton exportCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::TextButton exportMasterButton { "MASTER" };
    juce::TextButton exportStemsButton  { "PISTAS" };
    juce::TextButton exportCancelButton { "CANCELAR" };
    std::unique_ptr<Exporter> exportJob;
    double deviceSampleRate = 44100.0;

    //  The clock the USER picked, as opposed to whatever the driver last
    //  handed us. setAudioChannels() re-initialises the device from scratch
    //  and loses it, so it has to be remembered and put back. Zero means
    //  "never chosen, let the device decide". See keepChosenRate().
    double chosenRate = 0.0;
    void   keepChosenRate();
    juce::String exportStatus;
    bool         exportOk = false;
    void startExport (bool stems);
    void pollExport();
    juce::String exportSourceLabel() const;

    // --- What the audio device is actually giving us ----------------------
    //  The one latency figure the app can know on its own. It is NOT the
    //  tap-to-sound number — that includes the touchscreen and the compositor
    //  and can only be caught with a microphone — but it does tell you
    //  whether Android handed us the fast path or the slow one, which is the
    //  difference between playable and not.
    void paintAudioInfo (juce::Graphics& g, juce::Rectangle<int> area);

    //  ...and the two knobs that actually move it. Buffer and clock are the
    //  only settings in the app that change how the instrument FEELS rather
    //  than how it sounds, so they sit next to the number they affect.
    juce::OwnedArray<juce::TextButton> bufButtons, rateButtons;
    juce::Rectangle<int> bufRowArea, rateRowArea;
    void useLowestLatency();     // one native burst, not JUCE's 40 ms default

    //  What AAudio granted a bare exclusive request at startup, before any
    //  device of ours existed. This is the only honest answer to "are we on
    //  the fast lane", and it also configures the real stream.
    AudioPath::Fast fastPath;

    //  The measurement. Everything else in this panel is the device's own
    //  claim about itself; this is a click emitted and heard back.
    juce::TextButton measureButton { "MEDIR" };
    float measuredMs = -1.0f;          // last round trip, -1 = never measured
    double measuredRate = 0.0;         // the clock it was actually taken at
    float measuredOutMs = 0.0f;        // what the device claimed while measuring
    float measuredInMs  = 0.0f;
    bool  measuring  = false;
    juce::String measureNote;
    void startMeasure();
    void finishMeasure();
    void refreshAudioOptions();
    void applyAudioSetup (int bufferSize, double rate);

    //  Oboe restarts the device asynchronously, so reading the rate straight
    //  after setAudioDeviceSetup() can still return the OLD one - which is how
    //  the status bar ended up claiming 44100 Hz under a panel reading 48000.
    //  The timer re-reads it until it settles; deviceLine remembers what we
    //  last wrote so a real message (an error, a permission) is never clobbered.
    juce::String deviceLine;
    //  Extra height handed to every seam between sections, computed once
    //  per layout out of whatever the square pad grid did not need.
    int layoutAir = 0;

    //  Top of each seam that carries an engraved name, so paint() can centre
    //  the lettering in the gap instead of hanging it off the section below.
    int ctrlSeamTop = 0, fxSeamTop = 0, padSeamTop = 0;

    //  Set by resized() when the window is wider than it is tall: the face
    //  splits into a column you watch and set, and a column you play. Empty
    //  in portrait, where the whole width is one column.
    bool pressureAnnounced = false;
    //  Where in its breath the effect lamps are, 0..1. Advanced by the UI
    //  timer at two beats per cycle; see timerCallback.
    double fxPulsePhase = 0.0;
    bool wideFace = false;
    juce::Rectangle<int> faceColumn;

    //  Ticks spent chasing the safe area at startup; see timerCallback.
    int insetSettleTicks = 0;

    int lastDeviceBlock = 0, lastDeviceRate = 0;   // compared before a string is built

    //  What the ENGINE was last told the stream is, as opposed to what the
    //  stream actually is now. A phone changes audio route by rebuilding the
    //  stream, and it does not always come back through prepareToPlay - so
    //  these two can drift apart, and when they do everything the engine
    //  computes from the rate is wrong. Checked once a tick; see timerCallback.
    double enginePreparedRate  = 0.0;
    int    enginePreparedBlock = 0;
    int    engineResyncs       = 0;
    void refreshDeviceStatusLine (bool force = false);
    double outputLatencyMs() const;

public:
    //  Called from the application object when Android pauses or resumes the
    //  activity. Public because that is who calls them.
    void appSuspended();
    void appResumed();

    //  Open a sheet by name, for the self-measuring run (see UiAudit.h). The
    //  audit has to reach the sheets - most of the interface lives in them -
    //  and clicking synthetic mouse events at guessed coordinates is exactly
    //  the kind of test that passes because it missed.
    void auditOpen (const juce::String& which);

private:
    void autosave();

    //  The work that was never given a name. Written continuously in the
    //  background and read back on the next launch, so a process the system
    //  reclaimed does not take the session with it.
    SessionKeeper session;
    void restoreSession();
    bool sessionRestorePending = true;   // done on the first timer tick
    int  sessionSyncTick  = 0;
    int  sessionStateTick = 0;

    //  Android arbitrates the speaker between apps. Without asking for the
    //  focus we play over calls and can be silenced without ever being told.
    AudioFocus audioFocus { *this };
    bool pausedByFocus = false;          // ...so GAIN only resumes what WE paused
    void audioFocusLost (bool permanently) override;
    void audioFocusGained() override;

    // --- In-app sample browser -------------------------------------------
    //  A native FileChooser is a system dialog: it ignores the app's skin and
    //  on a tall phone screen its buttons fall outside the viewport. This is
    //  the same JUCE browser embedded in one of our own sheets instead.
    void openBrowseForPad (int index);
    void loadBrowserSelection();
    // Android 13+ hides shared storage behind READ_MEDIA_AUDIO: without it the
    // browser lists an empty directory even when the folder is full of WAVs.
    void ensureStoragePermission (std::function<void()> then);
    void selectionChanged() override;
    void fileClicked (const juce::File&, const juce::MouseEvent&) override {}
    void fileDoubleClicked (const juce::File& f) override;
    void browserRootChanged (const juce::File&) override {}

    std::unique_ptr<juce::WildcardFileFilter>   browseFilter;   // declared first: outlives the browser
    std::unique_ptr<juce::FileBrowserComponent> browser;
    juce::TextButton browseCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::TextButton browseLoadButton  { "CARGAR" };
    juce::TextButton browseSystemButton { "SISTEMA" };   // SAF / OS picker fallback
    std::unique_ptr<juce::FileChooser> chooser;          // only for that fallback
    void launchSystemPicker();
    void importIntoLibrary (const juce::URL& url);
    void cancelAudition();               // restore the pad if you leave without confirming
    juce::File        auditionedFile;
    SampleBuffer::Ptr preAuditionSample;
    juce::String      preAuditionName;
    int browseTargetPad = -1;

    void padClicked (int index);
    void stepCellToggled (int pad, int step);
    void refreshPad (int index);
    void refreshPadArt (int index);   // rebuild the tile waveform for its trim window
    void selectPad (int index);
    void updateControlsFromPad (int index);
    void refreshWaveformSegments();   // fragments sharing the selected pad's buffer
    void assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name = {});
    void toggleRecordArm();     // REC: live pad performance -> the pattern
    void toggleMicSampling();   // PADS sheet: mic -> the selected pad
    // --- AUTO CHOP --------------------------------------------------------
    //  Slicing a break is the most destructive thing in the app: it used to
    //  fire on one tap, always cut sixteen ways, and write over all sixteen
    //  pads including everything already on them. Now it asks: how many, and
    //  whether pads that already hold a sound are off limits.
    juce::TextButton chopCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") },
                     chopGoButton    { "CORTAR" },
                     chopSafeButton  { "RESPETAR PADS CON SONIDO" };
    juce::OwnedArray<juce::TextButton> chopCountBtns;
    static constexpr int kChopCounts[4] = { 2, 4, 8, 16 };
    int  chopSlices    = 8;
    bool chopOnlyEmpty = true;
    void openChopSheet();
    void applyAutoChop();
    juce::Array<int> chopTargets (int slices, bool onlyEmpty) const;
    void refreshChopSheet();
    //  Two-tap confirmation for the actions that destroy work and cannot be
    //  undone: deleting a project takes its folder off the disk, and starting
    //  a new one empties sixteen pads. The first tap arms the button and says
    //  so; the second does it; three seconds of not deciding disarms it.
    //  Cheaper than a sheet, and a sheet would be the third one deep here.
    bool armConfirm (juce::TextButton& b, const juce::String& armedText = "SEGURO?");
    void disarmConfirm();
    juce::TextButton* confirmPending = nullptr;
    juce::String      confirmOldText;
    int               confirmTicks = 0;

    // --- Language ---------------------------------------------------------
    //  Every static caption on the machine is set from one place, so changing
    //  language is one call rather than forty. Called from the constructor
    //  too, which is why no button's text is authoritative in its declaration.
    void retranslateUi();
    void refreshAccessibleNames();

    //  What the system is drawing over the window (Android 15 edge to edge).
    juce::BorderSize<int> systemInsets;
    juce::Rectangle<int> safeArea() const;
    void refreshSystemInsets();

    juce::OwnedArray<juce::TextButton> langButtons;
    juce::Rectangle<int> langRowArea;

    void pushUndo (const juce::String& what);   // snapshot before a destructive action
    void performUndo();
    void performRedo();
    juce::ValueTree undoState, redoState;
    juce::String    undoLabel;
    juce::TextButton undoButton { "DESHACER" };
    juce::TextButton redoButton { "REHACER" };
    void rebuildChain();
    int  firstEmptyPad() const;
    void layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap);

    static constexpr int kNumPads   = AudioEngine::kNumPads;     // 16
    static constexpr int kNumSteps  = AudioEngine::kNumSteps;    // 64 (max pattern length)
    static constexpr int kMinPatLen = AudioEngine::kMinPatLen;   // 16
    static constexpr int kMaxPatLen = AudioEngine::kMaxPatLen;   // 64
    static constexpr int kStepCols  = StepGrid::kBarSteps;       // one bar of 16 across

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<PadButton> pads;
    StepGrid stepGrid;
    juce::OwnedArray<juce::TextButton> barButtons;   // bar 1..4 when the pattern is longer than one
    bool  gridCells[AudioEngine::kNumSteps * AudioEngine::kNumPads] {};
    signed char gridNotes[AudioEngine::kNumSteps * AudioEngine::kNumPads] {};
    int   gridZati[AudioEngine::kNumPads] {};
    bool  gridLoaded[AudioEngine::kNumPads] {};
    int   selectedBar = 0;
    void  refreshStepGrid();

    // Module bar — rule of three: PADS / SEC / FX, each opening its floating
    // sheet (never a mode switch). CHOP lives inside PADS; CHAIN inside SEC.
    juce::TextButton padsButton  { "PADS" };
    juce::TextButton secButton   { "SEC" };
    juce::TextButton mixButton      { "MIX" };   // the 16-channel mixer sheet
    juce::TextButton songButton     { "SONG" };  // the arrangement timeline

    //  SONG: pick what to place from the palette, then tap a cell. Choosing
    //  first and placing second beats drag-and-drop on a phone — a drag from a
    //  palette to a 20px cell is a gesture you lose halfway.
    Playlist songGrid;
    juce::OwnedArray<juce::TextButton> songPatBtns;   // P1..P8
    juce::TextButton songPadModeBtn { "SONIDO" };     // place a one-shot instead
    juce::TextButton songClearBtn   { "VACIAR" };
    juce::TextButton songModeBtn    { "CANCION" };    // song transport vs pattern/chain
    juce::TextButton songCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::Slider     songLenSlider;
    juce::OwnedArray<juce::TextButton> songPageBtns;
    int songBrush   = 1;      // >0 pattern bank+1, <0 -(pad+1), 0 = eraser
    int songPage    = 0;
    int songCells[Playlist::kLanes * AudioEngine::kSongBars] {};
    void refreshSong();
    juce::TextButton setButton      { "SET" };   // skins + proyectos (spec: SET)
    juce::TextButton seqCloseButton   { juce::CharPointer_UTF8 ("\xc3\x97") },
                     padCloseButton   { juce::CharPointer_UTF8 ("\xc3\x97") },
                     mixCloseButton   { juce::CharPointer_UTF8 ("\xc3\x97") };
    //  A studio is where a track gets finished, and nothing gets finished
    //  without balancing it. One strip per pad: level, mute, solo.
    juce::OwnedArray<juce::Slider>     mixFaders, mixPans;

    //  The rack: one pad's six sends, opened from the mixer. An effect here
    //  is not on or off, it is how much of THIS channel goes into it - which
    //  is the only place where "the delay belongs to the snare" can be said.
    juce::TextButton rackButton { "RACK" }, rackCloseButton { "x" };
    juce::OwnedArray<juce::TextButton> rackPadBtns;
    juce::OwnedArray<juce::Slider>     rackSends;
    int rackPad = 0;
    void refreshRack();
    juce::OwnedArray<juce::TextButton> mixMutes, mixSolos;
    juce::TextButton mixClearSolo { "SIN SOLO" };
    void refreshMixStrip();

    //  SIXTEEN STRIPS THAT SCROLL RATHER THAN SIXTEEN STRIPS THAT SHRINK.
    //
    //  The mixer used to divide whatever height the card was allowed by
    //  sixteen and live with the answer: on a 640-tall phone that is a 24 px
    //  row, and M and S came out 36x20 - a third of the finger minimum, on the
    //  two controls you hit fastest and most often while something is playing.
    //  Measured across the matrix, it was the worst target in the app.
    //
    //  A mixer that scrolls is what every mixer does. The rows keep their full
    //  height everywhere and the card shows as many as it has room for.
    struct MixRows : public juce::Component
    {
        std::function<void (juce::Graphics&)> paintRows;
        void paint (juce::Graphics& g) override { if (paintRows) paintRows (g); }
    };
    MixRows        mixRows;
    juce::Viewport mixScroll;
    void paintMixRows (juce::Graphics& g);
    juce::OwnedArray<juce::TextButton> patternButtons;  // P1..P8 — chain include toggles

    // --- FX slots (spec Zone 5) -------------------------------------------
    //  Four one-tap effects that live next to the pads and never cover them,
    //  so they can be fired while playing. They are execution, not editing:
    //  the FX sheet stays the rack where you choose and set up. Tapping a slot
    //  arms its effect AND hands it the three CTRL knobs, which is what makes
    //  a single row worth more than a paged strip of every effect.
    // A slot has two gestures on one target: tap = fire, long press =
    // reassign. TextButton only reports the click, so the press duration is
    // measured here and a long hold suppresses the click that would follow.
    //  A cap with two gestures: tap, and hold.
    //
    //  It used to decide WHICH on release - mouseUp compared the length of the
    //  press against the threshold. That is a hold you cannot feel: you press,
    //  you wait, nothing on screen changes, and the only way to find out
    //  whether the gesture took is to let go. Held over a running effect while
    //  the sequencer plays, it reads as a button that does nothing, so you tap
    //  instead and switch the effect off - which is the complaint.
    //
    //  Now a timer fires AT the threshold, with the finger still down. The
    //  three knobs re-range under your thumb the instant the gesture lands,
    //  which is the feedback; the release afterwards is swallowed so the hold
    //  never also counts as a tap. A finger that slides off the cap cancels
    //  it, the same as every other press on the face.
    class HoldButton : public juce::TextButton,
                       private juce::Timer
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void()> onHold;
        //  Long enough not to fire on a firm tap, short enough that it lands
        //  while you still think of yourself as pressing. Android's own
        //  long-press is 500; a control you play with wants to be under it.
        static constexpr int kHoldMs = 420;

        void mouseDown (const juce::MouseEvent& e) override
        {
            held = false;
            startTimer (kHoldMs);
            juce::TextButton::mouseDown (e);
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! getLocalBounds().contains (e.getPosition()))
                stopTimer();
            juce::TextButton::mouseDrag (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            stopTimer();
            if (held)
            {
                setState (buttonNormal);   // swallow the click this press would fire
                return;
            }
            juce::TextButton::mouseUp (e);
        }

        bool wasHeld() const { return held; }

    private:
        void timerCallback() override
        {
            stopTimer();
            held = true;
            if (onHold) onHold();
        }

        bool held = false;
    };

    // --- The six effects --------------------------------------------------
    //  One row, six buttons, one effect each: ISO, HPF, DRV, DLY, CRSH, REV.
    //  There used to be four re-assignable slots plus three bank chips above
    //  the knobs, which meant an effect could be pointed at, switched on and
    //  edited from three different places — the "there are two delays" bug.
    //  Now a button IS its effect: tapping it hands the three CTRL knobs its
    //  three parameters and tapping it again switches it off. Six effects,
    //  six switches, no modes.
    static constexpr int kNumFx = 6;
    struct FxDef
    {
        const char* name;                  // face button
        const char* param[3];              // what CTRL 1-3 become
        struct Spec { double lo, hi, step, skewMid, def; int fmt; } spec[3];
        double onMix;                      // MIX applied when you switch it on
    };
    static const FxDef fxDefs[kNumFx];

    std::array<bool, kNumFx> fxOn {};
    int focusedFx = 0;                     // whose parameters CTRL 1-3 hold
    juce::OwnedArray<juce::TextButton> fxButtons;
    juce::OwnedArray<juce::Slider>     fxParams;   // kNumFx * 3, the real values
    juce::Rectangle<int> fxRowArea;

    void fxTapped (int fx);
    void fxFocusOnly (int fx);      // long press: take the knobs, leave the switch
    void setFxEnabled (int fx, bool on);
    void focusFx (int fx);
    void pushFxParam (int fx, int p);              // slider -> engine
    juce::Slider& fxParam (int fx, int p) { return *fxParams[fx * 3 + p]; }
    static juce::String fxFormat (const FxDef::Spec& sp, double v);

    // Dynamic knob labels (spec Zone 4): at rest the knob shows its permanent
    // name (CTRL 1/2/3); while it is being touched it shows the parameter it
    // currently drives, and returns to the base label ~800 ms after release.
    // One element names, the other measures — the label never shows figures.
    class LabelTimer : public juce::Timer
    {
    public:
        std::function<void()> onFire;
        void timerCallback() override { stopTimer(); if (onFire) onFire(); }
    };
    LabelTimer macroLabelTimer;
    std::array<bool, 3> macroTouched { { false, false, false } };
    juce::String macroBaseLabel (int idx) const;
    juce::String macroParamLabel (int idx) const;
    juce::String macroReadout (int idx) const;
    void setMacroTouched (int idx, bool touched);

    void refreshMacroValues();
    void macroMoved (int idx);

    // Skin cycler: four chassis TONES (TINTA/GRAFITO/ACERO/PLOMO), no hues.
    void applySkin();

    juce::TextButton loadButton { "LOAD" };
    juce::TextButton testButton { "TEST" };
    juce::TextButton recButton  { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton clearButton { "VACIAR" };
    juce::TextButton reverseButton { "REV" };
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton autocutButton { "AUTOCUT" };
    juce::TextButton chopButton { "AUTO CHOP" };
    juce::TextButton micButton  { "GRABAR MIC" };   // lives in the PADS sheet

    //  Auditioning from the PADS sheet: the wave answers a tap, and this plays
    //  it from the top without having to reach past the sheet for the pad.
    juce::TextButton previewButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6 OIR") };
    bool previewSounding = false;

    //  The project name you type, and where the folder actually is. GUARDAR
    //  used to invent "PROYECTO N" with no way to say otherwise, so every save
    //  was a new near-duplicate and none of them was called what you wanted.
    juce::TextEditor     projNameBox;
    juce::Rectangle<int> projNameRowArea, projPathRowArea;
    juce::Rectangle<int> zatiSwatchArea;
    //  The three group headers of the PADS sheet, placed in resized() and
    //  drawn in paintPadSheetContent: a sheet with eleven controls on it needs
    //  to say which of them belong together.
    std::array<juce::Rectangle<int>, 3> padSectionArea {};
    void setZati (int z);
    bool recArmed = false;                          // REC writes hits into the pattern

    juce::Slider pitchSlider, fineSlider, volSlider, startSlider, endSlider, bpmSlider, chokeSlider;
    //  CINTA moves pitch and length together, TONO keeps the length.
    juce::TextButton modeButton { "CINTA" };
    juce::Slider panSlider, attackSlider, releaseSlider;
    //  What a step DOES, not just which pads it fires: how hard, how many
    //  times, and how far off the grid the odd ones sit.
    juce::Slider patternSlider, noteSlider, lengthSlider, velSlider, rollSlider, swingSlider;
    juce::TextButton chainClearButton { "QUITAR CADENA" };
    juce::Slider macroCtrl1, macroCtrl2, macroCtrl3;   // CTRL 1-3, bank-dependent
    juce::Label  status, fxLabel;
    WaveformDisplay waveform;
    SpectrumDisplay spectrum;
    ZatiLookAndFeel lnf;
    float scopeTmp[1024] {};

    // Per-pad UI state.
    std::array<bool,  kNumPads> padHasSample {};
    std::array<float, kNumPads> padPitch {};      // whole semitones
    std::array<float, kNumPads> padCents {};      // -100..100, the part between them
    std::array<bool,  kNumPads> padKeepLen {};
    std::array<float, kNumPads> padGain {};
    std::array<float, kNumPads> padStart01 {};
    std::array<float, kNumPads> padEnd01 {};
    std::array<bool,  kNumPads> padLoop {};
    //  AUTOCUT, on by default - see AudioEngine's constructor. Filled in
    //  MainComponent's, because a std::array of bool cannot say "all true"
    //  in its declaration.
    std::array<bool,  kNumPads> padSelfCut {};
    std::array<bool,  kNumPads> padReverse {};
    std::array<int,   kNumPads> padChokeUI {};   // 0 = none
    std::array<float, kNumPads> padPan {};        // -1..1, 0 = centre
    std::array<float, kNumPads> padAttack {};     // ms
    std::array<float, kNumPads> padRelease {};    // ms
    std::array<SampleBuffer::Ptr, kNumPads> uiSample;
    std::array<juce::String, kNumPads> padName {};
    std::array<int, kNumPads> padZati {};       // fragment colour per pad (cut order)

    // Pattern mirror [bank][step][pad] for the sequencer UI.
    static constexpr int kNumPatterns = AudioEngine::kNumPatterns;   // 8
    std::array<std::array<std::array<bool, kNumPads>, kNumSteps>, kNumPatterns> pattern {};
    int selectedPattern = 0;
    int selectedStep    = -1;
    std::array<bool, kNumPatterns> patternActiveUI {};   // which banks are in the chain

    std::array<float, kNumPads> padFlash {};   // 1.0 on trigger, decays -> lit feedback
    // Chassis layout regions (set in resized(), drawn in paint()).
    juce::Rectangle<int> headerArea, screenBezel, tabBarArea,
                         editInfoArea, vuArea, stepStripArea, audioInfoArea,
                         padPlateArea, ctrlPlateArea;
    float vuL = 0.0f, vuR = 0.0f;   // smoothed output peaks for the VU strip

    int  selectedPad   = -1;
    bool loadArmed     = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

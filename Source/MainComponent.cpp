#include "MainComponent.h"
#include "Lang.h"
#include "SystemInsets.h"
#include "DeviceTier.h"

namespace
{
    // References, not copies: the accent tokens are mutable (skins).
    const juce::Colour& kPadLoaded = ZatiColours::amber;
    const juce::Colour& kAccent    = ZatiColours::amber;
    const juce::Colour  kRec       = ZatiColours::red;
    const juce::Colour  kStepOff   = ZatiColours::key;
    const juce::Colour  kKey       = ZatiColours::key;

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        // Text follows the cap luminance: dark ink on light caps, light on dark.
        const bool darkCap = c.getPerceivedBrightness() < 0.5f;
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, darkCap ? ZatiColours::inkLight : ZatiColours::ink);
        b.setColour (juce::TextButton::textColourOnId,  ZatiColours::ink);
    }

    // Cycle the 3 primaries across the 8 pattern banks so each has its own
    // colour identity in the chain-include row.
    // Pattern banks are told apart by TONE, not hue: the chassis carries no
    // colour of its own, so three steps of ink stand in for what used to be
    // three primaries. Re-read every call — the skin shifts the base tone.
    juce::Colour patternRowColour (int idx)
    {
        const juce::Colour tones[3] = { ZatiColours::accent,
                                        ZatiColours::accent.brighter (0.60f),
                                        ZatiColours::accent.brighter (1.25f) };
        return tones[(size_t) (idx % 3)];
    }
}

MainComponent::MainComponent()
{
    setLookAndFeel (&lnf);

    // Build the ZATI folder tree before anything can need it: the browser opens
    // in Samples/, projects save into Projects/, REC writes to Recordings/.
    ProjectStore::ensureTree();

    // Ask the phone what it will actually grant BEFORE opening the real
    // device - the answer decides how the real device gets opened, and the
    // probe needs the output free to ask for an exclusive stream at all.
    fastPath = AudioPath::probeFastPath (48000, 2);
    zatiOboeUsage    = fastPath.exclusive ? fastPath.usage : 0;
    zatiOboeForceI16 = (fastPath.exclusive && fastPath.useI16) ? 1 : 0;

    //  Ask for the speaker before opening the stream. A refusal is not fatal -
    //  we open anyway, because a silent instrument is a worse answer than one
    //  the system happens to be ducking - but asking is what puts us in the
    //  queue to be TOLD when somebody else takes it, which is the half that
    //  was missing.
    audioFocus.request();

    // Output only at startup so the app always makes sound; the mic input is
    // opened on demand when recording (avoids risking output on a denied perm).
    setAudioChannels (0, 2);
    useLowestLatency();

    padGain.fill (0.85f);
    padEnd01.fill (1.0f);
    padAttack.fill (2.0f);
    padRelease.fill (5.0f);
    //  AUTOCUT on everywhere, matching the engine. A pad that stacks over
    //  its own tail is the special case, not the normal one.
    padSelfCut.fill (true);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new PadButton (i);
        padZati[(size_t) i] = Zati::forPad (i);      // cut order: zati 1 is always red
        p->setZati (padZati[(size_t) i]);
        p->onClick = [this, i] { padClicked (i); };
        addAndMakeVisible (p);
        pads.add (p);
        refreshPad (i);
        refreshPadArt (i);          // also gives the pad its accessible name
    }

    stepGrid.onCell = [this] (int pad, int step) { stepCellToggled (pad, step); };
    seqSheet.addAndMakeVisible (stepGrid);

    // Bar selector: 64 steps will not fit across a phone at a size worth
    // tapping, so the grid pages a bar at a time instead of shrinking.
    for (int b = 0; b < kNumSteps / kStepCols; ++b)
    {
        auto* t = new juce::TextButton (juce::String (b + 1));
        styleButton (*t, kStepOff);
        t->setColour (juce::TextButton::buttonOnColourId, kAccent);
        t->setClickingTogglesState (true);
        t->onClick = [this, b]
        {
            selectedBar = b;
            for (int i = 0; i < barButtons.size(); ++i)
                barButtons[i]->setToggleState (i == b, juce::dontSendNotification);
            refreshStepGrid();
        };
        seqSheet.addAndMakeVisible (t);
        barButtons.add (t);
    }
    barButtons[0]->setToggleState (true, juce::dontSendNotification);

    // Module bar. FX is NOT a module any more: the six effects live in their
    // own row on the machine face, where you can reach them mid-take without
    // covering the pads. A sheet for them would only be a second way to switch
    // the same six things on.
    {
        juce::TextButton* mb[2]  = { &padsButton, &secButton };
        Sheet*            sh[2]  = { &padSheet, &seqSheet };
        for (int i = 0; i < 2; ++i)
        {
            styleButton (*mb[i], kKey);
            mb[i]->setColour (juce::TextButton::buttonOnColourId, kAccent);
            auto* s = sh[i]; auto* b = mb[i];
            b->onClick = [this, s, b] { if (s->isVisible()) closeAllSheets(); else openSheet (*s, *b); };
            addAndMakeVisible (b);
        }

        juce::TextButton* cb[2] = { &padCloseButton, &seqCloseButton };
        std::function<void (juce::Graphics&)> pc[2] =
        {
            [this] (juce::Graphics& g) { paintPadSheetContent (g); },
            [this] (juce::Graphics& g) { paintSeqSheetContent (g); },
        };
        for (int i = 0; i < 2; ++i)
        {
            auto* s = sh[i];
            addAndMakeVisible (s);
            s->setVisible (false);
            s->onDismiss = [this] { closeAllSheets(); };
            s->paintContent = pc[i];
            styleButton (*cb[i], kKey);
            cb[i]->onClick = [this] { closeAllSheets(); };
            s->addAndMakeVisible (cb[i]);
        }
    }

    // Projects sheet — reached from the header chip, not the module bar (the
    // bar stays a rule of three: PADS / SEC / FX).
    {
        addAndMakeVisible (projSheet);
        projSheet.setVisible (false);
        projSheet.onDismiss = [this] { closeAllSheets(); };
        projSheet.paintContent = [this] (juce::Graphics& g) { paintProjSheetContent (g); };

        styleButton (setButton, kKey);
        setButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
        setButton.onClick = [this]
        {
            if (projSheet.isVisible()) closeAllSheets();
            else { refreshProjectList(); refreshAudioOptions(); openSheet (projSheet, setButton); }
        };
        addAndMakeVisible (setButton);

        projList.setColour (juce::ListBox::backgroundColourId, ZatiColours::chassisTop);
        projList.setRowHeight (34);
        projModel.onChosen = [this] (int row)
        {
            if (juce::isPositiveAndBelow (row, projModel.names.size()))
                loadProject (projModel.names[row]);
        };
        projSheet.addAndMakeVisible (projList);

        styleButton (projCloseButton, kKey);
        projCloseButton.onClick = [this] { closeAllSheets(); };
        projSheet.addAndMakeVisible (projCloseButton);

        styleButton (projSaveButton, kAccent);
        projSaveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        projSaveButton.onClick = [this]
        {
            // Reuse the highlighted name when there is one, so GUARDAR
            // overwrites the project you are looking at rather than silently
            // spawning near-duplicates.
            const int sel = projList.getSelectedRow();
            juce::String name = juce::isPositiveAndBelow (sel, projModel.names.size())
                                  ? projModel.names[sel]
                                  : currentProject;
            if (name.isEmpty())
                name = "PROYECTO " + juce::String (ProjectStore::list().size() + 1);
            saveProject (name);
        };
        projSheet.addAndMakeVisible (projSaveButton);

        styleButton (projLoadButton, kKey);
        projLoadButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, projModel.names.size()))
                loadProject (projModel.names[sel]);
        };
        projSheet.addAndMakeVisible (projLoadButton);

        styleButton (projNewButton, kKey);
        //  NUEVO empties every pad and every pattern. Two taps.
        projNewButton.onClick = [this]
        {
            if (! armConfirm (projNewButton, "BORRA TODO?")) return;
            newProject();
        };
        projSheet.addAndMakeVisible (projNewButton);

        styleButton (projDeleteButton, kRec);
        //  ...and BORRAR takes a folder off the disk, audio and all, with no
        //  undo anywhere. The armed button names the project it will take.
        projDeleteButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (! juce::isPositiveAndBelow (sel, projModel.names.size()))
            {
                disarmConfirm();
                status.setText (T ("Elige un proyecto de la lista"), juce::dontSendNotification);
                return;
            }

            if (! armConfirm (projDeleteButton, "BORRAR " + projModel.names[sel] + "?")) return;
            deleteProject (projModel.names[sel]);
        };
        projSheet.addAndMakeVisible (projDeleteButton);

        styleButton (projExportButton, kKey);
        projExportButton.onClick = [this]
        {
            exportStatus.clear();
            exportOk = false;
            openSheet (exportSheet, setButton);
        };
        projSheet.addAndMakeVisible (projExportButton);

        // --- RACK: one pad's sends, opened from the mixer. ---------------
        styleButton (rackButton, kKey);
        rackButton.onClick = [this] { rackPad = juce::jmax (0, selectedPad); openSheet (rackSheet, mixButton); refreshRack(); };
        mixSheet.addAndMakeVisible (rackButton);

        for (int i = 0; i < kNumPads; ++i)
        {
            auto* b = new juce::TextButton (juce::String (i + 1).paddedLeft ('0', 2));
            styleButton (*b, kStepOff);
            b->setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { rackPad = i; selectPad (i); refreshRack(); };
            rackSheet.addAndMakeVisible (b);
            rackPadBtns.add (b);
        }

        for (int f = 0; f < kNumFx; ++f)
        {
            auto* sl = new juce::Slider();
            sl->setSliderStyle (juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 20);
            sl->setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
            sl->setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
            sl->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sl->setRange (0.0, 1.0, 0.01);
            sl->setValue (1.0, juce::dontSendNotification);
            sl->setDoubleClickReturnValue (true, 1.0);
            sl->setSliderSnapsToMousePosition (false);
            sl->textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)); };
            sl->updateText();
            sl->onValueChange = [this, f, sl] { engine.setPadSend (rackPad, f, (float) sl->getValue()); rackSheet.repaint(); };
            rackSheet.addAndMakeVisible (sl);
            rackSends.add (sl);
        }

        styleButton (rackCloseButton, kKey);
        rackCloseButton.onClick = [this] { closeAllSheets(); };
        rackSheet.addAndMakeVisible (rackCloseButton);
        addAndMakeVisible (rackSheet);
        rackSheet.setVisible (false);
        rackSheet.onDismiss = [this] { closeAllSheets(); };
        rackSheet.paintContent = [this] (juce::Graphics& g) { paintRackSheetContent (g); };

        // --- AUTO CHOP: the confirmation the destruction always deserved ---
        for (int i = 0; i < 4; ++i)
        {
            const int n = kChopCounts[i];
            auto* b = new juce::TextButton (juce::String (n));
            styleButton (*b, kStepOff);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7301);
            b->onClick = [this, n] { chopSlices = n; refreshChopSheet(); };
            chopSheet.addAndMakeVisible (b);
            chopCountBtns.add (b);
        }

        styleButton (chopSafeButton, kKey);
        chopSafeButton.setClickingTogglesState (true);
        chopSafeButton.setToggleState (true, juce::dontSendNotification);
        chopSafeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
        chopSafeButton.onClick = [this]
        {
            chopOnlyEmpty = chopSafeButton.getToggleState();
            refreshChopSheet();
        };
        chopSheet.addAndMakeVisible (chopSafeButton);

        styleButton (chopGoButton, ZatiColours::red);
        chopGoButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        chopGoButton.onClick = [this] { applyAutoChop(); };
        chopSheet.addAndMakeVisible (chopGoButton);

        styleButton (chopCloseButton, kKey);
        chopCloseButton.onClick = [this] { closeAllSheets(); };
        chopSheet.addAndMakeVisible (chopCloseButton);
        addAndMakeVisible (chopSheet);
        chopSheet.setVisible (false);
        chopSheet.onDismiss = [this] { closeAllSheets(); };
        chopSheet.paintContent = [this] (juce::Graphics& g) { paintChopSheetContent (g); };

        //  Four chips, each written the way that language writes itself:
        //  someone who cannot read the current one still recognises their own.
        for (int i = 0; i < Lang::numLanguages; ++i)
        {
            //  fromUTF8, not the char* constructor: juce::String (const char*)
            //  is documented as ASCII-only and turns 中文 into mojibake.
            auto* b = new juce::TextButton (juce::String::fromUTF8 (Lang::nativeName ((Lang::Id) i)));
            styleButton (*b, kStepOff);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7411);
            b->onClick = [this, i]
            {
                Lang::set ((Lang::Id) i);
                Lang::savePreference();
                retranslateUi();
            };
            projSheet.addAndMakeVisible (b);
            langButtons.add (b);
        }

        styleButton (measureButton, kKey);
        measureButton.onClick = [this] { startMeasure(); };
        projSheet.addAndMakeVisible (measureButton);
    }

    // EXPORT sheet — the only door out of the app. Two products: the master,
    // or the master plus one file per loaded pad.
    {
        addAndMakeVisible (exportSheet);
        exportSheet.setVisible (false);
        exportSheet.onDismiss = [this] { if (exportJob == nullptr) closeAllSheets(); };
        exportSheet.paintContent = [this] (juce::Graphics& g) { paintExportSheetContent (g); };

        styleButton (exportCloseButton, kKey);
        exportCloseButton.onClick = [this] { if (exportJob == nullptr) closeAllSheets(); };
        exportSheet.addAndMakeVisible (exportCloseButton);

        styleButton (exportMasterButton, kAccent);
        exportMasterButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        exportMasterButton.onClick = [this] { startExport (false); };
        exportSheet.addAndMakeVisible (exportMasterButton);

        styleButton (exportStemsButton, kKey);
        exportStemsButton.onClick = [this] { startExport (true); };
        exportSheet.addAndMakeVisible (exportStemsButton);

        styleButton (exportCancelButton, kRec);
        exportCancelButton.onClick = [this]
        {
            if (exportJob != nullptr) exportJob->signalThreadShouldExit();
        };
        exportSheet.addAndMakeVisible (exportCancelButton);
        exportCancelButton.setVisible (false);
    }

    // Sample browser sheet — no module button of its own: it is opened by the
    // LOAD flow (arm LOAD, tap a pad) and targets that pad.
    {
        addAndMakeVisible (browseSheet);
        browseSheet.setVisible (false);
        browseSheet.onDismiss = [this] { closeAllSheets(); };
        browseSheet.paintContent = [this] (juce::Graphics& g) { paintBrowseSheetContent (g); };

        browseFilter = std::make_unique<juce::WildcardFileFilter> (
            "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3", "*", "Muestras de audio");

        // Start one level above Music: on Android that is the shared-storage
        // root, so Music AND Download (where most samples land) are one tap
        // away instead of buried. On desktop it lands on the home folder.
        // Open in the app's own Samples folder. It always exists (the tree is
        // created at launch) and it is where the user is told to put audio, so
        // the first thing the browser shows is their own material instead of
        // whatever the OS considers home — which on desktop is an empty /root.
        auto start = ProjectStore::samples();
        if (! start.isDirectory())
            start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);

        browser = std::make_unique<juce::FileBrowserComponent> (
            juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectFiles
          | juce::FileBrowserComponent::filenameBoxIsReadOnly,   // no keyboard on mobile
            start, browseFilter.get(), nullptr);
        browser->addListener (this);
        //  JUCE's default row is about 22px — half a comfortable touch target.
        //  Choosing a sample is the one thing you do before anything else, so
        //  it should not be the fiddliest tap in the app.
        if (auto* list = dynamic_cast<juce::FileListComponent*> (browser->getDisplayComponent()))
            list->setRowHeight (Metrics::row);
        browseSheet.addAndMakeVisible (*browser);

        styleButton (browseCloseButton, kKey);
        browseCloseButton.onClick = [this] { cancelAudition(); closeAllSheets(); };
        browseSheet.addAndMakeVisible (browseCloseButton);

        styleButton (browseLoadButton, kAccent);
        browseLoadButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        browseLoadButton.onClick = [this] { loadBrowserSelection(); };
        browseSheet.addAndMakeVisible (browseLoadButton);

        // Escape hatch: hand off to the OS picker. Some Android ROMs hide media
        // files from a direct directory listing no matter what is granted; the
        // system picker always reaches them (and gets its own access grant).
        styleButton (browseSystemButton, kKey);
        browseSystemButton.onClick = [this] { launchSystemPicker(); };
        browseSheet.addAndMakeVisible (browseSystemButton);
    }

    // Transport / actions.
    loadButton.setClickingTogglesState (true);
    styleButton (loadButton, kKey);
    loadButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loadButton.onClick = [this]
    {
        loadArmed = loadButton.getToggleState();
        // ASCII only: a raw UTF-8 dash in a literal renders as mojibake on the
        // Android build (different execution charset), so keep these plain.
        status.setText (loadArmed ? T ("LOAD armado - toca un pad para cargarlo")
                                  : T ("Toca un pad para sonar"), juce::dontSendNotification);
    };
    addAndMakeVisible (loadButton);

    styleButton (testButton, kKey);
    testButton.onClick = [this] { engine.postTestTone(); status.setText (T ("Tono de prueba"), juce::dontSendNotification); };
    projSheet.addAndMakeVisible (testButton);

    styleButton (recButton, kKey);
    recButton.onClick = [this] { toggleRecordArm(); };
    addAndMakeVisible (recButton);

    styleButton (micButton, kKey);
    micButton.onClick = [this] { toggleMicSampling(); };
    padSheet.addAndMakeVisible (micButton);

    //  The zati row is painted, not built out of components: eight swatches
    //  in a strip of chip height. See paintPadSheetContent.
    padSheet.onContentClick = [this] (juce::Point<int> p)
    {
        if (selectedPad < 0 || ! zatiSwatchArea.contains (p)) return;
        const float w = (float) zatiSwatchArea.getWidth() / (float) Zati::kNumColours;
        setZati (juce::jlimit (0, Zati::kNumColours - 1,
                               (int) ((float) (p.x - zatiSwatchArea.getX()) / w)));
    };

    playButton.setClickingTogglesState (true);
    //  PLAY is the widest key on the face, and it used to be the dark one as
    //  well - which in this system means ENGAGED. A stopped transport was
    //  wearing the colour of a running one. It is a key like the others now
    //  and goes dark only while it is actually playing, which is what the
    //  toggle state is for.
    styleButton (playButton, kKey);
    playButton.setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
    playButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::inkLight);
    playButton.onClick = [this]
    {
        const bool on = playButton.getToggleState();
        engine.setPlaying (on);
        playButton.setButtonText (on ? T ("STOP") : T ("PLAY"));
    };
    addAndMakeVisible (playButton);

    styleButton (clearButton, kKey);
    clearButton.onClick = [this]
    {
        //  Emptying a whole pattern used to be one tap with nothing behind it.
        //  It is the same size of loss as a chop, so it gets the same net.
        pushUndo ("vaciar patron");
        engine.clearPattern (selectedPattern);
        for (auto& row : pattern[(size_t) selectedPattern]) row.fill (false);
        if (selectedPad >= 0) selectPad (selectedPad);
    };
    seqSheet.addAndMakeVisible (clearButton);

    // Per-pad edit controls.
    auto initSlider = [this] (juce::Slider& s, double lo, double hi, double step, double def)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 22);
        s.setRange (lo, hi, step);
        s.setValue (def, juce::dontSendNotification);
        s.setColour (juce::Slider::trackColourId, kPadLoaded);
        addAndMakeVisible (s);
    };
    initSlider (startSlider,   0.0,  1.0, 0.001, 0.0);
    initSlider (endSlider,     0.0,  1.0, 0.001, 1.0);
    initSlider (bpmSlider,    60.0, 200.0, 1.0, 120.0);
    bpmSlider.setTextValueSuffix (" bpm");
    bpmSlider.onValueChange = [this] { engine.setBpm (bpmSlider.getValue()); };
    seqSheet.addAndMakeVisible (bpmSlider);   // lives in the sequencer sheet, not the main tabs

    // Per-pad controls as rotary KNOBS, not faders — "nops, no faders".
    auto initKnob = [this] (juce::Slider& s, double lo, double hi, double step, double def,
                            double skewMid, std::function<void()> cb)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        s.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 20);
        s.setRange (lo, hi, step);
        if (skewMid > 0.0) s.setSkewFactorFromMidPoint (skewMid);
        s.setValue (def, juce::dontSendNotification);
        s.setDoubleClickReturnValue (true, def);     // double-tap = back to default
        //  How far you drag for the whole range. JUCE's default crosses it in
        //  a flick, which on a touchscreen means you cannot land on a value,
        //  only near one; this asks for a deliberate movement and gives back
        //  a knob you can actually set.
        s.setMouseDragSensitivity (320);
        s.onValueChange = std::move (cb);
        addAndMakeVisible (s);
    };
    //  Pitch is two controls because it is two decisions. Twenty-four
    //  semitones on one dial cannot be nudged by a cent - you would be asking
    //  for one part in 4800 out of a thumb - so the note and the tuning get a
    //  knob each, and the engine is handed their sum.
    auto sendPitch = [this]
    {
        if (selectedPad < 0) return;
        padPitch[(size_t) selectedPad] = (float) pitchSlider.getValue();
        padCents[(size_t) selectedPad] = (float) fineSlider.getValue();
        engine.setPadPitch (selectedPad, (float) (pitchSlider.getValue() + fineSlider.getValue() / 100.0));
    };
    initKnob (pitchSlider, -24.0, 24.0, 1.0, 0.0, 0.0, sendPitch);
    initKnob (fineSlider, -100.0, 100.0, 1.0, 0.0, 0.0, sendPitch);
    initKnob (volSlider, 0.0, 1.0, 0.01, 0.85, 0.0,
             [this] { if (selectedPad >= 0) { padGain[(size_t) selectedPad] = (float) volSlider.getValue(); engine.setPadGain (selectedPad, (float) volSlider.getValue()); } });
    initKnob (panSlider, -1.0, 1.0, 0.01, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padPan[(size_t) selectedPad] = (float) panSlider.getValue(); engine.setPadPan (selectedPad, (float) panSlider.getValue());
                                              if (auto* mp = mixPans[selectedPad]) mp->setValue (panSlider.getValue(), juce::dontSendNotification); } });
    initKnob (attackSlider, 0.0, 200.0, 1.0, 2.0, 20.0,
             [this] { if (selectedPad >= 0) { padAttack[(size_t) selectedPad] = (float) attackSlider.getValue(); engine.setPadAttack (selectedPad, (float) attackSlider.getValue()); } });
    initKnob (releaseSlider, 1.0, 800.0, 1.0, 5.0, 40.0,
             [this] { if (selectedPad >= 0) { padRelease[(size_t) selectedPad] = (float) releaseSlider.getValue(); engine.setPadRelease (selectedPad, (float) releaseSlider.getValue()); } });
    //  A choke group is off or 1..8 — nine discrete positions. A rotary asks
    //  you to aim for 4 and land on 3; increment buttons hit it first try and
    //  show the state without reading a number off a dial.
    initKnob (chokeSlider, 0.0, 8.0, 1.0, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padChokeUI[(size_t) selectedPad] = (int) chokeSlider.getValue(); engine.setPadChoke (selectedPad, (int) chokeSlider.getValue()); } });
    chokeSlider.setSliderStyle (juce::Slider::IncDecButtons);
    chokeSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, Metrics::chip);

    pitchSlider.setTextValueSuffix (" st");
    fineSlider.textFromValueFunction = [] (double v)
    {
        return (v > 0.0 ? "+" : "") + juce::String ((int) v) + " c";
    };
    fineSlider.updateText();
    attackSlider.setTextValueSuffix (" ms");
    releaseSlider.setTextValueSuffix (" ms");
    panSlider.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return (v < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (v) * 100.0));
    };
    panSlider.updateText();
    chokeSlider.textFromValueFunction = [] (double v) { return v <= 0.0 ? T ("off") : juce::String ((int) v); };
    chokeSlider.updateText();

    //  CINTA is what a sampler does by nature - pitch and length are the same
    //  knob - and TONO keeps the length, which is the difference between a
    //  vocal you can transpose and a chipmunk.
    styleButton (modeButton, kKey);
    modeButton.setClickingTogglesState (true);
    modeButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    modeButton.onClick = [this]
    {
        if (selectedPad < 0) return;
        const bool keep = modeButton.getToggleState();
        padKeepLen[(size_t) selectedPad] = keep;
        engine.setPadKeepLength (selectedPad, keep);
        modeButton.setButtonText (keep ? T ("TONO") : T ("CINTA"));
    };
    padSheet.addAndMakeVisible (modeButton);

    startSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmin (startSlider.getValue(), endSlider.getValue() - 0.01);
        padStart01[(size_t) selectedPad] = (float) v;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadStart (selectedPad, (int) (v * len));
        waveform.setTrim ((float) v, padEnd01[(size_t) selectedPad]);
        refreshPadArt (selectedPad);
        refreshWaveformSegments();
        repaint (editInfoArea.expanded (4));
    };
    endSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmax (endSlider.getValue(), startSlider.getValue() + 0.01);
        padEnd01[(size_t) selectedPad] = (float) v;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadEnd (selectedPad, (int) (v * len));
        waveform.setTrim (padStart01[(size_t) selectedPad], (float) v);
        refreshPadArt (selectedPad);
        refreshWaveformSegments();
        repaint (editInfoArea.expanded (4));
    };

    reverseButton.setClickingTogglesState (true);
    styleButton (reverseButton, kStepOff);
    reverseButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    reverseButton.onClick = [this] { if (selectedPad >= 0) { padReverse[(size_t) selectedPad] = reverseButton.getToggleState(); engine.setPadReverse (selectedPad, reverseButton.getToggleState()); } };
    addAndMakeVisible (reverseButton);

    loopButton.setClickingTogglesState (true);
    styleButton (loopButton, kStepOff);
    loopButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    loopButton.onClick = [this] { if (selectedPad >= 0) { padLoop[(size_t) selectedPad] = loopButton.getToggleState(); engine.setPadLoop (selectedPad, loopButton.getToggleState()); } };
    addAndMakeVisible (loopButton);

    //  AUTOCUT — the pad cuts itself. Off by default, because layering a pad
    //  over its own tail is what this sampler has always done and some pads
    //  want it; on, a second tap kills the first with a 1.5 ms declick, which
    //  is how a hardware one-shot behaves and what keeps a stab from turning
    //  into a chorus of itself when you play it fast.
    autocutButton.setClickingTogglesState (true);
    styleButton (autocutButton, kStepOff);
    autocutButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    autocutButton.onClick = [this]
    {
        if (selectedPad < 0) return;
        padSelfCut[(size_t) selectedPad] = autocutButton.getToggleState();
        engine.setPadSelfCut (selectedPad, autocutButton.getToggleState());
    };
    addAndMakeVisible (autocutButton);

    styleButton (chopButton, kKey);
    chopButton.onClick = [this] { openChopSheet(); };
    addAndMakeVisible (chopButton);

    // Pattern bank selector (drives what the step grid shows/edits).
    patternSlider.setSliderStyle (juce::Slider::IncDecButtons);
    patternSlider.setRange (0.0, (double) (kNumPatterns - 1), 1.0);
    patternSlider.setValue (0.0, juce::dontSendNotification);
    patternSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    patternSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    patternSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
    patternSlider.textFromValueFunction = [] (double v) { return "P" + juce::String ((int) v + 1); };
    patternSlider.updateText();   // refresh textbox with the new formatter
    patternSlider.onValueChange = [this]
    {
        selectedPattern = (int) patternSlider.getValue();
        engine.setEditPattern (selectedPattern);
        selectedStep = -1;
        noteSlider.setValue (0.0, juce::dontSendNotification);
        lengthSlider.setValue (engine.getPatternLength (selectedPattern), juce::dontSendNotification);
        selectedBar = 0;
        resized();
        refreshStepGrid();
        seqSheet.repaint();   // sheet card itself can grow/shrink with the bank's LEN
    };
    seqSheet.addAndMakeVisible (patternSlider);

    // Pattern length (FL-Studio-style fader): how many steps this bank plays
    // before looping / handing off to the next chain entry — 16 up to 64,
    // one row of 8 at a time. Changing it reflows the step grid itself
    // (more/fewer rows), so it forces a full resized(), not just a repaint.
    lengthSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    lengthSlider.setRange ((double) kMinPatLen, (double) kMaxPatLen, (double) kStepCols);   // whole bars
    lengthSlider.setValue ((double) kMinPatLen, juce::dontSendNotification);
    lengthSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    lengthSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    lengthSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lengthSlider.setColour (juce::Slider::trackColourId, ZatiColours::accent);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, 22);
    lengthSlider.textFromValueFunction = [] (double v) { return T ("%1 pasos", juce::String ((int) v)); };
    lengthSlider.updateText();
    lengthSlider.onValueChange = [this]
    {
        engine.setPatternLength (selectedPattern, (int) lengthSlider.getValue());
        resized();
        refreshStepGrid();
        seqSheet.repaint();   // sheet card grows/shrinks with LEN
    };
    seqSheet.addAndMakeVisible (lengthSlider);

    // Chain include row: 8 coloured toggles, one per pattern bank — tap to
    // put that bank in (or out of) the played sequence. 0 active = fall back
    // to just looping whichever bank is being edited (unchanged behaviour).
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = new juce::TextButton (juce::String (i + 1));
        styleButton (*b, kKey);
        b->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));
        b->setClickingTogglesState (true);
        b->onClick = [this, i]
        {
            patternActiveUI[(size_t) i] = patternButtons[i]->getToggleState();
            rebuildChain();
        };
        seqSheet.addAndMakeVisible (b);
        patternButtons.add (b);
    }

    styleButton (chainClearButton, kKey);
    chainClearButton.onClick = [this]
    {
        patternActiveUI.fill (false);
        for (auto* b : patternButtons) b->setToggleState (false, juce::dontSendNotification);
        rebuildChain();
    };
    seqSheet.addAndMakeVisible (chainClearButton);

    // Piano roll: per-step semitone offset for the selected pad (tap a step
    // to select it, then dial its pitch here — melodies from one sample).
    noteSlider.setSliderStyle (juce::Slider::IncDecButtons);
    noteSlider.setRange (-24.0, 24.0, 1.0);
    noteSlider.setValue (0.0, juce::dontSendNotification);
    noteSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    noteSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    noteSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 22);
    noteSlider.textFromValueFunction = [] (double v) { return (v > 0 ? juce::String ("+") : juce::String()) + juce::String ((int) v) + " st"; };
    noteSlider.updateText();   // refresh textbox with the new formatter
    noteSlider.onValueChange = [this]
    {
        if (selectedPad >= 0 && selectedStep >= 0)
            engine.setStepNote (selectedPattern, selectedStep, selectedPad, (int) noteSlider.getValue());

        refreshStepGrid();
    };
    seqSheet.addAndMakeVisible (noteSlider);

    // The six effects. Each row of the fxDefs table is one effect: its face
    // label, the three names CTRL 1-3 take when it holds the knobs, the range
    // and format of each, and the MIX it wakes up with. MIX is always the
    // third parameter and it is also the on/off switch — the engine skips a
    // stage whose mix is zero, so "off" and "inaudible" cannot disagree.
    //
    // These sliders are never parented to anything. They are where a value
    // LIVES; the three CTRL knobs are just the window onto whichever effect
    // currently has focus. One value, one owner — which is what the old four
    // re-assignable slots plus three bank chips could never manage.
    for (int f = 0; f < kNumFx; ++f)
        for (int pi = 0; pi < 3; ++pi)
        {
            const auto& sp = fxDefs[f].spec[pi];
            auto* sl = new juce::Slider (juce::Slider::RotaryVerticalDrag, juce::Slider::NoTextBox);
            sl->setRange (sp.lo, sp.hi, sp.step);
            if (sp.skewMid > 0.0) sl->setSkewFactorFromMidPoint (sp.skewMid);
            sl->setValue (sp.def, juce::dontSendNotification);
            sl->onValueChange = [this, f, pi] { pushFxParam (f, pi); };
            fxParams.add (sl);
        }

    // CTRL 1-3: context-sensitive macro knobs. Which parameters they touch
    // depends on the active bank (FILTRO / DELAY / PAD) — groovebox style,
    // three big knobs that are always the three most useful ones.
    initKnob (macroCtrl1, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (0); });
    initKnob (macroCtrl2, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (1); });
    initKnob (macroCtrl3, 0.0, 1.0, 0.001, 0.0, 0.0, [this] { macroMoved (2); });

    {
        juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        for (int i = 0; i < 3; ++i)
        {
            ks[i]->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);   // the readout row measures
            ks[i]->onDragStart = [this, i] { setMacroTouched (i, true); };
            ks[i]->onDragEnd   = [this, i] { setMacroTouched (i, false); };
        }
    }

    // Six effects, six buttons, one row. A button IS its effect: tapping it
    // hands the three CTRL knobs that effect's three parameters, tapping the
    // one that already has them switches it off. No slots to re-assign, no
    // bank chips above the knobs — those were three ways to reach one delay,
    // which is how there came to be two of them.
    {
        for (int f = 0; f < kNumFx; ++f)
        {
            auto* b = new HoldButton (fxDefs[f].name);
            styleButton (*b, kKey);
            b->setColour (juce::TextButton::buttonOnColourId, kAccent);
            b->onClick = [this, f] { fxTapped (f); };
            //  Hold to take the knobs without touching the switch: the only
            //  way to tune an effect that is already running now that a tap
            //  always means on/off.
            b->onHold  = [this, f] { fxFocusOnly (f); };
            addAndMakeVisible (b);
            fxButtons.add (b);
        }
    }

    //  Dragging the hero's handles is the same edit as the START/END faders in
    //  the PADS sheet — one model, two ways in.
    waveform.onTrimDragged = [this] (float s, float e)
    {
        if (selectedPad < 0) return;
        padStart01[(size_t) selectedPad] = s;
        padEnd01[(size_t) selectedPad]   = e;
        const int len = engine.getSampleLength (selectedPad);
        engine.setPadStart (selectedPad, (int) (s * len));
        engine.setPadEnd   (selectedPad, (int) (e * len));
        startSlider.setValue (s, juce::dontSendNotification);
        endSlider.setValue   (e, juce::dontSendNotification);
        refreshPadArt (selectedPad);
        refreshWaveformSegments();
        if (padSheet.isVisible()) padSheet.repaint();
    };

    //  Tap the wave, hear the wave. On a chopped source the fragment under the
    //  finger belongs to a particular pad, and that is the pad that speaks -
    //  otherwise auditioning the fifth slice would play the first one through
    //  the selected pad's settings, which is a different sound entirely.
    waveform.onAudition = [this] (float t)
    {
        int pad = selectedPad;
        if (pad < 0) return;

        if (auto src = uiSample[(size_t) pad])
            for (int i = 0; i < kNumPads; ++i)
                if (uiSample[(size_t) i] == src
                    && t >= padStart01[(size_t) i] && t < padEnd01[(size_t) i])
                {
                    pad = i;
                    break;
                }

        engine.postNoteOnFrom (pad, t);
    };

    //  MIX: one strip per pad — level, mute, solo. Mute and solo reach voices
    //  that are already sounding, so they work as performance controls too.
    for (int i = 0; i < kNumPads; ++i)
    {
        auto* f = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        f->setRange (0.0, 1.0, 0.01);
        f->setValue (padGain[(size_t) i], juce::dontSendNotification);
        f->setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        f->setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        f->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        f->setColour (juce::Slider::trackColourId, Zati::colour (i));
        f->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 20);
        //  A tap must not become a value. Snapping to the touch point turns a
        //  brushed finger into a channel slammed to zero; relative dragging
        //  means you take hold of the level and move it from where it was.
        f->setSliderSnapsToMousePosition (false);
        f->textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)); };
        f->onValueChange = [this, i, f]
        {
            padGain[(size_t) i] = (float) f->getValue();
            engine.setPadGain (i, (float) f->getValue());
            if (i == selectedPad) volSlider.setValue (f->getValue(), juce::dontSendNotification);
        };
        mixSheet.addAndMakeVisible (f);
        mixFaders.add (f);

        //  Pan on the strip, next to the level it belongs to. Placing a sound
        //  is half of mixing and it was only reachable one pad at a time, in
        //  another sheet - which is the wrong place to decide where things sit
        //  relative to each other. No number: the thumb against its centre
        //  tick says it, a double tap puts it back, and the PADS knob still
        //  gives the exact figure when you want one.
        auto* p = new juce::Slider();
        p->setSliderStyle (juce::Slider::LinearHorizontal);
        p->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        p->setRange (-1.0, 1.0, 0.01);
        p->setValue (padPan[(size_t) i], juce::dontSendNotification);
        p->setDoubleClickReturnValue (true, 0.0);
        p->setColour (juce::Slider::trackColourId, ZatiColours::inkDim.withAlpha (0.55f));
        p->getProperties().set ("pan", true);
        p->setSliderSnapsToMousePosition (false);
        p->onValueChange = [this, i, p]
        {
            padPan[(size_t) i] = (float) p->getValue();
            engine.setPadPan (i, (float) p->getValue());
            if (i == selectedPad) panSlider.setValue (p->getValue(), juce::dontSendNotification);
        };
        mixSheet.addAndMakeVisible (p);
        mixPans.add (p);

        auto* m = new juce::TextButton ("M");
        styleButton (*m, kStepOff);
        m->setColour (juce::TextButton::buttonOnColourId, ZatiColours::red);
        m->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        m->setClickingTogglesState (true);
        m->onClick = [this, i, m] { engine.setPadMute (i, m->getToggleState()); refreshMixStrip(); };
        mixSheet.addAndMakeVisible (m);
        mixMutes.add (m);

        auto* so = new juce::TextButton ("S");
        styleButton (*so, kStepOff);
        so->setColour (juce::TextButton::buttonOnColourId, ZatiColours::yellow);
        so->setClickingTogglesState (true);
        so->onClick = [this, i, so] { engine.setPadSolo (i, so->getToggleState()); refreshMixStrip(); };
        mixSheet.addAndMakeVisible (so);
        mixSolos.add (so);
    }
    styleButton (mixClearSolo, kKey);
    mixClearSolo.onClick = [this] { engine.clearSolo(); refreshMixStrip(); };
    mixSheet.addAndMakeVisible (mixClearSolo);

    styleButton (mixCloseButton, kKey);
    mixCloseButton.onClick = [this] { closeAllSheets(); };
    mixSheet.addAndMakeVisible (mixCloseButton);
    addAndMakeVisible (mixSheet);
    mixSheet.setVisible (false);
    mixSheet.onDismiss = [this] { closeAllSheets(); };
    mixSheet.paintContent = [this] (juce::Graphics& g) { paintMixSheetContent (g); };

    styleButton (mixButton, kKey);
    mixButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    mixButton.onClick = [this]
    {
        if (mixSheet.isVisible()) { closeAllSheets(); return; }
        for (int i = 0; i < kNumPads; ++i)
        {
            if (mixFaders[i] != nullptr) mixFaders[i]->setValue (padGain[(size_t) i], juce::dontSendNotification);
            if (mixPans[i]   != nullptr) mixPans[i]  ->setValue (padPan[(size_t) i],  juce::dontSendNotification);
        }
        openSheet (mixSheet, mixButton);
        refreshMixStrip();
    };
    addAndMakeVisible (mixButton);

    //  SONG: the arrangement. Pick a block from the palette, tap a bar to
    //  place it, tap it again to clear.
    for (int i = 0; i < kNumPatterns; ++i)
    {
        auto* b = new juce::TextButton ("P" + juce::String (i + 1));
        styleButton (*b, kStepOff);
        b->setColour (juce::TextButton::buttonOnColourId, Zati::colour (i));
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { songBrush = i + 1; refreshSong(); };
        songSheet.addAndMakeVisible (b);
        songPatBtns.add (b);
    }
    songPatBtns[0]->setToggleState (true, juce::dontSendNotification);

    styleButton (songPadModeBtn, kStepOff);
    songPadModeBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    songPadModeBtn.setClickingTogglesState (true);
    songPadModeBtn.onClick = [this]
    {
        // The selected pad becomes the brush: a one-shot dropped on a bar.
        songBrush = songPadModeBtn.getToggleState() ? -(juce::jmax (0, selectedPad) + 1) : 1;
        refreshSong();
    };
    songSheet.addAndMakeVisible (songPadModeBtn);

    styleButton (songClearBtn, kStepOff);
    songClearBtn.setColour (juce::TextButton::buttonOnColourId, ZatiColours::red);
    songClearBtn.setClickingTogglesState (true);
    songClearBtn.onClick = [this] { songBrush = songClearBtn.getToggleState() ? 0 : 1; refreshSong(); };
    songSheet.addAndMakeVisible (songClearBtn);

    styleButton (songModeBtn, kStepOff);
    songModeBtn.setColour (juce::TextButton::buttonOnColourId, kAccent);
    songModeBtn.setClickingTogglesState (true);
    songModeBtn.onClick = [this]
    {
        engine.setSongMode (songModeBtn.getToggleState());
        status.setText (songModeBtn.getToggleState() ? T ("PLAY toca la cancion")
                                                     : T ("PLAY toca el patron / la cadena"),
                        juce::dontSendNotification);
    };
    songSheet.addAndMakeVisible (songModeBtn);

    songLenSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    songLenSlider.setRange (1.0, (double) AudioEngine::kSongBars, 1.0);
    songLenSlider.setValue (8.0, juce::dontSendNotification);
    songLenSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    songLenSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    songLenSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    songLenSlider.setColour (juce::Slider::trackColourId, kAccent);
    songLenSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, Metrics::chip);
    songLenSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + " comp"; };
    songLenSlider.updateText();
    songLenSlider.onValueChange = [this]
    {
        engine.setSongLength ((int) songLenSlider.getValue());
        resized(); refreshSong();
    };
    songSheet.addAndMakeVisible (songLenSlider);

    for (int i = 0; i < AudioEngine::kSongBars / Playlist::kBarsView; ++i)
    {
        auto* b = new juce::TextButton (juce::String (i * Playlist::kBarsView + 1));
        styleButton (*b, kStepOff);
        b->setColour (juce::TextButton::buttonOnColourId, kAccent);
        b->setClickingTogglesState (true);
        b->onClick = [this, i]
        {
            songPage = i;
            for (int k = 0; k < songPageBtns.size(); ++k)
                songPageBtns[k]->setToggleState (k == i, juce::dontSendNotification);
            refreshSong();
        };
        songSheet.addAndMakeVisible (b);
        songPageBtns.add (b);
    }
    songPageBtns[0]->setToggleState (true, juce::dontSendNotification);

    songGrid.onCell = [this] (int lane, int bar)
    {
        // Placing a pattern claims as many bars as its length needs; the tail
        // bars are marked as continuation so the block reads as one thing.
        if (songBrush == 0 || engine.getSongCell (lane, bar) != 0)
        {
            // Clear this block, tail included.
            int start = bar;
            while (start > 0 && engine.getSongCell (lane, start) == AudioEngine::kContinued) --start;
            engine.setSongCell (lane, start, 0);
            for (int b = start + 1; b < engine.getSongLength(); ++b)
            {
                if (engine.getSongCell (lane, b) != AudioEngine::kContinued) break;
                engine.setSongCell (lane, b, 0);
            }
        }
        else if (songBrush > 0)
        {
            const int bank = songBrush - 1;
            const int bars = juce::jmax (1, (engine.getPatternLength (bank) + AudioEngine::kBarSteps - 1) / AudioEngine::kBarSteps);
            engine.setSongCell (lane, bar, songBrush);
            for (int b = bar + 1; b < bar + bars && b < engine.getSongLength(); ++b)
                engine.setSongCell (lane, b, AudioEngine::kContinued);
        }
        else
        {
            engine.setSongCell (lane, bar, songBrush);      // one-shot
        }
        refreshSong();
    };
    songSheet.addAndMakeVisible (songGrid);

    styleButton (songCloseButton, kKey);
    songCloseButton.onClick = [this] { closeAllSheets(); };
    songSheet.addAndMakeVisible (songCloseButton);
    addAndMakeVisible (songSheet);
    songSheet.setVisible (false);
    songSheet.onDismiss = [this] { closeAllSheets(); };
    songSheet.paintContent = [this] (juce::Graphics& g) { paintSongSheetContent (g); };

    styleButton (songButton, kKey);
    songButton.setColour (juce::TextButton::buttonOnColourId, kAccent);
    songButton.onClick = [this]
    {
        if (songSheet.isVisible()) { closeAllSheets(); return; }
        openSheet (songSheet, songButton);
        refreshSong();
    };
    addAndMakeVisible (songButton);

    //  The screen is the MASTER, not the selected pad. Trimming already has
    //  a whole popup of its own, so putting the same waveform and the same
    //  trim handles on the face was one job done twice — and it meant the
    //  biggest element on the instrument showed a sample sitting still
    //  instead of the sound actually coming out.
    addAndMakeVisible (spectrum);
    padSheet.addAndMakeVisible (waveform);

    //  There is no skin picker. ZATI has one look; a strip of alternative
    //  accents sitting on top of the project menu was a preference masquerading
    //  as a feature, and it stole the first line of a sheet that exists to
    //  manage work. Projects saved with another skin still load — the stored
    //  value is applied, it just cannot be changed from here.

    // Controls live inside their sheets, not on the machine face.
    for (juce::Component* c : { (juce::Component*) &pitchSlider, (juce::Component*) &fineSlider,
                                (juce::Component*) &volSlider, (juce::Component*) &panSlider,
                                (juce::Component*) &attackSlider, (juce::Component*) &releaseSlider, (juce::Component*) &chokeSlider,
                                (juce::Component*) &startSlider, (juce::Component*) &endSlider,
                                (juce::Component*) &reverseButton, (juce::Component*) &loopButton,
                                (juce::Component*) &autocutButton })
        padSheet.addAndMakeVisible (c);
    padSheet.addAndMakeVisible (chopButton);

    //  Play what is on screen. It sits on the sheet's own title row rather
    //  than in a row of its own, because the one thing this sheet is short of
    //  is height, and a transport button is not worth a fader's worth of it.
    styleButton (previewButton, kKey);
    previewButton.onClick = [this]
    {
        if (selectedPad < 0) return;

        //  Sounding: stop it. A loop with no way back off is the reason this
        //  is a toggle rather than a re-trigger.
        if (engine.getPadPosition01 (selectedPad) >= 0.0f)
            engine.postNoteOff (selectedPad);
        else
            engine.postNoteOn (selectedPad);
    };
    padSheet.addAndMakeVisible (previewButton);

    styleButton (undoButton, ZatiColours::red);
    undoButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    undoButton.onClick = [this] { performUndo(); };
    //  addChildComponent, not addAndMakeVisible: the latter turns the child
    //  visible, which is how DESHACER came to sit on the face from launch
    //  offering to undo something that had not happened yet.
    addChildComponent (undoButton);

    styleButton (redoButton, ZatiColours::key);
    redoButton.onClick = [this] { performRedo(); };
    addChildComponent (redoButton);

    status.setJustificationType (juce::Justification::centred);
    status.setColour (juce::Label::textColourId, ZatiColours::inkDim);
    status.setText (T ("Toca un pad para sonar"), juce::dontSendNotification);
    addAndMakeVisible (status);

    // Every parameter reaches the engine once, so the DSP and the knobs agree
    // before anything is touched.
    for (int f = 0; f < kNumFx; ++f)
        for (int pi = 0; pi < 3; ++pi)
            pushFxParam (f, pi);

    //  Accessible names.
    //
    //  A TextButton already announces its own caption, so the buttons were
    //  fine. Sliders are not: JUCE has no text to fall back on and every knob
    //  in the app came out as an anonymous "Slider", which makes the whole
    //  thing unusable with TalkBack on. The pads are named in refreshPadArt,
    //  where the sample name is known; these are the rest.
    struct Named { juce::Slider& s; const char* title; const char* what; };
    for (auto& n : { Named { pitchSlider,   "Tono",      "semitonos" },
                     Named { fineSlider,    "Afinado",   "centesimas" },
                     Named { volSlider,     "Volumen",   "del pad" },
                     Named { panSlider,     "Paneo",     "del pad" },
                     Named { attackSlider,  "Ataque",    "milisegundos" },
                     Named { releaseSlider, "Caida",     "milisegundos" },
                     Named { startSlider,   "Inicio",    "recorte" },
                     Named { endSlider,     "Fin",       "recorte" },
                     Named { chokeSlider,   "Choke",     "grupo de corte" },
                     Named { bpmSlider,     "Tempo",     "pulsos por minuto" },
                     Named { patternSlider, "Patron",    "del secuenciador" },
                     Named { noteSlider,    "Nota",      "del paso" },
                     Named { lengthSlider,  "Compases",  "del patron" },
                     Named { macroCtrl1,    "Control 1", "del efecto" },
                     Named { macroCtrl2,    "Control 2", "del efecto" },
                     Named { macroCtrl3,    "Control 3", "del efecto" } })
    {
        n.s.setTitle (n.title);
        n.s.setDescription (n.what);
    }

    //  The mixer builds its strips per pad, so they get named where they are
    //  made - but the channel number is the whole point of the name.
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto ch = " canal " + juce::String (i + 1);
        if (auto* f = mixFaders[i]) { f->setTitle ("Volumen" + ch); f->setDescription ("del mezclador"); }
        if (auto* p = mixPans[i])   { p->setTitle ("Paneo"   + ch); p->setDescription ("del mezclador"); }
        if (auto* m = mixMutes[i])  { m->setTitle ("Silencio" + ch); }
        if (auto* s = mixSolos[i])  { s->setTitle ("Solo"    + ch); }
    }

    //  Captions last: every button above was built with whatever text its
    //  declaration carried, and this is what makes them say it in the user's
    //  language. Lang itself was loaded before the window existed (Main.cpp).
    retranslateUi();

    //  Before the first layout: on Android 15 the window is the whole screen
    //  and the bars are drawn over it.
    systemInsets = SystemInsets::get();

    //  What this particular phone can carry. Everything that costs CPU or
    //  memory is read from here rather than from a constant written on the
    //  machine the app was developed on: the size of the voice pool, how often
    //  the interface redraws, how much of the master goes into the scope, how
    //  long a mic take may be, and whether pad tiles draw their waveform.
    {
        const auto& dev = DeviceTier::profile();
        engine.setPolyphony  (dev.voices, dev.voicesPerPad);
        engine.setRecordLimit (dev.recordSeconds, dev.recordStereo);
        for (auto* p : pads) if (p != nullptr) p->setArtEnabled (dev.padWaveformArt);
        startTimer (dev.uiIntervalMs);
    }
    setSize (500, 1080);
    focusFx (0);
    applySkin();
}

// Restyle everything that captured accent-coloured values at construction —
// the rest of the UI reads ZatiColours at paint time and only needs repaint.
void MainComponent::applySkin()
{
    const auto acc = ZatiColours::accent;
    // Lit-state text must stay legible on a dark accent (TINTA skin).
    const auto onTxt = acc.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink;

    juce::TextButton* accented[] = { &padsButton, &secButton, &loadButton };
    for (auto* b : accented)
    {
        b->setColour (juce::TextButton::buttonOnColourId, acc);
        b->setColour (juce::TextButton::textColourOnId, onTxt);
    }
    for (auto* b : fxButtons)
    {
        b->setColour (juce::TextButton::buttonOnColourId, acc);
        b->setColour (juce::TextButton::textColourOnId, onTxt);
    }
    for (int i = 0; i < patternButtons.size(); ++i)
        patternButtons[i]->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));

    styleButton (playButton, kKey);
    playButton.setColour (juce::TextButton::buttonOnColourId, acc);
    playButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::inkLight);

    juce::Slider* tracks[] = { &startSlider, &endSlider, &bpmSlider, &patternSlider, &lengthSlider };
    for (auto* s : tracks)
        s->setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::trackColourId, acc);
    lnf.setColour (juce::Slider::thumbColourId, acc);
    lnf.applyBrowserColours();                     // the file list follows the skin too
    styleButton (browseLoadButton, acc);
    browseLoadButton.setColour (juce::TextButton::textColourOffId, onTxt);

    repaint();
}

//  What the knob does, not what it is called.
//
//  At rest these read CTRL 1 / CTRL 2 / CTRL 3 and only named the parameter
//  while a finger was on them - so the one moment you could not see what you
//  were about to turn was before you turned it. The wedge over the FX row
//  already says WHICH effect owns them; this says WHAT each one moves.
juce::String MainComponent::macroBaseLabel (int idx) const
{
    return macroParamLabel (idx);
}

juce::String MainComponent::macroParamLabel (int idx) const
{
    return fxDefs[juce::jlimit (0, kNumFx - 1, focusedFx)].param[juce::jlimit (0, 2, idx)];
}

// The readout measures; it always carries a unit so the number means something
// on its own. Monospaced so digits do not shift as the value changes.
juce::String MainComponent::macroReadout (int idx) const
{
    const juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    const int p = juce::jlimit (0, 2, idx);
    return fxFormat (fxDefs[juce::jlimit (0, kNumFx - 1, focusedFx)].spec[p], ks[p]->getValue());
}

void MainComponent::setMacroTouched (int idx, bool touched)
{
    if (! juce::isPositiveAndBelow (idx, 3)) return;

    if (touched)
    {
        macroLabelTimer.stopTimer();
        macroTouched[(size_t) idx] = true;
    }
    else
    {
        // Hold the parameter name briefly after release: letting it snap back
        // the instant the finger lifts makes the name unreadable on a quick
        // tweak, which is when you most want to know what you just moved.
        macroLabelTimer.onFire = [this]
        {
            macroTouched.fill (false);
            repaint();
        };
        macroLabelTimer.startTimer (800);
    }
    repaint();
}

// Everything the UI knows about the six effects, in signal order. One table,
// so the wiring below can be read against it line for line.
const MainComponent::FxDef MainComponent::fxDefs[MainComponent::kNumFx] =
{
    { "ISO",  { "CUTOFF", "RESO", "MIX" },
      { {   20.0, 20000.0, 1.00, 1000.0,  1200.0, 0 },
        {    0.3,     4.0, 0.01,    0.0,   0.707, 1 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    { "HPF",  { "FREQ", "RESO", "MIX" },
      { {   20.0, 20000.0, 1.00,  400.0,   200.0, 0 },
        {    0.3,     4.0, 0.01,    0.0,   0.707, 1 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    { "DRV",  { "DRIVE", "TONE", "MIX" },
      { {    0.0,     1.0, 0.01,    0.0,    0.55, 2 },
        {  200.0, 20000.0, 1.00, 2000.0,  8000.0, 0 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.80 },

    { "DLY",  { "TIME", "FBK", "MIX" },
      { {   20.0,  1000.0, 1.00,    0.0,   250.0, 3 },
        {    0.0,    0.95, 0.01,    0.0,    0.35, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.35 },

    { "CRSH", { "BITS", "RATE", "MIX" },
      { {    1.0,    16.0, 1.00,    0.0,     8.0, 4 },
        {    1.0,    64.0, 1.00,    8.0,     4.0, 5 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.60 },

    { "REV",  { "SIZE", "DAMP", "MIX" },
      { {    0.0,     1.0, 0.01,    0.0,    0.55, 2 },
        {    0.0,     1.0, 0.01,    0.0,    0.45, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.30 },
};

// The readout always carries a unit, so a number means something on its own.
juce::String MainComponent::fxFormat (const FxDef::Spec& sp, double v)
{
    switch (sp.fmt)
    {
        case 0:  return v >= 1000.0 ? juce::String (v / 1000.0, 1) + " kHz"
                                    : juce::String ((int) v) + " Hz";
        case 1:  return "Q " + juce::String (v, 2);
        case 3:  return juce::String ((int) v) + " ms";
        case 4:  return juce::String ((int) v) + " bit";
        case 5:  return juce::String ((int) v) + "x";
        default: return juce::String (juce::roundToInt (v * 100.0)) + " %";
    }
}

// Slider -> engine, in the same order as the table above.
void MainComponent::pushFxParam (int f, int pi)
{
    if (! juce::isPositiveAndBelow (f, kNumFx) || ! juce::isPositiveAndBelow (pi, 3)) return;
    const float v = (float) fxParam (f, pi).getValue();

    switch (f * 3 + pi)
    {
        case  0: engine.setIsoCutoff (v); break;
        case  1: engine.setIsoReso   (v); break;
        case  2: engine.setIsoMix    (v); break;
        case  3: engine.setHpFreq    (v); break;
        case  4: engine.setHpReso    (v); break;
        case  5: engine.setHpMix     (v); break;
        case  6: engine.setFxDrive   (v); break;
        case  7: engine.setDrvTone   (v); break;
        case  8: engine.setDrvMix    (v); break;
        case  9: engine.setDlyTime   (v); break;
        case 10: engine.setDlyFb     (v); break;
        case 11: engine.setDlyMix    (v); break;
        case 12: engine.setCrushBits (v); break;
        case 13: engine.setCrushRate (v); break;
        case 14: engine.setCrushMix  (v); break;
        case 15: engine.setRevSize   (v); break;
        case 16: engine.setRevDamp   (v); break;
        case 17: engine.setRevMix    (v); break;
        default: break;
    }
}

// On/off is a MIX move, not a separate flag: one truth, and it is the same
// number the knob shows. Switching back on restores the effect's own default
// amount, so the button behaves like a switch rather than a fader you have to
// go and find again.
void MainComponent::setFxEnabled (int f, bool on)
{
    if (! juce::isPositiveAndBelow (f, kNumFx)) return;
    fxOn[(size_t) f] = on;
    fxButtons[f]->setToggleState (on, juce::dontSendNotification);
    fxParam (f, 2).setValue (on ? fxDefs[f].onMix : 0.0, juce::dontSendNotification);
    pushFxParam (f, 2);
    refreshMacroValues();
    status.setText (juce::String (fxDefs[f].name) + (on ? " ON" : " OFF"),
                    juce::dontSendNotification);
}

// Give an effect the three knobs: re-range them to its parameters and load its
// current values in silently.
void MainComponent::focusFx (int f)
{
    focusedFx = juce::jlimit (0, kNumFx - 1, f);

    juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    for (int pi = 0; pi < 3; ++pi)
    {
        const auto& sp = fxDefs[focusedFx].spec[pi];
        ks[pi]->setRange (sp.lo, sp.hi, sp.step);
        if (sp.skewMid > 0.0) ks[pi]->setSkewFactorFromMidPoint (sp.skewMid);
        else                  ks[pi]->setSkewFactor (1.0);
        ks[pi]->setDoubleClickReturnValue (true, sp.def);   // double-tap = this effect's default
    }
    refreshMacroValues();
    repaint();
}

// Tap once to take the knobs (switching the effect on if it was off); tap the
// one that already has them to switch it off.
//  One tap, one meaning: this effect goes on or off. It used to mean two
//  different things depending on which effect the knobs happened to be
//  pointing at - tapping an effect that was ON but not focused only moved the
//  knobs to it, so switching off the first of two effects took two taps and
//  the first one appeared to do nothing at all.
//
//  The knobs follow the tap, because you want to see what you just switched
//  on. To reach the knobs of an effect that is already running without
//  switching it off, hold the button.
void MainComponent::fxTapped (int f)
{
    if (! juce::isPositiveAndBelow (f, kNumFx)) return;

    setFxEnabled (f, ! fxOn[(size_t) f]);
    focusFx (f);
    repaint();
}

void MainComponent::fxFocusOnly (int f)
{
    if (! juce::isPositiveAndBelow (f, kNumFx)) return;

    focusFx (f);
    //  Say so: a gesture nobody can see needs to announce what it did, or the
    //  hold reads as a tap that failed.
    status.setText (T ("CTRL -> %1", fxDefs[f].name), juce::dontSendNotification);
    repaint();
}

// --- CTRL 1-3 ------------------------------------------------------------
// The three knobs are a window onto the focused effect's three parameters.
// They own nothing: every move writes straight through to the parameter that
// holds the value, and every read comes back from it.

void MainComponent::refreshMacroValues()
{
    juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    for (int pi = 0; pi < 3; ++pi)
    {
        ks[pi]->setValue (fxParam (focusedFx, pi).getValue(), juce::dontSendNotification);
        ks[pi]->updateText();
    }
    repaint();
}

void MainComponent::macroMoved (int idx)
{
    if (! juce::isPositiveAndBelow (idx, 3)) return;
    juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    fxParam (focusedFx, idx).setValue (ks[idx]->getValue(), juce::dontSendNotification);
    pushFxParam (focusedFx, idx);

    // Moving MIX off zero (or onto it) IS switching the effect on or off —
    // the button has to agree with the knob, or you get a lit button over a
    // silent effect.
    if (idx == 2)
    {
        const bool on = ks[2]->getValue() > 0.001;
        if (on != fxOn[(size_t) focusedFx])
        {
            fxOn[(size_t) focusedFx] = on;
            fxButtons[focusedFx]->setToggleState (on, juce::dontSendNotification);
        }
    }
    //  Only the knob strip, not the whole face: a full repaint during a drag
    //  redrew sixteen pad tiles and their waveform art on every mouse move.
    repaint (macroCtrl1.getBounds().getUnion (macroCtrl3.getBounds())
                                   .expanded (12, 26));
}

// --- Sheets ------------------------------------------------------------------
void MainComponent::openSheet (Sheet& s, juce::TextButton& toggle)
{
    closeAllSheets();
    if (selectedPad < 0) selectPad (0);
    toggle.setToggleState (true, juce::dontSendNotification);
    s.setVisible (true);
    s.toFront (false);
    resized();
    repaint();
}

void MainComponent::closeAllSheets()
{
    disarmConfirm();   // an armed button must not survive its own sheet closing

    juce::TextButton* mb[4] = { &padsButton, &secButton, &mixButton, &songButton };
    Sheet*            sh[4] = { &padSheet, &seqSheet, &mixSheet, &songSheet };
    for (int i = 0; i < 4; ++i)
    {
        mb[i]->setToggleState (false, juce::dontSendNotification);
        sh[i]->setVisible (false);
    }
    browseSheet.setVisible (false);
    projSheet.setVisible (false);
    exportSheet.setVisible (false);
    rackSheet.setVisible (false);
    chopSheet.setVisible (false);
    setButton.setToggleState (false, juce::dontSendNotification);
    repaint();
}

MainComponent::~MainComponent()
{
    // The bounce thread holds a reference to the engine and to the pad
    // buffers, so it must be gone before either can be.
    if (exportJob != nullptr) { exportJob->signalThreadShouldExit(); exportJob.reset(); }

    //  A clean exit is still an exit: leave the session where the next launch
    //  will find it.
    autosave();
    session.flush (2000);

    shutdownAudio();
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
//  Audio callbacks
// ---------------------------------------------------------------------------

void MainComponent::prepareToPlay (int samplesPerBlockExpected, double sampleRate)
{
    //  How many input channels the device actually gave us. The record buffer
    //  is sized from it, and this is the only moment it can be: JUCE calls
    //  this before the stream starts, so no callback is inside the buffer.
    int ins = 0;
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        ins = dev->getActiveInputChannels().countNumberOfSetBits();

    engine.prepareToPlay (sampleRate, samplesPerBlockExpected, ins);
    enginePreparedRate  = sampleRate;
    enginePreparedBlock = samplesPerBlockExpected;
    // A bounce renders at the device's own rate, so the file sounds exactly
    // like what came out of the speaker — no resampling in between.
    deviceSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
}

void MainComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    engine.renderNextBlock (*info.buffer, info.startSample, info.numSamples);
}

void MainComponent::releaseResources()
{
    engine.releaseResources();
}

// ---------------------------------------------------------------------------
//  UI
// ---------------------------------------------------------------------------

//  THE BAND A LABEL LIVES IN.
//
//  Every caption on this face names the thing directly under it, and the
//  layout always reserves a strip for it - placeKnobRow takes 16, the SEC
//  rows take 14 + kTextPad, and so on. What kept going wrong is that the
//  PAINTING then ignored that strip and used its own offset instead: draw
//  twelve pixels of text fifteen above the control and you get three
//  pixels of air over the word and one under it, every time, everywhere.
//
//  So the band is stated once, as a rectangle, and the text is centred in
//  it. Symmetric by construction rather than by arithmetic that has to be
//  redone correctly at each of the places that needs it.
static juce::Rectangle<int> bandAbove (const juce::Component& c, int bandH,
                                       int gapToTop, int sideBleed = 0)
{
    return { c.getX() - sideBleed,
             c.getY() - gapToTop - bandH,
             c.getWidth() + 2 * sideBleed,
             bandH };
}

void MainComponent::paint (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // 1. Full-bleed light chassis (edge to edge — the whole screen is the face).
    g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, full.getCentreX(), full.getY(),
                                             ZatiColours::chassisBot, full.getCentreX(), full.getBottom(), false));
    g.fillRect (full);

    //  1b. Structure. A white field with rows of caps on it is a list of
    //  buttons; an instrument has plates, seams and engraved lettering, and
    //  all three are drawn with lines rather than shaded. These cost no
    //  layout height at all - the seams live in gaps that already existed and
    //  the plate is drawn behind controls that were already positioned.
    auto rule = [&g] (float x1, float x2, float y, float alpha)
    {
        g.setColour (ZatiColours::ink.withAlpha (alpha));
        g.fillRect (x1, y, x2 - x1, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.9f));      // the engraved highlight
        g.fillRect (x1, y + 1.0f, x2 - x1, 1.0f);
    };

    //  A label that rides its seam, the way silkscreen does on hardware: the
    //  line breaks for the word instead of running behind it.
    //
    //  Centred, with the rule coming in from BOTH edges to meet it. Hung off
    //  the left it read as a caption sitting on top of a line; brought to the
    //  middle with the line arriving from either side it reads as one piece of
    //  lettering that the seam was engraved around - which is what it is, and
    //  what the three of them together are supposed to say about the face.
    //  ...and centred in the seam, not hung from the top of the section below
    //  it. Placing the word a fixed six pixels over the zone put every bit of
    //  the seam's slack ABOVE the lettering and none under it, so the label
    //  read as glued to the plate beneath rather than as sitting in its own
    //  band. It takes the two edges of the gap and puts itself in the middle
    //  of them, so the air is the same above and below whatever the seam is
    //  worth on this screen.
    auto engraveIn = [&g, &full, &rule] (const juce::String& text, int seamTop, int zoneTop)
    {
        auto engrave = [&g, &rule, &full] (const juce::String& t, float y)
        {
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.30f));
        const float tw  = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t);
        const float cx  = full.getCentreX();
        const float x0  = cx - tw * 0.5f;
        const float gap = 9.0f;                       // air the rule leaves around the word

        rule (full.getX() + 10.0f, x0 - gap, y, 0.16f);
        rule (x0 + tw + gap, full.getRight() - 10.0f, y, 0.16f);

        g.setColour (ZatiColours::ink.withAlpha (0.42f));
        g.drawText (t, (int) x0 - 1, (int) (y - 5.0f), (int) tw + 3, 11,
                    juce::Justification::centred);
        };

        engrave (text, (float) (seamTop + zoneTop) * 0.5f);
    };

    // 2. The pad plate: the pads are bolted to a recessed panel, not floating
    //    on the face. One tone step down, one hairline, four screws.
    if (! padPlateArea.isEmpty())
    {
        //  The tonal step has to be big enough to see. At 3% the plate was
        //  technically there and read as a rendering artefact; the face needs
        //  three distinct values - chassis, plate, cap - or the whole thing
        //  stays white on white however many lines are drawn on it.
        auto pp = padPlateArea.toFloat();
        g.setColour (ZatiColours::chassisBot.darker (0.10f));
        g.fillRoundedRectangle (pp, 4.0f);
        g.setColour (ZatiColours::ink.withAlpha (0.30f));
        g.drawRoundedRectangle (pp.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.55f));      // lip catching the light
        g.drawRoundedRectangle (pp.reduced (1.6f), 4.0f, 1.0f);

    }

    //  The control plate: CTRL 1-3 and the six modules are one zone, and
    //  giving it its own plate is what turns the middle of the face from a
    //  white field with rows on it into a section of an instrument.
    if (! ctrlPlateArea.isEmpty())
    {
        auto cp = ctrlPlateArea.toFloat();
        g.setColour (ZatiColours::chassisBot.darker (0.10f));
        g.fillRoundedRectangle (cp, 4.0f);
        g.setColour (ZatiColours::ink.withAlpha (0.30f));
        g.drawRoundedRectangle (cp.reduced (0.5f), 4.0f, 1.0f);
        g.setColour (ZatiColours::white.withAlpha (0.55f));
        g.drawRoundedRectangle (cp.reduced (1.6f), 4.0f, 1.0f);

    }

    //  A plate, a name. EFECTOS used to be the only engraved word on the
    //  face, which made it look like a caption someone forgot to remove
    //  rather than like silkscreen: the other two plated zones - the knobs
    //  and the pads - had no title at all, and the eye reads one label among
    //  three unlabelled neighbours as an accident.
    //
    //  So the rule is now literal and it is the same for all three: every
    //  zone that sits on a PLATE gets its name engraved on the seam directly
    //  above it, left-aligned at the same inset, with the rule breaking for
    //  the word. Nothing that is not on a plate gets one - the LCD says what
    //  it is by being a screen, and the transport keys say it by being
    //  labelled LOAD, REC and PLAY. resized() reserves the seam height for
    //  these, so they can never land on the section above.
    if (! ctrlPlateArea.isEmpty())
        engraveIn (T ("CONTROL"), ctrlSeamTop, ctrlPlateArea.getY());

    if (! fxRowArea.isEmpty())
        engraveIn (T ("EFECTOS"), fxSeamTop, fxRowArea.getY());

    if (! padPlateArea.isEmpty())
        engraveIn (T ("PADS"), padSeamTop, padPlateArea.getY());

        //  Which of the six owns the three knobs. A tap both switches an
        //  effect and hands it the knobs, and until now only the switching
        //  showed - so with two effects on there was nothing on screen saying
        //  whose parameters CTRL 1-3 were holding. A wedge in the seam above
        //  the button, pointing from the knobs down at the effect they
        //  belong to.
        if (juce::isPositiveAndBelow (focusedFx, fxButtons.size()))
            if (auto* fb = fxButtons[focusedFx])
            {
                //  In the band BELOW the rule, which is now empty: the word
                //  moved to the middle of the seam and takes the rule's line
                //  with it, so the pixels between that line and the caps are
                //  free - and they are the right place for a pointer, because
                //  it is nearer the thing it points at than to the lettering.
                const float cx = (float) fb->getBounds().getCentreX();
                const float y  = (float) fb->getY() - 3.0f;
                juce::Path wedge;
                wedge.addTriangle (cx - 5.0f, y - 6.0f, cx + 5.0f, y - 6.0f, cx, y);
                g.setColour (ZatiColours::ink.withAlpha (0.75f));
                g.fillPath (wedge);
            }

    // 3. Recessed LCD bezel around the scope, with the screws that hold the
    //    window down. This is the one object on the face that should read as
    //    hardware rather than as a rectangle of dark paint.
    if (! screenBezel.isEmpty())
    {
        auto r = screenBezel.toFloat();
        //  A thinner bezel, now that nothing is bolted through it. The five
        //  pixels were there to give twelve screw heads somewhere to sit, and
        //  twelve screw heads on a phone are twelve dots of noise at the exact
        //  size where they stop reading as hardware and start reading as
        //  dirt. Three pixels of frame say the same thing and hand the other
        //  two back to the glass.
        g.setColour (ZatiColours::knobBody2);
        g.fillRoundedRectangle (r.expanded (3.0f), 3.0f);
        g.setColour (ZatiColours::knobEdge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.expanded (3.0f).reduced (0.5f), 3.0f, 1.2f);
    }

    // 3. Header: ZATI wordmark left, fragment strip right. No touch targets
    //    here — the strip is a readout, not a control: it is the state of the
    //    kit at a glance, one swatch per fragment colour, dimmed where no pad
    //    of that colour is loaded.
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ZatiColours::ink);
        g.setFont (ZatiColours::displayFont (Metrics::fTitle).withExtraKerningFactor (0.16f));
        g.drawText ("ZATI", h.getX(), h.getY(), 140, h.getHeight(), juce::Justification::centredLeft);

        rule ((float) h.getX(), (float) h.getRight(), (float) h.getBottom() + 2.0f, 0.22f);

        const int sw = 9, sh = 13, gap = 4;
        const int stripW = Zati::kNumColours * sw + (Zati::kNumColours - 1) * gap;
        int x = h.getRight() - stripW;
        const int y = h.getCentreY() - sh / 2;

        //  The band between the wordmark and the strip was empty across the
        //  whole width of the machine, while the one thing you cannot see
        //  anywhere on the face - which project is open - was buried three
        //  taps deep in PROJ. It goes here, on the baseline of the wordmark.
        {
            const int nameX = h.getX() + 138;
            const int nameW = (x - Metrics::md) - nameX;
            if (nameW > 40)
            {
                const bool named = currentProject.isNotEmpty();
                g.setColour (ZatiColours::ink.withAlpha (named ? 0.55f : 0.28f));
                g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
                g.drawText (named ? currentProject.toUpperCase() : T ("SIN GUARDAR"),
                            nameX, h.getY(), nameW, h.getHeight(),
                            juce::Justification::bottomLeft, true);
            }
        }

        for (int i = 0; i < Zati::kNumColours; ++i)
        {
            bool used = false;
            for (int p = 0; p < kNumPads && ! used; ++p)
                used = padHasSample[(size_t) p] && padZati[(size_t) p] == i;

            g.setColour (used ? Zati::colour (i) : Zati::colour (i).withAlpha (0.25f));
            g.fillRect (x, y, sw, sh);
            x += sw + gap;
        }
    }

    // 4. Machine face: CTRL labels (bank-dependent), VU strip, step LEDs.
    {
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
        juce::Slider* ks[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        for (int i = 0; i < 3; ++i)
        {
            auto r = ks[i]->getBounds();
            const bool touched = macroTouched[(size_t) i];

            // Label names, readout measures — never the other way round.
            g.setColour (touched ? ZatiColours::ink : ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::labelFont (touched ? 10.5f : 10.0f, 0.16f));
            g.drawText (touched ? macroParamLabel (i) : macroBaseLabel (i),
                        r.getX() - 8, r.getY() - ZatiLookAndFeel::kCtrlName + ZatiLookAndFeel::kTextPad,
                        r.getWidth() + 16, ZatiLookAndFeel::kCtrlName - 2 * ZatiLookAndFeel::kTextPad,
                        juce::Justification::centred);

            auto chip = juce::Rectangle<int> (r.getX() - 2, r.getBottom() + 2, r.getWidth() + 4,
                                              ZatiLookAndFeel::kCtrlChip - 4);
            g.setColour (ZatiColours::screenBg);
            g.fillRoundedRectangle (chip.toFloat(), 2.0f);
            g.setColour (touched ? ZatiColours::lcdFg : ZatiColours::lcdFg.withAlpha (0.8f));
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true));
            g.drawText (macroReadout (i), chip, juce::Justification::centred);
        }

    }
}

// FX sheet: knob labels + the live filter response display.
void MainComponent::paintPadSheetContent (juce::Graphics& g)
{
    if (padSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  The pad's name is a file name and files are named by whoever made
    //  them, so this line has no length it can count on. Stop it before the
    //  close button and let it shrink rather than run underneath.
    auto padTitleRow = padSheet.sheetBounds.reduced (14, 12).removeFromTop (16);
    padTitleRow.setRight (juce::jmin (padTitleRow.getRight(), previewButton.getX() - Metrics::xs));
    //  Ellipsised rather than squeezed: a name long enough to need shrinking
    //  is long enough that shrinking will not save it, and a sentence cut off
    //  mid-letter reads as a bug where "..." reads as a long name.
    g.drawText (T ("PAD %1", juce::String (sp + 1))
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase() : juce::String()),
                padTitleRow, juce::Justification::centredLeft, true);

    {
        //  Group headers, each with a hairline running out to the right edge -
        //  the same engraved rule the machine face uses, so a sheet reads as
        //  three blocks (what the sound is, where it is cut, what the pad is)
        //  instead of eleven controls in a column.
        static const char* secNames[3] = { "SONIDO", "RECORTE", "EL PAD" };
        for (int i = 0; i < 3; ++i)
        {
            const auto r = padSectionArea[(size_t) i];
            if (r.isEmpty()) continue;

            g.setColour (ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.22f));
            //  Sitting on the bottom edge of its band put the word straight
            //  onto the control under it. It keeps its own padding now, and
            //  the rule it rides moves with it.
            //  Centred in its band, not sunk to the bottom of it: trimming
            //  only the bottom left seven pixels of air over the word and
            //  three under it.
            const auto secText = T (secNames[i]);
            const auto textRow = r;
            g.drawText (secText, textRow, juce::Justification::centredLeft);

            const float tw = juce::GlyphArrangement::getStringWidth (
                                 ZatiColours::labelFont (Metrics::fMeta, 0.22f),
                                 secText);
            const float ly = (float) textRow.getCentreY() + 1.0f;
            g.setColour (ZatiColours::ink.withAlpha (0.18f));
            g.fillRect ((float) r.getX() + tw + 8.0f, ly,
                        juce::jmax (0.0f, (float) r.getRight() - ((float) r.getX() + tw + 8.0f)), 1.0f);
        }

        // Knobs: label above (same convention as FX).
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        //  placeKnobRow reserves 16 for the name and then insets the knob by
        //  2, so the band is the sixteen pixels that end two above the dial.
        auto name = [&g] (juce::Slider& s, const char* t)
        {
            g.drawText (T (t), bandAbove (s, 16, 2, 6), juce::Justification::centred);
        };
        name (pitchSlider, "PITCH"); name (fineSlider, "FINO"); name (volSlider, "VOLUME");
        name (panSlider, "PAN");
        name (attackSlider, "ATTACK"); name (releaseSlider, "RELEASE");
        name (chokeSlider, "CHOKE");

        //  Same band, one pixel lower: the third row insets its cells by 3.
        g.drawText (T ("MODO"), bandAbove (modeButton, 16, 3, 6), juce::Justification::centred);

        // Start/End stay linear (a trim range, not a knob): label to the left.
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.06f));
        auto lab = [&g] (juce::Slider& s, const char* t)
        {
            auto r = s.getBounds();
            g.drawText (T (t), r.getX() - 66, r.getY(), 60, r.getHeight(), juce::Justification::centredLeft);
        };
        lab (startSlider, "START"); lab (endSlider, "END");

        // ZATI row: the fragment colour this pad carries, named as well as
        // shown — the number and the name are the non-chromatic half.
        if (! zatiSwatchArea.isEmpty())
        {
            const int sel = padZati[(size_t) sp];
            const auto r  = zatiSwatchArea.toFloat();
            const float w = r.getWidth() / (float) Zati::kNumColours;

            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + (float) i * w, r.getY(),
                                                    w, r.getHeight()).reduced (2.0f, 0.0f);

                //  The one it carries is the only one at full strength and the
                //  only one wearing an outline. The other seven are there to
                //  be picked, not to be looked at.
                const bool on = (i == sel);
                g.setColour (Zati::colour (i).withAlpha (on ? 1.0f : 0.38f));
                g.fillRoundedRectangle (cell, 2.0f);

                if (on)
                {
                    g.setColour (ZatiColours::ink.withAlpha (0.75f));
                    g.drawRoundedRectangle (cell.reduced (0.5f), 2.0f, 1.4f);
                }
            }
        }

    }
}

// Sequencer sheet content: title/chain text + step-selection/playhead rings.
// Called from SeqOverlay::paint() (set as its paintContent callback) so it
// draws in the overlay's own paint pass, on top of everything else.
void MainComponent::paintSeqSheetContent (juce::Graphics& g)
{
    if (seqSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = seqSheet.sheetBounds.reduced (12, 6);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    const juce::String t = T ("PASOS") + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
                         + (padName[(size_t) sp].isNotEmpty() ? "   " + padName[(size_t) sp] : juce::String())
                         + "   " + dot + "   P" + juce::String (selectedPattern + 1);
    g.drawText (t, inner.removeFromTop (16), juce::Justification::centredLeft);

    //  The bank selector and the chain toggles used to sit adjacent, look
    //  identical and never say which does what. Now each row is named, and the
    //  chain shows its ACTUAL ORDER — "P1 P1 P2 P3" — instead of eight
    //  switches you have to decode.
    juce::String chainStr;
    if (engine.getChainLength() <= 0)
        chainStr = T ("sin cadena - repite P%1", juce::String (selectedPattern + 1));
    else
    {
        chainStr = T ("cadena: ");
        for (int i = 0; i < engine.getChainLength(); ++i)
            chainStr += "P" + juce::String (engine.getChainSlot (i) + 1) + (i + 1 < engine.getChainLength() ? " " : "");
        if (engine.isPlaying())
            chainStr += "   " + dot + "  " + T ("suena P%1", juce::String (engine.getPlayingPattern() + 1));
    }
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    g.drawText (chainStr, inner.removeFromTop (14), juce::Justification::centredLeft);

    //  Every control is named, over the control itself rather than over the
    //  row - two things sharing a line are two different jobs, and one label
    //  stretched across both was how NOTA came to look like part of the chain.
    {
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.20f));

        //  The SEC rows reserve 14 + kTextPad and then inset their controls
        //  by 2. Drawn thirteen pixels tall at -17 + kTextPad it ended up with
        //  five pixels over the word and a pixel of the word INSIDE the row.
        auto over = [&g] (const juce::Component* c, const juce::String& t)
        {
            if (c == nullptr) return;
            auto band = bandAbove (*c, 14 + ZatiLookAndFeel::kTextPad, 2);
            band.setWidth (juce::jmax (60, band.getWidth()));
            g.drawText (t, band.translated (2, 0), juce::Justification::centredLeft);
        };

        over (&patternSlider,      T ("PATRON"));
        over (&lengthSlider,       T ("LARGO"));
        over (patternButtons[0],   T ("CADENA"));
        over (&noteSlider,         T ("NOTA DEL PASO"));
        over (&bpmSlider,          T ("TEMPO"));
        if (barButtons[0] != nullptr && barButtons[0]->isVisible())
            over (barButtons[0],   T ("COMPAS"));
    }

    // The grid paints its own playhead and lane colours (see StepGrid).
    // Ring the bank being edited on the chain-include row.
    if (auto* b = patternButtons[selectedPattern])
    {
        g.setColour (ZatiColours::ink.withAlpha (0.7f));
        g.drawRect (b->getBounds(), 2);
    }
}

void MainComponent::Sheet::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    if (sheetBounds.isEmpty()) return;

    //  The card is a printed plate, not a floating dialog: square corners, a
    //  solid ink block under it instead of a blur, one ruled border, and
    //  registration brackets at the corners. The brackets are the piece that
    //  does the work - they say "this is a panel of an instrument" with four
    //  lines and no texture at all.
    const auto card = sheetBounds.toFloat();
    constexpr float rad = 2.0f;

    g.setColour (ZatiColours::ink.withAlpha (0.55f));
    g.fillRoundedRectangle (card.translated (0.0f, 5.0f), rad);

    g.setColour (ZatiColours::chassisTop);
    g.fillRoundedRectangle (card, rad);
    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.drawRoundedRectangle (card.reduced (0.75f), rad, 1.5f);

    {
        auto b = card.reduced (5.0f);
        const float arm = 12.0f;
        g.setColour (ZatiColours::ink.withAlpha (0.45f));
        for (int corner = 0; corner < 4; ++corner)
        {
            const bool right  = (corner & 1) != 0;
            const bool bottom = (corner & 2) != 0;
            const float x = right  ? b.getRight()  : b.getX();
            const float y = bottom ? b.getBottom() : b.getY();
            const float dx = right  ? -arm : arm;
            const float dy = bottom ? -arm : arm;

            g.drawLine (x, y, x + dx, y, 1.2f);
            g.drawLine (x, y, x, y + dy, 1.2f);
        }
    }

    if (paintContent) paintContent (g);
}

// Flat-style overlay rings (drawn over the step buttons' plain fill, never
// blended into it): yellow marks the step selected for NOTE editing, red
// marks the live playhead — only when viewing the pattern that's actually
// sounding, so a chain playing a different bank doesn't ring the wrong grid.
void MainComponent::layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap)
{
    // The pads are the instrument, so they take the room rather than leaving
    // it. They were true squares, centred, which on a tall phone left a band
    // of dead chassis above and below while the targets stayed small. Now the
    // cell fills the height it is given and is allowed to run up to a fifth
    // taller than it is wide — past that they stop reading as pads.
    //  Never taller than wide. The ceiling used to be a fifth over square,
    //  which is where the "the pads change shape while the app is opening"
    //  came from: the first pass had the room to hit that ceiling and the
    //  second did not. A pad is a square, and resized() now books it as one.
    const int cellW = (area.getWidth()  - (cols - 1) * gap) / cols;
    const int cellH = juce::jlimit (cellW * 3 / 4, cellW,
                                    (area.getHeight() - (rows - 1) * gap) / rows);

    auto grid = area.withSizeKeepingCentre (cols * cellW + (cols - 1) * gap,
                                            rows * cellH + (rows - 1) * gap);

    //  The plate the pads are bolted to. Remembered rather than recomputed in
    //  paint(), because the grid is centred inside whatever room is left and
    //  only this function knows where that landed.
    //  Five, not eight: the eight were the room four screw heads needed at
    //  the corners. Without them the plate can hug the pads, and the three
    //  pixels it gives back become distance to the section above it.
    padPlateArea = grid.expanded (5, 5);

    // SP-style numbering: pad 01 sits BOTTOM-left, 16 top-right — logical row
    // r of the pad index maps to visual row (rows-1-r).
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int idx = r * cols + c;
            const int vr  = rows - 1 - r;
            if (auto* p = pads[idx])
                p->setBounds (grid.getX() + c * (cellW + gap),
                             grid.getY() + vr * (cellH + gap),
                             cellW, cellH);
        }
}

void MainComponent::resized()
{
    //  Height reserved on a seam that carries an engraved name.
    constexpr int kSeamLabelH = 12;

    editInfoArea = {};
    vuArea = stepStripArea = {};   // gone from the face; the screen draws them
    //  Both plates are only laid out on the main face; clearing them here
    //  stops a stale rectangle from being painted under another view.
    padPlateArea = ctrlPlateArea = {};

    //  Margin. Everything used to start 8 px from the glass, which on a phone
    //  reads as the app being too big for the screen rather than as a machine
    //  sitting on it. The extra costs the LCD height, not the controls, since
    //  the screen is what absorbs whatever is left.
    //  ...and inside that, whatever the system is painting on top of us.
    //  From Android 15 the window is the whole screen and the status bar and
    //  the gesture pill sit over it, so the header was under the clock and the
    //  status line under the pill. safeArea is zero everywhere else.
    auto area = safeArea().reduced (ZatiLookAndFeel::kFaceMargin);
    area.removeFromTop    (ZatiLookAndFeel::kEdgeV);
    area.removeFromBottom (ZatiLookAndFeel::kEdgeV);

    // The LCD grows to absorb whatever the face doesn't need (the pads are
    // width-bound squares) — the screen is the protagonist.
    int screenH;
    {
        //  Spelled out term by term and in layout order, because this used to
        //  be two hand-totalled constants that had drifted: the header was
        //  counted as 30 when it is Metrics::tab, and the FX row as 32 when it
        //  is Metrics::hit. Fourteen pixels the pads were assumed to have and
        //  did not - and since layoutPadGrid clamps its cell to a MINIMUM
        //  height, missing room becomes overflow rather than smaller pads.
        //  The VU and the step LEDs live inside the screen now, so the face
        //  no longer spends two strips and four gaps on them - all of it goes
        //  back to the panel that shows them.
        const int aboveScreen = ZatiLookAndFeel::kHeader + ZatiLookAndFeel::kAir;
        const int belowScreen = ZatiLookAndFeel::kAir + ZatiLookAndFeel::kModule
                              + Metrics::xs + ZatiLookAndFeel::kTransport
                              + ZatiLookAndFeel::kAir;
        const int bottomStrip = ZatiLookAndFeel::kStatus
                              + ZatiLookAndFeel::kAir + Metrics::sm;

        //  A pad is a SQUARE, and the budget says so.
        //
        //  It used to reserve room for pads a fifth taller than they are wide,
        //  and then layoutPadGrid clamped them back down and centred what was
        //  left - so the difference between what was booked and what was used
        //  turned into two bands of dead chassis, one above the grid and one
        //  below. On this phone that was the pads arriving at 1.19 x wide on
        //  the first layout pass and settling at 1.03 x once the safe area
        //  came through: a fifth of a pad row, reserved and then thrown away.
        //
        //  Booking them square recovers all of it at once, and it also means
        //  the pads no longer change SHAPE between the first pass and the
        //  second - they only move.
        const int cellW    = (area.getWidth() - 3 * ZatiLookAndFeel::kPadGap) / 4;
        const int padsNeed = 4 * cellW + 3 * ZatiLookAndFeel::kPadGap;
        const int bodyNeed = ZatiLookAndFeel::kCtrlPlate + ZatiLookAndFeel::kAir + Metrics::sm
                           + ZatiLookAndFeel::kFxRow + ZatiLookAndFeel::kAir
                           + padsNeed + 3 * kSeamLabelH;

        //  ...and what it recovers goes into the SEAMS, not into one pool.
        //
        //  Height left over is worth more spread along the six places where
        //  one section meets the next than added to any single box: it is what
        //  makes a face read as laid out rather than as packed. The LCD keeps
        //  whatever the seams do not take, so on a short screen the seams stay
        //  at their base and the screen is the one that gives.
        //  Two of the six seams carry an engraved name (CONTROL over the knob
        //  plate, PADS over the pad plate; EFECTOS already had room in its
        //  own). Reserving the lettering here rather than hoping the seam is
        //  fat enough is what makes those two labels safe on a short screen:
        //  they are laid out, not squeezed in.
        constexpr int kSeams   = 6;
        constexpr int kAirMax  = 11;   // past this the face reads as loose
        constexpr int kMinScreen = 96;

        const int freeH = area.getHeight() - aboveScreen - belowScreen - bottomStrip - bodyNeed;

        layoutAir = (freeH > kMinScreen)
                      ? juce::jlimit (0, kAirMax, (freeH - kMinScreen) / (kSeams + 2))
                      : 0;

        screenH = juce::jmax (kMinScreen, freeH - layoutAir * kSeams);
    }

    // --- Top chrome ---
    headerArea = area.removeFromTop (ZatiLookAndFeel::kHeader);
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir);

    //  VU above the screen and the step strip below it, so the two readouts
    //  frame the LCD instead of sitting among the controls. Both are watched,
    //  not touched, so they belong together up here.
    screenBezel = area.removeFromTop (screenH);
    spectrum.setBounds (screenBezel);
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir);   // the bezel is drawn 5 px proud

    //  Six modules and three transport keys will not fit across a phone in one
    //  row: LOAD came out as "LO...". They split again, but the module bar
    //  stays slim at 32 while the transport keeps its full 44 — the original
    //  complaint was that the menu was as heavy as PLAY, and that still holds.
    tabBarArea = area.removeFromTop (ZatiLookAndFeel::kModule);
    {
        auto row = tabBarArea;
        juce::TextButton* mb[5] = { &padsButton, &secButton, &songButton, &mixButton, &setButton };
        const int w = row.getWidth() / 5;
        for (int i = 0; i < 5; ++i)
            mb[i]->setBounds ((i < 4 ? row.removeFromLeft (w) : row).reduced (1, 0));
    }
    area.removeFromTop (Metrics::xs);

    {
        auto row = area.removeFromTop (ZatiLookAndFeel::kTransport);
        const int u = row.getWidth() / 4;
        loadButton.setBounds (row.removeFromLeft (u).reduced (2, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (2, 0));
        playButton.setBounds (row.reduced (2, 0));
    }
    ctrlSeamTop = area.getY();
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH);   // CONTROL rides here

    // Status pinned to the bottom; DESHACER sits on its right when armed, so
    // an undoable action announces itself where the result was reported.
    {
        auto strip = area.removeFromBottom (ZatiLookAndFeel::kStatus);
        //  ...and the pads do not sit on the sentence.
        area.removeFromBottom (ZatiLookAndFeel::kAir + layoutAir);
        if (undoButton.isVisible()) undoButton.setBounds (strip.removeFromRight (96).reduced (1, 0));
        if (redoButton.isVisible()) redoButton.setBounds (strip.removeFromRight (96).reduced (1, 0));
        status.setBounds (strip);
    }
    area.removeFromBottom (Metrics::sm);

    // --- Machine face: CTRL 1-3 and their readout, the six FX, pads ---
    {
        auto mrow = area.removeFromTop (ZatiLookAndFeel::kCtrlPlate);
        ctrlPlateArea = mrow.expanded (4, 2);            // the plate they sit on
        juce::Slider* mk[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
        const int w = mrow.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? mrow.removeFromLeft (w) : mrow);
            //  The plate keeps its height and the two labels give theirs
            //  up, so the knob inside grows by ten pixels without the section
            //  taking one from the pads.
            cell.removeFromTop (ZatiLookAndFeel::kCtrlName);
            cell.removeFromBottom (ZatiLookAndFeel::kCtrlChip);
            mk[i]->setBounds (cell.reduced (10, 0));
        }
        fxSeamTop = area.getY();
        area.removeFromTop (ZatiLookAndFeel::kAir + Metrics::sm + layoutAir + kSeamLabelH);
        fxRowArea = area.removeFromTop (ZatiLookAndFeel::kFxRow);
        {
            auto row = fxRowArea;
            const int sw = row.getWidth() / kNumFx;
            for (int f = 0; f < kNumFx; ++f)
                fxButtons[f]->setBounds ((f < kNumFx - 1 ? row.removeFromLeft (sw) : row).reduced (1, 0));
        }
        padSeamTop = area.getY();
        area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH);   // PADS rides here
        layoutPadGrid (area, 4, 4, ZatiLookAndFeel::kPadGap);
    }

    // --- Floating sheets (each sized by its own content, capped at 86%) ---
    const auto full = safeArea();
    //  Centred, not risen from the bottom. A bottom sheet at 86% buried the pad
    //  grid exactly while you were editing a pad — you lost sight of the thing
    //  you were adjusting. Centred at 78% x 92% the instrument stays visible
    //  behind the scrim and the window reads as temporary.
    auto sheetFromBottom = [&full] (Sheet& s, int desiredH)
    {
        s.setBounds (full);
        const int h = juce::jmin (desiredH, (int) (full.getHeight() * 0.78f));
        const int w = (int) (full.getWidth() * 0.92f);
        auto sheet = juce::Rectangle<int> (0, 0, w, h).withCentre (full.getCentre());
        s.sheetBounds = sheet;
        return sheet.reduced (Metrics::lg, Metrics::md);
    };
    auto placeKnobRow = [] (juce::Rectangle<int> row, juce::Slider** ks)
    {
        const int w = row.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto cell = (i < 2 ? row.removeFromLeft (w) : row);
            cell.removeFromTop (16);                     // gap for knob name
            ks[i]->setBounds (cell.reduced (6, 2));
        }
    };

    // PADS sheet: per-pad knobs, trim, REV/LOOP + AUTO CHOP, sample-info card.
    {
        constexpr int secH = 15 + 2 * ZatiLookAndFeel::kTextPad;
        auto inner = sheetFromBottom (padSheet, 670 + 3 * secH);
        auto titleRow = inner.removeFromTop (32);
        padCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));
        titleRow.removeFromRight (Metrics::xs);
        previewButton.setBounds (titleRow.removeFromRight (68).reduced (0, 2));

        padSectionArea[0] = inner.removeFromTop (secH);   // painted: SONIDO

        juce::Slider* k1[3] = { &pitchSlider, &fineSlider, &volSlider };
        juce::Slider* k2[3] = { &panSlider, &attackSlider, &releaseSlider };
        placeKnobRow (inner.removeFromTop (86), k1);
        placeKnobRow (inner.removeFromTop (86), k2);

        //  A third row for the two controls that are not dials: CHOKE, which
        //  is a pair of increment buttons, and the tape/tone switch. Giving
        //  them a knob-sized cell was what turned CHOKE into two tall slabs
        //  that swallowed their column.
        {
            auto r3 = inner.removeFromTop (16 + Metrics::hit);
            r3.removeFromTop (16);                       // gap for the names
            const int w3 = r3.getWidth() / 3;
            auto chokeCell = r3.removeFromLeft (w3).reduced (6, 3);
            //  JUCE stacks a slider's +/- buttons whenever the space left for
            //  them is taller than it is wide, and on a narrow screen the
            //  readout was eating enough of the cell to trigger exactly that -
            //  two 17-pixel slivers. Reserve the buttons their width first.
            chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                         juce::jmax (30, chokeCell.getWidth() - 70), Metrics::chip);
            chokeSlider.setBounds (chokeCell);
            modeButton.setBounds  (r3.removeFromLeft (w3).reduced (6, 3));
        }

        inner.removeFromTop (Metrics::sm);
        padSectionArea[1] = inner.removeFromTop (secH);   // painted: RECORTE

        const int labelW = 64;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (34)); inner.removeFromTop (4);
        endSlider.setBounds   (ctrlRow (34)); inner.removeFromTop (8);

        padSectionArea[2] = inner.removeFromTop (secH);   // painted: EL PAD
        auto rr = inner.removeFromTop (Metrics::hit);
        const int rw = rr.getWidth() / 3;
        reverseButton.setBounds (rr.removeFromLeft (rw).reduced (3, 0));
        loopButton.setBounds    (rr.removeFromLeft (rw).reduced (3, 0));
        autocutButton.setBounds (rr.reduced (3, 0));
        inner.removeFromTop (5);
        auto rr2 = inner.removeFromTop (Metrics::hit);
        chopButton.setBounds (rr2.removeFromLeft (rr2.getWidth() / 2).reduced (3, 0));
        micButton.setBounds  (rr2.reduced (3, 0));
        inner.removeFromTop (5);

        //  Colour is a TAG, not sound design, and it used to be the loudest
        //  thing on this card: a full 44 px row with two stepper keys and a
        //  bar of saturated colour across the middle, shouting over PITCH and
        //  TRIM. It is eight swatches in a chip-high strip now - a third of
        //  the height, none of the shouting, and one tap instead of stepping
        //  round a ring of eight.
        zatiSwatchArea = inner.removeFromTop (Metrics::chip).reduced (4, 0);
        inner.removeFromTop (8);

        //  The cut itself, with its fragments and its draggable trim handles.
        //  It used to be a painted, untouchable card here while the real one
        //  lived on the face; now the interactive one is where the editing is.
        editInfoArea = inner;
        waveform.setBounds (inner);
    }

    // BROWSE sheet: the tallest of them all — the file list wants the room.
    {
        auto inner = sheetFromBottom (browseSheet, full.getHeight());   // clamps to the 86% cap
        auto titleRow = inner.removeFromTop (32);
        browseCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        auto actions = inner.removeFromBottom (Metrics::btn);
        browseSystemButton.setBounds (actions.removeFromRight (actions.getWidth() / 3).reduced (2, 0));
        browseLoadButton.setBounds   (actions.reduced (2, 0));
        inner.removeFromBottom (8);
        if (browser != nullptr) browser->setBounds (inner);
    }

    // PROJECT sheet: list of saved projects + the four actions.
    {
        //  Height follows the list, instead of claiming 70% of the screen and
        //  leaving whatever the projects did not fill as a white hole. With no
        //  projects saved that hole was most of the card, which reads as
        //  something failing to load rather than as an empty list.
        const int listRowH = juce::jmax (22, projList.getRowHeight());
        const int listH    = juce::jlimit (1, 8, projModel.names.size()) * listRowH;
        const int wanted   = Metrics::md * 2 + 32 + 158 + Metrics::xs
                               + (Metrics::hit + Metrics::xs) * 3 + Metrics::xs
                               + Metrics::btn * 2 + Metrics::xs + 8
                               + listH + Metrics::sm;

        auto inner = sheetFromBottom (projSheet, wanted);
        auto titleRow = inner.removeFromTop (32);
        projCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));
        // TEST is a diagnostic — it belongs with the housekeeping, not among
        // the effects, where it was one more button that made no music.
        testButton.setBounds    (titleRow.removeFromRight (56).reduced (2));
        measureButton.setBounds (titleRow.removeFromRight (64).reduced (2));

        // What the audio device is giving us, at the top where you cannot
        // miss it. It is the only number in the app that says whether this
        // thing is playable, so it does not live behind another tap.
        //  One line taller than it was: the panel now opens with what the
        //  app decided this phone can carry, before anything about the
        //  stream it opened.
        audioInfoArea = inner.removeFromTop (158);
        inner.removeFromTop (Metrics::xs);

        auto chipRow = [&inner] (juce::OwnedArray<juce::TextButton>& btns, int labelW)
        {
            auto row = inner.removeFromTop (Metrics::hit);
            auto r = row.withTrimmedLeft (labelW);
            const int n = juce::jmax (1, btns.size());
            const int w = r.getWidth() / n;
            for (int i = 0; i < btns.size(); ++i)
                btns[i]->setBounds ((i < n - 1 ? r.removeFromLeft (w) : r).reduced (1, 2));
            inner.removeFromTop (Metrics::xs);
            return row;
        };
        //  A narrower gutter for the row names: six chips need the width more
        //  than "BUFER" needs the air around it.
        bufRowArea  = chipRow (bufButtons, 44);
        rateRowArea = chipRow (rateButtons, 44);
        langRowArea = chipRow (langButtons, 44);
        inner.removeFromTop (Metrics::xs);

        // EXPORTAR sits on its own row: it is the only action here that
        // produces something outside the app, and it needs room for its name.
        projExportButton.setBounds (inner.removeFromBottom (Metrics::btn).reduced (2, 0));
        inner.removeFromBottom (Metrics::xs);

        auto actions = inner.removeFromBottom (Metrics::btn);
        const int aw = actions.getWidth() / 4;
        projSaveButton.setBounds   (actions.removeFromLeft (aw).reduced (2, 0));
        projLoadButton.setBounds   (actions.removeFromLeft (aw).reduced (2, 0));
        projNewButton.setBounds    (actions.removeFromLeft (aw).reduced (2, 0));
        projDeleteButton.setBounds (actions.reduced (2, 0));
        inner.removeFromBottom (8);

        projList.setBounds (inner);
    }

    // EXPORT sheet: what will be rendered, then the two products.
    {
        auto inner = sheetFromBottom (exportSheet, 32 + 96 + Metrics::btn * 2 + Metrics::sm * 2);
        auto titleRow = inner.removeFromTop (32);
        exportCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        inner.removeFromTop (96);   // painted: source, length, destination, status

        auto row = inner.removeFromBottom (Metrics::btn);
        exportCancelButton.setBounds (row);
        const int hw = row.getWidth() / 2;
        exportMasterButton.setBounds (row.removeFromLeft (hw).reduced (2, 0));
        exportStemsButton.setBounds  (row.reduced (2, 0));
    }

    // RACK sheet: which pad, and how much of it reaches each effect.
    {
        const int chipRowH = Metrics::hit;
        //  sheetFromBottom takes the card's OUTER height and hands back the
        //  inside, so the vertical margin it removes has to be part of what we
        //  ask for - without it the last send row fell off the bottom edge.
        auto inner = sheetFromBottom (rackSheet, Metrics::md * 2 + 32 + 14
                                                   + (chipRowH + Metrics::xs) * 2
                                                   + Metrics::sm + kNumFx * 48 + Metrics::sm);
        auto titleRow = inner.removeFromTop (32);
        rackCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));
        inner.removeFromTop (14);                       // painted: which pad this is

        for (int r = 0; r < 2; ++r)
        {
            auto row = inner.removeFromTop (chipRowH);
            const int w = row.getWidth() / 8;
            for (int c = 0; c < 8; ++c)
            {
                const int i = r * 8 + c;
                rackPadBtns[i]->setBounds ((c < 7 ? row.removeFromLeft (w) : row).reduced (1, 1));
            }
            inner.removeFromTop (Metrics::xs);
        }
        inner.removeFromTop (Metrics::sm);

        //  The name of the effect is painted in the gutter, so the fader gets
        //  the width instead of a label component competing for it.
        for (int f = 0; f < kNumFx; ++f)
        {
            auto row = inner.removeFromTop (48);
            rackSends[f]->setBounds (row.withTrimmedLeft (54).reduced (2, 6));
        }
    }

    // AUTO CHOP sheet: how many pieces, where they land, and one red verb.
    {
        const int explainH = 40, plannedH = 40;
        auto inner = sheetFromBottom (chopSheet, Metrics::md * 2 + 32 + explainH
                                                   + Metrics::md + 14 + Metrics::hit
                                                   + Metrics::sm + Metrics::hit
                                                   + Metrics::md + plannedH
                                                   + Metrics::sm + Metrics::btn);
        auto titleRow = inner.removeFromTop (32);
        chopCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        inner.removeFromTop (explainH);                 // painted: what this does
        inner.removeFromTop (Metrics::md);
        inner.removeFromTop (14);                       // painted: "TROZOS"

        {
            auto row = inner.removeFromTop (Metrics::hit);
            const int w = row.getWidth() / chopCountBtns.size();
            for (int i = 0; i < chopCountBtns.size(); ++i)
                chopCountBtns[i]->setBounds ((i < chopCountBtns.size() - 1 ? row.removeFromLeft (w) : row)
                                                 .reduced (2, 0));
        }

        inner.removeFromTop (Metrics::sm);
        chopSafeButton.setBounds (inner.removeFromTop (Metrics::hit).reduced (2, 0));
        inner.removeFromTop (Metrics::md);
        inner.removeFromTop (plannedH);                 // painted: where they land
        inner.removeFromTop (Metrics::sm);
        chopGoButton.setBounds (inner.removeFromTop (Metrics::btn).reduced (2, 0));
    }

    // SONG sheet: palette, timeline, page row.
    {
        const int laneH = 40;
        auto inner = sheetFromBottom (songSheet, Metrics::md * 2 + 32 + Metrics::hit * 2 + Metrics::sm * 3
                                                  + Playlist::kLanes * laneH + Metrics::hit + Metrics::btn);
        auto titleRow = inner.removeFromTop (32);
        songCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        // Palette: P1..P8.
        {
            auto row = inner.removeFromTop (Metrics::hit);
            const int w = row.getWidth() / kNumPatterns;
            for (int i = 0; i < kNumPatterns; ++i)
                songPatBtns[i]->setBounds ((i < kNumPatterns - 1 ? row.removeFromLeft (w) : row).reduced (1, 2));
            inner.removeFromTop (Metrics::xs);
        }
        // Brush modes + song mode.
        {
            auto row = inner.removeFromTop (Metrics::hit);
            const int w = row.getWidth() / 3;
            songPadModeBtn.setBounds (row.removeFromLeft (w).reduced (2, 2));
            songClearBtn.setBounds   (row.removeFromLeft (w).reduced (2, 2));
            songModeBtn.setBounds    (row.reduced (2, 2));
            inner.removeFromTop (Metrics::sm);
        }

        auto bottom = inner.removeFromBottom (Metrics::btn);
        songLenSlider.setBounds (bottom.reduced (3, 6));
        inner.removeFromBottom (Metrics::xs);

        auto pageRow = inner.removeFromBottom (Metrics::hit);
        {
            const int n = songPageBtns.size();
            const int w = pageRow.getWidth() / juce::jmax (1, n);
            for (int i = 0; i < n; ++i)
            {
                const bool used = i * Playlist::kBarsView < engine.getSongLength();
                songPageBtns[i]->setVisible (used);
                songPageBtns[i]->setBounds ((i < n - 1 ? pageRow.removeFromLeft (w) : pageRow).reduced (1, 2));
            }
        }
        inner.removeFromBottom (Metrics::xs);

        songGrid.setBounds (inner);
    }

    // MIX sheet: sixteen channel strips.
    {
        //  Sixteen rows at finger height is what this sheet WANTS; on a short
        //  screen it is more than the card is allowed to be. Rather than lay
        //  out rows that fall off the bottom, work out what is left after the
        //  furniture and share it - never below 30, never above the target.
        const int mixFurniture = Metrics::md * 2 + 32 + Metrics::sm + Metrics::btn + Metrics::lg;
        const int mixRoom = (int) (full.getHeight() * 0.78f) - mixFurniture;
        //  Floor low enough that sixteen rows ALWAYS fit. A higher minimum
        //  looks better right up to the screen where it does not fit, and
        //  then the last rows are laid out with no height at all.
        const int rowH = juce::jlimit (24, Metrics::hit, mixRoom / kNumPads);
        auto inner = sheetFromBottom (mixSheet, mixFurniture + kNumPads * rowH);
        auto titleRow = inner.removeFromTop (32);
        mixCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        auto bottom = inner.removeFromBottom (Metrics::btn);
        rackButton.setBounds (bottom.removeFromRight (bottom.getWidth() / 3).reduced (3, 4));
        mixClearSolo.setBounds (bottom.reduced (3, 4));
        inner.removeFromBottom (Metrics::xs);

        for (int i = 0; i < kNumPads; ++i)
        {
            auto row = inner.removeFromTop (rowH).reduced (0, 1);
            row.removeFromLeft (76);                       // colour chip + number + name
            //  Padding here is not decoration, it is the hit area coming off
            //  the control. The pan was losing twelve pixels of a forty-pixel
            //  row to margins and ending up shorter than the M and S beside it.
            mixSolos[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (2, 1));
            mixMutes[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (2, 1));
            mixPans[i]->setBounds  (row.removeFromRight (juce::jmin (78, row.getWidth() / 3)).reduced (4, 1));
            mixFaders[i]->setBounds (row.reduced (4, 1));
        }
    }

    // SEC sheet: pattern/len, chain, bar selector, the pads x steps grid, bpm.
    {
        const int lanes  = StepGrid::kLanes;
        //  Every control row now carries its own name, and a name is 14px of
        //  height that has to be budgeted rather than borrowed from the grid.
        const int fixedRowsH = 350;               // title + named rows above and below the grid
        //  Same bargain as the mixer: the grid gets the room that is left,
        //  down to the density it had before rather than off the card.
        const int laneH  = juce::jlimit (20, 24, ((int) (full.getHeight() * 0.78f) - fixedRowsH) / lanes);
        const int gridH  = lanes * laneH;

        auto inner = sheetFromBottom (seqSheet, fixedRowsH + gridH);
        auto titleRow = inner.removeFromTop (32);
        seqCloseButton.setBounds (titleRow.removeFromRight (32).reduced (2));

        {
            //  Named groups, in the order the work happens: pick the bank and
            //  its length, build the chain, tune the step you tapped, choose
            //  the bar. Every one of those rows used to be an unlabelled strip
            //  of look-alike buttons.
            constexpr int nameH = 14 + ZatiLookAndFeel::kTextPad;

            inner.removeFromTop (nameH);                  // painted: PATRON / LARGO
            auto row1 = inner.removeFromTop (Metrics::hit);
            const int w1 = row1.getWidth() / 2;
            //  An IncDecButtons slider gives its two buttons whatever the text
            //  box does not take, so a narrow box on a wide row turns them into
            //  a pair of slabs twice the size of anything else on the sheet.
            //  Reserve the box first and the buttons come out finger-sized.
            patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                           juce::jmax (40, w1 - 4 - 2 * 40), 22);
            patternSlider.setBounds (row1.removeFromLeft (w1).reduced (2, 2));
            lengthSlider.setBounds  (row1.reduced (2, 2));

            inner.removeFromTop (Metrics::sm);
            inner.removeFromTop (nameH);                  // painted: CADENA
            auto row2 = inner.removeFromTop (Metrics::hit);
            const int pw = row2.getWidth() / kNumPatterns;
            for (int i2 = 0; i2 < kNumPatterns; ++i2)
                patternButtons[i2]->setBounds ((i2 < kNumPatterns - 1 ? row2.removeFromLeft (pw) : row2).reduced (2));
            inner.removeFromTop (Metrics::xs);

            //  QUITAR CADENA belongs to the row above it, NOTA to the step you
            //  tapped: two different jobs that happen to fit on one line, so
            //  the note half is the one that gets the name.
            inner.removeFromTop (nameH);                  // painted: NOTA DEL PASO
            auto row3 = inner.removeFromTop (Metrics::hit);
            chainClearButton.setBounds (row3.removeFromLeft (row3.getWidth() * 5 / 12).reduced (2, 2));
            noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                        juce::jmax (40, row3.getWidth() - 4 - 2 * 40), 22);
            noteSlider.setBounds       (row3.reduced (2, 2));
            inner.removeFromTop (Metrics::sm);

            // Bar row: only when the pattern is longer than one bar. A single
            // lone "1" would be a control that never does anything.
            const int patLen = engine.getPatternLength (selectedPattern);
            const int bars   = juce::jmax (1, patLen / kStepCols);
            if (selectedBar >= bars) selectedBar = 0;

            if (bars > 1)
            {
                inner.removeFromTop (nameH);              // painted: COMPAS
                auto row4 = inner.removeFromTop (Metrics::hit);
                const int bw = row4.getWidth() / bars;
                for (int b = 0; b < barButtons.size(); ++b)
                {
                    barButtons[b]->setVisible (b < bars);
                    if (b < bars)
                        barButtons[b]->setBounds ((b < bars - 1 ? row4.removeFromLeft (bw) : row4).reduced (2, 2));
                }
                inner.removeFromTop (Metrics::sm);
            }
            else
            {
                for (auto* b : barButtons) b->setVisible (false);
            }
        }

        auto bottom = inner.removeFromBottom (Metrics::hit);
        bpmSlider.setBounds (bottom.removeFromLeft ((int) (bottom.getWidth() * 0.66f)).reduced (2, 2));
        clearButton.setBounds (bottom.reduced (3, 2));
        inner.removeFromBottom (14 + ZatiLookAndFeel::kTextPad);   // painted: TEMPO
        inner.removeFromBottom (Metrics::sm);

        stepGrid.setBounds (inner);
    }
}

void MainComponent::padClicked (int index)
{
    if (loadArmed)
    {
        loadArmed = false;
        loadButton.setToggleState (false, juce::dontSendNotification);
        openBrowseForPad (index);
        return;
    }

    if (padHasSample[(size_t) index])
        engine.postNoteOn (index, pads[index] != nullptr ? pads[index]->getLastVelocity() : 1.0f);
    else
        status.setText (T ("Pad vacio - pulsa LOAD y toca el pad para cargarlo"), juce::dontSendNotification);

    // REC armed + transport rolling: write the hit into the bank that is
    // actually sounding, quantised to the NEAREST step - and compensated for
    // the milliseconds the phone spends between us writing a block and the
    // speaker moving.
    //
    //  You play along to what you HEAR, and what you hear left the app 47 ms
    //  ago on this device. So a hit that felt exactly on the beat arrives here
    //  47 ms after the beat, and quantising the arrival time records it late -
    //  at fast tempi late enough to land on the following step. Subtracting the
    //  output latency before rounding puts the hit where the player put it.
    //  This is what every DAW calls record delay compensation, and it is the
    //  one part of the latency we can actually give back.
    if (recArmed && engine.isPlaying() && padHasSample[(size_t) index])
    {
        const int bank = engine.getPlayingPattern();
        const int len  = engine.getPatternLength (bank);
        const int cur  = engine.getPlayStep();

        if (cur >= 0 && len > 0)
        {
            const double stepMs = (60000.0 / juce::jmax (20.0, engine.getBpm())) * 0.25;   // 16ths
            //  Clamped: a driver that reports nonsense should cost us a
            //  rounding error, never a hit two steps from where it was played.
            const double back = juce::jlimit (0.0, 2.0, outputLatencyMs() / stepMs);

            const double at   = (double) cur + (double) engine.getStepPhase() - back;
            const int    step = (int) (((juce::roundToInt (at) % len) + len) % len);
            pattern[(size_t) bank][(size_t) step][(size_t) index] = true;
            engine.setStep (bank, step, index, true);
            status.setText (T ("Grabado pad %1 en paso %2 (P%3)",
                               juce::String (index + 1), juce::String (step + 1),
                               juce::String (bank + 1)),
                            juce::dontSendNotification);
            if (seqSheet.isVisible()) seqSheet.repaint();
        }
    }

    selectPad (index);   // selection drives EDIT and SEC
}

void MainComponent::stepCellToggled (int pad, int step)
{
    if (step >= engine.getPatternLength (selectedPattern)) return;
    selectedStep = step;
    selectPad (pad);                 // the lane you touched becomes the pad you edit
    noteSlider.setValue (engine.getStepNote (selectedPattern, step, pad), juce::dontSendNotification);

    const bool nv = ! pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad];
    pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad] = nv;
    engine.setStep (selectedPattern, step, pad, nv);
    refreshStepGrid();
    seqSheet.repaint();
}

// Copy the pattern into the flat buffer the grid reads, plus each pad's colour
// and whether it holds a sample, then hand it the live playhead.
void MainComponent::refreshStepGrid()
{
    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
        {
            gridCells[st * kNumPads + p] = pattern[(size_t) selectedPattern][(size_t) st][(size_t) p];
            gridNotes[st * kNumPads + p] = (signed char) engine.getStepNote (selectedPattern, st, p);
        }

    for (int p = 0; p < kNumPads; ++p)
    {
        gridZati[p]   = padZati[(size_t) p];
        gridLoaded[p] = padHasSample[(size_t) p];
    }

    const int ps = (engine.isPlaying() && engine.getPlayingPattern() == selectedPattern)
                     ? engine.getPlayStep() : -1;

    stepGrid.setSource (gridCells, gridZati, gridLoaded, gridNotes,
                        engine.getPatternLength (selectedPattern),
                        selectedBar, ps, selectedPad,
                        ps >= 0 ? engine.getStepPhase() : 0.0f);

    //  The grid can only ring the live column when that column is on screen,
    //  so at four bars you would lose the beat entirely while editing bar 1
    //  and bar 3 played. The bar buttons carry it instead: the one sounding
    //  goes red, which keeps you oriented without yanking the view away from
    //  what you are editing.
    const int playingBar = ps >= 0 ? ps / kStepCols : -1;
    for (int b = 0; b < barButtons.size(); ++b)
        if (auto* t = barButtons[b])
        {
            //  Both colours, because the bar you are editing is usually also
            //  the one playing: the toggle-on colour would otherwise win and
            //  swallow the red exactly when you most want to see it.
            const bool live = (b == playingBar);
            t->setColour (juce::TextButton::buttonColourId,   live ? ZatiColours::red : kStepOff);
            t->setColour (juce::TextButton::buttonOnColourId, live ? ZatiColours::red : kAccent);
            t->setColour (juce::TextButton::textColourOffId,  live ? juce::Colours::white : ZatiColours::ink);
            t->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        }
}

// The tile art is the pad's own slice, so it has to be rebuilt whenever the
// trim window moves, not only when a sample is assigned.
void MainComponent::refreshPadArt (int index)
{
    if (index < 0 || index >= kNumPads) return;
    if (auto* p = pads[index])
    {
        p->setSampleInfo (uiSample[(size_t) index], padName[(size_t) index],
                          padStart01[(size_t) index], padEnd01[(size_t) index]);

        //  A pad draws its number and its waveform, so with a screen reader on
        //  there was nothing to announce - every one of the sixteen came out as
        //  "Button". The name has to be set here rather than once at startup,
        //  because this is the one place that knows what the pad now holds.
        p->setTitle ("Pad " + juce::String (index + 1));
        p->setDescription (padName[(size_t) index].isNotEmpty()
                               ? padName[(size_t) index]
                               : juce::String ("vacio"));
    }
}

void MainComponent::refreshPad (int index)
{
    if (auto* p = pads[index])
    {
        p->setSelected (index == selectedPad);
        p->setFlash (padFlash[(size_t) index]);
    }
}

void MainComponent::selectPad (int index)
{
    selectedPad = index;
    updateControlsFromPad (index);
    waveform.setSample (uiSample[(size_t) index]);
    waveform.setTrim (padStart01[(size_t) index], padEnd01[(size_t) index]);
    if (auto sb = uiSample[(size_t) index])
    {
        const double sr = sb->sourceSampleRate;
        const double secs = sr > 0.0 ? (double) sb->buffer.getNumSamples() / sr : 0.0;
        const juce::String tag = juce::String (index + 1).paddedLeft ('0', 2)
                               + (padName[(size_t) index].isNotEmpty() ? "  " + padName[(size_t) index].toUpperCase() : juce::String());
        waveform.setInfo (tag, sr, secs, sb->buffer.getNumChannels());
    }
    else
        waveform.setInfo ("PAD " + juce::String (index + 1), 0.0, 0.0, 0);
    refreshWaveformSegments();

    // Last link of CUT -> PAD -> WAVEFORM -> KNOBS: the selected pad tints the
    // three CTRL pointers, so the knobs always say which fragment they act on.
    {
        const auto frag = padHasSample[(size_t) index] ? Zati::colour (padZati[(size_t) index])
                                                       : ZatiColours::accent;
        for (juce::Slider* k : { &macroCtrl1, &macroCtrl2, &macroCtrl3 })
        {
            k->setColour (juce::Slider::rotarySliderFillColourId, frag);
            k->repaint();
        }
    }

    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    repaint (headerArea);          // the fragment strip tracks which zatis are loaded
    if (padSheet.isVisible()) padSheet.repaint();  // title, zati swatch and card follow the selection
}

// The display shows the whole cut, not one pad: every pad pointing at the
// selected pad's buffer contributes its trim window as a coloured fragment.
// Auto-chop leaves exactly that — one shared buffer, sixteen windows.
void MainComponent::refreshWaveformSegments()
{
    juce::Array<WaveformDisplay::Segment> segs;

    if (selectedPad >= 0)
    {
        if (auto src = uiSample[(size_t) selectedPad])
        {
            for (int i = 0; i < kNumPads; ++i)
            {
                if (uiSample[(size_t) i] != src) continue;

                WaveformDisplay::Segment s;
                s.start01   = padStart01[(size_t) i];
                s.end01     = padEnd01[(size_t) i];
                s.colour    = Zati::colour (padZati[(size_t) i]);
                s.padNumber = i + 1;
                s.selected  = (i == selectedPad);
                segs.add (s);
            }

            std::sort (segs.begin(), segs.end(),
                       [] (const WaveformDisplay::Segment& a, const WaveformDisplay::Segment& b)
                       { return a.start01 < b.start01; });
        }
    }

    waveform.setSegments (std::move (segs));
}

void MainComponent::updateControlsFromPad (int index)
{
    pitchSlider.setValue (padPitch[(size_t) index], juce::dontSendNotification);
    fineSlider.setValue  (padCents[(size_t) index], juce::dontSendNotification);
    modeButton.setToggleState (padKeepLen[(size_t) index], juce::dontSendNotification);
    modeButton.setButtonText (padKeepLen[(size_t) index] ? T ("TONO") : T ("CINTA"));
    volSlider.setValue   (padGain[(size_t) index],  juce::dontSendNotification);
    startSlider.setValue (padStart01[(size_t) index], juce::dontSendNotification);
    endSlider.setValue   (padEnd01[(size_t) index],   juce::dontSendNotification);
    reverseButton.setToggleState (padReverse[(size_t) index], juce::dontSendNotification);
    loopButton.setToggleState    (padLoop[(size_t) index],    juce::dontSendNotification);
    autocutButton.setToggleState (padSelfCut[(size_t) index], juce::dontSendNotification);
    chokeSlider.setValue (padChokeUI[(size_t) index], juce::dontSendNotification);
    panSlider.setValue     (padPan[(size_t) index],     juce::dontSendNotification);
    attackSlider.setValue  (padAttack[(size_t) index],  juce::dontSendNotification);
    releaseSlider.setValue (padRelease[(size_t) index], juce::dontSendNotification);
}

void MainComponent::assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name)
{
    if (sb == nullptr) return;
    padHasSample[(size_t) index] = true;
    uiSample[(size_t) index]     = sb;
    padStart01[(size_t) index]   = 0.0f;
    padEnd01[(size_t) index]     = 1.0f;
    if (name.isNotEmpty())
        padName[(size_t) index] = name.upToLastOccurrenceOf (".", false, false);

    // Push this pad's UI params into the engine. The engine's per-pad gain
    // defaults to 0 (silent); setVal(dontSendNotification) never fires the
    // slider callbacks, so without this the pad plays at zero gain.
    engine.setPadGain    (index, padGain[(size_t) index]);
    engine.setPadPitch   (index, padPitch[(size_t) index] + padCents[(size_t) index] / 100.0f);
    engine.setPadKeepLength (index, padKeepLen[(size_t) index]);
    engine.setPadLoop    (index, padLoop[(size_t) index]);
    engine.setPadReverse (index, padReverse[(size_t) index]);
    engine.setPadChoke   (index, padChokeUI[(size_t) index]);
    engine.setPadSelfCut (index, padSelfCut[(size_t) index]);
    engine.setPadPan     (index, padPan[(size_t) index]);
    engine.setPadAttack  (index, padAttack[(size_t) index]);
    engine.setPadRelease (index, padRelease[(size_t) index]);

    if (auto* p = pads[index]) p->setSampleInfo (uiSample[(size_t) index], padName[(size_t) index],
                                                 padStart01[(size_t) index], padEnd01[(size_t) index]);

    selectPad (index);
}

void MainComponent::rebuildChain()
{
    engine.clearChain();
    for (int i = 0; i < kNumPatterns; ++i)
        if (patternActiveUI[(size_t) i])
            engine.addToChain (i);
    repaint();
}

// Assignment follows cut order by default; this is the spec's manual override,
// for organising a kit by kind of sound instead of by position.
void MainComponent::setZati (int newZati)
{
    if (selectedPad < 0) return;

    auto& z = padZati[(size_t) selectedPad];
    if (z == newZati) return;
    z = juce::jlimit (0, Zati::kNumColours - 1, newZati);

    if (auto* p = pads[selectedPad]) p->setZati (z);
    refreshWaveformSegments();
    repaint();
    padSheet.repaint();

    status.setText (T ("Pad %1 -> zati %2 %3", juce::String (selectedPad + 1),
                       juce::String (z + 1), T (Zati::name (z))),
                    juce::dontSendNotification);
}

//  AUTO CHOP overwrites up to fifteen pads AND the source pad — after it, the
//  pad that held your break holds its first slice instead. That is a lot to do
//  with no way back, so the whole machine is snapshotted first and DESHACER in
//  the status bar puts it back.
// ============================================================================
//  Every caption on the machine, in one place.
//
//  A button's text is set here and not where the button is declared, because a
//  declaration runs once and a language can change at any moment. Anything
//  that depends on state - PLAY vs STOP, CINTA vs TONO - is re-derived from
//  that state rather than assumed, so switching language mid-take does not
//  quietly claim the transport is stopped when it is running.
// ============================================================================
void MainComponent::retranslateUi()
{
    padsButton  .setButtonText (T ("PADS"));
    secButton   .setButtonText (T ("SEC"));
    songButton  .setButtonText (T ("SONG"));
    mixButton   .setButtonText (T ("MIX"));
    setButton   .setButtonText (T ("SET"));

    loadButton  .setButtonText (T ("LOAD"));
    testButton  .setButtonText (T ("TEST"));
    measureButton.setButtonText (T ("MEDIR"));
    recButton   .setButtonText (recArmed ? T ("REC ON") : T ("REC"));
    playButton  .setButtonText (engine.isPlaying() ? T ("STOP") : T ("PLAY"));
    clearButton .setButtonText (T ("VACIAR"));
    undoButton  .setButtonText (T ("DESHACER"));
    redoButton  .setButtonText (T ("REHACER"));

    reverseButton.setButtonText (T ("REV|reverso"));
    loopButton   .setButtonText (T ("LOOP"));
    autocutButton.setButtonText (T ("AUTOCUT"));
    chopButton   .setButtonText (T ("AUTO CHOP"));
    micButton    .setButtonText (recordingActive ? T ("PARAR") : T ("GRABAR MIC"));
    previewButton.setButtonText (juce::String::fromUTF8 (previewSounding ? "\xe2\x96\xa0 " : "\xe2\x96\xb6 ")
                                   + T (previewSounding ? "STOP" : "OIR"));
    modeButton   .setButtonText (selectedPad >= 0 && padKeepLen[(size_t) selectedPad]
                                   ? T ("TONO") : T ("CINTA"));

    chainClearButton.setButtonText (T ("QUITAR CADENA"));

    projSaveButton  .setButtonText (T ("GUARDAR"));
    projLoadButton  .setButtonText (T ("ABRIR"));
    projNewButton   .setButtonText (T ("NUEVO"));
    projDeleteButton.setButtonText (T ("BORRAR"));
    projExportButton.setButtonText (T ("EXPORTAR"));

    browseLoadButton  .setButtonText (T ("CARGAR"));
    browseSystemButton.setButtonText (T ("SISTEMA"));

    exportMasterButton.setButtonText (T ("MASTER"));
    exportStemsButton .setButtonText (T ("PISTAS"));
    exportCancelButton.setButtonText (T ("CANCELAR"));

    rackButton   .setButtonText (T ("RACK"));
    mixClearSolo .setButtonText (T ("SIN SOLO"));
    songClearBtn .setButtonText (T ("VACIAR"));
    songModeBtn  .setButtonText (T ("CANCION"));

    chopSafeButton.setButtonText (T ("RESPETAR PADS CON SONIDO"));

    //  A slider that formats its own readout has to be told to run the
    //  formatter again; the text it is showing was made in the old language.
    for (auto* sl : { &lengthSlider, &chokeSlider })
        sl->updateText();
    refreshChopSheet();          // its verb carries the piece count
    refreshSong();               // the brush chip names itself

    //  An armed confirmation holds the old caption to put back, and that
    //  caption is now in the wrong language. Simplest correct answer: the
    //  confirmation does not survive the change.
    disarmConfirm();

    //  The idle line is part of the furniture, not a message someone is
    //  waiting to read: it says the same thing in the new language.
    status.setText (T ("Toca un pad para sonar"), juce::dontSendNotification);

    for (int i = 0; i < langButtons.size(); ++i)
        if (auto* b = langButtons[i])
            b->setToggleState (i == (int) Lang::current(), juce::dontSendNotification);

    resized();
    repaint();
}

//  The part of the window the system is not covering. Cached rather than
//  asked for on every layout pass: resized() runs on every sheet that opens
//  and every project that is saved, and this is a JNI round trip.
juce::Rectangle<int> MainComponent::safeArea() const
{
    return systemInsets.subtractedFrom (getLocalBounds());
}

//  The bars can come and go - a keyboard, a rotation, an immersive app handing
//  the screen back - so this is re-read every couple of seconds and the face is
//  laid out again only when the answer actually changed.
void MainComponent::refreshSystemInsets()
{
    const auto now = SystemInsets::get();

    if (now.getTop()    == systemInsets.getTop()
        && now.getLeft()   == systemInsets.getLeft()
        && now.getBottom() == systemInsets.getBottom()
        && now.getRight()  == systemInsets.getRight())
        return;

    systemInsets = now;
    resized();
    repaint();
}

bool MainComponent::armConfirm (juce::TextButton& b, const juce::String& armedText)
{
    if (confirmPending == &b)          // second tap: go ahead
    {
        disarmConfirm();
        return true;
    }

    disarmConfirm();                   // never leave two buttons armed at once

    confirmPending = &b;
    confirmOldText = b.getButtonText();
    confirmTicks   = 50;               // ~3 s at the 60 ms UI timer
    b.setButtonText (armedText);
    b.setColour (juce::TextButton::buttonColourId, ZatiColours::red);
    b.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    b.repaint();
    return false;
}

void MainComponent::disarmConfirm()
{
    if (confirmPending == nullptr) return;

    auto* b = confirmPending;
    confirmPending = nullptr;
    confirmTicks   = 0;
    b->setButtonText (confirmOldText);
    styleButton (*b, b == &projDeleteButton ? kRec : kKey);
    b->repaint();
}

void MainComponent::pushUndo (const juce::String& what)
{
    undoState = captureState();
    undoLabel = what;
    redoState = {};                 // a new action ends the old redo branch
    undoButton.setVisible (true);
    redoButton.setVisible (false);
    resized();
}

//  Undo and redo are the same move in opposite directions: each keeps what it
//  is about to replace, so you can step back and forth over one action instead
//  of the one-way trip DESHACER was on its own.
void MainComponent::performUndo()
{
    if (! undoState.isValid()) return;
    auto restore = undoState;
    redoState = captureState();
    undoState = {};
    undoButton.setVisible (false);
    redoButton.setVisible (true);
    applyState (restore);
    status.setText (T ("Deshecho: %1", undoLabel), juce::dontSendNotification);
    resized();
}

void MainComponent::performRedo()
{
    if (! redoState.isValid()) return;
    auto restore = redoState;
    undoState = captureState();
    redoState = {};
    redoButton.setVisible (false);
    undoButton.setVisible (true);
    applyState (restore);
    status.setText (T ("Rehecho: %1", undoLabel), juce::dontSendNotification);
    resized();
}

// ============================================================================
//  AUTO CHOP.
//
//  What it used to do, on a single tap and with no warning: cut the selected
//  sample into sixteen equal pieces and write every one of them over every
//  pad. If you had spent an hour building a kit and then tapped it to see what
//  it did, the kit was gone - DESHACER got it back, but only if you knew the
//  button existed and reached it before doing anything else.
//
//  Three things changed. It asks first, in a sheet that says exactly which
//  pads it is about to write. It cuts into as many pieces as you choose, not
//  always sixteen. And by default it will not touch a pad that already holds a
//  sound: the slices go to the source pad and then to whatever is empty. Turn
//  that off and it behaves like it always did - which is a real thing to want,
//  just not the default for someone who does not yet know what the button is.
// ============================================================================

//  Which pads a chop of `slices` pieces would land on, in order. The first is
//  always the source: the break is already there, and slice one IS the break's
//  own beginning, so it costs nothing.
juce::Array<int> MainComponent::chopTargets (int slices, bool onlyEmpty) const
{
    juce::Array<int> t;
    if (selectedPad < 0) return t;

    t.add (selectedPad);

    for (int k = 1; k < kNumPads && t.size() < slices; ++k)
    {
        const int i = (selectedPad + k) % kNumPads;
        if (onlyEmpty && padHasSample[(size_t) i]) continue;
        t.add (i);
    }

    return t;
}

void MainComponent::openChopSheet()
{
    if (selectedPad < 0) selectPad (0);

    for (int i = 0; i < chopCountBtns.size(); ++i)
        chopCountBtns[i]->setToggleState (kChopCounts[i] == chopSlices, juce::dontSendNotification);

    chopSafeButton.setToggleState (chopOnlyEmpty, juce::dontSendNotification);

    openSheet (chopSheet, padsButton);
    refreshChopSheet();
}

void MainComponent::refreshChopSheet()
{
    const int fits = chopTargets (chopSlices, chopOnlyEmpty).size();
    const bool can = selectedPad >= 0 && uiSample[(size_t) selectedPad] != nullptr && fits >= 2;

    chopGoButton.setEnabled (can);
    chopGoButton.setButtonText (can ? T ("CORTAR EN %1", juce::String (fits)) : T ("CORTAR"));
    chopSheet.repaint();
}

void MainComponent::applyAutoChop()
{
    if (selectedPad < 0) return;
    auto src = uiSample[(size_t) selectedPad];
    if (src == nullptr) return;

    const int len = src->buffer.getNumSamples();
    const auto targets = chopTargets (chopSlices, chopOnlyEmpty);
    const int  n = targets.size();

    //  Two pieces is the least that is still a chop, and a source shorter than
    //  one sample per piece has nothing to divide.
    if (n < 2 || len < n) return;

    pushUndo ("auto chop");

    const juce::String baseName = padName[(size_t) selectedPad].isNotEmpty()
                                 ? padName[(size_t) selectedPad] : juce::String ("CHOP");

    for (int k = 0; k < n; ++k)
    {
        const int i  = targets[k];
        const int st = (int) ((juce::int64) k * len / n);
        const int en = (int) ((juce::int64) (k + 1) * len / n);

        padHasSample[(size_t) i] = true;
        uiSample[(size_t) i]     = src;
        padStart01[(size_t) i]   = (float) st / (float) len;
        padEnd01[(size_t) i]     = (float) en / (float) len;
        padLoop[(size_t) i]      = false;
        padReverse[(size_t) i]   = false;
        padChokeUI[(size_t) i]   = 0;
        padName[(size_t) i]      = baseName + " " + juce::String (k + 1).paddedLeft ('0', 2);

        engine.publishSample (i, src);   // resets trim to full length — override right after
        engine.setPadStart   (i, st);
        engine.setPadEnd     (i, en);
        engine.setPadGain    (i, padGain[(size_t) i]);
        engine.setPadPitch   (i, padPitch[(size_t) i] + padCents[(size_t) i] / 100.0f);
        engine.setPadKeepLength (i, padKeepLen[(size_t) i]);
        engine.setPadLoop    (i, false);
        engine.setPadReverse (i, false);
        engine.setPadChoke   (i, 0);
        engine.setPadPan     (i, padPan[(size_t) i]);
        engine.setPadAttack  (i, padAttack[(size_t) i]);
        engine.setPadRelease (i, padRelease[(size_t) i]);

        if (auto* p = pads[i]) p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i],
                                                 padStart01[(size_t) i], padEnd01[(size_t) i]);
    }

    const int askedFor = chopSlices;
    closeAllSheets();
    selectPad (targets[0]);

    status.setText (n < askedFor
                        ? T ("Cortado en %1 (no cabian %2) - DESHACER para volver",
                             juce::String (n), juce::String (askedFor))
                        : T ("Cortado en %1 trozos - DESHACER para volver", juce::String (n)),
                    juce::dontSendNotification);
}

int MainComponent::firstEmptyPad() const
{
    for (int i = 0; i < kNumPads; ++i)
        if (! padHasSample[(size_t) i]) return i;
    return -1;
}

// Ask once for audio-read access, then run `then` either way — a refusal must
// still open the browser (internal/app storage is always readable).
void MainComponent::ensureStoragePermission (std::function<void()> then)
{
    using RP = juce::RuntimePermissions;

    if (! RP::isRequired (RP::readMediaAudio) || RP::isGranted (RP::readMediaAudio))
    {
        if (then) then();
        return;
    }

    RP::request (RP::readMediaAudio, [this, then] (bool granted)
    {
        if (! granted)
            status.setText (T ("Sin permiso de audio: no puedo leer tus carpetas de muestras"),
                            juce::dontSendNotification);
        if (then) then();
    });
}

//  Leaving the browser without confirming puts back whatever the pad held
//  before you started listening — otherwise auditioning through a folder would
//  quietly destroy the sample you already had.
void MainComponent::cancelAudition()
{
    if (browseTargetPad < 0 || auditionedFile == juce::File()) return;
    const int slot = browseTargetPad;
    if (preAuditionSample != nullptr)
    {
        engine.publishSample (slot, preAuditionSample);
        assignSampleToPad (slot, preAuditionSample, preAuditionName);
    }
    else
    {
        padHasSample[(size_t) slot] = false;
        uiSample[(size_t) slot] = nullptr;
        padName[(size_t) slot] = {};
        engine.publishSample (slot, nullptr);
        if (auto* p = pads[slot]) p->setSampleInfo (nullptr, {});
        selectPad (slot);
    }
    auditionedFile = juce::File();
    preAuditionSample = nullptr;
}

void MainComponent::openBrowseForPad (int index)
{
    browseTargetPad = index;
    auditionedFile = juce::File();
    // Remember what the pad held so cancelling an audition puts it back.
    preAuditionSample = uiSample[(size_t) index];
    preAuditionName   = padName[(size_t) index];
    selectPad (index);                       // the target pad reads as selected behind the sheet
    closeAllSheets();
    browseSheet.setVisible (true);
    browseSheet.toFront (false);
    resized();
    repaint();

    // The permission dialog is async: refresh the listing once it resolves, so
    // a folder that read as empty before the grant fills in straight away.
    ensureStoragePermission ([this]
    {
        if (browser != nullptr) browser->refresh();
        selectionChanged();                  // sync the CARGAR button to the selection
    });
}

// A file is only loadable once one is actually picked (folders don't count).
void MainComponent::selectionChanged()
{
    const bool ready = browser != nullptr
                    && browser->getNumSelectedFiles() > 0
                    && browser->getSelectedFile (0).existsAsFile();
    browseLoadButton.setEnabled (ready);
    browseSheet.repaint();                   // the header shows the pick

    //  Audition: one tap loads the file into the pad you are filling AND fires
    //  it, so you choose by ear instead of by filename. CARGAR then just
    //  confirms and closes; the x restores whatever the pad held before, so
    //  browsing through a folder never costs you the old sample.
    if (! ready || browseTargetPad < 0) return;
    const auto f = browser->getSelectedFile (0);
    if (f == auditionedFile) return;          // same pick, do not reload
    auditionedFile = f;

    const int slot = browseTargetPad;
    loader.loadAsync (juce::URL (f), slot, [this, slot, f] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
    {
        if (! ok || sb == nullptr) { status.setText (T ("No se pudo leer: %1", detail), juce::dontSendNotification); return; }
        assignSampleToPad (slot, sb, f.getFileName());
        engine.postNoteOn (slot);
        status.setText (f.getFileName(), juce::dontSendNotification);
    });
}

void MainComponent::fileDoubleClicked (const juce::File& f)
{
    if (f.existsAsFile())
        loadBrowserSelection();              // double-tap a file = load it straight away
}

// --- Projects ---------------------------------------------------------------

void MainComponent::ProjectList::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, names.size())) return;

    auto r = juce::Rectangle<int> (0, 0, w, h);
    if (selected)      { g.setColour (ZatiColours::accent);                 g.fillRect (r); }
    else if (row % 2)  { g.setColour (ZatiColours::ink.withAlpha (0.035f)); g.fillRect (r); }

    const auto fg = selected
        ? (ZatiColours::accent.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink)
        : ZatiColours::ink;
    g.setColour (fg);
    g.setFont (ZatiColours::monoFont (Metrics::fValue, true).withExtraKerningFactor (0.04f));
    g.drawFittedText (names[row], r.reduced (10, 0), juce::Justification::centredLeft, 1, 0.9f);
}

juce::ValueTree MainComponent::captureState() const
{
    juce::ValueTree s ("ZATI");
    {
        // The arrangement is the track. Stored as one row of ints per lane.
        juce::ValueTree song ("song");
        song.setProperty ("bars", engine.getSongLength(), nullptr);
        song.setProperty ("mode", engine.isSongMode(), nullptr);
        for (int lane = 0; lane < Playlist::kLanes; ++lane)
        {
            juce::String row;
            for (int b = 0; b < AudioEngine::kSongBars; ++b)
                row += juce::String (engine.getSongCell (lane, b)) + (b + 1 < AudioEngine::kSongBars ? "," : "");
            song.setProperty ("lane" + juce::String (lane), row, nullptr);
        }
        s.addChild (song, -1, nullptr);
    }
    s.setProperty ("version", 1, nullptr);
    s.setProperty ("bpm", bpmSlider.getValue(), nullptr);
    s.setProperty ("skin", ZatiColours::currentSkin, nullptr);
    s.setProperty ("focusedFx", focusedFx, nullptr);
    s.setProperty ("selectedPattern", selectedPattern, nullptr);

    juce::ValueTree fx ("FX");
    for (int f = 0; f < kNumFx; ++f)
        for (int pi = 0; pi < 3; ++pi)
            fx.setProperty (juce::String (fxDefs[f].name) + juce::String (pi),
                            fxParams[f * 3 + pi]->getValue(), nullptr);
    s.addChild (fx, -1, nullptr);

    juce::ValueTree pads ("PADS");
    for (int i = 0; i < kNumPads; ++i)
    {
        juce::ValueTree p ("PAD");
        p.setProperty ("i", i, nullptr);
        p.setProperty ("name",    padName[(size_t) i],    nullptr);
        p.setProperty ("has",     padHasSample[(size_t) i], nullptr);
        p.setProperty ("pitch",   padPitch[(size_t) i],   nullptr);
        p.setProperty ("cents",   padCents[(size_t) i],   nullptr);
        p.setProperty ("keeplen", padKeepLen[(size_t) i], nullptr);
        p.setProperty ("gain",    padGain[(size_t) i],    nullptr);
        // The mix is part of the track, not of the session.
        p.setProperty ("mute",    engine.isPadMuted (i),  nullptr);
        p.setProperty ("solo",    engine.isPadSoloed (i), nullptr);
        p.setProperty ("start",   padStart01[(size_t) i], nullptr);
        p.setProperty ("end",     padEnd01[(size_t) i],   nullptr);
        p.setProperty ("loop",    padLoop[(size_t) i],    nullptr);
        p.setProperty ("autocut", padSelfCut[(size_t) i], nullptr);
        p.setProperty ("reverse", padReverse[(size_t) i], nullptr);
        p.setProperty ("choke",   padChokeUI[(size_t) i], nullptr);
        p.setProperty ("pan",     padPan[(size_t) i],     nullptr);
        p.setProperty ("attack",  padAttack[(size_t) i],  nullptr);
        p.setProperty ("release", padRelease[(size_t) i], nullptr);
        p.setProperty ("zati",    padZati[(size_t) i],    nullptr);

        //  The six sends, as one string, so adding a seventh effect later
        //  does not need a seventh property or a migration.
        juce::StringArray sends;
        for (int f = 0; f < kNumFx; ++f)
            sends.add (juce::String (engine.getPadSend (i, f), 3));
        p.setProperty ("sends", sends.joinIntoString (","), nullptr);
        pads.addChild (p, -1, nullptr);
    }
    s.addChild (pads, -1, nullptr);

    juce::ValueTree banks ("BANKS");
    for (int b = 0; b < kNumPatterns; ++b)
    {
        juce::ValueTree bk ("BANK");
        bk.setProperty ("i", b, nullptr);
        bk.setProperty ("len", engine.getPatternLength (b), nullptr);
        bk.setProperty ("inChain", patternActiveUI[(size_t) b], nullptr);

        // One hex word per step (16 pads = 16 bits), plus the step pitches —
        // compact enough to stay readable in the XML.
        juce::String steps, notes;
        for (int st = 0; st < kNumSteps; ++st)
        {
            int mask = 0;
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[(size_t) b][(size_t) st][(size_t) p]) mask |= (1 << p);
            steps << juce::String::toHexString (mask) << " ";

            for (int p = 0; p < kNumPads; ++p)
                notes << engine.getStepNote (b, st, p) << " ";
        }
        bk.setProperty ("steps", steps.trim(), nullptr);
        bk.setProperty ("notes", notes.trim(), nullptr);
        banks.addChild (bk, -1, nullptr);
    }
    s.addChild (banks, -1, nullptr);
    return s;
}

void MainComponent::applyState (const juce::ValueTree& s)
{
    if (! s.hasType ("ZATI") && ! s.hasType ("COLORS")) return;   // COLORS: proyectos anteriores al renombrado

    ZatiColours::setSkin ((int) s.getProperty ("skin", 0));
    applySkin();

    bpmSlider.setValue ((double) s.getProperty ("bpm", 120.0), juce::sendNotification);

    if (auto fx = s.getChildWithName ("FX"); fx.isValid())
    {
        // Projects saved before the six-effect rework carry the old three
        // parameters; map what is there and leave the rest at its default.
        auto legacy = [&fx, this] (const char* key, int f, int pi, double dflt)
        {
            fxParam (f, pi).setValue ((double) fx.getProperty (key, dflt), juce::dontSendNotification);
        };
        for (int f = 0; f < kNumFx; ++f)
            for (int pi = 0; pi < 3; ++pi)
            {
                const auto k = juce::String (fxDefs[f].name) + juce::String (pi);
                if (fx.hasProperty (k))
                    fxParam (f, pi).setValue ((double) fx.getProperty (k), juce::dontSendNotification);
                else
                    fxParam (f, pi).setValue (fxDefs[f].spec[pi].def, juce::dontSendNotification);
            }
        if (fx.hasProperty ("cutoff"))
        {
            legacy ("cutoff",  0, 0, 20000.0);
            legacy ("reso",    0, 1, 0.707);
            legacy ("drive",   2, 0, 0.0);
            legacy ("dlyTime", 3, 0, 250.0);
            legacy ("dlyFb",   3, 1, 0.35);
            legacy ("dlyMix",  3, 2, 0.0);
            fxParam (2, 2).setValue ((double) fx.getProperty ("drive", 0.0) > 0.0 ? 1.0 : 0.0,
                                     juce::dontSendNotification);
        }
        for (int f = 0; f < kNumFx; ++f)
        {
            pushFxParam (f, 0); pushFxParam (f, 1); pushFxParam (f, 2);
            fxOn[(size_t) f] = fxParam (f, 2).getValue() > 0.001;
            fxButtons[f]->setToggleState (fxOn[(size_t) f], juce::dontSendNotification);
        }
    }

    if (auto pads = s.getChildWithName ("PADS"); pads.isValid())
    {
        for (const auto& p : pads)
        {
            const int i = (int) p.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (i, kNumPads)) continue;

            padName[(size_t) i]    = p.getProperty ("name", juce::String()).toString();
            padPitch[(size_t) i]   = (float) p.getProperty ("pitch", 0.0);
            padCents[(size_t) i]   = (float) p.getProperty ("cents", 0.0);
            padKeepLen[(size_t) i] = (bool)  p.getProperty ("keeplen", false);
            padGain[(size_t) i]    = (float) p.getProperty ("gain", 0.85);
            engine.setPadMute (i, (bool) p.getProperty ("mute", false));
            engine.setPadSolo (i, (bool) p.getProperty ("solo", false));
            padStart01[(size_t) i] = (float) p.getProperty ("start", 0.0);
            padEnd01[(size_t) i]   = (float) p.getProperty ("end", 1.0);
            padLoop[(size_t) i]    = (bool)  p.getProperty ("loop", false);
            //  Default true: a project saved before AUTOCUT existed has no
            //  such property, and it should come back behaving like every
            //  other pad rather than as the one that stacks.
            padSelfCut[(size_t) i] = (bool)  p.getProperty ("autocut", true);
            padReverse[(size_t) i] = (bool)  p.getProperty ("reverse", false);
            padChokeUI[(size_t) i] = (int)   p.getProperty ("choke", 0);
            padPan[(size_t) i]     = (float) p.getProperty ("pan", 0.0);
            padAttack[(size_t) i]  = (float) p.getProperty ("attack", 2.0);
            padRelease[(size_t) i] = (float) p.getProperty ("release", 5.0);
            padZati[(size_t) i]    = (int)   p.getProperty ("zati", Zati::forPad (i));

            //  Older projects have no sends; those pads go to every effect in
            //  full, which is what they sounded like when they were saved.
            juce::StringArray sends;
            sends.addTokens (p.getProperty ("sends", juce::String()).toString(), ",", "");
            for (int f = 0; f < kNumFx; ++f)
                engine.setPadSend (i, f, f < sends.size() ? sends[f].getFloatValue() : 1.0f);

            // Trim is stored 0..1 but the engine wants samples, and
            // publishSample has just reset the window to the whole file — so
            // it must be pushed back explicitly or every load plays untrimmed.
            if (const int len = engine.getSampleLength (i); len > 0)
            {
                engine.setPadStart (i, (int) (padStart01[(size_t) i] * len));
                engine.setPadEnd   (i, (int) (padEnd01[(size_t) i]   * len));
            }

            engine.setPadPitch   (i, padPitch[(size_t) i] + padCents[(size_t) i] / 100.0f);
            engine.setPadKeepLength (i, padKeepLen[(size_t) i]);
            engine.setPadGain    (i, padGain[(size_t) i]);
            engine.setPadLoop    (i, padLoop[(size_t) i]);
            engine.setPadSelfCut (i, padSelfCut[(size_t) i]);
            engine.setPadReverse (i, padReverse[(size_t) i]);
            engine.setPadChoke   (i, padChokeUI[(size_t) i]);
            engine.setPadPan     (i, padPan[(size_t) i]);
            engine.setPadAttack  (i, padAttack[(size_t) i]);
            engine.setPadRelease (i, padRelease[(size_t) i]);
        }
    }

    if (auto banks = s.getChildWithName ("BANKS"); banks.isValid())
    {
        for (const auto& bk : banks)
        {
            const int b = (int) bk.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (b, kNumPatterns)) continue;

            engine.setPatternLength (b, (int) bk.getProperty ("len", kMinPatLen));
            patternActiveUI[(size_t) b] = (bool) bk.getProperty ("inChain", false);
            if (auto* btn = patternButtons[b])
                btn->setToggleState (patternActiveUI[(size_t) b], juce::dontSendNotification);

            juce::StringArray st, nt;
            st.addTokens (bk.getProperty ("steps", "").toString(), " ", "");
            nt.addTokens (bk.getProperty ("notes", "").toString(), " ", "");
            st.removeEmptyStrings(); nt.removeEmptyStrings();

            for (int s2 = 0; s2 < kNumSteps; ++s2)
            {
                const int mask = s2 < st.size() ? (int) st[s2].getHexValue32() : 0;
                for (int p = 0; p < kNumPads; ++p)
                {
                    const bool on = (mask & (1 << p)) != 0;
                    pattern[(size_t) b][(size_t) s2][(size_t) p] = on;
                    engine.setStep (b, s2, p, on);

                    const int ni = s2 * kNumPads + p;
                    engine.setStepNote (b, s2, p, ni < nt.size() ? nt[ni].getIntValue() : 0);
                }
            }
        }
        rebuildChain();
    }

    for (int i = 0; i < kNumPads; ++i)
        if (auto* pb = pads[i]) pb->setZati (padZati[(size_t) i]);

    selectedPattern = juce::jlimit (0, kNumPatterns - 1, (int) s.getProperty ("selectedPattern", 0));
    patternSlider.setValue (selectedPattern, juce::dontSendNotification);   // 0-based; its text adds the +1
    patternSlider.updateText();
    engine.setEditPattern (selectedPattern);
    lengthSlider.setValue (engine.getPatternLength (selectedPattern), juce::dontSendNotification);

    if (auto song = s.getChildWithName ("song"); song.isValid())
    {
        engine.clearSong();
        engine.setSongLength ((int) song.getProperty ("bars", 8));
        songLenSlider.setValue ((double) engine.getSongLength(), juce::dontSendNotification);
        const bool sm = (bool) song.getProperty ("mode", false);
        engine.setSongMode (sm);
        songModeBtn.setToggleState (sm, juce::dontSendNotification);
        for (int lane = 0; lane < Playlist::kLanes; ++lane)
        {
            auto toks = juce::StringArray::fromTokens (song.getProperty ("lane" + juce::String (lane)).toString(), ",", "");
            for (int b = 0; b < juce::jmin (toks.size(), AudioEngine::kSongBars); ++b)
                engine.setSongCell (lane, b, toks[b].getIntValue());
        }
        songPage = 0;
        refreshSong();
    }

    focusFx ((int) s.getProperty ("focusedFx", 0));
    refreshRack();
    for (int i = 0; i < kNumPads; ++i)
    {
        if (mixFaders[i] != nullptr) mixFaders[i]->setValue (padGain[(size_t) i], juce::dontSendNotification);
        if (mixPans[i]   != nullptr) mixPans[i]  ->setValue (padPan[(size_t) i],  juce::dontSendNotification);
    }
    refreshMixStrip();
    selectPad (juce::jmax (0, selectedPad));
    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    resized();
    repaint();
}

void MainComponent::saveProject (const juce::String& rawName)
{
    const auto name   = ProjectStore::sanitise (rawName);
    const auto folder = ProjectStore::folderFor (name);
    folder.createDirectory();

    int written = 0, failed = 0;
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto dest = ProjectStore::sampleFile (folder, i);
        if (auto sb = uiSample[(size_t) i]; sb != nullptr && sb->buffer.getNumSamples() > 0)
        {
            if (ProjectStore::writeSample (dest, sb->buffer, sb->sourceSampleRate)) ++written;
            else                                                                    ++failed;
        }
        else
        {
            dest.deleteFile();      // pad emptied since the last save
        }
    }

    const auto xml = captureState().toXmlString();
    const bool ok  = folder.getChildFile ("project.xml").replaceWithText (xml);

    currentProject = name;
    repaint (headerArea);
    refreshProjectList();

    status.setText (ok && failed == 0
                        ? T ("Guardado \"%1\"  [%2 pads]", name, juce::String (written))
                        : T ("Guardado con fallos: %1 pads no se escribieron", juce::String (failed)),
                    juce::dontSendNotification);
    projSheet.repaint();
}

void MainComponent::loadProject (const juce::String& name)
{
    const auto folder = ProjectStore::folderFor (name);
    const auto xmlFile = folder.getChildFile ("project.xml");
    if (! xmlFile.existsAsFile())
    {
        status.setText (T ("No encuentro el proyecto \"%1\"", name), juce::dontSendNotification);
        return;
    }

    auto xml = juce::parseXML (xmlFile);
    if (xml == nullptr)
    {
        status.setText (T ("Proyecto ilegible: %1", name), juce::dontSendNotification);
        return;
    }

    // Stop first: loading rewrites every pattern bank and pad under the
    // sequencer's feet otherwise.
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setButtonText (T ("PLAY"));
    engine.setPlaying (false);

    int restored = 0, missing = 0;
    for (int i = 0; i < kNumPads; ++i)
    {
        auto sb = ProjectStore::readSample (ProjectStore::sampleFile (folder, i));
        if (sb != nullptr) { assignSampleToPad (i, sb, padName[(size_t) i]); ++restored; }
        else
        {
            uiSample[(size_t) i] = nullptr;
            padHasSample[(size_t) i] = false;
            padName[(size_t) i] = {};
            if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
        }
    }

    applyState (juce::ValueTree::fromXml (*xml));

    // Names live in the state, so re-stamp the tiles after applyState.
    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i], padStart01[(size_t) i], padEnd01[(size_t) i]);

    for (const auto& c : juce::ValueTree::fromXml (*xml).getChildWithName ("PADS"))
        if ((bool) c.getProperty ("has", false)
            && uiSample[(size_t) (int) c.getProperty ("i", 0)] == nullptr)
            ++missing;

    currentProject = name;
    repaint (headerArea);
    closeAllSheets();
    status.setText (missing > 0
                        ? T ("Abierto \"%1\"  [%2 pads, %3 sin audio]", name,
                             juce::String (restored), juce::String (missing))
                        : T ("Abierto \"%1\"  [%2 pads]", name, juce::String (restored)),
                    juce::dontSendNotification);
}

void MainComponent::deleteProject (const juce::String& name)
{
    ProjectStore::folderFor (name).deleteRecursively();
    if (currentProject == name)
    {
        currentProject = {};
        repaint (headerArea);
    }
    refreshProjectList();
    status.setText (T ("Borrado \"%1\"", name), juce::dontSendNotification);
    projSheet.repaint();
}

void MainComponent::newProject()
{
    playButton.setToggleState (false, juce::dontSendNotification);
    playButton.setButtonText (T ("PLAY"));
    engine.setPlaying (false);

    for (int i = 0; i < kNumPads; ++i)
    {
        uiSample[(size_t) i] = nullptr;
        padHasSample[(size_t) i] = false;
        padName[(size_t) i] = {};
        if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
    }
    for (int b = 0; b < kNumPatterns; ++b)
    {
        engine.clearPattern (b);
        engine.setPatternLength (b, kMinPatLen);
        for (auto& row : pattern[(size_t) b]) row.fill (false);
        patternActiveUI[(size_t) b] = false;
        if (auto* btn = patternButtons[b]) btn->setToggleState (false, juce::dontSendNotification);
    }
    rebuildChain();

    selectedPattern = 0;
    selectedStep = -1;
    currentProject = {};
    repaint (headerArea);

    //  A new project means there is nothing to come back to: without this the
    //  next launch would restore the machine the user just emptied.
    session.clear();
    session.adopt (uiSample.data(), kNumPads);

    selectPad (0);
    closeAllSheets();
    status.setText (T ("Proyecto nuevo"), juce::dontSendNotification);
}

void MainComponent::refreshProjectList()
{
    projModel.names = ProjectStore::list();
    projList.updateContent();

    const int sel = projModel.names.indexOf (currentProject);
    if (sel >= 0) projList.selectRow (sel);
    else          projList.deselectAllRows();
    projList.repaint();

    //  The sheet is as tall as this list, so saving or deleting a project
    //  changes its height. Without this the card keeps the size it had when
    //  it opened and the list scrolls inside a box that no longer fits it.
    resized();
}

//  Each strip is named the way the pad is: its colour, its number, its sample.
//  A mixer that says "01..16" and nothing else makes you count pads.
//  Feed the timeline from the engine and keep the palette honest about which
//  brush is loaded — placing the wrong block is the easiest mistake here.
void MainComponent::refreshSong()
{
    const int bars = engine.getSongLength();
    for (int lane = 0; lane < Playlist::kLanes; ++lane)
        for (int b = 0; b < bars; ++b)
            songCells[lane * bars + b] = engine.getSongCell (lane, b);

    for (int i = 0; i < songPatBtns.size(); ++i)
        songPatBtns[i]->setToggleState (songBrush == i + 1, juce::dontSendNotification);
    songPadModeBtn.setToggleState (songBrush < 0, juce::dontSendNotification);
    songClearBtn.setToggleState   (songBrush == 0, juce::dontSendNotification);
    songPadModeBtn.setButtonText (songBrush < 0
        ? T ("SONIDO|cancion") + " " + juce::String (-songBrush).paddedLeft ('0', 2)
        : T ("SONIDO|cancion"));

    songGrid.setSource (songCells, gridZati, bars, songPage,
                        engine.isSongMode() && engine.isPlaying() ? engine.getSongBar() : -1);
    songSheet.repaint();
}

void MainComponent::paintSongSheetContent (juce::Graphics& g)
{
    if (songSheet.sheetBounds.isEmpty()) return;
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("SONG"), songSheet.sheetBounds.reduced (14, 10).removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    const juce::String hint = songBrush == 0 ? "toca un bloque para borrarlo"
                            : songBrush < 0  ? "toca un compas para soltar el sonido"
                                             : "toca un compas para poner el patron";
    //  The close button lives in this same row, so the hint has to stop short
    //  of it - right-aligning into the full width ran the sentence underneath
    //  the X and off the card. Fitted, so a longer wording shrinks instead of
    //  losing its last word.
    auto hintRow = songSheet.sheetBounds.reduced (14, 10).removeFromTop (16);
    hintRow.setRight (juce::jmin (hintRow.getRight(), songCloseButton.getX() - Metrics::xs));
    g.drawFittedText (hint, hintRow, juce::Justification::centredRight, 1, 0.85f);
}

void MainComponent::paintMixSheetContent (juce::Graphics& g)
{
    if (mixSheet.sheetBounds.isEmpty()) return;

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (engine.anySolo() ? "MIX  ·  SOLO ACTIVO" : "MIX",
                mixSheet.sheetBounds.reduced (14, 12).removeFromTop (16), juce::Justification::centredLeft);

    for (int i = 0; i < kNumPads; ++i)
    {
        if (mixFaders[i] == nullptr) continue;
        const auto fr = mixFaders[i]->getBounds();
        const auto frag = Zati::colour (padZati[(size_t) i]);
        const bool has = padHasSample[(size_t) i];

        auto chip = juce::Rectangle<int> (mixSheet.sheetBounds.getX() + 18, fr.getY() + 4, 22, fr.getHeight() - 8);
        g.setColour (has ? frag : ZatiColours::padBorder.withAlpha (0.4f));
        g.fillRect (chip);
        g.setColour (has ? ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white)
                         : ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (juce::String (i + 1).paddedLeft ('0', 2), chip, juce::Justification::centred);

        g.setColour (has ? ZatiColours::ink.withAlpha (0.8f) : ZatiColours::inkDim.withAlpha (0.5f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta));
        //  The name gets everything between its colour chip and the fader,
        //  rather than a fixed 40 px that left a gap on one side and cut the
        //  name to eight characters on the other.
        const int nameX = chip.getRight() + 6;
        g.drawText (has && padName[(size_t) i].isNotEmpty() ? padName[(size_t) i].toUpperCase()
                                                            : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")),
                    nameX, fr.getY(), juce::jmax (24, fr.getX() - 6 - nameX), fr.getHeight(),
                    juce::Justification::centredLeft, true);
    }
}

//  Solo is a state of the whole mixer, not of one strip: every other channel
//  has to look silenced or you cannot tell why they went quiet.
void MainComponent::refreshMixStrip()
{
    const bool any = engine.anySolo();
    for (int i = 0; i < kNumPads; ++i)
    {
        if (mixMutes[i] != nullptr) mixMutes[i]->setToggleState (engine.isPadMuted (i), juce::dontSendNotification);
        if (mixSolos[i] != nullptr) mixSolos[i]->setToggleState (engine.isPadSoloed (i), juce::dontSendNotification);
        if (mixFaders[i] != nullptr)
        {
            const bool audible = ! engine.isPadMuted (i) && (! any || engine.isPadSoloed (i));
            mixFaders[i]->setAlpha (audible ? 1.0f : 0.45f);
            if (mixPans[i] != nullptr) mixPans[i]->setAlpha (audible ? 1.0f : 0.45f);
        }
    }
    mixClearSolo.setEnabled (any);
    mixSheet.repaint();
}

void MainComponent::refreshRack()
{
    rackPad = juce::jlimit (0, kNumPads - 1, rackPad);
    for (int i = 0; i < rackPadBtns.size(); ++i)
        rackPadBtns[i]->setToggleState (i == rackPad, juce::dontSendNotification);
    for (int f = 0; f < rackSends.size(); ++f)
        rackSends[f]->setValue (engine.getPadSend (rackPad, f), juce::dontSendNotification);
    rackSheet.repaint();
}

//  Each row says three things: which effect, whether it is switched on at
//  all, and how much of THIS pad is going into it. The middle one matters
//  because a send at 100 into an effect whose own MIX is down makes no
//  sound, and without saying so the fader looks broken.
//  The sheet says three things, in the order you need them: what the button is
//  about to do, in words; how many pieces; and the exact list of pads it will
//  write. Nothing here is a surprise by the time the red button is reachable.
void MainComponent::paintChopSheetContent (juce::Graphics& g)
{
    if (chopSheet.sheetBounds.isEmpty()) return;

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const int sp = juce::jmax (0, selectedPad);
    auto inner = chopSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);

    auto titleRow = inner.removeFromTop (32).withTrimmedTop (8);
    titleRow.setRight (juce::jmin (titleRow.getRight(), chopCloseButton.getX() - Metrics::xs));
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("AUTO CHOP") + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
                + (padName[(size_t) sp].isNotEmpty() ? "  " + dot + "  " + padName[(size_t) sp].toUpperCase()
                                                     : juce::String()),
                titleRow, juce::Justification::centredLeft, true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    g.drawFittedText (T ("Parte este sample en trozos iguales y los reparte por los pads. "
                         "El pad de origen se queda con el primero."),
                      inner.removeFromTop (40), juce::Justification::topLeft, 3, 1.0f);

    inner.removeFromTop (Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.75f));
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
    g.drawText (T ("TROZOS"), inner.removeFromTop (14), juce::Justification::centredLeft);

    inner.removeFromTop (Metrics::hit + Metrics::sm + Metrics::hit + Metrics::md);

    //  The plan, in pad numbers. This is the whole point of the sheet: the
    //  old one-tap chop was destructive precisely because it never said this.
    const auto targets = chopTargets (chopSlices, chopOnlyEmpty);
    auto planned = inner.removeFromTop (40);

    if (uiSample[(size_t) sp] == nullptr)
    {
        g.setColour (ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true));
        g.drawFittedText (T ("Este pad no tiene sonido que cortar."),
                          planned, juce::Justification::topLeft, 1, 0.8f);
        return;
    }

    juce::StringArray nums;
    for (int i = 0; i < targets.size(); ++i)
        nums.add (juce::String (targets[i] + 1).paddedLeft ('0', 2));

    int overwritten = 0;
    for (int i = 1; i < targets.size(); ++i)
        if (padHasSample[(size_t) targets[i]]) ++overwritten;

    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    g.drawFittedText (T ("va a pads: %1", nums.joinIntoString (" ")),
                      planned.removeFromTop (22), juce::Justification::topLeft, 2, 0.8f);

    juce::String warn;
    if (targets.size() < chopSlices)
        warn = "solo caben " + juce::String (targets.size()) + " sin pisar nada";
    else if (overwritten > 0)
        warn = "PISA " + juce::String (overwritten) + (overwritten == 1 ? " pad con sonido" : " pads con sonido");
    else
        warn = "no pisa ningun pad con sonido";

    g.setColour (overwritten > 0 ? ZatiColours::red : ZatiColours::inkDim);
    g.drawFittedText (warn, planned, juce::Justification::topLeft, 1, 0.8f);
}

void MainComponent::paintRackSheetContent (juce::Graphics& g)
{
    if (rackSheet.sheetBounds.isEmpty()) return;

    auto inner = rackSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    const juce::String nm  = padName[(size_t) rackPad];
    auto titleRow = inner.removeFromTop (16);
    titleRow.setRight (juce::jmin (titleRow.getRight(), rackCloseButton.getX() - Metrics::xs));
    g.drawText (T ("RACK") + "  " + dot + "  " + T ("PAD %1", juce::String (rackPad + 1))
                + (nm.isNotEmpty() ? "  " + dot + "  " + nm.toUpperCase() : juce::String()),
                titleRow, juce::Justification::centredLeft, true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    g.drawFittedText (T ("cuanto de este pad entra en cada efecto"),
                      inner.removeFromTop (14), juce::Justification::centredLeft, 1, 0.75f);

    for (int f = 0; f < kNumFx; ++f)
    {
        if (rackSends[f] == nullptr) continue;
        const auto r = rackSends[f]->getBounds();
        const bool on = fxOn[(size_t) f];

        g.setColour (on ? ZatiColours::ink : ZatiColours::inkDim.withAlpha (0.55f));
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.10f));
        g.drawText (fxDefs[f].name, rackSheet.sheetBounds.getX() + Metrics::lg, r.getY(),
                    50, r.getHeight(), juce::Justification::centredLeft);

        //  An effect that is switched off is not hidden, it is greyed: the
        //  send you set now is the send it will use when you switch it on.
        rackSends[f]->setAlpha (on ? 1.0f : 0.5f);
    }
}

void MainComponent::paintProjSheetContent (juce::Graphics& g)
{
    if (projSheet.sheetBounds.isEmpty()) return;

    auto inner = projSheet.sheetBounds.reduced (12, 6);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("PROYECTOS"), inner.removeFromTop (16), juce::Justification::centredLeft);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    //  MEDIR, TEST and the close button all hang off the right of this same
    //  band, so the subtitle has to end before they start - written across
    //  the full width it disappeared under MEDIR mid-sentence.
    auto subRow = inner.removeFromTop (14);
    subRow.setRight (juce::jmin (subRow.getRight(), measureButton.getX() - Metrics::xs));
    g.drawFittedText (currentProject.isNotEmpty()
                          ? T ("abierto: %1", currentProject)
                          : (projModel.names.isEmpty()
                                 ? T ("sin proyectos - GUARDAR crea el primero")
                                 : T ("elige uno de la lista")),
                      subRow, juce::Justification::centredLeft, 1, 0.8f);

    paintAudioInfo (g, audioInfoArea);

    // Row labels for the two chip rows. The asterisk marks the driver's own
    // burst size: on Android that is the fast path, and anything below it
    // buys nothing.
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
    if (! bufRowArea.isEmpty())
        g.drawText (T ("BUFER"), bufRowArea.withWidth (52), juce::Justification::centredLeft);
    if (! rateRowArea.isEmpty())
        g.drawText (T ("RELOJ"), rateRowArea.withWidth (52), juce::Justification::centredLeft);
        g.drawText (T ("IDIOMA"), langRowArea.withWidth (52), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------------------
//  Export — the bounce
// ---------------------------------------------------------------------------

juce::String MainComponent::exportSourceLabel() const
{
    if (engine.isSongMode())        return "CANCION";
    if (engine.getChainLength() > 0) return "CADENA (" + juce::String (engine.getChainLength()) + " patrones)";
    return "PATRON P" + juce::String (engine.getEditPattern() + 1);
}

void MainComponent::startExport (bool stems)
{
    if (exportJob != nullptr) return;

    if (engine.lengthInSteps() <= 0 || ! engine.hasContentToRender())
    {
        exportOk = false;
        exportStatus = "no hay nada grabado en " + exportSourceLabel().toLowerCase();
        exportSheet.repaint();
        return;
    }

    auto base = (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI"))
                  .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ")
                  .trim().replaceCharacter (' ', '-');
    if (base.isEmpty()) base = "ZATI";

    exportOk = false;
    exportStatus = "renderizando...";
    exportJob = std::make_unique<Exporter> (engine, uiSample, padName,
                                            ProjectStore::exports().getChildFile (base),
                                            base, stems, deviceSampleRate);

    exportMasterButton.setVisible (false);
    exportStemsButton.setVisible (false);
    exportCancelButton.setVisible (true);
    exportJob->startThread (juce::Thread::Priority::normal);
    exportSheet.repaint();
}

void MainComponent::pollExport()
{
    // The audio path can change under us (headphones in, a call, a route
    // switch), so the readout is refreshed while you are looking at it.
    if (projSheet.isVisible()) projSheet.repaint();
    if (measuring && ! engine.isProbing()) finishMeasure();

    if (exportJob == nullptr) return;

    if (! exportJob->finished.load (std::memory_order_acquire))
    {
        exportSheet.repaint();
        return;
    }

    exportOk     = exportJob->resultOk;
    exportStatus = exportJob->resultText;
    exportJob.reset();

    exportMasterButton.setVisible (true);
    exportStemsButton.setVisible (true);
    exportCancelButton.setVisible (false);
    exportSheet.repaint();
}

void MainComponent::paintExportSheetContent (juce::Graphics& g)
{
    if (exportSheet.sheetBounds.isEmpty()) return;

    auto inner = exportSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    inner.removeFromTop (2);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("EXPORTAR"), inner.removeFromTop (18), juce::Justification::centredLeft);
    inner.removeFromTop (10);

    // What is going to be rendered, and how long it will be. Stated before
    // you press, not after: a bounce is the one action here you cannot undo
    // by tapping again.
    const int steps = engine.lengthInSteps();
    const double secs = steps * (60.0 / juce::jmax (20.0, engine.getBpm())) * 0.25;
    int loaded = 0;
    for (auto& s : uiSample) if (s != nullptr) ++loaded;

    auto line = [&g, &inner] (const juce::String& k, const juce::String& v, juce::Colour vc)
    {
        auto r = inner.removeFromTop (17);
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
        g.drawText (k, r.removeFromLeft (76), juce::Justification::centredLeft);
        g.setColour (vc);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawText (v, r, juce::Justification::centredLeft);
    };

    line ("fuente", exportSourceLabel(), ZatiColours::ink);
    line ("duracion", steps > 0 ? juce::String (secs, 1) + " s  ·  " + juce::String (steps / 16) + " compases"
                                : juce::String ("vacio"),
          steps > 0 ? ZatiColours::ink : ZatiColours::red);
    line ("pistas", juce::String (loaded) + " pads con muestra", ZatiColours::ink);
    line ("destino", "ZATI/Exports/" + (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI")),
          ZatiColours::inkDim);

    inner.removeFromTop (6);

    // Progress, then the verdict.
    if (exportJob != nullptr)
    {
        auto bar = inner.removeFromTop (8).reduced (0, 2);
        g.setColour (ZatiColours::padBorder.withAlpha (0.4f));
        g.fillRect (bar);
        g.setColour (ZatiColours::accent);
        g.fillRect (bar.withWidth ((int) ((float) bar.getWidth()
                        * juce::jlimit (0.0f, 1.0f, exportJob->progress.load (std::memory_order_relaxed)))));

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("escribiendo %1", juce::String (exportJob->passDone.load (std::memory_order_relaxed) + 1)
                                       + "/" + juce::String (exportJob->passTotal.load (std::memory_order_relaxed))),
                    inner.removeFromTop (16), juce::Justification::centredLeft);
    }
    else if (exportStatus.isNotEmpty())
    {
        g.setColour (exportOk ? ZatiColours::accent : ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawFittedText (exportOk ? T ("listo: %1", exportStatus) : exportStatus,
                          inner.removeFromTop (24), juce::Justification::topLeft, 2);
    }
    else
    {
        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
        g.drawFittedText (T ("MASTER = un WAV con lo que oyes.  PISTAS = el master mas un WAV "
                             "por pad, para mezclar fuera."),
                          inner.removeFromTop (26), juce::Justification::topLeft, 2);
    }
}

// The audio path, measured rather than assumed. Everything here comes from
// the device itself; nothing is a constant we hope is true.
void MainComponent::paintAudioInfo (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty()) return;

    g.setColour (ZatiColours::screenBg);
    g.fillRoundedRectangle (area.toFloat(), 3.0f);
    auto inner = area.reduced (10, 7);

    auto* dev = deviceManager.getCurrentAudioDevice();

    g.setColour (ZatiColours::lcdDim);
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.20f));
    g.drawText (T ("AUDIO"), inner.removeFromTop (12), juce::Justification::centredLeft);

    //  What the app decided this phone can carry. It is not a setting, it is
    //  a report - and it is true whether or not a stream ever opened, so it
    //  goes above the part that needs one.
    {
        auto r = inner.removeFromTop (14);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("EQUIPO"), r.removeFromLeft (54), juce::Justification::centredLeft);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawFittedText (DeviceTier::describe(), r, juce::Justification::centredLeft, 1, 0.7f);
    }

    if (dev == nullptr)
    {
        g.setColour (ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawText (T ("sin dispositivo de audio"), inner, juce::Justification::centredLeft);
        return;
    }

    const double sr    = dev->getCurrentSampleRate();
    const int    block = dev->getCurrentBufferSizeSamples();
    const int    outL  = dev->getOutputLatencyInSamples();
    auto msOf = [sr] (double samples) { return sr > 0.0 ? samples * 1000.0 / sr : 0.0; };

    //  Oboe's figure is already the whole path from writing a block to the
    //  speaker moving, buffer included, so adding our block size to it was
    //  counting the same milliseconds twice.
    const double devMs   = msOf ((double) outL);
    const double blockMs = msOf ((double) block);
    const double totalMs = devMs > 0.0 ? devMs : blockMs;

    auto line = [&g, &inner] (const juce::String& k, const juce::String& v, juce::Colour c)
    {
        auto r = inner.removeFromTop (14);
        g.setColour (ZatiColours::lcdDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (k, r.removeFromLeft (54), juce::Justification::centredLeft);
        g.setColour (c);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        //  Half of these values come from the OS - device names, granted
        //  stream terms - so none of them has a length we can plan around.
        g.drawFittedText (v, r, juce::Justification::centredLeft, 1, 0.7f);
    };

    line (T ("ruta"),  dev->getTypeName() + " / " + dev->getName(), ZatiColours::lcdFg);
    line (T ("reloj"), juce::String ((int) sr) + " Hz", ZatiColours::lcdFg);
    const auto sizes = dev->getAvailableBufferSizes();
    const int  burst  = sizes.isEmpty() ? block : sizes.getFirst();
    line (T ("bufer"), juce::String (block) + " · " + juce::String (msOf (block), 1) + " ms"
                     + (block <= burst ? "  (" + T ("rafaga, el minimo") + ")"
                                       : "  (" + T ("rafaga %1", juce::String (burst)) + ")"),
          ZatiColours::lcdFg);

    //  Under ~15 ms a pad feels like a pad. Past ~30 ms you hear yourself
    //  arrive late and you start compensating, which is when an instrument
    //  stops being one.
    const auto verdict = totalMs <= 15.0 ? ZatiColours::lcdFg
                       : totalMs <= 30.0 ? ZatiColours::yellow
                                         : ZatiColours::red;
    line (T ("salida"), juce::String (totalMs, 1) + " ms  "
                    + (totalMs <= 15.0 ? T ("rapida")
                     : totalMs <= 30.0 ? T ("aceptable") : T ("LENTA")), verdict);

    //  Say WHOSE milliseconds these are. Our share is the block; everything
    //  past it belongs to the phone's audio path, and no setting in this app
    //  can give it back. Without this split a bad phone reads as a bad app.
    g.setColour (ZatiColours::lcdDim.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (9.0f, false));
    juce::String note = T ("de esos, %1 ms son el bufer", juce::String (blockMs, 1));
    if (totalMs - blockMs > 20.0)
    {
        //  Once the probe has told us we never got an MMAP stream, the leftover
        //  milliseconds have a name. Saying "el telefono" invited another week
        //  of looking for a setting; naming AudioFlinger closes the question.
        if (block > burst)
            note += " - " + T ("baja el bufer");
        else if (fastPath.ran && fastPath.mmapKnown && ! fastPath.mmapUsed)
            note += " - " + T ("el resto es el mezclador de Android, sin MMAP en este movil");
        else
            note += " - " + T ("el resto es el telefono, no lo pone nadie mas bajo");
    }
    g.drawFittedText (note, inner.removeFromTop (11), juce::Justification::centredLeft, 1, 0.7f);

    //  Whether the phone allows the fast lane at all. JUCE already asks Oboe
    //  for exclusive + low latency, so if the answer here is "no soportado"
    //  the remaining milliseconds are the device's and no build of this app
    //  will get them back.
    const auto policy = AudioPath::mmapPolicy();
    const auto excl   = AudioPath::exclusivePolicy();
    line (T ("mmap"), AudioPath::describe (policy) + " · " + T ("excl") + " " + AudioPath::describe (excl),
          policy == AudioPath::Mmap::Never || excl == AudioPath::Mmap::Never ? ZatiColours::red
        : policy == AudioPath::Mmap::Unknown ? ZatiColours::lcdDim
                                             : ZatiColours::lcdFg);

    //  ...and whether it granted it to US. "disponible" above is a capability;
    //  this line is the verdict on an actual stream, which is the only one
    //  that decides what the pads feel like.
    line (T ("via"), AudioPath::describe (fastPath),
          fastPath.exclusive ? ZatiColours::lcdFg
        : ! fastPath.ran     ? ZatiColours::lcdDim
        : fastPath.mmapUsed  ? ZatiColours::yellow   // shared, but still MMAP
                             : ZatiColours::red);    // AudioFlinger's mixer

    //  The measurement, kept visually apart from everything the device
    //  merely claims about itself.
    if (measuring)
        line (T ("medido"), T ("escuchando..."), ZatiColours::yellow);
    else if (measuredMs >= 0.0f)
        line (T ("medido"), T ("%1 ms ida y vuelta", juce::String (measuredMs, 1)),
              measuredMs <= 30.0f ? ZatiColours::lcdFg
            : measuredMs <= 60.0f ? ZatiColours::yellow : ZatiColours::red);

    g.setColour (ZatiColours::lcdDim.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (9.0f, false));
    g.drawFittedText (measureNote.isNotEmpty() ? measureNote
                                               : T ("MEDIR emite un click y lo escucha con el micro"),
                      inner.removeFromTop (11), juce::Justification::centredLeft, 1, 0.7f);
}

//  Take the smallest buffer the driver offers, which on Android is exactly
//  one native burst.
//
//  This is not a micro-optimisation, it is the difference between an
//  instrument and a toy. JUCE's own default targets a 40 ms buffer on a
//  low-latency device (juce_HighPerformanceAudioHelpers_android.h,
//  getDefaultBufferSize), so on a phone whose burst is 256 frames it stacks
//  EIGHT of them: 2048 frames, 42.7 ms of buffer and ~127 ms from callback to
//  speaker. Measured on the target device, that is what we were shipping.
//
//  getAvailableBufferSizes() is built as multiples of the native burst
//  starting at one, so element zero IS the burst — the fast path Oboe was
//  opened for in the first place.
//
//  One burst can glitch on a busy phone. That is why the BUFER chips exist:
//  if it crackles, step up one and lose ~5 ms. Better to start tight and let
//  you back off than to start slow and never tell you.
void MainComponent::useLowestLatency()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return;

    const auto sizes = dev->getAvailableBufferSizes();
    if (sizes.isEmpty()) return;

    //  The smallest the driver offers, full stop. On Android that list is
    //  built as multiples of the hardware burst starting at one, so the first
    //  entry IS the burst and nothing below it exists to ask for.
    //
    //  There used to be a "worth at least 3 ms" guard here. On a phone whose
    //  burst is 256 it changes nothing, but on one whose burst is 96 or 128 it
    //  would have quietly skipped past the fast path and doubled the latency
    //  to protect against a problem that only desktop drivers have.
    int burst = sizes.getFirst();
    for (int v : sizes) if (v > 0 && v < burst) burst = v;

    //  ...times what the device can actually keep up with. One burst is the
    //  fast path and what any decent phone gets; on an entry-level one a block
    //  that cannot be rendered in time is an under-run, and an under-run is a
    //  click - worse than the extra milliseconds it costs to avoid it.
    burst *= juce::jmax (1, DeviceTier::profile().bufferBursts);

    //  Only among the sizes the driver actually offers.
    if (! sizes.contains (burst))
    {
        int best = sizes.getFirst();
        for (int v : sizes) if (v >= burst && (best < burst || v < best)) best = v;
        burst = best;
    }

    if (burst <= 0 || burst == dev->getCurrentBufferSizeSamples()) return;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.bufferSize = burst;
    deviceManager.setAudioDeviceSetup (setup, true);
}

//  Emit a click, hear it back, and report the gap. This needs the microphone
//  open, which means the stream is reopened as input+output for the duration:
//  what comes out is the ROUND TRIP, mic path included, not the output path
//  alone. That is the figure OboeTester quotes and the one worth comparing,
//  but it is a ceiling — the real output-only latency is lower.
void MainComponent::startMeasure()
{
    if (measuring) return;

    using RP = juce::RuntimePermissions;
    auto begin = [this]
    {
        measuring = true;
        measuredMs = -1.0f;
        measureNote = T ("midiendo...");
        measureButton.setEnabled (false);
        setAudioChannels (1, 2);         // the probe has to hear itself
        useLowestLatency();
        measuredOutMs = measuredInMs = 0.0f;   // filled in finishMeasure()
        engine.startLatencyProbe();
        projSheet.repaint();
    };

    if (! RP::isRequired (RP::recordAudio) || RP::isGranted (RP::recordAudio))
        begin();
    else
        RP::request (RP::recordAudio, [this, begin] (bool granted)
        {
            if (granted) begin();
            else { measureNote = T ("sin permiso de microfono"); projSheet.repaint(); }
        });
}

void MainComponent::finishMeasure()
{
    if (! measuring || engine.isProbing()) return;

    measuredMs = engine.finishLatencyProbe();
    measuring = false;

    //  Read the two halves HERE, while the duplex stream is still open and has
    //  been running for the whole probe. Reading them right after asking for
    //  the input - which is what this used to do - reads a device that Oboe
    //  has not finished reopening: it answered 4.79 ms out and 0 ms in, and an
    //  input latency of zero does not exist.
    if (auto* dev = deviceManager.getCurrentAudioDevice())
    {
        const double sr = dev->getCurrentSampleRate() > 0.0 ? dev->getCurrentSampleRate() : 48000.0;
        measuredOutMs = (float) (dev->getOutputLatencyInSamples() * 1000.0 / sr);
        measuredInMs  = (float) (dev->getInputLatencyInSamples()  * 1000.0 / sr);
    }

    setAudioChannels (0, 2);             // back to output-only
    useLowestLatency();
    measureButton.setEnabled (true);

    //  Sound travels about 34 cm per millisecond, so holding the phone at
    //  arm's length adds a couple of ms of air. Worth saying, because at
    //  these numbers a couple of ms is not noise.
    //  Say where the milliseconds went. Measuring needs the microphone, and
    //  opening an input stream drops BOTH streams off the fast path, so this
    //  figure is the duplex configuration — not the one you play in. Without
    //  that split the number reads as an indictment of the app when most of
    //  it is the phone's capture path.
    //  One decimal, not zero: juce::String (x, 0) does not mean "no decimals" -
    //  it falls through to the generic format and prints 4.79167 in a line that
    //  has no room for it.
    //
    //  And an input latency of 0 is not a measurement. Oboe only reports one
    //  when the driver supports timestamps on the capture stream, which this
    //  one does not (isInputLatencyDetectionSupported comes back false), so
    //  JUCE leaves it at zero. Printing that zero blamed the whole round trip
    //  on the output. What we can honestly say is the subtraction.
    const float outMs = measuredOutMs;
    const float inMs  = measuredInMs > 0.0f ? measuredInMs
                                            : juce::jmax (0.0f, measuredMs - outMs);

    measureNote = measuredMs < 0.0f
                    ? T ("no oi el click - sube el volumen y no tapes el micro")
                    : T ("con micro abierto: salida %1 + entrada %2 ms%3",
                         juce::String (outMs, 1), juce::String (inMs, 1),
                         measuredInMs > 0.0f ? juce::String() : " " + T ("(por resta)"))
                        + ". " + T ("Tocando solo sales %1 ms", Lang::ltr (juce::String (outMs, 1)));
    refreshAudioOptions();
}

// Build the chips from what THIS device actually offers. Nothing is
// hardcoded: a phone that only does 48 kHz shows one clock, and the burst
// sizes are the ones the driver will really accept.
void MainComponent::refreshAudioOptions()
{
    bufButtons.clear();
    rateButtons.clear();

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) { resized(); return; }

    const int    curBuf  = dev->getCurrentBufferSizeSamples();
    const double curRate = dev->getCurrentSampleRate();
    //  The burst, not getDefaultBufferSize(): that one is JUCE's 40 ms
    //  target and marking it "native" is what hid this problem.
    const int    natBuf  = dev->getAvailableBufferSizes().isEmpty()
                             ? dev->getCurrentBufferSizeSamples()
                             : dev->getAvailableBufferSizes().getFirst();

    // Buffer sizes. Everything the driver offers from the burst up, six of
    // them rather than five - they share the row, so more of them just means
    // narrower chips, and the choice is worth more than the width.
    //
    // Nothing below the burst is listed because nothing below it exists: the
    // list Android hands us starts there, and it is one hardware period.
    {
        auto all = dev->getAvailableBufferSizes();
        juce::Array<int> pick;
        if (all.contains (natBuf)) pick.add (natBuf);
        for (int i = 0; i < all.size() && pick.size() < 6; ++i)
        {
            const int v = all[i];
            if (! pick.contains (v) && v >= natBuf) pick.add (v);
        }
        pick.sort();

        for (int v : pick)
        {
            auto* b = new juce::TextButton (juce::String (v) + (v == natBuf ? "*" : ""));
            styleButton (*b, ZatiColours::key);
            b->setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
            b->setColour (juce::TextButton::textColourOnId, ZatiColours::inkLight);
            b->setToggleState (v == curBuf, juce::dontSendNotification);
            b->onClick = [this, v] { applyAudioSetup (v, 0.0); };
            projSheet.addAndMakeVisible (b);
            bufButtons.add (b);
        }
    }

    //  Only rates worth using, and always the one we are on. Taking the
    //  first four of the driver's list gave 8k / 11k / 12k / 16k — telephone
    //  rates, none of them the 48k the device was actually running, and one
    //  tap away from wrecking the audio quality of the whole instrument.
    juce::Array<double> rates;
    for (double r : dev->getAvailableSampleRates())
        if (r >= 44000.0) rates.add (r);
    if (! rates.contains (curRate) && curRate > 0.0) rates.add (curRate);
    rates.sort();

    for (double r : rates)
    {
        if (rateButtons.size() >= 4) break;
        auto* b = new juce::TextButton (juce::String (r / 1000.0, (r == (double) (int) (r / 1000.0) * 1000.0) ? 0 : 1) + "k");
        styleButton (*b, ZatiColours::key);
        b->setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
        b->setColour (juce::TextButton::textColourOnId, ZatiColours::inkLight);
        b->setToggleState (std::abs (r - curRate) < 1.0, juce::dontSendNotification);
        b->onClick = [this, r] { applyAudioSetup (0, r); };
        projSheet.addAndMakeVisible (b);
        rateButtons.add (b);
    }

    resized();
    projSheet.repaint();
}

// Zero means "leave this one alone", so a chip only ever changes its own
// setting. The device is restarted by setAudioDeviceSetup, which calls
// prepareToPlay again — every buffer the engine owns is resized there, so
// nothing downstream has to know this happened.
void MainComponent::applyAudioSetup (int bufferSize, double rate)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (bufferSize > 0) setup.bufferSize = bufferSize;
    if (rate > 0.0)     setup.sampleRate = rate;

    const auto err = deviceManager.setAudioDeviceSetup (setup, true);

    if (err.isNotEmpty())
    {
        status.setText (T ("AUDIO") + ": " + Lang::ltr (err), juce::dontSendNotification);
        deviceLine.clear();          // a real message: the timer must not touch it
    }
    else
    {
        refreshDeviceStatusLine (true);
    }

    refreshAudioOptions();
}

//  Write the "N muestras · R Hz" line from what the device reports RIGHT NOW,
//  and only over our own previous line. Called from applyAudioSetup and again
//  from the timer, because Oboe's restart is asynchronous and the first read
//  lands before the new rate is in effect.
//  What the phone adds between us writing a block and the speaker moving.
//  Oboe reports the whole path, buffer included, so this is the figure the
//  panel shows and the one record compensation has to give back. Zero when
//  there is no device or the driver will not say.
double MainComponent::outputLatencyMs() const
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return 0.0;

    const double sr = dev->getCurrentSampleRate();
    if (sr <= 0.0) return 0.0;

    return juce::jmax (0.0, (double) dev->getOutputLatencyInSamples() * 1000.0 / sr);
}

void MainComponent::refreshDeviceStatusLine (bool force)
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return;

    //  Compare the two numbers before building anything. This runs on every
    //  UI tick, and it used to allocate two strings a tick - thirty a second,
    //  for their lifetime - only to find they said what the last pair said.
    const int blockNow = dev->getCurrentBufferSizeSamples();
    const int rateNow  = (int) dev->getCurrentSampleRate();

    if (! force && blockNow == lastDeviceBlock && rateNow == lastDeviceRate)
        return;

    lastDeviceBlock = blockNow;
    lastDeviceRate  = rateNow;

    const auto line = Lang::ltr (juce::String (blockNow)) + " " + T ("muestras") + " · "
                        + juce::String (rateNow) + " Hz";

    if (line == deviceLine) return;

    //  Anything else in the status bar is somebody's message. We only correct
    //  a stale line of our own - unless the setup call itself asked for it.
    if (! force && status.getText() != deviceLine) return;

    deviceLine = line;
    status.setText (line, juce::dontSendNotification);
}

void MainComponent::launchSystemPicker()
{
    if (browseTargetPad < 0) return;
    const int index = browseTargetPad;

    chooser = std::make_unique<juce::FileChooser> (
        "Muestra para el pad " + juce::String (index + 1),
        juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this, index] (const juce::FileChooser& fc)
        {
            const auto url = fc.getURLResult();
            if (url.isEmpty()) return;

            closeAllSheets();
            const juce::String fileName = url.getFileName();
            status.setText (T ("Cargando pad %1...", juce::String (index + 1)), juce::dontSendNotification);
            loader.loadAsync (url, index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
            {
                if (ok)
                {
                    assignSampleToPad (index, sb, fileName);
                    status.setText (T ("Pad %1 cargado  [%2]", juce::String (index + 1), detail), juce::dontSendNotification);
                }
                else
                {
                    status.setText (T ("Fallo al cargar: %1", detail), juce::dontSendNotification);
                }
            });
        });
}

void MainComponent::loadBrowserSelection()
{
    // Confirming keeps the audition: drop the undo snapshot.
    auditionedFile = juce::File();
    preAuditionSample = nullptr;

    if (browser == nullptr || browseTargetPad < 0) return;
    const auto f = browser->getSelectedFile (0);
    if (! f.existsAsFile()) return;

    const int index = browseTargetPad;
    const juce::String fileName = f.getFileName();
    closeAllSheets();

    status.setText (T ("Cargando pad %1...", juce::String (index + 1)), juce::dontSendNotification);
    loader.loadAsync (juce::URL (f), index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
    {
        if (ok)
        {
            assignSampleToPad (index, sb, fileName);
            status.setText (T ("Pad %1 cargado  [%2]", juce::String (index + 1), detail), juce::dontSendNotification);
        }
        else
        {
            status.setText (T ("Fallo al cargar: %1", detail), juce::dontSendNotification);
        }
    });
}

// Sheet header: which pad is being filled and what is currently picked.
void MainComponent::paintBrowseSheetContent (juce::Graphics& g)
{
    if (browseSheet.sheetBounds.isEmpty()) return;

    auto inner = browseSheet.sheetBounds.reduced (12, 6);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("CARGAR EN PAD %1", juce::String (juce::jmax (0, browseTargetPad) + 1)),
                inner.removeFromTop (16), juce::Justification::centredLeft);

    const bool picked = browser != nullptr && browser->getNumSelectedFiles() > 0
                     && browser->getSelectedFile (0).existsAsFile();
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    //  Same reason as the pad sheet: this is a file name, and the close button
    //  shares the band.
    auto browseSubRow = inner.removeFromTop (14);
    browseSubRow.setRight (juce::jmin (browseSubRow.getRight(), browseCloseButton.getX() - Metrics::xs));
    g.drawText (picked ? browser->getSelectedFile (0).getFileName()
                       : T ("elige una muestra  -  wav / aiff / flac / ogg / mp3"),
                browseSubRow, juce::Justification::centredLeft, true);
}

// REC on the transport arms PATTERN recording: pads you hit while the
// sequencer runs are written into the playing bank, quantised to the nearest
// step. Sampling from the mic is a per-pad action and lives in the PADS sheet.
void MainComponent::toggleRecordArm()
{
    recArmed = ! recArmed;
    styleButton (recButton, recArmed ? kRec : kKey);
    recButton.setButtonText (recArmed ? T ("REC ON") : T ("REC"));

    if (recArmed && ! engine.isPlaying())
    {
        // Arming with the transport stopped is a dead end — roll it.
        playButton.setToggleState (true, juce::dontSendNotification);
        playButton.setButtonText (T ("STOP"));
        engine.setPlaying (true);
    }

    status.setText (recArmed ? T ("REC: toca pads para grabarlos en el patron")
                             : T ("REC apagado"),
                    juce::dontSendNotification);
    repaint();
}

// ============================================================================
//  Going to the background, and coming back.
//
//  Nothing used to happen here at all, and three things went wrong for it.
//  The Oboe stream stayed open, so a real-time thread and its wakeups kept
//  running behind whatever the phone was doing. If REC was on, the microphone
//  stayed open too - and from Android 12 the system cuts background capture
//  without telling the app, so the recording kept "running" and recorded
//  silence. And nothing was written anywhere, so a process the system decided
//  to reclaim took the session with it.
//
//  A phone call, another app, or the screen going off all pause the activity,
//  which is why stopping here also covers the case that reads worst in a demo:
//  ZATI playing on top of a call.
// ============================================================================
void MainComponent::appSuspended()
{
    //  Stop the recording first, while the input stream is still alive and its
    //  buffer can still be collected. Doing it after shutdownAudio would throw
    //  away whatever had been captured.
    if (recordingActive)
        toggleMicSampling();

    engine.postPanic();          // no voice is left ringing into the silence
    autosave();

    //  Anything the background writer had not got to yet - a pad recorded
    //  seconds ago - gets a bounded moment to land. Bounded because Android
    //  counts a slow onPause as a hang.
    session.flush (1500);

    shutdownAudio();             // releases the output stream and the mic
    audioFocus.abandon();        // ...and hand the speaker back
    pausedByFocus = false;
}

void MainComponent::appResumed()
{
    audioFocus.request();
    pausedByFocus = false;
    setAudioChannels (0, 2);
    useLowestLatency();
    refreshDeviceStatusLine (true);
}

// ============================================================================
//  Audio focus. Android decides which app owns the speaker, and until now we
//  never asked and never listened - so ZATI played over calls, and when an OEM
//  build silenced us for it we could not tell: the meters kept moving with
//  nothing coming out, which reads as the app being broken.
//
//  A loss stops us the same way going to the background does. A transient one
//  remembers that it was US who paused, so the GAIN that follows resumes only
//  what we stopped and never something the user had deliberately left silent.
// ============================================================================
void MainComponent::audioFocusLost (bool permanently)
{
    if (recordingActive)
        toggleMicSampling();

    if (engine.isPlaying())
    {
        engine.setPlaying (false);
        playButton.setToggleState (false, juce::dontSendNotification);
    }

    engine.postPanic();
    shutdownAudio();

    //  Only a transient loss is worth remembering. After a permanent one
    //  Android will not send us a GAIN unless we ask again, which is what
    //  coming back to the foreground does.
    pausedByFocus = ! permanently;

    status.setText (permanently ? T ("Audio cedido a otra app")
                                : T ("En pausa: otra app tiene el audio"),
                    juce::dontSendNotification);
    deviceLine.clear();
}

void MainComponent::audioFocusGained()
{
    if (! pausedByFocus)
        return;

    pausedByFocus = false;
    setAudioChannels (0, 2);
    useLowestLatency();
    refreshDeviceStatusLine (true);
}

//  Two copies, and they answer different questions.
//
//  The session copy is unconditional: it is the only trace of work that was
//  never given a name, which is the state a sampler spends its first hour in.
//  The audio behind it has been written continuously by SessionKeeper's own
//  thread, so all that is left here is the small XML - which matters, because
//  onPause is not a moment Android lets an app take its time in.
//
//  The project copy only exists when a project is open, and it goes over that
//  project's own project.xml, next to the samples its last save wrote.
void MainComponent::autosave()
{
    const auto state = captureState();

    session.sync (uiSample.data(), kNumPads);
    session.writeState (state, currentProject);

    if (currentProject.isEmpty()) return;

    const auto folder = ProjectStore::folderFor (currentProject);
    if (! folder.isDirectory()) return;

    folder.getChildFile ("project.xml").replaceWithText (state.toXmlString());
}

//  Coming back from a cold start. Same shape as loadProject, from the folder
//  nobody had to remember to save into.
//
//  It runs off the first timer tick rather than the constructor: reading
//  sixteen WAVs takes long enough to be seen, and being seen as a face that
//  fills in is much better than being seen as a launch that hangs.
void MainComponent::restoreSession()
{
    if (! SessionKeeper::exists())
    {
        session.adopt (uiSample.data(), kNumPads);
        return;
    }

    auto xml = juce::parseXML (SessionKeeper::stateFile());
    if (xml == nullptr)
    {
        session.adopt (uiSample.data(), kNumPads);
        return;
    }

    const auto tree = juce::ValueTree::fromXml (*xml);

    int restored = 0;
    for (int i = 0; i < kNumPads; ++i)
        if (auto sb = ProjectStore::readSample (SessionKeeper::padFile (i)))
        {
            assignSampleToPad (i, sb);
            ++restored;
        }

    applyState (tree);

    //  Names live in the state, so the tiles are stamped after applyState.
    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i],
                              padStart01[(size_t) i], padEnd01[(size_t) i]);

    currentProject = tree.getProperty ("proyecto", "").toString();
    repaint (headerArea);
    refreshProjectList();

    //  These buffers came off this very folder: nothing to write back.
    session.adopt (uiSample.data(), kNumPads);

    if (restored > 0 || currentProject.isNotEmpty())
        status.setText (currentProject.isNotEmpty()
                            ? T ("Sesion recuperada - %1", currentProject)
                            : (restored == 1 ? T ("Sesion recuperada  [1 pad]")
                                             : T ("Sesion recuperada  [%1 pads]", juce::String (restored))),
                        juce::dontSendNotification);
}

void MainComponent::toggleMicSampling()
{
    if (! recordingActive)
    {
        int slot = (selectedPad >= 0) ? selectedPad : firstEmptyPad();
        if (slot < 0) slot = 0;

        // Ask for the mic explicitly: opening the input without the grant
        // silently yields a dead stream, which reads as "REC does nothing".
        using RP = juce::RuntimePermissions;
        auto begin = [this, slot]
        {
            recordingSlot = slot;
            //  Two in, not one: a phone with a stereo microphone records in
            //  stereo, and one that has a single capsule hands back one
            //  channel and the take stays mono. Asking for two and being
            //  given one is the normal case, not a failure.
            setAudioChannels (2, 2);
            engine.startRecording (slot);
            recordingActive = true;
            styleButton (micButton, kRec);
            micButton.setButtonText (T ("PARAR"));
            status.setText (T ("Grabando pad %1  %2s / %3s", juce::String (slot + 1), "0.0",
                               juce::String ((int) engine.getRecordLimitSeconds())),
                            juce::dontSendNotification);
        };

        if (! RP::isRequired (RP::recordAudio) || RP::isGranted (RP::recordAudio))
        {
            begin();
        }
        else
        {
            RP::request (RP::recordAudio, [this, begin] (bool granted)
            {
                if (granted) begin();
                else status.setText (T ("Sin permiso de microfono: no puedo grabar"),
                                     juce::dontSendNotification);
            });
        }
    }
    else
    {
        recordingActive = false;
        auto sb = engine.finishRecording();

        //  Everything the microphone hears arrives late by the capture path's
        //  own latency, so the take opens with that many samples of whatever
        //  was in the room before the sound - and a pad triggered on it fires
        //  into that gap. Read the figure while the duplex stream is still
        //  open (this is the last moment it exists) and cut the front off.
        //
        //  Only when the driver actually reports one. Oboe leaves it at zero
        //  on devices whose capture stream has no timestamps - this phone is
        //  one - and trimming by a guess would be worse than not trimming.
        if (sb != nullptr)
            if (auto* dev = deviceManager.getCurrentAudioDevice())
            {
                const int lead = dev->getInputLatencyInSamples();
                const int have = sb->buffer.getNumSamples();

                if (lead > 0 && lead < have / 2)
                {
                    juce::AudioBuffer<float> trimmed (sb->buffer.getNumChannels(), have - lead);
                    for (int ch = 0; ch < trimmed.getNumChannels(); ++ch)
                        trimmed.copyFrom (ch, 0, sb->buffer, ch, lead, have - lead);

                    sb->buffer = std::move (trimmed);
                }
            }

        setAudioChannels (0, 2);          // release the mic input, back to output-only
        useLowestLatency();               // ...and take the fast path back with it
        styleButton (micButton, kKey);
        micButton.setButtonText (T ("GRABAR MIC"));
        if (sb != nullptr)
        {
            assignSampleToPad (recordingSlot, sb, "REC " + juce::String (recordingSlot + 1));
            status.setText (T ("Grabado en el pad %1  [%2s]", juce::String (recordingSlot + 1),
                               juce::String (engine.getRecordSeconds(), 1)), juce::dontSendNotification);
        }
        else
        {
            status.setText (T ("No se grabo nada"), juce::dontSendNotification);
        }
    }
}

// ============================================================================
//  The stream is not something we set up once.
//
//  Unplugging headphones does not pause a phone, it REBUILDS the audio path,
//  and the app finds out afterwards or not at all. Two things can go wrong
//  and both of them are silent:
//
//    * the stream comes back at a different rate or block size without
//      passing through prepareToPlay, and every number the engine derives
//      from the rate - playback increment, envelope times, delay length,
//      smoothing coefficients - is now computed against a stream that no
//      longer exists;
//
//    * the tear-down and the build-up overlap, two callback threads meet
//      inside the transport queue, and it wedges. The engine survives that
//      now (the queue has a lifeboat), but a queue that is refusing work is
//      still telling us the device underneath it is not healthy.
//
//  So we watch, every tick, and repair rather than wait to be told. Both
//  repairs are cheap and neither interrupts anything that is sounding.
// ============================================================================
void MainComponent::watchAudioDevice()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr)
        return;

    const double rate  = dev->getCurrentSampleRate();
    const int    block = dev->getCurrentBufferSizeSamples();

    //  Re-sync on drift. jmap-free comparison on purpose: any difference at
    //  all matters, because the engine multiplies by this number.
    if (rate > 0.0 && block > 0
        && (std::abs (rate - enginePreparedRate) > 0.5 || block != enginePreparedBlock))
    {
        int ins = dev->getActiveInputChannels().countNumberOfSetBits();
        engine.prepareToPlay (rate, block, ins);
        enginePreparedRate  = rate;
        enginePreparedBlock = block;
        deviceSampleRate    = rate;
        ++engineResyncs;
    }

    //  A queue that refused a trigger is a queue that met two consumers. The
    //  lifeboat already carried the tap, so the user heard their pad; this
    //  puts the transport itself back on its feet for the next one.
    if (engine.takeDroppedCommands() > 0)
        deviceManager.restartLastAudioDevice();
}

void MainComponent::timerCallback()
{
    //  Once, on the first tick: the face is up by now, so a restore that takes
    //  a second reads as filling in rather than as a hang.
    if (sessionRestorePending)
    {
        sessionRestorePending = false;
        restoreSession();
    }

    //  The safe area, on EVERY tick for the first second and then on the slow
    //  cadence with the rest of the housekeeping.
    //
    //  It used to be asked for only once a second, together with the session
    //  sync - so the face was laid out with an inset of zero, drawn with the
    //  wordmark under the status bar, and then jumped a full second later when
    //  the real numbers arrived. Android does not have the insets ready at the
    //  moment the first frame goes up; the answer is to keep asking until it
    //  does, not to ask slowly.
    if (insetSettleTicks < 30)
    {
        ++insetSettleTicks;
        refreshSystemInsets();
    }

    //  ...and from then on, every couple of seconds, hand the live pads to the
    //  writer. With nothing changed this is sixteen pointer comparisons.
    if (++sessionSyncTick >= 33)
    {
        sessionSyncTick = 0;
        session.sync (uiSample.data(), kNumPads);
        refreshSystemInsets();

        //  ...and the state itself every twenty seconds or so. onPause writes
        //  it too, but a process killed without one - a crash, a battery pull,
        //  a task-switcher swipe on some OEM builds - never gets there, and
        //  audio on disk with no state beside it restores nothing.
        if (++sessionStateTick >= 10)
        {
            sessionStateTick = 0;
            session.writeState (captureState(), currentProject);
        }
    }

    engine.collectRetiredSamples();
    pollExport();
    refreshDeviceStatusLine();      // Oboe settles a beat after we ask it to
    watchAudioDevice();

    //  The master silhouette. The engine has already decimated its ~0.74 s
    //  window into min/max columns, so this copies 256 pairs instead of the
    //  35000 samples the window actually holds.
    {
        float cmn[AudioEngine::kMaxScopeColumns], cmx[AudioEngine::kMaxScopeColumns];
        const int nc = engine.copyScopeColumns (cmn, cmx, AudioEngine::kMaxScopeColumns);
        spectrum.setColumns (cmn, cmx, nc);
    }

    const int scopeN = juce::jmin ((int) (sizeof (scopeTmp) / sizeof (scopeTmp[0])),
                                   DeviceTier::profile().scopePoints);
    engine.copyScope (scopeTmp, scopeN);
    spectrum.setSamples (scopeTmp, scopeN);
    spectrum.setBpm (bpmSlider.getValue());

    const int ps = engine.getPlayStep();

    // Pad trigger feedback (taps + sequencer): flash then decay.
    const std::uint32_t trig = engine.fetchTriggered();
    bool anyFlash = false;
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((trig & (std::uint32_t) (1u << i)) != 0) padFlash[(size_t) i] = 1.0f;
        if (padFlash[(size_t) i] > 0.0f)
        {
            padFlash[(size_t) i] *= 0.8f;
            if (padFlash[(size_t) i] < 0.02f) padFlash[(size_t) i] = 0.0f;
            refreshPad (i);
            anyFlash = true;
        }
    }
    juce::ignoreUnused (anyFlash);

    //  The grid reads the pattern straight from our mirror - but only when
    //  the sheet that shows it is open. It used to run on every tick whether
    //  the sequencer was on screen or not, and it is not cheap: 64 steps x 16
    //  pads copied out of the mirror plus the same number of atomic loads for
    //  the step pitches, thirty times a second, to feed a component nobody
    //  was looking at.
    if (seqSheet.isVisible())
        refreshStepGrid();
    if (songSheet.isVisible() && engine.isPlaying()) refreshSong();

    const int prevPlayStep = lastPlayStep;
    lastPlayStep = ps;

    //  The read head over the wave, while the sheet that shows it is open.
    //  Any pad sharing the source counts: on a chopped break the fragment that
    //  is sounding is rarely the one that is selected.
    if (padSheet.isVisible() && selectedPad >= 0)
    {
        float head = -1.0f;
        if (auto src = uiSample[(size_t) selectedPad])
            for (int i = 0; i < kNumPads; ++i)
                if (uiSample[(size_t) i] == src)
                    head = juce::jmax (head, engine.getPadPosition01 (i));

        waveform.setPlayhead (head);

        const bool sounding = head >= 0.0f;
        if (sounding != previewSounding)
        {
            previewSounding = sounding;
            previewButton.setButtonText (sounding
                                             ? juce::String::fromUTF8 ("\xe2\x96\xa0 ") + T ("STOP")
                                             : juce::String::fromUTF8 ("\xe2\x96\xb6 ") + T ("OIR"));
        }
    }

    //  An armed confirmation that nobody answered goes back to being an
    //  ordinary button, so a red SEGURO? is never left lying on a sheet.
    if (confirmPending != nullptr && --confirmTicks <= 0)
        disarmConfirm();

    // Keep the SEC sheet's readout/rings fresh while the sequencer runs.
    if (engine.isPlaying() && seqSheet.isVisible())
        seqSheet.repaint();

    // Face strips: VU ballistics (fast attack, ~0.8 decay/frame) and the
    // step-LED playhead.
    {
        const float pl = engine.readOutPeakL();
        const float pr = engine.readOutPeakR();
        const float prevL = vuL, prevR = vuR;
        vuL = juce::jmax (pl, vuL * 0.80f); if (vuL < 0.004f) vuL = 0.0f;
        vuR = juce::jmax (pr, vuR * 0.80f); if (vuR < 0.004f) vuR = 0.0f;
        juce::ignoreUnused (prevL, prevR, prevPlayStep);
        spectrum.setVu (vuL, vuR);
        //  The loop-shape strip needs three things, not one: where the
        //  playhead is INSIDE the pattern, how long that pattern is (ZATI's
        //  are variable, 16 to 64 steps), and whether the transport is
        //  actually rolling - because a stopped transport settles the shape
        //  instead of holding it.
        const int playingBank = engine.getPlayingPattern();
        spectrum.setStep (ps,
                          engine.getPatternLength (playingBank),
                          engine.isPlaying(),
                          patternRowColour (juce::jmax (0, playingBank)));
    }

    if (recordingActive)
    {
        //  The take stops itself when the buffer fills; say so rather than
        //  letting the counter freeze and look like a hang.
        if (! engine.isRecording())
            toggleMicSampling();
        else
            status.setText (T ("Grabando pad %1  %2s / %3s", juce::String (recordingSlot + 1),
                               juce::String (engine.getRecordSeconds(), 1),
                               juce::String ((int) engine.getRecordLimitSeconds())),
                            juce::dontSendNotification);
    }
}

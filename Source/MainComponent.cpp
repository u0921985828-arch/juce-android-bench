#include "MainComponent.h"
#include "Kits.h"
#include "UiAudit.h"
#include "Lang.h"
#include "SystemInsets.h"
#include "Denoise.h"
#include "Onsets.h"
#include "DeviceTier.h"

namespace
{
    //  REFERENCES, ALL OF THEM.
    //
    //  kKey and kStepOff were copies, taken once when this translation unit
    //  was initialised - so they froze the palette of whatever skin the app
    //  happened to start in. Switching the chassis then repainted the body and
    //  left every key wearing the old one: on GRAFITO that is a pale cap with
    //  pale text on it, which is a control you can see and cannot read.
    //
    //  A skin token is mutable by definition. Anything that names one has to
    //  keep naming it, not remember what it said.
    const juce::Colour& kPadLoaded = ZatiColours::amber;
    const juce::Colour& kAccent    = ZatiColours::amber;
    const juce::Colour  kRec       = ZatiColours::red;      // semantic, never skinned
    const juce::Colour& kStepOff   = ZatiColours::key;
    const juce::Colour& kKey       = ZatiColours::key;

    //  WHICH TOKEN A CAP WAS PAINTED WITH, remembered on the cap itself.
    //
    //  applySkin used to restyle about eight buttons by name and leave the
    //  other sixty wearing the palette they were built with. On a chassis
    //  change the body, the plates and the LCD all moved and every key stayed
    //  behind - a cream cap on a petrol machine, and on GRAFITO a pale cap
    //  with pale text. A colour taken from a mutable token has to be RETAKEN,
    //  and the only way to retake it is to know which token it was.
    enum Role { roleKey = 0, roleAccent = 1, roleRec = 2, roleFixed = 3 };

    //  LA GANANCIA DE UN PAD, EN DECIBELIOS.
    //
    //  Era un mando lineal de 0 a 1 con paso 0.01, y eso son dos fallos en el
    //  mismo control. Arriba no habia margen: una muestra grabada baja se
    //  quedaba baja, porque 1.0 era el tope y no existia forma de subirla sin
    //  volver a grabarla. Y abajo la escala esta al reves de como se oye: de
    //  0.01 a 0.02 hay 6 dB - un salto enorme - y de 0.99 a 1.00 hay 0.09 dB,
    //  que no se oye. Cien pasos, y la mitad de ellos repartidos en los ultimos
    //  0.8 dB del recorrido.
    //
    //  En decibelios el paso es constante para el oido en todo el recorrido, y
    //  hay 12 dB por encima de la unidad para levantar lo que se grabo bajo.
    //  El valor guardado sigue siendo la amplitud lineal, asi que los proyectos
    //  de antes cargan exactamente igual: lo que cambia es la escala del mando,
    //  no lo que hay debajo.
    constexpr double kGainMinDb = -60.0;   // el ultimo paso de abajo es SILENCIO
    constexpr double kGainMaxDb =  12.0;

    float gainFromDb (double db) noexcept
    {
        return db <= kGainMinDb ? 0.0f : (float) juce::Decibels::decibelsToGain (db);
    }

    double dbFromGain (float g) noexcept
    {
        return juce::jlimit (kGainMinDb, kGainMaxDb,
                             juce::Decibels::gainToDecibels ((double) g, kGainMinDb));
    }

    //  El rotulo del mando: "-inf dB" abajo del todo, y signo siempre, porque
    //  un "3 dB" sin signo no dice si sube o baja.
    juce::String gainText (double db, bool withUnit)
    {
        if (db <= kGainMinDb)
            return juce::String::fromUTF8 ("-\xe2\x88\x9e") + (withUnit ? " dB" : "");

        const juce::String n = withUnit ? juce::String (db, 1) : juce::String ((int) std::round (db));
        return (db > 0.0 ? "+" : "") + n + (withUnit ? " dB" : "");
    }

    juce::Colour roleColour (int role)
    {
        switch (role)
        {
            case roleAccent: return ZatiColours::amber;
            case roleRec:    return ZatiColours::red;
            case roleKey:
            default:         return ZatiColours::key;
        }
    }

    void styleButton (juce::TextButton& b, juce::Colour c)
    {
        b.getProperties().set ("role", c == ZatiColours::amber ? (int) roleAccent
                                     : c == ZatiColours::red   ? (int) roleRec
                                     : c == ZatiColours::key   ? (int) roleKey
                                                               : (int) roleFixed);

        //  Text is chosen by MEASURING it against the cap it lands on, in
        //  both states. See ZatiColours::textOn - the old dark-cap ternary
        //  assumed which of the two inks was the dark one, and that stops
        //  being true the moment the chassis can be dark.
        const auto onCap = b.findColour (juce::TextButton::buttonOnColourId);
        b.setColour (juce::TextButton::buttonColourId, c);
        b.setColour (juce::TextButton::textColourOffId, ZatiColours::textOn (c));
        b.setColour (juce::TextButton::textColourOnId,  ZatiColours::textOn (onCap));
    }

    //  "THIS CAP LIGHTS IN THE ACCENT" IS A ROLE, NOT A COLOUR.
    //
    //  Twenty-three places wrote `setColour (buttonOnColourId, kAccent)` at
    //  construction, and applySkin refreshed three of them. Every other lit cap
    //  in the app - the four bank chips, the bar selector, the settings tabs,
    //  REV / LOOP / AUTOCUT, MODO, the song brushes, the pattern buttons -
    //  kept the accent of whatever skin the app happened to start in. You could
    //  not see it until you pressed one, which is exactly why it survived: the
    //  face repainted correctly and then a single button lit up amber on a
    //  petrol machine.
    //
    //  So the fact is recorded on the button and applySkin re-applies it to
    //  every cap that carries the mark. The lit-state text is MEASURED against
    //  the accent for the same reason the resting text is (see textOn): on a
    //  dark accent, light ink; on a bright one, dark.
    void litAccent (juce::TextButton& b)
    {
        b.getProperties().set ("lit", 1);
        b.setColour (juce::TextButton::buttonOnColourId, ZatiColours::accent);
        b.setColour (juce::TextButton::textColourOnId,   ZatiColours::textOn (ZatiColours::accent));
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

    //  Unidad, no 0.85. Un pad toca la muestra como esta: 0.85 eran -1.4 dB
    //  de rebaja escondida que nadie pidio y que ya no hace falta, porque el
    //  limitador de seguridad del master es quien cuida la suma de 64 pads.
    padGain.fill (1.0f);
    padEnd01.fill (1.0f);
    padAttack.fill (2.0f);
    padRelease.fill (5.0f);
    //  El espejo del filtro, abierto, igual que el motor. Cero aqui serian
    //  sesenta y cuatro mandos de corte en el tope de abajo: la ficha diria
    //  "20 Hz" en un pad que suena entero.
    padCut.fill (AudioEngine::kFiltOpenHz);
    //  AUTOCUT on everywhere, matching the engine. A pad that stacks over
    //  its own tail is the special case, not the normal one.
    padSelfCut.fill (true);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new PadButton (i);
        padZati[(size_t) i] = Zati::forPad (i);      // cut order: zati 1 is always red
        p->setZati (padZati[(size_t) i]);
        p->onClick = [this, i] { padClicked (i); };
        //  ...and holding it edits it, without a sound. See PadButton::onHold.
        p->onHold  = [this, i]
        {
            loadArmed = false;
            loadButton.setToggleState (false, juce::dontSendNotification);
            selectPad (i);
            openSheet (padSheet, padsButton);
            status.setText (T ("PAD %1", juce::String (i + 1)), juce::dontSendNotification);
        };
        addAndMakeVisible (p);
        pads.add (p);
        refreshPad (i);
        refreshPadArt (i);          // also gives the pad its accessible name
    }

    //  The grid speaks in LANES; this file speaks in pads.
    //  A B C D. They ride in the seam that already says PADS, so four more
    //  controls cost the face no height at all.
    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* t = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        styleButton (*t, kStepOff);
        litAccent (*t);
        t->setClickingTogglesState (true);
        t->setRadioGroupId (5150);
        t->onClick = [this, b] { selectBank (b); };
        addAndMakeVisible (t);
        bankButtons.add (t);
    }
    bankButtons[0]->setToggleState (true, juce::dontSendNotification);

    stepGrid.onCell = [this] (int lane, int step) { stepCellToggled (currentBank * kPadsPerBank + lane, step); };
    seqSheet.addAndMakeVisible (stepGrid);

    // Bar selector: 64 steps will not fit across a phone at a size worth
    // tapping, so the grid pages a bar at a time instead of shrinking.
    for (int b = 0; b < kNumSteps / kStepCols; ++b)
    {
        auto* t = new juce::TextButton (juce::String (b + 1));
        styleButton (*t, kStepOff);
        litAccent (*t);
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

    //  Y las cuatro de banco, dentro de la ficha. Ver seqBankButtons.
    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* t = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        styleButton (*t, kStepOff);
        litAccent (*t);
        t->setClickingTogglesState (true);
        t->setRadioGroupId (5151);
        t->onClick = [this, b] { selectBank (b); };
        seqSheet.addAndMakeVisible (t);
        seqBankButtons.add (t);
    }
    seqBankButtons[0]->setToggleState (true, juce::dontSendNotification);

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
            litAccent (*mb[i]);
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
        addAndMakeVisible (setSheet);
        setSheet.setVisible (false);
        setSheet.onDismiss = [this] { closeAllSheets(); };
        setSheet.paintContent = [this] (juce::Graphics& g)
        {
            if      (setPage == pageMidi)     paintMidiPage (g, midiArea);
            else if (setPage == pageAudio)    paintAudioSheetContent (g);
            else if (setPage == pageProjects) paintProjSheetContent  (g);
            else                              paintGesturesPage (g, gesturesArea);
        };

        styleButton (setButton, kKey);
        litAccent (setButton);
        setButton.onClick = [this]
        {
            if (setSheet.isVisible()) closeAllSheets();
            else { showSetPage (setPage); openSheet (setSheet, setButton); }
        };
        addAndMakeVisible (setButton);

        projList.setColour (juce::ListBox::backgroundColourId, ZatiColours::chassisTop);
        projList.setRowHeight (34);
        projModel.onSelected = [this] (int row)
        {
            if (juce::isPositiveAndBelow (row, projModel.names.size()))
                projNameBox.setText (projModel.names[row], juce::dontSendNotification);
        };

        projModel.onChosen = [this] (int row)
        {
            if (juce::isPositiveAndBelow (row, projModel.names.size()))
                loadProject (projModel.names[row]);
        };
        setSheet.addAndMakeVisible (projList);

        styleButton (setCloseButton, kKey);
        setCloseButton.onClick = [this] { closeAllSheets(); };
        setSheet.addAndMakeVisible (setCloseButton);

        styleButton (projSaveButton, kAccent);
        projSaveButton.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        //  The name box. It fills itself from whatever you have selected or
        //  open, so GUARDAR still overwrites the obvious thing by default -
        //  but now you can type over it, which is how you rename, how you
        //  save-as, and how a project ends up called what it is.
        projNameBox.setMultiLine (false);
        projNameBox.setReturnKeyStartsNewLine (false);
        projNameBox.setJustification (juce::Justification::centredLeft);
        projNameBox.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        projNameBox.setColour (juce::TextEditor::backgroundColourId, ZatiColours::screenBg);
        projNameBox.setColour (juce::TextEditor::textColourId,       ZatiColours::lcdFg);
        projNameBox.setColour (juce::TextEditor::outlineColourId,    ZatiColours::ink.withAlpha (0.35f));
        projNameBox.setColour (juce::TextEditor::highlightColourId,  ZatiColours::accent.withAlpha (0.35f));
        projNameBox.setColour (juce::TextEditor::focusedOutlineColourId, ZatiColours::ink);
        projNameBox.onReturnKey = [this] { projSaveButton.triggerClick(); };
        setSheet.addAndMakeVisible (projNameBox);

        projSaveButton.onClick = [this]
        {
            auto name = ProjectStore::sanitise (projNameBox.getText().trim());

            //  Fall back to what is selected or open, and only then to a
            //  generated name - and put it in the box so you can see what it
            //  is about to be called before it is called that.
            if (name.isEmpty())
            {
                const int sel = projList.getSelectedRow();
                name = juce::isPositiveAndBelow (sel, projModel.names.size())
                         ? projModel.names[sel] : currentProject;
            }
            if (name.isEmpty())
                name = "PROYECTO " + juce::String (ProjectStore::list().size() + 1);

            projNameBox.setText (name, juce::dontSendNotification);

            //  Overwriting someone else's project is a two-tap decision, the
            //  same as BORRAR. Saving over the one you already have open is
            //  not - that is just saving.
            if (name != currentProject && ProjectStore::list().contains (name)
                && ! armConfirm (projSaveButton, T ("Sobrescribir \"%1\"?", name)))
                return;

            disarmConfirm();
            saveProject (name);
        };
        setSheet.addAndMakeVisible (projSaveButton);

        styleButton (projLoadButton, kKey);
        projLoadButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, projModel.names.size()))
                loadProject (projModel.names[sel]);
        };
        setSheet.addAndMakeVisible (projLoadButton);

        styleButton (projNewButton, kKey);
        //  NUEVO empties every pad and every pattern. Two taps.
        projNewButton.onClick = [this]
        {
            if (! armConfirm (projNewButton, "BORRA TODO?")) return;
            newProject();
        };
        setSheet.addAndMakeVisible (projNewButton);

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

            if (! armConfirm (projDeleteButton, T ("BORRAR %1?", projModel.names[sel]))) return;
            deleteProject (projModel.names[sel]);
        };
        setSheet.addAndMakeVisible (projDeleteButton);

        styleButton (projExportButton, kKey);
        projExportButton.onClick = [this]
        {
            exportStatus.clear();
            exportOk = false;
            openSheet (exportSheet, setButton);
        };
        setSheet.addAndMakeVisible (projExportButton);

        // --- RACK: one pad's sends, opened from the mixer. ---------------
        styleButton (rackButton, kKey);
        rackButton.onClick = [this] { rackPad = juce::jmax (0, selectedPad); openSheet (rackSheet, mixButton); refreshRack(); };
        mixSheet.addAndMakeVisible (rackButton);

        for (int i = 0; i < kNumPads; ++i)
        {
            auto* b = new juce::TextButton (juce::String (i + 1).paddedLeft ('0', 2));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->onClick = [this, i] { rackPad = i; selectPad (i); refreshRack(); };
            rackSheet.addAndMakeVisible (b);
            rackPadBtns.add (b);
        }

        for (int f = 0; f < kNumFx; ++f)
        {
            auto* sl = new juce::Slider();
            sl->setSliderStyle (juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, Metrics::readout);
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
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7301);
            b->onClick = [this, n] { chopSlices = n; refreshChopSheet(); };
            chopSheet.addAndMakeVisible (b);
            chopCountBtns.add (b);
        }

        //  IGUALES / GOLPES, una pareja excluyente como las de idioma y
        //  carcasa: lo que cambia no es un parametro del corte sino QUE decide
        //  donde se corta.
        for (auto* b : { &chopEvenBtn, &chopHitsBtn })
        {
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7302);
            chopSheet.addAndMakeVisible (b);
        }
        chopEvenBtn.onClick = [this] { chopByHits = false; refreshChopSheet(); };
        chopHitsBtn.onClick = [this] { chopByHits = true;  refreshChopHits(); refreshChopSheet(); };

        styleButton (chopSafeButton, kKey);
        chopSafeButton.setClickingTogglesState (true);
        chopSafeButton.setToggleState (true, juce::dontSendNotification);
        litAccent (chopSafeButton);
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
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7411);
            b->onClick = [this, i]
            {
                Lang::set ((Lang::Id) i);
                Lang::savePreference();
                retranslateUi();
                //  Arabic moves the sheets' furniture, not just their words,
                //  so the language is a layout change now and has to run one.
                resized();
                repaint();
            };
            setSheet.addAndMakeVisible (b);
            langButtons.add (b);
        }

        //  ...and the chassis. Same shape of control as the language row:
        //  three chips, one lit, and picking one repaints the whole machine.
        for (int i = 0; i < 4; ++i)
        {
            auto* b = new juce::TextButton (ZatiColours::skinName (i));
            styleButton (*b, kKey);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7412);
            b->onClick = [this, i]
            {
                ZatiColours::setSkin (i);
                ZatiColours::saveSkinPreference();
                applySkin();
                //  Every cached colour in the tree is re-read on the next
                //  paint, but the ones components captured at construction are
                //  not - applySkin is what puts those back. The layout does
                //  not move, so a repaint is enough after it.
                lnf.applyBrowserColours();
                repaint();
                for (auto* sh : { &padSheet, &seqSheet, &browseSheet, &setSheet, &mixSheet,
                                  &songSheet, &exportSheet, &rackSheet, &chopSheet })
                    sh->repaint();
            };
            setSheet.addAndMakeVisible (b);
            skinButtons.add (b);
        }
        skinButtons[juce::jlimit (0, 3, ZatiColours::currentSkin)]
            ->setToggleState (true, juce::dontSendNotification);

        styleButton (quantButton, kKey);
        litAccent (quantButton);
        quantButton.setClickingTogglesState (true);
        quantButton.onClick = [this]
        {
            const bool on = quantButton.getToggleState();
            engine.setLiveQuantise (on);
            status.setText (on ? T ("Los pads suenan cuadrados al paso")
                               : T ("Los pads suenan cuando los tocas"),
                            juce::dontSendNotification);
        };
        setSheet.addAndMakeVisible (quantButton);

        styleButton (measureButton, kKey);
        measureButton.onClick = [this] { startMeasure(); };
        setSheet.addAndMakeVisible (measureButton);

        //  The two pages of this card. A tab row, not a door: the card stays
        //  where it is and its contents change, which is the difference
        //  between "settings has two pages" and "settings sends you somewhere
        //  else".
        juce::TextButton* pb[4] = { &pageAudioBtn, &pageMidiBtn, &pageProjBtn, &pageGestBtn };
        for (int i = 0; i < 4; ++i)
        {
            styleButton (*pb[i], kKey);
            pb[i]->setClickingTogglesState (true);
            pb[i]->setRadioGroupId (8802);
            litAccent (*pb[i]);
            pb[i]->onClick = [this, i] { showSetPage (i); };
            setSheet.addAndMakeVisible (pb[i]);
        }
        pageAudioBtn.setToggleState (true, juce::dontSendNotification);

        //  LA PAGINA DE MIDI, y va en su propia pagina y no en la de AUDIO por
        //  la misma razon por la que la ficha del pad acabo en tres: la de
        //  AUDIO ya pide 158 px de lectura mas cuatro filas de tapas, y en
        //  280x653 la ficha no puede pasar de 509. Cuatro controles mas ahi
        //  serian cuatro controles de altura cero, que es lo que mide el banco.
        for (auto* b : { &midiOutBtn, &midiInBtn })
        {
            styleButton (*b, kKey);
            b->setClickingTogglesState (true);
            litAccent (*b);
            setSheet.addAndMakeVisible (b);
        }
        for (auto* c : { &midiOutBox, &midiInBox })
        {
            c->setTextWhenNoChoicesAvailable (T ("nada enchufado"));
            c->setTextWhenNothingSelected (T ("nada enchufado"));
            setSheet.addAndMakeVisible (c);
        }

        midiOutBtn.onClick = [this] { applyMidiChoice(); };
        midiInBtn.onClick  = [this] { applyMidiChoice(); };
        midiOutBox.onChange = [this] { applyMidiChoice(); };
        midiInBox.onChange  = [this] { applyMidiChoice(); };

        //  Lo que llega de fuera. Se traduce a un pad y se empuja a la cola de
        //  MIDI - NO a la de comandos, que es de un solo productor. Esto lo
        //  llama un hilo de JUCE, asi que aqui dentro no puede haber nada que
        //  toque la interfaz.
        midi.onNoteOn  = [this] (int pad, float vel) { engine.postNoteOnFromMidi (pad, vel); };
        midi.onNoteOff = [this] (int pad)            { engine.postNoteOffFromMidi (pad); };
        midi.setSource (engine.midiOutQueue());
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

        styleButton (browseKitButton, kKey);
        //  KIT se lleva por delante los dieciseis pads del banco, al lado de
        //  CARGAR que solo toca uno. Dos toques, como BORRA TODO y como
        //  sobrescribir un proyecto: el segundo dice cuantos y en cual.
        browseKitButton.onClick = [this]
        {
            if (! armConfirm (browseKitButton, T ("SOBRESCRIBIR %1?",
                                                  juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))))
                return;
            loadFolderAsKit();
        };
        browseSheet.addAndMakeVisible (browseKitButton);

        //  FABRICA: los dieciseis de este banco, otra vez. Mismo peso que
        //  CARGAR KIT - se lleva el banco entero por delante - asi que la
        //  misma confirmacion de dos toques.
        styleButton (browseFactoryButton, kKey);
        browseFactoryButton.onClick = [this]
        {
            if (! armConfirm (browseFactoryButton, T ("SOBRESCRIBIR %1?",
                                                      juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))))
                return;
            disarmConfirm();
            loadFactoryKits (currentBank);
            status.setText (T ("Banco %1: %2", juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)),
                               T (Kits::bankName (currentBank))),
                            juce::dontSendNotification);
        };
        browseSheet.addAndMakeVisible (browseFactoryButton);

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
    litAccent (loadButton);
    //  Hold CARGAR to open the library on the pad you have selected, instead of
    //  arming it and then hunting for a pad to tap. Same destination, one
    //  gesture instead of two, and it does not leave the face armed if you
    //  change your mind.
    loadButton.onHold = [this]
    {
        loadArmed = false;
        loadButton.setToggleState (false, juce::dontSendNotification);
        openBrowseForPad (juce::jmax (0, selectedPad));
    };

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
    setSheet.addAndMakeVisible (testButton);

    styleButton (recButton, kKey);
    recButton.onClick = [this] { toggleRecordArm(); };
    addAndMakeVisible (recButton);

    styleButton (micButton, kKey);
    micButton.onClick = [this] { toggleMicSampling(); };
    padSheet.addAndMakeVisible (micButton);

    styleButton (resampleButton, kKey);
    resampleButton.onClick = [this] { toggleResample(); };
    padSheet.addAndMakeVisible (resampleButton);

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
    playButton.setColour (juce::TextButton::buttonOnColourId, ZatiColours::green);
    playButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::white);
    playButton.onClick = [this]
    {
        const bool on = playButton.getToggleState();
        engine.setPlaying (on);
        playButton.setButtonText (on ? T ("STOP") : T ("PLAY"));
    };

    //  Hold PLAY for silence NOW: the transport stops and every voice still
    //  ringing is cut with it. STOP on its own leaves long tails and held
    //  loops sounding, which is right for a musical stop and wrong for the
    //  moment you need the room quiet.
    playButton.onHold = [this]
    {
        engine.setPlaying (false);
        engine.postPanic();
        playButton.setToggleState (false, juce::dontSendNotification);
        playButton.setButtonText (T ("PLAY"));
        status.setText (T ("Todo parado"), juce::dontSendNotification);
    };
    addAndMakeVisible (playButton);

    styleButton (tapButton, kKey);
    tapButton.onClick = [this] { tapTempo(); };
    seqSheet.addAndMakeVisible (tapButton);

    styleButton (copyPatBtn, kKey);
    copyPatBtn.onClick = [this] { copyPattern(); };
    seqSheet.addAndMakeVisible (copyPatBtn);

    styleButton (pastePatBtn, kKey);
    pastePatBtn.setEnabled (false);
    pastePatBtn.onClick = [this] { pastePattern(); };
    seqSheet.addAndMakeVisible (pastePatBtn);

    styleButton (clearButton, kKey);
    clearButton.onClick = [this]
    {
        //  Emptying a whole pattern used to be one tap with nothing behind it.
        //  It is the same size of loss as a chop, so it gets the same net.
        pushUndo (T ("VACIAR"));
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
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, Metrics::readout);
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
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, Metrics::readout);
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
    //  GANANCIA en dB (ver kGainMinDb). El paso de 0.1 dB sobre 72 dB son 720
    //  posiciones en los 320 px de arrastre que pide initKnob: dos posiciones
    //  por pixel, que es exactamente lo que se puede apuntar con un dedo.
    initKnob (volSlider, kGainMinDb, kGainMaxDb, 0.1, 0.0, 0.0,
             [this]
             {
                 if (selectedPad < 0) return;
                 const float g = gainFromDb (volSlider.getValue());
                 padGain[(size_t) selectedPad] = g;
                 engine.setPadGain (selectedPad, g);
                 //  El mismo nivel esta en dos sitios: aqui y en la tira del
                 //  MEZCLADOR. Si no se copia, abrir el mezclador despues de
                 //  tocar este mando ensena el valor viejo y el primer roce
                 //  del fader lo devuelve a donde estaba.
                 if (auto* f = mixFaders[selectedPad])
                     f->setValue (volSlider.getValue(), juce::dontSendNotification);
             });
    volSlider.textFromValueFunction = [] (double v) { return gainText (v, true); };
    volSlider.updateText();
    initKnob (panSlider, -1.0, 1.0, 0.01, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padPan[(size_t) selectedPad] = (float) panSlider.getValue(); engine.setPadPan (selectedPad, (float) panSlider.getValue());
                                              if (auto* mp = mixPans[selectedPad]) mp->setValue (panSlider.getValue(), juce::dontSendNotification); } });
    initKnob (attackSlider, 0.0, 200.0, 1.0, 2.0, 20.0,
             [this] { if (selectedPad >= 0) { padAttack[(size_t) selectedPad] = (float) attackSlider.getValue(); engine.setPadAttack (selectedPad, (float) attackSlider.getValue()); } });
    initKnob (releaseSlider, 1.0, 800.0, 1.0, 5.0, 40.0,
             [this] { if (selectedPad >= 0) { padRelease[(size_t) selectedPad] = (float) releaseSlider.getValue(); engine.setPadRelease (selectedPad, (float) releaseSlider.getValue()); } });

    //  CORTE, con el punto medio del mando en 1 kHz.
    //
    //  Lineal, este mando es inutil: la mitad del recorrido iria de 10 a 20
    //  kHz, donde no se oye nada moverse, y los dos primeros milimetros se
    //  comerian de 20 Hz a 2 kHz, que es donde esta toda la musica. El oido
    //  cuenta octavas, no hercios - de 100 a 200 se oye igual de lejos que de
    //  1000 a 2000 - asi que el mando reparte por octavas, que es lo que hace
    //  setSkewFactorFromMidPoint con 1000 en un recorrido de 20 a 20000.
    initKnob (cutSlider, 20.0, (double) AudioEngine::kFiltOpenHz, 1.0,
              (double) AudioEngine::kFiltOpenHz, 1000.0,
             [this] { if (selectedPad >= 0) { padCut[(size_t) selectedPad] = (float) cutSlider.getValue(); engine.setPadCutoff (selectedPad, (float) cutSlider.getValue()); } });
    //  Y arriba del todo no dice "20000 Hz" sino que esta ABIERTO, que es la
    //  unica posicion del mando que significa algo distinto de un numero: es
    //  el pad sin filtrar, y sin ella haria falta un interruptor.
    cutSlider.textFromValueFunction = [] (double v)
    {
        if (v >= (double) AudioEngine::kFiltOpenHz - 1.0) return T ("ABIERTO");
        return v >= 1000.0 ? Lang::ltr (juce::String (v / 1000.0, 1) + " k")
                           : Lang::ltr (juce::String ((int) v) + " Hz");
    };
    cutSlider.updateText();
    initKnob (resoSlider, 0.0, 1.0, 0.01, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padReso[(size_t) selectedPad] = (float) resoSlider.getValue(); engine.setPadReso (selectedPad, (float) resoSlider.getValue()); } });
    resoSlider.textFromValueFunction = [] (double v) { return Lang::ltr (juce::String ((int) (v * 100.0 + 0.5)) + " %"); };
    resoSlider.updateText();
    //  A choke group is off or 1..8 — nine discrete positions. A rotary asks
    //  you to aim for 4 and land on 3; increment buttons hit it first try and
    //  show the state without reading a number off a dial.
    initKnob (chokeSlider, 0.0, 8.0, 1.0, 0.0, 0.0,
             [this] { if (selectedPad >= 0) { padChokeUI[(size_t) selectedPad] = (int) chokeSlider.getValue(); engine.setPadChoke (selectedPad, (int) chokeSlider.getValue()); } });
    chokeSlider.setSliderStyle (juce::Slider::IncDecButtons);
    chokeSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, Metrics::readout);

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
    litAccent (modeButton);
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
        const int len = padSourceLength (selectedPad);
        if (len > 0) engine.setPadStart (selectedPad, (int) (v * len));
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
        const int len = padSourceLength (selectedPad);
        if (len > 0) engine.setPadEnd (selectedPad, (int) (v * len));
        waveform.setTrim (padStart01[(size_t) selectedPad], (float) v);
        refreshPadArt (selectedPad);
        refreshWaveformSegments();
        repaint (editInfoArea.expanded (4));
    };

    reverseButton.setClickingTogglesState (true);
    styleButton (reverseButton, kStepOff);
    litAccent (reverseButton);
    reverseButton.onClick = [this] { if (selectedPad >= 0) { padReverse[(size_t) selectedPad] = reverseButton.getToggleState(); engine.setPadReverse (selectedPad, reverseButton.getToggleState()); } };
    addAndMakeVisible (reverseButton);

    loopButton.setClickingTogglesState (true);
    styleButton (loopButton, kStepOff);
    litAccent (loopButton);
    loopButton.onClick = [this] { if (selectedPad >= 0) { padLoop[(size_t) selectedPad] = loopButton.getToggleState(); engine.setPadLoop (selectedPad, loopButton.getToggleState()); } };
    addAndMakeVisible (loopButton);

    //  AUTOCUT — the pad cuts itself. Off by default, because layering a pad
    //  over its own tail is what this sampler has always done and some pads
    //  want it; on, a second tap kills the first with a 1.5 ms declick, which
    //  is how a hardware one-shot behaves and what keeps a stab from turning
    //  into a chorus of itself when you play it fast.
    autocutButton.setClickingTogglesState (true);
    styleButton (autocutButton, kStepOff);
    litAccent (autocutButton);

    styleButton (duckButton, kKey);
    litAccent (duckButton);
    duckButton.setClickingTogglesState (true);
    duckButton.onClick = [this]
    {
        //  Uno solo manda. Dos pads bombeando a la vez es una envolvente
        //  peleandose consigo misma, y ademas nadie sabria cual esta puesto.
        const bool on = duckButton.getToggleState();
        engine.setDuckPad (on ? selectedPad : -1);
        status.setText (on ? T ("El pad %1 hace bombear al resto", juce::String (selectedPad + 1))
                           : T ("Bombeo apagado"),
                        juce::dontSendNotification);
    };
    padSheet.addAndMakeVisible (duckButton);
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
    patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, Metrics::readout);
    patternSlider.textFromValueFunction = [] (double v) { return "P" + juce::String ((int) v + 1); };
    patternSlider.updateText();   // refresh textbox with the new formatter
    patternSlider.onValueChange = [this]
    {
        selectedPattern = (int) patternSlider.getValue();
        engine.setEditPattern (selectedPattern);
        selectedStep = -1;
        noteSlider.setValue (0.0, juce::dontSendNotification);
        velSlider.setValue  (127.0, juce::dontSendNotification);
        rollSlider.setValue (1.0, juce::dontSendNotification);
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
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 72, Metrics::readout);
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
    noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, Metrics::readout);
    noteSlider.textFromValueFunction = [] (double v) { return (v > 0 ? juce::String ("+") : juce::String()) + juce::String ((int) v) + " st"; };
    noteSlider.updateText();   // refresh textbox with the new formatter
    noteSlider.onValueChange = [this]
    {
        if (selectedPad >= 0 && selectedStep >= 0)
            engine.setStepNote (selectedPattern, selectedStep, selectedPad, (int) noteSlider.getValue());

        refreshStepGrid();
    };
    seqSheet.addAndMakeVisible (noteSlider);

    //  How HARD this step hits. The pattern was a typewriter without it: every
    //  strike identical, which is the one thing a drummer never does.
    velSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    velSlider.setRange (1.0, 127.0, 1.0);
    velSlider.setValue (127.0, juce::dontSendNotification);
    velSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    velSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    velSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    velSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, Metrics::readout);
    velSlider.textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0 / 127.0)) + " %"; };
    velSlider.updateText();
    velSlider.onValueChange = [this]
    {
        if (selectedPad >= 0 && selectedStep >= 0)
            engine.setStepVel (selectedPattern, selectedStep, selectedPad, (int) velSlider.getValue());
        refreshStepGrid();
    };
    seqSheet.addAndMakeVisible (velSlider);

    //  How MANY times. A roll is not a finer grid - the pattern keeps its
    //  sixteen steps - it is one step that speaks up to eight times inside
    //  its own slot, which is how a fill gets made without changing the bar.
    rollSlider.setSliderStyle (juce::Slider::IncDecButtons);
    rollSlider.setRange (1.0, 8.0, 1.0);
    rollSlider.setValue (1.0, juce::dontSendNotification);
    rollSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    rollSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    rollSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    rollSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, Metrics::readout);
    rollSlider.textFromValueFunction = [] (double v)
    { return (v <= 1.0) ? juce::String ("1") : ("x" + juce::String ((int) v)); };
    rollSlider.updateText();
    rollSlider.onValueChange = [this]
    {
        if (selectedPad >= 0 && selectedStep >= 0)
            engine.setStepRoll (selectedPattern, selectedStep, selectedPad, (int) rollSlider.getValue());
        refreshStepGrid();
    };
    seqSheet.addAndMakeVisible (rollSlider);

    //  SWING is the whole pattern's, not one step's: it is a feel, and a feel
    //  you can set per step is just a step in the wrong place.
    swingSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    swingSlider.setRange (50.0, 75.0, 1.0);
    swingSlider.setValue (50.0, juce::dontSendNotification);
    swingSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    swingSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    swingSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    swingSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, Metrics::readout);
    //  T(), not the bare word. The row for it has been in Lang.cpp since the
    //  four languages went in - straight / 平直 / مستقيم - and the slider was
    //  printing the Spanish literal over it in all four. A readout is text like
    //  any other; the only reason it slipped is that it is written inside a
    //  lambda instead of next to a setButtonText.
    swingSlider.textFromValueFunction = [] (double v)
    { return (v <= 50.5) ? T ("recto") : (juce::String ((int) v) + " %"); };
    swingSlider.updateText();
    swingSlider.onValueChange = [this] { engine.setSwing ((float) (swingSlider.getValue() / 100.0)); };
    seqSheet.addAndMakeVisible (swingSlider);

    //  LA REJILLA. Un paso duraba una semicorchea y no habia otra: ni un
    //  tresillo, ni una fusa, ni un patron de corcheas que ocupase dos
    //  compases. Es del transporte entero, como el tempo y el swing - ver
    //  AudioEngine::setStepBeats para por que no es de cada patron.
    gridSlider.setSliderStyle (juce::Slider::IncDecButtons);
    gridSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
    gridSlider.setRange (0.0, kNumGrids - 1, 1.0);
    gridSlider.setValue (2.0, juce::dontSendNotification);      // 1/16
    gridSlider.setDoubleClickReturnValue (true, 2.0);
    gridSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    gridSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    gridSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    gridSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 54, Metrics::readout);
    //  Dentro de Lang::ltr: son cifras latinas y en arabe la linea va al
    //  reves, asi que "1/16" sin envolver sale como "16/1".
    gridSlider.textFromValueFunction = [] (double v)
    { return Lang::ltr (gridName (juce::jlimit (0, kNumGrids - 1, (int) v))); };
    gridSlider.updateText();
    gridSlider.onValueChange = [this]
    {
        const int i = juce::jlimit (0, kNumGrids - 1, (int) gridSlider.getValue());
        engine.setStepBeats (kGridBeats[i]);
        status.setText (T ("Un paso dura %1", Lang::ltr (gridName (i))), juce::dontSendNotification);
        stepGrid.repaint();
    };
    seqSheet.addAndMakeVisible (gridSlider);

    //  The two tabs of the sequencer card, same furniture as the settings card
    //  so the gesture is already learnt: the card stays put and its contents
    //  change. Directly under the title on both pages, so the tab you are
    //  about to press does not move when you press the other one.
    {
        juce::TextButton* sb[2] = { &seqGridBtn, &seqStepBtn };
        for (int i = 0; i < 2; ++i)
        {
            styleButton (*sb[i], kKey);
            sb[i]->setClickingTogglesState (true);
            sb[i]->setRadioGroupId (8803);
            litAccent (*sb[i]);
            sb[i]->onClick = [this, i] { showSeqPage (i); };
            seqSheet.addAndMakeVisible (sb[i]);
        }
        seqGridBtn.setToggleState (true, juce::dontSendNotification);
    }

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
            litAccent (*b);
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
        const int len = padSourceLength (selectedPad);
        if (len > 0)
        {
            engine.setPadStart (selectedPad, (int) (s * len));
            engine.setPadEnd   (selectedPad, (int) (e * len));
        }
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
        //  En decibelios, igual que el mando GANANCIA de la ficha del pad: es
        //  el MISMO numero visto en dos sitios, y tenerlo en dos escalas
        //  distintas era pedir que uno de los dos mintiera. El fader llega
        //  tambien a +12 dB, que es lo que hace falta para levantar una toma
        //  floja sin tocar la muestra.
        f->setRange (kGainMinDb, kGainMaxDb, 0.1);
        f->setValue (dbFromGain (padGain[(size_t) i]), juce::dontSendNotification);
        //  La curva de un fader de mezcla: la unidad cae a tres cuartos del
        //  recorrido y los primeros dos tercios reparten los 20 dB de arriba,
        //  que es donde se mezcla. Lineal en dB deja la zona util apretada
        //  contra el tope.
        f->setSkewFactorFromMidPoint (-9.0);
        f->setDoubleClickReturnValue (true, 0.0);
        f->setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        f->setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        f->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        f->setColour (juce::Slider::trackColourId, Zati::colour (i));
        f->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, Metrics::readout);
        //  A tap must not become a value. Snapping to the touch point turns a
        //  brushed finger into a channel slammed to zero; relative dragging
        //  means you take hold of the level and move it from where it was.
        f->setSliderSnapsToMousePosition (false);
        //  Sin unidad: la casilla mide 46 px y "-60.0 dB" no cabe. El signo si
        //  va, que es lo que distingue subir de bajar.
        f->textFromValueFunction = [] (double v) { return gainText (v, false); };
        f->onValueChange = [this, i, f]
        {
            const float g = gainFromDb (f->getValue());
            padGain[(size_t) i] = g;
            engine.setPadGain (i, g);
            if (i == selectedPad) volSlider.setValue (f->getValue(), juce::dontSendNotification);
        };
        mixRows.addAndMakeVisible (f);
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
        mixRows.addAndMakeVisible (p);
        mixPans.add (p);

        auto* m = new juce::TextButton ("M");
        styleButton (*m, kStepOff);
        m->setColour (juce::TextButton::buttonOnColourId, ZatiColours::red);
        m->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        m->setClickingTogglesState (true);
        m->onClick = [this, i, m] { engine.setPadMute (i, m->getToggleState()); refreshMixStrip(); };
        mixRows.addAndMakeVisible (m);
        mixMutes.add (m);

        auto* so = new juce::TextButton ("S");
        styleButton (*so, kStepOff);
        so->setColour (juce::TextButton::buttonOnColourId, ZatiColours::yellow);
        so->setClickingTogglesState (true);
        so->onClick = [this, i, so] { engine.setPadSolo (i, so->getToggleState()); refreshMixStrip(); };
        mixRows.addAndMakeVisible (so);
        mixSolos.add (so);
    }
    //  The strips live in a scrolled panel and paint their own chips and
    //  names: those used to be drawn on the sheet behind the sliders, using
    //  the sliders' bounds, which stops working the moment the sliders move
    //  under a viewport.
    mixRows.paintRows = [this] (juce::Graphics& g) { paintMixRows (g); };
    //  EL MANUAL. Ficha propia con su desplazamiento, porque son ocho
    //  capitulos y en 360x640 no cabe ni la mitad. Se abre desde la pagina de
    //  GESTOS de AJUSTES, que es donde ya se va a buscar "como se hacia esto".
    busyBar.paintBar = [this] (juce::Graphics& g) { paintBusy (g); };
    addAndMakeVisible (busyBar);
    busyBar.setVisible (false);

    //  Y ya empieza puesta. Abrir la app no es instantaneo -abrir el
    //  dispositivo de audio y montar mil componentes son 700 ms medidos en el
    //  Redmi- y aunque esos 700 ms pasan ANTES del primer fotograma y no hay
    //  forma de pintar nada durante ellos, lo que se ve despues si importa: el
    //  primer fotograma sale con la barra puesta y no se apaga hasta que la
    //  sesion esta dentro, asi que el hueco entre "ya veo la app" y "ya
    //  responde la app" tiene algo que lo explique en vez de parecer colgada.
    //
    //  Menos cuando mide el banco: alli la app se maqueta y se va sin que el
    //  temporizador llegue a latir, asi que la barra se quedaria puesta para
    //  siempre - solapando la fila de abajo en las 448 corridas y colandose en
    //  las 34 fotos de la ficha de Play.
    if (! UiAudit::enabled())
        beginBusy (T ("Iniciando"));
    else
        startupBusy = false;

    manualBody.paintBody = [this] (juce::Graphics& g) { paintManualBody (g); };
    manualScroll.setViewedComponent (&manualBody, false);
    manualScroll.setScrollBarsShown (true, false);
    manualScroll.setScrollBarThickness (8);
    manualSheet.addAndMakeVisible (manualScroll);
    manualSheet.setVisible (false);
    manualSheet.onDismiss = [this] { closeAllSheets(); };
    manualSheet.paintContent = [this] (juce::Graphics& g) { paintManualSheetContent (g); };
    styleButton (manualCloseButton, kKey);
    manualCloseButton.onClick = [this] { closeAllSheets(); };
    manualSheet.addAndMakeVisible (manualCloseButton);
    addAndMakeVisible (manualSheet);
    //  DESPUES de anadirla, no antes: addAndMakeVisible hace justo lo que dice
    //  su nombre y vuelve a encenderla. Puesto al reves, la ficha del manual
    //  se quedaba VISIBLE desde el arranque, detras de la cara - y como los
    //  botones de la cara se anaden despues, se dibujaban encima de ella. Lo
    //  que parecia "los botones se cuelan por encima del manual" era el manual
    //  colandose por debajo de la maquina, desde el primer segundo.
    manualSheet.setVisible (false);

    styleButton (manualButton, kKey);
    manualButton.onClick = [this]
    {
        closeAllSheets();
        manualScroll.setViewPosition (0, 0);
        openSheet (manualSheet, setButton);
    };
    setSheet.addAndMakeVisible (manualButton);

    mixScroll.setViewedComponent (&mixRows, false);
    mixScroll.setScrollBarsShown (true, false);
    mixScroll.setScrollBarThickness (8);
    mixSheet.addAndMakeVisible (mixScroll);

    //  THE MIXER PAGES BY BANK, and it has to now. It listed one strip per pad
    //  and the machine went from sixteen pads to sixty-four: sixty-four strips
    //  in a card capped at 78% of a 640 px phone is eight visible and fifty-six
    //  behind a scrollbar, with nothing on screen saying which sixteen you are
    //  looking at. Same four chips as the face, same letters, same order - so
    //  "bank C" means one thing everywhere in the app.
    for (int b = 0; b < kNumBanks; ++b)
    {
        auto* t = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
        styleButton (*t, kKey);
        litAccent (*t);
        t->setClickingTogglesState (true);
        t->setRadioGroupId (5151);
        t->onClick = [this, b] { showMixBank (b); };
        mixSheet.addAndMakeVisible (t);
        mixBankBtns.add (t);
    }
    mixBankBtns[0]->setToggleState (true, juce::dontSendNotification);

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
    litAccent (mixButton);
    mixButton.onClick = [this]
    {
        if (mixSheet.isVisible()) { closeAllSheets(); return; }
        for (int i = 0; i < kNumPads; ++i)
        {
            if (mixFaders[i] != nullptr) mixFaders[i]->setValue (dbFromGain (padGain[(size_t) i]), juce::dontSendNotification);
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
    litAccent (songPadModeBtn);
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
    litAccent (songModeBtn);
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
    //  Eighty-eight, not sixty-four. The readout used to say "8 comp" - an
    //  abbreviation nobody had to translate - and the moment it said what it
    //  means in each language ("8 compases" is 72 px, and Arabic is wider) the
    //  box it had was twelve pixels short on every screen in the bench. A word
    //  costs width; the box is the thing that has to know it.
    songLenSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 88, Metrics::readout);
    //  Same gap, same cause: "8 comp" is an abbreviation of a Spanish word, and
    //  the "%1 compases" row it belongs to was already sitting in Lang.cpp
    //  translated into the other three.
    songLenSlider.textFromValueFunction = [] (double v)
    { return T ("%1 compases", juce::String ((int) v)); };
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
        litAccent (*b);
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
    litAccent (songButton);
    songButton.onClick = [this]
    {
        if (songSheet.isVisible()) { closeAllSheets(); return; }
        openSheet (songSheet, songButton);
        refreshSong();
    };
    addAndMakeVisible (songButton);

    // --- XY: la superficie de directo ---------------------------------------
    {
        styleButton (xyButton, kKey);
        litAccent (xyButton);
        xyButton.onClick = [this]
        {
            toggleXyPanel();
        };
        addAndMakeVisible (xyButton);

        //  Los seis efectos otra vez, dentro de la ficha. Repetirlos aqui en
        //  vez de mandar al usuario a cerrar el panel, tocar el efecto en la
        //  cara y volver a abrirlo es la diferencia entre una superficie de
        //  directo y un cuadro de dialogo.
        for (int f = 0; f < kNumFx; ++f)
        {
            auto* b = new juce::TextButton (fxDefs[f].name);
            styleButton (*b, kKey);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7710);
            b->onClick = [this, f] { selectXyFx (f); };
            xyPanel.addAndMakeVisible (b);
            xyFxButtons.add (b);
        }
        xyFxButtons[0]->setToggleState (true, juce::dontSendNotification);

        styleButton (xyLatchButton, kKey);
        litAccent (xyLatchButton);
        xyLatchButton.setClickingTogglesState (true);
        xyLatchButton.onClick = [this]
        {
            xyLatch = xyLatchButton.getToggleState();
            //  Al pasar a momentaneo con el dedo levantado, el efecto no puede
            //  quedarse colgado sonando: el modo cambia lo que significa
            //  SOLTAR, y ahora mismo esta soltado.
            if (! xyLatch && ! xyPad.isTouched() && fxOn[(size_t) xyFx])
                setFxEnabled (xyFx, false);
            status.setText (xyLatch ? T ("XY fijo - se queda donde lo dejes")
                                    : T ("XY momentaneo - suena mientras tocas"),
                            juce::dontSendNotification);
            refreshXyPad();
        };
        xyPanel.addAndMakeVisible (xyLatchButton);

        xyPad.onMove  = [this] (float x, float y) { xyMoved (x, y); };
        xyPad.onTouch = [this] (bool down)        { xyTouched (down); };
        xyPanel.addAndMakeVisible (xyPad);

        styleButton (xyCloseButton, kKey);
        xyCloseButton.onClick = [this] { closeAllSheets(); };
        xyPanel.addAndMakeVisible (xyCloseButton);

        addAndMakeVisible (xyPanel);
        xyPanel.setVisible (false);
        xyPanel.paintContent = [this] (juce::Graphics& g) { paintXySheetContent (g); };
    }

    //  The screen is the MASTER, not the selected pad. Trimming already has
    //  a whole popup of its own, so putting the same waveform and the same
    //  trim handles on the face was one job done twice — and it meant the
    //  biggest element on the instrument showed a sample sitting still
    //  instead of the sound actually coming out.
    //  Swipe the screen to walk the pattern banks. The one gesture on the face
    //  that changes what is PLAYING without covering the pads with a sheet.
    spectrum.onSwipe = [this] (int dir)
    {
        const int next = (selectedPattern + dir + kNumPatterns) % kNumPatterns;
        //  Go through the stepper the sheet already owns, rather than doing
        //  the same six things again beside it: one place decides what
        //  changing bank means, and this gesture is only another way to ask.
        patternSlider.setValue (next, juce::sendNotificationSync);
        status.setText (T ("PATRON") + " " + Lang::ltr ("P" + juce::String (next + 1)),
                        juce::dontSendNotification);
    };

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

    //  NORMALIZAR ocupa la tercera celda de la fila de CHOKE y MODO, que
    //  estaba vacia: una fila de tres con dos controles dentro.
    styleButton (normButton, kKey);
    normButton.onClick = [this] { normalisePad(); };
    padSheet.addAndMakeVisible (normButton);

    //  QUITAR RUIDO va con REV y LOOP, en la fila que hay justo encima de la
    //  onda: las tres son cosas de la MUESTRA que se esta mirando.
    styleButton (denoiseButton, kKey);
    denoiseButton.onClick = [this] { denoisePad(); };
    padSheet.addAndMakeVisible (denoiseButton);

    //  El zoom. Tres tapas sobre la esquina de la pantalla: menos, cuanto, mas.
    //  La del medio dice a que aumento se esta y vuelve al fichero entero.
    {
        juce::TextButton* zb[3] = { &zoomOutButton, &zoomFitButton, &zoomInButton };
        for (auto* b : zb) { styleButton (*b, kKey); padSheet.addAndMakeVisible (b); }
        //  SE AMPLIA SOBRE EL RECORTE, no sobre lo que se este mirando.
        //
        //  Ampliar alrededor del centro de la vista es lo que hace un visor de
        //  fotos, y aqui no se esta mirando una foto: se esta buscando DONDE
        //  CORTAR. Con veinte segundos de muestra y el trozo bueno en el
        //  segundo trece, cada toque de + dejaba el trozo un poco mas fuera de
        //  pantalla y habia que volver a arrastrar - el zoom daba mas detalle
        //  de justo lo que no importaba.
        //
        //  El ancla es el punto medio entre las dos asas, que es la definicion
        //  de "la parte que estoy recortando". El pellizco conserva la suya -
        //  el punto entre los dos dedos -, porque ahi el dedo SI dice donde.
        auto trimCentre = [this]
        {
            if (selectedPad < 0) return 0.5f;
            return (padStart01[(size_t) selectedPad] + padEnd01[(size_t) selectedPad]) * 0.5f;
        };
        zoomOutButton.onClick = [this, trimCentre] { waveform.setZoom (waveform.getZoom() * 0.5f, trimCentre()); };
        zoomInButton .onClick = [this, trimCentre] { waveform.setZoom (waveform.getZoom() * 2.0f, trimCentre()); };
        //  Vuelve al fichero entero, y si ya esta entero salta al recorte: es
        //  el boton que se pulsa cuando te has perdido, y "perdido" tiene esas
        //  dos formas.
        zoomFitButton.onClick = [this]
        {
            if (waveform.getZoom() > 1.005f) { waveform.setZoom (1.0f, 0.5f); return; }
            if (selectedPad < 0) return;
            const float a = padStart01[(size_t) selectedPad], b = padEnd01[(size_t) selectedPad];
            const float span = juce::jmax (1.0f / WaveformDisplay::kMaxZoom, b - a);
            waveform.setZoom (juce::jlimit (1.0f, WaveformDisplay::kMaxZoom, 1.0f / span), (a + b) * 0.5f);
        };
        waveform.onZoomChanged = [this] (float z)
        {
            zoomFitButton.setButtonText (Lang::ltr ("x" + juce::String ((int) std::round (z))));
        };
    }

    //  Las dos pestanas de la ficha del pad, con el mismo mueble que las del
    //  secuenciador y las de AJUSTES: la ficha se queda quieta y cambia lo de
    //  dentro. Debajo del titulo en las dos paginas, para que la pestana que
    //  vas a pulsar no se mueva cuando pulsas la otra.
    {
        juce::TextButton* pb[3] = { &padSoundBtn, &padTrimBtn, &padRigBtn };
        for (int i = 0; i < 3; ++i)
        {
            styleButton (*pb[i], kKey);
            pb[i]->setClickingTogglesState (true);
            pb[i]->setRadioGroupId (8804);
            litAccent (*pb[i]);
            pb[i]->onClick = [this, i] { showPadPage (i); };
            padSheet.addAndMakeVisible (pb[i]);
        }
        padSoundBtn.setToggleState (true, juce::dontSendNotification);
    }

    //  Los seis envios del pad. El mismo valor que mueve el RACK, con la
    //  diferencia de que aqui se ve la fila entera de un pad en vez de la
    //  columna de un efecto. Los dos leen del motor al abrirse, asi que no hay
    //  copia que se pueda quedar vieja.
    for (int f = 0; f < kNumFx; ++f)
    {
        auto* sl = new juce::Slider();
        initKnob (*sl, 0.0, 1.0, 0.01, 1.0, 0.0, {});
        sl->textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)); };
        sl->updateText();
        sl->onValueChange = [this, f, sl]
        {
            if (selectedPad >= 0) engine.setPadSend (selectedPad, f, (float) sl->getValue());
            if (rackSends[f] != nullptr && rackPad == selectedPad)
                rackSends[f]->setValue (sl->getValue(), juce::dontSendNotification);
        };
        padSheet.addAndMakeVisible (sl);
        padSends.add (sl);
    }

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
    refreshAccessibleNames();

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
    //  Establish which half of the sequencer card is showing BEFORE the first
    //  layout: every control of it was addAndMakeVisible'd at construction, so
    //  without this the two pages are both "visible" until something happens
    //  to call showSeqPage - and the very first resized() would lay the step
    //  controls out on top of the grid.
    //  LOS MANDOS DE LA FICHA SON HIJOS DE LA FICHA, no de la cara.
    //
    //  initKnob e initSlider los cuelgan de MainComponent, que es donde viven
    //  los dieciseis pads, asi que un mando de la ficha PADS y un pad son
    //  HERMANOS - y dos hermanos que se pisan son un solapamiento, tanto para
    //  el banco como para el dedo que apunta. Mientras la ficha fue baja no se
    //  noto; en cuanto la pagina SONIDO crecio una fila de mandos, la tarjeta
    //  -que se centra, no se apoya abajo- bajo su borde inferior sobre la
    //  rejilla y salieron 3300 solapes en las 476 corridas, ochenta por
    //  pantalla. Colgarlos de la ficha no mueve un pixel: Sheet ocupa la
    //  ventana entera, asi que las coordenadas son las mismas, y ademas los
    //  pinta DESPUES de la tarjeta y de los rotulos.
    for (juce::Component* c : { (juce::Component*) &pitchSlider,  (juce::Component*) &fineSlider,
                                (juce::Component*) &volSlider,    (juce::Component*) &panSlider,
                                (juce::Component*) &attackSlider, (juce::Component*) &releaseSlider,
                                (juce::Component*) &cutSlider,    (juce::Component*) &resoSlider,
                                (juce::Component*) &chokeSlider,  (juce::Component*) &startSlider,
                                (juce::Component*) &endSlider })
        padSheet.addAndMakeVisible (c);

    showSeqPage (seqPageGrid);
    showPadPage (padPageSound);
    showMixBank (0);
    applySkin();
}

// Restyle everything that captured accent-coloured values at construction —
// the rest of the UI reads ZatiColours at paint time and only needs repaint.
//  Every cap in the tree, not the eight that happened to be named here.
//
//  The buttons are children of nine different sheets and of the face itself,
//  so the walk is recursive; a cap whose colour was NOT a skin token - a
//  pattern chip wearing its own tone, a mute wearing red - is left alone,
//  which is what roleFixed means.
static void restyleTree (juce::Component& c, const std::function<void (juce::TextButton&)>& fn)
{
    for (auto* k : c.getChildren())
    {
        if (auto* tb = dynamic_cast<juce::TextButton*> (k))
            fn (*tb);
        restyleTree (*k, fn);
    }
}

void MainComponent::applySkin()
{
    //  Both halves of every cap, in one pass over the whole tree: the resting
    //  colour from its role, and the lit colour from its mark. Naming the
    //  handful that had to be refreshed by hand is how sixty of them ended up
    //  wearing the palette they were built in - first the resting caps, and
    //  then, once that was fixed, the pressed ones.
    restyleTree (*this, [] (juce::TextButton& b)
    {
        const auto& props = b.getProperties();
        if (props.contains ("role"))
        {
            const int role = (int) props["role"];
            if (role != roleFixed) styleButton (b, roleColour (role));
        }
        //  After styleButton, never before: styleButton derives the lit-state
        //  TEXT from whatever the lit-state CAP currently is, so re-lighting
        //  the cap first would leave the text measured against the old accent.
        if ((int) props.getWithDefault ("lit", 0) == 1) litAccent (b);
    });

    const auto acc = ZatiColours::accent;
    // Lit-state text must stay legible on a dark accent (TINTA skin).
    const auto onTxt = ZatiColours::textOn (acc);

    for (int i = 0; i < patternButtons.size(); ++i)
        patternButtons[i]->setColour (juce::TextButton::buttonOnColourId, patternRowColour (i));

    styleButton (playButton, kKey);
    playButton.setColour (juce::TextButton::buttonOnColourId, ZatiColours::green);
    playButton.setColour (juce::TextButton::textColourOnId,  ZatiColours::white);

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
    //  T(). Estos dieciocho rotulos estaban en la cara de la maquina en
    //  espanol en las cuatro compilaciones, y el banco de traduccion no los
    //  veia porque compara el texto de los COMPONENTES y estos se pintan a
    //  mano. Un punto ciego de la prueba, no del codigo - y por eso la prueba
    //  lo dice ahora en su cabecera.
    return T (fxDefs[juce::jlimit (0, kNumFx - 1, focusedFx)].param[juce::jlimit (0, 2, idx)]);
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

//  Los cinco nombres de la rejilla. Con T de tresillo, que es como se dice y
//  como se lee en cualquier caja de ritmos: 1/16T son tres pasos donde caben
//  dos.
const char* MainComponent::gridName (int i)
{
    static const char* names[kNumGrids] = { "1/8", "1/8T", "1/16", "1/16T", "1/32" };
    return names[juce::jlimit (0, kNumGrids - 1, i)];
}


// ============================================================================
//  EL MANUAL, en ocho capitulos de cuatro o cinco lineas.
//
//  De consulta y no de lectura: esto se mira con el telefono en la mano y en
//  mitad de algo, asi que cada linea tiene que valerse sola. El manual largo -
//  el que explica POR QUE la ganancia va en decibelios o por que cuatro de los
//  seis efectos restan el seco - es otra cosa y vive fuera.
//
//  Todo pasa por T(): un manual en castellano dentro de una compilacion en
//  chino no es un manual.
// ============================================================================
namespace
{
    struct ManualChapter { const char* title; const char* lines[5]; };

    constexpr int kManualChapterCount = 9;
    const ManualChapter kManual[kManualChapterCount] =
    {
        { "EMPEZAR", {
            "CARGAR y luego un pad abre la biblioteca en ese pad",
            "Un toque toca; una pulsacion larga configura",
            "Manten un pad para abrir su ficha sin que suene",
            nullptr, nullptr } },
        { "PADS Y BANCOS", {
            "Cuatro bancos de dieciseis pads: los otros 48 siguen sonando",
            "Arrastra la rejilla para cambiar de banco",
            "El color de un pad lo acompana en la onda y en la rejilla",
            "CARGAR KIT reparte una carpeta entera por los pads",
            nullptr } },
        { "RECORTE", {
            "Arrastra las asas para mover el inicio y el fin",
            "Toca la onda en medio y suena desde ahi",
            "Pellizca para ampliar hasta x64; arrastra para mover la vista",
            "El zoom se centra en el recorte, no en donde estas mirando",
            nullptr } },
        { "SONIDO DEL PAD", {
            "CINTA afina cambiando la duracion; TONO la mantiene",
            "La ganancia va en decibelios, de -60 a +12",
            "NORMALIZAR deja el pico del recorte en -0.3 dBFS",
            "QUITAR RUIDO saca el siseo sin comerse lo que suena",
            "Doble toque en un mando: vuelve a su valor de siempre" } },
        { "SECUENCIADOR", {
            "Toca una celda para poner un paso; arrastra para pintar varios",
            "La pestana PASO dice que hace: nota, golpe y repeticion",
            "REJILLA es lo que dura un paso, tresillos incluidos",
            "Ocho patrones, y la cadena decide en que orden suenan",
            nullptr } },
        { "MEZCLA Y EFECTOS", {
            "Tocar un efecto lo enciende y le da los tres mandos",
            "Mantenlo pulsado para cogerle los mandos sin encenderlo",
            "RACK: un efecto y los 64 pads. EL PAD: los seis envios de uno",
            "El XY deja los pads tocables debajo, para las dos manos",
            "Verde hasta -12 dB, amarillo hasta -3, y el rojo se queda puesto" } },
        { "GUARDAR Y EXPORTAR", {
            "Un proyecto lleva sus muestras dentro y se puede mover entero",
            "La sesion se recupera sola al abrir la app",
            "MASTER es lo que oyes; PISTAS son los stems que suman a el",
            "Deshacer y rehacer, dieciseis pasos",
            nullptr } },
        { "MIDI", {
            "AJUSTES > MIDI: manda las notas de lo que suena a otro aparato",
            "El pad 1 es la nota 36, y de ahi hacia arriba",
            "RECIBIR deja que un teclado dispare los pads",
            "El secuenciador manda tambien, no solo tus dedos",
            nullptr } },
        { "SI ALGO NO SUENA", {
            "Mira la ganancia del pad y si hay un SOLO puesto en otro",
            "Mira su envio al efecto que estas oyendo",
            "Si la onda no reacciona estas ampliado: toca la tapa del medio",
            "AJUSTES > AUDIO ensena la latencia y el tamano de bloque",
            nullptr } },
    };

    constexpr int kManualLineH  = 30;   // una linea de texto y su aire
    constexpr int kManualTitleH = 26;
    constexpr int kManualGap    = 14;
}

// Everything the UI knows about the six effects, in signal order. One table,
// so the wiring below can be read against it line for line.
const MainComponent::FxDef MainComponent::fxDefs[MainComponent::kNumFx] =
{
    //  FLT, no ISO: un barrido con el centro NEUTRO. Ver AudioEngine::
    //  setFltSweep. El rango va de -1 a +1 y el valor por defecto es 0, que es
    //  "filtro fuera" - y por eso el doble toque en el mando vuelve al centro
    //  en vez de a una frecuencia.
    { "FLT",  { "BARRIDO", "RESO", "MIX" },
      { {   -1.0,     1.0, 0.01,    0.0,     0.0, 6 },
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

    //  BIT, not CRSH: five names of three letters and one of four, and the
    //  four-letter one is the only cap on the face whose lettering has to be
    //  squeezed to fit - measured on every screen in the matrix, not just the
    //  small ones. A row of six switches reads as a row when the tokens share
    //  a rhythm, and BIT is what the hardware this descends from calls it.
    { "BIT",  { "BITS", "RATE", "MIX" },
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
        //  El barrido dice de que LADO esta, no solo cuanto. Un "-62 %" no
        //  significa nada en un filtro; "LP 62" y "HP 62" si, y el centro se
        //  llama por su nombre porque es un estado, no un numero.
        case 6:  return std::abs (v) <= 0.03 ? T ("fuera")
                     : (v < 0.0 ? juce::String ("LP ") : juce::String ("HP "))
                         + juce::String (juce::roundToInt (std::abs (v) * 100.0));
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
        case  0: engine.setFltSweep  (v); break;
        case  1: engine.setFltReso   (v); break;
        case  2: engine.setFltMix    (v); break;
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

    const bool wasOn = fxOn[(size_t) f];
    setFxEnabled (f, ! wasOn);
    focusFx (f);

    //  Teach the hold at the only moment it is worth knowing: the tap that
    //  just switched off an effect you were probably trying to tune. A hint
    //  in a manual is a hint nobody reads; a hint standing where the mistake
    //  happened is the next thing you try.
    if (wasOn)
        status.setText (T ("%1 OFF - manten pulsado para ajustar sin apagar", fxDefs[f].name),
                        juce::dontSendNotification);

    repaint();
}

// --- El panel XY ---------------------------------------------------------
//
//  Nada de esto guarda un valor. Los parametros siguen viviendo en fxParams,
//  igual que para los tres mandos, y el panel solo escribe en ellos: asi los
//  mandos y el panel no pueden discrepar, porque son dos ventanas al mismo
//  numero. Es la misma regla que ya seguian CTRL 1-3.

//  Abrir el panel NO cierra nada mas ni tapa los pads: es el unico sitio de
//  esta app que convive con la cara en vez de ponerse delante. Cerrarlo con el
//  efecto todavia sonando en momentaneo lo apaga, por lo mismo que lo hace
//  closeAllSheets: el modo dice "sale al soltar" y aqui no va a llegar el
//  mouseUp.
void MainComponent::toggleXyPanel()
{
    if (xyPanel.isVisible())
    {
        if (! xyLatch && ! xyWasOn && fxOn[(size_t) xyFx]) setFxEnabled (xyFx, false);
        xyPad.setTouched (false);
        xyPanel.setVisible (false);
        xyButton.setToggleState (false, juce::dontSendNotification);
        resized();
        repaint();
        return;
    }

    closeAllSheets();                 // una ficha abierta si taparia el panel
    selectXyFx (focusedFx);           // abre sobre el efecto que ya tenias delante
    xyPanel.setVisible (true);
    xyPanel.toFront (false);
    xyButton.setToggleState (true, juce::dontSendNotification);
    resized();
    repaint();
}

void MainComponent::selectXyFx (int f)
{
    if (! juce::isPositiveAndBelow (f, kNumFx)) return;

    //  Cambiar de efecto con el modo momentaneo y el anterior sonando lo
    //  dejaria abierto para siempre: el dedo que lo encendio ya no va a
    //  levantarse sobre EL. Se apaga al salir de el, no al entrar en el
    //  siguiente, que es cuando todavia se sabe cual era.
    if (! xyLatch && f != xyFx && ! xyPad.isTouched() && fxOn[(size_t) xyFx])
        setFxEnabled (xyFx, false);

    xyFx = f;
    if (auto* b = xyFxButtons[f]) b->setToggleState (true, juce::dontSendNotification);
    //  El panel toma tambien los tres mandos de la cara. Son el mismo efecto:
    //  volver de la ficha y encontrarse los mandos en otro es lo que hace que
    //  una app se sienta como dos apps.
    focusFx (f);
    refreshXyPad();
}

//  Donde esta el dedo, en 0..1, se convierte al valor real del parametro
//  usando el MISMO sesgo que el mando de la cara. Un filtro repartido lineal
//  entre 20 Hz y 20 kHz deja el 90% del recorrido por encima de los 2 kHz,
//  que es donde no pasa nada: sin el sesgo, el panel barre en un centimetro
//  todo lo que importa y en el resto nada. proportionOfLengthToValue es
//  exactamente la curva que ya tiene el mando.
void MainComponent::xyMoved (float x, float y)
{
    const float xy[2] = { x, y };
    for (int pi = 0; pi < 2; ++pi)
    {
        auto& p = fxParam (xyFx, pi);
        p.setValue (p.proportionOfLengthToValue ((double) juce::jlimit (0.0f, 1.0f, xy[pi])),
                    juce::dontSendNotification);
        pushFxParam (xyFx, pi);
    }
    refreshMacroValues();      // los mandos de la cara siguen al dedo
    refreshXyPad();
}

//  APOYAR Y LEVANTAR ES EL GESTO, y en momentaneo es lo que enciende y apaga.
//
//  Se recuerda como estaba ANTES de apoyar: si el efecto ya venia encendido,
//  levantar el dedo no puede apagarlo - no lo encendiste tu, y apagar algo que
//  no habias encendido es la clase de sorpresa que te deja sin efecto en mitad
//  de un directo.
void MainComponent::xyTouched (bool down)
{
    //  FIJO NO ES "NO ENTRA", ES "NO SALE". Tocar enciende igual - si no, el
    //  panel se movia, los numeros cambiaban y no sonaba nada, y la unica
    //  forma de averiguar por que era salir de la ficha a encender el efecto
    //  en la cara. Lo que cambia entre los dos modos es lo que hace SOLTAR.
    if (xyLatch)
    {
        if (down && ! fxOn[(size_t) xyFx]) setFxEnabled (xyFx, true);
        refreshXyPad();
        return;
    }

    if (down)
    {
        xyWasOn = fxOn[(size_t) xyFx];
        if (! xyWasOn) setFxEnabled (xyFx, true);
    }
    else if (! xyWasOn)
    {
        setFxEnabled (xyFx, false);
    }
    refreshXyPad();
}

//  El panel refleja el estado real de los parametros, no el ultimo sitio donde
//  estuvo el dedo: si mueves un mando de la cara con la ficha abierta, la cruz
//  se mueve. Un panel que solo se cree a si mismo miente en cuanto algo mas
//  toca el mismo numero.
void MainComponent::refreshXyPad()
{
    const auto& d = fxDefs[juce::jlimit (0, kNumFx - 1, xyFx)];
    xyPad.setAxisNames  (T (d.param[0]), T (d.param[1]));
    xyPad.setAxisValues (fxFormat (d.spec[0], fxParam (xyFx, 0).getValue()),
                         fxFormat (d.spec[1], fxParam (xyFx, 1).getValue()));
    xyPad.setPosition ((float) fxParam (xyFx, 0).valueToProportionOfLength (fxParam (xyFx, 0).getValue()),
                       (float) fxParam (xyFx, 1).valueToProportionOfLength (fxParam (xyFx, 1).getValue()));
    xyLatchButton.setButtonText (xyLatch ? T ("FIJO") : T ("MOMENTANEO"));
    xyPanel.repaint();
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

//  Swap the page inside the settings card. The controls of the page you are
//  not on are HIDDEN, not merely unpositioned: a JUCE child with stale bounds
//  is still a child, and it would keep drawing and keep taking taps behind the
//  page you are actually looking at.
void MainComponent::showSetPage (int page)
{
    setPage = juce::jlimit ((int) pageAudio, (int) pageGestures, page);
    const bool onAudio = (setPage == pageAudio);
    const bool onProj  = (setPage == pageProjects);
    const bool onMidi  = (setPage == pageMidi);

    pageAudioBtn.setToggleState (onAudio, juce::dontSendNotification);
    pageMidiBtn .setToggleState (onMidi,  juce::dontSendNotification);
    pageProjBtn .setToggleState (onProj,  juce::dontSendNotification);
    pageGestBtn .setToggleState (setPage == pageGestures, juce::dontSendNotification);

    midiOutBtn.setVisible (onMidi);
    midiInBtn.setVisible  (onMidi);
    midiOutBox.setVisible (onMidi);
    midiInBox.setVisible  (onMidi);

    measureButton.setVisible (onAudio);
    quantButton.setVisible   (onAudio);
    testButton.setVisible    (onAudio);
    for (auto* b : bufButtons)  b->setVisible (onAudio);
    for (auto* b : rateButtons) b->setVisible (onAudio);
    for (auto* b : langButtons) b->setVisible (onAudio);
    for (auto* b : skinButtons) b->setVisible (onAudio);

    projList.setVisible          (onProj);
    projNameBox.setVisible       (onProj);
    projSaveButton.setVisible    (onProj);
    projLoadButton.setVisible    (onProj);
    projNewButton.setVisible     (onProj);
    projDeleteButton.setVisible  (onProj);
    projExportButton.setVisible  (onProj);

    if (onProj)       refreshProjectList();
    else if (onAudio) refreshAudioOptions();
    else if (onMidi)  refreshMidiDevices();

    resized();
    setSheet.repaint();
}

//  Same rule as the settings card, and for the same reason: the controls of
//  the page you are not on are HIDDEN, not merely left with stale bounds. A
//  JUCE child that is still visible keeps painting and keeps eating taps
//  behind the page you are actually looking at - and on this card that would
//  mean the sixteen-lane grid swallowing every touch aimed at the note
//  stepper sitting on top of it.
void MainComponent::showSeqPage (int page)
{
    seqPage = juce::jlimit ((int) seqPageGrid, (int) seqPageStep, page);
    const bool onGrid = (seqPage == seqPageGrid);

    seqGridBtn.setToggleState (onGrid,   juce::dontSendNotification);
    seqStepBtn.setToggleState (! onGrid, juce::dontSendNotification);

    stepGrid.setVisible      (onGrid);
    //  La visibilidad de las tapas de banco NO se decide aqui: la decide
    //  resized(), que es el unico que sabe si caben sin encoger la rejilla.
    patternSlider.setVisible (onGrid);
    lengthSlider.setVisible  (onGrid);
    bpmSlider.setVisible     (onGrid);
    clearButton.setVisible   (onGrid);
    tapButton.setVisible     (onGrid);
    copyPatBtn.setVisible    (onGrid);
    pastePatBtn.setVisible   (onGrid);
    //  The bar row has a second condition - a one-bar pattern has nothing to
    //  select - so resized() is the only place allowed to turn it ON. Here it
    //  can only ever turn it off.
    if (! onGrid)
        for (auto* b : barButtons) b->setVisible (false);

    for (auto* b : patternButtons) b->setVisible (! onGrid);
    chainClearButton.setVisible (! onGrid);
    noteSlider.setVisible       (! onGrid);
    velSlider.setVisible        (! onGrid);
    rollSlider.setVisible       (! onGrid);
    swingSlider.setVisible      (! onGrid);
    gridSlider.setVisible       (! onGrid);

    resized();
    seqSheet.repaint();
}

//  Igual que showSeqPage: escondido, no solo sin colocar. Un control que sigue
//  visible fuera de su pagina se pinta encima de la que si esta, y se come los
//  arrastres de lo que tiene delante.
//  ¿CABEN LAS TRES PALABRAS DE FUENTE EN UNA FILA?
//
//  Se pregunta dos veces - al presupuestar la altura de la pagina y al colocar
//  la fila -, y las dos tienen que contestar lo mismo o la ficha reserva una
//  fila que no usa o usa una que no reservo. De ahi que sea una funcion y no
//  dos cuentas parecidas.
bool MainComponent::padSourceWraps (int rowWidth) const
{
    return ! padRowFits (rowWidth, { &chopButton, &micButton, &resampleButton });
}

//  ¿CABEN LAS CUATRO PESTANAS DE AJUSTES EN UNA FILA?
//
//  Y se mide con LA FUENTE QUE LAS DIBUJA, que es lo que el primer intento hizo
//  mal: reutilizo padRowFits, que mide a 11 px porque es lo que usan las tapas
//  de la ficha del pad, mientras drawButtonText escribe la pestana a
//  altura*0.38 - 12.16 px con Metrics::tab. Un diez por ciento de diferencia,
//  suficiente para que la cuenta dijera que caben y el banco midiera
//  "PROYECTOS pide 56 y tiene 42". Una medida hecha con otra fuente no es una
//  medida de esto.
bool MainComponent::setTabsFit (int rowWidth) const
{
    const auto capFont = ZatiColours::monoFont (juce::jlimit (10.0f, 14.5f, (float) Metrics::tab * 0.38f), true)
                             .withExtraKerningFactor (0.06f);
    const juce::TextButton* tabs[] = { &pageAudioBtn, &pageMidiBtn, &pageProjBtn, &pageGestBtn };

    //  La mas ancha decide, porque las cuatro reciben el MISMO cuarto. Sumar
    //  los cuatro anchos seria la cuenta de un reparto proporcional, que no es
    //  el que hace esta fila.
    float widest = 0.0f;
    for (const auto* b : tabs)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (capFont, b->getButtonText()));

    //  Lo que le queda a la letra dentro de un cuarto. El margen NO es
    //  kTextPad: drawButtonText reduce por `jlimit (3, 5, ancho / 14)`, que en
    //  una pestana de 56 px son 4 px por lado. Poner 3 dejaba pasar 344x882 -
    //  una fila, PROYECTOS recortado - mientras 280 y 360 salian bien, que es
    //  el sintoma clasico de un margen que se queda corto por dos pixeles.
    const float tabW   = (float) rowWidth / 4.0f - 2.0f * (float) Metrics::halfGap;
    const float inset  = juce::jlimit (3.0f, 5.0f, tabW / 14.0f);
    return widest <= tabW - 2.0f * inset;
}

//  ¿Caben estas tapas en una fila de este ancho? Con margen, porque la fuente
//  con la que se mide aqui no es exactamente la que dibuja la tapa.
bool MainComponent::padRowFits (int rowWidth,
                                std::initializer_list<const juce::TextButton*> bs) const
{
    const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);
    int need = 0;
    for (const juce::TextButton* b : bs)
        need += (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, b->getButtonText()))
              + 2 * Metrics::sm;
    return need <= rowWidth - Metrics::lg;
}

void MainComponent::showPadPage (int page)
{
    padPage = juce::jlimit ((int) padPageSound, (int) padPageRig, page);
    const bool onSound = (padPage == padPageSound);
    const bool onTrim  = (padPage == padPageTrim);
    const bool onRig   = (padPage == padPageRig);

    padSoundBtn.setToggleState (onSound, juce::dontSendNotification);
    padTrimBtn .setToggleState (onTrim,  juce::dontSendNotification);
    padRigBtn  .setToggleState (onRig,   juce::dontSendNotification);

    for (juce::Component* c : { (juce::Component*) &pitchSlider, (juce::Component*) &fineSlider,
                                (juce::Component*) &volSlider,   (juce::Component*) &panSlider,
                                (juce::Component*) &attackSlider,(juce::Component*) &releaseSlider,
                                (juce::Component*) &cutSlider,   (juce::Component*) &resoSlider,
                                (juce::Component*) &chokeSlider, (juce::Component*) &modeButton,
                                (juce::Component*) &normButton })
        c->setVisible (onSound);

    for (juce::Component* c : { (juce::Component*) &startSlider,   (juce::Component*) &endSlider,
                                (juce::Component*) &reverseButton, (juce::Component*) &loopButton,
                                (juce::Component*) &waveform,      (juce::Component*) &denoiseButton,
                                (juce::Component*) &zoomOutButton, (juce::Component*) &zoomFitButton,
                                (juce::Component*) &zoomInButton })
        c->setVisible (onTrim);

    for (auto* s : padSends) s->setVisible (onRig);
    autocutButton .setVisible (onRig);
    duckButton    .setVisible (onRig);
    chopButton    .setVisible (onRig);
    micButton     .setVisible (onRig);
    resampleButton.setVisible (onRig);

    resized();
    padSheet.repaint();
}

//  Only the sixteen strips of the bank on show exist as far as the layout and
//  the paint are concerned. Hidden, not merely unpositioned: forty-eight
//  sliders left visible inside a viewport keep painting and keep taking drags
//  through the sixteen in front of them.
void MainComponent::showMixBank (int bank)
{
    mixBank = juce::jlimit (0, kNumBanks - 1, bank);
    if (auto* t = mixBankBtns[mixBank]) t->setToggleState (true, juce::dontSendNotification);

    for (int i = 0; i < kNumPads; ++i)
    {
        const bool on = (i / kPadsPerBank) == mixBank;
        if (auto* f = mixFaders[i]) f->setVisible (on);
        if (auto* p = mixPans[i])   p->setVisible (on);
        if (auto* m = mixMutes[i])  m->setVisible (on);
        if (auto* s = mixSolos[i])  s->setVisible (on);
    }

    //  Back to the top of the new bank. Left where it was, switching from a
    //  bank you had scrolled to the foot of opened the next one halfway down,
    //  with its first strips above the fold and nothing saying so.
    mixScroll.setViewPosition (0, 0);

    resized();
    mixRows.repaint();
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
    setSheet.setVisible (false);
    exportSheet.setVisible (false);
    rackSheet.setVisible (false);
    chopSheet.setVisible (false);
    manualSheet.setVisible (false);

    //  CERRAR LA FICHA XY EN MOMENTANEO TIENE QUE APAGAR EL EFECTO.
    //
    //  El modo dice "sale al soltar", y cerrar la tarjeta con el dedo apoyado
    //  - tocando fuera, o con la tecla de cerrar - se lleva el panel por
    //  delante sin que llegue nunca el mouseUp. Sin esto te quedas con un
    //  delive abierto sobre el master y sin panel con el que quitarlo.
    if (xyPanel.isVisible() && ! xyLatch && ! xyWasOn && fxOn[(size_t) xyFx])
        setFxEnabled (xyFx, false);
    xyPad.setTouched (false);
    xyPanel.setVisible (false);
    xyButton.setToggleState (false, juce::dontSendNotification);

    setButton.setToggleState (false, juce::dontSendNotification);
    repaint();
}

MainComponent::~MainComponent()
{
    //  EL PUENTE MIDI SE CIERRA ANTES QUE NADA, y no es una precaucion: es un
    //  uso despues de liberar, todas las veces, con la salida encendida.
    //
    //  `midi` se declara en la linea 376 y `engine` en la 594, y los miembros
    //  se destruyen al REVES de como se declaran: el motor muere PRIMERO. El
    //  hilo del puente esta leyendo `src`, que apunta a la cola que vive dentro
    //  del motor - se la pasa setSource en el constructor -, asi que en cuanto
    //  ~AudioEngine termina, ese hilo drena memoria liberada hasta que le toca
    //  morir a el.
    //
    //  Reordenar los miembros lo arreglaria tambien y seria peor: dejaria la
    //  correccion dependiendo de que nadie mueva una linea en un fichero de mil
    //  quinientas. Cerrarlo aqui es explicito y sobrevive a cualquier orden.
    midi.closeInput();
    midi.closeOutput();

    // The bounce thread holds a reference to the engine and to the pad
    // buffers, so it must be gone before either can be.
    if (exportJob != nullptr) { exportJob->signalThreadShouldExit(); exportJob.reset(); endBusy(); }

    //  Un guardado a medias deja una carpeta con muestras y SIN project.xml:
    //  la lista de proyectos la ensena igual, y al abrirla dice "no encuentro
    //  el proyecto". Al cerrar se termina de golpe - son los pads que falten,
    //  no los 64 - porque aqui ya no hay temporizador que siga troceando.
    while (padSaveJob != nullptr) stepPadSaveJob();

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
    //  The rule runs the width of the ZONE the name belongs to, which is the
    //  whole face in portrait and one of the two columns when the window is
    //  wider than it is tall - a rule for the pads that crossed the screen and
    //  the knobs on its way there would be naming all three.
    auto engraveIn = [&g, &full, &rule] (const juce::String& text, int seamTop, int zoneTop,
                                         juce::Rectangle<int> span = {})
    {
        const auto s = span.isEmpty() ? full : span.toFloat();

        auto engrave = [&g, &rule, &s] (const juce::String& t, float y)
        {
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.30f));
        const float tw  = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), t);
        const float cx  = s.getCentreX();
        const float x0  = cx - tw * 0.5f;
        const float gap = 9.0f;                       // air the rule leaves around the word

        rule (s.getX() + 10.0f, x0 - gap, y, 0.16f);
        rule (x0 + tw + gap, s.getRight() - 10.0f, y, 0.16f);

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
        g.setColour (ZatiColours::plate);
        g.fillRoundedRectangle (pp, 4.0f);
        g.setColour (ZatiColours::plateEdge);
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
        g.setColour (ZatiColours::plate);
        g.fillRoundedRectangle (cp, 4.0f);
        g.setColour (ZatiColours::plateEdge);
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
        engraveIn (T ("CONTROL"), ctrlSeamTop, ctrlPlateArea.getY(), faceColumn);

    if (! fxRowArea.isEmpty())
        engraveIn (T ("EFECTOS"), fxSeamTop, fxRowArea.getY(), faceColumn);

    if (! padPlateArea.isEmpty())
    {
        //  ...and it stops where the bank chips start, on BOTH sides. A rule
        //  that runs under four controls is not naming a zone, it is crossing
        //  them out. With a pair at each end the word ends up centred in what
        //  is left between them, which is the middle of the seam - that is the
        //  symmetry, and it falls out of the geometry instead of being nudged.
        //  Compared by x and not by name: in Arabic the leading pair is the
        //  one on the right, and trimming "left by lead" would clip the wrong
        //  end and let the rule run straight through the chips.
        auto span = wideFace ? padPlateArea.expanded (ZatiLookAndFeel::kAir, 0)
                             : full.toNearestInt();
        const bool leadIsLeft = bankRowLeftArea.getX() <= bankRowRightArea.getX();
        const auto nearSide = leadIsLeft ? bankRowLeftArea  : bankRowRightArea;
        const auto farSide  = leadIsLeft ? bankRowRightArea : bankRowLeftArea;
        if (! nearSide.isEmpty())
            span.setLeft  (juce::jmax (span.getX(),     nearSide.getRight() + Metrics::gap));
        if (! farSide.isEmpty())
            span.setRight (juce::jmin (span.getRight(), farSide.getX()      - Metrics::gap));
        //  An empty span means "no span" to engraveIn, and it would answer by
        //  drawing the rule across the WHOLE face - straight through the chips
        //  it was just told to avoid. On a window too narrow for both pairs and
        //  a word, the word wins and the rule simply does not appear.
        if (span.getWidth() > 2)
            engraveIn (T ("PADS"), padSeamTop, padPlateArea.getY(), span);
    }

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

    // 3. Header: the wordmark, the open project, and a printed colour band
    //    across the whole face.
    //
    //    The band is the only colour on the chassis, and it is not decoration:
    //    it is the state of the kit, one segment per zati, lit where a pad of
    //    that colour has a sound in it and nearly out where none does. It used
    //    to be eight nine-pixel squares hiding in the top right corner - the
    //    same information, but small enough that nobody would ever look at it,
    //    and cramped into a corner instead of belonging to the machine.
    //
    //    Across the width it reads the way a printed stripe on a piece of
    //    studio gear reads: it tells you what the box is before it tells you
    //    anything else. No touch targets here - a readout, not a control.
    if (! headerArea.isEmpty())
    {
        auto h = headerArea;
        g.setColour (ZatiColours::ink);
        g.setFont (ZatiColours::displayFont (Metrics::fTitle).withExtraKerningFactor (0.16f));
        g.drawText ("ZATI", h.getX(), h.getY(), 140, h.getHeight(), juce::Justification::centredLeft);

        rule ((float) h.getX(), (float) h.getRight(), (float) h.getBottom() + 2.0f, 0.22f);

        //  The band between the wordmark and the strip was empty across the
        //  whole width of the machine, while the one thing you cannot see
        //  anywhere on the face - which project is open - was buried three
        //  taps deep in PROJ. It goes here, on the baseline of the wordmark.
        {
            //  Hard to the far edge, not trailing after the wordmark.
            //
            //  Floating just to the right of ZATI it read as a subtitle - part
            //  of the logo, drifting to a different place with every project
            //  name. Pinned to the opposite end it becomes the other half of a
            //  header: the machine on one side, what is loaded in it on the
            //  other, both anchored. That is how a piece of gear labels
            //  itself, and it stops moving when the name changes.
            const int nameX = h.getX() + 138;
            const int nameW = h.getRight() - nameX;
            if (nameW > 40)
            {
                const bool named = currentProject.isNotEmpty();
                g.setColour (ZatiColours::ink.withAlpha (named ? 0.55f : 0.28f));
                g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
                g.drawText (named ? currentProject.toUpperCase() : T ("SIN GUARDAR"),
                            nameX, h.getY(), nameW, h.getHeight(),
                            juce::Justification::bottomRight, true);
            }
        }

        //  The band itself: eight segments, edge to edge, sitting on the rule.
        {
            const float x0 = (float) h.getX();
            const float w  = (float) h.getWidth() / (float) Zati::kNumColours;
            const float y0 = (float) h.getBottom() + 4.0f;
            const float bh = 4.0f;

            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                bool used = false;
                for (int p = 0; p < kNumPads && ! used; ++p)
                    used = padHasSample[(size_t) p] && padZati[(size_t) p] == i;

                //  Lit or nearly out - never absent. A gap in the stripe would
                //  read as a printing fault; a dim segment reads as a colour
                //  you have not used yet.
                //  Even unused it has to READ as a printed stripe. At a fifth
                //  it was a smudge you would take for a rendering artefact;
                //  the difference between used and not is still obvious at
                //  these two values, and the machine keeps its colour whether
                //  you have loaded anything or not.
                g.setColour (Zati::colour (i).withAlpha (used ? 1.0f : 0.45f));
                g.fillRect (x0 + (float) i * w, y0, w - 1.0f, bh);
            }
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
                padTitleRow, Lang::start(), true);

    {
        //  Group headers, each with a hairline running out to the right edge -
        //  the same engraved rule the machine face uses, so a sheet reads as
        //  three blocks (what the sound is, where it is cut, what the pad is)
        //  instead of eleven controls in a column.
        static const char* secSound[3] = { "SONIDO", "", "" };
        static const char* secTrim[3]  = { "RECORTE", "", "" };
        static const char* secRig[3]   = { "ENVIOS", "CORTE",   "FUENTE" };
        static const char* secRigT[3]  = { "ENVIOS", "EL PAD",  "" };
        const char* const* secNames = (padPage == padPageSound) ? secSound
                                    : (padPage == padPageTrim)   ? secTrim
                                                                 : (padRigTight ? secRigT : secRig);
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
            g.drawText (secText, textRow, Lang::start());

            const float tw = juce::GlyphArrangement::getStringWidth (
                                 ZatiColours::labelFont (Metrics::fMeta, 0.22f),
                                 secText);
            const float ly = (float) textRow.getCentreY() + 1.0f;
            g.setColour (ZatiColours::ink.withAlpha (0.18f));
            g.fillRect ((float) r.getX() + tw + 8.0f, ly,
                        juce::jmax (0.0f, (float) r.getRight() - ((float) r.getX() + tw + 8.0f)), 1.0f);
        }

        // Knobs: label above (same convention as FX).
        //
        //  CON SU COLOR, que es lo que faltaba. Este bloque ponia la fuente y
        //  no el color, asi que heredaba el ultimo que se hubiera puesto - y
        //  el ultimo era el del FILETE de la seccion, tinta al 18%. Resultado:
        //  PITCH, FINO, GANANCIA, PAN, ATAQUE, CAIDA, INICIO, FIN y los seis
        //  envios se pintaban a 1.4 de contraste, mas claros que las propias
        //  marcas del dial que hay debajo. El nombre de un control mas flojo
        //  que su decoracion es el fallo de siempre: se ve y no se lee.
        //
        //  0.55 es el mismo con el que la cara pinta los nombres de sus tres
        //  mandos, medido en Tests/skins.py como "rotulo de seccion".
        g.setColour (ZatiColours::ink.withAlpha (0.55f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        //  placeKnobRow reserves 16 for the name and then insets the knob by
        //  2, so the band is the sixteen pixels that end two above the dial.
        auto name = [&g] (juce::Slider& s, const char* t)
        {
            g.drawText (T (t), bandAbove (s, ZatiLookAndFeel::kKnobName, 2, 6), juce::Justification::centred);
        };
        if (padPage == padPageSound)
        {
            name (pitchSlider, "PITCH"); name (fineSlider, "FINO"); name (volSlider, "GANANCIA");
            name (panSlider, "PAN");
            name (attackSlider, "ATTACK"); name (releaseSlider, "RELEASE");
            name (cutSlider, "CORTE|filtro"); name (resoSlider, "RESON");
            name (chokeSlider, "CHOKE");

            //  Same band, one pixel lower: the third row insets its cells by 3.
            g.drawText (T ("MODO"), bandAbove (modeButton, ZatiLookAndFeel::kKnobName, 3, 6), juce::Justification::centred);
        }
        else if (padPage == padPageTrim)
        {
            // Start/End stay linear (a trim range, not a knob): label to the left.
            g.setColour (ZatiColours::ink.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.06f));
            auto lab = [&g] (juce::Slider& s, const char* t)
            {
                auto r = s.getBounds();
                g.drawText (T (t), r.getX() - (ZatiLookAndFeel::kTrimLabel + 2), r.getY(),
                            ZatiLookAndFeel::kTrimLabel - 4, r.getHeight(), Lang::start());
            };
            lab (startSlider, "START"); lab (endSlider, "END");
        }
        else
        {
            //  Cada envio se llama como el efecto al que manda, que es la
            //  misma palabra que lleva la tapa de la cara. Un envio con un
            //  nombre propio - "ENV 3" - obliga a recordar el orden de los
            //  seis; con el nombre del efecto no hay nada que recordar.
            for (int f = 0; f < kNumFx && f < padSends.size(); ++f)
                if (auto* sl = padSends[f])
                    g.drawText (T (fxDefs[f].name), bandAbove (*sl, ZatiLookAndFeel::kKnobName, 2, 6),
                                juce::Justification::centred);
        }

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
    auto inner = seqSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    const juce::String t = T (seqPage == seqPageStep ? "PASO" : "PASOS")
                         + "  " + dot + "  " + T ("PAD %1", juce::String (sp + 1))
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
    //
    //  The bands come from resized(), which is the only thing that knows which
    //  page is showing. Deriving them here from each control's bounds drew the
    //  caption of every control on the card whether it was laid out or not:
    //  the moment the sheet grew a second page, PATRON / LARGO / TEMPO were
    //  still being painted - across the chain buttons of the OTHER page, at
    //  whatever coordinates they happened to hold from the last time they were
    //  visible.
    {
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.20f));

        for (const auto& lb : seqLabelBands)
        {
            auto band = lb.band;
            if (band.isEmpty()) continue;
            band.setWidth (juce::jmax (60, band.getWidth()));
            g.drawText (T (lb.key), band.translated (2, 0), Lang::start());
        }
    }

    //  The step page acts on ONE step, and until you have tapped one there is
    //  nothing for NOTA or GOLPE to act on. Saying so is the difference between
    //  a control that looks broken and a control that is waiting.
    if (seqPage == seqPageStep && ! seqFootArea.isEmpty())
    {
        g.setColour (ZatiColours::inkDim.withAlpha (selectedStep < 0 ? 0.95f : 0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
        g.drawText (selectedStep < 0
                      ? T ("toca un paso en PASOS para editarlo")
                      : T ("editando el paso %1", Lang::ltr (juce::String (selectedStep + 1))),
                    seqFootArea, Lang::start());
    }

    // The grid paints its own playhead and lane colours (see StepGrid).
    // Ring the bank being edited on the chain-include row.
    if (auto* b = patternButtons[selectedPattern]; b != nullptr && b->isVisible())
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

    //  El bloque bajo la tarjeta, como el de cualquier tapa: oscuro. Escrito
    //  con la tinta salia crema en LACA y hueso en GRAFITO - una tarjeta con
    //  halo claro por debajo, que es lo contrario de estar apoyada sobre algo.
    g.setColour (ZatiColours::groove (0.55f));
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
    //  A SQUARE THAT FITS BOTH WAYS.
    //
    //  The cell used to be jlimit (cellW * 3/4, cellW, roomPerRow), and jlimit
    //  clamps UP as readily as down: when the room per row fell below three
    //  quarters of the width - a short phone, a rotated one, a small window -
    //  the cell was clamped back UP to a size the area did not have, and
    //  withSizeKeepingCentre then centred a grid taller than its own box. The
    //  overflow went out both ends. Measured: at 360x640 the pads sat 7 px
    //  over the effects row, and rotated to 915x412 four of them were laid out
    //  past the bottom of the window entirely, on top of the transport keys.
    //
    //  A missing pixel has to come out of the pad, not out of the section
    //  next to it. One number derived from BOTH constraints can never exceed
    //  either, and it keeps the pad square - which is what it is for.
    //  Width still decides the cell - a pad grid that does not reach the sides
    //  of the face reads as a widget dropped on it, and shrinking to a small
    //  centred square was the first fix and the wrong one. Height only ever
    //  takes away: square while there is room for square, flatter than square
    //  when there is not, and never one pixel taller than the box it was
    //  handed.
    const int cellW = (area.getWidth() - (cols - 1) * gap) / cols;
    const int cellH = juce::jmax (24, juce::jmin (cellW, (area.getHeight() - (rows - 1) * gap) / rows));

    auto grid = area.withSizeKeepingCentre (cols * cellW + (cols - 1) * gap,
                                            rows * cellH + (rows - 1) * gap);

    //  The plate the pads are bolted to. Remembered rather than recomputed in
    //  paint(), because the grid is centred inside whatever room is left and
    //  only this function knows where that landed.
    //  Five, not eight: the eight were the room four screw heads needed at
    //  the corners. Without them the plate can hug the pads, and the three
    //  pixels it gives back become distance to the section above it.
    padPlateArea = grid.expanded (ZatiLookAndFeel::kPlateLip, ZatiLookAndFeel::kPlateLip);

    // SP-style numbering: pad 01 sits BOTTOM-left, 16 top-right — logical row
    // r of the pad index maps to visual row (rows-1-r).
    //  Only the bank on screen is laid out; the other forty-eight are hidden.
    //  They still exist, still hold their sample and their settings, and still
    //  sound when the sequencer asks for them - a bank you cannot see is not a
    //  bank that stopped playing.
    const int base = currentBank * kPadsPerBank;

    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setVisible (i >= base && i < base + kPadsPerBank);

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int idx = base + r * cols + c;
            const int vr  = rows - 1 - r;
            if (auto* p = pads[idx])
                p->setBounds (grid.getX() + c * (cellW + gap),
                             grid.getY() + vr * (cellH + gap),
                             cellW, cellH);
        }
}

//  LA BARRA DE MODULOS NO SE REPARTE A PARTES IGUALES.
//
//  Con cinco tapas daba 66 px cada una en un movil de 360 y todo cabia. Con
//  seis - XY es un modulo, no un ajuste escondido - da 55, y CANCION, el
//  rotulo mas largo de los seis en espanol, necesita 62. En horizontal, donde
//  la barra comparte fila con el transporte, el margen era aun mas justo:
//  medido, CANCION pedia 36 y tenia 32.
//
//  Asi que cada tapa pide lo que su palabra MIDE en el idioma en el que se
//  esta dibujando, y el sobrante se reparte en proporcion a lo pedido. Un
//  reparto ciego a partes iguales es lo que hace que anadir una pestana rompa
//  la fila entera en cuatro idiomas a la vez - y en chino y arabe las palabras
//  no miden lo que miden en espanol.
//
//  El ultimo se lleva el resto del rectangulo, no su cuota calculada: seis
//  divisiones enteras dejan la fila terminando hasta seis pixeles antes del
//  borde, y ese hueco se ve porque la fila de al lado si llega.
//  ¿Caben estas tapas en una fila de este ancho SIN apretar ninguna?
//
//  layoutModuleBar reparte lo que hay y no se niega nunca: si no llega, encoge.
//  Eso esta bien cuando falta un pixel y es un fallo cuando faltan catorce, y
//  el que llama es el unico que sabe si tiene una segunda fila que ofrecer.
//  Misma fuente y mismo margen que el reparto, o la respuesta seria a otra
//  pregunta.
bool MainComponent::moduleBarFits (int rowWidth, juce::TextButton** mb, int count) const
{
    //  EL AIRE QUE HAY QUE CONTAR NO ES EL DEL REPARTO, ES EL QUE SE COME LA
    //  CADENA DE DIBUJO ENTERA. layoutModuleBar reserva 2*Metrics::sm por tapa
    //  y luego encoge la tapa 2 px por lado (reduced), y encima
    //  drawButtonText le quita jlimit (3, 5, ancho / 14) mas por lado. Contar
    //  solo los 16 del reparto dejaba pasar por un pixel - "CARGAR KIT" pedia
    //  75 y tenia 74 - que es exactamente el fallo que esta funcion existe
    //  para no tener.
    const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);
    constexpr int kChrome = 2 * Metrics::sm + 2 * (Metrics::halfGap / 2) + 2 * 5;
    int total = 0;
    for (int i = 0; i < juce::jlimit (1, 8, count); ++i)
        total += (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, mb[i]->getButtonText()))
               + kChrome;
    return total <= rowWidth;
}

void MainComponent::layoutModuleBar (juce::Rectangle<int> row, juce::TextButton** mb, int vInset, int count)
{
    //  Vale para cualquier fila de tapas, no solo para la barra de modulos: la
    //  fila REV / LOOP / AUTOCUT / BOMBEO tiene el mismo problema y peor, que
    //  "AUTOCUT" pide 52 px y a cuartos le tocaban 42, y el arabe de AUTO CHOP
    //  pide 85.
    const int kMods = juce::jlimit (1, 8, count);
    //  La MISMA fuente con la que drawButtonText va a dibujar la tapa. Medir
    //  con otra es como se responde "cabe" a una pregunta que no se ha hecho:
    //  ya paso una vez en este proyecto, con getTextButtonFont.
    const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);

    int need[8] {}; int total = 0;
    for (int i = 0; i < kMods; ++i)
    {
        need[i] = (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, mb[i]->getButtonText()))
                + 2 * Metrics::sm;
        total += need[i];
    }

    const int spare = juce::jmax (0, row.getWidth() - total);
    for (int i = 0; i < kMods; ++i)
    {
        const int w = need[i] + spare * need[i] / juce::jmax (1, total);
        mb[i]->setBounds ((i < kMods - 1 ? row.removeFromLeft (juce::jmax (24, w)) : row)
                              .reduced (Metrics::halfGap / 2, vInset));
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
    //  The margin to the glass is a HORIZONTAL idea: it is what stops the pad
    //  grid touching the sides. Vertically the system bars already hold the
    //  face off the clock and the gesture pill, so the same fourteen applied
    //  top and bottom - and then ten more on each - was margin stacked behind
    //  margin, and the instrument ended up floating in the middle of its own
    //  screen with dead paper above and below it.
    auto area = safeArea().reduced (ZatiLookAndFeel::kFaceMargin,
                                    ZatiLookAndFeel::kEdgeV);

    //  TWO COLUMNS WHEN THE SCREEN IS WIDER THAN IT IS TALL.
    //
    //  Every band of this face is stacked, which is the right answer on a
    //  phone held upright and a hopeless one on anything else: rotated to
    //  915x412 there are 412 pixels of height to hold a header, a screen, a
    //  module bar, transport keys, a knob plate, six effects AND four rows of
    //  pads. The face did not fail gracefully, it overflowed - the pads were
    //  laid out below the bottom of the window, on top of the transport.
    //
    //  Rotating is not an error state to survive, it is the second layout an
    //  instrument gets for free: what you WATCH and what you SET on the left,
    //  what you PLAY on the right, which is how a groovebox is arranged on a
    //  desk anyway. Same components, same code below - only the rectangle the
    //  pads are given changes.
    //
    //  The pad column is booked as tall as it is wide, because the grid inside
    //  it is square; the left column keeps a floor so the screen and the knobs
    //  never get squeezed into a strip.
    faceColumn = {};
    juce::Rectangle<int> padCol;
    wideFace = area.getWidth() >= area.getHeight() * 5 / 4 && area.getWidth() >= 560;

    if (wideFace)
    {
        const int want = juce::jlimit (220, juce::jmax (220, area.getWidth() - 320), area.getHeight());
        padCol = area.removeFromRight (want);
        area.removeFromRight (ZatiLookAndFeel::kAir * 2);
        faceColumn = area;
    }

    // The LCD grows to absorb whatever the face doesn't need (the pads are
    // width-bound squares) — the screen is the protagonist.
    int screenH;
    //  ...except for the one seam that carries controls. See the block after
    //  the budget: `padSeamExtra` is what the PADS seam borrows so the four
    //  bank chips are a finger tall with air over and under, and
    //  `padBottomGive` is the part of it that comes out of the band under the
    //  grid rather than out of the screen.
    int padSeamExtra = 0, padBottomGive = 0;
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
        //  Rotated there is width to spare and no height at all, so the five
        //  module tabs and the three transport keys share one row instead of
        //  taking two. Thirty pixels back, and the row that gets them is the
        //  effects row, which was coming out 22 tall - a key you PLAY with,
        //  squeezed so a menu could keep its own line.
        const int belowScreen = ZatiLookAndFeel::kAir
                              + (wideFace ? ZatiLookAndFeel::kTransport
                                          : ZatiLookAndFeel::kModule + Metrics::xs + ZatiLookAndFeel::kTransport)
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
        //  ...and in two columns the pads are not in this budget at all: they
        //  are in the other one, together with the seam that names them.
        const int cellW    = (area.getWidth() - 3 * ZatiLookAndFeel::kPadGap) / 4;
        const int padsNeed = wideFace ? 0 : 4 * cellW + 3 * ZatiLookAndFeel::kPadGap;
        const int bodyNeed = ZatiLookAndFeel::kCtrlPlate + ZatiLookAndFeel::kAir + Metrics::sm
                           + ZatiLookAndFeel::kFxRow + ZatiLookAndFeel::kAir
                           + padsNeed + (wideFace ? 2 : 3) * kSeamLabelH;

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
        //  The screen is the protagonist and it is also the ONLY band that may
        //  give: everything else on this column is a target a finger has to
        //  land on. Ninety-six is what it takes to read a waveform and two
        //  meters; rotated, where the panel is wide and short, the same
        //  information fits in less height and the pixels are worth more to
        //  the effects row than to the wave.
        const int kMinScreen = wideFace ? 56 : 96;

        const int freeH = area.getHeight() - aboveScreen - belowScreen - bottomStrip - bodyNeed;

        layoutAir = (freeH > kMinScreen)
                      ? juce::jlimit (0, kAirMax, (freeH - kMinScreen) / (kSeams + 2))
                      : 0;

        screenH = juce::jmax (kMinScreen, freeH - layoutAir * kSeams);

        //  AIRE PARA LOS CHIPS DE BANCO.
        //
        //  They ride in the PADS seam so they cost the face no height, and the
        //  seam is kAir + layoutAir + kSeamLabelH = 22..33. Take the eight the
        //  cap needs off that and the chips came out 22 px tall - a hair over
        //  half the forty every other target on this machine is held to, on
        //  the control that decides WHICH SIXTEEN PADS you are playing.
        //
        //  The height is there, it was just parked where nothing uses it. Two
        //  places, in this order, and neither of them is a control:
        //
        //    1. The band under the grid. It removes kAir + layoutAir - up to
        //       twenty-one pixels - purely so the pads do not sit on the status
        //       sentence, and Metrics::sm is plenty for that.
        //    2. Whatever the LCD holds over its floor. The screen is the
        //       protagonist and it is also the band that gives, which is the
        //       rule already written above; this is the same rule with one more
        //       claimant.
        //
        //  Rotated, the pad column is its own rectangle and the grid inside it
        //  is width-bound, so the seam simply takes what it wants - there is
        //  nothing below it to give and nothing above it to lose.
        //  A finger, its air over and under, AND the lip of the plate below -
        //  which is painted five pixels above the grid and is therefore five
        //  pixels of the seam that the chips cannot have.
        const int bankSeamWant = Metrics::hit + Metrics::gap + ZatiLookAndFeel::kPlateLip;   // 53
        const int padSeamHave  = ZatiLookAndFeel::kAir + (wideFace ? 0 : layoutAir)
                               + kSeamLabelH;
        int want = juce::jmax (0, bankSeamWant - padSeamHave);

        if (! wideFace)
        {
            //  Down to Metrics::xs, not Metrics::sm. Four pixels between the
            //  last pad row and the status sentence is still four pixels; the
            //  chips are the ones with nothing to spare.
            padBottomGive = juce::jmin (want, juce::jmax (0, ZatiLookAndFeel::kAir
                                                            + layoutAir - Metrics::xs));
            want -= padBottomGive;

            const int fromScreen = juce::jmin (want, juce::jmax (0, screenH - kMinScreen));
            screenH -= fromScreen;
            padSeamExtra = padBottomGive + fromScreen;
        }
        else
        {
            padSeamExtra = want;
        }
    }

    // --- Top chrome ---
    const int faceTop = area.getY();          // donde empieza la cara, para el panel XY
    headerArea = area.removeFromTop (ZatiLookAndFeel::kHeader);
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir);

    //  VU above the screen and the step strip below it, so the two readouts
    //  frame the LCD instead of sitting among the controls. Both are watched,
    //  not touched, so they belong together up here.
    screenBezel = area.removeFromTop (screenH);
    //  La barra de trabajo, al pie del cristal y por dentro: es donde la
    //  maquina ya cuenta las cosas, y asi no le quita alto a nada.
    busyArea = screenBezel.reduced (Metrics::sm, Metrics::xs)
                          .removeFromBottom (Metrics::hit - 6);
    busyBar.setBounds (busyArea);
    if (busyJobs > 0) busyBar.toFront (false);
    spectrum.setBounds (screenBezel);
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir);   // the bezel is drawn 5 px proud

    //  Six modules and three transport keys will not fit across a phone in one
    //  row: LOAD came out as "LO...". They split again, but the module bar
    //  stays slim at 32 while the transport keeps its full 44 — the original
    //  complaint was that the menu was as heavy as PLAY, and that still holds.
    //  Rotated, the two bands become one: the eight caps have the width for it
    //  and the column has no height to spare. The module tabs still read as
    //  lighter than the transport - they are narrower, not just shorter.
    if (wideFace)
    {
        auto row = area.removeFromTop (ZatiLookAndFeel::kTransport);
        tabBarArea = row;
        //  Mismo reparto proporcional que en vertical, y por la misma razon:
        //  a partes iguales entre seis, CANCION pedia 36 px y tenia 32 en el
        //  unico sitio donde la barra comparte fila con el transporte.
        auto tabs = row.removeFromLeft (row.getWidth() * 6 / 10);
        juce::TextButton* mb[6] = { &padsButton, &secButton, &songButton, &mixButton, &xyButton, &setButton };
        layoutModuleBar (tabs, mb, ZatiLookAndFeel::kAir / 2);

        const int u = row.getWidth() / 3;
        loadButton.setBounds (row.removeFromLeft (u).reduced (Metrics::halfGap, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (Metrics::halfGap, 0));
        playButton.setBounds (row.reduced (Metrics::halfGap, 0));
    }
    else
    {
    tabBarArea = area.removeFromTop (ZatiLookAndFeel::kModule);
    {
        auto row = tabBarArea;
        juce::TextButton* mb[6] = { &padsButton, &secButton, &songButton, &mixButton, &xyButton, &setButton };
        layoutModuleBar (row, mb, 0);
    }
    area.removeFromTop (Metrics::xs);

    {
        auto row = area.removeFromTop (ZatiLookAndFeel::kTransport);
        const int u = row.getWidth() / 4;
        loadButton.setBounds (row.removeFromLeft (u).reduced (Metrics::halfGap, 0));
        recButton.setBounds  (row.removeFromLeft (u).reduced (Metrics::halfGap, 0));
        playButton.setBounds (row.reduced (Metrics::halfGap, 0));
    }
    }
    ctrlSeamTop = area.getY();
    area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH);   // CONTROL rides here

    // Status pinned to the bottom; DESHACER sits on its right when armed, so
    // an undoable action announces itself where the result was reported.
    {
        auto strip = area.removeFromBottom (ZatiLookAndFeel::kStatus);
        //  ...and the pads do not sit on the sentence. Metrics::sm is the floor
        //  that guarantees it; anything this band was holding above that floor
        //  has gone to the PADS seam, where four controls were living in 22 px.
        area.removeFromBottom (juce::jmax (Metrics::xs,
                                           ZatiLookAndFeel::kAir + layoutAir - padBottomGive));
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
                fxButtons[f]->setBounds ((f < kNumFx - 1 ? row.removeFromLeft (sw) : row).reduced (Metrics::halfGap, 0));
        }
        //  In two columns the pads have a column of their own and the seam
        //  above them is simply the room the square grid does not use, so the
        //  engraving lands there without anything being reserved for it.
        //  THE BANK CHIPS, MEASURED LIKE EVERYTHING ELSE.
        //
        //  They ride in the seam the engraved PADS already occupies, so they
        //  cost the face no height - but riding somewhere is not the same as
        //  being squeezed into it. They take the seam's full height less its
        //  air, they are `Metrics::gap` apart like every other row on this
        //  machine, and the engraved rule is told to stop before them instead
        //  of running underneath.
        //
        //  TWO AND TWO, one pair at each end of the seam. All four pinned to
        //  the trailing end left the word floating in a wide empty half with a
        //  clump of controls jammed against one edge - the seam read as
        //  lopsided at every one of the seven test sizes. Split down the
        //  middle, the engraved PADS sits exactly between the pairs and the
        //  rule breaks symmetrically on both sides of it.
        auto placeBanks = [this] (juce::Rectangle<int> seam)
        {
            //  THE BOTTOM OF A CAP IS ITS SHADOW, and the plate's lip is drawn
            //  five pixels above the grid. Centring the chip in the whole seam
            //  therefore centred it against a floor that is not where the face
            //  visibly ends: eight pixels of air over the caps, two under the
            //  shadows, and the shadow of every chip resting on the plate edge.
            //  Take the lip off the seam FIRST and then centre in what is left,
            //  so the air above the cap and the air below the shadow are the
            //  same number and that number is Metrics::halfGap.
            seam = seam.withTrimmedBottom (ZatiLookAndFeel::kPlateLip);

            //  Ceiling at Metrics::hit, not Metrics::tab. The seam is now given
            //  the height for a finger (see padSeamExtra), and a 32 px ceiling
            //  would have taken the extra and thrown eight of it away.
            //
            //  ...and a final clamp to the seam itself, because jlimit clamps UP
            //  as readily as down and that is how this exact bug is written
            //  three times over in this file's history. On a 360x640 the seam
            //  is worth nineteen usable pixels and the floor of twenty-two put
            //  a chip THREE PIXELS TALLER THAN ITS OWN SEAM, centred, so it
            //  overhung the effects row above and the plate lip below by one
            //  and a half each. A missing pixel comes out of the chip, never
            //  out of the section next to it.
            const int room    = seam.getHeight() - Metrics::gap;
            const int h       = juce::jmin (seam.getHeight(),
                                            juce::jmin (Metrics::hit, juce::jmax (20, room)));
            const int perSide = kNumBanks / 2;
            //  Half the seam belongs to the word; each pair gets one of the
            //  remaining quarters, so a pair plus its air can never grow into
            //  the room PADS needs however wide the screen is.
            const int w = juce::jlimit (30, 46,
                                        (seam.getWidth() / 4 - (perSide - 1) * Metrics::gap) / perSide);
            const int total = perSide * w + (perSide - 1) * Metrics::gap;

            //  takeStart/takeEnd, not removeFromLeft/Right: in Arabic the pair
            //  that reads first has to be the one on the right, or A B C D runs
            //  backwards across a face whose every other row was mirrored.
            auto lead  = Lang::takeStart (seam, total).withSizeKeepingCentre (total, h);
            auto trail = Lang::takeEnd   (seam, total).withSizeKeepingCentre (total, h);
            bankRowLeftArea  = lead;
            bankRowRightArea = trail;

            for (int b = 0; b < bankButtons.size(); ++b)
            {
                auto& row = (b < perSide ? lead : trail);
                bankButtons[b]->setBounds (Lang::takeStart (row, w));
                if (b % perSide < perSide - 1) Lang::takeStart (row, Metrics::gap);
            }
        };

        //  ...plus whatever the budget managed to borrow for the chips. The
        //  engraved word stays centred in the seam whatever it grows to, so the
        //  extra reads as air around the controls and not as a gap in the face.
        //  Lo que el panel XY puede ocupar: desde donde empieza la cara hasta
        //  donde empieza la costura de PADS, y en dos columnas solo la columna
        //  izquierda - la de los pads es de los pads.
        faceTopArea = wideFace ? faceColumn
                               : juce::Rectangle<int> (area.getX(), faceTop,
                                                       area.getWidth(),
                                                       juce::jmax (0, area.getY() - faceTop));

        if (wideFace)
        {
            padSeamTop = padCol.getY();
            auto seam = padCol.removeFromTop (ZatiLookAndFeel::kAir + kSeamLabelH + padSeamExtra);
            placeBanks (seam);
            layoutPadGrid (padCol, 4, 4, ZatiLookAndFeel::kPadGap);
        }
        else
        {
            padSeamTop = area.getY();
            auto seam = area.removeFromTop (ZatiLookAndFeel::kAir + layoutAir + kSeamLabelH + padSeamExtra);
            placeBanks (seam);
            layoutPadGrid (area, 4, 4, ZatiLookAndFeel::kPadGap);
        }
    }

    // --- Floating sheets (each sized by its own content, capped at 86%) ---
    const auto full = safeArea();
    //  Centred, not risen from the bottom. A bottom sheet at 86% buried the pad
    //  grid exactly while you were editing a pad — you lost sight of the thing
    //  you were adjusting. Centred at 78% x 92% the instrument stays visible
    //  behind the scrim and the window reads as temporary.
    //  The sheet covers the WHOLE window, not just the safe area.
    //
    //  Every rectangle below - the card and each control in it - is worked out
    //  in MainComponent coordinates from `full`, and then handed to children
    //  of the sheet. That only lines up if the sheet's own origin is (0,0):
    //  setBounds(full) put it at the system inset instead, so on Android 15
    //  the whole card and its contents were displaced downward by the height
    //  of the status bar, and sideways by the left inset in landscape. On a
    //  desktop, where the insets are zero, it was invisible.
    //
    //  Covering everything is also the better scrim: a dimmed sheet that stops
    //  short of the status bar reads as a panel with a gap behind it.
    auto sheetFromBottom = [&full, this] (Sheet& s, int desiredH)
    {
        s.setBounds (getLocalBounds());
        const int h = juce::jmin (desiredH, (int) (full.getHeight() * 0.78f));
        const int w = (int) (full.getWidth() * 0.92f);
        auto sheet = juce::Rectangle<int> (0, 0, w, h).withCentre (full.getCentre());
        s.sheetBounds = sheet;
        return sheet.reduced (Metrics::lg, Metrics::md);
    };
    auto placeKnobRow = [] (juce::Rectangle<int> row, juce::Slider** ks, int n = 3)
    {
        const int w = row.getWidth() / juce::jmax (1, n);
        for (int i = 0; i < n; ++i)
        {
            auto cell = (i < n - 1 ? row.removeFromLeft (w) : row);
            cell.removeFromTop (ZatiLookAndFeel::kKnobName);   // gap for knob name
            ks[i]->setBounds (cell.reduced (6, 2));
        }
    };

    // PADS sheet, dos paginas. Ver PadPage en la cabecera: SONIDO es lo que
    // suena el pad y EL PAD es lo que el pad es. Cada pagina pide la altura que
    // va a usar, asi que la ficha encoge cuando lo de dentro ocupa menos - la
    // de EL PAD no arrastra el hueco de la onda, que no lleva.
    {
        constexpr int secH = 15 + 2 * ZatiLookAndFeel::kTextPad;
        const bool onSound = (padPage == padPageSound);
        //  644 y 418 salen de sumar lo que lleva cada pagina, no de probar:
        //  ver el desglose de cada bloque mas abajo.
        const int sheetInnerW = (int) (full.getWidth() * 0.92f) - 2 * Metrics::lg;
        //  Lo que pide cada pagina, sumado y no probado:
        //  SONIDO  = titulo+pestanas+margenes (116) + secH + 86 + 86 + 86 + 56 + 8
        //  RECORTE = 116 + secH + 34+4+34+8 + hit + 8 + 180 de onda
        //  EL PAD  = 116 + 3*secH + 2*86 + 2*hit + 3*sm + chip
        //  sheetFromBottom recorta si no cabe, y de eso se ocupa el reparto.
        const int rigH = 418 + 3 * secH
                       + (padSourceWraps (sheetInnerW) ? Metrics::hit + Metrics::halfGap : 0);
        //  438 y no 352: la fila del filtro son 86 mas. Ver el desglose.
        const int wantH = (padPage == padPageSound) ? 438 + secH
                        : (padPage == padPageTrim)  ? 424 + secH
                                                    : rigH;
        auto inner = sheetFromBottom (padSheet, wantH);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        padCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        Lang::takeEnd (titleRow, Metrics::xs);
        previewButton.setBounds (Lang::takeEnd (titleRow, 68).reduced (0, 2));

        //  Las pestanas, debajo del titulo y en las dos paginas.
        inner.removeFromTop (Metrics::xs);
        {
            auto tabRow = inner.removeFromTop (Metrics::hit);
            juce::TextButton* tb[3] = { &padSoundBtn, &padTrimBtn, &padRigBtn };
            layoutModuleBar (tabRow, tb, 0, 3);
        }
        inner.removeFromTop (Metrics::sm);

        if (padPage == padPageRig)
        {
            //  EL PAD: 3*secH + 86*2 (envios) + 8 + 40 (corte) + 8 + 40
            //  (fuente) + 8 + chip + margenes = 418 + 3*secH.
            //  ¿HAY SITIO PARA LA PAGINA ENTERA?
            //
            //  En apaisado - 915x412 - la ficha no puede pasar del 78% de 412,
            //  que son 321, y esta pagina pide 481. Lo que se sale por abajo no
            //  desaparece: se queda con altura CERO, y un boton de altura cero
            //  se ve en el volcado como un control de 265x0. Asi que la pagina
            //  se mide contra el hueco que le han dado y, si no cabe, los seis
            //  envios pasan a una sola fila y las muestras de color - que son
            //  una etiqueta y no un ajuste - se quedan fuera.
            const int srcH = Metrics::hit
                           + (padSourceWraps (inner.getWidth()) ? Metrics::hit + Metrics::halfGap : 0);
            const int needFull = secH + 2 * 86 + Metrics::sm
                               + secH + Metrics::hit + Metrics::sm
                               + secH + srcH + Metrics::sm + Metrics::chip;
            //  APRETADO NO ES LO MISMO QUE ESTRECHO, y confundirlos costo una
            //  medida: en 280x653 la pagina tampoco cabe de alto, se fue por
            //  la rama de la fila unica, y cinco tapas en 225 px son 45 px
            //  cada una - AUTO CHOP, GRABAR MIC y AUTOCUT truncados los tres.
            //  Fundir las dos secciones solo sirve cuando lo que sobra es
            //  ANCHO, que es lo que pasa en apaisado y no en un movil de pie.
            const bool tight = inner.getHeight() < needFull;
            const bool merge = tight && padRowFits (inner.getWidth(),
                                                    { &autocutButton, &duckButton, &chopButton,
                                                      &micButton, &resampleButton });

            padRigTight = merge;
            padSectionArea[0] = inner.removeFromTop (secH);   // pintado: ENVIOS
            {
                juce::Slider* e[6] = { padSends[0], padSends[1], padSends[2],
                                       padSends[3], padSends[4], padSends[5] };
                //  Seis en una fila solo si a cada uno le tocan 40 px, que es
                //  el dedo minimo. En 280x653 seis mandos en 225 px son 25 px
                //  cada uno y su numero se queda en 21: el banco lo saco como
                //  "100" pidiendo 22. Ahi la fila unica ahorra 86 px de alto
                //  que no hacian falta, porque los 86 caben.
                if (tight && inner.getWidth() / kNumFx >= Metrics::hit)
                {
                    placeKnobRow (inner.removeFromTop (ZatiLookAndFeel::kKnobRow), e, 6);
                }
                else
                {
                    placeKnobRow (inner.removeFromTop (ZatiLookAndFeel::kKnobRow), e,     3);
                    placeKnobRow (inner.removeFromTop (ZatiLookAndFeel::kKnobRow), e + 3, 3);
                }
            }
            inner.removeFromTop (Metrics::sm);

            if (merge)
            {
                //  Apaisado: CORTE y FUENTE se funden en una sola seccion de
                //  cinco tapas. La ficha mide 841 px de ancho ahi y solo 321
                //  de alto, asi que lo que sobra es exactamente lo que a lo
                //  otro le falta - y dos titulos de seccion con sus dos filas
                //  cuestan 138 px de alto para decir lo mismo que una.
                padSectionArea[1] = inner.removeFromTop (secH);   // pintado: EL PAD
                padSectionArea[2] = {};
                auto rr = inner.removeFromTop (Metrics::hit);
                juce::TextButton* pb[5] = { &autocutButton, &duckButton,
                                            &chopButton, &micButton, &resampleButton };
                layoutModuleBar (rr, pb, 0, 5);
            }
            else
            {
                padSectionArea[1] = inner.removeFromTop (secH);   // pintado: CORTE
                {
                    auto rr = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* pb[2] = { &autocutButton, &duckButton };
                    layoutModuleBar (rr, pb, 0, 2);
                }
                inner.removeFromTop (Metrics::sm);

                padSectionArea[2] = inner.removeFromTop (secH);   // pintado: FUENTE
                //  Tres formas de poner un sonido en un pad: cortar uno que ya
                //  tienes, grabar la sala, o imprimir lo que la maquina esta
                //  tocando. En una fila cuando caben las tres palabras y en dos
                //  cuando no: en 280x653 son 225 px para AUTO CHOP, GRABAR MIC
                //  y REMUESTREAR, que piden 272 entre las tres. Antes esta fila
                //  se salia por abajo de la ficha y por eso el banco no la veia.
                if (padSourceWraps (inner.getWidth()))
                {
                    auto ra = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* p2[2] = { &chopButton, &micButton };
                    layoutModuleBar (ra, p2, 0, 2);
                    inner.removeFromTop (Metrics::halfGap);
                    auto rb = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* p1[1] = { &resampleButton };
                    layoutModuleBar (rb, p1, 0, 1);
                }
                else
                {
                    auto rr = inner.removeFromTop (Metrics::hit);
                    juce::TextButton* pb[3] = { &chopButton, &micButton, &resampleButton };
                    layoutModuleBar (rr, pb, 0, 3);
                }
            }
            inner.removeFromTop (Metrics::sm);

            //  El color es una ETIQUETA, no diseno de sonido, y llego a ser lo
            //  mas llamativo de la ficha: una fila entera de 44 px con dos
            //  teclas y una barra de color saturado gritando por encima de
            //  PITCH. Son ocho muestras en una tira de altura de chip - y son
            //  lo primero que se cae cuando no hay hueco, por ser lo unico de
            //  esta pagina que no cambia como suena nada.
            zatiSwatchArea = (inner.getHeight() < Metrics::chip)
                                 ? juce::Rectangle<int>()
                                 : inner.removeFromTop (Metrics::chip).reduced (4, 0);
            editInfoArea = {};
        }
        else if (padPage == padPageSound)
        {

        padSectionArea[0] = inner.removeFromTop (secH);   // painted: SONIDO

        //  Los 86 son el alto comodo de un mando con su nombre y su numero, y
        //  no son un derecho: en apaisado la ficha se queda en 321 px y esta
        //  pagina pide 373, asi que las dos filas de mandos se apretaban hasta
        //  que la tercera - CHOKE, MODO, NORMALIZAR - se salia por abajo con
        //  altura cero. Sale de lo que hay, con 60 de suelo.
        {
            const int forKnobs = inner.getHeight() - (ZatiLookAndFeel::kKnobName + Metrics::hit);
            const int knobH = juce::jlimit (60, ZatiLookAndFeel::kKnobRow, forKnobs / 3);
            juce::Slider* k1[3] = { &pitchSlider, &fineSlider, &volSlider };
            juce::Slider* k2[3] = { &panSlider, &attackSlider, &releaseSlider };
            //  El filtro son DOS y no tres: media fila vacia se lee como un
            //  mando que falta. Dos celdas anchas, que ademas es lo que pide
            //  un corte - es el mando que mas se arrastra de la ficha.
            juce::Slider* k3[2] = { &cutSlider, &resoSlider };
            placeKnobRow (inner.removeFromTop (knobH), k1);
            placeKnobRow (inner.removeFromTop (knobH), k2);
            placeKnobRow (inner.removeFromTop (knobH), k3, 2);
        }

        //  A third row for the two controls that are not dials: CHOKE, which
        //  is a pair of increment buttons, and the tape/tone switch. Giving
        //  them a knob-sized cell was what turned CHOKE into two tall slabs
        //  that swallowed their column.
        {
            auto r3 = inner.removeFromTop (ZatiLookAndFeel::kKnobName + Metrics::hit);
            r3.removeFromTop (ZatiLookAndFeel::kKnobName);   // gap for the names
            //  Tres celdas, no tres tercios. NORMALIZAR es la palabra mas
            //  larga de la ficha y en 280x653 pedia 66 px de un tercio que
            //  daba 55: el banco lo saco como TRUNC en cuanto entro el boton.
            //  Repartirlo a mano fue perseguirse la cola - 42 dejaba
            //  NORMALIZAR dos pixeles corto y 44 truncaba el MODO arabe, que
            //  es mas ancho que el castellano. CHOKE se queda con su tercio
            //  escaso, que es lo que piden sus dos teclas, y los otros dos se
            //  reparten POR EL TEXTO QUE LLEVAN, que es lo mismo que hacen
            //  las barras de modulos y lo unico que se ajusta solo en cuatro
            //  idiomas.
            const int w3 = r3.getWidth() * 32 / 100;
            //  Sin recorte vertical: la fila mide Metrics::hit justo, que es
            //  el dedo minimo, y quitarle 3 arriba y 3 abajo dejaba tres
            //  controles de 34 px que el banco saca como TOUCH. Encima hay 16
            //  px de rotulo y debajo Metrics::sm, asi que a 40 no toca nada.
            auto chokeCell = r3.removeFromLeft (w3).reduced (6, 0);
            //  JUCE stacks a slider's +/- buttons whenever the space left for
            //  them is taller than it is wide, and on a narrow screen the
            //  readout was eating enough of the cell to trigger exactly that -
            //  two 17-pixel slivers. Reserve the buttons their width first.
            //  70 was a hand-picked number that made CHOKE's keys a different
            //  size from every other stepper's. Same reservation as the rest.
            chokeSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                         juce::jmax (30, chokeCell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                         Metrics::readout);
            chokeSlider.setBounds (chokeCell);
            //  NORMALIZAR va aqui y no en la fila de REV/LOOP porque
            //  pertenece al nivel, y el nivel es esta seccion.
            juce::TextButton* r3b[2] = { &modeButton, &normButton };
            layoutModuleBar (r3, r3b, 0, 2);
        }

        padSectionArea[1] = {};

        padSectionArea[2] = {};
        zatiSwatchArea = {};
        editInfoArea = {};
        }
        else
        {
        //  RECORTE: la regla, la onda y las tres cosas que se le hacen a la
        //  muestra que se esta mirando.
        padSectionArea[0] = inner.removeFromTop (secH);   // pintado: RECORTE
        padSectionArea[1] = {};
        padSectionArea[2] = {};

        const int labelW = ZatiLookAndFeel::kTrimLabel;
        auto ctrlRow = [&inner, labelW] (int h) { auto r = inner.removeFromTop (h); r.removeFromLeft (labelW); return r; };
        startSlider.setBounds (ctrlRow (ZatiLookAndFeel::kTrimRow)); inner.removeFromTop (Metrics::xs);
        endSlider.setBounds   (ctrlRow (ZatiLookAndFeel::kTrimRow)); inner.removeFromTop (Metrics::sm);

        //  REV y LOOP viven aqui, con el recorte, y no en la barra de EL PAD:
        //  las dos deciden COMO SE RECORRE el trozo que se acaba de marcar,
        //  igual que START y END deciden cual es. Estaban al lado de AUTOCUT
        //  y BOMBEO, que son cosas del pad y no de la muestra. QUITAR RUIDO va
        //  con ellas por lo mismo: es de la muestra.
        {
            auto rr = inner.removeFromTop (Metrics::hit);
            juce::TextButton* pb[3] = { &reverseButton, &loopButton, &denoiseButton };
            layoutModuleBar (rr, pb, 0, 3);
        }
        inner.removeFromTop (Metrics::sm);
        //  Las muestras de color son de la otra pagina. Sin borrarlo, la tira
        //  se seguia pintando aqui - en las coordenadas donde estaba EN LA
        //  OTRA PAGINA -, que es el fallo clasico de un area guardada en un
        //  miembro y no vuelta a calcular.
        zatiSwatchArea = {};

        //  The cut itself, with its fragments and its draggable trim handles.
        //  It used to be a painted, untouchable card here while the real one
        //  lived on the face; now the interactive one is where the editing is.
        editInfoArea = inner;
        waveform.setBounds (inner);

        //  Las tres tapas del zoom, ENCIMA de la onda y pegadas a su esquina
        //  de abajo a la derecha, que es la unica parte de la pantalla donde
        //  no hay ni rotulo ni asa. Se colocan en coordenadas de la ficha
        //  porque son hermanas de la onda, no hijas suyas: hijas, un arrastre
        //  sobre ellas seria un arrastre sobre la onda.
        {
            //  Solo si queda pantalla debajo de ellas. En apaisado la ficha no
            //  puede pasar de 321 px y a la onda le quedan 56: tres tapas de
            //  40 encima de 56 no son un zoom, son una barra tapando lo unico
            //  que se estaba mirando - y colocadas donde no caben, salen a
            //  altura cero, que es un control que no se puede pulsar.
            const bool room = inner.getHeight() >= 2 * Metrics::hit;
            juce::TextButton* zb[3] = { &zoomOutButton, &zoomFitButton, &zoomInButton };
            for (auto* b : zb) b->setVisible (room);

            if (room)
            {
                auto strip = inner.removeFromBottom (Metrics::hit);
                strip = Lang::takeEnd (strip, juce::jmin (3 * Metrics::hit + 2 * Metrics::halfGap,
                                                          strip.getWidth()))
                            .withTrimmedBottom (Metrics::halfGap);
                const int w = juce::jmax (24, (strip.getWidth() - 2 * Metrics::halfGap) / 3);
                for (int i = 0; i < 3; ++i)
                {
                    zb[i]->setBounds (Lang::takeStart (strip, w));
                    if (i < 2) Lang::takeStart (strip, Metrics::halfGap);
                }
            }
        }
        }
    }

    // La ficha del MANUAL: titulo, subtitulo y todo lo demas es la lista, que
    // se desplaza. Pide de alto lo que le den - hasta el tope del 78% - porque
    // aqui cuanta mas se lea de una vez, mejor.
    {
        auto inner = sheetFromBottom (manualSheet, 1200);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        manualCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                        .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (14 + Metrics::sm);   // pintado: el subtitulo

        manualScroll.setBounds (inner);
        const int barW = manualScroll.getScrollBarThickness();
        manualBody.setSize (juce::jmax (40, inner.getWidth() - barW),
                            juce::jmax (inner.getHeight(),
                                        manualContentHeight (inner.getWidth() - barW)));
    }

    // BROWSE sheet: the tallest of them all — the file list wants the room.
    {
        auto inner = sheetFromBottom (browseSheet, full.getHeight());   // clamps to the 86% cap
        auto titleRow = inner.removeFromTop (Metrics::hit);
        browseCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        //  CUATRO ACCIONES NO CABEN EN UNA FILA ESTRECHA, igual que las cuatro
        //  pestanas de AJUSTES. Eran tres y entraban; FABRICA las puso en
        //  cuatro y el banco lo canto: en 280x653 "CARGAR KIT" pide 75 px y la
        //  tapa le dejaba 61, y con ella se apretaban LOAD KIT, FACTORY y
        //  تحميل. layoutModuleBar reparte proporcionalmente pero no se NIEGA
        //  cuando no hay sitio: aprieta y sigue.
        //
        //  Se pregunta con la misma cuenta que hace el reparto - misma fuente,
        //  mismo margen - y si no caben, dos filas de dos.
        juce::TextButton* pb[4] = { &browseLoadButton, &browseKitButton,
                                    &browseFactoryButton, &browseSystemButton };
        const bool actionsFit = moduleBarFits (inner.getWidth(), pb, 4);

        if (actionsFit)
        {
            auto actions = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (actions, pb, 0, 4);
        }
        else
        {
            auto lower = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (lower, pb + 2, 0, 2);
            inner.removeFromBottom (Metrics::xs);
            auto upper = inner.removeFromBottom (Metrics::btn);
            layoutModuleBar (upper, pb, 0, 2);
        }
        inner.removeFromBottom (8);
        if (browser != nullptr) browser->setBounds (inner);
    }

    // PROJECT sheet: list of saved projects + the four actions.
    {
        //  Height follows the list, instead of claiming 70% of the screen and
        //  leaving whatever the projects did not fill as a white hole. With no
        //  projects saved that hole was most of the card, which reads as
        //  something failing to load rather than as an empty list.
        //  ONE card, whichever page is showing. Its height is the height of
        //  that page: a settings card that stayed as tall as its tallest page
        //  would open with a hole in it half the time.
        const bool onAudio = (setPage == pageAudio);
        const bool onProj  = (setPage == pageProjects);
        const bool onGest  = (setPage == pageGestures);
        const bool onMidi  = (setPage == pageMidi);

        const int listRowH = juce::jmax (22, projList.getRowHeight());
        const int listH    = juce::jlimit (1, 8, projModel.names.size()) * listRowH;

        //  Y la altura de la tarjeta cuenta las DOS filas cuando hacen falta,
        //  o la ficha se queda corta y lo que se sale es lo que se maqueta al
        //  final. Se pregunta con el ancho que va a tener el interior.
        const int setInnerW = juce::jmax (1, setSheet.getWidth() - Metrics::md * 2);
        const bool tabsFitH = setTabsFit (setInnerW);
        const int tabsH = (tabsFitH ? Metrics::tab : Metrics::tab * 2 + Metrics::xs) + Metrics::sm;
        //  The gestures page is a printed list: one row per gesture, and the
        //  card is exactly as tall as the list is. See paintGesturesPage.
        const int gestRowH = 30;
        //  La pagina de MIDI: dos bloques de rotulo + tapa + selector, y el
        //  texto que explica la nota de cada pad.
        const int midiH = Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH
                            + (14 + Metrics::hit + Metrics::xs + Metrics::hit + Metrics::sm) * 2
                            + 40 + Metrics::sm;
        const int wanted = onMidi ? midiH
            : onAudio
            ? Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH + 158 + Metrics::xs
                + (Metrics::hit + Metrics::xs) * 4 + Metrics::sm
            : onGest
              ? Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH
                  + kNumGestures * gestRowH + Metrics::sm
              : Metrics::md * 2 + Metrics::hit + 14 + Metrics::sm + tabsH
                  + Metrics::hit + 14 + Metrics::sm
                  + Metrics::btn * 2 + Metrics::xs * 2 + 8 + listH + Metrics::sm;

        auto inner = sheetFromBottom (setSheet, wanted);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        setCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        if (onAudio)
        {
            testButton.setBounds    (Lang::takeEnd (titleRow, 56).reduced (2));
            measureButton.setBounds (Lang::takeEnd (titleRow, 64).reduced (2));
            Lang::takeEnd (titleRow, Metrics::xs);
            //  Setenta y dos no bastaban: "QUANTISE" pide 56 px de letra y la tapa
            //  le dejaba 49 en la pantalla mas estrecha del banco.
            quantButton.setBounds   (Lang::takeEnd (titleRow, juce::jmax (84, titleRow.getWidth() / 3)).reduced (2));
        }
        if (onProj) inner.removeFromTop (14);         // painted: which project is open
        inner.removeFromTop (Metrics::sm);

        //  The tab row, directly under the title on both pages so it does not
        //  move when you switch.
        {
            //  CUATRO PESTANAS NO CABEN EN UNA FILA ESTRECHA.
            //
            //  Eran tres y entraban; la de MIDI las puso en cuatro y el banco
            //  lo canto en la corrida siguiente: en 280x653 "PROYECTOS" pide 56
            //  px de letra y la tapa le dejaba 42, y con ella se recortaban
            //  tambien GESTOS, PROJECTS y المشاريع. Dieciseis rotulos cortados
            //  por una pestana nueva.
            //
            //  No se arregla acortando los rotulos - "PROYS" no es una palabra -
            //  sino preguntando si caben, que es lo que padRowFits ya hacia para
            //  la ficha del pad. Si no caben, dos filas de dos: la tarjeta crece
            //  32 px en el movil mas estrecho que existe y en todos los demas se
            //  queda como estaba.
            const bool tabsFit = setTabsFit (inner.getWidth());
            auto layTwo = [] (juce::Rectangle<int> row, juce::TextButton& a, juce::TextButton& b)
            {
                const int half = row.getWidth() / 2;
                a.setBounds (Lang::takeStart (row, half).reduced (Metrics::halfGap, 0));
                b.setBounds (row.reduced (Metrics::halfGap, 0));
            };

            if (tabsFit)
            {
                auto tabs = inner.removeFromTop (Metrics::tab);
                const int quarter = tabs.getWidth() / 4;
                pageAudioBtn.setBounds (Lang::takeStart (tabs, quarter).reduced (Metrics::halfGap, 0));
                pageMidiBtn.setBounds  (Lang::takeStart (tabs, quarter).reduced (Metrics::halfGap, 0));
                pageProjBtn.setBounds  (Lang::takeStart (tabs, quarter).reduced (Metrics::halfGap, 0));
                pageGestBtn.setBounds  (tabs.reduced (Metrics::halfGap, 0));
            }
            else
            {
                layTwo (inner.removeFromTop (Metrics::tab), pageAudioBtn, pageMidiBtn);
                inner.removeFromTop (Metrics::xs);
                layTwo (inner.removeFromTop (Metrics::tab), pageProjBtn, pageGestBtn);
            }
            inner.removeFromTop (Metrics::sm);

            //  Whatever is left of the card belongs to the gestures list.
            if (onGest)
            {
                //  El boton del manual, al pie de la pagina de gestos: los
                //  gestos son la mitad de las preguntas y el manual es la otra
                //  mitad, asi que estan en el mismo sitio.
                manualButton.setVisible (true);
                manualButton.setBounds (inner.removeFromBottom (Metrics::hit)
                                             .reduced (Metrics::halfGap, 2));
                inner.removeFromBottom (Metrics::sm);
                gesturesArea = inner;
            }
            else
            {
                manualButton.setVisible (false);
                gesturesArea = {};
            }
        }

        if (onMidi)
        {
            auto block = [&inner] (juce::TextButton& btn, juce::ComboBox& box)
            {
                inner.removeFromTop (14);                       // pintado: el rotulo
                auto row = inner.removeFromTop (Metrics::hit);
                btn.setBounds (Lang::takeStart (row, juce::jmax (96, row.getWidth() / 3)).reduced (1, 2));
                inner.removeFromTop (Metrics::xs);
                box.setBounds (inner.removeFromTop (Metrics::hit).reduced (1, 2));
                inner.removeFromTop (Metrics::sm);
            };
            block (midiOutBtn, midiOutBox);
            block (midiInBtn,  midiInBox);
            midiArea = inner.removeFromTop (40);                // pintado: la nota
            audioInfoArea = bufRowArea = rateRowArea = langRowArea = skinRowArea = {};
            projNameRowArea = projPathRowArea = {};
        }
        else if (onAudio)
        {
            midiArea = {};
            audioInfoArea = inner.removeFromTop (158);
            inner.removeFromTop (Metrics::xs);

            auto chipRow = [&inner] (juce::OwnedArray<juce::TextButton>& btns, int labelW)
            {
                auto row = inner.removeFromTop (Metrics::hit);
                auto r = row;
                Lang::takeStart (r, labelW);
                const int n = juce::jmax (1, btns.size());
                const int w = r.getWidth() / n;
                for (int i = 0; i < btns.size(); ++i)
                    btns[i]->setBounds ((i < n - 1 ? Lang::takeStart (r, w) : r).reduced (1, 2));
                inner.removeFromTop (Metrics::xs);
                return row;
            };
            bufRowArea  = chipRow (bufButtons, 44);
            rateRowArea = chipRow (rateButtons, 44);
            langRowArea = chipRow (langButtons, 44);
            skinRowArea = chipRow (skinButtons, 44);
            projNameRowArea = projPathRowArea = {};
        }
        else
        {
            midiArea = {};
            skinRowArea = {};
            projNameRowArea = inner.removeFromTop (Metrics::hit);
            {
                auto r = projNameRowArea;
                Lang::takeStart (r, 60);
                projNameBox.setBounds (r.reduced (2, 4));
            }
            projPathRowArea = inner.removeFromTop (14);
            inner.removeFromTop (Metrics::sm);

            projExportButton.setBounds (inner.removeFromBottom (Metrics::btn).reduced (2, 0));
            inner.removeFromBottom (Metrics::xs);

            auto actions = inner.removeFromBottom (Metrics::btn);
            const int aw = actions.getWidth() / 4;
            projSaveButton.setBounds   (actions.removeFromLeft (aw).reduced (Metrics::halfGap, 0));
            projLoadButton.setBounds   (actions.removeFromLeft (aw).reduced (Metrics::halfGap, 0));
            projNewButton.setBounds    (actions.removeFromLeft (aw).reduced (Metrics::halfGap, 0));
            projDeleteButton.setBounds (actions.reduced (Metrics::halfGap, 0));
            inner.removeFromBottom (8);

            projList.setBounds (inner);
            bufRowArea = rateRowArea = langRowArea = audioInfoArea = {};
        }
    }

    // EXPORT sheet: what will be rendered, then the two products.
    {
        auto inner = sheetFromBottom (exportSheet, 32 + 96 + Metrics::btn * 2 + Metrics::sm * 2);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        exportCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        inner.removeFromTop (96);   // painted: source, length, destination, status

        auto row = inner.removeFromBottom (Metrics::btn);
        exportCancelButton.setBounds (row);
        const int hw = row.getWidth() / 2;
        exportMasterButton.setBounds (row.removeFromLeft (hw).reduced (Metrics::halfGap, 0));
        exportStemsButton.setBounds  (row.reduced (Metrics::halfGap, 0));
    }

    // RACK sheet: which pad, and how much of it reaches each effect.
    {
        const int chipRowH = Metrics::hit;
        //  sheetFromBottom takes the card's OUTER height and hands back the
        //  inside, so the vertical margin it removes has to be part of what we
        //  ask for - without it the last send row fell off the bottom edge.
        auto inner = sheetFromBottom (rackSheet, Metrics::md * 2 + Metrics::hit + 14
                                                   + (chipRowH + Metrics::xs) * 2
                                                   + Metrics::sm + kNumFx * 48 + Metrics::sm);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        rackCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (14);                       // painted: which pad this is

        //  The rack picks a pad out of the bank on screen, two rows of eight.
        //  The other forty-eight chips are hidden rather than laid out: the
        //  rack is "which of THESE sixteen am I sending", not a directory.
        for (auto* b : rackPadBtns) if (b != nullptr) b->setVisible (false);

        for (int r = 0; r < 2; ++r)
        {
            auto row = inner.removeFromTop (chipRowH);
            const int w = row.getWidth() / 8;
            for (int c = 0; c < 8; ++c)
            {
                const int i = currentBank * kPadsPerBank + r * 8 + c;
                rackPadBtns[i]->setVisible (true);
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
        //  Una fila mas que antes: la de COMO se corta, encima de la de en
        //  cuantos trozos, porque el modo cambia lo que significa el numero.
        auto inner = sheetFromBottom (chopSheet, Metrics::md * 2 + Metrics::hit + explainH
                                                   + Metrics::md + 14 + Metrics::hit
                                                   + Metrics::md + 14 + Metrics::hit
                                                   + Metrics::sm + Metrics::hit
                                                   + Metrics::md + plannedH
                                                   + Metrics::sm + Metrics::btn);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        chopCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

        inner.removeFromTop (explainH);                 // painted: what this does
        inner.removeFromTop (Metrics::md);
        inner.removeFromTop (14);                       // pintado: "COMO"
        {
            auto row = inner.removeFromTop (Metrics::hit);
            juce::TextButton* mb[2] = { &chopEvenBtn, &chopHitsBtn };
            layoutModuleBar (row, mb, 0, 2);
        }
        inner.removeFromTop (Metrics::md);
        inner.removeFromTop (14);                       // pintado: "TROZOS"

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
        auto inner = sheetFromBottom (songSheet, Metrics::md * 2 + Metrics::hit + Metrics::hit * 2 + Metrics::sm * 3
                                                  + Playlist::kLanes * laneH + Metrics::hit + Metrics::btn);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        songCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));

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
            songPadModeBtn.setBounds (row.removeFromLeft (w).reduced (Metrics::halfGap, 2));
            songClearBtn.setBounds   (row.removeFromLeft (w).reduced (Metrics::halfGap, 2));
            songModeBtn.setBounds    (row.reduced (Metrics::halfGap, 2));
            inner.removeFromTop (Metrics::sm);
        }

        auto bottom = inner.removeFromBottom (Metrics::btn);
        songLenSlider.setBounds (bottom.reduced (Metrics::halfGap, 6));
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

    // MIX sheet: the sixteen channel strips of one bank, in a panel that scrolls.
    {
        //  The rows no longer negotiate with the card for their height: they
        //  are Metrics::row, always, and the card shows as many of them as it
        //  has room for. What used to be a 24 px row on a small phone - with
        //  a 20 px mute button on it - is now a scroll.
        //  FORTY-FOUR, NOT FORTY. At Metrics::hit the row lost one pixel top
        //  and bottom to its own air and two more to each control's, and M and
        //  S came out 36x36 on every screen in the matrix - under the forty the
        //  rest of the app is held to, on the two keys you hit fastest while
        //  something is playing. Four pixels of row is what buys them.
        const int rowH = Metrics::row;
        const int tabsH = Metrics::tab + Metrics::sm;
        const int mixFurniture = Metrics::md * 2 + Metrics::hit + Metrics::sm + tabsH
                               + Metrics::btn + Metrics::lg;
        auto inner = sheetFromBottom (mixSheet, mixFurniture + kPadsPerBank * rowH);
        auto titleRow = inner.removeFromTop (Metrics::hit);
        mixCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::sm);

        {
            auto tabs = inner.removeFromTop (Metrics::tab);
            const int bw = tabs.getWidth() / kNumBanks;
            for (int b = 0; b < mixBankBtns.size(); ++b)
                mixBankBtns[b]->setBounds ((b < kNumBanks - 1 ? Lang::takeStart (tabs, bw) : tabs)
                                             .reduced (Metrics::halfGap, 0));
            inner.removeFromTop (Metrics::sm);
        }

        auto bottom = inner.removeFromBottom (Metrics::btn);
        rackButton.setBounds (bottom.removeFromRight (bottom.getWidth() / 3).reduced (Metrics::halfGap, 4));
        mixClearSolo.setBounds (bottom.reduced (Metrics::halfGap, 4));
        inner.removeFromBottom (Metrics::xs);

        mixScroll.setBounds (inner);
        const int contentH = kPadsPerBank * rowH;
        //  Leave the bar its width only when there IS a bar, or every row is
        //  eight pixels short on the screens that did not need one.
        const int barW = contentH > inner.getHeight() ? mixScroll.getScrollBarThickness() : 0;
        mixRows.setSize (juce::jmax (80, inner.getWidth() - barW), contentH);

        auto rows = mixRows.getLocalBounds();
        for (int i = mixBank * kPadsPerBank; i < (mixBank + 1) * kPadsPerBank; ++i)
        {
            auto row = rows.removeFromTop (rowH).reduced (0, 1);
            row.removeFromLeft (juce::jlimit (48, 92, rows.getWidth() * 24 / 100));   // chip + number + name
            //  Padding here is not decoration, it is the hit area coming off
            //  the control. The pan was losing twelve pixels of a forty-pixel
            //  row to margins and ending up shorter than the M and S beside it.
            //  M and S are two different decisions about the channel, not one
            //  two-letter control, so they get the same air as everything else
            //  on the row.
            //  Reduced vertically only. Two pixels off each side of a cell that
            //  is exactly Metrics::hit wide is a 36 px key, and the air was
            //  already there: the halfGap between them is what separates M from
            //  S, so taking it out of the key as well paid for the same gap
            //  twice and left both under the floor on every screen measured.
            mixSolos[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (0, 1));
            row.removeFromRight (Metrics::halfGap);
            mixMutes[i]->setBounds (row.removeFromRight (Metrics::hit).reduced (0, 1));
            row.removeFromRight (Metrics::halfGap);
            //  ...and the pan is a target too, so it gets a floor rather than a
            //  share: a third of the row came to twenty-six pixels of travel on
            //  a 280 px screen, for a control that has to go both ways from
            //  centre. Where the floor and a fader worth aiming at do not both
            //  fit, THE PAN GOES - it is the one control on this row that has a
            //  full-size knob of its own one tap away in the PADS sheet, and a
            //  fader you cannot aim has no such second home. Hidden, not
            //  shrunk: jlimit would have clamped it back up to a width the row
            //  does not have and drawn it over the fader.
            const int panW  = juce::jlimit (Metrics::hit + 4, 78, row.getWidth() / 3);
            const bool room = row.getWidth() - panW >= 96;
            mixPans[i]->setVisible (room);
            if (room)
                mixPans[i]->setBounds (row.removeFromRight (panW).reduced (2, 1));

            //  On a narrow phone the level's number was eating the level.
            //  Forty-six pixels of readout plus its air out of an eighty-five
            //  pixel cell left thirty for the fader itself - a control you set
            //  by where the thumb is, reduced to a control you cannot aim.
            //  Where it does not fit, the number goes and the fader stays: the
            //  exact figure is one tap away in the PADS sheet, and a mixer is
            //  read by the shape of its faders, not by sixteen decimals.
            auto faderCell = row.reduced (4, 1);
            const bool tight = faderCell.getWidth() - Metrics::gap - 46 < 70;
            mixFaders[i]->setTextBoxStyle (tight ? juce::Slider::NoTextBox : juce::Slider::TextBoxRight,
                                           false, 46, Metrics::readout);
            mixFaders[i]->setBounds (faderCell);
        }
    }

    // El panel XY, si esta abierto: la mitad de arriba de la cara, y ni un
    // pixel dentro de la costura de PADS. Ver XyPanel en la cabecera.
    if (xyPanel.isVisible() && ! faceTopArea.isEmpty())
    {
        //  EL PANEL SE QUEDA CON LO QUE USA, no con la mitad de la cara.
        //
        //  Antes ocupaba faceTopArea entera y centraba dentro un cuadrado del
        //  lado menor: en un hueco de 380x700 eso son 380 de mando y 320 de
        //  NADA, repartidos en dos franjas vacias, arriba y abajo. La foto que
        //  lo enseno tenia el mando en la esquina de abajo a la izquierda y un
        //  tercio del panel en blanco encima.
        //
        //  Ahora el lado del cuadrado sale del ancho -que es lo que sobra en
        //  vertical- y la altura del panel sale del lado. Sin centrar, sin
        //  hueco: si no cabe, el que se recorta es el mando.
        const int fixed = 2 * Metrics::md          // margenes de arriba y abajo
                        + Metrics::hit             // titulo
                        + 14                       // que hace soltar el dedo
                        + Metrics::sm
                        + Metrics::hit             // los seis efectos, en UNA fila
                        + Metrics::sm;
        const int side = juce::jlimit (60,
                                       juce::jmax (60, faceTopArea.getWidth() - 2 * Metrics::lg),
                                       faceTopArea.getHeight() - fixed);
        xyPanel.setBounds (faceTopArea.withHeight (juce::jmin (faceTopArea.getHeight(),
                                                              fixed + side)));

        auto inner = xyPanel.getLocalBounds().reduced (Metrics::lg, Metrics::md);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        xyCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit)
                                    .withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        Lang::takeEnd (titleRow, Metrics::xs);
        //  MOMENTANEO / FIJO en la fila del titulo, no en una fila propia al
        //  fondo: es un interruptor de dos estados y estaba gastando 40 px de
        //  alto mas su rotulo para decir una palabra.
        {
            //  El hueco de la palabra son 2*md y no 2*sm: con 2*sm el banco
            //  saco MOMENTANEO pidiendo 70 px de los 63 que le quedaban en las
            //  siete pantallas. La fuente con la que se mide aqui no es
            //  exactamente la que dibuja la tapa, asi que el margen se pone
            //  por arriba y no se afina al pixel.
            const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);
            const int w = juce::jlimit (88, juce::jmax (88, titleRow.getWidth() / 2),
                                        (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                            capFont, xyLatchButton.getButtonText())) + 2 * Metrics::md);
            xyLatchButton.setBounds (Lang::takeEnd (titleRow, w).reduced (0, 2));
        }
        inner.removeFromTop (14);              // pintado: que hace soltar el dedo
        inner.removeFromTop (Metrics::sm);

        //  Los seis, en una fila y del mismo ancho que los seis de la cara.
        //  En dos columnas de tres al lado del mando eran una rejilla que hay
        //  que leer; en fila son la misma barra que ya esta aprendida.
        {
            auto row = inner.removeFromTop (Metrics::hit);
            for (int f = 0; f < xyFxButtons.size(); ++f)
                xyFxButtons[f]->setBounds (Lang::takeStart (row, row.getWidth() / (kNumFx - f))
                                             .reduced (Metrics::halfGap / 2, 2));
            inner.removeFromTop (Metrics::sm);
        }

        //  Cuadrado: el lado es el menor de los dos que quedan. Es lo unico
        //  que este componente tiene que garantizar - si un eje se barre con un
        //  gesto y el otro con dos, los dos parametros no se tocan igual.
        const int sq = juce::jmax (60, juce::jmin (inner.getWidth(), inner.getHeight()));
        xyPad.setBounds (inner.withSizeKeepingCentre (sq, sq));
    }

    // SEC sheet, two pages: PASOS is the grid and what plays it; PASO is the
    // step you tapped, plus the chain and the swing. See SeqPage in the header
    // for why it stopped being one card.
    {
        const int lanes = StepGrid::kLanes;
        constexpr int nameH = 14 + ZatiLookAndFeel::kTextPad;
        //  A named control is three things: its name, itself, and the air under
        //  it. Budget the band, never the control on its own.
        constexpr int bandH = nameH + Metrics::hit + Metrics::sm;

        const bool onGrid = (seqPage == seqPageGrid);

        const int patLen   = engine.getPatternLength (selectedPattern);
        const int bars     = juce::jmax (1, patLen / kStepCols);
        if (selectedBar >= bars) selectedBar = 0;
        //  A lone "1" is a control that can never do anything, so a one-bar
        //  pattern gets no bar row - and no band budgeted for it either.
        const bool showBars = (bars > 1);

        //  ASK FOR WHAT YOU WILL ACTUALLY GET. sheetFromBottom clamps the card
        //  at 78% of the window and says nothing; whatever the layout asked
        //  for beyond that is simply taken off the last thing laid out. The old
        //  card asked for 800 px on a 640 px phone, and the 300 px of shortfall
        //  came out of the grid - sixteen lanes in 55 px, three and a half
        //  pixels a lane. So the lane height is now DERIVED from the cap rather
        //  than clamped up to a number the card was never going to have, and
        //  `wanted` can never exceed `capH`.
        const int capH   = (int) (full.getHeight() * 0.78f);
        const int chrome = Metrics::md * 2          // the card's own margins
                         + Metrics::hit             // title row
                         + Metrics::sm
                         + Metrics::tab             // the two tabs
                         + Metrics::sm;

        //  Rotated, the card is short and wide: sixteen lanes cannot share 200
        //  px of height AND leave room for four stacked controls under them.
        //  So in landscape the grid takes the whole height of the card and the
        //  controls stand in a column beside it - which is the shape the window
        //  already is, instead of the shape a phone is.
        const int sideCol = wideFace ? juce::jlimit (150, 260, full.getWidth() / 4) : 0;

        int laneH  = 0;
        int wanted = 0;

        if (onGrid)
        {
            const int stacked = wideFace ? 0
                                         : bandH + bandH + (showBars ? bandH : 0) + bandH;
            //  Twelve is the floor at which a lane still reads as a lane. It is
            //  a floor, not a target: jlimit clamps UP too, and clamping up is
            //  exactly the bug this whole block exists to undo - so the value
            //  is only ever allowed to reach it when the room is genuinely
            //  there, which at 12 px a lane it always is.
            laneH  = juce::jlimit (12, 26, (capH - chrome - stacked) / lanes);
            wanted = chrome + stacked + lanes * laneH;
        }
        else
        {
            const int stepBands = nameH + Metrics::hit + Metrics::xs    // CADENA
                                + bandH                                 // NOTA DEL PASO
                                + bandH                                 // GOLPE
                                + nameH + Metrics::hit                  // SWING
                                + Metrics::sm + nameH + Metrics::hit;   // REJILLA
            //  The line at the foot that says which step is being edited is
            //  laid out, not squeezed in under the last control: unbudgeted it
            //  was drawn straight across the swing slider's track.
            wanted = chrome + (wideFace ? (bandH + Metrics::hit + nameH) : stepBands)
                            + Metrics::sm + kSeqFootH;
        }

        auto inner = sheetFromBottom (seqSheet, wanted);

        auto titleRow = inner.removeFromTop (Metrics::hit);
        seqCloseButton.setBounds (Lang::takeEnd (titleRow, Metrics::hit).withSizeKeepingCentre (Metrics::hit, Metrics::hit));
        inner.removeFromTop (Metrics::sm);

        {
            auto tabs = inner.removeFromTop (Metrics::tab);
            const int half = tabs.getWidth() / 2;
            seqGridBtn.setBounds (Lang::takeStart (tabs, half).reduced (Metrics::halfGap, 0));
            seqStepBtn.setBounds (tabs.reduced (Metrics::halfGap, 0));
            inner.removeFromTop (Metrics::sm);
        }

        //  Where each control's own name gets painted. paintSeqSheetContent
        //  used to reconstruct these bands from the control's bounds, which
        //  meant the name of a HIDDEN control was still drawn - four ghost
        //  captions floating over the grid the moment the card grew a second
        //  page. Now the layout records the band it reserved and paint draws
        //  only the ones that were reserved this pass.
        seqLabelBands.clear();
        seqFootArea = {};
        auto nameBand = [this, nameH] (juce::Rectangle<int>& col, const char* key)
        {
            auto b = col.removeFromTop (nameH);
            seqLabelBands.add ({ b, juce::String (key) });
            return b;
        };

        //  APAGADAS Y SIN SITIO, ANTES DE DECIDIR NADA.
        //
        //  Esto tiene que correr SIEMPRE, y el primer intento lo metio dentro
        //  del if (onGrid) - donde en la pagina PASO no corre nunca, asi que
        //  las cuatro tapas se quedaban con las coordenadas de la ultima vez
        //  que se maqueto PASOS: encima de los ocho botones de compas, 128
        //  solapes en las 476 corridas. Y ocultar no basta, hay que quitarles
        //  el sitio: un componente invisible que conserva sus limites sigue
        //  estando ahi para todo lo que mida geometria.
        for (auto* b : seqBankButtons) { b->setVisible (false); b->setBounds ({}); }

        if (onGrid)
        {
            //  In landscape the four controls stand in their own column and the
            //  grid keeps the full height; in portrait they stack under it in
            //  the order the work happens.
            auto side = wideFace ? Lang::takeEnd (inner, sideCol) : juce::Rectangle<int>();
            if (wideFace) Lang::takeEnd (inner, Metrics::gap);

            auto& col = wideFace ? side : inner;

            //  PATRON and LARGO share one row upright, so they share one band -
            //  split in two, a name over each control. One caption stretched
            //  across both is how NOTA once came to look like part of CADENA.
            {
                auto band = col.removeFromTop (nameH);
                if (wideFace)
                    seqLabelBands.add ({ band, juce::String ("PATRON") });
                else
                {
                    auto half = Lang::takeStart (band, band.getWidth() / 2);
                    seqLabelBands.add ({ half, juce::String ("PATRON") });
                    seqLabelBands.add ({ band, juce::String ("LARGO")  });
                }
            }
            {
                auto row = col.removeFromTop (Metrics::hit);
                //  An IncDecButtons slider gives its two keys whatever the text
                //  box leaves, so a narrow box on a wide row turns them into a
                //  pair of slabs twice the size of anything else on the card.
                //  Reserve the box first and the keys come out finger-sized.
                //  Stacked in the side column, each takes the whole width.
                const int w1 = wideFace ? row.getWidth() : row.getWidth() / 2;
                patternSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                               juce::jmax (40, w1 - 2 * Metrics::gap - 2 * Metrics::stepKey),
                                               Metrics::readout);
                if (wideFace)
                {
                    patternSlider.setBounds (row.reduced (Metrics::halfGap, 2));
                    col.removeFromTop (Metrics::sm);
                    nameBand (col, "LARGO");
                    lengthSlider.setBounds (col.removeFromTop (Metrics::hit).reduced (Metrics::halfGap, 2));
                }
                else
                {
                    patternSlider.setBounds (row.removeFromLeft (w1).reduced (Metrics::halfGap, 2));
                    lengthSlider.setBounds  (row.reduced (Metrics::halfGap, 2));
                }
            }
            col.removeFromTop (Metrics::sm);

            //  Copiar y pegar viven con el PATRON, que es lo que copian.
            {
                nameBand (col, "BANCO");
                auto row = col.removeFromTop (Metrics::hit);
                copyPatBtn.setBounds  (Lang::takeStart (row, row.getWidth() / 2).reduced (Metrics::halfGap, 2));
                pastePatBtn.setBounds (row.reduced (Metrics::halfGap, 2));
                col.removeFromTop (Metrics::sm);
            }

            //  El banco de pads, JUSTO ENCIMA de la rejilla que va a cambiar -
            //  la relacion entre las dos tiene que ser obvia sin leer nada -
            //  PERO SOLO SI CABE SIN ENCOGERLA.
            //
            //  Esta fila cuesta 66 px y salen enteros de la rejilla, que en las
            //  dos pantallas mas estrechas ya esta en el suelo: medido, la
            //  celda pasaba de 12 px de alto a OCHO en 280x653 y en 360x640, y
            //  doce ya era la tercera parte de un dedo. Una comodidad que
            //  encoge lo unico para lo que existe la ficha no es una comodidad.
            //
            //  Donde no cabe, las tapas A B C D de la cara siguen estando: se
            //  pierde el atajo, no la funcion. Y donde cabe - que son cinco de
            //  las siete pantallas del banco - se gana escribir un bombo del
            //  banco A y un bajo del D sin cerrar nada.
            const int lanesH   = juce::jmax (0, col.getHeight() - Metrics::hit * 2 - Metrics::sm * 2);
            const int rowCost  = 14 + Metrics::hit + Metrics::sm;
            const bool bankRowFits = (lanesH - rowCost) / kPadsPerBank >= kMinLaneH;

            //  Y SIN COORDENADAS CUANDO NO SE DIBUJAN.
            //
            //  Ocultarlas no basta: el banco mide SOLAPES sobre los limites que
            //  quedan puestos, y en la pagina PASO estas cuatro se quedaban
            //  encima de los ocho botones de compas - 128 solapes en las 476
            //  corridas, y el primer fallo duro de toda la sesion. Un
            //  componente que no se ve pero conserva su sitio sigue estando ahi
            //  para todo lo que mire geometria.
            const bool showBank = bankRowFits && onGrid;
            if (showBank)
            {
                for (auto* b : seqBankButtons) b->setVisible (true);
                nameBand (col, "PADS");
                auto row = col.removeFromTop (Metrics::hit);
                const int bw = row.getWidth() / kNumBanks;
                for (int b = 0; b < seqBankButtons.size(); ++b)
                    seqBankButtons[b]->setBounds ((b < kNumBanks - 1 ? row.removeFromLeft (bw) : row)
                                                      .reduced (Metrics::halfGap, 2));
                col.removeFromTop (Metrics::sm);
            }

            if (showBars)
            {
                nameBand (col, "COMPAS");
                auto row = col.removeFromTop (Metrics::hit);
                const int bw = row.getWidth() / bars;
                for (int b = 0; b < barButtons.size(); ++b)
                {
                    barButtons[b]->setVisible (b < bars);
                    if (b < bars)
                        barButtons[b]->setBounds ((b < bars - 1 ? row.removeFromLeft (bw) : row).reduced (Metrics::halfGap, 2));
                }
                col.removeFromTop (Metrics::sm);
            }
            else
            {
                for (auto* b : barButtons) b->setVisible (false);
            }

            //  TEMPO last, at the foot of whichever container it is in: it is
            //  the one number on this page that belongs to the machine rather
            //  than to the pattern.
            {
                auto row = col.removeFromBottom (Metrics::hit);
                //  Cuatro en la fila del tempo: el deslizador, TAP a su lado
                //  porque marcar y ver el numero es el mismo gesto, y luego
                //  VACIAR. Copiar y pegar van encima, con el patron.
                const int w4 = row.getWidth() / 4;
                bpmSlider.setBounds   (Lang::takeStart (row, row.getWidth() - 2 * w4).reduced (Metrics::halfGap, 2));
                tapButton.setBounds   (Lang::takeStart (row, w4).reduced (Metrics::halfGap, 2));
                clearButton.setBounds (row.reduced (Metrics::halfGap, 2));
                seqLabelBands.add ({ col.removeFromBottom (nameH), juce::String ("TEMPO") });
                col.removeFromBottom (Metrics::sm);
            }

            stepGrid.setBounds (inner);
        }
        else
        {
            //  The foot line first, so no column can lay a control over it.
            seqFootArea = inner.removeFromBottom (kSeqFootH);
            inner.removeFromBottom (Metrics::sm);

            //  Two columns rotated, one stacked upright - the same four groups
            //  either way, so the card never has to be taller than it is wide.
            auto colA = wideFace ? inner.removeFromLeft ((inner.getWidth() - Metrics::gap) / 2) : inner;
            auto colB = wideFace ? inner.withTrimmedLeft (Metrics::gap) : juce::Rectangle<int>();
            auto& second = wideFace ? colB : colA;

            nameBand (colA, "CADENA");
            {
                auto row = colA.removeFromTop (Metrics::hit);
                const int pw = row.getWidth() / kNumPatterns;
                for (int i2 = 0; i2 < kNumPatterns; ++i2)
                    patternButtons[i2]->setBounds ((i2 < kNumPatterns - 1 ? row.removeFromLeft (pw) : row).reduced (2));
                colA.removeFromTop (Metrics::xs);
            }

            //  QUITAR CADENA belongs to the row above it, NOTA to the step you
            //  tapped: two different jobs that happen to fit on one line, so
            //  the note half is the one that gets the name.
            nameBand (colA, "NOTA DEL PASO");
            {
                auto row = colA.removeFromTop (Metrics::hit);
                chainClearButton.setBounds (Lang::takeStart (row, row.getWidth() * 5 / 12).reduced (Metrics::halfGap, 2));
                noteSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                            juce::jmax (40, row.getWidth() - 2 * Metrics::gap - 2 * Metrics::stepKey),
                                            Metrics::readout);
                noteSlider.setBounds (row.reduced (Metrics::halfGap, 2));
                colA.removeFromTop (Metrics::sm);
            }

            //  What the step DOES: how hard, and how many times.
            nameBand (second, "GOLPE");
            {
                auto row = second.removeFromTop (Metrics::hit);
                velSlider.setBounds (Lang::takeStart (row, row.getWidth() * 7 / 12).reduced (Metrics::halfGap, 2));
                auto rollCell = row.reduced (Metrics::halfGap, 2);
                rollSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                            juce::jmax (36, rollCell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                            Metrics::readout);
                rollSlider.setBounds (rollCell);
                second.removeFromTop (Metrics::sm);
            }

            nameBand (second, "SWING");
            swingSlider.setBounds (second.removeFromTop (Metrics::hit).reduced (Metrics::halfGap, 2));
            second.removeFromTop (Metrics::sm);

            nameBand (second, "REJILLA");
            {
                auto cell = second.removeFromTop (Metrics::hit).reduced (Metrics::halfGap, 2);
                //  Las dos teclas primero, el numero con lo que quede: es la
                //  misma reserva que CHOKE y que GOLPE, y por la misma razon -
                //  sin ella JUCE apila el + sobre el - en cuanto la casilla se
                //  come el ancho, y quedan dos rendijas de 17 px.
                gridSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false,
                                            juce::jmax (40, cell.getWidth() - Metrics::gap - 2 * Metrics::stepKey),
                                            Metrics::readout);
                gridSlider.setBounds (cell);
            }
        }
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
    {
        engine.postNoteOn (index, pads[index] != nullptr ? pads[index]->getLastVelocity() : 1.0f);

        //  Said once, the first time this phone turns out to have a force
        //  sensor under the glass. A feature nobody is told about is a
        //  feature that reads as the app being inconsistent.
        //
        //  BRACES. Without them the announcement's `if` swallowed the `else`
        //  below it, so every tap on a LOADED pad played the sound and then
        //  reported "this pad is empty". The indentation said one thing and
        //  the compiler read another - which is the whole reason a one-line
        //  body does not stay a one-line body once something is added to it.
        if (! pressureAnnounced && pads[index] != nullptr && pads[index]->lastStrikeUsedPressure())
        {
            pressureAnnounced = true;
            status.setText (T ("Pads sensibles a la fuerza del golpe"), juce::dontSendNotification);
        }
    }
    else
    {
        status.setText (T ("Pad vacio - pulsa LOAD y toca el pad para cargarlo"), juce::dontSendNotification);
    }

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
    //  The three step controls follow whatever you just touched, so what they
    //  show is always the step under your finger and never the last one.
    noteSlider.setValue (engine.getStepNote (selectedPattern, step, pad), juce::dontSendNotification);
    velSlider.setValue  (engine.getStepVel  (selectedPattern, step, pad), juce::dontSendNotification);
    rollSlider.setValue (engine.getStepRoll (selectedPattern, step, pad), juce::dontSendNotification);

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
    //  Sixteen lanes, of whichever bank the face is on. The pattern itself
    //  holds all sixty-four - a step written in bank B keeps playing while you
    //  edit bank A, which is the whole point of banks - the grid just shows
    //  the sixteen you can currently reach with a thumb.
    const int base = currentBank * kPadsPerBank;

    for (int st = 0; st < kNumSteps; ++st)
        for (int p = 0; p < kPadsPerBank; ++p)
        {
            gridCells[st * kPadsPerBank + p] = pattern[(size_t) selectedPattern][(size_t) st][(size_t) (base + p)];
            gridNotes[st * kPadsPerBank + p] = (signed char) engine.getStepNote (selectedPattern, st, base + p);
        }

    for (int p = 0; p < kPadsPerBank; ++p)
    {
        gridZati[p]   = padZati[(size_t) (base + p)];
        gridLoaded[p] = padHasSample[(size_t) (base + p)];
    }

    const int ps = (engine.isPlaying() && engine.getPlayingPattern() == selectedPattern)
                     ? engine.getPlayStep() : -1;

    stepGrid.setSource (gridCells, gridZati, gridLoaded, gridNotes,
                        engine.getPatternLength (selectedPattern),
                        selectedBar, ps, selectedPad - base,
                        ps >= 0 ? engine.getStepPhase() : 0.0f,
                        base);   // el pad del carril 0, para que el canalon diga 17..32 en el banco B

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

//  Point the grid at another sixteen.
//
//  Nothing about the machine changes: every pad keeps its sound, the pattern
//  keeps every step in all four banks, and anything sounding goes on sounding.
//  The selected pad moves with the view, because the sheets - PADS, the trim,
//  the step controls - all edit "the pad you are on", and leaving that behind
//  in a bank you can no longer see is how you end up editing something you
//  cannot hear.
void MainComponent::selectBank (int bank)
{
    const int b = juce::jlimit (0, kNumBanks - 1, bank);
    if (b == currentBank) return;

    currentBank = b;
    if (auto* t = bankButtons[b]) t->setToggleState (true, juce::dontSendNotification);
    //  Las dos filas dicen lo mismo siempre: la de la cara y la de la ficha.
    if (auto* t = seqBankButtons[b]) t->setToggleState (true, juce::dontSendNotification);

    selectPad (currentBank * kPadsPerBank + (selectedPad % kPadsPerBank + kPadsPerBank) % kPadsPerBank);

    resized();
    refreshStepGrid();
    refreshMixStrip();
    repaint();
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
    volSlider.setValue   (dbFromGain (padGain[(size_t) index]), juce::dontSendNotification);
    startSlider.setValue (padStart01[(size_t) index], juce::dontSendNotification);
    endSlider.setValue   (padEnd01[(size_t) index],   juce::dontSendNotification);
    reverseButton.setToggleState (padReverse[(size_t) index], juce::dontSendNotification);
    loopButton.setToggleState    (padLoop[(size_t) index],    juce::dontSendNotification);
    autocutButton.setToggleState (padSelfCut[(size_t) index], juce::dontSendNotification);
    //  El bombeo lo manda UN pad de los sesenta y cuatro, asi que el boton
    //  esta encendido solo cuando el pad que tienes delante es ese.
    duckButton.setToggleState (engine.getDuckPad() == index, juce::dontSendNotification);
    chokeSlider.setValue (padChokeUI[(size_t) index], juce::dontSendNotification);
    panSlider.setValue     (padPan[(size_t) index],     juce::dontSendNotification);
    attackSlider.setValue  (padAttack[(size_t) index],  juce::dontSendNotification);
    releaseSlider.setValue (padRelease[(size_t) index], juce::dontSendNotification);
    cutSlider.setValue  (padCut[(size_t) index],  juce::dontSendNotification);
    resoSlider.setValue (padReso[(size_t) index], juce::dontSendNotification);
    //  Los envios se leen del MOTOR, que es quien los guarda: el RACK mueve
    //  los mismos seis numeros y una copia en la interfaz se quedaria vieja en
    //  cuanto se tocaran desde alli.
    for (int f = 0; f < padSends.size(); ++f)
        if (auto* sl = padSends[f])
            sl->setValue (engine.getPadSend (index, f), juce::dontSendNotification);
}

//  How long a pad's sound is, asked of the copy the INTERFACE holds.
//
//  engine.getSampleLength reads the pointer the audio thread has adopted, and
//  adoption happens at the top of a render block - so it is zero before the
//  first block after a load, and zero for as long as there is no device at
//  all. Every trim edit multiplied by it, so dragging a handle in either of
//  those moments wrote start=0 and end=0 and the pad lost its slice while the
//  interface went on showing a normal window. It is also a plain data race:
//  that pointer belongs to the audio thread.
//  NORMALIZAR: buscar el pico y poner la ganancia que lo deja a -0.3 dBFS.
//
//  El pico se mide SOBRE EL RECORTE, no sobre el fichero entero. Un corte de
//  un compas sacado de una cancion de tres minutos comparte buffer con el
//  resto de la cancion, y medir el fichero entero le daba la ganancia del
//  golpe mas fuerte del tema - que casi nunca esta dentro del trozo que suena.
//  Medido en el primer intento: un corte de charles salia a -19 dBFS despues
//  de "normalizarlo".
//
//  -0.3 y no 0: entre la muestra y el altavoz hay remuestreo, filtros y suma
//  de pads, y todos ellos pueden pasar de largo el pico que habia en la
//  muestra. Tres decimas de margen es lo que pide cualquier norma de entrega.
void MainComponent::normalisePad()
{
    if (selectedPad < 0) return;
    const size_t sp = (size_t) selectedPad;

    auto sb = uiSample[sp];
    const int len = padSourceLength (selectedPad);
    if (sb == nullptr || len <= 0)
    {
        status.setText (T ("El pad %1 no tiene sonido", juce::String (selectedPad + 1)),
                        juce::dontSendNotification);
        return;
    }

    const int a = juce::jlimit (0, len - 1, (int) std::floor (padStart01[sp] * (float) len));
    const int b = juce::jlimit (a + 1, len, (int) std::ceil  (padEnd01[sp]   * (float) len));
    const float peak = sb->buffer.getMagnitude (a, b - a);

    //  Silencio de verdad: dividir por el pico seria dividir por cero, y
    //  subir 60 dB de nada sigue siendo nada, con el ruido de fondo dentro.
    if (peak < 1.0e-5f)
    {
        status.setText (T ("El recorte esta en silencio"), juce::dontSendNotification);
        return;
    }

    constexpr double kTargetDb = -0.3;
    const double want = juce::Decibels::decibelsToGain (kTargetDb) / (double) peak;
    const double db   = juce::jlimit (kGainMinDb, kGainMaxDb,
                                      juce::Decibels::gainToDecibels (want, kGainMinDb));

    pushUndo (T ("NORMALIZAR"));

    const float g = gainFromDb (db);
    padGain[sp] = g;
    engine.setPadGain (selectedPad, g);
    volSlider.setValue (db, juce::dontSendNotification);
    if (auto* f = mixFaders[selectedPad]) f->setValue (db, juce::dontSendNotification);

    //  Decir el numero, y decirlo tambien cuando se ha quedado corto: una
    //  toma a -40 dBFS pide +40 dB y el mando llega a +12. Callarlo dejaria
    //  "normalizado" un pad que sigue sonando 28 dB por debajo.
    const bool capped = db >= kGainMaxDb - 0.05;
    status.setText ((capped ? T ("GANANCIA al tope: %1", Lang::ltr (gainText (db, true)))
                            : T ("Pico a -0.3 dBFS con %1", Lang::ltr (gainText (db, true)))),
                    juce::dontSendNotification);
}

//  QUITAR RUIDO del pad que se esta mirando.
//
//  Sobre una COPIA. La muestra la puede estar leyendo el hilo de audio en este
//  mismo instante, y ademas la pueden compartir varios pads si salio de un
//  auto chop - escribir encima seria cambiarle el sonido a los otros quince
//  sin avisar. Se copia, se limpia la copia y se publica por el mismo camino
//  que usa un corte: intercambio de puntero, y el viejo lo suelta el
//  temporizador en el hilo de mensajes.
//
//  El recorte se conserva. assignSampleToPad lo pone a 0..1 porque un buffer
//  nuevo suele ser otro sonido; aqui es EL MISMO sonido con menos siseo y con
//  exactamente la misma longitud, asi que perder el recorte seria perder el
//  trabajo.
void MainComponent::denoisePad()
{
    if (selectedPad < 0) return;
    const size_t sp = (size_t) selectedPad;

    auto src = uiSample[sp];
    const int len = padSourceLength (selectedPad);
    if (src == nullptr || len <= 0)
    {
        status.setText (T ("El pad %1 no tiene sonido", juce::String (selectedPad + 1)),
                        juce::dontSendNotification);
        return;
    }
    //  Menos de 2048 muestras son 46 ms: no hay ventanas suficientes para
    //  estimar un perfil, y lo que saldria seria la propia muestra tomada por
    //  ruido.
    if (len < 2048)
    {
        status.setText (T ("La muestra es demasiado corta para medir el ruido"),
                        juce::dontSendNotification);
        return;
    }

    //  UNA VEZ, Y EN OTRO HILO.
    //
    //  La resta espectral recorre el fichero entero dos veces y guarda el
    //  espectro de cada ventana: 7 ms por segundo de audio medidos en el banco.
    //  Con una muestra de cinco minutos son 2.1 s con la interfaz congelada y
    //  con una de veinte, ocho - y Android saca el cartel de "la aplicacion no
    //  responde" a los cinco. Se copia aqui, se limpia alli, y se vuelve.
    if (denoiseBusy) return;
    denoiseBusy = true;
    denoiseButton.setEnabled (false);
    beginBusy (T ("Quitando ruido"));
    status.setText (T ("Quitando ruido..."), juce::dontSendNotification);

    pushUndo (T ("QUITAR RUIDO"));

    SampleBuffer::Ptr clean = new SampleBuffer();
    clean->buffer.makeCopyOf (src->buffer);
    clean->sourceSampleRate = src->sourceSampleRate;

    const int  pad       = selectedPad;
    const auto keepSrc   = src;                       // para saber si sigue ahi al volver
    const float keepStart = padStart01[sp], keepEnd = padEnd01[sp];
    const juce::String keepName = padName[sp];

    denoisePool.addJob ([this, clean, keepSrc, pad, len, keepStart, keepEnd, keepName]
    {
        const float before = clean->buffer.getMagnitude (0, len);
        Denoise::process (clean->buffer, 0.6f);
        const float after = clean->buffer.getMagnitude (0, len);

        juce::MessageManager::callAsync ([this, clean, keepSrc, pad, len, keepStart, keepEnd, keepName, before, after]
        {
            denoiseBusy = false;
            denoiseButton.setEnabled (true);
            endBusy();

            //  Si mientras tanto ese pad ha cambiado de sonido, lo limpiado ya
            //  no es de nadie: se tira. Pisarlo seria devolverle a la persona
            //  el sonido que acaba de quitar.
            if (! juce::isPositiveAndBelow (pad, kNumPads) || uiSample[(size_t) pad] != keepSrc)
            {
                status.setText (T ("El pad cambio mientras se limpiaba"), juce::dontSendNotification);
                return;
            }

            assignSampleToPad (pad, clean, {});
            padName[(size_t) pad]    = keepName;
            padStart01[(size_t) pad] = keepStart;
            padEnd01[(size_t) pad]   = keepEnd;
            engine.setPadStart (pad, (int) (keepStart * (float) len));
            engine.setPadEnd   (pad, (int) (keepEnd   * (float) len));
            if (auto* p = pads[pad])
                p->setSampleInfo (uiSample[(size_t) pad], padName[(size_t) pad],
                                  padStart01[(size_t) pad], padEnd01[(size_t) pad]);
            selectPad (pad);

            //  Se dice cuanto ha BAJADO el pico, que es la unica forma de saber
            //  si ha hecho algo sin volver a escucharlo entero.
            //
            //  Y SE DICE QUE ES UNA BAJADA, NO UN PICO. "pico 5.2 dB" se lee
            //  como que el pico VALE 5.2 dB, que para un pico digital es
            //  imposible: siempre es cero o menos. Lo que sale de aqui es la
            //  DIFERENCIA entre antes y despues, asi que el rotulo lo dice y el
            //  signo se invierte para que el numero cuente lo que se ha quitado.
            const double db = juce::Decibels::gainToDecibels ((double) juce::jmax (1.0e-6f, after)
                                                            / (double) juce::jmax (1.0e-6f, before), -60.0);
            status.setText (T ("Ruido fuera - el pico baja %1 dB", Lang::ltr (juce::String (-db, 1))),
                            juce::dontSendNotification);
        });
    });
}

int MainComponent::padSourceLength (int pad) const
{
    if (! juce::isPositiveAndBelow (pad, kNumPads)) return 0;
    if (auto& sb = uiSample[(size_t) pad]; sb != nullptr)
        return sb->buffer.getNumSamples();
    return 0;
}

//  LOS SESENTA Y CUATRO DE FABRICA. Ver Kits.h.
//
//  Sintetizarlos son unos milisegundos y CERO bytes de instalacion, asi que se
//  hacen aqui mismo en vez de viajar dentro del APK. Va por el mismo camino que
//  cargar un fichero - assignSampleToPad - para que el motor, la sesion y la
//  onda no tengan ni que enterarse de que estos vienen de otro sitio.
void MainComponent::loadFactoryKits (int onlyBank)
{
    const int from = (onlyBank < 0) ? 0 : juce::jlimit (0, kNumBanks - 1, onlyBank) * kPadsPerBank;
    const int to   = (onlyBank < 0) ? kNumPads : from + kPadsPerBank;

    for (int i = from; i < to; ++i)
    {
        if (auto sb = Kits::render (i))
        {
            assignSampleToPad (i, sb, Kits::table()[i].name);
            //  El color del pad lo pone Zati::forPad y no se toca: el orden de
            //  corte manda sobre cualquier idea decorativa.
            padHasSample[(size_t) i] = true;
        }
    }

    for (int i = from; i < to; ++i) refreshPad (i);
    selectPad (juce::jmax (0, selectedPad));
}

void MainComponent::assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name)
{
    if (sb == nullptr || ! juce::isPositiveAndBelow (index, kNumPads)) return;
    padHasSample[(size_t) index] = true;
    uiSample[(size_t) index]     = sb;
    padStart01[(size_t) index]   = 0.0f;
    padEnd01[(size_t) index]     = 1.0f;
    if (name.isNotEmpty())
        padName[(size_t) index] = name.upToLastOccurrenceOf (".", false, false);

    //  AND THE ENGINE HAS TO BE TOLD.
    //
    //  This is the function that means "this buffer is now on this pad", and
    //  it did not publish. Only SampleLoader did, on its own thread, for the
    //  one path that goes through it - so LOAD worked and every other path
    //  did not. A restored session and an opened project read their WAVs with
    //  ProjectStore::readSample and handed them here: the tile drew the
    //  waveform, the name appeared, padHasSample went true, and
    //  AudioEngine::padSample stayed NULL. Sixteen pads that look loaded and
    //  make no sound. Open a project on top of another and it is worse - the
    //  pads play the PREVIOUS project's audio, because that is what the
    //  engine is still holding.
    //
    //  Publishing here, at the one place that owns the fact, is what makes
    //  the loader's own publish redundant rather than load-bearing. Two
    //  publishes of the same pointer are safe: each takes a reference and the
    //  exchange releases the one it displaces.
    //
    //  It also resets the engine's trim to the whole file, which is exactly
    //  what the two lines above just did to the interface's copy - so the two
    //  now say the same thing, and whoever restores a real trim (applyState)
    //  overrides both.
    engine.publishSample (index, sb);

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
//  The names TalkBack reads out.
//
//  These were sixteen sliders and sixty-four mixer controls named in Spanish
//  with string literals, in the constructor, once. So the app spoke four
//  languages to anyone who could see it and exactly one to anyone who could
//  not - and a language change did not reach them at all, because nothing ever
//  set them again. They go through T() now and retranslateUi calls this.
void MainComponent::refreshAccessibleNames()
{
    struct Named { juce::Slider& s; const char* title; const char* what; };
    for (auto& n : { Named { pitchSlider,   "Tono",      "semitonos" },
                     Named { fineSlider,    "Afinado",   "centesimas" },
                     Named { volSlider,     "Ganancia",  "decibelios" },
                     Named { panSlider,     "Paneo",     "del pad" },
                     Named { attackSlider,  "Ataque",    "milisegundos" },
                     Named { releaseSlider, "Caida",     "milisegundos" },
                     Named { startSlider,   "Inicio",    "recorte" },
                     Named { endSlider,     "Fin",       "recorte" },
                     Named { chokeSlider,   "Choke",     "grupo de corte" },
                     Named { bpmSlider,     "Tempo|nombre", "pulsos por minuto" },
                     Named { patternSlider, "Patron",    "del secuenciador" },
                     Named { noteSlider,    "Nota",      "del paso" },
                     Named { lengthSlider,  "Compases",  "del patron" },
                     Named { gridSlider,    "Rejilla",   "cuanto dura un paso" } })
    {
        n.s.setTitle (T (n.title));
        n.s.setDescription (T (n.what));
    }

    for (int f = 0; f < padSends.size(); ++f)
        if (auto* sl = padSends[f])
        {
            sl->setTitle (T ("Envio a %1", T (fxDefs[f].name)));
            sl->setDescription (T ("del pad"));
        }

    juce::Slider* macros[3] = { &macroCtrl1, &macroCtrl2, &macroCtrl3 };
    for (int i = 0; i < 3; ++i)
    {
        macros[i]->setTitle (T ("Control %1", juce::String (i + 1)));
        macros[i]->setDescription (T ("del efecto"));
    }

    //  The mixer builds its strips per pad, and the channel number is the
    //  whole point of the name - so it goes in as an argument rather than
    //  glued on, which is the only way it lands correctly in every language.
    for (int i = 0; i < kNumPads; ++i)
    {
        const auto ch = juce::String (i + 1);
        if (auto* f = mixFaders[i]) { f->setTitle (T ("Ganancia canal %1", ch)); f->setDescription (T ("del mezclador")); }
        if (auto* p = mixPans[i])   { p->setTitle (T ("Paneo canal %1",   ch)); p->setDescription (T ("del mezclador")); }
        if (auto* m = mixMutes[i])  { m->setTitle (T ("Silencio %1", ch)); }
        if (auto* s = mixSolos[i])  { s->setTitle (T ("Solo %1",     ch)); }
    }
}

void MainComponent::retranslateUi()
{
    refreshAccessibleNames();

    padsButton  .setButtonText (T ("PADS"));
    secButton   .setButtonText (T ("SEC"));
    songButton  .setButtonText (T ("SONG"));
    xyButton    .setButtonText (T ("XY"));
    mixButton   .setButtonText (T ("MIX"));
    setButton   .setButtonText (T ("SET"));

    loadButton  .setButtonText (T ("LOAD"));
    testButton  .setButtonText (T ("TEST"));
    measureButton.setButtonText (T ("MEDIR"));
    quantButton .setButtonText (T ("CUADRAR"));
    recButton   .setButtonText (recArmed ? T ("REC ON") : T ("REC"));
    playButton  .setButtonText (engine.isPlaying() ? T ("STOP") : T ("PLAY"));
    clearButton .setButtonText (T ("VACIAR"));
    seqGridBtn  .setButtonText (T ("PASOS"));
    tapButton   .setButtonText (T ("TAP"));
    copyPatBtn  .setButtonText (T ("COPIAR"));
    pastePatBtn .setButtonText (T ("PEGAR"));
    seqStepBtn  .setButtonText (T ("PASO"));
    //  The three tabs of the settings card. Their rows have been in Lang.cpp
    //  all along - PROJECTS / 工程 / المشاريع, GESTURES / 手势 / إيماءات - and
    //  nothing ever asked for them: the buttons were constructed with the
    //  Spanish literal and never retranslated, so the card that CONTAINS the
    //  language selector was the one card still in Spanish after you used it.
    //  AUDIO hid the bug for both its neighbours by being the same word.
    pageAudioBtn.setButtonText (T ("AUDIO"));
    pageProjBtn .setButtonText (T ("PROYECTOS"));
    pageGestBtn .setButtonText (T ("GESTOS"));
    pageMidiBtn .setButtonText (T ("MIDI"));
    manualButton.setButtonText (T ("MANUAL"));
    undoButton  .setButtonText (T ("DESHACER"));
    redoButton  .setButtonText (T ("REHACER"));

    reverseButton.setButtonText (T ("REV|reverso"));
    loopButton   .setButtonText (T ("LOOP"));
    autocutButton.setButtonText (T ("AUTOCUT"));
    duckButton   .setButtonText (T ("BOMBEO"));
    normButton   .setButtonText (T ("NORMALIZAR"));
    padSoundBtn  .setButtonText (T ("SONIDO"));
    padTrimBtn   .setButtonText (T ("RECORTE"));
    padRigBtn    .setButtonText (T ("EL PAD"));
    denoiseButton.setButtonText (T ("QUITAR RUIDO"));
    chopButton   .setButtonText (T ("AUTO CHOP"));
    micButton    .setButtonText (recordingActive ? T ("PARAR") : T ("GRABAR MIC"));
    resampleButton.setButtonText (resamplingActive ? T ("PARAR") : T ("REMUESTREAR"));
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
    browseKitButton   .setButtonText (T ("CARGAR KIT"));
    browseFactoryButton.setButtonText (T ("FABRICA"));
    browseSystemButton.setButtonText (T ("SISTEMA"));

    exportMasterButton.setButtonText (T ("MASTER"));
    exportStemsButton .setButtonText (T ("PISTAS"));
    exportCancelButton.setButtonText (T ("CANCELAR"));

    rackButton   .setButtonText (T ("RACK"));
    mixClearSolo .setButtonText (T ("SIN SOLO"));
    songClearBtn .setButtonText (T ("VACIAR"));
    songModeBtn  .setButtonText (T ("CANCION"));

    chopSafeButton.setButtonText (T ("RESPETAR PADS CON SONIDO"));
    //  Y las dos del modo, que se construyen con el literal y no se
    //  retraducirian jamas sin esta linea. Es el mismo fallo que tuvieron las
    //  tres pestanas de AJUSTES - la ficha que CONTIENE el selector de idioma -
    //  y lo caza la prueba comparativa, no la tabla.
    chopEvenBtn.setButtonText (T ("IGUALES"));
    chopHitsBtn.setButtonText (T ("GOLPES"));

    //  A slider that formats its own readout has to be told to run the
    //  formatter again; the text it is showing was made in the old language.
    //  ...ALL of them, not the two that were noticed. swingSlider prints
    //  "recto" and songLenSlider prints the bar count, and both were left off
    //  this list, so even once their formatters went through T() they would
    //  have kept showing the language the app was started in until the value
    //  next changed.
    for (auto* sl : { &lengthSlider, &chokeSlider, &swingSlider, &songLenSlider })
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
    //  Each button gets back the style it was BUILT with, not a guess.
    //  GUARDAR is an accent cap with white text (it is the only primary action
    //  in its row); restyling it as a plain key on disarm stripped that for
    //  the rest of the session, every time a name collision was armed and then
    //  confirmed or timed out.
    styleButton (*b, b == &projDeleteButton ? kRec
                   : b == &projSaveButton   ? kAccent : kKey);
    if (b == &projSaveButton)
        b->setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    b->repaint();
}

void MainComponent::capturePads (PadSet& into) const
{
    for (int i = 0; i < kNumPads; ++i)
        into[(size_t) i] = uiSample[(size_t) i];
}

//  Put the buffers back, then let applyState put the numbers back over them:
//  assignSampleToPad resets the trim to the whole file, so it has to run
//  BEFORE the state that knows the real one.
void MainComponent::restorePads (const PadSet& from)
{
    for (int i = 0; i < kNumPads; ++i)
    {
        if (from[(size_t) i] != nullptr)
        {
            assignSampleToPad (i, from[(size_t) i], padName[(size_t) i]);
        }
        else if (uiSample[(size_t) i] != nullptr)
        {
            uiSample[(size_t) i]     = nullptr;
            padHasSample[(size_t) i] = false;
            padName[(size_t) i]      = {};
            engine.clearPad (i);
            if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
        }
    }
}

//  Ver kTapSlots. Se usa Time::getMillisecondCounterHiRes porque es monotono:
//  la hora del sistema puede saltar y un salto atras daria un tempo negativo.
void MainComponent::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (tapCount > 0 && now - tapTimes[(tapCount - 1) % kTapSlots] > 2000.0)
        tapCount = 0;

    tapTimes[tapCount % kTapSlots] = now;
    ++tapCount;

    if (tapCount < 2)
    {
        status.setText (T ("Sigue marcando el tempo"), juce::dontSendNotification);
        return;
    }

    const int n = juce::jmin (tapCount, kTapSlots);
    const double first = tapTimes[(tapCount - n) % kTapSlots];
    const double span  = now - first;
    if (span < 1.0) return;

    const double bpm = juce::jlimit (60.0, 200.0, 60000.0 * (double) (n - 1) / span);
    bpmSlider.setValue (std::round (bpm), juce::sendNotificationSync);
    status.setText (T ("Tempo %1", juce::String (juce::roundToInt (bpm))),
                    juce::dontSendNotification);
}

void MainComponent::copyPattern()
{
    patClipLen = engine.getPatternLength (selectedPattern);
    for (int st = 0; st < AudioEngine::kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
        {
            patClip[(size_t) st][(size_t) p] = pattern[(size_t) selectedPattern][(size_t) st][(size_t) p];
            patClipNote[(size_t) st][(size_t) p] =
                (signed char) engine.getStepNote (selectedPattern, st, p);
        }
    patClipFull = true;
    pastePatBtn.setEnabled (true);
    status.setText (T ("P%1 copiado", juce::String (selectedPattern + 1)),
                    juce::dontSendNotification);
}

void MainComponent::pastePattern()
{
    if (! patClipFull) return;
    //  Pegar SOBRESCRIBE, asi que pasa por deshacer como cualquier otra cosa
    //  que se lleva por delante lo que habia.
    pushUndo (T ("PEGAR"));

    engine.setPatternLength (selectedPattern, patClipLen);
    lengthSlider.setValue (patClipLen, juce::dontSendNotification);
    for (int st = 0; st < AudioEngine::kNumSteps; ++st)
        for (int p = 0; p < kNumPads; ++p)
        {
            const bool on = patClip[(size_t) st][(size_t) p];
            pattern[(size_t) selectedPattern][(size_t) st][(size_t) p] = on;
            engine.setStep (selectedPattern, st, p, on);
            engine.setStepNote (selectedPattern, st, p, patClipNote[(size_t) st][(size_t) p]);
        }
    refreshStepGrid();
    seqSheet.repaint();
    status.setText (T ("Pegado en P%1", juce::String (selectedPattern + 1)),
                    juce::dontSendNotification);
}

void MainComponent::pushUndo (const juce::String& what)
{
    Snapshot snap;
    snap.state = captureState();
    capturePads (snap.pads);
    snap.label = what;
    undoStack.push_back (std::move (snap));
    //  La pila tiene fondo: el mas viejo se cae por abajo. Sin tope, una sesion
    //  larga acumula ValueTrees y punteros con cuenta hasta quedarse sin
    //  memoria justo cuando mas trabajo hay que perder.
    if ((int) undoStack.size() > kUndoDepth) undoStack.erase (undoStack.begin());
    redoStack.clear();              // una accion nueva termina la rama de rehacer
    undoButton.setVisible (true);
    redoButton.setVisible (false);
    resized();
}

//  Undo and redo are the same move in opposite directions: each keeps what it
//  is about to replace, so you can step back and forth over one action instead
//  of the one-way trip DESHACER was on its own.
void MainComponent::performUndo()
{
    if (undoStack.empty()) return;

    Snapshot now;
    now.state = captureState();
    capturePads (now.pads);
    now.label = undoStack.back().label;
    redoStack.push_back (std::move (now));

    auto snap = std::move (undoStack.back());
    undoStack.pop_back();

    restorePads (snap.pads);
    applyState (snap.state);
    undoButton.setVisible (! undoStack.empty());
    redoButton.setVisible (true);
    status.setText (T ("Deshecho: %1", snap.label), juce::dontSendNotification);
    resized();
}

void MainComponent::performRedo()
{
    if (redoStack.empty()) return;

    Snapshot now;
    now.state = captureState();
    capturePads (now.pads);
    now.label = redoStack.back().label;
    undoStack.push_back (std::move (now));

    auto snap = std::move (redoStack.back());
    redoStack.pop_back();

    restorePads (snap.pads);
    applyState (snap.state);
    redoButton.setVisible (! redoStack.empty());
    undoButton.setVisible (true);
    status.setText (T ("Rehecho: %1", snap.label), juce::dontSendNotification);
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

    //  A chop fills the bank you are LOOKING at, wrapping inside it. Spilling
    //  sixteen slices across a bank boundary puts half of them on a page you
    //  have to go and find, and the whole point of chopping is that the pieces
    //  are under your hand.
    const int base = (selectedPad / kPadsPerBank) * kPadsPerBank;
    for (int k = 1; k < kPadsPerBank && t.size() < slices; ++k)
    {
        const int i = base + (selectedPad - base + k) % kPadsPerBank;
        if (onlyEmpty && padHasSample[(size_t) i]) continue;
        t.add (i);
    }

    return t;
}

//  Los golpes, UNA VEZ. Se guardan contra el pad para el que se calcularon,
//  porque quien los pide es el repintado de la ficha y ese ocurre en cada
//  toque: recalcular una FFT de 1024 sobre cuatro segundos -750 ventanas- en
//  cada repintado seria congelar la ficha mientras alguien elige un numero.
void MainComponent::refreshChopHits()
{
    chopHits.clear();
    chopHitsFor = -1;
    if (selectedPad < 0) return;
    auto src = uiSample[(size_t) selectedPad];
    if (src == nullptr || src->buffer.getNumSamples() < 2048) return;

    chopHits    = Onsets::detect (src->buffer, src->sourceSampleRate);
    chopHitsFor = selectedPad;
}

void MainComponent::openChopSheet()
{
    if (selectedPad < 0) selectPad (0);

    for (int i = 0; i < chopCountBtns.size(); ++i)
        chopCountBtns[i]->setToggleState (kChopCounts[i] == chopSlices, juce::dontSendNotification);

    chopSafeButton.setToggleState (chopOnlyEmpty, juce::dontSendNotification);
    chopEvenBtn.setToggleState (! chopByHits, juce::dontSendNotification);
    chopHitsBtn.setToggleState (chopByHits,   juce::dontSendNotification);

    //  Se calculan al abrir, no al pulsar GOLPES: asi el numero de golpes ya
    //  esta en la ficha cuando se lee, y elegir el modo no tiene un tiron.
    if (chopHitsFor != selectedPad) refreshChopHits();

    openSheet (chopSheet, padsButton);
    refreshChopSheet();
}

void MainComponent::refreshChopSheet()
{
    const int fits = chopTargets (chopSlices, chopOnlyEmpty).size();
    //  En GOLPES manda lo que hay en el sonido, no lo que pide el boton: el
    //  numero es un TECHO. Un break con nueve golpes no se corta en dieciseis
    //  por mucho que se pulse dieciseis - saldrian siete trozos partidos por la
    //  mitad de un golpe, que es exactamente lo que este modo viene a evitar.
    const int hits = (chopHitsFor == selectedPad) ? (int) chopHits.size() : 0;
    const int n    = chopByHits ? juce::jmin (fits, hits) : fits;
    const bool can = selectedPad >= 0 && uiSample[(size_t) selectedPad] != nullptr && n >= 2;

    chopGoButton.setEnabled (can);
    chopGoButton.setButtonText (can ? T ("CORTAR EN %1", juce::String (n)) : T ("CORTAR"));
    chopSheet.repaint();
}

void MainComponent::applyAutoChop()
{
    if (selectedPad < 0) return;
    auto src = uiSample[(size_t) selectedPad];
    if (src == nullptr) return;

    const int len = src->buffer.getNumSamples();
    const auto targets = chopTargets (chopSlices, chopOnlyEmpty);

    //  Los puntos de corte: por aritmetica o por golpes. En golpes manda lo que
    //  el detector encontro, acotado por los pads que caben.
    if (chopByHits && chopHitsFor != selectedPad) refreshChopHits();
    const bool porGolpes = chopByHits && chopHitsFor == selectedPad && chopHits.size() >= 2;
    const int  n = porGolpes ? juce::jmin (targets.size(), (int) chopHits.size())
                             : targets.size();

    //  Two pieces is the least that is still a chop, and a source shorter than
    //  one sample per piece has nothing to divide.
    if (n < 2 || len < n) return;

    pushUndo (T ("AUTO CHOP"));

    const juce::String baseName = padName[(size_t) selectedPad].isNotEmpty()
                                 ? padName[(size_t) selectedPad] : juce::String ("CHOP");

    for (int k = 0; k < n; ++k)
    {
        const int i  = targets[k];
        //  El ultimo trozo llega hasta el final de la muestra en los dos modos:
        //  con golpes, el trozo que sigue al ultimo ataque es la cola, y
        //  cortarla en el siguiente golpe que no existe la dejaria fuera.
        const int st = porGolpes ? chopHits[(size_t) k]
                                 : (int) ((juce::int64) k * len / n);
        const int en = porGolpes ? (k + 1 < n ? chopHits[(size_t) (k + 1)] : len)
                                 : (int) ((juce::int64) (k + 1) * len / n);
        if (en <= st) continue;

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

    status.setText (porGolpes
                        ? T ("Cortado en %1 golpes - DESHACER para volver", juce::String (n))
                        : n < askedFor
                            ? T ("Cortado en %1 (no cabian %2) - DESHACER para volver",
                                 juce::String (n), juce::String (askedFor))
                            : T ("Cortado en %1 trozos - DESHACER para volver", juce::String (n)),
                    juce::dontSendNotification);
}

int MainComponent::firstEmptyPad() const
{
    //  Inside the bank you are LOOKING at first: a resample or a mic take that
    //  lands in bank D while the face shows bank A is a sound you have to go
    //  hunting for.
    const int base = currentBank * kPadsPerBank;
    for (int i = 0; i < kPadsPerBank; ++i)
        if (! padHasSample[(size_t) (base + i)]) return base + i;

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
        engine.clearPad (slot);
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
    beginBusy (T ("Cargando"));
    loader.loadAsync (juce::URL (f), slot, [this, slot, f] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
    {
        endBusy();

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
    s.setProperty ("swing", engine.getSwing(), nullptr);
    //  Se guarda el INDICE y no las negras por paso: un float en un fichero de
    //  proyecto que luego hay que volver a casar con uno de los cinco valores
    //  es una comparacion de flotantes esperando a fallar por un bit.
    s.setProperty ("gridres", (int) gridSlider.getValue(), nullptr);
    s.setProperty ("bpm", bpmSlider.getValue(), nullptr);
    //  The skin is deliberately NOT captured: it belongs to the person, not
    //  to the song. Old projects that carry one are simply ignored.
    s.setProperty ("focusedFx", focusedFx, nullptr);
    s.setProperty ("selectedPattern", selectedPattern, nullptr);

    juce::ValueTree fx ("FX");
    for (int f = 0; f < kNumFx; ++f)
        for (int pi = 0; pi < 3; ++pi)
            fx.setProperty (juce::String (fxDefs[f].name) + juce::String (pi),
                            fxParams[f * 3 + pi]->getValue(), nullptr);
    //  El XY es parte del proyecto: que efecto estabas tocando y si lo dejaste
    //  fijo o momentaneo. Sin esto, abrir un proyecto te devolvia el panel en
    //  FLT y en momentaneo aunque lo hubieras dejado en el delay y fijo.
    fx.setProperty ("duckPad", engine.getDuckPad(), nullptr);
    fx.setProperty ("xyFx",    xyFx,    nullptr);
    fx.setProperty ("xyLatch", xyLatch, nullptr);
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
        p.setProperty ("corte",   padCut[(size_t) i],     nullptr);
        p.setProperty ("reson",   padReso[(size_t) i],    nullptr);
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
        juce::String steps, notes, vels, rolls;
        for (int st = 0; st < kNumSteps; ++st)
        {
            int mask = 0;
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[(size_t) b][(size_t) st][(size_t) p]) mask |= (1 << p);
            steps << juce::String::toHexString (mask) << " ";

            for (int p = 0; p < kNumPads; ++p)
            {
                notes << engine.getStepNote (b, st, p) << " ";
                vels  << engine.getStepVel  (b, st, p) << " ";
                rolls << engine.getStepRoll (b, st, p) << " ";
            }
        }
        bk.setProperty ("steps", steps.trim(), nullptr);
        bk.setProperty ("notes", notes.trim(), nullptr);
        //  New in this version. A project written before them simply has no
        //  such property, and the loader falls back to full level and one hit
        //  - which is exactly how those patterns already sounded.
        bk.setProperty ("vels",  vels.trim(),  nullptr);
        bk.setProperty ("rolls", rolls.trim(), nullptr);
        banks.addChild (bk, -1, nullptr);
    }
    s.addChild (banks, -1, nullptr);
    return s;
}

void MainComponent::applyState (const juce::ValueTree& s)
{
    if (! s.hasType ("ZATI") && ! s.hasType ("COLORS")) return;   // COLORS: proyectos anteriores al renombrado

    //  NOT the skin. It used to be applied from here, so opening a project
    //  made on another phone repainted your machine to somebody else's taste,
    //  and a session with no skin property reset it on every launch. The
    //  chassis is a preference now; see ZatiColours::loadSkinPreference.
    applySkin();

    bpmSlider.setValue ((double) s.getProperty ("bpm", 120.0), juce::sendNotification);
    //  Straight is the default, so a project written before swing existed
    //  comes back playing exactly as it did.
    swingSlider.setValue ((double) s.getProperty ("swing", 0.5) * 100.0, juce::sendNotification);
    gridSlider.setValue ((double) (int) s.getProperty ("gridres", 2), juce::sendNotification);

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
        //  ...y el estado del panel XY. jlimit porque un proyecto viejo no
        //  tiene la propiedad y getProperty devuelve 0, que es un indice
        //  valido - pero uno guardado por una version con mas efectos no lo
        //  seria.
        engine.setDuckPad (juce::jlimit (-1, kNumPads - 1, (int) fx.getProperty ("duckPad", -1)));
        xyLatch = (bool) fx.getProperty ("xyLatch", false);
        selectXyFx (juce::jlimit (0, kNumFx - 1, (int) fx.getProperty ("xyFx", 0)));
        xyLatchButton.setToggleState (xyLatch, juce::dontSendNotification);
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
            //  Un proyecto guardado antes de que el filtro existiera no lleva
            //  estas dos, y tiene que volver SIN filtrar - abierto del todo -
            //  o sonaria distinto de como se guardo. El cero del array seria
            //  0 Hz, o sea mudo.
            padCut[(size_t) i]  = (float) p.getProperty ("corte", (double) AudioEngine::kFiltOpenHz);
            padReso[(size_t) i] = (float) p.getProperty ("reson", 0.0);
            engine.setPadCutoff (i, padCut[(size_t) i]);
            engine.setPadReso   (i, padReso[(size_t) i]);
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
            //
            //  The LENGTH has to come from the buffer the interface is holding,
            //  not from the engine. engine.getSampleLength reads the pointer
            //  the AUDIO THREAD has adopted, and adoption happens at the top of
            //  a render block - so on a cold start, before the device is open,
            //  it is still null and this whole branch was skipped. Every pad in
            //  a restored session came back playing the entire source file
            //  instead of its slice, which after a chop is sixteen copies of
            //  the same break.
            const int len = uiSample[(size_t) i] != nullptr
                                ? uiSample[(size_t) i]->buffer.getNumSamples()
                                : engine.getSampleLength (i);
            if (len > 0)
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

            juce::StringArray st, nt, vl, rl;
            st.addTokens (bk.getProperty ("steps", "").toString(), " ", "");
            nt.addTokens (bk.getProperty ("notes", "").toString(), " ", "");
            vl.addTokens (bk.getProperty ("vels",  "").toString(), " ", "");
            rl.addTokens (bk.getProperty ("rolls", "").toString(), " ", "");
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
                    engine.setStepVel  (b, s2, p, ni < vl.size() ? vl[ni].getIntValue() : 127);
                    engine.setStepRoll (b, s2, p, ni < rl.size() ? rl[ni].getIntValue() : 1);
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
        if (mixFaders[i] != nullptr) mixFaders[i]->setValue (dbFromGain (padGain[(size_t) i]), juce::dontSendNotification);
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
    //  Guardar es el mismo bucle de 64 ficheros que abrir, y por el mismo
    //  hilo, solo que escribiendo - que en almacenamiento compartido de
    //  Android no es mas barato que leer. Asi que se trocea igual, con dos
    //  reglas que solo tiene este lado:
    //
    //    - la cabecera va AL FINAL. Un project.xml escrito antes que sus
    //      muestras describe pads que todavia no estan en disco, y si la app
    //      muere en mitad del guardado eso es un proyecto que la lista ensena
    //      y que al abrirlo sale a medias.
    //    - un solo trabajo de 64 ficheros a la vez (padsBusy), porque guardar
    //      mientras se abre escribe en la carpeta una mezcla de los dos.
    if (padsBusy()) return;

    const auto name   = ProjectStore::sanitise (rawName);
    const auto folder = ProjectStore::folderFor (name);
    folder.createDirectory();

    beginBusy (T ("Guardando proyecto"));
    padSaveJob = std::make_unique<PadSaveJob>();
    padSaveJob->folder = folder;
    padSaveJob->name   = name;
    setBusyProgress (0.0f);
    stepPadSaveJob();
}

void MainComponent::stepPadSaveJob()
{
    if (padSaveJob == nullptr) return;

    const double t0 = juce::Time::getMillisecondCounterHiRes();

    while (padSaveJob->next < kNumPads
           && juce::Time::getMillisecondCounterHiRes() - t0 < 25.0)
    {
        const int i = padSaveJob->next++;
        const auto dest = ProjectStore::sampleFile (padSaveJob->folder, i);
        if (auto sb = uiSample[(size_t) i]; sb != nullptr && sb->buffer.getNumSamples() > 0)
        {
            if (ProjectStore::writeSample (dest, sb->buffer, sb->sourceSampleRate)) ++padSaveJob->written;
            else                                                                    ++padSaveJob->failed;
        }
        else
        {
            dest.deleteFile();      // pad emptied since the last save
        }
    }

    setBusyProgress ((float) padSaveJob->next / (float) kNumPads);

    if (padSaveJob->next >= kNumPads)
    {
        auto job = std::move (padSaveJob);
        endBusy();
        finishProjectSave (job->name, job->folder, job->written, job->failed);
    }
}

void MainComponent::finishProjectSave (const juce::String& name, const juce::File& folder,
                                       int written, int failed)
{
    //  Write it, then READ IT BACK. replaceWithText returning true is the
    //  filesystem saying it accepted the call, not that the bytes are there:
    //  on Android shared storage it can accept and quietly drop. The only
    //  honest confirmation is a file that exists, is not empty, and parses.
    const auto xml     = captureState().toXmlString();
    const auto xmlFile = folder.getChildFile ("project.xml");
    bool ok = xmlFile.replaceWithText (xml);
    if (ok)
        ok = xmlFile.existsAsFile() && xmlFile.getSize() > 0 && juce::parseXML (xmlFile) != nullptr;

    if (! ok)
    {
        //  Nothing was saved. Say so and say WHERE it tried, because the
        //  answer to this is almost always the folder, not the app.
        status.setText (T ("NO se pudo guardar en %1", Lang::ltr (folder.getFullPathName())),
                        juce::dontSendNotification);
        setSheet.repaint();
        return;
    }

    currentProject = name;
    repaint (headerArea);
    refreshProjectList();

    status.setText (failed == 0
                        ? T ("Guardado \"%1\"  [%2 pads]", name, juce::String (written))
                        : T ("Guardado con fallos: %1 pads no se escribieron", juce::String (failed)),
                    juce::dontSendNotification);
    setSheet.repaint();
}

void MainComponent::loadProject (const juce::String& name)
{
    if (padsBusy()) return;

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

    //  Igual que la sesion: las muestras por trozos, y el estado al final.
    const auto tree = juce::ValueTree::fromXml (*xml);
    beginBusy (T ("Abriendo proyecto"));
    padJob = std::make_unique<PadLoadJob>();
    padJob->folder = folder;
    padJob->clearMissing = true;
    padJob->onDone = [this, name, tree] (int restored) { finishProjectOpen (name, tree, restored); };
    setBusyProgress (0.0f);
    stepPadJob();
}

void MainComponent::finishProjectOpen (const juce::String& name, const juce::ValueTree& tree, int restored)
{
    int missing = 0;
    applyState (tree);

    // Names live in the state, so re-stamp the tiles after applyState.
    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i], padStart01[(size_t) i], padEnd01[(size_t) i]);

    for (const auto& c : tree.getChildWithName ("PADS"))
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
    setSheet.repaint();
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
        engine.clearPad (i);            // NUEVO has to empty the engine too
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

    //  Keep the box showing what GUARDAR would do if you pressed it now.
    //  Only when it is not being typed in - taking the caret away from
    //  somebody mid-word is worse than a stale suggestion.
    if (! projNameBox.hasKeyboardFocus (true))
        projNameBox.setText (currentProject, juce::dontSendNotification);
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
    g.drawText (T ("SONG"), songSheet.sheetBounds.reduced (14, 10).removeFromTop (16), Lang::start());

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
    g.drawText (engine.anySolo() ? juce::String (juce::CharPointer_UTF8 ("MIX  \xc2\xb7  SOLO ACTIVO")) : juce::String ("MIX"),
                mixSheet.sheetBounds.reduced (14, 12).removeFromTop (16), Lang::start());
}

//  The chip and the name belong to the ROW, so they are painted by the panel
//  the rows live in - in its coordinates, which scroll with them. Painted on
//  the sheet behind the sliders, as they were, they stayed put while the
//  strips moved and every channel ended up wearing another channel's name.
void MainComponent::paintMixRows (juce::Graphics& g)
{
    //  Only the bank on show. Painting all sixty-four drew the chip, number and
    //  name of forty-eight strips whose sliders are hidden - at whatever
    //  coordinates they were left holding - straight over the sixteen in front.
    for (int i = mixBank * kPadsPerBank; i < (mixBank + 1) * kPadsPerBank; ++i)
    {
        if (mixFaders[i] == nullptr) continue;
        const auto fr = mixFaders[i]->getBounds();
        const auto frag = Zati::colour (padZati[(size_t) i]);
        const bool has = padHasSample[(size_t) i];

        auto chip = juce::Rectangle<int> (4, fr.getY() + 4, 22, fr.getHeight() - 8);
        //  El chip de un canal sin sonido: marca medida contra la tarjeta, no
        //  el borde de la placa de pads - que en carcasa oscura sale mas claro
        //  que el fondo y hace que un canal vacio destaque mas que uno cargado.
        g.setColour (has ? frag : ZatiColours::markOn (ZatiColours::chassisTop, 0.20f));
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
                    Lang::start(), true);
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
//  El titulo de la ficha XY dice las tres cosas que hay que saber sin tocar
//  nada: que efecto estas tocando, si esta sonando ahora mismo, y que hace
//  soltar el dedo. La ultima es la que decide si te atreves a usarlo en medio
//  de un tema.
void MainComponent::paintXySheetContent (juce::Graphics& g)
{
    //  El panel pinta su propia tarjeta: no hay Sheet debajo que la dibuje, y
    //  no la hay a proposito - una tarjeta con velo se traga los toques que
    //  van a los pads, que es justo lo que este panel no puede hacer.
    const auto card = xyPanel.getLocalBounds().toFloat();
    if (card.isEmpty()) return;
    constexpr float rad = 2.0f;

    g.setColour (ZatiColours::groove (0.55f));
    g.fillRoundedRectangle (card.translated (0.0f, 4.0f).withTrimmedBottom (4.0f), rad);
    g.setColour (ZatiColours::chassisTop);
    g.fillRoundedRectangle (card, rad);
    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.drawRoundedRectangle (card.reduced (0.75f), rad, 1.5f);

    const juce::String dot = juce::String::charToString ((juce::juce_wchar) 0x00B7);
    auto inner = xyPanel.getLocalBounds().reduced (Metrics::lg, Metrics::md);

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    //  Se para antes del interruptor, que ahora comparte fila con el. Sin el
    //  tope, "XY - DLY - EN ESPERA" en arabe pasa por debajo de MOMENTANEO.
    {
        auto row = inner.removeFromTop (16);
        if (xyLatchButton.isVisible() && ! xyLatchButton.getBounds().isEmpty())
            row.setRight (juce::jmin (row.getRight(), xyLatchButton.getX() - Metrics::xs));
        g.drawText (T ("XY") + "  " + dot + "  " + juce::String (fxDefs[xyFx].name)
                      + "  " + dot + "  " + (fxOn[(size_t) xyFx] ? T ("SUENA") : T ("EN ESPERA")),
                    row, Lang::start(), true);
    }

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    g.drawText (xyLatch ? T ("se queda donde lo dejes")
                        : T ("entra al tocar y sale al soltar"),
                inner.removeFromTop (14), Lang::start());

}


//  ALTO DEL CONTENIDO y PINTADO, con la MISMA cuenta.
//
//  Son dos funciones y tienen que estar de acuerdo o el desplazamiento se queda
//  corto y el ultimo capitulo no se puede leer. Por eso las dos recorren la
//  misma tabla con las mismas alturas, en vez de que una sume constantes y la
//  otra dibuje lo que le parezca.
int MainComponent::manualContentHeight (int width) const
{
    juce::ignoreUnused (width);
    int h = Metrics::sm;
    for (const auto& ch : kManual)
    {
        h += kManualTitleH;
        for (const char* l : ch.lines)
            if (l != nullptr) h += kManualLineH;
        h += kManualGap;
    }
    return h + Metrics::md;
}

void MainComponent::paintManualBody (juce::Graphics& g)
{
    auto r = manualBody.getLocalBounds().reduced (Metrics::sm, 0);
    r.removeFromTop (Metrics::sm);

    for (int c = 0; c < kManualChapters; ++c)
    {
        const auto& ch = kManual[(size_t) c];

        //  El titulo del capitulo con su filete, igual que las secciones de
        //  las fichas: asi el manual se lee como parte de la misma maquina.
        auto band = r.removeFromTop (kManualTitleH);
        const auto secText = T (ch.title);
        g.setColour (ZatiColours::ink.withAlpha (0.55f));
        g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.22f));
        g.drawText (secText, band, Lang::start());

        const float tw = juce::GlyphArrangement::getStringWidth (
                             ZatiColours::labelFont (Metrics::fMeta, 0.22f), secText);
        const float ly = (float) band.getCentreY() + 1.0f;
        g.setColour (ZatiColours::ink.withAlpha (0.18f));
        //  El filete sale por el lado por el que se lee, no siempre por la
        //  derecha: en arabe la linea va al reves y un filete a la derecha
        //  cruzaria por encima del titulo.
        auto ruleRow = band;
        const auto rule = Lang::takeEnd (ruleRow, juce::jmax (0, band.getWidth() - (int) tw - 8));
        g.fillRect ((float) rule.getX(), ly, (float) rule.getWidth(), 1.0f);

        for (const char* line : ch.lines)
        {
            if (line == nullptr) continue;
            auto row = r.removeFromTop (kManualLineH);

            //  El punto de color del capitulo, delante de cada linea: es lo
            //  unico que hace que un capitulo se vea como un bloque cuando lo
            //  que hay debajo es una lista de frases sueltas.
            auto dot = Lang::takeStart (row, 14);
            g.setColour (Zati::colour (c).withAlpha (0.9f));
            g.fillEllipse ((float) dot.getX() + 2.0f, (float) dot.getCentreY() - 2.5f, 5.0f, 5.0f);

            g.setColour (ZatiColours::ink.withAlpha (0.92f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta));
            g.drawFittedText (T (line), row.reduced (2, 2), Lang::start(), 2, 0.9f);
        }

        r.removeFromTop (kManualGap);
    }
}


// ============================================================================
//  LA BARRA DE TRABAJO: que se esta haciendo, cuanto lleva y cuanto falta.
//
//  Una espera de dos segundos sin nada que se mueva y una app colgada se ven
//  exactamente igual. Y aqui NINGUNA de las esperas congela la interfaz - la
//  decodificacion, la limpieza y la exportacion corren en otro hilo -, asi que
//  lo unico que faltaba era decirlo.
//
//  Se cuenta con un CONTADOR y no con una bandera: cargar un kit lanza una
//  decodificacion por fichero y la barra tiene que seguir puesta hasta la
//  ultima, no irse con la primera que termine.
// ============================================================================
void MainComponent::beginBusy (const juce::String& what)
{
    if (busyJobs == 0)
    {
        busyStartMs  = juce::Time::getMillisecondCounterHiRes();
        busyProgress = -1.0f;
        busyWhat     = what;
    }
    else if (what.isNotEmpty())
    {
        busyWhat = what;   // lo ultimo que se empezo es lo que se cuenta
    }

    ++busyJobs;
    busyBar.setVisible (true);
    busyBar.toFront (false);
    busyBar.repaint();
}

void MainComponent::setBusyProgress (float p)
{
    busyProgress = (p >= 0.0f && p <= 1.0f) ? p : -1.0f;
    busyBar.repaint();
}

void MainComponent::endBusy()
{
    busyJobs = juce::jmax (0, busyJobs - 1);
    if (busyJobs == 0) { busyProgress = -1.0f; busyBar.setVisible (false); }
    busyBar.repaint();
}

void MainComponent::paintBusy (juce::Graphics& g)
{
    if (busyJobs <= 0 || busyBar.getWidth() < 40) return;

    const auto r = busyBar.getLocalBounds().toFloat();

    //  Sobre el cristal de la pantalla y con sus colores: la maquina ya habla
    //  ahi, y una tarjeta de otro color encima seria un cartel del sistema
    //  operativo pegado sobre un instrumento.
    //  Opaca del todo: al 94% se leia el tempo por debajo del rotulo, y dos
    //  textos superpuestos es exactamente lo que esta barra viene a evitar.
    g.setColour (ZatiColours::screenBg);
    g.fillRoundedRectangle (r, 2.0f);
    g.setColour (ZatiColours::lcdFg.withAlpha (0.30f));
    g.drawRoundedRectangle (r.reduced (0.5f), 2.0f, 1.0f);

    auto in = busyBar.getLocalBounds().reduced (Metrics::sm, 5);

    //  Lo que lleva, en segundos. Es el numero que convierte "esto no responde"
    //  en "esto esta tardando", y son dos cosas distintas.
    const double secs = juce::jmax (0.0, (juce::Time::getMillisecondCounterHiRes() - busyStartMs) / 1000.0);
    const auto   time = Lang::ltr (juce::String (secs, 1) + " s");

    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    const int timeW = (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), time)) + 6;
    auto timeCell = Lang::takeEnd (in, timeW);

    auto label = in.removeFromTop (14);
    g.setColour (ZatiColours::lcdFg);
    g.drawText (busyWhat, label, Lang::start(), true);
    g.setColour (ZatiColours::lcdDim);
    g.drawText (time, timeCell.withHeight (14).withY (label.getY()), Lang::end());

    //  La barra. Con progreso cuando se sabe - exportar y cargar un kit lo
    //  saben - y un bloque que va y viene cuando no: decodificar no puede
    //  decir cuanto falta sin mentir, y una barra que miente es peor que una
    //  que solo dice "sigo aqui".
    auto bar = in.removeFromTop (6);
    if (bar.getWidth() < 8) return;

    g.setColour (ZatiColours::lcdFg.withAlpha (0.16f));
    g.fillRoundedRectangle (bar.toFloat(), 1.5f);

    if (busyProgress >= 0.0f)
    {
        auto done = bar.withWidth ((int) ((float) bar.getWidth() * juce::jlimit (0.0f, 1.0f, busyProgress)));
        //  Con la tinta DE LA PANTALLA, no con el acento del chasis: en PAPEL
        //  el acento es casi negro, y una barra casi negra sobre el cristal
        //  oscuro es una barra que no se ve. Cada superficie con su tinta.
        g.setColour (ZatiColours::lcdFg);
        g.fillRoundedRectangle (done.toFloat(), 1.5f);
    }
    else
    {
        //  Dos segundos por vuelta, y el bloque mide un quinto: lo bastante
        //  lento como para no parecer nervioso y lo bastante rapido como para
        //  que se vea que se mueve en la primera mirada.
        const float t   = (float) std::fmod (secs, 2.0) / 2.0f;
        const float w   = (float) bar.getWidth() * 0.2f;
        const float ease = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::twoPi);
        const float x   = (float) bar.getX() + ease * ((float) bar.getWidth() - w);
        g.setColour (ZatiColours::lcdFg);
        g.fillRoundedRectangle (x, (float) bar.getY(), w, (float) bar.getHeight(), 1.5f);
    }
}

void MainComponent::paintManualSheetContent (juce::Graphics& g)
{
    if (manualSheet.sheetBounds.isEmpty()) return;

    auto inner = manualSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    auto titleRow = inner.removeFromTop (16);
    //  Se para antes del boton de cerrar, como todas las demas fichas.
    titleRow.setRight (juce::jmin (titleRow.getRight(), manualCloseButton.getX() - Metrics::xs));

    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("MANUAL"), titleRow, Lang::start(), true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.10f));
    g.drawText (T ("lo que hay que saber, en ocho capitulos"),
                inner.removeFromTop (14), Lang::start());
}

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
                titleRow, Lang::start(), true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    {
        //  Y se para antes del boton de cerrar. El titulo si lo hacia y esto
        //  no, y el boton mide 40 px sobre una fila pintada de 32: la primera
        //  linea - "...y los reparte por los pads." - pasaba por debajo de la
        //  x. El banco no puede verlo, porque mide componentes y esto es
        //  texto pintado a mano.
        auto para = inner.removeFromTop (40);
        para.setRight (juce::jmin (para.getRight(), chopCloseButton.getX() - Metrics::xs));
        //  La explicacion cambia con el modo, porque lo que hace el boton
        //  cambia: dejar la de trozos iguales puesta en modo GOLPES seria la
        //  ficha describiendo lo que hacia antes.
        g.drawFittedText (chopByHits
                              ? T ("Busca donde empieza cada golpe y corta ahi, no a intervalos "
                                   "iguales. El pad de origen se queda con el primero.")
                              : T ("Parte este sample en trozos iguales y los reparte por los pads. "
                                   "El pad de origen se queda con el primero."),
                          para, Lang::start (juce::Justification::top), 3, 1.0f);
    }

    inner.removeFromTop (Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.75f));
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
    g.drawText (T ("COMO"), inner.removeFromTop (14), Lang::start());
    inner.removeFromTop (Metrics::hit + Metrics::md);

    g.setColour (ZatiColours::ink.withAlpha (0.75f));
    g.setFont (ZatiColours::labelFont (Metrics::fMeta, 0.16f));
    //  En GOLPES el numero es un TECHO y el rotulo lo dice, porque un boton que
    //  pone 16 y produce 9 trozos parece roto si nadie lo explica.
    g.drawText (chopByHits ? T ("TROZOS (como mucho)") : T ("TROZOS"),
                inner.removeFromTop (14), Lang::start());

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
                          planned, Lang::start (juce::Justification::top), 1, 0.8f);
        return;
    }

    const int hits = (chopHitsFor == sp) ? (int) chopHits.size() : 0;
    const int n = chopByHits ? juce::jmin (targets.size(), hits) : targets.size();

    juce::StringArray nums;
    for (int i = 0; i < n; ++i)
        nums.add (juce::String (targets[i] + 1).paddedLeft ('0', 2));

    int overwritten = 0;
    for (int i = 1; i < n; ++i)
        if (padHasSample[(size_t) targets[i]]) ++overwritten;

    //  Cuantos golpes hay ahi dentro, que es la unica cifra que dice si este
    //  modo tiene algo que hacer con este sonido: un pad de un solo golpe no se
    //  trocea por golpes por mucho que se pida.
    if (chopByHits)
    {
        g.setColour (hits >= 2 ? ZatiColours::inkDim : ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
        g.drawFittedText (hits >= 2 ? T ("%1 golpes encontrados", juce::String (hits))
                                    : T ("no hay golpes que separar aqui"),
                          planned.removeFromTop (18), Lang::start (juce::Justification::top), 1, 0.8f);
    }

    g.setColour (ZatiColours::ink.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
    g.drawFittedText (T ("va a pads: %1", nums.joinIntoString (" ")),
                      planned.removeFromTop (22), Lang::start (juce::Justification::top), 2, 0.8f);

    juce::String warn;
    if (! chopByHits && targets.size() < chopSlices)
        warn = "solo caben " + juce::String (targets.size()) + " sin pisar nada";
    else if (overwritten > 0)
        warn = "PISA " + juce::String (overwritten) + (overwritten == 1 ? " pad con sonido" : " pads con sonido");
    else
        warn = "no pisa ningun pad con sonido";

    g.setColour (overwritten > 0 ? ZatiColours::red : ZatiColours::inkDim);
    g.drawFittedText (warn, planned, Lang::start (juce::Justification::top), 1, 0.8f);
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
                titleRow, Lang::start(), true);

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    g.drawFittedText (T ("cuanto de este pad entra en cada efecto"),
                      inner.removeFromTop (14), Lang::start(), 1, 0.75f);

    for (int f = 0; f < kNumFx; ++f)
    {
        if (rackSends[f] == nullptr) continue;
        const auto r = rackSends[f]->getBounds();
        const bool on = fxOn[(size_t) f];

        g.setColour (on ? ZatiColours::ink : ZatiColours::inkDim.withAlpha (0.55f));
        g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.10f));
        g.drawText (fxDefs[f].name, rackSheet.sheetBounds.getX() + Metrics::lg, r.getY(),
                    50, r.getHeight(), Lang::start());

        //  An effect that is switched off is not hidden, it is greyed: the
        //  send you set now is the send it will use when you switch it on.
        rackSends[f]->setAlpha (on ? 1.0f : 0.5f);
    }
}

//  The AUDIO card. What the device is doing, and the language it says it in.
void MainComponent::paintAudioSheetContent (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    auto inner = setSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("AUDIO"), inner.removeFromTop (16), Lang::start());

    paintAudioInfo (g, audioInfoArea);

    //  Row names for the three chip rows. The asterisk marks the driver's own
    //  burst size: on Android that is the fast path, and anything below it
    //  buys nothing.
    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.12f));
    if (! bufRowArea.isEmpty())
        { auto r = bufRowArea;  g.drawText (T ("BUFER"),  Lang::takeStart (r, 44), Lang::start()); }
    if (! rateRowArea.isEmpty())
        { auto r = rateRowArea; g.drawText (T ("RELOJ"),  Lang::takeStart (r, 44), Lang::start()); }
    if (! langRowArea.isEmpty())
        { auto r = langRowArea; g.drawText (T ("IDIOMA"), Lang::takeStart (r, 44), Lang::start()); }
    if (! skinRowArea.isEmpty())
        { auto r = skinRowArea; g.drawText (T ("CARCASA"), Lang::takeStart (r, 44), Lang::start()); }
}

//  THE GESTURES PAGE.
//
//  Printed like the legend on a machine's lid: the gesture on the left in the
//  ink that names things, what it does on the right in the ink that says them.
//  No controls at all - there is nothing here to set, only something to know -
//  so it is one paint call and no components.
//
//  Six rows, and the list is short on purpose. A machine with thirty hidden
//  gestures has none, because nobody can hold thirty; these are the six that
//  save a sheet or a trip across the face while something is playing.
void MainComponent::paintGesturesPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (area.isEmpty() || setSheet.sheetBounds.isEmpty()) return;

    //  The same title the other two pages carry, in the same place. A page of
    //  a card that skips it reads as a different card.
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("GESTOS"), setSheet.sheetBounds.reduced (14, 12).removeFromTop (16), Lang::start());

    struct Row { const char* how; const char* what; };
    const Row rows[kNumGestures] =
    {
        { "MANTEN UN PAD",      "abre sus ajustes sin sonar" },
        { "MANTEN UN EFECTO",   "coge los mandos sin apagarlo" },
        { "MANTEN CARGAR",      "abre la biblioteca en el pad elegido" },
        { "MANTEN PLAY",        "para y corta todo lo que suene" },
        { "ARRASTRA LA PANTALLA", "cambia de patron" },
        { "GOLPEA ARRIBA O ABAJO", "toca mas fuerte o mas flojo" },
    };

    const int rowH = juce::jmax (24, area.getHeight() / kNumGestures);

    for (int i = 0; i < kNumGestures; ++i)
    {
        auto r = area.removeFromTop (rowH);

        //  A hairline between rows, not a box around each: the page is a list
        //  on a card, and boxes would make six cards out of it.
        if (i > 0)
        {
            g.setColour (ZatiColours::ink.withAlpha (0.10f));
            g.fillRect (r.getX(), r.getY(), r.getWidth(), 1);
        }

        auto text = r.reduced (2, 0);

        g.setColour (ZatiColours::ink.withAlpha (0.92f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.06f));
        //  The gesture takes the width its own words need, and what it does
        //  takes the rest - a fixed split put "GOLPEA ARRIBA O ABAJO" over two
        //  lines in Spanish and left half the row empty in English.
        const int howW = juce::jlimit (90, text.getWidth() * 3 / 5,
                                       (int) juce::GlyphArrangement::getStringWidth (g.getCurrentFont(),
                                                                                     T (rows[i].how)) + 10);
        auto howCell = Lang::takeStart (text, howW);
        g.drawFittedText (T (rows[i].how), howCell, Lang::start(), 2, 0.9f);

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fFine));
        g.drawFittedText (T (rows[i].what), text, Lang::start(), 2, 0.85f);
    }
}

//  The PROJECTS card. The name, where it lives, and the list.
void MainComponent::paintProjSheetContent (juce::Graphics& g)
{
    if (setSheet.sheetBounds.isEmpty()) return;

    auto inner = setSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("PROYECTOS"), inner.removeFromTop (16), Lang::start());

    g.setColour (ZatiColours::inkDim);
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
    //  The close button hangs off the end of this same band, so the subtitle
    //  has to stop before it starts.
    //  Stop before the corner keys - on whichever side they are. In Arabic
    //  the close button is on the LEFT, and clamping the right edge to its x
    //  collapsed the row to nothing and the subtitle vanished entirely.
    auto subRow = inner.removeFromTop (14);
    if (Lang::isRightToLeft (Lang::current()))
        subRow.setLeft (juce::jmax (subRow.getX(), setCloseButton.getRight() + Metrics::xs));
    else
        subRow.setRight (juce::jmin (subRow.getRight(), setCloseButton.getX() - Metrics::xs));
    g.drawFittedText (currentProject.isNotEmpty()
                          ? T ("abierto: %1", currentProject)
                          : (projModel.names.isEmpty()
                                 ? T ("sin proyectos - GUARDAR crea el primero")
                                 : T ("elige uno de la lista")),
                      subRow, Lang::start(), 1, 0.8f);

    //  NAME, and under it the folder these projects actually live in. The
    //  path is there because when a save goes missing the answer is almost
    //  always "it went somewhere else", and until now there was no way to see
    //  where that was from inside the app.
    if (! projNameRowArea.isEmpty())
    {
        auto r = projNameRowArea;
        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
        g.drawText (T ("NOMBRE"), Lang::takeStart (r, 60), Lang::start());
    }
    if (! projPathRowArea.isEmpty())
    {
        auto r = projPathRowArea;
        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
        g.drawText (T ("CARPETA"), Lang::takeStart (r, 60), Lang::start());
        g.drawFittedText (Lang::ltr (ProjectStore::root().getFullPathName()),
                          r, Lang::start(), 1, 0.7f);
    }
}

// ---------------------------------------------------------------------------
//  Export — the bounce
// ---------------------------------------------------------------------------

juce::String MainComponent::exportSourceLabel() const
{
    if (engine.isSongMode())        return T ("CANCION");
    if (engine.getChainLength() > 0) return T ("CADENA (%1 patrones)", juce::String (engine.getChainLength()));
    return T ("PATRON P%1", juce::String (engine.getEditPattern() + 1));
}

void MainComponent::startExport (bool stems)
{
    if (exportJob != nullptr) return;

    if (engine.lengthInSteps() <= 0 || ! engine.hasContentToRender())
    {
        exportOk = false;
        exportStatus = T ("no hay nada grabado en %1", exportSourceLabel().toLowerCase());
        exportSheet.repaint();
        return;
    }

    auto base = (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI"))
                  .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ")
                  .trim().replaceCharacter (' ', '-');
    if (base.isEmpty()) base = "ZATI";

    exportOk = false;
    exportStatus = "renderizando...";
    beginBusy (T ("Exportando"));
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
    if (setSheet.isVisible()) setSheet.repaint();
    if (measuring && ! engine.isProbing()) finishMeasure();

    if (exportJob == nullptr) return;

    if (! exportJob->finished.load (std::memory_order_acquire))
    {
        exportSheet.repaint();
        return;
    }

    exportOk     = exportJob->resultOk;
    exportStatus = exportJob->resultText;
    endBusy();
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
    g.drawText (T ("EXPORTAR"), inner.removeFromTop (18), Lang::start());
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
        g.drawText (k, Lang::takeStart (r, 76), Lang::start());
        g.setColour (vc);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, true));
        g.drawText (v, r, Lang::start());
    };

    line (T ("fuente"), exportSourceLabel(), ZatiColours::ink);
    line (T ("duracion"), steps > 0 ? Lang::ltr (juce::String (secs, 1) + " s") + "  ·  "
                                        + T ("%1 compases", juce::String (steps / 16))
                                    : T ("vacio"),
          steps > 0 ? ZatiColours::ink : ZatiColours::red);
    line (T ("pistas"), T ("%1 pads con muestra", juce::String (loaded)), ZatiColours::ink);
    line (T ("destino"), Lang::ltr ("ZATI/Exports/" + (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI"))),
          ZatiColours::inkDim);

    inner.removeFromTop (6);

    // Progress, then the verdict.
    if (exportJob != nullptr)
    {
        auto bar = inner.removeFromTop (8).reduced (0, 2);
        //  El canal de la barra de progreso es un hueco; lo que lo llena es el
        //  acento. Con padBorder el canal salia mas claro que el relleno en las
        //  dos carcasas oscuras, y la barra parecia ir al reves.
        g.setColour (ZatiColours::groove (0.35f));
        g.fillRect (bar);
        g.setColour (ZatiColours::accent);
        g.fillRect (bar.withWidth ((int) ((float) bar.getWidth()
                        * juce::jlimit (0.0f, 1.0f, exportJob->progress.load (std::memory_order_relaxed)))));

        g.setColour (ZatiColours::inkDim);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawText (T ("escribiendo %1", juce::String (exportJob->passDone.load (std::memory_order_relaxed) + 1)
                                       + "/" + juce::String (exportJob->passTotal.load (std::memory_order_relaxed))),
                    inner.removeFromTop (16), Lang::start());
    }
    else if (exportStatus.isNotEmpty())
    {
        g.setColour (exportOk ? ZatiColours::accent : ZatiColours::red);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
        g.drawFittedText (exportOk ? T ("listo: %1", exportStatus) : exportStatus,
                          inner.removeFromTop (24), Lang::start (juce::Justification::top), 2);
    }
    else
    {
        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
        g.drawFittedText (T ("MASTER = un WAV con lo que oyes.  PISTAS = el master mas un WAV "
                             "por pad, para mezclar fuera."),
                          inner.removeFromTop (26), Lang::start (juce::Justification::top), 2);
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
    g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
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
        line (T ("medido"), T ("%1 ms ida y vuelta", juce::String (measuredMs, 1))
                              + (measuredRate > 0.0
                                   ? "  " + Lang::ltr (juce::String (measuredRate / 1000.0, 1) + "k")
                                   : juce::String()),
              measuredMs <= 30.0f ? ZatiColours::lcdFg
            : measuredMs <= 60.0f ? ZatiColours::yellow : ZatiColours::red);

    g.setColour (ZatiColours::lcdDim.withAlpha (0.85f));
    g.setFont (ZatiColours::monoFont (Metrics::fFine, false));
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
//  Put the user's clock back after a reopen.
//
//  MEDIR needs the microphone, so it reopens the stream as input+output with
//  setAudioChannels(1, 2) - and that goes through AudioDeviceManager::initialise,
//  which builds the device from defaults. The 44.1 kHz you picked was gone
//  before the click was even emitted, so the app measured 48 and told you 48
//  while the chip still said 44.1. The measurement has to be of the thing you
//  actually chose or it is not a measurement.
void MainComponent::keepChosenRate()
{
    if (chosenRate <= 0.0) return;

    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) return;
    if (std::abs (dev->getCurrentSampleRate() - chosenRate) < 0.5) return;

    //  Only if the device can still do it in this configuration: opening an
    //  input can shrink the list of rates on offer, and asking for one that is
    //  gone would fail the whole setup rather than just the rate.
    if (! dev->getAvailableSampleRates().contains (chosenRate)) return;

    auto setup = deviceManager.getAudioDeviceSetup();
    setup.sampleRate = chosenRate;
    deviceManager.setAudioDeviceSetup (setup, true);
}

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
    //
    //  Y EL NUMERO NO SE ADIVINA POR LA FICHA TECNICA, se corrige por lo que
    //  pasa. La gama la decide DeviceTier mirando nucleos y memoria, que es una
    //  suposicion razonable y nada mas: dos moviles con los mismos ocho nucleos
    //  se portan distinto segun lo que este haciendo el sistema al lado. Asi
    //  que ese numero es solo el PUNTO DE PARTIDA, y quien manda es el contador
    //  de under-runs. Ver checkXRuns.
    if (burstMult <= 0)
        burstMult = juce::jlimit (1, kMaxBursts,
                                  juce::jmax (loadBurstPreference(),
                                              DeviceTier::profile().bufferBursts));
    burst *= burstMult;

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

    //  El contador del dispositivo viejo no vale para el nuevo: se vuelve a
    //  empezar, y con unos ticks de gracia porque abrir un stream produce
    //  under-runs propios que no son culpa de nadie.
    lastXRuns = -1;
    xrunsSeen = 0;
    xrunGrace = 12;
}

juce::File MainComponent::burstPreferenceFile()
{
    return ProjectStore::home().getChildFile ("buffer.txt");
}

int MainComponent::loadBurstPreference()
{
    const auto f = burstPreferenceFile();
    return f.existsAsFile() ? f.loadFileAsString().trim().getIntValue() : 0;
}

//  LOS CHASQUIDOS SE CUENTAN Y SE CORRIGEN.
//
//  Un under-run es el hilo de audio llegando tarde: el driver se queda sin
//  bloque, mete silencio o repite el anterior, y eso se oye como un chasquido.
//  Es EL fallo de una app que pide el buffer mas pequeno que hay, y esta lo
//  pedia sin volver a mirar nunca. getXRunCount es un contador acumulado del
//  dispositivo abierto; lo que importa no es su valor sino que CREZCA.
//
//  Cuatro y no uno: abrir el stream, cambiar de ruta o volver de segundo plano
//  producen alguno suelto que no significa nada. Cuatro seguidos con el
//  dispositivo ya asentado significan que este telefono no llega, y entonces
//  se sube un burst - hasta cuatro - y se recuerda, para que el proximo
//  arranque empiece donde este acabo en vez de volver a crepitar para
//  aprender lo mismo.
void MainComponent::checkXRuns()
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) { lastXRuns = -1; return; }

    const int now = dev->getXRunCount();
    if (now < 0) return;                     // el dispositivo no lleva la cuenta

    if (xrunGrace > 0) { --xrunGrace; lastXRuns = now; return; }
    if (lastXRuns < 0) { lastXRuns = now; return; }

    const int nuevos = now - lastXRuns;
    lastXRuns = now;
    if (nuevos <= 0) return;

    xrunsSeen += nuevos;
    if (xrunsSeen < 4 || burstMult >= kMaxBursts) return;

    ++burstMult;
    burstPreferenceFile().replaceWithText (juce::String (burstMult));
    xrunsSeen = 0;
    useLowestLatency();

    //  Y SE DICE. Subir la latencia a espaldas de alguien que eligio esta app
    //  por la latencia es exactamente lo que no se puede hacer en silencio.
    status.setText (T ("Audio entrecortado - buffer a %1 muestras",
                       Lang::ltr (juce::String (deviceManager.getCurrentAudioDevice() != nullptr
                                                    ? deviceManager.getCurrentAudioDevice()->getCurrentBufferSizeSamples()
                                                    : 0))),
                    juce::dontSendNotification);
    refreshDeviceStatusLine (true);
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
        keepChosenRate();                // ...at the clock YOU picked
        useLowestLatency();
        measuredOutMs = measuredInMs = 0.0f;   // filled in finishMeasure()
        engine.startLatencyProbe();
        setSheet.repaint();
    };

    if (! RP::isRequired (RP::recordAudio) || RP::isGranted (RP::recordAudio))
        begin();
    else
        RP::request (RP::recordAudio, [this, begin] (bool granted)
        {
            if (granted) begin();
            else { measureNote = T ("sin permiso de microfono"); setSheet.repaint(); }
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
    keepChosenRate();
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

    //  ...and at WHAT CLOCK. Opening the microphone can force the driver off
    //  the rate you picked - some phones only capture at 48 - and a latency
    //  in milliseconds means nothing without the rate it was taken at. If it
    //  had to move, the line says so instead of quietly reporting a number
    //  from a configuration you did not choose.
    if (auto* d = deviceManager.getCurrentAudioDevice())
        measuredRate = d->getCurrentSampleRate();

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
//  LA PAGINA DE MIDI. Dos rotulos, y la unica cifra que hace falta saber para
//  enchufar algo: que nota manda cada pad.
void MainComponent::paintMidiPage (juce::Graphics& g, juce::Rectangle<int> area)
{
    //  Los rotulos de los dos bloques, sobre sus tapas. Se pintan desde las
    //  posiciones que dejo resized(), que es de donde salen todas las medidas
    //  de esta cara.
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
    g.setColour (ZatiColours::textOn (ZatiColours::chassisTop).withAlpha (0.55f));

    auto label = [&g] (juce::Rectangle<int> ctrl, const juce::String& text)
    {
        if (ctrl.isEmpty()) return;
        g.drawText (text, ctrl.withY (ctrl.getY() - 14).withHeight (14),
                    juce::Justification::centredLeft, false);
    };
    label (midiOutBtn.getBounds(), T ("MANDAR NOTAS A"));
    label (midiInBtn.getBounds(),  T ("RECIBIR NOTAS DE"));

    if (area.isEmpty()) return;

    //  Y la unica cifra que hace falta: sin ella hay que adivinar por que el
    //  modulo de al lado toca la nota equivocada.
    g.setColour (ZatiColours::textOn (ZatiColours::chassisTop).withAlpha (0.55f));
    g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
    g.drawFittedText (T ("El pad 1 es la nota %1, y de ahi hacia arriba. Canal %2.",
                         Lang::ltr (juce::String (MidiIo::kBaseNote)),
                         Lang::ltr (juce::String (midi.getChannel()))),
                      area, juce::Justification::centredLeft, 2, 1.0f);
}

//  Lo que hay enchufado AHORA. Se vuelve a preguntar cada vez que se abre la
//  pagina: un cable se enchufa con la app abierta, que es justo cuando a nadie
//  se le ocurre reiniciarla.
void MainComponent::refreshMidiDevices()
{
    auto fill = [] (juce::ComboBox& box, const juce::StringArray& names)
    {
        const auto keep = box.getText();
        box.clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i)
            box.addItem (names[i], i + 1);
        //  Se conserva lo elegido si sigue ahi. Sin esto, abrir la pagina
        //  soltaba el aparato que estabas usando.
        for (int i = 0; i < names.size(); ++i)
            if (names[i] == keep) { box.setSelectedId (i + 1, juce::dontSendNotification); return; }
        if (names.size() == 1) box.setSelectedId (1, juce::dontSendNotification);
    };

    fill (midiOutBox, MidiIo::Bridge::outputNames());
    fill (midiInBox,  MidiIo::Bridge::inputNames());

    midiOutBtn.setButtonText (T ("MANDAR"));
    midiInBtn.setButtonText  (T ("RECIBIR"));
    setSheet.repaint();
}

//  Abrir y cerrar los aparatos segun las dos tapas. Es el unico sitio que los
//  toca, y siempre desde el hilo de mensajes.
void MainComponent::applyMidiChoice()
{
    const bool wantOut = midiOutBtn.getToggleState();
    const bool wantIn  = midiInBtn.getToggleState();

    if (wantOut && midiOutBox.getText().isNotEmpty())
    {
        if (! midi.openOutput (midiOutBox.getText()))
        {
            midiOutBtn.setToggleState (false, juce::dontSendNotification);
            status.setText (T ("No se pudo abrir %1", midiOutBox.getText()), juce::dontSendNotification);
        }
    }
    else midi.closeOutput();

    //  El motor solo escribe en la cola cuando hay alguien al otro lado: con
    //  esto apagado, un disparo no cuesta ni un byte de mas en el hilo de
    //  audio.
    engine.setMidiOutEnabled (midi.hasOutput());

    if (wantIn && midiInBox.getText().isNotEmpty())
    {
        if (! midi.openInput (midiInBox.getText()))
        {
            midiInBtn.setToggleState (false, juce::dontSendNotification);
            status.setText (T ("No se pudo abrir %1", midiInBox.getText()), juce::dontSendNotification);
        }
    }
    else midi.closeInput();

    setSheet.repaint();
}

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
            litAccent (*b);
            b->setToggleState (v == curBuf, juce::dontSendNotification);
            b->onClick = [this, v] { applyAudioSetup (v, 0.0); };
            setSheet.addAndMakeVisible (b);
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
        litAccent (*b);
        b->setToggleState (std::abs (r - curRate) < 1.0, juce::dontSendNotification);
        b->onClick = [this, r] { applyAudioSetup (0, r); };
        setSheet.addAndMakeVisible (b);
        rateButtons.add (b);
    }

    resized();
    setSheet.repaint();
}

// Zero means "leave this one alone", so a chip only ever changes its own
// setting. The device is restarted by setAudioDeviceSetup, which calls
// prepareToPlay again — every buffer the engine owns is resized there, so
// nothing downstream has to know this happened.
void MainComponent::applyAudioSetup (int bufferSize, double rate)
{
    auto setup = deviceManager.getAudioDeviceSetup();
    if (bufferSize > 0) setup.bufferSize = bufferSize;
    if (rate > 0.0)   { setup.sampleRate = rate; chosenRate = rate; }

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

//  Copy a picked file into ZATI/Samples, without ever overwriting something
//  already there: a second "kick.wav" becomes "kick 2.wav" rather than
//  quietly replacing the one you had.
void MainComponent::importIntoLibrary (const juce::URL& url)
{
    const auto name = ProjectStore::sanitiseFileName (url.getFileName());
    if (name.isEmpty()) return;

    auto dest = ProjectStore::samples().getChildFile (name);
    if (dest.existsAsFile())
    {
        const auto stem = dest.getFileNameWithoutExtension();
        const auto ext  = dest.getFileExtension();
        for (int n = 2; n < 500 && dest.existsAsFile(); ++n)
            dest = ProjectStore::samples().getChildFile (stem + " " + juce::String (n) + ext);
        if (dest.existsAsFile()) return;
    }

    std::unique_ptr<juce::InputStream> in (url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)));
    if (in == nullptr) return;

    juce::FileOutputStream out (dest);
    if (! out.openedOk()) return;
    out.writeFromInputStream (*in, -1);
    out.flush();

    //  An empty file is worse than no file: it shows up in the browser and
    //  fails when you tap it.
    if (dest.getSize() <= 0) dest.deleteFile();
}

void MainComponent::launchSystemPicker()
{
    if (browseTargetPad < 0) return;
    const int index = browseTargetPad;

    chooser = std::make_unique<juce::FileChooser> (
        T ("Muestra para el pad %1", juce::String (index + 1)),
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

            //  BRING IT INTO THE LIBRARY, do not just read it where it lies.
            //
            //  The system picker hands back a content:// URL that we are
            //  allowed to read once. Load from it and the pad works today and
            //  is empty after a reboot, because the grant is gone and the file
            //  was never ours. Copying it into ZATI/Samples is what makes a
            //  sound part of the instrument instead of a link to somewhere on
            //  the phone - and it is what fills the browser, which is
            //  otherwise a folder tree with nothing in it.
            importIntoLibrary (url);
            beginBusy (T ("Cargando"));
    loader.loadAsync (url, index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
            {
        endBusy();

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

//  Ver browseKitButton. Los audios de la carpeta que se esta viendo, en el
//  mismo orden en que aparecen, repartidos por el banco de delante.
void MainComponent::loadFolderAsKit()
{
    if (browser == nullptr) return;
    auto dir = browser->getRoot();
    if (const auto sel = browser->getSelectedFile (0); sel.existsAsFile())
        dir = sel.getParentDirectory();
    if (! dir.isDirectory()) return;

    //  El mismo filtro que la lista, para que lo que se carga sea lo que se
    //  ve. Ordenado por nombre porque es el orden en que se ve, y porque
    //  "kick 01, kick 02..." es como esta nombrado cualquier kit.
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    files.sort();
    if (files.isEmpty())
    {
        status.setText (T ("No hay audio en esta carpeta"), juce::dontSendNotification);
        return;
    }

    //  Sobrescribe dieciseis pads: pasa por deshacer, como AUTO CHOP.
    pushUndo (T ("CARGAR KIT"));

    const int base = currentBank * kPadsPerBank;
    const int n    = juce::jmin (files.size(), kPadsPerBank);
    closeAllSheets();

    //  Y ESTE SI SABE CUANTO FALTA: son n ficheros y se cuentan los que han
    //  llegado. Una carpeta de dieciseis breaks tarda lo suyo, y es la espera
    //  mas larga que se hace con la app delante.
    beginBusy (T ("Repartiendo kit"));
    setBusyProgress (0.0f);
    auto done = std::make_shared<int> (0);

    for (int i = 0; i < n; ++i)
    {
        const int  slot = base + i;
        const auto f    = files[i];
        //  Uno por uno y en el orden en que se ven. El cargador tiene UN hilo,
        //  asi que dieciseis peticiones se atienden en fila y ninguna se pisa
        //  con otra; lo que no se puede es dar por hecho el orden de llegada,
        //  y por eso cada respuesta lleva su propio slot.
        loader.loadAsync (juce::URL (f), slot, [this, slot, f, n, done] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
        {
            ++(*done);
            setBusyProgress ((float) *done / (float) juce::jmax (1, n));
            if (*done >= n) endBusy();

            if (! ok || sb == nullptr)
            {
                status.setText (T ("No se pudo leer: %1", detail), juce::dontSendNotification);
                return;
            }
            assignSampleToPad (slot, sb, f.getFileName());
        });
    }

    status.setText (T ("Kit de %1 sonidos en el banco %2",
                       juce::String (n),
                       juce::String::charToString ((juce::juce_wchar) ('A' + currentBank))),
                    juce::dontSendNotification);
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
    beginBusy (T ("Cargando"));
    loader.loadAsync (juce::URL (f), index, [this, index, fileName] (bool ok, juce::String detail, SampleBuffer::Ptr sb)
    {
        endBusy();

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

    auto inner = browseSheet.sheetBounds.reduced (Metrics::lg, Metrics::md);
    g.setColour (ZatiColours::ink.withAlpha (0.9f));
    g.setFont (ZatiColours::labelFont (Metrics::fLabel, 0.14f));
    g.drawText (T ("CARGAR EN PAD %1", juce::String (juce::jmax (0, browseTargetPad) + 1)),
                inner.removeFromTop (16), Lang::start());

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
                browseSubRow, Lang::start(), true);
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
//  UNA MAQUINA CON TRABAJO DENTRO.
//
//  Las fotos de la ficha de Play tienen que enseñar lo que hace la caja, y una
//  caja recien abierta son dieciseis huecos grises, una onda vacia y una
//  rejilla en blanco. Esto le pone doce sonidos sinteticos con sus nombres,
//  sus colores y un patron escrito, que es el estado en el que la app se usa.
//
//  Sinteticos y no grabados: un WAV de verdad en el repositorio es peso, es
//  una licencia que aclarar y es una foto que deja de poder rehacerse en
//  cuanto el fichero se mueve. Estos salen de cuatro lineas de matematicas y
//  suenan de verdad - se pueden disparar en la foto y en la app.
//
//  Solo desde el arranque de auditoria. No hay ningun camino desde la interfaz
//  que llegue aqui.
void MainComponent::auditDemo()
{
    struct Piece { const char* name; int zati; int kind; float hz; float decay; };
    //  kind 0 = golpe con tono que cae, 1 = ruido con cuerpo, 2 = ruido corto,
    //  3 = nota mantenida. Doce piezas: una caja de ritmos con su bajo y sus
    //  acordes, que es lo que se monta de verdad.
    //  Dieciseis y no doce: la rejilla que se ve en la foto tiene dieciseis
    //  huecos y cuatro vacios al fondo se leen como una app a medio hacer.
    static const Piece kit[16] =
    {
        { "KICK",  0, 0,  58.0f, 0.30f }, { "SNARE", 1, 1, 190.0f, 0.16f },
        { "HAT",   2, 2,   0.0f, 0.04f }, { "CLAP",  3, 1, 320.0f, 0.12f },
        { "RIM",   4, 2,   0.0f, 0.03f }, { "TOM",   5, 0, 120.0f, 0.22f },
        { "BASS",  6, 3,  55.0f, 0.45f }, { "CHORD", 7, 3, 220.0f, 0.60f },
        { "PERC",  1, 2,   0.0f, 0.06f }, { "RIDE",  2, 2,   0.0f, 0.25f },
        { "VOX",   3, 3, 330.0f, 0.35f }, { "SUB",   6, 0,  42.0f, 0.40f },
        { "STAB",  5, 3, 440.0f, 0.28f }, { "SHAKE", 4, 2,   0.0f, 0.05f },
        { "CONGA", 0, 0, 180.0f, 0.18f }, { "PAD",   7, 3, 165.0f, 0.80f }
    };

    juce::Random rnd (404);
    constexpr double fs = 44100.0;

    //  EN EL BANCO QUE PIDAN, no siempre en el A.  ZATI_BANK=0|1|2|3.
    //
    //  La maqueta escribia sus dieciseis piezas en los pads 1..16 y se
    //  quedaba en el banco A, asi que el banco jamas ha medido B, C ni D - y
    //  ahi vivia el fallo que motiva esto: el canalon de la rejilla de pasos
    //  pintaba el numero del CARRIL, o sea 01..16 en los cuatro bancos,
    //  mientras la cabecera de la misma pista decia "PAD 17". Un fallo que
    //  solo existe fuera del banco A no lo ve una prueba que solo mira el A.
    const int demoBank = juce::jlimit (0, kNumBanks - 1,
                                       UiAudit::env ("ZATI_BANK").getIntValue());
    const int demoBase = demoBank * kPadsPerBank;

    for (int i = demoBase; i < demoBase + 16; ++i)
    {
        const auto& p = kit[(size_t) (i - demoBase)];
        const int len = juce::jmax (1024, (int) (fs * (p.decay * 2.5f)));

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->sourceSampleRate = fs;
        sb->buffer.setSize (1, len);
        float* d = sb->buffer.getWritePointer (0);

        double ph = 0.0;
        for (int n = 0; n < len; ++n)
        {
            const float t   = (float) n / (float) fs;
            const float env = std::exp (-t / juce::jmax (0.005f, p.decay));
            float v = 0.0f;

            switch (p.kind)
            {
                case 0:   // el tono baja mientras cae: eso es un bombo
                {
                    const double f = p.hz * (1.0 + 1.6 * std::exp (-t / 0.03f));
                    ph += 2.0 * juce::MathConstants<double>::pi * f / fs;
                    v = std::sin ((float) ph) * env;
                    break;
                }
                case 1:   // ruido con un cuerpo afinado debajo
                    ph += 2.0 * juce::MathConstants<double>::pi * p.hz / fs;
                    v = (0.6f * (rnd.nextFloat() * 2.0f - 1.0f) + 0.4f * std::sin ((float) ph)) * env;
                    break;
                case 2:   // ruido a secas, y la diferencia lo aclara
                    v = (rnd.nextFloat() * 2.0f - 1.0f) * env;
                    break;
                default:  // nota mantenida con dos armonicos
                    ph += 2.0 * juce::MathConstants<double>::pi * p.hz / fs;
                    v = (std::sin ((float) ph) + 0.4f * std::sin (2.0f * (float) ph)
                                               + 0.2f * std::sin (3.0f * (float) ph)) * env * 0.5f;
                    break;
            }
            d[n] = juce::jlimit (-0.98f, 0.98f, v * 0.9f);
        }

        //  El charles y el rim se aclaran con una diferencia de primer orden,
        //  que es un paso alto de un polo y cuesta una resta.
        if (p.kind == 2)
            for (int n = len - 1; n > 0; --n) d[n] = 0.7f * (d[n] - d[n - 1]);

        assignSampleToPad (i, sb, {});
        padName[(size_t) i] = kit[(size_t) (i - demoBase)].name;
        padZati[(size_t) i] = kit[(size_t) (i - demoBase)].zati;
        if (auto* pad = pads[i])
            pad->setSampleInfo (uiSample[(size_t) i], padName[(size_t) i],
                                padStart01[(size_t) i], padEnd01[(size_t) i]);
    }

    //  Y un patron escrito: cuatro por cuatro con el charles a corcheas y la
    //  caja en el dos y el cuatro. Una rejilla vacia no enseña un secuenciador.
    static const int kicks[]  = { 0, 6, 8, 14 };
    static const int snares[] = { 4, 12 };
    static const int hats[]   = { 0, 2, 4, 6, 8, 10, 12, 14 };
    static const int bass[]   = { 0, 3, 8, 11 };
    //  En el MOTOR y en el espejo de la interfaz. La rejilla dibuja desde
    //  pattern[][][], no desde el motor, asi que escribir solo en el motor
    //  dejaba la foto del secuenciador con la rejilla VACIA - un patron que
    //  suena y no se ve.
    auto write = [this, demoBase] (const int* steps, int n, int pad)
    {
        for (int i = 0; i < n; ++i)
        {
            engine.setStep (0, steps[i], demoBase + pad, true);
            pattern[0][(size_t) steps[i]][(size_t) (demoBase + pad)] = true;
        }
    };
    write (kicks,  (int) (sizeof (kicks)  / sizeof (int)), 0);
    write (snares, (int) (sizeof (snares) / sizeof (int)), 1);
    write (hats,   (int) (sizeof (hats)   / sizeof (int)), 2);
    write (bass,   (int) (sizeof (bass)   / sizeof (int)), 6);

    //  El banco DESPUES de escribir: selectBank vuelve a maquetar y a pedirle
    //  la rejilla al patron, y hacerlo antes dejaba la foto con la rejilla del
    //  banco nuevo dibujada sobre un patron que aun no existia.
    selectBank (demoBank);
    refreshStepGrid();

    selectPad (demoBase);

    //  Y con el aumento puesto, si el banco lo pide: la unica forma de mirar
    //  una foto del zoom es que la sonda pueda ponerlo.
    //  La barra de trabajo, puesta a mano: es la unica forma de mirar una foto
    //  de algo que dura dos segundos.
    //  La tira del medidor, puesta a mano. Igual que la barra de trabajo: en
    //  una maqueta estatica el nivel es cero, y una tira apagada no ensena si
    //  el verde se ve.  ZATI_VU=0.7,1.0 - izquierda,derecha.
    if (const auto v = UiAudit::env ("ZATI_VU"); v.contains (","))
    {
        vuL = (float) v.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        vuR = (float) v.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        //  Y se queda: el primer intento solo llamaba a setVu y la foto salia
        //  con el nivel REAL de la maqueta -los pads de ZATI_DEMO suenan- que
        //  son -16 dB y todo verde. Una sonda que la app pisa a la vuelta
        //  siguiente no es una sonda.
        vuHeld = true;
        spectrum.setVu (vuL, vuR);
    }

    if (const auto b = UiAudit::env ("ZATI_BUSY"); b.isNotEmpty())
    {
        beginBusy (T ("Cargando"));
        if (const double p = b.getDoubleValue(); p > 0.0 && p <= 1.0)
            setBusyProgress ((float) p);
    }

    if (const auto tr = UiAudit::env ("ZATI_TRIM"); tr.contains (","))
    {
        const float a = (float) tr.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        const float b = (float) tr.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        padStart01[(size_t) demoBase] = juce::jlimit (0.0f, 0.99f, a);
        padEnd01[(size_t) demoBase]   = juce::jlimit (padStart01[(size_t) demoBase] + 0.01f, 1.0f, b);
        const int len = padSourceLength (demoBase);
        engine.setPadStart (demoBase, (int) (padStart01[(size_t) demoBase] * (float) len));
        engine.setPadEnd   (demoBase, (int) (padEnd01[(size_t) demoBase]   * (float) len));
        selectPad (demoBase);
    }

    //  Con el aumento puesto y centrado donde lo centra el boton: en el medio
    //  del recorte. Es la unica forma de mirar una foto de esto.
    if (const auto z = UiAudit::env ("ZATI_ZOOM"); z.isNotEmpty())
        waveform.setZoom ((float) z.getDoubleValue(),
                          (padStart01[(size_t) demoBase] + padEnd01[(size_t) demoBase]) * 0.5f);

    repaint();
}

void MainComponent::auditOpen (const juce::String& which)
{
    //  Let the bench ask the ENGINE what it is holding, not just the tile.
    //  A pad that looks loaded and is silent is the failure this whole round
    //  was about, and a dump that only reports the tile cannot see it.
    UiAudit::engineLength = [this] (int pad) { return engine.hasSampleFor (pad) ? 1 : 0; };

    if (which.isEmpty()) return;

    if      (which == "pads") { showPadPage (padPageSound); openSheet (padSheet, padsButton); }
    else if (which == "pad2") { showPadPage (padPageTrim);  openSheet (padSheet, padsButton); }
    else if (which == "pad3") { showPadPage (padPageRig);   openSheet (padSheet, padsButton); }
    else if (which == "sec")  { showSeqPage (seqPageGrid); openSheet (seqSheet, secButton); }
    else if (which == "xy")   { closeAllSheets(); toggleXyPanel(); }
    else if (which == "paso") { showSeqPage (seqPageStep); openSheet (seqSheet, secButton); }
    else if (which == "song") openSheet (songSheet, songButton);
    else if (which == "mix")  { refreshMixStrip(); openSheet (mixSheet, mixButton); }
    else if (which == "set")  { showSetPage (pageAudio);    refreshAudioOptions(); openSheet (setSheet, setButton); }
    else if (which == "proj") { showSetPage (pageProjects); refreshProjectList(); openSheet (setSheet, setButton); }
    else if (which == "gest") { showSetPage (pageGestures); openSheet (setSheet, setButton); }
    else if (which == "midi") { showSetPage (pageMidi); refreshMidiDevices(); openSheet (setSheet, setButton); }
    else if (which == "rack") { rackPad = 0; openSheet (rackSheet, mixButton); refreshRack(); }
    else if (which == "chop")
    {
        //  ZATI_CHOP=golpes abre la ficha en el otro modo. Sin esto el banco
        //  solo puede fotografiar la mitad de la ficha, que es como no medirla:
        //  el modo cambia dos rotulos y una linea del plan.
        chopByHits = UiAudit::env ("ZATI_CHOP") == "golpes";
        openChopSheet();
    }
    else if (which == "manual") { closeAllSheets(); openSheet (manualSheet, setButton); }
    else if (which == "browse") openBrowseForPad (0);
}

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
    //  2500, not 1500: Android allows a few seconds in onPause before it
    //  calls the app hung, and what is being bought with them is the audio of
    //  pads that have no other copy anywhere.
    session.flush (2500);

    shutdownAudio();             // releases the output stream and the mic
    audioFocus.abandon();        // ...and hand the speaker back
    pausedByFocus = false;
    appInForeground = false;
}

void MainComponent::appResumed()
{
    appInForeground = true;
    focusGivenAway  = false;
    audioFocus.request();
    pausedByFocus = false;
    setAudioChannels (0, 2);
    keepChosenRate();
    useLowestLatency();

    //  A transport stranded by a trip to the background.
    //
    //  audioFocusLost remembers that the sequencer was rolling and
    //  audioFocusGained puts it back - but only if it is still the one that
    //  paused, and appSuspended clears that flag. Lose the focus, get
    //  backgrounded before the GAIN arrives, come back: the device returns and
    //  the music does not. Coming to the front is the other place that owes
    //  the answer.
    if (wasRollingBeforeFocus)
    {
        wasRollingBeforeFocus = false;
        engine.setPlaying (true);
        playButton.setToggleState (true, juce::dontSendNotification);
    }

    //  Whatever else happened out there, the master comes back up.
    duckedByFocus = false;
    duckTicksLeft = 0;
    engine.setMasterGain (1.0f);

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

    //  Remember whether the SEQUENCER was rolling, not just whether the audio
    //  device existed. Coming back used to restore the stream and leave the
    //  transport stopped, so after any interruption the app looked alive and
    //  played nothing until you noticed and pressed PLAY again.
    wasRollingBeforeFocus = engine.isPlaying();

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
    pausedByFocus  = ! permanently;
    //  A PERMANENT loss means the speaker belongs to another app until we ask
    //  for it again, which is what coming back to the foreground does. Without
    //  this the revival watchdog below reopened the stream a second later and
    //  played straight over whatever had taken it - the exact behaviour the
    //  focus contract exists to prevent.
    focusGivenAway = permanently;
    if (permanently) wasRollingBeforeFocus = false;

    status.setText (permanently ? T ("Audio cedido a otra app")
                                : T ("En pausa: otra app tiene el audio"),
                    juce::dontSendNotification);
    deviceLine.clear();
}

//  A notification, not an interruption. Turn down, keep playing, come back.
//
//  Nothing is stopped and nothing is released, so there is no rebuilt device
//  to fail and no transport to forget. The only state is one float and the
//  watchdog that guarantees it goes back to one.
void MainComponent::audioFocusDucked()
{
    duckedByFocus = true;
    duckTicksLeft = kDuckWatchdogMs;
    engine.setMasterGain (0.28f);
    status.setText (T ("Bajando un momento por un aviso del sistema"),
                    juce::dontSendNotification);
}

void MainComponent::audioFocusGained()
{
    //  Un-duck first and unconditionally: whatever else is true, the master
    //  must not be left turned down.
    if (duckedByFocus)
    {
        duckedByFocus = false;
        duckTicksLeft = 0;
        engine.setMasterGain (1.0f);
        refreshDeviceStatusLine (true);
    }

    if (! pausedByFocus)
        return;

    pausedByFocus = false;
    setAudioChannels (0, 2);
    keepChosenRate();
    useLowestLatency();

    //  ...and put the sequence back where it was. This is the half that was
    //  missing: the device came back, the music did not.
    if (wasRollingBeforeFocus)
    {
        wasRollingBeforeFocus = false;
        engine.setPlaying (true);
        playButton.setToggleState (true, juce::dontSendNotification);
    }

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

//  UN TROZO DE LECTURA POR VUELTA DEL TEMPORIZADOR.
//
//  Se lee lo que quepa en 25 ms y se suelta: la vuelta siguiente sigue por
//  donde iba. Es tiempo y no numero de ficheros porque los ficheros no miden
//  lo mismo - un charles de 30 KB y un break de 12 MB tardan dos ordenes de
//  magnitud distintos, y "dos por vuelta" seria fluido con los primeros y un
//  tiron con los segundos.

//  Un solo trabajo de 64 ficheros a la vez. Dos a la vez no se estorban en
//  disco, se estorban en los pads: abrir mientras se guarda escribe en la
//  carpeta las muestras del proyecto que se esta cargando encima, mezcladas
//  con las del que se guardaba. Se dice que espere, y se dice cual.
bool MainComponent::padsBusy()
{
    if (padJob == nullptr && padSaveJob == nullptr) return false;
    status.setText (T ("Espera a que termine %1", busyWhat.toLowerCase()),
                    juce::dontSendNotification);
    return true;
}

void MainComponent::stepPadJob()
{
    if (padJob == nullptr) return;

    const double t0 = juce::Time::getMillisecondCounterHiRes();

    while (padJob->next < kNumPads
           && juce::Time::getMillisecondCounterHiRes() - t0 < 25.0)
    {
        const int i = padJob->next++;
        const auto f = padJob->fromSession ? SessionKeeper::padFile (i)
                                           : ProjectStore::sampleFile (padJob->folder, i);

        if (auto sb = ProjectStore::readSample (f))
        {
            assignSampleToPad (i, sb, padName[(size_t) i]);
            ++padJob->restored;
        }
        else if (padJob->clearMissing)
        {
            //  ...y en el motor tambien, o este pad sigue tocando el proyecto
            //  que estaba abierto antes que este.
            uiSample[(size_t) i] = nullptr;
            padHasSample[(size_t) i] = false;
            padName[(size_t) i] = {};
            engine.clearPad (i);
            if (auto* p = pads[i]) p->setSampleInfo (nullptr, {});
        }
    }

    setBusyProgress ((float) padJob->next / (float) kNumPads);

    if (padJob->next >= kNumPads)
    {
        auto done = std::move (padJob->onDone);
        const int restored = padJob->restored;
        padJob.reset();
        endBusy();
        if (done) done (restored);
    }
}

void MainComponent::restoreSession()
{
    if (! SessionKeeper::exists())
    {
        //  PRIMERA VEZ: la maquina viene con sonidos dentro.
        //
        //  Sin esto la app abria con sesenta y cuatro huecos grises y nada
        //  que tocar hasta ir a buscar un fichero, que es lo contrario de lo
        //  que hace cualquiera al abrir un sampler: golpear. Y solo aqui -
        //  cuando NO hay sesion -, porque el dia que la persona ya tiene su
        //  trabajo dentro, meterle la fabrica encima seria borrarselo.
        loadFactoryKits();

        //  Y AQUI NO SE ADOPTA. adopt() significa "esto ya esta en disco, no
        //  hace falta escribirlo", que es verdad para lo que se acaba de LEER
        //  de la sesion y mentira para lo que se acaba de sintetizar. Con el
        //  adopt puesto - que es como estaba - los sesenta y cuatro sonidos no
        //  se escribian nunca: al segundo arranque habia un state.xml con
        //  sesenta y cuatro pads declarados y ni un solo WAV al lado, o sea la
        //  maquina entera vacia y un mensaje diciendo que no habia audio.
        //  Sin adoptar, el temporizador los ve nuevos y el hilo de sesion los
        //  escribe como escribe cualquier otra cosa.
        return;
    }

    auto xml = juce::parseXML (SessionKeeper::stateFile());
    if (xml == nullptr)
    {
        session.adopt (uiSample.data(), kNumPads);
        return;
    }

    const auto tree = juce::ValueTree::fromXml (*xml);

    //  Las muestras, por trozos y con la barra puesta. El resto de la sesion -
    //  el estado, los nombres, el mensaje - va en el remate, cuando estan las
    //  sesenta y cuatro: applyState pisa nombres y recortes, y hacerlo antes
    //  de tener el audio los dejaria a medias.
    beginBusy (T ("Recuperando sesion"));
    padJob = std::make_unique<PadLoadJob>();
    padJob->fromSession = true;
    padJob->onDone = [this, tree] (int restored) { finishSessionRestore (tree, restored); };
    setBusyProgress (0.0f);
    stepPadJob();
}

void MainComponent::finishSessionRestore (const juce::ValueTree& tree, int restored)
{
    applyState (tree);

    //  A pad the state says had a sound, and whose audio did not come back.
    //
    //  This used to be silent. The session would restore, report "recovered",
    //  and hand back a grid of empty pads with no explanation - which is
    //  exactly what "I left the app and the sounds are no longer on the pads"
    //  looks like from the outside. The project loader has always counted
    //  these; the session, which is the copy that matters most because nobody
    //  chose to make it, did not.
    int missing = 0;
    if (auto padsTree = tree.getChildWithName ("PADS"); padsTree.isValid())
        for (const auto& p : padsTree)
        {
            const int i = (int) p.getProperty ("i", -1);
            if (juce::isPositiveAndBelow (i, kNumPads)
                && (bool) p.getProperty ("has", false)
                && uiSample[(size_t) i] == nullptr)
                ++missing;
        }

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

    if (missing > 0)
        status.setText (T ("Sesion recuperada  [%1 pads, %2 sin audio]",
                           juce::String (restored), juce::String (missing)),
                        juce::dontSendNotification);
    else if (restored > 0 || currentProject.isNotEmpty())
        status.setText (currentProject.isNotEmpty()
                            ? T ("Sesion recuperada - %1", currentProject)
                            : (restored == 1 ? T ("Sesion recuperada  [1 pad]")
                                             : T ("Sesion recuperada  [%1 pads]", juce::String (restored))),
                        juce::dontSendNotification);
}

//  RESAMPLE: print the master onto a pad.
//
//  The same recorder the microphone uses, reading the other end of the block.
//  No permission, no input stream, nothing to ask for - the sound is already
//  in our own output buffer. What lands on the pad is what you just heard:
//  the effects, the master saturation, the level, all of it committed, which
//  is the point of doing it at all.
void MainComponent::toggleResample()
{
    if (! resamplingActive)
    {
        if (recordingActive) toggleMicSampling();     // one recorder, one take

        int slot = firstEmptyPad();
        if (slot < 0) slot = (selectedPad >= 0) ? selectedPad : 0;

        resamplingSlot   = slot;
        resamplingActive = true;
        engine.startRecording (slot, true);

        styleButton (resampleButton, kRec);
        resampleButton.setButtonText (T ("PARAR"));
        status.setText (T ("Remuestreando al pad %1", juce::String (slot + 1)),
                        juce::dontSendNotification);
        return;
    }

    resamplingActive = false;
    styleButton (resampleButton, kKey);
    resampleButton.setButtonText (T ("REMUESTREAR"));

    if (auto sb = engine.finishRecording())
    {
        pushUndo (T ("REMUESTREAR"));
        assignSampleToPad (resamplingSlot, sb, "RE " + juce::String (resamplingSlot + 1));
        refreshPad (resamplingSlot);
        refreshPadArt (resamplingSlot);
        session.sync (uiSample.data(), kNumPads);
        status.setText (T ("Pad %1 remuestreado", juce::String (resamplingSlot + 1)),
                        juce::dontSendNotification);
    }
    else
    {
        status.setText (T ("Nada que remuestrear"), juce::dontSendNotification);
    }
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

    //  Re-sync on drift - by RESTARTING the device, not by re-preparing the
    //  engine underneath it.
    //
    //  This used to call engine.prepareToPlay() straight from the timer. That
    //  function resizes padScratch, fxDry, fxBus, recordBuffer and the delay
    //  line, and the function above returns early only when the device is
    //  NULL - so it ran with the stream live and the callback holding raw
    //  pointers into every one of those buffers. A route change is exactly
    //  when it fires. It is a use-after-free on the audio thread, on the one
    //  path this function exists to repair.
    //
    //  inRender stops a second audio callback; it says nothing about the
    //  message thread. The safe way to re-prepare is the one JUCE already
    //  provides: stop the device and let it call prepareToPlay back, which is
    //  what restartLastAudioDevice does.
    if (rate > 0.0 && block > 0
        && (std::abs (rate - enginePreparedRate) > 0.5 || block != enginePreparedBlock))
    {
        ++engineResyncs;
        deviceManager.restartLastAudioDevice();
        keepChosenRate();
        return;                       // prepareToPlay will land on its own
    }

    //  A queue that refused a trigger is a queue that met two consumers. The
    //  lifeboat already carried the tap, so the user heard their pad; this
    //  puts the transport itself back on its feet for the next one.
    if (engine.takeDroppedCommands() > 0)
    {
        deviceManager.restartLastAudioDevice();
        keepChosenRate();
    }
}

void MainComponent::timerCallback()
{
    //  Mientras algo este cargando, la barra se repinta sola: es lo unico de
    //  la cara que tiene que moverse aunque no pase nada mas.
    if (busyJobs > 0) busyBar.repaint();
    stepPadJob();
    stepPadSaveJob();
    checkXRuns();

    //  La exportacion SI sabe cuanto falta - cuenta pasadas y bloques - asi
    //  que la barra deja de ir y venir y dice el numero.
    if (exportJob != nullptr)
        setBusyProgress (exportJob->progress.load (std::memory_order_relaxed));

    //  Once, on the first tick: the face is up by now, so a restore that takes
    //  a second reads as filling in rather than as a hang.
    if (sessionRestorePending)
    {
        sessionRestorePending = false;
        restoreSession();

        //  Y se suelta la cuenta del arranque DESPUES de que restoreSession
        //  haya abierto la suya, o la barra parpadea: llegar a cero apaga el
        //  componente, y volver a uno en la linea siguiente lo enciende otra
        //  vez en el mismo fotograma.
        if (startupBusy) { startupBusy = false; endBusy(); }
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

    //  THE SCREEN DOES NOT GO OUT IN THE MIDDLE OF A TAKE.
    //
    //  On Android this is FLAG_KEEP_SCREEN_ON, and JUCE sets it from
    //  setScreenSaverEnabled. A live instrument that lets the phone lock while
    //  the pattern is rolling is one that stops responding to the pads halfway
    //  through - you look down and the machine is asleep with the sound still
    //  coming out of it.
    //
    //  WHILE IT IS ROLLING OR RECORDING, and not a moment longer. Holding the
    //  flag the whole time the app is open is the same bug in the other
    //  direction: a groovebox left open on the bench would flatten the battery
    //  by itself, and the battery is exactly what went wrong the last time
    //  something in this app assumed nobody was watching the meter.
    {
        const bool busy = engine.isPlaying() || engine.isRecording();
        if (busy != holdingScreenAwake)
        {
            holdingScreenAwake = busy;
            juce::Desktop::getInstance().setScreenSaverEnabled (! busy);
        }
    }

    //  NOTHING MAY LEAVE THIS APP SILENT.
    //
    //  Two ways it could, and both are now bounded by this tick rather than by
    //  a callback arriving from outside.
    //
    //  One: ducked and never told to come back. Android owes us a GAIN after
    //  a CAN_DUCK and some builds never send it. Six seconds is far longer
    //  than any notification and far shorter than a person's patience.
    //  Counted in MILLISECONDS, not in ticks. uiIntervalMs is a device-tier
    //  number and it ranges from 33 to 100, so "100 ticks, about six seconds"
    //  was anything from 3.3 to 10 - un-ducking in the middle of the very
    //  notification it was making room for on a fast phone.
    if (duckedByFocus && (duckTicksLeft -= DeviceTier::profile().uiIntervalMs) <= 0)
    {
        duckedByFocus = false;
        engine.setMasterGain (1.0f);
        refreshDeviceStatusLine (true);
    }

    //  Two: no audio device at all, while the app is in the foreground and is
    //  not deliberately paused. watchAudioDevice used to give up here - it
    //  returns early on a null device - so a stream that failed to come back
    //  after an interruption stayed missing for the rest of the session, with
    //  the face fully alive and nothing coming out.
    //  ...and only while the app is actually in FRONT. appSuspended releases
    //  the stream on purpose, and the timer keeps ticking in the background:
    //  without this the revival would grab the audio device back a second
    //  after you left the app, fight whatever took it, and hand appResumed a
    //  device it did not open.
    if (appInForeground && ! pausedByFocus && ! focusGivenAway
        && deviceManager.getCurrentAudioDevice() == nullptr)
    {
        //  ...same here: one second of wall clock, whatever the tier redraws at.
        if ((deviceRevivalTicks += DeviceTier::profile().uiIntervalMs) >= 1000)
        {
            deviceRevivalTicks = 0;
            setAudioChannels (0, 2);
            keepChosenRate();
            useLowestLatency();
            refreshDeviceStatusLine (true);
        }
    }
    else
    {
        deviceRevivalTicks = 0;
    }

    //  The lamps under the effect keys.
    //
    //  Every effect that is ON breathes, focused or not - that is the whole
    //  point: the one holding the knobs already says so with the wedge in the
    //  seam above it, and what was missing was any sign at all from the ones
    //  still running behind it.
    //
    //  Two beats per cycle at the project's tempo, so the row breathes WITH
    //  the music instead of against it, and six lit keys are in phase with
    //  each other rather than six separate blinkers. Nothing lit means nothing
    //  repainted: the cost of this is zero on a face with no effects on.
    {
        const double periodMs = juce::jlimit (500.0, 3000.0,
                                              2.0 * 60000.0 / juce::jmax (20.0, engine.getBpm()));
        fxPulsePhase += (double) DeviceTier::profile().uiIntervalMs / periodMs;
        if (fxPulsePhase >= 1.0) fxPulsePhase -= std::floor (fxPulsePhase);

        //  A raised cosine: never fully off, so a running effect is lit even
        //  at the bottom of its breath. A lamp that goes dark once a second is
        //  a fault indicator, not a power light.
        const double lit = 0.42 + 0.58 * (0.5 - 0.5 * std::cos (fxPulsePhase * juce::MathConstants<double>::twoPi));

        for (int f = 0; f < kNumFx; ++f)
        {
            auto* b = fxButtons[f];
            if (b == nullptr) continue;

            const double want = fxOn[(size_t) f] ? lit : 0.0;
            const double had  = (double) b->getProperties().getWithDefault ("pulse", 0.0);
            if (std::abs (want - had) < 0.004) continue;

            b->getProperties().set ("pulse", want);
            b->repaint();
        }
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
    const std::uint64_t trig = engine.fetchTriggered();
    bool anyFlash = false;
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((trig & ((std::uint64_t) 1u << i)) != 0) padFlash[(size_t) i] = 1.0f;
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
    if (! vuHeld)
    {
        const float pl = engine.readOutPeakL();
        const float pr = engine.readOutPeakR();
        const float prevL = vuL, prevR = vuR;
        vuL = juce::jmax (pl, vuL * 0.80f); if (vuL < 0.004f) vuL = 0.0f;
        vuR = juce::jmax (pr, vuR * 0.80f); if (vuR < 0.004f) vuR = 0.0f;
        juce::ignoreUnused (prevL, prevR, prevPlayStep);
        spectrum.setVu (vuL, vuR);
        // (the LCD no longer carries a step strip)
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

#include "MainComponentInterno.h"

#include <csignal>
#include <cstdlib>
#include <thread>
#include <vector>

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

    //  LOS DEFECTOS DE UN PAD, EN UN SOLO SITIO. Ver ponPadPorDefecto.
    //
    //  Estaban aqui sueltos, en seis .fill() - unidad de ganancia, recorte
    //  entero, 2 y 5 ms de envolvente, el filtro abierto y el autocorte
    //  puesto - y no habia forma de volver a ellos: cargar un instrumento
    //  nuevo dejaba el pad con la afinacion, el filtro y el choke del sonido
    //  anterior. Escritos dos veces se habrian separado, que es lo mismo que
    //  ya obligo a sacar `normaliza` de dentro de `render`.
    for (int i = 0; i < kNumPads; ++i) ponPadPorDefecto (i);

    //  MENOS UNO es "este dedo no esta tocando nada". El array se
    //  inicializaba a ceros -que es lo que deja `{}`- o sea al PAD 0: levantar
    //  el dedo de un pad que no llego a sonar -con LOAD armado, o vacio-
    //  mandaba un "suelta" al pad 0 y le cortaba la nota a otro.
    notaViva.fill (-1);
    //  ANCHO a uno, o sea "como viene la muestra": el cero del array seria la
    //  maquina entera en mono.
    padAnchoUI.fill (1.0f);

    for (int i = 0; i < kNumPads; ++i)
    {
        auto* p = new PadButton (i);
        padZati[(size_t) i] = Zati::forPad (i);      // cut order: zati 1 is always red
        p->setZati (padZati[(size_t) i]);
        p->onClick = [this, i] { padClicked (i); };
        //  Y en modo tecla, apretar y levantar. Ver PadButton::setModoNota.
        p->onNotaOn  = [this, i] (float vel) { padNotaOn (i, vel); };
        p->onNotaOff = [this, i] { padNotaOff (i); };
        //  ...and holding it edits it. Suena igual, que es lo que este
        //  comentario negaba: ver PadButton::onHold.
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

    //  LA BARRA, QUE SUSTITUYE A LA FILA DE COMPASES.
    //
    //  Eran ocho tapas —1, 2, 3, 4…— y hacian dos cosas mal a la vez: la
    //  ventana saltaba de dieciseis en dieciseis, asi que una figura que cruza
    //  el filo de un compas no se podia mirar entera —«cambiar de pantalla todo
    //  el rato», que fue como se pidio esto—, y un numero no dice cuanto queda
    //  ni por donde va el cabezal cuando lo que suena esta fuera de la vista.
    //  Esa segunda mitad la hacian pintando de rojo la tapa del compas vivo, y
    //  se muda a la barra, que ademas dice DONDE cae sobre el total.
    //
    //  UNA SOLA para las dos paginas, que es lo que ya eran las ocho tapas: el
    //  piano y la rejilla son dos VISTAS del mismo patron, con distinto numero
    //  de columnas, asi que compartir el primer paso es lo que hace que cambiar
    //  de pestaña no te mueva de sitio. Dos variables serian dos reglas.
    seqBarra.onMueve = [this] (int primero)
    {
        seqPrimerPaso = primero;
        //  LA QUE SE ESTA VIENDO, y no siempre la rejilla. La FICHA no es la
        //  PAGINA: el mismo fallo que ya tuvo el cabezal del piano.
        if (seqPage == seqPagePiano) refreshPiano();
        else                         refreshStepGrid();
    };
    seqBarra.setTitle (T ("PASOS"));
    seqSheet.addAndMakeVisible (seqBarra);

    //  Y LA VERTICAL DEL PIANO, que sustituye al salto de OCTAVA de doce en
    //  doce. Recorre `pianoBase` semitono a semitono hasta donde `baseMax`
    //  admite, asi que una melodia que cruza un DO se puede centrar. Donde no
    //  cabe —se lleva `Metrics::hit` de ANCHO de la rejilla, y en las pantallas
    //  estrechas eso deja la columna del paso por debajo de su suelo— se queda
    //  el par de tapas: cada mando en UN sitio en cada pantalla, nunca en dos y
    //  nunca en ninguno, que es la regla de REPETIR y CORTE en la tira.
    pianoBarra.ponEje (true);
    pianoBarra.onMueve = [this] (int primero)
    {
        pianoBase = juce::jlimit (-24, pianoGrid.baseMax(), primero - 24);
        refreshPiano();
    };
    pianoBarra.setTitle (T ("OCTAVA"));
    seqSheet.addAndMakeVisible (pianoBarra);

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

    //  Y COMO SE LLAMA CADA FICHA, en un solo sitio. Ver Sheet::nombre: lo
    //  escribe la caja negra al abrirla, asi que tiene que ser el mismo nombre
    //  con el que el banco la abre (`ZATI_OPEN`) y no el rotulo traducido de la
    //  tapa que la levanta.
    {
        padSheet   .nombre = "pad";      seqSheet   .nombre = "sec";
        mixSheet   .nombre = "mix";      songSheet  .nombre = "song";
        setSheet   .nombre = "set";      rackSheet  .nombre = "rack";
        chopSheet  .nombre = "chop";     browseSheet.nombre = "browse";
        exportSheet.nombre = "exportar"; manualSheet.nombre = "manual";
        tourSheet  .nombre = "tour";     instSheet  .nombre = "inst";
        vstSheet   .nombre = "vst";      padPickSheet.nombre = "padpick";
        canalSheet .nombre = "canal";    ranuraSheet.nombre = "ranura";
        eqBandaSheet.nombre = "eqb";
    }

    //  LA REJILLA DE DIECISEIS PARA ELEGIR PAD. Ver padPickSheet en la cabecera.
    {
        addAndMakeVisible (padPickSheet);
        padPickSheet.setVisible (false);
        padPickSheet.onDismiss    = [this] { abrePadPicker (false); };
        padPickSheet.paintContent = [this] (juce::Graphics& g) { paintPadPickContent (g); };
        styleButton (padPickCloseBtn, kKey);
        padPickCloseBtn.onClick = [this] { abrePadPicker (false); };
        padPickSheet.addAndMakeVisible (padPickCloseBtn);

        for (int i = 0; i < kNumPads; ++i)
        {
            auto* b = new juce::TextButton (juce::String (i + 1).paddedLeft ('0', 2));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            //  Elegir y cerrar: la rejilla existe para llegar al pad de un
            //  gesto, y dejarla abierta despues de acertar seria un segundo
            //  toque para volver a lo que se estaba haciendo.
            b->onClick = [this, i] { selectPad (i); abrePadPicker (false); };
            padPickSheet.addAndMakeVisible (b);
            padPickBtns.add (b);
        }

        for (int b = 0; b < kNumBanks; ++b)
        {
            auto* t = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b)));
            styleButton (*t, kStepOff);
            litAccent (*t);
            t->setClickingTogglesState (true);
            t->setRadioGroupId (5151);
            t->onClick = [this, b] { selectBank (b); refrescaPadPicker(); };
            padPickSheet.addAndMakeVisible (t);
            padPickBankBtns.add (t);
        }

        //  Las dos puertas. La misma tapa en el mismo sitio -a la derecha de la
        //  cabecera- en las dos fichas que editan "el pad que tengas elegido".
        for (auto* b : { &pianoPadPickBtn, &padPadPickBtn, &chopPadPickBtn })
        {
            styleButton (*b, kKey);
            b->onClick = [this] { abrePadPicker (! padPickAbierto); };
        }
        seqSheet.addAndMakeVisible (pianoPadPickBtn);
        padSheet.addAndMakeVisible (padPadPickBtn);
        chopSheet.addAndMakeVisible (chopPadPickBtn);
    }

    //  LA REJILLA DE DIECISEIS PARA ELEGIR CANAL. Ver canalSheet en la cabecera.
    {
        addAndMakeVisible (canalSheet);
        canalSheet.setVisible (false);
        canalSheet.onDismiss    = [this] { abreCanalPicker (false); };
        canalSheet.paintContent = [this] (juce::Graphics& g) { paintCanalPickContent (g); };
        styleButton (canalCloseBtn, kKey);
        canalCloseBtn.onClick = [this] { abreCanalPicker (false); };
        canalSheet.addAndMakeVisible (canalCloseBtn);

        for (int i = 0; i < kNumCanales; ++i)
        {
            auto* b = new juce::TextButton (juce::String (i + 1).paddedLeft ('0', 2));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            //  Elegir y cerrar, como la rejilla de pads: llegar de un gesto es
            //  todo el argumento, y dejarla abierta seria un segundo toque para
            //  volver a lo que se estaba haciendo.
            b->onClick = [this, i]
            {
                pushUndo (T ("CANAL"));
                engine.setPadCanal (selectedPad, i);
                //  Y LA FILA DE LA CARA CON EL, que es para lo que el canal
                //  existe: mover el pad de canal cambia sus seis ranuras.
                canalActual = i;
                refrescaRanuras();
                refrescaCanalDelPad();
                if (rackSheet.isVisible()) refreshRack();
                refreshMixStrip();
                abreCanalPicker (false);
            };
            canalSheet.addAndMakeVisible (b);
            canalBtns.add (b);
        }

        styleButton (padCanalBtn, kKey);
        padCanalBtn.onClick = [this] { abreCanalPicker (! canalPickAbierto); };
        padSheet.addAndMakeVisible (padCanalBtn);
    }

    //  EL MENU DE UNA RANURA. Ver ranuraSheet en la cabecera.
    {
        addAndMakeVisible (ranuraSheet);
        ranuraSheet.setVisible (false);
        ranuraSheet.onDismiss    = [this] { abreMenuRanura (-1); };
        ranuraSheet.paintContent = [this] (juce::Graphics& g) { paintRanuraContent (g); };
        styleButton (ranuraCloseBtn, kKey);
        ranuraCloseBtn.onClick = [this] { abreMenuRanura (-1); };
        ranuraSheet.addAndMakeVisible (ranuraCloseBtn);

        for (int f = 0; f < kNumFx; ++f)
        {
            auto* b = new juce::TextButton (fxDefs[f].name);
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->getProperties().set ("icono", (int) iconoDeFx (f));
            //  Elegir y cerrar, como la rejilla de pads: el menu existe para
            //  llenar la ranura de un gesto, y dejarlo abierto despues de
            //  acertar es un segundo toque para volver a lo que hacias.
            b->onClick = [this, f] { ponEnRanura (ranuraEditada, f); abreMenuRanura (-1); };
            ranuraSheet.addAndMakeVisible (b);
            ranuraBtns.add (b);
        }

        //  VACIAR solo aparece con la ranura LLENA, que es la unica vez que
        //  significa algo. Desde la cara este menu se abre sobre un «+», o sea
        //  vacia, y entonces no esta: un control que no puede hacer nada no es
        //  informacion, es ruido.
        styleButton (ranuraVaciarBtn, kKey);
        ranuraVaciarBtn.onClick = [this]
        {
            ponEnRanura (ranuraEditada, kSlotVacia);
            abreMenuRanura (-1);
        };
        ranuraSheet.addAndMakeVisible (ranuraVaciarBtn);
    }

    //  LA FICHA DE UNA BANDA DEL EQ. Ver eqBandaSheet en la cabecera.
    {
        addAndMakeVisible (eqBandaSheet);
        eqBandaSheet.setVisible (false);
        eqBandaSheet.onDismiss    = [this] { abreBandaEq (-1); };
        eqBandaSheet.paintContent = [this] (juce::Graphics& g) { paintEqBandaContent (g); };
        styleButton (eqBandaCloseBtn, kKey);
        eqBandaCloseBtn.onClick = [this] { abreBandaEq (-1); };
        eqBandaSheet.addAndMakeVisible (eqBandaCloseBtn);

        //  CINCO CHIPS DE TIPO, repartidos por `layoutModuleBar` -o sea POR EL
        //  TEXTO- y no a quintos: «PASO BAJO» pide el doble que «CAMPANA» y a
        //  quintos se corta en arabe. Es la misma cuenta que ya deciden BANCO,
        //  PADS y la tira del paso.
        for (int t = 0; t < Eq5::kNumTipos; ++t)
        {
            auto* b = new juce::TextButton (T (Eq5::nombreTipo (t)));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->onClick = [this, t] { ponTipoEq (eqBandaSel, t); refrescaBandaEq(); };
            eqBandaSheet.addAndMakeVisible (b);
            eqTipoBtns.add (b);
        }

        //  Q es el TERCER EJE de una campana -lo ancha que es- y en un dedo
        //  sobre la curva no hay tercer eje: la Y es la ganancia y la X la
        //  frecuencia. Por eso es un mando y esta aqui.
        eqQKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        eqQKnob.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        eqQKnob.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        eqQKnob.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        eqQKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, Metrics::readout);
        eqQKnob.setRange (Eq5::kQMin, Eq5::kQMax, 0.01);
        eqQKnob.setSkewFactorFromMidPoint (1.4);
        eqQKnob.setValue (Eq5::kQDef, juce::dontSendNotification);
        eqQKnob.setDoubleClickReturnValue (true, Eq5::kQDef);
        eqQKnob.setMouseDragSensitivity (320);
        //  La lectura es del MANDO y no un rotulo al lado: un rotulo aparte es
        //  un segundo sitio que hay que acordarse de refrescar, que es
        //  exactamente lo que le paso a la tira del paso.
        eqQKnob.textFromValueFunction = [] (double v)
        { return Lang::ltr (juce::String (v, 2)); };
        eqQKnob.onValueChange = [this]
        {
            ponQEq (eqBandaSel, (float) eqQKnob.getValue());
            refrescaBandaEq();
        };
        eqBandaSheet.addAndMakeVisible (eqQKnob);
    }

    // Projects sheet — reached from the header chip, not the module bar (the
    // bar stays a rule of three: PADS / SEC / FX).
    {
        //  AJUSTES SE DESPLAZA. Es la ficha con mas paginas y la que peor lo
        //  pasaba: en 915x412 pedia 458 px dentro de una tarjeta de 346 y las
        //  cuatro tapas de CARCASA salian a CERO de alto. Ver Sheet::hazDesplazable.
        setSheet.hazDesplazable();
        //  LAS TRES QUE TRAEN SU PROPIA LISTA. No se desplazan como ficha
        //  -anidar dos arrastres es la otra forma de que un gesto no se sepa de
        //  quien es- y piden a proposito mas alto del que hay, porque lo que
        //  las llena ya se desplaza solo. Ver Sheet::listaPropia. (PROYECTOS no
        //  esta: es una pagina de AJUSTES, y esa ficha si se desplaza entera.)
        manualSheet.listaPropia = true;
        browseSheet.listaPropia = true;
        mixSheet.listaPropia    = true;
        addAndMakeVisible (setSheet);
        setSheet.setVisible (false);
        setSheet.onDismiss = [this] { closeAllSheets(); };
        setSheet.paintContent = [this] (juce::Graphics& g)
        {
            //  EL TITULO, DE LAS CUATRO PAGINAS Y EN UN SOLO SITIO.
            //
            //  Lo pintaba cada pagina por su cuenta, asi que AUDIO decia
            //  "AJUSTES - AUDIO", PROYECTOS decia "PROYECTOS" a secas, y MIDI y
            //  GESTOS no decian NADA: la tarjeta abria con una fila vacia y una
            //  x, y nada en la pantalla contaba donde estabas. Tests/plano.py
            //  lo llevaba avisando -"midi no tiene titulo: se abre y no dice
            //  donde estas"- y un aviso que nadie lee es lo mismo que no
            //  ponerlo.
            paintSetTitle (g);
            //  Y los grupos, debajo del titulo pero encima de nada: se pintan
            //  antes que los rotulos de seccion, que van dentro. Ver
            //  pintaPaneles - esta ficha tenia cuatro paginas de filas sueltas
            //  con su nombre a la izquierda, y BUFER se leia como de la misma
            //  familia que RELOJ solo porque una fila esta debajo de la otra.
            pintaPaneles (g, setGrupos);
            if      (setPage == pageMidi)     paintMidiPage (g, midiArea);
            else if (setPage == pageAudio)    paintAudioSheetContent (g);
            else if (setPage == pageAspecto)  paintAspectoPage (g);
            else if (setPage == pageProjects) paintProjSheetContent  (g);
            else                              paintGesturesPage (g, gesturesArea);
        };

        //  LA CUENTA ATRAS, tres chips en la pagina de AUDIO. Es una
        //  preferencia de la persona -como el idioma, la carcasa y el master- y
        //  por eso vive aqui y no en el proyecto: cuantos compases te hacen
        //  falta para coger aire es tuyo y del momento, no de la cancion.
        {
            //  OFF y no «SIN»: es la palabra que esta app ya usa para el
            //  extremo apagado de un mando -los cinco que dicen «off»- asi que
            //  no hay una segunda forma de decir lo mismo, y ademas coincide de
            //  verdad en los cuatro idiomas. Con «SIN» el banco lo canto a la
            //  primera: «SIN identical in es and en», siete veces.
            const char* kCuenta[] = { "OFF", "1", "2" };
            for (int i = 0; i < 3; ++i)
            {
                auto* b = new juce::TextButton (kCuenta[i]);
                styleButton (*b, kStepOff);
                litAccent (*b);
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7311);
                b->onClick = [this, i]
                {
                    cuentaCompases = i;
                    saveCuentaPref();
                    status.setText (i == 0 ? T ("Sin cuenta atras")
                                           : T ("Cuenta atras: %1", juce::String (i)),
                                    juce::dontSendNotification);
                };
                setSheet.cuerpo.addChildComponent (b);
                cuentaButtons.add (b);
            }
        }

        //  Y EL BANCO DONDE CAEN LAS TOMAS.
        //
        //  Cuatro chips y no una lista de sesenta y cuatro pads: lo que se
        //  elige es DONDE se acumulan, no en cual cae la siguiente - eso lo
        //  decide `padParaToma`, del 01 hacia arriba, para que queden en orden.
        //  Las letras no se traducen: son las mismas cuatro que la fila de
        //  bancos de la cara, y ahi es donde la mano las busca.
        {
            for (int b4 = 0; b4 < kNumBanks; ++b4)
            {
                auto* b = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b4)));
                styleButton (*b, kStepOff);
                litAccent (*b);
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7312);
                b->setTitle (T ("Banco de tomas %1",
                                juce::String::charToString ((juce::juce_wchar) ('A' + b4))));
                b->onClick = [this, b4]
                {
                    bancoTomas = b4;
                    saveTomasPref();
                    status.setText (T ("Las tomas van al banco %1",
                                       juce::String::charToString ((juce::juce_wchar) ('A' + b4))),
                                    juce::dontSendNotification);
                };
                setSheet.cuerpo.addChildComponent (b);
                tomasButtons.add (b);
            }
        }

        //  Y EL MONITOR, dos chips en la misma columna y por la misma razon:
        //  llevar cascos o no es de la persona y del momento. Ver
        //  `AudioEngine::setMonitor`.
        //
        //  Apagado de fabrica, que no es timidez: encendido sobre el altavoz de
        //  un telefono es un acople, y ademas la produccion se cuela en la toma.
        //  La guarda de ruta lo impide igualmente -ver `aplicaMonitor`- y un
        //  defecto que hay que impedir es un defecto mal elegido.
        {
            for (int i = 0; i < 2; ++i)
            {
                //  Con la MISMA pareja de claves que la fila del movimiento:
                //  las dos dicen encendido o apagado y nada mas. Aqui el
                //  encendido es el segundo, que es el orden de la cuenta atras
                //  -lo apagado primero-.
                auto* b = new juce::TextButton (T (i == 1 ? "SI|chip" : "NO|chip"));
                styleButton (*b, kStepOff);
                litAccent (*b);
                b->setClickingTogglesState (true);
                b->setRadioGroupId (7312);
                b->onClick = [this, i]
                {
                    monitorOn = (i == 1);
                    saveMonitorPref();
                    aplicaMonitor (true);
                };
                setSheet.cuerpo.addChildComponent (b);
                monButtons.add (b);
            }
        }

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
        setSheet.cuerpo.addAndMakeVisible (projList);

        styleButton (setCloseButton, kKey);
        setCloseButton.onClick = [this] { closeAllSheets(); };
        setSheet.cuerpo.addAndMakeVisible (setCloseButton);

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
        setSheet.cuerpo.addAndMakeVisible (projNameBox);

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
        setSheet.cuerpo.addAndMakeVisible (projSaveButton);

        styleButton (projLoadButton, kKey);
        projLoadButton.onClick = [this]
        {
            const int sel = projList.getSelectedRow();
            if (juce::isPositiveAndBelow (sel, projModel.names.size()))
                loadProject (projModel.names[sel]);
        };
        setSheet.cuerpo.addAndMakeVisible (projLoadButton);

        styleButton (projNewButton, kKey);
        //  NUEVO empties every pad and every pattern. Two taps.
        projNewButton.onClick = [this]
        {
            if (! armConfirm (projNewButton, T ("BORRA TODO?"))) return;
            newProject();
        };
        setSheet.cuerpo.addAndMakeVisible (projNewButton);

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
        setSheet.cuerpo.addAndMakeVisible (projDeleteButton);

        styleButton (projExportButton, kKey);
        projExportButton.onClick = [this]
        {
            exportStatus.clear();
            exportOk = false;
            destinoCache = juce::File();
            openSheet (exportSheet, setButton);
            //  Y se pide el permiso AL ABRIR la ficha, no al pulsar EXPORTAR:
            //  el dialogo del sistema encima de un rebote que ya arranco es la
            //  forma de que la persona lo cierre sin leerlo. En Android 10 y
            //  posteriores isRequired dice que no y esto no ensena nada.
            ensureStoragePermission ({});
        };
        setSheet.cuerpo.addAndMakeVisible (projExportButton);

        //  GUARDAR KIT, al lado de EXPORTAR y no entre GUARDAR y ABRIR: las dos
        //  de esta fila producen algo que SALE de este proyecto - un rebote y un
        //  banco de sonidos-, mientras que las cuatro de abajo son el proyecto
        //  en si. Y comparte el nombre que ya hay escrito arriba, que es el que
        //  la persona acaba de teclear.
        styleButton (projKitButton, kKey);
        projKitButton.onClick = [this]
        {
            auto n = projNameBox.getText().trim();
            //  Sin nombre escrito, el del proyecto abierto: guardar el kit de
            //  "BREAKS 90" con el nombre en blanco no es una peticion de nada.
            if (n.isEmpty()) n = currentProject;
            //  Sobrescribir un kit borra dieciseis sonidos de la biblioteca y no
            //  pasa por deshacer, asi que se confirma - la misma red que
            //  CARGAR KIT y que guardar encima de un proyecto.
            //  El MISMO nombre que va a usar guardarKit, o la pregunta de
            //  sobrescribir mira una carpeta y se escribe en otra.
            const auto dir = ProjectStore::kits().getChildFile (
                                 ProjectStore::componente (
                                     juce::File::createLegalFileName (n.trim()).substring (0, 60)));
            if (dir.isDirectory() && ! armConfirm (projKitButton, T ("Sobrescribir \"%1\"?", n)))
                return;
            guardarKit (n);
        };
        setSheet.cuerpo.addAndMakeVisible (projKitButton);

        // --- RACK: los envios de UN CANAL, abierto desde la mesa. ---------
        //
        //  Era «los envios de un pad» y son mil trescientos cuarenta y cuatro
        //  numeros que nadie gestiona. Desde que el canal es el dueño del envio
        //  son dieciseis filas de seis, y el selector de abajo elige canal.
        styleButton (rackButton, kKey);
        rackButton.onClick = [this] { openSheet (rackSheet, mixButton); refreshRack(); };
        mixSheet.addAndMakeVisible (rackButton);

        //  DIECISEIS CANALES EN CUATRO POR CUATRO, que es la forma de la cara y
        //  la del selector de pad que ya estaba aqui — con la diferencia de que
        //  ahora son dieciseis y no sesenta y cuatro, asi que la fila de bancos
        //  se va: cuatro por cuatro son exactamente los que hay.
        for (int i = 0; i < kNumCanales; ++i)
        {
            auto* b = new juce::TextButton (juce::String (i + 1).paddedLeft ('0', 2));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            //  Y CAMBIA EL CANAL ACTUAL, que es el mismo que lee la cara: hay UN
            //  canal elegido y no dos. Con un `rackCanal` propio, la fila de la
            //  cara y la del rack dirian cosas distintas del mismo aparato — que
            //  es el desajuste que el `rackPad` de antes tenia con `selectPad`.
            b->onClick = [this, i] { canalActual = i; refrescaRanuras(); refreshRack(); };
            rackSheet.cuerpo.addAndMakeVisible (b);
            rackPadBtns.add (b);
        }

        //  UNA FILA POR RANURA y no por tipo: el rack dice cuanto manda ESTE
        //  canal a cada sitio de su fila, y la fila son seis. Con una fila por
        //  tipo habria un fader para un efecto que no esta puesto en ninguna
        //  parte, o sea un mando que no puede hacer nada.
        for (int f = 0; f < kNumRanuras; ++f)
        {
            auto* sl = new juce::Slider();
            sl->setSliderStyle (juce::Slider::LinearHorizontal);
            sl->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, Metrics::readout);
            sl->setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
            sl->setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
            sl->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sl->setRange (0.0, 1.0, 0.01);
            //  Cero, que es donde nace el envio y donde vuelve al tocarlo dos
            //  veces. El uno de antes era el valor del constructor mintiendo:
            //  el mando decia 100 y refreshRack lo bajaba a lo que el motor
            //  tuviera, asi que el rack se abria por primera vez ensenando algo
            //  distinto de lo que sonaba.
            sl->setValue (0.0, juce::dontSendNotification);
            sl->setDoubleClickReturnValue (true, 0.0);
            sl->setSliderSnapsToMousePosition (false);
            sl->textFromValueFunction = [] (double v) { return juce::String ((int) std::round (v * 100.0)); };
            sl->updateText();
            //  `f` es la RANURA. A que efecto va este envio lo dice `slotFx`
            //  en el momento de mover el fader y no aqui: una ranura puede
            //  cambiar de contenido y el mando es el mismo.
            sl->onValueChange = [this, f, sl]
            {
                const int fx = enRanura (f);
                if (fx < 0) return;                 // ranura vacia: no hay bus
                engine.setCanalSend (canalActual, fx, (float) sl->getValue());
                rackSheet.repaint();
            };
            rackSheet.cuerpo.addAndMakeVisible (sl);
            rackSends.add (sl);

            //  Y LA TAPA DEL CANALON, que es donde se cambia el efecto de una
            //  ranura. Aqui el nombre y el dibujo se PINTABAN, o sea que la
            //  columna de la izquierda era decoracion: ahora es la puerta.
            //
            //  Desde la cara solo se puede LLENAR una ranura -el «+» aparece
            //  con ella vacia y se va en cuanto se elige-, asi que este es el
            //  unico sitio desde el que se cambia o se quita lo que ya esta.
            //  Una funcion, un dueño.
            auto* g = new juce::TextButton();
            styleButton (*g, kStepOff);
            litAccent (*g);
            g->onClick = [this, f] { abreMenuRanura (f); };
            rackSheet.cuerpo.addAndMakeVisible (g);
            rackSlotBtns.add (g);

            //  Y LA TAPA DE APAGAR, al lado. Ver MainComponent.h.
            //
            //  Sin rotulo: lo que dice es el ESTADO de un interruptor y el
            //  nombre del efecto ya esta en el canalon de al lado, o sea que
            //  una palabra aqui seria la segunda vez que la fila dice lo
            //  mismo. `reparteTapa` ya admite ese caso -con el rotulo vacio el
            //  dibujo se lleva la tapa entera- y `Tests/planos.py` no la
            //  cuenta como gemela por lo mismo: sin texto no hay homonimo.
            auto* m = new juce::TextButton();
            styleButton (*m, kStepOff);
            litAccent (*m);
            m->onClick = [this, f]
            {
                const int fx = enRanura (f);
                if (fx < 0) return;             // ranura vacia: no hay que apagar
                fxTapped (fx);   // el mismo camino que la fila de la cara
            };
            rackSheet.cuerpo.addAndMakeVisible (m);
            rackMuteBtns.add (m);
        }

        styleButton (rackCloseButton, kKey);
        rackCloseButton.onClick = [this] { closeAllSheets(); };
        rackSheet.cuerpo.addAndMakeVisible (rackCloseButton);
        //  Y EL RACK, que ademas va a crecer: su selector de pad pasa de
        //  dieciseis tapas en una fila -26 px de ancho en la pantalla mas
        //  estrecha- a la misma rejilla de cuatro por cuatro que la cara.
        rackSheet.hazDesplazable();
        addAndMakeVisible (rackSheet);
        rackSheet.setVisible (false);
        rackSheet.onDismiss = [this] { closeAllSheets(); };
        rackSheet.paintContent = [this] (juce::Graphics& g) { paintRackSheetContent (g); };

        // --- INSTRUMENTOS: el contenido, y la fabrica dentro de el -------
        //
        //  DIECISEIS TAPAS FIJAS Y NO UNA POR INSTRUMENTO. Cuantos hay depende
        //  de lo que haya en el disco, y crear tapas al vuelo desde resized()
        //  es lo que cerro la app entera la primera vez que la caja negra
        //  sirvio para algo: `CAIDA senal 11 en arranque`. El pack no puede
        //  traer mas de dieciseis -Instrumentos::kMaxInstr- asi que el pozo es
        //  fijo y las que sobran se apagan Y se quedan sin limites, las dos
        //  cosas, que es lo que la regla dice y lo que media app hacia a medias.
        for (int i = 0; i < Instrumentos::kMaxInstr; ++i)
        {
            auto* b = new juce::TextButton();
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->onClick = [this, i] { cargaInstrumento (i); };
            instSheet.cuerpo.addAndMakeVisible (b);
            instBtns.add (b);
        }
        //  LOS DIECISEIS DEL DESTINO y las cuatro del banco. Fijas, como las
        //  de arriba: crear tapas al vuelo desde resized() es lo que cerro la
        //  app la vez que la caja negra sirvio para algo.
        for (int i = 0; i < kPadsPerBank; ++i)
        {
            auto* b = new juce::TextButton();
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->onClick = [this, i]
            {
                instDestPad = instBancoDest * kPadsPerBank + i;
                refreshInst();
                instSheet.repaint();
            };
            instSheet.cuerpo.addAndMakeVisible (b);
            instDestBtns.add (b);
        }
        for (int b4 = 0; b4 < kNumBanks; ++b4)
        {
            auto* b = new juce::TextButton (juce::String::charToString ((juce::juce_wchar) ('A' + b4)));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->onClick = [this, b4]
            {
                instBancoDest = b4;
                //  El destino se arrastra con el banco y se queda en la misma
                //  casilla: cambiar de banco no puede dejar apuntando a un pad
                //  que ya no se ve.
                instDestPad = b4 * kPadsPerBank + instDestPad % kPadsPerBank;
                refreshInst();
                resized();
                instSheet.repaint();
            };
            instSheet.cuerpo.addAndMakeVisible (b);
            instBancoBtns.add (b);
        }

        for (auto* b : { &instPackDownBtn, &instPackUpBtn })
        {
            styleButton (*b, kKey);
            instSheet.cuerpo.addAndMakeVisible (*b);
        }
        styleButton (vstButton, kKey);
        vstButton.onClick = [this] { abreVst(); };
        padSheet.addAndMakeVisible (vstButton);

        instPackDownBtn.onClick = [this] { pasoPack (-1); };
        instPackUpBtn  .onClick = [this] { pasoPack ( 1); };
        styleButton (instCloseButton, kKey);
        instCloseButton.onClick = [this] { closeAllSheets(); };
        instSheet.cuerpo.addAndMakeVisible (instCloseButton);
        //  DE CONTROLES, asi que se desplaza: son cuatro filas de tapas mas la
        //  del pack, y en apaisado la tarjeta se queda en 321 px. Sin esto la
        //  ultima fila se cae por el mismo sitio por el que se caia la del RACK.
        instSheet.hazDesplazable();
        addAndMakeVisible (instSheet);
        instSheet.setVisible (false);
        instSheet.onDismiss = [this] { closeAllSheets(); };
        instSheet.paintContent = [this] (juce::Graphics& g) { paintInstSheetContent (g); };
    }

    // ------------------------------------------------------------------
    //  LA FICHA DEL INSTRUMENTO. Ver MainComponent.h.
    // ------------------------------------------------------------------
    {
        //  LAS DOS FLECHAS DEL PRESET. Da la vuelta por los dos lados: con
        //  dieciseis y un menos que se queda quieto en el primero, la flecha a
        //  veces no hace nada y eso no se distingue de una rota.
        auto pasoPreset = [this] (int d)
        {
            if (! padEsInstrumento (vstPad)) return;
            const int n = Sintes::kPresets;
            eligePreset ((uiSample[(size_t) vstPad]->preset + d % n + n) % n);
        };
        vstPreDown.onClick = [pasoPreset] { pasoPreset (-1); };
        vstPreUp  .onClick = [pasoPreset] { pasoPreset ( 1); };
        for (auto* b : { &vstPreDown, &vstPreUp })
        {
            styleButton (*b, kKey);
            litAccent (*b);
            vstSheet.cuerpo.addAndMakeVisible (*b);
        }

        for (auto* b : { &vstOctDown, &vstOctUp })
        {
            styleButton (*b, kKey);
            vstSheet.cuerpo.addAndMakeVisible (*b);
        }
        //  LA OCTAVA MUEVE EL TECLADO Y NO EL PAD. Escribir el tono del pad
        //  para pasear por el teclado es exactamente el fallo que ya costo una
        //  medida en la audicion del piano roll: dejaba el pad afinado en la
        //  ultima tecla que se toco.
        vstOctDown.onClick = [this] { vstTeclado.setBase (vstTeclado.getBase() - 12); refreshVst(); };
        vstOctUp  .onClick = [this] { vstTeclado.setBase (vstTeclado.getBase() + 12); refreshVst(); };

        vstTeclado.onNota = [this] (int semis)
        {
            //  postNoteOnAt y no setPadPitch + postNoteOn: el semitono viaja EN
            //  el comando, asi que oir una tecla no afina el pad.
            //
            //  Y SOSTENIDA: la nota dura lo que el dedo este encima, igual que
            //  en un pad en modo tecla. Con el largo de audicion, una tecla que
            //  se mantiene se cortaria sola a los 1200 ms.
            if (padHasSample[(size_t) juce::jlimit (0, kNumPads - 1, vstPad)])
                engine.postNoteOnAt (vstPad, semis, 0.9f, AudioEngine::kSostenida);
        };
        vstTeclado.onSuelta = [this] { engine.postNoteOff (vstPad); };
        vstSheet.cuerpo.addAndMakeVisible (vstTeclado);

        styleButton (vstCloseButton, kKey);
        vstCloseButton.onClick = [this] { closeAllSheets(); };
        vstSheet.cuerpo.addAndMakeVisible (vstCloseButton);

        vstSheet.hazDesplazable();
        addAndMakeVisible (vstSheet);
        vstSheet.setVisible (false);
        vstSheet.onDismiss = [this] { closeAllSheets(); };
        vstSheet.paintContent = [this] (juce::Graphics& g) { paintVstSheetContent (g); };

        // --- AUTO CHOP: the confirmation the destruction always deserved ---
        for (int i = 0; i < 4; ++i)
        {
            const int n = kChopCounts[i];
            auto* b = new juce::TextButton (juce::String (n));
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7301);
            b->onClick = [this, n] { chopSlices = n; recalculaCortes(); refreshChopSheet(); };
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
        //  Cambiar de modo REHACE la lista: son dos formas de proponer los
        //  cortes, no dos listas que convivan. Lo que la persona haya movido a
        //  mano se pierde, y eso es lo correcto - pedir GOLPES despues de
        //  editar es pedir la propuesta del detector otra vez.
        chopEvenBtn.onClick = [this] { chopByHits = false; recalculaCortes(); refreshChopSheet(); };
        chopHitsBtn.onClick = [this] { chopByHits = true;  refreshChopHits(); recalculaCortes(); refreshChopSheet(); };

        //  LOS TRES GESTOS DE LA VISTA. Mover y quitar comparten el toque
        //  inicial y se separan al soltar, por la distancia recorrida: en una
        //  marca de dos pixeles no hay sitio para dos zonas distintas.
        chopVista.onMueve = [this] (int idx, int muestra)
        {
            if (idx <= 0 || idx >= (int) chopCortes.size()) return;   // la cero es el principio
            //  Acotado entre sus vecinas y con el trozo minimo por lado, o una
            //  marca arrastrada encima de otra deja un trozo de cero muestras
            //  -que es un click, no un sonido- y ademas se cruzan de orden.
            const int lo = chopCortes[(size_t) (idx - 1)] + ChopPreview::kMinMuestras;
            const int hi = (idx + 1 < (int) chopCortes.size()
                              ? chopCortes[(size_t) (idx + 1)]
                              : padSourceLength (selectedPad)) - ChopPreview::kMinMuestras;
            if (hi <= lo) return;
            chopCortes[(size_t) idx] = juce::jlimit (lo, hi, muestra);
            chopVista.setCortes (chopCortes);
        };

        chopVista.onAnade = [this] (int muestra)
        {
            const int len = padSourceLength (selectedPad);
            if (len < 2 || chopCortes.empty()) return;
            //  No mas marcas que pads donde meterlas: una marca de mas seria
            //  un trozo que se dibuja y no llega a ningun sitio.
            if ((int) chopCortes.size() >= chopTargets (chopSlices, chopOnlyEmpty).size())
            {
                status.setText (T ("No caben mas trozos"), juce::dontSendNotification);
                return;
            }
            for (int c : chopCortes)
                if (std::abs (c - muestra) < ChopPreview::kMinMuestras) return;
            chopCortes.push_back (juce::jlimit (0, len - 1, muestra));
            std::sort (chopCortes.begin(), chopCortes.end());
            refreshChopSheet();
        };

        chopVista.onQuita = [this] (int idx)
        {
            if (idx <= 0 || idx >= (int) chopCortes.size()) return;
            chopCortes.erase (chopCortes.begin() + idx);
            refreshChopSheet();
        };
        chopSheet.addAndMakeVisible (chopVista);

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
            setSheet.cuerpo.addAndMakeVisible (b);
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
                                  &songSheet, &exportSheet, &rackSheet, &chopSheet, &instSheet,
                                  &vstSheet })
                    sh->repaint();
            };
            setSheet.cuerpo.addAndMakeVisible (b);
            skinButtons.add (b);
        }
        skinButtons[juce::jlimit (0, 3, ZatiColours::currentSkin)]
            ->setToggleState (true, juce::dontSendNotification);

        //  Y EL MOVIMIENTO, en la misma pagina y con la misma forma de
        //  control: dos chips, uno encendido. Es lo que ve la persona de «como
        //  se ve la maquina», que es de lo que trata esta pagina.
        for (int i = 0; i < 2; ++i)
        {
            auto* b = new juce::TextButton (T (i == 0 ? "SI|chip" : "NO|chip"));
            styleButton (*b, kKey);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7413);
            b->onClick = [this, i] { ponMovimiento (i == 0); };
            setSheet.cuerpo.addAndMakeVisible (b);
            movButtons.add (b);
        }

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
        setSheet.cuerpo.addAndMakeVisible (quantButton);

        styleButton (measureButton, kKey);
        measureButton.onClick = [this] { startMeasure(); };
        setSheet.cuerpo.addAndMakeVisible (measureButton);

        //  The two pages of this card. A tab row, not a door: the card stays
        //  where it is and its contents change, which is the difference
        //  between "settings has two pages" and "settings sends you somewhere
        //  else".
        juce::TextButton* pb[5] = { &pageAudioBtn, &pageMidiBtn, &pageAspBtn,
                                    &pageProjBtn, &pageGestBtn };
        for (int i = 0; i < 5; ++i)
        {
            styleButton (*pb[i], kKey);
            pb[i]->setClickingTogglesState (true);
            pb[i]->setRadioGroupId (8802);
            litAccent (*pb[i]);
            pb[i]->onClick = [this, i] { showSetPage (i); };
            setSheet.cuerpo.addAndMakeVisible (pb[i]);
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
            setSheet.cuerpo.addAndMakeVisible (b);
        }
        for (auto* c : { &midiOutBox, &midiInBox })
        {
            c->setTextWhenNoChoicesAvailable (T ("nada enchufado"));
            c->setTextWhenNothingSelected (T ("nada enchufado"));
            setSheet.cuerpo.addAndMakeVisible (c);
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

        //  CAMBIAR el destino, al lado de la linea que lo dice. Cargar un
        //  sonido abre un navegador y se elige de donde; esto es lo mismo por
        //  el otro lado y hasta ahora no existia.
        styleButton (exportDirBtn, kKey);
        exportDirBtn.onClick = [this] { openBrowseForExportDir(); };
        exportSheet.addAndMakeVisible (exportDirBtn);

        //  EL TERCER MODO: la cancion suena y lo que suena se escribe. Ver
        //  MainComponent.h y `RebotVivo`.
        styleButton (exportLiveButton, kKey);
        exportLiveButton.onClick = [this] { alternaRebotVivo(); };
        exportSheet.addAndMakeVisible (exportLiveButton);

        styleButton (exportStemsButton, kKey);
        exportStemsButton.onClick = [this] { startExport (true); };
        exportSheet.addAndMakeVisible (exportStemsButton);

        styleButton (exportCancelButton, kRec);
        //  El formato, al lado de los dos verbos y no en AJUSTES: se elige
        //  justo antes de exportar y no una vez en la vida.
        styleButton (exportFmtBtn, kKey);
        litAccent (exportFmtBtn);
        exportFmtBtn.setClickingTogglesState (true);
        exportFmtBtn.onClick = [this]
        {
            exportOgg = exportFmtBtn.getToggleState();
            exportFmtBtn.setButtonText (exportOgg ? "OGG" : "WAV");
            exportSheet.repaint();
        };
        exportSheet.addAndMakeVisible (exportFmtBtn);

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

        //  Y TAMBIEN CARPETAS, que es lo que le falta para servir de las dos
        //  cosas: elegir un sonido y elegir donde cae el rebote. CARGAR sigue
        //  pidiendo un FICHERO -selectionChanged ya exige existsAsFile- asi que
        //  anadir esto no afloja nada de lo que ya habia.
        browser = std::make_unique<juce::FileBrowserComponent> (
            juce::FileBrowserComponent::openMode
          | juce::FileBrowserComponent::canSelectFiles
          | juce::FileBrowserComponent::canSelectDirectories
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

        styleButton (browseUseDirBtn, kAccent);
        browseUseDirBtn.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
        browseUseDirBtn.onClick = [this] { usarCarpetaDeExport(); };
        browseSheet.addAndMakeVisible (browseUseDirBtn);

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

        styleButton (browseFactoryButton, kKey);
        //  FABRICA SE CONVIRTIO EN LA PUERTA DE INSTRUMENTOS, y no es una tapa
        //  que cambia de nombre: es la misma accion con el resto alrededor.
        //
        //  Esta tapa recargaba el banco de delante con sus dieciseis sonidos de
        //  fabrica, o sea cargaba UN instrumento de UN pack - el de dentro -
        //  sin que hubiera forma de llegar a ningun otro. Ahora la fabrica es
        //  el primer pack del catalogo y sus cuatro bancos son sus cuatro
        //  instrumentos, asi que lo que hacia esta tapa sigue estando a un
        //  toque de aqui, con todo lo demas al lado.
        //
        //  Una funcion, un dueno: la que se queda es la que sabe algo mas, y la
        //  que se va deja una puerta y nunca una copia. Y la fila sigue siendo
        //  de cinco, asi que el reparto medido no se mueve.
        browseFactoryButton.onClick = [this] { openInstSheet(); };
        browseSheet.addAndMakeVisible (browseFactoryButton);

        // Escape hatch: hand off to the OS picker. Some Android ROMs hide media
        // files from a direct directory listing no matter what is granted; the
        // system picker always reaches them (and gets its own access grant).
        styleButton (browseSystemButton, kKey);
        browseSystemButton.onClick = [this] { launchSystemPicker(); };
        browseSheet.addAndMakeVisible (browseSystemButton);

        //  MIS KITS: la puerta que le faltaba a ZATI/Kits.
        //
        //  GUARDAR KIT escribe alli y el navegador no iba nunca, asi que para
        //  volver a usar un kit propio habia que buscarlo a mano por el
        //  telefono. La carpeta se crea al llegar y no al guardar: asi la tapa
        //  lleva a algun sitio tambien la primera vez, que es cuando alguien
        //  la toca para ver que hay.
        //
        //  Y NO CARGA NADA, solo lleva. Desde alli el kit entra por CARGAR KIT
        //  como cualquier pack descargado, que es la misma puerta a proposito:
        //  un kit guardado aqui y uno bajado de fuera tienen la misma forma.
        styleButton (browseKitsDirButton, kKey);
        browseKitsDirButton.onClick = [this]
        {
            const auto dir = ProjectStore::kits();
            ProjectStore::ensureDirectory (dir);
            if (browser != nullptr && dir.isDirectory()) browser->setRoot (dir);
            if (dir.getNumberOfChildFiles (juce::File::findDirectories) == 0)
                status.setText (T ("Aun no has guardado ningun kit"), juce::dontSendNotification);
        };
        browseSheet.addAndMakeVisible (browseKitsDirButton);
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
        //  Un modo armado a la vez: ver ponSoloArmado.
        if (loadArmed) ponSoloArmado (false);
        else           refrescaRejillaModo();
        // ASCII only: a raw UTF-8 dash in a literal renders as mojibake on the
        // Android build (different execution charset), so keep these plain.
        status.setText (loadArmed ? T ("LOAD armado - toca un pad para cargarlo")
                                  : T ("Toca un pad para sonar"), juce::dontSendNotification);
    };
    addAndMakeVisible (loadButton);

    styleButton (testButton, kKey);
    testButton.onClick = [this] { engine.postTestTone(); status.setText (T ("Tono de prueba"), juce::dontSendNotification); };
    setSheet.cuerpo.addAndMakeVisible (testButton);

    styleButton (recButton, kKey);
    recButton.onClick = [this] { toggleRecordArm(); };
    addAndMakeVisible (recButton);

    //  TOCAR ARMA, MANTENER LIMPIA. Vaciar los solos es lo unico de esta
    //  funcion que no se deshace tocando otra vez, asi que no puede compartir
    //  gesto con armarla: es la misma pareja que AUTO en la cancion.
    styleButton (soloButton, kKey);
    soloButton.setClickingTogglesState (true);
    soloButton.onClick = [this] { ponSoloArmado (soloButton.getToggleState()); };
    soloButton.onHold  = [this]
    {
        engine.clearSolo();
        refrescaRejillaModo();
        status.setText (T ("Sin solos"), juce::dontSendNotification);
    };
    addAndMakeVisible (soloButton);

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
    playButton.onClick = [this] { ponTransporte (playButton.getToggleState()); };

    //  Hold PLAY for silence NOW: the transport stops and every voice still
    //  ringing is cut with it. STOP on its own leaves long tails and held
    //  loops sounding, which is right for a musical stop and wrong for the
    //  moment you need the room quiet.
    playButton.onHold = [this]
    {
        ponTransporte (false);
        engine.postPanic();
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
    //  PASO CONTINUO, no de milesimas. Una milesima de un tema de 235 s son
    //  235 ms: con el paso viejo el minimo nuevo -5.3 ms- no se podia ni pedir,
    //  porque el mando redondeaba la peticion antes de que nadie la mirase.
    initSlider (startSlider,   0.0,  1.0, 0.0, 0.0);
    initSlider (endSlider,     0.0,  1.0, 0.0, 1.0);

    //  Y LA CASILLA DICE SEGUNDOS, no la fraccion cruda.
    //
    //  Con el paso continuo la fraccion pierde el poco sentido que tenia: en un
    //  tema de 235 s, mover el recorte cinco milisegundos cambia el numero en
    //  0.00002 y la casilla sigue diciendo "0.010". Un limite que no se puede
    //  LEER es medio limite - se puede poner y no se puede comprobar.
    //
    //  Se dan tres decimales por debajo de diez segundos -que es donde se
    //  trabaja recortando un golpe-, dos por debajo de cien y uno por encima,
    //  que es lo que cabe en la casilla sin encoger la letra.
    {
        auto texto = [this] (double v01)
        {
            const int len = padVisibleLength (selectedPad);
            const double sr = (selectedPad >= 0 && uiSample[(size_t) selectedPad] != nullptr)
                                ? uiSample[(size_t) selectedPad]->sourceSampleRate : 0.0;
            if (len <= 0 || sr <= 0.0) return juce::String (v01, 3);
            const double sec = v01 * (double) len / sr;
            const int dec = sec < 10.0 ? 3 : (sec < 100.0 ? 2 : 1);
            return Lang::ltr (juce::String (sec, dec) + "s");
        };
        auto valor = [this] (const juce::String& t)
        {
            const int len = padVisibleLength (selectedPad);
            const double sr = (selectedPad >= 0 && uiSample[(size_t) selectedPad] != nullptr)
                                ? uiSample[(size_t) selectedPad]->sourceSampleRate : 0.0;
            const double n = t.retainCharacters ("0123456789.,-").replace (",", ".").getDoubleValue();
            if (len <= 0 || sr <= 0.0) return juce::jlimit (0.0, 1.0, n);
            return juce::jlimit (0.0, 1.0, n * sr / (double) len);
        };
        for (juce::Slider* sl : { &startSlider, &endSlider })
        {
            sl->textFromValueFunction = texto;
            sl->valueFromTextFunction = valor;
            sl->updateText();
        }
    }
    //  SUAVE y no ABRUPTO: el mando dice cuantos milisegundos tarda el borde
    //  en abrirse. Cero es el corte seco de siempre, que es lo que quiere un
    //  golpe de bateria; cinco quitan el clic de un corte en medio de un grave
    //  sin que se note que hay un fundido; cien es un swell.
    //
    //  Punto medio en 10 ms: lineal, la mitad del recorrido iria de 250 a 500,
    //  donde ya no hay decisiones que tomar, y los primeros cinco milisegundos
    //  -que es donde esta todo- cabrian en un pelo del recorrido.
    initSlider (fadeInSlider,  0.0, 500.0, 0.5, 0.0);
    initSlider (fadeOutSlider, 0.0, 500.0, 0.5, 0.0);
    for (auto* sl : { &fadeInSlider, &fadeOutSlider })
    {
        sl->setSkewFactorFromMidPoint (10.0);
        sl->textFromValueFunction = [] (double v)
        {
            return v < 0.05 ? T ("SECO") : Lang::ltr (juce::String (v, v < 10.0 ? 1 : 0) + " ms");
        };
        sl->updateText();
    }
    fadeInSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        padFadeIn[(size_t) selectedPad] = (float) fadeInSlider.getValue();
        engine.setPadFadeIn (selectedPad, (float) fadeInSlider.getValue());
        pushFadesToWaveform();
    };
    fadeOutSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        padFadeOut[(size_t) selectedPad] = (float) fadeOutSlider.getValue();
        engine.setPadFadeOut (selectedPad, (float) fadeOutSlider.getValue());
        pushFadesToWaveform();
    };

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
    //  ANCHO: 0 mono, 1 como viene, 2 el doble. Doble click al UNO y no al
    //  cero, que es lo que este mando significa "sin tocar" - y es el valor con
    //  el que vuelve cualquier proyecto anterior.
    initKnob (anchoSlider, 0.0, 2.0, 0.01, 1.0, 1.0,
             [this] { if (selectedPad >= 0) { padAnchoUI[(size_t) selectedPad] = (float) anchoSlider.getValue();
                                              engine.setPadAncho (selectedPad, (float) anchoSlider.getValue());
                                              if (auto* ma = mixAnchos[selectedPad]) ma->setValue (anchoSlider.getValue(), juce::dontSendNotification); } });
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
    //  Con Lang::ltr, como las otras cuatro lecturas que ya lo llevaban: un
    //  "+50 c" en arabe se reordena y el signo acaba al otro lado del numero.
    fineSlider.textFromValueFunction = [] (double v)
    {
        return Lang::ltr ((v > 0.0 ? "+" : "") + juce::String ((int) v) + " c");
    };
    fineSlider.updateText();
    attackSlider.setTextValueSuffix (" ms");
    releaseSlider.setTextValueSuffix (" ms");
    panSlider.textFromValueFunction = [] (double v)
    {
        if (std::abs (v) < 0.005) return juce::String ("C");
        return Lang::ltr ((v < 0 ? "L" : "R") + juce::String ((int) std::round (std::abs (v) * 100.0)));
    };
    panSlider.updateText();
    //  MONO en el extremo de abajo y no un "0%": cero por ciento de ancho es
    //  una frase y mono es la palabra. Es lo mismo que hace CHOKE con su off.
    anchoSlider.textFromValueFunction = [] (double v)
    {
        if (v < 0.005) return T ("MONO");
        return Lang::ltr (juce::String ((int) std::round (v * 100.0)) + "%");
    };
    anchoSlider.updateText();
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
        double v = juce::jmin (startSlider.getValue(), endSlider.getValue() - minTrim01 (selectedPad));
        padStart01[(size_t) selectedPad] = (float) v;
        const int len = padSourceLength (selectedPad);
        if (len > 0) engine.setPadStart (selectedPad, (int) (v * len));
        waveform.setTrim ((float) v, padEnd01[(size_t) selectedPad]);
        pushFadesToWaveform();
        refreshPadArt (selectedPad);
        refreshWaveformSegments();
        repaint (editInfoArea.expanded (4));
    };
    endSlider.onValueChange = [this]
    {
        if (selectedPad < 0) return;
        double v = juce::jmax (endSlider.getValue(), startSlider.getValue() + minTrim01 (selectedPad));
        padEnd01[(size_t) selectedPad] = (float) v;
        const int len = padSourceLength (selectedPad);
        if (len > 0) engine.setPadEnd (selectedPad, (int) (v * len));
        waveform.setTrim (padStart01[(size_t) selectedPad], (float) v);
        pushFadesToWaveform();
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
        seqPrimerPaso = 0;
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
    noteSlider.textFromValueFunction = [] (double v) { return Lang::ltr ((v > 0 ? juce::String ("+") : juce::String()) + juce::String ((int) v) + " st"); };
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
    velSlider.textFromValueFunction = [] (double v) { return Lang::ltr (juce::String ((int) std::round (v * 100.0 / 127.0)) + " %"); };
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
    { return (v <= 1.0) ? juce::String ("1") : Lang::ltr ("x" + juce::String ((int) v)); };
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
        //  TRES desde que el piano roll dejo de tener ficha propia: PASOS y
        //  PIANO son dos vistas de lo mismo -las notas del patron- y PATRON es
        //  lo que le pasa al patron entero. El indice de la tapa ES la pagina,
        //  asi que el orden de este array manda; ver SeqPage.
        juce::TextButton* sb[3] = { &seqGridBtn, &seqPianoBtn, &seqStepBtn };
        for (int i = 0; i < 3; ++i)
        {
            styleButton (*sb[i], kKey);
            sb[i]->setClickingTogglesState (true);
            sb[i]->setRadioGroupId (8803);
            litAccent (*sb[i]);
            sb[i]->onClick = [this, i] { showSeqPage (i); if (i == seqPagePiano) refreshPiano(); };
            seqSheet.addAndMakeVisible (sb[i]);
        }
        seqGridBtn.setToggleState (true, juce::dontSendNotification);

        //  LA VENTANA DE PISTAS. Tres estados en una tapa y no un interruptor
        //  de dos, porque con ocho a la vista hace falta poder ir a las otras
        //  ocho: 1-16 ensena el banco entero, 1-8 y 9-16 lo parten y doblan el
        //  alto de la celda. Ver StepGrid::setVentana.
        styleButton (seqPistasBtn, kKey);
        seqPistasBtn.onClick = [this]
        {
            aplicaPistas ((pistasVista + 1) % 3);
            savePistasPref();
        };
        seqSheet.addAndMakeVisible (seqPistasBtn);

        //  EL ZOOM DE ANCHO, con la escalera de siempre: se prueban los pasos
        //  en orden y se coge el primero que deje la celda por encima de su
        //  suelo. Se mide POR LA TAPA y no poniendo el numero por dentro, que
        //  es lo unico que ve la escalera — con `seqZoomW = 0.75f` el paso que
        //  no cabe no se salta nunca.
        styleButton (seqZoomBtn, kKey);
        seqZoomBtn.onClick = [this]
        {
            //  Cuadrado, el doble de ancho —para escribir un redoble— y tres
            //  cuartos, que es ver mas de golpe. Por debajo no se ofrece: media
            //  celda cae por debajo del suelo en las siete pantallas, asi que
            //  seria un paso que la escalera salta siempre.
            static const float pasos[] = { 1.0f, 2.0f, 0.75f };
            const int n = (int) (sizeof (pasos) / sizeof (pasos[0]));
            int donde = 0;
            for (int i = 0; i < n; ++i)
                if (std::abs (pasos[i] - seqZoomW) < 0.001f) donde = i;
            for (int k = 1; k <= n; ++k)
            {
                const float cand = pasos[(donde + k) % n];
                if (std::abs (cand - seqZoomW) < 0.001f || stepGrid.cabeZoom (cand))
                {
                    aplicaZoomPasos (cand);
                    return;
                }
            }
        };
        seqSheet.addAndMakeVisible (seqZoomBtn);

        styleButton (seqPlayBtn, kKey);
        litAccent (seqPlayBtn);
        seqPlayBtn.setClickingTogglesState (true);
        seqPlayBtn.onClick = [this]
        {
            ponTransporte (seqPlayBtn.getToggleState());
        };
        seqSheet.addAndMakeVisible (seqPlayBtn);

        styleButton (seqHumanBtn, kKey);
        seqHumanBtn.onClick = [this] { humanizePattern(); };
        seqSheet.addAndMakeVisible (seqHumanBtn);

        //  EUCLIDES. Ver euclidesPattern: el mando dice CUANTOS golpes, y la
        //  fila del pad se reescribe entera con ellos repartidos.
        euclidSlider.setSliderStyle (juce::Slider::IncDecButtons);
        euclidSlider.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        euclidSlider.setRange (0.0, 16.0, 1.0);
        euclidSlider.setValue (0.0, juce::dontSendNotification);
        euclidSlider.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        euclidSlider.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        euclidSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        euclidSlider.textFromValueFunction = [] (double v)
            { return v <= 0.0 ? T ("vacio") : Lang::ltr (juce::String ((int) v)); };
        for (auto* b : { &copyRowBtn, &pasteRowBtn })
        {
            styleButton (*b, kKey);
            seqSheet.addAndMakeVisible (*b);
        }
        copyRowBtn.onClick  = [this] { copiarFila(); };
        pasteRowBtn.onClick = [this] { pegarFila(); };
        pasteRowBtn.setEnabled (false);   // no hay nada que pegar hasta que se copie

        euclidSlider.onValueChange = [this] { euclidesPattern ((int) euclidSlider.getValue()); };
        //  Y se refresca el texto: la funcion se asigna DESPUES del setValue, y
        //  sin esto la casilla se queda con el numero crudo hasta que alguien
        //  mueva el mando - "0" donde tiene que poner "vacio".
        euclidSlider.updateText();
        seqSheet.addAndMakeVisible (euclidSlider);

        styleButton (seqFollowBtn, kStepOff);
        litAccent (seqFollowBtn);
        seqFollowBtn.setClickingTogglesState (true);
        seqFollowBtn.onClick = [this]
        {
            seqFollow = seqFollowBtn.getToggleState();
            status.setText (seqFollow ? T ("La vista sigue al compas que suena")
                                      : T ("La vista se queda donde la dejes"),
                            juce::dontSendNotification);
        };
        seqSheet.addAndMakeVisible (seqFollowBtn);

        //  De 0 a 101: el 0 es APAGADO y el resto el porcentaje del recorrido
        //  del corte. Un valor fuera de la escala para decir "ninguno" es lo
        //  que ahorra un interruptor y una fila.
        initSlider (lockSlider, 0.0, 101.0, 1.0, 0.0);
        lockSlider.textFromValueFunction = [] (double v)
        {
            //  T("off") y no el literal "OFF": la tabla tiene la fila -关 en
            //  chino, مغلق en arabe- y CHOKE ya la usa. Habia DOS grafias de la
            //  misma palabra, una traducida y otra no, y esta se quedaba en
            //  ingles en las cuatro compilaciones. No la veia el banco porque
            //  los deslizadores estan excluidos del volcado a proposito -su
            //  rotulo es un numero en una caja medida-, y eso es cierto hasta
            //  que el rotulo es una PALABRA.
            if (v < 0.5) return T ("off");
            const float hz = AudioEngine::lockToHz ((int) v - 1);
            return hz >= 1000.0f ? Lang::ltr (juce::String (hz / 1000.0f, 1) + "k")
                                 : Lang::ltr (juce::String ((int) hz) + " Hz");
        };
        lockSlider.updateText();
        lockSlider.onValueChange = [this]
        {
            if (selectedStep < 0 || selectedPad < 0) return;
            const int v = (int) lockSlider.getValue();
            engine.setStepLock (selectedPattern, selectedStep, selectedPad,
                                v < 1 ? AudioEngine::kNoLock : v - 1);
        };
        seqSheet.addAndMakeVisible (lockSlider);

        //  LOS OTROS CUATRO BLOQUEOS DEL PASO. Ver AudioEngine::setStepPLock.
        //  Mismo idioma que el del corte: el extremo de abajo esta FUERA de la
        //  escala y dice OFF, que es lo que vale un paso que no toca ese
        //  parametro. Sin esa posicion harian falta cuatro interruptores al
        //  lado, y una fila mas en la tira cuesta 3.6 px de celda por carril.
        //
        //  Giratorios y no de + y -: cuatro cajas de IncDecButtons piden 122 px
        //  y en un telefono de 412 les tocan 103. Ver la declaracion.
        {
            auto plock = [this] (juce::Slider& sl, int cual,
                                 std::function<juce::String (int)> texto)
            {
                sl.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
                sl.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
                sl.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
                sl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
                //  La casilla a la DERECHA y no debajo: la fila mide
                //  Metrics::hit -40- y una casilla debajo se lleva la mitad,
                //  dejando el mando en un punto de 16 px que no se puede
                //  agarrar. Al lado, el mando se queda con los 40 enteros.
                sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, Metrics::readout);
                sl.setRange (-1.0, 100.0, 1.0);
                sl.setValue (-1.0, juce::dontSendNotification);
                sl.setDoubleClickReturnValue (true, -1.0);   // dos toques = quitar el bloqueo
                sl.textFromValueFunction = [texto] (double v)
                {
                    return v < -0.5 ? T ("off") : texto ((int) v);   // ver lockSlider
                };
                sl.updateText();
                sl.onValueChange = [this, &sl, cual]
                {
                    if (selectedStep < 0 || selectedPad < 0) return;
                    const int v = (int) sl.getValue();
                    engine.setStepPLock (selectedPattern, selectedStep, selectedPad, cual,
                                         v < 0 ? AudioEngine::kNoPLock : v);
                };
                seqSheet.addAndMakeVisible (sl);
            };

            plock (atkPasoSlider, AudioEngine::plockAtaque, [] (int pct)
                   { return Lang::ltr (juce::String ((int) AudioEngine::plockAtaqueMs (pct)) + " ms"); });
            plock (relPasoSlider, AudioEngine::plockCaida, [] (int pct)
                   { return Lang::ltr (juce::String ((int) AudioEngine::plockCaidaMs (pct)) + " ms"); });
            plock (iniPasoSlider, AudioEngine::plockInicio, [] (int pct)
                   { return Lang::ltr (juce::String (pct) + " %"); });
            //  El pan NO se dice en porcentaje: un mando de pan al 30 % no
            //  significa nada. Izquierda, centro y derecha, que es lo que la
            //  mesa de mezclas escribe al lado.
            plock (panPasoSlider, AudioEngine::plockPan, [] (int pct)
                   {
                       const int lado = pct * 2 - 100;
                       if (lado == 0) return juce::String ("C");
                       return Lang::ltr ((lado < 0 ? juce::String ("L") : juce::String ("R"))
                                         + juce::String (std::abs (lado)));
                   });
        }
    }

    //  Las tres herramientas del PATRON, en la pagina PASO. Ver patLeftBtn.
    for (auto* b : { &patLeftBtn, &patRightBtn, &patDoubleBtn })
    {
        styleButton (*b, kKey);
        seqSheet.addAndMakeVisible (*b);
    }
    patLeftBtn.onClick   = [this] { rotatePattern (-1); };
    patRightBtn.onClick  = [this] { rotatePattern (+1); };
    patDoubleBtn.onClick = [this] { doublePattern(); };

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

    //  LA CURVA DEL EQ, que ocupa el plato cuando el efecto que tiene los
    //  mandos es el suyo. Nace INVISIBLE: quien la enciende es `focusFx`, que
    //  es el unico sitio que sabe que efecto esta delante.
    eqCurva.setFuente (&eqEspejo);
    eqCurva.onBanda = [this] (int b, float hz, float dB) { ponBandaEq (b, hz, dB); };
    //  MANTENER sobre un nodo abre su ficha: el tipo de campana y la Q. El
    //  gesto se reparte por el ESTADO como en la fila de efectos -tocar mueve,
    //  mantener abre- y la curva lo cancela en cuanto el dedo se mueve seis
    //  pixeles, o cada arrastre acabaria abriendo un menu al soltar.
    eqCurva.onNodo = [this] (int b) { abreBandaEq (b); };
    //  Y avisa a la barra de estado como cualquier otro mando: sin eso, mover
    //  un nodo es el unico gesto de la cara que no dice lo que acaba de hacer.
    eqCurva.onTouch = [this] (bool cogido)
    {
        if (cogido) status.setText (T ("EQ - arrastra el nodo"), juce::dontSendNotification);
        //  Y al soltar publica, igual que los tres mandos y por lo mismo.
        else if (autoArmado) publicaAutomacion();
    };
    addChildComponent (eqCurva);
    //  Y la miniatura de los otros diez, en el mismo plato. Ver FxMini.h.
    addChildComponent (platoMini);

    // Six effects, six buttons, one row. A button IS its effect: tapping it
    // hands the three CTRL knobs that effect's three parameters, tapping the
    // one that already has them switches it off. No slots to re-assign, no
    // bank chips above the knobs — those were three ways to reach one delay,
    // which is how there came to be two of them.
    {
        for (int f = 0; f < kNumRanuras; ++f)
        {
            //  El rotulo lo pone `refrescaRanuras`, que es quien sabe que hay
            //  en la ranura: aqui se nace con el del tipo que le toca por
            //  orden y esa es la ultima vez que el sitio decide el contenido.
            auto* b = new HoldButton (fxDefs[f].name);
            styleButton (*b, kKey);
            litAccent (*b);
            //  `f` es la RANURA y no el efecto. Quien traduce lo uno en lo
            //  otro es `ranuraTocada`, que ademas es quien sabe que hacer con
            //  una vacia.
            b->onClick = [this, f] { ranuraTocada (f); };
            //  Hold to take the knobs without touching the switch: the only
            //  way to tune an effect that is already running now that a tap
            //  always means on/off.
            b->onHold  = [this, f] { ranuraMantenida (f); };
            addAndMakeVisible (b);
            fxButtons.add (b);
        }

        //  LAS SEIS RANURAS NACEN VACIAS, que es lo que NUEVO ya hacia.
        //
        //  Nacian LLENAS y en orden -FLT HPF DRV DLY BIT REV- y eso dejaba la
        //  maquina con DOS caras distintas para «vacia»: una instalacion limpia
        //  enseñaba seis efectos que nadie habia puesto y NUEVO dejaba seis
        //  huecos. El argumento de la tanda de las ranuras estaba escrito y se
        //  aplico a un solo camino: *«una maquina recien abierta enseñaba seis
        //  efectos que nadie habia puesto, con el mismo argumento que ya costo
        //  una medida en los envios: una mezcla se hace subiendo lo que
        //  quieres, no apagando lo que no»*.
        //
        //  Y no rompe lo de antes, que es lo unico que habia que comprobar: un
        //  proyecto SIN la propiedad `slots` no depende de este valor - lo
        //  repone `applyState`, con la fila de siempre, en su propia rama.
        //  Aqui lo que se decide es con que abre una maquina en la que nadie ha
        //  guardado nada todavia.
        //
        //  `std::array<int,6> {}` seguiria estando mal: cero es un tipo VALIDO
        //  -FLT- y eso es el fallo de `notaViva` y del cero de `padAncho`. Se
        //  escribe la sentinela a mano.
        for (auto& fila : slotFx) fila.fill (kSlotVacia);
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
        //  Arrastrar un asa estrecha o ensancha la ventana, y el fundido esta
        //  acotado a un tercio de ella: sin esto, cerrar el recorte dejaba
        //  dibujada la rampa ancha de antes mientras la voz ya tocaba la
        //  estrecha.
        pushFadesToWaveform();
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

        //  Y EL ANCHO AL LADO DEL PAN, que es la otra mitad de donde se pone un
        //  sonido: el pan dice DONDE esta y el ancho CUANTO ocupa, y sin los
        //  dos no hay forma de estrechar un break que se come el centro ni de
        //  abrir un colchon que suena plano sin ir pad por pad a EL PAD.
        //
        //  El motor no crece: `setPadAncho` existe desde la tanda del ancho
        //  estereo -medio/lado antes del pan, acotado 0..2 con uno de defecto-
        //  asi que esto es una segunda ventana al MISMO numero, como el pan.
        //
        //  Y APAGADO EN UNA MUESTRA MONO, que es lo que ya hace su mando en EL
        //  PAD: sin lado que abrir ni cerrar, un mando que se mueve y no hace
        //  nada es peor que no tenerlo.
        auto* an = new juce::Slider();
        an->setSliderStyle (juce::Slider::LinearHorizontal);
        an->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        an->setRange (0.0, 2.0, 0.01);
        an->setValue (padAnchoUI[(size_t) i], juce::dontSendNotification);
        an->setDoubleClickReturnValue (true, 1.0);
        an->setColour (juce::Slider::trackColourId, ZatiColours::inkDim.withAlpha (0.55f));
        an->getProperties().set ("pan", true);
        an->setSliderSnapsToMousePosition (false);
        an->onValueChange = [this, i, an]
        {
            padAnchoUI[(size_t) i] = (float) an->getValue();
            engine.setPadAncho (i, (float) an->getValue());
            if (i == selectedPad) anchoSlider.setValue (an->getValue(), juce::dontSendNotification);
        };
        mixRows.addAndMakeVisible (an);
        mixAnchos.add (an);

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
    //  Y LAS DIECISEIS TIRAS DE CANAL, en el mismo Viewport y con la misma
    //  forma de fila: color, numero, fader, `M` y cuantos pads le entran. Sin
    //  pan —el sitio en la imagen es del pad, que es lo que se coloca— y sin
    //  SOLO, que se queda donde ya estaba.
    for (int c = 0; c < kNumCanales; ++c)
    {
        auto* f = new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
        f->setRange (kGainMinDb, kGainMaxDb, 0.1);
        f->setValue (0.0, juce::dontSendNotification);
        f->setSkewFactorFromMidPoint (-9.0);
        f->setDoubleClickReturnValue (true, 0.0);
        f->setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        f->setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        f->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        f->setColour (juce::Slider::trackColourId, Zati::colour (c));
        f->setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, Metrics::readout);
        f->setSliderSnapsToMousePosition (false);
        f->textFromValueFunction = [] (double v) { return gainText (v, false); };
        f->onValueChange = [this, c, f] { engine.setCanalGain (c, gainFromDb (f->getValue())); };
        mixRows.addAndMakeVisible (f);
        canFaders.add (f);

        auto* m = new juce::TextButton ("M");
        styleButton (*m, kStepOff);
        m->setColour (juce::TextButton::buttonOnColourId, ZatiColours::red);
        m->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        m->setClickingTogglesState (true);
        m->onClick = [this, c, m] { engine.setCanalMute (c, m->getToggleState()); refreshMixStrip(); };
        mixRows.addAndMakeVisible (m);
        canMutes.add (m);
    }

    //  EL INTERRUPTOR DE VISTA, en el renglon del titulo y no en la fila de
    //  chips: mismo estilo, mismo gesto y mismo sitio que el de CANCION, que
    //  es la otra ficha con dos vistas de la misma rejilla.
    styleButton (mixVistaBtn, kKey);
    litAccent (mixVistaBtn);
    mixVistaBtn.onClick = [this]
    {
        showMixPage (mixPage == mixPageCanales ? mixPagePads : mixPageCanales);
    };
    mixSheet.addAndMakeVisible (mixVistaBtn);

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
    //  El cuerpo pinta el contenido de SU ficha, asi que lleva su numero de
    //  capa. Ver ManualBody y UiAudit::capaActual.
    manualBody.capa = (int) manualSheet.getProperties()["capa"];
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
    setSheet.cuerpo.addAndMakeVisible (manualButton);

    //  EL TOUR DE BIENVENIDA. Ver la declaracion: cinco tarjetas y sale una
    //  vez. Las tres tapas se maquetan con layoutModuleBar, que reparte por el
    //  texto - "SIGUIENTE" mide casi el doble que "ATRAS" y a tercios se
    //  cortaba en aleman de cualquiera de las cuatro lenguas.
    tourSheet.setVisible (false);
    //  Tocar FUERA de la tarjeta NO lo cierra, a diferencia de todas las
    //  demas: es lo primero que ve alguien que acaba de instalar la app y
    //  cerrarse por un roce deja la maquina sin explicar y sin forma evidente
    //  de volver. Se sale por SALTAR o llegando al final.
    tourSheet.pintaTodo = true;
    tourSheet.onDismiss = nullptr;
    tourSheet.paintContent = [this] (juce::Graphics& g) { paintTourSheetContent (g); };
    for (auto* b : { &tourBackBtn, &tourNextBtn, &tourSkipBtn })
    {
        styleButton (*b, kKey);
        tourSheet.addAndMakeVisible (*b);
    }
    litAccent (tourNextBtn);
    tourBackBtn.onClick = [this] { showTour (tourPaso - 1); };
    tourNextBtn.onClick = [this]
    {
        if (! tourEsLaPuerta() && tourPaso + 1 < kTourPasos) { showTour (tourPaso + 1); return; }
        //  Al final se marca visto y se cierra. Ya lo esta desde que se enseño
        //  -ver el arranque- asi que esto es idempotente; se deja porque el
        //  tour tambien se abre a mano desde AJUSTES y acabarlo por ahi tiene
        //  que dejar la misma marca.
        ProjectStore::escribeTexto (tourFile(), "1");
        closeAllSheets();
    };
    tourSkipBtn.onClick = [this]
    {
        //  La marca se escribe en los dos caminos, y en el de la puerta
        //  tambien: quien se va a los once ya ha visto la bienvenida, y sin
        //  esto cerrar a mitad del recorrido largo la traeria manana entera.
        ProjectStore::escribeTexto (tourFile(), "1");
        if (tourEsLaPuerta()) { showTour (kTourBienvenida); return; }
        closeAllSheets();
    };
    addAndMakeVisible (tourSheet);
    tourSheet.setVisible (false);

    styleButton (tourButton, kKey);
    tourButton.onClick = [this] { closeAllSheets(); showTour (0); openSheet (tourSheet, setButton); };
    setSheet.cuerpo.addAndMakeVisible (tourButton);

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

    //  EL MASTER. En decibelios y con los mismos ayudantes que los otros
    //  diecisiete faders de la app: un mando que se lee en una escala distinta
    //  de los que tiene al lado obliga a traducir de cabeza cada vez.
    //
    //  Y el tope es 0 dB, que no es timidez: por encima de la unidad lo unico
    //  que se gana es empujar el limitador del master, que es donde se pierde
    //  el golpe. Para sonar mas alto esta el volumen del telefono, que no
    //  distorsiona.
    masterFader.setSliderStyle (juce::Slider::LinearHorizontal);
    masterFader.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, Metrics::readout);
    masterFader.setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
    masterFader.setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
    masterFader.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    //  Hasta +12 como cualquier canal, y no hasta 0: ver AudioEngine::
    //  kMasterMaxGain. Un patron de la fabrica llega a 0.566 de pico, o sea
    //  que la maquina tenia cinco decibelios de margen y ningun mando que los
    //  usara. El doble clic sigue devolviendo a 0 dB, que es donde nace.
    masterFader.setRange (kGainMinDb, kGainMaxDb, 0.1);
    masterFader.setValue (0.0, juce::dontSendNotification);
    masterFader.setDoubleClickReturnValue (true, 0.0);
    //  Un roce no puede ser un valor, y aqui menos que en ningun otro sitio:
    //  saltar al punto tocado con el dedo en el borde izquierdo deja la maquina
    //  MUDA de golpe y nadie sabe por que.
    masterFader.setSliderSnapsToMousePosition (false);
    masterFader.textFromValueFunction = [] (double v) { return gainText (v, true); };
    masterFader.onValueChange = [this]
    {
        masterUserGain = gainFromDb (masterFader.getValue());
        engine.setMasterUser (masterUserGain);
        //  EL FICHERO SE ESCRIBE AL SOLTAR, no en cada valor. Esto llamaba a
        //  `saveMasterPref` -temporal, validador y renombrado, o sea tres o
        //  cuatro syscalls- UNA VEZ POR FOTOGRAMA DE ARRASTRE, en el
        //  almacenamiento compartido de un telefono. Y el `mixSheet.repaint()`
        //  que habia detras pedia la ficha entera para un mando que se repinta
        //  solo: nada de lo que pinta esa ficha depende del nivel del master.
        masterPrefSucio = true;
    };
    masterFader.onDragEnd = [this] { guardaMasterSiHaceFalta(); };
    mixSheet.addAndMakeVisible (masterFader);

    masterLabel.setText (T ("MASTER"), juce::dontSendNotification);
    masterLabel.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
    masterLabel.setColour (juce::Label::textColourId, ZatiColours::ink);
    masterLabel.setJustificationType (Lang::start());
    masterLabel.setInterceptsMouseClicks (false, false);
    mixSheet.addAndMakeVisible (masterLabel);
    //  Y se lee YA, no al abrir la mesa: el nivel tiene que estar puesto en el
    //  primer bloque de audio y la mesa puede no abrirse nunca.
    loadMasterPref();

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
            if (mixAnchos[i] != nullptr) mixAnchos[i]->setValue (padAnchoUI[(size_t) i], juce::dontSendNotification);
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

    //  LA BROCHA CICLA: PATRON · SONIDO · CLIP.
    //
    //  Con las dos familias en la misma rejilla hace falta poder decir «lo que
    //  suelto aqui es un CLIP», y eso es un estado de la brocha y no una tapa
    //  nueva: la fila ya esta medida y una septima tapa la parte en dos. La
    //  tapa sigue diciendo el ESTADO, que es lo que ya hacia.
    styleButton (songPadModeBtn, kStepOff);
    litAccent (songPadModeBtn);
    songPadModeBtn.setClickingTogglesState (true);
    songPadModeBtn.onClick = [this]
    {
        songPincel = (songPincel + 1) % 3;
        if (songPincel == 0) songBrush = juce::jmax (1, songBrush);   // vuelve al patron
        else                 songBrush = -(juce::jmax (0, selectedPad) + 1);
        refreshSong();
    };
    songSheet.addAndMakeVisible (songPadModeBtn);

    //  VACIAR se retiro: es la GOMA de la barra de herramientas. Dos tapas
    //  para borrar eran dos duenos de la misma funcion, y la que se queda es
    //  la que ademas se VE armada - que es lo que un modo tiene que hacer.

    styleButton (songDoubleBtn, kKey);
    songDoubleBtn.onClick = [this] { doubleSong(); };
    songSheet.addAndMakeVisible (songDoubleBtn);

    //  Las cinco herramientas de arreglo. Ver songCursor: las cuatro primeras
    //  actuan sobre el compas marcado y sobre los cuatro carriles a la vez.
    for (auto* b : { &songLeftBtn, &songRightBtn, &songShortBtn, &songLongBtn,
                     &songInsertBtn, &songRemoveBtn, &songCopyBtn, &songPasteBtn })
    {
        styleButton (*b, kKey);
        songSheet.addAndMakeVisible (*b);
    }
    songShortBtn.onClick = [this] { resizeSongBlock (-1); };
    songLongBtn.onClick  = [this] { resizeSongBlock (+1); };
    songLeftBtn.onClick  = [this] { moveSongBar (-1); };
    songRightBtn.onClick = [this] { moveSongBar (+1); };
    songInsertBtn.onClick = [this] { insertSongBar(); };
    songRemoveBtn.onClick = [this] { removeSongBar(); };
    songCopyBtn.onClick   = [this] { copySongBar(); };
    songPasteBtn.onClick  = [this] { pasteSongBar(); };
    songPasteBtn.setEnabled (false);      // hasta que haya algo copiado

    styleButton (songPlayBtn, kKey);
    litAccent (songPlayBtn);
    songPlayBtn.setClickingTogglesState (true);
    songPlayBtn.onClick = [this]
    {
        const bool on = songPlayBtn.getToggleState();
        //  El modo CANCION se enciende con el transporte y no aparte: esta
        //  tapa esta en la pagina de la cancion, y lo que se espera de ella es
        //  oir la cancion.
        if (on && ! engine.isSongMode())
            ponModoCancion (true);
        ponTransporte (on);
    };
    songSheet.addAndMakeVisible (songPlayBtn);

    styleButton (songLoopBtn, kStepOff);
    songLoopBtn.setColour (juce::TextButton::buttonOnColourId, ZatiColours::green);
    songLoopBtn.setClickingTogglesState (true);
    songLoopBtn.onClick = [this] { toggleSongLoop(); };
    songSheet.addAndMakeVisible (songLoopBtn);

    styleButton (songModeBtn, kStepOff);
    litAccent (songModeBtn);
    songModeBtn.setClickingTogglesState (true);
    songModeBtn.onClick = [this] { ponModoCancion (songModeBtn.getToggleState()); };
    songSheet.addAndMakeVisible (songModeBtn);

    //  LAS OTRAS DOS, una por fila de transporte. Ver ponModoCancion.
    for (auto* b : { &modoBtn, &seqModoBtn })
    {
        styleButton (*b, kStepOff);
        litAccent (*b);
        b->setClickingTogglesState (true);
        b->onClick = [this, b] { ponModoCancion (b->getToggleState()); };
    }
    addAndMakeVisible (modoBtn);
    seqSheet.addAndMakeVisible (seqModoBtn);

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

    //  LA BARRA DE LA LINEA DE TIEMPO, en el sitio de la fila de paginas.
    //
    //  Escribe el primer compas visible y refresca; no toca el zoom, que es de
    //  la tapa de al lado. Una funcion, un dueno.
    songBarra.onMueve = [this] (int primero)
    {
        songPrimerCompas = primero;
        refreshSong();
    };
    songSheet.addAndMakeVisible (songBarra);

    //  LA CANALETA SILENCIA, y en la vista de audio silencia la PISTA. Es el
    //  mismo sitio con el mismo significado en las dos, que es lo que hace que
    //  no haya nada nuevo que aprender: lo que cambia es a quien se lo dice.
    //  LAS CUATRO HERRAMIENTAS, en iconos y sin rotulo.
    //
    //  Sin rotulo a proposito: `reparteTapa` admite ese caso desde siempre -con
    //  el rotulo vacio el dibujo se lleva la tapa entera- y es lo que hace que
    //  cuatro tapas ocupen 160 px en vez de los 300 y pico que piden MOVER,
    //  LAPIZ, GOMA y SILENCIAR escritos. El nombre para quien no ve la pantalla
    //  lo pone `setTitle`, que es lo que lee TalkBack: una tapa sin rotulo se
    //  anunciaria por su clase, o sea «boton».
    //
    //  Y la GOMA no es una tapa nueva: es la brocha VACIAR de siempre, con su
    //  dibujo y vista como lo que es -un modo armado-. Dos sitios para borrar
    //  serian dos duenos de la misma funcion.
    {
        for (int i = 0; i < kNumHerramientas; ++i)
        {
            auto* b = new juce::TextButton();
            styleButton (*b, kStepOff);
            litAccent (*b);
            b->getProperties().set ("icono", (int) kHerramientas[i].ico);
            b->setClickingTogglesState (true);
            //  EL VALOR SALE DE LA TABLA Y NO DEL INDICE DE LA TAPA. La primera
            //  version paso `i`, y el orden en pantalla -MANO, LAPIZ, GOMA,
            //  MUTE- no es el del enum -lapiz, goma, mano, mute-: la tapa de
            //  MOVER armaba el LAPIZ. El banco lo canto entero, `estirado
            //  [7,2]`, que es exactamente lo que pinta un lapiz arrastrado.
            const int h = kHerramientas[i].id;
            b->onClick = [this, h] { ponHerramienta (h); };
            songSheet.addAndMakeVisible (b);
            songToolBtns.add (b);
        }
    }

    //  EL ZOOM DE LA LINEA DE TIEMPO. La vista estaba clavada en ocho compases
    //  y por eso un estribillo de dieciseis no se podia mirar entero. Una tapa
    //  que CICLA y no tres -8, 16, 4- que es lo que ya hace el zoom del piano,
    //  y por lo mismo: tres tapas en el renglon del titulo no caben, y aqui
    //  ademas la fila la comparte con el interruptor de vista.
    {
        styleButton (songZoomBtn, kStepOff);
        songZoomBtn.onClick = [this]
        {
            //  EL CICLO SALTA EL PASO QUE NO CABE, con la misma pregunta que ya
            //  deciden BANCO, PADS y la tira del paso: a dieciseis compases la
            //  celda cae a la mitad de ancho, y una celda que se pinta con el
            //  dedo arrastrado por debajo de su suelo es una celda que se
            //  falla. Se prueban los pasos en orden y se coge el primero que
            //  quepa; si no cabe ninguno, se queda donde esta.
            static const int pasos[] = { Playlist::kBarsViewDef, 16, 4 };
            const int n = (int) (sizeof (pasos) / sizeof (pasos[0]));
            int donde = 0;
            for (int i = 0; i < n; ++i) if (pasos[i] == songGrid.getCompasesVista()) donde = i;
            for (int k = 1; k <= n; ++k)
            {
                const int cand = pasos[(donde + k) % n];
                if (cand == songGrid.getCompasesVista() || songGrid.cabeVista (cand))
                {
                    ponVistaCompases (cand);
                    return;
                }
            }
        };
        songSheet.addAndMakeVisible (songZoomBtn);
    }

    //  UN CARRIL, UN SILENCIO. El motor guarda DOS mascaras -los carriles de
    //  patron y las pistas de audio- y las dos son correctas: son dos caminos
    //  de render distintos. Lo que dejo de ser verdad es que sean dos COSAS
    //  para quien mira, desde que el carril 1 y la pista 1 son el mismo carril.
    //  La canaleta escribe las dos; `Playlist.h` avisaba de compartir el numero
    //  SIN QUERER y aqui se comparte a proposito.
    songGrid.onLane = [this] (int lane)
    {
        const int p = juce::jlimit (0, AudioEngine::kAudioTracks - 1, lane);
        const bool ahora = ! engine.isSongLaneMuted (juce::jlimit (0, Playlist::kLanes - 1, lane));
        engine.setPistaMute (p, ahora);
        toggleSongLane (lane);
    };

    //  GRABAR AL ARREGLO Y EL METRONOMO, en la vista de audio.
    {
        //  NACEN VISIBLES desde que la ficha tiene una sola vista: eran de la
        //  de AUDIO y por eso se creaban apagadas -puestas se quedaban visibles
        //  y de 0x0 hasta que alguien tocara la pestaña, 56 hallazgos del
        //  banco-. Con una rejilla sola no hay pestaña que tocar.
        styleButton (songRecBtn, kKey);
        songRecBtn.onClick = [this] { grabaAlArreglo(); };
        songSheet.addAndMakeVisible (songRecBtn);

        styleButton (songClickBtn, kKey);
        litAccent (songClickBtn);
        songClickBtn.setClickingTogglesState (true);
        songClickBtn.onClick = [this]
        {
            engine.setClick (songClickBtn.getToggleState());
            //  Y SE RECUERDA. Esta tapa ES la opcion de metronomo -por eso no
            //  hay una segunda casilla en AJUSTES- y una opcion que se olvida
            //  al cerrar la app no es una opcion.
            saveCuentaPref();
        };
        songSheet.addAndMakeVisible (songClickBtn);

        //  AUTO, la tercera de la fila de grabar. Va aqui y no en la cara por
        //  lo mismo que GRABAR y el clic: la automatizacion es de la CANCION
        //  -sus eventos van en pasos de la linea de tiempo- y la fila ya
        //  existe, asi que no cuesta un pixel de alto.
        //
        //  TOCAR ARMA, MANTENER BORRA. Es un `HoldButton` -el mismo que las
        //  seis tapas de efecto- y por la misma razon: vaciar la
        //  automatizacion es lo unico de esta funcion que no se deshace
        //  tocando otra vez, asi que no puede compartir gesto con armarla. Y
        //  cuesta CERO de ancho, que es lo que no hay en esta fila: una
        //  septima tapa la parte en dos y le quita 44 px a los carriles.
        styleButton (autoBtn, kKey);
        litAccent (autoBtn);
        autoBtn.onClick = [this] { ponAutoArmado (! autoArmado); };
        autoBtn.onHold  = [this]
        {
            //  Desarmar antes de vaciar: si no, el primer mando que se toque
            //  despues vuelve a escribir sobre lo que se acaba de borrar.
            if (autoArmado) ponAutoArmado (false);
            vaciaAutomacion();
        };
        songSheet.addAndMakeVisible (autoBtn);
    }

    songGrid.onClipNuevo = [this] (int pista, int compas) { ponClip (pista, compas); };
    songGrid.onClipMueve = [this] (int i, int pista, int compas) { mueveClip (i, pista, compas); };
    songGrid.onClipQuita = [this] (int i) { quitaClip (i); };
    songGrid.onClipLargo = [this] (int i, int d, int h) { largoClip (i, d, h); };


    //  EL FILO DE UN BLOQUE LO ESTIRA, por el MISMO camino que las tapas
    //  ACORTAR y ALARGAR: dos caminos que calculan el largo por su cuenta se
    //  separan, y el sintoma seria un bloque que se come al vecino arrastrando
    //  y no con la tapa. Ver `ponLargoBloque`.
    //  MOVER UN BLOQUE ENTERO. Se quita de donde estaba y se pone donde cae,
    //  y en ese orden: escribir primero y borrar despues se llevaria por
    //  delante lo que se acaba de poner cuando el destino solapa al origen -
    //  que es exactamente lo que pasa al arrastrar un compas. Es la misma
    //  leccion que el mover del piano, donde quitar todas antes de poner es lo
    //  que impide que un acorde pierda una nota.
    songGrid.onMueveBloque = [this] (int carril, int cabeza, int nuevoCarril, int nuevoCompas, bool primero)
    {
        const int len = engine.getSongLength();
        int largo = 1;
        while (cabeza + largo < len
               && engine.getSongCell (carril, cabeza + largo) == AudioEngine::kContinued) ++largo;
        if (nuevoCompas < 0 || nuevoCompas + largo > len) return;

        //  Y NO SE COME AL VECINO, que es la misma regla que ya tiene ALARGAR:
        //  pisar un bloque que hay debajo seria borrar algo que nadie ha pedido
        //  borrar, y para eso esta la goma. Se mira TODO el tramo de destino
        //  saltandose el origen, que se va a quedar vacio.
        for (int b = nuevoCompas; b < nuevoCompas + largo; ++b)
        {
            const bool esMio = (nuevoCarril == carril && b >= cabeza && b < cabeza + largo);
            if (! esMio && engine.getSongCell (nuevoCarril, b) != 0) return;
        }

        if (primero) pushUndo (T ("MOVER"));

        const int v = engine.getSongCell (carril, cabeza);
        const bool mudo = engine.isSongCellMuted (carril, cabeza);
        for (int b = cabeza; b < cabeza + largo; ++b)
        {
            engine.setSongCell (carril, b, 0);
            engine.setSongCellMute (carril, b, false);
        }
        engine.setSongCell (nuevoCarril, nuevoCompas, v);
        engine.setSongCellMute (nuevoCarril, nuevoCompas, mudo);
        for (int b = nuevoCompas + 1; b < nuevoCompas + largo; ++b)
            engine.setSongCell (nuevoCarril, b, AudioEngine::kContinued);

        songCursor = nuevoCompas;
        refreshSong();
    };

    //  SILENCIAR UN BLOQUE. Se apunta en la CABEZA, que es donde el motor lo
    //  lee: una continuacion no adopta patron, asi que silenciarla no callaria
    //  nada y el bloque seguiria sonando con la tapa encendida.
    songGrid.onMuteBloque = [this] (int carril, int cabeza)
    {
        const bool nuevo = ! engine.isSongCellMuted (carril, cabeza);
        pushUndo (T ("SILENCIAR"));
        engine.setSongCellMute (carril, cabeza, nuevo);
        refreshSong();
        status.setText (nuevo ? T ("Bloque en silencio") : T ("Bloque suena"),
                        juce::dontSendNotification);
    };

    songGrid.onLargoBloque = [this] (int carril, int cabeza, int largo, bool primero)
    {
        ponLargoBloque (carril, cabeza, largo, primero);
    };

    songGrid.onCell = [this] (int lane, int bar)
    {
        //  El compas que tocas es el compas sobre el que actuan las
        //  herramientas. Sin esto habria que inventar un segundo gesto para
        //  decir "aqui", y el gesto que ya existe dice exactamente eso.
        songCursor = bar;

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

    // --- PIANO ROLL: las notas del pad, en tono contra tiempo ---------------
    {
        //  LA COLUMNA NO ES EL PASO, y este era el unico de los cuatro gestos
        //  que se lo creia.
        //
        //  El piano dibuja UN compas -el `selectedBar`- en dieciseis columnas,
        //  asi que la columna 0 del compas 2 es el paso 16. Estirar, borrar y
        //  cortar ya sumaban la base; poner una nota no, y llamaba a
        //  pianoCellToggled con la columna como si fuera el paso absoluto.
        //
        //  El sintoma no era escribir en el compas equivocado: era que la
        //  rejilla NO RESPONDIA. Se escribia en el compas 1, la vista estaba en
        //  el 2, y refreshPiano volvia a pintar el 2 sin la nota. Y la
        //  proteccion de pianoCellToggled -paso >= largo del patron- tampoco
        //  saltaba, porque una columna siempre vale menos de dieciseis.
        pianoGrid.onCelda = [this] (int paso, int semi)
        {
            pianoCellToggled (seqPrimerPaso + paso, semi);
        };
        //  ESTIRAR UNA NOTA. El largo es del PASO y no de cada nota del acorde:
        //  las cuatro notas de una columna son un acorde y un acorde dura lo
        //  que dura, no cuatro cosas distintas.
        pianoGrid.onLargo = [this] (int paso, int, int cuartos)
        {
            if (selectedPad < 0) return;
            const int st = seqPrimerPaso + paso;
            if (st < 0 || st >= engine.getPatternLength (selectedPattern)) return;
            engine.setStepLen (selectedPattern, st, selectedPad, cuartos);
            refreshPiano();
        };
        //  El teclado SUENA y no escribe: buscar la nota antes de ponerla es
        //  la mitad de escribir una melodia.
        //  LA SELECCION: la banda, el arrastre del bloque y el vaciado. El
        //  componente reporta el gesto y aqui se resuelve contra los datos.
        pianoGrid.estaSel    = [this] (int paso, int semi) { return pianoEnSel (paso, semi); };
        pianoGrid.onBanda    = [this] (int p0, int s0, int p1, int s1) { pianoBanda (p0, s0, p1, s1); };
        pianoGrid.onMueveSel = [this] (int dp, int ds) { pianoMueveSel (dp, ds); };
        pianoGrid.onVaciaSel = [this] { pianoVaciaSel(); moviendoSel = false; resized(); };

        pianoGrid.onTecla = [this] (int semi)
        {
            if (selectedPad >= 0)
                engine.postNoteOnAt (selectedPad, semi, 0.9f);
        };
        //  EL PIANO VIVE DENTRO DE LA FICHA DEL SECUENCIADOR.
        //
        //  Tenia ficha propia y escribia EXACTAMENTE lo mismo que la rejilla de
        //  pasos - las notas del patron - desde otro sitio: dos popups para un
        //  trabajo, que es lo que esta app no permite. Escribir notas es un
        //  trabajo con dos vistas: pasos y piano. Ahora son dos paginas de la
        //  misma ficha, y la pestana que ocupaban los ajustes del paso -que ya
        //  no existen ahi- es la que las separa.
        seqSheet.addAndMakeVisible (pianoGrid);

        for (auto* b : { &pianoOctDownBtn, &pianoOctUpBtn, &pianoClearBtn })
        {
            styleButton (*b, kKey);
            seqSheet.addAndMakeVisible (*b);
        }
        //  La ventana se mueve de OCTAVA en octava y no de semitono en
        //  semitono: mover doce filas de una es lo que hace que la vista
        //  siga siendo la misma vista - las teclas negras caen igual.
        //  EL RECORRIDO ES EL DEL MOTOR, no una octava por debajo y ninguna
        //  por encima. setStepNote acota en +-24 semitonos, o sea cuatro
        //  octavas de rango, y la ventana ensena veinticinco filas: con el tope
        //  de arriba en 0 la mitad de agudo del pad era inalcanzable desde el
        //  piano - se podia escribir un -24 y no un +24.
        //  El tope de arriba sale de CUANTAS filas se ven -ver PianoRoll::baseMax-
        //  y no de un doce escrito a mano: con dos octavas a la vista, una base
        //  de +12 dibujaria hasta +36 y las doce de arriba escribirian notas que
        //  setStepNote recorta a +24. Doce filas que mienten, que es el fallo
        //  que ya tuvo esta pagina cuando ensenaba veinticinco siempre.
        pianoOctDownBtn.onClick = [this] { pianoBase = juce::jmax (-24, pianoBase - 12); refreshPiano(); };
        pianoOctUpBtn.onClick   = [this] { pianoBase = juce::jmin (pianoGrid.baseMax(), pianoBase + 12); refreshPiano(); };
        pianoClearBtn.onClick   = [this]
        {
            if (selectedPad < 0) return;
            pushUndo (T ("VACIAR"));
            for (int st = 0; st < kNumSteps; ++st)
            {
                pattern[(size_t) selectedPattern][(size_t) st][(size_t) selectedPad] = false;
                engine.setStep (selectedPattern, st, selectedPad, false);
                //  Los nueve campos, no la nota y el acorde: ver
                //  AudioEngine::vaciaPaso. Se dejaba la fuerza, el redoble, el
                //  largo, el empujon, el bloqueo y los cuatro empaquetados, asi
                //  que la fila "vaciada" seguia llevando la mitad de la vieja.
                engine.vaciaPaso (selectedPattern, st, selectedPad);
            }
            refreshPiano();
            refreshStepGrid();
        };

        for (auto* b : { &pianoPadDownBtn, &pianoPadUpBtn })
        {
            styleButton (*b, kKey);
            seqSheet.addAndMakeVisible (*b);
        }
        pianoPadDownBtn.onClick = [this] { pianoStepPad (-1); };
        pianoPadUpBtn  .onClick = [this] { pianoStepPad ( 1); };

        //  GOMA y TIJERAS. Excluyentes entre si y las dos apagadas por defecto:
        //  una herramienta que se queda puesta sin verse es como se borra media
        //  melodia sin querer, asi que la tapa se enciende con el acento.
        for (auto* b : { &pianoLapizBtn, &pianoGomaBtn, &pianoCorteBtn })
        {
            styleButton (*b, kKey);
            litAccent (*b);
            b->setClickingTogglesState (true);
            seqSheet.addAndMakeVisible (*b);
        }
        auto ponUtil = [this] (int cual)
        {
            pianoGrid.setHerramienta (cual);
            pianoLapizBtn.setToggleState (cual == PianoRoll::lapiz,   juce::dontSendNotification);
            pianoGomaBtn .setToggleState (cual == PianoRoll::goma,    juce::dontSendNotification);
            pianoCorteBtn.setToggleState (cual == PianoRoll::tijeras, juce::dontSendNotification);
            pianoSelBtn  .setToggleState (cual == PianoRoll::sel,     juce::dontSendNotification);
            //  Al salir del modo, la seleccion se va con el: dejarla puesta
            //  seria un bloque marcado que ya no se puede mover ni copiar, o
            //  sea una marca que miente.
            if (cual != PianoRoll::sel) pianoVaciaSel();
            resized();      // COPIAR y PEGAR solo existen con seleccion
        };
        //  LAS TRES SON EXCLUYENTES y las tres se apagan: sin ninguna, un toque
        //  ALTERNA -escribe donde no hay y quita donde hay-, que es como
        //  funcionaba y es lo mas corto para corregir una nota. El LAPIZ solo
        //  escribe, y con el se puede pintar por encima de lo que ya hay sin
        //  borrarlo, que es lo que un arrastre hacia antes sin querer.
        pianoLapizBtn.onClick = [this, ponUtil]
            { ponUtil (pianoLapizBtn.getToggleState() ? PianoRoll::lapiz   : PianoRoll::dibujar); };
        pianoGomaBtn.onClick  = [this, ponUtil]
            { ponUtil (pianoGomaBtn.getToggleState()  ? PianoRoll::goma    : PianoRoll::dibujar); };
        pianoCorteBtn.onClick = [this, ponUtil]
            { ponUtil (pianoCorteBtn.getToggleState() ? PianoRoll::tijeras : PianoRoll::dibujar); };

        //  Y LA CUARTA: SELECCIONAR. Es una herramienta y no un gesto nuevo -
        //  ver PianoRoll::sel. Con ella arrastrar sobre vacio marca un bloque y
        //  arrastrar desde una nota marcada lo mueve entero.
        styleButton (pianoSelBtn, kKey);
        litAccent (pianoSelBtn);
        pianoSelBtn.setClickingTogglesState (true);
        seqSheet.addAndMakeVisible (pianoSelBtn);
        pianoSelBtn.onClick = [this, ponUtil]
            { ponUtil (pianoSelBtn.getToggleState() ? PianoRoll::sel : PianoRoll::dibujar); };

        //  COPIAR y PEGAR NACEN APAGADAS: solo existen con seleccion puesta,
        //  que es la regla de la tira del paso - un control que no puede hacer
        //  nada no es informacion, es ruido - y ademas es lo que hace que no
        //  cuesten sitio en una fila de nueve tapas que ya se parte en dos.
        for (juce::TextButton* b : { &pianoCopiaBtn, &pianoPegaBtn })
        {
            styleButton (*b, kKey);
            seqSheet.addChildComponent (*b);
        }
        pianoCopiaBtn.onClick = [this] { pianoCopiaSel(); };
        pianoPegaBtn.onClick  = [this] { pianoPegaSel(); };

        //  EL ZOOM HORIZONTAL. Ocho columnas son medio compas con celdas del
        //  doble de ancho -que es lo que hace falta para escribir en 1/32- y
        //  treinta y dos son dos compases para ver la frase. Dieciseis es lo de
        //  siempre. La escalera la decide el suelo de la celda, como todo.
        styleButton (pianoZoomBtn, kKey);
        pianoZoomBtn.onClick = [this]
        {
            //  SE PIDE LO QUE HAY, que es la regla de siempre. Medido en
            //  412x915: con 32 columnas la celda del paso queda en **10 px**,
            //  por debajo del suelo de 12 - o sea dos compases que se ven y no
            //  se pueden escribir. Donde no caben, el ciclo salta a 8.
            const int util = pianoGrid.getWidth() - PianoRoll::kGutter;
            const bool caben32 = util >= 32 * Metrics::celdaPaso;
            pianoCols = (pianoCols == 8) ? AudioEngine::kBarSteps
                      : (pianoCols == AudioEngine::kBarSteps && caben32) ? 32 : 8;
            pianoVaciaSel();
            refreshPiano (true);
            resized();
        };
        seqSheet.addAndMakeVisible (pianoZoomBtn);

        //  CUANTAS OCTAVAS SE VEN. Las dos cuentas sirven para cosas distintas
        //  y ninguna gana siempre: con una octava la fila mide 34 px en un
        //  movil grande y una nota se coloca sin apuntar; con dos se VE una
        //  melodia entera, que es la otra mitad de para que existe un piano
        //  roll. Elegir es de quien mira y no nuestro.
        styleButton (pianoVerBtn, kKey);
        pianoVerBtn.onClick = [this]
        {
            aplicaFilasPiano (pianoGrid.getFilas() >= PianoRoll::kFilasMax
                                ? PianoRoll::kFilasMin : PianoRoll::kFilasMax);
            savePianoPref();
        };
        seqSheet.addAndMakeVisible (pianoVerBtn);

        //  BORRAR: quita la nota que se toca, y con ella el paso si era la
        //  ultima que quedaba - un paso encendido sin ninguna nota es un golpe
        //  que suena y no se ve.
        pianoGrid.onBorrar = [this] (int paso, int semi)
        {
            if (selectedPad < 0) return;
            const int st = seqPrimerPaso + paso;
            if (st < 0 || st >= engine.getPatternLength (selectedPattern)) return;

            bool tenia = false;
            for (int k = 0; k < PianoRoll::kMaxNotas; ++k)
                if (pianoCells[paso * PianoRoll::kMaxNotas + k] == (signed char) semi) tenia = true;
            //  CON EL PASO ABSOLUTO, que es la quinta vez que este offset
            //  falta en esta pagina. El guardia de arriba ya calculaba `st` y
            //  la llamada seguia pasando la COLUMNA: la goma en el compas 2
            //  borraba la nota que estuviera en el mismo sitio del compas 1 -
            //  una nota que no se toca desaparece y la que se frota se queda.
            if (tenia) pianoCellToggled (st, semi);     // quitar es lo mismo que alternar una puesta
        };

        //  CORTAR: el largo pasa a ser lo que va del arranque de la nota al
        //  dedo. Ver PianoRoll::tijeras.
        pianoGrid.onCortar = [this] (int paso, int cuartos)
        {
            if (selectedPad < 0) return;
            const int st = seqPrimerPaso + paso;
            if (st < 0 || st >= engine.getPatternLength (selectedPattern)) return;
            engine.setStepLen (selectedPattern, st, selectedPad, cuartos);
            refreshPiano();
        };

        //  LA PUERTA VIVE EN LA PAGINA DEL PAD y no en la barra de la cara.
        //
        //  La barra ya lleva seis modulos y en 280 px se parte en dos filas: un
        //  septimo la parte siempre. Y el sitio es este de todas formas - el
        //  piano roll escribe las notas de UN pad, igual que ENVIOS decide a
        //  donde va UN pad, asi que las dos puertas estan juntas en la pagina
        //  que habla de ese pad.
        styleButton (pianoButton, kKey);
        litAccent (pianoButton);
        //  Y la puerta lleva a la PAGINA del piano dentro de la ficha del
        //  secuenciador, que es donde vive ahora.
        pianoButton.onClick = [this]
        {
            openSheet (seqSheet, secButton);
            showSeqPage (seqPagePiano);
            refreshPiano();
        };
        padSheet.addAndMakeVisible (pianoButton);
    }

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

        //  LAS SEIS RANURAS otra vez, dentro de la ficha. Repetirlas aqui en
        //  vez de mandar a cerrar el panel, tocar el efecto en la cara y volver
        //  a abrirlo es la diferencia entre una superficie de directo y un
        //  cuadro de dialogo.
        //
        //  Y son las RANURAS y no los TIPOS, que desde el EQ ya no son el
        //  mismo numero. Con una tapa por tipo esta fila crecia sola cada vez
        //  que entra un efecto -y son siete hoy y veinte manana-, asi que en
        //  280 px les tocarian 40, 28, 14... Ademas seria una segunda lista de
        //  efectos al lado de la de la cara, con la pregunta de siempre:
        //  ¿cual de las dos es la buena? El XY toca lo que la maquina TIENE
        //  PUESTO, que es lo que un panel de directo puede tocar.
        for (int r = 0; r < kNumRanuras; ++r)
        {
            auto* b = new juce::TextButton();
            styleButton (*b, kKey);
            litAccent (*b);
            b->setClickingTogglesState (true);
            b->setRadioGroupId (7710);
            b->onClick = [this, r]
            {
                const int fx = enRanura (r);
                if (fx >= 0) selectXyFx (fx);
            };
            xyPanel.addAndMakeVisible (b);
            xyFxButtons.add (b);
        }

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
    //  Swipe the screen to walk the PATTERNS. The one gesture on the face
    //  that changes what is PLAYING without covering the pads with a sheet.
    //  Y patrones y no «bancos», que es lo que decia este comentario y lo que
    //  acabo diciendo el ingles de la ficha GESTOS: un banco en esta app es
    //  A B C D y son dieciseis pads, no un patron del secuenciador.
    cristal.onSwipe = [this] (int dir)
    {
        const int next = (selectedPattern + dir + kNumPatterns) % kNumPatterns;
        //  Go through the stepper the sheet already owns, rather than doing
        //  the same six things again beside it: one place decides what
        //  changing bank means, and this gesture is only another way to ask.
        patternSlider.setValue (next, juce::sendNotificationSync);
        status.setText (T ("PATRON") + " " + Lang::ltr ("P" + juce::String (next + 1)),
                        juce::dontSendNotification);
    };

    addAndMakeVisible (cristal);
    padSheet.addAndMakeVisible (waveform);

    //  There is no skin picker. ZATI has one look; a strip of alternative
    //  accents sitting on top of the project menu was a preference masquerading
    //  as a feature, and it stole the first line of a sheet that exists to
    //  manage work. Projects saved with another skin still load — the stored
    //  value is applied, it just cannot be changed from here.

    // Controls live inside their sheets, not on the machine face.
    for (juce::Component* c : { (juce::Component*) &pitchSlider, (juce::Component*) &fineSlider,
                                (juce::Component*) &volSlider, (juce::Component*) &panSlider,
                                (juce::Component*) &anchoSlider,
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

    //  RECORTAR va en esa misma fila: las cuatro son cosas de la MUESTRA que
    //  se esta mirando, y esta ademas actua sobre lo que las asas de justo
    //  debajo estan marcando.
    styleButton (recorteButton, kKey);
    recorteButton.onClick = [this] { recortaPad(); };
    padSheet.addAndMakeVisible (recorteButton);

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

    //  LA PUERTA DEL RACK, donde estaban los seis envios duplicados. Ver la
    //  maqueta de la pagina RIG: los mismos seis valores se movian desde dos
    //  fichas distintas, y de las dos el RACK es la que dice mas.
    //  16 NIVELES. Ver nivel16: se captura el pad al encender.
    styleButton (nivelesButton, kKey);
    litAccent (nivelesButton);
    nivelesButton.setClickingTogglesState (true);
    nivelesButton.onClick = [this]
    {
        nivel16 = nivelesButton.getToggleState();
        nivelPad = juce::jlimit (0, kNumPads - 1, selectedPad);
        refreshModoNota();
        status.setText (nivel16 ? T ("16 NIVELES: PAD %1", juce::String (nivelPad + 1))
                                : T ("16 NIVELES OFF"), juce::dontSendNotification);
        repaint();
    };
    padSheet.addAndMakeVisible (nivelesButton);

    styleButton (padRackBtn, kKey);
    litAccent (padRackBtn);
    padRackBtn.onClick = [this]
    {
        //  El canal ya lo puso `selectPad`: la puerta lleva al rack DEL CANAL
        //  de este pad, que es lo que hace que abrirla desde aqui signifique
        //  algo. Con un canal propio del rack, esta puerta habria abierto la
        //  fila de otro.
        openSheet (rackSheet, mixButton);
        refreshRack();
    };
    padSheet.addAndMakeVisible (padRackBtn);

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

        //  EL RELOJ, que es el de MANTENIMIENTO y ya no el del dibujo. La
        //  cadencia es la misma de siempre porque para un vigilante sobra: lo
        //  que cambia es que ya no decide cuantas veces por segundo se ve
        //  moverse la aguja de un medidor.
        startTimer (dev.relojMs);
    }

    //  Y EL DIBUJO, AL RITMO DE LA PANTALLA. Se engancha DESPUES de que la
    //  cara exista y no en la lista de inicializacion: `VBlankAttachment`
    //  necesita un componente vivo y busca el peer del display en el que
    //  esta, asi que atarlo a medio construir es atarlo a nada.
    //
    //  Con ZATI_VBLANK=hz manda el banco y no el panel: X11 entrega vblanks a
    //  la frecuencia que declare el display -100 Hz en Xvfb, que no declara
    //  ninguna- asi que sin esta puerta no hay forma de preguntar «¿se ve
    //  igual a 60 que a 120?». Y son excluyentes a proposito: con las dos
    //  puestas el cuadro se pintaria dos veces por vuelta y el `dt` mediria la
    //  mitad de lo que dura.
    {
        const int hz = UiAudit::env ("ZATI_VBLANK").getIntValue();
        if (hz > 0)
        {
            relojDibujo.fn = [this]
            {
                const double t = juce::Time::getMillisecondCounterHiRes();
                enVBlank (t / 1000.0);
            };
            relojDibujo.startTimerHz (juce::jlimit (1, 240, hz));
        }
        else
        {
            vblank = juce::VBlankAttachment (this, [this] (double t) { enVBlank (t); });
        }
    }
    //  Y AL FINAL, no donde el master. aplicaFilasPiano llama a resized(), y
    //  resized() desde la mitad del constructor es exactamente como se cerro
    //  esta app antes de ensenar la ventana: la lista de tapas que pide todavia
    //  esta vacia. Aqui ya esta todo construido.
    loadPianoPref();
    loadPistasPref();
    //  Y EL ROTULO DEL ZOOM DE ANCHO SE ESCRIBE UNA VEZ AL ARRANCAR, por lo
    //  mismo que sus dos hermanos: `aplicaZoomPasos` es quien lo pone y sin
    //  esta linea la tapa salia con el texto del constructor -o sea vacia en
    //  el volcado- y ademas sin pasar por `Lang::ltr`, que es lo que decide
    //  como se lee "2:1" en arabe. No es una preferencia que se guarde: la
    //  celda arranca CUADRADA por decision, asi que lo unico que hace falta es
    //  que la tapa diga en que paso esta.
    aplicaZoomPasos (seqZoomW);
    loadCuentaPref();
    loadTomasPref();
    loadMovPref();
    //  Y el monitor. `aplicaMonitor` sin avisar: en el constructor no hay
    //  nadie a quien decirselo, y la linea de estado la escribe lo que la
    //  persona toque despues.
    loadMonitorPref();
    aplicaMonitor (false);

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
                                (juce::Component*) &anchoSlider,
                                (juce::Component*) &attackSlider, (juce::Component*) &releaseSlider,
                                (juce::Component*) &cutSlider,    (juce::Component*) &resoSlider,
                                (juce::Component*) &chokeSlider,  (juce::Component*) &startSlider,
                                (juce::Component*) &endSlider,
                                (juce::Component*) &fadeInSlider, (juce::Component*) &fadeOutSlider })
        padSheet.addAndMakeVisible (c);

    showSeqPage (seqPageGrid);
    showPadPage (padPageSound);
    showMixBank (0);
    ponIconos();
    //  Y la herramienta de arranque: el LAPIZ, o sea la rejilla comportandose
    //  exactamente como siempre. Sin esta linea las cuatro tapas nacerian
    //  apagadas y la rejilla armada con la que fuera - un modo que nadie ha
    //  pedido es peor que ninguno.
    ponHerramienta (Playlist::hLapiz);
    //  DESPUES de ponIconos, que reparte los dibujos de toda la app por su
    //  sitio: la fila de la cara los suyos los toma de `slotFx` y no del
    //  orden, asi que tiene que ser la ultima palabra sobre esas seis tapas.
    refrescaRanuras();
    applySkin();
}

// ============================================================================
//  QUE DIBUJO LLEVA CADA TAPA.
//
//  Una tabla y no una llamada repartida por el constructor: quien mira esto
//  quiere ver el JUEGO ENTERO de una vez -para saber si hay dos tapas de la
//  misma fila con el mismo dibujo, que es el fallo que un icono tiene y un
//  rotulo no- y no cuarenta lineas sueltas a mil lineas de distancia.
//
//  El icono es el ADORNO y la palabra es la funcion: donde no caben los dos,
//  sale la palabra. Esa decision no esta aqui sino en reparteTapa, que es
//  quien sabe cuanto sitio hay; aqui solo se dice cual seria.
//
//  Y NO LLEVAN ICONO los mandos de valor ni las pestanas de pagina dentro de
//  una ficha: un icono al lado de un numero no dice nada que el numero no
//  diga, y una pestana ya esta dicha por la pagina que abre.
// ============================================================================
//  EL FONDO, con dos clientes: paint y la portada del arranque. Salio de
//  dentro de paint en cuanto hubo el segundo, por lo mismo que normaliza salio
//  de dentro de render - dos caminos que pintan lo mismo por su cuenta se
//  separan, y el sintoma habria sido «la portada y la cara no son del mismo
//  color» sin poder decir por que.
void MainComponent::pintaFondo (juce::Graphics& g)
{
    auto full = getLocalBounds().toFloat();

    // 1. Full-bleed light chassis (edge to edge — the whole screen is the face).
    //  EL CUERPO SE PINTA UNA VEZ Y SE COPIA.
    //
    //  (El grano se probo y se quito - ver ZatiColours: sobre un chasis
    //  acromatico no se leia como material sino como suciedad. Lo que se queda
    //  es el horneado, que es lo que se midio y vale igual con el fondo liso.)
    //
    //  Era un degradado liso, y un degradado liso es una app: un aparato tiene
    //  MATERIAL. Con el grano encima el fondo pasaba de 1.27 ms a 3.59 -una
    //  pasada de mezcla alfa sobre los 377 mil pixeles de la ventana en cada
    //  fotograma completo-, asi que se hornea: degradado y grano se dibujan en
    //  una imagen OPACA al cambiar de tamano o de carcasa, y pintar el fondo
    //  pasa a ser una copia de filas. Sale mas barato que el degradado que
    //  habia antes, y el grano sale gratis.
    //
    //  Y respeta el recorte, que es lo que hace que siga valiendo: una banda
    //  de 30 px copia 30 px, no la ventana entera. Ver ZATI_PAINT.
    if (! fondoVivo)
    {
        if (fondoCache.getWidth() != getWidth() || fondoCache.getHeight() != getHeight()
            || fondoSkin != ZatiColours::currentSkin)
            reconstruyeFondo();
        g.drawImageAt (fondoCache, 0, 0);
    }
    else
    {
        //  La corrida de control del banco: el fondo de antes, sin grano y sin
        //  hornear. Sin ella no hay forma de decir cuanto cuesta la textura -
        //  los milisegundos dependen de la maquina, asi que hay que medir las
        //  dos en la misma.
        g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, full.getCentreX(), full.getY(),
                                                 ZatiColours::chassisBot, full.getCentreX(), full.getBottom(), false));
        g.fillRect (full);
    }

}

//  EL FONDO HORNEADO. Ver MainComponent::paint.
void MainComponent::reconstruyeFondo()
{
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());

    //  OPACA -Image::RGB- y no ARGB: el fondo no tiene nada detras, y una
    //  imagen con alfa obliga al copiado a mezclar cada pixel, que es
    //  exactamente el coste que este cache existe para quitar.
    fondoCache = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (fondoCache);

    const auto full = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
    g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, full.getCentreX(), 0.0f,
                                             ZatiColours::chassisBot, full.getCentreX(), (float) h, false));
    g.fillRect (full);

    fondoSkin = ZatiColours::currentSkin;
}

void MainComponent::ponIconos()
{
    struct Par { juce::TextButton* tapa; Iconos::Id id; };

    const Par tabla[] =
    {
        //  La cara: las seis pestanas de modulo y el transporte.
        { &padsButton, Iconos::Id::pads },        { &secButton,  Iconos::Id::sec },
        { &mixButton,  Iconos::Id::mezcla },      { &songButton, Iconos::Id::cancion },
        { &xyButton,   Iconos::Id::xy },          { &setButton,  Iconos::Id::ajustes },
        { &rackButton, Iconos::Id::rack },        { &manualButton, Iconos::Id::manual },
        { &chopButton, Iconos::Id::chop },        { &pianoButton, Iconos::Id::piano },
        { &recButton,  Iconos::Id::rec },         { &tapButton,  Iconos::Id::tap },
        { &soloButton, Iconos::Id::solo },
        { &clearButton, Iconos::Id::vaciar },     { &loopButton, Iconos::Id::loop },
        { &undoButton, Iconos::Id::deshacer },    { &redoButton, Iconos::Id::rehacer },
        { &quantButton, Iconos::Id::cuadrar },

        //  El secuenciador: las herramientas del patron.
        { &seqGridBtn, Iconos::Id::sec },         { &seqPianoBtn, Iconos::Id::piano },
        { &patLeftBtn, Iconos::Id::atras },       { &patRightBtn, Iconos::Id::adelante },
        { &patDoubleBtn, Iconos::Id::doblar },    { &seqHumanBtn, Iconos::Id::humanizar },
        { &copyPatBtn, Iconos::Id::copiar },      { &pastePatBtn, Iconos::Id::pegar },
        { &copyRowBtn, Iconos::Id::copiar },      { &pasteRowBtn, Iconos::Id::pegar },
        { &pianoGomaBtn, Iconos::Id::goma },      { &pianoCorteBtn, Iconos::Id::tijeras },
        { &pianoLapizBtn, Iconos::Id::lapiz },
        { &pianoClearBtn, Iconos::Id::vaciar },

        //  La cancion: ocho herramientas de arreglo, ocho dibujos distintos.
        //  Es la fila donde mas rinde -INSERTAR y QUITAR se leen igual de
        //  rapido en cualquiera de los cuatro idiomas- y la que obligo a que
        //  esos dos no sean el mismo dibujo con un signo cambiado. Ver Iconos.h.
        { &songPadModeBtn, Iconos::Id::sonido },
        //  songModeBtn, modoBtn y seqModoBtn NO estan en esta tabla: su dibujo
        //  cambia con el estado -CANCION o PATRON- y lo pone modoTapa. Dos
        //  sitios escribiendo el mismo icono es uno de los dos quedandose viejo.
        { &songDoubleBtn, Iconos::Id::doblar },
        { &songInsertBtn, Iconos::Id::insertar }, { &songRemoveBtn, Iconos::Id::quitar },
        { &songCopyBtn, Iconos::Id::copiar },     { &songPasteBtn, Iconos::Id::pegar },
        { &songLoopBtn, Iconos::Id::loop },       { &songLeftBtn, Iconos::Id::atras },
        { &songRightBtn, Iconos::Id::adelante },  { &songShortBtn, Iconos::Id::acortar },
        { &songLongBtn, Iconos::Id::alargar },

        //  Proyectos y ficheros.
        { &projSaveButton, Iconos::Id::guardar }, { &projLoadButton, Iconos::Id::abrir },
        { &projNewButton, Iconos::Id::nuevo },    { &projDeleteButton, Iconos::Id::borrar },
        { &projExportButton, Iconos::Id::exportar }, { &projKitButton, Iconos::Id::guardar },
        { &exportMasterButton, Iconos::Id::exportar },
        { &exportStemsButton, Iconos::Id::exportar },
        { &exportDirBtn, Iconos::Id::carpeta },

        //  El navegador. Cinco tapas en una fila, cinco dibujos distintos: si
        //  tres de ellas llevaran la misma carpeta, el dibujo no diria nada
        //  que el sitio no dijera ya.
        { &browseLoadButton, Iconos::Id::cargar }, { &browseKitButton, Iconos::Id::carpeta },
        { &browseKitsDirButton, Iconos::Id::abrir },
        { &browseFactoryButton, Iconos::Id::instrumentos },
        { &browseUseDirBtn, Iconos::Id::carpeta },

        //  El pad, y lo que se le hace.
        { &padSoundBtn, Iconos::Id::sonido },     { &padTrimBtn, Iconos::Id::recorte },
        { &padRigBtn, Iconos::Id::pad },          { &padRackBtn, Iconos::Id::rack },
        //  El canal del pad y las dos paginas de la mesa. `mezcla` para las dos
        //  del canal: es literalmente lo que son, y el dibujo ya existe.
        { &padCanalBtn, Iconos::Id::mezcla },
        { &nivelesButton, Iconos::Id::niveles },
        { &autocutButton, Iconos::Id::autocut },  { &duckButton, Iconos::Id::bombeo },
        { &micButton, Iconos::Id::mic },          { &resampleButton, Iconos::Id::remuestrear },
        { &loadButton, Iconos::Id::cargar },
        { &browseSystemButton, Iconos::Id::sistema },

        //  El patron entero, y la cadena.
        { &seqStepBtn, Iconos::Id::patron },
        { &chainClearButton, Iconos::Id::cadena },

        //  AJUSTES: sus cuatro paginas y sus tres pruebas.
        { &pageAudioBtn, Iconos::Id::sonido },    { &pageMidiBtn, Iconos::Id::midi },
        { &pageProjBtn, Iconos::Id::lista },      { &pageGestBtn, Iconos::Id::mano },
        { &measureButton, Iconos::Id::medir },    { &testButton, Iconos::Id::altavoz },
        { &tourButton, Iconos::Id::mano },

        //  LOS CINCO HUECOS DE FILA, que salieron de Tests/planos.py y no de
        //  mirar la app: una fila con unas tapas dibujadas y otras no se lee
        //  como una tapa a la que le falta algo. Ver el bloque de Iconos.h.
        //
        //  ASPECTO iba en la fila de arriba y se quedo fuera de la tabla el
        //  dia que se escribio: AUDIO y MIDI llevaban dibujo y la tercera
        //  pestana de la MISMA fila no. Es la ficha que contiene el selector
        //  de carcasa, o sea la pagina de la que va el dibujo.
        { &pageAspBtn, Iconos::Id::aspecto },
        { &reverseButton, Iconos::Id::reves },    { &denoiseButton, Iconos::Id::ruido },
        { &recorteButton, Iconos::Id::recortar },
        { &mixClearSolo, Iconos::Id::sinsolo },   { &exportFmtBtn, Iconos::Id::comprimir },

        //  Y la fila que no tenia NINGUNO, que es la que la persona senalo.
        { &midiOutBtn, Iconos::Id::mandar },      { &midiInBtn, Iconos::Id::recibir },

        //  Y LOS SIETE HUECOS QUE `Tests/planos.py` LLEVABA CANTANDO SIN QUE
        //  NADIE LO CORRIERA. No estaba en `banco.yml` -solo `plano.py`, que es
        //  otra prueba- asi que su rojo era permanente y mudo. Tres filas:
        //
        //    SEL      junto a VACIAR, LAPIZ, GOMA y TIJERAS, en el piano
        //    GRABAR / CLIC / AUTO   junto a PATRON, en la banda de audio
        //
        //  GRABAR no necesitaba dibujo nuevo: `rec` existe desde el primer dia
        //  y a esa tapa no se le habia asignado nunca.
        { &pianoSelBtn, Iconos::Id::sel },
        { &songRecBtn, Iconos::Id::rec },         { &songClickBtn, Iconos::Id::clic },
        { &autoBtn, Iconos::Id::automacion },
    };

    for (const auto& p : tabla)
        p.tapa->getProperties().set ("icono", (int) p.id);

    //  LAS QUE NO LLEVAN DIBUJO A PROPOSITO, dicho aqui y no deducido fuera.
    //
    //  Es la otra mitad de la tabla de arriba: sin ella, "esta tapa no tiene
    //  dibujo" y "a esta tapa se le olvido el dibujo" son la misma linea en el
    //  volcado, y la regla de la fila (Tests/planos.py) no puede distinguirlas.
    //  Se marcan por lo que SON y no por lo que dicen -el rotulo cambia en las
    //  otras tres compilaciones y una excepcion escrita en espanol deja de
    //  encajar en ingles sin que nadie se entere-.
    //
    //  Dos clases, y las dos por la misma razon de fondo -el rotulo ya hace el
    //  trabajo que haria el dibujo-:
    //
    //    el SIGNO de un par que sube y baja, que es un dibujo escrito;
    //    y el rotulo que dice el ESTADO en vez de la accion (pianoVerBtn),
    //    donde ademas no hay verbo que dibujar.
    for (juce::TextButton* b : { &pianoPadDownBtn, &pianoPadUpBtn,
                                 &pianoOctDownBtn, &pianoOctUpBtn, &pianoVerBtn,
                                 &instPackDownBtn, &instPackUpBtn,
                                 &vstPreDown, &vstPreUp, &vstOctDown, &vstOctUp,
                                 &zoomOutButton, &zoomInButton, &zoomFitButton,
                                 //  «1 COMPAS» dice CUANTO SE VE, o sea el
                                 //  ESTADO del zoom del piano y no una accion:
                                 //  no hay verbo que dibujar, y el rotulo ya
                                 //  hace el trabajo que haria el dibujo. Es la
                                 //  misma clase que pianoVerBtn.
                                 &pianoZoomBtn,
                                 //  Y el de la linea de tiempo, que es la misma
                                 //  clase con otra rejilla: «8 COMPASES» dice
                                 //  cuanto se ve, no lo que pasa al tocarlo.
                                 &songZoomBtn })
        b->getProperties().set ("valor", 1);

    //  LOS SEIS EFECTOS, por su orden en la fila. Es la unica fila de la app
    //  cuyos rotulos son ABREVIATURAS -FLT, HPF, DRV...- y tres letras no se
    //  traducen: quien abre la app por primera vez no sabe cual es cual en
    //  ninguno de los cuatro idiomas. Aqui es donde mas rinde un dibujo.
    //  Las dos filas -la de la cara y la del XY- las pone `refrescaRanuras`,
    //  que es quien sabe que tipo vive en cada ranura: aqui el dibujo
    //  dependeria del SITIO y no del contenido, que es justo lo que dejo de
    //  ser verdad el dia que la fila paso a ser de ranuras.

    //  Y LA TAPA DE APAGAR DE CADA FILA DEL RACK. El dibujo es FIJO -no
    //  depende del tipo que viva en la ranura, que eso ya lo dice el canalon
    //  de al lado- asi que va aqui y no en `refrescaRanuras`.
    for (auto* m : rackMuteBtns)
        if (m != nullptr) m->getProperties().set ("icono", (int) Iconos::Id::apagar);

    //  Las dos de transporte nacen paradas; a partir de ahi las mueve
    //  `transporte`, que cambia el rotulo y el dibujo a la vez.
    transporte (playButton,  false);
    transporte (seqPlayBtn,  false);
    transporte (songPlayBtn, false);
    oirTapa (previewButton, false);
    //  El candado del XY lo mueve refreshXy, que es quien sabe si esta fijo.
    xyLatchButton.getProperties().set ("icono", (int) Iconos::Id::momentaneo);
}


void MainComponent::applySkin()
{
    //  LA LISTA DE PROYECTOS, que no es una tapa y por eso se quedaba fuera.
    //
    //  juce::ListBox guarda el color que se le da, y el suyo se ponia UNA vez
    //  al construirla - o sea con la paleta del arranque. En cuanto la persona
    //  se pasaba a una carcasa oscura, la lista seguia pintando el fondo claro
    //  del chasis de PAPEL: una caja blanca dentro de una tarjeta oscura, con
    //  la fila sin seleccionar casi ilegible. Es exactamente lo que dice la
    //  regla de la casa - todo token que una piel mueve hay que VOLVER A
    //  LEERLO, y una copia congela la paleta del arranque - y se colo porque
    //  restyleTree solo recorre TextButtons.
    projList.setColour (juce::ListBox::backgroundColourId, ZatiColours::chassisTop);
    projList.setColour (juce::ListBox::outlineColourId, ZatiColours::plateEdge);
    projList.repaint();

    //  Y LAS OTRAS TRES QUE TAMPOCO SON TAPAS. La misma regla incumplida por
    //  las mismas razones: `projNameBox` es un TextEditor con cinco colores
    //  puestos en el constructor, y `masterLabel` y `status` son Labels con la
    //  tinta cogida una sola vez. En GRAFITO y en LACA se quedaban con la
    //  paleta de PAPEL - la caja del nombre de proyecto en blanco dentro de una
    //  tarjeta oscura, que es literalmente el fallo que ya costo la lista.
    projNameBox.setColour (juce::TextEditor::backgroundColourId, ZatiColours::screenBg);
    projNameBox.setColour (juce::TextEditor::textColourId,       ZatiColours::lcdFg);
    projNameBox.setColour (juce::TextEditor::outlineColourId,    ZatiColours::ink.withAlpha (0.35f));
    projNameBox.setColour (juce::TextEditor::highlightColourId,  ZatiColours::accent.withAlpha (0.35f));
    projNameBox.setColour (juce::TextEditor::focusedOutlineColourId, ZatiColours::ink);
    projNameBox.applyColourToAllText (ZatiColours::lcdFg, true);
    projNameBox.repaint();
    masterLabel.setColour (juce::Label::textColourId, ZatiColours::ink);
    status.setColour (juce::Label::textColourId, ZatiColours::inkDim);

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
    const int f = juce::jlimit (0, kNumFx - 1, focusedFx);

    //  LA REDUCCION DE GANANCIA SE VE, O EL COMPRESOR ES INVISIBLE. Sin ella,
    //  «no se si esta comprimiendo» no tiene respuesta: los tres mandos de un
    //  compresor dicen lo que le has PEDIDO y ninguno lo que esta haciendo.
    //
    //  Y donde MIX, que es la unica casilla de las tres cuyo numero se puede
    //  deducir mirando otra cosa -la tapa esta encendida o no-. Y SOLO CON EL
    //  DEDO FUERA: mientras se toca el mando, la casilla dice el valor del
    //  mando. Un control y su lectura contando cosas distintas es el fallo que
    //  ya costo una medida con el corte del pad.
    const int d = dinamicaDeFx (f);
    if (p == 2 && d >= 0 && ! macroTouched[2])
    {
        const float red = engine.getDynReduccion (d);
        if (red > 0.05f)
            return Lang::ltr ("-" + juce::String (red, 1) + " dB");
    }

    return fxFormat (fxDefs[f].spec[p], ks[p]->getValue());
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
        //  Y AL SOLTAR SE PUBLICA lo que se acabe de escribir. Publicar por
        //  VALOR reservaria una tabla de cuatro mil eventos en cada fotograma
        //  de arrastre; soltar es el momento en que la frase esta entera y
        //  ademas es el unico que ocurre una vez.
        if (autoArmado) publicaAutomacion();

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
    static const char* names[kNumGrids] = { "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64" };
    return names[juce::jlimit (0, kNumGrids - 1, i)];
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

    { "DRV",  { "DRIVE", "TONE|fx", "MIX" },
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

    //  EL EQ, Y LOS DOS MANDOS QUE LA CURVA NO PUEDE DECIR.
    //
    //  Los diez numeros de las cinco bandas -donde y cuanto- viven en `Eq5` y
    //  se mueven arrastrando los nodos, que es lo que trae la superficie
    //  propia. Aqui quedan los dos que un nodo no puede llevar: lo ANCHO que
    //  es una campana -el tercer eje, y en un dedo no hay tercer eje- y la
    //  SALIDA, que no es de ninguna banda: cinco bandas subidas se comen el
    //  margen del master y eso se corrige con un solo numero.
    //
    //  Asi que el reparto es limpio: la curva pone DONDE y CUANTO, los mandos
    //  ponen lo ANCHO y cuanto SALE, y MIX sigue siendo el parametro 2 de la
    //  fila -o sea el interruptor- sin tocar una linea de `setFxEnabled`.
    { "EQ",   { "ANCHO", "SALIDA", "MIX" },
      { {    0.4,     3.0, 0.01,    0.0,     1.0, 7 },
        {  -12.0,    12.0, 0.10,    0.0,     0.0, 8 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  LA FAMILIA DE DINAMICA. Las cuatro caben en tres mandos y por eso NO
    //  traen cara propia: es el EQ el que no cabia -diez numeros- y no estas.
    //  El ataque y la caida de CMP van por constante musical y no por mando
    //  (ver Dinamica.h): con MIX ocupando uno de los tres, un compresor de
    //  cinco numeros no entra, y forzarlo seria repartir el plato entre cinco
    //  mandos de 30 px. Cuando haga falta, CMP traera su curva de
    //  transferencia como la trajo el EQ.
    { "CMP",  { "UMBRAL", "RATIO", "MIX" },
      { {  -60.0,     0.0, 0.50,    0.0,   -18.0, 8 },
        {    1.0,    20.0, 0.10,    4.0,     4.0, 9 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    { "GTE",  { "UMBRAL", "CIERRE", "MIX" },
      { {  -80.0,     0.0, 0.50,    0.0,   -40.0, 8 },
        {    5.0,   500.0, 1.00,  100.0,   120.0, 3 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    { "DSS",  { "FREQ", "FUERZA", "MIX" },
      { { 2000.0, 12000.0, 10.0, 6000.0,  6000.0, 0 },
        {    0.0,     1.0, 0.01,    0.0,     0.5, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  EL TECHO NO PASA DE 0 dB Y NO ES TIMIDEZ, que es lo mismo que ya se
    //  escribio del master: por encima de la unidad lo unico que se gana es
    //  empujar el limitador de la salida, que es donde se pierde el golpe.
    { "LIM",  { "TECHO", "SOLTAR", "MIX" },
      { {  -24.0,     0.0, 0.10,    0.0,    -1.0, 8 },
        {    5.0,   500.0, 1.00,  100.0,   120.0, 3 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  LOS CUATRO DE MODULACION. Sus RATE llevan clave propia -`RATE|lfo`-
    //  porque `RATE` ya existe y es el de BIT, que en chino dice 采样率, o sea
    //  «frecuencia de muestreo»: un LFO no es eso. Es el mecanismo que
    //  `TONE|fx` ya usa, y por lo mismo — reaprovechar una clave por parecerse
    //  en espanol sale mal en las otras tres.
    //
    //  El recorrido de RATE es exponencial con el punto medio en 1 Hz: de 0.05
    //  a 8 Hz repartidos linealmente deja los tres primeros milimetros del
    //  mando para todo lo que se usa. Es lo mismo que hace el tiempo de DLY.
    { "CHO",  { "RATE|lfo", "PROF", "MIX" },
      { {   0.05,     8.0, 0.01,    1.0,    0.80, 10 },
        {    0.0,     1.0, 0.01,    0.0,    0.55, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.50 },

    //  FLA lleva REALIMENTACION donde CHO lleva profundidad, que es lo que los
    //  separa: el recorrido del retardo es fijo y corto, y lo que hace el
    //  peine es cuantas veces vuelve. El mando va de 0 a 1 y se mapea a
    //  -0.95..+0.95 en el motor: por debajo de la mitad la realimentacion es
    //  NEGATIVA, que es el flanger que suena a avion y no a metal.
    { "FLA",  { "RATE|lfo", "FBK", "MIX" },
      { {   0.05,     8.0, 0.01,    1.0,    0.25, 10 },
        {    0.0,     1.0, 0.01,    0.0,    0.55, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.50 },

    { "PHA",  { "RATE|lfo", "PROF", "MIX" },
      { {   0.05,     8.0, 0.01,    1.0,    0.35, 10 },
        {    0.0,     1.0, 0.01,    0.0,    0.70, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.50 },

    //  TRM llega a 20 Hz y los otros tres a 8: por encima de dieciseis un
    //  temblor deja de oirse como pulso y empieza a sonar a timbre, y eso es
    //  justo lo que un trémolo rapido hace.
    { "TRM",  { "RATE|lfo", "PROF", "MIX" },
      { {   0.20,    20.0, 0.01,    3.0,    4.50, 10 },
        {    0.0,     1.0, 0.01,    0.0,    0.80, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  LOS SEIS DE CARACTER. Los seis SUSTITUYEN -ver `fxSustituye`- asi que
    //  su `onMix` es 1.00 salvo RNG: un ancho, un excitador o un moldeador de
    //  transitorios a medias suena a dos cosas a la vez, y un anillo a tope es
    //  lo que menos falta hace de fabrica.
    //
    //  RNG lleva ANILLO y no «simetria»: lo que el mando mueve es cuanto de
    //  anillo hay. A 1 la portadora cruza el cero -el tono original desaparece
    //  y quedan las dos bandas laterales, que es la definicion- y a 0 no lo
    //  cruza nunca, o sea que el original sigue ahi con un temblor encima. El
    //  nombre dice el estado del extremo alto, como las demas.
    { "RNG",  { "FREQ", "ANILLO", "MIX" },
      { {   20.0,  4000.0, 1.00,  400.0,   220.0, 0 },
        {    0.0,     1.0, 0.01,    0.0,    1.00, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 0.50 },

    //  PIT dice los semitonos CON SIGNO: «7» no distingue subir de bajar, y
    //  esa es toda la funcion del mando. Y el GRANO en milisegundos, que es lo
    //  unico que se oye de el -corto suena metalico y largo emborrona el
    //  ataque-.
    { "PIT",  { "SEMIS", "GRANO", "MIX" },
      { {  -12.0,    12.0, 1.00,    0.0,    7.00, 11 },
        {   20.0,   120.0, 1.00,    0.0,    60.0, 3 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  WID dice el ancho en VECES y no en porcentaje, como la salida del EQ y
    //  por lo mismo: uno es «como viene» y lo que importa es cuanto se aparta.
    //  Y MONO es la frecuencia por debajo de la cual no se abre nada: el sub
    //  es lo unico que no puede moverse -en un sistema grande es mono por
    //  construccion-.
    { "WID",  { "ANCHO", "MONO", "MIX" },
      { {    0.0,     2.0, 0.01,    1.0,    1.40, 7 },
        {   20.0,   400.0, 1.00,  120.0,   120.0, 0 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    { "EXC",  { "CRUCE", "FUERZA", "MIX" },
      { { 1000.0, 10000.0, 10.0, 4000.0,  4000.0, 0 },
        {    0.0,     1.0, 0.01,    0.0,    0.50, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  TRN va de -1 a +1 y no de 0 a 1: los dos mandos SUBEN y BAJAN su tramo,
    //  y un recorrido que solo sube dejaria fuera la mitad para la que existe
    //  un moldeador -quitarle el golpe a una caja que pega demasiado-. El
    //  centro es no hacer nada.
    { "TRN",  { "ATTACK", "RELEASE", "MIX" },
      { {   -1.0,     1.0, 0.01,    0.0,    0.60, 12 },
        {   -1.0,     1.0, 0.01,    0.0,    0.00, 12 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },

    //  FRZ: la ventana en milisegundos y el cruce de su costura. Ver la etapa
    //  en `AudioEngine.cpp` — captura en el flanco y despues da vueltas.
    { "FRZ",  { "VENTANA", "SUAVE", "MIX" },
      { {   20.0,   500.0, 1.00,  180.0,   180.0, 3 },
        {    0.0,     1.0, 0.01,    0.0,    0.35, 2 },
        {    0.0,     1.0, 0.01,    0.0,     0.0, 2 } }, 1.00 },
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
        //  Un LFO se dice con DECIMAL. Con el formato de frecuencia de los
        //  filtros -entero- los cuatro de modulacion arrancarian diciendo
        //  «0 Hz» debajo de 1 Hz, que es un mando roto: la mitad de su
        //  recorrido cae ahi.
        case 10: return juce::String (v, 2) + " Hz";
        //  Los semitonos, CON SIGNO y con la unidad del sector. Un «7» a secas
        //  no distingue subir de bajar, y esa es toda la funcion del mando.
        case 11: return (v > 0.0 ? juce::String ("+") : juce::String())
                          + juce::String ((int) v) + " st";
        //  Y el porcentaje con signo, por lo mismo: los dos mandos de TRN
        //  suben y bajan, y el defecto de abajo se come el «+».
        case 12: return (v > 0.0 ? juce::String ("+") : juce::String())
                          + juce::String (juce::roundToInt (v * 100.0)) + " %";
        //  El barrido dice de que LADO esta, no solo cuanto. Un "-62 %" no
        //  significa nada en un filtro; "LP 62" y "HP 62" si, y el centro se
        //  llama por su nombre porque es un estado, no un numero.
        //  El ANCHO se dice en veces y no en porcentaje: uno es el de
        //  fabrica y lo que importa es cuanto se aparta de el.
        case 7:  return "x" + juce::String (v, 2);
        //  Un ratio se dice «4.0:1» y no «4.00»: el «:1» es lo que dice que es
        //  una proporcion y no un nivel.
        case 9:  return juce::String (v, 1) + ":1";
        //  Y la SALIDA en dB CON SIGNO, que es lo unico que separa "sube tres"
        //  de "baja tres": un "3.0 dB" a secas no dice hacia donde.
        case 8:  return (v > 0.0 ? juce::String ("+") : juce::String())
                          + juce::String (v, 1) + " dB";
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

    //  UNA SOLA TRADUCCION, y vive en el MOTOR. Aqui habia un switch de
    //  veintiun casos y era correcto mientras el unico que movia un parametro
    //  fuese un mando; con la automatizacion hay un segundo cliente y esta en
    //  el hilo de audio, asi que copiarlo habria sido la misma regla escrita
    //  dos veces. Ver AudioEngine::setFxParam.
    engine.setFxParam (f, pi, v);

    //  Y SI ESTA ARMADO, SE ESCRIBE. Aqui y no en `onValueChange` del mando:
    //  por esta funcion pasan TODOS los caminos que mueven un parametro -el
    //  mando, el pad XY, la curva del EQ y el interruptor- y escribir en cada
    //  uno serian cuatro reglas.
    anotaAutomacion (f, pi, v);
}

//  UN EVENTO POR PASO Y POR PARAMETRO, y el ultimo gana.
//
//  Pasar dos veces por el mismo compas SOBREESCRIBE lo que hiciste la vez
//  anterior en los pasos que toques, y deja los demas: eso es sobregrabar, que
//  es lo que hace un groovebox y lo que se puede aprender sin leer nada. La
//  alternativa -borrar el tramo entero al entrar en el- es "latch", y con ella
//  una pasada en la que no tocas nada BORRA lo que habia.
void MainComponent::anotaAutomacion (int fx, int par, float v)
{
    if (! autoArmado) return;
    const int paso = engine.pasoDeCancion();
    if (paso < 0) return;                    // sin cancion rodando no hay donde

    for (auto& e : autoEventos)
        if (e.paso == paso && e.fx == (juce::uint8) fx && e.par == (juce::uint8) par)
        {
            e.valor = v;
            return;
        }

    //  Y EL TOPE SE CUENTA. Un tope que se supera en silencio no protege,
    //  esconde: sin la linea de estado, la automatizacion dejaria de grabarse
    //  a mitad de la cancion y no lo diria nadie.
    if ((int) autoEventos.size() >= AudioEngine::kMaxAuto)
    {
        status.setText (T ("AUTO lleno: %1 eventos", juce::String (AudioEngine::kMaxAuto)),
                        juce::dontSendNotification);
        return;
    }

    autoEventos.push_back ({ paso, (juce::uint8) fx, (juce::uint8) par, v });
}

//  El espejo al motor. Se publica al SOLTAR el mando y al parar el transporte,
//  no por valor: publicar en cada movimiento reservaria una tabla de cuatro mil
//  eventos por fotograma de arrastre.
void MainComponent::publicaAutomacion()
{
    engine.publicaAutomacion (autoEventos.data(), (int) autoEventos.size());
}

void MainComponent::ponAutoArmado (bool on)
{
    autoArmado = on;
    engine.setAutoEscribe (on);
    autoBtn.setToggleState (on, juce::dontSendNotification);
    //  Y LA LINEA DE TIEMPO LO DICE, que es donde cae el dedo. La tapa AUTO
    //  esta en la fila de arriba y el gesto que cambia esta aqui: rojo, que es
    //  lo que esta casa reserva para «se esta escribiendo».
    songGrid.setModo (on ? kRec : juce::Colours::transparentBlack);
    //  Al DESARMAR se publica: lo que se acaba de tocar tiene que estar en la
    //  tabla que suena antes de la vuelta siguiente, y no en el tic que venga.
    if (! on) publicaAutomacion();
    status.setText (on ? T ("AUTO grabando - manten pulsado para borrar")
                       : T ("AUTO: %1 eventos", juce::String ((int) autoEventos.size())),
                    juce::dontSendNotification);
}

void MainComponent::vaciaAutomacion()
{
    autoEventos.clear();
    publicaAutomacion();
    status.setText (T ("AUTO vacio"), juce::dontSendNotification);
}

// On/off is a MIX move, not a separate flag: one truth, and it is the same
// number the knob shows. Switching back on restores the effect's own default
// amount, so the button behaves like a switch rather than a fader you have to
// go and find again.
void MainComponent::setFxEnabled (int f, bool on)
{
    if (! juce::isPositiveAndBelow (f, kNumFx)) return;
    fxOn[(size_t) f] = on;
    //  La luz va a la RANURA donde este ese tipo, que ya no es su indice. Un
    //  tipo que no esta puesto no tiene tapa que encender, y no se pierde
    //  nada: `refrescaRanuras` vuelve a pintar las seis desde `slotFx`.
    if (const int s = slotDeFx (f); s >= 0)
        fxButtons[s]->setToggleState (on, juce::dontSendNotification);
    fxParam (f, 2).setValue (on ? fxDefs[f].onMix : 0.0, juce::dontSendNotification);
    pushFxParam (f, 2);
    refreshMacroValues();
    //  ENCENDIDO Y APAGADO POR `T()`, que es la regla de la casa y llevaba sin
    //  cumplirse desde que existe esta linea: el nombre del efecto son tres
    //  letras y no se traduce -esa es la fila que mas rinde con dibujo al
    //  lado-, pero «ON» y «OFF» si. El banco no podia verlo hasta que hubo una
    //  ficha que enciende un efecto al abrirse: `EQ ON identical in es and en`,
    //  catorce veces.
    status.setText (juce::String (fxDefs[f].name) + " " + T (on ? "ENCENDIDO" : "APAGADO"),
                    juce::dontSendNotification);
}

//  UN NODO SE MUEVE EN UN SITIO Y SE ESCRIBE EN DOS: el espejo, que es de
//  donde se pinta, y el motor, que es lo que suena. Escrito en el callback de
//  la curva serian dos caminos el dia que un preset o el fichero de proyecto
//  pongan una banda, y el sintoma seria «la curva y el sonido no coinciden»
//  sin poder decir por que. Es la misma razon por la que `normaliza` salio de
//  dentro de `render`.
void MainComponent::ponBandaEq (int b, float hz, float dB)
{
    eqEspejo.ponBanda (b, hz, dB);
    engine.setEqBand  (b, hz, dB);
    eqCurva.repaint();
}

//  Y sus dos hermanas. Van por el MISMO sitio y no directas al motor: el tipo
//  y la Q cambian la FORMA de la curva, asi que escribir solo el motor dejaria
//  la cara dibujando una campana donde ya suena un paso alto.
void MainComponent::ponTipoEq (int b, int t)
{
    eqEspejo.ponTipo (b, t);
    engine.setEqTipo  (b, t);
    eqCurva.repaint();
}

void MainComponent::ponQEq (int b, float q)
{
    eqEspejo.ponQ (b, q);
    engine.setEqQ  (b, q);
    eqCurva.repaint();
}

//  MANTENER SOBRE UN NODO abre su ficha. -1 la cierra. Se apaga *y* se le
//  vacian los limites a lo que lleva dentro, que son las dos mitades de la
//  misma regla: apagar sin vaciar es lo que tuvo a SEGUIR visible y de 0x0
//  desde el primer dia.
void MainComponent::abreBandaEq (int b)
{
    const bool abrir = (b >= 0);
    if (abrir) eqBandaSel = juce::jlimit (0, Eq5::kBands - 1, b);

    eqBandaSheet.setVisible (abrir);
    if (abrir)
    {
        refrescaBandaEq();
        eqBandaSheet.toFront (false);
    }
    else
    {
        for (auto* t : eqTipoBtns) if (t != nullptr) t->setBounds ({});
        eqQKnob.setBounds ({});
        eqBandaCloseBtn.setBounds ({});
        eqBandaSheet.sheetBounds  = {};
        eqBandaTituloBanda        = {};
    }
    resized();
    repaint();
}

//  LA GANANCIA SE APAGA EN UN TIPO DE PASO, que no la tiene: un mando que se
//  mueve y no hace nada es peor que no tenerlo -lo mismo que ya se decidio con
//  los mandos de recorte en un pad de instrumento-. Aqui el mando es el NODO,
//  asi que quien lo apaga es `Eq5::esPaso` dentro de la curva; lo que esta
//  ficha apaga es el chip del tipo que ya esta puesto.
void MainComponent::refrescaBandaEq()
{
    const int b = juce::jlimit (0, Eq5::kBands - 1, eqBandaSel);
    for (int t = 0; t < eqTipoBtns.size(); ++t)
        if (auto* c = eqTipoBtns[t])
            c->setToggleState (t == (int) eqEspejo.tipoDe (b), juce::dontSendNotification);

    eqQKnob.setValue (eqEspejo.qDe (b), juce::dontSendNotification);
    eqBandaSheet.repaint();
    eqCurva.repaint();
}

//  Y el espejo desde el MOTOR, que es quien acota: `Eq5::ponBanda` recorta la
//  frecuencia y la ganancia, asi que preguntarle a el es lo unico que
//  garantiza que la curva dibuje lo que de verdad se quedo puesto. Se llama al
//  abrir un proyecto y al vaciar.
void MainComponent::refrescaEq()
{
    for (int b = 0; b < Eq5::kBands; ++b)
    {
        eqEspejo.ponBanda (b, engine.getEqFreq (b), engine.getEqGain (b));
        eqEspejo.ponTipo  (b, engine.getEqTipo (b));
        eqEspejo.ponQ     (b, engine.getEqQ (b));
    }
    eqCurva.repaint();
    if (eqBandaSheet.isVisible()) refrescaBandaEq();
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
    refrescaPlato();
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
// --- Las seis ranuras ----------------------------------------------------
//
//  Una ranura es un SITIO de la fila; un efecto es lo que se pone en ella. Lo
//  que sigue es toda la traduccion entre las dos cosas, y no hay mas: el resto
//  de la app -el motor, el XY, los parametros, el fichero de proyecto- sigue
//  hablando de TIPOS, que es lo que suena.

int MainComponent::slotDeFxEn (int canal, int fx) const
{
    if (fx < 0 || ! juce::isPositiveAndBelow (canal, kNumCanales)) return -1;
    for (int s = 0; s < kNumRanuras; ++s)
        if (slotFx[(size_t) canal][(size_t) s] == fx) return s;
    return -1;
}

int MainComponent::canalDeFx (int fx) const
{
    if (fx < 0) return -1;
    for (int c = 0; c < kNumCanales; ++c)
        if (slotDeFxEn (c, fx) >= 0) return c;
    return -1;
}

//  Poner un tipo en una ranura del canal actual, o vaciarla con kSlotVacia.
void MainComponent::ponEnRanura (int ranura, int fx)
{
    if (! juce::isPositiveAndBelow (ranura, kNumRanuras)) return;
    if (fx != kSlotVacia && ! juce::isPositiveAndBelow (fx, kNumFx)) return;
    const size_t c = (size_t) juce::jlimit (0, kNumCanales - 1, canalActual);

    //  UN INSERTO, UN CANAL. Su estado en el motor es uno solo -un filtro, un
    //  compresor, un congelador- asi que dos canales con el mismo inserto
    //  serian dos ventanas al mismo aparato, con dos interruptores que se
    //  contradicen. Y MUEVE en vez de rechazar: si el tipo ya estaba en otro
    //  sitio, se va de alli. Rechazar dejaria un toque sin efecto visible, que
    //  se lee como que el boton no responde.
    //
    //  UN ENVIO ES DE TODOS, que es la otra mitad y no una excepcion: DLY, REV,
    //  CHO, FLA y PHA SUMAN, y una linea de retardo existe justo para que
    //  varias fuentes entren en la misma cola. Restringirlos a un canal es lo
    //  contrario de lo que un envio significa. Lo dice `sustituye` y no una
    //  lista escrita aqui: la misma tabla que el hilo de audio usa para restar
    //  seco, que es lo unico que separa las dos familias.
    const bool inserto = AudioEngine::sustituye (fx);
    if (fx != kSlotVacia)
    {
        const int mismoCanal = slotDeFxEn ((int) c, fx);
        if (mismoCanal >= 0 && mismoCanal != ranura)
            slotFx[c][(size_t) mismoCanal] = kSlotVacia;   // dentro del canal, siempre
        else if (inserto)
            for (int oc = 0; oc < kNumCanales; ++oc)
                if (oc != (int) c)
                    if (const int s = slotDeFxEn (oc, fx); s >= 0)
                        slotFx[(size_t) oc][(size_t) s] = kSlotVacia;
    }

    //  Y lo que SALE de la ranura se apaga, pero SOLO si no le queda ningun
    //  otro sitio: un envio puede estar en tres canales y vaciarlo de uno no lo
    //  deja sin tapa. Un efecto encendido al que no le queda ninguna sigue
    //  sonando y no hay donde tocarlo — la hermana de «ningun camino puede
    //  dejar la app en silencio» por el otro lado.
    const int salia = slotFx[c][(size_t) ranura];
    slotFx[c][(size_t) ranura] = fx;
    if (salia >= 0 && salia != fx && fxOn[(size_t) salia] && canalDeFx (salia) < 0)
        setFxEnabled (salia, false);

    refrescaRanuras();
}

//  Rotulo, dibujo y luz de las seis tapas. Se llama entera y no por ranura
//  porque `filaDeIconos` decide la fila de golpe -todo-o-nada- y porque mover
//  una ranura puede vaciar otra.
void MainComponent::refrescaRanuras()
{
    for (int s = 0; s < fxButtons.size() && s < kNumRanuras; ++s)
    {
        const int fx = enRanura (s);
        auto* b = fxButtons[s];

        //  El signo de una ranura vacia. No pasa por T() a proposito: «+» es
        //  el mismo caracter en los cuatro idiomas, igual que la cruz de
        //  cerrar, y la regla de traduccion del banco ya lo da por bueno.
        b->setButtonText (fx < 0 ? "+" : fxDefs[fx].name);

        //  Y LLEVA LA MARCA `valor`, que es lo que separa «aqui no cabia el
        //  dibujo» de «aqui no hay dibujo que poner». Sin ella, una fila con
        //  cinco efectos dibujados y un «+» pelado sale como un HUECO en
        //  Tests/planos.py, que es exactamente el fallo que esa regla existe
        //  para cazar - y aqui seria falso: un signo ES el dibujo.
        if (fx < 0)
        {
            b->getProperties().remove ("icono");
            b->getProperties().set ("valor", 1);
        }
        else
        {
            b->getProperties().set ("icono", (int) iconoDeFx (fx));
            b->getProperties().remove ("valor");
        }

        b->setToggleState (fx >= 0 && fxOn[(size_t) fx], juce::dontSendNotification);

        //  Y EL NOMBRE PARA QUIEN NO VE LA PANTALLA. El rotulo YA es el nombre
        //  -eso lo hace `retranslateUi` para todo el arbol- y ahi esta el
        //  agujero: estas dos tapas cambian de texto DESPUES, cada vez que una
        //  ranura cambia de contenido, asi que se quedaban con el nombre del
        //  efecto de antes. Un «+» ademas no dice nada: se lee «ranura vacia».
        b->setTitle (fx < 0 ? T ("VACIA") : juce::String (fxDefs[fx].name));

        //  Y EL CANALON DEL RACK, la MISMA ranura y la misma tabla. Eran dos
        //  sitios que decian lo que hay en una ranura y solo uno se
        //  actualizaba: la fila del rack pintaba su nombre de una tabla suya
        //  -copiada- que no sabia nada de esto. Una regla escrita dos veces son
        //  dos reglas.
        if (auto* rb = (s < rackSlotBtns.size() ? rackSlotBtns[s] : nullptr))
        {
            rb->setButtonText (fx < 0 ? "+" : fxDefs[fx].name);
            if (fx < 0)
            {
                rb->getProperties().remove ("icono");
                rb->getProperties().set ("valor", 1);
            }
            else
            {
                //  Y AQUI EL DIBUJO NO ES EL DEL TIPO SINO EL DE LA FAMILIA.
                //
                //  El canalon ya dice CUAL es el efecto con su nombre —«FLT»,
                //  «DLY»— asi que el dibujo del tipo repetiria lo mismo. Lo que
                //  no decia nadie es si esa fila RESTA seco o SUMA encima, que
                //  es la unica diferencia que hay entre las dos familias y la
                //  que se pidio ver. Ver Iconos::inserto.
                rb->getProperties().set ("icono", (int) (AudioEngine::sustituye (fx)
                                                           ? Iconos::Id::inserto
                                                           : Iconos::Id::envio));
                rb->getProperties().remove ("valor");
            }
            rb->setToggleState (fx >= 0 && fxOn[(size_t) fx], juce::dontSendNotification);
            rb->setTitle (fx < 0 ? T ("VACIA") : juce::String (fxDefs[fx].name));
        }

        //  Y LA TAPA DE APAGAR DE ESA MISMA FILA, aqui y no en `refreshRack`:
        //  este es el UNICO punto de reparto a las ventanas de una ranura, y
        //  escribirlo en los dos seria la misma regla dos veces con una que un
        //  dia se queda vieja. Con las dos mitades: se apaga cuando la ranura
        //  esta vacia -no hay efecto que sacar de en medio- y se enciende con
        //  el acento cuando el efecto SUENA.
        if (auto* m = (s < rackMuteBtns.size() ? rackMuteBtns[s] : nullptr))
        {
            m->setEnabled (fx >= 0);
            m->setToggleState (fx >= 0 && fxOn[(size_t) fx], juce::dontSendNotification);
            //  Y EN PALABRAS PARA QUIEN NO VE LA PANTALLA: sin rotulo esta tapa
            //  se anuncia como «boton» seis veces seguidas, que es lo que la
            //  tanda de la feria subio del 18 % al 79 %.
            m->setTitle (fx < 0 ? T ("VACIA")
                                : juce::String (fxDefs[fx].name) + " "
                                  + juce::String::charToString ((juce::juce_wchar) 0x00B7) + " "
                                  + T (fxOn[(size_t) fx] ? "ENCENDIDO" : "APAGADO"));
        }

        //  Y LA MISMA FILA EN EL XY, que es la tercera ventana a la ranura.
        //  Aqui la luz NO dice si el efecto suena sino cual esta bajo el dedo
        //  -es un selector con grupo de radio- asi que solo se reponen el
        //  rotulo, el dibujo y si se puede tocar: una ranura vacia no lleva a
        //  ninguna parte, y un control que no puede hacer nada no es
        //  informacion, es ruido.
        if (auto* xb = (s < xyFxButtons.size() ? xyFxButtons[s] : nullptr))
        {
            xb->setButtonText (fx < 0 ? "+" : fxDefs[fx].name);
            if (fx < 0)
            {
                xb->getProperties().remove ("icono");
                xb->getProperties().set ("valor", 1);
                xb->setToggleState (false, juce::dontSendNotification);
            }
            else
            {
                xb->getProperties().set ("icono", (int) iconoDeFx (fx));
                xb->getProperties().remove ("valor");
            }
            xb->setEnabled (fx >= 0);
        }
    }

    //  Y LOS TRES MANDOS SE APAGAN CUANDO NO HAY NADA QUE TOCAR. Con la fila
    //  entera vacia -que es como abre un proyecto nuevo- CTRL 1-3 seguirian
    //  moviendo el parametro de un efecto que no esta puesto: un mando que se
    //  mueve y no hace nada es peor que no tenerlo, que es lo mismo que ya se
    //  decidio con los mandos de recorte en un pad de instrumento.
    const bool hayAlguno = fxEstaPuesto (focusedFx);
    macroCtrl1.setEnabled (hayAlguno);
    macroCtrl2.setEnabled (hayAlguno);
    macroCtrl3.setEnabled (hayAlguno);

    refrescaPlato();
}

//  EL PLATO CAMBIA DE INQUILINO. Un efecto con cara propia se lo queda entero
//  y los tres mandos se apagan Y se quedan sin limites -las dos cosas, que
//  apagar sin vaciar es lo que tuvo a SEGUIR visible y de 0x0 desde el primer
//  dia; los limites los vacia `resized`-.
//
//  En su propia funcion porque lo mueven DOS cosas y no una: cambiar de efecto
//  con el dedo (`focusFx`) y vaciar la ranura donde vivia (`refrescaRanuras`).
//  Escrito en las dos serian dos reglas, y la que se quedara vieja dejaria el
//  plato con la curva de un efecto que ya no esta puesto.
void MainComponent::refrescaPlato()
{
    const bool conCara = fxEstaPuesto (focusedFx) && fxTraeCara (focusedFx);
    const bool cambia  = (conCara != eqCurva.isVisible());
    eqCurva.setVisible (conCara);
    if (cambia) resized();
    repaint (bandaMandos());
}

//  EL MENU DE UNA RANURA, ABIERTO O CERRADO. -1 lo cierra.
void MainComponent::abreMenuRanura (int ranura)
{
    const bool abrir = juce::isPositiveAndBelow (ranura, kNumRanuras);
    ranuraEditada = abrir ? ranura : -1;
    ranuraSheet.setVisible (abrir);

    if (abrir)
    {
        ranuraSheet.toFront (false);
        refrescaMenuRanura();
    }
    else
    {
        //  APAGAR *Y* VACIAR LOS LIMITES, las dos cosas, que es lo que costo
        //  SEGUIR y lo que la septima regla del banco existe para cazar.
        for (auto* b : ranuraBtns) if (b != nullptr) b->setBounds ({});
        ranuraCloseBtn.setBounds ({});
        ranuraVaciarBtn.setBounds ({});
        ranuraSheet.sheetBounds = {};
        ranuraTituloBanda = {};
    }

    resized();
    repaint();
}

void MainComponent::refrescaMenuRanura()
{
    const int puesto = juce::isPositiveAndBelow (ranuraEditada, kNumRanuras)
                         ? enRanura (ranuraEditada) : kSlotVacia;

    for (int f = 0; f < ranuraBtns.size() && f < kNumFx; ++f)
        if (auto* b = ranuraBtns[f])
        {
            //  LO QUE YA ESTA PUESTO EN OTRA RANURA NO SE OFRECE, y se apaga
            //  en vez de esconderse: media rejilla con celdas y media sin
            //  ellas se lee como una celda rota, y ademas el sitio de cada
            //  efecto dejaria de ser el mismo cada vez que se abre el menu -
            //  que es justo lo que hace que se pueda aprender donde esta.
            const int suya = slotDeFx (f);
            const bool libre = (suya < 0 || suya == ranuraEditada);
            b->setEnabled (libre);
            b->setToggleState (f == puesto, juce::dontSendNotification);
        }

    ranuraVaciarBtn.setVisible (puesto >= 0);
}

void MainComponent::ranuraTocada (int ranura)
{
    if (! juce::isPositiveAndBelow (ranura, kNumRanuras)) return;
    const int fx = enRanura (ranura);
    if (fx < 0) { abreMenuRanura (ranura); return; }
    fxTapped (fx);
}

void MainComponent::ranuraMantenida (int ranura)
{
    if (! juce::isPositiveAndBelow (ranura, kNumRanuras)) return;
    const int fx = enRanura (ranura);
    if (fx < 0) { abreMenuRanura (ranura); return; }
    fxFocusOnly (fx);
}

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
    //  La luz va a la RANURA donde vive ese tipo, igual que en la cara. Un
    //  tipo que no esta puesto no tiene tapa que encender.
    for (int r = 0; r < xyFxButtons.size(); ++r)
        xyFxButtons[r]->setToggleState (enRanura (r) == f, juce::dontSendNotification);
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
    //  El candado va con la palabra: cerrado en FIJO, abierto en MOMENTANEO.
    //  Con el mismo dibujo en los dos estados, el icono contradice al rotulo
    //  la mitad del tiempo - que es peor que no tener icono.
    xyLatchButton.setButtonText (xyLatch ? T ("FIJO") : T ("MOMENTANEO"));
    xyLatchButton.getProperties().set ("icono", (int) (xyLatch ? Iconos::Id::fijo
                                                               : Iconos::Id::momentaneo));
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

    refrescaVisorPlato();

    //  SOLO EL RENGLON DE LOS MANDOS, que es lo unico que esta funcion cambia.
    //  El `repaint()` pelado que habia aqui es exactamente el que `macroMoved`
    //  ya tenia acotado veinticuatro lineas mas abajo -«un repintado completo
    //  durante un arrastre redibujaba dieciseis pads y su onda en cada
    //  movimiento del raton»- y esta funcion la llama `xyMoved`, o sea UNA VEZ
    //  POR FOTOGRAMA DE ARRASTRE del pad XY. La misma regla escrita en un
    //  sitio y no en el de al lado: ahora el rectangulo lo dice una funcion y
    //  lo usan las dos.
    repaint (bandaMandos());
}

//  EL VISOR DEL PLATO, QUE ES LA OTRA COSA QUE ESOS TRES NUMEROS MUEVEN.
//
//  Y ES UNA FUNCION PORQUE TIENE DOS DUEÑOS, que es el fallo que la trajo.
//  Esto vivia DENTRO de `refreshMacroValues`, y esa la llaman `focusFx`, el
//  pad XY y la apertura de un proyecto — pero NO `macroMoved`, que es el
//  callback de los tres mandos. O sea que arrastrar CORTE movia el sonido y
//  dejaba dibujada la curva de antes; cambiabas de efecto y volvias, y
//  entonces si se rehacia. Exactamente lo que se vio en el telefono.
//
//  Y EL BANCO DECIA QUE SI: `auditRack` escribia el parametro a mano y
//  llamaba a `refreshMacroValues`, o sea a la funcion donde el fallo NO
//  existe. Ahora mueve el MANDO con notificacion, que es lo que acaba
//  llamando un dedo. Es la misma leccion que los cinco fallos del compas del
//  piano y que la del pad que sonaba al levantar.
//
//  Ver FxMini.h: se repinta solo si la curva se movio, asi que esto puede
//  llamarse una vez por fotograma de arrastre sin costar un repintado.
void MainComponent::refrescaVisorPlato()
{
    platoMini.ponTipo (fxEstaPuesto (focusedFx) && ! fxTraeCara (focusedFx) ? focusedFx : -1);
    if (platoMini.tipo() >= 0)
        platoMini.refresca ((float) fxParam (focusedFx, 0).getValue(),
                            (float) fxParam (focusedFx, 1).getValue(),
                            (float) fxParam (focusedFx, 2).getValue());

    //  Y SE LE DICE AL MOTOR DE CUAL SE CAPTURA. El plato enseña UN visor a la
    //  vez y la curva grande del EQ es ese mismo sitio con otro inquilino, asi
    //  que capturar los once seria pagar diez anillos para dibujar uno. Ver
    //  AudioEngine::miraFx.
    engine.miraFx (fxTraeCara (focusedFx) && fxEstaPuesto (focusedFx) ? focusedFx
                                                                     : platoMini.tipo());
}

//  El renglon de CTRL 1-3, con el aire que su rotulo pintado necesita.
juce::Rectangle<int> MainComponent::bandaMandos() const
{
    //  Con un efecto que trae su propia cara los tres mandos no se maquetan, y
    //  la union de tres rectangulos VACIOS es el rectangulo vacio -o sea que
    //  el repintado acotado no repintaria nada y la curva se quedaria como
    //  estaba-. La banda es entonces la del plato, que es exactamente lo que
    //  la superficie ocupa.
    //  Y EL ALTO SALE DEL PLATO Y NO DE UN NUMERO. Eran `expanded (12, 26)`,
    //  o sea la union de los tres mandos crecida a ojo hasta tapar el plato:
    //  veintiseis por lado sobre una celda de mando de 52 son 104 px para un
    //  plato que mide 86, asi que ademas repintaba de mas. El plato ya esta
    //  medido aqui al lado.
    const auto u = macroCtrl1.getBounds().getUnion (macroCtrl3.getBounds());
    return u.isEmpty() ? ctrlPlateArea
                       : u.expanded (Metrics::md, 0).withTop (ctrlPlateArea.getY())
                                                    .withBottom (ctrlPlateArea.getBottom());
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
            //  A la RANURA del tipo enfocado, no a su indice. Ver setFxEnabled.
            if (const int s = slotDeFx (focusedFx); s >= 0)
                fxButtons[s]->setToggleState (on, juce::dontSendNotification);
        }
    }
    //  Y LA CURVA DEL PLATO, que es lo que este mando acaba de cambiar. Ver
    //  refrescaVisorPlato: faltaba justo aqui.
    refrescaVisorPlato();

    //  Only the knob strip, not the whole face: a full repaint during a drag
    //  redrew sixteen pad tiles and their waveform art on every mouse move.
    repaint (bandaMandos());
}

// --- Sheets ------------------------------------------------------------------
void MainComponent::openSheet (Sheet& s, juce::TextButton& toggle)
{
    //  Que ficha estaba abierta es la mitad de cualquier informe de un cierre:
    //  "se cerro al exportar" y "se cerro al abrir la mezcla" no se arreglan en
    //  el mismo sitio. Ver Bitacora.h y Sheet::nombre - va el nombre de la
    //  FICHA y no el rotulo de la tapa, que esta traducido y ademas no la
    //  identifica.
    Bitacora::paso ((juce::String ("ficha ") + s.nombre).toRawUTF8());

    closeAllSheets();
    if (selectedPad < 0) selectPad (0);
    toggle.setToggleState (true, juce::dontSendNotification);
    s.setVisible (true);
    s.toFront (false);

    //  Y CON ELLA, EL PERMISO PARA TOCAR LOS PADS QUE ASOMAN. Ver
    //  Sheet::onFuera y tocaPadDetras. Se pone AQUI, que es el embudo por el
    //  que pasa toda ficha que se abre, y no en once sitios.
    //
    //  Y el tour SI entra por aqui -dos veces: la tapa TOUR de AJUSTES y el
    //  primer tick del arranque-, que es lo contrario de lo que decia este
    //  comentario. Que no se cierre ni se desvie por un roce no lo consigue no
    //  pasar por este embudo: lo consigue que `tourSheet.sheetBounds` este
    //  vacio -ningun punto cae «dentro», asi que `Sheet::mouseDown` no llega a
    //  preguntar- y que `onDismiss` sea nulo. El comentario mandaba a buscar
    //  una condicion que no existe, que es lo que se le reprocha a un manual.
    s.onFuera = [this] (juce::Point<int> p) { return tocaPadDetras (p); };

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
    const bool onAsp   = (setPage == pageAspecto);

    pageAudioBtn.setToggleState (onAudio, juce::dontSendNotification);
    pageMidiBtn .setToggleState (onMidi,  juce::dontSendNotification);
    pageAspBtn  .setToggleState (onAsp,   juce::dontSendNotification);
    pageProjBtn .setToggleState (onProj,  juce::dontSendNotification);
    pageGestBtn .setToggleState (setPage == pageGestures, juce::dontSendNotification);

    //  APAGAR Y VACIAR, LAS DOS COSAS.
    //
    //  La regla esta escrita al lado de seqFollowBtn -"se apaga *y* se le
    //  vacian los limites, que es lo que la regla dice y lo que media app hacia
    //  a medias"- y aqui se hacia a medias: de las quince tapas de esta ficha
    //  solo projKitButton se quedaba sin coordenadas. Un control apagado con
    //  las coordenadas de la pagina anterior es exactamente lo que el ciclado
    //  ZATI_PAGES existe para cazar, y lo que dejo tres tapas de PASO encima de
    //  la fila COMPAS.
    auto muestra = [] (juce::Component& c, bool on)
    {
        c.setVisible (on);
        if (! on) c.setBounds ({});
    };

    muestra (midiOutBtn, onMidi);
    muestra (midiInBtn,  onMidi);
    muestra (midiOutBox, onMidi);
    muestra (midiInBox,  onMidi);

    muestra (measureButton, onAudio);
    muestra (quantButton,   onAudio);
    muestra (testButton,    onAudio);
    for (auto* b : bufButtons)  muestra (*b, onAudio);
    //  Y los tres de la cuenta, con el que toca encendido: la tapa dice el
    //  ESTADO y no un verbo, que es lo que ya hacen los bancos y las carcasas.
    for (int i = 0; i < cuentaButtons.size(); ++i)
        if (auto* b = cuentaButtons[i])
        {
            b->setToggleState (i == cuentaCompases, juce::dontSendNotification);
            muestra (*b, onAudio);
        }
    //  Y los cuatro del banco de tomas.
    for (int i = 0; i < tomasButtons.size(); ++i)
        if (auto* b = tomasButtons[i])
        {
            b->setToggleState (i == bancoTomas, juce::dontSendNotification);
            muestra (*b, onAudio);
        }
    //  Y los dos del monitor, con la misma regla.
    for (int i = 0; i < monButtons.size(); ++i)
        if (auto* b = monButtons[i])
        {
            b->setToggleState (i == (monitorOn ? 1 : 0), juce::dontSendNotification);
            muestra (*b, onAudio);
        }
    for (auto* b : rateButtons) muestra (*b, onAudio);
    //  EL IDIOMA Y LA CARCASA SE VAN A SU PAGINA. Estaban en AUDIO porque ahi
    //  habia sitio, no porque tengan nada que ver con el reloj y el bufer.
    for (auto* b : langButtons) muestra (*b, onAsp);
    for (auto* b : skinButtons) muestra (*b, onAsp);
    //  Con el que toca encendido, y las dos cosas de la regla: apagar Y
    //  vaciar los limites fuera de su pagina.
    for (int i = 0; i < movButtons.size(); ++i)
        if (auto* b = movButtons[i])
        {
            b->setToggleState ((i == 0) == movimiento, juce::dontSendNotification);
            muestra (*b, onAsp);
        }

    muestra (projList,         onProj);
    muestra (projNameBox,      onProj);
    muestra (projSaveButton,   onProj);
    muestra (projKitButton,    onProj);
    muestra (projLoadButton,   onProj);
    muestra (projNewButton,    onProj);
    muestra (projDeleteButton, onProj);
    muestra (projExportButton, onProj);

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
    const bool onGrid  = (seqPage == seqPageGrid);
    const bool onPiano = (seqPage == seqPagePiano);
    const bool onPat   = (seqPage == seqPageStep);

    seqGridBtn .setToggleState (onGrid,  juce::dontSendNotification);
    seqPianoBtn.setToggleState (onPiano, juce::dontSendNotification);
    seqStepBtn .setToggleState (onPat,   juce::dontSendNotification);

    //  Lo del piano solo en su pagina, y sin sitio en las otras dos: un
    //  componente invisible que conserva sus limites sigue estando ahi para
    //  todo lo que mida geometria.
    pianoGrid.setVisible (onPiano);
    for (juce::TextButton* b : { &pianoOctDownBtn, &pianoOctUpBtn, &pianoClearBtn,
                                 &pianoPadDownBtn, &pianoPadUpBtn,
                                 &pianoLapizBtn, &pianoGomaBtn, &pianoCorteBtn, &pianoVerBtn,
                                 //  Y las cuatro nuevas. Sin ellas aqui salian
                                 //  visibles y de 0x0 en las otras dos paginas
                                 //  de la ficha -248 hallazgos del banco- y
                                 //  ademas dejaban sus coordenadas puestas al
                                 //  volver, que es la regla del RESIDUO: 24.
                                 &pianoSelBtn, &pianoZoomBtn,
                                 &pianoCopiaBtn, &pianoPegaBtn })
    {
        b->setVisible (onPiano);
        if (! onPiano) b->setBounds ({});
    }
    if (! onPiano) pianoGrid.setBounds ({});

    stepGrid.setVisible      (onGrid);
    //  EL TRANSPORTE ESTA EN LAS DOS PAGINAS QUE ESCRIBEN NOTAS, que es lo que
    //  la pagina del piano no tenia: "faltan botones para controlar el
    //  proyecto". Oir lo que llevas escrito es la mitad de escribirlo.
    //  En PATRON no: esa pagina actua sobre el patron entero y no se toca
    //  mientras suena. El modo, con el, por lo mismo.
    seqPlayBtn.setVisible    (onGrid || onPiano);
    seqModoBtn.setVisible    (onGrid || onPiano);
    if (onPat) { seqPlayBtn.setBounds ({}); seqModoBtn.setBounds ({}); }
    seqHumanBtn.setVisible   (onPat);
    //  SEGUIR NO SE ENCIENDE AQUI. Va en la fila del transporte y solo
    //  entra donde las cuatro tapas caben, y quien lo sabe es resized().
    //  Misma regla que las tapas de banco: aqui solo se puede APAGAR.
    if (! onGrid) { seqFollowBtn.setVisible (false); seqFollowBtn.setBounds ({}); }
    //  La visibilidad de las tapas de banco NO se decide aqui: la decide
    //  resized(), que es el unico que sabe si caben sin encoger la rejilla.
    patternSlider.setVisible (onGrid);
    lengthSlider.setVisible  (onGrid);
    bpmSlider.setVisible     (onGrid);
    clearButton.setVisible   (onGrid);
    tapButton.setVisible     (onGrid);
    copyPatBtn.setVisible    (onGrid);
    pastePatBtn.setVisible   (onGrid);
    //  La barra tiene una segunda condicion —un patron que cabe entero no
    //  tiene nada que desplazar— asi que resized() es el unico que puede
    //  ENCENDERLA. Aqui solo se apaga, y con los limites vaciados: media regla
    //  es lo que tuvo a SEGUIR visible y de 0x0 desde el primer dia.
    if (! onGrid && seqPage != seqPagePiano)
    { seqBarra.setVisible (false); seqBarra.setBounds ({}); }
    if (seqPage != seqPagePiano)
    { pianoBarra.setVisible (false); pianoBarra.setBounds ({}); }

    for (auto* b : patternButtons) b->setVisible (onPat);
    chainClearButton.setVisible (onPat);
    swingSlider.setVisible      (onPat);
    gridSlider.setVisible       (onPat);
    euclidSlider.setVisible     (onPat);
    //  LOS CUATRO DEL PASO NO SE DECIDEN AQUI. Viven en la tira de debajo de
    //  la rejilla cuando cabe y en la pagina del patron cuando no, y quien
    //  sabe cual de las dos es resized(). Aqui solo se pueden APAGAR - la
    //  misma regla que las tapas de banco y por la misma razon.
    for (juce::Slider* sl : { &noteSlider, &velSlider, &rollSlider, &lockSlider,
                              &atkPasoSlider, &relPasoSlider, &iniPasoSlider, &panPasoSlider })
        sl->setVisible (false);

    resized();
    seqSheet.repaint();
}

//  Igual que showSeqPage: escondido, no solo sin colocar. Un control que sigue
//  visible fuera de su pagina se pinta encima de la que si esta, y se come los
//  arrastres de lo que tiene delante.
//  Ver MainComponent::altoContenidoElPad. Tres secciones con su titulo, tres
//  filas de tapas -la de FUENTE puede ser dos- y la tira de muestras de color.
int MainComponent::altoContenidoElPad (int ancho) const
{
    constexpr int secH = 15 + 2 * ZatiLookAndFeel::kTextPad;
    const int srcH = Metrics::hit
                   + (padSourceWraps (ancho) ? Metrics::hit + Metrics::halfGap : 0);
    //  Y LA FILA DE PUERTAS PUEDE SER DOS, por lo mismo que la de FUENTE: con
    //  el CANAL dentro son cuatro tapas -cinco en un pad de instrumento- y en
    //  280x653 a RACK le tocaban 35 px de ancho contra un dedo de 40. Medido:
    //  35 TOUCH nuevos en pantallas que ya estaban. Donde no caben, el CANAL
    //  se queda una fila entera -es el unico que dice un VALOR y no abre una
    //  ficha, asi que solo se lee bien- y las puertas vuelven a repartirse
    //  como siempre. Es lo que ya hace CHOKE en la pagina de SONIDO.
    const int puertasH = Metrics::hit
                       + (padPuertasWraps (ancho) ? Metrics::hit + Metrics::halfGap : 0);
    return secH + puertasH     + Metrics::sm
         + secH + Metrics::hit + Metrics::sm
         + secH + srcH         + Metrics::sm + Metrics::chip;
}

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

//  ¿CABEN LAS PUERTAS DEL PAD EN UNA FILA, CON EL CANAL DENTRO?
//
//  La misma pregunta y por el mismo sitio que `padSourceWraps`: se hace al
//  presupuestar la altura de la pagina y al colocar la fila, y las dos tienen
//  que contestar lo mismo o la ficha reserva una fila que no usa o usa una que
//  no reservo. La quinta tapa solo cuenta cuando la hay - un pad de
//  instrumento -, que es la misma condicion con la que se coloca.
bool MainComponent::padMuestraWraps (int rowWidth) const
{
    return ! padRowFits (rowWidth, { &reverseButton, &loopButton,
                                     &denoiseButton, &recorteButton });
}

bool MainComponent::padPuertasWraps (int rowWidth) const
{
    return vstButton.isVisible()
             ? ! padRowFits (rowWidth, { &padCanalBtn, &padRackBtn, &pianoButton,
                                         &nivelesButton, &vstButton })
             : ! padRowFits (rowWidth, { &padCanalBtn, &padRackBtn, &pianoButton,
                                         &nivelesButton });
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
    //  Y LA ALTURA ES LA DE LA TAPA, no la de la FILA. Esa es la segunda mitad
    //  del mismo fallo y la que quedaba: el cuerpo salia de Metrics::tab -44,
    //  o sea 14.5 px tras el tope- y drawButtonText lo saca de `capaDe`, que es
    //  max(26, alto*0.75) = 33, o sea 12.54. Un 16% mas de sitio del que el
    //  rotulo ocupa, asi que las pestanas se partian en dos filas en pantallas
    //  donde caben en una - y eso no lo caza ninguna regla del banco, porque no
    //  produce ni TRUNC ni SQUEEZE: solo sobra tarjeta. Es exactamente lo que
    //  ya costo 93 apretones inexistentes en UiAudit::captionOf.
    const auto capH = ZatiLookAndFeel::capaDe (juce::Rectangle<float> (0.0f, 0.0f, 10.0f,
                                                                       (float) Metrics::tab)).getHeight();
    const auto capFont = ZatiLookAndFeel::letraDeTapa (capH);
    const juce::TextButton* tabs[] = { &pageAudioBtn, &pageMidiBtn, &pageAspBtn,
                                       &pageProjBtn, &pageGestBtn };

    //  La mas ancha decide, porque las cinco reciben el MISMO quinto. Sumar
    //  los cinco anchos seria la cuenta de un reparto proporcional, que no es
    //  el que hace esta fila.
    float widest = 0.0f;
    for (const auto* b : tabs)
        widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidth (capFont, b->getButtonText()));

    //  Lo que le queda a la letra dentro de un cuarto. El margen NO es
    //  kTextPad: es el que `drawButtonText` deja por dentro, o sea
    //  `Metrics::margenTapa`, y se pide de alli en vez de escribirlo aqui.
    //  Estaba puesto a mano y la formula del dibujo era otra -`jlimit (3, 5,
    //  ancho / 14)`-, que es como una fila cabe en la cuenta y no en la
    //  pantalla: con un 3 escrito a mano contra un 4 dibujado, 344x882 pasaba
    //  con PROYECTOS recortado mientras 280 y 360 salian bien.
    //  Y la fila lleva ademas la x de cerrar, que no es una pestana pero se
    //  lleva su ancho: preguntar por el ancho entero es como una fila cabe en
    //  la cuenta y no en la pantalla.
    const float tabW   = (float) (rowWidth - Metrics::hit - Metrics::xs) / 5.0f
                           - 2.0f * (float) Metrics::halfGap;
    return widest <= tabW - 2.0f * (float) Metrics::margenTapa;
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
                                (juce::Component*) &anchoSlider,
                                (juce::Component*) &attackSlider,(juce::Component*) &releaseSlider,
                                (juce::Component*) &cutSlider,   (juce::Component*) &resoSlider,
                                (juce::Component*) &chokeSlider, (juce::Component*) &modeButton,
                                (juce::Component*) &normButton })
        c->setVisible (onSound);

    for (juce::Component* c : { (juce::Component*) &startSlider,   (juce::Component*) &endSlider,
                                (juce::Component*) &reverseButton, (juce::Component*) &loopButton,
                                (juce::Component*) &waveform,      (juce::Component*) &denoiseButton,
                                (juce::Component*) &recorteButton,
                                (juce::Component*) &zoomOutButton, (juce::Component*) &zoomFitButton,
                                (juce::Component*) &zoomInButton,
                                (juce::Component*) &fadeInSlider,  (juce::Component*) &fadeOutSlider })
        c->setVisible (onTrim);
    //  Y VACIADA ADEMAS DE APAGADA. RECORTAR es la unica de esta lista que
    //  puede quedarse SIN COLOCAR estando encendida: la fila donde vive se
    //  parte en dos donde no caben las cuatro, asi que hay un camino en el que
    //  `layoutModuleBar` no le da coordenadas. El banco lo canto en la primera
    //  corrida - CERO 84 y RESIDUO 28 - que es exactamente para lo que esa
    //  regla existe: apagar sin vaciar es media regla.
    if (! onTrim) recorteButton.setBounds ({});

    padRackBtn.setVisible (onRig);
    padCanalBtn.setVisible (onRig);
    if (! onRig) padCanalBtn.setBounds ({});
    nivelesButton.setVisible (onRig);
    if (! onRig) nivelesButton.setBounds ({});
    //  Y LA PUERTA DEL INSTRUMENTO, apagada Y sin limites cuando no toca. Las
    //  dos cosas: la maqueta que le da coordenadas vive dentro de la pagina
    //  RIG, asi que en las otras se quedaba encendida y de 0x0 - que es lo que
    //  la regla del volcado existe para cazar, y lo canto: 84 hallazgos.
    {
        const bool puerta = onRig && padEsInstrumento (selectedPad);
        vstButton.setVisible (puerta);
        if (! puerta) vstButton.setBounds ({});
    }
    pianoButton.setVisible (onRig);
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
//  LAS DOS PAGINAS DE LA MESA. Apagar *Y* vaciar los limites, las dos cosas:
//  una tira encendida y de 0x0 -o apagada con las coordenadas de la ultima
//  vez- es lo que tuvo a SEGUIR visible desde el primer dia y lo que las
//  reglas de CERO y de RESIDUO existen para cazar. Lo segundo lo hace
//  `showMixBank`, que corre justo detras.
void MainComponent::showMixPage (MixPage p)
{
    mixPage = p;
    mixVistaBtn.setButtonText (T (p == mixPageCanales ? "CANALES" : "PADS"));
    mixVistaBtn.setToggleState (p == mixPageCanales, juce::dontSendNotification);

    for (int c = 0; c < kNumCanales; ++c)
    {
        const bool on = (p == mixPageCanales);
        if (auto* f = canFaders[c]) { f->setVisible (on); if (! on) f->setBounds ({}); }
        if (auto* m = canMutes[c])  { m->setVisible (on); if (! on) m->setBounds ({}); }
    }
    //  Y SIN SOLO tampoco: el solo es de un PAD, y en la pagina de CANALES no
    //  hay una sola tapa de solo que quitar. Un control que no puede hacer
    //  nada visible en la pagina donde esta es lo que esta casa llama ruido.
    //  Apagar *Y* vaciar los limites, las dos cosas.
    mixClearSolo.setVisible (p == mixPagePads);
    if (p != mixPagePads) mixClearSolo.setBounds ({});

    //  Los cuatro chips de banco son de la pagina de PADS: un canal no vive en
    //  un banco.
    for (auto* t : mixBankBtns) if (t != nullptr)
    {
        t->setVisible (p == mixPagePads);
        if (p != mixPagePads) t->setBounds ({});
    }

    mixScroll.setViewPosition (0, 0);
    showMixBank (mixBank);       // apaga y vacia las tiras de pad si toca
    refreshMixStrip();
}

void MainComponent::showMixBank (int bank)
{
    mixBank = juce::jlimit (0, kNumBanks - 1, bank);
    if (auto* t = mixBankBtns[mixBank]) t->setToggleState (true, juce::dontSendNotification);

    for (int i = 0; i < kNumPads; ++i)
    {
        //  Y con la pagina: en CANALES no se ve ni una tira de pad, y las que
        //  se apagan se quedan ademas sin coordenadas.
        const bool on = mixPage == mixPagePads && (i / kPadsPerBank) == mixBank;
        if (! on)
        {
            if (auto* f = mixFaders[i]) f->setBounds ({});
            if (auto* p = mixPans[i])   p->setBounds ({});
        if (auto* a = mixAnchos[i]) a->setBounds ({});
            if (auto* m = mixMutes[i])  m->setBounds ({});
            if (auto* s = mixSolos[i])  s->setBounds ({});
        }
        if (auto* f = mixFaders[i]) f->setVisible (on);
        if (auto* p = mixPans[i])   p->setVisible (on);
        if (auto* a = mixAnchos[i]) a->setVisible (on);
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

//  SI HAY ALGO DELANTE DE LA MAQUINA.
//
//  Una ficha ocupa la ventana entera y lleva un velo del 45 %, asi que lo que
//  hay debajo no se ve y repintarlo es trabajo tirado. La lista NO se escribe a
//  mano: son doce fichas y la trece que alguien anada manana se quedaria fuera
//  sin que nada avisara -es el mismo fallo que `applySkin` restilando ocho
//  tapas por su nombre-. Se recorre el arbol y se pregunta por el TIPO.
//
//  Y el panel XY, que no es una `Sheet` -pinta su propia tarjeta opaca porque
//  una con velo se traga los toques que van a los pads- pero tapa el cristal
//  igual. Es lo mismo que le faltaba para tener capa en el banco.
bool MainComponent::caraTapada()
{
    for (auto* c : getChildren())
        if (c->isVisible()
            && (dynamic_cast<Sheet*> (c) != nullptr || c == static_cast<juce::Component*> (&xyPanel)))
            return true;
    return false;
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
    instSheet.setVisible (false);
    vstSheet.setVisible (false);
    pianoButton.setToggleState (false, juce::dontSendNotification);
    chopSheet.setVisible (false);
    manualSheet.setVisible (false);
    tourSheet.setVisible (false);
    //  Y LA REJILLA DE PADS, que vive ENCIMA de la ficha que la abrio: sin
    //  esto, cerrar el secuenciador dejaba flotando su selector de pad sobre
    //  la cara. Ver abrePadPicker.
    if (padPickAbierto) abrePadPicker (false);
    //  Y la de canales, que vive igual.
    if (canalPickAbierto) abreCanalPicker (false);

    //  Y EL MENU DE UNA RANURA, por lo mismo: tambien vive ENCIMA de todo, asi
    //  que sin esto se queda flotando sobre la ficha que se acaba de abrir.
    if (ranuraEditada >= 0) abreMenuRanura (-1);

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


// FX sheet: knob labels + the live filter response display.
//  UN TITULO DE FICHA SE PINTA POR AQUI, y no con un drawText suelto.
//
//  Catorce fichas y catorce llamadas parecidas pero no iguales, y en una de
//  ellas -la mesa- el texto iba sin T(): decia "MIX" en los cuatro idiomas
//  mientras la tabla tenia la fila MIX->MEZCLA y la pestana que abre la ficha
//  ya la usaba. Un sitio, un dueno.
//
//  Y de paso el titulo queda APUNTADO para el banco. Las seis reglas recorren
//  el arbol de componentes y un rotulo pintado no es un componente: por eso
//  aquello llevaba ahi desde el primer dia sin que nada lo viera. Ver
//  UiAudit::rotulo y Tests/plano.py.
//  LO QUE OCUPA UN TEXTO DENTRO DE SU BANDA, apuntado para el banco.
//
//  Sale de pintaTitulo en cuanto tuvo un segundo cliente, por lo mismo que
//  `normaliza` salio de dentro de `render`: los titulos que se dibujan con
//  drawFittedText -el del secuenciador, el del manual, el del navegador- no
//  pueden usar pintaTitulo, que dibuja con drawText, y por eso no los apuntaba
//  nadie. Un rotulo que el banco no ve es un rotulo que puede acabar debajo de
//  la x sin que 896 corridas digan nada: es exactamente como la mesa estuvo
//  titulada "MIX" a mano durante meses.
juce::Rectangle<int> MainComponent::apunta (juce::Graphics& g, juce::Rectangle<int> caja,
                                            const juce::String& texto, const char* tipo,
                                            float minimo)
{
    auto real = caja;
    const int pide  = (int) std::ceil (juce::GlyphArrangement::getStringWidth (
                                           g.getCurrentFont(), texto));
    const int usado = juce::jmin (caja.getWidth(), pide);
    if (Lang::isRightToLeft (Lang::current())) real = real.removeFromRight (usado);
    else                                       real = real.removeFromLeft (usado);

    //  Y SE APUNTA LO QUE PIDE, ademas de lo que ocupa. `usado` esta acotado a
    //  la banda, asi que un texto que no cabe salia con el mismo rectangulo que
    //  uno que cabe justo y no habia forma de preguntar si se lee entero. Ver
    //  UiAudit::Rotulo.
    UiAudit::rotulo (real, texto, tipo, (int) std::ceil (pide * minimo));
    return real;
}

bool MainComponent::pintaAyuda (juce::Graphics& g, juce::Rectangle<int> banda,
                                const juce::String& texto, juce::Justification justif,
                                float apreton)
{
    if (! cabeEntero (g, banda, texto, apreton))
        return false;

    apunta (g, banda, texto, "dato", apreton);
    g.drawFittedText (texto, banda, justif, 1, apreton);
    return true;
}

//  EL MODO ES UNO Y LAS TAPAS SON TRES.
//
//  La cara, la ficha de la cancion y la del secuenciador tienen cada una su
//  fila de transporte, y las tres preguntan lo mismo: que toca PLAY. Es
//  exactamente lo que ya pasa con PLAY -playButton, songPlayBtn y seqPlayBtn
//  son tres tapas de un estado- asi que se sigue el mismo patron y no se
//  inventa otro: quien escribe el estado es esta funcion, y las tres tapas
//  sacan su cara de aqui.
//
//  Estaba SOLO en la ficha de la cancion, que es la unica de las tres que no
//  tiene PLAY al lado: armar el modo alli y salir a pulsar PLAY es un viaje, y
//  la pregunta se hace justo antes de pulsar.
//  EL TRANSPORTE ES UNO Y LAS TAPAS SON TRES, que es lo que el comentario de
//  `ponModoCancion` daba por hecho y no era verdad.
//
//  Ahi abajo dice «es exactamente lo que ya pasa con PLAY -playButton,
//  songPlayBtn y seqPlayBtn son tres tapas de un estado- asi que se sigue el
//  mismo patron». El modo SI tenia embudo; el transporte tenia DIEZ
//  escritores y ninguno, cada uno sincronizando las tapas que se acordaba:
//
//   - la tapa de la cara no tocaba las otras dos;
//   - `seqPlayBtn` y `songPlayBtn` se acordaban de la cara y no la una de la
//     otra;
//   - abrir un proyecto y vaciar un pad ponian el ROTULO a mano
//     -`setButtonText (T ("PLAY"))`- sin el dibujo, o sea una tapa que dice
//     PLAY con el icono de STOP;
//   - y los dos caminos del foco de audio -perder una llamada y volver-
//     hacian `setToggleState` y NADA MAS, asi que al volver de una llamada la
//     cara se quedaba diciendo PLAY con el transporte rodando.
//
//  Un estado, un dueno. Y aqui se ve por que hacia falta: la lista de tapas
//  esta escrita UNA vez, asi que la cuarta fila de transporte que alguien
//  anada manana entra sola.
void MainComponent::ponTransporte (bool on)
{
    //  PARAR DESARMA AUTO, y eso no es una comodidad: un modo de escritura que
    //  se queda puesto es como se borra una automatizacion buena en la pasada
    //  siguiente sin haber tocado nada a proposito. Ademas `ponAutoArmado`
    //  publica al desarmar, asi que lo que se acabe de tocar entra en la tabla
    //  que suena antes de la vuelta siguiente.
    if (! on && autoArmado) ponAutoArmado (false);

    engine.setPlaying (on);
    juce::TextButton* tapas[3] = { &playButton, &seqPlayBtn, &songPlayBtn };
    for (juce::TextButton* b : tapas)
    {
        b->setToggleState (on, juce::dontSendNotification);
        transporte (*b, on);
    }
}

void MainComponent::ponModoCancion (bool on)
{
    engine.setSongMode (on);
    for (juce::TextButton* b : { &songModeBtn, &modoBtn, &seqModoBtn })
    {
        b->setToggleState (on, juce::dontSendNotification);
        modoTapa (*b, on);
    }
    status.setText (on ? T ("PLAY toca la cancion") : T ("PLAY toca el patron / la cadena"),
                    juce::dontSendNotification);
}

juce::Rectangle<int> MainComponent::pintaTitulo (juce::Graphics& g, juce::Rectangle<int> caja,
                                                 const juce::String& texto, const char* tipo,
                                                 bool elipsis, float apretar)
{
    //  LO QUE SE APUNTA ES LO QUE OCUPA EL TEXTO, no la banda que se le dio.
    //
    //  Casi todas las bandas de rotulo de esta app son del ancho entero de la
    //  tarjeta y el texto ocupa un tercio, asi que apuntar la banda hace
    //  imposible la pregunta que importa: si el rotulo llega hasta debajo de un
    //  control. "AJUSTES - AUDIO" pasaba por debajo de la tapa de CUADRAR y por
    //  debajo de la x, y ninguna de las reglas del banco podia verlo porque un
    //  rotulo pintado no es un componente y la banda solapaba de todas formas.
    const auto real = apunta (g, caja, texto, tipo,
                              elipsis ? 0.0f : (apretar > 0.0f ? apretar : 1.0f));

    //  Y APRETAR ES DE ESTA FUNCION, no de quien la llama. Habia CUATRO formas
    //  de pintar el titulo de una ficha -esta, `UiAudit::rotulo` + `drawText`
    //  en la mesa, `apunta` + `drawFittedText` en el secuenciador y NADA en el
    //  tour- y las dos del medio existian solo porque aqui no se podia apretar.
    //  Una regla escrita cuatro veces son cuatro reglas; la cuarta es la que un
    //  dia se escribe mal, y en el tour ya lo estaba: era el unico titulo de
    //  ficha que no publicaba su banda, o sea invisible para el volcado de
    //  rotulos.
    //
    //  `apretar` a cero deja el `drawText` de siempre, asi que las llamadas
    //  que ya habia no mueven un pixel.
    if (apretar > 0.0f) g.drawFittedText (texto, caja, Lang::start(), 1, apretar);
    else                g.drawText (texto, caja, Lang::start(), elipsis);
    return real;
}


//  EL PANEL DE UN GRUPO. Ver la declaracion para POR QUE esta aqui fuera y no
//  dentro del pintor del secuenciador, que es donde nacio.
//
//  Se separa con `groupOn` y no con `groove`, que es lo que hacia hasta ahora:
//  `groove` es una SOMBRA y escoge su direccion con el corte de recess, y en
//  LACA -la carcasa de fabrica- eso dejaba el panel a 5.3 de dE contra la
//  tarjeta, por debajo del 6.0 que este proyecto le exige al hueco de una
//  celda. Ver ZatiColours::groupOn para la cuenta entera.
//
//  Y la superficie es `chassisTop`, que aqui NO es una suposicion: Sheet::paint
//  rellena la tarjeta con `chassisTop` exacto, asi que la superficie de una
//  ficha y la de la cara son la misma. El dia que una ficha se pinte de otro
//  color, se le pasa esa - que es la misma regla que ya costo una medida con la
//  sombra de las tapas, escrita con la TINTA sobre un chasis oscuro.
//
//  DOS pixeles de aire arriba y abajo y halfGap a los lados, no cuatro por
//  lado: entre grupo y grupo hay exactamente Metrics::sm, asi que cuatro los
//  dejaba TOCANDOSE y los seis paneles de la pagina PASO se leian como una sola
//  losa, que es lo mismo que se lee sin dibujar nada.
void MainComponent::pintaPaneles (juce::Graphics& g,
                                  const juce::Array<juce::Rectangle<int>>& grupos) const
{
    const auto relleno = ZatiColours::groupOn (ZatiColours::chassisTop, Metrics::panelHondura);
    //  El filo se decide contra el CHASIS y no contra el relleno: groupOn
    //  escoge el lado midiendo el brillo de lo que se le pasa, y el relleno es
    //  un color TRANSLUCIDO -blanco o negro con alfa- asi que preguntarle su
    //  brillo daria el del blanco o el del negro y no el de la superficie.
    //  Pintado encima del relleno, el resultado compuesto es el mismo.
    const auto filo = ZatiColours::groupOn (ZatiColours::chassisTop, Metrics::panelBorde);

    for (const auto& e : grupos)
    {
        if (e.isEmpty()) continue;
        const auto caja = e.expanded (Metrics::panelAireX, Metrics::panelAireY);
        //  Apuntado para que el banco pueda medir el aire de los cuatro lados.
        //  Ver Tests/paneles.py: un panel no se puede salir ni solapar -las seis
        //  reglas no le aplican- asi que su unico fallo posible es el reparto
        //  del aire, y eso no lo ve ninguna de las que ya hay.
        UiAudit::panel (caja);
        g.setColour (relleno);
        g.fillRoundedRectangle (caja.toFloat(), (float) Metrics::sm);
        g.setColour (filo);
        g.drawRoundedRectangle (caja.toFloat().reduced (0.5f), (float) Metrics::sm, 1.0f);
    }
}

void MainComponent::Sheet::paint (juce::Graphics& g)
{
    //  Ver UiAudit::capaActual: lo que esta ficha pinte queda apuntado como
    //  suyo, para que el banco no compare un rotulo de aqui con una tapa de la
    //  cara que sigue maquetada debajo.
    UiAudit::capaActual = (int) getProperties()["capa"];
    //  La ficha ocupa la ventana entera, asi que sus coordenadas ya son las
    //  de la ventana. Ver UiAudit::origenPintado.
    UiAudit::origenPintado = { 0, 0 };

    //  Ver pintaTodo: el tour se dibuja entero el, foco incluido.
    if (pintaTodo) { if (paintContent) paintContent (g); return; }

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

    //  Y el contenido lo pinta el CUERPO cuando la ficha se desplaza: sus
    //  bandas estan en coordenadas del cuerpo, asi que pintarlas aqui las
    //  dibujaria desplazadas por el margen de la tarjeta y quietas mientras el
    //  contenido se mueve. Ver Sheet::hazDesplazable.
    if (paintContent && ! desplazable) paintContent (g);
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
    //  drawButtonText le quita `Metrics::margenTapa` mas por lado. Contar
    //  solo los 16 del reparto dejaba pasar por un pixel - "CARGAR KIT" pedia
    //  75 y tenia 74 - que es exactamente el fallo que esta funcion existe
    //  para no tener.
    //  Y LA FUENTE SE QUEDA EN ONCE, MEDIDO.
    //
    //  Se probo sacarla de `capaDe` como en setTabsFit -11.4 px en una fila de
    //  Metrics::hit- porque el comentario de arriba promete "misma fuente que
    //  el reparto" y no lo era. Salio PEOR: 2214 incumplimientos del dedo pasan
    //  a 2222 en las 896 corridas. Subir el ancho medido hace que mas filas
    //  contesten "no cabemos" y se partan en dos, y una fila partida deja las
    //  tapas mas estrechas y mas bajas justo en las pantallas que ya iban
    //  justas. Es el tercer arreglo de maquetado que esta casa deshace por
    //  medirlo, y va escrito para no volver a intentarlo.
    const auto capFont = ZatiColours::monoFont (11.0f, true).withExtraKerningFactor (0.06f);
    constexpr int kChrome = 2 * Metrics::sm + 2 * (Metrics::halfGap / 2) + 2 * Metrics::margenTapa;
    int total = 0;
    //  Doce, el mismo tope que layoutModuleBar: si esta contase ocho y aquella
    //  colocase nueve, la respuesta "cabe" seria sobre una fila que no es la
    //  que se dibuja.
    for (int i = 0; i < juce::jlimit (1, 12, count); ++i)
        total += (int) std::ceil (juce::GlyphArrangement::getStringWidth (capFont, mb[i]->getButtonText()))
               + kChrome;
    return total <= rowWidth;
}

//  UNA FILA, UN TRATO: los iconos de una fila salen todos o no sale ninguno.
//
//  reparteTapa decide tapa por tapa -donde el rotulo no cabe entero, el dibujo
//  no sale- y esa regla es correcta y sale MAL en una fila: medido en la cara,
//  cinco pestanas tenian sitio y CANCION no, asi que la barra salia con cinco
//  iconos y un hueco. Eso no se lee como "aqui no cabia", se lee como una tapa
//  a la que le falta algo.
//
//  Vive aqui y no en reparteTapa porque reparteTapa solo ve UNA tapa: quien
//  sabe cuales son hermanas es quien las coloca. Y se llama desde todas las
//  filas, no solo desde layoutModuleBar - las cuatro pestanas de AJUSTES se
//  reparten a cuartos a mano y salian con tres dibujos y un hueco.
//
//  Y SE BORRA LA MARCA ANTES DE PREGUNTAR. La respuesta de la pasada anterior
//  entra en la pregunta de la siguiente -reparteTapa la mira-, asi que sin este
//  barrido la fila contestaria "aqui no cabe ninguno" porque la ultima vez no
//  cabia, se desmarcaria, y al maquetar otra vez volveria a marcarse: una barra
//  que parpadea entre con y sin iconos cada vez que se gira el telefono.
void MainComponent::filaDeIconos (juce::TextButton** fila, int n)
{
    for (int i = 0; i < n; ++i)
        fila[i]->getProperties().remove ("sinIcono");

    bool todas = true;
    for (int i = 0; i < n && todas; ++i)
        if ((int) fila[i]->getProperties().getWithDefault ("icono", 0) != 0)
            todas = ZatiLookAndFeel::reparteTapa (*fila[i]).id != Iconos::Id::ninguno;

    if (! todas)
        for (int i = 0; i < n; ++i)
            fila[i]->getProperties().set ("sinIcono", 1);
}


//  QUE SUENA AL TOCAR EL PAD `index`, y con que fuerza.
//
//  El nivel sale de la posicion DENTRO del banco y no del numero absoluto: la
//  rejilla ensena dieciseis y el 01 esta abajo a la izquierda, asi que el mas
//  flojo cae donde la mano ya lo busca. Y el pad es el CAPTURADO al encender el
//  modo: leerlo del selector en cada golpe haria que tocar un pad cambiase el
//  destino y el modo se perseguiria a si mismo.
MainComponent::Disparo MainComponent::disparoDe (int index) const
{
    const int i = juce::jlimit (0, kNumPads - 1, index);
    if (! nivel16) return { i, 1.0f };

    const int n = i % kPadsPerBank;
    return { juce::jlimit (0, kNumPads - 1, nivelPad),
             (float) (n + 1) / (float) kPadsPerBank };
}

//  EL DEDO COMO TECLA. Ver PadButton::setModoNota.
//
//  Pasa por disparoDe, que es la misma cuenta que usa el toque normal, asi que
//  16 NIVELES sigue mandando: una rejilla de niveles sobre un instrumento son
//  dieciseis fuerzas del mismo sonido, y cada una con su propio largo.
void MainComponent::padNotaOn (int index, float vel)
{
    //  Con LOAD armado el toque es para cargar, no para sonar: lo recoge el
    //  click, que llega al levantar.
    if (loadArmed) return;

    const auto d = disparoDe (index);
    if (! padHasSample[(size_t) d.pad]) return;

    //  ESTA la sostiene el dedo, asi que va sin largo: el "suelta" llega en
    //  padNotaOff. Por postNoteOnAt y no por postNoteOn, que es el camino de un
    //  golpe y le pone el largo de una audicion - 1200 ms - que en modo tecla
    //  cortaria la nota con el dedo todavia encima.
    engine.postNoteOnAt (d.pad, 0, d.vel * vel, AudioEngine::kSostenida);
    //  Se apunta QUE pad esta sonando por este dedo y no se recalcula al
    //  levantar: entre medias se puede haber apagado 16 NIVELES, y entonces
    //  se soltaria una nota que no es la que empezo - o ninguna.
    notaViva[(size_t) juce::jlimit (0, kNumPads - 1, index)] = d.pad;
}

void MainComponent::padNotaOff (int index)
{
    const int i = juce::jlimit (0, kNumPads - 1, index);
    if (notaViva[(size_t) i] < 0) return;
    engine.postNoteOff (notaViva[(size_t) i]);
    notaViva[(size_t) i] = -1;
}

//  Un pad se toca como tecla cuando lo que va a sonar es un instrumento, y eso
//  con 16 NIVELES no es el pad que se toca sino el capturado.
void MainComponent::refreshModoNota()
{
    const int destino = nivel16 ? juce::jlimit (0, kNumPads - 1, nivelPad) : -1;
    for (int i = 0; i < kNumPads; ++i)
        if (auto* p = pads[i])
            p->setModoNota (padEsInstrumento (destino >= 0 ? destino : i));
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

    //  CON SOLO ARMADO, TOCAR AISLA Y NO SUENA. Las dos mitades: si ademas
    //  disparara, «aisla» seria verdad y «es un modo» no — y el golpe que
    //  buscas oir aislado te lo comerias tu mismo en el mismo toque.
    //
    //  Y NO SE ARMA SOLO EN UN PAD VACIO: aislar un hueco deja la maquina muda
    //  y con cara de rota. Es la misma guardia que ya tiene el disparo.
    if (soloArmado)
    {
        if (! padHasSample[(size_t) index])
        {
            status.setText (T ("Pad vacio - no hay nada que aislar"), juce::dontSendNotification);
            return;
        }

        const bool ahora = ! engine.isPadSoloed (index);
        engine.setPadSolo (index, ahora);
        selectPad (index);
        refrescaRejillaModo();
        status.setText (ahora ? T ("SOLO pad %1", juce::String (index + 1))
                              : T ("SOLO fuera del pad %1", juce::String (index + 1)),
                        juce::dontSendNotification);
        return;
    }

    //  EN MODO TECLA LA NOTA YA EMPEZO al apretar y ya se solto al levantar.
    //  Dispararla otra vez aqui seria un golpe de mas por toque - el mismo
    //  fallo que la prueba de la cancion tuvo con el compas que vuelve a cero.
    if (pads[index] != nullptr && pads[index]->enModoNota()) return;

    //  DIECISEIS NIVELES: la rejilla deja de ser dieciseis pads y pasa a ser UN
    //  pad a dieciseis fuerzas. Ver disparoDe.
    if (nivel16 && padHasSample[(size_t) juce::jlimit (0, kNumPads - 1, nivelPad)])
    {
        const auto d = disparoDe (index);
        engine.postNoteOn (d.pad, d.vel);
        status.setText (T ("PAD %1 - nivel %2", juce::String (d.pad + 1),
                           juce::String (index % kPadsPerBank + 1)),
                        juce::dontSendNotification);
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

//  LOS SIETE MANDOS DE LA TIRA, APUNTANDO AL PASO QUE HAY TOCADO.
//
//  Estaba escrito dentro de stepCellToggled, o sea que solo se refrescaba al
//  tocar una celda. Cambiar de PAD con el mismo paso tocado -PAD -/+ en el
//  piano, un toque en un pad, la rejilla de dieciseis- dejaba los siete
//  ensenando los valores del pad ANTERIOR, y el primer arrastre los escribia
//  en el pad nuevo: exactamente el fallo que el comentario de abajo ya
//  describia para el paso, entrando por otra puerta.
//
//  Sin paso tocado no hay nada que ensenar y la tira no se maqueta siquiera.
void MainComponent::refrescaTiraPaso()
{
    const int step = selectedStep, pad = selectedPad;
    if (step < 0 || pad < 0) return;

    //  Los mandos siguen a lo que se acaba de tocar, asi que lo que ensenan es
    //  siempre el paso que hay debajo del dedo y nunca el ultimo.
    noteSlider.setValue (engine.getStepNote (selectedPattern, step, pad), juce::dontSendNotification);
    velSlider.setValue  (engine.getStepVel  (selectedPattern, step, pad), juce::dontSendNotification);
    rollSlider.setValue (engine.getStepRoll (selectedPattern, step, pad), juce::dontSendNotification);

    const int lk = engine.getStepLock (selectedPattern, step, pad);
    lockSlider.setValue (lk == AudioEngine::kNoLock ? 0.0 : (double) (lk + 1),
                         juce::dontSendNotification);
    //  Y los otros cuatro. Sin esto los mandos ensenan el paso ANTERIOR y
    //  el primer arrastre escribe ese valor en el que se acaba de tocar,
    //  que es como se pierde un bloqueo sin tocarlo.
    juce::Slider* cuatro[4] = { &atkPasoSlider, &relPasoSlider, &iniPasoSlider, &panPasoSlider };
    for (int i = 0; i < 4; ++i)
        cuatro[i]->setValue ((double) engine.getStepPLock (selectedPattern, step, pad, i),
                             juce::dontSendNotification);
}

void MainComponent::stepCellToggled (int pad, int step)
{
    if (step >= engine.getPatternLength (selectedPattern)) return;
    //  El PRIMER paso que se toca hace aparecer la tira de debajo de la
    //  rejilla, y eso es un cambio de maqueta y no de contenido: sin este
    //  resized la tira no sale hasta que algo mas la provoque - girar el
    //  telefono, cambiar de pagina - o sea nunca, mirandolo desde el dedo.
    const bool teniaPaso = (selectedStep >= 0);
    selectedStep = step;
    selectPad (pad);                 // the lane you touched becomes the pad you edit
    refrescaTiraPaso();

    const bool nv = ! pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad];
    pattern[(size_t) selectedPattern][(size_t) step][(size_t) pad] = nv;
    engine.setStep (selectedPattern, step, pad, nv);
    refreshStepGrid();
    if (! teniaPaso) resized();
    seqSheet.repaint();
}

// Copy the pattern into the flat buffer the grid reads, plus each pad's colour
// and whether it holds a sample, then hand it the live playhead.
//  SEGUIR. La vista salta al compas que suena, y solo cuando cambia: pedir el
//  salto en cada tick repintaria la ficha entera treinta veces por segundo,
//  que es justo lo que costo arreglar hace dos tandas.
//
//  Y vive FUERA de refreshStepGrid desde que el temporizador pregunta por
//  PAGINA: cual es el compas que se mira no es cosa de la rejilla de pasos
//  sino de la ficha, y el piano dibuja el compas `selectedBar` igual que ella.
//  Dejarlo dentro habria dejado el piano clavado en su compas con SEGUIR
//  puesto; copiarlo en los dos sitios seria la misma regla escrita dos veces,
//  que es como se separan.
//  Ver MainComponent::kMinTrimSamples. Sin muestra no hay longitud que
//  convertir, asi que se cae al valor de siempre - y el tope de 0.5 es para que
//  una muestra de dos milisegundos no pida una ventana mayor que ella misma.
double MainComponent::minTrim01 (int pad) const
{
    const int len = padVisibleLength (pad);
    return len > 0 ? juce::jlimit (1.0e-6, 0.5, (double) kMinTrimSamples / (double) len)
                   : 0.005;
}

void MainComponent::seguirCompas (int ps)
{
    if (! seqFollow || ps < 0) return;

    //  SEGUIR MUEVE LA VENTANA POR PAGINAS Y NO POR COMPASES, que es lo que la
    //  ventana continua obliga a decidir: si el paso que suena ya se ve no se
    //  mueve nada —una vista que se recentra en cada paso no se puede leer— y
    //  si se sale, salta a la pagina que lo contiene. Cuantas columnas son una
    //  pagina lo dice la rejilla que se este mirando, que es quien conoce su
    //  ancho y su zoom.
    const int cols = juce::jmax (1, (seqPage == seqPagePiano) ? pianoGrid.numPasos()
                                                             : stepGrid.numCols());
    if (ps >= seqPrimerPaso && ps < seqPrimerPaso + cols) return;

    const int len = engine.getPatternLength (selectedPattern);
    seqPrimerPaso = juce::jlimit (0, juce::jmax (0, len - cols), (ps / cols) * cols);
}

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

    seguirCompas (ps);

    {
        const bool rodando = engine.isPlaying();
        seqPlayBtn.setToggleState (rodando, juce::dontSendNotification);
        transporte (seqPlayBtn, rodando);
    }

    stepGrid.setSource (gridCells, gridZati, gridLoaded, gridNotes,
                        engine.getPatternLength (selectedPattern),
                        seqPrimerPaso, ps, selectedPad - base,
                        ps >= 0 ? engine.getStepPhase() : 0.0f,
                        base);   // el pad del carril 0, para que el canalon diga 17..32 en el banco B

    //  Y LA BARRA DICE DONDE ESTA LA VENTANA Y DONDE EL CABEZAL.
    //
    //  Lo segundo lo hacian las tapas de compas pintando de rojo la que sonaba:
    //  la rejilla solo puede marcar la columna viva cuando esa columna esta en
    //  pantalla, asi que con cuatro compases se perdia el pulso entero mientras
    //  editabas el 1 y sonaba el 3. La barra lo dice sobre el TOTAL, que es
    //  ademas lo que la fila de tapas no podia decir.
    {
        const int len  = engine.getPatternLength (selectedPattern);
        const int cols = stepGrid.numCols();
        seqBarra.ponRango (seqPrimerPaso, cols, len);
        seqBarra.ponCabezal (ps);
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
    //  El modo tecla se decide aqui porque aqui pasa TODO lo que cambia lo que
    //  un pad tiene: cargar, vaciar, deshacer, abrir un proyecto y el reparto
    //  de un kit. Ponerlo en assignSampleToPad habria dejado fuera al que
    //  vacia, y un pad vaciado se habria quedado esperando un "suelta".
    refreshModoNota();

    if (auto* p = pads[index])
    {
        p->setSelected (index == selectedPad);
        p->setFlash (padFlash[(size_t) index]);
        //  Callado por el solo de OTRO. Se pregunta al motor y no a un espejo:
        //  el solo se pone tambien desde la mesa y desde un proyecto, y una
        //  copia aqui seria la tercera version de la misma verdad.
        p->setMudo (engine.anySolo() && ! engine.isPadSoloed (index));
        p->setModo (tinteDelModo());
    }
}

//  DE QUE COLOR ES EL MODO QUE HAY ARMADO, o transparente si no hay ninguno.
//
//  Escrito UNA vez y no en los tres sitios que arman: LOAD, REC y SOLO son la
//  misma clase de estado -tocar un pad deja de sonar y hace otra cosa- y el
//  aviso que la rejilla da tiene que ser el mismo mecanismo, o el dia que
//  entre un cuarto modo se quedara sin el.
//
//  Rojo grabando, que es la regla de la casa escrita en `Zati.h`; el acento
//  para los otros dos, que es lo que ya dice «esta tapa esta encendida».
//  COMO SE LLAMA CADA MANDO, escrito UNA vez.
//
//  Lo usan dos: `retranslateUi`, que lo pone como nombre accesible -lo que lee
//  TalkBack-, y `paintPadSheetContent`, que lo DIBUJA en la banda de encima del
//  dial. Antes el pintor llevaba los literales y el nombre accesible no
//  existia; con la tabla, la palabra que se ve y la que se oye son la misma por
//  construccion.
//
//  Las claves son las que ya estaban en el pintor, sin traducir aqui: quien
//  llama decide si las pasa por `T()` -el pintor si, y el nombre accesible
//  tambien-.
const std::vector<MainComponent::Mando>& MainComponent::tablaDeMandos()
{
    //  Se llena una vez y se queda: son punteros a miembros de ESTA instancia,
    //  asi que un `static` local los congelaria del primer objeto que llamara.
    if (! mandos.empty()) return mandos;

    mandos =
    {
        //  EL PAD, pagina SONIDO. Estos los dibuja el pintor leyendo esta misma
        //  tabla.
        { &pitchSlider,   "PITCH" },      { &fineSlider,    "FINO" },
        { &volSlider,     "GANANCIA" },   { &panSlider,     "PAN" },
        { &attackSlider,  "ATTACK" },     { &releaseSlider, "RELEASE" },
        { &cutSlider,     "CORTE|filtro" },{ &resoSlider,   "RESON" },
        { &anchoSlider,   "ANCHO" },      { &chokeSlider,   "CHOKE" },
        //  ...y pagina RECORTE.
        { &startSlider,   "START" },      { &endSlider,     "END" },
        { &fadeInSlider,  "SUAVE IN" },   { &fadeOutSlider, "SUAVE OUT" },

        //  EL SECUENCIADOR. Aqui el nombre lo pinta la maqueta -`seqLabelBands`
        //  guarda la banda con su clave, no el mando- asi que la palabra vuelve
        //  a aparecer alli. Es la unica pareja que esta tabla no puede unir sin
        //  reescribir como se colocan esas bandas, y va anotado en vez de
        //  arreglado a medias.
        { &bpmSlider,     "TEMPO" },      { &patternSlider, "PATRON" },
        { &lengthSlider,  "LARGO" },      { &swingSlider,   "SWING" },
        { &gridSlider,    "REJILLA" },    { &euclidSlider,  "EUCLIDES" },
        { &noteSlider,    "NOTA" },       { &velSlider,     "GOLPE" },
        { &rollSlider,    "REPETIR" },    { &lockSlider,    "CORTE|filtro" },
        { &atkPasoSlider, "ATTACK" },     { &relPasoSlider, "RELEASE" },
        { &iniPasoSlider, "START" },      { &panPasoSlider, "PAN" },

        //  LA CANCION y el maestro.
        { &songLenSlider, "COMPASES" },   { &masterFader,   "MASTER" },

        //  Y el ANCHO de una banda del EQ, que vive en su propia ficha.
        { &eqQKnob,       "ANCHO" },
    };
    return mandos;
}

//  La clave de UN mando, o nullptr si no esta en la tabla.
const char* MainComponent::claveDeMando (const juce::Slider& s)
{
    for (const auto& m : tablaDeMandos())
        if (m.s == &s) return m.clave;
    return nullptr;
}

juce::Colour MainComponent::tinteDelModo() const
{
    if (recArmed)   return kRec;
    if (soloArmado) return ZatiColours::accent;
    if (loadArmed)  return ZatiColours::accent;
    return juce::Colours::transparentBlack;
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
    //  LAS DOS PUERTAS DICEN A QUE PAD LLEVAN, y con DOS cifras siempre: un
    //  rotulo que pasa de "9" a "10" cambia de ancho, y la fila se reparte por
    //  el texto que lleva - o sea que la cabecera daria un salto al cambiar de
    //  pad. Es el mismo argumento por el que el muelle del tour mide con el
    //  parrafo mas largo y no con el que toca.
    {
        const auto dosCifras = juce::String (index + 1).paddedLeft ('0', 2);
        pianoPadPickBtn.setButtonText (dosCifras);
        padPadPickBtn  .setButtonText (dosCifras);
        chopPadPickBtn .setButtonText (dosCifras);
    }
    updateControlsFromPad (index);
    waveform.setSample (uiSample[(size_t) index]);
    {
        int zi = 0, zf = 0;
        if (! zonaVisible (index, zi, zf)) { zi = 0; zf = 0; }
        waveform.setVentana (zi, zf);
    }
    //  Y con la muestra puesta, lo mas estrecho que puede quedar su recorte:
    //  ver minTrim01. La onda lo necesita porque el asa se arrastra sobre ella
    //  y es alli donde el limite se toca de verdad.
    waveform.setMinTrim ((float) minTrim01 (index));
    //  Y la casilla, que ahora dice segundos y por tanto depende de la muestra:
    //  sin esto, cambiar de pad con el recorte en el mismo sitio dejaba escrito
    //  el tiempo del pad anterior.
    startSlider.updateText();
    endSlider.updateText();
    waveform.setTrim (padStart01[(size_t) index], padEnd01[(size_t) index]);
    pushFadesToWaveform();
    if (auto sb = uiSample[(size_t) index])
    {
        const double sr = sb->sourceSampleRate;
        const double secs = sr > 0.0 ? (double) padVisibleLength (index) / sr : 0.0;
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
    //  Y el piano roll, que dibuja las notas de ESTE pad: dejarlo sin avisar
    //  ensena las notas del pad anterior con el nombre del nuevo en la cabecera,
    //  que es la peor de las dos mentiras posibles.
    if (seqSheet.isVisible() && seqPage == seqPagePiano) refreshPiano();
    //  Y LA TIRA DEL PASO, que actua sobre (paso, pad) y hasta ahora solo se
    //  refrescaba al tocar una celda. Ver refrescaTiraPaso.
    refrescaTiraPaso();
    if (seqSheet.isVisible())
    {
        refreshStepGrid();      // el carril marcado es el del pad elegido
        seqSheet.repaint();     // y la cabecera dice de que pad son las notas
    }
    //  Y EL TROCEADO, que ya se podia cambiar de pad DESDE QUE la ficha pasa
    //  por `openSheet` -o sea desde que un toque en un pad que asoma llega a
    //  `selectPad`- y no se enteraba: la cabecera decia «PAD 05», los golpes
    //  eran los del pad anterior y la lista de destinos tambien. Un pad y su
    //  ficha contando cosas distintas es el fallo que ya costo una medida con
    //  el corte del pad.
    if (chopSheet.isVisible())
    {
        if (chopHitsFor != selectedPad) refreshChopHits();
        recalculaCortes();
        refreshChopSheet();
    }
    if (padPickAbierto) refrescaPadPicker();

    //  Y EL CANAL DEL PAD, que es la linea que arregla «cuando pasas de un pad
    //  a otro, los huecos de los efectos sigue igual».
    //
    //  `selectPad` refrescaba quince cosas y NINGUNA era del lado de los
    //  efectos: ni las seis ranuras, ni los tres mandos, ni el plato, ni el
    //  rack. Con la fila siendo del CANAL, elegir un pad elige su canal y los
    //  tres espejos se rehacen desde `refrescaRanuras`, que ya era el unico
    //  punto de reparto.
    //
    //  Y el RACK con ellos, que es ademas el desajuste que ya existia: su
    //  `rackPad` era suyo y `selectPad` no lo tocaba, asi que tocar un pad que
    //  asoma detras de la tarjeta dejaba el rack enseñando los envios de otro.
    const int canalDelPad = engine.getPadCanal (index);
    if (canalDelPad != canalActual)
    {
        canalActual = canalDelPad;
        refrescaRanuras();
        if (rackSheet.isVisible()) refreshRack();
    }
    refrescaCanalDelPad();
}

//  UN TOQUE EN UN PAD QUE ASOMA POR DEBAJO DE UNA FICHA ABIERTA.
//
//  La tarjeta se centra al 78 % PARA QUE la maquina se siga viendo -lo dice
//  sheetFromBottom- y se veia y no se podia tocar: cualquier toque fuera de la
//  tarjeta cerraba la ficha. O sea que cambiar el pad que edita el
//  secuenciador costaba cerrar, elegir y volver a abrir, con la rejilla de
//  pads delante todo el rato.
//
//  Solo lo que se VE: el punto ya viene de fuera de la tarjeta, asi que un pad
//  medio tapado responde por su mitad visible y ni un pixel mas. Se toca lo que
//  se ve, que es la unica regla que no hay que explicar - y para llegar a los
//  dieciseis esta la rejilla de padPickSheet.
//  QUE PAD CAE DEBAJO DEL PUNTO, o -1. Sale de dentro de `tocaPadDetras` en
//  cuanto tuvo tres clientes -el toque normal, la ficha del instrumento y el
//  menu de INSTRUMENTOS-, por lo mismo que `normaliza` salio de dentro de
//  `render`: dos busquedas del mismo pad escritas por su cuenta se separan.
int MainComponent::padDetras (juce::Point<int> p) const
{
    //  Las coordenadas cuadran porque la ficha se pone en getLocalBounds() y no
    //  en el area segura: su origen es (0,0), o sea el mismo sistema en el que
    //  estan los limites de los pads. Ver sheetFromBottom.
    for (int i = 0; i < pads.size(); ++i)
        if (auto* b = pads[i])
            if (b->isVisible() && b->getBounds().contains (p))
                return i;
    return -1;
}

bool MainComponent::tocaPadDetras (juce::Point<int> p)
{
    const int i = padDetras (p);
    if (i < 0) return false;

    //  padClicked es el embudo de la cara: suena, anuncia la presion, graba si
    //  REC esta armado y termina en selectPad. Mismo gesto, mismo resultado -
    //  que es lo unico que hace que no haya que aprender nada nuevo.
    padClicked (i);
    return true;
}

//  EL ROTULO DE LA PUERTA Y LA LUZ DE LA REJILLA. Dos cifras siempre: un «9»
//  que pasa a «10» cambia de ancho, y la fila se reparte por el texto — la
//  cabecera daria un salto al cambiar de pad.
void MainComponent::refrescaCanalDelPad()
{
    const int c = engine.getPadCanal (selectedPad);
    padCanalBtn.setButtonText (T ("CANAL") + " " + Lang::ltr (juce::String (c + 1).paddedLeft ('0', 2)));
    for (int i = 0; i < canalBtns.size(); ++i)
        canalBtns[i]->setToggleState (i == c, juce::dontSendNotification);
}

//  LA REJILLA DE DIECISEIS CANALES, ABIERTA O CERRADA.
void MainComponent::abreCanalPicker (bool abrir)
{
    canalPickAbierto = abrir;
    canalSheet.setVisible (abrir);

    if (abrir)
    {
        canalSheet.toFront (false);
        refrescaCanalDelPad();
    }
    else
    {
        //  APAGAR *Y* VACIAR LOS LIMITES, las dos cosas — la regla que tuvo a
        //  SEGUIR visible y de 0x0 desde el primer dia.
        for (auto* b : canalBtns) if (b != nullptr) b->setBounds ({});
        canalCloseBtn.setBounds ({});
        canalSheet.sheetBounds = {};
    }

    resized();
    repaint();
}

//  LA REJILLA DE DIECISEIS, ABIERTA O CERRADA. Ver padPickSheet en la cabecera.
void MainComponent::abrePadPicker (bool abrir)
{
    padPickAbierto = abrir;
    padPickSheet.setVisible (abrir);

    if (abrir)
    {
        //  ENCIMA de la ficha que la abrio, que es todo el argumento: no cuesta
        //  alto porque no vive dentro de la maqueta de nadie.
        padPickSheet.toFront (false);
        refrescaPadPicker();
    }
    else
    {
        //  APAGAR *Y* VACIAR LOS LIMITES, las dos cosas. Un control encendido y
        //  de 0x0 -o apagado con las coordenadas de la ultima vez- es
        //  exactamente lo que costo SEGUIR y lo que la septima regla del banco
        //  existe para cazar.
        for (auto* b : padPickBtns)     if (b != nullptr) b->setBounds ({});
        for (auto* b : padPickBankBtns) if (b != nullptr) b->setBounds ({});
        padPickCloseBtn.setBounds ({});
        padPickSheet.sheetBounds = {};
    }

    resized();
    repaint();
}

void MainComponent::refrescaPadPicker()
{
    for (int i = 0; i < padPickBtns.size(); ++i)
        if (auto* b = padPickBtns[i])
        {
            b->setToggleState (i == selectedPad, juce::dontSendNotification);
            //  LOS VACIOS SE VEN Y SE LEEN COMO VACIOS. En un kit de cinco
            //  sonidos, dieciseis numeros iguales no dicen donde estan los
            //  cinco - y esa es justo la informacion por la que PAD -/+ salta
            //  huecos. Aqui no hace falta saltarlos: se enseñan.
            b->setAlpha (padHasSample[(size_t) i] ? 1.0f : 0.45f);
        }

    for (int b = 0; b < padPickBankBtns.size(); ++b)
        if (auto* t = padPickBankBtns[b])
            t->setToggleState (b == currentBank, juce::dontSendNotification);

    padPickSheet.repaint();
}


// The display shows the whole cut, not one pad: every pad pointing at the
// selected pad's buffer contributes its trim window as a coloured fragment.
// Auto-chop leaves exactly that — one shared buffer, sixteen windows.
//  LA CURVA QUE SE DIBUJA TIENE QUE SALIR DE LOS MISMOS NUMEROS QUE SUENAN.
//
//  El fundido lo aplica la voz en muestras de la FUENTE, asi que la conversion
//  a fraccion necesita la frecuencia del fichero y su longitud - y las dos
//  salen del buffer que sostiene la INTERFAZ, nunca de engine.getSampleLength,
//  que lee el puntero que ha adoptado el hilo de audio y es nulo hasta el
//  primer bloque. Se llama despues de cada setTrim, porque el tope de un
//  tercio de ventana depende del recorte y mover un asa lo cambia.
void MainComponent::pushFadesToWaveform()
{
    if (selectedPad < 0) { waveform.setFades (0.0f, 0.0f, 0.0, 0); return; }

    auto sb = uiSample[(size_t) selectedPad];
    waveform.setFades (padFadeIn[(size_t) selectedPad],
                       padFadeOut[(size_t) selectedPad],
                       sb != nullptr ? sb->sourceSampleRate : 0.0,
                       sb != nullptr ? padVisibleLength (selectedPad) : 0);
}

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
    //  UN PAD DE INSTRUMENTO SE RECORTA, Y TRES MANDOS SIGUEN SIN SIGNIFICAR
    //  NADA EN EL.
    //
    //  Aqui se apagaban CINCO con este argumento: "un instrumento no tiene un
    //  trozo, tiene diez zonas". Vale para el BUCLE -lo decide la zona, que es
    //  quien sabe donde acaba su ataque-, para el REVES -leer una zona hacia
    //  atras es leer su cola y el ataque al final- y para CINTA/TONO, que
    //  cambian el largo de una muestra que aqui no es una sino diez.
    //
    //  Y NO vale para INICIO y FIN, que es lo que la persona pidio con estas
    //  palabras: "si yo acorto ese sonido, el bucle tiene que ser de ese
    //  sonido". Se aplican como FRACCION de la zona que toca (ver triggerPad),
    //  asi que significan lo mismo en las cinco octavas. Con ellos vuelven los
    //  dos SUAVE, que son los que quitan el chasquido de la costura que la
    //  persona acaba de crear al recortar.
    const bool instr = (uiSample[(size_t) index] != nullptr
                        && uiSample[(size_t) index]->familia >= 0);
    for (juce::Component* c : { (juce::Component*) &loopButton,
                                (juce::Component*) &reverseButton,
                                (juce::Component*) &modeButton })
        c->setEnabled (! instr);

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
    anchoSlider.setValue   (padAnchoUI[(size_t) index], juce::dontSendNotification);
    //  Y APAGADO EN UNA MUESTRA MONO, que no tiene lado que abrir ni cerrar.
    //  El mando se moveria y no pasaria nada, que es lo que esta casa no deja:
    //  un control que no puede hacer nada no es informacion, es ruido.
    anchoSlider.setEnabled (uiSample[(size_t) index] != nullptr
                            && uiSample[(size_t) index]->buffer.getNumChannels() > 1);
    attackSlider.setValue  (padAttack[(size_t) index],  juce::dontSendNotification);
    releaseSlider.setValue (padRelease[(size_t) index], juce::dontSendNotification);
    cutSlider.setValue  (padCut[(size_t) index],  juce::dontSendNotification);
    resoSlider.setValue (padReso[(size_t) index], juce::dontSendNotification);
    fadeInSlider.setValue  (padFadeIn[(size_t) index],  juce::dontSendNotification);
    fadeOutSlider.setValue (padFadeOut[(size_t) index], juce::dontSendNotification);
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
    status.setText ((capped ? T ("GANANCIA al tope: %1", gainText (db, true))
                            : T ("Pico a -0.3 dBFS con %1", gainText (db, true))),
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

        //  Y SE DEVUELVE EL NIVEL QUE LA LIMPIEZA SE LLEVO.
        //
        //  Una resta espectral solo puede ATENUAR -eso es una regla del motor
        //  y el banco la mide-, asi que despues de limpiar la muestra suena
        //  mas baja SIEMPRE. Sin esto, limpiar una toma costaba bajarla de
        //  nivel y la persona tenia que ir a NORMALIZAR a mano cada vez, o
        //  sea que la funcion dejaba trabajo hecho a medias.
        //
        //  Y ES COMPENSAR LO SUYO, NO NORMALIZAR: se devuelve el pico QUE
        //  TENIA y no se lleva a -0.3 dBFS. NORMALIZAR es el dueno del nivel
        //  absoluto y sigue siendolo -*una funcion, un dueno*-; lo que esto
        //  hace es no cobrar nivel por limpiar. La diferencia se ve en una
        //  toma que ya venia baja: con esto sigue igual de baja, y es
        //  NORMALIZAR quien la sube.
        //
        //  Va AQUI y no dentro de `Denoise::process` a proposito: alli
        //  romperia la regla del banco que dice que esa funcion nunca
        //  amplifica, que es la que caza el fallo del divisor sin suelo en los
        //  bordes. La etapa que no amplifica sigue sin amplificar; quien
        //  devuelve el nivel es quien llama.
        float recupera = 1.0f;
        if (after > 1.0e-6f && before > 1.0e-6f && after < before)
        {
            recupera = before / after;
            clean->buffer.applyGain (recupera);
        }

        juce::MessageManager::callAsync ([this, clean, keepSrc, pad, len, keepStart, keepEnd, keepName, before, after, recupera]
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
            //  Se mide ANTES de devolver el nivel y no despues, o el numero
            //  saldria siempre cero: lo que se quiere decir es cuanto se ha
            //  quitado, no en que nivel ha quedado.
            const double db = juce::Decibels::gainToDecibels ((double) juce::jmax (1.0e-6f, after)
                                                            / (double) juce::jmax (1.0e-6f, before), -60.0);
            const double sube = juce::Decibels::gainToDecibels ((double) recupera, -60.0);
            status.setText (T ("Ruido fuera - baja %1 dB y se recupera %2 dB",
                               Lang::ltr (juce::String (-db, 1)),
                               Lang::ltr (juce::String (sube, 1))),
                            juce::dontSendNotification);
        });
    });
}

//  RECORTAR: la muestra pasa a ser el trozo que las asas marcan.
//
//  Lo que cuesta que esto no exista: un pad de un break de cuatro minutos con
//  el recorte en un golpe ARRASTRA los cuatro minutos a todas partes -a la
//  sesion, al proyecto, al kit que se guarde, al clip que se ponga en la
//  cancion- para tocar medio segundo. GUARDAR KIT ya tenia esa regla escrita
//  -«se escribe lo que suena, no el fichero entero»- y aqui no habia forma de
//  aplicarla al propio pad.
//
//  SOBRE UNA COPIA Y NUNCA EN EL SITIO. Un troceado son N pads apuntando al
//  MISMO SampleBuffer, asi que reescribirlo le cambiaria el sonido a los otros
//  quince. Ademas el hilo de audio puede estar leyendolo AHORA: el buffer nuevo
//  entra por el mismo intercambio de punteros que un corte o un remuestreo, y
//  el viejo lo suelta el temporizador.
//
//  Y SE VACIAN LAS ASAS. Despues de recortar, el trozo ES la muestra, asi que
//  dejarlas donde estaban seria recortar dos veces lo mismo: la segunda
//  pulsacion se quedaria con la cuarta parte. 0..1.
//
//  Lo que NO se toca: el nombre, la ganancia, el pan, el filtro y lo demas del
//  pad. Cambia el material, no los ajustes -que es justo lo contrario de
//  cargar un instrumento, donde lo que cambia es todo-.
void MainComponent::recortaPad()
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

    //  Un pad de instrumento son DIEZ zonas pegadas y el recorte es una
    //  fraccion de la zona, no una posicion en el buffer: recortar aqui
    //  dejaria un buffer que ya no son diez zonas y el instrumento se
    //  convertiria en una muestra afinada. Es la misma razon por la que BUCLE,
    //  REVES y CINTA/TONO estan apagados en esos pads.
    if (src->nZonas > 0)
    {
        status.setText (T ("Un instrumento son diez zonas: no se puede recortar"),
                        juce::dontSendNotification);
        return;
    }

    const int a = juce::jlimit (0, len - 1, (int) std::floor (padStart01[sp] * (float) len));
    const int b = juce::jlimit (a + 1, len, (int) std::ceil  (padEnd01[sp]   * (float) len));
    const int n = b - a;

    //  Si las asas estan donde nacieron no hay nada fuera que tirar, y hacerlo
    //  igual gastaria una entrada de deshacer y una copia entera del buffer
    //  para dejarlo exactamente como estaba.
    if (n >= len)
    {
        status.setText (T ("El recorte es la muestra entera: no hay nada que tirar"),
                        juce::dontSendNotification);
        return;
    }

    pushUndo (T ("RECORTAR"));

    SampleBuffer::Ptr corto = new SampleBuffer();
    corto->sourceSampleRate = src->sourceSampleRate;
    const int ch = src->buffer.getNumChannels();
    corto->buffer.setSize (ch, n);
    for (int c = 0; c < ch; ++c)
        corto->buffer.copyFrom (c, 0, src->buffer, c, a, n);

    const juce::String keepName = padName[sp];
    assignSampleToPad (selectedPad, corto, {});
    padName[sp]    = keepName;
    padStart01[sp] = 0.0f;
    padEnd01[sp]   = 1.0f;
    engine.setPadStart (selectedPad, 0);
    engine.setPadEnd   (selectedPad, n);
    if (auto* pb = pads[selectedPad])
        pb->setSampleInfo (uiSample[sp], padName[sp], padStart01[sp], padEnd01[sp]);
    selectPad (selectedPad);

    //  Se dicen las DOS cifras y no el porcentaje: «se queda el 12%» no dice
    //  si lo que queda son mil muestras o un millon, y el largo es justo lo
    //  que esta funcion existe para cambiar.
    status.setText (T ("Recortado a %1 de %2 muestras",
                       Lang::ltr (juce::String (n)), Lang::ltr (juce::String (len))),
                    juce::dontSendNotification);
}

//  LA ZONA QUE SE ENSEÑA de un pad de instrumento: raiz 0 y capa fuerte, que
//  es la de referencia. Devuelve false para todo lo demas, y entonces lo que se
//  enseña es el buffer entero.
//
//  Los otros nueve trozos no son "mas muestra": son el MISMO sonido rendido en
//  otra octava y con otra fuerza. Dibujarlos seguidos es dibujar diez veces lo
//  mismo, y encima deja las asas de recorte sobre una tira que no se toca.
bool MainComponent::zonaVisible (int pad, int& ini, int& fin) const
{
    if (! juce::isPositiveAndBelow (pad, kNumPads)) return false;
    auto& sb = uiSample[(size_t) pad];
    if (sb == nullptr || sb->nZonas <= 0) return false;

    int mejor = 0;
    for (int z = 0; z < sb->nZonas && z < SampleBuffer::kMaxZonas; ++z)
        if (sb->zonas[(size_t) z].raiz == 0 && sb->zonas[(size_t) z].capa == 1) { mejor = z; break; }

    ini = sb->zonas[(size_t) mejor].ini;
    fin = sb->zonas[(size_t) mejor].fin;
    return fin > ini;
}

//  Y CUANTO DURA LO QUE SE ENSEÑA, que no es lo mismo que cuanto pesa el
//  buffer. El recorte se escribe en el motor como fraccion del BUFFER -es lo
//  que padStart guarda- pero los segundos que la casilla dice, el ancho minimo
//  del recorte y el fundido en milisegundos son de lo que suena. Con la
//  longitud del buffer, un instrumento decia "4.2 s" de una nota de 0.42.
int MainComponent::padVisibleLength (int pad) const
{
    int a = 0, b = 0;
    if (zonaVisible (pad, a, b)) return b - a;
    return padSourceLength (pad);
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
//  UN BANCO DE FABRICA EN EL BANCO QUE SE DIGA, que no siempre es el suyo.
//
//  Esto era `loadFactoryKits (b)` y escribia el banco b EN el banco b: "vuelve
//  a poner la fabrica aqui". Sirve para arrancar la maquina y para la tapa que
//  recargaba el banco de delante, y no sirve para lo que hace falta desde que
//  la fabrica es el primer pack del catalogo: elegir TEXTURA estando en el
//  banco A tiene que dejar TEXTURA EN A. Con el origen mandando sobre el
//  destino, la ficha decia "van al banco A" y los ponia en el C - o sea
//  cambiaba en silencio un banco que no estabas mirando, que es la peor forma
//  de equivocarse que tiene esta app.
//
//  Medido: la fabrica llenaba 0 pads del banco de delante.
//  LOS SONIDOS DE FABRICA SE SINTETIZAN EN PARALELO.
//
//  `Kits::render` es una funcion PURA de su indice -su semilla es
//  `1000 + index * 37`, escrita ahi- asi que los dieciseis de un banco no se
//  hablan entre ellos. Y era el unico trozo del arranque sin trocear: la
//  primera vez que alguien abre la app se rinden los sesenta y cuatro de
//  golpe en el hilo de mensajes, con la portada puesta y sin nada mas que
//  hacer. Medido en esta maquina: la primera apertura son 620 ms contra 253
//  de la segunda, o sea que la fabrica es la diferencia.
//
//  Lo unico compartido es el registro de formatos de `desdeRecurso`, que es un
//  `static` que se monta una vez bajo su guardia y del que solo se LEE.
//  Repartir y esperar, que el reparto lo hace el sistema mejor que un troceo
//  por ticks: aqui no hay nada que dibujar mientras tanto.
static void rindeFabrica (int primero, int cuantos, SampleBuffer::Ptr* salida)
{
    const int hilos = juce::jlimit (1, 4, juce::SystemStats::getNumCpus());
    if (hilos <= 1 || cuantos <= 1)
    {
        for (int i = 0; i < cuantos; ++i) salida[i] = Kits::render (primero + i);
        return;
    }

    std::atomic<int> siguiente { 0 };
    std::vector<std::thread> hebras;
    hebras.reserve ((size_t) hilos);
    for (int h = 0; h < hilos; ++h)
        hebras.emplace_back ([&]
        {
            for (int i = siguiente.fetch_add (1); i < cuantos; i = siguiente.fetch_add (1))
                salida[i] = Kits::render (primero + i);
        });
    for (auto& x : hebras) x.join();
}

void MainComponent::cargaFabricaEnBanco (int origen, int destino)
{
    origen  = juce::jlimit (0, kNumBanks - 1, origen);
    destino = juce::jlimit (0, kNumBanks - 1, destino);

    SampleBuffer::Ptr rendidos[kPadsPerBank];
    rindeFabrica (origen * kPadsPerBank, kPadsPerBank, rendidos);

    for (int i = 0; i < kPadsPerBank; ++i)
    {
        const int src = origen  * kPadsPerBank + i;
        const int dst = destino * kPadsPerBank + i;
        //  El reparto SI es del hilo de mensajes: toca el motor y la cara.
        if (auto sb = rendidos[i])
        {
            //  EL PAD SE VACIA ANTES DE RECIBIR. Ver ponPadPorDefecto: sin esto
            //  la fabrica entraba con la afinacion, el filtro y el choke del
            //  sonido que hubiera, y el instrumento nuevo sonaba como el viejo.
            ponPadPorDefecto (dst);
            assignSampleToPad (dst, sb, Kits::table()[src].name);
            //  Y SE MARCA DE FABRICA, DESPUES: `assignSampleToPad` lo borra
            //  porque es el embudo de todo lo que entra en un pad, asi que
            //  quien sabe que esto es la fabrica es esta funcion. Es lo que
            //  hace que una toma pueda ocupar el hueco sin pisar nada tuyo.
            padDeFabrica[(size_t) dst] = true;
            //  El color del pad lo pone Zati::forPad y no se toca: el orden de
            //  corte manda sobre cualquier idea decorativa.
            padHasSample[(size_t) dst] = true;
        }
    }

    for (int i = 0; i < kPadsPerBank; ++i) refreshPad (destino * kPadsPerBank + i);
    selectPad (juce::jmax (0, selectedPad));
}

void MainComponent::loadFactoryKits (int onlyBank)
{
    //  Cada banco en el suyo, que es lo que significa "la fabrica" al arrancar
    //  y lo que significaba esta funcion antes de que hubiera catalogo.
    if (onlyBank >= 0) { cargaFabricaEnBanco (onlyBank, onlyBank); return; }
    for (int b = 0; b < kNumBanks; ++b) cargaFabricaEnBanco (b, b);
}

//  UN PAD, COMO NACE.
//
//  Existe porque cargar un instrumento nuevo daba el instrumento nuevo sonando
//  con los ajustes del anterior. assignSampleToPad -por donde entran TODOS los
//  caminos que ponen sonido en un pad- reiniciaba el recorte y nada mas: la
//  afinacion, el corte del filtro, la resonancia, la ganancia, el pan, la
//  envolvente, los fundidos, el reves, el bucle, el choke, el autocorte y los
//  seis envios se quedaban donde los dejo el sonido de antes. Cargar un banco
//  encima de uno donde habias cerrado un filtro o puesto un choke te daba
//  dieciseis sonidos nuevos que no se oian, y la causa no se veia en ninguna
//  parte.
//
//  Es la misma herencia que ya se cazo en NUEVO: vaciar la mitad de un
//  proyecto es peor que no vaciar nada, porque lo que queda parece tuyo.
//
//  Escribe el espejo Y empuja al motor, que es lo unico que garantiza que los
//  dos digan lo mismo. El constructor la llama tambien: los defectos vivian
//  ahi sueltos en seis .fill(), y una regla escrita dos veces son dos reglas.
//
//  NO toca la muestra, ni el nombre, ni el color: el color del pad lo pone
//  Zati::forPad por orden de corte y no es un ajuste de sonido.
void MainComponent::ponPadPorDefecto (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumPads)) return;
    const auto k = (size_t) i;

    padPitch[k]    = 0.0f;
    padCents[k]    = 0.0f;
    padKeepLen[k]  = false;
    //  Unidad, no 0.85. Un pad toca la muestra como esta: 0.85 eran -1.4 dB
    //  de rebaja escondida que nadie pidio y que ya no hace falta, porque el
    //  limitador de seguridad del master es quien cuida la suma de 64 pads.
    padGain[k]     = 1.0f;
    padStart01[k]  = 0.0f;
    padEnd01[k]    = 1.0f;
    padLoop[k]     = false;
    //  AUTOCUT on everywhere, matching the engine. A pad that stacks over
    //  its own tail is the special case, not the normal one.
    padSelfCut[k]  = true;
    padReverse[k]  = false;
    padChokeUI[k]  = 0;
    padPan[k]      = 0.0f;
    padAttack[k]   = 2.0f;
    padRelease[k]  = 5.0f;
    //  El espejo del filtro, abierto, igual que el motor. Cero aqui serian
    //  sesenta y cuatro mandos de corte en el tope de abajo: la ficha diria
    //  "20 Hz" en un pad que suena entero.
    padCut[k]      = AudioEngine::kFiltOpenHz;
    padReso[k]     = 0.0f;
    padFadeIn[k]   = 0.0f;
    padFadeOut[k]  = 0.0f;

    engine.setPadPitch      (i, 0.0f);
    engine.setPadKeepLength (i, false);
    engine.setPadGain       (i, 1.0f);
    engine.setPadLoop       (i, false);
    engine.setPadSelfCut    (i, true);
    engine.setPadReverse    (i, false);
    engine.setPadChoke      (i, 0);
    engine.setPadPan        (i, 0.0f);
    engine.setPadAttack     (i, 2.0f);
    engine.setPadRelease    (i, 5.0f);
    engine.setPadCutoff     (i, AudioEngine::kFiltOpenHz);
    engine.setPadReso       (i, 0.0f);
    engine.setPadFadeIn     (i, 0.0f);
    engine.setPadFadeOut    (i, 0.0f);
    //  Y EL REPARTO: el canal 0 y el recorte NEUTRO.
    //
    //  Era `setPadSend (i, f, 0.0f)` y el cero se ha mudado: quien manda ahora
    //  es `canalSend`, que nace a cero, y el recorte del pad es lo que
    //  MULTIPLICA. Dejarlo en cero aqui haria un pad al que ningun canal puede
    //  llegar — un valor por defecto que ademas es valido, que es el fallo del
    //  `brillo` del `Recipe` y el del cero de `padAncho`.
    engine.setPadCanal (i, 0);
    for (int f = 0; f < AudioEngine::kNumFx; ++f) engine.setPadRecorte (i, f, 1.0f);
}

void MainComponent::assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name)
{
    if (sb == nullptr || ! juce::isPositiveAndBelow (index, kNumPads)) return;
    padHasSample[(size_t) index] = true;
    //  Y DEJA DE SER DE FABRICA, sea lo que sea lo que entra. Esta es la unica
    //  puerta por la que un pad recibe audio -LOAD, un kit, un troceado, un
    //  instrumento, el remuestreo y la toma- asi que la marca se borra aqui una
    //  vez y no en cada camino: quien lo pone otra vez es la fabrica.
    padDeFabrica[(size_t) index] = false;
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
    engine.setPadAncho   (index, padAnchoUI[(size_t) index]);
    engine.setPadAttack  (index, padAttack[(size_t) index]);
    engine.setPadRelease (index, padRelease[(size_t) index]);
    //  Y EL FILTRO Y LOS FUNDIDOS, que faltaban y no se veia.
    //
    //  Estos cuatro no se empujaban nunca, asi que el motor conservaba lo que
    //  tuviera - y el corte del pad NO lo escribe solo esta funcion: lo escribe
    //  tambien el bloqueo de corte de un paso, desde el hilo de audio, y se
    //  queda puesto hasta que otro paso diga otra cosa. Un pad que habia sonado
    //  en un patron con bloqueos se cargaba filtrado mientras el mando de la
    //  ficha decia "abierto": el espejo y el motor contando cosas distintas, que
    //  es la unica clase de fallo que no se puede ver mirando la pantalla.
    engine.setPadCutoff  (index, padCut[(size_t) index]);
    engine.setPadReso    (index, padReso[(size_t) index]);
    engine.setPadFadeIn  (index, padFadeIn[(size_t) index]);
    engine.setPadFadeOut (index, padFadeOut[(size_t) index]);

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

    padRackBtn.setTitle (T ("RACK"));
    padRackBtn.setDescription (T ("del pad"));
    padCanalBtn.setDescription (T ("del pad"));

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
        if (auto* f = mixFaders[i]) { f->setTitle (T ("Ganancia pad %1", ch)); f->setDescription (T ("del mezclador")); }
        if (auto* p = mixPans[i])   { p->setTitle (T ("Paneo pad %1",   ch)); p->setDescription (T ("del mezclador")); }
        if (auto* a = mixAnchos[i]) { a->setTitle (T ("Ancho pad %1",   ch)); a->setDescription (T ("del mezclador")); }
        if (auto* m = mixMutes[i])  { m->setTitle (T ("Silencio %1", ch)); }
        if (auto* s = mixSolos[i])  { s->setTitle (T ("Solo %1",     ch)); }
    }
    //  Y LAS DIECISEIS TIRAS DE CANAL, que sin esto se anuncian «deslizador»
    //  dieciseis veces seguidas.
    for (int c = 0; c < kNumCanales; ++c)
    {
        const auto ch = juce::String (c + 1);
        if (auto* f = canFaders[c]) { f->setTitle (T ("Ganancia canal %1", ch)); f->setDescription (T ("del mezclador")); }
        if (auto* m = canMutes[c])  { m->setTitle (T ("Silencio canal %1", ch)); }
    }
}

void MainComponent::retranslateUi()
{
    refreshAccessibleNames();

    //  SINGULAR, que es lo que abre. Esta pestana no lleva a los pads -esos
    //  estan siempre en la cara, debajo- sino a los AJUSTES DEL PAD ELEGIDO, y
    //  la ficha que abre ya se titula "PAD 07". "PADS" prometia una rejilla.
    //
    //  Y no "AJUSTES DEL PAD", que seria lo explicito: son seis pestanas en una
    //  fila y ahi no caben con su aire ni las cortas -medido: 69 apretones-, asi
    //  que un rotulo de tres palabras se lo quita a las otras cinco. Singular
    //  cuesta una letra MENOS que el plural.
    padsButton  .setButtonText (T ("PAD"));
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
    soloButton  .setButtonText (T ("SOLO"));
    transporte (playButton, engine.isPlaying());
    clearButton .setButtonText (T ("VACIAR"));
    seqGridBtn  .setButtonText (T ("PASOS"));
    seqPianoBtn .setButtonText (T ("PIANO"));
    seqHumanBtn .setButtonText (T ("HUMANIZAR"));
    copyRowBtn  .setButtonText (T ("COPIAR FILA"));
    pasteRowBtn .setButtonText (T ("PEGAR FILA"));
    seqFollowBtn.setButtonText (T ("SEGUIR"));
    patLeftBtn  .setButtonText (T ("ATRAS"));
    patRightBtn .setButtonText (T ("ADELANTE"));
    patDoubleBtn.setButtonText (T ("DOBLAR"));
    tapButton   .setButtonText (T ("TAP"));
    copyPatBtn  .setButtonText (T ("COPIAR"));
    pastePatBtn .setButtonText (T ("PEGAR"));
    seqStepBtn  .setButtonText (T ("PATRON"));
    //  The three tabs of the settings card. Their rows have been in Lang.cpp
    //  all along - PROJECTS / 工程 / المشاريع, GESTURES / 手势 / إيماءات - and
    //  nothing ever asked for them: the buttons were constructed with the
    //  Spanish literal and never retranslated, so the card that CONTAINS the
    //  language selector was the one card still in Spanish after you used it.
    //  AUDIO hid the bug for both its neighbours by being the same word.
    pageAudioBtn.setButtonText (T ("AUDIO"));
    //  Y LA QUINTA, que se quedo fuera al anadirla y por tanto se quedaba con
    //  el literal del constructor para siempre. Es EXACTAMENTE el fallo que
    //  cuenta el parrafo de aqui arriba, una pestana mas tarde: la ficha que
    //  CONTIENE el selector de idioma con una pestana en espanol en las cuatro
    //  compilaciones. La regla comparativa lo canto -35 hallazgos-, que es para
    //  lo que existe.
    pageAspBtn  .setButtonText (T ("ASPECTO"));
    //  Y LOS DOS CHIPS DEL MOVIMIENTO, por lo mismo que la linea de arriba:
    //  se construyen con el literal en espanol y sin esto se quedarian con el
    //  para siempre. El banco lo canta a la primera —«SI identical in es and
    //  en»— y es el mismo fallo que ya costo tres pestanas de esta ficha, la
    //  fila VACIAR de las ranuras y los chips de la cuenta atras.
    for (int i = 0; i < movButtons.size(); ++i)
        if (auto* b = movButtons[i]) b->setButtonText (T (i == 0 ? "SI|chip" : "NO|chip"));
    //  Y los dos del monitor. Sin esta linea la fila se construye con el
    //  literal del idioma de arranque y no se retraduce jamas, que es el fallo
    //  de las tres pestañas de AJUSTES y el que el banco ya cazo en la fila
    //  VACIAR de las ranuras.
    for (int i = 0; i < monButtons.size(); ++i)
        if (auto* b = monButtons[i]) b->setButtonText (T (i == 1 ? "SI|chip" : "NO|chip"));
    //  Y EL NOMBRE ACCESIBLE de los cuatro chips del banco de tomas: su ROTULO
    //  es una letra y no cambia de idioma, pero lo que TalkBack lee si. Puesto
    //  aqui y no en el constructor, que ahi se quedaria clavado en el idioma
    //  del arranque - el fallo de las tres pestañas de AJUSTES.
    for (int i = 0; i < tomasButtons.size(); ++i)
        if (auto* b = tomasButtons[i])
            b->setTitle (T ("Banco de tomas %1",
                            juce::String::charToString ((juce::juce_wchar) ('A' + i))));
    pageProjBtn .setButtonText (T ("PROYECTOS"));
    pageGestBtn .setButtonText (T ("GESTOS"));
    pageMidiBtn .setButtonText (T ("MIDI"));
    manualButton.setButtonText (T ("MANUAL"));
    tourButton  .setButtonText (T ("TOUR"));
    tourBackBtn .setButtonText (T ("TOUR ATRAS"));
    tourSkipBtn .setButtonText (tourSkipCaption());
    //  Y LA DE AVANZAR SOLO EL ROTULO, que es lo unico que hace falta aqui.
    //
    //  Esto llamaba a showTour, y showTour no pone un rotulo: ENSEÑA un paso.
    //  Pasa por tourPrepara, que abre la ficha que el paso explica -o cierra
    //  todas, en el caso por defecto- y termina con dos lineas sin condicion:
    //  `tourSheet.setVisible (true)` y `toFront`. Correctas cuando el tour esta
    //  puesto -cada paso llama a openSheet, que lo apagaria- y aqui son otra
    //  cosa, porque retranslateUi corre en dos sitios que no son el tour:
    //
    //    - EL CONSTRUCTOR. Asi que la bienvenida quedaba visible al terminar de
    //      construir, en CADA arranque, mirase o no la marca de visto. El
    //      bloque que si la mira corre despues, en el primer tick, y solo puede
    //      AÑADIR: cuando no es la primera vez no hace nada y la tarjeta ya
    //      estaba puesta. De ahi que borrar zati-tour.txt no cambiara nada y
    //      que la marca estuviera bien escrita todo el tiempo.
    //    - Y TOCAR UN IDIOMA (ver langButtons), o sea que cambiar de lengua te
    //      cerraba AJUSTES -la unica ficha desde la que se cambia- y te
    //      levantaba la bienvenida encima.
    //
    //  Ninguna de las seis reglas de expo.py puede verlo: una tarjeta a
    //  pantalla completa no solapa a nadie -sus tres tapas van en fila-, no se
    //  sale, no corta rotulos, esta traducida y no mide cero. Y de las 33
    //  fichas que mide el banco, 32 la tapan sin querer, porque cualquier
    //  ZATI_OPEN que abra una ficha empieza por closeAllSheets.
    if (tourSheet.isVisible()) showTour (tourPaso);
    else                       tourNextBtn.setButtonText (tourNextCaption());
    undoButton  .setButtonText (T ("DESHACER"));
    redoButton  .setButtonText (T ("REHACER"));
    //  Y su nombre accesible, que lleva ADEMAS que se va a deshacer y por eso
    //  no lo puede poner el barrido de abajo: ese solo rellena los vacios.
    refrescaNombresDeshacer();

    reverseButton.setButtonText (T ("REV|reverso"));
    loopButton   .setButtonText (T ("LOOP"));
    autocutButton.setButtonText (T ("AUTOCUT"));
    duckButton   .setButtonText (T ("BOMBEO"));
    normButton   .setButtonText (T ("NORMALIZAR"));
    padSoundBtn  .setButtonText (T ("SONIDO"));
    padTrimBtn   .setButtonText (T ("RECORTE"));
    padRigBtn    .setButtonText (T ("EL PAD"));
    padRackBtn   .setButtonText (T ("RACK"));
    //  Y EL CANAL, que lleva su numero dentro: `refrescaCanalDelPad` lo escribe
    //  entero -rotulo y cifra- asi que aqui basta con volver a llamarla. Sin
    //  esto la tapa se construye con el literal en espanol y no se retraduce
    //  jamas, que es el fallo de las tres pestañas de AJUSTES y el que el banco
    //  ya canto tres veces en dos tandas.
    refrescaCanalDelPad();
    mixVistaBtn.setButtonText (T (mixPage == mixPageCanales ? "CANALES" : "PADS"));
    nivelesButton.setButtonText (T ("16 NIVELES"));
    pianoButton  .setButtonText (T ("PIANO"));
    //  SOLO EL SIGNO, como las flechas del preset en la ficha del instrumento
    //  y por lo mismo: "OCTAVA -" en arabe pide 69 px de letra y en 344x882 la
    //  fila le da 53. Quien las nombra es el renglon de ayuda que va justo
    //  encima de la rejilla, que ya dice OCTAVA y en que notas estas.
    pianoOctDownBtn.setButtonText ("-");
    pianoOctUpBtn  .setButtonText ("+");
    pianoPadDownBtn.setButtonText (T ("PAD") + " -");
    pianoPadUpBtn  .setButtonText (T ("PAD") + " +");
    pianoLapizBtn  .setButtonText (T ("LAPIZ"));
    pianoGomaBtn   .setButtonText (T ("GOMA"));
    pianoCorteBtn  .setButtonText (T ("TIJERAS"));
    pianoSelBtn    .setButtonText (T ("SEL"));
    pianoCopiaBtn  .setButtonText (T ("COPIAR"));
    pianoPegaBtn   .setButtonText (T ("PEGAR"));
    //  El rotulo dice CUANTO se ve, no un verbo: es la misma gramatica que
    //  pianoVerBtn y que la tapa de vista de la cancion.
    pianoZoomBtn   .setButtonText (pianoCols == 8  ? T ("1/2 COMPAS")
                                 : pianoCols == 32 ? T ("2 COMPASES") : T ("1 COMPAS"));
    pianoClearBtn  .setButtonText (T ("VACIAR"));
    //  El rotulo de esta dice el ESTADO, asi que no es una clave fija: la elige
    //  cuantas filas hay puestas. Sin esta linea, cambiar de idioma dejaba
    //  "1 OCTAVA" en espanol dentro de una compilacion en ingles - que es
    //  exactamente el fallo que la prueba comparativa de idiomas existe para
    //  cazar, y lo cazaria.
    pianoVerBtn    .setButtonText (pianoGrid.getFilas() >= PianoRoll::kFilasMax
                                     ? T ("2 OCTAVAS") : T ("1 OCTAVA"));
    denoiseButton.setButtonText (T ("QUITAR RUIDO"));
    recorteButton.setButtonText (T ("RECORTAR"));
    chopButton   .setButtonText (T ("AUTO CHOP"));
    micButton    .setButtonText (recordingActive ? T ("PARAR") : T ("GRABAR MIC"));
    resampleButton.setButtonText (resamplingActive ? T ("PARAR") : T ("REMUESTREAR"));
    oirTapa (previewButton, previewSounding);
    modeButton   .setButtonText (selectedPad >= 0 && padKeepLen[(size_t) selectedPad]
                                   ? T ("TONO") : T ("CINTA"));

    chainClearButton.setButtonText (T ("QUITAR CADENA"));

    projSaveButton  .setButtonText (T ("GUARDAR"));
    projKitButton   .setButtonText (T ("GUARDAR KIT"));
    projLoadButton  .setButtonText (T ("ABRIR"));
    projNewButton   .setButtonText (T ("NUEVO"));
    projDeleteButton.setButtonText (T ("BORRAR"));
    projExportButton.setButtonText (T ("EXPORTAR"));

    browseLoadButton  .setButtonText (T ("CARGAR"));
    browseUseDirBtn   .setButtonText (T ("USAR ESTA CARPETA"));
    exportDirBtn      .setButtonText (T ("CAMBIAR"));
    browseKitButton   .setButtonText (T ("CARGAR KIT"));
    browseFactoryButton.setButtonText (T ("INSTRUMENTOS"));
    vstButton.setButtonText (T ("PRESETS"));
    //  SOLO EL SIGNO, sin la palabra. El nombre del preset se pinta ENTRE las
    //  dos flechas, asi que repetirlo en cada tapa es decirlo tres veces - y
    //  ademas no cabe: "PRESETS -" pide 53 px y en 280x653 tiene 42, y en arabe
    //  54. Un interruptor de menos y mas no necesita rotulo cuando lo que
    //  cambia esta escrito en medio.
    vstPreDown.setButtonText ("-");
    vstPreUp  .setButtonText ("+");
    vstOctDown.setButtonText (T ("OCT") + " -");
    vstOctUp  .setButtonText (T ("OCT") + " +");
    instPackDownBtn.setButtonText (T ("PACK") + " -");
    instPackUpBtn  .setButtonText (T ("PACK") + " +");
    //  Y los nombres de la rejilla, que salen del disco y no de la tabla: si el
    //  catalogo ya estaba leido hay que volver a escribirlos, porque
    //  retranslateUi corre tambien al cambiar de idioma con la ficha abierta.
    if (! instCatalogo.empty()) refreshInst();
    browseSystemButton.setButtonText (T ("SISTEMA"));
    browseKitsDirButton.setButtonText (T ("MIS KITS"));

    exportMasterButton.setButtonText (T ("MASTER"));
    exportStemsButton .setButtonText (T ("PISTAS"));
    //  Y la del rebote en vivo, que dice PARAR mientras rueda: retraducirla a
    //  «EN VIVO» a mitad de una toma dejaria la tapa mintiendo. Es la misma
    //  guarda que ya lleva la del microfono.
    exportLiveButton  .setButtonText (T (vivoJob != nullptr ? "PARAR" : "EN VIVO"));
    exportCancelButton.setButtonText (T ("CANCELAR"));

    rackButton   .setButtonText (T ("RACK"));
    mixClearSolo .setButtonText (T ("SIN SOLO"));
    //  Y LA DEL MENU DE UNA RANURA. Se construyo con el literal en español y
    //  no se retraducia jamas: es exactamente el fallo de las tres pestañas de
    //  AJUSTES -la ficha que CONTIENE el selector de idioma- y lo canto el
    //  banco en la primera corrida con la ficha nueva puesta, «VACIAR
    //  identical in es and en», siete veces.
    ranuraVaciarBtn.setButtonText (T ("VACIAR"));
    //  Las tres del modo dicen el ESTADO, no un verbo: ver modoTapa.
    for (juce::TextButton* b2 : { &songModeBtn, &modoBtn, &seqModoBtn })
        modoTapa (*b2, engine.isSongMode());

    chopSafeButton.setButtonText (T ("RESPETAR PADS CON SONIDO"));
    //  Y las dos del modo, que se construyen con el literal y no se
    //  retraducirian jamas sin esta linea. Es el mismo fallo que tuvieron las
    //  tres pestanas de AJUSTES - la ficha que CONTIENE el selector de idioma -
    //  y lo caza la prueba comparativa, no la tabla.
    songDoubleBtn.setButtonText (T ("DOBLAR"));
    songZoomBtn.setButtonText (T ("%1 COMPASES|zoom", juce::String (songGrid.getCompasesVista())));
    //  LAS CUATRO HERRAMIENTAS NO LLEVAN ROTULO, asi que su nombre para quien
    //  no ve la pantalla hay que ponerlo a mano: sin esto TalkBack las anuncia
    //  por su clase, o sea «boton» cuatro veces. Y aqui y no en el
    //  constructor, que es donde los textos ya estan en el idioma que toca.
    {
        for (int i = 0; i < songToolBtns.size() && i < kNumHerramientas; ++i)
            songToolBtns[i]->setTitle (T (kHerramientas[i].nombre));
    }
    songRecBtn.setButtonText (T (grabandoAlArreglo ? "PARAR" : "GRABAR"));
    autoBtn.setButtonText (T ("AUTO"));
    songClickBtn.setButtonText (T ("CLIC"));
    //  LAS NUEVE DE ARREGLO SE QUEDAN SIN ROTULO Y CON SU DIBUJO.
    //
    //  Eran DOS Y HASTA TRES FILAS de palabras -medido antes de tocarlas: una
    //  en tableta, dos en un movil grande y tres en 360x640, 280x653 y
    //  apaisado- en la ficha cuyo unico trabajo son cuatro carriles. Y los
    //  nueve dibujos ya estaban asignados desde la tanda de los iconos: lo
    //  unico que hacian las palabras era pedir ancho.
    //
    //  El nombre no se pierde, cambia de sitio: va a `setTitle`, que es lo que
    //  lee TalkBack. Una tapa sin rotulo se anunciaria por su clase - «boton»
    //  nueve veces - y eso es exactamente lo que la tanda de la feria subio del
    //  18 % al 79 %.
    {
        static const char* kArr[] = { "ACORTAR", "ALARGAR", "ATRAS", "ADELANTE",
                                      "INSERTAR", "QUITAR", "COPIAR", "PEGAR", "LOOP" };
        juce::TextButton* arr[] = { &songShortBtn, &songLongBtn, &songLeftBtn, &songRightBtn,
                                    &songInsertBtn, &songRemoveBtn, &songCopyBtn,
                                    &songPasteBtn, &songLoopBtn };
        for (int i = 0; i < 9; ++i)
        {
            arr[i]->setButtonText ({});
            arr[i]->setTitle (T (kArr[i]));
        }
    }
    chopEvenBtn.setButtonText (T ("IGUALES"));
    chopHitsBtn.setButtonText (T ("GOLPES"));

    //  Y las lecturas que TRADUCEN una palabra en vez de dar un numero. Su
    //  textFromValueFunction se llama una vez al construir el mando, asi que
    //  "SECO" se quedaba escrito en los cuatro idiomas - el mismo fallo que el
    //  de los botones de arriba, pero en un sitio donde no se ve venir porque
    //  el texto no lo pone nadie: lo pone el propio Slider.
    fadeInSlider.updateText();
    fadeOutSlider.updateText();

    //  A slider that formats its own readout has to be told to run the
    //  formatter again; the text it is showing was made in the old language.
    //  ...ALL of them, not the two that were noticed. swingSlider prints
    //  "recto" and songLenSlider prints the bar count, and both were left off
    //  this list, so even once their formatters went through T() they would
    //  have kept showing the language the app was started in until the value
    //  next changed.
    //  Y SIGUEN FALTANDO SIETE, contadas una por una: los cinco que dicen
    //  "off" -el bloqueo de corte y los cuatro de ataque, caida, inicio y pan-,
    //  el de EUCLIDES que dice "vacio", y la rejilla, que pasa su numero por
    //  Lang::ltr y por tanto cambia con el idioma. Es la misma lista que ya se
    //  amplio una vez con el comentario de arriba: se cuentan TODOS los que
    //  formatean su lectura, no los que alguien recuerda.
    for (auto* sl : { &lengthSlider, &chokeSlider, &swingSlider, &songLenSlider,
                      &lockSlider, &atkPasoSlider, &relPasoSlider, &iniPasoSlider,
                      &panPasoSlider, &euclidSlider, &gridSlider })
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

    //  Y EL NOMBRE DE TODO LO DEMAS, PARA QUIEN NO VE LA PANTALLA.
    //
    //  `refreshAccessibleNames` nombra a mano lo que no tiene rotulo -los 64
    //  pads, los dieciseis canales de la mesa, los mandos-, y eso deja fuera a
    //  todo lo que SI lo tiene: medido con `Tests/carga.py`, **117 de 647
    //  controles** llevaban nombre, con VEINTIDOS pantallas de treinta y nueve
    //  al CERO POR CIENTO. Un control sin `getTitle` se anuncia por su clase, o
    //  sea que la app entera suena a «boton».
    //
    //  Y el rotulo YA ES el nombre: si la tapa dice VACIAR, «VACIAR» es
    //  exactamente lo que hay que leer en voz alta. Lo unico que faltaba era
    //  copiarlo, y aqui -al final de `retranslateUi`- porque aqui los rotulos
    //  ya estan puestos EN EL IDIOMA QUE TOCA: hacerlo en el constructor los
    //  dejaria clavados en el idioma del arranque, que es el mismo fallo que
    //  las tres pestañas de AJUSTES.
    //
    //  Sin pisar lo que ya tiene nombre a mano: ahi el rotulo dice menos que la
    //  frase escrita -«01» contra «Pad 1»- y quien la escribio sabia por que.
    //  Y LOS DESLIZADORES, QUE NO LLEVAN ROTULO Y POR ESO SE QUEDABAN FUERA.
    //
    //  El barrido de abajo copia el rotulo de una TAPA a su nombre, y con eso
    //  la app paso del 18 % al 79 %. Lo que quedaba —lo dice la propia medida—
    //  son los mandos: un `juce::Slider` no tiene rotulo que copiar, su nombre
    //  lo PINTA la ficha en la banda de encima, y sin `setTitle` se anuncia
    //  como «deslizador» y ya.
    //
    //  La clave es la MISMA que se dibuja, y para el plato del pad la lee de
    //  aqui el propio pintor (ver `claveDeMando`): dos sitios escribiendo el
    //  nombre del mismo mando es uno de los dos quedandose viejo, que es el
    //  fallo que este fichero lleva encontrando desde el principio.
    for (const auto& m : tablaDeMandos())
        if (m.s != nullptr) m.s->setTitle (T (m.clave));

    std::function<void (juce::Component&)> nombra = [&] (juce::Component& c)
    {
        for (auto* h : c.getChildren())
        {
            if (auto* b = dynamic_cast<juce::Button*> (h))
                if (b->getTitle().isEmpty() && b->getButtonText().isNotEmpty())
                    b->setTitle (b->getButtonText());
            nombra (*h);
        }
    };
    nombra (*this);

    resized();
    repaint();
}


void MainComponent::pintaPortada (juce::Graphics& g)
{
    ++portadaPintadas;
    pintaFondo (g);

    //  La marca centrada, con el mismo lado que el icono del splash usa dentro
    //  de su zona segura: no es continuidad al pixel -Android decide el suyo en
    //  dp- pero si la misma proporcion y el mismo dibujo.
    const auto caja = getLocalBounds().toFloat();
    const float lado = juce::jmin (caja.getWidth(), caja.getHeight()) * 0.28f;

    auto marca = Iconos::marca();
    const auto lim = marca.getBounds();
    if (lim.getWidth() > 0.0f && lim.getHeight() > 0.0f)
    {
        const float k = juce::jmin (lado / lim.getWidth(), lado / lim.getHeight());
        marca.applyTransform (juce::AffineTransform::scale (k)
                                  .translated (caja.getCentreX() - lim.getCentreX() * k,
                                               caja.getCentreY() - lim.getCentreY() * k));
        //  Medida contra el chasis y no elegida: aqui SI hay carcasa que mover
        //  -son las cuatro del aparato-, al reves que en el icono del lanzador,
        //  que es un PNG y por eso lleva blanco escrito.
        g.setColour (ZatiColours::textOn (ZatiColours::chassisTop));
        g.fillPath (marca);
    }
}

//  EL PRIMER INSTANTE EN EL QUE ANDROID PUEDE CONTESTAR. Preguntar solo por
//  temporizador deja la portada puesta un latido de mas; en cuanto la ventana
//  engancha, getRootWindowInsets ya devuelve algo.
void MainComponent::parentHierarchyChanged()
{
    refreshSystemInsets();
}

//  LOS MARGENES DE AHORA, con la entrada del banco delante.
//
//  En el escritorio SystemInsets::get() devuelve {} siempre, o sea que el salto
//  que esto arregla NO EXISTE aqui y no habria forma de medirlo. ZATI_INSETS
//  con su tick simula la respuesta tardia de Android, que es lo mismo que hace
//  ZATI_SKIN con la carcasa: convertir en ENTRADA lo que si no seria nada.
juce::BorderSize<int> MainComponent::margenesDeAhora() const
{
    if (bancoInsets.isNotEmpty())
    {
        auto n = juce::StringArray::fromTokens (bancoInsets, ",", {});
        if (n.size() == 4)
            return { n[0].getIntValue(), n[1].getIntValue(),
                     n[2].getIntValue(), n[3].getIntValue() };
    }
    return SystemInsets::get();
}

//  SI YA CONTESTAN, que es una pregunta distinta de cuanto valen — y es la que
//  se equivoco primero. En Android «contesta» es «la ventana esta enganchada»:
//  getRootWindowInsets devuelve nulo antes y el valor de verdad despues, y ese
//  valor puede ser CERO y ser correcto (cualquier aparato anterior a Android 15
//  coloca la ventana debajo de las barras). Asi que la condicion no puede ser
//  «los margenes no son cero».
//
//  Y por eso el banco tiene que simular el SILENCIO y no un cero: con
//  ZATI_INSETS_TICK marcando solo el valor, la primera pregunta ya daba la cara
//  por lista y la portada se levantaba en el tick 1.
bool MainComponent::margenesContestan() const
{
    if (bancoInsets.isNotEmpty()) return arranqueTicks >= bancoInsetsTick;
    return isShowing() || getPeer() != nullptr;
}

//  LAS DOS CONDICIONES, y hacen falta las dos. La geometria es definitiva
//  cuando se ha preguntado por los margenes con la ventana ya enganchada -y no
//  cuando valen algo distinto de cero, que en un aparato anterior a Android 15
//  valen cero para siempre y es correcto- y la sesion ha vuelto.
//
//  Con el TOPE por encima de las dos: un aparato que no conteste nunca no puede
//  dejar la portada puesta. Es la hermana de «ningun camino puede dejar la app
//  en silencio».
void MainComponent::miraSiLaCaraEstaLista()
{
    if (caraLista) return;

    if ((insetsPreguntados && ! sessionRestorePending) || portadaMs >= kPortadaTopeMs)
    {
        caraLista = true;
        repaint();
    }
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
    if (! margenesContestan()) return;

    const auto now = margenesDeAhora();
    //  Que han CONTESTADO, que es la condicion de verdad: «los margenes no son
    //  cero» seria falso para siempre en un aparato anterior a Android 15,
    //  donde el sistema ya coloca la ventana por debajo de las barras.
    insetsPreguntados = true;

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
    confirmMs      = kConfirmMs;
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
    confirmMs      = 0.0;
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
    refrescaNombresDeshacer();
    resized();
}

//  Undo and redo are the same move in opposite directions: each keeps what it
//  is about to replace, so you can step back and forth over one action instead
//  of the one-way trip DESHACER was on its own.
//  QUE SE VA A DESHACER, DICHO ANTES Y NO DESPUES.
//
//  `pushUndo` recibe una etiqueta desde el dia que existe -«PEGAR», «VACIAR
//  PAD», «CARGAR KIT»- y no la ensenaba nadie: dieciseis pasos de pila y una
//  tapa que dice DESHACER a secas, o sea una apuesta en vez de una decision.
//
//  Va al NOMBRE ACCESIBLE, que es lo que lee TalkBack y lo que la tanda de la
//  feria subio del 18 % al 79 %, y no al rotulo: esa tapa mide 96 px clavados
//  en la banda de estado y «DESHACER PEGAR» no cabe — cambiar un rotulo
//  entero por uno cortado no es un arreglo, que es lo que ya costo deshacer
//  dos veces el reparto de `layoutModuleBar`. Para quien ve la pantalla, la
//  frase que la accion deja en el renglon de estado —justo a la izquierda de
//  la tapa— ya dice lo mismo, y ese renglon existe precisamente por eso.
void MainComponent::refrescaNombresDeshacer()
{
    undoButton.setTitle (undoStack.empty()
                           ? T ("DESHACER")
                           : T ("DESHACER %1", undoStack.back().label));
    redoButton.setTitle (redoStack.empty()
                           ? T ("REHACER")
                           : T ("REHACER %1", redoStack.back().label));
}

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
    refrescaNombresDeshacer();
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
    refrescaNombresDeshacer();
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

    //  La lista se propone al ABRIR: la ficha tiene que enseñar los cortes
    //  desde el primer momento, no despues de tocar un boton.
    recalculaCortes();

    openSheet (chopSheet, padsButton);
    refreshChopSheet();
}

//  LOS PUNTOS DE CORTE, EN UN SOLO SITIO.
//
//  Esto vivia dentro de applyAutoChop y se calculaba al pulsar CORTAR, que es
//  lo que hacia imposible enseñarlos antes: cualquier marca que la persona
//  moviera se habria perdido. Ahora se calculan aqui, la ficha los dibuja y se
//  pueden tocar; applyAutoChop no calcula nada, aplica esta lista.
void MainComponent::recalculaCortes()
{
    chopCortes.clear();
    if (selectedPad < 0) return;
    auto src = uiSample[(size_t) selectedPad];
    if (src == nullptr) return;
    const int len = src->buffer.getNumSamples();
    if (len < 2) return;

    const int cabenPads = chopTargets (chopSlices, chopOnlyEmpty).size();
    if (cabenPads < 2) return;

    if (chopByHits)
    {
        if (chopHitsFor != selectedPad) refreshChopHits();
        //  En GOLPES manda lo que hay en el sonido y el numero es un TECHO: un
        //  break de nueve golpes no se parte en dieciseis por pulsar dieciseis.
        const int n = juce::jmin (cabenPads, (int) chopHits.size());
        for (int k = 0; k < n; ++k) chopCortes.push_back (chopHits[(size_t) k]);
    }
    else
    {
        for (int k = 0; k < cabenPads; ++k)
            chopCortes.push_back ((int) ((juce::int64) k * len / cabenPads));
    }

    //  El primero SIEMPRE es el principio de la muestra. El detector puede
    //  colocar su primer ataque unos milisegundos dentro -un golpe empieza
    //  antes de su pico- y sin esto la cabeza del sonido se quedaria fuera de
    //  todos los pads sin que nada lo dijera.
    if (! chopCortes.empty()) chopCortes[0] = 0;
}

void MainComponent::refreshChopSheet()
{
    chopVista.setFuente (selectedPad >= 0 ? uiSample[(size_t) selectedPad] : nullptr);
    chopVista.setCortes (chopCortes);

    const int fits = chopTargets (chopSlices, chopOnlyEmpty).size();
    //  En GOLPES manda lo que hay en el sonido, no lo que pide el boton: el
    //  numero es un TECHO. Un break con nueve golpes no se corta en dieciseis
    //  por mucho que se pulse dieciseis - saldrian siete trozos partidos por la
    //  mitad de un golpe, que es exactamente lo que este modo viene a evitar.
    //  El numero sale de la LISTA y no de una cuenta paralela: la persona
    //  puede haber anadido o quitado marcas, y un boton que dice "CORTAR EN 8"
    //  mientras hay nueve marcas dibujadas es la ficha contradiciendose.
    const int n = (int) chopCortes.size();
    juce::ignoreUnused (fits);
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

    //  APLICA LA LISTA Y NO LA VUELVE A CALCULAR.
    //
    //  Antes esta funcion decidia aqui los puntos -aritmetica o detector- en el
    //  momento de pulsar CORTAR, y por eso la ficha no podia enseñarlos antes:
    //  cualquier marca movida se habria perdido. Ahora la lista la llena
    //  recalculaCortes, la vista la dibuja y la persona la edita; aqui solo se
    //  aplica. Lo que ves es lo que sale.
    if (chopCortes.empty()) recalculaCortes();
    const int n = juce::jmin (targets.size(), (int) chopCortes.size());

    //  Dos trozos es lo menos que sigue siendo un troceado, y una fuente con
    //  menos de una muestra por trozo no tiene nada que dividir.
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
        //  El ultimo trozo llega SIEMPRE hasta el final: lo que sigue al
        //  ultimo corte es la cola, y terminarla en un corte que no existe la
        //  dejaria fuera de todos los pads.
        const int st = juce::jlimit (0, len - 1, chopCortes[(size_t) k]);
        const int en = (k + 1 < n) ? juce::jlimit (0, len, chopCortes[(size_t) (k + 1)]) : len;
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

    //  Ya no dice "golpes" ni "trozos" segun el modo: desde que las marcas se
    //  pueden mover, una y otra cosa se mezclan en la misma lista y decir de
    //  cual venia seria mentir la mitad de las veces.
    status.setText (n < askedFor
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

//  Ver la cabecera: una toma cae dentro del banco de tomas y solo ahi.
int MainComponent::padParaToma() const
{
    const int base = juce::jlimit (0, kNumBanks - 1, bancoTomas) * kPadsPerBank;

    //  Primero los huecos de verdad, que es lo que la persona espera al vaciar
    //  un pad para hacer sitio.
    for (int i = 0; i < kPadsPerBank; ++i)
        if (! padHasSample[(size_t) (base + i)]) return base + i;

    //  Y despues los de fabrica, que no son «lo que la persona haya puesto»:
    //  vuelven enteros desde INSTRUMENTOS - FABRICA en cualquier momento.
    for (int i = 0; i < kPadsPerBank; ++i)
        if (padDeFabrica[(size_t) (base + i)]) return base + i;

    return -1;
}

// Ask once for audio-read access, then run `then` either way — a refusal must
// still open the browser (internal/app storage is always readable).
void MainComponent::ensureStoragePermission (std::function<void()> then)
{
    using RP = juce::RuntimePermissions;

    //  Y EL DE ESCRITURA DETRAS, donde todavia sirve para algo.
    //
    //  Hay que decirlo claro porque es facil enganarse: desde Android 10 el
    //  sistema IGNORA este permiso para el almacenamiento compartido, y desde
    //  el 11 no se puede recuperar. Sale CONCEDIDO y la escritura sigue
    //  fallando, que es la peor forma de fallar. En el manifiesto va acotado a
    //  SDK 28 y aqui isRequired ya devuelve false de Android 10 en adelante,
    //  asi que esto solo pregunta en los telefonos donde la respuesta cambia
    //  algo. Para los demas la puerta es MediaStore, que no pide nada.
    auto luegoElDeEscribir = [this, then]
    {
        if (RP::isRequired (RP::writeExternalStorage) && ! RP::isGranted (RP::writeExternalStorage))
        {
            RP::request (RP::writeExternalStorage, [then] (bool) { if (then) then(); });
            return;
        }
        if (then) then();
    };

    if (! RP::isRequired (RP::readMediaAudio) || RP::isGranted (RP::readMediaAudio))
    {
        luegoElDeEscribir();
        return;
    }

    RP::request (RP::readMediaAudio, [this, luegoElDeEscribir] (bool granted)
    {
        if (! granted)
            status.setText (T ("Sin permiso de audio: no puedo leer tus carpetas de muestras"),
                            juce::dontSendNotification);
        luegoElDeEscribir();
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

//  EL MISMO NAVEGADOR, ELIGIENDO CARPETA. Ver ModoBrowse: cambia lo que se
//  acepta al final, no la lista ni el gesto.
void MainComponent::openBrowseForExportDir()
{
    browseModo = browseCarpeta;
    browseTargetPad = -1;
    auditionedFile = juce::File();
    closeAllSheets();
    browseSheet.setVisible (true);
    browseSheet.toFront (false);
    //  Empieza donde ya cae hoy, que es de donde se sale para cambiarlo.
    if (browser != nullptr)
        browser->setRoot (ProjectStore::exports());
    resized();
    repaint();

    ensureStoragePermission ([this]
    {
        if (browser != nullptr) browser->refresh();
    });
}

void MainComponent::usarCarpetaDeExport()
{
    if (browser == nullptr) return;

    //  La carpeta es la SENALADA si hay una, y si no aquella en la que se esta
    //  mirando: entrar en una carpeta y pulsar "usar esta" sin haberla tocado
    //  en la lista es exactamente lo que uno espera que valga.
    juce::File elegida = browser->getRoot();
    if (browser->getNumSelectedFiles() > 0)
    {
        const auto sel = browser->getSelectedFile (0);
        if (sel.isDirectory()) elegida = sel;
    }

    //  Y SE COMPRUEBA ESCRIBIENDO, no mirando. En Android la mitad de las
    //  carpetas que se pueden LISTAR no aceptan que dejes nada dentro, y
    //  enterarse al final de un rebote de cuarenta segundos es enterarse tarde:
    //  la exportacion es la unica accion de esta app que no se deshace tocando
    //  otra vez. ProjectStore::canReallyWriteInto deja un fichero de un byte y
    //  lo vuelve a leer, que es la unica prueba que no miente.
    if (! ProjectStore::setExports (elegida))
    {
        status.setText (T ("Esa carpeta no deja escribir - prueba otra"),
                        juce::dontSendNotification);
        return;
    }

    browseModo = browsePad;
    closeAllSheets();
    destinoCache = juce::File();
    openSheet (exportSheet, setButton);
    status.setText (T ("El rebote caera en %1", elegida.getFileName()),
                    juce::dontSendNotification);
}

void MainComponent::openBrowseForPad (int index)
{
    browseModo = browsePad;
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
    //  Y el nombre se guarda aqui, que es donde ya se pregunta por el disco: el
    //  pintor lo leia el solo -`existsAsFile()` mas `getFileName()`- o sea dos
    //  syscalls dentro de `paint`, la misma pregunta escrita dos veces.
    browsePickName = ready ? browser->getSelectedFile (0).getFileName() : juce::String();
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
    g.drawFittedText (names[row], r.reduced (ZatiLookAndFeel::kAir, 0), juce::Justification::centredLeft, 1, 0.9f);
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
        //  El silenciado de carriles y el tramo en bucle: son estado del
        //  arreglo igual que las celdas. Sin guardarlos, volver a abrir un
        //  proyecto devuelve los cuatro carriles sonando y la cancion entera
        //  dando vueltas, que no es lo que la persona dejo puesto.
        {
            juce::String mudos;
            for (int lane = 0; lane < Playlist::kLanes; ++lane)
                mudos += (engine.isSongLaneMuted (lane) ? "1" : "0");
            song.setProperty ("mudos", mudos, nullptr);
        }
        //  Y EL SILENCIO POR BLOQUE, cuatro numeros y no una lista dispersa:
        //  es un bit por compas en un uint64 por carril, o sea que la tabla
        //  ENTERA son cuatro enteros. Escribir tripletes «carril compas 1»
        //  seria mas largo que el dato. Se guardan en decimal porque
        //  `getProperty` devuelve un var y un uint64 en hexadecimal habria que
        //  parsearlo a mano.
        {
            juce::StringArray cm;
            for (int lane = 0; lane < Playlist::kLanes; ++lane)
                cm.add (juce::String ((juce::int64) engine.songCellMuteMask (lane)));
            song.setProperty ("bmudos", cm.joinIntoString (","), nullptr);
        }
        song.setProperty ("bucleA", engine.getSongLoopFrom(), nullptr);
        song.setProperty ("bucleB", engine.getSongLoopTo(), nullptr);

        //  LOS CLIPS DE AUDIO, DISPERSOS. Una linea por clip y nada cuando no
        //  hay ninguno, que es como se guardan el acorde y el empujon: casi
        //  ningun proyecto los lleva y una tabla entera de sesenta y cuatro
        //  seria ruido en todos. Y un proyecto de antes de que existieran no
        //  tiene la propiedad, o sea que vuelve sin clips, que es exactamente
        //  como sonaba el dia que se guardo.
        if (! clips.empty())
        {
            juce::String filas;
            for (const auto& c : clips)
                filas << c.pad << " " << c.pista << " " << c.compas << " "
                      << c.desde << " " << c.largo << " " << juce::String (c.gain, 4) << ";";
            song.setProperty ("clips", filas, nullptr);
        }

        //  Y LA AUTOMATIZACION, DISPERSA Y POR LA MISMA RAZON. «paso fx par
        //  valor;» y nada cuando no hay eventos. Va dentro de <song> y no de
        //  <FX> porque sus pasos son de la LINEA DE TIEMPO: guardarla al lado
        //  de los parametros diria que es del efecto, y es de la cancion.
        if (! autoEventos.empty())
        {
            juce::String filas;
            for (const auto& e : autoEventos)
                filas << e.paso << " " << (int) e.fx << " " << (int) e.par << " "
                      << juce::String (e.valor, 4) << ";";
            song.setProperty ("auto", filas, nullptr);
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
    //  LAS SEIS RANURAS DE CADA CANAL, en una sola propiedad: las seis de un
    //  canal separadas por comas -«que tipo vive en cada sitio», con -1 para
    //  vacia- y los canales por punto y coma. Una propiedad por canal serian
    //  dieciseis y la lista no es dispersa: las seis de cada uno valen siempre
    //  algo.
    {
        juce::StringArray filas;
        for (int c = 0; c < kNumCanales; ++c)
        {
            juce::StringArray r;
            for (int s = 0; s < kNumRanuras; ++s) r.add (juce::String (slotFx[(size_t) c][(size_t) s]));
            filas.add (r.joinIntoString (","));
        }
        fx.setProperty ("slots", filas.joinIntoString (";"), nullptr);
    }

    //  Y LOS ENVIOS DEL CANAL, con la misma forma: veintiun numeros por canal,
    //  los canales por punto y coma. Es lo que ANTES vivia en cada `<PAD>` como
    //  `sends`, y esa propiedad ya no se escribe — ver la rama de lectura.
    {
        juce::StringArray filas;
        for (int c = 0; c < kNumCanales; ++c)
        {
            juce::StringArray r;
            for (int f = 0; f < kNumFx; ++f) r.add (juce::String (engine.getCanalSend (c, f), 3));
            filas.add (r.joinIntoString (","));
        }
        fx.setProperty ("csends", filas.joinIntoString (";"), nullptr);
    }

    //  El fader y el mute de cada canal, dos listas cortas y no dispersas.
    {
        juce::StringArray g, m;
        for (int c = 0; c < kNumCanales; ++c)
        {
            g.add (juce::String (engine.getCanalGain (c), 3));
            m.add (engine.getCanalMute (c) ? "1" : "0");
        }
        fx.setProperty ("cgain", g.joinIntoString (","), nullptr);
        fx.setProperty ("cmute", m.joinIntoString (","), nullptr);
    }
    //  LAS CINCO BANDAS DEL EQ, DISPERSAS Y EN UNA SOLA PROPIEDAD, por lo
    //  mismo que el acorde y el empujon: son diez numeros y casi ningun
    //  proyecto los mueve. «hz:dB;hz:dB;...» y lo que no este vale su defecto,
    //  que es ademas lo que hace que un proyecto anterior -que no tiene la
    //  propiedad- suene exactamente igual que el dia que se guardo.
    //
    //  Y NO se guardan aqui ANCHO ni SALIDA: esos dos son parametros de la
    //  fila, viven en `fxParams` y ya los escribe el bucle de arriba. Un
    //  numero, un dueno - guardarlos dos veces es como dos sitios acaban
    //  discrepando.
    {
        juce::StringArray e;
        for (int b = 0; b < Eq5::kBands; ++b)
            e.add (juce::String (engine.getEqFreq (b), 1) + ":"
                     + juce::String (engine.getEqGain (b), 2) + ":"
                     + juce::String (engine.getEqTipo (b)) + ":"
                     + juce::String (engine.getEqQ (b), 2));
        fx.setProperty ("eq", e.joinIntoString (";"), nullptr);
    }
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
        //  Y DE DONDE SALIO. Sin esto la marca se perderia entre el primer
        //  arranque y el segundo -la sesion devuelve los sesenta y cuatro desde
        //  sus WAV- y la regla del banco de tomas diria una cosa hoy y otra
        //  mañana con la misma maquina.
        p.setProperty ("fab",     padDeFabrica[(size_t) i], nullptr);
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
        p.setProperty ("ancho",   padAnchoUI[(size_t) i], nullptr);
        p.setProperty ("attack",  padAttack[(size_t) i],  nullptr);
        p.setProperty ("release", padRelease[(size_t) i], nullptr);
        //  DE QUE PAD SALE EL AUDIO DE ESTE, que es lo que convierte
        //  dieciseis pads en un troceado y no en dieciseis sonidos sueltos.
        //
        //  Un AUTO CHOP no copia la muestra dieciseis veces: los dieciseis pads
        //  apuntan al MISMO SampleBuffer y se diferencian por su recorte. Todo
        //  lo que sabe que son trozos de lo mismo -la onda dibujando los trozos
        //  hermanos, el color, poder mover un corte- compara PUNTEROS. Y el
        //  disco no guarda punteros: se escribia un WAV por pad, asi que al
        //  volver eran dieciseis buffers distintos con el mismo contenido. El
        //  troceado seguia SONANDO igual y habia dejado de ser un troceado:
        //  dieciseis copias del break en memoria, dieciseis en disco, y la onda
        //  sin un solo hermano que ensenar.
        //
        //  Se guarda el pad DUENO: el mas bajo que comparte ese buffer. Un pad
        //  que es su propio dueno se guarda a si mismo.
        int fuente = i;
        for (int j = 0; j < i; ++j)
            if (uiSample[(size_t) j] != nullptr && uiSample[(size_t) j] == uiSample[(size_t) i]) { fuente = j; break; }
        p.setProperty ("fuente",  fuente,                 nullptr);
        //  Y SI ES UN INSTRUMENTO, LA RECETA Y NO EL AUDIO.
        //
        //  Un instrumento son diez zonas -cinco octavas por dos capas- en un
        //  buffer de 2 MB. Escribirlo como WAV lo devolveria SONANDO parecido y
        //  sin ser ya un instrumento: un solo trozo, sin zonas, sin capas, sin
        //  las otras cuatro octavas y con 2 MB en disco por pad. Es exactamente
        //  lo que le paso al troceado -volvian los trozos y no volvia la
        //  relacion- contado con otra pieza.
        //
        //  Se guarda familia*16+preset, o -1. Un proyecto anterior no trae la
        //  propiedad, sale -1, y su WAV se lee como siempre.
        p.setProperty ("inst",
                       (uiSample[(size_t) i] != nullptr && uiSample[(size_t) i]->familia >= 0)
                           ? uiSample[(size_t) i]->familia * Sintes::kPresets
                                 + uiSample[(size_t) i]->preset
                           : -1,
                       nullptr);
        p.setProperty ("corte",   padCut[(size_t) i],     nullptr);
        p.setProperty ("reson",   padReso[(size_t) i],    nullptr);
        p.setProperty ("suavein", padFadeIn[(size_t) i],  nullptr);
        p.setProperty ("suaveout",padFadeOut[(size_t) i], nullptr);
        p.setProperty ("zati",    padZati[(size_t) i],    nullptr);

        //  A QUE CANAL VA. Es lo unico del reparto que es del PAD desde que el
        //  envio es del canal; el cuanto vive en `<FX csends=...>`.
        p.setProperty ("canal", engine.getPadCanal (i), nullptr);

        //  Y `sends` YA NO SE ESCRIBE, que no es un olvido: era el envio del
        //  pad y hoy es el RECORTE con el que un fichero anterior se guardo.
        //  Un proyecto nuevo no lo trae, y «sin la propiedad vale uno» —o sea
        //  neutro— es exactamente la respuesta que hace falta. Escribirlo con
        //  todo unos seria un campo que dice lo mismo siempre y que el dia que
        //  alguien lo lea al reves apaga los sesenta y cuatro pads.
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

        //  UNA PALABRA HEX POR PASO, Y DE SESENTA Y CUATRO BITS.
        //
        //  Era `int mask` con `1 << p`, y el comentario decia "16 pads = 16
        //  bits": el numero de cuando la maquina tenia dieciseis pads. Con
        //  sesenta y cuatro, `1 << p` para p >= 32 es DESPLAZAR UN INT DE 32
        //  BITS MAS DE 32 - comportamiento indefinido, y en ARM y en x86 el
        //  contador se toma modulo 32, asi que 1 << 32 vale 1.
        //
        //  O sea: el pad 32 escribia el bit del pad 0. Los pads 32..47 son el
        //  banco C y los 0..15 el banco A, asi que guardar una secuencia del
        //  banco A y volver a abrir el proyecto la hacia aparecer TAMBIEN en el
        //  banco C - y lo mismo entre B y D. No era una copia: es que los dos
        //  bancos compartian los mismos dieciseis bits.
        //
        //  El motor ya guardaba la mascara en uint64 (ver patternBank); lo que
        //  se quedo en 32 fue el fichero.
        juce::String steps, notes, vels, rolls;
        for (int st = 0; st < kNumSteps; ++st)
        {
            std::uint64_t mask = 0;
            for (int p = 0; p < kNumPads; ++p)
                if (pattern[(size_t) b][(size_t) st][(size_t) p]) mask |= (1ull << p);
            steps << juce::String::toHexString ((juce::int64) mask) << " ";

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

        //  Y LO QUE NO SE GUARDABA: el acorde, el empujon, el bloqueo del corte
        //  y el largo de la nota. Cuatro cosas que la app sabe escribir y no
        //  sabia recordar - un acorde de cuatro notas volvia siendo una, y un
        //  patron humanizado volvia recto. Se encontro al ir a guardar el largo
        //  y mirar quien mas faltaba.
        //
        //  DISPERSO y no una tabla entera: notes, vels y rolls escriben 4096
        //  numeros por banco cada uno porque casi todos los pasos los llevan,
        //  pero un acorde o un empujon los lleva un punado de casillas. En
        //  tripletes "paso pad valor", y lo que no esta vale su defecto - que
        //  es ademas lo que hace que un proyecto viejo, que no tiene ni la
        //  propiedad, suene exactamente igual que antes.
        juce::String chords, nudges, locks, lens, plocks;
        for (int st = 0; st < kNumSteps; ++st)
            for (int p = 0; p < kNumPads; ++p)
            {
                if (const auto c = engine.getStepChordRaw (b, st, p); c != 0)
                    chords << st << " " << p << " " << juce::String::toHexString ((juce::int64) c) << " ";
                if (const int n = engine.getStepNudge (b, st, p); n != 0)
                    nudges << st << " " << p << " " << n << " ";
                if (const int lk = engine.getStepLock (b, st, p); lk != AudioEngine::kNoLock)
                    locks  << st << " " << p << " " << lk << " ";
                if (const int lg = engine.getStepLen (b, st, p); lg != AudioEngine::kLenSuelto)
                    lens   << st << " " << p << " " << lg << " ";
                //  Los cuatro bloqueos de ataque, caida, inicio y pan van en un
                //  solo triplete porque viven empaquetados en un uint32: cuatro
                //  listas dispersas serian cuatro sitios que mantener para un
                //  dato que el motor ya guarda junto.
                if (const auto pl = engine.getStepPLockRaw (b, st, p); pl != 0)
                    plocks << st << " " << p << " " << juce::String::toHexString ((juce::int64) pl) << " ";
            }
        if (chords.isNotEmpty()) bk.setProperty ("chords", chords.trim(), nullptr);
        if (nudges.isNotEmpty()) bk.setProperty ("nudges", nudges.trim(), nullptr);
        if (locks.isNotEmpty())  bk.setProperty ("locks",  locks.trim(),  nullptr);
        if (lens.isNotEmpty())   bk.setProperty ("lens",   lens.trim(),   nullptr);
        if (plocks.isNotEmpty()) bk.setProperty ("plocks", plocks.trim(), nullptr);

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

    //  Y LO MISMO CON LOS EFECTOS: sin <FX>, los seis se quedaban donde los
    //  dejo el proyecto anterior. El arbol invalido responde que no a
    //  hasProperty, asi que el bucle de abajo -que ya sabe poner el defecto de
    //  cada mando- vale igual para las dos ramas y no hay una segunda lista de
    //  defectos que mantener.
    auto fx = s.getChildWithName ("FX");
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
            //  La luz de las seis tapas la reparte `refrescaRanuras` al
            //  final de esta funcion, que es quien sabe donde vive cada tipo.
            juce::ignoreUnused (f);
        }

        //  LAS SEIS RANURAS.
        //
        //  Y LO QUE NO ESTA EN EL FICHERO VALE SU DEFECTO ANTIGUO: un proyecto
        //  guardado antes de que las ranuras existieran no tiene la propiedad,
        //  y entonces vale la fila de siempre -la ranura s con el tipo s- y no
        //  la de hoy, que es vacia. Lo que manda no es cual es el defecto de
        //  hoy sino como sonaba el dia que se guardo, que es la regla que ya
        //  gobierna los envios y que `Tests/proyectos` comprueba con tres
        //  ficheros congelados.
        //
        //  Acotado EN LA PUERTA y no en quien llama: el valor sale de un
        //  project.xml que puede estar corrupto o ser de otra epoca, y un
        //  entero cualquiera aqui es un indice fuera de `fxDefs`. Lo que no
        //  encaje vale VACIA, que es el unico valor que no puede hacer daño.
        {
            //  El defecto ANTIGUO es la fila de siempre EN EL CANAL 0, que es
            //  donde caen los pads de un proyecto sin canales: alli la maquina
            //  suena como sonaba, y los otros quince nacen vacios.
            for (auto& fila : slotFx) fila.fill (kSlotVacia);
            for (int s = 0; s < kNumRanuras; ++s) slotFx[0][(size_t) s] = s;

            if (fx.hasProperty ("slots"))
            {
                //  DIECISEIS FILAS SEPARADAS POR PUNTO Y COMA, y una sola sin
                //  el es el fichero de la epoca de una fila: entonces esa es la
                //  del canal 0 y los demas quedan vacios, que es como sonaba.
                juce::StringArray filas;
                filas.addTokens (fx.getProperty ("slots").toString(), ";", "");
                for (auto& fila : slotFx) fila.fill (kSlotVacia);

                for (int c = 0; c < kNumCanales && c < filas.size(); ++c)
                {
                    juce::StringArray r;
                    r.addTokens (filas[c], ",", "");
                    for (int s = 0; s < kNumRanuras; ++s)
                    {
                        const int v = s < r.size() ? r[s].getIntValue() : kSlotVacia;
                        slotFx[(size_t) c][(size_t) s] = juce::isPositiveAndBelow (v, kNumFx) ? v : kSlotVacia;
                    }
                }

                //  UN INSERTO, UN CANAL, tambien al volver del disco. Un
                //  fichero escrito a mano puede repetir un tipo y eso serian
                //  dos ventanas al mismo aparato: se queda la primera. Los
                //  cinco que SUMAN pueden repetirse entre canales -es lo que un
                //  envio significa- pero no dentro del mismo.
                for (int c = 0; c < kNumCanales; ++c)
                    for (int s = 0; s < kNumRanuras; ++s)
                    {
                        const int v = slotFx[(size_t) c][(size_t) s];
                        if (v < 0) continue;
                        bool repe = false;
                        for (int t = 0; t < s && ! repe; ++t)
                            repe = slotFx[(size_t) c][(size_t) t] == v;
                        if (! repe && AudioEngine::sustituye (v))
                            for (int oc = 0; oc < c && ! repe; ++oc)
                                repe = slotDeFxEn (oc, v) >= 0;
                        if (repe) slotFx[(size_t) c][(size_t) s] = kSlotVacia;
                    }
            }

            //  Y un efecto que quedo ENCENDIDO en el fichero y cuya ranura ya
            //  no existe se apaga: seguiria sonando sin tapa donde tocarlo.
            for (int f = 0; f < kNumFx; ++f)
                if (fxOn[(size_t) f] && canalDeFx (f) < 0)
                    setFxEnabled (f, false);
        }

        //  LOS ENVIOS DEL CANAL, y LAS DOS RAMAS QUE HACEN QUE UN PROYECTO
        //  ANTERIOR SUENE IGUAL.
        //
        //  Un fichero SIN `csends` es anterior a que la mesa existiera, y
        //  entonces el envio era del PAD: vuelve con el canal 0 a UNO en los
        //  tipos que el fichero NOMBRA y a cero en los demas, y el recorte de
        //  cada pad lleva lo que su `sends` decia. El producto de los dos es
        //  exactamente el numero de antes.
        //
        //  Y el «en los tipos que nombra» es la mitad que hay que escribir con
        //  cuidado: con el canal 0 a uno en los veintiuno, un proyecto de la
        //  epoca de seis abriria con los quince nuevos DE PAR EN PAR — que es
        //  el mismo fallo que `04-seis-envios.xml` ya caza entrando por la otra
        //  puerta. Cuantos nombra lo dice el primer `<PAD sends=...>`, y si no
        //  hay ninguno el fichero es anterior a los envios y valen los
        //  veintiuno, que es como sonaba entonces.
        {
            const bool traeCanales = fx.hasProperty ("csends");
            int viejos = kNumFx;
            if (! traeCanales)
            {
                viejos = 0;
                if (auto padsV = s.getChildWithName ("PADS"); padsV.isValid())
                    for (int n = 0; n < padsV.getNumChildren() && viejos == 0; ++n)
                        if (padsV.getChild (n).hasProperty ("sends"))
                        {
                            juce::StringArray t;
                            t.addTokens (padsV.getChild (n).getProperty ("sends").toString(), ",", "");
                            viejos = t.size();
                        }
                if (viejos == 0) viejos = kNumFx;   // anterior a los envios: todo abierto
            }

            juce::StringArray filas;
            filas.addTokens (fx.getProperty ("csends", juce::String()).toString(), ";", "");
            for (int c = 0; c < kNumCanales; ++c)
            {
                juce::StringArray r;
                if (c < filas.size()) r.addTokens (filas[c], ",", "");
                for (int f = 0; f < kNumFx; ++f)
                    engine.setCanalSend (c, f,
                                         traeCanales ? (f < r.size() ? r[f].getFloatValue() : 0.0f)
                                                     : (c == 0 && f < viejos ? 1.0f : 0.0f));
            }

            //  El fader y el mute, con su defecto antiguo: uno y apagado, que
            //  es una mesa que no existia y por tanto no atenuaba nada.
            juce::StringArray gs, ms;
            gs.addTokens (fx.getProperty ("cgain", juce::String()).toString(), ",", "");
            ms.addTokens (fx.getProperty ("cmute", juce::String()).toString(), ",", "");
            for (int c = 0; c < kNumCanales; ++c)
            {
                engine.setCanalGain (c, c < gs.size() ? gs[c].getFloatValue() : 1.0f);
                engine.setCanalMute (c, c < ms.size() && ms[c].getIntValue() != 0);
            }
        }

        //  ...y el estado del panel XY, UNA vez. La llave del for cerraba ocho
        //  lineas mas abajo de donde decia la sangria, asi que estas cuatro
        //  corrian seis veces -una por efecto- en cada apertura de proyecto.
        //  jlimit porque un proyecto viejo no tiene la propiedad y getProperty
        //  devuelve 0, que es un indice valido - pero uno guardado por una
        //  version con mas efectos no lo seria.
        //  LAS CINCO BANDAS. Sin la propiedad quedan donde `Eq5` nace -el
        //  reparto de fabrica y la curva plana- que es como sonaba un proyecto
        //  escrito antes de que el EQ existiera. Acotado EN LA PUERTA, o sea
        //  en `Eq5::ponBanda`: el valor sale de un project.xml que puede estar
        //  corrupto o ser de otra epoca.
        for (int b = 0; b < Eq5::kBands; ++b)
        {
            engine.setEqBand (b, Eq5::kFreqDef[b], 0.0f);
            engine.setEqTipo (b, (int) Eq5::tipoDeFabrica (b));
            engine.setEqQ    (b, Eq5::kQDef);
        }
        if (fx.hasProperty ("eq"))
        {
            juce::StringArray e;
            e.addTokens (fx.getProperty ("eq").toString(), ";", "");
            for (int b = 0; b < Eq5::kBands && b < e.size(); ++b)
            {
                //  Cuatro campos hoy y DOS en un proyecto de la tanda
                //  anterior: los que falten valen su defecto ANTIGUO -el
                //  reparto de fabrica y Q 0.70-, que es como sonaba el dia que
                //  se guardo y no como sonaria hoy. Es la regla de siempre,
                //  aplicada dentro de una propiedad y no a la propiedad
                //  entera.
                juce::StringArray c;
                c.addTokens (e[b], ":", "");
                if (c.size() < 2) continue;
                engine.setEqBand (b, c[0].getFloatValue(), c[1].getFloatValue());
                if (c.size() >= 3) engine.setEqTipo (b, c[2].getIntValue());
                if (c.size() >= 4) engine.setEqQ    (b, c[3].getFloatValue());
            }
        }
        refrescaEq();

        engine.setDuckPad (juce::jlimit (-1, kNumPads - 1, (int) fx.getProperty ("duckPad", -1)));
        xyLatch = (bool) fx.getProperty ("xyLatch", false);
        selectXyFx (juce::jlimit (0, kNumFx - 1, (int) fx.getProperty ("xyFx", 0)));
        xyLatchButton.setToggleState (xyLatch, juce::dontSendNotification);

        //  Y LA FILA, LA ULTIMA: el rotulo, el dibujo y la luz de las seis
        //  tapas salen de `slotFx`, que se acaba de leer.
        refrescaRanuras();
    }

    //  LOS SESENTA Y CUATRO A SU DEFECTO ANTES DE APLICAR LO QUE TRAIGA.
    //
    //  Un proyecto de dieciseis pads dejaba los otros cuarenta y ocho con la
    //  ganancia, el pan y el filtro del proyecto anterior mientras clearMissing
    //  les quitaba el audio. Lo que el fichero no dice no se hereda: se pone a
    //  lo que vale en una maquina recien encendida.
    for (int i = 0; i < kNumPads; ++i) padPorDefecto (i);

    if (auto pads = s.getChildWithName ("PADS"); pads.isValid())
    {
        for (const auto& p : pads)
        {
            const int i = (int) p.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (i, kNumPads)) continue;

            padName[(size_t) i]    = p.getProperty ("name", juce::String()).toString();
            //  LO QUE NO TRAE LA PROPIEDAD SE MIGRA UNA VEZ, y no se le da un
            //  defecto: un fichero escrito antes de que esta marca existiera no
            //  puede decir de donde salio cada pad, y las dos respuestas fijas
            //  son malas -«todo de fabrica» deja que una toma se coma algo tuyo
            //  y «nada de fabrica» deja la maquina sin sitio donde grabar-. Se
            //  reconoce por el nombre, que es exacto para el caso que importa
            //  -una maquina que no ha cargado nada todavia- y solo puede
            //  equivocarse hacia el lado seguro, que es negarse a grabar.
            padDeFabrica[(size_t) i] = p.hasProperty ("fab")
                                         ? (bool) p.getProperty ("fab")
                                         : (padName[(size_t) i] == Kits::table()[(size_t) i].name);
            padPitch[(size_t) i]   = (float) p.getProperty ("pitch", 0.0);
            padCents[(size_t) i]   = (float) p.getProperty ("cents", 0.0);
            padKeepLen[(size_t) i] = (bool)  p.getProperty ("keeplen", false);
            padGain[(size_t) i]    = (float) p.getProperty ("gain", 0.85);
            engine.setPadMute (i, (bool) p.getProperty ("mute", false));
            engine.setPadSolo (i, (bool) p.getProperty ("solo", false));
            //  El recorte se guarda en 0..1 y se convierte a muestras mas
            //  abajo: un 9 o un -3 en el fichero daria un indice fuera del
            //  buffer, y `(int)` sobre eso es conversion indefinida antes de
            //  llegar siquiera al motor. Los demas los acotan los setters.
            padStart01[(size_t) i] = juce::jlimit (0.0f, 1.0f, (float) p.getProperty ("start", 0.0));
            padEnd01[(size_t) i]   = juce::jlimit (0.0f, 1.0f, (float) p.getProperty ("end", 1.0));
            padLoop[(size_t) i]    = (bool)  p.getProperty ("loop", false);
            //  Default true: a project saved before AUTOCUT existed has no
            //  such property, and it should come back behaving like every
            //  other pad rather than as the one that stacks.
            padSelfCut[(size_t) i] = (bool)  p.getProperty ("autocut", true);
            padReverse[(size_t) i] = (bool)  p.getProperty ("reverse", false);
            padChokeUI[(size_t) i] = (int)   p.getProperty ("choke", 0);
            padPan[(size_t) i]     = (float) p.getProperty ("pan", 0.0);
            //  UNO por defecto: un proyecto guardado antes de que el ancho
            //  existiera no trae la propiedad y tiene que volver sonando
            //  exactamente como se guardo. El cero del array seria mono.
            padAnchoUI[(size_t) i] = (float) p.getProperty ("ancho", 1.0);
            engine.setPadAncho (i, padAnchoUI[(size_t) i]);
            padAttack[(size_t) i]  = (float) p.getProperty ("attack", 2.0);
            padRelease[(size_t) i] = (float) p.getProperty ("release", 5.0);
            //  Un proyecto guardado antes de que el filtro existiera no lleva
            //  estas dos, y tiene que volver SIN filtrar - abierto del todo -
            //  o sonaria distinto de como se guardo. El cero del array seria
            //  0 Hz, o sea mudo.
            padCut[(size_t) i]  = (float) p.getProperty ("corte", (double) AudioEngine::kFiltOpenHz);
            padReso[(size_t) i] = (float) p.getProperty ("reson", 0.0);
            //  Cero por defecto: un proyecto de antes de que esto existiera se
            //  guardo con el corte seco y tiene que volver seco.
            padFadeIn[(size_t) i]  = (float) p.getProperty ("suavein", 0.0);
            padFadeOut[(size_t) i] = (float) p.getProperty ("suaveout", 0.0);
            engine.setPadFadeIn  (i, padFadeIn[(size_t) i]);
            engine.setPadFadeOut (i, padFadeOut[(size_t) i]);
            engine.setPadCutoff (i, padCut[(size_t) i]);
            engine.setPadReso   (i, padReso[(size_t) i]);
            padZati[(size_t) i]    = (int)   p.getProperty ("zati", Zati::forPad (i));

            //  EL CANAL DEL PAD. Sin la propiedad, el 0: es donde `applyState`
            //  pone la fila de siempre y los envios de un proyecto anterior, o
            //  sea el unico canal que en aquel fichero significaba algo.
            engine.setPadCanal (i, (int) p.getProperty ("canal", 0));

            //  Y `sends` PASA A SER EL RECORTE, que es lo que hace que un
            //  proyecto anterior suene igual.
            //
            //  Un proyecto SIN la propiedad es una de dos cosas y las dos piden
            //  lo mismo: o es anterior a que los envios existieran -y entonces
            //  cada pad iba entero a todos- o es de HOY, que ya no la escribe
            //  porque el dueño del envio es el canal. En los dos casos el
            //  recorte es NEUTRO y quien manda es `canalSend`, que la rama de
            //  arriba deja en uno para el primero y en lo que el fichero diga
            //  para el segundo.
            //
            //  Y NO ES LO MISMO QUE UNO CON LA PROPIEDAD Y MENOS TOKENS, que
            //  es donde estaba el fallo que esto ya arreglo una vez. La lista
            //  es POSICIONAL, asi que un proyecto guardado con once tipos abre
            //  con once numeros; los que no existian cuando se guardo no
            //  sonaban, asi que su recorte vale CERO — y con el canal 0 abierto
            //  solo en esos once, los diez nuevos siguen mudos por los dos
            //  lados.
            const bool traeEnvios = p.hasProperty ("sends");
            juce::StringArray sends;
            sends.addTokens (p.getProperty ("sends", juce::String()).toString(), ",", "");
            for (int f = 0; f < kNumFx; ++f)
                engine.setPadRecorte (i, f, f < sends.size() ? sends[f].getFloatValue()
                                                             : (traeEnvios ? 0.0f : 1.0f));

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
                //  Y se lee en 64 bits, por lo mismo. Un proyecto guardado con
                //  el fallo trae como mucho ocho digitos hex: sus bancos A y B
                //  vuelven bien y lo que hubiera en C y D no vuelve, porque no
                //  llego a escribirse - estaba encima de los bits de A y B y no
                //  hay forma de distinguirlo. Lo que si deja de pasar es que se
                //  duplique.
                const std::uint64_t mask = s2 < st.size()
                                             ? (std::uint64_t) st[s2].getHexValue64() : 0ull;
                for (int p = 0; p < kNumPads; ++p)
                {
                    const bool on = (mask & (1ull << p)) != 0;
                    pattern[(size_t) b][(size_t) s2][(size_t) p] = on;
                    engine.setStep (b, s2, p, on);

                    const int ni = s2 * kNumPads + p;
                    engine.setStepNote (b, s2, p, ni < nt.size() ? nt[ni].getIntValue() : 0);
                    engine.setStepVel  (b, s2, p, ni < vl.size() ? vl[ni].getIntValue() : 127);
                    engine.setStepRoll (b, s2, p, ni < rl.size() ? rl[ni].getIntValue() : 1);
                    //  Lo disperso se pone a su defecto antes de leerlo: si no,
                    //  abrir un proyecto encima de otro deja el acorde y el
                    //  empujon del anterior donde el nuevo no dice nada.
                    engine.clearStepExtras (b, s2, p);
                    engine.setStepNudge (b, s2, p, 0);
                    engine.setStepLock  (b, s2, p, AudioEngine::kNoLock);
                    engine.setStepLen   (b, s2, p, AudioEngine::kLenSuelto);
                    engine.setStepPLockRaw (b, s2, p, 0);
                }
            }

            //  Y LO DISPERSO, en tripletes "paso pad valor". Ver captureState:
            //  un proyecto anterior no trae la propiedad y entonces no hay nada
            //  que poner, que es exactamente como sonaba.
            auto tripletes = [&bk] (const char* clave)
            {
                juce::StringArray t;
                t.addTokens (bk.getProperty (clave, "").toString(), " ", "");
                t.removeEmptyStrings();
                return t;
            };
            {
                const auto ch = tripletes ("chords");
                for (int i2 = 0; i2 + 2 < ch.size(); i2 += 3)
                    engine.setStepChordRaw (b, ch[i2].getIntValue(), ch[i2 + 1].getIntValue(),
                                            (std::uint32_t) ch[i2 + 2].getHexValue64());

                const auto nu = tripletes ("nudges");
                for (int i2 = 0; i2 + 2 < nu.size(); i2 += 3)
                    engine.setStepNudge (b, nu[i2].getIntValue(), nu[i2 + 1].getIntValue(),
                                         nu[i2 + 2].getIntValue());

                const auto lk = tripletes ("locks");
                for (int i2 = 0; i2 + 2 < lk.size(); i2 += 3)
                    engine.setStepLock (b, lk[i2].getIntValue(), lk[i2 + 1].getIntValue(),
                                        lk[i2 + 2].getIntValue());

                const auto lg = tripletes ("lens");
                for (int i2 = 0; i2 + 2 < lg.size(); i2 += 3)
                    engine.setStepLen (b, lg[i2].getIntValue(), lg[i2 + 1].getIntValue(),
                                       lg[i2 + 2].getIntValue());

                const auto pl = tripletes ("plocks");
                for (int i2 = 0; i2 + 2 < pl.size(); i2 += 3)
                    engine.setStepPLockRaw (b, pl[i2].getIntValue(), pl[i2 + 1].getIntValue(),
                                            (std::uint32_t) pl[i2 + 2].getHexValue64());
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

    //  LO QUE EL FICHERO NO TRAE VUELVE A SU DEFECTO, NO SE HEREDA.
    //
    //  `clearSong` vivia DENTRO del if, asi que abrir un proyecto sin <song>
    //  -uno anterior a la linea de tiempo, o uno guardado sin ella- dejaba
    //  sonando el arreglo del proyecto que estuviera abierto. Es la misma
    //  herencia que ya se pago dos veces en newProject, contada en el otro
    //  camino: vaciar la mitad de un proyecto es peor que no vaciar nada,
    //  porque lo que queda parece tuyo.
    auto song = s.getChildWithName ("song");
    if (! song.isValid())
    {
        engine.clearSong();
        engine.setSongLength (8);
        songLenSlider.setValue (8.0, juce::dontSendNotification);
        ponModoCancion (false);
        for (int lane = 0; lane < Playlist::kLanes; ++lane)
        {
            engine.setSongLaneMute (lane, false);
            engine.setSongCellMuteMask (lane, 0);
        }
        engine.setSongLoop (0, 0);
    }
    else
    {
        engine.clearSong();
        engine.setSongLength ((int) song.getProperty ("bars", 8));
        songLenSlider.setValue ((double) engine.getSongLength(), juce::dontSendNotification);
        //  Por ponModoCancion, que es quien pone las TRES tapas: abrir un
        //  proyecto en modo cancion dejaba la de la cara diciendo PATRON.
        ponModoCancion ((bool) song.getProperty ("mode", false));
        for (int lane = 0; lane < Playlist::kLanes; ++lane)
        {
            auto toks = juce::StringArray::fromTokens (song.getProperty ("lane" + juce::String (lane)).toString(), ",", "");
            for (int b = 0; b < juce::jmin (toks.size(), AudioEngine::kSongBars); ++b)
                engine.setSongCell (lane, b, toks[b].getIntValue());
        }
        //  Los mudos y el bucle. Ausentes -un proyecto de antes de que
        //  existieran- valen cero, que es "nada silenciado y sin bucle": el
        //  comportamiento que ese proyecto tenia.
        {
            const auto mudos = song.getProperty ("mudos").toString();
            for (int lane = 0; lane < Playlist::kLanes; ++lane)
                engine.setSongLaneMute (lane, lane < mudos.length() && mudos[lane] == '1');
        }
        //  Y el silencio por bloque. Un proyecto anterior no trae la propiedad
        //  y vuelve con los cuatro a CERO - o sea, todos los bloques sonando,
        //  que es exactamente como sonaba el dia que se guardo. Lo que manda no
        //  es cual es el defecto de hoy sino como sonaba entonces.
        {
            const auto cm = juce::StringArray::fromTokens (
                                song.getProperty ("bmudos").toString(), ",", "");
            for (int lane = 0; lane < Playlist::kLanes; ++lane)
                engine.setSongCellMuteMask (lane, lane < cm.size()
                                                    ? (juce::uint64) cm[lane].getLargeIntValue()
                                                    : 0);
        }
        engine.setSongLoop ((int) song.getProperty ("bucleA", 0),
                            (int) song.getProperty ("bucleB", 0));

        songPrimerCompas = 0;
        songCursor = 0;
        refreshSong();
    }

    //  LOS CLIPS DE AUDIO. Se vacian SIEMPRE y se rellenan si el fichero los
    //  trae, que es la regla que ya costo dos veces en newProject y otra al
    //  abrir: vaciar la mitad de un proyecto es peor que no vaciar nada,
    //  porque lo que queda parece tuyo. Un proyecto sin la propiedad -de antes
    //  de que los clips existieran- vuelve sin ninguno.
    clips.clear();
    if (song.isValid())
    {
        const auto filas = juce::StringArray::fromTokens (
            song.getProperty ("clips").toString(), ";", "");
        for (const auto& fila : filas)
        {
            const auto n = juce::StringArray::fromTokens (fila.trim(), " ", "");
            if (n.size() < 6) continue;
            ClipUI c;
            c.pad    = n[0].getIntValue();
            c.pista  = n[1].getIntValue();
            c.compas = n[2].getIntValue();
            c.desde  = n[3].getIntValue();
            c.largo  = n[4].getIntValue();
            c.gain   = n[5].getFloatValue();
            //  Se acota EN LA PUERTA, que es donde entra un fichero que puede
            //  venir de otra epoca o corrupto: cada consumidor volviendo a
            //  validar es como el color de un bloque acabo leyendo fuera del
            //  array. Ver setSongCell.
            if (! juce::isPositiveAndBelow (c.pad, kNumPads)) continue;
            if (c.largo <= 0) continue;
            c.pista  = juce::jlimit (0, AudioEngine::kAudioTracks - 1, c.pista);
            c.compas = juce::jlimit (0, AudioEngine::kSongBars - 1, c.compas);
            c.desde  = juce::jmax (0, c.desde);
            c.gain   = juce::jlimit (0.0f, 4.0f, c.gain);
            if ((int) clips.size() < AudioEngine::kMaxClips) clips.push_back (c);
        }
    }
    publicaClips();

    //  Y LA AUTOMATIZACION, con la misma puerta y las mismas cotas. Un proyecto
    //  de antes no tiene la propiedad y vuelve SIN eventos, que es exactamente
    //  como sonaba el dia que se guardo: con los parametros donde los dejo su
    //  ultima linea de <FX> y sin nada moviendolos.
    autoEventos.clear();
    if (song.isValid())
    {
        const auto filas = juce::StringArray::fromTokens (
            song.getProperty ("auto").toString(), ";", "");
        for (const auto& fila : filas)
        {
            const auto n = juce::StringArray::fromTokens (fila.trim(), " ", "");
            if (n.size() < 4) continue;
            const int paso = n[0].getIntValue();
            const int fx   = n[1].getIntValue();
            const int par  = n[2].getIntValue();
            if (paso < 0 || paso >= AudioEngine::kSongBars * AudioEngine::kBarSteps) continue;
            if (! juce::isPositiveAndBelow (fx, kNumFx) || ! juce::isPositiveAndBelow (par, 3)) continue;
            if ((int) autoEventos.size() >= AudioEngine::kMaxAuto) break;
            autoEventos.push_back ({ paso, (juce::uint8) fx, (juce::uint8) par,
                                     n[3].getFloatValue() });
        }
    }
    //  Y SE PUBLICA, en su propia linea. Estaba saliendo de rebote por el
    //  `ponAutoArmado (false)` de abajo -que publica al desarmar- y eso es una
    //  regla apoyada en un efecto secundario: el dia que desarmar deje de
    //  publicar, la automatizacion vuelve del fichero escrita y MUDA sin que
    //  nada falle. Roto a proposito quitando esta linea: `motor 0`.
    publicaAutomacion();
    //  Y se DESARMA al abrir: dejar el modo de escritura puesto de un proyecto
    //  a otro es como se borra una automatizacion sin tocarla.
    ponAutoArmado (false);

    focusFx ((int) s.getProperty ("focusedFx", 0));
    refreshRack();
    for (int i = 0; i < kNumPads; ++i)
    {
        if (mixFaders[i] != nullptr) mixFaders[i]->setValue (dbFromGain (padGain[(size_t) i]), juce::dontSendNotification);
        if (mixPans[i]   != nullptr) mixPans[i]  ->setValue (padPan[(size_t) i],  juce::dontSendNotification);
            if (mixAnchos[i] != nullptr) mixAnchos[i]->setValue (padAnchoUI[(size_t) i], juce::dontSendNotification);
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
        //  Un instrumento se guarda como receta en el XML, asi que aqui no hay
        //  nada que escribir - y si quedara un WAV de antes, sobra: al abrir se
        //  sintetiza y el fichero seria 2 MB muertos que nadie lee.
        if (auto sb = uiSample[(size_t) i];
            sb != nullptr && sb->familia >= 0)
        {
            dest.deleteFile();
        }
        else if (sb != nullptr && sb->buffer.getNumSamples() > 0)
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
    //
    //  Y SE COMPRUEBA ANTES DE PISAR EL BUENO, que es lo que faltaba: la
    //  comprobacion estaba bien y llegaba TARDE - cuando parseXML decia que no,
    //  el project.xml anterior ya no existia, asi que "NO se pudo guardar"
    //  salia encima de un proyecto que acababa de quedarse sin cabecera y fuera
    //  de ProjectStore::list, que exige ese fichero. Es la misma regla que
    //  writeSample ya tenia escrita al lado: nunca se borra el bueno antes de
    //  tener el nuevo.
    const auto xml     = captureState().toXmlString();
    const auto xmlFile = folder.getChildFile ("project.xml");
    bool ok = ProjectStore::escribeTexto (xmlFile, xml, ProjectStore::esXmlLegible);

    if (! ok)
    {
        //  Nothing was saved. Say so and say WHERE it tried, because the
        //  answer to this is almost always the folder, not the app.
        status.setText (T ("NO se pudo guardar en %1", Lang::ltr (folder.getFullPathName())),
                        juce::dontSendNotification);
        setSheet.repaint();
        return;
    }

    apuntaProyecto (name);
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
    ponTransporte (false);

    //  Igual que la sesion: las muestras por trozos, y el estado al final.
    const auto tree = juce::ValueTree::fromXml (*xml);
    beginBusy (T ("Abriendo proyecto"));
    padJob = std::make_unique<PadLoadJob>();
    padJob->folder = folder;
    padJob->clearMissing = true;
    readSourceMap (tree, padJob->source);
    readInstMap   (tree, padJob->inst);
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

    apuntaProyecto (name);
    closeAllSheets();
    status.setText (missing > 0
                        ? T ("Abierto \"%1\"  [%2 pads, %3 sin audio]", name,
                             juce::String (restored), juce::String (missing))
                        : T ("Abierto \"%1\"  [%2 pads]", name, juce::String (restored)),
                    juce::dontSendNotification);
}

//  EL PROYECTO Y SU FECHA SON UN SOLO HECHO, asi que los escribe una sola
//  funcion. Estaban en cinco `currentProject = ...` sueltos -guardar, abrir,
//  borrar, NUEVO y restaurar la sesion- y una fecha anadida a mano en cinco
//  sitios es una fecha que se queda vieja en el que se olvide. Es la misma
//  leccion que `padPorDefecto` y que `armaCuentaSiToca`.
//
//  Y AQUI ES DONDE SE LEE EL DISCO, que es el otro motivo de que exista: una
//  sola vez por apertura o por guardado, y nunca desde `paint`.
void MainComponent::apuntaProyecto (const juce::String& name)
{
    currentProject = name;
    proyectoFecha  = name.isEmpty()
                       ? juce::Time()
                       : ProjectStore::folderFor (name).getChildFile ("project.xml")
                                                       .getLastModificationTime();
    repaint (headerArea);
}

//  QUE PROYECTO, CUANTO TRABAJO Y DE CUANDO — y en ese orden de importancia,
//  que es el orden en que se CAEN al reves.
//
//  El nombre es el unico de los tres que no se puede deducir mirando la
//  maquina: los pads llenos se ven en la rejilla y «hace dos dias» se intuye.
//  Asi que el nombre no se cae nunca y los otros dos se piden con el TEXTO
//  puesto, que es la misma pregunta que ya deciden BANCO, PADS, la tira del
//  paso y las seis pestanas — y no con el ancho de la ventana, que es lo que
//  hace que una escalera mienta en arabe y en chino.
//
//  La cifra va dentro de `Lang::ltr`, que es la regla de la casa para numeros
//  latinos en escritura arabe, y «HACE n D» lleva CLAVE PROPIA: reaprovechar
//  una por parecerse en espanol es lo que costo ATRAS y EMPEZAR en el tour.
juce::String MainComponent::lineaDeContinuidad (int anchoDisponible,
                                                const juce::Font& fuente) const
{
    auto cabe = [&fuente, anchoDisponible] (const juce::String& t)
    {
        return (int) std::ceil (juce::GlyphArrangement::getStringWidth (fuente, t))
                   <= anchoDisponible;
    };

    juce::String linea = currentProject.isNotEmpty() ? currentProject.toUpperCase()
                                                     : T ("SIN GUARDAR");

    const juce::String sep = juce::String::fromUTF8 ("  \xc2\xb7  ");

    //  Los pads llenos, del mismo array que la tira de zatis de la banda de
    //  abajo: cero E/S y cero estado nuevo que mantener al dia.
    int llenos = 0;
    for (int i = 0; i < kNumPads; ++i)
        if (padHasSample[(size_t) i]) ++llenos;

    if (llenos > 0)
    {
        const auto pads = T ("%1 PADS|cont", Lang::ltr (juce::String (llenos)));
        if (cabe (linea + sep + pads)) linea += sep + pads;
    }

    //  De cuando es. Sin fecha valida no hay campo — «nunca se ha guardado» no
    //  es un hueco que rellenar con un cero, es un estado real: el trabajo esta
    //  en la sesion y vuelve entero igual.
    if (proyectoFecha.toMilliseconds() > 0)
    {
        const auto d = juce::Time::getCurrentTime() - proyectoFecha;
        const juce::String cuando =
              d.inHours() <  1.0 ? T ("AHORA|cont")
            : d.inDays()  <  1.0 ? T ("HACE %1 H|cont", Lang::ltr (juce::String ((int) d.inHours())))
                                 : T ("HACE %1 D|cont", Lang::ltr (juce::String ((int) d.inDays())));

        if (cabe (linea + sep + cuando)) linea += sep + cuando;
    }

    return linea;
}

void MainComponent::deleteProject (const juce::String& name)
{
    ProjectStore::folderFor (name).deleteRecursively();
    if (currentProject == name)
    {
        apuntaProyecto ({});
    }
    refreshProjectList();
    status.setText (T ("Borrado \"%1\"", name), juce::dontSendNotification);
    setSheet.repaint();
}

//  LA CANCION DE UN PROYECTO RECIEN NACIDO: el patron 1 en el primer hueco.
//
//  La pagina CANCION abria con los cuatro carriles vacios y la unica forma de
//  saber que se pinta con el dedo era leer el renglon del pie. Con un bloque
//  puesto la pagina se explica sola -esto es un bloque, ocupa un compas, se
//  arrastra- y ademas PLAY en modo cancion suena desde el primer toque en vez
//  de recorrer sesenta y cuatro compases mudos.
//
//  Y ocupa los compases que el patron mide, con las mismas dos lineas que usa
//  el pincel: un bloque de un compas escrito a mano donde el patron mide dos se
//  lee como un bloque y suena como medio.
void MainComponent::songPorDefecto()
{
    engine.clearSong();
    const int bars = juce::jmax (1, (engine.getPatternLength (0) + AudioEngine::kBarSteps - 1) / AudioEngine::kBarSteps);
    engine.setSongCell (0, 0, 1);
    for (int b = 1; b < bars && b < engine.getSongLength(); ++b)
        engine.setSongCell (0, b, AudioEngine::kContinued);
}

//  UN PAD RECIEN NACIDO, EN UN SOLO SITIO.
//
//  Los parametros de pad se ponian a su defecto en applyState -y solo para los
//  pads que el fichero trae- y en ningun sitio mas. Las dos consecuencias:
//
//   - NUEVO vaciaba muestra, nombre y envios y se dejaba pitch, ganancia, pan,
//     corte, reves, fundidos, mute/solo y los dos de envolvente donde los dejo
//     el proyecto de ayer. Cargar un sonido en el pad 03 despues de NUEVO podia
//     sonar al reves y filtrado a 200 Hz sin que nadie hubiera tocado nada.
//   - Y abrir un proyecto de dieciseis pads dejaba los pads 16..63 con la
//     ganancia, el pan y el filtro del proyecto anterior mientras clearMissing
//     les quitaba el audio: la mitad de un pad de otro proyecto.
//
//  Es la tercera vez que la misma clase de herencia aparece en esta funcion, y
//  la respuesta de la casa es siempre la misma: un defecto, un dueno.
void MainComponent::padPorDefecto (int i)
{
    if (! juce::isPositiveAndBelow (i, kNumPads)) return;
    const auto k = (size_t) i;

    padName[k]    = {};
    padDeFabrica[k] = false;
    padPitch[k]   = 0.0f;
    padCents[k]   = 0.0f;
    padKeepLen[k] = false;
    padGain[k]    = 0.85f;
    padStart01[k] = 0.0f;
    padEnd01[k]   = 1.0f;
    padLoop[k]    = false;
    //  Cierto por defecto, que es como se comporta cualquier otro pad: el que
    //  apila es la excepcion y hay que pedirla.
    padSelfCut[k] = true;
    padReverse[k] = false;
    padChokeUI[k] = 0;
    padPan[k]     = 0.0f;
    padAnchoUI[k] = 1.0f;
    padAttack[k]  = 2.0f;
    padRelease[k] = 5.0f;
    padCut[k]     = (float) AudioEngine::kFiltOpenHz;
    padReso[k]    = 0.0f;
    padFadeIn[k]  = 0.0f;
    padFadeOut[k] = 0.0f;
    padZati[k]    = Zati::forPad (i);

    engine.setPadPitch      (i, 0.0f);
    engine.setPadKeepLength (i, false);
    engine.setPadGain       (i, 0.85f);
    engine.setPadLoop       (i, false);
    engine.setPadSelfCut    (i, true);
    engine.setPadReverse    (i, false);
    engine.setPadChoke      (i, 0);
    engine.setPadPan        (i, 0.0f);
    engine.setPadAncho      (i, 1.0f);
    engine.setPadAttack     (i, 2.0f);
    engine.setPadRelease    (i, 5.0f);
    engine.setPadCutoff     (i, (float) AudioEngine::kFiltOpenHz);
    engine.setPadReso       (i, 0.0f);
    engine.setPadFadeIn     (i, 0.0f);
    engine.setPadFadeOut    (i, 0.0f);
    engine.setPadMute       (i, false);
    engine.setPadSolo       (i, false);
    //  El canal 0 y el recorte NEUTRO. El cero que hace que una mezcla se haga
    //  subiendo lo que quieres vive ahora en `canalSend`, que es su dueño; el
    //  recorte multiplica, asi que a cero aqui el pad no llegaria a ningun
    //  canal. Ver Tests/nuevo.py.
    engine.setPadCanal (i, 0);
    for (int f = 0; f < AudioEngine::kNumFx; ++f) engine.setPadRecorte (i, f, 1.0f);
}

void MainComponent::newProject()
{
    ponTransporte (false);

    for (int i = 0; i < kNumPads; ++i)
    {
        uiSample[(size_t) i] = nullptr;
        padHasSample[(size_t) i] = false;
        engine.clearPad (i);            // NUEVO has to empty the engine too
        //  Y TODO LO DEMAS DEL PAD. clearPad vacia la muestra y deja los
        //  parametros -y los seis envios- donde los dejo el proyecto anterior.
        //  Un estado que sobrevive a NUEVO no es un defecto: es una herencia, y
        //  ninguna de las dos puertas a un proyecto vacio puede dejar la mitad
        //  puesta. Ver padPorDefecto.
        padPorDefecto (i);
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

    //  Y LA CANCION TAMBIEN. NUEVO vaciaba los pads y los ocho patrones y se
    //  dejaba la linea de tiempo puesta: el proyecto siguiente nacia con el
    //  arreglo del anterior encima, bloques apuntando a patrones que ya no
    //  existen. Vaciar la mitad de un proyecto es peor que no vaciar nada,
    //  porque lo que queda parece tuyo.
    songPorDefecto();

    //  Y LA FILA DE EFECTOS SALE VACIA, que es la mitad de por que existen las
    //  ranuras. Una maquina recien abierta enseñaba seis efectos que nadie
    //  habia puesto: seis tapas encendidas de las que no sabes cuales vas a
    //  usar, con el mismo argumento que ya costo una medida en los envios -
    //  «una mezcla se hace subiendo lo que quieres, no apagando lo que no».
    //
    //  Se APAGA lo que estuviera sonando antes de vaciar: un efecto encendido
    //  cuya tapa desaparece sigue sonando y no hay donde tocarlo. `ponEnRanura`
    //  ya lo hace, y por eso se vacia con ella y no escribiendo el array.
    //  LAS DIECISEIS FILAS y no solo la del canal actual: vaciar la mitad de un
    //  proyecto es peor que no vaciar nada, porque lo que queda parece tuyo. Es
    //  la misma herencia que ya se pago dos veces aqui.
    {
        const int guarda = canalActual;
        for (int c = 0; c < kNumCanales; ++c)
        {
            canalActual = c;
            for (int s = 0; s < kNumRanuras; ++s) ponEnRanura (s, kSlotVacia);
        }
        canalActual = guarda;
    }

    //  Y LA MESA ENTERA con ellas: los envios a cero -que es como nace una
    //  mezcla-, el fader en uno y sin mute. Sin esto, el proyecto siguiente
    //  nacia con los envios y el volumen de canal del de ayer, que es
    //  exactamente lo que `Tests/nuevo.py` existe para cazar.
    for (int c = 0; c < kNumCanales; ++c)
    {
        for (int f = 0; f < kNumFx; ++f) engine.setCanalSend (c, f, 0.0f);
        engine.setCanalGain (c, 1.0f);
        engine.setCanalMute (c, false);
    }
    canalActual = 0;

    //  Y LA CURVA DEL EQ VUELVE A SU SITIO. Vaciar la ranura apaga el efecto y
    //  deja las cinco bandas donde estaban: el proyecto siguiente nacia con el
    //  ecualizador del anterior, y basta volver a poner el EQ para que suene.
    //  Es la misma herencia que ya se pago dos veces aqui con los envios y con
    //  la linea de tiempo.
    for (int b = 0; b < Eq5::kBands; ++b)
        engine.setEqBand (b, Eq5::kFreqDef[b], 0.0f);
    refrescaEq();

    //  Y LA AUTOMATIZACION, que es de la cancion y se va con ella: sin esto el
    //  proyecto siguiente nacia con los barridos del anterior escritos en unos
    //  compases donde ya no hay nada. Es la misma herencia que ya se pago con
    //  los envios y con la linea de tiempo.
    ponAutoArmado (false);
    vaciaAutomacion();

    selectedPattern = 0;
    selectedStep = -1;
    apuntaProyecto ({});

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
//  DOBLAR: la cancion entera otra vez detras de si misma.
//
//  Una cancion se construye repitiendo y variando. Sin esto, pasar de cuatro
//  compases a ocho es tocar treinta y dos celdas a mano, y por eso una pagina
//  de arreglo se abandona a los dos minutos.
//
//  Se copia el LARGO ACTUAL, no los compases escritos: si la cancion mide ocho
//  y solo los tres primeros tienen algo, lo que se repite son los ocho - los
//  cinco vacios incluidos - porque ese silencio es parte del arreglo y quitarlo
//  cambiaria donde cae todo lo que venga detras.
//
//  Y kContinued viaja con su patron. Una celda que dice "este compas lo sigue
//  cubriendo el patron que empezo antes" copiada sin el compas que lo empezo
//  seria un patron que continua sin haber empezado: el motor la leeria como
//  silencio y el arreglo saldria con agujeros. Como se copia el bloque entero y
//  en orden, el que empieza va siempre delante.
//  DESPLAZAR EL PATRON, con la vuelta puesta.
//
//  Un groove que entra un paso tarde no se arregla moviendo dieciseis celdas
//  a mano: se mueve el patron. Se desplaza TODO lo que lleva un paso -si
//  suena, con que nota, con que fuerza y cuantas veces- porque desplazar solo
//  el "suena" dejaria las notas y los golpes donde estaban y el patron
//  saldria mudo de matices en cuanto se toque una vez.
//
//  La vuelta es por el LARGO DEL PATRON y no por 64: en un patron de 12
//  pasos, lo que sale por el final tiene que volver a entrar por el paso 0 y
//  no por el 52, donde no lo ve nadie.
//  HUMANIZAR: escribir el temblor, no sortearlo al tocar.
//
//  Un temblor sorteado en el hilo de audio suena distinto cada vuelta - no es
//  un groove, es ruido - y no se puede deshacer, ni guardar, ni volver a oir
//  igual. Escrito en el patron se puede hacer las tres cosas, y ademas se ve:
//  la fuerza de cada golpe cambia y el paso lleva su empujon.
//
//  Se aplica a los pasos QUE SUENAN y solo a ellos: empujar un silencio no
//  hace nada y gastaria la mitad de la aleatoriedad en pasos que nadie oye.
//  Y desde una semilla FIJA por patron, para que dos toques seguidos den lo
//  mismo: "no me gusta como ha quedado" se arregla deshaciendo, y deshacer
//  algo que no se puede reproducir es media funcion.
//  COPIAR Y PEGAR LA FILA DE UN PAD.
//
//  Se lleva TODO lo que un paso lleva y no solo el "suena": la nota, la fuerza,
//  la repeticion, el largo, el empujon, el bloqueo del corte y el acorde. Una
//  copia que se deja la mitad sigue funcionando -algo se copia- hasta que un
//  dia el bombo pegado suena recto donde el original estaba humanizado, y eso
//  no se ve mirando que casillas estan encendidas.
void MainComponent::copiarFila()
{
    if (selectedPad < 0) return;
    const int b = selectedPattern, p = selectedPad;

    for (int st = 0; st < kNumSteps; ++st)
        filaPortapapeles[(size_t) st] = { pattern[(size_t) b][(size_t) st][(size_t) p],
                                          engine.getStepNote (b, st, p),
                                          engine.getStepVel  (b, st, p),
                                          engine.getStepRoll (b, st, p),
                                          engine.getStepLen  (b, st, p),
                                          engine.getStepNudge (b, st, p),
                                          engine.getStepLock (b, st, p),
                                          engine.getStepChordRaw (b, st, p),
                                          engine.getStepPLockRaw (b, st, p) };
    filaCopiada = true;
    pasteRowBtn.setEnabled (true);
    status.setText (T ("Fila del pad %1 copiada", juce::String (p + 1)), juce::dontSendNotification);
}

void MainComponent::pegarFila()
{
    if (selectedPad < 0 || ! filaCopiada) return;
    const int b = selectedPattern, p = selectedPad;

    pushUndo (T ("PEGAR FILA"));

    for (int st = 0; st < kNumSteps; ++st)
    {
        const auto& f = filaPortapapeles[(size_t) st];
        pattern[(size_t) b][(size_t) st][(size_t) p] = f.on;
        engine.setStep      (b, st, p, f.on);
        engine.setStepNote  (b, st, p, f.nota);
        engine.setStepVel   (b, st, p, f.vel);
        engine.setStepRoll  (b, st, p, f.roll);
        engine.setStepLen   (b, st, p, f.largo);
        engine.setStepNudge (b, st, p, f.empujon);
        engine.setStepLock  (b, st, p, f.corte);
        engine.setStepChordRaw (b, st, p, f.acorde);
        engine.setStepPLockRaw (b, st, p, f.bloqueos);
    }

    refreshStepGrid();
    refreshPiano (false);
    seqSheet.repaint();
    status.setText (T ("Fila pegada en el pad %1", juce::String (p + 1)), juce::dontSendNotification);
}

//  EUCLIDES: N golpes repartidos lo mas uniformemente posible en la fila.
//
//  Es lo que el proyecto anterior llama EUCLIDEAN y lo que en una caja de ritmos vale por
//  media hora de tocar celdas: casi todo lo que suena a clave, a afro o a
//  tresillo cabe en "cinco golpes en dieciseis" o "siete en doce", y a mano
//  cuesta contar y equivocarse.
//
//  El reparto se hace con la cuenta de Bresenham -el mismo truco que dibuja una
//  linea inclinada en pixeles-, que da exactamente el mismo resultado que el
//  algoritmo de Bjorklund para el caso que importa aqui y cabe en cuatro
//  lineas: un golpe donde el acumulador cambia de entero. Cinco en dieciseis
//  salen en 0 3 6 10 13, que es la clave de tresillo de siempre.
//
//  Reescribe la fila ENTERA del pad elegido, incluida la nota y la fuerza de
//  cada paso nuevo: dejar a medias los pasos viejos convertiria "cinco golpes"
//  en "cinco golpes y lo que hubiera", que no es lo que dice el mando.
void MainComponent::euclidesPattern (int golpes)
{
    if (selectedPad < 0) return;

    const int b = selectedPattern, p = selectedPad;
    const int len = engine.getPatternLength (b);
    const int n = juce::jlimit (0, len, golpes);

    pushUndo (T ("EUCLIDES"));

    for (int st = 0; st < len; ++st)
    {
        //  El golpe cae en el paso donde el acumulador PASA de entero, que es
        //  el borde de abajo: asi el primero cae siempre en el paso 0 y el
        //  patron empieza a tiempo.
        const bool on = n > 0 && (st * n) % len < n;
        pattern[(size_t) b][(size_t) st][(size_t) p] = on;
        engine.setStep (b, st, p, on);
        //  Y la fila ENTERA a su defecto, encendida o no. Se reescribian nota,
        //  fuerza, redoble y empujon y se quedaban el largo, el bloqueo, el
        //  acorde y los cuatro empaquetados del patron anterior: justo lo que
        //  el comentario de arriba dice que no puede pasar.
        engine.vaciaPaso (b, st, p);
    }

    refreshStepGrid();
    refreshPiano (false);
    seqSheet.repaint();
    status.setText (n > 0 ? T ("%1 golpes repartidos en %2 pasos",
                               juce::String (n), juce::String (len))
                          : T ("Fila vacia"),
                    juce::dontSendNotification);
}

void MainComponent::humanizePattern()
{
    const int len = engine.getPatternLength (selectedPattern);
    pushUndo (T ("HUMANIZAR"));

    juce::Random r (selectedPattern * 7919 + len * 31 + 1);
    int tocados = 0;

    for (int st = 0; st < len; ++st)
        for (int p = 0; p < kNumPads; ++p)
        {
            if (! pattern[(size_t) selectedPattern][(size_t) st][(size_t) p]) continue;

            //  Doce centesimas de paso a cada lado. A 120 BPM en semicorcheas
            //  un paso son 125 ms, asi que son unos 15 ms: lo que separa a un
            //  baterista de un metronomo. El doble ya no se lee como groove,
            //  se lee como que el patron esta mal escrito.
            engine.setStepNudge (selectedPattern, st, p, r.nextInt ({ -12, 13 }));

            //  Y la fuerza, que es la otra mitad de lo humano: entre el 78 % y
            //  el 100 % de lo que tuviera. Solo hacia ABAJO - subirla mete
            //  golpes por encima de lo que la persona puso, y el margen del
            //  master no es nuestro para gastarlo.
            const int v0 = engine.getStepVel (selectedPattern, st, p);
            const int base = v0 <= 0 ? 127 : v0;
            engine.setStepVel (selectedPattern, st, p,
                               juce::jlimit (1, 127, (int) std::lround (base * (0.78 + 0.22 * r.nextDouble()))));
            ++tocados;
        }

    refreshStepGrid();
    seqSheet.repaint();
    status.setText (T ("Humanizados %1 golpes", juce::String (tocados)),
                    juce::dontSendNotification);
}

void MainComponent::rotatePattern (int by)
{
    const int len = engine.getPatternLength (selectedPattern);
    if (len <= 1) return;

    pushUndo (T ("DESPLAZAR"));

    struct Paso { bool on; int nota, vel, roll; };
    std::vector<Paso> copia ((size_t) len * (size_t) kNumPads);

    for (int st = 0; st < len; ++st)
        for (int p = 0; p < kNumPads; ++p)
            copia[(size_t) (st * kNumPads + p)] =
                { pattern[(size_t) selectedPattern][(size_t) st][(size_t) p],
                  engine.getStepNote (selectedPattern, st, p),
                  engine.getStepVel  (selectedPattern, st, p),
                  engine.getStepRoll (selectedPattern, st, p) };

    for (int st = 0; st < len; ++st)
    {
        const int src = ((st - by) % len + len) % len;
        for (int p = 0; p < kNumPads; ++p)
        {
            const auto& s = copia[(size_t) (src * kNumPads + p)];
            pattern[(size_t) selectedPattern][(size_t) st][(size_t) p] = s.on;
            engine.setStep     (selectedPattern, st, p, s.on);
            engine.setStepNote (selectedPattern, st, p, s.nota);
            engine.setStepVel  (selectedPattern, st, p, s.vel);
            engine.setStepRoll (selectedPattern, st, p, s.roll);
        }
    }

    refreshStepGrid();
    seqSheet.repaint();
    status.setText (by > 0 ? T ("Patron un paso a la derecha")
                           : T ("Patron un paso a la izquierda"),
                    juce::dontSendNotification);
}

//  DOBLAR EL PATRON: copiarlo detras de si mismo y duplicar el largo.
//
//  Subir LARGO de 16 a 32 daba dieciseis pasos escritos y dieciseis mudos, y
//  la variacion que uno quiere -la misma vuelta con un remate distinto al
//  final- empezaba por volver a escribir la primera mitad. Esto la escribe.
void MainComponent::doublePattern()
{
    const int len = engine.getPatternLength (selectedPattern);
    if (len * 2 > AudioEngine::kMaxPatLen)
    {
        status.setText (T ("El patron ya no cabe doblado"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("DOBLAR"));

    for (int st = 0; st < len; ++st)
        for (int p = 0; p < kNumPads; ++p)
        {
            const bool on = pattern[(size_t) selectedPattern][(size_t) st][(size_t) p];
            pattern[(size_t) selectedPattern][(size_t) (len + st)][(size_t) p] = on;
            engine.setStep     (selectedPattern, len + st, p, on);
            engine.setStepNote (selectedPattern, len + st, p, engine.getStepNote (selectedPattern, st, p));
            engine.setStepVel  (selectedPattern, len + st, p, engine.getStepVel  (selectedPattern, st, p));
            engine.setStepRoll (selectedPattern, len + st, p, engine.getStepRoll (selectedPattern, st, p));
        }

    engine.setPatternLength (selectedPattern, len * 2);
    lengthSlider.setValue (len * 2, juce::dontSendNotification);
    resized();
    refreshStepGrid();
    seqSheet.repaint();
    status.setText (T ("Patron doblado a %1 pasos", juce::String (len * 2)),
                    juce::dontSendNotification);
}

void MainComponent::doubleSong()
{
    const int len = engine.getSongLength();
    if (len < 1 || len * 2 > AudioEngine::kSongBars)
    {
        status.setText (T ("La cancion ya no cabe doblada"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("DOBLAR"));

    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
        for (int b = 0; b < len; ++b)
            engine.setSongCell (lane, len + b, engine.getSongCell (lane, b));

    engine.setSongLength (len * 2);
    songLenSlider.setValue (len * 2, juce::dontSendNotification);
    refreshSong();
    status.setText (T ("Cancion doblada a %1 compases", juce::String (len * 2)),
                    juce::dontSendNotification);
}

//  Y LO QUE MUEVE LA LINEA DE TIEMPO MUEVE LAS DOS COSAS, que es literalmente
//  «para que se puedan cuadrar mejor».
//
//  INSERTAR y QUITAR corren las celdas de patron de los cuatro carriles y
//  dejaban los clips donde estaban: meter un compas desincronizaba el audio del
//  arreglo — la toma de voz sonando un compas antes de la parte que acompaña, y
//  sin que nada fallara. Es exactamente el fallo que esta ficha existe para no
//  tener, y solo aparecio cuando las dos vistas se fundieron en una: con la
//  banda de audio aparte, «el arreglo» y «el audio» eran dos paginas y nadie
//  esperaba que una herramienta de la primera tocara la segunda.
//
//  Se corre lo que empieza EN el compas o despues, que es la misma frontera que
//  usa el bucle de celdas de arriba, y lo que se saldria de la cancion se queda
//  en el ultimo compas en vez de perderse: un clip que desaparece al meter un
//  compas es trabajo que no se puede deshacer mirando la pantalla.
void MainComponent::corredClips (int desdeCompas, int delta)
{
    if (delta == 0 || clips.empty()) return;
    bool tocado = false;
    for (auto& c : clips)
    {
        if (c.compas < desdeCompas) continue;
        const int nuevo = juce::jlimit (0, AudioEngine::kSongBars - 1, c.compas + delta);
        if (nuevo == c.compas) continue;
        c.compas = nuevo;
        tocado = true;
    }
    if (! tocado) return;
    publicaClips();
}

//  METER UN COMPAS DONDE FALTA.
//
//  Todo lo que va detras del cursor se corre un compas a la derecha, en los
//  cuatro carriles, y el cursor queda vacio. Es la operacion que no se puede
//  hacer a mano: en una cancion de treinta y dos compases son ciento
//  veintiocho celdas que recolocar, y a la tercera se abandona la pagina.
//
//  Se recorre DESDE EL FINAL, que es la unica forma de correr algo dentro de
//  si mismo sin machacar lo que aun no se ha leido.
void MainComponent::insertSongBar()
{
    const int len = engine.getSongLength();
    if (len >= AudioEngine::kSongBars)
    {
        status.setText (T ("La cancion ya esta en su maximo"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("INSERTAR"));

    const int at = juce::jlimit (0, len - 1, songCursor);
    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
    {
        for (int b = len; b > at; --b)
            engine.setSongCell (lane, b, engine.getSongCell (lane, b - 1));
        engine.setSongCell (lane, at, 0);
    }

    //  Y la COLA de un bloque que quedaba partida por el compas nuevo deja de
    //  ser cola: un patron de cuatro compases con un hueco metido en medio no
    //  es un patron de cinco, es uno de cuatro que ya no empieza donde decia.
    //  Se corta ahi, que es lo unico que puede significar meter un silencio
    //  dentro de un bloque.
    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
        for (int b = at + 1; b < len + 1; ++b)
        {
            if (engine.getSongCell (lane, b) != AudioEngine::kContinued) break;
            engine.setSongCell (lane, b, 0);
        }

    corredClips (at, +1);

    engine.setSongLength (len + 1);
    songLenSlider.setValue (len + 1, juce::dontSendNotification);
    resized();
    refreshSong();
    status.setText (T ("Compas metido en %1", juce::String (at + 1)), juce::dontSendNotification);
}

//  ...y quitar el que sobra, que es la misma operacion al reves.
void MainComponent::removeSongBar()
{
    const int len = engine.getSongLength();
    if (len <= 1)
    {
        status.setText (T ("Una cancion no puede quedarse sin compases"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("QUITAR"));

    const int at = juce::jlimit (0, len - 1, songCursor);
    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
    {
        for (int b = at; b < len - 1; ++b)
            engine.setSongCell (lane, b, engine.getSongCell (lane, b + 1));
        engine.setSongCell (lane, len - 1, 0);

        //  Si lo que queda en el cursor es una COLA, se quedo sin cabeza: el
        //  compas que la empezaba es el que acabamos de quitar. Una cola
        //  huerfana se pinta como parte de un bloque que ya no existe.
        if (engine.getSongCell (lane, at) == AudioEngine::kContinued)
            for (int b = at; b < len - 1; ++b)
            {
                if (engine.getSongCell (lane, b) != AudioEngine::kContinued) break;
                engine.setSongCell (lane, b, 0);
            }
    }

    //  Y los clips van detras, igual que en INSERTAR: los que empiezan DESPUES
    //  del compas que se va se corren uno a la izquierda. El que empezaba en el
    //  compas quitado se queda donde estaba, que es lo que hace la celda de
    //  patron de al lado — lo que ocupaba ese compas pasa a ocupar el siguiente.
    corredClips (at + 1, -1);

    engine.setSongLength (len - 1);
    songLenSlider.setValue (len - 1, juce::dontSendNotification);
    songCursor = juce::jlimit (0, len - 2, songCursor);
    resized();
    refreshSong();
    status.setText (T ("Compas %1 quitado", juce::String (at + 1)), juce::dontSendNotification);
}

//  COPIAR Y PEGAR UN COMPAS, con sus cuatro carriles. Es como se repite un
//  trozo que funciona sin volver a colocarlo, y con PEGAR repetido se monta
//  una seccion entera en cuatro toques.
void MainComponent::copySongBar()
{
    const int at = juce::jlimit (0, engine.getSongLength() - 1, songCursor);
    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
        songClip[lane] = engine.getSongCell (lane, at);
    songClipLleno = true;
    refreshSong();
    status.setText (T ("Compas %1 copiado", juce::String (at + 1)), juce::dontSendNotification);
}

void MainComponent::pasteSongBar()
{
    if (! songClipLleno)
    {
        status.setText (T ("No hay ningun compas copiado"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("PEGAR"));

    const int at = juce::jlimit (0, engine.getSongLength() - 1, songCursor);
    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
    {
        //  Una COLA copiada se pega como hueco: pegarla tal cual pondria la
        //  continuacion de un bloque en un sitio donde ese bloque no empieza,
        //  y la rejilla la pintaria buscando hacia atras una cabeza que no
        //  esta - un bloque que aparece de la nada.
        const int v = songClip[lane];
        engine.setSongCell (lane, at, v == AudioEngine::kContinued ? 0 : v);
    }

    refreshSong();
    status.setText (T ("Pegado en el compas %1", juce::String (at + 1)), juce::dontSendNotification);
}

//  REORDENAR: intercambiar el compas marcado con el de al lado.
//
//  Es la operacion que no se puede improvisar con las otras. Con copiar,
//  pegar y quitar se llega al mismo sitio en cuatro pasos y dejando el
//  original detras, que es como se pierde un compas sin enterarse. Y el
//  cursor se va CON el compas: si se quedara quieto, mover dos veces movería
//  dos compases distintos en vez de llevar el mismo dos sitios.
void MainComponent::moveSongBar (int dir)
{
    const int len = engine.getSongLength();
    const int a = juce::jlimit (0, len - 1, songCursor);
    const int b = a + dir;

    if (b < 0 || b >= len)
    {
        status.setText (T ("El compas ya esta en el borde"), juce::dontSendNotification);
        return;
    }

    pushUndo (T ("MOVER"));

    for (int lane = 0; lane < AudioEngine::kSongLanes; ++lane)
    {
        //  Una COLA que cambia de sitio deja de ser cola: la cabeza que la
        //  explicaba se queda donde estaba. Se convierte en hueco por el mismo
        //  motivo que al pegar - un bloque que aparece de la nada es peor que
        //  un silencio.
        auto limpia = [] (int v) { return v == AudioEngine::kContinued ? 0 : v; };
        const int va = limpia (engine.getSongCell (lane, a));
        const int vb = limpia (engine.getSongCell (lane, b));
        engine.setSongCell (lane, a, vb);
        engine.setSongCell (lane, b, va);
    }

    songCursor = b;
    //  Y LA VENTANA SE ACERCA AL COMPAS, en vez de saltar a su pagina: con la
    //  ventana continua «la pagina del compas b» ya no existe, y lo que hace
    //  falta es que b se vea. Si ya se ve, no se mueve nada - desplazar por
    //  desplazar es lo que hace que se pierda de vista lo que estabas mirando.
    songPrimerCompas = acercaCompas (b);

    refreshSong();
    status.setText (T ("Compas movido al %1", juce::String (b + 1)), juce::dontSendNotification);
}

//  RECORTAR O ALARGAR UN BLOQUE.
//
//  La longitud de un bloque es cuantos compases ocupa: el suyo mas la cola de
//  continuaciones. Acortarlo tira la cola sobrante; alargarlo solo se come
//  compases VACIOS - comerse el bloque de al lado seria borrar algo que
//  nadie ha pedido borrar, y para eso ya esta la goma.
void MainComponent::resizeSongBlock (int dir)
{
    const int len = engine.getSongLength();
    const int at  = juce::jlimit (0, len - 1, songCursor);

    //  La cabeza del bloque: hacia atras hasta que deje de ser continuacion.
    int carril = -1, cabeza = -1;
    for (int ln = 0; ln < AudioEngine::kSongLanes; ++ln)
    {
        int b = at;
        while (b > 0 && engine.getSongCell (ln, b) == AudioEngine::kContinued) --b;
        const int v = engine.getSongCell (ln, b);
        if (v > 0 && v != AudioEngine::kContinued) { carril = ln; cabeza = b; break; }
    }

    if (carril < 0)
    {
        status.setText (T ("No hay ningun bloque en este compas"), juce::dontSendNotification);
        return;
    }

    int largo = 1;
    while (cabeza + largo < len
           && engine.getSongCell (carril, cabeza + largo) == AudioEngine::kContinued) ++largo;

    ponLargoBloque (carril, cabeza, largo + dir);
}

//  EL LARGO DE UN BLOQUE LO ESCRIBE UNA FUNCION.
//
//  La regla entera vivia dentro de `resizeSongBlock`, que es quien la llamaba
//  desde las tapas ACORTAR y ALARGAR; en cuanto el gesto del filo fue un
//  segundo cliente, dejarla ahi habria sido la misma cuenta escrita dos veces
//  -«alargar solo se come compases VACIOS», el tope de la cancion, el
//  `pushUndo`- y la que se quedara vieja seria un bloque que se come al vecino
//  desde un camino y no desde el otro. Es la extraccion de `normaliza` fuera de
//  `render` con otra pieza.
//
//  Y UNA SOLA ENTRADA DE DESHACER POR GESTO: un arrastre emite un evento por
//  movimiento, asi que apilar uno por evento no es deshacer, es contar. Se
//  apunta solo cuando el largo de verdad cambia, y quien arrastra vuelve a
//  llamar con el mismo numero mientras el dedo no cruce a otra celda.
void MainComponent::ponLargoBloque (int carril, int cabeza, int nuevo, bool apunta)
{
    const int len = engine.getSongLength();
    if (carril < 0 || carril >= AudioEngine::kSongLanes || cabeza < 0 || cabeza >= len) return;

    int largo = 1;
    while (cabeza + largo < len
           && engine.getSongCell (carril, cabeza + largo) == AudioEngine::kContinued) ++largo;

    if (nuevo == largo) return;
    if (nuevo < 1 || cabeza + nuevo > len)
    {
        status.setText (T ("El bloque no puede medir eso"), juce::dontSendNotification);
        return;
    }
    //  Alargar solo se come compases VACIOS: comerse el bloque de al lado seria
    //  borrar algo que nadie ha pedido borrar, y para eso ya esta la goma. Con
    //  el gesto hace falta mirar TODOS los que se van a ocupar y no solo el
    //  siguiente - un dedo salta varias celdas de una vez donde una tapa avanza
    //  de uno en uno.
    for (int b = cabeza + largo; b < cabeza + nuevo; ++b)
        if (engine.getSongCell (carril, b) != 0)
        {
            status.setText (T ("El compas siguiente ya esta ocupado"), juce::dontSendNotification);
            return;
        }

    if (apunta) pushUndo (T ("LARGO"));

    for (int b = cabeza + 1; b < cabeza + nuevo; ++b)
        engine.setSongCell (carril, b, AudioEngine::kContinued);
    for (int b = cabeza + nuevo; b < cabeza + largo; ++b)
        engine.setSongCell (carril, b, 0);

    songCursor = cabeza;
    refreshSong();
    status.setText (T ("Bloque de %1 compases", juce::String (nuevo)), juce::dontSendNotification);
}

//  EL BUCLE DEL TRAMO QUE SE ESTA MIRANDO.
//
//  No pide un rango porque no hace falta pedirlo: la rejilla ya pagina de
//  ocho en ocho, y el tramo que quieres repetir es casi siempre el que tienes
//  delante. Una segunda pulsacion lo quita.
void MainComponent::toggleSongLoop()
{
    const int len  = engine.getSongLength();
    const int a    = songPrimerCompas;
    const int b    = juce::jmin (len, a + songGrid.getCompasesVista());

    if (engine.hasSongLoop() && engine.getSongLoopFrom() == a && engine.getSongLoopTo() == b)
    {
        engine.clearSongLoop();
        songLoopBtn.setToggleState (false, juce::dontSendNotification);
        status.setText (T ("Bucle quitado"), juce::dontSendNotification);
    }
    else if (b > a)
    {
        engine.setSongLoop (a, b);
        songLoopBtn.setToggleState (true, juce::dontSendNotification);
        status.setText (T ("Bucle en los compases %1 a %2",
                           juce::String (a + 1), juce::String (b)),
                        juce::dontSendNotification);
    }

    refreshSong();
}

//  SILENCIAR UN CARRIL, desde su canaleta. Ver Playlist::mouseDown.
void MainComponent::toggleSongLane (int lane)
{
    const bool nuevo = ! engine.isSongLaneMuted (lane);
    engine.setSongLaneMute (lane, nuevo);
    refreshSong();
    status.setText (nuevo ? T ("Carril %1 en silencio", juce::String (lane + 1))
                          : T ("Carril %1 suena", juce::String (lane + 1)),
                    juce::dontSendNotification);
}

//  EL PIANO ROLL ES DE UN PAD, ASI QUE HAY QUE PODER CAMBIAR DE PAD.
//
//  Las notas ya eran por pad - viven en (patron, paso, pad) dentro del motor -
//  pero la unica puerta estaba en la pagina del pad, y la ficha tapa la rejilla
//  de pads: escribir un bajo y luego una campana costaba cerrar, elegir y
//  volver a abrir. Con esto la ficha se queda abierta y el pad cambia debajo.
//
//  Y salta los VACIOS. En un kit de cinco sonidos cargados, avanzar de uno en
//  uno por sesenta y cuatro huecos es no tener el boton: se recorren los 63
//  desplazamientos en orden y se para en el primero que tenga muestra. Si no
//  hay ninguno cargado - proyecto recien abierto - se mueve uno y ya, que es
//  mejor que no responder.
void MainComponent::pianoStepPad (int dir)
{
    if (selectedPad < 0) return;

    int destino = -1;
    for (int k = 1; k < kNumPads && destino < 0; ++k)
    {
        const int c = (selectedPad + dir * k + kNumPads * 2) % kNumPads;
        if (padHasSample[(size_t) c]) destino = c;
    }
    if (destino < 0) destino = (selectedPad + dir + kNumPads) % kNumPads;

    //  El banco va DELANTE: selectBank vuelve a elegir pad - la misma casilla
    //  del banco nuevo - asi que llamarlo despues borraria el destino.
    const int banco = destino / kPadsPerBank;
    if (banco != currentBank) selectBank (banco);
    selectPad (destino);            // y selectPad ya refresca el piano abierto
    refreshStepGrid();
}

//  PONER Y QUITAR UNA NOTA.
//
//  El conjunto de notas de un paso es la RAIZ mas hasta tres del acorde, y la
//  raiz es la que el resto de la app ya conoce - la que mueve el mando NOTA de
//  la pagina PASO y la que se guarda en el proyecto. Asi que quitar la raiz no
//  puede dejar el acorde huerfano: asciende la primera de las extras.
//  ------------------------------------------------------------------------
//  LA SELECCION DEL PIANO
//  ------------------------------------------------------------------------

//  Poner o quitar una nota SIN alternar, que es lo que mover en bloque
//  necesita: `pianoCellToggled` alterna, y mover doce notas alternando
//  apagaria las que caigan encima de otra en vez de ponerlas.
//
//  Devuelve el largo que habia, porque moverse dejandose el largo es la misma
//  leccion de `copiarFila` contada en el piano: una nota son (semitono, largo),
//  y la mitad no es la nota.
int MainComponent::pianoEscribe (int paso, int semi, bool poner, int cuartos)
{
    if (selectedPad < 0 || paso < 0 || paso >= engine.getPatternLength (selectedPattern)) return 0;
    const int b = selectedPattern, p = selectedPad;

    juce::Array<int> notas;
    if (pattern[(size_t) b][(size_t) paso][(size_t) p]) notas.add (engine.getStepNote (b, paso, p));
    for (int e = 0; e < AudioEngine::kExtraNotes; ++e)
    {
        const int v = engine.getStepExtra (b, paso, p, e);
        if (v != -128) notas.addIfNotAlreadyThere (v);
    }

    const int largoAntes = engine.getStepLen (b, paso, p);

    if (poner)
    {
        if (! notas.contains (semi))
        {
            if (notas.size() >= PianoRoll::kMaxNotas) return largoAntes;
            notas.add (semi);
        }
    }
    else
    {
        notas.removeAllInstancesOf (semi);
    }
    notas.sort();

    const bool queda = ! notas.isEmpty();
    pattern[(size_t) b][(size_t) paso][(size_t) p] = queda;
    engine.setStep (b, paso, p, queda);
    engine.setStepNote (b, paso, p, queda ? notas[0] : 0);
    engine.clearStepExtras (b, paso, p);
    for (int e = 0; e + 1 < notas.size() && e < AudioEngine::kExtraNotes; ++e)
        engine.setStepExtra (b, paso, p, e, notas[e + 1], true);

    //  El largo es del PASO y no de la nota -asi lo guarda el motor- asi que
    //  solo se escribe al poner, y solo si el que llama trae uno: al quitar se
    //  deja como esta, que el paso puede seguir teniendo otras notas.
    if (poner && cuartos > 0) engine.setStepLen (b, paso, p, cuartos);
    else if (! queda)         engine.setStepLen (b, paso, p, AudioEngine::kLenSuelto);

    return largoAntes;
}

//  LA BANDA ELASTICA. Llega en (paso, semi) porque quien la dibuja no tiene por
//  que saber cuanto mide una celda, y aqui se resuelve contra lo que hay
//  ESCRITO: seleccionar huecos no significa nada.
void MainComponent::pianoBanda (int paso0, int semi0, int paso1, int semi1)
{
    const int b = selectedPattern, p = juce::jmax (0, selectedPad);
    const int pa = juce::jmin (paso0, paso1), pb = juce::jmax (paso0, paso1);
    const int sa = juce::jmin (semi0, semi1), sb = juce::jmax (semi0, semi1);
    const int base = seqPrimerPaso;
    const int len  = engine.getPatternLength (b);

    pianoSel.clear();
    for (int c = pa; c <= pb; ++c)
    {
        const int st = base + c;
        if (st < 0 || st >= len) continue;
        if (! pattern[(size_t) b][(size_t) st][(size_t) p]) continue;

        auto mira = [&] (int v)
        {
            if (v != -128 && v >= sa && v <= sb) pianoSel.push_back ({ c, v });
        };
        mira (engine.getStepNote (b, st, p));
        for (int e = 0; e < AudioEngine::kExtraNotes; ++e)
            mira (engine.getStepExtra (b, st, p, e));
    }
    refreshPiano (false);
}

void MainComponent::pianoVaciaSel()
{
    if (pianoSel.empty()) return;
    pianoSel.clear();
    refreshPiano (false);
}

//  MOVER EL BLOQUE. Se QUITAN TODAS PRIMERO y se ponen despues, que es el orden
//  y no un detalle: una nota que se mueve encima de otra del mismo bloque se
//  comeria a su hermana antes de que a esa le tocara moverse, y el sintoma
//  seria «al mover un acorde se pierde una nota».
//
//  Y UN SOLO pushUndo para el bloque entero: deshacer un movimiento de doce
//  notas doce veces no es deshacer, es contar.
void MainComponent::pianoMueveSel (int dPaso, int dSemi)
{
    if (pianoSel.empty() || (dPaso == 0 && dSemi == 0)) return;

    const int b = selectedPattern;
    const int base = seqPrimerPaso;
    const int len  = engine.getPatternLength (b);

    //  Lo que no cabe no se mueve, y entonces NO se mueve nada: mover medio
    //  bloque contra el borde lo deforma, y deformar no es lo que se pidio.
    for (const auto& n : pianoSel)
    {
        const int np = n.paso + dPaso, ns = n.semi + dSemi;
        if (np < 0 || base + np >= len) return;
        if (ns < -24 || ns > 24) return;
    }

    if (! moviendoSel) { pushUndo (T ("MOVER")); moviendoSel = true; }

    //  Con su LARGO, que se lee antes de quitar nada.
    std::vector<int> largos;
    largos.reserve (pianoSel.size());
    for (const auto& n : pianoSel)
        largos.push_back (engine.getStepLen (b, base + n.paso, juce::jmax (0, selectedPad)));

    for (const auto& n : pianoSel) pianoEscribe (base + n.paso, n.semi, false, 0);

    for (size_t i = 0; i < pianoSel.size(); ++i)
    {
        pianoSel[i].paso += dPaso;
        pianoSel[i].semi += dSemi;
        pianoEscribe (base + pianoSel[i].paso, pianoSel[i].semi, true, largos[i]);
    }
    refreshPiano (false);
}

//  COPIAR, en coordenadas RELATIVAS a la esquina de arriba a la izquierda: sin
//  eso, pegar caeria donde se copio y no donde se toca, que es la mitad de para
//  lo que se copia.
void MainComponent::pianoCopiaSel()
{
    if (pianoSel.empty()) return;
    const int b = selectedPattern, p = juce::jmax (0, selectedPad);
    const int base = seqPrimerPaso;

    int p0 = pianoSel[0].paso;
    for (const auto& n : pianoSel) p0 = juce::jmin (p0, n.paso);

    pianoPortapapeles.clear();
    for (const auto& n : pianoSel)
        pianoPortapapeles.push_back ({ n.paso - p0, n.semi,
                                       engine.getStepLen (b, base + n.paso, p) });

    status.setText (T ("%1 notas copiadas", juce::String ((int) pianoPortapapeles.size())),
                    juce::dontSendNotification);
    resized();      // PEGAR aparece
}

//  PEGAR EN EL COMPAS QUE SE VE, desde su primera columna. Lo que se saldria del
//  patron NO se pega, y no se recorta a la fuerza: una figura recortada suena
//  como otra cosa, y «pegó» lo cumple igual algo que deja media.
void MainComponent::pianoPegaSel()
{
    if (pianoPortapapeles.empty()) return;
    const int b = selectedPattern;
    const int base = seqPrimerPaso;
    const int len  = engine.getPatternLength (b);

    pushUndo (T ("PEGAR"));
    pianoSel.clear();
    for (const auto& n : pianoPortapapeles)
    {
        const int st = base + n.dPaso;
        if (st < 0 || st >= len) continue;
        if (n.semi < -24 || n.semi > 24) continue;
        pianoEscribe (st, n.semi, true, n.cuartos);
        pianoSel.push_back ({ n.dPaso, n.semi });
    }
    refreshPiano (false);
}

void MainComponent::pianoCellToggled (int paso, int semi)
{
    if (selectedPad < 0 || paso < 0 || paso >= engine.getPatternLength (selectedPattern)) return;

    const int b = selectedPattern, p = selectedPad;
    const bool sonando = pattern[(size_t) b][(size_t) paso][(size_t) p];

    //  Lo que hay ahora: raiz mas extras, en una lista corta.
    juce::Array<int> notas;
    if (sonando) notas.add (engine.getStepNote (b, paso, p));
    for (int e = 0; e < AudioEngine::kExtraNotes; ++e)
    {
        const int v = engine.getStepExtra (b, paso, p, e);
        if (v != -128) notas.addIfNotAlreadyThere (v);
    }

    //  EL LAPIZ ESCRIBE Y NO ALTERNA, que es la goma por el otro lado y por el
    //  mismo motivo escrito ahi: arrastrar sobre una fila con notas y huecos
    //  apagaba las notas y encendia los huecos, o sea que pintar encima de lo
    //  que ya hay lo BORRA. Con el lapiz armado, una casilla que ya suena se
    //  queda como esta.
    if (notas.contains (semi))
    {
        if (pianoGrid.getHerramienta() == PianoRoll::lapiz) return;
        notas.removeAllInstancesOf (semi);
    }
    else if (notas.size() < PianoRoll::kMaxNotas) notas.add (semi);
    else
    {
        //  Cuatro es el tope del motor. Decirlo es mejor que tragarse el toque
        //  en silencio, que se lee como que la rejilla no responde.
        status.setText (T ("Un paso admite %1 notas", juce::String (PianoRoll::kMaxNotas)),
                        juce::dontSendNotification);
        return;
    }

    notas.sort();

    const bool quedaAlgo = ! notas.isEmpty();
    pattern[(size_t) b][(size_t) paso][(size_t) p] = quedaAlgo;
    engine.setStep (b, paso, p, quedaAlgo);
    engine.setStepNote (b, paso, p, quedaAlgo ? notas[0] : 0);
    engine.clearStepExtras (b, paso, p);
    for (int e = 0; e + 1 < notas.size() && e < AudioEngine::kExtraNotes; ++e)
        engine.setStepExtra (b, paso, p, e, notas[e + 1], true);

    selectedStep = paso;
    refreshPiano();
    refreshStepGrid();
}

void MainComponent::refreshPiano (bool repintarTarjeta)
{
    const int b = selectedPattern, p = juce::jmax (0, selectedPad);
    const int len = engine.getPatternLength (b);

    //  El compas que se mira lo decide SEGUIR, y en esta pagina tambien: ver
    //  seguirCompas. ANTES de calcular la base, que es de donde sale.
    if (engine.isPlaying() && engine.getPlayingPattern() == b)
        seguirCompas (engine.getPlayStep());

    //  CUANTAS COLUMNAS SE VEN, y acotado aqui por lo mismo que el primer
    //  paso: esta funcion la llama el temporizador treinta veces por segundo y
    //  `pianoCols` lo mueve una tapa. Con 32 en un patron de 16 pasos, `base +
    //  c` se sale de la tabla - que es exactamente el cuarto de los cinco
    //  fallos del compas, escrito con otro numero.
    const int verCols = juce::jlimit (1, AudioEngine::kNumSteps, pianoCols);

    //  Y EL PRIMER PASO SE ACOTA AQUI Y NO SOLO EN resized(): la maqueta corre
    //  cuando le toca y esto treinta veces por segundo, asi que un patron que
    //  acaba de encoger -de 32 pasos a 16- deja la ventana apuntando fuera de
    //  la tabla.
    seqPrimerPaso = juce::jlimit (0, juce::jmax (0, len - verCols), seqPrimerPaso);

    const int base = seqPrimerPaso;
    const int cols = juce::jlimit (0, verCols, len - base);

    //  Y SE VACIAN LAS DIECISEIS, no solo las que se rellenan.
    //
    //  Las celdas se limpiaban DENTRO del bucle de columnas validas, asi que un
    //  compas corto -o un `base` que se salia- dejaba las de la derecha con lo
    //  que hubiera antes: notas dibujadas que no estan en el patron, que no se
    //  pueden borrar porque no existen, y que desaparecen solas al cambiar de
    //  pagina. Se lee como "la rejilla no responde".
    //
    //  Y el jmax(1, ...) de antes era peor que inutil: con `base` fuera del
    //  patron daba UNA columna en vez de ninguna, o sea que garantizaba que
    //  quince se quedaran viejas.
    for (int c = 0; c < verCols; ++c)
    {
        for (int k = 0; k < PianoRoll::kMaxNotas; ++k)
            pianoCells[c * PianoRoll::kMaxNotas + k] = -128;
        pianoLargos[c] = 0;
    }

    for (int c = 0; c < cols; ++c)
    {
        const int st = base + c;   // ya vaciadas arriba, las dieciseis

        pianoLargos[c] = (unsigned char) engine.getStepLen (b, st, p);

        int n = 0;
        if (pattern[(size_t) b][(size_t) st][(size_t) p])
            pianoCells[c * PianoRoll::kMaxNotas + n++] = (signed char) engine.getStepNote (b, st, p);
        for (int e = 0; e < AudioEngine::kExtraNotes && n < PianoRoll::kMaxNotas; ++e)
        {
            const int v = engine.getStepExtra (b, st, p, e);
            if (v != -128) pianoCells[c * PianoRoll::kMaxNotas + n++] = (signed char) v;
        }
    }

    const int ps = (engine.isPlaying() && engine.getPlayingPattern() == b)
                     ? engine.getPlayStep() - base : -1;

    //  Ver UiAudit::cabezalPiano: se cuenta cuando CAMBIA de columna, que es
    //  lo que separa una barra viva de una dibujada.
    {
        static int ultimo = -2;
        ++UiAudit::pianoTicks;
        if (ps >= 0 && ps != ultimo) ++UiAudit::cabezalPiano;
        ultimo = ps;
    }

    //  Y `base`, QUE ES EL PASO DE LA PRIMERA COLUMNA. Las notas ya llegaban
    //  recolocadas y el cabezal relativizado, asi que el piano tenia todo lo que
    //  se MUEVE y nada con lo que dibujar el fondo: su cuadricula se teñia por
    //  la columna. La rejilla de pasos lo recibe desde el dia que la ventana es
    //  continua.
    pianoGrid.setSource (pianoCells, cols, pianoBase,
                         (ps >= 0 && ps < cols) ? ps : -1,
                         padZati[(size_t) p],
                         ps >= 0 ? engine.getStepPhase() : 0.0f,
                         pianoLargos, base);

    //  LAS DOS BARRAS. La horizontal es la MISMA que la de la rejilla —una
    //  ventana, un dueño— y solo cambia cuantas columnas caben; la vertical es
    //  del tono, y su total son las cuarenta y nueve posiciones que `setStepNote`
    //  admite (-24..+24) contadas desde cero, que es lo unico que una barra sabe
    //  manejar.
    seqBarra.ponRango (base, verCols, juce::jmax (1, len));
    seqBarra.ponCabezal (engine.isPlaying() && engine.getPlayingPattern() == b
                             ? engine.getPlayStep() : -1);
    pianoBarra.ponRango (pianoBase + 24, pianoGrid.getFilas(), 49);

    //  El transporte de esta ficha es seqPlayBtn, que vive en la pagina de la
    //  rejilla: el piano tenia su propio PLAY y era una tapa que hacia lo mismo
    //  en dos paginas de la misma ficha.
    if (repintarTarjeta) seqSheet.repaint();
}


//  LAS DOS VISTAS DE LA LINEA DE TIEMPO. Mismo patron que showSeqPage, y no
//  uno nuevo: lo que se apaga se queda ADEMAS sin coordenadas, porque un
//  componente invisible que conserva sus limites sigue estando ahi para todo lo
//  que mida geometria.
//  ARMAR UNA DE LAS CUATRO HERRAMIENTAS.
//
//  La GOMA no es un modo aparte del pincel: ES la brocha VACIAR, que ya
//  existia. Armarla la pone y desarmarla devuelve el pincel al patron 1, asi
//  que borrar tiene UN dueno y ademas se ve armado - que es justo lo que la
//  tapa VACIAR no hacia: encendida y apagada se distinguian por un color, y el
//  dedo estaba en la rejilla y no en la tapa.
void MainComponent::ponHerramienta (int h)
{
    songHerramienta = juce::jlimit (0, (int) Playlist::hMute, h);
    songGrid.herramienta = songHerramienta;

    if (songHerramienta == Playlist::hGoma)      songBrush = 0;
    else if (songBrush == 0)                     songBrush = 1;

    for (int i = 0; i < songToolBtns.size() && i < kNumHerramientas; ++i)
        songToolBtns[i]->setToggleState (kHerramientas[i].id == songHerramienta,
                                         juce::dontSendNotification);

    //  EL MODO SE VE DONDE SE ACTUA y no donde se armo, que es la regla que ya
    //  gobierna SOLO en la cara y las cuatro del piano: el dedo esta en la
    //  rejilla, no en la tapa. Transparente es «el lapiz», o sea lo de siempre.
    songGrid.setModo (songHerramienta == Playlist::hLapiz
                        ? juce::Colours::transparentBlack
                        : ZatiColours::accent.withAlpha (0.85f));
    refreshSong();
}

//  QUE UN COMPAS SE VEA, moviendo la ventana lo MINIMO.
//
//  Con la ventana continua «la pagina del compas b» no existe, asi que lo que
//  se pide es que b caiga dentro y nada mas: si ya se ve, la ventana no se
//  mueve. Desplazar por desplazar es lo que hace que se pierda de vista lo que
//  estabas mirando, y es lo que hacia la fila de paginas cada vez que el
//  cursor cruzaba un multiplo de ocho.
int MainComponent::acercaCompas (int compas) const
{
    const int ven = songGrid.getCompasesVista();
    const int tope = juce::jmax (0, engine.getSongLength() - ven);
    int p = songPrimerCompas;
    if (compas <  p)       p = compas;
    if (compas >= p + ven) p = compas - ven + 1;
    return juce::jlimit (0, tope, p);
}

//  CUANTOS COMPASES SE VEN DE UNA VEZ.
//
//  Dos cosas que hay que hacer aqui y no en la tapa, porque las dos dependen de
//  la vista y no del gesto:
//
//  - El primer compas visible se CONSERVA en vez de ponerse a cero. El que
//    estabas mirando tiene que seguir en pantalla: saltar al principio cada
//    vez que se toca el zoom es lo que hace que un zoom no se use.
//  - Y se acota al final, porque al abrir la vista lo que era el ultimo compas
//    visible puede caer detras del final de la cancion.
void MainComponent::ponVistaCompases (int n)
{
    const int visible = songPrimerCompas;           //  el primer compas que se ve

    songGrid.setCompasesVista (n);
    const int ahora = songGrid.getCompasesVista();

    //  EL PRIMER COMPAS SE CONSERVA, no se re-deriva de una pagina. Con la
    //  ventana continua el que estabas mirando ES el primero, asi que basta
    //  con acotarlo al final: al abrir la vista, lo que era el ultimo compas
    //  visible puede caer detras del final de la cancion.
    songPrimerCompas = juce::jlimit (0, juce::jmax (0, engine.getSongLength() - ahora), visible);

    //  Una cifra dentro de una frase traducida es la unica clase de constante
    //  que no se puede contrastar leyendo el codigo de al lado, asi que se
    //  INTERPOLA - la misma decision que el subtitulo del manual - y la fila de
    //  la tabla es una sola. Clave propia: COMPAS ya significa otra cosa en la
    //  fila del secuenciador, y reaprovecharla por parecerse en espanol sale
    //  mal en las otras tres.
    songZoomBtn.setButtonText (T ("%1 COMPASES|zoom", juce::String (ahora)));

    resized();
    refreshSong();
}

//  GRABAR AL ARREGLO, que es la mitad que le faltaba a la banda de audio.
//
//  Hasta aqui un clip solo se podia poner desde un pad que YA tuviera sonido,
//  asi que «grabar una guitarra encima del arreglo» -que es exactamente lo que
//  separa un groovebox de un DAW- no se podia hacer. La unica puerta al audio
//  de la cancion era el fichero de proyecto.
//
//  Y NO HAY QUE ESCRIBIR UN GRABADOR: YA EXISTE. `toggleMicSampling` resuelve
//  el permiso de microfono, el setAudioChannels(2,2), el recorte de la latencia
//  de captura y la continua; `finishRecording` devuelve el buffer en el hilo de
//  mensajes. Lo unico que faltaba es EN QUE COMPAS EMPEZO, y eso lo apunta el
//  motor porque es el unico que sabe el instante exacto.
//
//  La toma entra ademas en un PAD, que es la puerta que esta maquina ya tiene
//  para el audio: asi queda tocable desde la rejilla sin inventar un segundo
//  sitio donde vive un sonido, y el clip la referencia como cualquier otro.
void MainComponent::grabaAlArreglo()
{
    if (grabandoAlArreglo)
    {
        //  Parar: la toma se cierra por el camino de siempre y ademas cae en la
        //  linea de tiempo, en el compas que el motor apunto al arrancarla.
        grabandoAlArreglo = false;
        const int compas = juce::jmax (0, engine.getCompasGrabado());
        engine.setPlaying (false);
        toggleMicSampling();                  // cierra la toma y la deja en el pad
        if (padHasSample[(size_t) recordingSlot])
        {
            const int guarda = selectedPad;
            selectedPad = recordingSlot;      // ponClip pone LO QUE SUENA en el pad elegido
            ponClip (pistaGrabacion, compas);
            selectedPad = guarda;
        }
        styleButton (songRecBtn, kKey);
        songRecBtn.setButtonText (T ("GRABAR"));
        refreshSong (true);
        return;
    }

    //  Empezar. El destino sale del BANCO DE TOMAS y no de «el primero vacio»,
    //  que en una maquina de fabrica no existe: los sesenta y cuatro vienen
    //  llenos, asi que la caida que habia aqui se comia el pad elegido -el 01-
    //  en cada toma. Ver padParaToma.
    const int slot = padParaToma();
    if (slot < 0)
    {
        //  Y NO SE GRABA. Pisar la toma anterior seria ademas llevarse el audio
        //  de su clip, que ya esta puesto en la linea de tiempo.
        status.setText (T ("El banco de tomas esta lleno: vacia un pad o elige otro"),
                        juce::dontSendNotification);
        return;
    }
    recordingSlot = slot;

    //  El clic y la cuenta salen de la preferencia. Ver armaCuentaSiToca.

    using RP = juce::RuntimePermissions;
    auto arranca = [this, slot]
    {
        recordingActive = true;               // el camino de PARAR es el del micro
        grabandoAlArreglo = true;
        setAudioChannels (2, 2);
        useLowestLatency();                   // ver toggleMicSampling
        aplicaMonitor (false);
        engine.setSongMode (true);
        //  Con la cuenta a cero la toma entra YA y el transporte arranca igual:
        //  grabar al arreglo sin transporte no es grabar al arreglo.
        if (! armaCuentaSiToca (slot))
        {
            engine.startRecording (slot);
            engine.setPlaying (true);
        }
        songClickBtn.setToggleState (engine.isClick(), juce::dontSendNotification);
        styleButton (micButton, kRec);        // la misma tapa que cierra la toma
        micButton.setButtonText (T ("PARAR"));
        styleButton (songRecBtn, kRec);
        songRecBtn.setButtonText (T ("PARAR"));
        //  Y SE DICE DONDE VA A CAER, antes y no despues: es lo unico que
        //  convierte «graba» en «graba ahi». Va en el renglon de estado, que es
        //  texto pintado y no cuesta un pixel; en la tapa no, que esa fila se
        //  reparte por el TEXTO y un rotulo que crece le quita el dedo a sus
        //  hermanas.
        status.setText (T ("La toma va al pad %1", etiquetaPad (slot)),
                        juce::dontSendNotification);
        refreshSong (true);
    };

    if (! RP::isRequired (RP::recordAudio) || RP::isGranted (RP::recordAudio))
        arranca();
    else
        RP::request (RP::recordAudio, [this, arranca] (bool granted)
        {
            if (granted) arranca();
            else status.setText (T ("Sin permiso de microfono: no puedo grabar"),
                                 juce::dontSendNotification);
        });
}

//  PONER UN CLIP: el sonido del pad elegido, desde el compas que se toco.
//
//  El pad y no un navegador de ficheros, que es la puerta que ya existe para
//  meter audio en esta maquina: lo que suena en un pad ya esta cargado,
//  recortado y publicado, asi que un clip es una REFERENCIA y no una lectura de
//  disco. Y el largo es el de la MUESTRA y no un compas: un clip que se corta
//  al final del compas no es una toma, es un golpe - y para eso ya estan los
//  golpes sueltos de la otra vista.
void MainComponent::ponClip (int pista, int compas)
{
    const int pad = selectedPad;
    if (! juce::isPositiveAndBelow (pad, kNumPads)) return;
    //  El largo sale del buffer que sostiene LA CARA y nunca de
    //  engine.getSampleLength(), que lee el puntero que ha adoptado el hilo de
    //  audio y es nulo hasta el primer bloque. Es la regla escrita en los
    //  invariantes, y aqui el sintoma seria un clip de largo cero.
    auto* buf = uiSample[(size_t) pad].get();
    if (buf == nullptr || buf->buffer.getNumSamples() <= 0) return;
    if ((int) clips.size() >= AudioEngine::kMaxClips) return;

    //  Y EL CLIP TOMA EL RECORTE DEL PAD, que es lo que SUENA y no el fichero.
    //
    //  Es la misma regla que GUARDAR KIT -«se escribe lo que suena, no el
    //  fichero entero»- y sin ella un pad de un break de cuatro minutos con el
    //  recorte puesto en un golpe entra en la cancion como cuatro minutos: el
    //  clip se dibujaria ocupando la cancion entera y sonaria un golpe.
    const int total = buf->buffer.getNumSamples();
    const int ini   = juce::jlimit (0, total, (int) (padStart01[(size_t) pad] * (float) total));
    const int fin   = juce::jlimit (ini + 1, total, (int) (padEnd01[(size_t) pad] * (float) total));

    ClipUI c;
    c.pad    = pad;
    c.pista  = juce::jlimit (0, AudioEngine::kAudioTracks - 1, pista);
    c.compas = juce::jlimit (0, AudioEngine::kSongBars - 1, compas);
    c.desde  = ini;
    c.largo  = fin - ini;
    c.gain   = 1.0f;
    clips.push_back (c);
    publicaClips();
    refreshSong (false);
}

void MainComponent::mueveClip (int indice, int pista, int compas)
{
    if (! juce::isPositiveAndBelow (indice, (int) clips.size())) return;
    clips[(size_t) indice].pista  = juce::jlimit (0, AudioEngine::kAudioTracks - 1, pista);
    clips[(size_t) indice].compas = juce::jlimit (0, AudioEngine::kSongBars - 1, compas);
    publicaClips();
    refreshSong (false);
}

//  EL LARGO DE UN CLIP, arrastrando un filo. Llega en COMPASES -es lo que la
//  rejilla sabe- y aqui se traduce a muestras, que es donde vive el audio: la
//  cuenta la hace `engine.muestrasPorCompas()`, que es la unica dueña de esa
//  regla y la misma que usa el motor para reproducirlos.
//
//  Y el filo de la IZQUIERDA mueve tambien el punto de la fuente: acortar un
//  clip por delante es empezar mas tarde dentro del sonido, no dejar un hueco.
void MainComponent::largoClip (int indice, int desdeCompas, int hastaCompas)
{
    if (! juce::isPositiveAndBelow (indice, (int) clips.size())) return;
    auto& c = clips[(size_t) indice];

    const double porCompas = juce::jmax (1.0, engine.muestrasPorCompas());
    const int d = juce::jlimit (0, AudioEngine::kSongBars - 1, desdeCompas);
    const int h = juce::jlimit (d + 1, AudioEngine::kSongBars, hastaCompas);

    if (d != c.compas)
    {
        //  Lo que se recorta por delante se le quita al principio de la fuente,
        //  acotado en cero: arrastrar mas alla del principio no puede empezar a
        //  leer antes del fichero.
        const int mueve = (int) ((double) (d - c.compas) * porCompas);
        c.desde = juce::jmax (0, c.desde + mueve);
        c.compas = d;
    }
    c.largo = juce::jmax (1, (int) ((double) (h - d) * porCompas));

    publicaClips();
    refreshSong (false);
}

void MainComponent::quitaClip (int indice)
{
    if (! juce::isPositiveAndBelow (indice, (int) clips.size())) return;
    clips.erase (clips.begin() + indice);
    publicaClips();
    refreshSong (false);
}

void MainComponent::refreshSong (bool repintarTarjeta)
{
    const int bars = engine.getSongLength();
    for (int lane = 0; lane < Playlist::kLanes; ++lane)
        for (int b = 0; b < bars; ++b)
            songCells[lane * bars + b] = engine.getSongCell (lane, b);

    for (int i = 0; i < songPatBtns.size(); ++i)
        songPatBtns[i]->setToggleState (songBrush == i + 1, juce::dontSendNotification);
    songPadModeBtn.setToggleState (songBrush < 0, juce::dontSendNotification);
    songPadModeBtn.setButtonText (songBrush < 0
        ? T ("SONIDO|cancion") + " " + juce::String (-songBrush).paddedLeft ('0', 2)
        : T ("SONIDO|cancion"));

    unsigned mudos = 0;
    for (int ln = 0; ln < Playlist::kLanes; ++ln)
        if (engine.isSongLaneMuted (ln)) mudos |= (1u << (unsigned) ln);

    //  LA BROCHA EN **CLIP** LA LEE LA REJILLA, que es quien decide a que
    //  familia va el gesto (`Playlist::tocaAlClip`). Estaba escrita dos veces
    //  -`songPincel` aqui y `pincelClip` alli- y solo se escribia la primera:
    //  la segunda no la ponia NADIE, asi que el unico camino que soltaba un
    //  clip era el `mouseDown` de la vista de audio, que dejo de existir al
    //  fundir las dos vistas. La brocha CLIP se veia armada y no hacia nada, y
    //  `Tests/clips.py` lo canto entero: `puesto []`.
    //
    //  Y se resuelve poniendo la que ya existe en vez de desviar el gesto
    //  desde `onCell`: la rejilla no sabe -ni tiene por que- que brochas hay,
    //  pero si tiene que saber si el hueco de debajo del dedo es de patron o de
    //  audio, que es lo unico que este booleano dice. Una funcion, un dueño.
    songGrid.pincelClip = (songPincel == 2);

    songCursor = juce::jlimit (0, juce::jmax (0, bars - 1), songCursor);
    songPasteBtn.setEnabled (songClipLleno);
    //  El transporte se puede parar desde la cara, desde un gesto o solo, asi
    //  que la tapa lo LEE en vez de recordarlo.
    {
        const bool rodando = engine.isPlaying();
        songPlayBtn.setToggleState (rodando, juce::dontSendNotification);
        transporte (songPlayBtn, rodando);
    }
    songLoopBtn.setToggleState (engine.hasSongLoop(), juce::dontSendNotification);

    //  LOS SESENTA Y CUATRO Y NO LOS DIECISEIS DEL BANCO DE DELANTE. Una celda
    //  de un solo golpe guarda -(pad+1) con el pad de los 64, asi que pasarle
    //  `gridZati` -que tiene dieciseis- era leer fuera del array en cada
    //  repintado. Ver Playlist::blockColour.
    //  Y LO QUE CADA BLOQUE LLEVA DENTRO. La tabla de pasos es la MISMA que
    //  alimenta la rejilla del secuenciador y viaja por puntero: copiarla
    //  serian 32 KB duplicados y una segunda copia que un dia se queda vieja.
    //  Los largos se leen del motor, que es quien los guarda.
    for (int q = 0; q < kNumPatterns; ++q) songLargos[(size_t) q] = engine.getPatternLength (q);
    songGrid.setPatrones (&pattern[0][0][0], songLargos, kNumPatterns, kNumSteps, kNumPads);

    //  El silencio por bloque, cuatro enteros: el motor los guarda como un bit
    //  por compas y la rejilla los lee igual, asi que no hay traduccion que
    //  pueda quedarse vieja.
    juce::uint64 bmudos[Playlist::kLanes] {};
    for (int ln = 0; ln < Playlist::kLanes; ++ln)
        bmudos[ln] = engine.songCellMuteMask (ln);

    songGrid.setSource (songCells, padZati.data(), (int) padZati.size(), bars, songPrimerCompas,
                        engine.isSongMode() && engine.isPlaying() ? engine.getSongBar() : -1,
                        songCursor, mudos,
                        engine.getSongLoopFrom(), engine.getSongLoopTo(),
                        bmudos);

    //  LOS CLIPS, TRADUCIDOS A COMPASES. La rejilla dibuja compases y el motor
    //  guarda muestras, asi que alguien traduce; se hace aqui y con
    //  `engine.muestrasPorCompas()`, que es la unica cuenta de esa regla - la
    //  misma que usa renderClips. Repetirla con getBpm y una frecuencia
    //  supuesta dibujaria el clip donde no suena.
    //
    //  Y REDONDEANDO HACIA ARRIBA el compas final, no hacia abajo: un clip que
    //  acaba a la mitad del compas 3 OCUPA el compas 3, y truncando se dibujaria
    //  terminando donde todavia suena. Con el suelo en uno, que un clip mas
    //  corto que un compas sigue siendo un clip y sin el saldria de ancho cero
    //  - invisible e imposible de agarrar.
    {
        const double porCompas = juce::jmax (1.0, engine.muestrasPorCompas());
        songClipsVista.clear();
        for (const auto& c : clips)
        {
            Playlist::ClipVista v;
            v.pista = c.pista;
            v.pad   = c.pad;
            v.desde = c.compas;
            v.hasta = c.compas + juce::jmax (1, (int) std::ceil ((double) c.largo / porCompas));
            songClipsVista.push_back (v);
        }
        unsigned mudosAudio = 0;
        for (int t = 0; t < AudioEngine::kAudioTracks; ++t)
            if (engine.isPistaMute (t)) mudosAudio |= (1u << (unsigned) t);
        songGrid.setAudio (songClipsVista.data(), (int) songClipsVista.size(), -1, mudosAudio);
    }

    if (repintarTarjeta) songSheet.repaint();
}


//  Solo is a state of the whole mixer, not of one strip: every other channel
//  has to look silenced or you cannot tell why they went quiet.
void MainComponent::refreshMixStrip()
{
    //  Y LAS DIECISEIS TIRAS DE CANAL, con lo que el motor tiene: el fader y el
    //  mute vuelven de un proyecto y nadie los reponia.
    for (int c = 0; c < kNumCanales; ++c)
    {
        if (auto* f = canFaders[c])
            f->setValue (dbFromGain (engine.getCanalGain (c)), juce::dontSendNotification);
        if (auto* m = canMutes[c])
            m->setToggleState (engine.getCanalMute (c), juce::dontSendNotification);
    }

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
            if (mixAnchos[i] != nullptr) mixAnchos[i]->setAlpha (audible ? 1.0f : 0.45f);
        }
    }
    mixClearSolo.setEnabled (any);
    mixSheet.repaint();
}

//  DE LOS CLIPS DEL MODELO A LA TABLA DEL MOTOR.
//
//  Aqui se resuelve pad -> buffer, y solo aqui: el hilo de audio recibe
//  punteros ya listos y no sabe que existe un pad. Un clip cuyo pad esta vacio
//  no viaja - no es un error, es un pad que todavia no tiene sonido.
void MainComponent::publicaClips()
{
    std::array<AudioEngine::ClipAudio, AudioEngine::kMaxClips> tabla {};
    int n = 0;
    for (const auto& c : clips)
    {
        if (n >= AudioEngine::kMaxClips) break;
        if (! juce::isPositiveAndBelow (c.pad, kNumPads)) continue;
        auto* fuente = uiSample[(size_t) c.pad].get();
        if (fuente == nullptr || c.largo <= 0) continue;

        auto& d = tabla[(size_t) n++];
        d.fuente = fuente;
        d.pista  = juce::jlimit (0, AudioEngine::kAudioTracks - 1, c.pista);
        d.compas = juce::jlimit (0, AudioEngine::kSongBars - 1, c.compas);
        d.desde  = juce::jmax (0, c.desde);
        d.largo  = c.largo;
        d.gain   = c.gain;
    }
    engine.publicaClips (tabla.data(), n);
}

void MainComponent::refreshRack()
{
    canalActual = juce::jlimit (0, kNumCanales - 1, canalActual);
    for (int i = 0; i < rackPadBtns.size(); ++i)
        rackPadBtns[i]->setToggleState (i == canalActual, juce::dontSendNotification);
    //  UNA FILA POR RANURA. El envio va al BUS del tipo que vive en ella, y
    //  una ranura vacia no tiene bus: su fader se apaga -no se esconde- por lo
    //  mismo que el de un efecto cerrado, que una fila que aparece y desaparece
    //  cambia de sitio las de abajo cada vez que se toca el menu.
    for (int s = 0; s < rackSends.size() && s < kNumRanuras; ++s)
    {
        const int fx = enRanura (s);
        rackSends[s]->setValue (fx >= 0 ? engine.getCanalSend (canalActual, fx) : 0.0,
                                juce::dontSendNotification);
        rackSends[s]->setEnabled (fx >= 0);

        //  Y EN PALABRAS PARA QUIEN NO VE LA PANTALLA, que es lo que lee
        //  TalkBack y no cuesta un pixel: sin esto la fila se anuncia como
        //  «deslizador» seis veces seguidas.
        rackSends[s]->setTitle (fx < 0 ? T ("VACIA")
                                       : juce::String (fxDefs[fx].name) + " "
                                         + juce::String::charToString ((juce::juce_wchar) 0x00B7) + " "
                                         + T (AudioEngine::sustituye (fx) ? "SUSTITUYE" : "SUMA"));
    }
    refrescaRanuras();      // el canalon de cada fila, con las seis de la cara
    rackSheet.repaint();
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


//  EL TOUR DE BIENVENIDA. Ver la declaracion en la cabecera.
//
//  El fichero vive donde viven el idioma y la carcasa - el directorio interno
//  de la app - y no en la biblioteca: hay que poder leerlo antes de que
//  ProjectStore haya decidido donde esta la biblioteca, que es exactamente el
//  orden en que arranca esto.
//  EL NIVEL DEL MASTER, en el directorio interno y por la misma razon que la
//  carcasa: hay que poder leerlo antes de que ProjectStore haya decidido donde
//  esta la biblioteca.
//  QUIEN FIRMA, y va donde el master, el idioma y la carcasa: es de la PERSONA
//  y no del proyecto. Guardarlo en el proyecto significaria que abrir un tema
//  de otro te pone su nombre en lo que exportes tu.
//
//  Se lee del fichero cada vez que se exporta en vez de guardarse en un miembro
//  a la vuelta: son dos lecturas de disco al ano, fuera del hilo de audio, y a
//  cambio no hay una copia que pueda quedarse vieja. Es la misma razon por la
//  que un token de carcasa se vuelve a leer y no se copia.
juce::String MainComponent::artistaPref()
{
    const auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("zati-artista.txt");
    //  Acotado: un fichero corrupto no puede meter una linea entera en una
    //  cabecera INFO, y los saltos de linea rompen un comentario Vorbis.
    return f.existsAsFile() ? f.loadFileAsString().removeCharacters ("\r\n").trim().substring (0, 64)
                            : juce::String();
}

juce::File MainComponent::masterPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-master.txt");
}

//  Y EL RESPALDO, para los caminos que no son un arrastre: el doble clic que
//  devuelve a 0 dB no pasa por `onDragEnd`, y un valor que no llega al disco
//  vuelve mañana con el de ayer. Un tic con el dedo levantado y ya esta.
void MainComponent::guardaMasterSiHaceFalta()
{
    if (! masterPrefSucio || masterFader.isMouseButtonDown()) return;
    masterPrefSucio = false;
    saveMasterPref();
}

void MainComponent::saveMasterPref() const
{
    //  Por la MISMA puerta que la sesion y el proyecto. Esto eran tres lineas
    //  copiadas en siete sitios -crear la carpeta, escribir, no mirar el
    //  resultado- mientras ProjectStore::escribeTexto existe justo para eso:
    //  temporal, validador y renombrado. Una regla escrita siete veces es
    //  siete reglas, y la septima es la que un dia se escribe mal.
    ProjectStore::escribeTexto (masterPrefFile(), juce::String (masterFader.getValue(), 2));
}

void MainComponent::loadMasterPref()
{
    const auto f = masterPrefFile();
    //  Sin fichero, 0 dB. Y el que hay se lee ACOTADO al rango del mando: un
    //  fichero a medio escribir o de una version con otro minimo devolveria un
    //  numero que el Slider aceptaria y el motor no, y el sintoma seria una
    //  maquina muda sin ninguna pista de por que.
    const double db = f.existsAsFile()
                        ? juce::jlimit (kGainMinDb, kGainMaxDb, f.loadFileAsString().trim().getDoubleValue())
                        : 0.0;
    masterFader.setValue (db, juce::sendNotificationSync);
}

//  CUANTAS OCTAVAS SE VEN EN EL PIANO, y va donde el master y la carcasa: es
//  una decision de la PERSONA y del aparato que tiene en la mano, no de la
//  cancion. Guardarla en el proyecto significaria que abrirlo en una tableta te
//  trae el tamano que elegiste en un movil de 280 px.
//  LA VENTANA DE PISTAS, donde el master, la carcasa y las octavas del piano:
//  cuantas pistas te caben en la mano depende de la mano y del aparato, no de
//  la cancion.
//  EL METRONOMO Y LA CUENTA ATRAS. Ver MainComponent.h.
//  EL MOVIMIENTO, donde el master, el idioma, la carcasa y la cuenta atras: en
//  el directorio interno de la app y no en la biblioteca, porque hay que poder
//  leerlo antes de que `ProjectStore` haya decidido donde esta la biblioteca —
//  que es exactamente el orden en que arranca esto.
juce::File MainComponent::movPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-movimiento.txt");
}

void MainComponent::saveMovPref() const
{
    //  Por `escribeTexto` y no con un `replaceWithText` a pelo: es la puerta
    //  unica desde que `autosave()` casi costo un proyecto, y una regla escrita
    //  ocho veces son ocho reglas.
    ProjectStore::escribeTexto (movPrefFile(), movimiento ? "1" : "0");
}

void MainComponent::loadMovPref()
{
    //  Lo que no esta escrito vale lo de antes: la app latia, asi que sin
    //  fichero late. Quien ya la usaba no nota el cambio.
    const auto f = movPrefFile();
    if (! f.existsAsFile()) return;
    movimiento = f.loadFileAsString().trim().getIntValue() != 0;
}

//  AL APAGARLO, UN CUADRO EN SILENCIO Y LUEGO NADA.
//
//  Sin esto el cristal se queda con el ultimo fotograma dibujado y una onda
//  congelada se lee como «esto esta roto», que es exactamente lo que ya obligo
//  a que el analizador del EQ cayera a su suelo en vez de quedarse quieto. Se
//  le da un cuadro de ceros —su propia guardia de silencio hace el resto— y a
//  partir de ahi no se le vuelve a hablar.
void MainComponent::ponMovimiento (bool on)
{
    if (movimiento == on) return;
    movimiento = on;
    saveMovPref();

    if (! movimiento)
    {
        float cero[64] {};
        cristal.setSamples (cero, 64, (double) DeviceTier::profile().relojMs);
        cristal.setColumns (cero, cero, 0);
        cristal.setVu (0.0f, 0.0f);
        for (auto* b : fxButtons)
            if (b != nullptr && (double) b->getProperties().getWithDefault ("pulse", 0.0) != 0.0)
            {
                b->getProperties().set ("pulse", 0.0);
                b->repaint();
            }
        for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    }

    status.setText (movimiento ? T ("La cara se mueve") : T ("La cara esta quieta"),
                    juce::dontSendNotification);
}

juce::File MainComponent::cuentaPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-cuenta.txt");
}

void MainComponent::saveCuentaPref() const
{
    //  Los dos numeros en una linea. Escrito con `escribeTexto` y no con
    //  `replaceWithText`, que es la red que esta casa se puso el dia que
    //  `autosave` casi cuesta un proyecto: temporal, validador y renombrado.
    ProjectStore::escribeTexto (cuentaPrefFile(),
                                juce::String (cuentaCompases) + " "
                                  + juce::String (engine.isClick() ? 1 : 0));
}

juce::File MainComponent::tomasPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-tomas.txt");
}

void MainComponent::saveTomasPref() const
{
    ProjectStore::escribeTexto (tomasPrefFile(), juce::String (bancoTomas));
}

void MainComponent::loadTomasPref()
{
    //  Acotado AQUI y no en quien llama, como el resto: el fichero puede estar
    //  a medias o ser de otra version, y un banco fuera de rango dejaria a
    //  `padParaToma` buscando en un sitio que no existe.
    const auto f = tomasPrefFile();
    if (! f.existsAsFile()) return;
    bancoTomas = juce::jlimit (0, kNumBanks - 1, f.loadFileAsString().trim().getIntValue());
}

void MainComponent::loadCuentaPref()
{
    //  El defecto es UN compas y el clic puesto, que es exactamente lo que la
    //  app hacia clavado hasta ahora: quien ya la usaba no nota el cambio, y
    //  quien quiera otra cosa ya puede pedirla. Es la misma regla que gobierna
    //  un proyecto de otra epoca - lo que no esta escrito vale lo de antes.
    const auto f = cuentaPrefFile();
    if (! f.existsAsFile()) return;

    juce::StringArray p;
    p.addTokens (f.loadFileAsString().trim(), " ", "");
    //  Acotado AQUI y no en quien llama: el fichero puede estar a medias o ser
    //  de otra version, y `armaCuentaAtras` acepta hasta ocho compases - dos es
    //  lo que esta app ofrece.
    if (p.size() > 0) cuentaCompases = juce::jlimit (0, 2, p[0].getIntValue());
    if (p.size() > 1) engine.setClick (p[1].getIntValue() != 0);
}


// ---------------------------------------------------------------------------
//  EL MONITOR. Ver MainComponent.h y AudioEngine::setMonitor.
//
//  Fichero propio y no un tercer numero en el de la cuenta: ese se llama
//  `zati-cuenta.txt` y meterle dentro algo que no es la cuenta es como un
//  nombre deja de ser verdad, que en esta casa ya costo renombrar
//  `uiIntervalMs` y `fxIsTone`.
// ---------------------------------------------------------------------------
juce::File MainComponent::monitorPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-monitor.txt");
}

void MainComponent::saveMonitorPref()
{
    ProjectStore::escribeTexto (monitorPrefFile(), juce::String (monitorOn ? 1 : 0));
}

void MainComponent::loadMonitorPref()
{
    const auto f = monitorPrefFile();
    if (! f.existsAsFile()) return;
    monitorOn = (f.loadFileAsString().trim().getIntValue() != 0);
}

//  LA GUARDA ES LA RUTA Y NO LA CASILLA.
//
//  Sin cascos, el microfono saliendo por el altavoz es un lazo, y ademas la
//  produccion se imprime dentro de la toma. Se le pregunta al APARATO en cada
//  toma y no al arrancar - los cascos se enchufan y se quitan en mitad de una
//  sesion, que es el mismo argumento por el que la carpeta de exportacion se
//  revalida en cada rebote y no solo al elegirla.
//
//  Y se DICE. Un monitor que la persona ha encendido y que no suena, sin una
//  linea que lo explique, se lee como que la app esta rota.
void MainComponent::aplicaMonitor (bool avisa)
{
    const bool altavoz = RutaAudio::porAltavoz();
    const bool suena   = monitorOn && ! altavoz;

    engine.setMonitor (suena ? 1.0f : 0.0f);

    for (int i = 0; i < monButtons.size(); ++i)
        if (auto* b = monButtons[i])
            b->setToggleState (i == (monitorOn ? 1 : 0), juce::dontSendNotification);

    if (avisa)
        status.setText (! monitorOn      ? T ("Monitor apagado")
                        : altavoz        ? T ("Monitor: hacen falta cascos")
                                         : T ("Monitor encendido"),
                        juce::dontSendNotification);
}

//  UN SOLO SITIO PARA LOS DOS CAMINOS DE GRABACION.
//
//  `armaCuentaAtras (1)` estaba escrito en `grabaAlArreglo` y en ningun otro
//  lado, asi que el microfono de la cara no podia tener cuenta atras sin copiar
//  la regla - y una regla escrita dos veces son dos reglas. Devuelve si hay que
//  ESPERARLA, que es lo unico que el que llama necesita saber para decidir si
//  arranca la toma ahora o la deja armada.
bool MainComponent::armaCuentaSiToca (int slot)
{
    //  EL CLIC YA NO SE FUERZA, y esa es la otra mitad de la peticion.
    //
    //  Se encendia siempre con este argumento: «grabar al arreglo sin metronomo
    //  es grabar a ojo». Es verdad la primera vez y deja de serlo en cuanto ya
    //  llevas la referencia puesta - y entonces el clic se cuela en la toma por
    //  los cascos, que es peor que no tenerlo. La tapa CLIC decide, y su estado
    //  se recuerda: el interruptor que ya existia ES la opcion, y una segunda
    //  casilla que dijera lo mismo serian dos sitios para una decision.
    //
    //  Con la cuenta puesta y el clic quitado la cuenta no se oye, y eso es una
    //  eleccion valida: cuentas mirando la rejilla. Lo que no se hace es
    //  decidirlo por la persona.
    if (cuentaCompases <= 0) return false;

    engine.armaGrabacionEnCuenta (slot);
    engine.armaCuentaAtras (cuentaCompases);
    engine.setPlaying (true);
    return true;
}

juce::File MainComponent::pistasPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-pistas.txt");
}

void MainComponent::savePistasPref() const
{
    ProjectStore::escribeTexto (pistasPrefFile(), juce::String (pistasVista));
}

void MainComponent::loadPistasPref()
{
    const auto f = pistasPrefFile();
    aplicaPistas (f.existsAsFile() ? f.loadFileAsString().trim().getIntValue() : 0);
}

//  Un solo sitio que mueve las tres cosas que dependen del modo: la ventana de
//  la rejilla, el rotulo de la tapa y la maqueta - con ocho pistas la celda mide
//  el doble de alto, y quien decide si las filas prescindibles caben es resized.
void MainComponent::aplicaPistas (int modo)
{
    pistasVista = juce::jlimit (0, 2, modo);
    const int n     = (pistasVista == 0) ? StepGrid::kLanes : StepGrid::kLanes / 2;
    const int desde = (pistasVista == 2) ? StepGrid::kLanes / 2 : 0;
    stepGrid.setVentana (n, desde);
    seqPistasBtn.setButtonText (Lang::ltr (pistasVista == 0 ? "1-16"
                                         : pistasVista == 1 ? "1-8" : "9-16"));
    resized();
    refreshStepGrid();
}

//  Un solo sitio que mueve las tres cosas que dependen del zoom: la rejilla,
//  el rotulo de la tapa y la ventana —al ensanchar la celda caben menos
//  columnas, asi que el primer paso puede haberse quedado por encima de su
//  nuevo maximo y la ventana apuntaria fuera del patron.
void MainComponent::aplicaZoomPasos (float z)
{
    stepGrid.ponZoomAncho (z);
    seqZoomW = stepGrid.getZoomAncho();
    seqZoomBtn.setButtonText (Lang::ltr (seqZoomW > 1.5f ? "2:1"
                                       : seqZoomW < 0.9f ? "3:4" : "1:1"));
    const int len = engine.getPatternLength (selectedPattern);
    seqPrimerPaso = juce::jlimit (0, juce::jmax (0, len - stepGrid.numCols()), seqPrimerPaso);
    refreshStepGrid();
}

juce::File MainComponent::pianoPrefFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-piano.txt");
}

void MainComponent::savePianoPref() const
{
    ProjectStore::escribeTexto (pianoPrefFile(), juce::String (pianoGrid.getFilas()));
}

void MainComponent::loadPianoPref()
{
    const auto f = pianoPrefFile();
    //  Cualquier cosa que no sea el numero grande cae en el pequeno, que es el
    //  que hace que una nota se pueda colocar: un fichero a medias no puede
    //  dejar la rejilla en una cuenta que el boton de OCTAVA no sabe recorrer.
    aplicaFilasPiano (f.existsAsFile() ? f.loadFileAsString().trim().getIntValue()
                                       : PianoRoll::kFilasMin);
}

//  Un solo sitio que mueve las tres cosas que dependen de la cuenta: la rejilla,
//  el rotulo de la tapa y el tope de OCTAVA - si la ventana crecio, la base
//  puede haberse quedado por encima de su nuevo maximo y entonces la mitad de
//  arriba dibujaria notas que el motor recorta.
void MainComponent::aplicaFilasPiano (int filas)
{
    pianoGrid.setFilas (filas);
    pianoBase = juce::jlimit (-24, pianoGrid.baseMax(), pianoBase);
    pianoVerBtn.setButtonText (pianoGrid.getFilas() >= PianoRoll::kFilasMax
                                 ? T ("2 OCTAVAS") : T ("1 OCTAVA"));
    resized();
    refreshPiano();
}

juce::File MainComponent::tourFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("zati-tour.txt");
}

//  La ultima tapa cambia de nombre y no solo de efecto: "SIGUIENTE" en la
//  ultima tarjeta es una promesa de una siguiente que no existe.
juce::String MainComponent::tourNextCaption() const
{
    //  Y en el paso de la PUERTA tambien dice EMPEZAR, que es lo que hace:
    //  cerrar y ponerse a tocar. Seguir leyendo es la otra tapa.
    if (tourEsLaPuerta() || tourPaso + 1 >= kTourPasos) return T ("TOUR EMPEZAR");
    return T ("SIGUIENTE");
}

//  LA TERCERA TAPA. En los tres primeros pasos es SALTAR -salirse del
//  recorrido- y en el cuarto es la PUERTA a los once que quedan. No es una tapa
//  mas: esa fila son tres y una cuarta la parte en dos justo en la pantalla que
//  se ve la primera vez que alguien abre esto.
//
//  Y en el cuarto no se pierde nada por reaprovecharla: alli SALTAR y EMPEZAR
//  hacen ya exactamente lo mismo -marcar visto y cerrar- asi que una de las dos
//  sobraba.
juce::String MainComponent::tourSkipCaption() const
{
    return tourEsLaPuerta() ? T ("TOUR VER MAS") : T ("SALTAR");
}

void MainComponent::showTour (int paso)
{
    tourPaso = juce::jlimit (0, kTourPasos - 1, paso);
    //  Primero se abre lo que el paso explica y DESPUES se maqueta: el muelle
    //  se coloca segun donde quede el objetivo, y el objetivo de casi todos los
    //  pasos vive dentro de una ficha que este paso acaba de abrir.
    tourPrepara (tourPaso);
    tourBackBtn.setEnabled (tourPaso > 0);
    tourNextBtn.setButtonText (tourNextCaption());
    tourSkipBtn.setButtonText (tourSkipCaption());
    resized();
    tourSheet.repaint();
}


//  QUE SENALA CADA PASO. Vacio = sin objetivo: el primero y el ultimo hablan de
//  la app entera y no de una pieza, y ahi un anillo alrededor de algo seria
//  senalar por senalar.
juce::Rectangle<int> MainComponent::tourObjetivo (int paso) const
{
    auto deComponente = [this] (const juce::Component* c) -> juce::Rectangle<int>
    {
        //  Y solo si esta en pantalla: un componente escondido tiene limites y
        //  no tiene sitio, asi que el anillo caeria sobre nada. Es la misma
        //  regla que el banco aplica a los controles que no se maquetan.
        if (c == nullptr || ! c->isShowing()) return {};
        return getLocalArea (c, c->getLocalBounds());
    };

    switch (paso)
    {
        case 1:  return padPlateArea;
        case 2:  return bankButtons.isEmpty() ? juce::Rectangle<int>()
                        : deComponente (bankButtons[0]).getUnion (deComponente (bankButtons[bankButtons.size() - 1]));
        case 3:  return deComponente (&loadButton).getUnion (deComponente (&playButton));
        case 4:  return fxButtons.isEmpty() ? juce::Rectangle<int>()
                        : deComponente (fxButtons[0]).getUnion (deComponente (fxButtons[fxButtons.size() - 1]));
        case 5:  return ctrlPlateArea;
        case 6:  return deComponente (&stepGrid);
        //  LA TIRA DEL PASO, POR SUS MANDOS Y NO POR UN RECTANGULO MUERTO.
        //
        //  Esto apuntaba a `stepStripArea`, y esa variable solo se ASIGNA en un
        //  sitio: `vuArea = stepStripArea = {}` con el comentario "gone from the
        //  face; the screen draws them". O sea que quedo de cuando la tira vivia
        //  en la cara y desde entonces vale vacio SIEMPRE. El paso abria la
        //  ficha, tocaba un paso para que la tira existiera, y luego señalaba un
        //  rectangulo de cero: velo uniforme, ni agujero ni anillo.
        //
        //  Se pregunta a los mandos, que es lo que la persona tiene que mirar, y
        //  se toma la union de los que HAY: la tercera fila -los cuatro
        //  bloqueos- se cae en las pantallas estrechas, y deComponente ya
        //  devuelve vacio para lo que no esta en pantalla, asi que la union sale
        //  bien sea cual sea el numero de filas.
        case 7:
        {
            auto r = deComponente (&noteSlider).getUnion (deComponente (&velSlider));
            r = r.getUnion (deComponente (&rollSlider)).getUnion (deComponente (&lockSlider));
            r = r.getUnion (deComponente (&atkPasoSlider)).getUnion (deComponente (&panPasoSlider));
            return r;
        }
        case 8:  return deComponente (&pianoGrid);
        case 9:  return deComponente (&patDoubleBtn).getUnion (deComponente (&seqHumanBtn));
        case 10: return deComponente (&waveform);
        case 11: return deComponente (&rackButton);
        case 12: return deComponente (&songGrid);
        case 13: return deComponente (&exportMasterButton).getUnion (deComponente (&exportStemsButton));
        //  Y EL ULTIMO PASO NO SEÑALABA NADA, que es la otra mitad del mismo
        //  descuido: la tabla llegaba al 13 y el tour tiene quince pasos. El 14
        //  abre AJUSTES para hablar del idioma, las carcasas y el manual, y
        //  caia en `default` - o sea que la unica pantalla donde se explica como
        //  cambiar de idioma se enseñaba sin señalar el selector de idioma.
        //
        //  El cero SI es un vacio a proposito: es la portada y no tiene a que
        //  apuntar. Un `default` que atiende dos casos -uno correcto y uno
        //  olvidado- es como se esconde el segundo.
        case 14:
        {
            auto r = langButtons.isEmpty() ? juce::Rectangle<int>()
                   : deComponente (langButtons[0]).getUnion (deComponente (langButtons[langButtons.size() - 1]));
            if (! skinButtons.isEmpty())
                r = r.getUnion (deComponente (skinButtons[0]))
                     .getUnion (deComponente (skinButtons[skinButtons.size() - 1]));
            return r;
        }
        default: return {};
    }
}

//  Y CADA PASO ABRE LO QUE EXPLICA. Un tour que dice "en SEC esta la rejilla" y
//  deja a la persona en la cara no ha ensenado la rejilla: la ha nombrado. El
//  del proyecto anterior abre cada pop-up y lo explica en vivo, y es lo que lo separa de un
//  folleto.
void MainComponent::tourPrepara (int paso)
{
    switch (paso)
    {
        case 6:  showSeqPage (seqPageGrid);  openSheet (seqSheet, secButton); break;
        case 7:  showSeqPage (seqPageGrid);  openSheet (seqSheet, secButton);
                 //  Con un paso tocado, que la tira solo existe entonces.
                 if (selectedStep < 0) stepCellToggled (0, 4);
                 break;
        case 8:  openSheet (seqSheet, secButton); showSeqPage (seqPagePiano); refreshPiano(); break;
        case 9:  showSeqPage (seqPageStep);  openSheet (seqSheet, secButton); break;
        case 10: showPadPage (padPageTrim);  openSheet (padSheet, padsButton); break;
        //  Y EL PASO 11 ABRE LA PAGINA QUE NOMBRA. Su texto dice «la mesa tiene
        //  dieciseis PADS y dieciseis CANALES», y sin fijar la pagina la mesa
        //  abre en la que hubiera - por defecto PADS, o sea que la palabra
        //  CANALES no se ve por ningun sitio. Es el residuo de siempre: los
        //  pasos 6 a 9 llaman a showSeqPage y el 10 a showPadPage justo por
        //  esto, y este se habia quedado sin su linea.
        case 11: showMixPage (mixPageCanales); refreshMixStrip();
                 openSheet (mixSheet, mixButton);  break;
        case 12: openSheet (songSheet, songButton); break;
        case 13: exportStatus.clear(); exportOk = false; destinoCache = juce::File();
                 openSheet (exportSheet, setButton); break;
        //  El ultimo paso explica el IDIOMA y la CARCASA, que desde que tienen
        //  pagina propia ya no estan en AUDIO: abrir AUDIO dejaba el anillo
        //  alrededor de nada, que es exactamente lo que tourObjetivo evita
        //  devolviendo vacio - y un paso que no senala nada no explica nada.
        case 14: showSetPage (pageAspecto); openSheet (setSheet, setButton); break;
        default: closeAllSheets(); break;
    }

    //  Y EL TOUR POR ENCIMA. openSheet cierra todo y sube la ficha que abre, asi
    //  que sin esto el tour se queda debajo de lo que acaba de abrir para
    //  explicarlo - que es exactamente el fallo que el proyecto anterior anoto
    //  como "el
    //  tour se eleva por encima del pop-up".
    tourSheet.setVisible (true);
    tourSheet.toFront (false);
}

//  Ver MainComponent::tourBodyFont.
juce::Font MainComponent::tourBodyFont()
{
    return ZatiColours::monoFont (Metrics::fLabel + 3.0f, false).withExtraKerningFactor (0.02f);
}

int MainComponent::tourBodyHeight (int ancho) const
{
    if (ancho <= 0) return 0;
    const auto f = tourBodyFont();
    const int lineaH = (int) std::ceil (f.getHeight() * 1.15f);
    int peor = 1;
    for (int i = 0; i < kTourPasos; ++i)
    {
        //  Ancho del texto seguido dividido por el ancho de la caja, mas uno:
        //  drawFittedText parte por palabras, asi que una linea nunca se llena
        //  del todo y redondear por abajo deja la ultima fuera.
        const double w = juce::GlyphArrangement::getStringWidth (f, T (ZatiTour::cuerpos[i]));
        peor = juce::jmax (peor, (int) std::ceil (w / (double) ancho) + 1);
    }
    return peor * lineaH;
}


// ---------------------------------------------------------------------------
//  Export — the bounce
// ---------------------------------------------------------------------------

juce::String MainComponent::exportSourceLabel() const
{
    //  Y AQUI LA CLAVE ES «SONG» Y NO «CANCION». La fila de «CANCION» decia
    //  «SONG MODE» en ingles -es la de la tapa de modo, que ahora tiene clave
    //  propia- asi que esta linea decia «Origen: SONG MODE» en tres de los
    //  cuatro idiomas. Lo que exporta es la CANCION, no un modo.
    if (engine.isSongMode())        return T ("SONG");
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

    Bitacora::paso (stems ? "exportar/arranca pistas" : "exportar/arranca master");

    exportOk = false;
    //  Y traducido, que era la unica linea de esta ficha que se quedo en
    //  castellano en las cuatro compilaciones.
    exportStatus = T ("renderizando...");
    beginBusy (T ("Exportando"));
    exportJob = std::make_unique<Exporter> (engine, uiSample, padName,
                                            ProjectStore::exports().getChildFile (base),
                                            base, stems, deviceSampleRate, exportOgg,
                                            artistaPref());

    exportMasterButton.setVisible (false);
    exportStemsButton.setVisible (false);
    exportLiveButton.setVisible (false);
    exportFmtBtn.setVisible (false);
    //  Y CAMBIAR, que a mitad de un rebote dejaria las pistas repartidas en dos
    //  carpetas: el hilo ya tiene su destino y no lo vuelve a mirar.
    exportDirBtn.setVisible (false);
    exportCancelButton.setVisible (true);
    exportJob->startThread (juce::Thread::Priority::normal);
    exportSheet.repaint();
}

// ---------------------------------------------------------------------------
//  EL REBOTE EN VIVO. Ver MainComponent.h y RebotVivo en Exporter.h.
//
//  La misma tapa arranca y para, como GRABAR MIC y como REMUESTREAR: es una
//  toma, y una toma no tiene dos botones.
//
//  Y LA CUENTA ATRAS ES LA QUE YA HAY. `armaCuentaSiToca` devuelve si hay que
//  esperarla y `arranqueEnBorde` hace que la cancion entre en la linea de
//  compas; escribir una segunda aqui seria la misma regla dos veces, que es
//  exactamente lo que se arreglo el dia que grabar de normal no tenia ninguna.
// ---------------------------------------------------------------------------
void MainComponent::alternaRebotVivo()
{
    if (vivoJob != nullptr) { terminaRebotVivo(); return; }
    if (exportJob != nullptr) return;         // un rebote a la vez

    if (engine.lengthInSteps() <= 0 || ! engine.hasContentToRender())
    {
        exportOk = false;
        exportStatus = T ("no hay nada grabado en %1", exportSourceLabel().toLowerCase());
        exportSheet.repaint();
        return;
    }

    //  EL DESTINO Y EL FORMATO SON LOS QUE YA HAY. Un segundo camino a la
    //  carpeta serian dos reglas, y ademas se saltaria la revalidacion por
    //  escritura que `ProjectStore::exports()` hace en cada exportacion.
    auto base = (currentProject.isNotEmpty() ? currentProject : juce::String ("ZATI"))
                  .retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ ")
                  .trim().replaceCharacter (' ', '-');
    if (base.isEmpty()) base = "ZATI";

    Bitacora::paso ("exportar/arranca en vivo");

    vivoFichero = ProjectStore::exports()
                    .getChildFile (base + " VIVO" + (exportOgg ? ".ogg" : ".wav"));
    vivoJob = std::make_unique<RebotVivo> (engine, vivoFichero, deviceSampleRate,
                                           exportOgg, artistaPref(), base);

    //  EL ORDEN IMPORTA: el anillo se arma ANTES de que el transporte ruede, o
    //  los primeros bloques de la cancion se pierden. Y el hilo escritor
    //  arranca despues del anillo, que si no lee un FIFO de tamano uno.
    engine.vivoArma (2);
    vivoJob->startThread (juce::Thread::Priority::normal);

    engine.setSongMode (true);
    //  Con la cuenta a cero la cancion entra YA. Y con cuenta, el transporte lo
    //  arranca `armaCuentaSiToca` en el borde de compas: el clic suena, el
    //  compas no avanza, y lo que se escribe empieza en la linea.
    if (! armaCuentaSiToca (-1))
        engine.setPlaying (true);

    ponModoCancion (true);
    styleButton (exportLiveButton, kRec);
    exportLiveButton.setButtonText (T ("PARAR"));
    exportOk = false;
    exportStatus = T ("grabando en vivo...");
    exportSheet.repaint();
}

void MainComponent::terminaRebotVivo()
{
    if (vivoJob == nullptr) return;

    //  PRIMERO SE CIERRA EL GRIFO Y DESPUES SE PARA EL HILO, que es el orden y
    //  no un detalle: al reves, el hilo sale con lo ultimo que sono todavia
    //  dentro del anillo y la cancion se corta antes de tiempo.
    engine.vivoPara();
    engine.setPlaying (false);
    vivoJob->signalThreadShouldExit();
    vivoJob->stopThread (4000);

    const auto n     = vivoJob->escritas();
    const bool mal   = vivoJob->fueMal();
    const int  tiradas = engine.vivoTiradas();
    vivoJob.reset();

    styleButton (exportLiveButton, kKey);
    exportLiveButton.setButtonText (T ("EN VIVO"));
    exportOk = (! mal && n > 0);
    if (! exportOk)
        exportStatus = T ("no se pudo escribir el rebote en vivo");
    else
        //  Y LO TIRADO SE DICE. Un anillo que se llena porque el disco no
        //  llega deja huecos en el fichero, y un rebote con huecos que no
        //  avisa es peor que uno que falla.
        exportStatus = tiradas > 0
                         ? T ("En vivo: %1 con %2 muestras perdidas",
                              vivoFichero.getFileName(), juce::String (tiradas))
                         : T ("En vivo: %1", vivoFichero.getFileName());

    //  Y AL ALMACEN DE MEDIOS, por el MISMO camino que el rebote offline: es
    //  un fichero suelto y no una carpeta, asi que va directo a `MediaStore` en
    //  vez de por `publicarExport`, que barre una carpeta entera.
    //
    //  Se COPIA y no se mueve mientras la copia no este puesta, que es la regla
    //  que ya gobierna el otro camino: un fallo aqui no puede costar el
    //  trabajo.
    if (exportOk && ! ProjectStore::exportsElegida())
    {
        const auto ruta = MediaStore::publicar (vivoFichero, "ZATI",
                                                exportOgg ? "audio/ogg" : "audio/wav");
        if (ruta.isNotEmpty()) vivoFichero.deleteFile();
    }
    exportSheet.repaint();
}

//  DE LA CARPETA DE LA APP A LA MUSICA COMPARTIDA. Ver MediaStore::publicar.
//
//  Android 10 en adelante ignora el permiso de escritura para el almacenamiento
//  compartido, asi que la unica puerta es el almacen de medios - y es mejor
//  puerta: no pide permiso ninguno, deja el fichero en Music/, lo indexa el
//  escaner asi que sale tambien en los reproductores, y sobrevive a desinstalar
//  la app. Por debajo de Android 10 esto devuelve vacio y no hace falta: alli el
//  permiso vale y ProjectStore ya escribe en la Musica compartida.
void MainComponent::publicarExport (const juce::File& carpeta)
{
    if (! carpeta.isDirectory()) return;

    juce::Array<juce::File> hechos;
    carpeta.findChildFiles (hechos, juce::File::findFiles, false, "*.wav;*.ogg");
    if (hechos.isEmpty()) return;

    const juce::String sub = "ZATI/" + carpeta.getFileName();
    int puestos = 0;
    juce::String donde;
    for (auto& f : hechos)
    {
        const auto ruta = MediaStore::publicar (f, sub,
                                                f.hasFileExtension ("ogg") ? "audio/ogg" : "audio/wav");
        if (ruta.isNotEmpty())
        {
            ++puestos;
            if (donde.isEmpty()) donde = "Music/" + sub;
            //  El original se borra SOLO cuando la copia esta puesta: el sitio
            //  nuevo es estrictamente mas alcanzable que el viejo, y dejar los
            //  dos duplica 48 MB en el caso de las pistas. Si la copia fallo, el
            //  original se queda: perder el rebote no es una opcion.
            f.deleteFile();
        }
    }

    if (puestos > 0)
    {
        exportStatus = T ("%1 en %2", juce::String (puestos), donde);
        //  Y la carpeta vacia se va con ellos, que si no queda un rastro de
        //  carpetas sin nada dentro por cada rebote.
        if (carpeta.getNumberOfChildFiles (juce::File::findFilesAndDirectories) == 0)
            carpeta.deleteRecursively();
    }
}

void MainComponent::pollExport()
{
    //  The audio path can change under us (headphones in, a call, a route
    //  switch), so the readout is refreshed while you are looking at it.
    //
    //  Pero SOLO cuando algo de lo que dice ha cambiado. Esto corria en cada
    //  tick y repintaba la ficha entera: AJUSTES ocupa la ventana y lleva un
    //  velo al 45 %, asi que cada repintado arrastraba el chasis, los
    //  dieciseis pads, el espectro y los cuarenta controles de debajo.
    //  Medido con el contador de fotogramas: doscientos fotogramas completos
    //  en doce segundos con la ficha abierta y NADA cambiando - 1601 ms de
    //  CPU contra los 267 que cuesta la misma cara quieta.
    //
    //  Se comparan valores y no un texto montado: construir la linea para
    //  compararla seria volver a asignar dos cadenas por tick, que es la
    //  costumbre que refreshDeviceStatusLine ya se quito de encima.
    if (setSheet.isVisible())
    {
        auto* dev = deviceManager.getCurrentAudioDevice();
        //  El PUNTERO del dispositivo, no su nombre: un cambio de ruta
        //  construye un objeto nuevo, y comparar punteros no asigna nada.
        const Readout ahora {
            dev,
            dev != nullptr ? dev->getCurrentSampleRate() : 0.0,
            dev != nullptr ? dev->getCurrentBufferSizeSamples() : 0,
            dev != nullptr ? dev->getOutputLatencyInSamples() : 0,
            measuring, measuredMs, measuredRate,
            fastPath.ran, fastPath.mmapKnown, fastPath.mmapUsed, fastPath.exclusive,
            (int) AudioPath::mmapPolicy(), (int) AudioPath::exclusivePolicy(),
            measureNote.hashCode()
        };

        if (! (ahora == lastReadout))
        {
            lastReadout = ahora;
            setSheet.repaint();
        }
    }
    if (measuring && ! engine.isProbing()) finishMeasure();

    if (exportJob == nullptr) return;

    if (! exportJob->finished.load (std::memory_order_acquire))
    {
        //  Y SOLO SI LA FICHA SE VE. Una ficha ocupa la ventana entera y lleva
        //  velo, asi que esto pedia el chasis, los dieciseis pads y los
        //  cuarenta controles de debajo treinta veces por segundo durante TODO
        //  el rebote -cuarenta segundos seguidos- aunque la persona la hubiera
        //  cerrado. Es el mismo fallo que la rejilla de pasos y el cabezal del
        //  piano, en el unico sitio de la app que dura tanto. La barra de
        //  progreso se sigue moviendo: la pinta `busyBar`, que es suya.
        if (exportSheet.isVisible()) exportSheet.repaint();
        return;
    }

    exportOk     = exportJob->resultOk;
    exportStatus = exportJob->resultText;
    const auto salio = exportJob->resultFolder;
    endBusy();
    exportJob.reset();

    //  Y AHORA SE DEJA DONDE CUALQUIER GESTOR LO VEA.
    //
    //  El rebote ya esta escrito y comprobado cuando esto corre, asi que
    //  publicar es copiar y no mover: si el almacen de medios dice que no, lo
    //  peor que pasa es que los ficheros se quedan donde estaban. Un fallo aqui
    //  no puede costar el trabajo.
    //
    //  Solo cuando la carpeta NO la eligio nadie: si la persona senalo un sitio,
    //  el sitio es ese y publicar una copia en otro seria decidir por ella.
    if (exportOk && ! ProjectStore::exportsElegida())
        publicarExport (salio);

    exportMasterButton.setVisible (true);
    exportStemsButton.setVisible (true);
    exportLiveButton.setVisible (true);
    exportFmtBtn.setVisible (true);
    exportDirBtn.setVisible (true);
    exportCancelButton.setVisible (false);
    exportSheet.repaint();
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
    //  En MILISEGUNDOS y no en ticks, por lo mismo que el tope de la portada:
    //  doce ticks eran 0.4 s en un movil bueno y 1.2 s en uno de gama baja, o
    //  sea que el aparato al que MAS le cuesta abrir un stream era el que mas
    //  gracia se llevaba - justo al reves de lo que hace falta.
    xrunGraceMs = kXRunGraciaMs;
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
void MainComponent::checkXRuns (double dtMs)
{
    auto* dev = deviceManager.getCurrentAudioDevice();
    if (dev == nullptr) { lastXRuns = -1; return; }

    const int now = dev->getXRunCount();
    if (now < 0) return;                     // el dispositivo no lleva la cuenta

    if (xrunGraceMs > 0.0) { xrunGraceMs -= dtMs; lastXRuns = now; return; }
    if (lastXRuns < 0) { lastXRuns = now; return; }

    const int nuevos = now - lastXRuns;
    lastXRuns = now;
    if (nuevos <= 0)
    {
        //  Y LA CUENTA SE OLVIDA. El comentario de arriba dice «cuatro
        //  SEGUIDOS» y `xrunsSeen` no bajaba nunca: cuatro chasquidos
        //  repartidos en una hora de tocar subian el buffer igual que cuatro
        //  en el mismo segundo, o sea que cualquier sesion larga acababa en el
        //  buffer mas grande hubiera hecho falta o no - y ese buffer es
        //  latencia, que es el argumento entero de esta app. Un tramo limpio
        //  lo suficientemente largo y se empieza de cero.
        if (xrunsSeen > 0 && (xrunLimpioMs += dtMs) >= kXRunOlvidoMs)
        {
            xrunsSeen    = 0;
            xrunLimpioMs = 0.0;
        }
        return;
    }

    xrunLimpioMs = 0.0;
    xrunsSeen += nuevos;
    if (xrunsSeen < 4 || burstMult >= kMaxBursts) return;

    ++burstMult;
    ProjectStore::escribeTexto (burstPreferenceFile(), juce::String (burstMult));
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
            setSheet.cuerpo.addAndMakeVisible (b);
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
        setSheet.cuerpo.addAndMakeVisible (b);
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


void MainComponent::guardarKit (const juce::String& nombre)
{
    //  Por la puerta de los componentes de ruta: createLegalFileName quita `\`
    //  y `/` y DEJA los puntos, asi que un kit llamado `..` acababa escribiendo
    //  sus dieciseis WAV en la raiz de la biblioteca. Ver ProjectStore::componente.
    const auto limpio = ProjectStore::componente (
                            juce::File::createLegalFileName (nombre.trim()).substring (0, 60));
    if (limpio.isEmpty())
    {
        status.setText (T ("Ponle nombre primero"), juce::dontSendNotification);
        return;
    }

    const auto dir = ProjectStore::kits().getChildFile (limpio);
    //  Y la POSTCONDICION, que es lo unico que no se puede sortear con otro
    //  nombre raro: si el resultado no cuelga de Kits, no se escribe.
    if (! dir.isAChildOf (ProjectStore::kits()) || ! ProjectStore::ensureDirectory (dir))
    {
        status.setText (T ("No se pudo escribir el kit"), juce::dontSendNotification);
        return;
    }

    const int base = currentBank * kPadsPerBank;
    int escritos = 0, vacios = 0;

    beginBusy (T ("Guardando kit"));
    for (int i = 0; i < kPadsPerBank; ++i)
    {
        setBusyProgress ((float) i / (float) kPadsPerBank);
        const int pad = base + i;
        auto sb = uiSample[(size_t) pad];
        if (sb == nullptr || sb->buffer.getNumSamples() <= 0) { ++vacios; continue; }

        //  El recorte, en muestras de la FUENTE y sacado del buffer que sostiene
        //  la interfaz - nunca de engine.getSampleLength, que lee el puntero que
        //  ha adoptado el hilo de audio y es nulo hasta el primer bloque.
        const int total = sb->buffer.getNumSamples();
        const int desde = juce::jlimit (0, total - 1, (int) (padStart01[(size_t) pad] * (float) total));
        const int hasta = juce::jlimit (desde + 1, total, (int) (padEnd01[(size_t) pad] * (float) total));
        const int n     = hasta - desde;

        juce::AudioBuffer<float> trozo (sb->buffer.getNumChannels(), n);
        for (int c = 0; c < trozo.getNumChannels(); ++c)
            trozo.copyFrom (c, 0, sb->buffer, c, desde, n);

        //  Dos cifras de delante y el nombre detras: el numero manda el orden al
        //  volver y el nombre es para leerlo. Sin las dos cifras, "10" se ordena
        //  antes que "2" y el kit vuelve barajado.
        const auto etiqueta = padName[(size_t) pad].isNotEmpty()
                                ? juce::File::createLegalFileName (padName[(size_t) pad])
                                : juce::String ("pad");
        const auto f = dir.getChildFile (juce::String (i + 1).paddedLeft ('0', 2)
                                           + " " + etiqueta + ".wav");
        if (ProjectStore::writeSample (f, trozo, sb->sourceSampleRate)) ++escritos;
    }
    endBusy();

    status.setText (escritos > 0
                      ? T ("Kit \"%1\": %2 sonidos", limpio, juce::String (escritos))
                      : T ("No hay sonidos en este banco"),
                    juce::dontSendNotification);
    juce::ignoreUnused (vacios);
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

    repartePorBanco (files, T ("CARGAR KIT"));
}

//  EL REPARTO, QUE AHORA TIENE DOS CLIENTES Y POR ESO VIVE APARTE.
//
//  Lo llamaban CARGAR KIT y ahora tambien un instrumento del catalogo, y son
//  el mismo trabajo: n ficheros ordenados por nombre a los n primeros pads del
//  banco de delante. Se saco de dentro en cuanto hubo dos formas de llegar
//  aqui, por lo mismo que `normaliza` salio de dentro de `render`: dos caminos
//  que hacen lo mismo por su cuenta se separan, y el sintoma habria sido "el
//  kit y el instrumento no cargan igual" sin poder decir por que.
void MainComponent::repartePorBanco (const juce::Array<juce::File>& files, const juce::String& motivo)
{
    //  Sobrescribe dieciseis pads: pasa por deshacer, como AUTO CHOP.
    pushUndo (motivo);

    const int base = currentBank * kPadsPerBank;
    const int n    = juce::jmin (files.size(), kPadsPerBank);
    closeAllSheets();

    //  LOS PADS QUE VAN A RECIBIR SE VACIAN AQUI, ANTES DE PEDIR NADA.
    //
    //  Ver ponPadPorDefecto. Y aqui y no dentro de cada respuesta por dos
    //  razones: el reparto es ASINCRONO, asi que limpiar por respuesta dejaria
    //  medio banco viejo si una lectura falla -y "vaciar la mitad es peor que
    //  no vaciar nada"-, y ademas esto es sincrono, que es lo que permite
    //  medirlo desde el banco, donde no hay bucle de mensajes que recoja las
    //  respuestas.
    //
    //  Solo los `n` que reciben sonido: una carpeta de cinco ficheros deja
    //  once pads con su muestra de antes, y reiniciarles los ajustes seria
    //  cambiar en silencio pads que nadie ha sustituido.
    for (int i = 0; i < n; ++i) ponPadPorDefecto (base + i);

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

// ============================================================================
//  LA FICHA DE INSTRUMENTOS. Ver Instrumentos.h.
// ============================================================================

//  DOS PACKS DE MENTIRA PARA EL BANCO, uno abierto y otro de pago.
//
//  El catalogo de verdad depende de lo que haya en el disco de cada telefono,
//  asi que sin esto el banco solo puede medir el pack de dentro - cuatro
//  instrumentos de cuatro tapas - y jamas la rejilla llena ni el candado, que
//  son justo los dos estados donde los rotulos miden otra cosa. Es el mismo
//  andamio que ZATI_SKIN: convierte en ENTRADA lo que si no seria lo que
//  hubiera.
//
//  Un fichero de audio de verdad y no uno vacio: el catalogo cuenta ficheros
//  por su extension, pero cargar el instrumento los abre, y un banco que
//  planta ceros mediria la mitad del camino.
// ============================================================================
//  LA APP CON TRABAJO DENTRO.
//
//  Todo lo que este banco mide -las once reglas de `expo.py`, la carga de cada
//  pantalla, los planos, el coste por cuadro- se mide sobre una maquina VACIA o
//  con el kit de fabrica. Un proyecto de treinta pads con nombres de fichero de
//  verdad, ocho patrones llenos, una cancion de sesenta y cuatro compases con
//  sus clips y su automatizacion no lo abria ningun banco, y esa es media app
//  sin medir: casi todo lo que puede fallar aqui es una fila que cabia vacia.
//
//  NO ES UNA REGLA NUEVA: es un ESTADO nuevo para las que ya hay. Exactamente
//  lo mismo que `ZATI_DLC` plantando dos packs, `ZATI_SKIN` fijando la carcasa
//  y `ZATI_SONANDO` bombeando bloques — convertir en ENTRADA lo que si no seria
//  «lo que hubiera».
//
//  Y con NOMBRES LARGOS, que es la mitad del valor: un pad se llama como el
//  fichero que cargaste y no «01», y la cabecera, la mesa y el troceado
//  reparten su ancho por el TEXTO.
void MainComponent::llenaDePrueba()
{
    //  Nombres de fichero de verdad y no «PAD 12»: lo que se mide es lo que
    //  pasa cuando el rotulo mide lo que mide fuera del banco.
    static const char* kNombres[] =
    {
        "amen_break_bar1", "808 sub deep long", "vinyl crackle loop",
        "snare_rimshot_hard", "hihat closed tight", "clap layered wide",
        "kick punchy 60hz", "perc shaker offbeat", "tom floor low",
        "crash cymbal long", "bass saw detuned", "pad strings warm",
        "vocal chop female", "fx riser 4 bars", "impact hit big",
        "conga slap high", "rim click short", "ride bell ping",
        "sub drop long tail", "noise sweep down"
    };
    const int nNombres = (int) (sizeof (kNombres) / sizeof (kNombres[0]));

    //  TREINTA PADS, que es un kit de trabajo y no los sesenta y cuatro de
    //  fabrica: lo que se quiere medir es una maquina usada, no una llena.
    for (int i = 0; i < 30 && i < kNumPads; ++i)
    {
        padName[(size_t) i] = juce::String (kNombres[i % nNombres]);
        refreshPadArt (i);
        refreshPad (i);
    }

    //  LOS OCHO PATRONES LLENOS, con nota y fuerza distintas por casilla: un
    //  patron de unos y ceros no mueve un solo rotulo de la tira del paso.
    for (int pat = 0; pat < AudioEngine::kNumPatterns; ++pat)
    {
        engine.setPatternLength (pat, 32);
        for (int st = 0; st < 32; ++st)
            for (int pad = 0; pad < 16; ++pad)
                if (((st + pad * 3 + pat) % 5) == 0)
                {
                    engine.setStep     (pat, st, pad, true);
                    engine.setStepNote (pat, st, pad, ((st + pad) % 25) - 12);
                    engine.setStepVel  (pat, st, pad, 40 + (st * 5 + pad * 7) % 87);
                }
    }

    //  Y LA CANCION ENTERA. Sesenta y cuatro compases es el tope que la app
    //  admite, y es donde la linea de tiempo reparte su celda mas estrecha.
    engine.setSongLength (AudioEngine::kSongBars);
    for (int bar = 0; bar < AudioEngine::kSongBars; ++bar)
        for (int lane = 0; lane < 4; ++lane)
            if (((bar + lane) % 3) != 2)
                engine.setSongCell (lane, bar, 1 + ((bar / 2 + lane) % AudioEngine::kNumPatterns));

    //  Cuatro clips de audio, uno por pista, del recorte de su pad — que es lo
    //  que `ponClip` hace y por eso se llama a `ponClip` y no se rellena la
    //  lista a mano: un clip escrito aqui no pasaria por el recorte.
    const int guardado = selectedPad;
    for (int pista = 0; pista < 4; ++pista)
    {
        selectedPad = pista * 4;
        ponClip (pista, pista * 8);
    }
    selectedPad = guardado;

    //  Y una automatizacion de verdad: un barrido por compas sobre el primer
    //  parametro del primer efecto.
    autoEventos.clear();
    for (int bar = 0; bar < AudioEngine::kSongBars; ++bar)
    {
        AudioEngine::EventoAuto e;
        e.paso  = bar * 16;
        e.fx    = 0;
        e.par   = 0;
        e.valor = (float) bar / (float) AudioEngine::kSongBars;
        autoEventos.push_back (e);
    }
    publicaAutomacion();

    //  Con nombre de proyecto y su fecha, que es lo que la cabecera dice desde
    //  esta misma tanda. Sin esto la linea de continuidad se mediria en el
    //  unico estado en el que no tiene nada que contar.
    currentProject = "sesion nocturna larga";
    proyectoFecha  = juce::Time::getCurrentTime() - juce::RelativeTime::days (2);

    refreshStepGrid();
    refreshSong (false);
    refreshMixStrip();
    resized();
}

void MainComponent::plantaPacksDePrueba()
{
    const auto raiz = ProjectStore::instrumentos();
    if (! raiz.isDirectory()) return;

    juce::AudioBuffer<float> pip (1, 480);          // 10 ms a 48 k
    for (int i = 0; i < pip.getNumSamples(); ++i)
        pip.setSample (0, i, 0.2f * std::sin (juce::MathConstants<float>::twoPi * 440.0f
                                              * (float) i / 48000.0f));

    //  Nombres LARGOS a proposito en uno de los dos: el rotulo de una tapa de
    //  una rejilla de cuatro por cuatro es lo primero que se corta en 280 px,
    //  y un pack real se llamara "ELECTRIC PIANO" y no "EP".
    static const char* kNombres[Instrumentos::kMaxInstr] =
    { "SUBBASS", "ELECTRIC PIANO", "STRINGS", "BRASS", "CHOIR", "MARIMBA",
      "SYNTH LEAD", "PLUCK", "ORGAN", "CLAV", "FLUTE", "BELL",
      "PAD WARM", "SAW STACK", "UPRIGHT BASS", "GLASS" };

    for (int p = 0; p < 2; ++p)
    {
        auto pack = raiz.getChildFile (p == 0 ? "01 DEMO" : "02 ESTUDIO");
        pack.createDirectory();
        //  El segundo declara que es de pago y NO lleva licencia, que es la
        //  unica forma de medir el candado cerrado.
        pack.getChildFile ("pack.txt")
            .replaceWithText (p == 0 ? "nombre=DEMO\n" : "nombre=ESTUDIO\npago=1\n");

        for (int i = 0; i < Instrumentos::kMaxInstr; ++i)
        {
            auto dir = pack.getChildFile (juce::String (i + 1).paddedLeft ('0', 2)
                                          + " " + kNombres[i]);
            dir.createDirectory();
            auto f = dir.getChildFile ("01 " + juce::String (kNombres[i]) + ".wav");
            if (f.existsAsFile()) continue;
            std::unique_ptr<juce::FileOutputStream> out (f.createOutputStream());
            if (out == nullptr || ! out->openedOk()) continue;
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> w (
                wav.createWriterFor (out.get(), 48000.0, 1, 16, {}, 0));
            if (w == nullptr) continue;
            out.release();
            w->writeFromAudioSampleBuffer (pip, 0, pip.getNumSamples());
        }
    }
}


void MainComponent::openInstSheet()
{
    //  EL CATALOGO SE LEE AL ABRIR Y NO AL PINTAR. Es un barrido de directorio,
    //  y hacerlo treinta veces por segundo para una lista que no cambia es el
    //  mismo derroche que costo la CPU de la cara. Y al ABRIR y no una sola vez
    //  al arrancar, porque un pack puede aparecer con la app puesta: se copia
    //  por cable, o lo deja la tienda mientras esto esta en segundo plano.
    instCatalogo = Instrumentos::lee();
    instPack = juce::jlimit (0, juce::jmax (0, (int) instCatalogo.size() - 1), instPack);
    refreshInst();
    closeAllSheets();

    //  Y EL TOQUE AL PAD DE DETRAS. Esta ficha tampoco pasa por `openSheet`, y
    //  aqui el toque NO es `tocaPadDetras`: lo que esta ficha edita no es el
    //  pad elegido sino su DESTINO -la rejilla de arriba, la que dice DONDE- y
    //  seleccionar un pad que la ficha ignora seria un toque que parece hacer
    //  algo. Se escribe el destino, que es exactamente lo que hace esa rejilla.
    instSheet.onFuera = [this] (juce::Point<int> p)
    {
        const int i = padDetras (p);
        if (i < 0) return false;
        instDestPad   = i;
        instBancoDest = i / kPadsPerBank;
        refreshInst();
        resized();
        repaint();
        return true;
    };

    instSheet.setVisible (true);
    instSheet.toFront (false);
    resized();
}

void MainComponent::pasoPack (int d)
{
    if (instCatalogo.empty()) return;
    const int n = (int) instCatalogo.size();
    //  DA LA VUELTA. Con dos packs, un menos que se queda quieto en el primero
    //  es una tapa que a veces no hace nada, y eso no se distingue de una rota.
    instPack = (instPack + d % n + n) % n;
    refreshInst();
    resized();
    instSheet.repaint();
}

//  SI EL DIBUJO NO CABE EN UNA CELDA, NO LO LLEVA NINGUNA.
//
//  Es filaDeIconos con otra forma, y ahora dice lo MISMO: se cae el dibujo,
//  que es el adorno, y se queda la palabra, que es la funcion.
//
//  Antes se caia al reves y por eso el menu de instrumentos eran dieciseis
//  dibujos mudos - "no se cual es el nombre de cada uno". La cuenta lo dice:
//  en 412x915 la celda de cuatro columnas mide 82 px y su caja de rotulo 72,
//  "CUERDA PULS" pide 64.5 y CABE; lo que no cabe son 64.5 + 4 de aire + 13
//  del dibujo mas pequeno posible. La pregunta "caben los dos?" se contestaba
//  que no y se quitaba la palabra, en una casa cuya regla escrita es que un
//  apreton no se cambia por un corte y que el rotulo manda. Ver reparteTapa.
//
//  Y se decide para las dieciseis a la vez, por lo mismo que una fila decide
//  junta: media rejilla con dibujo y media sin el se lee como una celda rota.
void MainComponent::rejillaDeIconos (juce::OwnedArray<juce::TextButton>& celdas, int n)
{
    const int cuantas = juce::jmin (n, celdas.size());
    //  La marca se borra ANTES de preguntar, o la respuesta de la pasada
    //  anterior entra en la pregunta de la siguiente y la rejilla parpadea
    //  entre con y sin dibujos cada vez que se gira el telefono. Es el mismo
    //  fallo que tuvo filaDeIconos en su primera version.
    for (int i = 0; i < cuantas; ++i)
        celdas[i]->getProperties().remove ("sinIcono");

    bool todas = true;
    for (int i = 0; i < cuantas && todas; ++i)
        if ((int) celdas[i]->getProperties().getWithDefault ("icono", 0) != 0)
            todas = ZatiLookAndFeel::reparteTapa (*celdas[i]).id != Iconos::Id::ninguno;

    if (! todas)
        for (int i = 0; i < cuantas; ++i)
            celdas[i]->getProperties().set ("sinIcono", 1);
}

void MainComponent::refreshInst()
{
    if (instCatalogo.empty()) return;
    const auto& p = instCatalogo[(size_t) juce::jlimit (0, (int) instCatalogo.size() - 1, instPack)];

    //  LA REJILLA DEL DESTINO: cada celda es un pad y lleva el dibujo de lo que
    //  ya tiene. Un pad con muestra normal enseña su numero y nada mas; solo un
    //  instrumento tiene dibujo, que es lo que hace que el mapa se lea.
    instDestPad   = juce::jlimit (0, kNumPads - 1, instDestPad);
    instBancoDest = juce::jlimit (0, kNumBanks - 1, instBancoDest);
    for (int i = 0; i < instDestBtns.size(); ++i)
    {
        const int pad = instBancoDest * kPadsPerBank + i;
        instDestBtns[i]->setButtonText (juce::String (pad + 1).paddedLeft ('0', 2));
        const auto id = padEsInstrumento (pad)
                            ? Iconos::deFamilia (uiSample[(size_t) pad]->familia)
                            : Iconos::Id::ninguno;
        if (id != Iconos::Id::ninguno) instDestBtns[i]->getProperties().set ("icono", (int) id);
        else                           instDestBtns[i]->getProperties().remove ("icono");
        instDestBtns[i]->setToggleState (pad == instDestPad, juce::dontSendNotification);
        //  El numero es dato: es el mismo en los cuatro idiomas a proposito.
        instDestBtns[i]->getProperties().set ("dato", 1);
    }
    for (int b4 = 0; b4 < instBancoBtns.size(); ++b4)
        instBancoBtns[b4]->setToggleState (b4 == instBancoDest, juce::dontSendNotification);

    //  EL CANDADO SE VE, no se esconde. Un pack cerrado que no aparece no se
    //  compra nunca: lo que hace falta es que se vea QUE hay y que al tocarlo
    //  diga por que no suena.
    const juce::String candado (juce::CharPointer_UTF8 ("\xf0\x9f\x94\x92 "));
    for (int i = 0; i < instBtns.size(); ++i)
    {
        const bool hay = i < (int) p.instr.size();
        //  LA FABRICA SE TRADUCE Y LO DEL DISCO NO, que son dos cosas y el
        //  banco las separo: ACUSTICA / MAQUINA / TEXTURA / TONOS estan en la
        //  tabla y se listaban en crudo -identicas en los cuatro idiomas, y la
        //  regla comparativa lo canto-, mientras que el nombre de una carpeta
        //  es dato y traducirlo no significa nada.
        const auto nombre = ! hay ? juce::String()
                          : (p.dentro ? T (p.instr[(size_t) i].nombre) : p.instr[(size_t) i].nombre);
        instBtns[i]->setButtonText (hay && ! p.abierto ? candado + nombre : nombre);
        //  Y SU DIBUJO, que es lo que permite que esto sea una rejilla: en una
        //  celda de 56 px no cabe "CUERDA PULS" y si cabe su icono.
        const auto id = (hay && p.instr[(size_t) i].familiaSintes >= 0)
                            ? Iconos::deFamilia (p.instr[(size_t) i].familiaSintes)
                            : Iconos::Id::ninguno;
        if (id != Iconos::Id::ninguno) instBtns[i]->getProperties().set ("icono", (int) id);
        else                           instBtns[i]->getProperties().remove ("icono");
        //  Y lo que sale de una carpeta se marca como dato, o la regla de
        //  traduccion lo cuenta como sin traducir - 168 hallazgos falsos en la
        //  primera corrida, todos tapando los de verdad. Ver UiAudit::walk.
        if (hay && ! p.dentro) instBtns[i]->getProperties().set ("dato", 1);
        else                   instBtns[i]->getProperties().remove ("dato");
    }
    //  Y las dos tapas del pack se apagan cuando solo hay uno: con un unico
    //  pack instalado, menos y mas vuelven al mismo sitio.
    const bool varios = instCatalogo.size() > 1;
    instPackDownBtn.setEnabled (varios);
    instPackUpBtn  .setEnabled (varios);
}

void MainComponent::cargaInstrumento (int idx)
{
    if (instCatalogo.empty()) return;
    const auto& p = instCatalogo[(size_t) juce::jlimit (0, (int) instCatalogo.size() - 1, instPack)];
    if (! juce::isPositiveAndBelow (idx, (int) p.instr.size())) return;
    const auto& in = p.instr[(size_t) idx];

    if (! p.abierto)
    {
        status.setText (T ("%1 no esta comprado", p.nombre), juce::dontSendNotification);
        return;
    }

    //  UN INSTRUMENTO DE SINTES VA A SU PAD, Y CON EL PRIMER PRESET.
    //
    //  Aqui NO se elige el sonido concreto: elegir el instrumento y elegir su
    //  preset son dos decisiones distintas y en dos momentos distintos. La
    //  segunda vive en la ficha del pad, que es donde se edita todo lo demas
    //  de un pad, y ademas con el teclado delante - o sea pudiendo OIRLOS,
    //  que es la unica forma de elegir un sonido.
    //
    //  Y no lleva confirmacion, a diferencia del reparto de abajo: se lleva UN
    //  pad por delante y no dieciseis, igual que CARGAR.
    if (in.familiaSintes >= 0)
    {
        const int fam = juce::jlimit (0, Sintes::kFamilias - 1, in.familiaSintes);
        //  EL DESTINO SE ELIGE, no viene dado por la familia. Era el pad de su
        //  mismo numero en el banco D, que hacia el mapa previsible y tambien
        //  imposible: no se podian tener dos CUERDAS ni dejar un pad de
        //  percusion en medio del banco melodico. El banco D sigue siendo por
        //  donde ABRE la ficha, que es lo que valia de aquello.
        const int pad = juce::jlimit (0, kNumPads - 1, instDestPad);

        pushUndo (T ("INSTRUMENTOS"));
        //  Y LA FICHA NO SE CIERRA. Ahora hay un destino puesto y dieciseis
        //  instrumentos que probar contra el: cerrar en cada toque obligaria a
        //  volver a abrir y a volver a elegir el pad para oir el siguiente.
        ponInstrumentoEnPad (pad, fam, 0);
        selectBank (pad / kPadsPerBank);
        for (int i = 0; i < kPadsPerBank; ++i) refreshPad ((pad / kPadsPerBank) * kPadsPerBank + i);
        selectPad (pad);
        refreshInst();
        instSheet.repaint();

        status.setText (Sintes::nombreDe (fam, 0) + "  "
                            + juce::String::charToString ((juce::juce_wchar) 0x00B7) + "  "
                            + T ("PAD %1", Lang::ltr (juce::String (pad + 1))),
                        juce::dontSendNotification);
        return;
    }

    //  SE LLEVA DIECISEIS PADS POR DELANTE, asi que se confirma - la misma
    //  regla que ya tenian FABRICA y AUTO CHOP, y en la misma tapa que se
    //  acaba de tocar para que la pregunta este donde estaba el dedo.
    if (auto* b = instBtns[idx])
    {
        if (! armConfirm (*b, T ("SOBRESCRIBIR %1?",
                                 juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)))))
            return;
        disarmConfirm();
    }

    if (in.bancoFabrica >= 0)
    {
        //  LA FABRICA NO SON FICHEROS. Se sintetiza o sale de los recursos
        //  incrustados, asi que entra por su propia puerta y no por el reparto.
        closeAllSheets();
        cargaFabricaEnBanco (in.bancoFabrica, currentBank);
        status.setText (T ("Banco %1: %2",
                           juce::String::charToString ((juce::juce_wchar) ('A' + currentBank)),
                           T (Kits::bankName (in.bancoFabrica))),
                        juce::dontSendNotification);
        return;
    }

    const auto files = Instrumentos::presetsDe (in);
    if (files.isEmpty())
    {
        //  Una carpeta que tenia audio cuando se leyo el catalogo y no lo tiene
        //  ahora: la tarjeta se ha desmontado, o alguien la ha vaciado desde el
        //  gestor de ficheros con la app abierta. Se dice, no se carga medio.
        status.setText (T ("No hay audio en esta carpeta"), juce::dontSendNotification);
        return;
    }
    repartePorBanco (files, T ("INSTRUMENTOS"));
}

// ----------------------------------------------------------------------------
//  UN PRESET VA A UN PAD, y siempre al MISMO pad.
//
//  El instrumento numero n va al pad n del banco D. Que este clavado no es una
//  limitacion, es la funcion: los otros tres bancos son percusion y este es el
//  melodico -ya lo era, se llamaba TONOS-, asi que las dieciseis casillas de la
//  rejilla son los dieciseis instrumentos y el 07 esta donde la mano lo busca
//  sin acordarse de donde lo dejo. Es la misma decision que hizo que el
//  selector del RACK dejara de ser una fila de dieciseis y pasara a tener la
//  forma de la cara.
//
//  Y se cambia de banco Y se elige el pad: cargar algo donde no se ve es la
//  forma mas rapida de que parezca que no ha pasado nada.
// ----------------------------------------------------------------------------
void MainComponent::eligePreset (int pre)
{
    if (! padEsInstrumento (vstPad) || ! juce::isPositiveAndBelow (pre, Sintes::kPresets)) return;
    const int fam = uiSample[(size_t) vstPad]->familia;

    //  EL MISMO PRESET SOLO SUENA. Volver a sintetizarlo costaria 77 ms para
    //  dejar el pad exactamente como estaba, y ademas se llevaria un paso de
    //  deshacer por no hacer nada.
    if (uiSample[(size_t) vstPad]->preset != pre)
    {
        pushUndo (T ("PRESETS"));
        //  SE SUELTA LO QUE ESTE SONANDO ANTES DE CAMBIAR LA MUESTRA. Debajo
        //  del pad se cambia el buffer entero y sus diez zonas, asi que una voz
        //  viva se quedaria leyendo la ventana de la zona ANTERIOR sobre el
        //  sonido nuevo: recortada contra el buffer, o sea sin reventar, pero
        //  sonando lo que no es.
        engine.postNoteOff (vstPad);
        ponInstrumentoEnPad (vstPad, fam, pre);
        refreshVst();
    }

    //  Y NO SUENA AL ELEGIRLO.
    //
    //  Sonaba: "un preset que hay que ir a tocar al pad para saber como suena
    //  es una lista de nombres". El argumento vale para una LISTA, donde se
    //  toca uno y se decide; con las flechas se pasa por los dieciseis para
    //  buscar, y entonces cada toque encima una nota mas - y como un
    //  instrumento sostiene, ninguna se acababa. Quien suena es el teclado, que
    //  esta justo encima y ademas deja elegir la nota.

    status.setText (Sintes::nombreDe (fam, pre), juce::dontSendNotification);
}

//  SINTETIZAR TARDA, asi que esto no puede vivir en el hilo de audio ni en una
//  respuesta a un toque que tenga que pintar antes. Corre en el de mensajes -
//  como leer un WAV - y por eso la ficha se cierra primero: lo que se ve es la
//  rejilla de pads mientras se hace, y no una tarjeta congelada.
void MainComponent::ponInstrumentoEnPad (int pad, int familia, int preset)
{
    if (! juce::isPositiveAndBelow (pad, kNumPads)) return;
    auto sb = Sintes::sintetiza (familia, preset);
    if (sb == nullptr) return;

    assignSampleToPad (pad, sb, Sintes::nombreDe (familia, preset));

    //  Y NACE CON EL RECORTE ENTERO Y SIN BUCLE DE PAD: las zonas mandan sobre
    //  las dos cosas dentro del motor -triggerPad las ignora- pero la interfaz
    //  las sigue ensenando, y un pad que dice "BUCLE" sin que el mando haga
    //  nada es peor que uno que no lo dice.
    //  Y CON UNA CAIDA DE INSTRUMENTO, no la de fabrica.
    //
    //  Un pad nace con 5 ms de caida, que es lo correcto para percusion -una
    //  muestra ya se acaba sola, y esos 5 ms solo quitan el chasquido del
    //  final-. En un instrumento la caida ES el final de la nota: soltar la
    //  tecla con 5 ms es un corte, y se oye como un chasquido al levantar el
    //  dedo. 180 ms es lo que tarda en apagarse una cuerda pulsada al
    //  silenciarla con la mano. Sigue siendo un defecto: el mando CAIDA manda.
    padRelease[(size_t) pad] = 180.0f;
    engine.setPadRelease (pad, 180.0f);

    padStart01[(size_t) pad] = 0.0f;
    padEnd01[(size_t) pad]   = 1.0f;
    padLoop[(size_t) pad]    = false;
    padReverse[(size_t) pad] = false;
    padKeepLen[(size_t) pad] = false;
    engine.setPadLoop    (pad, false);
    engine.setPadReverse (pad, false);
    engine.setPadKeepLength (pad, false);

    refreshPad (pad);
}

// ----------------------------------------------------------------------------
//  LA FICHA DEL INSTRUMENTO.
// ----------------------------------------------------------------------------
void MainComponent::abreVst()
{
    if (! padEsInstrumento (selectedPad)) return;
    vstPad = selectedPad;
    //  El teclado empieza en el tono que el pad tiene puesto, redondeado a la
    //  octava: abrir siempre en el cero dejaria un bajo afinado dos octavas
    //  abajo sonando en un sitio que no es el suyo.
    const int t = (int) std::lround (padPitch[(size_t) vstPad]);
    vstTeclado.setBase (juce::jlimit (-24, 12, (t >= 0 ? t / 12 : (t - 11) / 12) * 12));
    refreshVst();
    closeAllSheets();

    //  Y EL TOQUE AL PAD DE DETRAS, que esta ficha no tenia porque no pasa por
    //  `openSheet`. Cerrarse al tocar un pad es justo lo contrario de lo que se
    //  espera de la ficha que EDITA un pad: obliga a cerrar, elegir y volver a
    //  abrir con la rejilla delante todo el rato, que es el viaje que
    //  `tocaPadDetras` existe para quitar.
    //
    //  Aqui no vale `tocaPadDetras`, que termina en `selectPad`: esta ficha
    //  mira `vstPad` y no el pad elegido, asi que seleccionar y no retargetear
    //  seria un toque que parece hacer algo y no hace nada. Y si el pad tocado
    //  NO lleva instrumento, la ficha se cierra -devolviendo false-, porque una
    //  ficha de instrumento de un pad sin instrumento no existe.
    vstSheet.onFuera = [this] (juce::Point<int> p)
    {
        const int i = padDetras (p);
        if (i < 0 || ! padEsInstrumento (i)) return false;
        //  Retargeteado a mano y no llamando a `abreVst`: esa reasigna este
        //  mismo `std::function` desde DENTRO de su propia llamada, o sea
        //  destruye el objeto que se esta ejecutando.
        selectPad (i);
        vstPad = i;
        const int t = (int) std::lround (padPitch[(size_t) vstPad]);
        vstTeclado.setBase (juce::jlimit (-24, 12, (t >= 0 ? t / 12 : (t - 11) / 12) * 12));
        refreshVst();
        resized();
        repaint();
        return true;
    };

    vstSheet.setVisible (true);
    vstSheet.toFront (false);
    resized();
}

void MainComponent::refreshVst()
{
    auto* sb = uiSample[(size_t) juce::jlimit (0, kNumPads - 1, vstPad)].get();
    const int fam = (sb != nullptr) ? sb->familia : -1;
    const int pre = (sb != nullptr) ? sb->preset  : -1;

    const bool hayPreset = (fam >= 0 && pre >= 0);
    vstPreDown.setEnabled (hayPreset);
    vstPreUp  .setEnabled (hayPreset);
    //  Los topes del teclado son los mismos que admite un paso: setStepNote
    //  acota en +-24, asi que pasear mas alla seria escribir notas que el motor
    //  recorta - las mismas doce filas que mentian en el piano roll.
    vstOctDown.setEnabled (vstTeclado.getBase() > -24);
    vstOctUp  .setEnabled (vstTeclado.getBase() < 12);
    vstSheet.repaint();
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


// REC on the transport arms PATTERN recording: pads you hit while the
// sequencer runs are written into the playing bank, quantised to the nearest
// step. Sampling from the mic is a per-pad action and lives in the PADS sheet.
//  ARMAR EL SOLO, Y UN SOLO MODO ARMADO A LA VEZ.
//
//  LOAD y SOLO son la misma clase de estado —tocar un pad deja de sonar y hace
//  otra cosa— y los dos entran por el mismo embudo, `padClicked`. Con los dos
//  armados a la vez habria que decidir cual gana, y esa es una pregunta que no
//  deberia existir: armar uno desarma el otro. REC no, que REC no cambia lo que
//  el toque hace sino lo que ADEMAS ocurre, y por eso convive con los dos.
void MainComponent::ponSoloArmado (bool on)
{
    if (soloArmado == on) { refrescaRejillaModo(); return; }
    soloArmado = on;

    if (soloArmado && loadArmed)
    {
        loadArmed = false;
        loadButton.setToggleState (false, juce::dontSendNotification);
    }

    styleButton (soloButton, soloArmado ? kRec : kKey);
    soloButton.setToggleState (soloArmado, juce::dontSendNotification);

    status.setText (soloArmado
                      ? T ("SOLO: toca pads para aislarlos - manten SOLO para quitarlos todos")
                      : T ("SOLO apagado"),
                    juce::dontSendNotification);
    refrescaRejillaModo();
}

//  LA REJILLA ENTERA, que es donde se ve el modo y quien esta callado. Los dos
//  estados son de los dieciseis a la vez -«cual suena» solo significa algo
//  comparando- asi que se repasan los dieciseis y no el que se acaba de tocar.
void MainComponent::refrescaRejillaModo()
{
    for (int i = 0; i < kNumPads; ++i) refreshPad (i);
    refreshMixStrip();
}

void MainComponent::toggleRecordArm()
{
    recArmed = ! recArmed;
    styleButton (recButton, recArmed ? kRec : kKey);
    recButton.setButtonText (recArmed ? T ("REC ON") : T ("REC"));

    if (recArmed && ! engine.isPlaying())
    {
        // Arming with the transport stopped is a dead end — roll it.
        ponTransporte (true);
    }

    status.setText (recArmed ? T ("REC: toca pads para grabarlos en el patron")
                             : T ("REC apagado"),
                    juce::dontSendNotification);
    //  Y la rejilla lo dice: el aviso de que tocar un pad hace otra cosa tiene
    //  que estar donde esta el dedo.
    refrescaRejillaModo();
    repaint();
}


void MainComponent::esperaExport (bool cancelar, int vueltas)
{
    //  El temporizador de la app es quien llama a pollExport; aqui solo se
    //  espera a que termine, que es lo que hace la persona mirando la barra.
    if (exportJob != nullptr && vueltas < 600)
    {
        juce::Timer::callAfterDelay (50, [this, cancelar, vueltas] { esperaExport (cancelar, vueltas + 1); });
        return;
    }

    juce::Array<juce::File> hechos;
    ProjectStore::exports().getChildFile ("BANCO_EXPORT")
        .findChildFiles (hechos, juce::File::findFiles, false, "*.wav");

    std::cout << "{\"export\":\"" << (cancelar ? "cancelado" : "asincrono")
              << "\",\"esperas\":" << vueltas
              << ",\"ok\":" << (exportOk ? 1 : 0)
              << ",\"job\":" << (exportJob == nullptr ? 0 : 1)
              << ",\"ficheros\":" << hechos.size()
              << ",\"parte\":\"" << exportStatus << "\"}" << std::endl;

    if (auto* app = juce::JUCEApplication::getInstance()) app->systemRequestedQuit();
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

    //  Y LA ULTIMA LINEA DE LA CAJA NEGRA, aqui y no solo en `shutdown()`.
    //
    //  En Android este es el unico gancho que corre al salir: ATRAS manda la
    //  tarea al fondo sin destruir la actividad, y lo que la mata despues es un
    //  SIGKILL. Sin esta linea el fichero se quedaba SIEMPRE sin «fin limpio» y
    //  el arranque siguiente daba un parte de caida que no lo era. Va la
    //  ULTIMA, cuando ya no queda nada que pueda fallar en esta funcion: si
    //  algo de arriba se cayera, eso si es una caida y tiene que constar.
    Bitacora::finLimpio();
}

void MainComponent::appResumed()
{
    //  LO PRIMERO, que es lo que hace util al «fin limpio» de appSuspended:
    //  mientras la app este delante, la ultima linea no puede decir que se
    //  cerro bien. Sin esto, una caida DESPUES de volver de segundo plano se
    //  leeria como un cierre correcto.
    Bitacora::reanudada();

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
        ponTransporte (true);
    }

    //  Whatever else happened out there, the master comes back up.
    duckedByFocus = false;
    duckTicksLeft = 0;
    engine.setDucked (false);

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
        ponTransporte (false);

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
    //  Y NO "pon el master a 0.28": eso se llevaria por delante el nivel que la
    //  persona haya dejado puesto. Se enciende el aviso y el motor multiplica.
    engine.setDucked (true);
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
        engine.setDucked (false);
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
        ponTransporte (true);
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

    //  Y POR LA MISMA PUERTA QUE LA SESION. Esto era un replaceWithText a pelo
    //  ocho lineas debajo de la llamada a writeState, que lleva doce lineas de
    //  comentario explicando por que eso pierde ficheros. Y corre en cada
    //  onPause, o sea en el instante en el que Android mata el proceso.
    ProjectStore::escribeTexto (folder.getChildFile ("project.xml"),
                                state.toXmlString(), ProjectStore::esXmlLegible);
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

        //  Si este pad sale de otro, se comparte el buffer en vez de leer un
        //  fichero: es lo que devuelve el troceado tal y como estaba, con sus
        //  hermanos y sin dieciseis copias del mismo break. Ver captureState.
        const int fuente = padJob->source[(size_t) i];
        if (fuente >= 0 && fuente < i && uiSample[(size_t) fuente] != nullptr)
        {
            assignSampleToPad (i, uiSample[(size_t) fuente], padName[(size_t) i]);
            ++padJob->restored;
            continue;
        }

        //  Y SI ERA UN INSTRUMENTO, se vuelve a sintetizar. Es mas rapido que
        //  leer 2 MB de disco y ademas es lo unico que lo devuelve SIENDO un
        //  instrumento: con sus cinco octavas y sus dos capas.
        const int receta = padJob->inst[(size_t) i];
        if (receta >= 0)
        {
            if (auto sb = Sintes::sintetiza (receta / Sintes::kPresets, receta % Sintes::kPresets))
            {
                assignSampleToPad (i, sb, Sintes::nombreDe (receta / Sintes::kPresets,
                                                            receta % Sintes::kPresets));
                ++padJob->restored;
                continue;
            }
        }

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
        //  Y la cancion con el patron 1 puesto, por lo mismo: una maquina que
        //  abre por primera vez tiene que traer algo que tocar en todas sus
        //  paginas, no solo en la rejilla de pads.
        songPorDefecto();

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
    readSourceMap (tree, padJob->source);
    readInstMap   (tree, padJob->inst);
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

    apuntaProyecto (tree.getProperty ("proyecto", "").toString());
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

    //  Y SI LA VEZ ANTERIOR NO ACABO BIEN, se dice. Una app que se cierra sola
    //  y vuelve a abrir como si nada deja a la persona sin nada que contar y a
    //  quien lo arregla sin nada que mirar. Ver Bitacora.h.
    if (Bitacora::previa.isNotEmpty())
        status.setText (T ("La vez anterior se cerro en: %1", Bitacora::previa),
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

        //  El mismo destino que una toma de microfono al arreglo, y por la
        //  misma razon: un remuestreo es una toma. Una regla, un dueño.
        const int slot = padParaToma();
        if (slot < 0)
        {
            status.setText (T ("El banco de tomas esta lleno: vacia un pad o elige otro"),
                            juce::dontSendNotification);
            return;
        }

        resamplingSlot   = slot;
        resamplingActive = true;
        engine.startRecording (slot, true);

        styleButton (resampleButton, kRec);
        resampleButton.setButtonText (T ("PARAR"));
        status.setText (T ("Remuestreando al pad %1", etiquetaPad (slot)),
                        juce::dontSendNotification);
        return;
    }

    resamplingActive = false;
    styleButton (resampleButton, kKey);
    resampleButton.setButtonText (T ("REMUESTREAR"));

    if (auto sb = engine.finishRecording())
    {
        pushUndo (T ("REMUESTREAR"));
        assignSampleToPad (resamplingSlot, sb, "TOMA " + juce::String (resamplingSlot % kPadsPerBank + 1));
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
            //  Y EL BURST MAS CORTO TAMBIEN AL ABRIR, que solo se pedia al
            //  CERRAR: `setAudioChannels` reabre el dispositivo en duplex con
            //  el tamano por defecto del driver, asi que la toma entera corria
            //  con el bloque grande. No se notaba mientras no hubiera monitor
            //  —lo que se graba llega igual— y con monitor es latencia de
            //  monitor regalada, que es justo lo que un cantante oye.
            useLowestLatency();
            recordingSlot = slot;
            //  Y la ruta se vuelve a preguntar AQUI, con el dispositivo ya
            //  reabierto: los cascos se enchufan justo antes de grabar, que es
            //  cuando mas probable es. Ver `aplicaMonitor`.
            aplicaMonitor (false);
            //  Y AQUI TAMBIEN LA CUENTA ATRAS, que era la mitad que faltaba: una
            //  toma que entra a ojo entra corrida, la grabes sobre el arreglo o
            //  sola. El mecanismo es el mismo que ya usaba el otro camino - lo
            //  unico que no se hace aqui es poner el modo cancion, porque
            //  muestrear una guitarra suelta no es tocar el arreglo.
            const bool espera = armaCuentaSiToca (slot);
            if (! espera) engine.startRecording (slot);
            recordingActive = true;
            styleButton (micButton, kRec);
            micButton.setButtonText (T ("PARAR"));
            status.setText (espera
                              ? T ("Cuenta atras: la toma entra en el compas")
                              : T ("Grabando pad %1  %2s / %3s", etiquetaPad (slot), "0.0",
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

        //  Y LA CONTINUA FUERA, que es lo que trae cualquier entrada de movil.
        //
        //  Un convertidor no centra su cero exactamente: la toma sale montada
        //  sobre un valor fijo pequeno. No se oye -es 0 Hz- y hace tres danos
        //  que si se notan: se come margen de volumen por el lado al que este
        //  desplazada, mete un clic al empezar y al acabar porque el primer
        //  dato no vale cero, y al normalizar sube ese desplazamiento como si
        //  fuera senal. Se quita restando la MEDIA de cada canal, que es la
        //  definicion, y no con un paso alto: un filtro tambien se llevaria los
        //  graves de verdad, y en un bombo eso es el bombo.
        if (sb != nullptr && sb->buffer.getNumSamples() > 16)
        {
            for (int ch = 0; ch < sb->buffer.getNumChannels(); ++ch)
            {
                const int n = sb->buffer.getNumSamples();
                const auto* d = sb->buffer.getReadPointer (ch);
                double suma = 0.0;
                for (int i = 0; i < n; ++i) suma += (double) d[i];
                const float media = (float) (suma / (double) n);
                //  Por debajo de -60 dBFS no hay continua que quitar, hay
                //  ruido: restar la media de una toma limpia solo la mueve.
                if (std::abs (media) > 0.001f)
                    juce::FloatVectorOperations::add (sb->buffer.getWritePointer (ch), -media, n);
            }
        }

        setAudioChannels (0, 2);          // release the mic input, back to output-only
        useLowestLatency();               // ...and take the fast path back with it
        styleButton (micButton, kKey);
        micButton.setButtonText (T ("GRABAR MIC"));
        if (sb != nullptr)
        {
            //  «TOMA 3» y no «REC 34»: el numero que importa es cual de tus
            //  tomas es y no en que hueco del banco cayo, que eso ya lo dice el
            //  pad donde esta. Dieciseis pads con nombre en un banco son la
            //  lista, sin inventar un segundo sitio donde vive un sonido.
            assignSampleToPad (recordingSlot, sb,
                               "TOMA " + juce::String (recordingSlot % kPadsPerBank + 1));
            status.setText (T ("Grabado en el pad %1  [%2s]", etiquetaPad (recordingSlot),
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
    //  function resizes padScratch, fxBus, recordBuffer and the delay
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

//  EL APARATO DE SONIDO DEL BANCO. Ver la nota de `bancoSonando`.
void MainComponent::bombeaAudioDePrueba()
{
    //  Con aparato de verdad el hilo de audio ya renderiza, y la cola de
    //  comandos es de un solo CONSUMIDOR por contrato: dos no la degradan, la
    //  atascan para siempre.
    if (deviceManager.getCurrentAudioDevice() != nullptr) return;

    constexpr int    kRafaga = 128;
    constexpr double kRate   = 48000.0;

    if (bancoBloque.getNumSamples() != kRafaga)
    {
        bancoBloque.setSize (2, kRafaga);
        engine.prepareToPlay (kRate, kRafaga);
        enginePreparedRate  = kRate;
        enginePreparedBlock = kRafaga;
    }

    //  Los bloques que caben en un tick, que es lo que el aparato habria
    //  entregado en ese tiempo. Con menos, el osciloscopio avanzaria a camara
    //  lenta y la medida diria que la cara se repinta menos de lo que se
    //  repinta.
    const int bloques = juce::jmax (1, (int) (kRate * (double) DeviceTier::profile().relojMs
                                              / 1000.0 / (double) kRafaga));
    for (int i = 0; i < bloques; ++i)
    {
        bancoBloque.clear();
        engine.renderNextBlock (bancoBloque, 0, kRafaga);
    }
}

//  EL RELOJ. Lo que no puede depender de que la pantalla refresque.
//
//  Y CONTANDO MILISEGUNDOS DE VERDAD. Los seis contadores de aqui abajo hacian
//  `+= DeviceTier::profile().uiIntervalMs`, o sea daban por hecho que el tick
//  duro exactamente su intervalo nominal — falso en cuanto el temporizador
//  llega tarde, que en un movil cargado es la mitad de las veces, y falso por
//  construccion desde que el dibujo va por su cuenta. Es el mismo fallo que
//  este proyecto ya tiene documentado y arreglado tres veces —«ticks donde
//  tenia que haber milisegundos»— sin aplicar al propio reloj que los cuenta.
void MainComponent::timerCallback()
{
    const double ahora = juce::Time::getMillisecondCounterHiRes();
    //  Acotado por arriba: si el proceso se queda parado -el depurador, una
    //  suspension, el sistema robando el hilo- un salto de dos segundos
    //  vaciaria de golpe el ducking y el plazo de la portada. Lo que se pierde
    //  al acotar es tiempo que la app no ha estado viva.
    const double dt = relojUltimoMs > 0.0
                        ? juce::jlimit (1.0, 250.0, ahora - relojUltimoMs)
                        : (double) DeviceTier::profile().relojMs;
    relojUltimoMs = ahora;

    if (bancoSonando) bombeaAudioDePrueba();

    guardaMasterSiHaceFalta();

    stepPadJob();
    stepPadSaveJob();
    checkXRuns (dt);

    //  La exportacion SI sabe cuanto falta - cuenta pasadas y bloques - asi
    //  que la barra deja de ir y venir y dice el numero.
    if (exportJob != nullptr)
        setBusyProgress (exportJob->progress.load (std::memory_order_relaxed));

    //  EL RELOJ DEL ARRANQUE, para el tope de la portada y para el banco.
    //  SIEMPRE, y no solo mientras la portada este puesta: pararlo al destapar
    //  deja el contador clavado y la linea de ZATI_ARRANQUE imprimiendose para
    //  siempre, que es como se descubrio.
    ++arranqueTicks;
    portadaMs += dt;

    //  Once, on the first tick: the face is up by now, so a restore that takes
    //  a second reads as filling in rather than as a hang.
    //
    //  Y DESPUES DE QUE LA PORTADA SE HAYA PINTADO, no en el primer tick a
    //  secas: restoreSession bloquea este hilo casi un segundo -restaurar
    //  sintetiza, y llenar un banco de instrumentos son 1238 ms medidos- asi
    //  que lanzarla antes de que la portada llegue a la pantalla la dejaria
    //  congelada con la cara a medio hacer y sin nada dibujado encima. Con
    //  ZATI_AUDIT no hay portada, asi que el banco no espera a nadie.
    if (sessionRestorePending && (portadaPintada || UiAudit::enabled()))
    {
        sessionRestorePending = false;
        restoreSession();

        //  Y se suelta la cuenta del arranque DESPUES de que restoreSession
        //  haya abierto la suya, o la barra parpadea: llegar a cero apaga el
        //  componente, y volver a uno en la linea siguiente lo enciende otra
        //  vez en el mismo fotograma.
        if (startupBusy) { startupBusy = false; endBusy(); }

        //  Y LA PRIMERA VEZ, EL TOUR. Aqui y no en el constructor: alli la
        //  sesion todavia no ha vuelto y la barra de "Iniciando" esta puesta,
        //  asi que la primera tarjeta saldria encima de una app a medio
        //  levantar. Y nunca cuando mide el banco - con ZATI_OPEN el banco
        //  pide una ficha concreta y una tarjeta encima seria diecinueve
        //  fichas medidas a traves del tour.
        //  Y SE MARCA AL ENSENARLO, no al acabarlo.
        //
        //  Estaba al reves, con este argumento escrito: "si la app se cierra a
        //  la mitad, el tour no vuelve nunca y nadie sabe que existio". El
        //  argumento es FALSO, y lo era desde el dia que se escribio: AJUSTES
        //  tiene una tapa TOUR que lo abre cuando quieras, asi que nadie lo
        //  pierde. Lo que si pasaba es lo otro - salir del tour por el boton
        //  ATRAS de Android, o cerrar la app, no marca nada - y entonces sale
        //  EN CADA ARRANQUE, que es como se lee una app rota.
        //
        //  Una bienvenida se enseña una vez. Volver a ofrecerla sin que nadie
        //  la pida es lo contrario de lo que la palabra significa.
        //
        //  Se escribe ANTES de enseñarlo, no despues: entre las dos lineas hay
        //  un maquetado entero, y si algo se cae ahi el tour volveria manana.
        const bool primeraVez = ! tourFile().existsAsFile();
        if (primeraVez)
        {
            ProjectStore::escribeTexto (tourFile(), "1");
        }

        //  Y el banco lo mide por AQUI, que es donde se decide, y no por una
        //  copia de la regla: con ZATI_AUDIT el tour no se enseña -una tarjeta
        //  encima serian diecinueve fichas medidas a traves de ella- pero la
        //  marca se escribe igual, que es justo lo que hay que comprobar.
        //
        //  Y "puesto", que es la otra mitad y la que faltaba: la marca se
        //  escribia bien y la bienvenida salia igual en cada arranque, porque
        //  la levantaba retranslateUi desde el constructor sin mirar nada. Con
        //  ZATI_AUDIT la tarjeta no se enseña NUNCA a proposito, asi que un uno
        //  aqui es exactamente eso - puesta sin que nadie la pida - y una
        //  regla que solo mira la marca no lo ve. Salia 1 y 1.
        if (UiAudit::enabled())
            std::cout << "{\"arranque\":\"tour\",\"primera\":" << (primeraVez ? 1 : 0)
                      << ",\"puesto\":" << (tourSheet.isVisible() ? 1 : 0)
                      << "}" << std::endl;

        if (! UiAudit::enabled() && primeraVez)
        {
            showTour (0);
            openSheet (tourSheet, setButton);
        }
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
    if (insetSettleMs < kMargenesPlazoMs)
    {
        insetSettleMs += dt;
        refreshSystemInsets();
    }

    //  Y CON TODO PREGUNTADO, si la cara ya puede enseñarse. Va DESPUES de
    //  refreshSystemInsets en el mismo tick: al reves, la portada duraria un
    //  latido de mas por preguntarlo antes de tener la respuesta.
    miraSiLaCaraEstaLista();

    //  ZATI_ARRANQUE=n: una linea por tick con lo que hace falta para juzgar
    //  esto - si la cara esta tapada y donde ha quedado la placa de los pads,
    //  que es lo que se mueve cuando llegan los margenes. Aparte de ZATI_AUDIT
    //  a proposito: alli no hay portada.
    if (bancoArranque > 0 && arranqueTicks <= bancoArranque)
    {
        //  Y EL TOPE RESUELTO EN TICKS, que es lo que el banco necesita saber
        //  y no puede deducir: el plazo esta en milisegundos y el tick lo pone
        //  el aparato. Escrito en el banco seria la misma regla en dos sitios.
        std::cout << "{\"arranque\":\"cara\",\"tope\":"
                  << ((kPortadaTopeMs + DeviceTier::profile().relojMs - 1)
                      / DeviceTier::profile().relojMs)
                  << ",\"tick\":" << arranqueTicks
                  << ",\"cubierta\":" << (caraLista ? 0 : 1)
                  << ",\"pintadas\":" << portadaPintadas
                  << ",\"placa\":[" << padPlateArea.getX() << "," << padPlateArea.getY()
                  << "," << padPlateArea.getWidth() << "," << padPlateArea.getHeight()
                  //  Y COMO ACABO LA VEZ ANTERIOR, que es lo unico de la caja
                  //  negra que se puede medir desde fuera: vacio es «acabo
                  //  bien». No lo miraba nadie -el unico `grep` de la bitacora
                  //  en `Tests/` era `expo.py` BORRANDOLA- y por eso el parte
                  //  falso salia en cada arranque. Ver Bitacora.h.
                  << "],\"previa\":\""
                  << Bitacora::previa.replaceCharacter ('"', '\'') << "\"}" << std::endl;
        if (arranqueTicks == bancoArranque) juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //  Y LA MUERTE DE ANDROID, SIMULADA. Ver bancoMuere en la cabecera.
    if (bancoMuere > 0 && arranqueTicks >= bancoMuere)
    {
        if (bancoSenal > 0) std::raise (bancoSenal);
        //  El orden ES la secuencia del telefono: onPause primero -que es donde
        //  vive «fin limpio»- y el proceso desaparece despues sin pasar por
        //  `shutdown()`. Con `_Exit` no corre ni un destructor, igual que un
        //  SIGKILL.
        appSuspended();
        std::_Exit (0);
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
    //  Counted in MILLISECONDS, not in ticks. relojMs is a device-tier
    //  number and it ranges from 33 to 100, so "100 ticks, about six seconds"
    //  was anything from 3.3 to 10 - un-ducking in the middle of the very
    //  notification it was making room for on a fast phone.
    if (duckedByFocus && (duckTicksLeft -= dt) <= 0.0)
    {
        duckedByFocus = false;
        engine.setDucked (false);
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
        if ((deviceRevivalTicks += dt) >= 1000.0)
        {
            deviceRevivalTicks = 0.0;
            setAudioChannels (0, 2);
            keepChosenRate();
            useLowestLatency();
            refreshDeviceStatusLine (true);
        }
    }
    else
    {
        deviceRevivalTicks = 0.0;
    }

    //  ...and from then on, every couple of seconds, hand the live pads to the
    //  writer. With nothing changed this is sixteen pointer comparisons.
    //
    //  Y EN MILISEGUNDOS, que estaba en TICKS: «cada par de segundos» eran 33
    //  ticks y un tick vale 33, 40, 60 o 100 ms segun el aparato, asi que la
    //  sesion se sincronizaba cada 1.1 s en un movil bueno y cada 3.3 en uno de
    //  gama basica — y desde que el reloj no es el que dibuja, un tick ya no
    //  dura ni siquiera lo que diga la tabla.
    if ((sessionSyncMs += dt) >= kSyncSesionMs)
    {
        sessionSyncMs = 0.0;
        session.sync (uiSample.data(), kNumPads);
        refreshSystemInsets();

        //  ...and the state itself every twenty seconds or so. onPause writes
        //  it too, but a process killed without one - a crash, a battery pull,
        //  a task-switcher swipe on some OEM builds - never gets there, and
        //  audio on disk with no state beside it restores nothing.
        if ((sessionStateMs += kSyncSesionMs) >= kEstadoSesionMs)
        {
            sessionStateMs = 0.0;
            session.writeState (captureState(), currentProject);
        }
    }

    engine.collectRetiredSamples();
    pollExport();
    refreshDeviceStatusLine();      // Oboe settles a beat after we ask it to
    watchAudioDevice();

    //  UNA CONFIRMACION ARMADA QUE NADIE CONTESTO vuelve a ser un boton
    //  normal, para que un SEGURO? rojo no se quede olvidado en una ficha.
    //  Aqui y no en el dibujo: es un plazo de reloj de pared, y ademas tres
    //  segundos de verdad — estaba en `confirmTicks = 50`, con el comentario
    //  «~3 s at the 60 ms UI timer» al lado, que en un movil de gama alta son
    //  1.65 s y en uno de gama basica cinco.
    if (confirmPending != nullptr && (confirmMs -= dt) <= 0.0)
        disarmConfirm();

    //  Y SI NO HAY VBLANK, EL DIBUJO SE CAE AQUI. Un peer que no entrega
    //  vblanks -o una ventana que todavia no tiene pantalla- dejaria la cara
    //  congelada: la aguja clavada, el cabezal parado y el analizador quieto,
    //  o sea una app que parece rota. Se pinta al ritmo del SUELO, que es
    //  exactamente la cadencia que esta app tenia antes de esta tanda.
    if (ahora - vblankUltimoMs > 2.0 * DeviceTier::profile().relojMs)
        pintaCuadro (dt);
}

// ============================================================================
//  UN VBLANK, Y LA APP MIDIENDOSE SOLA.
//
//  Pintar a 120 Hz lo que se pintaba a 16.7 es siete veces el trabajo, y un
//  fotograma de la cara con la maquina sonando no es barato -medido con
//  ZATI_PAINT a 412x915: 3.53 ms el arbol entero, 0.75 solo el fondo-. Asi que
//  la app se mide lo que le cuesta un cuadro y, cuando se pasa de su
//  presupuesto, SALTA vblanks: es la misma forma que ya tiene `bufferBursts`
//  subiendo cuando aparecen under-runs, que es la unica regla de esta casa para
//  un aparato que no llega.
//
//  El coste son las DOS mitades: `pintaCuadro` -alimentar el osciloscopio, las
//  dos FFT del analizador, las tres rejillas- y el `paint` que ese cuadro
//  provoca. Medir solo la primera diria que un fotograma cuesta cero, porque
//  `repaint()` no pinta: marca.
//
//  Con MEDIA MOVIL y no con el ultimo valor: un solo cuadro interrumpido por el
//  sistema partiria la tasa por dos y la subiria en el siguiente, o sea una
//  cadencia que oscila, que se ve peor que una lenta.
//
//  Y NUNCA MAS LENTO QUE EL SUELO: el salto se acota a lo que `relojMs`
//  permite. Ahi es donde ese numero dejo de ser «cada cuanto se pinta» y paso a
//  ser «lo mas lento a lo que se nos permite caer».
// ============================================================================
void MainComponent::enVBlank (double timestampSec)
{
    const double ahora = juce::Time::getMillisecondCounterHiRes();
    vblankUltimoMs = ahora;

    //  El periodo del panel, de su propia marca de tiempo. Acotado: un salto
    //  de segundos es la app volviendo de segundo plano, no un panel lento.
    const double periodo = vblankUltimoSec > 0.0
                             ? juce::jlimit (4.0, 200.0, (timestampSec - vblankUltimoSec) * 1000.0)
                             : 16.7;
    vblankUltimoSec = timestampSec;

    if (cuadroSaltar > 0) { --cuadroSaltar; return; }

    //  Lo que se pasa es el tiempo desde el ultimo cuadro PINTADO y no desde el
    //  ultimo vblank: con saltos, los dos dejan de ser lo mismo y usar el
    //  segundo haria que la aguja cayera a camara lenta justo en el aparato que
    //  no llega.
    const double dt = cuadroUltimoMs > 0.0
                        ? juce::jlimit (1.0, 250.0, ahora - cuadroUltimoMs)
                        : periodo;
    cuadroUltimoMs = ahora;

    cuadroGastoMs = 0.0;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    pintaCuadro (dt);
    cuadroGastoMs += juce::Time::getMillisecondCounterHiRes() - t0;

    //  Y el `paint` que esto acaba de pedir corre DESPUES de volver de aqui,
    //  asi que lo que se suma es el del cuadro anterior. Un cuadro de retraso
    //  en una media movil no cambia la respuesta y evita tener que adivinar
    //  cuando el peer ha terminado de despachar.
    cuadroCosteMs = 0.8 * cuadroCosteMs + 0.2 * cuadroGastoMs;

    const int techo = juce::jmax (0, (int) (DeviceTier::profile().relojMs / periodo) - 1);
    const int quiere = (int) std::floor (cuadroCosteMs / periodo);
    cuadroSaltar = juce::jlimit (0, techo, quiere);
}

// ============================================================================
//  EL DIBUJO, al ritmo de la pantalla.
//
//  `dtMs` son los milisegundos de verdad entre este cuadro y el anterior, y no
//  un adorno: todas las constantes de tiempo de aqui abajo estaban escritas POR
//  TICK y documentadas contra treinta cuadros por segundo, que es lo que solo
//  tenia la gama alta. O sea que el aviso de clip duraba 3 s en un movil bueno
//  y 9 en uno de gama basica, y la aguja del medidor caia a cuatro velocidades
//  distintas segun el telefono. Con el vblank eso habria pasado de cuatro
//  velocidades a una por panel. Se aplican con `exp (-dt / tau)`.
// ============================================================================
void MainComponent::pintaCuadro (double dtMs)
{
    ++UiAudit::cuadrosPintados;

    //  Mientras algo este cargando, la barra se repinta sola: es lo unico de
    //  la cara que tiene que moverse aunque no pase nada mas.
    if (busyJobs > 0) busyBar.repaint();


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
        fxPulsePhase += dtMs / periodMs;
        if (fxPulsePhase >= 1.0) fxPulsePhase -= std::floor (fxPulsePhase);

        //  A raised cosine: never fully off, so a running effect is lit even
        //  at the bottom of its breath. A lamp that goes dark once a second is
        //  a fault indicator, not a power light.
        const double lit = 0.42 + 0.58 * (0.5 - 0.5 * std::cos (fxPulsePhase * juce::MathConstants<double>::twoPi));

        //  POR RANURA Y NO POR TIPO. `fxButtons` tiene `kNumRanuras` tapas y
        //  este bucle iba hasta `kNumFx`: se salvaba por el `continue` de
        //  abajo -y por que los seis primeros tipos coincidian con las seis
        //  ranuras de fabrica- pero encendia la lampara del tipo f en la tapa
        //  f, y desde que una ranura puede llevar cualquier tipo esas dos
        //  cosas dejaron de ser la misma. `setFxEnabled` ya traduce con
        //  `slotDeFx`; esto no lo hacia.
        for (int sRan = 0; sRan < fxButtons.size() && sRan < kNumRanuras; ++sRan)
        {
            auto* b = fxButtons[sRan];
            const int f = enRanura (sRan);
            if (b == nullptr || f < 0) continue;

            //  Y CON EL MOVIMIENTO APAGADO, la lampara se queda ENCENDIDA y
            //  quieta: lo que sobra es el latido, no la informacion de que ese
            //  efecto esta puesto. Apagarla del todo convertiria «no quiero que
            //  parpadee» en «no se cual esta sonando».
            const double want = fxOn[(size_t) f] ? (movimiento ? lit : 1.0) : 0.0;
            const double had  = (double) b->getProperties().getWithDefault ("pulse", 0.0);
            if (std::abs (want - had) < 0.004) continue;

            b->getProperties().set ("pulse", want);
            b->repaint();
        }
    }

    //  Y LO DE LA CARA, SOLO CUANDO LA CARA SE VE.
    //
    //  El cristal es la pieza mas grande de la maquina y su propio comentario
    //  la llama «el coste en reposo mas grande de la app». Se rinde sola en
    //  silencio -esa guardia ya estaba- y por eso el banco no vio nunca lo
    //  otro: en un escritorio sin tarjeta de sonido el motor no renderiza, el
    //  osciloscopio ve silencio y no se repinta jamas. Con la maquina SONANDO
    //  se repinta treinta veces por segundo, y una ficha ocupa la ventana
    //  entera y lleva velo, asi que ese repintado no repinta el cristal:
    //  repinta el chasis, los dieciseis pads y los cuarenta controles que hay
    //  debajo. Medido con ZATI_SONANDO en 8 s: **134 fotogramas completos en
    //  MEZCLA y en AJUSTES**, donde el cristal no se ve, contra 1 con la app
    //  quieta. Es el mismo parrafo que ya gobierna la rejilla de pasos y el
    //  cabezal del piano, sin terminar de aplicar.
    //
    //  El destello de los pads, igual: hasta dieciseis `refreshPad` por tick
    //  debajo de una ficha. La CUENTA sigue corriendo -si no, un pad se queda
    //  encendido hasta que alguien lo mire- y lo unico que se salta es el
    //  repintado.
    const bool seVeLaCara = ! caraTapada();

    //  Y LO QUE SE MUEVE SOLO, solo si la persona lo quiere. Es la misma
    //  guardia que `seVeLaCara` y por la misma razon —saltarse el repintado de
    //  lo que nadie esta mirando— con otro motivo: aqui se ve y no se quiere
    //  ver moverse. Ver `movimiento`.
    const bool animar = seVeLaCara && movimiento;

    if (animar)
    {
        //  The master silhouette. The engine has already decimated its ~0.74 s
        //  window into min/max columns, so this copies 256 pairs instead of the
        //  35000 samples the window actually holds.
        {
            float cmn[AudioEngine::kMaxScopeColumns], cmx[AudioEngine::kMaxScopeColumns];
            const int nc = engine.copyScopeColumns (cmn, cmx, AudioEngine::kMaxScopeColumns);
            cristal.setColumns (cmn, cmx, nc);
        }

        //  EL ANALIZADOR DEL EQ, y solo con la curva a la vista: son dos FFT
        //  de 1024 por tick y pagarlas con el plato enseñando tres mandos
        //  seria trabajo tirado. Las dos: la mancha de ENTRADA al fondo dice
        //  DONDE hay que tocar y la linea de SALIDA delante confirma que la
        //  correccion hizo lo que querias — con una sola no se puede
        //  distinguir «no habia nada ahi» de «ya lo he quitado».
        //  UNA COPIA Y NO DOS, que es lo que el motor ya garantiza: los
        //  anillos son del efecto MIRADO, y mirado hay uno. Con la curva
        //  grande delante alimenta a la curva, y con el plato en su fila de
        //  mandos alimenta al visor — nunca los dos, porque nunca se ven los
        //  dos.
        const int nScope = (int) (sizeof (eqPreTmp) / sizeof (eqPreTmp[0]));
        if (eqCurva.isVisible() || (platoMini.isVisible() && platoMini.tipo() >= 0))
        {
            engine.copyFxScope (eqPreTmp, eqPostTmp, nScope);

            if (eqCurva.isVisible())
            {
                eqCurva.setMuestras (eqPreTmp, eqPostTmp, nScope, dtMs);
                //  Y VIVO o no: sin envios al bus no se escribe nada en los
                //  anillos, y una mancha congelada se lee como «esto esta
                //  roto» en vez de «no pasa nada por aqui».
                eqCurva.ponVivo (engine.fxScopeVivo());
            }
            else
            {
                //  La reduccion MEDIDA, que es lo que pone la altura del
                //  punto de trabajo en los cuatro de dinamica. `dinamicaDeFx`
                //  ya hace la unica traduccion que hay entre las dos
                //  numeraciones, asi que no hace falta una segunda.
                const int dd = dinamicaDeFx (platoMini.tipo());
                platoMini.setMuestras (eqPreTmp, eqPostTmp, nScope, dtMs,
                                       dd >= 0 ? engine.getDynReduccion (dd) : 0.0f,
                                       engine.getLfoFase (platoMini.tipo()));
                platoMini.ponVivo (engine.fxScopeVivo());
            }
        }
        else if (platoMini.isVisible())
        {
            platoMini.ponVivo (false);
        }

        //  LA VENTANA DEL ANALIZADOR MANDA SOBRE `scopePoints`, y esa es la
        //  unica linea que el espectro del cristal costo aqui. `scopePoints`
        //  vale 256 en la gama baja y sale del mismo `classify()` que el
        //  deposito de voces: para el pico y la aguja da igual cuantas
        //  muestras se miren, pero una FFT de 1024 necesita 1024 - con menos
        //  `Analizador::analiza` se rinde y la gama baja se quedaria sin
        //  espectro. El anillo del motor son 2048, asi que pedirlas es siempre
        //  valido, y lo que cuesta de mas es copiar 768 floats: la FFT es el
        //  coste y esa se paga o no se paga entera.
        const int scopeN = juce::jmin ((int) (sizeof (scopeTmp) / sizeof (scopeTmp[0])),
                                       juce::jmax (DeviceTier::profile().scopePoints,
                                                   Analizador::kFft));
        engine.copyScope (scopeTmp, scopeN);
        cristal.setSamples (scopeTmp, scopeN, dtMs, deviceSampleRate);
        cristal.setBpm (bpmSlider.getValue());
    }

    const int ps = engine.getPlayStep();

    // Pad trigger feedback (taps + sequencer): flash then decay.
    const std::uint64_t trig = engine.fetchTriggered();
    const float caida = (float) std::exp (-dtMs / kTauDestelloMs);
    for (int i = 0; i < kNumPads; ++i)
    {
        if ((trig & ((std::uint64_t) 1u << i)) != 0) padFlash[(size_t) i] = 1.0f;
        if (padFlash[(size_t) i] > 0.0f)
        {
            //  EN MILISEGUNDOS. Estaba en `*= 0.8f` POR TICK, que a los 33 ms
            //  de la gama alta son 148 ms de constante de tiempo y a los 100
            //  de la basica, 448: el mismo destello duraba el triple en el
            //  telefono mas lento. Con el vblank habria sido peor todavia
            //  -a 120 Hz, 37 ms- o sea un parpadeo en vez de un destello.
            padFlash[(size_t) i] *= caida;
            if (padFlash[(size_t) i] < 0.02f) padFlash[(size_t) i] = 0.0f;
            //  La CUENTA sigue corriendo con el movimiento apagado y lo unico
            //  que se salta es el repintado — igual que debajo de una ficha. Si
            //  se parara, al volver a encenderlo dieciseis pads se quedarian
            //  destellando donde estuvieran.
            if (animar) refreshPad (i);
        }
    }

    //  The grid reads the pattern straight from our mirror - but only when
    //  the sheet that shows it is open. It used to run on every tick whether
    //  the sequencer was on screen or not, and it is not cheap: 64 steps x 16
    //  pads copied out of the mirror plus the same number of atomic loads for
    //  the step pitches, thirty times a second, to feed a component nobody
    //  was looking at.
    //  Y CADA PAGINA ALIMENTA LA SUYA, que es el mismo parrafo de arriba sin
    //  terminar de aplicar. El arreglo de entonces pregunto por la FICHA y la
    //  ficha tiene tres paginas: en PIANO y en PATRON la rejilla de pasos esta
    //  invisible -showSeqPage la apaga- y se seguia rellenando treinta veces
    //  por segundo, 64 pasos x 16 pads copiados del espejo mas otras tantas
    //  cargas atomicas para un componente que nadie ve, en dos de las tres.
    //
    //  Y al reves, que es lo que se notaba: en la pagina del PIANO la unica
    //  llamada a refreshPiano venia de tocar algo -cambiar de pagina, de
    //  octava, de pad-, asi que el cabezal se pintaba donde estuviera al
    //  entrar y ahi se quedaba. La barra estaba dibujada desde el primer dia
    //  (ver PianoRoll::paint) y no estaba VIVA, que es la unica forma que
    //  tiene un piano roll de decir por donde va lo que suena.
    if (seqSheet.isVisible())
    {
        if (seqPage == seqPagePiano) refreshPiano (false);
        else if (seqPage == seqPageGrid) refreshStepGrid();
    }
    if (songSheet.isVisible() && engine.isPlaying()) refreshSong (false);

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
            oirTapa (previewButton, sounding);
        }
    }

    //  El renglon de la cadena, y SOLO el renglon. Ver seqChainBand: la ficha
    //  ocupa la ventana entera y es translucida, asi que pedirle un repintado
    //  completo para mover un texto de catorce pixeles arrastraba consigo el
    //  chasis, los pads y todo lo demas - 6.25 ms de fotograma treinta veces
    //  por segundo contra los 0.27 que cuesta la banda.
    if (engine.isPlaying() && seqSheet.isVisible())
    {
        const int suena = engine.getChainLength() > 0 ? engine.getPlayingPattern() : -1;
        if (suena != shownChainPattern)
        {
            shownChainPattern = suena;
            //  Un pixel de mas por cada lado: el rotulo se dibuja DENTRO de la
            //  banda pero el suavizado de los bordes se sale de ella, y
            //  repintar la banda exacta deja media linea del texto anterior.
            if (! seqChainBand.isEmpty()) seqSheet.repaint (seqChainBand.expanded (1));
        }
    }
    else
    {
        shownChainPattern = -2;   // al volver a rodar, que se pinte la primera vez
    }

    // Face strips: VU ballistics (fast attack, 100 ms decay) and the
    // step-LED playhead.
    if (! vuHeld)
    {
        const float pl = engine.readOutPeakL();
        const float pr = engine.readOutPeakR();
        const float prevL = vuL, prevR = vuR;
        //  Misma cuenta y misma razon que el destello del pad: la balistica
        //  de una aguja es una constante de TIEMPO y no un factor por cuadro.
        const float caidaVu = (float) std::exp (-dtMs / kTauAgujaMs);
        vuL = juce::jmax (pl, vuL * caidaVu); if (vuL < 0.004f) vuL = 0.0f;
        vuR = juce::jmax (pr, vuR * caidaVu); if (vuR < 0.004f) vuR = 0.0f;
        juce::ignoreUnused (prevL, prevR, prevPlayStep);
        //  La balistica corre siempre -el pico se lee y se vacia, y pararla
        //  dejaria la aguja clavada donde estuviera al abrir una ficha- y lo
        //  unico que se salta es el repintado. Ver `seVeLaCara`.
        if (animar) cristal.setVu (vuL, vuR);
        // (the LCD no longer carries a step strip)

        //  Y LA TERCERA TIRA: el canal del pad elegido.
        //
        //  Se le dice al motor QUE canal medir aqui y no en `selectPad`, que es
        //  lo unico que lo apaga solo: con una ficha abierta encima el medidor
        //  no se ve, y medirlo igual pagaria el camino largo de esos pads para
        //  dibujar algo que esta tapado. -1 es «ninguno». Ver `miraCanal`.
        const int canalVisto = animar ? engine.getPadCanal (selectedPad) : -1;
        engine.miraCanal (canalVisto);

        //  Misma balistica que la aguja del master, y por lo mismo: es una
        //  constante de TIEMPO y no un factor por cuadro.
        const float pc = engine.readCanalPico();
        const float caidaCan = (float) std::exp (-dtMs / kTauAgujaMs);
        vuCanal = juce::jmax (pc, vuCanal * caidaCan);
        if (vuCanal < 0.004f) vuCanal = 0.0f;
        if (animar) cristal.setCanal (canalVisto, vuCanal);
    }

    if (recordingActive)
    {
        //  The take stops itself when the buffer fills; say so rather than
        //  letting the counter freeze and look like a hang.
        if (! engine.isRecording())
            toggleMicSampling();
        else
            status.setText (T ("Grabando pad %1  %2s / %3s", etiquetaPad (recordingSlot),
                               juce::String (engine.getRecordSeconds(), 1),
                               juce::String ((int) engine.getRecordLimitSeconds())),
                            juce::dontSendNotification);
    }
}

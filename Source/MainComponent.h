#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "SpectrumDisplay.h"
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include "ProjectStore.h"
#include "StepGrid.h"
#include "Playlist.h"
#include "PianoRoll.h"
#include "AudioFocus.h"
#include "SessionKeeper.h"
#include "Exporter.h"
#include "Bitacora.h"
#include "AudioPath.h"
#include "XyPad.h"
#include "MidiIo.h"

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
    //  EL PIANO ROLL. Ver PianoRoll.h: las notas de un pad en una rejilla de
    //  tono contra tiempo, que es como se escribe una melodia desde que existe
    //  el pentagrama. Hasta ahora afinar un paso era abrir PASO y mover un
    //  mando: un semitono, un paso, un viaje - y tres acordes son treinta y
    //  seis viajes, que es por lo que nadie los escribia.
    PianoRoll pianoGrid;
    juce::TextButton pianoOctDownBtn { "OCTAVA -" }, pianoOctUpBtn { "OCTAVA +" };
    juce::TextButton pianoClearBtn   { "VACIAR" };
    juce::TextButton pianoButton     { "PIANO" };
    //  CAMBIAR DE PAD SIN CERRAR. La ficha tapa la rejilla de pads, asi que
    //  escribir el bajo y luego la campana costaba cerrar, elegir y abrir por
    //  cada instrumento. Y saltan los pads VACIOS: en un kit de cinco sonidos,
    //  avanzar de uno en uno por sesenta y cuatro es no tener el boton.
    juce::TextButton pianoPadDownBtn { "PAD -" }, pianoPadUpBtn { "PAD +" };
    //  Las dos herramientas del piano. Excluyentes: con las dos apagadas se
    //  dibuja, que es lo que hace falta el 90% del tiempo.
    juce::TextButton pianoGomaBtn { "GOMA" }, pianoCorteBtn { "TIJERAS" };
    void pianoStepPad (int dir);
    int pianoBase = -12;                       // el semitono de la fila de abajo
    signed char pianoCells[AudioEngine::kNumSteps * PianoRoll::kMaxNotas] {};
    //  Un largo por paso, en cuartos. Ver AudioEngine::setStepLen.
    unsigned char pianoLargos[AudioEngine::kNumSteps] {};
    void refreshPiano (bool repintarTarjeta = true);
    void pianoCellToggled (int paso, int semi);
    void paintPianoSheetContent (juce::Graphics& g);

    Sheet padSheet, seqSheet, browseSheet, setSheet, mixSheet, songSheet,
          exportSheet, rackSheet, chopSheet;

    //  EL XY NO ES UNA FICHA. Es lo unico de esta app que se usa CON las dos
    //  manos: una barre y la otra dispara. Abierto como tarjeta flotante sobre
    //  un velo, tapaba la rejilla y se tragaba los toques que iban a los pads -
    //  y entonces el gesto que hace falta, barrer el filtro mientras metes un
    //  golpe, no se puede hacer.
    //
    //  Asi que ocupa la MITAD DE ARRIBA de la cara - el sitio de la pantalla,
    //  los mandos y los efectos, que es justo lo que el panel sustituye - y se
    //  detiene donde empieza la costura de PADS. Sin velo y sin capturar nada:
    //  los dieciseis pads siguen debajo, visibles y tocables.
    struct XyPanel : public juce::Component
    {
        std::function<void (juce::Graphics&)> paintContent;
        void paint (juce::Graphics& g) override { if (paintContent) paintContent (g); }
    };
    XyPanel xyPanel;
    juce::Rectangle<int> faceTopArea;   // desde donde puede ocupar el panel
    //  ...and a THIRD page, which is the one that makes the gestures real.
    //
    //  A shortcut nobody knows about is not a shortcut, it is dead code with a
    //  timer on it. Every gesture on this machine is invisible by definition -
    //  a hold and a swipe leave no mark on the face - so there has to be one
    //  place that lists them, and it has to be inside the app rather than in a
    //  manual nobody opens. This is that place.
    enum SetPage { pageAudio = 0, pageMidi, pageProjects, pageGestures };
    int setPage = pageAudio;
    juce::TextButton pageAudioBtn { "AUDIO" }, pageMidiBtn { "MIDI" },
                     pageProjBtn { "PROYECTOS" }, pageGestBtn  { "GESTOS" };
    void paintGesturesPage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMidiPage (juce::Graphics& g, juce::Rectangle<int> area);
    juce::Rectangle<int> midiArea;
    juce::Rectangle<int> gesturesArea;
    static constexpr int kNumGestures = 6;
    void showSetPage (int page);

    //  THE SEQUENCER CARD HAS TWO PAGES, and it has them because measuring it
    //  said so. Everything used to be on one card: pattern, length, chain,
    //  step note, velocity, roll, swing, the bar selector, the sixteen-lane
    //  grid and the tempo - 800 px of content asking a card that is capped at
    //  78% of the window. On a 360x640 phone that cap is 499, so 300 px had to
    //  go somewhere, and it came out of whatever was laid out LAST: the grid.
    //  Measured on the bench: sixteen lanes sharing 55 px, three and a half
    //  pixels each, with the pad numbers painted on top of one another. The
    //  one thing the sheet exists for was the smallest thing on it.
    //
    //  So: PASOS is the grid and the four controls you touch while it plays -
    //  which pattern, how long, which bar, what tempo. PASO is the step you
    //  tapped - its note, how hard it hits, how many times it repeats - plus
    //  the chain and the swing. Two pages, each of which asks for exactly the
    //  height it will be given, so nothing is ever squeezed out of the bottom.
    //  TRES paginas y una sola ficha: la rejilla de pasos, el piano roll y lo
    //  que es del patron entero. El piano vivia en una ficha aparte y escribia
    //  EXACTAMENTE lo mismo -las notas del patron- desde otro sitio: dos
    //  popups para un trabajo es lo que esta app no permite. Escribir notas es
    //  un trabajo con dos vistas, no dos trabajos.
    enum SeqPage { seqPageGrid = 0, seqPagePiano, seqPageStep };
    int seqPage = seqPageGrid;
    //  La segunda pestana se llama PATRON y no PASO desde que los mandos del
    //  paso viven debajo de la rejilla: lo que queda aqui -cadena, desplazar,
    //  doblar, humanizar, swing, rejilla- es del patron entero.
    juce::TextButton seqGridBtn { "PASOS" }, seqPianoBtn { "PIANO" }, seqStepBtn { "PATRON" };
    //  EL TRANSPORTE, DENTRO DE LA FICHA, igual que en CANCION y en PIANO.
    //  Escribir un paso y OIRLO es el mismo gesto, y la tapa de PLAY de la
    //  cara se queda debajo del velo: habia que cerrar, tocar, y volver a
    //  abrir buscando por donde ibas.
    juce::TextButton seqPlayBtn { "PLAY" };
    //  HUMANIZAR y SEGUIR, las dos que faltaban del editor de patrones.
    //
    //  HUMANIZAR escribe un empujon distinto en cada paso y una fuerza
    //  distinta en cada golpe, y lo ESCRIBE - no lo sortea al tocar. Un
    //  temblor sorteado en el hilo de audio suena distinto cada vuelta: no es
    //  un groove, es ruido, y no se puede deshacer, ni guardar, ni volver a
    //  oir igual. Escrito se puede hacer las tres cosas.
    //
    //  SEGUIR mueve la vista al compas que suena. Con patrones de cuatro
    //  compases, mirar el 1 mientras suena el 3 es la mitad del tiempo
    //  mirando una rejilla que no se mueve.
    juce::TextButton seqHumanBtn  { "HUMANIZAR" };
    //  EUCLIDES: reparte N golpes lo mas uniformemente posible en la fila del
    //  pad elegido. Ver euclidesPattern.
    juce::Slider euclidSlider;
    void euclidesPattern (int golpes);

    //  COPIAR Y PEGAR LA FILA DE UN PAD, que es lo que no se podia hacer:
    //  copiar el bombo del patron 1 al 3 obligaba a copiar el BANCO entero -y
    //  con el los otros quince pads- o a ir casilla por casilla.
    juce::TextButton copyRowBtn { "COPIAR FILA" }, pasteRowBtn { "PEGAR FILA" };
    struct PasoFila { bool on; int nota, vel, roll, largo, empujon, corte;
                      std::uint32_t acorde, bloqueos; };
    std::array<PasoFila, AudioEngine::kNumSteps> filaPortapapeles {};
    bool filaCopiada = false;
    void copiarFila();
    void pegarFila();
    juce::TextButton seqFollowBtn { "SEGUIR" };
    bool seqFollow = false;
    void humanizePattern();
    //  EL BLOQUEO DEL CORTE DE ESTE PASO. Ver AudioEngine::setStepLock: el
    //  paso que lo lleva escribe el corte del pad al dispararse, asi que un
    //  patron puede abrir y cerrar el filtro solo. En el mando, el extremo de
    //  abajo no es 20 Hz sino APAGADO - que es lo que vale un paso que no
    //  toca el filtro, y sin esa posicion haria falta un interruptor al lado.
    juce::Slider lockSlider;
    //  Y LOS OTROS CUATRO BLOQUEOS del paso: ataque, caida, punto de inicio y
    //  pan. Ver AudioEngine::setStepPLock. Van en una SOLA fila de la tira -
    //  cuatro mandos donde las otras dos filas llevan dos- porque cada fila
    //  cuesta 58 px y en 412x915 la celda de la rejilla cae de 16.9 a 13.3 con
    //  una fila mas y a 9.7 con dos. Dos filas de dos no caben en un telefono,
    //  o sea que no existirian.
    //
    //  Y giratorios, no de + y -, por la misma cuenta pero de ancho: cuatro
    //  cajas de IncDecButtons piden 122 px cada una -dos teclas de 40, el aire
    //  y la casilla- y en 412 les tocan 103.
    juce::Slider atkPasoSlider, relPasoSlider, iniPasoSlider, panPasoSlider;
    //  LAS TRES HERRAMIENTAS DEL PATRON.
    //
    //  Viven en la pagina PASO y no en PASOS por una razon medida: la pagina
    //  de la rejilla ya llega al suelo de 12 px por carril en las dos
    //  pantallas mas estrechas, y una fila mas de 66 px sale entera de la
    //  rejilla - lo unico para lo que existe la ficha. En PASO ya viven la
    //  CADENA y el SWING, que tampoco son del paso sino del patron, asi que
    //  la fila cae donde ya estaban sus iguales.
    //
    //  DESPLAZAR mueve el patron entero un paso, con la vuelta puesta: es como
    //  se arregla un groove que entra tarde sin volver a escribirlo. DOBLAR
    //  copia el patron detras de si mismo y duplica el largo, que es lo que
    //  falta para pasar de 16 a 32 pasos sin dejar la segunda mitad muda.
    //  Con PALABRAS y no con flechas. Dos motivos medidos: la fuente mono de
    //  las tapas no trae los triangulos y salian como una 'a' con sombrero
    //  seguida de dos huecos, y una flecha a la izquierda en arabe -que se
    //  lee de derecha a izquierda- apunta a lo que viene DESPUES. Un simbolo
    //  que cambia de significado con el idioma no es un simbolo.
    juce::TextButton patLeftBtn  { "ATRAS" };
    juce::TextButton patRightBtn { "ADELANTE" };
    juce::TextButton patDoubleBtn { "DOBLAR" };
    void rotatePattern (int by);
    void doublePattern();
    //  LAS CUATRO TAPAS DE BANCO, OTRA VEZ, DENTRO DEL SECUENCIADOR.
    //
    //  La rejilla de pasos ensena SIEMPRE los dieciseis del banco en curso
    //  (stepGrid.onCell suma currentBank * kPadsPerBank), y las tapas A B C D
    //  de la cara quedan DEBAJO de esta ficha. Asi que para escribir un bombo
    //  del banco A y un bajo del D habia que cerrar, cambiar y volver a abrir -
    //  tres gestos para lo que un secuenciador hace todo el rato. Estas son las
    //  mismas: llaman a selectBank y se quedan al dia con las de la cara.
    juce::OwnedArray<juce::TextButton> seqBankButtons;
    //  El suelo de alto de una pista de la rejilla de pasos. Es el mismo numero
    //  que vigila Tests/expo.py (MIN_CELL): si cambia alli, cambia aqui.
    static constexpr int kMinLaneH = 12;
    void showSeqPage (int page);

    //  LA FICHA DEL PAD, TAMBIEN EN DOS PAGINAS.
    //
    //  Tenia dieciocho controles en una columna: tres filas de mandos, dos
    //  reglas de recorte, dos barras de botones, ocho muestras de color y la
    //  onda, todo bajo tres rotulos. En 280x653 eso son 670 px pedidos a una
    //  ficha que no puede pasar del 78% de 653 - 509 -, asi que la onda se
    //  quedaba en una franja y no habia sitio para nada mas.
    //
    //  SONIDO es lo que suena el pad: afinado, nivel, envolvente y recorte.
    //  EL PAD es lo que el pad ES: a que efectos manda, como corta, de donde
    //  saca su sonido y de que color es. La division no es por espacio sino
    //  por pregunta - la primera se toca mezclando y la segunda montando.
    //  Y TRES, no dos. Con dos, SONIDO seguia pidiendo 686 px - tres filas de
    //  mandos, las dos reglas de recorte, la fila de botones y la onda - de una
    //  ficha que en 360x640 no puede pasar de 499. Lo que se salia era LA ONDA,
    //  que es justo lo que hay que mirar para cortar: el banco la saco a altura
    //  cero en tres de las siete pantallas, con sus tres tapas de zoom encima,
    //  tambien a cero.
    //
    //  SONIDO es como suena, RECORTE es que trozo suena, EL PAD es lo que el
    //  pad es. Con el recorte en su propia pagina la onda se queda con 180 px
    //  en el movil mas pequeno, que es la diferencia entre ver un ataque y
    //  adivinarlo.
    enum PadPage { padPageSound = 0, padPageTrim, padPageRig };
    int padPage = padPageSound;
    juce::TextButton padSoundBtn { "SONIDO" }, padTrimBtn { "RECORTE" }, padRigBtn { "EL PAD" };
    void showPadPage (int page);
    bool padSourceWraps (int rowWidth) const;
    bool setTabsFit (int rowWidth) const;
    bool padRowFits (int rowWidth, std::initializer_list<const juce::TextButton*> bs) const;
    //  El reparto apretado de EL PAD, decidido en resized() y necesario en
    //  paint() para titular la seccion del medio con lo que de verdad hay
    //  dentro. Sin el, en apaisado la fila de cinco tapas se titulaba CORTE.
    bool padRigTight = false;

    //  LOS SEIS ENVIOS DEL PAD.
    //
    //  LA PUERTA DEL RACK, no una segunda copia de los seis envios.
    //
    //  Aqui hubo una fila de seis mandos que movia exactamente los mismos seis
    //  valores que el RACK. Dos sitios para una cosa no son dos comodidades:
    //  son dos maquetados que mantener, dos formas de que uno se quede viejo y
    //  una pregunta -"¿cual de las dos es la buena?"- que no deberia existir.
    //  Se quedo el RACK, que apaga el envio cuyo efecto esta cerrado y trae su
    //  propio selector de pad; aqui queda una tapa que lo abre en el pad que
    //  se esta editando, y 86 px de alto que vuelven a la pagina mas apretada.
    juce::TextButton padRackBtn { "ENVIOS" };

    //  The captions of the sequencer card, recorded by resized() instead of
    //  reconstructed by paint() from each control's bounds. Reconstructing
    //  them was fine while every control was on screen at once; with two pages
    //  it drew the name of a control that was HIDDEN - four ghost captions
    //  lying across the grid. What is laid out is what is labelled.
    //  `filas` son las filas de CONTROL que cuelgan del rotulo, para el panel
    //  que se pinta detras del grupo: casi todos llevan una, y la fila de
    //  herramientas del patron lleva dos cuando no caben las seis en una.
    struct SeqLabel { juce::Rectangle<int> band; juce::String key; int filas = 1; };
    juce::Array<SeqLabel> seqLabelBands;
    //  ...and the line at the foot of the PASO page that names the step being
    //  edited. Reserved by resized() for the same reason: drawn from the card's
    //  bottom edge without being booked, it landed on the swing slider.
    static constexpr int kSeqFootH = 14;
    //  Cuantas filas de mandos del paso se llevo la tira de la rejilla en la
    //  ultima maqueta. Lo apunta resized() y lo lee paint(): es lo que decide
    //  si la pagina del patron todavia tiene algo del paso que explicar.
    int seqTiraFilas = 0;
    juce::Rectangle<int> seqFootArea;

    //  EL RENGLON DE LA CADENA, apuntado para poder repintar SOLO ese.
    //
    //  Mientras el secuenciador rueda, lo unico que cambia en toda la ficha
    //  SEC es este renglon - "cadena: P1 P2  ·  suena P2" - y el temporizador
    //  pedia seqSheet.repaint() entero treinta veces por segundo para
    //  moverlo. La ficha ocupa la ventana completa y lleva un velo al 45 %,
    //  asi que ese repaint no repinta la ficha: repinta el chasis, los
    //  dieciseis pads, el espectro y los cuarenta controles que hay debajo.
    //  Medido en el banco de pintado a 412x915: el fotograma entero son
    //  6.25 ms y una banda de 30 px 0.27 - veintitres veces menos.
    //
    //  Y ni siquiera cada tick: el renglon solo cambia cuando cambia el
    //  patron que suena, asi que se compara antes de pedir nada.
    juce::Rectangle<int> seqChainBand;
    int shownChainPattern = -2;

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
    //  WAV o comprimido. Ver Exporter: un master de tres minutos pasa de 30 MB
    //  a 3, que es lo que separa "lo tengo" de "te lo mando".
    juce::TextButton exportFmtBtn { "WAV" };
    bool exportOgg = false;
    double deviceSampleRate = 44100.0;

    //  The clock the USER picked, as opposed to whatever the driver last
    //  handed us. setAudioChannels() re-initialises the device from scratch
    //  and loses it, so it has to be remembered and put back. Zero means
    //  "never chosen, let the device decide". See keepChosenRate().
    double chosenRate = 0.0;

    //  EL BUFFER QUE EL TELEFONO AGUANTA, no el que anuncia.
    //
    //  useLowestLatency pide el burst mas pequeno que ofrece el driver, que es
    //  el argumento entero de esta app. Lo que no habia era forma de saber
    //  cuando el telefono NO llega: un bloque que no se renderiza a tiempo es
    //  un under-run, y un under-run es un chasquido. La app lo pedia y no
    //  volvia a mirar, asi que un movil justo crepitaba para siempre sin que
    //  nada lo dijera ni lo corrigiera. Ahora se cuentan (getXRunCount) y, si
    //  aparecen, se sube un burst y se recuerda.
    int burstMult   = 0;     // 0 = aun sin leer del disco; luego 1..kMaxBursts
    int lastXRuns   = -1;    // -1 = todavia no se ha leido ninguno
    int xrunsSeen   = 0;     // desde el ultimo cambio de buffer
    int xrunGrace   = 0;     // ticks de gracia despues de abrir el dispositivo
    static constexpr int kMaxBursts = 4;
    void   keepChosenRate();
    juce::String exportStatus;
    bool         exportOk = false;
    void startExport (bool stems);
    void pollExport();
    //  Lo que dice el recuadro de AUDIO de la ficha AJUSTES, en numeros, para
    //  no repintar la ficha -y con ella la ventana entera- treinta veces por
    //  segundo diciendo lo mismo. Ver pollExport.
    struct Readout
    {
        const juce::AudioIODevice* dev = nullptr;
        double sr = 0.0;
        int block = 0, latency = 0;
        bool midiendo = false;
        float medido = -1.0f;
        double relojMedido = 0.0;
        bool ran = false, mmapKnown = false, mmapUsed = false, exclusive = false;
        int politicaMmap = -1, politicaExcl = -1;
        int nota = 0;

        bool operator== (const Readout& o) const noexcept
        {
            return dev == o.dev && sr == o.sr && block == o.block && latency == o.latency
                && midiendo == o.midiendo && medido == o.medido && relojMedido == o.relojMedido
                && ran == o.ran && mmapKnown == o.mmapKnown && mmapUsed == o.mmapUsed
                && exclusive == o.exclusive && politicaMmap == o.politicaMmap
                && politicaExcl == o.politicaExcl && nota == o.nota;
        }
    };
    Readout lastReadout;
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
    //  THE CHASSIS. Three bodies the same machine can be made of; see
    //  ZatiColours::skinTable. It sits beside the language because both are
    //  the same kind of question - what this app is, rather than what it does.
    juce::OwnedArray<juce::TextButton> skinButtons;
    juce::Rectangle<int> skinRowArea;
    juce::Rectangle<int> bufRowArea, rateRowArea;
    void useLowestLatency();
    void checkXRuns();
    static juce::File burstPreferenceFile();
    static int loadBurstPreference();     // one native burst, not JUCE's 40 ms default

    //  What AAudio granted a bare exclusive request at startup, before any
    //  device of ours existed. This is the only honest answer to "are we on
    //  the fast lane", and it also configures the real stream.
    AudioPath::Fast fastPath;

    //  The measurement. Everything else in this panel is the device's own
    //  claim about itself; this is a click emitted and heard back.
    juce::TextButton measureButton { "MEDIR" };
    //  Cuantizar el disparo en directo. Vive en AJUSTES / AUDIO porque es una
    //  preferencia de como responde la maquina, no un ajuste de la obra: si la
    //  quieres puesta, la quieres puesta en todos tus proyectos.
    juce::TextButton quantButton { "CUADRAR" };
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
    //  Una maquina con trabajo dentro, para las fotos de la ficha de Play.
    //  Solo se llama desde el arranque de auditoria: una caja vacia enseña
    //  dieciseis huecos grises y no dice nada de lo que hace la app.
    void auditDemo();
    //  Y el transporte, para la medida de CPU en reposo (ZATI_SPIN). Sin
    //  esto la unica forma de arrancarlo desde fuera era sintetizar un clic
    //  en las coordenadas donde se cree que esta PLAY, que es la clase de
    //  prueba que pasa porque ha fallado el tiro.
    void auditPlay (bool on);
    //  LAS HERRAMIENTAS DE ARREGLO, medidas. Ver auditArrange: monta una
    //  cancion y un patron conocidos, ejecuta las seis operaciones y dice lo
    //  que quedo. Sin esto, "insertar un compas" es una tapa que se pulsa y
    //  algo se mueve, y nadie sabe si lo que se movio es lo que tenia que
    //  moverse - que es exactamente como se perdio la relacion de un troceado
    //  al guardar y volver.
    void auditArrange();
    //  GUARDAR UN PROYECTO Y VOLVER A ABRIRLO, que es lo que hace la persona
    //  y NO lo que medía la prueba de sesion. El fallo de los bancos altos
    //  clonados se arreglo en captureState/applyState y la prueba que lo
    //  cubria pasaba por el autoguardado de sesion, que es otro camino: si
    //  el arreglo se hubiera caido solo en el del proyecto, la prueba habria
    //  seguido en verde. Se mide el camino que se usa.
    void auditProject();
    //  EL PIANO ROLL, medido. Ver auditPiano: escribe un acorde por la rejilla,
    //  cambia de pad con la ficha abierta y toca el teclado, que son las tres
    //  cosas que la ficha promete. Las tres tenian un fallo que una captura de
    //  pantalla no ve: el acorde vive en (patron, paso, pad) y hay que leerlo
    //  de ahi, el cambio de pad no repintaba la rejilla, y oir una tecla
    //  afinaba el pad para siempre.
    void auditPiano();
    //  LA EXPORTACION, medida de verdad y no mirando la barra. Ver auditExport:
    //  monta un patron con los sonidos de fabrica y hace el rebote entero -
    //  master y pistas - en el hilo que llama, contando ficheros y bytes.
    void auditExport();
    void auditExportAsync (bool cancelar);
    void esperaExport (bool cancelar, int vueltas);

private:
    void autosave();

    //  The work that was never given a name. Written continuously in the
    //  background and read back on the next launch, so a process the system
    //  reclaimed does not take the session with it.
    //  MIDI. Ver MidiIo.h: la entrada llega por su propia cola y la salida
    //  la escribe el hilo de audio y la envia el hilo del puente.
    MidiIo::Bridge midi;
    juce::TextButton midiOutBtn { "MIDI OUT" }, midiInBtn { "MIDI IN" };
    juce::ComboBox   midiOutBox, midiInBox;
    juce::Label      midiOutLbl, midiInLbl;
    void refreshMidiDevices();
    void applyMidiChoice();

    SessionKeeper session;
    void restoreSession();
    //  Los 64 sonidos de fabrica. onlyBank < 0 = los cuatro bancos. Ver Kits.h.
    void loadFactoryKits (int onlyBank = -1);
    bool sessionRestorePending = true;   // done on the first timer tick
    bool startupBusy = true;             // la barra ya esta puesta al primer fotograma
    int  sessionSyncTick  = 0;
    int  sessionStateTick = 0;

    //  Android arbitrates the speaker between apps. Without asking for the
    //  focus we play over calls and can be silenced without ever being told.
    AudioFocus audioFocus { *this };
    bool pausedByFocus = false;          // ...so GAIN only resumes what WE paused
    //  Whether the SEQUENCER was rolling when we lost the focus. Restoring the
    //  audio device without this brings the stream back and leaves the music
    //  stopped, which is indistinguishable from the app having crashed quietly.
    bool wasRollingBeforeFocus = false;

    //  Turned down under a notification, and the countdown that guarantees it
    //  comes back up even if the GAIN we are owed never arrives. Some OEM
    //  builds never send it after a CAN_DUCK, and "quiet for ever" is the
    //  exact failure this whole path exists to make unreachable.
    bool duckedByFocus = false;
    int  duckTicksLeft = 0;   // milliseconds remaining, not ticks
    int  deviceRevivalTicks = 0;
    //  The app starts in front; appSuspended/appResumed move it.
    bool appInForeground = true;
    //  Whether we are currently holding FLAG_KEEP_SCREEN_ON. Kept as a flag
    //  because setScreenSaverEnabled crosses into the JVM, and the timer runs
    //  ten to thirty times a second: only the CHANGES are worth a JNI call.
    bool holdingScreenAwake = false;
    //  Somebody else owns the speaker until we ask again. Set by a PERMANENT
    //  focus loss, cleared by coming back to the foreground.
    bool focusGivenAway = false;
    static constexpr int kDuckWatchdogMs = 6000;     // longer than any notification
    void audioFocusDucked() override;
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
    //  CARGAR UNA CARPETA ENTERA COMO KIT. Dieciseis samples es un kit, y
    //  montarlo de uno en uno son dieciseis viajes a la biblioteca por cada
    //  banco. Coge los audios de la carpeta que estas viendo, en el orden en
    //  que se ven, y los reparte por el banco que tengas delante.
    juce::TextButton browseKitButton { "CARGAR KIT" };
    //  Los sonidos de fabrica del banco que se esta viendo. Ver Kits.h.
    juce::TextButton browseFactoryButton { "FABRICA" };
    void loadFolderAsKit();
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
    //  La curva de los bordes, con los mismos numeros que suenan. Se llama
    //  detras de cada setTrim: el tope de un tercio de ventana depende del
    //  recorte, asi que mover un asa cambia la curva aunque el mando no se
    //  haya tocado.
    void pushFadesToWaveform();
    int  padSourceLength (int pad) const;
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

    //  DOS FORMAS DE CORTAR, y la segunda es la que hacia falta.
    //
    //  IGUALES divide por aritmetica: vale para un loop cuadrado y no vale para
    //  nada mas. GOLPES busca donde empieza cada golpe (Onsets.h) y corta ahi,
    //  que es lo que hace un troceador de verdad y lo que esta app no tenia.
    juce::TextButton chopEvenBtn { "IGUALES" }, chopHitsBtn { "GOLPES" };
    bool chopByHits = false;

    //  Los golpes del pad seleccionado, calculados una vez al abrir la ficha o
    //  al cambiar de modo, no en cada repintado: una FFT de 1024 sobre cuatro
    //  segundos son 750 ventanas, y el repintado ocurre en cada toque.
    std::vector<int> chopHits;
    int chopHitsFor = -1;              // para que pad se calcularon
    void refreshChopHits();

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

    //  ...and WHICH SOUND was on each pad, which the ValueTree does not carry.
    //
    //  captureState stores names, trims and parameters; it does not store the
    //  buffer a pad points at, and applyState cannot put one back. So undo
    //  could reverse every number and nothing that MOVED AUDIO BETWEEN PADS -
    //  which is the one action the sheet asks you to confirm. AUTO CHOP over
    //  sixteen pads, then DESHACER: the status said "undone", every pad still
    //  held the chop source, and each now played the whole break because the
    //  trims had been restored to 0..1. Sixteen bars of the same loop, and no
    //  way back.
    //
    //  Sixty-four reference-counted pointers is the whole cost, and they are
    //  released on the message thread like every other copy.
    //
    //  SIZED FROM AudioEngine::kNumPads, never written by hand. It was a
    //  literal 16 while the machine grew to 64, and capturePads/restorePads
    //  loop kNumPads: every pushUndo wrote 48 pointers past the end of the
    //  array - into whatever member the linker had put next - and then undo
    //  read them back and handed the wreckage to assignSampleToPad. It did not
    //  crash on the bench because the neighbours were other pad arrays of the
    //  same type; that is luck, not correctness.
    using PadSet = std::array<SampleBuffer::Ptr, AudioEngine::kNumPads>;
    void capturePads (PadSet& into) const;
    void restorePads (const PadSet& from);
    void performUndo();
    void performRedo();
    //  DESHACER MULTINIVEL. Era un solo escalon: una accion atras y se acabo,
    //  asi que dos AUTO CHOP seguidos dejaban el primero irrecuperable. Ahora
    //  son dos PILAS - atras y adelante - de hasta kUndoDepth. El coste es
    //  dieciseis ValueTree y dieciseis juegos de punteros con cuenta, que se
    //  sueltan en el hilo de mensajes como todo lo demas.
    static constexpr int kUndoDepth = 16;
    struct Snapshot { juce::ValueTree state; PadSet pads; juce::String label; };
    std::vector<Snapshot> undoStack, redoStack;

    //  TAP TEMPO. Cuatro toques, la media de los tres intervalos. Menos de
    //  cuatro no es un tempo, es un accidente; mas de cuatro y el primero ya no
    //  se parece al ultimo. Un hueco de mas de dos segundos empieza cuenta
    //  nueva, porque nadie marca un tempo cada dos segundos.
    juce::TextButton tapButton { "TAP" };
    static constexpr int kTapSlots = 4;
    double tapTimes[kTapSlots] {};
    int    tapCount = 0;
    void tapTempo();

    //  COPIAR Y PEGAR UN PATRON entre los ocho bancos. Rehacer a mano un
    //  patron para probarlo con otro sonido es la friccion mas tonta que tiene
    //  un secuenciador de ocho bancos.
    juce::TextButton copyPatBtn { "COPIAR" }, pastePatBtn { "PEGAR" };
    bool patClipFull = false;
    std::array<std::array<bool, AudioEngine::kNumPads>, AudioEngine::kNumSteps> patClip {};
    std::array<std::array<signed char, AudioEngine::kNumPads>, AudioEngine::kNumSteps> patClipNote {};
    int patClipLen = 16;
    void copyPattern();
    void pastePattern();
    juce::TextButton undoButton { "DESHACER" };
    juce::TextButton redoButton { "REHACER" };
    void rebuildChain();
    int  firstEmptyPad() const;
    void layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap);

    static constexpr int kNumPads      = AudioEngine::kNumPads;      // 64
    static constexpr int kPadsPerBank  = AudioEngine::kPadsPerBank;  // 16 on screen
    static constexpr int kNumBanks     = AudioEngine::kNumBanks;     // A B C D

    //  WHICH SIXTEEN THE GRID IS POINTING AT.
    //
    //  All sixty-four PadButtons exist, all the time; only the current bank's
    //  are visible and laid out. That is deliberate: every `pads[i]`,
    //  `refreshPad(i)` and `padClicked(i)` in this file takes a GLOBAL pad
    //  index, and there are twenty-five of them. Keeping the array global
    //  means not one of them has to learn about banks, and there is no
    //  off-by-sixteen to get wrong.
    int currentBank = 0;
    void selectBank (int bank);
    juce::OwnedArray<juce::TextButton> bankButtons;
    //  Two chips at each end of the seam, not four bunched at one end: the
    //  engraved PADS sits between them and the seam reads as balanced.
    //  paint() needs BOTH rectangles to know where the rule may run.
    juce::Rectangle<int> bankRowLeftArea, bankRowRightArea;
    static constexpr int kNumSteps  = AudioEngine::kNumSteps;    // 64 (max pattern length)
    static constexpr int kMinPatLen = AudioEngine::kMinPatLen;   // 16
    static constexpr int kMaxPatLen = AudioEngine::kMaxPatLen;   // 64
    static constexpr int kStepCols  = StepGrid::kBarSteps;       // one bar of 16 across

    AudioEngine  engine;
    SampleLoader loader { engine };

    juce::OwnedArray<PadButton> pads;
    StepGrid stepGrid;
    juce::OwnedArray<juce::TextButton> barButtons;   // bar 1..4 when the pattern is longer than one
    //  The grid shows ONE bank: sixteen lanes, whichever sixteen those are.
    bool  gridCells[AudioEngine::kNumSteps * AudioEngine::kPadsPerBank] {};
    signed char gridNotes[AudioEngine::kNumSteps * AudioEngine::kPadsPerBank] {};
    int   gridZati[AudioEngine::kPadsPerBank] {};
    bool  gridLoaded[AudioEngine::kPadsPerBank] {};
    int   selectedBar = 0;
    void  refreshStepGrid();

    // Module bar — rule of three: PADS / SEC / FX, each opening its floating
    // sheet (never a mode switch). CHOP lives inside PADS; CHAIN inside SEC.
    juce::TextButton padsButton  { "PADS" };
    juce::TextButton secButton   { "SEC" };
    juce::TextButton mixButton      { "MIX" };   // the 16-channel mixer sheet
    juce::TextButton songButton     { "SONG" };  // the arrangement timeline
    juce::TextButton xyButton       { "XY" };    // the live performance surface

    //  EL PANEL XY: LO QUE CONVIERTE SEIS EFECTOS EN UN INSTRUMENTO.
    //
    //  Los tres mandos de la cara sirven para AJUSTAR: los dejas donde quieres
    //  y ahi se quedan. Lo que no hacen es TOCAR. Un barrido de filtro no se
    //  hace girando un mando con el pulgar mientras la otra mano dispara pads,
    //  y menos dos parametros a la vez - que es justo lo que piden un filtro
    //  (frecuencia y resonancia), un delay (tiempo y realimentacion) o un
    //  crusher (bits y diezmado).
    //
    //  MOMENTANEO ES EL MODO POR DEFECTO, y es la decision que hace que esto
    //  sea un instrumento y no otro panel de ajustes: apoyas el dedo y el
    //  efecto ENTRA con su mezcla propia, lo mueves y barre, lo levantas y
    //  SALE. Eso es un gesto, tiene principio y final, y se puede fallar sin
    //  dejar la pista con un delay abierto. FIJO existe para cuando quieres
    //  dejarlo puesto, y es lo que hacen los tres mandos, asi que no es el que
    //  hace falta por defecto.
    XyPad xyPad;
    juce::OwnedArray<juce::TextButton> xyFxButtons;   // que efecto toca el panel
    juce::TextButton xyLatchButton { "FIJO" };
    juce::TextButton xyCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    int  xyFx = 0;                 // el efecto que el panel esta tocando
    bool xyLatch = false;          // false = momentaneo (entra al tocar, sale al soltar)
    bool xyWasOn = false;          // como estaba el efecto antes de apoyar el dedo
    void selectXyFx (int f);
    void toggleXyPanel();
    void xyMoved (float x, float y);
    void xyTouched (bool down);
    void refreshXyPad();
    void paintXySheetContent (juce::Graphics& g);
    bool moduleBarFits (int rowWidth, juce::TextButton** mb, int count) const;
    void layoutModuleBar (juce::Rectangle<int> row, juce::TextButton** mb, int vInset, int count = 6);

    //  SONG: pick what to place from the palette, then tap a cell. Choosing
    //  first and placing second beats drag-and-drop on a phone — a drag from a
    //  palette to a 20px cell is a gesture you lose halfway.
    Playlist songGrid;
    juce::OwnedArray<juce::TextButton> songPatBtns;   // P1..P8
    juce::TextButton songPadModeBtn { "SONIDO" };     // place a one-shot instead
    juce::TextButton songClearBtn   { "VACIAR" };
    juce::TextButton songModeBtn    { "CANCION" };    // song transport vs pattern/chain
    //  DOBLAR: la operacion que convierte esta pagina en un arreglo.
    //
    //  Una cancion se construye repitiendo y variando, no escribiendo sesenta y
    //  cuatro compases a mano. Sin esto, montar ocho compases a partir de
    //  cuatro es tocar treinta y dos celdas una por una, que es la razon por la
    //  que una pagina de arreglo se abandona.
    juce::TextButton songDoubleBtn  { "DOBLAR" };
    //  Las cinco herramientas de arreglo. Ver songCursor.
    juce::TextButton songInsertBtn  { "INSERTAR" };
    juce::TextButton songRemoveBtn  { "QUITAR" };
    juce::TextButton songCopyBtn    { "COPIAR" };
    juce::TextButton songPasteBtn   { "PEGAR" };
    juce::TextButton songLoopBtn    { "LOOP" };
    juce::TextButton songLeftBtn    { "ATRAS" };
    juce::TextButton songRightBtn   { "ADELANTE" };
    juce::TextButton songShortBtn   { "ACORTAR" };
    juce::TextButton songLongBtn    { "ALARGAR" };
    //  EL TRANSPORTE, DENTRO DE LA FICHA.
    //
    //  Montar un arreglo es poner un bloque y OIRLO, y la tapa de PLAY se
    //  queda debajo del velo: habia que cerrar la ficha, tocar PLAY, volver a
    //  abrirla y buscar por donde ibas. Esta enciende ademas el modo CANCION,
    //  porque darle a play en la pagina de la cancion y que suene el patron
    //  suelto es la respuesta a una pregunta que nadie hizo.
    juce::TextButton songPlayBtn { "PLAY" };
    juce::TextButton songCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::Slider     songLenSlider;
    juce::OwnedArray<juce::TextButton> songPageBtns;
    int songBrush   = 1;      // >0 pattern bank+1, <0 -(pad+1), 0 = eraser
    int songPage    = 0;
    int songCells[Playlist::kLanes * AudioEngine::kSongBars] {};
    //  El repintado de la TARJETA es opcional, y por eso es un parametro.
    //  La rejilla de la cancion se repinta sola cuando cambia su fuente; lo
    //  que hay pintado en la tarjeta -titulo y pista- no depende del
    //  transporte, asi que la llamada del temporizador no tiene nada que
    //  repintar y pedirlo costaba el fotograma entero, velo incluido.
    void refreshSong (bool repintarTarjeta = true);
    void doubleSong();

    //  LAS HERRAMIENTAS DE ARREGLO.
    //
    //  Con la paleta y la rejilla sola, una pagina de cancion sirve para
    //  escribir y para borrar, y eso no es arreglar: arreglar es meter un
    //  compas donde falta, quitar el que sobra y repetir el trozo que
    //  funciona. Sin ellas, meter un compas en medio de treinta y dos quiere
    //  decir volver a colocar a mano los treinta que van detras, que es por
    //  lo que una pagina de arreglo se abandona.
    //
    //  Las cuatro actuan sobre EL CURSOR - el ultimo compas que tocaste - y
    //  sobre los cuatro carriles a la vez, porque un compas de una cancion es
    //  una columna y no una casilla.
    int songCursor = 0;
    int songClip[Playlist::kLanes] {};   // el compas copiado, un carril por hueco
    bool songClipLleno = false;
    void insertSongBar();
    void removeSongBar();
    void copySongBar();
    void pasteSongBar();
    void toggleSongLoop();
    //  MOVER EL COMPAS MARCADO uno a la izquierda o a la derecha, con sus
    //  cuatro carriles. Reordenar era la unica operacion de arreglo que
    //  faltaba y la unica que no se puede improvisar con las otras: copiar,
    //  pegar y quitar deja el original detras y hay que acordarse de borrarlo,
    //  que es como se pierde un compas sin enterarse. Esto INTERCAMBIA, asi
    //  que no crea ni destruye nada y el cursor se va con el compas.
    void moveSongBar (int dir);
    //  RECORTAR O ALARGAR EL BLOQUE que hay bajo el cursor. Su longitud es
    //  cuantos compases ocupa en la linea de tiempo, no la del patron que
    //  lleva dentro: acortar un bloque obligaba antes a acortar el patron
    //  entero, o sea a cambiarlo en los otros sitios donde estuviera puesto.
    void resizeSongBlock (int dir);
    void toggleSongLane (int lane);
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

    //  Which sixteen of the sixty-four the mixer is showing. Its own value, not
    //  the face's: you mix bank C while the grid in front of you plays bank A,
    //  and a mixer that jumped whenever the face changed bank would be a mixer
    //  you cannot leave open.
    int mixBank = 0;
    juce::OwnedArray<juce::TextButton> mixBankBtns;
    void showMixBank (int bank);

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
    //  DONDE EMPIEZA LA FILA DE CADA CANAL. El chip de color y el nombre se
    //  pintan a mano, y estaban clavados en x=4 - que es cierto con una sola
    //  columna. Girado la mesa se reparte en dos, y los ocho canales de la
    //  derecha habrian pintado su chip encima de los de la izquierda. Se
    //  apunta al maquetar y se lee al pintar: un numero, un sitio.
    std::array<int, kNumPads> mixRowX {};

    //  EL MANUAL, DENTRO DE LA APP.
    //
    //  Un manual que vive en una pagina web es un manual que no esta ahi
    //  cuando hace falta: se consulta con el telefono en la mano, en mitad de
    //  algo, y casi siempre sin cobertura. Asi que va dentro, en los cuatro
    //  idiomas, y en la ficha de AJUSTES - que es donde ya estan los gestos,
    //  que son la mitad de las preguntas.
    //
    //  De consulta, no de lectura: ocho capitulos de cuatro o cinco lineas.
    //  El manual largo, con el porque de cada decision, es otra cosa y va
    //  fuera; esto es lo que se mira con una mano.
    struct ManualBody : public juce::Component
    {
        std::function<void (juce::Graphics&)> paintBody;
        void paint (juce::Graphics& g) override { if (paintBody) paintBody (g); }
    };
    //  LA BARRA DE TRABAJO.
    //
    //  Hay cuatro cosas en esta app que tardan lo suficiente como para que
    //  parezca que se ha colgado: decodificar un fichero grande, repartir una
    //  carpeta entera por los pads, quitar el ruido de una muestra larga y
    //  exportar. Ninguna congela la interfaz - todas corren en otro hilo -,
    //  pero eso no lo sabe nadie mirando la pantalla: sin nada que se mueva,
    //  una espera de dos segundos y una app colgada se ven exactamente igual.
    //
    //  Asi que se dice: que se esta haciendo, cuanto lleva, y cuanto falta
    //  cuando se puede saber. Va sobre la PANTALLA, que es donde la maquina
    //  habla, y NO bloquea: los pads siguen sonando mientras carga, que es la
    //  misma regla que ya cumple un aviso del sistema.
    juce::String busyWhat;
    int    busyJobs = 0;          // cuenta, no bandera: dos cargas a la vez
    float  busyProgress = -1.0f;  // negativo = no se sabe cuanto falta
    double busyStartMs = 0.0;
    juce::Rectangle<int> busyArea;
    //  Y es un COMPONENTE, no una pincelada en paint().
    //
    //  La pantalla la ocupa entera SpectrumDisplay, que es un hijo, y un hijo
    //  se pinta DESPUES que su padre: la barra dibujada en el paint de la cara
    //  quedaba debajo del espectro y no se veia ni un pixel. Como hijo se
    //  pinta encima, y ademas no se traga los toques - los pads de detras
    //  siguen sonando mientras carga, que es la regla de esta app.
    struct BusyBar : public juce::Component
    {
        std::function<void (juce::Graphics&)> paintBar;
        BusyBar() { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override { if (paintBar) paintBar (g); }
    };
    BusyBar busyBar;

    //  LEER SESENTA Y CUATRO MUESTRAS SIN CONGELAR NADA.
    //
    //  Arrancar la app y abrir un proyecto hacen lo mismo: leer hasta 64 WAV
    //  del disco. Estaba escrito como un bucle de 64 vueltas EN EL HILO DE LA
    //  INTERFAZ, y ahi una barra de progreso no sirve absolutamente de nada -
    //  mientras el bucle corre no se repinta nada, asi que la barra ni
    //  aparece. Lo que hace falta es trocear el trabajo.
    //
    //  El temporizador se come lo que quepa en 25 ms por vuelta y suelta. El
    //  total tarda lo mismo que antes - los bytes son los mismos - pero se
    //  reparte en trozos que caben entre dos fotogramas, asi que la maquina
    //  responde, la barra se mueve y los pads aparecen segun llegan en vez de
    //  aparecer los sesenta y cuatro a la vez detras de una ventana congelada.
    struct PadLoadJob
    {
        juce::File folder;            // vacia = la sesion
        bool fromSession = false;
        bool clearMissing = false;    // abrir proyecto vacia lo que no trae
        int  next = 0;
        int  restored = 0;
        //  De que pad sale el audio de cada uno, leido del estado ANTES de
        //  empezar a cargar: el trabajo corre en trozos de 25 ms y applyState
        //  no se llama hasta el final, asi que preguntarselo al arbol sobre la
        //  marcha llegaria tarde. -1 = tiene fichero propio.
        std::array<int, AudioEngine::kNumPads> source;
        PadLoadJob() { source.fill (-1); }
        std::function<void (int restored)> onDone;
    };
    std::unique_ptr<PadLoadJob> padJob;
    void stepPadJob();

    //  Y el simetrico: guardar tambien son 64 ficheros, y escribir en el
    //  almacenamiento compartido de Android no sale mas barato que leer.
    //  Mismo troceado, misma barra.
    struct PadSaveJob
    {
        juce::File   folder;
        juce::String name;
        int next = 0, written = 0, failed = 0;
    };
    std::unique_ptr<PadSaveJob> padSaveJob;
    bool padsBusy();
    void stepPadSaveJob();
    void finishProjectSave (const juce::String& name, const juce::File& folder,
                            int written, int failed);

    void finishSessionRestore (const juce::ValueTree& tree, int restored);
    void finishProjectOpen (const juce::String& name, const juce::ValueTree& tree, int restored);

    void beginBusy (const juce::String& what);
    void setBusyProgress (float p);
    void endBusy();
    void paintBusy (juce::Graphics& g);

    ManualBody     manualBody;
    juce::Viewport manualScroll;
    Sheet          manualSheet;
    juce::TextButton manualButton { "MANUAL" }, manualCloseButton { "x" };
    void paintManualSheetContent (juce::Graphics& g);
    void paintManualBody (juce::Graphics& g);
    int  manualContentHeight (int width) const;
    //  Ocho capitulos, y el numero de lineas de cada uno. Se declara aqui
    //  porque lo necesitan el alto del contenido y el pintado, y esos dos
    //  TIENEN que contar lo mismo o el desplazamiento se queda corto.
    static constexpr int kManualChapters = 9;   // ver kManual en el .cpp
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

    //  Two of the transport keys carry a second gesture (see the GESTOS page):
    //  hold CARGAR to open the library, hold PLAY to cut everything.
    HoldButton loadButton { "LOAD" };
    juce::TextButton testButton { "TEST" };
    juce::TextButton recButton  { "REC" };
    HoldButton playButton { "PLAY" };
    juce::TextButton clearButton { "VACIAR" };
    juce::TextButton reverseButton { "REV" };
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton autocutButton { "AUTOCUT" };
    //  BOMBEO: este pad hace agacharse a todo lo demas. Vive en la ficha del
    //  PAD y no en el mezclador porque es una propiedad del pad - cual de los
    //  sesenta y cuatro manda - y no una del canal.
    juce::TextButton duckButton { "BOMBEO" };
    juce::TextButton chopButton { "AUTO CHOP" };
    juce::TextButton micButton  { "GRABAR MIC" };
    //  RESAMPLE. The move this whole lineage is built on: play something,
    //  catch it, play the catch. Engine side in AudioEngine::startRecording.
    juce::TextButton resampleButton { "REMUESTREAR" };
    bool resamplingActive = false;
    void toggleResample();
    int  resamplingSlot = 0;   // lives in the PADS sheet

    //  Auditioning from the PADS sheet: the wave answers a tap, and this plays
    //  it from the top without having to reach past the sheet for the pad.
    juce::TextButton previewButton { juce::CharPointer_UTF8 ("\xe2\x96\xb6 OIR") };
    bool previewSounding = false;

    //  NORMALIZAR: la ganancia que pone el pico del RECORTE a -0.3 dBFS.
    //
    //  Sube la GANANCIA del pad; no toca la muestra. Es lo que hay que hacer
    //  aqui: la muestra la comparten el pad, la forma de onda, los cortes que
    //  salgan de ella y el proyecto guardado, y reescribirla obligaria a pasar
    //  un buffer nuevo por el intercambio de punteros y a soltar el viejo por
    //  el temporizador. La ganancia es un atomic float, se aplica en el mismo
    //  bloque, se deshace y no gasta memoria.
    juce::TextButton normButton { "NORMALIZAR" };
    void normalisePad();

    //  QUITAR RUIDO. Resta espectral con el perfil sacado de la propia
    //  muestra - ver Denoise.h para por que no es una puerta de ruido. Corre
    //  en el hilo de mensajes sobre una COPIA, y la copia sustituye a la
    //  muestra del pad por el mismo camino que un corte o un remuestreo: el
    //  audio nunca ve un buffer a medio escribir.
    juce::TextButton denoiseButton { "QUITAR RUIDO" };
    void denoisePad();
    //  Un hilo para limpiar, porque la limpieza NO cabe en el hilo de la
    //  interfaz: son 7 ms por segundo de audio medidos en el banco, o sea 2.1 s
    //  con una muestra de cinco minutos y ocho con una de veinte. Android
    //  ensena el cartel de "la aplicacion no responde" a los cinco.
    juce::ThreadPool denoisePool { 1 };
    bool denoiseBusy = false;

    //  Los tres del zoom, encima de la propia onda y no en una fila suya: la
    //  ficha ya iba justa de alto y una fila mas se la habria quitado a lo
    //  unico que este zoom existe para mirar.
    juce::TextButton zoomOutButton { "-" }, zoomInButton { "+" }, zoomFitButton { "x1" };

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
    //  El filtro del pad. Ver AudioEngine::setPadCutoff: no lleva interruptor
    //  porque el corte arriba del todo ya es "sin filtro".
    juce::Slider cutSlider, resoSlider;
    //  El fundido de los bordes del recorte. Va en RECORTE y no en SONIDO
    //  porque es de la muestra y no del pad: lo que suaviza es el sitio por el
    //  que se corto. Ver AudioEngine::setPadFadeIn.
    juce::Slider fadeInSlider, fadeOutSlider;
    //  What a step DOES, not just which pads it fires: how hard, how many
    //  times, and how far off the grid the odd ones sit.
    juce::Slider patternSlider, noteSlider, lengthSlider, velSlider, rollSlider, swingSlider;
    //  LA REJILLA: cuanto dura un paso. Cinco posiciones y no un mando libre,
    //  porque los valores utiles son cinco y estan a distancias que no son
    //  regulares - entre 1/16 y su tresillo hay un factor de 1.5 y entre 1/16
    //  y 1/32 hay uno de 2. Un dial con esos cinco puntos seria un dial que
    //  hay que acertar; dos teclas los recorren y ademas dicen cual es.
    juce::Slider gridSlider;
    static constexpr int kNumGrids = 7;
    //  En negras por paso, en el mismo orden que los nombres de abajo.
    //  SIETE. Faltaban el tresillo de fusa y la semifusa: con 1/32 como paso
    //  mas corto no se puede escribir un redoble de trap ni un tresillo rapido,
    //  que es media musica hecha con esto. El motor acota stepBeats en 0.02, y
    //  1/64 son 0.0625.
    static constexpr float kGridBeats[kNumGrids] =
        { 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f };
    static const char* gridName (int i);
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
    std::array<float, kNumPads> padCut {};        // Hz, kFiltOpenHz = abierto
    std::array<float, kNumPads> padReso {};       // 0..1
    std::array<float, kNumPads> padFadeIn {};     // ms
    std::array<float, kNumPads> padFadeOut {};    // ms
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
    bool  vuHeld = false;           // solo el banco: ZATI_VU congela la tira

    int  selectedPad   = -1;
    bool loadArmed     = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

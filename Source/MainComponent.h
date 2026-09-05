#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AudioEngine.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "ChopPreview.h"
#include "SpectrumDisplay.h"
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include "ProjectStore.h"
#include "StepGrid.h"
#include "Playlist.h"
#include "PianoRoll.h"
#include "Teclado.h"
#include "AudioFocus.h"
#include "SessionKeeper.h"
#include "Exporter.h"
#include "Bitacora.h"
#include "AudioPath.h"
#include "UiAudit.h"
#include "XyPad.h"
#include "EqCurve.h"
#include "FxMini.h"
#include "MidiIo.h"
#include "Instrumentos.h"

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
    //  Cuantas tarjetas tiene el tour. Publico porque los textos viven fuera de
    //  la clase - los mide la maqueta ademas de pintarlos.
    static constexpr int kTourPasos = 15;

    //  LA BIENVENIDA SON CUATRO, Y LOS OTROS ONCE ESTAN DETRAS DE UNA PUERTA.
    //
    //  Quince tarjetas la primera vez son el manual otra vez: este proyecto ya
    //  escribio que «un parrafo largo encima de una maquina oscurecida no se
    //  lee, se salta», y quince tarjetas son la misma frase a otra escala.
    //
    //  Los cuatro primeros -los pads, los bancos, CARGAR/REC/PLAY y el
    //  transporte- son LA APP: con eso ya suena. Los otros once son el
    //  recorrido, y se piden. No se borra ninguno: el cuarto ofrece seguir, y
    //  la tapa TOUR de AJUSTES sigue abriendo el recorrido entero desde el
    //  principio.
    static constexpr int kTourBienvenida = 4;
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;

    void paint (juce::Graphics& g) override;
    //  LA PORTADA DEL ARRANQUE. Ver MainComponent::paintOverChildren: la cara
    //  no se enseña hasta que ha dejado de moverse.
    void paintOverChildren (juce::Graphics& g) override;
    //  El primer instante en el que se le puede preguntar a Android por sus
    //  margenes: hasta que la ventana no esta enganchada, getRootWindowInsets
    //  devuelve nulo. Ver refreshSystemInsets.
    void parentHierarchyChanged() override;
    void resized() override;

private:
    //  EL RELOJ Y EL DIBUJO SON DOS COSAS, y hasta hoy eran un temporizador.
    //
    //  Habia UN `startTimer (dev.uiIntervalMs)` y de el colgaba todo lo que se
    //  mueve: medidores, osciloscopio, destellos, los tres cabezales, el
    //  analizador del EQ y la portada, mas los seis vigilantes. O sea que la
    //  tasa de refresco de la app la decidia una tabla de gama escrita a mano
    //  -10, 16.7, 25 o 30 fps- y no el panel que tienes delante.
    //
    //  `timerCallback` se queda con lo que NO puede depender de que la
    //  pantalla refresque: un vblank no llega con la app en segundo plano ni
    //  con la pantalla apagada, y estos son vigilantes -el ducking sin GAIN de
    //  vuelta, el dispositivo que no revive, los under-runs, el tope de la
    //  portada-. Ponerlos en el vblank seria que el ducking no se deshace con
    //  el movil en el bolsillo.
    //
    //  `pintaCuadro` es el DIBUJO, y cuelga del vblank. Toma los milisegundos
    //  de verdad transcurridos porque las constantes de tiempo visuales pasan
    //  a estar en milisegundos: a 120 Hz un `peak *= 0.72` por fotograma cae
    //  cuatro veces mas rapido que a 30, que es exactamente el fallo que la
    //  app YA tenia entre un movil de gama alta y uno de gama basica.
    void timerCallback() override;
    void pintaCuadro (double dtMs);
    void watchAudioDevice();

    //  EL REPINTADO CUELGA DEL VBLANK. En Android es Choreographer
    //  (`ComponentPeerView.java` -> `handleDoFrameCallback` ->
    //  `callVBlankListeners`), o sea la cadencia real del panel; en escritorio
    //  X11 lo emula a la frecuencia del display. Con la firma que trae MARCA
    //  DE TIEMPO, que es de donde sale el `dt` de verdad y no de un numero
    //  escrito aqui.
    juce::VBlankAttachment vblank;
    double vblankUltimoSec = 0.0;
    //  SIN VBLANK NO HAY APP MUDA: si no llega ninguno -un peer que no los
    //  entrega, una ventana sin pantalla- el reloj pinta el, a la cadencia del
    //  suelo. Es la hermana de «ningun camino puede dejar la app en silencio».
    double vblankUltimoMs = 0.0;
    //  LO QUE CUESTA UN FOTOGRAMA, Y CUANTOS SE SALTAN. Pintar a 120 Hz lo que
    //  se pintaba a 16.7 es siete veces el trabajo, asi que la app se lo mide y
    //  se defiende: cuando un cuadro se pasa de su presupuesto se saltan
    //  vblanks, que es la misma forma que ya tiene `bufferBursts` subiendo
    //  cuando aparecen under-runs. Media movil y no el ultimo valor: un solo
    //  cuadro interrumpido por el sistema no puede partir la tasa por dos.
    double cuadroCosteMs = 0.0;
    int    cuadroSaltar  = 0;
    //  Lo GASTADO en el cuadro que se acaba de pedir: `pintaCuadro` -que
    //  alimenta el osciloscopio, las dos FFT del analizador y las tres
    //  rejillas- mas lo que cueste el `paint` que ese cuadro provoca. Son las
    //  dos mitades del mismo trabajo y las dos corren en el hilo de mensajes,
    //  asi que sumarlas es lo unico que mide un fotograma entero.
    double cuadroGastoMs = 0.0;
    double cuadroUltimoMs = 0.0;

    //  ZATI_VBLANK=hz — LA CADENCIA COMO ENTRADA DEL BANCO, igual que ZATI_SKIN
    //  con la carcasa, ZATI_DLC con los packs y ZATI_INSETS con los margenes.
    //  Sin esto solo se puede medir la maquina que haya delante: X11 entrega
    //  vblanks a la frecuencia del display -100 Hz cuando no la declara, que es
    //  lo que da Xvfb- y no hay forma de preguntar «¿se ve igual a 60 que a
    //  120?», que es justo la regla que esta tanda necesita.
    struct RelojDibujo : public juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    };
    RelojDibujo relojDibujo;
    //  Y NUNCA MAS LENTO QUE EL SUELO: el salto se acota a lo que `relojMs`
    //  permite. Un aparato que no llega cae hasta ahi y no mas.
    void enVBlank (double timestampSec);

    // One perform screen; every deep feature (pad settings, sequencer,
    // pattern chain, auto chop, FX) opens as a pop-up sheet over it — a dim
    // scrim + a bottom card, closed by tapping outside or the x button.
    // Controls are children of their sheet, not of MainComponent, so an
    // open sheet naturally blocks clicks to the machine face behind it.
    class Sheet : public juce::Component
    {
    public:
        //  EL NUMERO DE CAPA. Ver UiAudit::capaActual: un rotulo pintado y un
        //  control solo se pisan de verdad si viven en la misma ficha, y la
        //  cara sigue debajo de una ficha abierta con todas sus tapas
        //  maquetadas. Se pone al construir y lo hereda todo lo que cuelga.
        Sheet() { getProperties().set ("capa", UiAudit::siguienteCapa++); }

        std::function<void()> onDismiss;
        std::function<void (juce::Graphics&)> paintContent;   // titles, readouts, rings
        //  A click INSIDE the card. Painted controls - things with no
        //  component of their own, like the zati swatches - hang off this.
        std::function<void (juce::Point<int>)> onContentClick;
        juce::Rectangle<int> sheetBounds;

        //  UNA FICHA QUE SE PINTA ENTERA ELLA. El tour no tiene tarjeta: no
        //  dibuja un rectangulo centrado con un velo detras, dibuja un foco -
        //  todo oscuro menos el control que explica- y un muelle pegado a un
        //  borde. Sin esto, Sheet::paint le echaba su propio velo del 45 % por
        //  encima y ademas se iba antes de llamarla, porque sheetBounds esta
        //  vacio a proposito.
        bool pintaTodo = false;

        //  UNA FICHA QUE SE DESPLAZA.
        //
        //  Hasta ahora la regla era "todo tiene que caber en la tarjeta, y lo
        //  que no cabe se cae", y de ahi salia todo lo demas: las pestanas a 32
        //  px en vez de 44, las filas apretadas, y una escalera de prioridad en
        //  cada ficha decidiendo que funcion desaparece en un movil estrecho.
        //  Medido: de las veintiuna fichas, diez tenian controles por debajo del
        //  dedo minimo y en casi todas la causa era la misma - no habia alto.
        //
        //  Con desplazamiento el alto deja de ser escaso y cada control puede
        //  medir lo que tiene que medir. El punto de palanca es que TODAS las
        //  fichas empiezan su maquetado en el rectangulo que devuelve
        //  sheetFromBottom: si ese rectangulo pasa a estar dentro de un
        //  Viewport, el maquetado entero se muda solo y no hay que tocar una
        //  sola cuenta de las que ya estaban medidas.
        //
        //  Solo las de CONTROLES. Las de LIENZO -la rejilla de pasos, el piano
        //  y la cancion- no se desplazan nunca: se pintan con el dedo
        //  arrastrado, y un arrastre vertical que a veces escribe una nota y a
        //  veces mueve la pagina es un gesto que no se puede aprender. Y las
        //  que YA traen su propia lista desplazable dentro -la mesa, el manual
        //  y el navegador- tampoco, que anidar dos desplazamientos es la otra
        //  forma de que un arrastre no se sepa de quien es.
        struct Cuerpo : public juce::Component
        {
            std::function<void (juce::Graphics&)> paintBody;
            std::function<void (juce::Point<int>)> onClick;
            int capa = 0;
            void paint (juce::Graphics& g) override
            {
                //  El cuerpo desplazable pinta el contenido de SU ficha, asi
                //  que lleva su mismo numero de capa. Ver UiAudit::capaActual.
                UiAudit::capaActual = capa;
                //  Y el cuerpo esta DESPLAZADO dentro de la ventana: sus
                //  coordenadas no son las del volcado. Ver origenPintado.
                if (auto* top = getTopLevelComponent())
                    UiAudit::origenPintado = top->getLocalPoint (this, juce::Point<int> (0, 0));
                if (paintBody) paintBody (g);
            }
            void mouseDown (const juce::MouseEvent& e) override { if (onClick) onClick (e.getPosition()); }
        };
        //  Y LA QUE TRAE SU PROPIA LISTA DENTRO. La mesa, el manual, el
        //  navegador y PROYECTOS no se desplazan como ficha -anidar dos
        //  arrastres es la otra forma de que un gesto no se sepa de quien es-
        //  pero se alcanzan enteras igual, porque lo que las llena es una lista
        //  que ya se desplaza sola. Piden a proposito mas alto del que hay, asi
        //  que la regla de UiAudit::tarjeta no les aplica: sin decirlo, la
        //  decima regla del banco las sacaria como fallo en cada corrida.
        bool listaPropia = false;

        Cuerpo cuerpo;
        juce::Viewport vista;
        bool desplazable = false;

        //  Se llama una vez, al construir, y decide donde viven los hijos de
        //  esta ficha. Quien no la llame se queda exactamente como estaba.
        void hazDesplazable()
        {
            desplazable = true;
            cuerpo.capa = (int) getProperties()["capa"];
            vista.setViewedComponent (&cuerpo, false);
            vista.setScrollBarsShown (true, false);
            vista.setScrollBarThickness (8);
            addAndMakeVisible (vista);
            cuerpo.paintBody = [this] (juce::Graphics& g) { if (paintContent) paintContent (g); };
            cuerpo.onClick   = [this] (juce::Point<int> p) { if (onContentClick) onContentClick (p); };
        }

        //  Donde se anaden los hijos: el cuerpo si se desplaza, la ficha si no.
        juce::Component& donde() { return desplazable ? (juce::Component&) cuerpo : (juce::Component&) *this; }

        //  UN TOQUE FUERA DE LA TARJETA, ANTES DE CERRAR.
        //
        //  La tarjeta se centra al 78 % para que la maquina se siga viendo por
        //  debajo -lo dice sheetFromBottom- y se veia, pero no se podia tocar:
        //  cualquier toque ahi cerraba la ficha. O sea que los pads que asoman
        //  estaban de adorno, y cambiar el pad que edita el secuenciador
        //  costaba cerrar, elegir y volver a abrir.
        //
        //  Devuelve true si el toque se consumio, y entonces la ficha se queda.
        //
        //  El tour la lleva puesta como todas -entra por `openSheet`- y aun asi
        //  no se cierra por un roce: lo que lo protege no es no tenerla sino que
        //  `onDismiss` sea nulo. Su tarjeta ademas mide vacio, asi que TODO
        //  toque cae «fuera» y llega aqui: tocar un pad que asoma durante el
        //  tour lo hace sonar, que es exactamente lo que el tour esta pidiendo
        //  que se toque.
        std::function<bool (juce::Point<int>)> onFuera;

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! sheetBounds.contains (e.getPosition()))
            {
                if (onFuera && onFuera (e.getPosition())) return;
                if (onDismiss) onDismiss();
            }
            //  Y si se desplaza, el toque dentro lo recoge el cuerpo, que es
            //  quien tiene las coordenadas buenas: aqui llegaria el de la
            //  ventana y pintaria el zati que no es.
            else if (onContentClick && ! desplazable)
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

    //  Y LA REJILLA DE DIECISEIS, que es lo que las flechas no pueden ser.
    //
    //  PAD -/+ pasean, y pasear de uno en uno por un banco entero para llegar
    //  a la campana que esta en el 14 no es tener el boton. Un selector de
    //  dieciseis EN FILA ya esta medido y no cabe -26 px en 280- asi que va en
    //  cuatro por cuatro, que es ademas la forma de la cara: el 07 esta donde
    //  la mano ya sabe. Es la misma solucion que ya usan el RACK, la paleta de
    //  patrones y la cadena.
    //
    //  Y va COMO CAPA encima de la tarjeta y no como fila dentro de la ficha:
    //  la pagina del piano es de LIENZO -no se desplaza- y cuatro filas de
    //  tapas cuestan ~172 px de los ~260 que tiene la rejilla de tono, o sea
    //  que dejaria la celda por debajo del suelo que costo arreglar. Encima
    //  cuesta CERO de alto permanente.
    //
    //  Es un Sheet y no una clase nueva: asi trae ya su capa para el banco -lo
    //  mismo que le faltaba a XyPanel-, su velo, y el cierre al tocar fuera.
    Sheet padPickSheet;
    juce::TextButton padPickCloseBtn { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::OwnedArray<juce::TextButton> padPickBtns;
    //  Y LOS CUATRO BANCOS DEBAJO, porque si no la rejilla llega a dieciseis
    //  pads y no a sesenta y cuatro - y PAD -/+, que es lo que viene a
    //  sustituir, si arrastran la vista de bancos.
    juce::OwnedArray<juce::TextButton> padPickBankBtns;
    void paintPadPickContent (juce::Graphics& g);
    void refrescaPadPicker();
    //  Las dos puertas: la cabecera del piano y la de la ficha del pad. Dos
    //  puertas a una funcion no son dos copias - la que se va deja una tapa
    //  que lleva a ella, y aqui no se va ninguna.
    //  Y la tercera: el troceado tambien es de UN pad -«va a pads: 5 6 7 8»- y
    //  hasta ahora la unica forma de cambiarlo era cerrar, elegir y volver a
    //  abrir. Es exactamente el viaje que esta puerta existe para quitar.
    juce::TextButton pianoPadPickBtn { "PAD" }, padPadPickBtn { "PAD" }, chopPadPickBtn { "PAD" };
    bool padPickAbierto = false;
    void abrePadPicker (bool abrir);

    //  A QUE CANAL VA ESTE PAD. La misma rejilla y por lo mismo: dieciseis en
    //  fila son 26 px en 280, y cuatro por cuatro son exactamente los que hay
    //  -sin fila de bancos, que es lo unico que la separa de la de pads-.
    //
    //  Y va como CAPA por el mismo argumento: la pagina RIG de EL PAD ya se
    //  mide contra el hueco que le dan y tiene su propia escalera de lo que se
    //  cae; cuatro filas de tapas dentro se llevarian por delante la fila de
    //  puertas. Encima cuesta CERO de alto permanente y una tapa en la fila que
    //  ya estaba.
    Sheet canalSheet;
    juce::TextButton canalCloseBtn { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::OwnedArray<juce::TextButton> canalBtns;
    juce::TextButton padCanalBtn { "CANAL" };
    bool canalPickAbierto = false;
    void abreCanalPicker (bool abrir);
    void paintCanalPickContent (juce::Graphics& g);
    //  El rotulo de la puerta, que dice a que canal va el pad elegido. Lo llama
    //  `selectPad` y quien mueva la asignacion.
    void refrescaCanalDelPad();
    //  Un toque en un pad que asoma por debajo de una ficha abierta. Ver
    //  Sheet::onFuera.
    int  padDetras (juce::Point<int> p) const;
    bool tocaPadDetras (juce::Point<int> p);
    //  Los siete mandos de la tira, apuntando al paso tocado del pad elegido.
    void refrescaTiraPaso();
    //  Las dos herramientas del piano. Excluyentes: con las dos apagadas se
    //  dibuja, que es lo que hace falta el 90% del tiempo.
    juce::TextButton pianoLapizBtn { "LAPIZ" }, pianoGomaBtn { "GOMA" }, pianoCorteBtn { "TIJERAS" };
    //  CUANTAS OCTAVAS SE VEN. Su rotulo dice el estado y no la accion, que es
    //  lo que hace falta cuando el estado no se puede deducir mirando: trece
    //  filas y veinticinco se distinguen contando, y nadie cuenta.
    juce::TextButton pianoVerBtn { "1 OCTAVA" };
    static juce::File pianoPrefFile();
    void  loadPianoPref();
    void  savePianoPref() const;
    void  aplicaFilasPiano (int filas);
    //  Cuantas filas de tapas pidio la pagina del piano en esta pasada, para
    //  que la pregunta de "¿caben dos octavas?" se haga con el alto de verdad y
    //  no con uno inventado en el sitio - que es el fallo que ya costo la altura
    //  de la fila de herramientas de CANCION.
    int   filasTapasPiano = 1;
    //  El suelo de la fila de una nota. Ver Tests/expo.py: MIN_NOTE.
    static constexpr int kSueloNota = 16;
    void pianoStepPad (int dir);
    int pianoBase = -12;                       // el semitono de la fila de abajo
    signed char pianoCells[AudioEngine::kNumSteps * PianoRoll::kMaxNotas] {};
    //  Un largo por paso, en cuartos. Ver AudioEngine::setStepLen.
    unsigned char pianoLargos[AudioEngine::kNumSteps] {};
    void refreshPiano (bool repintarTarjeta = true);
    void seguirCompas (int pasoAbsoluto);

    //  LO MAS ESTRECHO QUE PUEDE QUEDAR UN RECORTE, en MUESTRAS y no en
    //  porcentaje del fichero. Estaba escrito dos veces y en las dos como
    //  fraccion -0.01 en los mandos, 0.005 en la onda-, y una fraccion del
    //  fichero entero no significa lo mismo en un golpe de bombo que en un
    //  tema de 235 segundos: alli el 1% son 2.35 SEGUNDOS, o sea que aislar el
    //  primer golpe de un disco no se podia por mucho que ampliaras.
    //
    //  Doscientas cincuenta y seis muestras son 5.3 ms a 48 kHz. Sigue siendo
    //  un minimo -por debajo no hay ni rampa de fundido ni cuatro muestras que
    //  darle a la interpolacion de Hermite- pero es un minimo del tamano de un
    //  chasquido y no del tamano de un compas.
    static constexpr int kMinTrimSamples = 256;
    double minTrim01 (int pad) const;
    //  Lo que la costura de los bancos necesita para que un chip llegue al
    //  dedo: la tapa, su aire y el labio de la placa que se pinta cinco
    //  pixeles por encima de la rejilla y que por tanto no es suyo. Vive aqui
    //  porque lo preguntan dos sitios -quien reparte el alto y quien coloca la
    //  costura- y la misma regla escrita dos veces son dos reglas.
    static constexpr int kBankSeamWant = Metrics::hit + Metrics::gap + ZatiLookAndFeel::kPlateLip;
    void pianoCellToggled (int paso, int semi);

    //  ------------------------------------------------------------------
    //  LA SELECCION DEL PIANO, y lo que se puede hacer con ella.
    //
    //  Vive aqui y no en PianoRoll porque aqui estan los datos: el componente
    //  sabe geometria y este sabe que hay escrito. Un conjunto de (paso, semi)
    //  guardado como pares ordenados - son unas pocas decenas de notas y
    //  buscarlas linealmente cuesta menos que mantener un indice.
    struct NotaSel { int paso; int semi; };
    std::vector<NotaSel> pianoSel;
    bool pianoEnSel (int paso, int semi) const
    {
        for (const auto& n : pianoSel) if (n.paso == paso && n.semi == semi) return true;
        return false;
    }
    int  pianoEscribe (int paso, int semi, bool poner, int cuartos);
    bool moviendoSel = false;   // un solo pushUndo por arrastre
    void pianoBanda (int paso0, int semi0, int paso1, int semi1);
    void pianoMueveSel (int dPaso, int dSemi);
    void pianoVaciaSel();
    void pianoCopiaSel();
    void pianoPegaSel();

    //  EL PORTAPAPELES DE NOTAS, RELATIVO a la esquina de arriba a la
    //  izquierda de lo copiado: pegar tiene que caer donde toques y no donde se
    //  copio. Y CON EL LARGO, que es la leccion de copiarFila contada en el
    //  piano - una copia que se deja el largo ha dejado de ser la misma figura.
    struct NotaPeg { int dPaso; int semi; int cuartos; };
    std::vector<NotaPeg> pianoPortapapeles;

    juce::TextButton pianoSelBtn { "SEL" }, pianoCopiaBtn { "COPIAR" }, pianoPegaBtn { "PEGAR" };

    //  CUANTAS COLUMNAS SE VEN. Ocho es medio compas con celdas del doble de
    //  ancho -para escribir en 1/32-, dieciseis es el compas de siempre y
    //  treinta y dos son dos compases para ver la frase. Con el suelo de la
    //  celda decidiendo, como todo lo demas.
    int pianoCols = AudioEngine::kBarSteps;
    juce::TextButton pianoZoomBtn { "1 COMPAS" };
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
        //  SU NUMERO DE CAPA, igual que una ficha. Ver UiAudit::capaActual:
        //  un rotulo pintado y un control solo se pisan de verdad si viven en
        //  la misma superficie, y este panel pinta su PROPIA tarjeta opaca
        //  encima de la cara (ver paintXySheetContent) - lo que queda debajo
        //  no se ve. No lo tenia porque no es un Sheet, asi que sus seis
        //  efectos y su MOMENTANEO se comparaban contra la maquina entera.
        //
        //  Estaba ahi desde el primer dia y no se noto hasta que la cabecera
        //  paso a decir el nombre entero: "ZATI" mide 47 px y no llegaba a
        //  MOMENTANEO, "ZATI SAMPLER" mide 152 y si - 13 hallazgos de TAPADO
        //  en las 896 corridas, todos la misma pareja. Un solape debajo de una
        //  tarjeta opaca es exactamente lo que la regla dice que no cuenta.
        XyPanel() { getProperties().set ("capa", UiAudit::siguienteCapa++); }

        std::function<void (juce::Graphics&)> paintContent;
        void paint (juce::Graphics& g) override
        {
            //  Y lo que ESTE panel pinte queda apuntado como suyo, no como de
            //  la cara: sin esto su titulo se quedaria en la capa 0 y dejaria
            //  de compararse con su propio MOMENTANEO, que es justo el solape
            //  que `antesDe` protege ahi -"XY - DLY - EN ESPERA" en arabe
            //  pasaba por debajo-. Quitar un falso positivo no puede costar
            //  una comprobacion de verdad.
            UiAudit::capaActual = (int) getProperties()["capa"];
            if (paintContent) paintContent (g);
        }
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
    //  Y UNA QUINTA, que es donde el IDIOMA y la CARCASA tenian que haber
    //  estado siempre: colgaban de AUDIO porque ahi habia sitio, no porque
    //  tengan nada que ver con el reloj y el bufer. Una pagina se llama por lo
    //  que contiene.
    enum SetPage { pageAudio = 0, pageMidi, pageAspecto, pageProjects, pageGestures };
    int setPage = pageAudio;
    juce::TextButton pageAudioBtn { "AUDIO" }, pageMidiBtn { "MIDI" },
                     pageAspBtn { "ASPECTO" },
                     pageProjBtn { "PROYECTOS" }, pageGestBtn  { "GESTOS" };
    void paintGesturesPage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintMidiPage (juce::Graphics& g, juce::Rectangle<int> area);
    void paintAspectoPage (juce::Graphics& g);
    juce::Rectangle<int> midiArea;
    juce::Rectangle<int> gesturesArea;
    //  Donde se pinta el parrafo del tour. Ver paintTourSheetContent.
    juce::Rectangle<int> tourBodyArea;
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
    //  EL INTERRUPTOR DE LO QUE TOCA PLAY, en la fila del transporte y no en
    //  otra ficha: es la pregunta que se hace justo antes de pulsar PLAY. Ver
    //  ponModoCancion - el estado es UNO y las tapas son tres, igual que ya
    //  pasa con PLAY (la cara, la cancion y el secuenciador).
    juce::TextButton modoBtn { "PATRON" }, seqModoBtn { "PATRON" };
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
    //  LO QUE MIDE EL CONTENIDO DE LA PAGINA "EL PAD", en un solo sitio.
    //
    //  Estaba escrito dos veces: la ficha PEDIA 418 + 3*secH y el maquetado
    //  calculaba lo que de verdad necesita -needFull- para decidir si apretar.
    //  Los dos numeros vivieron juntos hasta que los seis envios se mudaron al
    //  RACK: el maquetado se entero y la peticion no, asi que la tarjeta seguia
    //  reservando dos filas de mandos de 86 px que ya no existen. Medido en
    //  412x915: 499 px pedidos para 365 de contenido, 134 de hueco al final de
    //  la ficha. La misma regla escrita dos veces son dos reglas.
    int  altoContenidoElPad (int ancho) const;
    bool padPuertasWraps (int rowWidth) const;
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
    juce::TextButton padRackBtn { "RACK" };

    //  DIECISEIS NIVELES.
    //
    //  El gesto de siempre en un sampler de pads: se elige uno y los dieciseis
    //  de la rejilla pasan a ser ESE pad tocado a dieciseis fuerzas, de la mas
    //  floja abajo a la mas fuerte arriba. Es la unica forma de tocar dinamica
    //  con los dedos en una pantalla que no tiene tacto: un cristal no sabe
    //  cuanto aprietas, y sin esto todos los golpes salen iguales.
    //
    //  El pad se CAPTURA al encender y no se lee del selector en cada golpe:
    //  si se leyera, tocar un pad cambiaria el pad de destino y el modo se
    //  perseguiria a si mismo.
    juce::TextButton nivelesButton { "16 NIVELES" };
    bool nivel16 = false;
    int  nivelPad = 0;

    //  QUE SUENA AL TOCAR EL PAD `index`, y con que fuerza. Una funcion y no la
    //  cuenta escrita dentro de padClicked, porque el banco tiene que poder
    //  preguntar exactamente lo que el dedo hace: una regla medida en un sitio
    //  y aplicada en otro son dos reglas. Ver auditNiveles.
    struct Disparo { int pad; float vel; };
    Disparo disparoDe (int index) const;

    //  The captions of the sequencer card, recorded by resized() instead of
    //  reconstructed by paint() from each control's bounds. Reconstructing
    //  them was fine while every control was on screen at once; with two pages
    //  it drew the name of a control that was HIDDEN - four ghost captions
    //  lying across the grid. What is laid out is what is labelled.
    //  `filas` son las filas de CONTROL que cuelgan del rotulo, para el panel
    //  que se pinta detras del grupo: casi todos llevan una, y la fila de
    //  herramientas del patron lleva dos cuando no caben las seis en una.
    //  `grupo` une bandas que NO comparten renglon. El panel se deduce
    //  uniendo las que empiezan a la misma altura, que es lo correcto para
    //  PATRON y LARGO -parten un renglon en dos- y no llega para la tira del
    //  paso: NOTA/GOLPE, REPETIR/CORTE y los cuatro bloqueos son tres filas
    //  separadas por Metrics::halfGap, o sea cuatro pixeles, y tres paneles a
    //  cuatro se comen dos por lado cada uno y salen TOCANDOSE - cero de hueco,
    //  medido por Tests/paneles.py. Y ademas son una sola cosa: todo lo que hay
    //  ahi actua sobre el paso que se acaba de tocar.
    struct SeqLabel { juce::Rectangle<int> band; juce::String key; int filas = 1; int grupo = 0; };
    juce::Array<SeqLabel> seqLabelBands;

    //  EL PANEL DE UN GRUPO, en UN sitio y con cuatro clientes.
    //
    //  Estaba escrito dentro del pintor del secuenciador, que era la unica
    //  ficha de veintiuna que agrupaba sus controles. En cuanto le salieron
    //  tres clientes mas se saco, que es lo que ya se hizo con `normaliza`
    //  cuando `render` dejo de ser el unico camino al final, y con el reparto
    //  de un kit cuando le salio el segundo: dos sitios que dibujan el mismo
    //  panel por su cuenta se separan, y el sintoma habria sido "en AJUSTES
    //  los grupos se ven distinto" sin poder decir por que.
    void pintaPaneles (juce::Graphics& g, const juce::Array<juce::Rectangle<int>>& grupos) const;

    //  ...y los grupos de las tres fichas que no son el secuenciador. Uno por
    //  ficha y no uno compartido: resized() maqueta TODAS las fichas en la
    //  misma pasada -este es un fichero donde eso ya se paga en varios sitios-
    //  asi que un array unico lo llenaria la ultima y lo pintarian las cuatro.
    //
    //  Como los del secuenciador, NO se maquetan: se apuntan mientras se
    //  reparte el alto, con las coordenadas que la maqueta acaba de dar. Por
    //  eso no cuestan un pixel - que es lo que el banco comprueba en las 896
    //  corridas de expo.py, donde la celda de una rejilla canta cualquier
    //  altura que alguien se haya llevado.
    juce::Array<juce::Rectangle<int>> padGrupos, setGrupos, songGrupos;
    //  ...and the line at the foot of the PASO page that names the step being
    //  edited. Reserved by resized() for the same reason: drawn from the card's
    //  bottom edge without being booked, it landed on the swing slider.
    static constexpr int kSeqFootH = 14;
    //  Cuantas filas de mandos del paso se llevo la tira de la rejilla en la
    //  ultima maqueta. Lo apunta resized() y lo lee paint(): es lo que decide
    //  si la pagina del patron todavia tiene algo del paso que explicar.
    int seqTiraFilas = 0;
    //  SI LA FILA DE COMPAS LLEVA SU ROTULO. Se decide en resized() -es la
    //  unica que sabe cuanto alto queda- y la lee el propio maquetado en el
    //  sitio donde coloca la fila: dos cuentas para lo mismo es como una banda
    //  se reserva en un sitio y se dibuja en otro.
    bool seqBarsRotulo = true;
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
    //  Ver el .cpp: apunta lo que un texto OCUPA para que el banco lo vea,
    //  sin dibujarlo. pintaTitulo es apunta + drawText.
    void apunta (juce::Graphics& g, juce::Rectangle<int> caja,
                 const juce::String& texto, const char* tipo);
    void ponTransporte (bool on);
    void ponModoCancion (bool on);
    //  `apretar` a cero deja el `drawText` de siempre; por encima de cero se
    //  dibuja con `drawFittedText` a ese factor de apreton. Vive AQUI y no en
    //  quien llama porque habia CUATRO formas de pintar el titulo de una ficha
    //  y dos de ellas existian solo porque esta funcion no sabia apretar.
    void pintaTitulo (juce::Graphics& g, juce::Rectangle<int> caja, const juce::String& texto,
                      const char* tipo = "titulo", bool elipsis = false, float apretar = 0.0f);
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
    //  EL ANCHO DE UNA TARJETA Y EL DE SU INTERIOR, en un sitio.
    //
    //  El 0.92 estaba escrito SEIS veces -y cuatro de ellas partiendo de
    //  safeArea() en vez de del rectangulo con el que sheetFromBottom decide-,
    //  asi que la pregunta "cabe esto en la tarjeta" se contestaba con un ancho
    //  y se colocaba con otro. La ficha de AJUSTES lo pagaba: presupuestaba las
    //  pestanas con el ancho de la VENTANA y las colocaba con el de la tarjeta,
    //  o sea que reservaba una fila y usaba dos, y la fila de CARCASA se quedaba
    //  con 4 px de alto.
    static int anchoTarjeta (int anchoVentana) noexcept
    { return (int) ((float) anchoVentana * 0.92f); }
    static int anchoTarjetaInterior (int anchoVentana) noexcept
    { return anchoTarjeta (anchoVentana) - 2 * Metrics::lg; }

    //  Y EL TOPE DE ALTO, por lo mismo. Estaba escrito TRES veces —el propio
    //  `sheetFromBottom`, el reparto de columnas de AJUSTES y el menu de la
    //  ranura— y es la misma clase de duplicado que el `0.92` de arriba, que
    //  llego a estar en seis sitios. Girado no queda nada que proteger -la
    //  cara ya esta en dos columnas- y de pie el 0.78 deja ver la maquina
    //  detras, que es para lo que la tarjeta se centro.
    static int altoTarjeta (juce::Rectangle<int> zona) noexcept
    {
        return (int) ((float) zona.getHeight()
                        * (zona.getWidth() > zona.getHeight() ? 0.90f : 0.78f));
    }

    //  Lo que mide el recuadro de AUDIO, que es texto pintado y por tanto no
    //  lo dice ningun componente. Ver estAltoAudio: estaba escrito a mano en
    //  cuatro sitios.
    static constexpr int kAltoAudioInfo = 158;

    void padPorDefecto (int i);
    void newProject();
    //  LA CANCION DE UN PROYECTO RECIEN NACIDO. Ver la definicion.
    void songPorDefecto();
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
    //  GUARDAR EL BANCO DE DELANTE COMO KIT. Ver guardarKit: sale a la
    //  biblioteca con la forma de un banco descargado, para que vuelva a entrar
    //  por CARGAR KIT igual que cualquier pack de internet.
    juce::TextButton projKitButton  { "GUARDAR KIT" };
    //  LA VENTANA DE PISTAS de la rejilla de pasos. Su rotulo es el RANGO y no
    //  una palabra -"1-16", "1-8", "9-16"- por dos razones: dice el estado, que
    //  contando carriles no se deduce, y es el mismo en los cuatro idiomas, o
    //  sea que no puede quedarse sin traducir ni cortarse en chino.
    juce::TextButton seqPistasBtn { "1-16" };
    int  pistasVista = 0;               // 0 = las dieciseis, 1 = 1-8, 2 = 9-16
    static juce::File pistasPrefFile();

    //  EL METRONOMO Y LA CUENTA ATRAS SON DEL APARATO, NO DE UNA FICHA.
    //
    //  El clic existia y su tapa vivia SOLO en la vista de audio de CANCION, y
    //  la cuenta atras era `armaCuentaAtras (1)` escrito UNA vez en toda la
    //  app: un compas, clavado, sin opcion, y solo en el camino de grabar al
    //  arreglo. Grabar de normal -el microfono de la cara- no tenia ninguna de
    //  las dos, que es la mitad que faltaba: una toma que entra a ojo entra
    //  corrida, la grabes sobre el arreglo o sola.
    //
    //  Y son una PREFERENCIA de la persona, no del proyecto: cuantos compases
    //  te hacen falta para coger aire es tuyo y del momento, igual que el
    //  idioma, la carcasa y el master. Guardarlo en el proyecto significaria
    //  que abrirlo en otro sitio te trae la cuenta que dejaste una noche.
    //  El clic no lleva casilla propia: la tapa CLIC que ya existe ES la
    //  opcion, y lo unico que le faltaba era que grabar dejara de forzarla y
    //  que su estado se recordara. Dos sitios para una decision es lo que esta
    //  casa llama ruido.
    int  cuentaCompases = 1;               // 0, 1 o 2
    juce::OwnedArray<juce::TextButton> cuentaButtons;   // SIN / 1 / 2
    juce::Rectangle<int> cuentaRowArea;
    static juce::File cuentaPrefFile();
    void saveCuentaPref() const;
    void loadCuentaPref();
    //  Arma la cuenta y devuelve si de verdad hay que esperarla. Un solo sitio
    //  para los dos caminos de grabacion: con `armaCuentaAtras (1)` escrito en
    //  uno de los dos, el otro no podia tenerla sin copiar la regla.
    bool armaCuentaSiToca (int slot);
    void loadPistasPref();
    void savePistasPref() const;
    void aplicaPistas (int modo);
    void guardarKit (const juce::String& nombre);
    juce::TextButton projDeleteButton { "BORRAR" };
    juce::TextButton projExportButton { "EXPORTAR" };


    juce::String currentProject;

    //  EL RENGLON DE CONTINUIDAD, Y POR QUE LA FECHA SE CACHEA.
    //
    //  La banda de la cabecera decia el nombre del proyecto y nada mas. La
    //  feria pidio que dijera ademas cuanto trabajo hay dentro y cuando fue la
    //  ultima vez -«eso es lo que hace volver: no un premio, sino la sensacion
    //  de que hay algo empezado»- y de las tres cosas dos son gratis: el
    //  nombre ya esta aqui y los pads llenos se cuentan sobre `padHasSample`,
    //  que es el mismo bucle que la tira de zatis de dos lineas mas abajo.
    //
    //  La tercera NO es gratis: la fecha vive en el disco, y leerla desde
    //  `paint` es exactamente el fallo que ya se pago dos veces -el
    //  `createDirectory` de PROYECTOS y el fichero de un byte que
    //  `paintExportSheetContent` escribia y borraba en cada tic del rebote-.
    //  Se lee UNA vez, cuando el proyecto se abre o se guarda, y aqui queda.
    //  Invalida es «este trabajo no se ha guardado nunca», que es un estado
    //  real y no un hueco: la sesion vuelve entera igual.
    juce::Time proyectoFecha;

    //  Un solo sitio que escribe los dos, porque son un solo hecho: el
    //  proyecto que hay puesto y de cuando es. Estaban en cinco `currentProject
    //  = ...` sueltos y la fecha habria quedado vieja en el que se olvidara.
    void apuntaProyecto (const juce::String& name);

    //  Lo que la banda dice, compuesto donde se sabe cuanto sitio hay. Los tres
    //  campos caen POR ORDEN -primero el cuando, luego los pads- porque el
    //  nombre es el unico que no se puede deducir mirando la maquina.
    juce::String lineaDeContinuidad (int anchoDisponible,
                                     const juce::Font& fuente) const;

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
    //  ELEGIR DONDE CAE EL REBOTE.
    //
    //  Se reutiliza el navegador de CARGAR en vez de montar otro: es el mismo
    //  gesto -entrar en carpetas y senalar una- con la misma lista, la misma
    //  altura de fila y el mismo permiso de almacenamiento ya resuelto. Lo
    //  unico que cambia es QUE se acepta al final, asi que el navegador tiene
    //  un modo y no dos vidas.
    enum ModoBrowse { browsePad = 0, browseCarpeta };
    ModoBrowse browseModo = browsePad;
    juce::TextButton browseUseDirBtn { "USAR ESTA CARPETA" };
    juce::TextButton exportDirBtn { "CAMBIAR" };
    void openBrowseForExportDir();
    //  Deja el rebote donde cualquier gestor lo vea. Ver MediaStore.
    void publicarExport (const juce::File& carpeta);
    void usarCarpetaDeExport();

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
    double xrunGraceMs = 0.0; // gracia despues de abrir el dispositivo, en ms
    static constexpr int kXRunGraciaMs = 720;
    //  Cuanto tiene que aguantar limpio para que la cuenta vuelva a cero. Ver
    //  checkXRuns: sin esto «cuatro seguidos» eran cuatro EN TODA LA SESION.
    double xrunLimpioMs = 0.0;
    static constexpr int kXRunOlvidoMs = 5000;
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

    //  APAGAR EL MOVIMIENTO, Y LO QUE NO SE APAGA.
    //
    //  La cara late: la lampara de un efecto encendido respira a la mitad del
    //  tempo, los pads destellan, el osciloscopio y el analizador del EQ corren
    //  treinta veces por segundo. Todo eso esta acotado y medido y no habia
    //  forma de pararlo — y para alguien con sensibilidad vestibular o con
    //  epilepsia fotosensible «no hay forma de apagarlo» es «no hay forma de
    //  usarlo».
    //
    //  Y NO ES UN INTERRUPTOR DE «SIN ANIMACION», que es lo primero que sale y
    //  es peor que no tenerlo: un cabezal parado no es una app mas tranquila,
    //  es una app que ha dejado de decir por donde va el transporte, y un VU
    //  clavado ha dejado de ser un medidor. Lo que se apaga es lo que se mueve
    //  SOLO —respirar, destellar, el cristal, el analizador—; lo que mueve el
    //  transporte se queda. La banda de riesgo es la primera y no la segunda.
    //
    //  Y se apaga el REPINTADO, no la cuenta: la balistica de la aguja y la
    //  caida del destello siguen corriendo, igual que ya hacen debajo de una
    //  ficha. Pararlas dejaria una lampara congelada a media respiracion el dia
    //  que se vuelva a encender.
    //
    //  Es preferencia de la PERSONA y no del proyecto, como el idioma, la
    //  carcasa, el master y la cuenta atras.
    bool movimiento = true;
    juce::OwnedArray<juce::TextButton> movButtons;      // SI / NO
    juce::Rectangle<int> movRowArea;
    static juce::File movPrefFile();
    void saveMovPref() const;
    void loadMovPref();
    void ponMovimiento (bool on);
    juce::Rectangle<int> skinRowArea;
    juce::Rectangle<int> bufRowArea, rateRowArea;
    void useLowestLatency();
    void checkXRuns (double dtMs);
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

    //  LA CARA NO SE ENSEÑA HASTA QUE HA DEJADO DE MOVERSE.
    //
    //  Con targetSdk 36 la app va edge to edge y tiene que restarse las barras
    //  del sistema (SystemInsets), y en el CONSTRUCTOR no se pueden pedir: la
    //  vista no esta enganchada a una ventana todavia, asi que
    //  getRootWindowInsets devuelve nulo y salen ceros. El primer fotograma se
    //  maquetaba con la cara metida debajo del reloj y de la pastilla de
    //  gestos, y el ajuste llegaba en el primer o segundo latido -60 ms por
    //  tick- justo despues de que Android retirara su splash. Ese era el salto.
    //
    //  Mientras tanto se pinta la PORTADA -el chasis con la marca-, que es el
    //  mismo dibujo sobre el mismo fondo que el splash del sistema, asi que la
    //  entrega se lee como una sola pantalla.
    bool caraLista = false;
    //  Que los margenes ya se han preguntado con la ventana enganchada. No
    //  vale «los insets son distintos de cero»: en un aparato anterior a
    //  Android 15, o fuera de Android, valen cero para siempre y son
    //  correctos.
    bool insetsPreguntados = false;
    //  Y que la portada ha llegado a la pantalla ANTES de restaurar la sesion,
    //  que bloquea el hilo de mensajes casi un segundo -llenar un banco de
    //  instrumentos son 1238 ms medidos-. Sin esto se congelaria con la cara a
    //  medio hacer y sin nada dibujado encima.
    bool portadaPintada = false;
    //  LA MARCA DEL RELOJ, para contar milisegundos de verdad y no ticks
    //  nominales. Cero es «todavia no ha latido».
    double relojUltimoMs = 0.0;
    //  Ticks desde que la app abrio, para el tope de abajo y para ZATI_ARRANQUE.
    int arranqueTicks = 0;
    //  CUANTAS VECES SE HA PINTADO LA PORTADA DE VERDAD, y no es un adorno del
    //  volcado: la primera version del banco publicaba `caraLista`, o sea la
    //  BANDERA, y al quitar la portada a proposito siguio saliendo verde -la
    //  bandera seguia diciendo «tapada» con la cara entera a la vista-. Se
    //  cuenta dentro de pintaPortada, que es quien tapa.
    int portadaPintadas = 0;
    //  UN TOPE, y no es prudencia: si el aparato no contesta nunca a los
    //  margenes, la portada no puede quedarse puesta. Es la hermana de
    //  «ningun camino puede dejar la app en silencio».
    //
    //  Y EN MILISEGUNDOS, que estaba en TICKS. El comentario decia «treinta
    //  ticks a 60 ms son 1.8 s» y el tick es `DeviceTier::profile().
    //  relojMs`, que vale 33, 40, 60 o 100 segun el aparato: el plazo
    //  real iba de 1.0 s en un movil bueno a 3.0 s en uno de gama baja, o sea
    //  que el telefono que MAS tarda en arrancar era el que mas se quedaba
    //  mirando una portada. Es el mismo fallo que el temporizador del ducking
    //  ya tiene documentado y arreglado ocho mil lineas mas abajo -«contado en
    //  MILISEGUNDOS, no en ticks»- sin aplicar aqui.
    static constexpr int kPortadaTopeMs = 1800;
    //  Y el mismo plazo para dejar de preguntar por los margenes, por la misma
    //  razon y con el mismo numero: es una sola respuesta que llega tarde.
    static constexpr int kMargenesPlazoMs = 1800;
    //  En MILISEGUNDOS REALES desde que el reloj se separo del dibujo: un
    //  `+= relojMs` da por hecho que el tick duro exactamente su intervalo
    //  nominal, que es falso en cuanto el temporizador llega tarde.
    double portadaMs      = 0.0;
    double insetSettleMs  = 0.0;
    void pintaPortada (juce::Graphics& g);
    void miraSiLaCaraEstaLista();

    //  EL BANCO, porque en el escritorio esto no pasa: SystemInsets::get()
    //  devuelve {} siempre, o sea que el salto no existe aqui.
    //
    //  ZATI_INSETS=t,l,b,r  y  ZATI_INSETS_TICK=n  simulan la respuesta tardia
    //  de Android: hasta el tick n los margenes valen cero y a partir de ahi
    //  valen eso. Convierte en ENTRADA lo que si no seria nada, igual que
    //  ZATI_SKIN con la carcasa y ZATI_DLC con los packs.
    juce::BorderSize<int> margenesDeAhora() const;
    bool margenesContestan() const;
    const juce::String bancoInsets  = juce::SystemStats::getEnvironmentVariable ("ZATI_INSETS", {});
    const int bancoInsetsTick = juce::SystemStats::getEnvironmentVariable ("ZATI_INSETS_TICK", "0").getIntValue();
    //  ZATI_ARRANQUE=n deja correr n ticks e imprime una linea por tick. Aparte
    //  de ZATI_AUDIT a proposito: ese no puede llevar portada -las 924 corridas
    //  miden la cara y las fotos de ZATI_SHOT saldrian con el chasis vacio-.
    const int bancoArranque = juce::SystemStats::getEnvironmentVariable ("ZATI_ARRANQUE", "0").getIntValue();

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

    //  CON QUE ABRE LA MAQUINA. Ver auditNuevo: vuelca el proyecto tal y como
    //  nace -sonidos, cancion y envios- y otra vez despues de NUEVO, que es el
    //  otro camino por el que se llega a un proyecto vacio.
    void auditNuevo();
    //  CUANTO CUESTA EL PRIMER SONIDO, con la maquina recien instalada. Ver
    //  auditPrimerSonido y Tests/carga.py.
    void auditPrimerSonido();
    //  Ver Tests/kit.py: guarda el banco de delante como kit y vuelca lo que
    //  quedo EN DISCO. Se mide el resultado -los ficheros y su tamano- y no que
    //  la funcion no se queje, que es la diferencia entre comprobar y mirar
    //  para otro lado.
    void auditKit (const juce::String& nombre);
    //  GUARDAR UN PROYECTO Y VOLVER A ABRIRLO, que es lo que hace la persona
    //  y NO lo que medía la prueba de sesion. El fallo de los bancos altos
    //  clonados se arreglo en captureState/applyState y la prueba que lo
    //  cubria pasaba por el autoguardado de sesion, que es otro camino: si
    //  el arreglo se hubiera caido solo en el del proyecto, la prueba habria
    //  seguido en verde. Se mide el camino que se usa.
    void auditProject();
    //  Proyectos de otra epoca, congelados en Tests/proyectos.
    void auditViejos (const juce::String& carpeta);
    //  EL PIANO ROLL, medido. Ver auditPiano: escribe un acorde por la rejilla,
    //  cambia de pad con la ficha abierta y toca el teclado, que son las tres
    //  cosas que la ficha promete. Las tres tenian un fallo que una captura de
    //  pantalla no ve: el acorde vive en (patron, paso, pad) y hay que leerlo
    //  de ahi, el cambio de pad no repintaba la rejilla, y oir una tecla
    //  afinaba el pad para siempre.
    void auditClips();
    void auditPiano();
    //  LAS SEIS RANURAS DE LA FILA DE EFECTOS. Ver Tests/ranuras.py.
    void auditRanuras();
    void auditCanales();
    void auditRack();
    //  SOLO desde la cara y el modo visto en el lienzo. Ver Tests/modos.py.
    void auditModos();
    void auditEq();
    void auditAuto();
    void auditDinamica();
    //  LA CUENTA ATRAS Y EL METRONOMO. Ver Tests/cuenta.py.
    void auditCuenta();
    void auditBalistica();
    //  EL CATALOGO DE CONTENIDO Y EL CANDADO. Ver Tests/dlc.py.
    void auditDlc();
    void auditNiveles();
    void auditInstr();
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
    void refreshMidiDevices();
    void applyMidiChoice();

    SessionKeeper session;
    void restoreSession();
    //  Los 64 sonidos de fabrica. onlyBank < 0 = los cuatro bancos. Ver Kits.h.
    void loadFactoryKits (int onlyBank = -1);
    void cargaFabricaEnBanco (int origen, int destino);
    bool sessionRestorePending = true;   // done on the first timer tick
    bool startupBusy = true;             // la barra ya esta puesta al primer fotograma
    //  EN MILISEGUNDOS Y NO EN TICKS, que es la misma leccion que ya costo
    //  tres medidas en esta app -el ducking, la portada y la gracia de los
    //  under-runs- sin aplicar a estos dos. «Cada par de segundos» eran 33
    //  ticks, y un tick vale 33, 40, 60 o 100 ms segun el aparato: la sesion
    //  se sincronizaba cada 1.1 s en un movil bueno y cada 3.3 en uno de gama
    //  basica. Y desde que el reloj no es el que dibuja, un tick ya no dura ni
    //  siquiera lo que diga la tabla.
    double sessionSyncMs  = 0.0;
    double sessionStateMs = 0.0;

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
    double duckTicksLeft = 0.0;   // milliseconds remaining, not ticks
    double deviceRevivalTicks = 0.0;
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

    //  LAS CONSTANTES DE TIEMPO, EN MILISEGUNDOS Y NO POR CUADRO.
    //
    //  Las cinco de abajo estaban escritas como un factor por tick y
    //  documentadas contra treinta cuadros por segundo — `peak *= 0.72f`,
    //  `hold *= 0.985f`, `clipHold = 90` («~3 s a 30 cuadros»),
    //  `padFlash *= 0.8f` y `s += 0.25f * (dB - s)` —. Treinta cuadros por
    //  segundo es lo que tenia la gama ALTA: en la basica el mismo aviso de
    //  clip duraba nueve segundos y la aguja caia tres veces mas lento. Es la
    //  misma clase de fallo que este proyecto ya arreglo tres veces en el
    //  reloj —«ticks donde tenia que haber milisegundos»— sin aplicar nunca al
    //  lado visual, y con el vblank pasaria de cuatro velocidades a una por
    //  panel.
    //
    //  Los numeros no se inventan: son los de hoy resueltos a 33 ms, que es la
    //  cadencia contra la que se escribieron. `-33 / ln (0.72)` son 100 ms;
    //  `-33 / ln (0.985)`, 2183, y su propio comentario ya decia «unos dos
    //  segundos»; `-33 / ln (0.8)`, 148; `-33 / ln (0.75)`, 115.
    static constexpr double kTauAgujaMs      = 100.0;   // la aguja del medidor
    static constexpr double kTauRetencionMs  = 2200.0;  // el pico retenido
    static constexpr double kAvisoClipMs     = 3000.0;  // el aviso de recorte
    static constexpr double kTauDestelloMs   = 150.0;   // el destello de un pad
    static constexpr double kTauAnalizadorMs = 115.0;   // la caida del analizador

    //  Y las tres del reloj, que estaban contadas en ticks por la misma razon.
    static constexpr double kSyncSesionMs   = 2000.0;   // los pads al escritor
    static constexpr double kEstadoSesionMs = 20000.0;  // y el estado entero
    static constexpr double kConfirmMs      = 3000.0;   // un SEGURO? sin contestar
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
    //  LA BIBLIOTECA DE KITS TENIA SITIO Y NO TENIA PUERTA.
    //
    //  GUARDAR KIT escribe en ZATI/Kits y la app no volvia alli JAMAS: para
    //  usar en otro proyecto un kit que acababas de guardar habia que navegar
    //  hasta la carpeta a mano. Un sitio donde la maquina deja cosas y del que
    //  no sabe volver es medio funcion, como el troceado que volvia sin ser un
    //  troceado.
    juce::TextButton browseKitsDirButton { "MIS KITS" };
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
    //  Ver el .cpp: la zona que se enseña de un instrumento, y cuanto dura.
    bool zonaVisible (int pad, int& ini, int& fin) const;
    int  padVisibleLength (int pad) const;
    void assignSampleToPad (int index, SampleBuffer::Ptr sb, const juce::String& name = {});
    //  Los defectos de un pad, escritos UNA vez. Ver ponPadPorDefecto.
    void ponPadPorDefecto (int i);
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

    //  LOS CORTES QUE SE VEN SON LOS QUE SE APLICAN.
    //
    //  Antes applyAutoChop volvia a calcular los puntos -aritmetica o
    //  detector- en el momento de cortar, asi que la ficha no podia enseñar
    //  nada editable: cualquier marca que se moviera se habria perdido al
    //  pulsar CORTAR. Ahora esta lista es la unica fuente, la llena
    //  recalculaCortes cuando cambia el modo o el numero, y la persona la
    //  edita encima. Lo que ves es lo que sale.
    //  Lo que mide la vista previa del troceado. Aqui y no a mano en los dos
    //  sitios que la usan -quien PIDE el alto de la ficha y quien la COLOCA-,
    //  que es como una ficha acaba pidiendo una cosa y colocando otra.
    static constexpr int kChopVistaH = 96;

    std::vector<int> chopCortes;
    ChopPreview      chopVista;
    void recalculaCortes();
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
    double            confirmMs = 0.0;

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
    //  Y que la tapa DIGA que. Ver refrescaNombresDeshacer.
    void refrescaNombresDeshacer();

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
    //  Y los canales de la mesa, que son los del motor: escribirlos aqui otra
    //  vez son dos reglas, que es lo que ya costo `kNumFx`.
    static constexpr int kNumCanales   = AudioEngine::kNumCanales;   // 16

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
    //  Una fila, un trato: los iconos de una fila salen todos o ninguno.
    void filaDeIconos (juce::TextButton** fila, int n);
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

    //  DOS VISTAS DE LA MISMA LINEA DE TIEMPO: los cuatro carriles de patron y
    //  las cuatro pistas de audio. No son ocho carriles porque no caben - esta
    //  medido en Playlist: 20.2 px por carril en 280x653 y un clip se arrastra
    //  - y son la misma ficha porque son el mismo trabajo con dos vistas, que
    //  es la decision que ya tomo el secuenciador con PASOS, PIANO y PATRON.
    //  UNA TAPA Y NO DOS, medido: con dos pestanas en el renglon del titulo, en
    //  280x653 a cada una le tocaban 20 px de letra donde "AUDIO" pide 29 - y
    //  ponerlas en su propia fila cuesta 44 px que esta ficha no tiene, que es
    //  lo que dejo la celda de la linea de tiempo en 9 px. Con dos vistas basta
    //  un interruptor, y ademas es el idioma que la app ya usa: la tapa dice el
    //  ESTADO -PATRONES o AUDIO, con su dibujo- y no un verbo, igual que
    //  songModeBtn y que modoTapa. Una que dijera "IR A AUDIO" obliga a mirar si
    //  esta encendida para saber donde estas.
    juce::TextButton songVistaBtn { "PATRONES" };

    // ------------------------------------------------------------------
    //  LA AUTOMATIZACION. Ver AudioEngine::EventoAuto y Tests/auto.py.
    //
    //  La fila de efectos existe para TOCAR -esa es la mitad de por que las
    //  seis tapas son ranuras- y todo lo que se tocaba se perdia al exportar:
    //  el rebote salia con el numero que estuviera puesto al final. Lo que se
    //  pidio es exactamente eso, «automatizaciones guardadas cuando se graba».
    //
    //  Los eventos viven AQUI y no en el motor: el motor tiene la tabla que
    //  SUENA -inmutable y publicada de una vez- y editarla en su sitio seria
    //  una lectura rota a medio bloque. Es la misma reparticion que ya tienen
    //  los clips.

    //  GRABAR AL ARREGLO, y el metronomo al lado. Viven en la VISTA DE AUDIO y
    //  no en la cara: alli REC ya significa otra cosa -grabar pads en el
    //  patron- y dos verbos iguales con dos significados es lo que esta casa
    //  llama ruido.
    juce::TextButton songRecBtn  { "GRABAR" };
    juce::TextButton songClickBtn { "CLIC" };
    bool grabandoAlArreglo = false;
    int  pistaGrabacion = 0;
    void grabaAlArreglo();
    int songVista = Playlist::vistaPatrones;
    void showSongPage (int v);

    //  La tabla que la rejilla dibuja: los clips traducidos a COMPASES. Se
    //  rehace en refreshSong y vive aqui porque el componente la presta, no la
    //  copia - lo mismo que songCells.
    std::vector<Playlist::ClipVista> songClipsVista;
    void ponClip   (int pista, int compas);
    void mueveClip (int indice, int pista, int compas);
    void quitaClip (int indice);
    void largoClip (int indice, int desdeCompas, int hastaCompas);
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
    //  El canalon de la izquierda de cada fila. Era texto PINTADO -el nombre
    //  del efecto y su dibujo- y ahora es una tapa, porque el RACK pasa a ser
    //  el sitio donde se cambia lo que hay en una ranura: la fila del rack es
    //  una RANURA y no un efecto.
    juce::OwnedArray<juce::TextButton> rackSlotBtns;
    void refreshRack();
    juce::OwnedArray<juce::TextButton> mixMutes, mixSolos;
    juce::TextButton mixClearSolo { "SIN SOLO" };

    //  INSTRUMENTOS - el contenido descargable. Ver Instrumentos.h para el
    //  formato y para por que la fabrica es el primer pack.
    //
    //  UN INSTRUMENTO SON DIECISEIS PRESETS, o sea un banco entero, y esa es la
    //  unidad porque es como esta hecha la maquina: la rejilla ensena dieciseis
    //  pads y cambiar de banco los cambia los dieciseis a la vez. Los sesenta y
    //  cuatro de fabrica ya venian en cuatro grupos con nombre propio; lo que
    //  faltaba era donde ensenarlos y por donde meter mas.
    //
    //  EL PACK SE ELIGE CON MENOS Y MAS y no con una fila de pestanas: cuantos
    //  packs hay no lo sabe nadie hasta que se mira el disco, y una fila de
    //  tapas creadas al vuelo es exactamente lo que cerro la app la vez que la
    //  caja negra sirvio para algo. Es ademas el idioma que la app ya usa para
    //  lo mismo - PAD -/+ en el piano - y aguanta cualquier numero de packs.
    Sheet instSheet;
    juce::TextButton instCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::TextButton instPackDownBtn { "PACK -" }, instPackUpBtn { "PACK +" };
    juce::OwnedArray<juce::TextButton> instBtns;      // los instrumentos: lista o rejilla
    //  LA REJILLA DEL DESTINO: los dieciseis pads de un banco, cada uno con el
    //  dibujo de lo que lleva. Es un MAPA, no una lista: se elige DONDE va el
    //  instrumento tocando el sitio donde va a estar.
    juce::OwnedArray<juce::TextButton> instDestBtns;
    juce::OwnedArray<juce::TextButton> instBancoBtns;   // A B C D, para el destino
    int instDestPad  = AudioEngine::kNumBanks * AudioEngine::kPadsPerBank - 16;   // D01
    int instBancoDest = AudioEngine::kNumBanks - 1;
    std::vector<Instrumentos::Pack> instCatalogo;
    int instPack = 0;


    // ------------------------------------------------------------------------
    //  LA FICHA DEL INSTRUMENTO: una por pad, con forma de VST.
    //
    //  Los presets NO se eligen al cargar. Cargar un instrumento es elegir el
    //  INSTRUMENTO; elegir el sonido concreto es editar el pad, que es donde se
    //  edita todo lo demas de un pad. Mezclar las dos cosas obligaba a decidir
    //  el preset antes de haber oido ninguno.
    //
    //  Y ESTA FICHA NO SE PARECE A LAS OTRAS, a proposito: las demas son filas
    //  de mandos y esta es una cabecera con el dibujo de la familia, un teclado
    //  que se toca y la lista de los dieciseis. Es la forma que tiene un
    //  instrumento en cualquier aparato que los tenga, y es la que dice de un
    //  vistazo que este pad ya no es un golpe.
    Sheet vstSheet;
    //  LOS PRESETS SON UN INTERRUPTOR, no una lista.
    //
    //  Dieciseis filas de nombre son un menu, y esto no es un menu: es el mando
    //  de un instrumento. Con dos flechas se pasa de uno al siguiente OYENDOLO,
    //  que es como se elige un sonido de verdad, y la ficha pasa de 700 px de
    //  lista a un renglon - o sea que el teclado y la cabecera dejan de estar
    //  al final de un desplazamiento.
    juce::TextButton vstPreDown { "-" }, vstPreUp { "+" };
    juce::Rectangle<int> vstPreArea;
    juce::TextButton vstCloseButton { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::TextButton vstOctDown { "OCT -" }, vstOctUp { "OCT +" };
    //  Y LA PUERTA, en EL PAD y solo cuando el pad lleva instrumento.
    juce::TextButton vstButton { "PRESETS" };
    Teclado vstTeclado;
    int vstPad = 0;
    juce::Rectangle<int> vstTitleArea, vstIconArea, vstNombreArea, vstOctArea;
    //  LOS TRES PANELES DE LA FICHA, deducidos del maquetado y no maquetados:
    //  es la misma tecnica que agrupa la ficha del secuenciador, y por lo mismo
    //  no cuestan un pixel de alto. Ver paintVstSheetContent.
    juce::Rectangle<int> vstPanelCab, vstPanelPre, vstPanelTec;

    void abreVst();
    //  QUE PADS SE TOCAN COMO TECLAS y cual esta sonando por cual. Ver
    //  PadButton::setModoNota: un instrumento sostiene, asi que su nota tiene
    //  que soltarse, y quien la suelta es el dedo.
    void padNotaOn (int index, float vel);
    void padNotaOff (int index);
    void refreshModoNota();
    std::array<int, AudioEngine::kNumPads> notaViva {};
    void refreshVst();
    void paintVstSheetContent (juce::Graphics& g);
    //  Y SI ESTE PAD ES UN INSTRUMENTO, que lo preguntan cuatro sitios.
    bool padEsInstrumento (int i) const noexcept
    {
        return juce::isPositiveAndBelow (i, kNumPads)
            && uiSample[(size_t) i] != nullptr && uiSample[(size_t) i]->familia >= 0;
    }
    void repartePorBanco (const juce::Array<juce::File>& files, const juce::String& motivo);
    //  LA APP CON TRABAJO DENTRO, para el banco. Ver llenaDePrueba.
    void llenaDePrueba();
    void plantaPacksDePrueba();
    void openInstSheet();
    void refreshInst();
    void pasoPack (int d);
    void cargaInstrumento (int idx);
    //  Y la decision de rejilla: si el nombre no cabe en NINGUNA celda, ninguna
    //  lo lleva. Es filaDeIconos por el otro lado - alli se cae el dibujo y
    //  aqui la palabra - y por la misma razon: media rejilla con nombre y media
    //  sin el se lee peor que ninguna.
    void rejillaDeIconos (juce::OwnedArray<juce::TextButton>& celdas, int n);
    void eligePreset (int pre);

    //  EL BANCO DE LOS INSTRUMENTOS. Los otros tres son de percusion y este es
    //  el melodico -ya lo era: TONOS-, asi que el instrumento numero n vive
    //  siempre en el pad n de este banco. Que este clavado es la funcion: el
    //  07 esta donde la mano lo busca sin tener que acordarse de donde lo dejo.
    static constexpr int kBancoInstr = AudioEngine::kNumBanks - 1;   // D
    void ponInstrumentoEnPad (int pad, int familia, int preset);
    void paintInstSheetContent (juce::Graphics& g);

    //  EL MASTER, y vive en la mesa por la misma razon que los faders: es el
    //  fader que falta. Hasta aqui se podia bajar cada pad uno por uno y no
    //  habia forma de bajar la maquina entera, que es lo primero que se hace al
    //  enchufar unos cascos.
    //
    //  FUERA del Viewport: los dieciseis canales se desplazan y el master no.
    //  Un master que hay que buscar arrastrando no es un master.
    juce::Slider     masterFader;
    //  El rotulo es un Label y no texto pintado como los nombres de canal: lo
    //  pintado no existe para UiAudit, asi que un rotulo pintado no se mide ni
    //  se comprueba que este traducido. Si hay forma de que el banco lo vea, se
    //  usa esa.
    juce::Label      masterLabel;
    float            masterUserGain = 1.0f;
    //  Y se recuerda como se recuerda la carcasa y el idioma: en un fichero de
    //  la app, NO en el proyecto. Bajar el master para no despertar a nadie es
    //  una decision de la persona y del momento, no de la cancion - guardarlo
    //  en el proyecto significa que abrirlo en otro sitio te trae el volumen
    //  con el que lo dejaste una noche.
    static juce::String artistaPref();
    static juce::File masterPrefFile();
    void  loadMasterPref();
    void  saveMasterPref() const;
    void  guardaMasterSiHaceFalta();
    bool  masterPrefSucio = false;
    void refreshMixStrip();

    //  Which sixteen of the sixty-four the mixer is showing. Its own value, not
    //  the face's: you mix bank C while the grid in front of you plays bank A,
    //  and a mixer that jumped whenever the face changed bank would be a mixer
    //  you cannot leave open.
    int mixBank = 0;
    juce::OwnedArray<juce::TextButton> mixBankBtns;
    void showMixBank (int bank);

    //  LA MESA TIENE DOS PAGINAS: los pads y los CANALES.
    //
    //  Es el patron de la casa —`SetPage`, `SeqPage`, `PadPage`,
    //  `showSongPage`— y la fila de chips ya estaba puesta: los cuatro del
    //  banco son `Metrics::tab` con su grupo de radio, asi que la pagina no
    //  cuesta un pixel de mueble nuevo.
    //
    //  Y los dieciseis canales tienen las MISMAS cuatro cosas que un pad
    //  —color, nombre, fader y mute— menos SOLO, que se queda en el pad: acaba
    //  de entrar en la cara como modo hermano de REC y `effectiveGain` lo
    //  resuelve en una lectura, y dos ambitos de solo son dos respuestas a «oye
    //  solo esto». En su sitio va la cuenta de PADS que le entran, que es lo
    //  que un canal vacio necesita decir de si mismo.
    enum MixPage { mixPagePads = 0, mixPageCanales };
    MixPage mixPage = mixPagePads;
    //  UNA tapa y no dos pestanas, que es la leccion que la ficha de CANCION
    //  ya tenia medida: con dos vistas basta un INTERRUPTOR, y meterlas en la
    //  fila de chips de los bancos deja los cuatro en 29 px de ancho en
    //  280x653 -medido: 35 TOUCH nuevos en pantallas que ya estaban-. La tapa
    //  dice el ESTADO -PADS o CANALES-, como `songVistaBtn` y `modoTapa`.
    juce::TextButton mixVistaBtn { "PADS" };
    void showMixPage (MixPage p);
    juce::OwnedArray<juce::Slider>     canFaders;
    juce::OwnedArray<juce::TextButton> canMutes;
    //  Donde empieza cada tira, por lo mismo que `mixRowX`: el chip de color, el
    //  numero y la cuenta de pads se PINTAN, asi que el pintor necesita saber
    //  donde el maquetado los dejo.
    std::array<int, kNumCanales> canRowX {};

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
    //  De consulta, no de lectura: capitulos de cuatro o cinco lineas.
    //  El manual largo, con el porque de cada decision, es otra cosa y va
    //  fuera; esto es lo que se mira con una mano.
    //  LO MISMO QUE `Sheet::Cuerpo`, y por lo mismo: el manual trae su propia
    //  lista desplazable, asi que sus titulos de capitulo se apuntaban con las
    //  coordenadas del CUERPO -que va desplazado dentro de la ventana- y en la
    //  capa de quien hubiera pintado antes. Es exactamente el fallo que ya se
    //  pago con INSTRUMENTOS -pintar desde la tarjeta y maquetar desde el
    //  cuerpo- contado en el otro sitio de la app que se desplaza solo.
    struct ManualBody : public juce::Component
    {
        std::function<void (juce::Graphics&)> paintBody;
        int capa = 0;
        void paint (juce::Graphics& g) override
        {
            UiAudit::capaActual = capa;
            if (auto* top = getTopLevelComponent())
                UiAudit::origenPintado = top->getLocalPoint (this, juce::Point<int> (0, 0));
            if (paintBody) paintBody (g);
        }
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
        //  Y CUAL DE ELLOS ES UN INSTRUMENTO, por la MISMA razon y en el mismo
        //  sitio: un instrumento no es un WAV, es una receta - familia y preset
        //  -, y guardarlo como audio lo devolveria sonando parecido y sin
        //  zonas, o sea sin las otras cuatro octavas y sin las dos capas. Es la
        //  leccion del troceado contada con otra pieza: lo que no volvia no era
        //  el sonido, era lo que ese sonido ERA.
        //  -1 = no es un instrumento; si no, familia * 16 + preset.
        std::array<int, AudioEngine::kNumPads> inst;
        PadLoadJob() { source.fill (-1); inst.fill (-1); }
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

    //  EL TOUR DE BIENVENIDA, que no es el manual.
    //
    //  El manual sirve para consultar; esto sirve para
    //  la primera vez, que es un problema distinto: alguien que acaba de
    //  instalar la app no lee un manual entero, toca cosas. Cinco tarjetas, la
    //  ultima lleva al manual, y sale UNA vez - la marca vive en el mismo
    //  sitio que el idioma y la carcasa, porque es de la persona y no del
    //  proyecto, y tiene que poder leerse antes de que ProjectStore haya
    //  decidido donde esta la biblioteca.
    Sheet tourSheet;
    juce::TextButton tourNextBtn { "SIGUIENTE" }, tourBackBtn { "TOUR ATRAS" },
                     tourSkipBtn { "SALTAR" }, tourButton { "TOUR" };
    //  Si la pagina PATRON se quedo con la banda de los cuatro bloqueos. Lo
    //  decide la cuenta de la altura y lo usa la maqueta: preguntarlo dos
    //  veces con dos cuentas es como una fila se queda con altura cero.
    bool seqLocksAqui = false;
    //  Y si se quedo con la fila de la CADENA. Misma razon.
    bool seqCadenaAqui = true;
    //  Y NO ES UNA PILA DE TARJETAS, ES UN FOCO.
    //
    //  La primera version eran cinco tarjetas con texto, y explicaban la app
    //  sin ensenarla: quien las lee sigue sin saber DONDE esta lo que le acaban
    //  de contar. El tour del proyecto anterior -de donde sale esta app- hace lo
    //  contrario
    //  y es lo que lo hace util: oscurece la maquina entera menos el control
    //  del que habla, le pone un anillo, y va abriendo las fichas de verdad
    //  para explicarlas en vivo.
    //
    //  Y el texto va en un MUELLE FIJO, no en una tarjeta al lado de lo
    //  senalado. Aquel llego ahi despues de dos redisenos -"la barra ya no
    //  tapa la seleccion", "el mensaje nunca tapa la zona senalada"- porque una
    //  tarjeta colocada junto al objetivo acaba tapandolo en cuanto el objetivo
    //  es grande o esta en un borde. Junto al control solo va un NUMERO, y el
    //  muelle se pone en la mitad CONTRARIA a la del objetivo: es lo unico que
    //  garantiza que no lo tape sin negociar posiciones.
    int  tourPaso = 0;
    //  Lo que el paso senala, en coordenadas de MainComponent. Vacio = sin
    //  objetivo: ni anillo ni numero, y el muelle se centra.
    juce::Rectangle<int> tourFoco;
    juce::Rectangle<int> tourDock;
    void tourPrepara (int paso);              // abre la ficha que el paso explica
    juce::Rectangle<int> tourObjetivo (int paso) const;
    void showTour (int paso);
    //  EL ROTULO DE LA ULTIMA TAPA, en un solo sitio. Lo piden dos: showTour,
    //  que es quien cambia de paso, y retranslateUi, que solo quiere el texto
    //  en el idioma nuevo y NO puede llamar a showTour - ver el guardia de
    //  alli. Escrito dos veces, el dia que el tour tenga un paso mas la ultima
    //  tarjeta promete una siguiente que no existe desde uno de los dos lados.
    juce::String tourNextCaption() const;
    //  Y la tercera tapa, que en el paso de la puerta deja de decir SALTAR.
    juce::String tourSkipCaption() const;
    bool tourEsLaPuerta() const { return tourPaso == kTourBienvenida - 1; }
    int  tourBodyHeight (int ancho) const;
    //  LA LETRA DEL PARRAFO DEL TOUR, en un solo sitio. La escribian dos -quien
    //  mide el alto del muelle y quien lo pinta- y tienen que decir lo MISMO o
    //  el muelle se queda corto y la ultima linea cae fuera de la tarjeta.
    //
    //  Catorce y no once. Once es el cuerpo de un rotulo de control -dos o tres
    //  palabras que se leen de un vistazo- y esto es un parrafo que alguien lee
    //  entero, en un telefono, la primera vez que abre la app. Es el unico
    //  texto largo de la interfaz y estaba con la letra del mas corto.
    static juce::Font tourBodyFont();
    void paintTourSheetContent (juce::Graphics& g);
    static juce::File tourFile();
    void paintManualSheetContent (juce::Graphics& g);
    void paintManualBody (juce::Graphics& g);
    int  manualContentHeight (int width) const;
    //  Aqui vivia un `kManualChapters = 9` con este comentario encima: «se
    //  declara aqui porque lo necesitan el alto del contenido y el pintado, y
    //  esos dos TIENEN que contar lo mismo». Exactamente eso es lo que no
    //  pasaba: la tabla tenia diez filas, manualContentHeight las recorria
    //  todas y el pintado se paraba en nueve. Una cuenta escrita al lado de la
    //  tabla no es la tabla; las dos funciones la recorren ahora.
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
        static constexpr int kHoldMs = Metrics::holdMs;

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
    //  One row, six buttons, one effect each: FLT, HPF, DRV, DLY, BIT, REV.
    //  There used to be four re-assignable slots plus three bank chips above
    //  the knobs, which meant an effect could be pointed at, switched on and
    //  edited from three different places — the "there are two delays" bug.
    //  Now a button IS its effect: tapping it hands the three CTRL knobs its
    //  three parameters and tapping it again switches it off. Six effects,
    //  six switches, no modes.
    //  SIETE TIPOS Y SEIS RANURAS, y desde el EQ ya no son el mismo numero.
    //  `kNumFx` son los TIPOS -una fila de `fxDefs`, un bus del motor, una
    //  columna de envios por pad- y `kNumRanuras` cuantas tapas hay en la
    //  fila de la cara. Escribirlos con la misma constante funcionaba mientras
    //  coincidian y es lo que habria hecho que el septimo efecto trajera una
    //  septima tapa que no cabe: seis por cuarenta son 240 px y el Fold cerrado
    //  mide 225, o sea que la fila YA esta en su tope medido.
    //  Y SE TOMA DEL MOTOR, que no se escribe aqui otra vez. Estaba escrito en
    //  los dos sitios y coincidian por costumbre: al pasar a once con la
    //  familia de dinamica, el motor los tenia y la cara seguia en siete, o sea
    //  cuatro efectos que suenan y no aparecen en ninguna fila. Una regla
    //  escrita dos veces son dos reglas.
    static constexpr int kNumFx      = AudioEngine::kNumFx;
    static constexpr int kNumRanuras = 6;
    struct FxDef
    {
        const char* name;                  // face button
        const char* param[3];              // what CTRL 1-3 become
        struct Spec { double lo, hi, step, skewMid, def; int fmt; } spec[3];
        double onMix;                      // MIX applied when you switch it on
    };
    static const FxDef fxDefs[kNumFx];

    //  Que efecto trae su propia superficie, escrito UNA vez. Hoy es uno; el
    //  dia que sean cuatro esto es una tabla y no cuatro `if` repartidos por
    //  el maquetado, el pintor y el foco.
    static constexpr int kFxEq = 6;
    static bool fxTraeCara (int f) noexcept { return f == kFxEq; }

    //  Y CUAL DE LOS CUATRO DE DINAMICA ES, o -1. Los indices salen del motor
    //  y no se escriben aqui: son los mismos que decide `AudioEngine::kFxCmp`
    //  y siguientes, y copiarlos seria la misma regla en dos sitios -que es
    //  justo lo que acaba de costar `kNumFx`-. DSS no entra: un de-esser baja
    //  una banda y no el nivel, asi que su reduccion no se lee sobre el
    //  numero que el mando dice.
    static int dinamicaDeFx (int f) noexcept
    {
        return (f == AudioEngine::kFxCmp) ? 0
             : (f == AudioEngine::kFxGte) ? 1
             : (f == AudioEngine::kFxLim) ? 3 : -1;
    }

    std::array<bool, kNumFx> fxOn {};
    int focusedFx = 0;                     // whose parameters CTRL 1-3 hold

    //  LA FILA SON SEIS RANURAS, NO SEIS EFECTOS.
    //
    //  Hasta aqui el indice de un boton ERA su efecto: el boton 3 tocaba el
    //  DLY y no habia forma de que tocara otra cosa. Eso hace dos cosas mal a
    //  la vez: una maquina recien abierta enseña seis efectos que nadie ha
    //  puesto -y de los que no sabes cuales vas a usar- y el dia que haya mas
    //  de seis tipos no hay donde meterlos.
    //
    //  `slotFx[s]` dice QUE tipo vive en la ranura s, o -1 si esta vacia. Un
    //  tipo esta en UNA ranura como mucho: su estado en el motor -el filtro,
    //  la linea de retardo, la reverb- es uno solo, asi que dos ranuras del
    //  mismo tipo serian dos ventanas al mismo aparato. Esa es tambien la
    //  regla de la casa: una funcion, un dueño.
    //
    //  Lo que NO cambia es el motor: los seis buses siguen siendo uno por
    //  TIPO y todo lo que recibe un `fx` sigue recibiendo un tipo. Una ranura
    //  es donde se toca, no lo que suena. Por eso este cambio no toca una sola
    //  linea del hilo de audio.
    //
    //  Y LA FILA ES DEL CANAL, que es lo que arregla «cuando pasas de un pad a
    //  otro, los huecos de los efectos sigue igual». `slotFx[c][s]` es la fila
    //  del canal c; `canalActual` es el canal del pad elegido y lo pone
    //  `selectPad`. Los tres espejos -la cara, el canalon del rack y la fila del
    //  XY- siguen saliendo de `refrescaRanuras`, asi que ninguno sabe que
    //  existe un canal.
    //
    //  UN INSERTO ES DE UN CANAL Y UN ENVIO ES DE TODOS, que es la regla «un
    //  tipo, una ranura» generalizada y sale de `AudioEngine::sustituye`: de
    //  los veintiun tipos, dieciseis SUSTITUYEN -y su estado en el motor es uno
    //  solo, asi que dos canales con el mismo inserto serian dos ventanas al
    //  mismo aparato- y cinco SUMAN, que es lo que un envio significa y por eso
    //  cualquier canal puede mandarle.
    static constexpr int kSlotVacia = -1;
    std::array<std::array<int, kNumRanuras>, kNumCanales> slotFx {};
    int canalActual = 0;

    //  QUE HAY EN LA RANURA s DEL CANAL ACTUAL, que es lo que leen los tres
    //  espejos. Con `slotFx[canalActual][s]` escrito en los quince sitios que
    //  lo preguntan, el dia que el canal deje de ser el del pad elegido habria
    //  que tocar los quince.
    int enRanura (int s) const
    {
        return juce::isPositiveAndBelow (s, kNumRanuras)
                 ? slotFx[(size_t) juce::jlimit (0, kNumCanales - 1, canalActual)][(size_t) s]
                 : kSlotVacia;
    }

    //  Tipo -> ranura DENTRO DE UN CANAL, o -1. El que no lleva canal pregunta
    //  por el actual, que es lo que hace la cara.
    int  slotDeFx (int fx) const { return slotDeFxEn (canalActual, fx); }
    int  slotDeFxEn (int canal, int fx) const;
    //  Y EN QUE CANAL VIVE UN TIPO, o -1. Es la mitad que hace falta para «un
    //  inserto, un canal»: al ponerlo en otro hay que quitarlo de donde estaba.
    int  canalDeFx (int fx) const;
    bool fxEstaPuesto (int fx) const { return slotDeFx (fx) >= 0; }
    void ponEnRanura (int ranura, int fx); // fx = kSlotVacia para vaciarla
    void refrescaRanuras();                // rotulo, icono y luz de las seis

    //  Los dos gestos de una tapa de la fila, que se reparten por el ESTADO de
    //  la ranura y no por el gesto: sobre una ranura llena tocar enciende y
    //  mantener afina, que es lo que hacian desde siempre; sobre una VACIA los
    //  dos abren el menu, porque en un «+» no hay nada que encender ni nada
    //  que afinar y un gesto que no hace nada se lee como una tapa rota.
    void ranuraTocada (int ranura);
    void ranuraMantenida (int ranura);

    //  EL MENU DE UNA RANURA. Una capa encima de todo, como la rejilla de
    //  dieciseis pads del piano y por lo mismo: no vive dentro de la maqueta
    //  de nadie, asi que no cuesta un pixel de alto a la cara -que es la
    //  pantalla mas apretada de la app y la que no se puede evitar-.
    //
    //  En DOS columnas y no en una fila ni en cuatro: cada celda lleva el
    //  nombre del efecto y su dibujo, y eso ya esta medido en la rejilla de
    //  instrumentos - con cuatro columnas la celda cae a 82 px y el nombre no
    //  entra; con dos queda en 106 px hasta en la pantalla mas estrecha.
    //
    //  Y es un `Sheet` y no una clase nueva, asi que trae su capa para el
    //  banco -lo que le faltaba a XyPanel-, su velo y el cierre al tocar
    //  fuera.
    Sheet ranuraSheet;
    juce::TextButton ranuraCloseBtn { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::OwnedArray<juce::TextButton> ranuraBtns;     // un tipo por celda
    juce::TextButton ranuraVaciarBtn { "VACIAR" };
    int  ranuraEditada = -1;               // que ranura se esta eligiendo, o -1
    void abreMenuRanura (int ranura);      // -1 lo cierra
    void refrescaMenuRanura();
    void paintRanuraContent (juce::Graphics& g);
    juce::Rectangle<int> ranuraTituloBanda;

    juce::OwnedArray<juce::TextButton> fxButtons;

    std::vector<AudioEngine::EventoAuto> autoEventos;
    HoldButton autoBtn { "AUTO" };
    bool autoArmado = false;
    void anotaAutomacion (int fx, int par, float v);
    void publicaAutomacion();       // el espejo al motor
    void ponAutoArmado (bool on);
    void vaciaAutomacion();
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
    juce::Rectangle<int> bandaMandos() const;
    void macroMoved (int idx);

    // Skin cycler: four chassis TONES (TINTA/GRAFITO/ACERO/PLOMO), no hues.
    //  El juego de iconos de las tapas, en una tabla. Ver ponIconos().
    void ponIconos();
    //  El titulo de AJUSTES, con la pagina detras. Ver paintSetTitle.
    void paintSetTitle (juce::Graphics& g);
    //  La banda del rotulo PRUEBAS de la pagina de AUDIO.
    juce::Rectangle<int> pruebasLabelArea;
    //  Las dos bandas pintadas de INSTRUMENTOS, publicadas por resized().
    juce::Rectangle<int> instTitleArea, instPackArea, instPieArea;
    //  La carpeta de destino, resuelta al ABRIR la ficha y no en cada
    //  repintado: preguntarla escribe en disco. Ver paintExportSheetContent.
    juce::File destinoCache;
    //  Y la raiz de proyectos, por lo mismo: `ProjectStore::root()` hace
    //  `createDirectory()`, o sea un syscall por repintado de PROYECTOS.
    juce::File raizCache;
    //  Lo que el navegador tiene senalado, escrito por `selectionChanged`.
    juce::String browsePickName;

    //  EL CUERPO DE LA MAQUINA, HORNEADO. Degradado y grano en una imagen
    //  opaca que se rehace al cambiar de tamano o de carcasa; pintar el fondo
    //  pasa a ser una copia. Ver MainComponent::paint.
    void reconstruyeFondo();
    //  El fondo, en una funcion propia en cuanto tuvo dos clientes -paint y la
    //  portada-: dos caminos que pintan lo mismo por su cuenta se separan, que
    //  es la razon por la que normaliza salio de dentro de render.
    void pintaFondo (juce::Graphics& g);
    juce::Image fondoCache;
    int fondoSkin = -1;
    //  La corrida de control del banco: el degradado calculado en cada
    //  fotograma, que es el fondo de antes de hornearlo. Es lo unico que
    //  permite comparar las dos en la MISMA maquina.
    const bool fondoVivo = juce::SystemStats::getEnvironmentVariable ("ZATI_FONDO_VIVO", {}).isNotEmpty();

    //  LA MAQUINA SONANDO, que es el estado que este banco no ha medido nunca.
    //
    //  `Tests/cpu.py` cuenta fotogramas completos con la app abierta y QUIETA,
    //  y en un escritorio sin tarjeta de sonido no hay aparato: el motor no
    //  renderiza, el osciloscopio ve silencio y `SpectrumDisplay::setSamples`
    //  se rinde en su guardia de silencio. O sea que la pieza mas grande de la
    //  cara -su propio comentario la llama «el coste en reposo mas grande de la
    //  app»- no se repinta NUNCA en el banco, y todo lo que cuelga de que la
    //  maquina suene queda fuera de medida. En un telefono con algo sonando se
    //  repinta treinta veces por segundo, se vea o no.
    //
    //  Con esto el temporizador bombea los bloques que le tocan a su tick y el
    //  camino entero -motor, cola, osciloscopio, VU, destellos- corre de
    //  verdad. Es lo mismo que hacen `ZATI_SKIN` con la carcasa y `ZATI_DLC`
    //  con los packs: convertir en ENTRADA lo que si no seria «lo que hubiera».
    //  Solo cuando NO hay aparato: con uno de verdad el hilo de audio ya es el
    //  consumidor de la cola, y esa cola es de un solo consumidor por contrato.
    const bool bancoSonando = juce::SystemStats::getEnvironmentVariable ("ZATI_SONANDO", {}).isNotEmpty();
    juce::AudioBuffer<float> bancoBloque;
    void bombeaAudioDePrueba();
    void applySkin();

    //  Si hay una ficha (o el panel XY) delante de la maquina. Ver el cuerpo.
    bool caraTapada();

    //  Two of the transport keys carry a second gesture (see the GESTOS page):
    //  hold CARGAR to open the library, hold PLAY to cut everything.
    HoldButton loadButton { "LOAD" };
    juce::TextButton testButton { "TEST" };
    juce::TextButton recButton  { "REC" };

    //  SOLO DESDE LA CARA, que es el gesto mas repetido de una sesion y estaba
    //  en la mesa — a un toque, y la mesa TAPA la rejilla: aislar un sonido
    //  mientras tocas obligaba a cerrar, mirar y volver.
    //
    //  ES UN MODO Y NO UN GESTO, porque en un pad no queda ninguno: tocar
    //  dispara -al APOYAR, desde que se midio que el `onClick` le sumaba al
    //  golpe todo el tiempo del dedo- y mantener abre la ficha. Inventar un
    //  tercero es lo que esta casa lleva escrito que no se puede aprender.
    //
    //  Y hermano de REC, que es el precedente exacto de esta fila: una tapa
    //  armada que cambia lo que hace tocar un pad. Con las dos mitades de AUTO:
    //  TOCAR ARMA y MANTENER LIMPIA, porque vaciar los solos es lo unico de
    //  esta funcion que no se deshace tocando otra vez.
    HoldButton   soloButton { "SOLO" };
    bool soloArmado = false;
    void ponSoloArmado (bool on);
    //  De que color avisa la rejilla, o transparente si no hay modo armado.
    juce::Colour tinteDelModo() const;

    //  COMO SE LLAMA CADA MANDO. Ver tablaDeMandos: la misma palabra la dibuja
    //  el pintor y la lee TalkBack.
    struct Mando { juce::Slider* s; const char* clave; };
    std::vector<Mando> mandos;                 // se llena una vez, ver tablaDeMandos
    const std::vector<Mando>& tablaDeMandos();
    const char* claveDeMando (const juce::Slider& s);
    void refrescaRejillaModo();
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
    juce::TextButton previewButton { "OIR" };   // el triangulo es ahora un icono, ver oirTapa
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
    juce::Slider panSlider, anchoSlider, attackSlider, releaseSlider;
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

    //  LA CARA PROPIA DE UN EFECTO, y el EQ es la primera que la trae.
    //
    //  Los tres mandos son de TODOS los efectos, y hay efectos que no caben en
    //  tres numeros: un ecualizador de cinco bandas necesita diez. Asi que un
    //  efecto puede traer su propia superficie y ocupar el plato en vez de
    //  pedir prestados los mandos, que es lo que se pidio -«aprovechar el
    //  espacio que ocupan los knobs y todo el contorno»-.
    //
    //  LA CURVA SE LLEVA EL PLATO ENTERO, y eso es una medida y no una forma
    //  de hablar: en 280x653 a cada nodo le tocan 48 px -por encima del dedo-
    //  y en dos tercios de plato **31**, medido con el reparto puesto a
    //  proposito. Un nodo que no se puede agarrar es peor que un mando que hay
    //  que ir a buscar al XY. Por eso no hay reparto entre la
    //  curva y un mando superviviente: ANCHO y SALIDA se tocan desde el XY,
    //  que es la ficha que existe justo para mover dos numeros con un dedo, y
    //  MIX es el toque de la ranura.
    EqCurve eqCurva;
    //  Y LA MINIATURA DE LOS OTROS DIEZ, en el MISMO plato. Ver FxMini.h: la
    //  curva del EQ contesta «que hay dentro» para uno de los once tipos y los
    //  demas no contestaban nada — tres mandos dicen lo que le has PEDIDO al
    //  efecto y ninguno lo que esta haciendo. Vive donde ya vive esa respuesta.
    FxMini platoMini;
    //  El ESPEJO desde el que se pinta. No es el `Eq5` del motor: ese lo lee el
    //  hilo de audio y dibujarlo desde aqui seria leer sus coeficientes
    //  mientras los recalcula. Es la MISMA clase, asi que la curva que se
    //  dibuja sale de las mismas formulas que la que suena - que es justo lo
    //  que `respuestaEnDb` existe para garantizar.
    Eq5  eqEspejo;
    void ponBandaEq (int b, float hz, float dB);   // espejo + motor, un camino
    //  Y sus dos hermanas, por el MISMO camino y por la misma razon: el tipo
    //  de una banda y su Q los mueve la ficha de banda, y si escribieran solo
    //  el motor la curva seguiria dibujando la campana de antes.
    void ponTipoEq  (int b, int t);
    void ponQEq     (int b, float q);
    void refrescaEq();                              // el espejo desde el motor
    //  LA FICHA DE UNA BANDA, que se abre MANTENIENDO sobre su nodo. Es un
    //  `Sheet` y no un panel dibujado a mano: `XyPanel` se hizo asi y se quedo
    //  sin numero de CAPA, o sea que sus rotulos se comparaban contra la
    //  maquina entera y la regla del rotulo tapado no podia verlos. Un `Sheet`
    //  trae la capa, el velo y el cierre al tocar fuera de serie.
    Sheet eqBandaSheet;
    int   eqBandaSel = 0;
    juce::OwnedArray<juce::TextButton> eqTipoBtns;
    juce::Slider eqQKnob;
    juce::TextButton eqBandaCloseBtn { juce::CharPointer_UTF8 ("\xc3\x97") };
    juce::Rectangle<int> eqBandaTituloBanda;
    void abreBandaEq (int b);
    void refrescaBandaEq();
    void paintEqBandaContent (juce::Graphics&);
    void refrescaPlato();                           // curva o mandos, segun quien tenga el foco
    void refrescaVisorPlato();                      // la forma del efecto enfocado; dos dueños
    juce::Label  status;
    WaveformDisplay waveform;
    //  SE LLAMA CRISTAL Y NO ESPECTRO, porque no es un espectro: no lleva una
    //  sola FFT dentro. Dibuja la silueta de la onda del master sobre 0.74 s
    //  -ver SpectrumDisplay.h, que ya lo decia en su cabecera-. El NOMBRE
    //  mentia en los ocho sitios donde se usa, y quien lo leyera buscaria un
    //  analizador que no existe. Es el mismo fallo que la sombra pintada con
    //  ZatiColours::ink: un nombre que miente es un fallo, no un detalle.
    SpectrumDisplay cristal;
    ZatiLookAndFeel lnf;
    float scopeTmp[1024] {};
    //  Los dos anillos del analizador del EQ, del tamano de la FFT que hace la
    //  curva. Miembros y no locales: 8 KB de pila por tick en el hilo de
    //  mensajes es gratis hasta que un movil con la pila justa dice que no.
    float eqPreTmp[1024] {};
    float eqPostTmp[1024] {};

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
    //  ANCHO ESTEREO por pad: 0 mono, 1 como viene, 2 el doble de lado. Ver
    //  Voice::ancho. Se inicializa a uno en el constructor, que un array de
    //  floats deja ceros y eso seria la maquina entera en mono.
    std::array<float, kNumPads> padAnchoUI {};
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

    //  LOS CLIPS DE AUDIO DE LA LINEA DE TIEMPO.
    //
    //  UN CLIP REFERENCIA UN PAD, no un buffer suelto, y eso no es un atajo:
    //  es el modelo de esta app. Todo el audio entra por un pad -el micro, el
    //  remuestreo, un fichero, un troceado- y los pads YA se guardan en el
    //  proyecto con su WAV. Un clip que apuntara a un buffer sin pad obligaria
    //  a escribir ficheros nuevos, a numerarlos, a compartirlos entre clips que
    //  salen del mismo sitio -que es justo el fallo del troceado, N copias de
    //  lo mismo- y a limpiar los huerfanos. Apuntando al pad, todo eso ya esta
    //  resuelto y ademas el sonido sigue siendo tocable con el dedo.
    struct ClipUI
    {
        int   pad    = -1;      // de que pad sale el audio
        int   pista  = 0;       // 0..kAudioTracks-1
        int   compas = 0;       // donde empieza en la cancion
        int   desde  = 0;       // primera muestra que suena
        int   largo  = 0;       // cuantas
        float gain   = 1.0f;
    };
    std::vector<ClipUI> clips;

    //  Traduce los clips a la tabla del motor resolviendo pad -> buffer, y la
    //  publica. Se llama desde el hilo de mensajes y en ningun otro sitio.
    void publicaClips();

    std::array<float, kNumPads> padFlash {};   // 1.0 on trigger, decays -> lit feedback
    // Chassis layout regions (set in resized(), drawn in paint()).
    juce::Rectangle<int> headerArea, screenBezel, tabBarArea,
                         editInfoArea, audioInfoArea,
                         padPlateArea, ctrlPlateArea;
    float vuL = 0.0f, vuR = 0.0f;   // smoothed output peaks for the VU strip
    //  Y el del canal del pad elegido, que vive en la misma banda del cristal.
    //  Ver SpectrumDisplay::setCanal y AudioEngine::miraCanal.
    float vuCanal = 0.0f;
    bool  vuHeld = false;           // solo el banco: ZATI_VU congela la tira

    int  selectedPad   = -1;
    bool loadArmed     = false;
    bool recordingActive = false;
    int  recordingSlot = -1;
    int  lastPlayStep  = -1;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

#pragma once

#include <JuceHeader.h>
#include <vector>
#include "AudioEngine.h"
#include "FxPresets.h"
#include "SampleLoader.h"
#include "WaveformDisplay.h"
#include "SpectrumDisplay.h"
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include "ProjectStore.h"
#include "StepGrid.h"
#include "BarraVista.h"
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
#include "Sintes.h"
#include "MidiArchivo.h"

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
    //  LA CADENCIA EN LA QUE SE ESTA, que es distinta de la que se querria.
    //
    //  El salto se decidia con `floor (coste / periodo)` y sin memoria, o sea
    //  recalculado desde cero en cada cuadro. Con un coste que ronda el periodo
    //  -que es el caso NORMAL en un telefono, porque para eso se elige el
    //  presupuesto- ese `floor` cae a un lado y a otro con la media movil y la
    //  app alterna entre pintar todos los cuadros y pintar uno de cada dos:
    //  16.7 ms, 33.3, 16.7, 33.3. La media sale bien -«50 cuadros por segundo»-
    //  y lo que se ve es un tiron por cada cambio.
    //
    //  Asi que el nivel se RECUERDA y solo se mueve de uno en uno, con banda
    //  muerta: se sube pasado el 130 % del presupuesto y se baja por debajo del
    //  70 %, que deja un 60 % de holgura entre las dos decisiones y es lo que
    //  impide que un cuadro caro suelto cambie la cadencia.
    int    saltoNivel    = 0;
    double saltoCambioMs = 0.0;
    //  El periodo del panel, suavizado. Ver enVBlank: con el instantaneo el
    //  suelo de la cadencia aleteaba con el jitter del propio aviso de vblank.
    double periodoPanel  = 0.0;
    //  El periodo con el que se resolvio el suelo de cadencia, y el suelo. Ver
    //  enVBlank: recalcularlo por cuadro deja que el temblor del aviso cruce un
    //  umbral entero y cambie la cadencia sin que haya cambiado nada.
    double periodoNivel    = 0.0;
    int    nivelMinVigente = 0;
    //  Medio segundo, y sale de lo que el ojo distingue: por debajo de ahi dos
    //  cambios seguidos se leen como un tiron y no como la app acomodandose.
    static constexpr double kCadenciaEsperaMs = 500.0;
    //  El techo de dibujo. Ver enVBlank: por encima de esto no se gana nada que
    //  se vea y se paga el doble de CPU en el mismo hilo que tiene que dejar
    //  respirar al de audio.
    static constexpr int kDibujoTopeHz = 60;
    //  Y el suelo por debajo del cual el techo se afloja: ver enVBlank.
    static constexpr int kDibujoSueloHz = 50;
    //  ZATI_LASTRE=ms — ver lastreDeBanco.
    void lastreDeBanco();
    double       lastreMs      = 0.0;
    unsigned int lastreSemilla = 12345u;

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

        //  EL NOMBRE DE LA FICHA, SIN TRADUCIR. Lo escribe la caja negra
        //  (`openSheet`) y por eso NO puede ser el rotulo de la tapa que la
        //  abre, que es lo que hacia: un parte de un telefono en chino decia
        //  «ficha 混音» y no se puede buscar en el fuente. Y ademas el rotulo
        //  no identifica la ficha - el RACK se abre pasando `mixButton`, asi
        //  que escribia «ficha MEZCLA» igual que la mesa.
        //
        //  Son los mismos nombres que `ZATI_OPEN`, y eso no es una casualidad
        //  comoda: un parte que dice «ficha rack» se reproduce en el banco con
        //  `ZATI_OPEN=rack`. Una lista escrita dos veces son dos listas.
        const char* nombre = "?";

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

        //  Y EL MISMO RECTANGULO QUE RECIBIO EL MAQUETADO, para quien PINTA.
        //
        //  `sheetFromBottom` devuelve `cuerpo.getLocalBounds()` cuando la ficha
        //  se desplaza y `sheetBounds.reduced (margenes)` cuando no, y los
        //  dibujantes de contenido repetian la segunda mitad a mano. Mientras
        //  no hubo ninguna ficha desplazable con rotulo pintado las dos cuentas
        //  dieron lo mismo; en cuanto EXPORTAR paso a desplazarse, el rotulo se
        //  pintaba en coordenadas de ventana y sus tapas vivian en el cuerpo,
        //  que empieza en (0,0): el banco lo canto como `CABECERA 20` en las
        //  cinco pantallas de pie -«EXPORTAR ocupa 244..260 y su fila de tapas
        //  252..292»- y el desvio era distinto en cada una, que es la firma de
        //  dos origenes y no de un margen mal puesto.
        //
        //  Dos tablas que dicen lo mismo son dos reglas. Esta es la unica.
        juce::Rectangle<int> areaContenido() const
        {
            return desplazable ? cuerpo.getLocalBounds()
                               : sheetBounds.reduced (Metrics::margenFichaX, Metrics::margenFichaY);
        }

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
    juce::TextButton padPickCloseBtn { juce::CharPointer_UTF8 (Metrics::cruz) };
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
    juce::TextButton canalCloseBtn { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::OwnedArray<juce::TextButton> canalBtns;
    //  LOS DOS CHIPS DE BANCO, que es lo que treinta y dos canales obligan a
    //  tener: la rejilla enseña dieciseis y el resto sigue estando. El indice
    //  es UNO para los dos selectores -el de aqui y el del RACK- porque el
    //  canal elegido tambien es uno: con un banco por selector, abrir el rack
    //  despues de mover un pad al canal 20 enseñaria la rejilla del 1 al 16 con
    //  el elegido fuera de ella.
    juce::OwnedArray<juce::TextButton> canalBankBtns;
    //  La celda de SALIR de la mesa. Aparte de las treinta y dos a proposito:
    //  no es un canal numero cero, es no estar en ninguno. Ver
    //  AudioEngine::kSinCanal.
    juce::TextButton canalNingunoBtn { "SIN CANAL" };
    juce::TextButton padCanalBtn { "CANAL" };
    int  canalBanco = 0;
    void ponCanalBanco (int b);
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
    void pianoCellToggled (int paso, int semi, bool arrastrando = false);

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
    //  CORTE es COPIAR y BORRAR seguidos, y se escribe asi -llamando a las dos-
    //  y no repitiendo el bucle: dos caminos que hacen lo mismo por su cuenta
    //  se separan, que es lo que ya costo sacar `repartePorBanco` de dentro.
    void pianoCortaSel();
    void pianoBorraSel();

    //  EL PORTAPAPELES DE NOTAS, RELATIVO a la esquina de arriba a la
    //  izquierda de lo copiado: pegar tiene que caer donde toques y no donde se
    //  copio. Y CON EL LARGO, que es la leccion de copiarFila contada en el
    //  piano - una copia que se deja el largo ha dejado de ser la misma figura.
    struct NotaPeg { int dPaso; int semi; int cuartos; };
    std::vector<NotaPeg> pianoPortapapeles;

    //  LA TIRA DE ACCIONES DE LA SELECCION. Ver el reparto en
    //  MainComponent_Layout.cpp: solo existe con notas seleccionadas.
    juce::TextButton pianoSelBtn { "SEL" }, pianoCopiaBtn { "COPIAR" }, pianoPegaBtn { "PEGAR" },
                     pianoCorteSelBtn { "CORTE" }, pianoBorraSelBtn { "BORRAR" };

    //  CUANTAS COLUMNAS SE VEN. Ocho es medio compas con celdas del doble de
    //  ancho -para escribir en 1/32-, dieciseis es el compas de siempre y
    //  treinta y dos son dos compases para ver la frase. Con el suelo de la
    //  celda decidiendo, como todo lo demas.
    int pianoCols = StepGrid::kBarSteps;
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

            //  Y EN SUS COORDENADAS, que es la otra mitad y la que faltaba.
            //  Este panel pinta en LAS SUYAS -esta en 14,6- y el volcado va en
            //  las de la ventana, asi que su titulo y su renglon de ayuda se
            //  apuntaban catorce pixeles a la izquierda y seis mas arriba de
            //  donde de verdad estan: el banco llevaba desde el primer dia
            //  midiendo esta cabecera en el sitio equivocado. Es exactamente
            //  el fallo de INSTRUMENTOS -pintar desde la tarjeta y maquetar
            //  desde el cuerpo- y el de `ManualBody`, en el tercer y ultimo
            //  sitio de la app que pinta fuera de la cara.
            const auto antes = UiAudit::origenPintado;
            UiAudit::origenPintado = getPosition();
            if (paintContent) paintContent (g);
            UiAudit::origenPintado = antes;
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
    //  OCHO Y NO SEIS: entraron MANTEN SOLO y MANTEN AUTO, que llevaban dos
    //  tandas existiendo sin fila en la unica pagina que los enumera. Ver
    //  paintGesturesPage, y la regla de `Tests/plano.py` que exige que cada
    //  HoldButton de la cara tenga la suya.
    //
    //  Y NUEVE desde que el canalon del rack abre los presets del efecto que
    //  lleva: es la puerta que se pidio -«el tema de los presets no esta muy
    //  accesible ni legible»- y es un gesto sin marca en la cara, que es la
    //  unica clase de gesto que esta pagina tiene que enumerar.
    static constexpr int kNumGestures = 11;
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
    //  El paso entero lo define AudioEngine::Paso: esta lista estuvo escrita
    //  aqui y era la unica completa de la app, asi que COPIAR PATRON,
    //  DESPLAZAR y DOBLAR tenian cada uno la suya, mas corta.
    std::array<AudioEngine::Paso, AudioEngine::kNumSteps> filaPortapapeles {};
    bool filaCopiada = false;
    void copiarFila();
    void pegarFila();

    //  ------------------------------------------------------------------
    //  LA BANDA DE LA REJILLA DE PASOS: pads x pasos.
    //
    //  Esta rejilla era la unica de las tres que no tenia seleccion de ninguna
    //  clase: el piano marca notas con SEL y la linea de tiempo marca tramos,
    //  y aqui lo mas fino que habia era COPIAR FILA -un pad entero, los 192
    //  pasos- y COPIAR PATRON -los 64 pads-. Llevarse los dos primeros compases
    //  del bombo y la caja a otro patron pedia copiar el BANCO entero y borrar
    //  a mano los catorce pads que sobraban.
    //
    //  Es el MISMO modelo que las otras dos y no uno nuevo: SEL arma, el
    //  arrastre marca, un toque suelto vacia, y la tira de cuatro aparece con
    //  la banda. Un solo gesto de seleccion en las tres pantallas.
    juce::TextButton seqSelBtn { "SEL" };
    //  LA TIRA DE ACCIONES DE LA BANDA. Ver el reparto en
    //  MainComponent_Layout.cpp: solo existe con banda puesta, y PEGAR ademas
    //  pide portapapeles - pegar lo que no se ha copiado no es nada.
    juce::TextButton seqCopiaBtn { "COPIAR" }, seqCorteSelBtn { "CORTE" },
                     seqPegaBtn { "PEGAR" }, seqBorraSelBtn { "BORRAR" };
    //  La banda en las coordenadas de la rejilla: `pad0/pad1` son CARRILES del
    //  banco que se ve -0..15- y `paso0/paso1` son CASILLAS DE LA VISTA, que
    //  con la rejilla en 1/8 sobre un patron de 1/16 no son pasos guardados.
    //  La traduccion la hace `pasoDeCelda`, en un solo sitio.
    StepGrid::Sel seqSel;

    //  EL PORTAPAPELES DE PASOS, y con los NUEVE campos de `AudioEngine::Paso`.
    //
    //  Es la leccion que este proyecto ya pago dos veces y esta escrita en la
    //  cabecera de `AudioEngine::Paso`: COPIAR PATRON se llevaba TRES de los
    //  nueve y DESPLAZAR y DOBLAR, cuatro, y la queja llego con las dos
    //  mitades - «la velocidad no se copia» y «de un acorde de tres notas solo
    //  se pega una». Por eso aqui no se toca `setStep`, que solo pone el
    //  on/off: se lee con `leePaso` y se escribe con `escribePaso`, que son los
    //  dos unicos que conocen la lista entera.
    //
    //  En coordenadas RELATIVAS a la esquina de la banda -como el piano y como
    //  la cancion- porque pegar tiene que caer donde miras y no donde se copio.
    struct PasoPeg { int dPaso; int dPad; AudioEngine::Paso paso; };
    std::vector<PasoPeg> seqPortapapeles;
    //  Y EL TAMANO DEL HUECO, que el vector no puede dar: solo lleva los pasos
    //  que SUENAN, asi que una banda de dieciseis pasos con dos golpes tiene
    //  dos entradas. Sin estas tres cifras PEGAR borraria el destino hasta el
    //  ultimo golpe copiado y dejaria vivo lo que hubiera detras - la figura
    //  nueva mezclada con la cola de la vieja, que es el mismo fallo que
    //  `escribePaso` vino a cerrar una capa mas abajo.
    int seqPegPad0 = 0;     // el carril en el que empezaba la banda copiada
    int seqPegPads = 0;     // cuantos carriles medía
    int seqPegPasos = 0;    // y cuantos PASOS GUARDADOS de ancho
    void seqBanda (int pad0, int paso0, int pad1, int paso1);
    void seqVaciaSel();
    void seqCopiaSel();
    void seqPegaSel();
    //  CORTE es COPIAR y BORRAR seguidos, y se escribe asi -llamando a las
    //  dos- y no repitiendo el bucle: dos caminos que hacen el mismo trabajo
    //  por su cuenta acaban separandose, y el sintoma es «cortar y copiar no
    //  pegan igual» sin poder decir por que. Es la cuarta vez que esta casa lo
    //  escribe: piano, cancion y aqui.
    void seqCortaSel();
    void seqBorraSel();
    //  VACIAR UN RECTANGULO DE PASOS, en PASOS GUARDADOS. La usan BORRAR,
    //  CORTE y el carvado de PEGAR, que es la misma reparticion que
    //  `vaciaBanda` tiene en la cancion.
    void vaciaBandaPasos (int lane0, int lane1, int st0, int st1);

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
    //  Y LA PAGINA DE SONIDO, QUE ERA LA UNICA DE LAS TRES CON SU PEDIDO A
    //  MANO. `wantH` decia `438 + secH + 2 * panelAireY`, con el desglose solo
    //  en un comentario: «116 + secH + 86 + 86 + 86 + 56 + 8». Medido en
    //  412x915, la pagina PIDE 500 px y COLOCA 490, y los diez que sobran se
    //  quedan dentro del panel de abajo -entre NORMALIZAR, que acaba en 627, y
    //  la banda de CHOKE, que empieza en 639-, que es el agujero que se ve en
    //  la foto del telefono.
    //
    //  Los diez son dos errores de suma que el comentario tapaba: el `+ 8` es
    //  el `Metrics::sm` de debajo de las pestañas, que YA estaba contado dentro
    //  de los 116, y `panelAireY` se pedia dos veces donde el maquetado reserva
    //  uno solo (`inner.removeFromBottom (Metrics::panelAireY)`).
    //
    //  Es el mismo fallo que ya se pago en EL PAD -499 pedidos para 365 de
    //  contenido- y se arregla igual: la peticion y el maquetado preguntan a la
    //  MISMA funcion. Una regla escrita dos veces son dos reglas.
    int  altoContenidoPadSonido (int ancho) const;
    //  LO QUE PIDE LA CELDA DE CHOKE, escrito UNA vez.
    //
    //  Estaba escrito en la condicion que decide si CHOKE se va solo a su fila,
    //  y no en la que le da el ancho: cuando se iba solo, la celda se quedaba
    //  `r3` ENTERA -lo que sobraba- y la casilla del deslizador salia de 347 px
    //  en 412x915, contra los 107 de las otras nueve casillas de la ficha. Un
    //  control no mide lo que sobra: mide lo que pide.
    //
    //  Los 34 son el suelo de la casilla -dice tambien "off", y en arabe مغلق
    //  pide 28 px de letra- y los 12, el aire que el rotulo necesita para no
    //  pegarse a la tecla.
    static constexpr int chokeCeldaPide = 12 + 34 + Metrics::gap + 2 * Metrics::stepKey;
    bool padPuertasWraps (int rowWidth) const;
    //  Y LA DE LA MUESTRA: REV, BUCLE, QUITAR RUIDO y RECORTAR.
    //  Cuatro tapas con dos rotulos largos no caben en un movil
    //  estrecho, y la pregunta se hace en DOS sitios -al pedir el
    //  alto de la pagina y al colocar la fila- que tienen que
    //  contestar lo mismo.
    bool padMuestraWraps (int rowWidth) const;
    //  Y LA DE CHOKE, QUE NO ES UN SI/NO SINO UN NUMERO: cuantas de las dos
    //  tapas caben en su fila. Ver padChokeAcompanan y chokeCeldaPide.
    int  padChokeAcompanan (int rowWidth) const;
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
    //  `tinte` invalido -el defecto- deja el filo NEUTRO, que es lo que
    //  llevan las nueve fichas que agrupan controles. Con un color puesto el
    //  filo lo toma del PAD, que es la unica ficha donde eso significa algo:
    //  ver paintVstSheetContent.
    //  Y `nombre` es el del array que se le pasa: el volcado apunta
    //  `padGrupos[2]` en vez de un rectangulo anonimo, que es lo que separa un
    //  fallo que se va a arreglar de uno que hay que buscar primero. Ver
    //  UiAudit::panel.
    void pintaPaneles (juce::Graphics& g, const juce::Array<juce::Rectangle<int>>& grupos,
                       const char* nombre = nullptr, juce::Colour tinte = {}) const;

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
    //  Y los del PIANO. Era la unica de las tres paginas de la ficha del
    //  secuenciador sin un solo panel -PASOS lleva tres y PATRON cinco- asi
    //  que sus dos filas de tapas flotaban sobre el mismo fondo que la
    //  rejilla. Como rectangulos y no como bandas de rotulo (`SeqLabel`):
    //  una banda con nombre cuesta su alto, y esta pagina pide 672 px donde la
    //  tarjeta da 663. Ver `padGrupos`, que nacio por lo mismo.
    //  Y LOS DE EXPORTAR y el TROCEADO, que eran dos de las fichas que se
    //  quedaron sin agrupar: la referencia que llego del telefono es la
    //  ficha EL PAD con sus paneles, y estas dos son las que mas se leen
    //  como una columna de renglones sueltos sobre el mismo fondo.
    juce::Array<juce::Rectangle<int>> padGrupos, setGrupos, songGrupos, pianoGrupos,
                                      exportGrupos, chopGrupos;
    //  ...and the line at the foot of the PASO page that names the step being
    //  edited. Reserved by resized() for the same reason: drawn from the card's
    //  bottom edge without being booked, it landed on the swing slider.
    static constexpr int kSeqFootH = 14;
    //  Cuantas filas de mandos del paso se llevo la tira de la rejilla en la
    //  ultima maqueta. Lo apunta resized() y lo lee paint(): es lo que decide
    //  si la pagina del patron todavia tiene algo del paso que explicar.
    int seqTiraFilas = 0;
    //  SI LA BARRA DE LA VENTANA SE COLOCA. Se decide en resized() -es la
    //  unica que sabe cuanto alto queda y cuantas columnas caben- y la lee el
    //  propio maquetado donde la coloca: dos cuentas para lo mismo es como una
    //  banda se reserva en un sitio y se dibuja en otro.
    bool seqHayBarra = false;
    //  Y si la del TONO cabe, que es lo que decide si el par de tapas de
    //  OCTAVA se queda: cada mando en un sitio en cada pantalla.
    bool pianoHayBarraVert = false;
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
    //  De donde venias al abrir esta ficha, para la profundidad. Se llama
    //  ANTES de cerrar nada; ver su definicion.
    void apuntaApertura (Sheet& s);
    void closeAllSheets();
    void paintAudioSheetContent (juce::Graphics& g);
    void paintSeqSheetContent (juce::Graphics& g);
    //  Ver el .cpp: apunta lo que un texto OCUPA para que el banco lo vea,
    //  sin dibujarlo. pintaTitulo es apunta + drawText.
    //  `minimo` es el ancho por debajo del cual ese texto deja de leerse
    //  entero, en fraccion de lo que pide: 1.0 para un `drawText` -por debajo
    //  se corta-, el factor de apreton para un `drawFittedText`, y CERO para lo
    //  que se elide a proposito, que es «no lo juzgues». Ver UiAudit::Rotulo.
    //  Y DEVUELVEN EL RECTANGULO QUE EL TEXTO OCUPA, que es lo que hacia falta
    //  para que un rotulo se pueda apartar de OTRO: `antesDe` ya sabe hacerlo
    //  con un rectangulo, y hasta ahora solo se le podian dar tapas. En CANCION
    //  el titulo y la ayuda se apartaban cada uno de las dos tapas del renglon
    //  y NINGUNO del otro - 112 hallazgos, 46x16 px de solape.
    juce::Rectangle<int> apunta (juce::Graphics& g, juce::Rectangle<int> caja,
                                 const juce::String& texto, const char* tipo,
                                 float minimo = 1.0f, int lineas = 1);
    void ponTransporte (bool on);
    void ponModoCancion (bool on);
    //  `apretar` a cero deja el `drawText` de siempre; por encima de cero se
    //  dibuja con `drawFittedText` a ese factor de apreton. Vive AQUI y no en
    //  quien llama porque habia CUATRO formas de pintar el titulo de una ficha
    //  y dos de ellas existian solo porque esta funcion no sabia apretar.
    //  UN RENGLON DE AYUDA NO SE DIBUJA A MEDIAS.
    //
    //  Los quince renglones de ayuda de esta app se pintaban con `drawText` o
    //  con `drawFittedText` sobre la banda que hubiera, o sea que donde no
    //  cabian salian CORTADOS: «toca el teclado para oir, la rejilla para
    //  escribir · OCTAVA C-1 - C» pedia 316 px con 41 en 280x653 —se veia un
    //  octavo de frase— y el de EXPORTAR 273 con 181.
    //
    //  El orden es el que esta casa ya tiene escrito para una tapa: entero,
    //  apretado, y solo entonces fuera. *Cambiar un apreton por un corte no es
    //  un arreglo*, y media frase de ayuda se lee como un fallo mientras que
    //  ninguna se lee como una pantalla limpia — el manual lo dice todo entero.
    //  Devuelve si se dibujo, que es lo que hace falta cuando debajo hay algo
    //  que ocupa su sitio.
    //  Y DE CUANTAS LINEAS. Los pies de INSTRUMENTOS y de la ficha del
    //  instrumento se dibujaban con `drawFittedText` a pelo -dos y tres
    //  renglones- asi que no pasaban por aqui: sin `apunta`, los dos eran
    //  invisibles para CORTADO, TAPADO y PISADO, que es la misma falta que ya
    //  costo que la mesa estuviera titulada «MIX» a mano durante meses.
    bool pintaAyuda (juce::Graphics& g, juce::Rectangle<int> banda,
                     const juce::String& texto, juce::Justification justif,
                     float apreton = Metrics::apretonAyuda, int lineas = 1);

    juce::Rectangle<int> pintaTitulo (juce::Graphics& g, juce::Rectangle<int> caja,
                                      const juce::String& texto, const char* tipo = "titulo",
                                      bool elipsis = false, float apretar = 0.0f);
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
    { return anchoTarjeta (anchoVentana) - 2 * Metrics::margenFichaX; }

    //  Y EL TOPE DE ALTO, por lo mismo. Estaba escrito TRES veces —el propio
    //  `sheetFromBottom`, el reparto de columnas de AJUSTES y el menu de la
    //  ranura— y es la misma clase de duplicado que el `0.92` de arriba, que
    //  llego a estar en seis sitios.
    //
    //  Y DE PIE DEJA DE SER UN PORCENTAJE: SE DERIVA.
    //
    //  El 78 % existia por una razon medida -que por debajo de la tarjeta se
    //  sigan viendo los pads- y desde la tanda de `onFuera` esos pads ademas se
    //  PUEDEN TOCAR: `Sheet::onFuera` los dispara y los selecciona. O sea que
    //  la condicion no es «tres cuartos de pantalla», es «que asome un pad
    //  entero» — un dedo de pad, `Metrics::hit`, mas el renglon de estado que
    //  va debajo. Y eso es un numero que sale de la maqueta y no una fraccion
    //  elegida a ojo.
    //
    //  Medido: en 412x915 el tope pasa de 713 px a 803 -0.878, noventa pixeles
    //  que se lleva la rejilla que la ficha muestre- y en 280x653 de 509 a 541.
    //  Con el 0.90 clavado en 412x915 quedarian 29 px de pad asomando y
    //  `tocaPadDetras` dejaria de poder acertarse: se cambiaria una funcion
    //  medida por unos pixeles.
    //
    //  Girado no se toca: alli la cara ya esta en dos columnas y no hay pad que
    //  proteger debajo, asi que el tope es el sitio que hay.
    static int altoTarjeta (juce::Rectangle<int> zona) noexcept
    {
        if (zona.getWidth() > zona.getHeight())
            return (int) ((float) zona.getHeight() * 0.90f);

        return juce::jmax (zona.getHeight() / 2,
                           zona.getHeight() - 2 * (ZatiLookAndFeel::kStatus + Metrics::hit));
    }

    //  Y SI LA TARJETA ES MAS ANCHA QUE ALTA, que NO es la misma pregunta
    //  que `wideFace`.
    //
    //  `wideFace` pide ademas unos 556 px de area segura -es «la CARA cabe en
    //  dos columnas»- y una ficha no es la cara: lo unico que decide si su
    //  contenido se puede partir en dos es la forma del rectangulo en el que
    //  va a caber. En 412x480 -la pantalla partida- `wideFace` es falso y la
    //  tarjeta mide 379x368, o sea mas ancha que alta, y las fichas la
    //  maquetaban en UNA columna: CANCION pedia 490 px sobre 368 y
    //  `sheetFromBottom` recortaba en silencio, que es como la celda de la
    //  linea de tiempo acabo en 6 px.
    //
    //  Se pregunta con los MISMOS dos numeros con los que se va a dibujar la
    //  tarjeta -`anchoTarjeta` y `altoTarjeta`- y no con los de la ventana:
    //  preguntar con una cuenta y colocar con otra es de donde sale la mitad
    //  de los comentarios de este fichero.
    static bool tarjetaAncha (juce::Rectangle<int> zona) noexcept
    { return anchoTarjeta (zona.getWidth()) > altoTarjeta (zona); }

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
    juce::TextButton setCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
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

    //  EL ZOOM DE ANCHO DE LA REJILLA DE PASOS. Su rotulo es la PROPORCION
    //  —«1:1», «2:1», «3:4»— y no una palabra, por lo mismo que el de arriba:
    //  dice el estado y es el mismo en los cuatro idiomas. La celda es CUADRADA
    //  de arranque, que es lo que se pidio, y este multiplicador es lo unico
    //  que la separa de serlo.
    juce::TextButton seqZoomBtn { "1:1" };
    void aplicaZoomPasos (float z);

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

    //  Y LOS CUATRO CHIPS DEL BANCO DE TOMAS, en la misma pagina y por la misma
    //  razon. Ver `padParaToma` y `bancoTomas`.
    juce::OwnedArray<juce::TextButton> tomasButtons;    // A B C D

    //  MONITOR: oirte por los cascos mientras grabas. Ver
    //  `AudioEngine::setMonitor` y `RutaAudio::porAltavoz`.
    //
    //  Vive aqui y no en la fila FUENTE del pad por lo mismo que la cuenta
    //  atras: es una preferencia de la PERSONA y de su aparato -si llevas
    //  cascos o no-, no del proyecto ni de la toma. Y ademas esa fila ya se
    //  parte en dos en 280x653 con las tres palabras que tiene.
    juce::OwnedArray<juce::TextButton> monButtons;      // OFF / ON
    juce::Rectangle<int> monRowArea;
    bool monitorOn = false;
    static juce::File monitorPrefFile();
    void saveMonitorPref();
    void loadMonitorPref();
    //  Escribe la ganancia que toca AHORA: cero si la persona lo tiene apagado
    //  y cero tambien si la salida es el altavoz, que es la guarda. Se llama al
    //  tocar el chip y cada vez que se arma una toma, porque los cascos se
    //  enchufan y se quitan en mitad de una sesion.
    void aplicaMonitor (bool avisa);
    juce::Rectangle<int> cuentaRowArea;
    juce::Rectangle<int> tomasRowArea;
    static juce::File cuentaPrefFile();
    void saveCuentaPref() const;
    void loadCuentaPref();

    //  Fichero propio y no un numero mas en el de la cuenta, por lo mismo que
    //  el del monitor: ese se llama `zati-cuenta.txt` y meterle dentro algo que
    //  no es la cuenta es como un nombre deja de ser verdad.
    static juce::File tomasPrefFile();
    void saveTomasPref() const;
    void loadTomasPref();
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
    juce::TextButton exportCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::TextButton exportMasterButton { "MASTER" };
    juce::TextButton exportStemsButton  { "PISTAS" };
    juce::TextButton exportCancelButton { "CANCELAR" };
    std::unique_ptr<Exporter> exportJob;

    //  EL REBOTE EN VIVO: la cancion suena y lo que suena se escribe.
    //
    //  Se pidio «opcion de exportar en Live, con un count in para no perder el
    //  tiempo». MASTER y PISTAS son los dos OFFLINE -un motor clonado fuera de
    //  tiempo real- y REMUESTREAR es un rebote en vivo pero a un PAD.
    //
    //  EN SU PROPIA FILA y no como cuarta tapa al lado de MASTER y PISTAS: esas
    //  dos son dos PRODUCTOS del mismo rebote -el mismo render con las pistas
    //  aparte- y esto es otro MODO. Ademas de que la fila se reparte por el
    //  texto y una cuarta palabra le quita ancho a las tres que ya estan
    //  medidas.
    juce::TextButton exportLiveButton { "EN VIVO" };
    //  COMPARTIR comparte fila con EN VIVO y solo aparece cuando hay un
    //  `content://` que mandar. Fila propia no cabia: la ficha ya pide
    //  Metrics::hit*4 + btn*2 y en 915x412 la tarjeta da 370 px para 432
    //  pedidos — lo que falta se lo come lo ultimo que se maqueta.
    juce::TextButton exportShareBtn { "COMPARTIR" };
    std::unique_ptr<RebotVivo> vivoJob;
    juce::File vivoFichero;
    void alternaRebotVivo();
    void terminaRebotVivo();
    //  WAV o comprimido. Ver Exporter: un master de tres minutos pasa de 30 MB
    //  a 3, que es lo que separa "lo tengo" de "te lo mando".
    //  ELEGIR DONDE CAE EL REBOTE.
    //
    //  Se reutiliza el navegador de CARGAR en vez de montar otro: es el mismo
    //  gesto -entrar en carpetas y senalar una- con la misma lista, la misma
    //  altura de fila y el mismo permiso de almacenamiento ya resuelto. Lo
    //  unico que cambia es QUE se acepta al final, asi que el navegador tiene
    //  un modo y no dos vidas.
    //  Y UN TERCER MODO: buscar un .mid para meterlo en el piano roll. Mismo
    //  gesto, misma lista, mismo permiso ya resuelto — lo unico que cambia es
    //  que se acepta al final y QUE se enseña, que lo decide `FiltroBrowse`.
    enum ModoBrowse { browsePad = 0, browseCarpeta, browseMidi };
    ModoBrowse browseModo = browsePad;
    juce::TextButton browseUseDirBtn { "USAR ESTA CARPETA" };
    //  Y la unica accion del modo MIDI: traerse el fichero senalado.
    juce::TextButton browseMidiBtn { "IMPORTAR" };
    juce::TextButton exportDirBtn { "CAMBIAR" };
    //  LAS DOS CARPETAS QUE SE PIDIERON DESPUES, y la que las tres comparten.
    //
    //  «Molaria poder elegir cual es la carpeta predeterminada para apertura y
    //  guardar proyectos, abrir y guardar samples». El rebote ya se podia
    //  dirigir desde la primera tanda que lo pidio; estas dos son las mismas
    //  tres preguntas —donde empieza el navegador, que se escribe al aceptar, a
    //  que ficha se vuelve— asi que van por el MISMO gesto: ver
    //  `openBrowseForFolder` y `ProjectStore::Carpeta`.
    juce::TextButton projDirBtn    { "PROYECTOS" };
    juce::TextButton samplesDirBtn { "SONIDOS" };
    //  Cual de las tres se esta eligiendo. El navegador es uno solo, asi que
    //  sin esto «usar esta carpeta» tendria que adivinar de donde vino — que es
    //  como estaba y por eso el destino estaba escrito dentro de la funcion.
    ProjectStore::Carpeta carpetaQueSeElige = ProjectStore::Carpeta::exports;
    void openBrowseForFolder (ProjectStore::Carpeta que);
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
    //  Y EL BUFFER TAMBIEN BAJA, que es la mitad que faltaba.
    //
    //  `burstMult` solo subia. Subia por cuatro chasquidos, se escribia en
    //  `buffer.txt` y de ahi no se movia nunca mas: ni en esa sesion ni en
    //  ninguna de las siguientes, porque el arranque lee el fichero. O sea que
    //  UN mal rato -otra app comiendose el telefono, una llamada, el sistema
    //  indexando- dejaba la app en 4 bursts PARA SIEMPRE. Con un burst de 256 a
    //  48 kHz eso es pasar de 5.3 ms de buffer a 21.3, y de ~16 ms de salida a
    //  ~64: cuatro veces la latencia, en la app cuyo argumento entero es la
    //  latencia, por un chasquido de hace tres semanas.
    //
    //  Asi que un tramo limpio LARGO baja un burst y vuelve a probar. Cuarenta
    //  y cinco segundos y no cinco: cinco es lo que hace falta para olvidar una
    //  cuenta de chasquidos, y bajar el buffer reabre el stream -lo que corta
    //  el sonido un instante-, asi que hacerlo cada cinco segundos seria peor
    //  que el problema.
    static constexpr int kXRunBajaMs = 45000;
    //  Y SI AL BAJAR VUELVE A CREPITAR, ese nivel queda descartado en esta
    //  sesion. Sin esto la app oscilaria entre dos buffers para siempre en el
    //  telefono justo, que es exactamente el caso para el que existe todo esto.
    int    burstSuelo   = 1;
    double burstBajoMs  = -1.0;   // ms desde la ultima bajada; < 0 = ninguna
    //  ZATI_XRUN / ZATI_XRUN_ESCALA — ver checkXRuns.
    juce::String xrunGuion;
    double       xrunEscala  = 1.0;
    double       xrunRelojMs = 0.0;
    void   keepChosenRate();
    juce::String exportStatus;
    bool         exportOk = false;
    //  Lo que hace falta para MANDAR el rebote, capturado en `publicarExport`:
    //  el `content://` del primer fichero publicado y su MIME. Sin esto el
    //  rebote quedaba visible en Music/ZATI y punto — habia que salir a un
    //  gestor de ficheros para compartirlo, que es el unico camino que la app
    //  no tenia. El selector de Android manda un fichero por vez, asi que en
    //  PISTAS se queda con el master, que es el primero de la lista.
    juce::String exportUri, exportMime;
    void startExport (bool stems);
    void pollExport();
    //  ABRIR la ficha EN LIMPIO, que son cinco renglones y estaban copiados en
    //  dos sitios -el boton de AJUSTES y el paso 13 del tour-. Con COMPARTIR
    //  pasaban a ser siete, y el que se olvidara uno dejaria la tapa ofreciendo
    //  mandar el rebote ANTERIOR: una funcion, un dueño. Volver del navegador
    //  de carpetas NO pasa por aqui a proposito: ahi la ficha se reabre con lo
    //  que ya decia, que es el mismo gesto contestando otra pregunta.
    void openExportSheet();
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

    //  ZATI_MUERE=n: MORIRSE COMO SE MUERE EN ANDROID, que es lo que un
    //  escritorio no sabe hacer solo.
    //
    //  Alli el boton ATRAS manda la tarea al fondo -corre `appSuspended`- y lo
    //  que mata el proceso despues es un SIGKILL, que no se puede capturar: o
    //  sea que `shutdown()` NO corre. Aqui pasa lo contrario: cualquier salida
    //  pasa por `shutdown()`, asi que la caja negra siempre acaba con «fin
    //  limpio» y el parte falso que esto existe para medir no se puede
    //  reproducir. Con la entrada, la app llama a `appSuspended` y se va con
    //  `_Exit` -sin destructores y sin `shutdown()`-, que es exactamente la
    //  secuencia del telefono.
    //
    //  Y con ZATI_SENAL=n en vez de eso se levanta esa senal, que es la OTRA
    //  mitad: un parte de caida de verdad lleva prefijo -«CAIDA senal 11 en
    //  ...»- y sin las dos, un mecanismo que no avisa nunca pasa la primera
    //  comprobacion sola y el de ayer -que avisaba siempre- pasaba la segunda.
    const int bancoMuere = juce::SystemStats::getEnvironmentVariable ("ZATI_MUERE", "0").getIntValue();
    const int bancoSenal = juce::SystemStats::getEnvironmentVariable ("ZATI_SENAL", "0").getIntValue();

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
    //  El pico del espectro del cristal, para el banco. Ver SpectrumDisplay.
    float auditPicoEspectro() const { return cristal.picoEspectro(); }
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
    //  LAS DOS SELECCIONES DE RANGO, en una sola sonda. Ver Tests/sel.py.
    //
    //  UNA y no dos porque son la MISMA funcion en dos lienzos -banda, cuatro
    //  acciones, portapapeles relativo- y arrancar el binario dos veces para
    //  medir dos mitades del mismo contrato cuesta el doble y mide lo mismo.
    //  Lo que no se junta es lo que cada una guarda: el paso lleva NUEVE
    //  campos y el bloque lleva `offset`, y esas dos son las cifras que
    //  ninguna captura de pantalla puede dar.
    void auditSelecciones();
    void auditPiano();
    //  LAS SEIS RANURAS DE LA FILA DE EFECTOS. Ver Tests/ranuras.py.
    void auditRanuras();
    void auditCanales();
    void auditRack();
    //  LOS PRESETS DE CADA EFECTO. Ver Tests/presets.py.
    void auditFxPresets();
    //  SOLO desde la cara y el modo visto en el lienzo. Ver Tests/modos.py.
    void auditModos();
    void auditEq();
    void auditAuto();
    void auditDinamica();
    //  LA CUENTA ATRAS Y EL METRONOMO. Ver Tests/cuenta.py.
    void auditCuenta();
    //  EL BANCO DE TOMAS: donde cae lo que se graba. Ver Tests/tomas.py.
    void auditTomas();
    void auditMidi();
    void auditBalistica();
    //  EL CATALOGO DE CONTENIDO Y EL CANDADO. Ver Tests/dlc.py.
    void auditDlc();
    void auditNiveles();
    void auditInstr();
    //  LA EXPORTACION, medida de verdad y no mirando la barra. Ver auditExport:
    //  monta un patron con los sonidos de fabrica y hace el rebote entero -
    //  master y pistas - en el hilo que llama, contando ficheros y bytes.
    void auditExport();
    //  EL REBOTE EN VIVO, que es el tercer modo. Ver Tests/export.py.
    void auditVivo();
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
    //  CUANTO DURA EL «A SALVO» de la banda de continuidad. Ver
    //  `lineaDeContinuidad`: es un estado binario y no un contador, asi que
    //  esta cifra es lo unico que hay que elegir. Treinta segundos son quince
    //  veces `kSyncSesionMs`: con el escritor vivo el campo no se apaga nunca, y
    //  si se apaga es que de verdad hace medio minuto que no se escribe nada —
    //  que es exactamente cuando la persona tiene que enterarse.
    static constexpr juce::int64 kASalvoMs = 30000;
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

    //  EL FILTRO PREGUNTA POR EL MODO, y por eso no es un `WildcardFileFilter`
    //  a secas: la lista tiene que enseñar muestras cuando se busca un sonido y
    //  ficheros .mid cuando se busca una melodia. Con un comodin fijo habria
    //  dos reglas -lo que se lista y lo que se acepta- y la de listar diria que
    //  la carpeta esta vacia con el fichero delante.
    struct FiltroBrowse : public juce::FileFilter
    {
        explicit FiltroBrowse (const ModoBrowse& m)
            : juce::FileFilter ("Muestras de audio"), modo (m) {}

        bool isFileSuitable (const juce::File& f) const override
        {
            const auto ext = f.getFileExtension().toLowerCase();
            return modo == browseMidi ? (ext == ".mid" || ext == ".midi")
                                      : juce::String (".wav .aiff .aif .flac .ogg .mp3")
                                            .containsWholeWord (ext);
        }
        bool isDirectorySuitable (const juce::File&) const override { return true; }

        const ModoBrowse& modo;
    };
    std::unique_ptr<FiltroBrowse>   browseFilter;   // declared first: outlives the browser
    std::unique_ptr<juce::FileBrowserComponent> browser;
    //  LO QUE DICE UNA CARPETA VACIA, que es lo unico que el navegador no
    //  decia. Dos tercios de la ficha en negro y ni una linea se leen como que
    //  la app no cargo, y la respuesta —«aqui no hay nada, entra en otra»— es
    //  justo la que hace falta ahi. AJUSTES · PROYECTOS ya lo hace en el mismo
    //  estado («sin proyectos · GUARDAR crea el primero»): la figura existia en
    //  esta casa y faltaba en la ficha de al lado.
    //
    //  Es un rotulo ENCIMA del navegador y no texto pintado por la ficha, por
    //  dos razones: lo que pinta `paintBrowseSheetContent` queda DEBAJO del
    //  navegador -es un hijo suyo- y ademas un componente lo ve el banco.
    juce::Label browseVacio;
    void refrescaBrowseVacio();
    juce::TextButton browseCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
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
    void stepCellToggled (int pad, int step, bool arrastrando = false);
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
    juce::TextButton chopCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) },
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
    //  EL MISMO VISOR QUE EL RECORTE, en modo marcas. Era un `ChopPreview`
    //  propio de 201 lineas sin zoom, sin pellizco, sin arrastre de la vista y
    //  sin audicion — o sea el mismo trabajo hecho dos veces y una de las dos a
    //  medias. Ver `WaveformDisplay::Modo`.
    WaveformDisplay  chopVista;
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


    //  Rehace el rango del mando de LARGO con el compas de la rejilla de
    //  ahora: un compas son cuatro pulsos y cuantos pasos sean depende de
    //  cuanto dura un paso. Ver el cuerpo.
    void reajustaMandoLargo();
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
    std::array<std::array<AudioEngine::Paso, AudioEngine::kNumPads>,
               AudioEngine::kNumSteps> patClip {};
    int patClipLen = 16;
    void copyPattern();
    void pastePattern();
    juce::TextButton undoButton { "DESHACER" };
    juce::TextButton redoButton { "REHACER" };
    void rebuildChain();
    int  firstEmptyPad() const;

    //  EL DESTINO DE UNA TOMA, que no es «el primero libre».
    //
    //  Lo era, y en una maquina de fabrica eso es -1 SIEMPRE: los sesenta y
    //  cuatro pads vienen llenos (`Tests/carga.py` mide 64 con sonido en una
    //  instalacion limpia), asi que la caida de al lado -«y si no hay, el pad
    //  elegido»- se comia el pad 01 en cada toma, justo debajo del comentario
    //  que prometia que una toma nueva no pisa lo que la persona haya puesto.
    //  Y peor: un clip apunta al PAD, asi que la segunda toma reescribia el
    //  audio de la primera y el clip ya puesto en la linea de tiempo pasaba a
    //  sonar otra cosa. Dos tomas al arreglo no se podian hacer.
    //
    //  Ahora una toma cae dentro del BANCO DE TOMAS y solo ahi, del 01 hacia
    //  arriba para que queden en orden y se lean como una lista. Ocupado es
    //  «tiene sonido y NO es de fabrica»: sin esa mitad una maquina recien
    //  instalada diria «lleno» a la primera, y sin la otra la segunda toma se
    //  comeria la primera. Si no queda ninguno devuelve -1 y quien llama lo
    //  dice y no arranca la toma - nada se pierde nunca.
    int  padParaToma() const;
    void layoutPadGrid (juce::Rectangle<int> area, int cols, int rows, int gap);

    static constexpr int kNumPads      = AudioEngine::kNumPads;      // 64
    static constexpr int kPadsPerBank  = AudioEngine::kPadsPerBank;  // 16 on screen
    static constexpr int kNumBanks     = AudioEngine::kNumBanks;     // A B C D
    //  Y los canales de la mesa, que son los del motor: escribirlos aqui otra
    //  vez son dos reglas, que es lo que ya costo `kNumFx`.
    static constexpr int kNumCanales   = AudioEngine::kNumCanales;   // 32

    //  Y CUANTOS ENSEÑA UN SELECTOR DE CANAL. Los dos que hay -el de EL PAD y
    //  el del RACK- son la MISMA rejilla de cuatro por cuatro que la cara, asi
    //  que con treinta y dos canales hacen falta dos bancos y una fila de dos
    //  chips, exactamente como los pads tienen A B C D. Dieciseis en fila ya
    //  estaba medido y no cabe -26 px en 280- y treinta y dos menos.
    //
    //  Derivado y no escrito: el dia que `kNumCanales` vuelva a subir, los dos
    //  selectores crecen solos y nadie tiene que acordarse de una segunda
    //  constante. Es lo mismo que hace el menu de una ranura con `kNumFx`.
    static constexpr int kCanalesPorBanco = 16;
    static constexpr int kNumCanalBancos  = kNumCanales / kCanalesPorBanco;
    static_assert (kNumCanales % kCanalesPorBanco == 0,
                   "un banco a medias dejaria celdas vacias en la rejilla");

    //  WHICH SIXTEEN THE GRID IS POINTING AT.
    //
    //  All sixty-four PadButtons exist, all the time; only the current bank's
    //  are visible and laid out. That is deliberate: every `pads[i]`,
    //  `refreshPad(i)` and `padClicked(i)` in this file takes a GLOBAL pad
    //  index, and there are twenty-five of them. Keeping the array global
    //  means not one of them has to learn about banks, and there is no
    //  off-by-sixteen to get wrong.
    int currentBank = 0;
    //  EL BANCO DONDE CAEN LAS TOMAS. Preferencia de la PERSONA y no del
    //  proyecto -como el idioma, la carcasa, el master y la cuenta atras-:
    //  cuantos sonidos de fabrica estas dispuesto a gastar es tuyo y del
    //  momento. El defecto se DERIVA y no se elige a ojo: el D es la casa de
    //  los instrumentos por diseño -el instrumento n va siempre al pad n del
    //  banco D- asi que el de las tomas es el ultimo que no lo es.
    static constexpr int kBancoTomasDeFabrica = kNumBanks - 2;   // C
    int bancoTomas = kBancoTomasDeFabrica;
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
    //  LA BARRA DE LA VENTANA, una para las dos paginas. Ver el constructor:
    //  eran ocho tapas de compas y la ventana saltaba de dieciseis en dieciseis.
    BarraVista seqBarra, pianoBarra;
    //  The grid shows ONE bank: sixteen lanes, whichever sixteen those are.
    bool  gridCells[AudioEngine::kNumSteps * AudioEngine::kPadsPerBank] {};
    signed char gridNotes[AudioEngine::kNumSteps * AudioEngine::kPadsPerBank] {};
    //  Y LO QUE DURA CADA GOLPE, en cuartos de paso guardado.
    //
    //  La rejilla dibujaba UNA celda por golpe pasara lo que pasara, asi que
    //  una redonda escrita en 1/8 y mirada en 1/32 se veia igual de corta que
    //  una semicorchea. El piano lo dibuja desde el primer dia (`pianoLargos`);
    //  la rejilla de pasos no, y en una linea melodica eso es no ver la mitad
    //  de lo que hay escrito. Ver StepGrid::paint, que pinta la cola atenuada.
    std::uint16_t gridLargos[AudioEngine::kNumSteps * AudioEngine::kPadsPerBank] {};
    int   gridZati[AudioEngine::kPadsPerBank] {};
    bool  gridLoaded[AudioEngine::kPadsPerBank] {};
    //  EL PRIMER PASO DE LA VENTANA, continuo y no en multiplos de dieciseis.
    //  Lo comparten la rejilla y el piano porque son dos vistas del mismo
    //  patron: cambiar de pestaña no puede moverte de sitio.
    //  LA PRIMERA CASILLA QUE SE VE, EN CASILLAS DE LA VISTA y no en pasos
    //  guardados. Se llamaba `seqPrimerPaso` y cuando la vista y el paso
    //  dejaron de ser lo mismo el nombre paso a decir dos cosas.
    int   seqPrimerCelda = 0;
    //  El ancho de la celda contra su alto, que es el zoom horizontal de la
    //  rejilla de pasos. Uno es CUADRADO, que es como arranca. Ver seqZoomBtn.
    float seqZoomW = 1.0f;
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
    juce::TextButton xyCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
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
    //  COPIAR y PEGAR DE COMPAS SE RETIRARON, y no por sitio: copiaban UNA
    //  columna de cuatro celdas sin sus clips y sin poder decir «de aqui a
    //  aqui». Lo que se pidio es copiar MEDIO patron, y medio patron no es un
    //  compas. Las sustituye la tira de la banda -COPIAR · CORTE · PEGAR ·
    //  BORRAR- que recorta por los filos de lo seleccionado. Ver songCopiaSel.
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
    juce::TextButton songCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::Slider     songLenSlider;
    //  LA BARRA DE LA LINEA DE TIEMPO, donde estaba la fila de paginas
    //  -1, 9, 17...-. Ocho tapas numeradas saltaban de pagina en pagina, asi
    //  que un estribillo que cruzara el compas 8 no se podia mirar entero;
    //  esta desplaza compas a compas y dibuja el cabezal sobre el total.
    BarraVista songBarra;
    int songBrush   = 1;      // >0 pattern bank+1, <0 -(pad+1), 0 = eraser
    //  QUE SUELTA LA BROCHA en un hueco: 0 el patron de la paleta, 1 el sonido
    //  del pad elegido, 2 un CLIP de ese pad. Ver songPadModeBtn.
    int songPincel  = 0;
    //  EL PRIMER COMPAS VISIBLE, continuo y no un numero de pagina. Era
    //  `songPage` y la ventana empezaba en `pagina * compasesVista`, asi que
    //  un estribillo que cruzara el compas 8 obligaba a elegir una mitad.
    int songPrimerCompas = 0;

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
    //  CUANTOS COMPASES SE VEN. La vista estaba clavada en ocho, asi que un
    //  estribillo de dieciseis no cabia en una pantalla y no habia forma de
    //  mirarlo entero. Una tapa que CICLA -8, 16, 4- y no tres, que es lo que
    //  ya hace el zoom del piano; y salta el paso que no cabe, con la misma
    //  pregunta que ya deciden BANCO y PADS. Vive en el renglon del titulo,
    //  al lado del interruptor de vista, asi que cuesta CERO de alto - que es
    //  lo unico que en esta ficha no sobra.
    juce::TextButton songZoomBtn { "8 COMPASES" };

    //  LA BARRA DE HERRAMIENTAS DE LA LINEA DE TIEMPO: MANO, LAPIZ, GOMA y
    //  MUTE, en iconos y sin rotulo.
    //
    //  Eran DOS Y HASTA TRES FILAS DE PALABRAS -medido: una en tableta, dos en
    //  un movil grande y TRES en 360x640, 280x653 y apaisado- en la ficha cuyo
    //  unico trabajo son cuatro carriles. Un icono no pide ancho de texto, asi
    //  que la fila se reparte por el DEDO y no por el rotulo, y con eso caben
    //  en una donde antes hacian falta tres.
    //
    //  Y son MODOS y no acciones: con el lapiz armado la rejilla se comporta
    //  exactamente como siempre, y cada gesto tiene UN significado. Ver
    //  `Playlist::Herramienta`.
    juce::OwnedArray<juce::TextButton> songToolBtns;
    int songHerramienta = 0;   // Playlist::hLapiz

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
    //  El zoom de la linea de tiempo: cuantos compases se ven de una vez.
    //  Re-deriva el primer compas visible para que el que estabas mirando siga
    //  en pantalla -poner cero seria saltar al principio cada vez que se toca-.
    void ponVistaCompases (int n);
    //  Devuelve el primer compas visible que hace falta para que `compas` se
    //  vea, moviendo la ventana lo minimo: si ya se ve, el que hay.
    int  acercaCompas (int compas) const;
    //  Arma una de las cuatro herramientas de la linea de tiempo. Toca las
    //  cuatro tapas, la rejilla y el pincel: la GOMA es la brocha VACIAR, asi
    //  que armarla es ponerla, y ese es el unico dueno de borrar.
    void ponHerramienta (int h);

    //  La tabla que la rejilla dibuja: los clips traducidos a COMPASES. Se
    //  rehace en refreshSong y vive aqui porque el componente la presta, no la
    //  copia - lo mismo que songCells.
    std::vector<Playlist::ClipVista> songClipsVista;

    //  LA ONDA DE CADA CLIP, CALCULADA UNA VEZ Y GUARDADA.
    //
    //  `refreshSong` corre treinta veces por segundo con la ficha CANCION
    //  delante; resumir varios segundos de audio por clip en cada tick es la
    //  misma clase de derroche que `setSource` y `setAudio` ya evitan con su
    //  comparacion. Se recalcula SOLO cuando cambia lo que la onda dibuja -que
    //  pad, y que ventana de ese pad- y no cuando cambia donde esta puesto.
    struct OndaClip
    {
        int pad = -1, desde = -1, largo = -1;
        juce::Array<float> mm;      // intercalada min,max por columna
    };
    std::vector<OndaClip> songClipsOnda;
    //  Sesenta y cuatro columnas: un bloque de clip mide entre 23 px -un
    //  compas con la vista en dieciseis- y unos 300, asi que una cifra fija en
    //  medio dibuja la forma en los dos casos sin recalcular al mover la vista.
    static constexpr int kColumnasOndaClip = 64;
    //  Los tres reciben PASOS ABSOLUTOS de la cancion -`compas * pasosPorCompas()
    //  + paso`- y no compases: la rejilla ya los entrega pegados a la division
    //  que dibuja, y partirlos en dos numeros en el camino es la forma de que
    //  uno de los dos se quede sin acotar.
    void ponClip   (int pista, int paso);
    void mueveClip (int indice, int pista, int paso);
    void quitaClip (int indice);
    void largoClip (int indice, int desdePaso, int hastaPaso);
    //  PARTIR UN CLIP EN DOS por un paso absoluto de la cancion. Los dos
    //  trozos SUMAN el original: no se escribe audio, no nace un pad y no hay
    //  un fichero nuevo que limpiar - un clip es una referencia, y partir una
    //  referencia es quedarse con dos ventanas de la misma fuente.
    void parteClip (int indice, int paso);
    //  LOS BLOQUES, TRADUCIDOS A PASOS para la rejilla. Era
    //  `songCells[4][64]`, una celda por compas, y por eso un bloque no podia
    //  empezar a mitad de compas ni durar medio: en una celda no caben dos
    //  cabezas, asi que partir por el paso 8 no tenia donde guardarse. Ahora es
    //  una lista, como los clips, y vive aqui y no en `refreshSong` por lo
    //  mismo que `songClipsVista`: la rejilla guarda el PUNTERO y no copia.
    std::vector<Playlist::BloqueVista> songBloquesVista;
    //  Y CUANTOS PASOS DURA CADA PATRON, al lado de los bloques y por lo mismo:
    //  la rejilla guarda el PUNTERO -no copia- asi que un array local se
    //  quedaria colgando en cuanto acabara el bloque que lo llena.
    int songLargos[AudioEngine::kNumPatterns] {};
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
    void insertSongBar();
    void removeSongBar();
    //  Y LO QUE MUEVE LA LINEA DE TIEMPO MUEVE LAS DOS COSAS. Ver el cuerpo.
    void corredClips (int desdeCompas, int delta);
    //  Lo mismo para los bloques: sin esto, meter un compas corria el audio y
    //  dejaba los patrones donde estaban, que es el mismo desfase al reves.
    void corredBloques (int desdeCompas, int delta);
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
    //  EN PASOS Y POR INDICE, no en compases y por cabeza: con la lista, la
    //  identidad de un bloque es su sitio en el vector -dos bloques pueden
    //  empezar en el mismo compas- y su largo es un numero de pasos.
    void ponLargoBloque (int indice, int desdePaso, int hastaPaso, bool apunta = true);
    //  PARTIR UN BLOQUE EN DOS por un paso absoluto, que es lo que las TIJERAS
    //  significan en un lienzo y lo que no se podia hacer con celdas. El
    //  segundo trozo se lleva el `offset` que le toca -`(offset + corte -
    //  desde) % len`- o sonaria desde el principio del patron y el corte se
    //  oiria como un salto. Es la misma cuenta que `parteClip` con muestras.
    void parteBloque (int indice, int paso);
    void toggleSongLane (int lane);
    juce::TextButton setButton      { "SET" };   // skins + proyectos (spec: SET)
    juce::TextButton seqCloseButton   { juce::CharPointer_UTF8 (Metrics::cruz) },
                     padCloseButton   { juce::CharPointer_UTF8 (Metrics::cruz) },
                     mixCloseButton   { juce::CharPointer_UTF8 (Metrics::cruz) };
    //  A studio is where a track gets finished, and nothing gets finished
    //  without balancing it. One strip per pad: level, mute, solo.
    juce::OwnedArray<juce::Slider>     mixFaders, mixPans, mixAnchos;

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

    //  Y VIVE AQUI ARRIBA, no donde nacio.
    //
    //  Nacio para las ranuras de la cara y hoy la usan tambien los canalones
    //  del RACK, que se declaran antes: un tipo anidado tiene que estar
    //  completo donde se nombra. Es una mudanza y no un cambio - el cuerpo es
    //  el mismo, incluido el temporizador que dispara CON el dedo puesto.

    //  The rack: one pad's six sends, opened from the mixer. An effect here
    //  is not on or off, it is how much of THIS channel goes into it - which
    //  is the only place where "the delay belongs to the snare" can be said.
    juce::TextButton rackButton { "RACK" },
                 rackCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::OwnedArray<juce::TextButton> rackPadBtns;
    //  Y sus dos chips de banco. Ver `canalBankBtns`: el indice es compartido.
    juce::OwnedArray<juce::TextButton> rackBankBtns;
    juce::OwnedArray<juce::Slider>     rackSends;
    //  El canalon de la izquierda de cada fila. Era texto PINTADO -el nombre
    //  del efecto y su dibujo- y ahora es una tapa, porque el RACK pasa a ser
    //  el sitio donde se cambia lo que hay en una ranura: la fila del rack es
    //  una RANURA y no un efecto.
    //  Y CON DOS GESTOS DESDE ESTA TANDA: toque cambia lo que hay en la
    //  ranura -que es para lo que nacio- y MANTENER abre sus presets.
    //
    //  Del telefono: *«hay que mejorar el tema de los presets para los
    //  efectos, porque no esta muy accesible o legible que digamos»*. La unica
    //  puerta que habia estaba dos toques adentro y en un sitio que no se
    //  adivina: abrir el menu de TIPOS de la ranura y pulsar PRESETS en el
    //  renglon del titulo, o sea pasar por la pantalla de cambiar el efecto
    //  para no cambiarlo. El rack es donde ya estas cuando piensas en ese
    //  efecto, y mantener pulsado es el gesto que esta casa ya usa para «lo
    //  mismo, pero a fondo»: la ranura de la cara, el pad de instrumento, SOLO
    //  y AUTO. Cero tapas nuevas y cero pixeles nuevos, que es lo que hizo
    //  falta la primera vez para NO poner la puerta aqui.
    juce::OwnedArray<HoldButton> rackSlotBtns;
    //  Y LA TAPA DE APAGAR, al lado del canalon.
    //
    //  Se pidio con esas palabras -«al lado del boton del plugin, una opcion
    //  para sustituirlo o MUTEARLO tambien»-: sustituir y vaciar ya se hacian
    //  desde el canalon y apagar no, porque `setFxEnabled` solo se alcanzaba
    //  desde la fila de la cara y desde el XY. El rack PINTABA el estado -el
    //  canalon con el acento, el fader al 50 % de alfa- y no dejaba tocarlo.
    //
    //  Escribe por `fxTapped`, o sea por el MISMO camino que la fila de la
    //  cara: las tres ventanas -cara, rack y XY- siguen siendo tapas de un
    //  estado y `refrescaRanuras` ya las reparte. Un segundo camino a
    //  `setFxEnabled` seria la misma regla escrita dos veces.
    juce::OwnedArray<juce::TextButton> rackMuteBtns;
    //  Y LA TAPA DEL PRESET, EN LA MISMA FILA.
    //
    //  Del telefono, con la foto del rack delante: *«ahi falta un cuadrado o
    //  un visor en el que tu puedas cambiar el preset sin tener que entrar al
    //  propio efecto»*. Mantener el canalon ya abria la rejilla de presets
    //  desde la tanda anterior, y un gesto que no se ve no lo encuentra nadie:
    //  lo que faltaba era que la fila DIJERA que preset lleva y se pudiera
    //  tocar.
    //
    //  DOS GESTOS, y son los dos que hacen falta: el TOQUE pasa al siguiente
    //  de fabrica -con el sonido puesto, sin abrir nada, que es lo que se hace
    //  con la musica sonando- y MANTENER abre la rejilla entera, que es donde
    //  estan los tuyos y donde cada celda dibuja su curva.
    //
    //  Y EN LA MISMA LINEA que el canalon, la tapa de apagar y el fader, con
    //  el ancho sacado del FADER. Estuvo media tanda en un segundo renglon
    //  -«el ancho no estaba»- y eso subia la fila de 48 a 96 px y dejaba un
    //  hueco por cada ranura vacia; del telefono, con la ficha delante: *«?por
    //  que dejas los huecos? Puede hacerse la misma linea. Acorta mas el fader
    //  y entra ahi perfectamente»*. Tiene razon y la cuenta lo dice: en
    //  280x653 el fader reparte 119 px, de los que 44 son el cuadrito del
    //  numero, y lo que se le quita es RECORRIDO -de 75 px a 31-, que en un
    //  mando de 0 a 100 se anda igual con el mismo dedo. El reparto y sus dos
    //  topes estan en `colocaFilaRack`.
    juce::OwnedArray<HoldButton> rackPresetBtns;
    //  PASAR AL SIGUIENTE PRESET DE FABRICA de la ranura `s` del canal actual.
    //  Desde MOVIDO o desde uno tuyo vuelve al primero: los tuyos no tienen
    //  orden -son ficheros de una carpeta- y pasearlos con el dedo seria un
    //  recorrido que cambia segun lo que hayas guardado.
    void pasaFxPreset (int ranura);
    //  Y QUE ENSENA ESA TAPA, que depende del ancho que le haya tocado:
    //  el nombre, su numero, o solo el dibujo. Ver el cuerpo.
    void rotulaFxPreset (HoldButton& b, int fx);
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
    juce::TextButton instCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::TextButton instPackDownBtn { "PACK -" }, instPackUpBtn { "PACK +" };
    juce::OwnedArray<juce::TextButton> instBtns;      // los instrumentos: lista o rejilla
    //  LA REJILLA DEL DESTINO: los dieciseis pads de un banco, cada uno con el
    //  dibujo de lo que lleva. Es un MAPA, no una lista: se elige DONDE va el
    //  instrumento tocando el sitio donde va a estar.
    juce::OwnedArray<juce::TextButton> instDestBtns;
    juce::OwnedArray<juce::TextButton> instBancoBtns;   // A B C D, para el destino
    int instDestPad  = AudioEngine::kNumBanks * AudioEngine::kPadsPerBank - 16;   // D01
    //  EL BANCO DEL DESTINO SE DERIVA Y NO SE GUARDA. Era un campo, y los seis
    //  sitios que lo escribian tenian que acordarse de escribirlo derivado: el
    //  chip de celda, el de banco -que arrastra el destino a proposito-, la
    //  siembra al abrir, el toque a un pad de detras y los dos ganchos del
    //  banco. Cinco lo cumplian y el sexto -`instg`- escribia solo el destino,
    //  asi que funcionaba por casualidad mientras el inicializador era el banco
    //  D y su pedido tambien; con la siembra puesta habria dibujado el banco
    //  del pad elegido y perdido en silencio justo el estado que existe para
    //  medir. Derivado no hay nada que acordarse: una regla, un dueno.
    int bancoDestino() const noexcept { return instDestPad / AudioEngine::kPadsPerBank; }
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
    juce::TextButton vstCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    //  LA PUERTA A EL PAD, en la cabecera y del lado del cierre, que es donde
    //  esta ficha ya tiene sus tapas. Existe desde que la pestaña PAD de la
    //  cara abre LO QUE EL PAD ES: un pad de instrumento llega aqui y no a EL
    //  PAD, y la ganancia, el pan, el filtro y la envolvente son suyos igual
    //  que de una muestra. La que va al reves ya existia y se queda -
    //  `vstButton`, en EL PAD · RIG -: dos puertas y ninguna copia.
    juce::TextButton vstPadBtn { "PAD" };
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

    //  LOS OCHO MANDOS DEL INSTRUMENTO, que es lo que le faltaba a esta ficha
    //  para ser la de un instrumento y no la de una LISTA de instrumentos.
    //
    //  Los dieciseis por dieciseis eran de SOLO LECTURA: se pasaba de un preset
    //  al siguiente y no habia una sola forma de tocar ninguno. Con eso, «un
    //  pad se convierte en un instrumento» era medio verdad - suena como un
    //  instrumento y no se puede ajustar como uno.
    //
    //  Cuatro de FORMA y cuatro comunes, que es la particion que Sintes ya
    //  tiene escrita: los cuatro primeros significan cosas distintas en cada
    //  una de las dieciseis -y eso es justo lo que las hace dieciseis
    //  instrumentos y no uno con los numeros movidos- asi que su NOMBRE y su
    //  RECORRIDO los dice la familia, no esta ficha.
    juce::OwnedArray<juce::Slider> vstMandos;
    //  Y LA VUELTA, que es lo unico que hace que mover sea reversible sin
    //  contar toques: deshacer devuelve UN paso y esto devuelve la receta de
    //  fabrica de ese preset. Solo existe con la receta movida - un control que
    //  no puede hacer nada no es informacion, es ruido.
    juce::TextButton vstVolver { "VOLVER" };
    //  DOS PANELES Y NO UNO para los ocho mandos: los cuatro de FORMA -cuyo
    //  nombre y recorrido los dice la FAMILIA- y los cuatro COMUNES, que son
    //  los mismos en las dieciseis. Esa particion es la que hace que haya
    //  dieciseis instrumentos y no uno con los numeros movidos, y hasta aqui
    //  no se veia: los ocho se leian como una lista plana.
    juce::Rectangle<int> vstPanelForma, vstPanelMandos, vstPieArea;

    //  Y LAS TRES PUERTAS DEL RIG, que es lo otro que se pidio: «la mayoria de
    //  opciones y botones de envios o cosas que hay en pad settings y no hay en
    //  la pantalla del plugin instrumento».
    //
    //  PUERTAS Y NO COPIAS, que es la regla de la casa y aqui ademas la unica
    //  forma: la ganancia, el pan, el filtro y la envolvente de un pad de
    //  instrumento son los mismos mandos que los de una muestra, y duplicar
    //  nueve deslizadores serian nueve sitios que un dia dicen cosas distintas.
    //  Esos ya tienen su puerta -`vstPadBtn`, en la cabecera- y su dueño: EL
    //  PAD. Lo que NO tenia puerta es el reparto: desde esta ficha, el RACK
    //  estaba a TRES toques -PAD, RIG, RACK- y el canal y el piano a tres
    //  tambien. Ahora a uno.
    //
    //  Las tres son ACCIONES y no interruptores, que es por lo que 16 NIVELES
    //  no entra: es un `toggle`, y un toggle en dos sitios son dos estados que
    //  mantener en acuerdo - el fallo que ya costo una medida en `showMixBank`.
    //  Y CHOP, MIC y REMUESTREAR tampoco: las tres ESCRIBEN una muestra en el
    //  pad, o sea que se llevarian por delante el instrumento que esta ficha
    //  edita.
    juce::TextButton vstCanalBtn { "CANAL" }, vstRackBtn { "RACK" }, vstPianoBtn { "PIANO" };
    juce::Rectangle<int> vstPanelRig;
    //  Y LAS DOS ACCIONES, escritas una vez porque ahora las piden dos tapas.
    //  Dos caminos que abren la misma ficha por su cuenta se separan, y el
    //  sintoma seria «desde EL PAD lleva al rack de este canal y desde el
    //  instrumento al de otro».
    void abreRackDelPad();
    void abrePianoDelPad();

    //  ==================================================================
    //  MIDI: EL PATRON DE UN PAD SALE Y ENTRA COMO FICHERO
    //  ==================================================================
    //
    //  Se pidio asi: «en el piano roll se podria tanto exportar como importar
    //  midi para que se lean y se apliquen bien en las cuadriculas». La
    //  conversion no vive aqui sino en `MidiArchivo.h`, que no conoce el motor
    //  y por eso el banco la puede cruzar entera; esto es la puerta.
    //
    //  Y LA PUERTA VA EN EL RENGLON DEL TITULO de la ficha del secuenciador,
    //  que es lo unico que cuesta CERO de alto y ademas es lo correcto: el
    //  fichero es del PATRON y del PAD, no de una de las tres paginas. La fila
    //  de herramientas del piano ya sale de once tapas y se parte en dos en
    //  media pantalla — meter dos mas ahi es quitarle sitio a la rejilla, que
    //  es lo unico que esa pagina no tiene.
    juce::TextButton midiBtn { "MIDI" };
    Sheet midiSheet;
    juce::TextButton midiExportBtn { "EXPORTAR" }, midiImportBtn { "IMPORTAR" };
    juce::TextButton midiCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::Rectangle<int> midiTitleArea, midiAyudaArea, midiParteArea, midiPanel;
    //  Lo ultimo que paso, para que la ficha lo diga donde se actua y no en el
    //  renglon de estado de la cara, que queda detras de la tarjeta.
    juce::String midiParte;
    void abreMidiSheet();
    void exportaMidiPatron();
    void importaMidiPatron (const juce::File& f);
    void openBrowseForMidi();
    //  Las notas del pad elegido en el patron actual, en las unidades de
    //  `MidiArchivo`. Escrito una vez porque lo piden el exportador y el gancho
    //  del banco: dos lecturas del mismo sitio se separan.
    std::vector<MidiArchivo::Nota> notasDelPatron (int patron, int pad) const;

    void abreFichaDelPad();
    void abreVst();
    //  QUE PADS SE TOCAN COMO TECLAS y cual esta sonando por cual. Ver
    //  PadButton::setModoNota: un instrumento sostiene, asi que su nota tiene
    //  que soltarse, y quien la suelta es el dedo.
    void padNotaOn (int index, float vel);
    void padNotaOff (int index);
    void refreshModoNota();
    std::array<int, AudioEngine::kNumPads> notaViva {};
    void refreshVst();
    //  RE-SINTETIZAR AL SOLTAR EL MANDO Y NO AL MOVERLO. Un preset son diez
    //  zonas y cuesta 77 ms medidos: hacerlo por cada valor del arrastre serian
    //  setenta y siete milisegundos por fotograma, o sea un mando que no se
    //  puede mover. Mientras el dedo esta encima solo se escribe el numero.
    void resintetizaInstrumento (int pad);
    void refrescaMandosVst();
    void paintVstSheetContent (juce::Graphics& g);
    void paintMidiSheetContent (juce::Graphics& g);
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
    //  Y CON SU RECETA, que es lo que hace que abrir un proyecto no cueste dos
    //  sintesis: nulo es «la de la tabla», que es como nace un preset recien
    //  elegido. Escribir la receta a mano y re-sintetizar despues rendiria los
    //  diez zonas dos veces para dejar el pad como se queria a la primera.
    void ponInstrumentoEnPad (int pad, int familia, int preset,
                              const Sintes::Preset* receta = nullptr, bool movida = false);
    //  LA MISMA, PERO ESPERANDO A QUE ESTE. Existe para el banco: la de arriba
    //  devuelve con el pad todavia vacio -rinde en otro hilo- y una medida que
    //  lea el pad justo despues leeria el hueco. Ver `esperaInstrumentos`.
    void ponInstrumentoYEspera (int pad, int familia, int preset,
                                const Sintes::Preset* receta = nullptr, bool movida = false);
    void esperaInstrumentos();
    void montaInstrumentoRendido (int pad, SampleBuffer::Ptr sb, const juce::String& nombre);
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
    //  Y los canales tienen las MISMAS cinco cosas que un pad: color, nombre,
    //  fader, mute y SOLO. Mas la cuenta de PADS que le entran, que es lo que un
    //  canal vacio necesita decir de si mismo.
    //
    //  Aqui decia «menos SOLO, que se queda en el pad… dos ambitos de solo son
    //  dos respuestas a "oye solo esto"», y era cierto con dieciseis canales
    //  recien estrenados. Con treinta y dos dejo de serlo: un canal es un GRUPO
    //  —la bateria, las voces, el bajo— y aislar un grupo con el solo del pad
    //  pide acertar los once pads que lo forman y apagarlos de uno en uno. No
    //  son dos respuestas contradictorias porque no se pisan: el pad pasa su
    //  filtro en `effectiveGain` y el canal el suyo en la ganancia del bloque,
    //  cada uno en su ambito. Ver `AudioEngine::setCanalSolo`.
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
    juce::OwnedArray<juce::TextButton> canSolos;
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
        //  Y LA RECETA MOVIDA, por la MISMA razon que las dos de arriba y en
        //  el mismo sitio. `applyState` corre en el `onDone`, o sea DESPUES de
        //  que este trabajo haya rendido los pads, asi que si la receta se
        //  leyera alli el instrumento habria sonado ya con la fila de la
        //  tabla: vuelve del fichero con los ocho mandos donde la persona los
        //  dejo y suena con los de fabrica hasta que alguien mueva uno.
        //  Vacia = la receta es la de la tabla.
        std::array<juce::String, AudioEngine::kNumPads> receta;
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
    juce::TextButton manualButton { "MANUAL" },
                 manualCloseButton { juce::CharPointer_UTF8 (Metrics::cruz) };

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
    //  Y si la pagina de CANCION se quedo con su fila de herramientas de
    //  arreglo. Misma razon, y ver donde se decide: en 412x480 la pagina pide
    //  434 px sobre una tarjeta de 368 con el carril YA en su suelo, asi que
    //  algo se tiene que caer, y lo prescindible es lo que actua sobre lo que
    //  ya esta puesto -no lo que lo pone-.
    bool songUtilAqui = true;
    //  Y el segundo escalon de la misma escalera: la fila de MODOS. Ver donde
    //  se decide.
    bool songModosAqui = true;
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
    //  CUANTOS PARAMETROS TIENE UN EFECTO, escrito una vez. Eran tres -p0, p1 y
    //  mezcla- y desde el enganche del modulador son cuatro; el numero aparecia
    //  a mano en el guardado, en la carga y en el volcado del proyecto, que son
    //  tres sitios donde acordarse.
    static constexpr int kParamsPorFx = AudioEngine::kNumParFx;
    struct FxDef
    {
        const char* name;                  // face button
        const char* param[3];              // what CTRL 1-3 become
        struct Spec { double lo, hi, step, skewMid, def; int fmt; } spec[3];
        double onMix;                      // MIX applied when you switch it on
    };
    static const FxDef fxDefs[kNumFx];

    //  LOS VEINTICUATRO, POR TIPO Y NO POR ETAPA.
    //
    //  `fxDefs` esta en el orden de la CADENA -filtro, saturacion, retardo...-
    //  que es el orden en el que el motor los ejecuta y el que los proyectos
    //  guardan. Un menu en ese orden deja el wah a nueve sitios del filtro y
    //  el octavador a siete del afinador, o sea que para encontrar un filtro
    //  hay que leerse la lista entera.
    //
    //  SEIS FAMILIAS DE CUATRO, y de ahi el cuarto efecto:
    //
    //      FILTRO       FLT HPF WAH EQ  FRM
    //      SATURACION   DRV BIT RNG EXC FLD
    //      MODULACION   CHO FLA PHA TRM ROT
    //      ESPACIO      DLY REV WID AMB PNG
    //      DINAMICA     CMP GTE LIM DSS DUC
    //      TIEMPO       PIT OCT TRN FRZ REP
    //
    //  Con veintitres no habia reparto igual posible -es primo-, asi que
    //  ESPACIO se quedaba en tres. Se escribio el que faltaba (AMB) en vez de
    //  apretar el reparto hasta que cuadrara, que es lo mismo que se decidio
    //  con las familias de Sintes cuando salieron cuatro y cuatro.
    //
    //  Y CADA FILA DEL MENU ES UNA FAMILIA porque el menu pide cuatro columnas
    //  (ver `menuRanuraColumnas`): sin eso el orden esta pero no se ve.
    //  CINCO desde que entraron FRM, FLD, ROT, PNG, DUC y REP: uno por
    //  familia, para que el reparto siguiera cuadrando. El menu pide ese mismo
    //  numero de columnas, asi que cada fila sigue siendo una familia.
    static constexpr int kFxPorTipo = 5;
    static const int* ordenFx();          // kNumFx indices de `fxDefs`
    //  Y SUS NOMBRES, que hasta ahora vivian en un comentario de `ordenFx`.
    //
    //  Un reparto que solo esta en el codigo fuente no lo ve nadie con el
    //  telefono en la mano: la rejilla eran treinta tapas seguidas y habia que
    //  leerlas todas para encontrar una. Del telefono, "organiza el pop-up de
    //  efectos por categorias o secciones como esta el de los instrumentos".
    //
    //  El numero SE DERIVA y no se escribe: son las familias que salen de
    //  `kNumFx` y `kFxPorTipo`, que es quien decide el reparto. Escribir el
    //  seis aqui serian dos topes, y el dia que entrara un efecto mas uno de
    //  los dos se quedaria viejo sin que nada fallara.
    static constexpr int kFxCategorias = kNumFx / kFxPorTipo;
    static_assert (kNumFx % kFxPorTipo == 0,
                   "cada familia de efectos tiene que ser una fila entera del menu");
    static const char* const* categoriasFx();   // kFxCategorias nombres, para T()
    //  Y LA VUELTA: en que celda del menu esta un tipo. La usa el banco, que
    //  pulsa tapas de verdad y tiene el indice del tipo, no el de la celda.
    static int celdaDeFx (int fx);

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

    //  Y LA LUZ TAMBIEN ES DEL CANAL, que es la otra mitad de que un inserto
    //  lo sea: `fxOn` era una fila de veintiuno y con dieciseis canales el
    //  mismo tipo puede estar encendido en uno y apagado en el de al lado.
    //  Quien decide que casilla se mira es `AudioEngine::canalDeParam`, que es
    //  la MISMA condicion con la que el motor indexa `fxP`: un envio colapsa
    //  al canal cero, asi que su luz es una y no dieciseis.
    std::array<std::array<bool, kNumFx>, kNumCanales> fxOn {};
    bool  fxEncendido (int fx) const
    { return juce::isPositiveAndBelow (fx, kNumFx)
          && fxOn[(size_t) AudioEngine::canalDeParam (canalActual, fx)][(size_t) fx]; }
    void  ponFxEncendido (int fx, bool on)
    { if (juce::isPositiveAndBelow (fx, kNumFx))
          fxOn[(size_t) AudioEngine::canalDeParam (canalActual, fx)][(size_t) fx] = on; }

    //  EL CANAL DE DELANTE SE PONE POR UNA PUERTA, que es lo que hace posible
    //  que los sesenta y tres deslizadores dejen de ser el ALMACEN y pasen a
    //  ser una VENTANA. Mil ocho mandos moverian el recuento de componentes y
    //  con el los TOUCH que se leen contra la tanda anterior, asi que quien
    //  tiene los 1008 numeros es el MOTOR y la cara los relee al cambiar.
    //
    //  Y el orden es el que va escrito: `canalActual` PRIMERO y la recarga
    //  DESPUES, siempre con `dontSendNotification`. Al reves, el primer mando
    //  que se toque escribe el ajuste del canal viejo en el nuevo, en silencio
    //  y solo a partir del segundo cambio.
    void  ponCanalActual (int c);
    void  recargaFxDelCanal();
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
    juce::TextButton ranuraCloseBtn { juce::CharPointer_UTF8 (Metrics::cruz) };
    juce::OwnedArray<juce::TextButton> ranuraBtns;     // un tipo por celda
    juce::TextButton ranuraVaciarBtn { "VACIAR" };
    int  ranuraEditada = -1;               // que ranura se esta eligiendo, o -1
    void abreMenuRanura (int ranura);      // -1 lo cierra
    void refrescaMenuRanura();
    void paintRanuraContent (juce::Graphics& g);
    juce::Rectangle<int> ranuraTituloBanda;

    //  LA FICHA DE PRESETS DE UN EFECTO. Ver FxPresets.h.
    //
    //  Se abre desde el RENGLON DEL TITULO del menu de ranura y no desde una
    //  tapa nueva en la fila del rack: esa fila ya esta llena y medida -canalon
    //  52, apagar 40 y el fader 249 en 412x915 pero **119 en 280x653**- y otra
    //  tapa de cuarenta la dejaria en 79. Cero gestos nuevos y cero pixeles
    //  nuevos donde no hay.
    Sheet presetSheet;
    juce::TextButton presetCloseBtn { juce::CharPointer_UTF8 (Metrics::cruz) };
    //  Y la puerta, en el renglon del titulo del menu de ranura. Solo con un
    //  tipo puesto: sobre una ranura vacia no hay presets que elegir, que es la
    //  misma regla que ya tiene VACIAR.
    juce::TextButton ranuraPresetsBtn { "PRESETS" };

    //  CUANTOS TUYOS CABEN EN LA LISTA. Un tope y no una lista sin fin: la
    //  rejilla se reparte en columnas con `menuRanuraColumnas`, que pide el
    //  numero de celdas, y sin tope una carpeta con doscientos ficheros
    //  devuelve una ficha de mil pixeles que `sheetFromBottom` recorta en
    //  silencio — que es la causa de la fila de CADENA y de la REJILLA a 217x0.
    static constexpr int kFxPresetsTuyosMax = 12;
    juce::OwnedArray<juce::TextButton> presetBtns;   // 6 de fabrica + los tuyos
    //  Y LA CURVA DE CADA UNO, que es la mitad legible de la ficha.
    //
    //  Del telefono: *«hay que mejorar el tema de los presets para los efectos,
    //  porque no esta muy accesible o legible que digamos»*. Lo que habia eran
    //  dieciocho celdas con un nombre —«CIERRA», «TAPA», «TELEFONO»— y un
    //  nombre no dice si eso va a cerrar el filtro un poco o del todo: para
    //  saberlo habia que ponerlo y oirlo, uno por uno, perdiendo por el camino
    //  lo que tenias puesto.
    //
    //  ES `FxMini` Y NO UN DIBUJO NUEVO: la miniatura de la cara ya sabe pintar
    //  los treinta tipos a partir de tres numeros —`FxVisor::muestrea`— y aqui
    //  los tres numeros son los del PRESET y no los del motor, que es la unica
    //  diferencia. Un segundo dibujante para lo mismo seria la misma regla
    //  escrita dos veces, y esta casa ya sabe como acaba eso.
    juce::OwnedArray<FxMini> presetCurvas;
    juce::TextEditor presetNombreBox;
    juce::TextButton presetGuardarBtn { "GUARDAR" };
    int presetEditado = -1;                 // que efecto se esta eligiendo, o -1
    juce::StringArray presetTuyosVistos;    // los que la rejilla esta ensenando
    //  Y SUS TRES NUMEROS, leidos cuando se leyo la carpeta y no en cada
    //  repintado: dibujar la curva de un preset tuyo pide sus parametros, y
    //  pedirlos al fichero desde `paint` seria una lectura de disco por
    //  fotograma — que es literalmente lo que dejo la lista del navegador
    //  parpadeando y por lo que `presetTuyosVistos` ya se guarda aqui.
    std::vector<std::array<float, 3>> presetTuyosP;
    juce::Rectangle<int> presetTituloBanda;

    void abreMenuPresets (int fx);
    void refrescaMenuPresets();
    void paintPresetContent (juce::Graphics& g);

    juce::OwnedArray<juce::TextButton> fxButtons;

    std::vector<AudioEngine::EventoAuto> autoEventos;
    HoldButton autoBtn { "AUTO" };
    bool autoArmado = false;
    void anotaAutomacion (int canal, int fx, int par, float v);
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
    //  EL EMBUDO DE VERDAD, y esta un piso por debajo de `pushFxParam`.
    //
    //  `pushFxParam` lee del deslizador, y el deslizador solo existe para los
    //  TRES primeros parametros: el cuarto -el enganche del modulador- lo
    //  guarda el motor y no tiene mando. Un preset escribe los cuatro, asi que
    //  necesita una puerta que no pase por un mando que no hay. Las dos lineas
    //  que de verdad importan -escribir en el motor y anotar la automatizacion-
    //  viven aqui y no se copian.
    void escribeFxParam (int fx, int p, float v);
    juce::Slider& fxParam (int fx, int p) { return *fxParams[fx * 3 + p]; }

    //  LOS PRESETS DE EFECTO. Ver Source/FxPresets.h.
    //
    //  `fxPreset[canal][fx]` es CUAL esta puesto, y `-1` es MOVIDO: el patron
    //  exacto de `padRecetaMovida`, y por la misma razon — una ficha que dice
    //  «PLACA» con el audio ya cambiado miente, y mentir sobre lo que suena es
    //  peor que no decir nada.
    static constexpr int kFxPresetMovido = -1;
    //  Y la guarda que impide que poner un preset se marque a si mismo como
    //  MOVIDO: `escribeFxParam` es el embudo de los dos caminos.
    bool aplicandoFxPreset = false;
    //  Puesta mientras `applyState` repone: lo que se mueve desde ahi no es una
    //  accion de la persona y no lleva foto de deshacer. Ver gridSlider.
    bool aplicandoEstado = false;
    void marcaFxMovido (int fx);
    std::array<std::array<int, kNumFx>, kNumCanales> fxPresetPuesto {};
    //  Y el nombre, cuando el puesto es uno TUYO de `ZATI/Presets`. Disperso a
    //  proposito: la inmensa mayoria de las casillas no llevan ninguno.
    std::array<std::array<juce::String, kNumFx>, kNumCanales> fxPresetTuyo;

    double acotaFxPreset (int fx, int p, double v) const;
    void   aplicaFxPreset (int fx, int k);
    void   aplicaBandasEq (const juce::String& txt);
    void   aplicaFxPresetTuyo (int fx, const juce::String& nombre);
    //  LEER UN PRESET TUYO DEL DISCO, y nada mas. Es el embudo de los DOS que
    //  lo necesitan —ponerlo y DIBUJARLO— y por eso sale de `aplicaFxPresetTuyo`
    //  en vez de copiarse: el que dibuja la curva de la ficha y el que la
    //  escribe en el motor tienen que leer el mismo fichero de la misma manera,
    //  incluido el acotado en la puerta, o la ficha ensena una curva y suena
    //  otra cosa. Devuelve falso si no hay fichero.
    bool   leeFxPresetTuyo (int fx, const juce::String& nombre,
                            double p[kParamsPorFx], juce::String* bandasEq = nullptr) const;
    bool   guardaFxPresetTuyo (int fx, const juce::String& nombre);
    juce::StringArray fxPresetsTuyos (int fx) const;
    static juce::File carpetaFxPresets (int fx);
    juce::String      fxPresetNombre (int fx) const;   // lo que la ficha ensena
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
    //  LAS CUATRO BANDAS DE CATEGORIA de la lista de SINTES. Las publica el
    //  maquetado y las pinta el pintor, que es la regla que ya costo el titulo
    //  y el nombre del pack: una cuenta, un dueno. Vacias cuando el pack de
    //  delante no es el de familias -un pack de disco no tiene categorias-.
    std::array<juce::Rectangle<int>, (size_t) Sintes::kCategorias> instCatArea {};
    //  Y LAS SEIS DEL MENU DE EFECTOS, por lo mismo: un rotulo que solo se lee
    //  no necesita ser una tapa, y siendolo le quitaria ancho a las cinco que
    //  si se tocan. Vienen vacias cuando la rejilla no sale a cinco columnas.
    std::array<juce::Rectangle<int>, (size_t) kFxCategorias> ranuraCatArea {};
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

    //  RECORTAR: tira lo que queda fuera de las asas y deja el trozo.
    //
    //  Es la contraria de NORMALIZAR y por eso van en paginas distintas: esa
    //  no toca la muestra a proposito -es una ganancia, un atomic float que se
    //  deshace y no gasta memoria- y esta la REESCRIBE, porque lo que se pide
    //  es justo que lo de fuera deje de existir. Un pad de un break de cuatro
    //  minutos con el recorte en un golpe pesa cuatro minutos en la sesion, en
    //  el proyecto y en el kit que salga de el; recortado pesa el golpe.
    //
    //  Y SIEMPRE SOBRE UNA COPIA, nunca en el sitio. Un troceado son N pads
    //  apuntando al MISMO SampleBuffer -se diferencian por su recorte- asi que
    //  reescribir el buffer le cambiaria el sonido a los otros quince sin que
    //  nadie los haya tocado. La copia cuesta el trozo, que es menos que el
    //  original por definicion.
    juce::TextButton recorteButton { "RECORTAR" };
    void recortaPad();
    //  Un hilo para limpiar, porque la limpieza NO cabe en el hilo de la
    //  interfaz: son 7 ms por segundo de audio medidos en el banco, o sea 2.1 s
    //  con una muestra de cinco minutos y ocho con una de veinte. Android
    //  ensena el cartel de "la aplicacion no responde" a los cinco.
    juce::ThreadPool denoisePool { 1 };
    bool denoiseBusy = false;

    //  Y OTRO PARA SINTETIZAR, por lo mismo y con una cifra propia: rendir un
    //  instrumento cuesta **473 ms de mediana y 1.6 s el peor** en el hilo de
    //  mensajes -medido en `Tests/instr.py`, quince zonas a cuatro veces la
    //  tasa-, asi que poner un instrumento en un pad congelaba la interfaz ese
    //  tiempo y sin decir nada. La barra de trabajo ya existia pero no se
    //  llegaba a pintar: `beginBusy` solo pide un repintado, y un repintado
    //  pedido desde el hilo que se va a bloquear no ocurre hasta que el bloqueo
    //  termina, o sea justo cuando ya no hace falta.
    //
    //  Uno solo y en cola: dos sintesis a la vez se pelearian por los hilos que
    //  `Sintes` ya reparte por zonas.
    juce::ThreadPool sintesPool { 1 };
    //  QUIEN PIDIO LO QUE VUELVE. Si mientras se rendia el pad cambio de
    //  instrumento -dos toques seguidos en la lista-, lo que llega es de nadie
    //  y se tira; pisarlo pondria el penultimo elegido.
    std::array<int, (size_t) kNumPads> sintesMarca {};
    //  LO RENDIDO ESPERA AQUI, y no viaja dentro del `callAsync`.
    //
    //  Parece dar igual y no lo da: con el buffer dentro del mensaje, la unica
    //  forma de cobrarlo es que el bucle de mensajes corra, y el banco NO lo
    //  hace correr -va de arriba a abajo por ese mismo hilo-. `JUCE_MODAL_LOOPS`
    //  esta a cero en esta app a proposito, asi que `runDispatchLoopUntil` no
    //  existe y no hay forma de bombearlo. Con el buzon, el que espera lo vacia
    //  el mismo y el mensaje se encuentra la bandeja limpia.
    struct Rendido { int pad = -1, marca = 0; SampleBuffer::Ptr sb; juce::String nombre; };
    juce::CriticalSection  sintesLock;
    std::vector<Rendido>   sintesHechos;
    void drenaInstrumentos();

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
    //  LA REJILLA QUE SE MIRA, QUE NO ES EL PASO QUE SE GUARDA.
    //
    //  Ver `AudioEngine::remapeaPaso`: el motor guarda el patron con un paso
    //  propio -el grano de la tabla- y esto es solo el tamano del cuadradito.
    //  Una casilla de la vista son `pasosPorCelda()` pasos guardados, entero,
    //  y mirar mas gordo no toca la tabla. Eran la misma variable y por eso
    //  elegir 1/8 reescribia el patron con la mitad de casillas.
    //
    //  Y ADEMAS es el indice que la rejilla tenia antes del aviso: la foto de
    //  deshacer se toma con el, porque `onValueChange` llega con el nuevo ya
    //  puesto. No son dos datos: es la vista vigente leida en dos momentos.
    int vistaRejilla = 2;                    // 1/16, la misma con la que nace
    static constexpr int kNumGrids = 7;
    //  En negras por paso, en el mismo orden que los nombres de abajo.
    //  SIETE. Faltaban el tresillo de fusa y la semifusa: con 1/32 como paso
    //  mas corto no se puede escribir un redoble de trap ni un tresillo rapido,
    //  que es media musica hecha con esto. El motor acota stepBeats en 0.02, y
    //  1/64 son 0.0625.
    static constexpr float kGridBeats[kNumGrids] =
        { 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f, 1.0f / 12.0f, 0.0625f };
    //  LA MISMA TABLA EN LA MONEDA DEL MOTOR (1/48 de pulso), DERIVADA. Las
    //  siete rejillas son enteros ahi -24, 16, 12, 8, 6, 4, 3- y por eso las
    //  cuentas del remapeo salen sin epsilon. Escribir los siete numeros a
    //  mano seria una segunda tabla que dice lo mismo, y la que se quedara
    //  vieja pondria un golpe en el pulso equivocado sin que nada fallara.
    static constexpr int rejillaU (int i)
    {
        return (int) (kGridBeats[i < 0 ? 0 : (i >= kNumGrids ? kNumGrids - 1 : i)]
                        * (float) AudioEngine::kUnidadesPorPulso + 0.5f);
    }
    static const char* gridName (int i);

    //  LAS CUATRO CUENTAS QUE CONVIERTEN CASILLA DE LA VISTA EN PASO GUARDADO.
    //  Viven aqui y en ningun otro sitio: la cara tiene diecisiete puntos que
    //  hacian `seqPrimerPaso + columna`, y diecisiete copias de una conversion
    //  son diecisiete sitios donde olvidarla - que es literalmente como se
    //  escribio cinco veces el fallo del compas en la pagina del piano.
    int pasosPorCelda() const;
    int celdasDePatron (int pat) const;
    int pasoDeCelda (int celda) const;
    int celdaDePaso (int paso) const;
    //  LA COLUMNA QUE SE VE -0 es la primera de la ventana- EN PASO GUARDADO.
    //  Es la unica forma de escribir `seqPrimerCelda + columna` en esta app.
    int pasoDeColumna (int columna) const;
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
    juce::TextButton eqBandaCloseBtn { juce::CharPointer_UTF8 (Metrics::cruz) };
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
    //  DE DONDE SALIO LO QUE HAY EN EL PAD, que es lo unico que hace posible la
    //  regla de `padParaToma`: la fabrica lo pone al repartir y lo borra
    //  `assignSampleToPad`, que es el embudo por el que entra todo lo demas
    //  -LOAD, un kit, un troceado, un instrumento y la propia toma-. Se guarda
    //  con los pads, o la regla cambiaria entre el primer arranque y el
    //  segundo: la sesion devuelve los sesenta y cuatro desde sus WAV.
    std::array<bool,  kNumPads> padDeFabrica {};
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
    //  LA RECETA VIVA DE UN PAD DE INSTRUMENTO, y el bit que dice si esta
    //  movida. El `SampleBuffer` guarda de QUE fila salio -familia y preset- y
    //  eso es lo que el interruptor de presets mueve; esto guarda los ocho
    //  numeros que se estan oyendo, que ya no tienen por que ser los de la
    //  tabla.
    //
    //  Y el bit no es «distinta de la tabla»: es «la persona la ha tocado».
    //  Deducirlo comparando los ocho numeros seria una regla escrita dos veces
    //  -aqui y en la tabla- y ademas mentiria en el unico caso que importa,
    //  volver a poner a mano el valor de fabrica de un mando.
    std::array<Sintes::Preset, kNumPads> padReceta {};
    std::array<bool, kNumPads> padRecetaMovida {};
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
        //  Y EN QUE PASO GUARDADO DE ESE COMPAS. Ver `AudioEngine::ClipAudio`:
        //  con solo el compas, una toma que entra a la mitad del 3 no se podia
        //  colocar donde entra -medio compas a 120 son 1000 ms- y eso era
        //  exactamente lo que se pidio poder hacer.
        int   paso   = 0;       // 0..pasosPorCompas()-1
        int   desde  = 0;       // primera muestra que suena
        int   largo  = 0;       // cuantas
        float gain   = 1.0f;
    };
    std::vector<ClipUI> clips;

    //  Traduce los clips a la tabla del motor resolviendo pad -> buffer, y la
    //  publica. Se llama desde el hilo de mensajes y en ningun otro sitio.
    void publicaClips();

    //  UN BLOQUE DE LA CANCION, con la misma forma que un clip y por la misma
    //  razon.
    //
    //  Era `songCell[4][64]` en el motor: un entero por compas, cabeza o
    //  `kContinued`. Con eso un bloque SIEMPRE ocupaba compases enteros y
    //  SIEMPRE arrancaba en el paso 0 del patron, asi que «copiar medio patron»
    //  -lo que se pidio- no se podia ni representar: partir un bloque por el
    //  paso 8 pide dos cabezas en el mismo compas y en una celda solo cabe una.
    //  Los clips de audio ya eran por paso desde que una toma podia entrar a
    //  mitad del compas; esto es el mismo modelo para los patrones.
    struct BloqueUI
    {
        int  lane   = 0;    // 0..kSongLanes-1
        //  QUE SUENA: banco 0..kNumPatterns-1 de patron, o negativo para un
        //  golpe suelto -(pad+1), que es la misma codificacion que la brocha.
        int  bank   = 0;
        int  compas = 0;    // donde empieza
        int  paso   = 0;    // y en que paso guardado de ese compas
        //  CUANTOS PASOS OCUPA en la linea de tiempo, que no es el largo del
        //  patron: un bloque puede llevar medio patron o repetirlo tres veces.
        int  largo  = 0;
        //  CON QUE PASO DEL PATRON ARRANCA. Cero en todo lo que se pinta; lo
        //  mueven las tijeras y el recorte de COPIAR, que es lo que hace que
        //  media frase siga sonando por donde iba.
        int  offset = 0;
        bool mudo   = false;
    };
    std::vector<BloqueUI> bloques;

    //  Publica la lista al motor por intercambio de puntero, igual que
    //  `publicaClips`. Hilo de mensajes y ningun otro.
    void publicaBloques();

    //  LOS TRES HELPERS QUE LA AUDITORIA USA para hablar en compases, que es
    //  como estaba escrita. Viven aqui y no en el banco porque la conversion
    //  compas <-> bloque es del modelo: repetirla en `auditArrange` seria la
    //  misma cuenta escrita dos veces y la copia que se quede vieja mediria
    //  otra cosa que la app.
    void ponBloqueCompas (int lane, int bar, int valor, int nBars = 1);
    int  celdaCancion (int lane, int bar) const;
    void vaciaCancion();
    //  Y EL INDICE DEL BLOQUE QUE CUBRE UN PASO, o -1. Ver Playlist::bloqueEn:
    //  la rejilla hace la misma pregunta con sus coordenadas y esta es la
    //  version del modelo.
    int  bloqueEnPaso (int lane, int paso) const;

    // ------------------------------------------------------------------
    //  LA SELECCION DE LA LINEA DE TIEMPO: una banda de carriles x pasos.
    //
    //  Es la otra mitad de lo que se pidio -«que la seleccion funcione bien y
    //  se pueda copiar y pegar medio patron sin virguerias»-. Calco del piano
    //  (`pianoSel`), que ya resolvio el mismo problema: se ve por RELLENO, la
    //  tira de acciones aparece SOLA en cuanto hay banda y se va con ella, y
    //  un toque fuera la vacia. Nada de mantener pulsado ni de menus.
    Playlist::Sel songSel;
    //  DONDE CAE PEGAR: el punto que marca un toque con SEL. Sin marca, el
    //  carril 0 del primer compas visible - pegar tiene que caer donde miras y
    //  no donde se copio, que es la leccion de `pianoPortapapeles`.
    int songMarcaLane = -1, songMarcaPaso = -1;
    //  Un solo pushUndo por arrastre, como `moviendoSel` en el piano: apilar
    //  uno por evento de raton no es deshacer, es contar.
    bool moviendoSelCancion = false;

    //  EL PORTAPAPELES DE RANGO, RELATIVO a la esquina de la banda y con los
    //  clips dentro. Se lleva SOLO LO SELECCIONADO: un bloque de dos compases
    //  cruzado por una banda de 24 pasos sale como bloque de 24 con su offset
    //  ajustado, y un clip se recorta con la misma cuenta que `parteClip`. Eso
    //  es lo que «copiar medio patron y no el bucle entero» quiere decir.
    struct PortaCancion
    {
        std::vector<BloqueUI> bloques;
        std::vector<ClipUI>   clips;
        int carriles = 0, pasos = 0;
    };
    PortaCancion songPortapapeles;

    void songBanda (int lane0, int paso0, int lane1, int paso1);
    void songMueveSel (int dLane, int dPaso);
    void songVaciaSel();
    void songCopiaSel();
    void songPegaSel();
    void songBorraSel();
    void songCortaSel();
    //  BORRAR UN TRAMO dejando lo que asoma por fuera: el bloque se PARTE por
    //  el filo en vez de irse entero. Sin esto, borrar dos compases de un
    //  bloque de ocho se lleva los ocho, que es justo lo que una banda existe
    //  para no hacer. La usan BORRAR, CORTE y el carvado de PEGAR.
    void vaciaBanda (int lane0, int lane1, int paso0, int paso1);

    //  LA TIRA DE ACCIONES DE LA BANDA. Ver el reparto en
    //  MainComponent_Layout.cpp: solo existe con seleccion, y PEGAR solo con
    //  portapapeles. Calco de la tira del piano.
    juce::TextButton songCopiaBtn { "COPIAR" }, songPegaBtn { "PEGAR" },
                     songCorteSelBtn { "CORTE" }, songBorraSelBtn { "BORRAR" };

    //  LA VISTA DE ANTES DE LA LUPA. Un toque sin arrastre vuelve a donde
    //  estabas: sin eso, acercar un tramo es un viaje de ida y volver pide
    //  adivinar el zoom que tenias, que es lo que hace que nadie use el zoom.
    struct { int primero = 0; int compases = Playlist::kBarsViewDef; } songVistaAntes;

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

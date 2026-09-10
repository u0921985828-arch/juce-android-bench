#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "Voice.h"
#include "SampleBuffer.h"
#include "CommandFifo.h"
#include "Fdn.h"
#include "Eq5.h"
#include "Dinamica.h"
#include "Lfo.h"
#include "Estereo.h"
#include "MidiIo.h"

// ============================================================================
//  AudioEngine — real-time core (P1).
//
//  Features: 16-pad matrix (sample per pad), per-pad params (pitch, gain, trim
//  start/end, loop, reverse), a 16-step sequencer with BPM, and mic recording.
//
//  Threading: audio thread renders only (no alloc/lock/free); message thread
//  posts commands, edits per-pad params (atomics), edits the pattern, and does
//  all memory frees. Per-pad samples cross via a pending mailbox per slot; old
//  buffers go to a lock-free retired queue drained by a timer.
// ============================================================================
class AudioEngine
{
public:
    //  SIXTY-FOUR PADS, IN FOUR BANKS OF SIXTEEN.
    //
    //  Sixteen is one kit. A beat is three or four - a drum bank, a chop bank,
    //  a bass bank, one for the stabs - and every sampler this descends from
    //  knows it. The grid on the face stays sixteen because a thumb has not
    //  changed size; what changes is which sixteen it is pointing at.
    //
    //  Everything below is already indexed by pad, so the arrays simply grow.
    //  The one thing that could not was the step mask - see patternBank.
    static constexpr int kPadsPerBank   = 16;
    static constexpr int kNumBanks      = 4;
    static constexpr int kNumPads       = kPadsPerBank * kNumBanks;   // 64
    //  FLT, HPF, DRV, DLY, BIT, REV — el orden que enseña la cara, que es el
    //  de la tabla `fxDefs` de MainComponent.cpp y no otro. Este renglon decia
    //  «ISO ... CRSH», que son dos nombres que la app no usa desde hace tandas.
    //  Y no era este solo: la misma lista estaba mal en Lang.h, en
    //  MainComponent.h y en AudioEngine.cpp — CUATRO sitios en el codigo,
    //  ademas de los tres documentos que ya se corrigieron. Una lista escrita
    //  siete veces son siete listas, y estas cuatro son comentarios: no
    //  compilan, no fallan, y por eso duraron.
    //  SIETE TIPOS Y SEIS RANURAS, que desde el EQ ya no son el mismo numero.
    //  Aqui `kNumFx` son los TIPOS: un bus, una fila de `fxSustituye`, una columna
    //  de envios por pad. Cuantas tapas hay en la cara lo dice
    //  `MainComponent::kNumRanuras`, y no tiene por que coincidir - una ranura
    //  es donde se toca, no lo que suena.
    //  ONCE desde que entra la familia de DINAMICA -CMP, GTE, DSS, LIM-, que
    //  es la que hace falta para grabar voces encima de una produccion. Las
    //  cuatro son un INSERTO y no un envio: comprimir una copia y dejar el
    //  original al lado no comprime nada, que es el mismo argumento que ya
    //  puso el EQ en `fxSustituye`.
//  Y QUINCE desde que entra la familia de MODULACION -CHO, FLA, PHA, TRM-,
    //  que son cuatro clientes de UNA pieza: `Lfo`. Se agrupan por eso y no por
    //  contarlos de cuatro en cuatro; los seis que faltan van con la suya -tres
    //  extracciones para WID, EXC y TRN, y de cero para PIT y FRZ-.
    //
    //  Tres de los cuatro SUMAN, que es una decision y no una copia: el peine
    //  de un flanger y las muescas de un phaser SON la suma de la copia
    //  procesada con la seca, asi que restarle el seco al pad los convierte en
    //  un vibrato y en un allpass puro. TRM al reves -dejar el seco al lado
    //  CANCELA el temblor-. Ver `fxSustituye`.
    //  Y VEINTIUNO con los seis de CARACTER: RNG, PIT, WID, EXC, TRN y FRZ.
    //  Estos no comparten una pieza como los cuatro de modulacion — se agrupan
    //  por lo contrario: tres son EXTRACCIONES de codigo que ya existia
    //  escrito dos veces o privado (`Estereo::ancho` salia de dos copias en
    //  linea de `Voice.h`, y `Dinamica::cruza` y `coefDe` eran privadas) y
    //  tres se escriben de cero.
    static constexpr int kNumFx         = 21;
    //  Que indice es cada uno de los cuatro de dinamica, escrito UNA vez: los
    //  usa el bucle de la etapa, `setFxParam` y la cara para saber de cual
    //  leer la reduccion.
    static constexpr int kFxCmp = 7, kFxGte = 8, kFxDss = 9, kFxLim = 10;
    //  Y LOS OTROS SIETE, que estaban escritos como literales dentro de
    //  `setFxParam` y de las etapas. Ver `fxP`.
    static constexpr int kFxFlt = 0, kFxHpf = 1, kFxDrv = 2, kFxDly = 3,
                         kFxBit = 4, kFxRev = 5, kFxEq  = 6;
    //  Y LOS CUATRO DE MODULACION. Ver `Lfo.h`.
    static constexpr int kFxCho = 11, kFxFla = 12, kFxPha = 13, kFxTrm = 14;
    //  Y LOS SEIS DE CARACTER.
    static constexpr int kFxRng = 15, kFxPit = 16, kFxWid = 17,
                         kFxExc = 18, kFxTrn = 19, kFxFrz = 20;

    //  DIECISEIS CANALES, que es la mesa entre los pads y los efectos.
    //
    //  Hasta aqui el envio era del PAD: 64 x 21 numeros, y su unica puerta el
    //  RACK. Eso hace dos cosas mal a la vez. Son mil trescientos cuarenta y
    //  cuatro numeros que nadie gestiona, y sobre todo la fila de la cara —las
    //  seis ranuras y sus tres mandos— era GLOBAL, asi que cambiar de pad no
    //  cambiaba nada de lo que el pad suena por dentro: «cuando pasas de un pad
    //  a otro, los huecos de los efectos sigue igual».
    //
    //  Ahora el pad elige su CANAL y el canal es el dueño del envio. Y el
    //  reparto entre canales no se inventa: sale de `fxSustituye`, que ya
    //  estaba y ya se mide. De los veintiun tipos, CINCO suman -DLY, REV, CHO,
    //  FLA, PHA- y DIECISEIS sustituyen.
    //
    //    · un INSERTO es de UN canal. Es la regla «un tipo, una ranura»
    //      generalizada y existe por lo mismo: el estado en el motor es uno
    //      solo -un filtro, un compresor, un congelador- asi que dos canales
    //      con el mismo inserto serian dos ventanas al mismo aparato con dos
    //      interruptores que se contradicen. Dieciseis insertos, dieciseis
    //      canales: el numero ya cuadraba.
    //    · un ENVIO es de TODOS. Una linea de retardo existe para que varias
    //      fuentes entren en la misma cola; restringir el DLY a un canal es lo
    //      contrario de lo que un envio significa.
    //
    //  Y por eso el motor NO CRECE: cero buses nuevos, cero etapas nuevas, cero
    //  estado pesado nuevo. Lo unico que cambia es de donde sale el numero que
    //  ya se calculaba una vez por bloque. Medido, la alternativa -una seccion
    //  de efectos por canal- son 1.3 MB por canal, o sea 21 MB por dieciseis,
    //  mas el indice de canal en `fxP`, en las diez etapas y en `EventoAuto`.
    //  Queda para su propia tanda y no tira nada de esta: la abstraccion de
    //  canal hace falta igual, y lo unico que cambiaria es de quien es el
    //  array.
    static constexpr int kNumCanales = 16;

    //  Los valores de fabrica de los tres parametros de cada tipo, en una
    //  tabla y no en diecinueve llaves de inicializacion repartidas por esta
    //  cabecera. Tienen que decir lo mismo que `MainComponent::fxDefs[f]
    //  .spec[p].def`, que es donde arranca el MANDO: lo comprueba
    //  `Tests/ranuras.py`, porque un control y su motor contando cosas
    //  distintas es el fallo que ya costo una medida con el corte del pad. Y
    //  es publica por eso: para que el banco pueda compararlas.
    //
    //  Y en cuanto se compararon, TRES no cuadraban: DRV arrancaba con el
    //  DRIVE en 0.0 y el TONO en 20 kHz mientras su mando decia 0.55 y 8 kHz,
    //  y la FUERZA del de-esser en 0.0 con el mando en 0.5. El mando se
    //  construye con `setValue (def, dontSendNotification)` —a proposito: no
    //  hay motor al que empujar todavia— asi que nadie los igualaba nunca, y
    //  hasta que alguien tocara el mando o abriera un proyecto el motor sonaba
    //  con un numero y la cara decia otro. Manda la CARA, que es donde el
    //  numero esta razonado (0.55 de drive y 8 kHz de tono llevan su parrafo
    //  al lado en `fxDefs`), y la fuerza a cero era ademas un mando que se
    //  movia y no hacia nada, que es un fallo que esta casa ya ha pagado.
    //  LOS DOS TOPES DE LA FAMILIA DE CARACTER, publicos porque los pide el
    //  VISOR: el grano mas largo de PIT y la ventana mas larga de FRZ deciden
    //  la escala del eje que dibujan, y escribirlos alli otra vez son dos
    //  reglas -que es exactamente el fallo que `FxVisor` existe para no tener-.
    static constexpr double kPitGranoMax   = 0.120;   // segundos
    static constexpr double kFrzVentanaMax = 0.500;   // segundos

    static constexpr float kFxDef[kNumFx][3] =
    {
        {     0.0f,   0.707f, 0.0f },   // FLT  barrido, reso, mix
        {   200.0f,   0.707f, 0.0f },   // HPF  freq, reso, mix
        {    0.55f,  8000.0f, 0.0f },   // DRV  drive, tono, mix
        {   250.0f,    0.35f, 0.0f },   // DLY  tiempo, realimentacion, mix
        {     8.0f,     4.0f, 0.0f },   // BIT  bits, rate, mix
        {    0.55f,    0.45f, 0.0f },   // REV  tamano, amortiguado, mix
        {     1.0f,     0.0f, 0.0f },   // EQ   ancho, salida, mix
        {   -18.0f,     4.0f, 0.0f },   // CMP  umbral, ratio, mix
        {   -40.0f,   120.0f, 0.0f },   // GTE  umbral, cierre, mix
        {  6000.0f,     0.5f, 0.0f },   // DSS  freq, fuerza, mix
        {    -1.0f,   120.0f, 0.0f },   // LIM  techo, soltar, mix
        {    0.80f,    0.55f, 0.0f },   // CHO  rate, profundidad, mix
        {    0.25f,    0.55f, 0.0f },   // FLA  rate, realimentacion, mix
        {    0.35f,    0.70f, 0.0f },   // PHA  rate, profundidad, mix
        {    4.50f,    0.80f, 0.0f },   // TRM  rate, profundidad, mix
        {   220.0f,    1.00f, 0.0f },   // RNG  frecuencia, anillo, mix
        {     7.00f,   60.0f, 0.0f },   // PIT  semitonos, grano ms, mix
        {     1.40f,  120.0f, 0.0f },   // WID  ancho, mono Hz, mix
        {  4000.0f,    0.50f, 0.0f },   // EXC  cruce, fuerza, mix
        {     0.60f,    0.00f, 0.0f },   // TRN  ataque, caida, mix
        {   180.0f,    0.35f, 0.0f },   // FRZ  ventana ms, suave, mix
    };
    static constexpr int kNumSteps      = 64;   // max steps per pattern (length is variable, see below)
    static constexpr int kMinPatLen     = 16;
    static constexpr int kMaxPatLen     = kNumSteps;   // 64 = four bars of 16
    static constexpr int kNumPatterns   = 8;    // pattern banks
    static constexpr int kMaxChain      = 16;   // chain slots (pattern indices, in play order)

    //  --- Song / playlist -------------------------------------------------
    //  The chain was one queue of banks: a track in a straight line. A song is
    //  a TIMELINE — several lanes running at once, each holding either a
    //  pattern (which occupies as many bars as its own length needs) or a
    //  single sample fired at that bar. That is what lets a break, a vocal
    //  one-shot and a drum pattern all land on bar 9 together.
    static constexpr int kSongLanes = 4;
    //  PISTAS DE AUDIO, que es lo que separa un groovebox de un DAW: hasta
    //  ahora la linea de tiempo solo admitia bloques de patron y golpes
    //  sueltos, y TODO el audio entraba por un pad. Cuatro pistas y sesenta y
    //  cuatro clips: el tope no es un numero redondo sino lo que la memoria de
    //  la gama mas baja aguanta - ver DeviceTier y `publicaClips`.
    static constexpr int kAudioTracks = 4;
    static constexpr int kMaxClips    = 64;
    static constexpr int kSongBars  = 64;
    static constexpr int kBarSteps  = 16;

    //  Cell encoding, kept as one int so the audio thread reads it atomically:
    //     0            empty
    //     1..8         a pattern STARTS here (value = bank + 1)
    //     -(pad+1)     a one-shot: fire this pad at the top of the bar
    //     kContinued   this bar is still covered by a pattern started earlier
    static constexpr int kContinued = 1000;

    AudioEngine();
    ~AudioEngine();

    // --- Audio thread ---
    void prepareToPlay (double sampleRate, int maxBlockSize, int inputChannels = 0) noexcept;
    void releaseResources() noexcept;
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) noexcept;

    // --- Triggers (message thread) ---
    void postNoteOn  (int slot, float vel = 1.0f) noexcept;   // uses the pad's stored params
    //  Audition: same note, but read from a point in the source instead of
    //  from the pad's trim. Nothing about the pad changes.
    void postNoteOnFrom (int slot, float from01, float vel = 1.0f) noexcept;
    //  Y AUDICION EN OTRA NOTA, que es lo que pide un teclado.
    //
    //  UNA NOTA DE INSTRUMENTO NO SE ACABA SOLA, y por eso el largo tiene tres
    //  respuestas y no dos.
    //
    //  Una muestra de percusion termina cuando se acaba el fichero: se dispara
    //  y ya esta. Una zona de instrumento DA VUELTAS -es lo que hace que una
    //  nota se sostenga- asi que una voz sin largo no termina nunca. Medido con
    //  un acorde en el secuenciador: la primera vuelta sonaba y la segunda ya
    //  no, porque las notas de la primera seguian vivas ocupando el pool.
    //
    //  kGateSuelta la sostiene QUIEN LA DISPARA -un dedo en un pad, una tecla,
    //  el MIDI- y mandara su NoteOff. kGateAuto es "dispara y se va": el
    //  secuenciador sin largo escrito, la cancion, el bote salvavidas. Ahi el
    //  motor pone UN PASO, que es lo que una casilla dura.
    //  Y kGateAudicion, que es "dispara y se va" desde la INTERFAZ - tocar un
    //  preset, una tecla del piano roll, un toque en la onda -. Dura mas que un
    //  paso porque no es musica sino escuchar: 1.2 s es lo que se tiene un dedo
    //  encima de una tecla para saber como suena.
    //
    //  Las tres se resuelven en triggerPad y SOLO donde hacen falta: una zona
    //  que no da vueltas se acaba sola, asi que a una campana de dos segundos
    //  no se le pone ningun largo. Ver el bloque que las convierte.
    static constexpr int kGateSuelta   = -1;
    static constexpr int kGateAuto     = -2;
    static constexpr int kGateAudicion = -3;
    static constexpr float kAudicionSeg = 1.2f;

    //  Existe porque la unica forma que habia de oir un semitono desde la
    //  interfaz era setPadPitch seguido de postNoteOn, y eso hace DOS danos:
    //  deja el pad afinado en la ultima tecla que se toco - el mando NOTA de la
    //  pagina PASO se movia solo por pasear por el piano - y ademas suena
    //  desafinado la primera vez, porque postNoteOn lee lo que hay ALMACENADO y
    //  el orden de las dos llamadas no es el orden en que el hilo de audio las
    //  ve. El semitono viaja en el comando; el pad no se toca.
    //  Ver kGateSuelta: kSostenida la sostiene quien la dispara y mandara su
    //  NoteOff -un dedo en un pad en modo tecla, una tecla del teclado de la
    //  ficha, el MIDI-, y es lo unico que puede decir "hasta que yo diga".
    static constexpr int kSostenida = kGateSuelta;

    void postNoteOnAt (int slot, int semis, float vel = 1.0f,
                       int gate = kGateAudicion) noexcept;
    void postNoteOff (int slot) noexcept;
    void postPanic() noexcept;
    void postTestTone() noexcept;

    void noteOnByLifeboat (int slot) noexcept;   // see postNoteOn

    //  --- MIDI -----------------------------------------------------------
    //  Entrada: la llama el hilo con el que JUCE entrega el MIDI, que NO es el
    //  de mensajes. Va por su propia cola. Ver MidiIo.h.
    void postNoteOnFromMidi  (int slot, float vel) noexcept;
    void postNoteOffFromMidi (int slot) noexcept;

    //  Salida: el hilo de AUDIO deja aqui cada golpe y otro hilo lo envia.
    //  Encenderla no cuesta nada cuando no hay nadie escuchando - el hilo de
    //  audio comprueba un bool atomico y no escribe.
    void setMidiOutEnabled (bool on) noexcept { midiOutOn.store (on, std::memory_order_relaxed); }
    bool isMidiOutEnabled() const noexcept { return midiOutOn.load (std::memory_order_relaxed); }
    MidiIo::NoteFifo& midiOutQueue() noexcept { return midiOut; }

    //  How many triggers the command queue has refused since the last check.
    //  Reading it clears it. A non-zero answer means taps are reaching the
    //  audio thread by the lifeboat rather than by the queue, which is worth
    //  rebuilding the device over.
    int takeDroppedCommands() noexcept { return droppedCommands.exchange (0, std::memory_order_relaxed); }

    // --- Per-pad params (message thread) ---
    void setPadPitch   (int slot, float semis) noexcept { store (padPitch,   slot, semis); }
    //  Existe para el banco: "oir una nota no afina el pad" es una promesa que
    //  no se puede comprobar sin leer lo que quedo guardado.
    float getPadPitch (int slot) const noexcept
    {
        return (slot >= 0 && slot < kNumPads) ? padPitch[(size_t) slot].load (std::memory_order_relaxed) : 0.0f;
    }
    //  ACOTADOS EN LA PUERTA, no en quien llama.
    //
    //  Estos seis salian de `getProperty` sobre el fichero de proyecto y
    //  entraban con un `store` a pelo: un project.xml a medio escribir -o
    //  editado a mano- metia gain=1e30 o pan=900 en el hilo de audio, y el
    //  fader de la mesa SI acota al pintarse, asi que el mando ensenaba 0 dB y
    //  el motor tenia el numero crudo. Es lo mismo que loadMasterPref lleva
    //  escrito desde el dia que se escribio: se lee acotado al rango del mando.
    //  Y aqui y no en applyState, que es la unica puerta por la que pasan
    //  todos los caminos - el fichero, el mando, el bloqueo de paso y el kit.
    void setPadGain    (int slot, float g)     noexcept { store (padGain,    slot, juce::jlimit (0.0f, 4.0f, g)); }
    void setPadStart   (int slot, int s)       noexcept { store (padStart,   slot, s); }
    void setPadEnd     (int slot, int e)       noexcept { store (padEnd,     slot, e); }
    void setPadLoop    (int slot, bool b)      noexcept { store (padLoop,    slot, b); }
    void setPadReverse (int slot, bool b)      noexcept { store (padReverse, slot, b); }
    //  Whether pitch drags the length with it (tape) or not (tone).
    void setPadKeepLength (int slot, bool b)   noexcept { store (padKeepLength, slot, b); }
    bool getPadKeepLength (int slot) const noexcept
    { return slot >= 0 && slot < kNumPads && padKeepLength[(size_t) slot].load (std::memory_order_relaxed); }
    void setPadChoke   (int slot, int group)   noexcept { store (padChoke,   slot, group); }   // 0 = none
    //  AUTOCUT: a pad that cuts ITSELF. CHOKE settles arguments between
    //  different pads (the open hat and the closed one); this one is about a
    //  pad retriggering over its own tail, which is what a hardware sampler
    //  does by default and what makes a stab or a vocal sound like one
    //  instrument instead of a chorus of itself.
    void setPadSelfCut (int slot, bool on)     noexcept { store (padSelfCut, slot, on); }
    bool getPadSelfCut (int slot) const noexcept
    { return slot >= 0 && slot < kNumPads && padSelfCut[(size_t) slot].load (std::memory_order_relaxed); }
    void setPadPan     (int slot, float p)     noexcept { store (padPan,     slot, juce::jlimit (-1.0f, 1.0f, p)); }
    //  ANCHO ESTEREO: 0 mono, 1 como viene, 2 el doble de lado. Ver Voice::ancho:
    //  el pan dice DONDE esta el sonido y esto CUANTO ocupa, y son dos cosas.
    void setPadAncho   (int slot, float w)     noexcept { store (padAncho,   slot, juce::jlimit (0.0f, 2.0f, w)); }
    float getPadAncho (int slot) const noexcept
    {
        return (slot >= 0 && slot < kNumPads) ? padAncho[(size_t) slot].load (std::memory_order_relaxed) : 1.0f;
    }
    void setPadAttack  (int slot, float ms)    noexcept { store (padAttack,  slot, juce::jlimit (0.0f, 10000.0f, ms)); }
    void setPadRelease (int slot, float ms)    noexcept { store (padRelease, slot, juce::jlimit (0.0f, 20000.0f, ms)); }

    //  EL FILTRO DEL PAD. Un paso bajo por pad, con corte y resonancia.
    //
    //  Va aqui y no dentro de Voice porque un filtro es del PAD y no de cada
    //  golpe: dieciseis disparos del mismo bombo comparten un filtro, igual
    //  que en cualquier sampler, y meterlo en la voz habria hecho que cada
    //  redisparo reiniciara el estado del filtro - un chasquido en cada nota
    //  cuando el corte esta bajo. Ademas el bucle interior de Voice::render
    //  se recorre hasta cuarenta y ocho veces por bloque y este no: una vez
    //  por pad que suene.
    //
    //  ABIERTO ES GRATIS, y por eso no hay interruptor: con el corte arriba y
    //  la resonancia a cero el pad no pasa por aqui - ni siquiera por el
    //  camino separado que hace falta para filtrarlo. Un mando de mas que
    //  apagar es un mando que la gente deja mal puesto.
    void setPadCutoff (int slot, float hz) noexcept
    {
        if (slot < 0 || slot >= kNumPads) return;
        padCutoff[(size_t) slot].store (juce::jlimit (20.0f, kFiltOpenHz, hz), std::memory_order_relaxed);
        //  Y EL BLOQUEO DE PASO SE RINDE: quien toca el mando manda. Sin esto,
        //  un patron con un paso bloqueado dejaria el pad sordo a su propio
        //  CORTE para siempre - el bloqueo se queda puesto hasta que otro paso
        //  diga otra cosa, que es lo que tiene que hacer mientras suena y no
        //  cuando la persona lo mueve a mano.
        pasoCutoff[(size_t) slot].store (-1.0f, std::memory_order_relaxed);
        refreshFiltMask (slot);
    }
    void setPadReso (int slot, float r) noexcept
    {
        if (slot < 0 || slot >= kNumPads) return;
        padReso[(size_t) slot].store (juce::jlimit (0.0f, 1.0f, r), std::memory_order_relaxed);
        refreshFiltMask (slot);
    }
    float getPadCutoff (int slot) const noexcept
    { return (slot >= 0 && slot < kNumPads) ? padCutoff[(size_t) slot].load (std::memory_order_relaxed) : kFiltOpenHz; }
    float getPadReso (int slot) const noexcept
    { return (slot >= 0 && slot < kNumPads) ? padReso[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; }

    static constexpr float kFiltOpenHz = 20000.0f;   // corte arriba del todo = sin filtro

    //  EL FUNDIDO DE LOS BORDES DEL RECORTE. Ver Voice::fadeInSamp.
    //
    //  No es el ataque del pad: ese cuenta desde que se golpea y suena una vez.
    //  Este cuenta desde el borde de la ventana, asi que en un bucle suena en
    //  cada vuelta y en un troceado suaviza el sitio por el que se corto.
    void setPadFadeIn  (int slot, float ms) noexcept { store (padFadeIn,  slot, juce::jlimit (0.0f, 500.0f, ms)); }
    void setPadFadeOut (int slot, float ms) noexcept { store (padFadeOut, slot, juce::jlimit (0.0f, 500.0f, ms)); }
    float getPadFadeIn  (int slot) const noexcept
    { return (slot >= 0 && slot < kNumPads) ? padFadeIn[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; }
    float getPadFadeOut (int slot) const noexcept
    { return (slot >= 0 && slot < kNumPads) ? padFadeOut[(size_t) slot].load (std::memory_order_relaxed) : 0.0f; }
    //  Mute and solo fold into the SAME gain the voices already follow at
    //  control rate, so they take hold on notes that are already sounding —
    //  a mute you have to wait out is not a mute.
    void setPadMute    (int slot, bool m)      noexcept { store (padMute, slot, m); refreshSolo(); }
    void setPadSolo    (int slot, bool s)      noexcept { store (padSolo, slot, s); refreshSolo(); }
    bool isPadMuted    (int slot) const noexcept { return slot >= 0 && slot < kNumPads && padMute[(size_t) slot].load (std::memory_order_relaxed); }
    bool isPadSoloed   (int slot) const noexcept { return slot >= 0 && slot < kNumPads && padSolo[(size_t) slot].load (std::memory_order_relaxed); }
    bool anySolo() const noexcept { return soloActive.load (std::memory_order_relaxed); }
    void clearSolo() noexcept { for (auto& s : padSolo) s.store (false, std::memory_order_relaxed); refreshSolo(); }

    // Audible gain for a pad = its own level, silenced by mute or by someone
    // else's solo.
    float effectiveGain (int slot) const noexcept
    {
        if (slot < 0 || slot >= kNumPads) return 0.0f;
        if (padMute[(size_t) slot].load (std::memory_order_relaxed)) return 0.0f;
        if (soloActive.load (std::memory_order_relaxed)
            && ! padSolo[(size_t) slot].load (std::memory_order_relaxed)) return 0.0f;
        return padGain[(size_t) slot].load (std::memory_order_relaxed);
    }
    int  getSampleLength (int slot) const noexcept;   // 0 if none

    //  Does the engine have a sound for this pad - adopted OR on its way?
    //
    //  getSampleLength answers only the first, because it reads the pointer
    //  the audio thread owns and adoption happens at the top of a render
    //  block. With no device open no block ever runs, so a freshly published
    //  pad reads as empty forever. That is fine for the audio thread and
    //  useless for anyone asking "did this land", which is what the bench and
    //  the interface actually want to know.
    bool hasSampleFor (int slot) const noexcept
    {
        if (! juce::isPositiveAndBelow (slot, kNumPads)) return false;
        return padSample[(size_t) slot] != nullptr
            || pendingPad[(size_t) slot].load (std::memory_order_acquire) != nullptr;
    }

    //  Polyphony, decided by the device rather than by a constant. Called once
    //  before any audio runs; both values are plain ints because nothing reads
    //  them except the audio thread, and it reads them after they are set.
    void setPolyphony (int totalVoices, int perPad) noexcept
    {
        voiceLimit     = juce::jlimit (4, kNumVoices, totalVoices);
        maxVoicesOnPad = juce::jlimit (2, voiceLimit, perPad);
    }
    int getPolyphony() const noexcept { return voiceLimit; }

    //  Where this pad's read head is inside its whole source, 0..1, or -1 when
    //  nothing of it is sounding. Cosmetic: the UI draws it, nobody acts on it,
    //  so a block's worth of staleness is exactly right.
    float getPadPosition01 (int slot) const noexcept
    {
        return (slot >= 0 && slot < kNumPads)
                 ? padPos[(size_t) slot].load (std::memory_order_relaxed) : -1.0f;
    }

    //  How much of the pool is in use. Read by the UI for a polyphony readout
    //  and by the offline checks; a benign race with the audio thread is fine
    //  for both, since neither acts on the number.
    int getActiveVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices) if (v.active) ++n;
        return n;
    }

    // --- Samples (message thread) ---
    void publishSample (int slot, SampleBuffer::Ptr newBuffer) noexcept;

    //  TAKE THE SOUND OFF A PAD - and mean it.
    //
    //  publishSample (slot, nullptr) reads like the way to do this and is a
    //  no-op: it returns early on a null buffer. So every place that emptied a
    //  pad - cancelling an audition, opening a project with fewer pads, NUEVO -
    //  emptied only the INTERFACE. The engine kept the buffer, the sequencer
    //  kept triggering it, and the pad you had just cleared went on making the
    //  sound of whatever used to be there.
    //
    //  A pad cannot be cleared by publishing, because "nothing" is exactly the
    //  value publishing uses to mean "no change". It needs a flag of its own,
    //  which the audio thread consumes at the top of a block the same way it
    //  adopts an incoming buffer.
    void clearPad (int slot) noexcept
    {
        if (juce::isPositiveAndBelow (slot, kNumPads))
            pendingClear[(size_t) slot].store (true, std::memory_order_release);
    }
    void collectRetiredSamples() noexcept;

    //  UN CLIP ES UNA REFERENCIA, no un fichero que se abre.
    //
    //  Apunta a un `SampleBuffer` que YA esta publicado y contado, dice en que
    //  compas de la cancion empieza, que trozo de la fuente suena y con que
    //  ganancia. El hilo de audio solo lee: ni reserva, ni suelta, ni abre
    //  nada, que es el invariante que gobierna todo lo de aqui.
    //
    //  El compas se guarda en COMPASES y el largo en MUESTRAS, y esa mezcla es
    //  deliberada: un compas es tiempo musical -se mueve si cambia el tempo- y
    //  el audio de un clip mide lo que mide. Sin estirado, un clip grabado a
    //  120 deja de encajar si el proyecto se pone a 140, que es exactamente lo
    //  que hace cualquier DAW sin warp puesto.
    struct ClipAudio
    {
        SampleBuffer* fuente = nullptr;
        int   pista  = 0;         // 0..kAudioTracks-1
        int   compas = 0;         // donde empieza, en compases de la cancion
        int   desde  = 0;         // primera muestra de la fuente que suena
        int   largo  = 0;         // cuantas muestras suenan
        float gain   = 1.0f;
    };

    //  La tabla es INMUTABLE y se publica entera por intercambio de puntero,
    //  igual que un buffer de pad (ver pendingPad): el hilo de audio la adopta
    //  al principio del bloque y la vieja se va a la cola de retirados para que
    //  la suelte el hilo de mensajes. Cambiar los clips en su sitio seria una
    //  lectura rota a medio bloque, y tomar un cerrojo esta prohibido.
    struct TablaClips
    {
        int n = 0;
        std::array<ClipAudio, kMaxClips> c {};
    };

    //  La construye y la publica el hilo de mensajes. Se queda una referencia
    //  de cada fuente mientras la tabla viva: un clip cuyo buffer se suelte por
    //  otro lado dejaria al audio leyendo memoria liberada.
    void publicaClips (const ClipAudio* clips, int cuantos) noexcept;

    // ------------------------------------------------------------------
    //  LA AUTOMATIZACION, que es lo que separa «toco los efectos» de «el
    //  rebote suena como lo toque».
    //
    //  Hasta aqui un parametro de efecto era UN numero: el que estuviera
    //  puesto al exportar. La fila de la cara sirve para tocar en directo -es
    //  la mitad de por que existe- y todo eso se perdia en el rebote, asi que
    //  la unica forma de que un barrido de filtro saliera en el fichero era
    //  quedarse quieto y no tocarlo.
    //
    //  UN EVENTO ES UN PASO DE LA CANCION Y UN VALOR, y va en la LINEA DE
    //  TIEMPO y no en el patron: es la misma decision que ya tomaron los
    //  clips, y por lo mismo -un barrido de ocho compases no es de ningun
    //  patron-. En modo patron no se escribe ni se reproduce nada, que dos
    //  relojes para una automatizacion son dos reglas.
    struct EventoAuto
    {
        int          paso  = 0;    // paso absoluto de la cancion
        juce::uint8  fx    = 0;    // tipo de efecto, no ranura: lo que suena
        juce::uint8  par   = 0;    // 0..2
        //  Y EL CANAL, que cabe en el hueco de alineacion que ya habia: doce
        //  bytes antes y doce despues. Un inserto es de un canal, asi que un
        //  barrido de filtro sin decir cual se aplicaria siempre al cero.
        juce::uint8  canal = 0;
        float        valor = 0.0f;
    };
    static_assert (sizeof (EventoAuto) == 12, "el canal tiene que caber en el hueco");

    //  Cuatro mil eventos son 62 compases de los 64 que la cancion admite
    //  moviendo un parametro en CADA paso, o veintiuno moviendose a la vez
    //  cada cuatro pasos. Y el tope se cuenta, que un tope que se supera en
    //  silencio no protege: esconde.
    static constexpr int kMaxAuto = 4096;
    struct TablaAuto
    {
        int n = 0;
        std::array<EventoAuto, kMaxAuto> e {};
    };

    //  UN PARAMETRO DE EFECTO, POR SU NUMERO. Esta traduccion vivia entera en
    //  `MainComponent::pushFxParam` -un switch de veintiun casos- y ahi era
    //  correcta mientras el unico que movia un parametro fuese un mando. Con
    //  la automatizacion hay un segundo cliente Y esta en el hilo de audio,
    //  asi que copiarla habria sido la misma regla escrita dos veces: la que
    //  se quedara vieja dejaria un parametro que se automatiza y no suena.
    //  Vive aqui, que es donde estan los atomicos.
    void setFxParam (int canal, int fx, int par, float v) noexcept;

    //  Y SU LECTOR, que es lo que le faltaba a esta tabla desde que existe: la
    //  cara guardaba en el fichero de proyecto lo que tuvieran sus sesenta y
    //  tres deslizadores, o sea el ALMACEN, y con dieciseis canales esos
    //  deslizadores pasan a ser una VENTANA al canal de delante. Quien tiene
    //  los 1008 numeros es el motor.
    float getFxParam (int canal, int fx, int par) const noexcept
    {
        if (! juce::isPositiveAndBelow (fx, kNumFx) || ! juce::isPositiveAndBelow (par, 3))
            return 0.0f;
        return fxParamDe (canal, fx, par).load (std::memory_order_relaxed);
    }

    void publicaAutomacion (const EventoAuto* ev, int cuantos) noexcept;
    int  numAuto() const noexcept { return autoVivos.load (std::memory_order_relaxed); }
    //  ESCRIBIR APAGA LEER, que es lo unico que separa grabar de pelearse con
    //  lo grabado: con el modo de escritura armado, los eventos de la pasada
    //  anterior no se aplican - si no, el mando se movia solo debajo del dedo.
    void setAutoEscribe (bool on) noexcept { autoEscribe.store (on, std::memory_order_relaxed); }
    bool isAutoEscribe() const noexcept { return autoEscribe.load (std::memory_order_relaxed); }
    //  En que paso de la cancion esta el transporte, para que quien graba sepa
    //  donde poner el evento. -1 si no hay cancion rodando.
    int  pasoDeCancion() const noexcept { return pasoAuto.load (std::memory_order_relaxed); }
    void renderClick (juce::AudioBuffer<float>& out, int offset, int n) noexcept;
    int  numClips() const noexcept { return clipsVivos.load (std::memory_order_relaxed); }

    void setPistaMute (int pista, bool on) noexcept
    {
        if (pista >= 0 && pista < kAudioTracks)
            pistaMute[(size_t) pista].store (on, std::memory_order_relaxed);
    }
    bool isPistaMute (int pista) const noexcept
    {
        return pista >= 0 && pista < kAudioTracks
            && pistaMute[(size_t) pista].load (std::memory_order_relaxed);
    }
    //  Punteros que la cola de retirados tuvo que tirar por estar llena, o sea
    //  buffers cuya cuenta no bajara nunca. Cero es lo unico correcto.
    int takeRetiredLost() noexcept { return retired.takePerdidos(); }

    // --- Sequencer (message thread) ---
    void setPlaying (bool p) noexcept { playing.store (p, std::memory_order_relaxed); }

    //  MASTER GAIN, for ducking under a notification.
    //
    //  A chime from another app is not a reason to tear the audio device
    //  down: it lasts a third of a second, and rebuilding the stream around
    //  it costs more than it saves and puts the instrument through the one
    //  path where it can come back silent. Turning the master down for a
    //  moment is what the system is asking for, and it costs nothing.
    //
    //  Ramped in the render, never stepped: a jump straight to a quarter is a
    //  click, and a click is what the notification was trying to avoid.
    //  EL LIMITADOR DE SEGURIDAD ES DE LA ESCUCHA, NO DE LA OBRA.
    //
    //  Existe para que dieciseis pads a tope mas un delay realimentado no
    //  lleguen al DAC por encima de 0 dBFS y salgan recortados a hacha. Eso es
    //  monitorizacion. Pero renderNextBlock es TAMBIEN lo que usa el Exporter,
    //  asi que el master exportado salia con el limitador ya impreso - y el
    //  Exporter, justo despues, medía el pico y bajaba la ganancia "para no
    //  saturar" sobre una señal que ya venia saturada. Media un pico que el
    //  limitador acababa de aplastar, no lo encontraba nunca por encima de 1.0
    //  y anunciaba que no habia hecho falta bajar nada.
    //
    //  Puesto a false, el motor entrega la suma tal cual y el Exporter mide y
    //  compensa de verdad, que es lo que su propio comentario dice que hace.
    void setSafetyLimiter (bool on) noexcept { safetyLimiter.store (on, std::memory_order_relaxed); }

    //  EL MASTER SON DOS COSAS Y NO UNA, y mezclarlas es un fallo con forma de
    //  simplificacion.
    //
    //  masterTarget -lo unico que lee el hilo de audio- valia para atenuar por
    //  un aviso del sistema: 0.28 al llegar la notificacion y 1.0 al volver. Si
    //  el mando de la persona escribiera ahi tambien, la primera notificacion
    //  se llevaria su nivel por delante: el aviso baja a 0.28, el aviso pasa,
    //  y el "vuelve a 1.0" pone el master AL MAXIMO aunque estuviera a la
    //  mitad. Y con el vigilante de seis segundos detras, tambien pasaria sin
    //  que llegue el GAIN de vuelta.
    //
    //  Asi que se guardan por separado y el objetivo es el PRODUCTO. Lo que se
    //  restaura al recuperar el foco es el permiso para sonar, no un numero.
    //  Y EL TOPE NO ES LA UNIDAD.
    //
    //  Estaba acotado en 1.0 con el argumento de que por encima solo se gana
    //  empujar el limitador, y ese argumento da por hecho que la senal YA esta
    //  en el techo. No lo esta: la fabrica esta igualada por SONORIDAD, no por
    //  pico, asi que un patron de verdad llega a 0.566 -medido en el banco del
    //  motor, con 0.00% de saturacion-. Son casi cinco decibelios de margen que
    //  la maquina no podia usar, porque el unico mando que sube el conjunto
    //  entero solo sabia bajar.
    //
    //  Y los dieciseis canales SI llegan a +12. O sea que para subir la maquina
    //  habia que subir dieciseis faders uno a uno - y eso no es lo mismo: cada
    //  uno entra en el bus por su lado y la mezcla se mueve. El master llega
    //  ahora al mismo sitio que ellos, que ademas es el numero que ya existe.
    static constexpr float kMasterMaxGain = 3.9810717f;   // +12 dB, el mismo tope que un canal

    void setMasterUser (float g) noexcept
    {
        masterUser.store (juce::jlimit (0.0f, kMasterMaxGain, g), std::memory_order_relaxed);
        refreshMasterTarget();
    }
    float getMasterUser() const noexcept { return masterUser.load (std::memory_order_relaxed); }

    void setDucked (bool on) noexcept
    {
        ducked.store (on, std::memory_order_relaxed);
        refreshMasterTarget();
    }
    bool isDucked() const noexcept { return ducked.load (std::memory_order_relaxed); }

    //  Cuanto se baja por un aviso. Aqui y no en quien lo llama: el numero es
    //  del motor porque la rampa que lo sigue tambien lo es.
    static constexpr float kDuckGain = 0.28f;

    //  El efectivo, que es el producto de los dos. Solo lo miran las pruebas y
    //  el hilo de audio: nadie DECIDE con este numero.
    float getMasterGain() const noexcept { return masterTarget.load (std::memory_order_relaxed); }
    bool isPlaying() const noexcept   { return playing.load (std::memory_order_relaxed); }
    // float, not double: atomic<double> is NOT lock-free on 32-bit ARM, and
    // this is read inside the audio callback (the Android armeabi-v7a build
    // would otherwise take a runtime lock there).
    void setBpm (double b) noexcept   { bpm.store ((float) b, std::memory_order_relaxed); }
    double getBpm() const noexcept    { return (double) bpm.load (std::memory_order_relaxed); }

    //  MUESTRAS POR COMPAS AL TEMPO DE AHORA, y publica porque la cara la
    //  necesita: un clip se guarda en muestras -el audio mide lo que mide- y la
    //  linea de tiempo lo dibuja en compases, asi que alguien tiene que
    //  traducir. Escrita UNA vez y aqui, que es donde viven el tempo y la
    //  frecuencia: la misma cuenta hecha en la cara con `getBpm` y una
    //  frecuencia supuesta es la regla duplicada de siempre, y el sintoma
    //  seria un clip dibujado donde no suena.
    double muestrasPorCompas() const noexcept { return samplesPerStepNow() * (double) kBarSteps; }

    //  EL METRONOMO. Una ayuda para tocar y no parte de la cancion: ver
    //  copyStateFrom, donde deliberadamente NO viaja.
    void setClick (bool on) noexcept { clickOn.store (on, std::memory_order_relaxed); }
    bool isClick() const noexcept    { return clickOn.load (std::memory_order_relaxed); }

    //  LA CUENTA ATRAS, en compases. Mientras dura suena el clic y la cancion
    //  no avanza; al acabar, el transporte arranca solo.
    void armaCuentaAtras (int compases) noexcept
    {
        cuentaPasos.store (juce::jlimit (0, 8, compases) * kBarSteps, std::memory_order_relaxed);
    }
    bool enCuentaAtras() const noexcept { return cuentaPasos.load (std::memory_order_relaxed) > 0; }

    //  Prepara la toma y la deja ARMADA: no graba todavia. El hilo de audio la
    //  arranca en el primer paso que ya no es de la cuenta, o sea en la linea
    //  de compas. Ver renderNextBlock.
    void armaGrabacionEnCuenta (int slot) noexcept
    {
        if (slot < 0 || slot >= kNumPads) return;
        recordSlot = slot;
        recordPos.store (0, std::memory_order_relaxed);
        recordFromMaster.store (false, std::memory_order_release);
        compasGrabado.store (-1, std::memory_order_relaxed);
        grabarTrasCuenta.store (true, std::memory_order_release);
    }
    //  En que compas empezo la toma. Lo apunta quien la arranca, que es el
    //  unico que sabe el instante exacto.
    int getCompasGrabado() const noexcept { return compasGrabado.load (std::memory_order_relaxed); }
    void setStep (int patternIdx, int step, int pad, bool on) noexcept;
    void clearPattern (int patternIdx) noexcept;
    int  getPlayStep() const noexcept { return playStep.load (std::memory_order_relaxed); }
    // How far through the current step we are, 0..1. The UI needs it to
    // quantise a live pad hit to the NEAREST step instead of always the one
    // that happens to be sounding — a hit landing just before the beat would
    // otherwise be written a whole 16th late.
    float getStepPhase() const noexcept { return stepPhase.load (std::memory_order_relaxed); }

    // --- Pattern length (message thread) ---
    //  How many steps (kMinPatLen..kMaxPatLen, i.e. 16..64) play before this
    //  bank loops/hands off to the next chain entry — FL-Studio-style
    //  variable pattern length.
    void setPatternLength (int patternIdx, int len) noexcept;
    int  getPatternLength (int patternIdx) const noexcept;

    // --- Pattern chain (message thread) ---
    //  editPattern is the bank the UI edits/steps; when the chain is empty,
    //  playback simply loops editPattern (legacy single-pattern behaviour).
    //  A non-empty chain plays its pattern banks in order, looping the chain.
    void setEditPattern (int p) noexcept { editPattern.store (juce::jlimit (0, kNumPatterns - 1, p), std::memory_order_relaxed); }
    int  getEditPattern() const noexcept { return editPattern.load (std::memory_order_relaxed); }
    bool addToChain (int patternIdx) noexcept;
    void clearChain() noexcept { chainLength.store (0, std::memory_order_relaxed); }
    int  getChainLength() const noexcept { return chainLength.load (std::memory_order_relaxed); }
    int  getChainSlot (int i) const noexcept { return (i >= 0 && i < kMaxChain) ? chainSlots[(size_t) i].load (std::memory_order_relaxed) : 0; }
    int  getPlayingPattern() const noexcept { return playingPattern.load (std::memory_order_relaxed); }

    // --- Song / playlist (message thread) --------------------------------
    void setSongMode (bool on) noexcept { songMode.store (on, std::memory_order_relaxed); }
    bool isSongMode() const noexcept    { return songMode.load (std::memory_order_relaxed); }
    //  Y EL VALOR TAMBIEN SE ACOTA, no solo los indices.
    //
    //  Esto comprobaba `lane` y `bar` y guardaba `value` tal cual, y ese valor
    //  viene del project.xml (`toks[b].getIntValue()`): un fichero corrupto, o
    //  de otra epoca, metia cualquier entero en una celda. Cada consumidor
    //  tenia entonces que volver a validarlo — el hilo de audio lo hacia
    //  (`cell > 0 && cell <= kNumPatterns`, y `triggerPad` acota el pad) y el
    //  dibujo de la ficha NO, que es como se llego a leer fuera de una tabla.
    //
    //  Un valor es una de cuatro cosas y nada mas: vacio, kContinued, un banco
    //  de patron 1..kNumPatterns, o un golpe suelto -(pad+1). Lo que no encaje
    //  vale VACIO, que es lo unico que no puede sonar ni pintar de nada.
    static bool songCellValido (int v) noexcept
    {
        return v == 0 || v == kContinued
            || (v > 0 && v <= kNumPatterns)
            || (v < 0 && -v <= kNumPads);
    }

    void setSongCell (int lane, int bar, int value) noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return;
        songCell[(size_t) lane][(size_t) bar].store (songCellValido (value) ? value : 0,
                                                     std::memory_order_relaxed);
    }
    int getSongCell (int lane, int bar) const noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return 0;
        return songCell[(size_t) lane][(size_t) bar].load (std::memory_order_relaxed);
    }
    void clearSong() noexcept
    {
        for (auto& lane : songCell) for (auto& c : lane) c.store (0, std::memory_order_relaxed);
        //  Y el silencio por bloque con ellas: vaciar la cancion y dejar los
        //  bits puestos es exactamente la herencia que ya se pago dos veces en
        //  NUEVO - lo que queda parece tuyo. Un bloque escrito donde antes
        //  hubo uno silenciado naceria mudo sin que nadie lo haya pedido.
        for (auto& m : songCellMute) m.store (0, std::memory_order_relaxed);
    }
    void setSongLength (int bars) noexcept { songBars.store (juce::jlimit (1, kSongBars, bars), std::memory_order_relaxed); }
    int  getSongLength() const noexcept    { return songBars.load (std::memory_order_relaxed); }

    //  SILENCIAR UN CARRIL. Cuatro carriles suenan a la vez y hasta ahora la
    //  unica forma de oir uno solo era borrar los otros tres y volver a
    //  escribirlos - o sea, no habia forma. Se lee en el arranque de cada
    //  compas, que es donde el carril decide que patron adopta: silenciar en
    //  mitad de un compas cortaria un patron por la mitad, y lo que se
    //  silencia es una PISTA, no un sonido.
    void setSongLaneMute (int lane, bool m) noexcept
    {
        if (lane < 0 || lane >= kSongLanes) return;
        songLaneMute[(size_t) lane].store (m, std::memory_order_relaxed);
    }
    bool isSongLaneMuted (int lane) const noexcept
    {
        return lane >= 0 && lane < kSongLanes
            && songLaneMute[(size_t) lane].load (std::memory_order_relaxed);
    }

    //  Y EL SILENCIO DE UN BLOQUE SUELTO, que es otra cosa: el del carril
    //  calla la pista entera y este calla UN bloque de la linea de tiempo -
    //  probar una cancion sin ese estribillo sin borrarlo y volver a
    //  escribirlo, que es exactamente para lo que existe en un secuenciador de
    //  patrones.
    //
    //  UN BIT POR COMPAS EN UN `uint64` POR CARRIL, y no una tabla de bool: la
    //  cancion mide SESENTA Y CUATRO compases clavados, asi que caben exactos
    //  en un entero de 64 bits y el hilo de audio lee UNA carga atomica por
    //  compas en vez de recorrer nada. Y con `static_assert`, que el dia que
    //  `kSongBars` deje de valer 64 esto tiene que fallar al compilar y no
    //  silenciar el compas equivocado.
    static_assert (kSongBars <= 64, "el silencio por bloque es un bit por compas en un uint64");

    void setSongCellMute (int lane, int bar, bool m) noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return;
        const juce::uint64 bit = (juce::uint64) 1 << bar;
        auto& v = songCellMute[(size_t) lane];
        const juce::uint64 antes = v.load (std::memory_order_relaxed);
        v.store (m ? (antes | bit) : (antes & ~bit), std::memory_order_relaxed);
    }
    bool isSongCellMuted (int lane, int bar) const noexcept
    {
        if (lane < 0 || lane >= kSongLanes || bar < 0 || bar >= kSongBars) return false;
        return (songCellMute[(size_t) lane].load (std::memory_order_relaxed)
                  >> bar) & 1u;
    }
    juce::uint64 songCellMuteMask (int lane) const noexcept
    {
        return (lane >= 0 && lane < kSongLanes)
                 ? songCellMute[(size_t) lane].load (std::memory_order_relaxed) : 0;
    }
    void setSongCellMuteMask (int lane, juce::uint64 m) noexcept
    {
        if (lane >= 0 && lane < kSongLanes)
            songCellMute[(size_t) lane].store (m, std::memory_order_relaxed);
    }

    //  EL BUCLE DE UN TRAMO. Trabajar en el estribillo de una cancion de
    //  treinta y dos compases queria decir esperar a que diera la vuelta
    //  entera; con el bucle puesto, el transporte da la vuelta al tramo que
    //  estas mirando. Dos compases, no dos pasos: un bucle que empieza a
    //  mitad de compas no es un tramo de una cancion.
    //
    //  De cero a cero significa APAGADO, para que el estado por defecto de un
    //  proyecto viejo sea el de siempre.
    void setSongLoop (int fromBar, int toBar) noexcept
    {
        songLoopA.store (juce::jlimit (0, kSongBars - 1, fromBar), std::memory_order_relaxed);
        songLoopB.store (juce::jlimit (0, kSongBars,     toBar),   std::memory_order_relaxed);
    }
    void clearSongLoop() noexcept { songLoopA.store (0, std::memory_order_relaxed);
                                    songLoopB.store (0, std::memory_order_relaxed); }
    int  getSongLoopFrom() const noexcept { return songLoopA.load (std::memory_order_relaxed); }
    int  getSongLoopTo()   const noexcept { return songLoopB.load (std::memory_order_relaxed); }
    bool hasSongLoop() const noexcept
    { return songLoopB.load (std::memory_order_relaxed) > songLoopA.load (std::memory_order_relaxed); }
    int  getSongBar() const noexcept       { return songBar.load (std::memory_order_relaxed); }

    // --- Piano roll: per-step semitone offset from the pad's own pitch ---
    //  (message thread). Lets one pad's sample play a melody across the
    //  16-step grid instead of one fixed pitch per pad.
    void setStepNote (int patternIdx, int step, int pad, int semis) noexcept;
    //  LAS TRES NOTAS DE MAS. Ver stepChord. La raiz es setStepNote; estas son
    //  las que la acompanan, e `indice` va de 0 a 2. Un semitono fuera de
    //  rango o `puesta = false` apagan esa voz del acorde.
    static constexpr int kExtraNotes = 3;
    void setStepExtra (int patternIdx, int step, int pad, int indice, int semis, bool puesta) noexcept;
    int  getStepExtra (int patternIdx, int step, int pad, int indice) const noexcept;   // -128 = ninguna
    void clearStepExtras (int patternIdx, int step, int pad) noexcept;
    //  Los NUEVE campos de un paso a su defecto, sin tocar si suena o no. Ver
    //  su comentario: habia tres sitios vaciando tres subconjuntos distintos.
    void vaciaPaso (int patternIdx, int step, int pad) noexcept;
    std::uint32_t getStepChordRaw (int patternIdx, int step, int pad) const noexcept
    {
        if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
            || pad < 0 || pad >= kNumPads) return 0;
        return stepChord[(size_t) patternIdx][(size_t) step][(size_t) pad].load (std::memory_order_relaxed);
    }
    void setStepChordRaw (int patternIdx, int step, int pad, std::uint32_t v) noexcept
    {
        if (patternIdx < 0 || patternIdx >= kNumPatterns || step < 0 || step >= kNumSteps
            || pad < 0 || pad >= kNumPads) return;
        stepChord[(size_t) patternIdx][(size_t) step][(size_t) pad].store (v, std::memory_order_relaxed);
    }
    void setStepVel   (int patternIdx, int step, int pad, int vel)  noexcept;
    void setStepRoll  (int patternIdx, int step, int pad, int hits) noexcept;
    //  EL EMPUJON DE CADA PASO, en centesimas de paso y con signo.
    //
    //  El swing mueve las corcheas pares y nada mas: es una regla, y una regla
    //  aplicada a los dieciseis pasos suena a maquina por definicion. Un
    //  humano no llega tarde SIEMPRE lo mismo. Esto guarda cuanto se aparta
    //  CADA paso de su sitio, que es lo que HUMANIZAR escribe y lo que se
    //  puede deshacer, guardar y volver a oir igual - un temblor sorteado en
    //  el hilo de audio suena distinto cada vuelta y no es un groove, es ruido.
    void setStepNudge (int patternIdx, int step, int pad, int centesimas) noexcept;
    int  getStepNudge (int patternIdx, int step, int pad) const noexcept;

    //  EL LARGO DE LA NOTA, EN CUARTOS DE PASO.
    //
    //  Un paso era un disparo y la nota duraba lo que durase la muestra: un
    //  cuadrado, siempre del mismo tamano. Eso vale para percusion y no vale
    //  para nada mas - un bajo, una cuerda, una voz - donde el largo es la
    //  mitad de lo que se escribe, y por eso un piano roll dibuja BARRAS y no
    //  casillas.
    //
    //  En CUARTOS de paso y no en pasos, que es la otra mitad del problema:
    //  medido en pasos, lo mas corto que se puede escribir es la rejilla, asi
    //  que para una nota mas corta habria que cambiar la rejilla del patron
    //  ENTERO - y entonces el sitio donde va la nota deja de estar donde
    //  estaba. Con cuartos, una nota puede durar un cuarto de casilla sin que
    //  el resto del patron se entere.
    //
    //  Cero es SUELTA: la nota dura lo que dure la muestra, que es como nace un
    //  pad y como sonaba todo hasta ahora. Un patron viejo vale cero en todas
    //  sus casillas y suena exactamente igual.
    static constexpr int kLenSuelto = 0;
    static constexpr int kLenMax    = 63;    // 15.75 pasos, casi un compas
    void setStepLen (int patternIdx, int step, int pad, int cuartos) noexcept;
    int  getStepLen (int patternIdx, int step, int pad) const noexcept;

    //  EL BLOQUEO DE PARAMETRO: el corte del filtro, guardado PASO A PASO.
    //
    //  Un filtro por pad es un ajuste; un filtro que cambia en cada paso es
    //  una linea de bajo que se abre y se cierra sola, y esa es la diferencia
    //  entre una caja de ritmos y un secuenciador. El paso que lleva bloqueo
    //  escribe el corte del pad al dispararse, asi que se queda puesto hasta
    //  que otro paso diga otra cosa - que es exactamente como suena en la
    //  maquina donde esto se inventó.
    //
    //  Se guarda en HERCIOS/100 y no en un indice de una tabla: una tabla es
    //  una segunda fuente de verdad que hay que mantener a los dos lados, y el
    //  numero cabe igual - 20 Hz a 20 kHz son 0 a 200 en un byte con signo si
    //  se guarda el porcentaje del recorrido, que es lo que hace el mando.
    //  -1 = este paso no toca el filtro.
    static constexpr int kNoLock = -1;
    void setStepLock (int patternIdx, int step, int pad, int porCiento) noexcept;
    int  getStepLock (int patternIdx, int step, int pad) const noexcept;

    //  Y LOS OTROS CUATRO BLOQUEOS: ataque, caida, punto de inicio y pan.
    //
    //  El bloqueo del corte se aplica escribiendo el parametro DEL PAD, y no
    //  habia otro sitio: el filtro se calcula una vez por bloque, fuera de la
    //  voz. Estos cuatro los consume `Voice::start`, asi que viajan CON el
    //  disparo y no tocan el pad. La diferencia no es de estilo - escribir el
    //  pad mueve su mando solo, que es exactamente el fallo que ya costo una
    //  medida en la audicion del piano ("oir una tecla no puede afinar el
    //  pad") - y ademas un paso sin bloqueo suena como el pad y no como el
    //  ultimo paso que lo movio.
    //
    //  Los cuatro van EMPAQUETADOS en un uint32, un byte cada uno, por lo
    //  mismo que el acorde: cuatro tablas de 16 KB son cuatro sitios que
    //  vaciar, cuatro que copiar y cuatro listas dispersas en el fichero de
    //  proyecto. Cada byte vale 0 = este paso no toca eso, 1..101 = 0..100 %
    //  del recorrido del mando, con el desplazamiento de uno por la misma
    //  razon que setStepLock: cero es lo que vale un patron viejo.
    enum PLock { plockAtaque = 0, plockCaida, plockInicio, plockPan, kNumPLocks };
    static constexpr int kNoPLock = -1;
    void setStepPLock (int patternIdx, int step, int pad, int cual, int porCiento) noexcept;
    int  getStepPLock (int patternIdx, int step, int pad, int cual) const noexcept;
    //  El paquete entero, para el fichero de proyecto y para copiar filas: sin
    //  esto habria que preguntar cuatro veces por casilla y volver a empaquetar
    //  fuera, que es la segunda fuente de verdad de siempre.
    std::uint32_t getStepPLockRaw (int patternIdx, int step, int pad) const noexcept;
    void setStepPLockRaw (int patternIdx, int step, int pad, std::uint32_t v) noexcept;

    //  De porcentaje del recorrido al valor que espera la voz. Los recorridos
    //  son los MISMOS que los mandos del pad (ATAQUE 0..200 ms, CAIDA 1..800,
    //  PAN -1..1), escritos aqui porque los usan el motor y la interfaz y dos
    //  copias se separan.
    static float plockAtaqueMs (int pct) noexcept { return (float) (juce::jlimit (0, 100, pct) * 2.0); }
    static float plockCaidaMs  (int pct) noexcept { return (float) (1.0 + juce::jlimit (0, 100, pct) * 7.99); }
    static float plockPanPos   (int pct) noexcept { return (float) (juce::jlimit (0, 100, pct) / 50.0 - 1.0); }

    //  De porcentaje del recorrido a hercios, con la MISMA curva por octavas
    //  que usa el mando: 20 Hz a kFiltOpenHz con el punto medio en 1000, que
    //  es donde el oido pone la mitad. Escrita aqui y no en la interfaz porque
    //  la usan los dos y dos copias se separan.
    static float lockToHz (int porCiento) noexcept
    {
        const double t = juce::jlimit (0.0, 1.0, (double) porCiento / 100.0);
        //  20 * (kFiltOpenHz/20)^t da las octavas repartidas por igual.
        return (float) (20.0 * std::pow ((double) kFiltOpenHz / 20.0, t));
    }
    static int hzToLock (float hz) noexcept
    {
        const double r = juce::jlimit (20.0, (double) kFiltOpenHz, (double) hz);
        return (int) std::lround (100.0 * std::log (r / 20.0) / std::log ((double) kFiltOpenHz / 20.0));
    }
    int  getStepVel   (int patternIdx, int step, int pad) const noexcept;
    int  getStepRoll  (int patternIdx, int step, int pad) const noexcept;

    //  0.5 straight .. 0.75 hard shuffle.
    void  setSwing (float s) noexcept { swing.store (juce::jlimit (0.5f, 0.75f, s), std::memory_order_relaxed); }
    float getSwing() const noexcept   { return swing.load (std::memory_order_relaxed); }
    int  getStepNote  (int patternIdx, int step, int pad) const noexcept;

    // UI feedback: bitmask of pads triggered since the last call (taps + sequencer).
    //  One bit per pad, and there are sixty-four of them: a uint32 silently
    //  dropped every pad in banks C and D, and `1u << 40` is undefined
    //  behaviour rather than a lost flash.
    std::uint64_t fetchTriggered() noexcept { return triggeredMask.exchange (0, std::memory_order_relaxed); }

    // --- Master FX: filter + drive (message thread setters) ---
    void setDlyTime  (float ms)  noexcept { dlyTime.store  (ms,  std::memory_order_relaxed); }
    void setDlyFb    (float f)    noexcept { dlyFb.store    (f,   std::memory_order_relaxed); }
    void setDlyMix   (float m)    noexcept { dlyMix.store   (m,   std::memory_order_relaxed); }
    //  Para el banco: lo que la automatizacion acaba de escribir. Sin un
    //  getter, «el evento llego» solo se puede mirar por el sonido, y ahi un
    //  cambio de mezcla del delay tarda su cola en notarse.
    float getDlyMix() const noexcept { return dlyMix.load (std::memory_order_relaxed); }

    // --- The six effects -------------------------------------------------
    //  FLT, HPF, DRIVE, DELAY, CRUSH, REVERB. Six independent stages in that
    //  order, three parameters each, and the third is always MIX. MIX is the
    //  switch as well as the amount: at zero the stage is skipped outright,
    //  so an effect you are not using costs nothing and cannot colour the
    //  sound. Nothing is shared between stages, which is what lets FLT and
    //  HPF run at once as a band-pass instead of fighting over one filter.

    //  UN FILTRO SE BARRE EN LAS DOS DIRECCIONES, O NO ES UN FILTRO DE TOCAR.
    //
    //  Esto era ISO: un paso bajo con la frecuencia de corte de 20 a 20000 Hz.
    //  Un solo sentido. Puesto en el eje de un panel XY no dice nada - todo el
    //  recorrido hacia un lado abre y no pasa nada, y hacia el otro cierra
    //  hasta el silencio - cuando lo que hace cualquiera con un filtro en
    //  directo es salir del centro hacia arriba O hacia abajo y volver.
    //
    //  Asi que el parametro es un BARRIDO de -1 a +1 con el centro NEUTRO:
    //  negativo cierra por arriba (paso bajo bajando desde 20 kHz), positivo
    //  abre por abajo (paso alto subiendo desde 20 Hz), y en el centro la
    //  etapa no procesa nada. Es el filtro de una mesa de DJ, que es el gesto
    //  que esto imita, y en un eje X cada mitad significa algo.
    //
    //  La zona muerta central no es adorno: sin ella, "centro" es un valor
    //  exacto que un dedo no acierta, y el filtro nunca queda del todo fuera.
    //  CUANTIZAR EL DISPARO EN DIRECTO.
    //
    //  Un pad tocado con el transporte rodando sonaba en el instante exacto en
    //  que el dedo tocaba el cristal. La grabacion SI se cuantizaba - ver
    //  padClicked - asi que lo que oias y lo que quedaba escrito eran dos
    //  cosas distintas: tocabas fuera y al reproducir estaba dentro.
    //
    //  Con esto puesto, el disparo espera al 1/16 mas cercano, igual que hace
    //  la grabacion, y lo que suena es lo que se graba. Se cuenta en la misma
    //  cola de golpes pendientes que ya usan los redobles: nada nuevo en el
    //  hilo de audio.
    //  BOMBEO. Un pad hace agacharse a todo lo demas cada vez que suena, y se
    //  recupera solo. Es el sidechain de manual: el bombo abre hueco y el resto
    //  respira a su ritmo, que en el genero al que apunta esta caja no es un
    //  efecto sino la textura.
    //
    //  Se aplica al master ENTERO, incluido el pad que lo dispara. Excluirlo
    //  exigiria un bus aparte para el, y en la practica un bombo agachandose a
    //  si mismo un instante suena como un bombo comprimido - que es lo que
    //  todo el mundo pone despues de todas formas.
    void setDuckPad    (int pad)   noexcept { duckPad.store (pad, std::memory_order_relaxed); }
    int  getDuckPad()        const noexcept { return duckPad.load (std::memory_order_relaxed); }
    void setDuckAmount (float a)   noexcept { duckAmt.store (a, std::memory_order_relaxed); }
    float getDuckAmount()    const noexcept { return duckAmt.load (std::memory_order_relaxed); }
    void setDuckRelease (float ms) noexcept { duckRel.store (ms, std::memory_order_relaxed); }
    float getDuckRelease()   const noexcept { return duckRel.load (std::memory_order_relaxed); }

    //  LA REJILLA: cuanto dura un paso, medido en negras. 0.25 son
    //  semicorcheas, que es lo que habia y sigue siendo lo normal; 1/6 y 1/12
    //  son los tresillos, que antes no se podian escribir de ninguna manera.
    //  Es del transporte entero y no de cada patron, igual que el tempo: dos
    //  patrones encadenados con rejillas distintas no son un groove, son un
    //  fallo de sincronia esperando a que alguien encadene.
    void  setStepBeats (float b) noexcept { stepBeats.store (juce::jlimit (0.02f, 4.0f, b), std::memory_order_relaxed); }
    float getStepBeats() const noexcept   { return stepBeats.load (std::memory_order_relaxed); }

    void setLiveQuantise (bool on) noexcept { liveQuant.store (on, std::memory_order_relaxed); }
    bool getLiveQuantise() const noexcept { return liveQuant.load (std::memory_order_relaxed); }

    void setFltSweep (int canal, float s) noexcept
    { fxParamDe (canal, kFxFlt, 0).store (s, std::memory_order_relaxed); }

    //  DONDE CAE EL BARRIDO, escrito UNA vez.
    //
    //  El reparto es exponencial y con zona muerta -el oido oye octavas, y el
    //  centro no procesa- y esos tres numeros vivian sueltos dentro de la etapa
    //  del hilo de audio. Desde que la fila del rack DIBUJA lo que el efecto
    //  hace, hay un segundo cliente, y una regla escrita dos veces son dos
    //  reglas: el dia que se moviera el recorrido, el dibujo seguiria contando
    //  el de ayer sin que nada fallara.
    struct Barrido { bool activo; bool alto; float hz; };
    static Barrido barridoDe (float sweep) noexcept
    {
        constexpr float kDead = 0.03f;
        const float s   = juce::jlimit (-1.0f, 1.0f, sweep);
        const float mag = std::abs (s);
        if (mag <= kDead) return { false, false, 0.0f };

        const float t = (mag - kDead) / (1.0f - kDead);            // 0..1
        return s < 0.0f
             //  Cerrando por arriba: de 20 kHz a 90 Hz.
             ? Barrido { true, false, 20000.0f * std::pow (90.0f / 20000.0f, t) }
             //  Abriendo por abajo: de 20 Hz a 6 kHz.
             : Barrido { true, true,     20.0f * std::pow (6000.0f /  20.0f, t) };
    }
    //  ------------------------------------------------------------------
    //  LO QUE EL VISOR DEL PLATO TIENE QUE PODER PREGUNTAR.
    //
    //  `barridoDe` salio de la etapa el dia que la fila del rack dibujo lo que
    //  el efecto hace, con este argumento: «una regla escrita dos veces son dos
    //  reglas». Lo que no se hizo entonces fue mirar las otras diez, y medidas
    //  salio esto: FLT y HPF se DIBUJABAN con un polo de 6 dB/oct mientras el
    //  motor es un SVF de 12 con resonancia -mas un pico gaussiano inventado en
    //  el visor-, y DRV y BIT llevaban la formula del motor COPIADA caracter
    //  por caracter, o sea funcionando hoy y dibujando la de ayer el dia que
    //  alguien moviera el 24.0f o el *0.5f. Las cuatro pasan a salir de aqui.
    //  ------------------------------------------------------------------

    //  EL tanh DE LA CASA, que vivia en un namespace anonimo del .cpp y por
    //  tanto no se podia dibujar. Importa que sea EL MISMO y no `std::tanh`:
    //  es un Pade acotado en +-5, y esa cota es la barrera que impide que una
    //  muestra de 1e30 apague la maquina entera. Un visor con `std::tanh`
    //  dibujaria una curva que el motor no hace.
    static float fastTanh (float x) noexcept
    {
        //  FUERA DE RANGO, ANTES DE ELEVAR AL CUADRADO: el cuadrado de 1e30 no
        //  cabe en un float, e inf/inf es NaN — y `jlimit` no lo tapa, porque
        //  comparar con NaN siempre es falso.
        if (! std::isfinite (x)) return 0.0f;
        if (x >  5.0f) return  1.0f;
        if (x < -5.0f) return -1.0f;

        const float x2 = x * x;
        const float a  = x  * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float b  = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return juce::jlimit (-1.0f, 1.0f, a / b);
    }

    //  LA SATURACION DE DRV, con sus dos numeros. `mk` compensa por la
    //  ganancia que ENTRA y no por el techo del tanh: `tanh(k)` vale ~1 para
    //  cualquier k util, asi que aquel «makeup» no hacia nada y DRIVE al 70 %
    //  salia tres veces mas alto que el seco.
    struct Drive { float k, mk; };
    static Drive driveDe (float drive) noexcept
    {
        const float d = juce::jlimit (0.0f, 1.0f, drive);
        return { 1.0f + d * 24.0f, 1.0f / (1.0f + d * 2.5f) };
    }
    static float saturaDe (float x, Drive dr) noexcept { return fastTanh (dr.k * x) * dr.mk; }

    //  BIT: cuantos escalones de amplitud, y el retenedor.
    static float nivelesDe (float bits) noexcept
    {
        return juce::jmax (1.0f, std::pow (2.0f, juce::jlimit (1.0f, 16.0f, bits)) * 0.5f);
    }
    //  Y EL RETENEDOR ENTERO, no solo el cuantizador. Las dos mitades de un
    //  crusher son la amplitud y el TIEMPO, y el visor dibujaba solo la
    //  primera: RATE se movia y el dibujo no decia nada. La fase es del EFECTO
    //  y no del canal -por eso viaja por referencia y el motor la guarda
    //  despues del primer canal-, que es lo que mantiene los dos canales
    //  retenidos a la vez y lo que hace que suene un crusher y no dos.
    static void crush (float* w, int n, float levels, float step,
                       float& fase, float& hold) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            fase += 1.0f;
            if (fase >= step) { fase -= step; hold = std::round (w[i] * levels) / levels; }
            w[i] = hold;
        }
    }

    //  EL MODULO DE UN SVF DE SEGUNDO ORDEN, que es lo que FLT y HPF SON.
    //
    //  `juce::dsp::StateVariableTPTFilter` con `setResonance(q)` deja
    //  `R2 = 1/q` y `g = tan(pi fc/fs)`, y su respuesta es exactamente la del
    //  prototipo analogico con la frecuencia PREDISTORSIONADA: con
    //  `x = tan(pi f/fs) / tan(pi fc/fs)`, el paso bajo es
    //  `1 / |1 - x^2 + j x/q|` y el alto ese mismo modulo por `x^2`. Doce
    //  decibelios por octava y el pico de resonancia SALE DE LA FORMULA, que
    //  es lo que el visor dibujaba con una gaussiana inventada.
    //
    //  `fs` por parametro y con defecto de 48 kHz: el visor no tiene motor al
    //  que preguntarle la ruta, y a 44.1 kHz la diferencia solo se ve pegada a
    //  Nyquist. El banco mide a 48 k, que es donde las dos cuentas coinciden.
    static float svfDb (float hz, float corte, float q, bool alto,
                        float fs = 48000.0f) noexcept
    {
        const float nyq = fs * 0.5f;
        const float f   = juce::jlimit (1.0f, nyq * 0.999f, hz);
        const float fc  = juce::jlimit (1.0f, nyq * 0.999f, corte);
        const float w   = std::tan (juce::MathConstants<float>::pi * f  / fs);
        const float wc  = std::tan (juce::MathConstants<float>::pi * fc / fs);
        const float x   = w / juce::jmax (1.0e-9f, wc);
        const float re  = 1.0f - x * x;
        const float im  = x / juce::jmax (0.05f, q);
        const float den = std::sqrt (re * re + im * im);
        const float mag = (alto ? x * x : 1.0f) / juce::jmax (1.0e-6f, den);
        return juce::Decibels::gainToDecibels (juce::jmax (1.0e-5f, mag));
    }

    //  A QUE CANAL VA ESTE PAD. Es lo unico que el pad decide del reparto: el
    //  cuanto lo dice el canal.
    void setPadCanal (int slot, int canal) noexcept
    {
        if (slot < 0 || slot >= kNumPads) return;
        padCanal[(size_t) slot].store ((juce::uint8) juce::jlimit (0, kNumCanales - 1, canal),
                                       std::memory_order_relaxed);
        refrescaSendMask();
    }
    int getPadCanal (int slot) const noexcept
    {
        if (slot < 0 || slot >= kNumPads) return 0;
        return (int) padCanal[(size_t) slot].load (std::memory_order_relaxed);
    }

    //  CUANTO MANDA UN CANAL A UN EFECTO. Es el envio de verdad, y lo que la
    //  fila del RACK mueve.
    void setCanalSend (int canal, int fx, float v) noexcept
    {
        if (canal < 0 || canal >= kNumCanales || fx < 0 || fx >= kNumFx) return;
        canalSend[(size_t) canal][(size_t) fx].store (juce::jlimit (0.0f, 1.0f, v),
                                                      std::memory_order_relaxed);
        refrescaSendMask();
    }
    float getCanalSend (int canal, int fx) const noexcept
    {
        if (canal < 0 || canal >= kNumCanales || fx < 0 || fx >= kNumFx) return 0.0f;
        return canalSend[(size_t) canal][(size_t) fx].load (std::memory_order_relaxed);
    }

    //  EL FADER Y EL MUTE DEL CANAL, que es lo que lo hace un canal de mesa y
    //  no una etiqueta. Cuestan dos multiplicaciones: `dryGain[p]` y
    //  `sendGain[p][f]` ya se calculan una vez por bloque, asi que la ganancia
    //  del canal las multiplica ahi y no hay una etapa nueva en el hilo de
    //  audio.
    //
    //  Y SOLO se queda en el PAD. Acaba de entrar en la cara como modo hermano
    //  de REC y `effectiveGain` lo resuelve en una lectura; dos ambitos de solo
    //  son dos respuestas a «oye solo esto».
    void setCanalGain (int canal, float g) noexcept
    {
        if (canal < 0 || canal >= kNumCanales) return;
        canalGain[(size_t) canal].store (juce::jlimit (0.0f, 4.0f, g), std::memory_order_relaxed);
    }
    float getCanalGain (int canal) const noexcept
    {
        if (canal < 0 || canal >= kNumCanales) return 1.0f;
        return canalGain[(size_t) canal].load (std::memory_order_relaxed);
    }
    void setCanalMute (int canal, bool m) noexcept
    {
        if (canal < 0 || canal >= kNumCanales) return;
        canalMute[(size_t) canal].store (m, std::memory_order_relaxed);
    }
    bool getCanalMute (int canal) const noexcept
    {
        if (canal < 0 || canal >= kNumCanales) return false;
        return canalMute[(size_t) canal].load (std::memory_order_relaxed);
    }

    //  EL RECORTE DEL PAD, que es el `padSend` de siempre con otro papel.
    //
    //  Dejo de ser EL ENVIO el dia que el canal paso a serlo, y sobrevive
    //  porque es lo unico que hace posible abrir un proyecto anterior sonando
    //  igual: un fichero con `sends` trae 64 filas distintas y no hay forma de
    //  meterlas en dieciseis canales sin cambiar como suena. Nace en UNO
    //  -neutro- y NO tiene mando: no es un control, es parte del formato viejo.
    //  Por eso `applyState` lo escribe desde `sends` y `captureState` ya no
    //  escribe esa propiedad — un fichero nuevo no la trae, y «sin la propiedad
    //  vale uno» es exactamente la respuesta que hace falta.
    void setPadRecorte (int slot, int fx, float v) noexcept
    {
        if (slot < 0 || slot >= kNumPads || fx < 0 || fx >= kNumFx) return;
        padRecorte[(size_t) slot][(size_t) fx].store (juce::jlimit (0.0f, 1.0f, v),
                                                      std::memory_order_relaxed);
        refrescaSendMask();
    }
    float getPadRecorte (int slot, int fx) const noexcept
    {
        if (slot < 0 || slot >= kNumPads || fx < 0 || fx >= kNumFx) return 1.0f;
        return padRecorte[(size_t) slot][(size_t) fx].load (std::memory_order_relaxed);
    }

    //  CUANTO DE ESTE PAD LLEGA A ESTE EFECTO, que es el producto de los dos y
    //  lo unico que el hilo de audio pregunta. Escrito UNA vez: la cara lo usa
    //  para dibujar y el motor para repartir, y las dos cuentas escritas por
    //  separado serian dos reglas.
    float sendDePad (int slot, int fx) const noexcept
    {
        return getCanalSend (getPadCanal (slot), fx) * getPadRecorte (slot, fx);
    }

    //  QUE HACE ESE FADER, PARA QUIEN LO DIBUJA.
    //
    //  De los veintiun tipos, DIECISEIS son insertos: `renderNextBlock` hace
    //  `if (fxSustituye[f]) dry *= (1.0f - g)`, o sea que subir su envio le
    //  QUITA senal seca al pad. Solo DLY y REV suman encima. Los dos caminos
    //  mandan igual -una copia escalada entra en el bus y el bus vuelve al
    //  master a nivel pleno-; la unica diferencia es esa resta.
    //
    //  El rack dibujaba las once filas identicas y las titulaba «cuanto de
    //  este pad entra en cada efecto», que describe un envio y por tanto es
    //  falso en nueve de once. Esto es la puerta por la que la cara pregunta
    //  cual de las dos cosas esta dibujando, para no escribir la tabla dos
    //  veces: una regla escrita dos veces son dos reglas.
    static bool sustituye (int fx) noexcept
    {
        return fx >= 0 && fx < kNumFx && fxSustituye[fx];
    }

    void setRevSize (float s) noexcept { rvSize.store (s, std::memory_order_relaxed); }
    void setRevDamp (float d) noexcept { rvDamp.store (d, std::memory_order_relaxed); }
    void setRevMix  (float m) noexcept { rvMix.store  (m, std::memory_order_relaxed); }

    //  EL EQ. Sus diez numeros no pasan por atomicos uno a uno: `Eq5` guarda
    //  los cinco pares y una bandera `sucio`, y el hilo de audio recalcula los
    //  coeficientes en el bloque siguiente. Ver la cabecera de Eq5.h.
    //  CON CANAL, que es la queja entera de esta tanda dicha en una firma: el
    //  EQ es un INSERTO, asi que hay uno por canal y preguntar sin decir cual
    //  es preguntar por el cero. `insDe` acota y es la unica que lo hace.
    void setEqBand  (int canal, int b, float hz, float dB) noexcept { insDe (canal).eqFx.ponBanda (b, hz, dB); }
    void setEqTipo  (int canal, int b, int t)   noexcept { insDe (canal).eqFx.ponTipo (b, t); }
    void setEqQ     (int canal, int b, float q) noexcept { insDe (canal).eqFx.ponQ (b, q); }
    int   getEqTipo (int canal, int b) const noexcept { return (int) insDe (canal).eqFx.tipoDe (b); }
    float getEqQ    (int canal, int b) const noexcept { return insDe (canal).eqFx.qDe (b); }
    float getEqFreq (int canal, int b) const noexcept { return insDe (canal).eqFx.freqDe (b); }
    float getEqGain (int canal, int b) const noexcept { return insDe (canal).eqFx.gainDe (b); }
    void setEqMix   (int canal, float m) noexcept
    { fxParamDe (canal, kFxEq, 2).store (m, std::memory_order_relaxed); }
    // How much silence a bounce must keep past the last note so the tail is
    // not guillotined. Only AUDIBLE stages count — a ten-second delay with
    // its mix at zero must not pad every export.
    double getFxTailSeconds() const noexcept
    {
        double t = 0.0;
        if (dlyMix.load (std::memory_order_relaxed) > 0.001f)
            t = juce::jmax (t, 4.0 * (double) dlyTime.load (std::memory_order_relaxed) * 0.001);
        if (rvMix.load (std::memory_order_relaxed) > 0.001f)
            t = juce::jmax (t, 3.0);
        return t;
    }

    // --- Scope (message thread): copy the last n post-FX master samples ---
    void copyScope (float* dst, int n) noexcept;

    //  Y LOS DOS DEL EFECTO QUE LA CARA ESTA MIRANDO, del hilo de mensajes y
    //  por el mismo camino. Ver `miraFx`.
    void copyFxScope (float* pre, float* post, int n) noexcept;

    //  DINAMICA. Los tres de cada uno, y la reduccion que se lee. Los cuatro
    //  SUSTITUYEN, asi que son de un canal como el EQ.
    void setDynP0  (int canal, int i, float v) noexcept { setFxParam (canal, dynIdx (i), 0, v); }
    void setDynP1  (int canal, int i, float v) noexcept { setFxParam (canal, dynIdx (i), 1, v); }
    void setDynMix (int canal, int i, float v) noexcept { setFxParam (canal, dynIdx (i), 2, v); }
    float getDynP0 (int canal, int i) const noexcept
    { return juce::isPositiveAndBelow (i, 4) ? fxParamDe (canal, dynIdx (i), 0).load (std::memory_order_relaxed) : 0.0f; }
    float getDynP1 (int canal, int i) const noexcept
    { return juce::isPositiveAndBelow (i, 4) ? fxParamDe (canal, dynIdx (i), 1).load (std::memory_order_relaxed) : 0.0f; }
    float getDynReduccion (int canal, int i) const noexcept
    {
        return juce::isPositiveAndBelow (i, 4)
             ? insDe (canal).dynRed[(size_t) i].load (std::memory_order_relaxed) : 0.0f;
    }

    //  MODULACION. La fase de AHORA del tipo `f`, o -1 si ese tipo no lleva
    //  LFO. Es lo que hace visible el mando RATE: su eje se mide en PERIODOS,
    //  asi que la curva no puede enseñarlo -lo declara `FxVisor::mandosDe`- y
    //  quien lo enseña es el punto de trabajo viajando a la velocidad de
    //  verdad. Hermana de `getDynReduccion`, y por lo mismo.
    float getLfoFase (int canal, int f) const noexcept
    {
        //  TRM SUSTITUYE, asi que su LFO vive en `Inserto` y los otros tres
        //  -que SUMAN- en la clase. La familia se parte 3/1 y esta es la unica
        //  puerta que tiene que saberlo.
        if (f == kFxTrm) return insDe (canal).trmFase.load (std::memory_order_relaxed);
        const int m = modDe (f);
        return (m >= 0 && m < kNumModEnvio)
             ? modFase[(size_t) m].load (std::memory_order_relaxed) : -1.0f;
    }
    //  Si el bus MIRADO ha dado señal hace poco. Sin esto la cara no sabe
    //  distinguir «nada suena» de «nada pasa por aqui», y una mancha clavada en
    //  el suelo se lee como un fallo.
    bool fxScopeVivo() const noexcept { return mirHot.load (std::memory_order_relaxed) > 0; }

    //  QUE EFECTO ESTA MIRANDO LA CARA, y por tanto de cual se captura.
    //
    //  El plato enseña UN visor a la vez -el del efecto que tengas enfocado- y
    //  la curva grande del EQ es el mismo sitio con otro inquilino. Asi que
    //  capturar los once seria pagar diez anillos y diez analisis para dibujar
    //  uno: se captura el que se mira y ya. -1 es «ninguno», que es lo que vale
    //  con una ficha abierta encima o con el plato en su fila de mandos.
    //
    //  Y al cambiar de efecto se apaga el testigo: los anillos siguen llenos
    //  con las muestras del ANTERIOR durante un bloque, y dibujarlas seria
    //  enseñar el eco de un delay dentro del visor de una puerta de ruido.
    void miraFx (int f) noexcept
    {
        const int nuevo = (f >= 0 && f < kNumFx) ? f : -1;
        if (mirado.exchange (nuevo, std::memory_order_relaxed) != nuevo)
            mirHot.store (0, std::memory_order_relaxed);
    }
    int fxMirado() const noexcept { return mirado.load (std::memory_order_relaxed); }

    //  Min/max columns spanning ~0.74 s of master output, oldest first.
    //  Returns how many were written. See renderNextBlock section 5c.
    int  copyScopeColumns (float* dstMin, float* dstMax, int n) noexcept;
    static constexpr int kMaxScopeColumns = 256;

    // --- Output peak meters (message thread): max |sample| since last read ---
    float readOutPeakL() noexcept { return outPeakL.exchange (0.0f, std::memory_order_relaxed); }
    float readOutPeakR() noexcept { return outPeakR.exchange (0.0f, std::memory_order_relaxed); }

    //  Y EL DEL CANAL QUE LA CARA ESTA MIRANDO, con la forma de `miraFx`.
    //
    //  Un medidor por canal en la cara era la ultima fila viva de la lente del
    //  productor -«hay VU de master; el nivel de un pad solo se ve en la
    //  mesa»- y desde que el canal es lo que pasa por los efectos no hay forma
    //  de ver uno trabajando sin abrir la mesa, que tapa la rejilla.
    //
    //  No hay bus por canal que medir: el canal es un RE-INDICE de las
    //  ganancias, que es justo lo que hace barato el reparto. Asi que el pico
    //  se saca de `padScratch`, que es donde un pad ya suena solo, y **solo de
    //  los pads del canal MIRADO**: la cara enseña UNO a la vez -el del pad
    //  elegido- asi que medir los dieciseis seria pagar sesenta y cuatro
    //  barridos para dibujar uno. Es la cuenta de `miraFx` con otra pieza.
    //
    //  El precio es que esos pads toman el camino LARGO aunque no manden a
    //  nadie. Son cuatro de sesenta y cuatro de media y esta medido en
    //  `Tests/Cpu.cpp` contra la fila de al lado; -1 -que es lo que vale con
    //  una ficha abierta encima o sin cara- no cuesta nada.
    void miraCanal (int c) noexcept
    {
        canalMirado.store ((c >= 0 && c < kNumCanales) ? c : -1, std::memory_order_relaxed);
    }
    int getCanalMirado() const noexcept { return canalMirado.load (std::memory_order_relaxed); }

    //  Max |muestra| del canal mirado desde la ultima lectura, como los dos de
    //  arriba: la balistica es de la cara y el motor solo dice el pico.
    float readCanalPico() noexcept { return canalPico.exchange (0.0f, std::memory_order_relaxed); }

    // --- Offline bounce (message thread) --------------------------------
    //  An export does NOT render through this engine. It builds a SECOND
    //  engine, copies the whole machine into it and drives that one from a
    //  background thread as fast as the CPU allows. Two reasons: the live
    //  audio callback is never disturbed (you can keep playing while a
    //  bounce runs), and the render is not tied to real time — three minutes
    //  of music does not take three minutes to write.
    void setOffline (bool o) noexcept { offlineMode = o; }

    //  Copies every parameter, pattern, note, chain slot and song cell.
    //  Samples are NOT copied here: the caller publishes the same
    //  ref-counted buffers, so a bounce costs no extra audio memory.
    void copyStateFrom (const AudioEngine& src) noexcept;

    //  How long a bounce of the CURRENT mode would be, in 16th-note steps:
    //  the song's bars, or the chain's patterns end to end, or just the
    //  pattern being edited. Zero means there is nothing to render.
    int  lengthInSteps() const noexcept;

    //  Is there actually a note anywhere in what would be rendered? A pattern
    //  bank always has a LENGTH, so length alone would happily export two
    //  bars of silence.
    bool hasContentToRender() const noexcept;

    // --- Latency probe (message thread) ----------------------------------
    //  The only honest way to know what the round trip really is: emit a
    //  click at a frame we chose, record the microphone through the same
    //  callback, and measure how far apart the two ended up. Everything the
    //  device reports is a claim; this is a measurement.
    void  startLatencyProbe() noexcept;
    bool  isProbing() const noexcept { return probing.load (std::memory_order_acquire); }
    //  Milliseconds from emitting to hearing, or -1 if nothing was heard.
    float finishLatencyProbe() const noexcept;

    // --- Recording (message thread) ---
    //  How long a take can be, and how many channels it keeps. Sixty seconds
    //  stereo at 48 kHz is 23 MB, which a phone that is already holding
    //  sixteen samples will not notice, and it is the difference between
    //  sampling a phrase and sampling a hit.
    //  Sixty seconds of stereo float at 48 kHz is 23 MB, which a phone that
    //  is already holding sixteen samples will not notice - unless it is an
    //  entry-level phone, which is why the device decides (see DeviceTier).
    void setRecordLimit (double seconds, bool allowStereo) noexcept
    {
        recordSeconds = juce::jlimit (5.0, 300.0, seconds);
        recordAllowStereo = allowStereo;
    }
    float getRecordLimitSeconds() const noexcept { return (float) recordSeconds; }
    int   getRecordChannels() const noexcept { return juce::jmax (1, recordBuffer.getNumChannels()); }

    //  EL REBOTE EN VIVO: la cancion suena y lo que suena se escribe.
    //
    //  Se pidio «opcion de exportar en Live, con un count in para no perder el
    //  tiempo». MASTER y PISTAS son los dos OFFLINE -un motor clonado fuera de
    //  tiempo real, tres minutos de musica en un par de segundos- y REMUESTREAR
    //  es un rebote en vivo pero a un PAD, no a un fichero. Ningun camino de
    //  exportacion sonaba, y ninguno tenia cuenta atras.
    //
    //  Se captura DONDE EL REMUESTREO y por lo mismo: despues de los efectos y
    //  del saturador del master, y ANTES del fader y del ducking. Bajar el
    //  master para no despertar a nadie es monitorizacion y dura un segundo; el
    //  fichero que mandas no puede llevarlo impreso.
    //
    //  El hilo de audio solo EMPUJA a un anillo. Reservar, abrir un fichero o
    //  escribir en disco desde aqui es lo que esta casa no hace nunca.
    void vivoArma (int segundos) noexcept
    {
        //  El anillo se dimensiona AQUI y no en el callback. Dos segundos es
        //  mucho mas de lo que el hilo escritor puede tardar en volver, y son
        //  768 KB: la memoria del rebote sigue siendo constante y no depende de
        //  lo que dure la cancion, que es lo que este diseño existe para tener.
        const int n = juce::jmax (4096, (int) (systemSampleRate * juce::jlimit (1, 8, segundos)));
        vivoRing.setSize (2, n);
        vivoRing.clear();
        vivoFifo.setTotalSize (n);
        vivoFifo.reset();
        vivoPerdidas.store (0, std::memory_order_relaxed);
        vivoOn.store (true, std::memory_order_release);
    }

    void vivoPara() noexcept { vivoOn.store (false, std::memory_order_release); }
    bool vivoArmado() const noexcept { return vivoOn.load (std::memory_order_acquire); }
    int  vivoTiradas() const noexcept { return vivoPerdidas.load (std::memory_order_relaxed); }

    //  Lo que el hilo escritor saca. Devuelve cuantas muestras por canal ha
    //  copiado, que puede ser cero: un anillo vacio no es un fallo, es que el
    //  escritor va mas rapido que la musica.
    int vivoLee (juce::AudioBuffer<float>& dest, int max) noexcept
    {
        const int quiere = juce::jmin (max, dest.getNumSamples());
        int i1 = 0, n1 = 0, i2 = 0, n2 = 0;
        vivoFifo.prepareToRead (quiere, i1, n1, i2, n2);
        for (int ch = 0; ch < juce::jmin (2, dest.getNumChannels()); ++ch)
        {
            if (n1 > 0) dest.copyFrom (ch, 0,  vivoRing, ch, i1, n1);
            if (n2 > 0) dest.copyFrom (ch, n1, vivoRing, ch, i2, n2);
        }
        vivoFifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

    //  MONITORIZACION DIRECTA: oirte por los cascos mientras grabas.
    //
    //  Se pidio «grabar voces con el micro con cascos, mientras escucho la
    //  produccion», y la mitad que faltaba es esta: la produccion se oia desde
    //  el primer dia -no hay una sola rama que atenue la salida por estar
    //  grabando- y lo que entraba por el microfono se copiaba a `recordBuffer`
    //  y MORIA en el `out.clear` de la etapa 2. O sea que se cantaba a ciegas.
    //
    //  LA SUMA VA ANTES DEL MASTER (etapa 5d-mon, justo encima de la barrera)
    //  y no despues, que es lo corto y esta mal: la ultima etapa del master es
    //  uno de los tres sitios por los que pasan TODOS los caminos, y colgar la
    //  entrada detras de ella deja que un NaN del aparato apague la maquina
    //  entera -que es exactamente lo que costo cuatro formas de enmudecer la
    //  app con un fichero-. De paso pasa por el limitador, que es lo que
    //  convierte un grito pegado al microfono en saturacion y no en desgarro.
    //
    //  Y NO ENTRA EN LA TOMA, y eso no es una comprobacion sino la forma: la
    //  toma se captura en la etapa 0, antes de que esta suma exista. Se mide
    //  igualmente, porque «no esta» es la clase de cosa que alguien anade sin
    //  querer.
    //
    //  Cero es apagado, y ademas es el defecto: un monitor encendido de
    //  fabrica sobre el altavoz de un telefono es un acople.
    void setMonitor (float gain) noexcept
    { monitorTarget.store (juce::jlimit (0.0f, 1.0f, gain), std::memory_order_relaxed); }
    float getMonitor() const noexcept { return monitorTarget.load (std::memory_order_relaxed); }

    //  RESAMPLING: record the MASTER back onto a pad.
    //
    //  The move this whole lineage is built on - play something, catch it,
    //  play the catch, catch that. It is the same recorder the microphone
    //  uses; the only difference is WHERE in the block it reads. The mic is
    //  taken at the top, before the output is cleared, because that is where
    //  the input arrives; the master is taken at the very bottom, after the
    //  voices, the effects and the master stage, because that is the sound
    //  that actually leaves the phone.
    void              startRecording (int slot, bool fromMaster = false) noexcept;
    SampleBuffer::Ptr finishRecording() noexcept;   // stop + build + publish; returns the buffer
    bool isRecording() const noexcept { return recording.load (std::memory_order_relaxed); }
    float getRecordSeconds() const noexcept;

private:
    void handleCommand (const Command& c) noexcept;               // audio thread
    //  `cortaSuCola` es false para las notas de un ACORDE: el autocorte esta
    //  puesto por defecto y es lo correcto para un pad de percusion -un golpe
    //  nuevo se come el anterior- pero un acorde son cuatro golpes del MISMO
    //  pad en el MISMO instante, y con el autocorte los tres primeros mueren
    //  antes de sonar. Medido: cuatro notas daban UNA voz viva.
    //  `gate` son muestras hasta soltar la nota, -1 = suelta sola. Ver
    //  setStepLen y Voice::gate.
    //  `plock` son los cuatro bloqueos empaquetados del paso que dispara, 0 si
    //  no hay ninguno. Viajan con el disparo en vez de escribirse en el pad
    //  para que el mando no se mueva solo. Ver setStepPLock.
    void triggerPad (int slot, int extraSemis = 0, float vel = 1.0f,
                     float from01 = -1.0f, bool cortaSuCola = true,
                     int gate = kGateAuto, std::uint32_t plock = 0) noexcept;   // audio thread

    template <typename Arr, typename V>
    static void store (Arr& a, int slot, V v) noexcept
    {
        if (slot >= 0 && slot < kNumPads) a[(size_t) slot].store (v, std::memory_order_relaxed);
    }

    class RetiredQueue
    {
    public:
        void push (SampleBuffer* p) noexcept
        {
            if (p == nullptr) return;
            int s1, z1, s2, z2;
            fifo.prepareToWrite (1, s1, z1, s2, z2);
            //  SI NO CABE, SE PIERDE UN BUFFER ENTERO. Para cuando esto corre,
            //  el hilo de audio ya ha soltado su puntero -pone padSample a
            //  nullptr justo despues-, asi que descartar aqui es un
            //  SampleBuffer cuyo contador no baja nunca: megabytes que no
            //  vuelven. Son 127 huecos utiles contra un temporizador de 33-60
            //  ms, y recargar la fabrica escribe 64 de golpe. Se cuenta, que es
            //  lo unico que separa "no pasa" de "no lo mira nadie".
            if (z1 + z2 < 1) { perdidos.fetch_add (1, std::memory_order_relaxed); return; }
            (z1 > 0 ? store[(size_t) s1] : store[(size_t) s2]) = p;
            fifo.finishedWrite (z1 + z2);
        }
        //  Cuantos punteros se han tirado por cola llena desde la ultima vez.
        int takePerdidos() noexcept { return perdidos.exchange (0, std::memory_order_relaxed); }

        template <typename Fn>
        void drain (Fn&& fn) noexcept
        {
            int s1, z1, s2, z2;
            fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) fn (store[(size_t) (s1 + i)]);
            for (int i = 0; i < z2; ++i) fn (store[(size_t) (s2 + i)]);
            fifo.finishedRead (z1 + z2);
        }
    private:
        static constexpr int cap = 128;
        juce::AbstractFifo fifo { cap };
        std::array<SampleBuffer*, cap> store {};
        std::atomic<int> perdidos { 0 };
    };

    // Two voices per pad, round-robin: a retrigger steals the previous
    // instance with a fast declick fade instead of hard-resetting it (the
    // single-voice reset produced a waveform discontinuity = audible click).
    //  One shared pool, not two voices bolted to each pad.
    //
    //  Two per pad meant a three second break cut itself off on the third hit
    //  while fifteen silent pads sat on thirty voices nobody was using. A pool
    //  spends the polyphony where it is actually being played, which is what
    //  every sampler does and what anyone reaching for a stand demo will try
    //  within about ten seconds.
    //
    //  The per-pad cap stays, just far higher: without one a single held pad
    //  could take the whole pool and starve the other fifteen.
    //  The array is the ceiling; how much of it is USED is the device's
    //  answer, set once at startup (see DeviceTier). Sized rather than
    //  allocated, so nothing about a tier decision can ever reach the audio
    //  thread as a reallocation.
    static constexpr int kNumVoices     = 64;
    std::array<Voice, kNumVoices>       voices {};
    int voiceLimit      = 48;   // <= kNumVoices, message thread sets, audio reads
    int maxVoicesOnPad  = 8;

    //  Which voices belong to which pad, rebuilt once per render segment.
    //  Without it the block cost was pads x voices - sixteen scans of the
    //  whole pool to answer "does this pad have anything sounding", and
    //  sixteen more to render, so a thousand iterations of pure bookkeeping
    //  whether one voice was playing or sixty-four. One pass over the pool
    //  builds a per-pad chain and every later loop touches only its own.
    //  Audio thread only.
    std::array<int, kNumPads>    padFirstVoice {};
    std::array<int, kNumVoices>  voiceNextInPad {};
    std::uint32_t                       voiceSerial = 0;   // audio-thread only, for oldest-steal
    CommandFifo commands;
    //  LA SEGUNDA COLA, y existe por una razon y no por comodidad.
    //
    //  CommandFifo es SPSC de un solo productor POR CONTRATO, y el hilo con el
    //  que JUCE entrega el MIDI no es el de mensajes. Empujar los dos ahi no
    //  degrada la cola: la atasca, para siempre, y el sintoma seria "los pads
    //  dejaron de sonar en cuanto enchufe el teclado". Una cola por productor,
    //  y el hilo de audio - que sigue siendo un solo consumidor - drena las dos.
    CommandFifo midiCommands;

    //  La salida. midiOutOn se lee una vez por disparo en el hilo de audio;
    //  apagada, triggerPad no escribe ni un byte.
    std::atomic<bool>  midiOutOn { false };
    MidiIo::NoteFifo   midiOut;

    //  THE LIFEBOAT. See postNoteOn / renderNextBlock.
    //
    //  One bit per pad, OR'd by the message thread, exchanged to zero by the
    //  audio thread. It carries no velocity and no start point, so it is a
    //  worse trigger than the queue in every way except the only one that
    //  matters here: a single atomic word cannot be left in a broken state,
    //  so it cannot stop working. It is what the test tone has always used,
    //  and the test tone is the thing that kept sounding when the pads did
    //  not.
    std::atomic<std::uint64_t> fallbackTriggers { 0 };

    //  How many triggers the queue has refused. Nothing in the audio path
    //  reads it; the UI does, because a queue that starts refusing is the
    //  app telling us it is wedged and wants the device rebuilt.
    std::atomic<int> droppedCommands { 0 };

    //  Two audio callbacks must never be inside the drain at once. During a
    //  route change the old stream's thread can still be in here when the new
    //  one arrives, and AbstractFifo is single-consumer BY CONTRACT: two
    //  readers move validStart and validEnd past each other and the queue is
    //  wedged for the rest of the process - every push refused, every drain
    //  empty. That is not a glitch that passes, it is permanent, and it looks
    //  exactly like "the pads stopped working but the test tone still beeps".
    std::atomic<bool> inRender { false };

    std::array<SampleBuffer*, kNumPads>              padSample {};
    std::array<std::atomic<SampleBuffer*>, kNumPads> pendingPad {};
    std::array<std::atomic<bool>, kNumPads> pendingClear {};
    RetiredQueue retired;

    //  LAS TABLAS RETIRADAS SON UNA COLA Y NO UN HUECO.
    //
    //  El primer intento guardaba la tabla saliente en un solo `atomic` con un
    //  compare-exchange, y si la anterior no se habia recogido todavia la CAS
    //  fallaba y la tabla se PERDIA: con ella las referencias de sus fuentes,
    //  o sea megabytes que no vuelven. Bastaban dos publicaciones adoptadas
    //  dentro del mismo tic del temporizador. Es el mismo razonamiento que
    //  RetiredQueue, con el mismo contador de lo que se tira.
    //  Y ES UNA PLANTILLA desde que hay DOS tablas que se publican asi -los
    //  clips y la automatizacion-. Copiarla habria sido la misma pieza escrita
    //  dos veces, con el mismo contador de perdidas que arreglar en dos sitios.
    template <typename T>
    class TablaQueueDe
    {
    public:
        void push (T* t) noexcept
        {
            if (t == nullptr) return;
            int s1, z1, s2, z2;
            fifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 + z2 < 1) { perdidas.fetch_add (1, std::memory_order_relaxed); return; }
            (z1 > 0 ? store[(size_t) s1] : store[(size_t) s2]) = t;
            fifo.finishedWrite (z1 + z2);
        }
        int takePerdidas() noexcept { return perdidas.exchange (0, std::memory_order_relaxed); }
        template <typename Fn>
        void drain (Fn&& fn) noexcept
        {
            int s1, z1, s2, z2;
            fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) fn (store[(size_t) (s1 + i)]);
            for (int i = 0; i < z2; ++i) fn (store[(size_t) (s2 + i)]);
            fifo.finishedRead (z1 + z2);
        }
    private:
        static constexpr int cap = 16;
        juce::AbstractFifo fifo { cap };
        std::array<T*, cap> store {};
        std::atomic<int> perdidas { 0 };
    };
    using TablaQueue = TablaQueueDe<TablaClips>;
    using AutoQueue  = TablaQueueDe<TablaAuto>;

    //  Los clips: la que suena, la que espera y las que hay que soltar.
    TablaClips*               clips        = nullptr;   // solo el hilo de audio
    std::atomic<TablaClips*>  pendingClips { nullptr };
    TablaQueue                clipsRetiradas;
    std::atomic<int>          clipsVivos   { 0 };

    //  Y LA AUTOMATIZACION, por el mismo camino. Esta tabla NO lleva punteros
    //  a nada -son cuatro POD por evento- asi que no hace falta una cola de
    //  retiradas con referencias: basta con que el `delete` caiga en el hilo
    //  de mensajes, que es la regla de la casa y no un detalle de este caso.
    TablaAuto*                autom        = nullptr;   // solo el hilo de audio
    std::atomic<TablaAuto*>   pendingAuto  { nullptr };
    AutoQueue                 autoRetiradas;
    std::atomic<int>          autoVivos    { 0 };
    std::atomic<bool>         autoEscribe  { false };
    std::atomic<int>          pasoAuto     { -1 };
    //  Aplica los eventos que caen EXACTAMENTE en este paso. Del hilo de audio
    //  y sin bucle anidado sobre nada que reserve: es un barrido lineal de la
    //  tabla y un `store` por acierto.
    void aplicaAutomacion (int paso) noexcept;
    std::array<std::atomic<bool>, kAudioTracks> pistaMute {};

    //  Mezcla los clips que caen dentro de [pos, pos+n) de la cancion.
    void renderClips (juce::AudioBuffer<float>& out, int offset, int n,
                      double pos, double porCompas) noexcept;
    static void sueltaTabla (TablaClips* t) noexcept;

    // Per-pad params (message writes, audio reads).
    std::array<std::atomic<float>, kNumPads> padPitch {};
    //  Target and the ramped value the render actually multiplies by. The
    //  second one is audio-thread only, so it is a plain float.
    std::atomic<float> masterTarget { 1.0f };
    std::atomic<float> masterUser   { 1.0f };
    std::atomic<bool>  ducked       { false };
    void refreshMasterTarget() noexcept
    {
        const float u = masterUser.load (std::memory_order_relaxed);
        masterTarget.store (ducked.load (std::memory_order_relaxed) ? u * kDuckGain : u,
                            std::memory_order_relaxed);
    }
    std::atomic<bool>  safetyLimiter { true };
    std::atomic<bool>  liveQuant     { false };
    std::atomic<int>   duckPad { -1 };        // -1 = sin bombeo
    std::atomic<float> duckAmt { 0.55f };     // cuanto se agacha, 0..1
    std::atomic<float> duckRel { 180.0f };    // ms de recuperacion
    float duckEnv = 0.0f;                     // solo hilo de audio
    //  Muestras por paso de 1/16 al tempo actual. Se necesita en la seccion 3
    //  - la cuantizacion del disparo en directo - y alli todavia no se ha
    //  calculado el transporte, que va en la 4+5.
    double samplesPerStepNow() const noexcept
    {
        const double bpmNow = juce::jmax (20.0, (double) bpm.load (std::memory_order_relaxed));
        return juce::jmax (1.0, (60.0 / bpmNow)
                                * (double) stepBeats.load (std::memory_order_relaxed)
                                * systemSampleRate);
    }
    //  Negras por paso. Ver setStepBeats.
    std::atomic<float> stepBeats { 0.25f };
    float masterGain = 1.0f;

    std::array<std::atomic<float>, kNumPads> padGain {};
    std::array<std::atomic<int>,   kNumPads> padStart {};
    std::array<std::atomic<int>,   kNumPads> padEnd {};
    std::array<std::atomic<bool>,  kNumPads> padLoop {};
    std::array<std::atomic<bool>,  kNumPads> padReverse {};
    std::array<std::atomic<bool>,  kNumPads> padKeepLength {};   // true = pitch only, false = tape
    std::array<std::atomic<int>,   kNumPads> padChoke {};   // 0 = none, 1..8 = choke group
    std::array<std::atomic<float>, kNumPads> padPan {};      // -1 (L) .. 0 (centre) .. 1 (R)
    //  UNO por defecto y no cero, que es lo que deja un array de atomicos: cero
    //  seria "todos los pads en mono" y un proyecto anterior sonaria plano. Lo
    //  llena prepareToPlay, que es quien puede.
    std::array<std::atomic<float>, kNumPads> padAncho {};    // 0 mono .. 1 como viene .. 2 doble
    std::array<std::atomic<float>, kNumPads> padAttack {};   // ms
    std::array<std::atomic<float>, kNumPads> padRelease {};  // ms

    //  El filtro del pad: corte, resonancia, y UN BIT por pad que dice si hay
    //  algo que filtrar. El bit existe por la misma razon que padSendMask -
    //  para que el reparto de cada bloque no pregunte 64 veces por dos floats
    //  que casi siempre significan "no" - y se recalcula en el hilo de
    //  mensajes, que es el unico que mueve los dos mandos.
    std::array<std::atomic<float>, kNumPads> padFadeIn {};   // ms, borde de entrada del recorte
    std::array<std::atomic<float>, kNumPads> padFadeOut {};  // ms, borde de salida
    std::array<std::atomic<float>, kNumPads> padCutoff {};   // Hz
    std::array<std::atomic<float>, kNumPads> padReso {};     // 0..1
    std::atomic<std::uint64_t> padFiltMask { 0 };

    //  EL CORTE QUE ESCRIBE UN PASO, APARTE DEL DEL PAD.
    //
    //  El bloqueo escribia en padCutoff, que es lo que lee el mando CORTE y lo
    //  que guarda el fichero de proyecto: dos compases con un paso bloqueado a
    //  200 Hz y el proyecto se guardaba con 200 en un pad que la persona habia
    //  dejado en 8 kHz - o sea que tocar una secuencia CAMBIABA el proyecto. Es
    //  exactamente el dano por el que los otros cuatro bloqueos se sacaron del
    //  pad y viajan con el disparo.
    //
    //  Aqui no pueden viajar con el disparo -el filtro se calcula una vez por
    //  bloque, fuera de la voz- asi que van a su propio sitio: negativo es "no
    //  hay bloqueo puesto" y entonces manda el pad. Lo escribe el hilo de audio
    //  y lo lee el hilo de audio; nadie mas lo mira.
    std::array<std::atomic<float>, kNumPads> pasoCutoff {};

    //  El corte que suena: el del paso si lo hay, y si no el del pad.
    float corteVivo (int slot) const noexcept
    {
        const float paso = pasoCutoff[(size_t) slot].load (std::memory_order_relaxed);
        return paso > 0.0f ? paso : padCutoff[(size_t) slot].load (std::memory_order_relaxed);
    }

    void refreshFiltMask (int slot) noexcept
    {
        const float hz = corteVivo (slot);
        const float rs = padReso[(size_t) slot].load (std::memory_order_relaxed);
        const bool  on = (hz < kFiltOpenHz - 1.0f) || (rs > 0.01f);
        const std::uint64_t bit = 1ull << (unsigned) slot;
        if (on) padFiltMask.fetch_or  (bit,  std::memory_order_relaxed);
        else    padFiltMask.fetch_and (~bit, std::memory_order_relaxed);
    }

    //  Filtro de variable de estado en forma TPT (Zavalishin). Dos estados por
    //  canal, y no una biquad de coeficientes directos, porque a esta le puedes
    //  mover el corte mientras suena sin que salte: los estados guardan
    //  integradores y no muestras pasadas, asi que un cambio de coeficiente no
    //  reinterpreta la historia. Barrer un corte con una biquad DF-I es el
    //  chirrido clasico.
    struct PadSvf { float ic1 = 0.0f, ic2 = 0.0f; };
    std::array<std::array<PadSvf, 2>, kNumPads> padFiltState {};
    std::array<std::atomic<bool>,  kNumPads> padMute {};
    std::array<std::atomic<bool>,  kNumPads> padSolo {};
    std::atomic<bool> soloActive { false };   // cached: is anything soloed
    void refreshSolo() noexcept
    {
        bool any = false;
        for (auto& s : padSolo) any = any || s.load (std::memory_order_relaxed);
        soloActive.store (any, std::memory_order_relaxed);
    }

    // Sequencer.
    std::atomic<bool>   playing { false };
    std::atomic<float>  bpm { 120.0f };   // float: lock-free on 32-bit ARM too
    //  ONE BIT PER PAD, PER STEP - and there are sixty-four pads now, so the
    //  word that holds them is sixty-four bits wide. It was a uint16, which is
    //  exactly why banks could not simply be added: the mask WAS the sixteen.
    //  std::atomic<uint64_t> is lock-free on every architecture this ships to.
    std::array<std::array<std::atomic<std::uint64_t>, kNumSteps>, kNumPatterns> patternBank {};
    std::atomic<int>    playStep { -1 };
    std::atomic<float>  stepPhase { 0.0f };   // 0..1 within the current step
    std::atomic<std::uint64_t> triggeredMask { 0 };   // pads triggered, read by UI
    std::array<std::atomic<float>, (size_t) kNumPads> padPos {};   // read head, 0..1, -1 = silent
    std::array<std::atomic<bool>,  (size_t) kNumPads> padSelfCut {};   // retrigger cuts its own tail
    double stepAccum = 0.0;      // audio-thread only
    int    currentStep = 0;      // audio-thread only
    bool   wasPlaying = false;   // audio-thread only

    // Pattern chain.
    std::array<std::atomic<int>, kNumPatterns> patternLength {};   // steps, kMinPatLen..kMaxPatLen
    std::atomic<int> editPattern { 0 };                        // bank the UI is editing
    std::array<std::atomic<int>, kMaxChain> chainSlots {};
    std::atomic<int> chainLength { 0 };
    std::atomic<int> playingPattern { 0 };                     // bank actually sounding, for UI
    int chainPos = 0;            // audio-thread only

    // Piano roll: per-(pattern, step, pad) semitone offset from the pad's own pitch.
    std::array<std::array<std::array<std::atomic<std::int8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepNote {};
    //  LAS TRES NOTAS DE MAS, para que un paso pueda ser un ACORDE.
    //
    //  stepNote guarda UNA nota por paso y por pad, que es todo lo que hace
    //  falta para percusion afinada. Para tocar un instrumento -acordes,
    //  melodias, arpegios- hace falta que un mismo pad suene varias veces a la
    //  vez en el mismo paso, y eso no cabe en un solo semitono.
    //
    //  Se guardan APARTE y no se cambia stepNote: la nota raiz sigue donde
    //  estaba, asi que todo lo que ya lee y escribe patrones -el fichero de
    //  proyecto, el secuenciador, la ficha PASO- sigue funcionando sin tocarlo
    //  y un patron viejo vuelve exactamente igual. Tres extras empaquetadas en
    //  un entero: un byte por nota y tres bits que dicen cuales estan puestas,
    //  porque el cero es un semitono valido y no puede significar "ninguna".
    //  Cuatro notas es un acorde de verdad y son 64 KB; ocho serian 128 y no
    //  hay dedos para escribirlas en una rejilla de telefono.
    std::array<std::array<std::array<std::atomic<std::uint32_t>, kNumPads>, kNumSteps>, kNumPatterns> stepChord {};
    //  El empujon de cada paso. Ver setStepNudge. Cero es "en su sitio", que
    //  es lo que dice un patron escrito antes de que esto existiera.
    std::array<std::array<std::array<std::atomic<std::int8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepNudge {};
    //  El largo de cada paso, en cuartos de paso. Ver setStepLen.
    std::array<std::array<std::array<std::atomic<std::uint8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepLen {};
    //  El bloqueo del corte, en porcentaje del recorrido. Ver setStepLock. Se
    //  inicializa a CERO y no a -1, que es lo que vale un patron viejo, asi
    //  que el cero tiene que significar "sin bloqueo" y no "20 Hz": se guarda
    //  desplazado un uno - 0 es ninguno, 1..101 es 0..100 %.
    std::array<std::array<std::array<std::atomic<std::int8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepLock {};
    //  Los otros cuatro bloqueos, empaquetados. Ver setStepPLock: un byte por
    //  bloqueo, cero es "este paso no toca eso", que es lo que vale un patron
    //  escrito antes de que esto existiera.
    std::array<std::array<std::array<std::atomic<std::uint32_t>, kNumPads>, kNumSteps>, kNumPatterns> stepPLock {};

    //  What a step DOES, beyond which pads it fires.
    //
    //  A pattern that can only say "this pad, this step" plays like a typewriter:
    //  every hit identical, every hit on the grid. These are the two things
    //  that turn a grid into a performance, and they are per pad and per step
    //  because that is the only place they mean anything.
    //
    //    stepVel   how hard, 1..127, 127 = as loud as the pad is set to.
    //    stepRoll  how MANY, 1..8 evenly spaced inside the step. A roll is not
    //              a shorter step - the grid does not get finer - it is one
    //              step that speaks more than once.
    std::array<std::array<std::array<std::atomic<std::uint8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepVel {};
    std::array<std::array<std::array<std::atomic<std::uint8_t>, kNumPads>, kNumSteps>, kNumPatterns> stepRoll {};

    //  SWING. 0.5 is straight; above it the odd sixteenths are pushed late by
    //  that fraction of a step. It is the difference between a machine playing
    //  a pattern and somebody playing it.
    std::atomic<float> swing { 0.5f };

    //  Hits waiting to speak inside the current step. Swing moves a hit off the
    //  boundary and a roll puts several between boundaries, so a step is no
    //  longer a single instant and the render loop has to be able to stop
    //  between them.
    //  `corta` distingue la nota RAIZ de las del acorde: la raiz corta la cola
    //  del pad como siempre, y las que la acompanan no, o se matarian entre
    //  ellas antes de sonar. Ver triggerPad.
    struct PendingHit { int countdown; int pad; int semis; float vel; bool corta = true; int gate = -1;
                        std::uint32_t plock = 0; };
    //  DIMENSIONADA A LOS SESENTA Y CUATRO PADS, no a los dieciseis de antes.
    //
    //  Eran 96 huecos "para los dieciseis pads", y hay 64: un paso con los 64
    //  puestos y redoble de 2 pide 128, y 24 pads con acorde de cuatro ya son
    //  96. Lo que sobraba desaparecia SESGADO hacia los pads altos -el bucle va
    //  0->63- y sin que nadie lo contara. Un tope que se supera en silencio no
    //  protege, esconde.
    //
    //  El peor caso de verdad son 64 pads x (1 raiz + 3 del acorde) x 2 golpes
    //  de redoble = 512. Son 512 x 28 bytes = 14 KB de miembro, que en un
    //  motor que ya reserva megabytes de buffers no se nota. Y lo que aun asi
    //  no quepa se cuenta en droppedCommands, que es donde la interfaz ya mira
    //  lo que se pierde.
    static constexpr int kMaxPending = 512;
    std::array<PendingHit, kMaxPending> pending {};
    int numPending = 0;

    // Song / playlist.
    std::atomic<bool> songMode { false };
    std::array<std::array<std::atomic<int>, kSongBars>, kSongLanes> songCell {};
    std::atomic<int> songBars { 8 };      // how many bars the song is long
    std::array<std::atomic<bool>, kSongLanes> songLaneMute {};   // ver setSongLaneMute
    std::array<std::atomic<juce::uint64>, kSongLanes> songCellMute {};  // ver setSongCellMute
    //  El tramo en bucle, en COMPASES y medio abierto: [A, B). A >= B es
    //  apagado, que es lo que vale un proyecto que no lo conocia.
    std::atomic<int> songLoopA { 0 }, songLoopB { 0 };
    std::atomic<int> songBar  { -1 };     // live playhead bar, for the UI
    // Audio-thread only: what each lane is currently running.
    int  lanePattern[kSongLanes] { -1, -1, -1, -1 };
    int  laneStartStep[kSongLanes] { 0, 0, 0, 0 };
    //  Cuantos compases ocupa el bloque que suena en cada carril. Ver fireStep:
    //  es lo que le da al bloque una longitud propia, distinta de la del
    //  patron que lleva dentro.
    int  laneBars[kSongLanes] { 1, 1, 1, 1 };
    int  songStep = 0;                    // absolute step within the song

    //  EL METRONOMO Y LA CUENTA ATRAS.
    //
    //  No habia ninguno, y sin ellos grabar al arreglo no se puede hacer: una
    //  toma que entra a ojo entra corrida, y una que empieza en el instante en
    //  que se pulsa no empieza en el compas.
    //
    //  Todo POD y del hilo de audio salvo los dos atomicos, que es lo que el
    //  contrato de este fichero exige: ni reservas, ni cerrojos, ni E/S.
    std::atomic<bool> clickOn { false };
    //  Pasos que faltan de cuenta atras. Mientras sea > 0 el clic suena y la
    //  cancion NO avanza; al llegar a cero arranca el transporte.
    //
    //  EN PASOS Y NO EN MILISEGUNDOS, que es la trampa que este fichero ya se
    //  ha comido tres veces con los ticks: un compas son dieciseis pasos pase
    //  lo que pase, y cuantos milisegundos sean lo decide el tempo.
    std::atomic<int>  cuentaPasos { 0 };
    float clickEnv   = 0.0f;      // solo hilo de audio
    float clickPhase = 0.0f;
    float clickHz    = 0.0f;
    int   clicPaso   = 0;         // pasos desde que el transporte arranco
    //  EL ARRANQUE ESPERA AL BORDE DE COMPAS. Ver el primer disparo del
    //  transporte: `if (currentStep < 0) fireStep()` corre una vez por BLOQUE
    //  mientras no haya sonado el primer paso, asi que sin esto la cancion
    //  arrancaria en el bloque siguiente a que la cuenta acabe -hasta 5500
    //  muestras antes del compas- en vez de en la linea de compas.
    bool  arranqueEnBorde = false;
    //  GRABAR AL ARREGLO: la toma empieza cuando acaba la cuenta atras, y eso
    //  lo decide el hilo de AUDIO. Hacerlo desde el temporizador de la cara le
    //  metería el latido del hilo de mensajes -60 ms- justo en el instante que
    //  decide si la toma entra a tiempo, en la app cuyo argumento entero es la
    //  latencia.
    std::atomic<bool> grabarTrasCuenta { false };
    std::atomic<int>  compasGrabado { -1 };

    // Latency probe. The click is emitted a moment AFTER the stream starts,
    // so the measurement is of a settled stream rather than of its first
    // fumbling blocks.
    std::atomic<bool> probeArm { false };
    std::atomic<bool> probing  { false };
    int probeCounter = 0;            // audio-thread only
    int probeClickAt = 0;
    int probeLength  = 0;

    // Recording.
    std::atomic<bool> recording { false };
    std::atomic<bool> recordFromMaster { false };
    std::atomic<int>  recordPos { 0 };
    juce::AudioBuffer<float> recordBuffer;   // allocated in prepareToPlay, never in the callback
    int    recordChannels = 1;               // how many of the input channels we keep
    double recordSeconds  = 60.0;
    bool   recordAllowStereo = true;
    int recordSlot = 0;

    //  EL REBOTE EN VIVO: lo que suena, escrito a un fichero mientras suena.
    //
    //  Ver `vivoArma`. Un anillo y no un buffer del largo de la cancion, que es
    //  la misma leccion que ya obligo al rebote offline a escribir por bloques:
    //  `recordBuffer` mide de 20 a 120 s segun la gama del aparato y una
    //  cancion larga son 128, asi que aqui no cabe. Con un anillo la memoria es
    //  constante y el hilo de audio no reserva ni escribe en disco: empuja y
    //  sigue.
    //
    //  Un solo productor -el hilo de audio- y un solo consumidor -el hilo que
    //  escribe-, que es el contrato de todo lo demas en esta casa.
    juce::AbstractFifo vivoFifo { 1 };
    juce::AudioBuffer<float> vivoRing;
    std::atomic<bool> vivoOn { false };
    std::atomic<int>  vivoPerdidas { 0 };

    //  MONITORIZACION DIRECTA: lo que entra por el micro, saliendo por los
    //  cascos. Ver `setMonitor`. El buffer se reserva en prepareToPlay y nunca
    //  en el callback, como el de grabacion, y no existe en el motor del
    //  rebote -que no tiene entrada y no monitoriza nada-.
    std::atomic<float> monitorTarget { 0.0f };
    juce::AudioBuffer<float> monitorBuf;
    int   monitorInChans = 0;        // cuantos canales trae de verdad la entrada
    float smMonitor = 0.0f;          // solo el hilo de audio

    //  ESTADO MUERTO, QUITADO. `fxType`, `fxCutoff` y sus dos setters no
    //  tenian un solo llamante en toda la app: se copiaban de un motor a otro
    //  en `copyStateFrom` y ya. Son de cuando el FLT era un corte y un tipo, y
    //  el barrido bidireccional -`fltSweep`, que si se usa- los sustituyo. Con
    //  ellos se van `smCutoff`, `smFxMix`, `smCrMix` y `smRvMix`, que solo se
    //  asignaban ahi, y `fxDry`, que reservaba 2 x maxBlock de memoria y no lo
    //  leia nadie. Un parametro que se copia y no se usa se lee como si
    //  hiciera algo, y el dia que alguien lo mueva no pasara nada.
    //  ==================================================================
    //  LOS PARAMETROS DE TODOS LOS EFECTOS, EN UNA TABLA.
    //
    //  Estaban en diecinueve atomicos con nombre propio repartidos por esta
    //  cabecera, mas dos arrays de cuatro para la dinamica, y de ahi salian
    //  TRES sitios que hay que escribir a mano por cada tipo nuevo — y los
    //  tres fallan en SILENCIO, que es lo que los hace caros:
    //
    //    · `fxMixNow[kNumFx]` (seccion 5b) era una lista literal de once
    //      cargas atomicas: el tipo que falte se inicializa a 0.0f, o sea
    //      MUDO, sin un aviso del compilador.
    //    · `setFxParam` era un `switch (fx * 3 + par)` con TREINTA Y TRES
    //      casos escritos uno a uno — sesenta y tres con veintiun tipos — y
    //      el que falte cae en el `default`, o sea un mando que se mueve y no
    //      hace nada.
    //    · `copyStateFrom` copiaba VEINTIDOS pares de atomicos a mano, y su
    //      propio comentario dice que eso ya se olvido tres veces. El sintoma
    //      es que el rebote no suena como la escucha, y solo se descubre
    //      cuando ya lo has mandado.
    //
    //  Con la tabla, los tres son un bucle. El indice es el MISMO que usan
    //  `fxDefs` en la cara y `setFxParam (fx, par)` aqui, asi que no hay una
    //  segunda numeracion que mantener.
    //
    //  Y POR CANAL, que es la otra mitad de que un inserto sea de UN canal:
    //  su ESTADO se replica en `Inserto` y sus PARAMETROS aqui. Un corte de
    //  filtro compartido por los dieciseis seria la misma ventana al mismo
    //  aparato por la puerta de los mandos en vez de por la del estado.
    //
    //  Los CINCO ENVIOS no: una linea de retardo es de todos, asi que su fila
    //  vive en el canal cero y los quince restantes no se leen nunca. Quien
    //  escribe esa regla UNA vez es `fxParamDe`, y nadie mas indexa `fxP`.
    std::array<std::array<std::array<std::atomic<float>, 3>, kNumFx>, kNumCanales> fxP;

    //  LA PUERTA UNICA. `sustituye` decide si el canal cuenta: escrito en cada
    //  sitio de lectura serian cincuenta copias de la misma condicion y la que
    //  se quedara vieja seria un delay por canal que no existe.
    std::atomic<float>& fxParamDe (int c, int f, int par) noexcept
    {
        const int cc = (fxSustituye[f] && c > 0 && c < kNumCanales) ? c : 0;
        return fxP[(size_t) cc][(size_t) f][(size_t) par];
    }
    const std::atomic<float>& fxParamDe (int c, int f, int par) const noexcept
    {
        const int cc = (fxSustituye[f] && c > 0 && c < kNumCanales) ? c : 0;
        return fxP[(size_t) cc][(size_t) f][(size_t) par];
    }

    //  Y LOS NOMBRES SE QUEDAN, como REFERENCIAS a su hueco — pero SOLO los de
    //  los dos ENVIOS que quedan aqui: los trece de inserto se retiran, porque
    //  una referencia no sabe de canal y dejarla seria una puerta trasera al
    //  canal cero. Las ~50 lecturas de las etapas de DLY y REV no se tocan.

    // Audio-thread-only smoothed FX params (one-pole toward the atomics):
    // knob moves arrive as per-block jumps otherwise — zipper on the filter,
    // crackle on the delay time. ~20 ms time constant.
    float smDlyMix  = 0.0f;
    float smDlyFb   = 0.35f;
    float smDlySamp = 0.0f;      // delay time in samples, smoothed per sample

    // Master delay.
    //  LAGRANGE, NO LINEAL, PORQUE HAY REALIMENTACION.
    //
    //  Interpolar linealmente un retardo fraccionario no es aproximar: es un
    //  paso bajo cuya frecuencia de corte depende de la PARTE FRACCIONARIA
    //  del retardo - transparente en fraccion 0, y en fraccion 0.5 unos 3 dB
    //  menos en Nyquist/2 y un cero en Nyquist. Con una sola pasada eso se
    //  perdona; aqui la linea se realimenta hasta 0.95, asi que el error se
    //  COMPONE en cada repeticion y la cola se apaga en agudos mucho antes de
    //  lo que dice el mando. Y como el retardo se suaviza por muestra, la
    //  fraccion barre todo su recorrido durante un movimiento de TIME: el
    //  brillo de las repeticiones modula con ella.
    //
    //  Medido en el banco - un tono de 8 kHz, 33.34375 ms de retardo (1600.5
    //  muestras, media muestra clavada de fraccion) y 0.9 de realimentacion:
    //  la octava repeticion salia 5.9 dB por debajo de lo que la
    //  realimentacion sola predice. Con Lagrange, 0.7 dB. Cinco decibelios de
    //  agudos que el mando prometia y la linea se comia.
    //
    //  Cuesta tres multiplicaciones-acumulaciones mas por muestra y por canal
    //  sobre una etapa que no llega al 1% de carga.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delayLine { 96000 };
    std::atomic<float>& dlyTime = fxP[0][kFxDly][0];   // ms
    std::atomic<float>& dlyFb   = fxP[0][kFxDly][1];   // 0..0.95
    std::atomic<float>& dlyMix  = fxP[0][kFxDly][2];   // 0..1

    // ISO wet/dry, so the low-pass can be blended rather than only replacing.

    // HPF: its OWN filter, not the ISO one switched to high-pass. Two objects
    // cost a few hundred bytes and buy a band-pass you can sweep from both
    // ends — one shared filter would have made them mutually exclusive.

    // Drive tone: a one-pole low-pass after the tanh, because saturation
    // without somewhere for the harmonics to go is just harsh.

    // Crush: bit depth and sample-and-hold rate, the two halves of lo-fi.

    // Reverb, last in the chain so everything ahead of it lands in the room.
    //  Ver Fdn.h. Sustituye a juce::dsp::Reverb, que es Freeverb: ocho peines
    //  y cuatro allpass publicados en 2000, con la cola metalica que eso
    //  implica. En una caja que apunta a produccion, la reverb es lo primero
    //  que delata que el motor es de juguete.
    Fdn reverb;
    std::atomic<float>& rvSize = fxP[0][kFxRev][0];
    std::atomic<float>& rvDamp = fxP[0][kFxRev][1];
    std::atomic<float>& rvMix  = fxP[0][kFxRev][2];

    //  EL EQ DE CINCO BANDAS. Es un INSERTO -fxSustituye- y no un envio: lo que
    //  un pad manda aqui deja de ir por el camino seco, porque ecualizar la
    //  copia y dejar el original sonando al lado no ecualiza nada.

    //  LOS DOS ANILLOS DEL ANALIZADOR, que son lo que la cara dibuja detras de
    //  la curva: lo que ENTRA al EQ dice DONDE hay que tocar y lo que SALE
    //  confirma que la correccion hizo lo que querias. Copiados de `scope` y
    //  `copyScope`, que es la pieza que esta casa ya tiene para esto.
    //
    //  Se escriben SOLO con el bus vivo. Con el EQ sin envios no se escribe
    //  nada y el analizador cae a su suelo en vez de congelarse, que es la
    //  diferencia entre «no pasa nada por aqui» y «esto esta roto».
    //  LA FAMILIA DE DINAMICA: cuatro tipos y UNA sola pieza con cuatro
    //  configuraciones (`Source/Dinamica.h`). Cada uno tiene su propio estado
    //  -su envolvente y su ganancia suavizada- porque son cuatro buses que
    //  pueden estar abiertos a la vez, y compartir el detector haria que la
    //  puerta se cerrase cuando el limitador pegase.
    //  Sus parametros viven en `fxP` como los de cualquier otro tipo: un
    //  array de cuatro indexado por MODO era una segunda numeracion al lado
    //  de la de `fxDefs`, y traducir entre las dos es de donde salen los
    //  cruces. `dynIdx` hace la unica traduccion que queda.
    static constexpr int dynIdx (int d) noexcept { return kFxCmp + d; }
    //  Lo que esta bajando cada uno, para la casilla de lectura de CTRL 3. Lo
    //  escribe el hilo de audio y lo lee la cara: un float atomico, que es lo
    //  mismo que ya hacen `vuL` y los demas medidores.

    //  LA FAMILIA DE MODULACION: cuatro tipos y UNA pieza (`Source/Lfo.h`).
    //  Un LFO por tipo y no uno compartido, por lo mismo que la dinamica tiene
    //  cuatro detectores: son cuatro buses que pueden estar abiertos a la vez
    //  y compartir la fase haria que el coro latiera al ritmo del temblor.
    //  TRES Y NO CUATRO. TRM es el unico de la familia que SUSTITUYE, asi que
    //  su LFO vive en `Inserto` -uno por canal- y aqui se quedan los tres que
    //  SUMAN. Dejar el hueco de TRM sin escribir habria sido estado muerto, que
    //  es lo que este fichero ya llama por su nombre con `fxType` y `fxDry`.
    std::array<Lfo, 3> mod;
    static constexpr int modIdx (int m) noexcept { return kFxCho + m; }
    static constexpr int kNumModEnvio = 3;
    //  Cual de los cuatro es un tipo, o -1. La usan la etapa y `getLfoFase`.
    static constexpr int modDe (int f) noexcept
    { return (f >= kFxCho && f <= kFxTrm) ? f - kFxCho : -1; }

    //  LA FASE DE AHORA, para el punto de trabajo del visor. Es el hermano de
    //  `dynRed`, y por la misma razon: el eje de esta familia se mide en
    //  PERIODOS, asi que RATE no cabe en la curva -eso lo declara
    //  `FxVisor::mandosDe`- y donde se ve es en el punto viajando a la
    //  velocidad de verdad. La escribe el hilo de audio, la lee la cara.
    std::array<std::atomic<float>, 3> modFase { { { 0.0f }, { 0.0f }, { 0.0f } } };

    //  Las dos lineas de retardo de CHO y FLA. Del MISMO tipo que la de DLY
    //  -`Lagrange3rd`- porque un retardo que se barre y no interpola crepita, y
    //  este proyecto ya lo tiene medido: 5.9 dB de perdida con lineal contra
    //  0.7 con Lagrange de tercer orden. Cortas a proposito: 30 ms de coro y
    //  10 de flanger son los recorridos del sector, y reservar un segundo como
    //  DLY seria pagar 96000 muestras por canal para usar mil.
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> choLine { 4096 };
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> flaLine { 2048 };
    //  EL TOPE DE LA REALIMENTACION DE FLA, medido y no elegido. A 0.95 el
    //  peine sale de 14.32 dB y el nivel MEDIO de la banda sube +5.13: eso es
    //  margen del master gastado por abrir un efecto, y el margen del master
    //  no es nuestro -es la misma regla que ya tiene HUMANIZAR con la fuerza y
    //  el tope de 0 dB del fader-.
    static constexpr float kFlaFbMax = 0.85f;
    float smChoProf = 0.0f, smFlaFb = 0.0f, smPhaProf = 0.0f;
    std::array<bool, 3> modWasActive {};
    //  Los cuatro allpass de PHA, uno por etapa y por canal. Estado de primer
    //  orden: una muestra cada uno.
    static constexpr int kPhaEtapas = 4;
    float phaZ[2][kPhaEtapas] {};
    float flaFbZ[2] {};

    //  LA FAMILIA DE CARACTER: RNG · PIT · WID · EXC · TRN · FRZ.
    //
    //  No comparten una pieza como los cuatro de modulacion — se agrupan por
    //  lo contrario, que es lo que hace que entren juntos: TRES son
    //  EXTRACCIONES de codigo que ya existia escrito dos veces o privado
    //  (`Estereo::ancho` salia de dos copias en linea de `Voice.h`, y
    //  `Dinamica::cruceEn` y `Dinamica::cruza` eran privadas) y tres se
    //  escriben de cero.
    //
    //  RNG: su oscilador es de AUDIO y por eso no es un `Lfo`. `Lfo::ponPaso`
    //  acota a 40 Hz A PROPOSITO -es lo que separa un temblor de un zumbido- y
    //  aqui el recorrido llega a 4 kHz. Lo que SI se reutiliza es la FORMA:
    //  `Lfo::valorEn` es la misma funcion que dibuja el visor, que es la razon
    //  por la que nacio estatica.

    //  PIT: una linea de retardo leida a otra velocidad, con DOS cabezas
    //  desfasadas medio grano y cruzadas. Una sola cabeza da un salto audible
    //  cada vez que da la vuelta -eso es un «glitch» y no un afinador-, y el
    //  cruce es lo que lo tapa. 16384 muestras son 341 ms a 48 kHz, o sea tres
    //  granos del mas largo.

    //  WID: el cruce que deja los graves en MONO. Sin el, abrir el ancho de
    //  una mezcla con bajo desplaza el bajo -que es lo unico que NO puede
    //  moverse: en un sistema grande el sub es mono por construccion-. Cuarto
    //  orden por lo mismo que el de-esser: es el unico cuyas dos mitades SUMAN
    //  planas, y con segundo orden la suma tiene un bache de 3 dB justo en el
    //  corte.

    //  EXC: el MISMO cruce, y el mismo argumento. Lo que se satura es la banda
    //  alta sola y se vuelve a sumar con la baja intacta: saturar la mezcla
    //  entera es DRV, que ya existe.

    //  TRN: dos seguidores de envolvente por canal —uno rapido y uno lento— y
    //  su DIFERENCIA es el transitorio. Enlazados como los de `Dinamica` y por
    //  lo mismo: con un detector por canal, el lado que pega baja y el otro se
    //  queda, o sea que la imagen estereo se mueve con cada golpe.

    //  FRZ: la ventana capturada y por donde va su lectura. Estereo y del
    //  largo del tope del mando -500 ms a 48 kHz son 24000- porque reservar
    //  aqui esta prohibido: se reserva en `prepareToPlay` y el mando solo
    //  mueve CUANTO se usa.

    //  El flanco de cada uno vive en `Inserto`, que es donde vive el estado
    //  que limpia. Aqui se queda solo la traduccion de tipo a indice.
    static constexpr int carDe (int f) noexcept
    { return (f >= kFxRng && f <= kFxFrz) ? f - kFxRng : -1; }

    //  LOS ANILLOS DEL EFECTO MIRADO. Eran del EQ y ahora son de quien la cara
    //  este enseñando: el analizador del EQ resulto ser un caso de la misma
    //  pregunta -«que esta pasando por este bus AHORA»- y tener dos capturas,
    //  una para el EQ y otra para los demas, habria sido la misma regla
    //  escrita dos veces. El nombre viejo decia lo que ya no es.
    static constexpr int kFxScope = 2048;   // potencia de dos
    std::array<float, kFxScope> mirPre {}, mirPost {};
    std::atomic<int> mirWrite { 0 };
    //  Bloques que el bus lleva vivo, a la baja. Un booleano se apagaria el
    //  primer bloque de silencio entre dos golpes y la mancha parpadearia.
    std::atomic<int> mirHot { 0 };
    std::atomic<int> mirado { -1 };

    // ------------------------------------------------------------------
    //  Sends. Each effect is a bus with its own input, and every pad decides
    //  how much of itself goes into each one. That is what makes an effect
    //  belong to a channel rather than to the whole instrument, and it is
    //  also what makes cutting one behave the way it does on hardware: the
    //  SEND closes, the RETURN stays open, so whatever was already inside a
    //  delay or a reverb rings out instead of being amputated.
    //
    //  The four tone effects also take the pad OFF the dry path by the same
    //  amount they take it on to theirs — a filter you can hear around is
    //  not a filter. Delay and reverb add on top, as sends do.
    // ------------------------------------------------------------------
    //  El EQ es el septimo y es de los que RESTAN SECO: un ecualizador es un
    //  inserto. Mandar una copia al EQ y dejar el original sonando al lado da
    //  la suma de los dos, o sea la mitad de la correccion y con fase de
    //  regalo - que es literalmente lo que hace un filtro peine.
    //  Y SE LLAMA POR LO QUE HACE. Se llamo `fxIsTone` mientras los que
    //  restaban seco eran los cuatro «de tono» -filtro, paso alto, saturacion,
    //  reduccion-; desde el EQ y la familia de dinamica son NUEVE de once, o
    //  sea que el nombre describia una coincidencia de aquel dia y no la
    //  regla. Un campo que ya no significa lo que dice su nombre manda a
    //  buscar, que es lo mismo que costo renombrar `uiIntervalMs` a `relojMs`.
    //  Y LOS CUATRO DE MODULACION: CHO, FLA y PHA SUMAN; TRM SUSTITUYE. El
    //  peine de un flanger y las muescas de un phaser salen de sumar la copia
    //  procesada con la seca, asi que restarle el seco al pad deja un vibrato
    //  y un allpass puro -que no suenan a nada-. Un temblor es al reves: la
    //  suma de una senal y su version modulada no tiembla, porque lo que el
    //  LFO quita de una lo tiene la otra. Lo mide `Tests/rack.py`
    //  comprobacion 1, comparando lo que la fila del rack DICE contra lo que
    //  el motor hace.
    static constexpr bool fxSustituye[kNumFx] = { true, true, true, false, true, false, true,
                                                  true, true, true, true,
                                                  false, false, false, true,
                                                  //  Los seis de caracter: los seis
                                                  //  SUSTITUYEN. Un ancho, un
                                                  //  excitador o un moldeador de
                                                  //  transitorios que deja el original
                                                  //  al lado no hace nada -la suma
                                                  //  devuelve lo que habia-, y un
                                                  //  congelador con el seco encima es
                                                  //  el seco. RNG igual: la suma de la
                                                  //  señal y su producto por una
                                                  //  portadora deja la fundamental
                                                  //  intacta, que es justo lo que un
                                                  //  ring mod existe para quitar.
                                                  true, true, true, true, true, true };
    //  Y NO SE PUEDE QUEDAR CORTA EN SILENCIO. Una lista de inicializacion de
    //  agregado rellena con `false` lo que no se nombre, asi que un tipo nuevo
    //  al que se le olvide su fila aqui entraria como ENVIO —sumando encima en
    //  vez de sustituir— sin un aviso de nadie. Es lo mas barato que puede
    //  costar una regla escrita dos veces, y es lo que ya hace `MidiIo` con
    //  `kMaxPads`.
    static_assert (sizeof (fxSustituye) / sizeof (fxSustituye[0]) == kNumFx,
                   "fxSustituye tiene que tener una fila por tipo");
    static_assert (sizeof (kFxDef) / sizeof (kFxDef[0]) == kNumFx,
                   "kFxDef tiene que tener una fila por tipo");

    //  ============================================================
    //  EL INDICE DEL BUS: UN INSERTO ES DE UN CANAL, UN ENVIO ES DE TODOS
    //  ============================================================
    //
    //  Llego del telefono: «hay un error cuando colocas en diferentes canales
    //  ciertos efectos como un ecualizador, solo es posible que funcione y sea
    //  colocado en un canal solo». Era exacto, y no era de la cara: `fxBus`
    //  tenia UN buffer por TIPO, asi que poner el EQ en el canal 4 se lo
    //  quitaba al 0 sin decir nada.
    //
    //  EL REPARTO NO SE INVENTA: sale de `fxSustituye`, que es la unica linea
    //  que separa las dos familias y la que `Tests/rack.py` ya mide. Los
    //  DIECISEIS insertos se replican -su estado es uno solo, asi que dos
    //  canales con el mismo inserto serian dos ventanas al mismo aparato- y
    //  los CINCO envios se quedan globales, que es lo que un envio significa:
    //  una linea de retardo existe para que varias fuentes entren en la misma
    //  cola, y restringir el DLY a un canal es exactamente lo contrario.
    //
    //  Y `kIns` se DERIVA de `fxSustituye` y no se escribe: una lista literal
    //  aqui seria la tercera copia de la misma regla, y este fichero ya lleva
    //  tres fallos en silencio de esa clase -`fxIsTone`, `fxMixNow` y el
    //  `switch` de `setFxParam`-.
    //  El numero va a mano y lo VIGILA EL COMPILADOR, que es lo mas barato que
    //  puede costar una regla derivada: `contarInsertos()` recorre
    //  `fxSustituye` y el `static_assert` de debajo de la clase los contrasta.
    //  Aqui dentro no se puede llamar -la clase todavia esta incompleta en el
    //  punto en el que haria falta el valor- y ahi fuera si.
    static constexpr int kNumIns = 16;
    static constexpr int contarInsertos() noexcept
    {
        int n = 0;
        for (int f = 0; f < kNumFx; ++f) if (fxSustituye[f]) ++n;
        return n;
    }

    //  Cual de los `kNumIns` es este tipo, o -1 si es un envio.
    static constexpr int insIdx (int f) noexcept
    {
        if (f < 0 || f >= kNumFx || ! fxSustituye[f]) return -1;
        int n = 0;
        for (int i = 0; i < f; ++i) if (fxSustituye[i]) ++n;
        return n;
    }

    //  EL ORDEN DE LAS ETAPAS SE QUEDA INTACTO, y ese es el argumento fuerte y
    //  no la memoria. Los primeros `kNumFx` huecos siguen indexados POR TIPO
    //  -o sea `busDe (c, f) == f` para los cinco envios- y los insertos se
    //  apilan detras. Reordenar a canal-mayor tumbaria la fila de control BIT A
    //  BIT por construccion: hoy `returnBus` acumula en orden de etapa, y con
    //  canal-mayor los cinco envios volverian DESPUES de los dieciseis canales.
    //  La suma en coma flotante no es asociativa, asi que saldria rojo sin que
    //  nadie mueva un mando y no habria forma de distinguir «reordene la suma»
    //  de «rompi un inserto». Con este orden, `live` es falso en los quince
    //  canales vacios y la secuencia de `out.addFrom` es identica muestra a
    //  muestra.
    static constexpr int kNumBuses = kNumFx + kNumIns * kNumCanales;

    static constexpr int busDe (int canal, int fx) noexcept
    {
        const int i = insIdx (fx);
        if (i < 0) return fx;                       // un envio es de todos
        const int c = (canal < 0 || canal >= kNumCanales) ? 0 : canal;
        return kNumFx + i * kNumCanales + c;
    }

    //  ============================================================
    //  EL ESTADO DE LOS DIECISEIS INSERTOS, EN UNA PIEZA
    //  ============================================================
    //
    //  Un inserto es de UN canal, asi que su estado se replica dieciseis
    //  veces. Estaba suelto por la cabecera -un filtro aqui, dos flotantes de
    //  suavizado alla, una ventana de congelador mas abajo- y multiplicarlo a
    //  mano por dieciseis multiplica por dieciseis la probabilidad de que a
    //  uno se le olvide su `prepare`, su `reset` o su cebado. Es el mismo
    //  argumento que ya vale para `copyStateFrom`, cuyo propio comentario dice
    //  que ese sitio ya se olvido tres veces.
    //
    //  Lo que NO entra aqui son los CINCO ENVIOS -DLY, REV, CHO, FLA y PHA-,
    //  que siguen siendo unicos: su linea de retardo, su reverb y sus LFO se
    //  quedan en la clase. Un envio existe para que varias fuentes entren en
    //  la misma cola.
    //
    //  Y tampoco los PARAMETROS: `fxP` es lo que la persona mueve y lo que se
    //  guarda; esto es lo que el hilo de audio se lleva de un bloque al
    //  siguiente.
    struct Inserto
    {
        //  FLT: el filtro del barrido bidireccional. Se sigue llamando
        //  `masterFilter` de cuando el FLT era el unico y era del master.
        juce::dsp::StateVariableTPTFilter<float> masterFilter;
        float smSweep = 0.0f;        // el barrido, suavizado como el resto
        float smReso  = 0.707f;
        bool  filterWasActive = false;
        bool  fltWasHigh      = false;   // de que lado del centro venia FLT

        //  HPF: su PROPIO filtro y no el de FLT conmutado, que es lo que hace
        //  que se pueda barrer una banda desde los dos extremos.
        juce::dsp::StateVariableTPTFilter<float> hpFilter;
        float smHpFreq = 200.0f, smHpReso = 0.707f, smHpMix = 0.0f;
        bool  hpWasActive = false;

        //  DRV: la saturacion y el paso bajo que va detras, porque saturar sin
        //  sitio donde poner los armonicos es solo aspereza.
        float smDrive   = 0.0f;
        float smDrvTone = 20000.0f, smDrvMix = 0.0f;
        float drvLp[2] { 0.0f, 0.0f };
        bool  drvWasActive = false;   // flanco de reactivacion: ver seccion 5b/3

        //  BIT: la retencion y la fase del diezmado.
        float crHold[2] { 0.0f, 0.0f };
        float crPhase = 0.0f;
        bool  crWasActive = false;

        //  EQ: el de cinco bandas. Es el efecto de la queja que abrio esta
        //  tanda -«solo es posible que funcione en un canal»- y ahora hay uno
        //  por canal.
        Eq5 eqFx;

        //  LA FAMILIA DE DINAMICA: CMP, GTE, DSS y LIM, una pieza con cuatro
        //  configuraciones. `dynIdx` hace la unica traduccion que queda.
        std::array<Dinamica, 4> dyn;
        //  Lo que esta bajando cada uno, para la casilla de lectura de CTRL 3.
        std::array<std::atomic<float>, 4> dynRed { { { 0.0f }, { 0.0f }, { 0.0f }, { 0.0f } } };

        //  TRM: el UNICO de los cuatro de modulacion que SUSTITUYE, asi que la
        //  familia se parte 3/1. CHO, FLA y PHA suman y su LFO se queda en la
        //  clase; el de TRM viene aqui, con su fase para el punto de trabajo
        //  del visor.
        Lfo   modTrm;
        float smTrmProf = 0.0f;
        bool  trmWasActive = false;
        std::atomic<float> trmFase { 0.0f };

        //  RNG: su oscilador es de AUDIO -llega a 4 kHz- y por eso no es un
        //  `Lfo`, que acota a 40 Hz a proposito.
        float rngFase = 0.0f, smRngAnillo = 0.0f;

        //  PIT: la linea que se lee a otra velocidad, con sus dos cabezas.
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> pitLine { 16384 };
        float pitFase = 0.0f, smPitSemis = 0.0f;

        //  WID y EXC: el MISMO cruce Linkwitz-Riley, uno para dejar los graves
        //  en mono y otro para saturar solo la banda alta.
        Dinamica::Svf widAlta[2][2], widBaja[2][2];
        float smWidAncho = 0.0f;
        Dinamica::Svf excAlta[2][2], excBaja[2][2];
        float smExcFuerza = 0.0f;

        //  TRN: los dos seguidores de envolvente, uno rapido y uno lento.
        float trnRapido = 0.0f, trnLento = 0.0f;
        float smTrnAtaque = 0.0f, smTrnCaida = 0.0f;

        //  FRZ: la ventana capturada y por donde va su lectura.
        std::array<std::vector<float>, 2> frzVent;
        int   frzEscritas = 0, frzLargo = 0;
        float frzLee = 0.0f;
        bool  frzLlena = false, frzOyo = false;
        float smFrzSuave = 0.0f;

        //  EL FLANCO de los seis de caracter, para limpiar su estado al volver
        //  a abrirse: sin el, un efecto que se reabre suena con la cola de la
        //  vez anterior. Es lo mismo que `modWasActive` y por lo mismo.
        std::array<bool, 6> carWasActive {};

        //  LOS SUAVIZADOS ARRANCAN YA EN SU DESTINO, y esto vive AQUI y no
        //  suelto en `copyStateFrom` por la razon que ese sitio ya lleva
        //  escrita: es la lista que se ha olvidado TRES veces. Con dieciseis
        //  insertos, escribirla a mano multiplica por dieciseis esa
        //  probabilidad — asi que la escribe quien es dueño de los campos.
        //
        //  Un motor vivo desliza en ~20 ms porque un mando acaba de moverse; un
        //  rebote no tiene ese pasado, y deslizar desde los defectos meteria el
        //  filtro en el primer compas de cada exportacion.
        void cebaSuavizados (const std::array<std::array<std::atomic<float>, 3>, kNumFx>& fila) noexcept
        {
            const auto v = [&fila] (int f, int par) noexcept
            { return fila[(size_t) f][(size_t) par].load (std::memory_order_relaxed); };

            smSweep   = v (kFxFlt, 0);
            smReso    = v (kFxFlt, 1);
            smHpFreq  = v (kFxHpf, 0);
            smHpReso  = v (kFxHpf, 1);
            smHpMix   = v (kFxHpf, 2);
            smDrive   = v (kFxDrv, 0);
            smDrvTone = v (kFxDrv, 1);
            smDrvMix  = v (kFxDrv, 2);
            smTrmProf = v (kFxTrm, 1);
        }
    };

    //  Y DIECISEIS, uno por canal. `Inserto` no tiene punteros ni reserva
    //  fuera de `frzVent` y `pitLine`, que se dimensionan en `prepareToPlay`
    //  como se dimensionaban antes.
    std::array<Inserto, kNumCanales> ins;

public:
    //  LA PUERTA ACOTADA, que es la hermana de `fxParamDe` para el ESTADO: el
    //  canal llega de la cara, del fichero de proyecto y de la automatizacion,
    //  y ninguno de los tres esta obligado a traerlo dentro del rango.
    Inserto&       insDe (int c) noexcept
    { return ins[(size_t) juce::jlimit (0, kNumCanales - 1, c)]; }
    const Inserto& insDe (int c) const noexcept
    { return ins[(size_t) juce::jlimit (0, kNumCanales - 1, c)]; }
private:

    //  EL RECORTE DEL PAD -uno por defecto- y EL ENVIO DEL CANAL. El producto
    //  de los dos es lo que llega al bus. Ver `setPadRecorte` y `setCanalSend`.
    std::array<std::array<std::atomic<float>, kNumFx>, kNumPads> padRecorte {};
    std::array<std::atomic<juce::uint8>, kNumPads> padCanal {};
    std::array<std::array<std::atomic<float>, kNumFx>, kNumCanales> canalSend {};
    std::array<std::atomic<float>, kNumCanales> canalGain {};
    std::array<std::atomic<bool>,  kNumCanales> canalMute {};
    //  Ver `miraCanal`: cual se mide y cuanto ha dado desde que se leyo.
    std::atomic<int>   canalMirado { -1 };
    std::atomic<float> canalPico   { 0.0f };
    //  Bit i puesto = el pad i manda a algun efecto. Ver refrescaSendMask.
    std::atomic<std::uint64_t> padSendMask { 0 };
    static_assert (kNumPads <= 64, "padSendMask es de 64 bits");

    //  UN BIT POR PAD, para que el hilo de audio no tenga que preguntar 1344
    //  veces por bloque si alguien manda algo a algun sitio.
    //
    //  El reparto de envios recorria kNumPads x kNumFx entero en CADA bloque
    //  -1344 cargas atomicas y 1344 pasos de suavizado- hiciera lo que hiciera
    //  la maquina. Con los efectos apagados y ni un pad sonando el trabajo era
    //  exactamente el mismo que con dieciseis pads sonando.
    //
    //  Y SE RECALCULA ENTERA y no un pad cada vez, que es lo que cambia al
    //  entrar el canal: el envio ya no es del pad, asi que mover UN numero del
    //  canal 3 cambia el bit de todos los pads que viven en el canal 3. Un
    //  recorrido de 64 x 21 en el hilo de mensajes cuando alguien mueve un
    //  fader es gratis; el fallo que evita es un pad que manda y al que el
    //  bloque se salta.
    void refrescaSendMask() noexcept
    {
        std::uint64_t m = 0;
        for (int p = 0; p < kNumPads; ++p)
        {
            const int c = (int) padCanal[(size_t) p].load (std::memory_order_relaxed);
            for (int f = 0; f < kNumFx; ++f)
                if (canalSend[(size_t) c][(size_t) f].load (std::memory_order_relaxed) > 0.0f
                    && padRecorte[(size_t) p][(size_t) f].load (std::memory_order_relaxed) > 0.0f)
                    { m |= 1ull << (unsigned) p; break; }
        }
        padSendMask.store (m, std::memory_order_relaxed);
    }
    std::array<std::array<float, kNumFx>, kNumPads> smSend {};    // audio thread only
    //  El fader del canal, suavizado POR PAD y no por canal: el pad es quien lo
    //  aplica -es su seco y sus envios los que se escalan- y guardarlo por
    //  canal obligaria a leer dos suavizados en el mismo bucle para saber si
    //  este pad ya termino de moverse. Nace en uno, como el fader.
    std::array<float, kNumPads> smCanalDePad {};
    //  Que pads siguen moviendose. Sin esto, saltarse un pad congelaba su
    //  envio a medio cerrar. Solo del hilo de audio, como smSend.
    std::array<bool, kNumPads> smSendHot {};
    //  Un buffer por BUS y no por tipo. Ver `busDe`: los primeros `kNumFx`
    //  siguen siendo los del tipo -que es lo que usan los cinco envios- y
    //  detras van los dieciseis insertos por los dieciseis canales.
    std::array<juce::AudioBuffer<float>, kNumBuses> fxBus;
    std::array<bool, kNumBuses> busRinging {};
    juce::AudioBuffer<float> padScratch;

    // Scope ring (post-FX mono), written by the audio thread.
    static constexpr int kScopeSize = 2048;   // power of two
    std::array<float, kScopeSize> scope {};
    std::atomic<int> scopeWrite { 0 };

    //  The decimated silhouette ring: one min/max column per scopeColLen
    //  frames. Audio thread writes, message thread copies.
    static constexpr int kScopeCols = kMaxScopeColumns;   // power of two
    std::array<float, kScopeCols> scopeColMin {};
    std::array<float, kScopeCols> scopeColMax {};
    std::atomic<int> scopeColWrite { 0 };
    int   scopeColLen = 740;              // set from the rate in prepareToPlay
    float colMin =  1.0e9f, colMax = -1.0e9f;   // audio-thread accumulators
    int   colCount = 0;

    // Running output peak per channel; UI consumes-and-resets via readOutPeak*.
    std::atomic<float> outPeakL { 0.0f }, outPeakR { 0.0f };

    // Diagnostic test tone.
    std::atomic<int> testToneRemaining { 0 };
    double           testPhase = 0.0;

    double systemSampleRate = 44100.0;
    int    maxBlock         = 512;
    bool   offlineMode      = false;   // a bounce clone: no record buffer, no meters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioEngine)
};

//  EL 64 ESTA ESCRITO DOS VECES Y LAS DOS COPIAS SE INDEXAN ENTRE SI.
//
//  `MidiIo::Bridge::gate[kMaxPads]` se indexa con el slot del motor, asi que un
//  quinto banco seria una escritura fuera de rango en el hilo de envio MIDI y
//  no habria nada que avisara. Esto deja de compilar el dia que dejen de
//  coincidir, que es lo mas barato que puede costar una regla duplicada.
static_assert (MidiIo::kMaxPads >= AudioEngine::kNumPads,
               "MidiIo::kMaxPads se ha quedado por debajo de los pads del motor");


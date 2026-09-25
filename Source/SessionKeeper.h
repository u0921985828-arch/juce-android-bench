#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"

// ============================================================================
//  SessionKeeper - la copia de tu trabajo que sobrevive a que maten el proceso.
//
//  Android no pregunta antes de reclamar una app. La pausa, y un rato despues
//  el proceso simplemente no esta; el siguiente arranque es en frio y con la
//  maquina vacia. ZATI contestaba a eso con autosave(), que reescribia el
//  project.xml del proyecto abierto - y no hacia absolutamente nada cuando no
//  habia ninguno abierto, que es justo el estado en el que un sampler pasa su
//  primera hora. Trocear un break, grabar cuatro pads del microfono, coger una
//  llamada, volver: nada.
//
//  Asi que hay un segundo proyecto, invisible, que nadie tiene que acordarse de
//  guardar. Vive fuera de Projects/ para no salir nunca en el navegador, y
//  tiene la misma forma que uno de verdad:
//
//      ZATI/.sesion/
//          state.xml          la maquina entera, mas el nombre del proyecto que
//                             estaba abierto (asi vuelve tambien la cabecera)
//          samples/pad01.wav  una copia de cada pad cargado
//
//  Escribir dieciseis WAV no es algo para lo que onPause tenga tiempo: Android
//  da unos segundos ahi antes de declarar la app colgada. Asi que el audio se
//  escribe DURANTE la sesion, por este hilo, un par de segundos despues de que
//  un pad cambie. Cuando la actividad se pausa normalmente no queda nada que
//  hacer y lo unico que se escribe es el XML, que es pequeno.
//
//  Nada de esto se engancha a los sitios que cambian un pad. A sync() se le
//  pasa el array vivo y lo compara contra lo ultimo que escribio, puntero a
//  puntero, y asi caza todos los caminos - cargar, trocear, microfono,
//  deshacer, abrir proyecto - incluidos los que se escriban despues de este
//  fichero.
// ============================================================================
class SessionKeeper : private juce::Thread
{
public:
    SessionKeeper();
    ~SessionKeeper() override;

    static juce::File folder();
    static juce::File stateFile();
    static juce::File padFile (int pad);

    //  Hay algo a lo que volver?
    //
    //  Y SI SOLO QUEDA EL TEMPORAL YA VALIDADO, se rescata: es lo que deja una
    //  muerte entre escribirlo y ponerlo encima, y es la sesion entera. Sin
    //  esto el arranque cargaba la fabrica y el escritor pisaba los 64 WAV.
    static bool exists();

    //  Hilo de mensajes. Compara los pads vivos con el ultimo juego escrito y
    //  encola lo que se haya movido. Sale bastante barato como para llamarlo
    //  desde el latido de la interfaz: sin cambios son dieciseis comparaciones
    //  de punteros.
    //
    //  Y SI HAY CAMBIOS, EL ESTADO VA DELANTE. `estado` construye el texto de
    //  state.xml y solo se llama cuando algun pad cambio: el escritor lo pone
    //  en disco ANTES de tocar ningun WAV. Sin eso un troceado borraba pad02 a
    //  pad16.wav a los dos segundos y state.xml seguia diciendo durante hasta
    //  veinte que esos quince pads tenian audio propio - una muerte en esa
    //  ventana (ANR, caida) volvia con quince pads «sin audio» (Tribunal
    //  2026-09, 7.3).
    void sync (const SampleBuffer::Ptr* live, int numPads,
               const std::function<juce::String()>& estado = {});

    //  El texto de state.xml para un estado y un proyecto. Hilo de mensajes:
    //  copiar el arbol es lo que lo hace legible desde otro hilo.
    static juce::String textoDe (const juce::ValueTree& state, const juce::String& projectName);

    //  Encarga el estado al escritor y vuelve. Es el guardado de cada veinte
    //  segundos: escribirlo aqui eran 85 KB por FUSE en el hilo de mensajes
    //  (Tribunal 2026-09, 4.6). Si el texto es el mismo que ya esta en disco
    //  no se escribe (8.4): con la app quieta eran 180 escrituras por hora.
    void pideEstado (juce::String texto);

    //  Dar por escritos los pads vivos, sin encolar nada. Se usa justo despues
    //  de una recuperacion: esos buffers salieron de esta misma carpeta.
    void adopt (const SampleBuffer::Ptr* live, int numPads);

    //  Sincrono: lo usa `onPause`, que no puede fiarse de que el proceso siga
    //  vivo para ver acabar a otro hilo. Devuelve si quedo en disco.
    bool writeState (const juce::ValueTree& state, const juce::String& projectName);

    //  Espera a que la cola se vacie. Devuelve false si se agota el plazo, y
    //  quien llama sigue de todas formas: que la espera tenga tope es el
    //  objetivo.
    bool flush (int timeoutMs);

    //  Ya no hay sesion, y el siguiente arranque empieza limpio.
    void clear();

    //  CUANDO SE ESCRIBIO POR ULTIMA VEZ, para que la interfaz lo pueda DECIR.
    //
    //  Esto guarda los pads cada dos segundos y el estado entero cada veinte, y
    //  hasta ahora no lo contaba nadie: no habia indicador, ni punto, ni estado
    //  sucio. Quien no produce musica no sabe que esta a salvo, asi que o guarda
    //  compulsivamente o no guarda nunca — y el unico mensaje que existia salia
    //  al RECUPERAR, o sea cuando ya te habias llevado el susto.
    //
    //  Es un instante y no un booleano «guardando»: lo que tranquiliza no es que
    //  este ocupado ahora, es cuanto hace que lo que tienes delante quedo
    //  escrito. Ver `lineaDeContinuidad`.
    //
    //  ATOMICO Y SIN CERROJO porque lo escribe el hilo del escritor y lo lee el
    //  temporizador de la interfaz. Coger `lock` para leer una pista desde el
    //  hilo que casi siempre lo tiene es como `flush` acabaria esperandose a si
    //  mismo — el mismo razonamiento que ya tiene `hurry` tres lineas mas abajo.
    //  Cero es «todavia no ha escrito nada».
    juce::int64 ultimaEscrituraMs() const noexcept
        { return escrituraMs.load (std::memory_order_relaxed); }

    //  Banco: cuantos estados se escribieron y cuantos se ahorraron por ser
    //  iguales al que ya estaba en disco.
    int estadosEscritos() const noexcept { return escritos.load(); }
    int estadosIguales()  const noexcept { return iguales.load(); }


private:
    void run() override;
    bool isIdle() const;
    bool escribeEstado (const juce::String& texto);

    juce::CriticalSection lock;

    //  Todos los arrays de abajo van bajo `lock`.
    //
    //  `seen` mantiene una referencia a proposito. Comparar direcciones peladas
    //  sin ella estaria mal: se libera un buffer, se carga otro, y el asignador
    //  puede devolver la misma direccion - un pad que cambio y compara igual.
    //  Sostener la referencia lo hace imposible y no cuesta nada, porque la
    //  interfaz esta sujetando esos mismos objetos de todas formas.
    //
    //  Al menos AudioEngine::kNumPads. Era 32 cuando la maquina tenia 16, y
    //  pasar a 64 habria dejado de proteger la mitad sin avisar - sync() y
    //  adopt() acotan las dos con jmin.
    static constexpr int kMaxPads = 64;
    std::array<SampleBuffer::Ptr, kMaxPads> seen, queued;
    std::array<bool, kMaxPads> dirty {};
    //  De que pad sale el audio de cada uno: el indice mas bajo que comparte su
    //  buffer, o -1 si esta vacio. Ver sync().
    //  Cero-inicializado seria "todos salen del pad 0", que es lo contrario
    //  de vacio: el constructor lo pone a -1.
    std::array<int, kMaxPads> ownedBy;
    bool writing = false;

    //  Lo pone flush() y lo lee el escritor. Sin `lock`: es una pista, y coger
    //  el cerrojo para leer una pista desde el hilo que lo tiene casi siempre es
    //  como un flush acaba esperandose a si mismo.
    std::atomic<bool> hurry { false };

    //  Ver ultimaEscrituraMs(). La ponen los DOS caminos que dejan algo escrito
    //  en disco -el estado en `writeState` y cada pad en `run`- porque las dos
    //  cosas son «tu trabajo esta a salvo» y contar solo una mentiria la mitad
    //  del tiempo.
    std::atomic<juce::int64> escrituraMs { 0 };

    //  El estado encargado y todavia no escrito, bajo `lock`. Vacio es nada.
    juce::String estadoPendiente;

    //  Lo ultimo que llego a disco, y el cerrojo que serializa las dos manos
    //  que escriben state.xml: el escritor y `writeState` desde `onPause`. Sin
    //  el, las dos escribirian el MISMO state.xml.tmp a la vez.
    juce::CriticalSection escribiendoEstado;
    juce::String ultimoEstado;
    std::atomic<int> escritos { 0 }, iguales { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionKeeper)
};

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
    static bool exists() { return stateFile().existsAsFile(); }

    //  Hilo de mensajes. Compara los pads vivos con el ultimo juego escrito y
    //  encola lo que se haya movido. Sale bastante barato como para llamarlo
    //  desde el latido de la interfaz: sin cambios son dieciseis comparaciones
    //  de punteros.
    void sync (const SampleBuffer::Ptr* live, int numPads);

    //  Dar por escritos los pads vivos, sin encolar nada. Se usa justo despues
    //  de una recuperacion: esos buffers salieron de esta misma carpeta.
    void adopt (const SampleBuffer::Ptr* live, int numPads);

    //  Hilo de mensajes, sincrono, pequeno.
    void writeState (const juce::ValueTree& state, const juce::String& projectName);

    //  Espera a que la cola se vacie. Devuelve false si se agota el plazo, y
    //  quien llama sigue de todas formas: que la espera tenga tope es el
    //  objetivo.
    bool flush (int timeoutMs);

    //  Ya no hay sesion, y el siguiente arranque empieza limpio.
    void clear();

private:
    void run() override;
    bool isIdle() const;

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
    bool writing = false;

    //  Lo pone flush() y lo lee el escritor. Sin `lock`: es una pista, y coger
    //  el cerrojo para leer una pista desde el hilo que lo tiene casi siempre es
    //  como un flush acaba esperandose a si mismo.
    std::atomic<bool> hurry { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionKeeper)
};

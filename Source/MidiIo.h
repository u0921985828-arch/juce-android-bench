#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>

// ============================================================================
//  MidiIo — la app deja de estar sola.
//
//  Hasta aqui Zati era un instrumento cerrado: lo que sonaba salia por el
//  altavoz y no habia forma de que nada de fuera lo tocara ni de que el
//  secuenciador tocara nada. Eso esta bien para una caja de ritmos de bolsillo
//  y deja de estarlo en cuanto hay una mesa delante, que es donde se decide si
//  una app se usa de verdad o se prueba una tarde.
//
//  Dos caminos, y ninguno de los dos puede tocar el hilo de audio con las
//  manos:
//
//  SALIDA. Cada golpe que suena manda una nota. El secuenciador de esta app
//  pasa a ser un cerebro para el hardware de al lado, que es exactamente el
//  movimiento que hizo la competencia en 2026. El disparo nace en el hilo de
//  AUDIO - triggerPad - y enviar MIDI reserva memoria y habla con el sistema,
//  o sea las dos cosas que ese hilo no puede hacer. Asi que el audio solo
//  escribe un POD en una cola sin cerrojos y sigue; quien envia es otro hilo.
//
//  Y otro hilo PROPIO, no el temporizador de la interfaz. El temporizador late
//  cada 60 ms: mandar las notas ahi les meteria hasta 60 ms de retraso y un
//  jitter del mismo orden, en una app cuyo argumento entero es la latencia.
//  Un hilo que espera en la cola y envia en cuanto hay algo cuesta lo mismo
//  que SessionKeeper y no arrastra la unica cifra que aqui importa.
//
//  ENTRADA. Un teclado dispara los pads. JUCE entrega el MIDI en un hilo suyo
//  de prioridad alta, y ahi la tentacion es empujar a la cola de comandos que
//  ya existe - y seria un fallo silencioso y permanente: esa cola es SPSC de
//  UN SOLO PRODUCTOR POR CONTRATO (ver CommandFifo.h), y dos productores no la
//  degradan, la atascan. Por eso la entrada MIDI tiene su PROPIA cola y el
//  hilo de audio drena las dos. Cada cola sigue teniendo un productor, nadie
//  espera a nadie, y el retardo sigue siendo un bloque.
//
//  La nota de un pad es 36 + pad, que es do1 hacia arriba: el mapa que usan
//  todas las cajas de ritmos y el unico que hace que enchufar algo funcione
//  sin configurar nada. 64 pads llegan a la nota 99, dentro de rango.
// ============================================================================
namespace MidiIo
{
    static constexpr int kBaseNote = 36;    // C1, el do de la caja de ritmos
    static constexpr int kMaxPads  = 64;

    inline int noteForPad (int pad) noexcept
    {
        return juce::jlimit (0, 127, kBaseNote + pad);
    }

    inline int padForNote (int note) noexcept
    {
        const int pad = note - kBaseNote;
        return juce::isPositiveAndBelow (pad, kMaxPads) ? pad : -1;
    }

    // ------------------------------------------------------------------------
    //  Y LA OTRA MITAD: UN TECLADO TOCANDO UN INSTRUMENTO.
    //
    //  `padForNote` es el mapa de una CAJA DE RITMOS -una nota, un pad- y es el
    //  correcto para lo que esta app era. Con un pad de instrumento debajo es
    //  un sinsentido medido: una escala de DO sobre un pad con GRAND dispara
    //  ocho PADS distintos, o sea ocho sonidos distintos, y las notas que caen
    //  fuera de 36..99 no suenan. Enchufar un teclado maestro y tocar una
    //  escala era, literalmente, lo unico que esta app no podia hacer.
    //
    //  Dos modos y no un ajuste dentro del otro: quien tiene un controlador de
    //  pads quiere lo de siempre y quien tiene un teclado quiere un
    //  instrumento, y son dos aparatos distintos encima de la misma mesa.
    enum class Modo : std::uint8_t { Pads = 0, Teclado = 1 };

    //  DO3, la misma raiz que el teclado de la ficha del pad y que el piano
    //  roll: el semitono que viaja es la distancia a ella, y el motor lo suma
    //  a la afinacion del pad. Asi tocar la raiz suena EXACTAMENTE como tocar
    //  el pad con el dedo, que es lo que hace que los dos caminos se puedan
    //  comparar.
    static constexpr int kNotaRaiz = 60;
    //  El motor admite +-48 semitonos; pedir mas es pedir un `ratio` que no
    //  significa nada. Se acota aqui y no en quien llama porque esto es la
    //  puerta: lo que entra viene de un aparato de fuera.
    static constexpr int kSemisTope = 48;

    struct Mapa
    {
        Modo modo    = Modo::Pads;
        int  padDest = 0;              // en TECLADO, el pad que se toca
        int  raiz    = kNotaRaiz;
    };

    struct Evento
    {
        enum class Tipo : std::uint8_t { Nada, NoteOn, NoteOff, TodasOff, Pedal };
        Tipo  tipo  = Tipo::Nada;
        int   pad   = -1;
        int   semis = 0;
        float vel   = 1.0f;
        bool  abajo = false;           // solo en Pedal
    };

    //  LA DECISION, SEPARADA DEL TRANCE.
    //
    //  Esto vivia dentro de `handleIncomingMidiMessage`, que es un `override`
    //  privado al que solo puede llamar JUCE con un aparato de verdad
    //  enchufado: o sea que el mapa de entrada -la unica parte con reglas- era
    //  la pieza del motor que NINGUNA medida podia tocar. Es la misma figura
    //  que `busDe`: una funcion pura a la que el banco le da un mensaje y le
    //  pregunta que sale.
    inline Evento traduce (const juce::MidiMessage& m, const Mapa& mapa) noexcept
    {
        Evento e;

        const auto destino = [&mapa] (int nota, int& pad, int& semis) noexcept
        {
            if (mapa.modo == Modo::Teclado)
            {
                if (! juce::isPositiveAndBelow (mapa.padDest, kMaxPads)) return false;
                pad   = mapa.padDest;
                semis = juce::jlimit (-kSemisTope, kSemisTope, nota - mapa.raiz);
                return true;
            }
            pad   = padForNote (nota);
            semis = 0;
            return pad >= 0;
        };

        //  Velocidad cero es un apagado disfrazado, y lleva siendolo desde
        //  1983. Un teclado de cada tres los manda asi.
        if (m.isNoteOn() && m.getVelocity() > 0)
        {
            if (! destino (m.getNoteNumber(), e.pad, e.semis)) return {};
            e.tipo = Evento::Tipo::NoteOn;
            e.vel  = (float) m.getVelocity() / 127.0f;
        }
        else if (m.isNoteOff() || m.isNoteOn())
        {
            if (! destino (m.getNoteNumber(), e.pad, e.semis)) return {};
            e.tipo = Evento::Tipo::NoteOff;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            e.tipo = Evento::Tipo::TodasOff;
        }
        //  CC 64. Va DESPUES de los dos de arriba porque los tres son
        //  mensajes de control y el orden decide: `isAllNotesOff` es el 123 y
        //  el pedal el 64, asi que no se pisan, pero preguntar por el pedal
        //  primero seria confiar en eso.
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
        {
            e.tipo  = Evento::Tipo::Pedal;
            e.abajo = m.isSustainPedalOn();
        }

        return e;
    }

    //  Lo que el hilo de audio deja escrito. POD, sin punteros y sin nada que
    //  construir: escribirlo son cuatro bytes.
    struct NoteEvent
    {
        std::uint8_t pad = 0;
        std::uint8_t vel = 100;
        bool         on  = true;
        std::int8_t  semis = 0;   // secuenciador#11: la tecla del piano roll
        std::uint16_t gateMs = 0; // secuenciador#11: el largo; cero es la puerta de siempre
        enum class Tipo : std::uint8_t { Nota, Reloj, Arranca, Para };
        Tipo         tipo = Tipo::Nota;   // secuenciador#11: reloj 24 ppq, Start y Stop
        float        retrasoMs = 0.0f;    // midi#reloj: donde cae dentro del bloque
    };

    //  secuenciador#11: lo que no es nota sale tal cual.
    inline juce::MidiMessage mensajeDe (const NoteEvent& e) noexcept
    {
        switch (e.tipo)
        {
            case NoteEvent::Tipo::Reloj:   return juce::MidiMessage::midiClock();
            case NoteEvent::Tipo::Arranca: return juce::MidiMessage::midiStart();
            case NoteEvent::Tipo::Para:    return juce::MidiMessage::midiStop();
            case NoteEvent::Tipo::Nota:
            default:                       return {};
        }
    }

    inline int notaDe (const NoteEvent& e) noexcept { return juce::jlimit (0, 127, noteForPad (e.pad) + e.semis); }

    //  La cola audio -> hilo de envio. Misma forma que CommandFifo y que
    //  RetiredQueue, por la misma razon: un productor, un consumidor, y ni una
    //  reserva en el lado del audio.
    class NoteFifo
    {
    public:
        //  Hilo de AUDIO. Si esta llena se tira el evento: perder una nota de
        //  un aparato externo es un fallo pequeno, y esperar en el hilo de
        //  audio es un fallo que se oye.
        bool push (const NoteEvent& e) noexcept
        {
            int s1, z1, s2, z2;
            fifo.prepareToWrite (1, s1, z1, s2, z2);
            if (z1 + z2 < 1) { dropped.fetch_add (1, std::memory_order_relaxed); return false; }
            (z1 > 0 ? store[(size_t) s1] : store[(size_t) s2]) = e;
            fifo.finishedWrite (z1 + z2);
            return true;
        }

        template <typename Fn>
        void drain (Fn&& fn) noexcept
        {
            int s1, z1, s2, z2;
            fifo.prepareToRead (fifo.getNumReady(), s1, z1, s2, z2);
            for (int i = 0; i < z1; ++i) fn (store[(size_t) (s1 + i)]);
            for (int i = 0; i < z2; ++i) fn (store[(size_t) (s2 + i)]);
            fifo.finishedRead (z1 + z2);
        }

        int  numReady() const noexcept { return fifo.getNumReady(); }
        int  takeDropped() noexcept { return dropped.exchange (0, std::memory_order_relaxed); }

    private:
        static constexpr int cap = 256;
        juce::AbstractFifo fifo { cap };
        std::array<NoteEvent, cap> store {};
        std::atomic<int> dropped { 0 };
    };

    //  midi#acorde: una puerta por nota y no por pad; el acorde de un pad son varias notas con su largo cada una.
    struct Puertas
    {
        struct Puerta { double hasta = 0.0; int pad = -1, nota = 0, canal = 1; };
        std::array<Puerta, kMaxPads> p {};

        //  `envia (on, canal, nota, vel)`.
        template <typename Envia>
        void enciende (int pad, int nota, int canal, int vel, double hasta, Envia&& envia)
        {
            //  La misma nota otra vez: se apaga y se reusa. Si no, un hueco; sin hueco, la que antes vence.
            Puerta* q = nullptr;
            for (auto& x : p)
                if (x.hasta > 0.0 && x.canal == canal && x.nota == nota) { q = &x; break; }
            if (q == nullptr)
                for (auto& x : p)
                    if (x.hasta <= 0.0) { q = &x; break; }
            if (q == nullptr)
            {
                q = &p[0];
                for (auto& x : p) if (x.hasta < q->hasta) q = &x;
            }
            if (q->hasta > 0.0) envia (false, q->canal, q->nota, 0);
            envia (true, canal, nota, vel);
            *q = { hasta, pad, nota, canal };
        }

        template <typename Envia>
        void apaga (int pad, Envia&& envia)
        {
            for (auto& q : p)
                if (q.hasta > 0.0 && q.pad == pad) { envia (false, q.canal, q.nota, 0); q.hasta = 0.0; }
        }

        //  Cierra las vencidas y dice si queda alguna abierta.
        template <typename Envia>
        bool vence (double ahora, Envia&& envia)
        {
            bool abiertas = false;
            for (auto& q : p)
            {
                if (q.hasta <= 0.0) continue;
                if (q.hasta <= ahora) { envia (false, q.canal, q.nota, 0); q.hasta = 0.0; }
                else abiertas = true;
            }
            return abiertas;
        }

        template <typename Envia>
        void todas (Envia&& envia) { vence (std::numeric_limits<double>::max(), envia); }
    };

    //  EL PUENTE: los aparatos, y los dos hilos que no son el de audio.
    //
    //  Vive en el hilo de mensajes salvo por su propio hilo de envio. Abrir y
    //  cerrar aparatos, listar puertos y hablar con ALSA o con Android son
    //  cosas del hilo de mensajes; enviar es del hilo de dentro.
    class Bridge : private juce::Thread,
                   private juce::MidiInputCallback
    {
    public:
        //  Que hacer con una nota que llega. La pone MainComponent y siempre
        //  acaba en engine.postNoteOnFromMidi, que empuja a la cola de MIDI y
        //  no a la de comandos. Se llama desde el hilo de JUCE, no del de
        //  mensajes: lo que haya dentro tiene que aguantarlo.
        //  CON EL SEMITONO, que es la mitad que faltaba: sin el, el modo
        //  TECLADO no puede existir -todas las teclas dirian la misma nota- y
        //  el modo PADS manda un cero, que es exactamente lo de antes.
        std::function<void (int pad, int semis, float vel)> onNoteOn;
        std::function<void (int pad, int semis)>            onNoteOff;
        std::function<void ()>                              onTodasOff;
        std::function<void (bool abajo)>                    onPedal;

        Bridge() : juce::Thread ("zati-midi-out") {}

        ~Bridge() override
        {
            closeInput();
            //  Primero se para el hilo y DESPUES se suelta el aparato: al
            //  reves, el hilo puede estar dentro de sendMessageNow sobre un
            //  puntero que acaba de morir.
            signalThreadShouldExit();
            notify();
            if (stopThread (1000)) allNotesOff();   // con el hilo muerto no se escribe en su salida (estabilidad#29)
            out.reset();
        }

        // --- Lo que hay enchufado (hilo de mensajes) ----------------------
        static juce::StringArray outputNames()
        {
            juce::StringArray n;
            for (const auto& d : juce::MidiOutput::getAvailableDevices()) n.add (d.name);
            return n;
        }

        static juce::StringArray inputNames()
        {
            juce::StringArray n;
            for (const auto& d : juce::MidiInput::getAvailableDevices()) n.add (d.name);
            return n;
        }

        // --- Salida --------------------------------------------------------
        bool openOutput (const juce::String& name)
        {
            closeOutput();
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                if (d.name == name)
                {
                    out = juce::MidiOutput::openDevice (d.identifier);
                    break;
                }
            if (out == nullptr) return false;

            if (! isThreadRunning())
                startThread (juce::Thread::Priority::high);
            return true;
        }

        void closeOutput()
        {
            if (out == nullptr) return;
            signalThreadShouldExit();
            notify();
            if (stopThread (1000)) allNotesOff();   // con el hilo muerto no se escribe en su salida (estabilidad#29)
            out.reset();
        }

        bool hasOutput() const noexcept { return out != nullptr; }

        //  DE DONDE LEE. El puente no es dueno de la cola: la cola vive en el
        //  motor, porque el que escribe es el hilo de audio y el motor es
        //  quien lo tiene. Tener una aqui tambien fue el primer intento, y
        //  eran dos colas que no se hablaban: el audio escribia en una y este
        //  hilo vaciaba la otra, para siempre vacia, sin un solo error.
        void setSource (NoteFifo& q) noexcept { src = &q; }

        //  Canal 1..16. Diez es el de percusion por convenio general MIDI, y
        //  es el que espera cualquier caja de ritmos que se enchufe sin
        //  configurar nada.
        void setChannel (int ch) noexcept { channel.store (juce::jlimit (1, 16, ch), std::memory_order_relaxed); }
        int  getChannel() const noexcept { return channel.load (std::memory_order_relaxed); }

        // --- Entrada -------------------------------------------------------
        bool openInput (const juce::String& name)
        {
            closeInput();
            for (const auto& d : juce::MidiInput::getAvailableDevices())
                if (d.name == name)
                {
                    in = juce::MidiInput::openDevice (d.identifier, this);
                    break;
                }
            if (in == nullptr) return false;
            in->start();
            return true;
        }

        void closeInput()
        {
            if (in != nullptr) { in->stop(); in.reset(); }
        }

        bool hasInput() const noexcept { return in != nullptr; }

        //  EL MAPA DE ENTRADA, en atomicos: lo ESCRIBE el hilo de mensajes -la
        //  tapa de la pagina MIDI, y la seleccion de pad- y lo LEE el hilo con
        //  el que JUCE entrega el MIDI. Dos enteros sueltos y no una struct
        //  con cerrojo: leer el modo un mensaje tarde manda una nota al pad de
        //  antes, que es lo peor que puede pasar aqui.
        void setModo (Modo m) noexcept { modo.store ((int) m, std::memory_order_relaxed); }
        Modo getModo() const noexcept { return (Modo) modo.load (std::memory_order_relaxed); }
        void setPadTeclado (int pad) noexcept { padTeclado.store (pad, std::memory_order_relaxed); }

        Mapa mapaActual() const noexcept
        {
            Mapa m;
            m.modo    = getModo();
            m.padDest = padTeclado.load (std::memory_order_relaxed);
            return m;
        }

    private:
        //  EL HILO DE ENVIO. Espera en la cola y manda en cuanto hay algo.
        //
        //  Y las notas se APAGAN SOLAS. Un pad es un golpe: no tiene final que
        //  esperar, asi que si solo se mandara el encendido el aparato de al
        //  lado se quedaria con la nota pisada para siempre. Sesenta
        //  milisegundos de puerta es lo que hace cualquier caja de ritmos -
        //  bastante para que un modulo de percusion dispare su envolvente
        //  entera, bastante poco para que dos golpes seguidos del mismo pad no
        //  se pisen a 300 pulsaciones por minuto.
        void run() override
        {
            static constexpr int kGateMs = 60;

            while (! threadShouldExit())
            {
                bool didSomething = false;
                const double ahora = juce::Time::getMillisecondCounterHiRes();

                const auto envia = [this] (bool on, int canal, int nota, int vel)
                {
                    out->sendMessageNow (on ? juce::MidiMessage::noteOn (canal, nota, (juce::uint8) vel)
                                            : juce::MidiMessage::noteOff (canal, nota));
                };

                if (src != nullptr) src->drain ([this, &didSomething, ahora, &envia] (const NoteEvent& e) noexcept
                {
                    if (out == nullptr) return;
                    didSomething = true;
                    if (e.tipo == NoteEvent::Tipo::Reloj && e.retrasoMs > 0.0f && relojes.size() < relojes.max_size())
                    {
                        relojes.push (ahora + (double) e.retrasoMs);   // midi#reloj: sale cuando le toca, no con el bloque
                        return;
                    }
                    if (e.tipo != NoteEvent::Tipo::Nota)   // secuenciador#11
                    {
                        if (e.tipo == NoteEvent::Tipo::Para) relojes.vacia();
                        if (e.tipo != NoteEvent::Tipo::Reloj) corriendo = (e.tipo == NoteEvent::Tipo::Arranca);
                        out->sendMessageNow (mensajeDe (e));
                        return;
                    }
                    const int ch = channel.load (std::memory_order_relaxed);
                    if (e.on)
                        puertas.enciende (e.pad, notaDe (e), ch, e.vel,
                                          ahora + (e.gateMs > 0 ? (double) e.gateMs : (double) kGateMs), envia);   // secuenciador#11
                    else
                        puertas.apaga (e.pad, envia);
                });

                //  Las puertas que vencen, por reloj y no por vueltas: una
                //  vuelta de un milisegundo dura mas de uno, y en una nota de
                //  dos segundos eso se nota (secuenciador#11).
                const double luego = juce::Time::getMillisecondCounterHiRes();
                while (out != nullptr && relojes.size() > 0 && relojes.primero() <= luego)
                {
                    relojes.saca();
                    out->sendMessageNow (juce::MidiMessage::midiClock());
                }
                const bool anyOpen = out != nullptr && puertas.vence (luego, envia);

                if (anyOpen || didSomething || corriendo || (src != nullptr && src->numReady() > 0))
                    juce::Thread::sleep (1);
                else
                    wait (50);      // nada abierto y nada que mandar: a dormir
            }
        }

        //  Nadie se queda con una nota pisada porque la app se cerro o porque
        //  se cambio de aparato. Es lo primero que se echa de menos y lo
        //  ultimo que se recuerda escribir.
        void allNotesOff()
        {
            if (out == nullptr) return;
            relojes.vacia();
            const int ch = channel.load (std::memory_order_relaxed);
            puertas.todas ([this] (bool, int canal, int nota, int) { out->sendMessageNow (juce::MidiMessage::noteOff (canal, nota)); });
            if (corriendo) { out->sendMessageNow (juce::MidiMessage::midiStop()); corriendo = false; }   // secuenciador#11
            out->sendMessageNow (juce::MidiMessage::allNotesOff (ch));
        }

        //  ENTRADA. Hilo de JUCE, ni el de mensajes ni el de audio: aqui solo
        //  se traduce y se empuja, y quien recibe lo mete en la cola de MIDI.
        //  Y AQUI YA NO SE DECIDE NADA: se traduce con `traduce` -una funcion
        //  pura que el banco si puede llamar- y se reparte. Todo lo que tenia
        //  reglas vivia dentro de este `override` privado, o sea en el unico
        //  sitio de la app al que ninguna medida llega.
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override
        {
            const auto e = traduce (m, mapaActual());

            switch (e.tipo)
            {
                case Evento::Tipo::NoteOn:
                    if (onNoteOn) onNoteOn (e.pad, e.semis, e.vel);
                    break;
                case Evento::Tipo::NoteOff:
                    if (onNoteOff) onNoteOff (e.pad, e.semis);
                    break;
                case Evento::Tipo::TodasOff:
                    if (onTodasOff) onTodasOff();
                    break;
                case Evento::Tipo::Pedal:
                    if (onPedal) onPedal (e.abajo);
                    break;
                case Evento::Tipo::Nada:
                default:
                    break;
            }
        }

        std::unique_ptr<juce::MidiOutput> out;
        std::unique_ptr<juce::MidiInput>  in;
        NoteFifo*         src { nullptr };   // vive en el motor, ver setSource
        std::atomic<int>  channel { 10 };      // percusion, por convenio
        std::atomic<int>  modo { (int) Modo::Pads };   // lo de siempre por defecto
        std::atomic<int>  padTeclado { 0 };
        Puertas           puertas;              // midi#acorde: cada nota con su puerta, su canal y su pad
        struct Relojes   // midi#reloj: los pendientes, en orden y sin reservar
        {
            std::array<double, 64> t {};
            size_t ini = 0, n = 0;
            size_t size() const noexcept { return n; }
            static constexpr size_t max_size() noexcept { return 64; }
            void push (double x) noexcept { t[(ini + n++) % 64] = x; }
            double primero() const noexcept { return t[ini]; }
            void saca() noexcept { ini = (ini + 1) % 64; --n; }
            void vacia() noexcept { ini = n = 0; }
        } relojes;
        bool              corriendo = false;    // secuenciador#11: entre Start y Stop el hilo no duerme
    };
}

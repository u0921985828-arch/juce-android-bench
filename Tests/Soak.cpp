// ============================================================================
//  Soak - un millon de sesiones distintas, cada una con su semilla.
//
//  Las otras pruebas del banco miden UNA cosa cada una y la miden bien: el
//  delay contra lo que promete el mando, el filtro contra su propia curva, la
//  sesion contra el disco. Todas son deterministas y todas prueban el camino
//  que alguien penso. Lo que no cubre ninguna es la COMBINACION: cambiar de
//  ruta mientras dieciseis pads suenan y el secuenciador va, con el filtro
//  cerrado, la reverberacion abierta y una muestra de dos milisegundos en el
//  pad que se esta redisparando. Nadie escribe esa prueba a mano porque nadie
//  se la imagina; es la que aparece sola cuando mucha gente usa la app.
//
//  Asi que cada "persona" es una semilla: un aparato -frecuencia de reloj y
//  tamano de bloque de los que hay en la calle-, un puñado de sonidos de
//  formas distintas, y una tirada de acciones sacadas de lo que se hace de
//  verdad con un sampler. Se ejecuta, y de cada una se comprueba lo unico que
//  no puede pasar nunca:
//
//    * NaN o infinito en la salida. Se propaga por el bus, por el delay
//      realimentado y por el master, y la app se queda muda hasta reiniciar.
//    * Una voz colgada: despues de un panico y de un cuarto de segundo, el
//      motor tiene que estar en silencio. Si no, un pad se quedo sonando para
//      siempre y ocupando su hueco del deposito.
//    * Salida por encima de lo que el limitador de seguridad permite. El
//      master satura a 0.944 y limita: nada puede salir por encima de 1.
//
//  La semilla lo hace reproducible: cuando una falla, se vuelve a correr esa y
//  solo esa. Un fallo que solo aparece en una de cada cien mil corridas no se
//  arregla si no se puede volver a provocar.
//
//      cmake -B build -DZATI_BENCH=ON && cmake --build build --target Soak
//      ./build/Soak_artefacts/Soak [personas] [hilos]
// ============================================================================
#include <JuceHeader.h>
#include "../Source/AudioEngine.h"
#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{
    struct Fallo
    {
        juce::uint32 semilla = 0;
        int          clase   = 0;      // 0 NaN, 1 voz colgada, 2 fuera de rango
        int          accion  = 0;      // la ultima que se ejecuto
    };

    const char* nombreClase (int c)
    {
        switch (c)
        {
            case 0:  return "NaN o infinito en la salida";
            case 1:  return "voz colgada: no calla tras el panico";
            default: return "salida fuera de rango";
        }
    }

    //  Los aparatos que hay en la calle, no los que son comodos de probar. Un
    //  movil de gama baja abre a 44.1 con bloques de 512 y uno reciente a 48
    //  con 96, y el motor tiene que dar igual en los dos.
    struct Aparato { double rate; int bloque; };
    const Aparato kAparatos[] = {
        { 48000.0,  96 }, { 48000.0, 128 }, { 48000.0, 192 }, { 48000.0, 256 },
        { 44100.0, 128 }, { 44100.0, 256 }, { 44100.0, 512 }, { 48000.0, 512 },
    };

    //  Sonidos de formas distintas, y ninguno "bonito": lo que rompe cosas es
    //  una muestra de dos milisegundos, una de un solo canal, una que empieza
    //  a tope y una que es casi silencio.
    SampleBuffer::Ptr sonido (juce::Random& r, double rate)
    {
        auto* sb = new SampleBuffer();
        const int forma = r.nextInt (5);
        const int n = forma == 0 ? 64                      // dos milisegundos
                    : forma == 1 ? (int) (rate * 0.02)
                    : forma == 2 ? (int) (rate * 0.4)
                    : forma == 3 ? (int) (rate * 2.0)
                                 : (int) (rate * 0.15);
        const int ch = r.nextBool() ? 2 : 1;
        sb->buffer.setSize (ch, juce::jmax (8, n));
        sb->sourceSampleRate = r.nextBool() ? rate : (rate == 48000.0 ? 44100.0 : 48000.0);

        const float amp = forma == 3 ? 0.02f : 0.9f;
        const float hz  = 40.0f + r.nextFloat() * 6000.0f;
        for (int c = 0; c < ch; ++c)
        {
            float* d = sb->buffer.getWritePointer (c);
            for (int i = 0; i < sb->buffer.getNumSamples(); ++i)
            {
                const float t = (float) i / (float) rate;
                d[i] = amp * (0.7f * std::sin (juce::MathConstants<float>::twoPi * hz * t)
                              + 0.3f * (r.nextFloat() * 2.0f - 1.0f));
            }
        }
        return SampleBuffer::Ptr (sb);
    }

    //  UNA PERSONA. Devuelve el fallo si lo hubo.
    bool unaSesion (juce::uint32 semilla, Fallo& fallo)
    {
        juce::Random r ((juce::int64) semilla);
        const auto& ap = kAparatos[r.nextInt (juce::numElementsInArray (kAparatos))];

        AudioEngine e;
        e.prepareToPlay (ap.rate, ap.bloque);
        e.setPolyphony (r.nextInt ({ 8, 49 }), r.nextInt ({ 2, 9 }));

        juce::AudioBuffer<float> out (2, ap.bloque);

        //  Entre dos y ocho pads cargados, que es lo que tiene un kit a medio
        //  montar, y no los sesenta y cuatro: lo raro es tenerlos todos.
        const int cargados = r.nextInt ({ 2, 9 });
        for (int k = 0; k < cargados; ++k)
            e.publishSample (r.nextInt (AudioEngine::kNumPads), sonido (r, ap.rate));

        const int acciones = r.nextInt ({ 20, 90 });
        int ultima = -1;

        for (int a = 0; a < acciones; ++a)
        {
            const int pad = r.nextInt (AudioEngine::kNumPads);
            ultima = r.nextInt (18);

            switch (ultima)
            {
                case 0:  e.postNoteOn (pad, r.nextFloat()); break;
                case 1:  e.postNoteOff (pad); break;
                case 2:  e.setPadPitch (pad, -24.0f + r.nextFloat() * 48.0f); break;
                case 3:  e.setPadGain (pad, r.nextFloat() * 4.0f); break;
                case 4:  e.setPadCutoff (pad, 20.0f + r.nextFloat() * 20000.0f);
                         e.setPadReso (pad, r.nextFloat()); break;
                case 5:  e.setPadLoop (pad, r.nextBool());
                         e.setPadReverse (pad, r.nextBool()); break;
                case 6:  e.setPadKeepLength (pad, r.nextBool()); break;
                case 7:  e.setPadChoke (pad, r.nextInt (9));
                         e.setPadSelfCut (pad, r.nextBool()); break;
                case 8:  e.setPadAttack (pad, r.nextFloat() * 200.0f);
                         e.setPadRelease (pad, 1.0f + r.nextFloat() * 800.0f); break;
                case 9:  for (int f = 0; f < AudioEngine::kNumFx; ++f)
                             e.setPadSend (pad, f, r.nextFloat());
                         break;
                case 10: e.setStep (r.nextInt (AudioEngine::kNumPatterns),
                                    r.nextInt (AudioEngine::kNumSteps), pad, r.nextBool()); break;
                case 11: e.setBpm (40.0 + r.nextDouble() * 200.0);
                         e.setSwing (0.5f + r.nextFloat() * 0.25f); break;
                case 12: e.setPlaying (r.nextBool()); break;
                case 13: e.postPanic(); break;
                //  El cambio de ruta EN MITAD de la frase: auriculares que
                //  entran, altavoz que vuelve, el sistema que reabre el flujo.
                case 14: e.prepareToPlay (kAparatos[r.nextInt (juce::numElementsInArray (kAparatos))].rate,
                                          ap.bloque); break;
                case 15: e.setPadMute (pad, r.nextBool());
                         e.setPadSolo (pad, r.nextBool()); break;
                //  Un pad que se vacia o cambia de sonido mientras suena.
                case 16: if (r.nextBool()) e.clearPad (pad);
                         else               e.publishSample (pad, sonido (r, ap.rate));
                         break;
                default: e.postNoteOnFromMidi (pad, r.nextFloat()); break;
            }

            //  Y se renderiza: una accion sin bloques detras no ha pasado por
            //  el hilo de audio, que es donde estan los fallos.
            const int bloques = r.nextInt ({ 1, 6 });
            for (int b = 0; b < bloques; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, ap.bloque);

                for (int c = 0; c < 2; ++c)
                {
                    const float* d = out.getReadPointer (c);
                    for (int i = 0; i < ap.bloque; ++i)
                    {
                        if (! std::isfinite (d[i])) { fallo = { semilla, 0, ultima }; return false; }
                        if (std::abs (d[i]) > 1.001f) { fallo = { semilla, 2, ultima }; return false; }
                    }
                }
            }
        }

        //  Y AL FINAL, EL SILENCIO. Panico y un cuarto de segundo: lo que
        //  quede sonando es una voz que no se puede parar.
        e.setPlaying (false);
        e.postPanic();
        const int hasta = (int) (ap.rate * 0.25 / ap.bloque) + 4;
        double resto = 0.0;
        for (int b = 0; b < hasta; ++b)
        {
            out.clear();
            e.renderNextBlock (out, 0, ap.bloque);
            if (b < hasta - 3) continue;              // los ultimos tres bloques
            for (int c = 0; c < 2; ++c)
            {
                const float* d = out.getReadPointer (c);
                for (int i = 0; i < ap.bloque; ++i)
                {
                    if (! std::isfinite (d[i])) { fallo = { semilla, 0, ultima }; return false; }
                    resto = juce::jmax (resto, (double) std::abs (d[i]));
                }
            }
        }

        //  El umbral no es cero: la reverberacion y el delay tienen cola por
        //  diseno y el panico no la corta, corta las VOCES. Lo que no puede
        //  quedar es senal a nivel de nota.
        if (resto > 0.02) { fallo = { semilla, 1, ultima }; return false; }

        return true;
    }
}

int main (int argc, char** argv)
{
    const juce::ScopedJuceInitialiser_GUI juceInit;

    const long personas = argc > 1 ? std::atol (argv[1]) : 1000000;
    const int  hilos    = argc > 2 ? std::atoi (argv[2])
                                   : juce::jmax (1, (int) std::thread::hardware_concurrency() - 1);

    std::printf ("simulando %ld sesiones en %d hilos\n", personas, hilos);
    std::fflush (stdout);

    std::atomic<long> hechas { 0 };
    std::atomic<int>  fallos { 0 };
    std::vector<std::vector<Fallo>> porHilo ((size_t) hilos);
    std::vector<std::thread> pool;

    const auto t0 = juce::Time::getMillisecondCounterHiRes();

    for (int h = 0; h < hilos; ++h)
    {
        pool.emplace_back ([&, h]
        {
            for (long i = h; i < personas; i += hilos)
            {
                Fallo f;
                if (! unaSesion ((juce::uint32) (i + 1), f))
                {
                    ++fallos;
                    //  Solo las primeras de cada hilo: si hay un fallo
                    //  sistematico son un millon de lineas iguales.
                    if (porHilo[(size_t) h].size() < 12) porHilo[(size_t) h].push_back (f);
                }
                ++hechas;
            }
        });
    }

    for (auto& t : pool) t.join();

    const double segs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;

    int conteo[3] = { 0, 0, 0 };
    std::vector<Fallo> muestra;
    for (auto& v : porHilo)
        for (auto& f : v) { ++conteo[juce::jlimit (0, 2, f.clase)]; muestra.push_back (f); }

    std::printf ("\n%ld sesiones en %.0f s  (%.0f/s)\n", (long) hechas, segs,
                 segs > 0.0 ? (double) hechas / segs : 0.0);
    std::printf ("fallos: %d\n", (int) fallos);

    if (fallos > 0)
    {
        for (int c = 0; c < 3; ++c)
            if (conteo[c] > 0) std::printf ("  %-40s %d\n", nombreClase (c), conteo[c]);
        std::printf ("\nsemillas para repetir:\n");
        for (size_t i = 0; i < juce::jmin<size_t> (muestra.size(), 20); ++i)
            std::printf ("  semilla %-10u  %-40s  ultima accion %d\n",
                         muestra[i].semilla, nombreClase (muestra[i].clase), muestra[i].accion);
    }

    std::printf ("\n%s\n", fallos == 0 ? "ninguna sesion rompe el motor"
                                       : "HAY SESIONES QUE ROMPEN EL MOTOR");
    return fallos > 0 ? 1 : 0;
}

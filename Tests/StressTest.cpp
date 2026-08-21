// ZATI expo bench — the audio half.
//
// The interface can be measured by walking a component tree. The engine has to
// be RUN, and run the way a stand runs it: sixteen pads slammed at once, the
// same pad retriggered faster than a human can, the command queue pushed past
// its own capacity, the device torn down and rebuilt mid-phrase. What comes
// out is checked for the three things that end a demo — silence, a NaN, and a
// block that took longer than it had.
#include <JuceHeader.h>
#include "../Source/AudioEngine.h"
#include "../Source/Denoise.h"
#include "../Source/MidiIo.h"
#include "../Source/Kits.h"
#include "../Source/Onsets.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <algorithm>

using Clock = std::chrono::steady_clock;

//  Sixteen coherent sine waves sum to sixteen times one sine wave, which no
//  sampler ever plays and which puts the master saturator into permanent
//  action - a bench that measures its own test signal. Each pad gets its own
//  phase and a noise floor, so the sum behaves like sixteen real one-shots.
static SampleBuffer::Ptr makeSample (double sr, double seconds, float freq, bool decay = true)
{
    auto* sb = new SampleBuffer();
    const int n = (int) (sr * seconds);
    juce::Random rng ((juce::int64) (freq * 1000.0f));
    const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    sb->buffer.setSize (2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float env = decay ? std::exp (-3.0f * (float) i / (float) n) : 1.0f;
            const float tone = std::sin (phase + juce::MathConstants<float>::twoPi * freq * (float) i / (float) sr);
            sb->buffer.setSample (c, i, 0.6f * env * (0.8f * tone + 0.2f * (rng.nextFloat() * 2.0f - 1.0f)));
        }
    sb->sourceSampleRate = sr;
    return SampleBuffer::Ptr (sb);
}

struct Stats { double peak = 0, rms = 0, worstBlockMs = 0, totalMs = 0; int blocks = 0; bool nan = false; bool clipped = false;
               long hot = 0, samples = 0; };   // hot = samples the master saturator had to bend

static Stats runBlocks (AudioEngine& e, juce::AudioBuffer<float>& buf, int blockSize, int nBlocks,
                        std::function<void (int)> beforeBlock = {})
{
    Stats s;
    double acc = 0.0;
    for (int b = 0; b < nBlocks; ++b)
    {
        if (beforeBlock) beforeBlock (b);
        const auto t0 = Clock::now();
        e.renderNextBlock (buf, 0, blockSize);
        const double ms = std::chrono::duration<double, std::milli> (Clock::now() - t0).count();
        s.worstBlockMs = juce::jmax (s.worstBlockMs, ms);
        s.totalMs += ms;
        ++s.blocks;

        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = buf.getSample (c, i);
                if (std::isnan (v) || std::isinf (v)) s.nan = true;
                if (std::abs (v) > 1.0f) s.clipped = true;
                s.peak = juce::jmax (s.peak, (double) std::abs (v));
                if (std::abs (v) > 0.944f) ++s.hot;      // -0.5 dBFS, the saturator's knee
                ++s.samples;
                acc += (double) v * v;
            }
    }
    s.rms = std::sqrt (acc / juce::jmax (1.0, (double) (nBlocks * blockSize * buf.getNumChannels())));
    return s;
}

static void report (const char* name, const Stats& s, double blockMsBudget)
{
    const double load = s.totalMs / juce::jmax (1, s.blocks) / blockMsBudget * 100.0;
    std::printf ("%-34s peak %.3f  rms %.4f  sat %5.2f%%  avg %.3f ms  worst %.3f ms  budget %.2f ms  load %.1f%%  %s%s\n",
                 name, s.peak, s.rms, 100.0 * (double) s.hot / juce::jmax (1.0, (double) s.samples),
                 s.totalMs / juce::jmax (1, s.blocks), s.worstBlockMs, blockMsBudget, load,
                 s.nan ? "NaN! " : "", s.clipped ? "CLIP!" : "");
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 48000.0;
    const int    bs = 128;                       // Oboe's low-latency burst on a modern phone
    const double budgetMs = 1000.0 * bs / sr;    // 2.67 ms

    AudioEngine e;
    e.prepareToPlay (sr, bs);
    e.setPolyphony (32, 4);

    juce::AudioBuffer<float> buf (2, bs);

    //  padGain is value-initialised to zero: the app sets every channel at
    //  startup, so a bench that skips it renders a perfect, silent pass.
    for (int p = 0; p < 16; ++p)
    {
        e.setPadGain (p, 0.85f);
        e.publishSample (p, makeSample (44100.0, 1.2, 110.0f * (float) (p + 1)));
    }

    // The engine adopts a published sample at the top of a block.
    runBlocks (e, buf, bs, 8);

    // 1. SILENCE — nothing playing must cost nothing and produce nothing.
    report ("idle", runBlocks (e, buf, bs, 400), budgetMs);

    // 2. ALL SIXTEEN AT ONCE — the thing every demo does in the first minute.
    {
        auto s = runBlocks (e, buf, bs, 600, [&e] (int b)
        {
            if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 1.0f);
        });
        report ("16 pads at once", s, budgetMs);
    }

    // 3. MACHINE-GUN RETRIGGER — one pad, every single block, for 5 seconds.
    //    This is where a voice pool leaks or a choke group deadlocks.
    {
        auto s = runBlocks (e, buf, bs, 1875, [&e] (int) { e.postNoteOn (3, 0.9f); });
        report ("1 pad retriggered every block", s, budgetMs);
    }

    // 4. QUEUE SATURATION — push far more commands per block than the FIFO
    //    holds. Dropping is fine; wedging is not, so we check it still plays.
    {
        auto s = runBlocks (e, buf, bs, 400, [&e] (int)
        {
            for (int k = 0; k < 64; ++k) e.postNoteOn (k % 16, 0.7f);
        });
        report ("queue saturated (64 cmds/block)", s, budgetMs);
        std::printf ("%-34s dropped %d commands, still audible: %s\n", "", e.takeDroppedCommands(),
                     s.rms > 1.0e-4 ? "YES" : "NO  <-- WEDGED");
    }

    // 5. DEVICE CHANGE MID-PHRASE — the headphone-unplug path, 40 times.
    {
        int reprepares = 0;
        auto s = runBlocks (e, buf, bs, 800, [&e, &reprepares] (int b)
        {
            if (b % 20 == 0) { e.prepareToPlay (b % 40 == 0 ? 44100.0 : 48000.0, 128); ++reprepares; }
            if (b % 7 == 0)  e.postNoteOn (b % 16, 0.9f);
        });
        report ("route changes mid-phrase", s, budgetMs);
        std::printf ("%-34s %d re-prepares, still audible: %s\n", "", reprepares,
                     s.rms > 1.0e-4 ? "YES" : "NO  <-- DEAD");
    }

    // 5b. THE NOTIFICATION. A chime from another app ducks us and gives the
    //     level back. Measured on ONE sustained pad, not on sixteen: with the
    //     whole kit going the master saturator is already bending the peaks,
    //     so peak level says nothing about gain and RMS on a predictable
    //     signal says everything.
    //
    //     Three things have to be true: it gets quieter by the amount asked
    //     for, it comes ALL the way back, and the move is a ramp - a step in
    //     the master is a click, which is louder than the notification that
    //     caused it.
    {
        AudioEngine d;
        d.prepareToPlay (sr, bs);
        d.setPolyphony (32, 4);
        d.setPadGain (0, 0.85f);
        //  Flat, not decaying: a sample that fades on its own would make the
        //  "did the level come back" answer depend on when it was asked.
        d.publishSample (0, makeSample (48000.0, 30.0, 220.0f, false));
        juce::AudioBuffer<float> db (2, bs);
        runBlocks (d, db, bs, 4);
        d.postNoteOn (0, 1.0f);

        std::vector<double> blockRms;
        for (int b = 0; b < 400; ++b)
        {
            if (b == 80)  d.setDucked (true);
            if (b == 240) d.setDucked (false);
            d.renderNextBlock (db, 0, bs);
            double acc = 0.0;
            for (int i = 0; i < bs; ++i) { const double v = db.getSample (0, i); acc += v * v; }
            blockRms.push_back (std::sqrt (acc / bs));
        }

        auto avg = [&] (int a, int b) { double t = 0; for (int i = a; i < b; ++i) t += blockRms[(size_t) i]; return t / (b - a); };
        const double before  = avg (40, 78);
        const double ducked  = avg (140, 238);
        const double after   = avg (330, 398);
        const double ratio   = before > 0 ? ducked / before : 0.0;
        const double back    = before > 0 ? after  / before : 0.0;

        //  How many blocks the ramp took to cross most of the way down. At
        //  128 samples and 48 kHz a block is 2.67 ms, so a 25 ms ramp is
        //  about nine of them; one or two would be a step.
        int rampBlocks = 0;
        for (int i = 80; i < 140; ++i)
        {
            if (blockRms[(size_t) i] <= ducked * 1.05) { rampBlocks = i - 80; break; }
        }

        const bool ok = ratio > 0.24 && ratio < 0.33 && back > 0.97 && back < 1.03 && rampBlocks >= 3 && rampBlocks <= 24;
        std::printf ("%-34s ducked to %.3f of level  back to %.3f  ramp %d blocks (%.1f ms)  %s\n",
                     "notification duck / restore", ratio, back, rampBlocks,
                     rampBlocks * 1000.0 * bs / sr, ok ? "OK" : "<-- FAILED");
    }

    // 5c. RESAMPLE. The master printed back onto a pad: what lands there has
    //     to be what came out, at the level it came out at, and it must not
    //     also contain the microphone.
    {
        AudioEngine r;
        r.prepareToPlay (sr, bs);
        r.setPolyphony (32, 4);
        r.setPadGain (0, 0.85f);
        r.publishSample (0, makeSample (48000.0, 4.0, 220.0f, false));
        juce::AudioBuffer<float> rb (2, bs);
        runBlocks (r, rb, bs, 4);

        //  Play pad 0 and resample onto pad 5 while it sounds.
        r.postNoteOn (0, 1.0f);
        runBlocks (r, rb, bs, 20);
        double heard = 0.0;
        r.startRecording (5, true);
        for (int b = 0; b < 300; ++b)
        {
            r.renderNextBlock (rb, 0, bs);
            for (int i = 0; i < bs; ++i) heard += (double) rb.getSample (0, i) * rb.getSample (0, i);
        }
        heard = std::sqrt (heard / (300.0 * bs));

        auto sb = r.finishRecording();
        double printed = 0.0;
        int len = 0;
        if (sb != nullptr)
        {
            len = sb->buffer.getNumSamples();
            for (int i = 0; i < len; ++i)
                printed += (double) sb->buffer.getSample (0, i) * sb->buffer.getSample (0, i);
            printed = std::sqrt (printed / juce::jmax (1, len));
        }

        const double ratio = heard > 0 ? printed / heard : 0.0;
        const bool ok = sb != nullptr && len > (int) (sr * 0.5) && ratio > 0.95 && ratio < 1.05;
        std::printf ("%-34s heard %.4f  printed %.4f  ratio %.3f  %d samples  %s\n",
                     "resample master -> pad", heard, printed, ratio, len,
                     ok ? "OK" : "<-- FAILED");
    }

    // 6. EVERY BUFFER SIZE — the load has to fit the budget at the SMALLEST
    //    one, because that is the one that makes the app feel like hardware.
    for (int b : { 64, 96, 128, 192, 256, 480, 512 })
    {
        AudioEngine e2;
        e2.prepareToPlay (sr, b);
        e2.setPolyphony (32, 4);
        for (int p = 0; p < 16; ++p) { e2.setPadGain (p, 0.85f); e2.publishSample (p, makeSample (44100.0, 1.2, 110.0f * (float) (p + 1))); }
        juce::AudioBuffer<float> bb (2, b);
        runBlocks (e2, bb, b, 4);
        auto s = runBlocks (e2, bb, b, (int) (sr * 3 / b), [&e2] (int i) { if (i % 4 == 0) e2.postNoteOn (i % 16, 1.0f); });
        char name[64]; std::snprintf (name, sizeof name, "buffer %d (%.2f ms round trip)", b, 2.0 * 1000.0 * b / sr);
        report (name, s, 1000.0 * b / sr);
    }

    //  MIDI QUE SALE. La nota, la velocidad, y sobre todo QUE NO FALTE NINGUNA.
    //
    //  El envio se puso en triggerPad y no en handleCommand a proposito: es el
    //  embudo por el que pasan el dedo, el secuenciador, la cadena y una celda
    //  de la cancion. Ponerlo un piso mas arriba habria dejado fuera al
    //  secuenciador, que es justo lo que la gente quiere mandar al hardware, y
    //  el sintoma seria "los pads mandan notas pero el patron no" - que nadie
    //  llama fallo, se llama "no funciona".
    //
    //  Y se comprueba tambien que un pad VACIO no manda: mandar la nota de un
    //  pad sin sonido hace que el modulo de al lado toque algo que en esta app
    //  no se oye.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (16, 4);
        e.setMidiOutEnabled (true);
        for (int p = 0; p < 4; ++p) { e.setPadGain (p, 1.0f); e.publishSample (p, makeSample (48000.0, 0.1, 200.0f)); }
        //  El pad 5 se queda sin muestra: no debe mandar nada.
        e.setPadGain (4, 1.0f);

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.midiOutQueue().drain ([] (const MidiIo::NoteEvent&) {});   // limpia la adopcion

        //  Cuatro dedos y un pad vacio.
        for (int p = 0; p < 5; ++p) e.postNoteOn (p, (p + 1) * 0.2f);
        b.clear(); e.renderNextBlock (b, 0, 512);

        int notes[8] = {}, vels[8] = {}, n = 0;
        e.midiOutQueue().drain ([&] (const MidiIo::NoteEvent& ev)
        {
            if (n < 8) { notes[n] = MidiIo::noteForPad (ev.pad); vels[n] = ev.vel; ++n; }
        });

        const bool countOk = (n == 4);                       // el vacio no manda
        bool mapOk = countOk;
        for (int i = 0; i < n; ++i) if (notes[i] != 36 + i) mapOk = false;
        //  0.2 -> 25, 0.4 -> 51, 0.6 -> 76, 0.8 -> 102. Y nunca cero, que en
        //  MIDI significa apagado desde 1983.
        bool velOk = countOk;
        for (int i = 0; i < n; ++i)
        {
            const int want = juce::jlimit (1, 127, (int) std::lround ((i + 1) * 0.2f * 127.0f));
            if (vels[i] != want || vels[i] < 1) velOk = false;
        }

        //  Y el secuenciador, que es el camino que se habria quedado fuera.
        e.setStep (0, 0, 0, true);
        e.setPlaying (true);
        int fromSeq = 0;
        for (int blk = 0; blk < 60; ++blk)
        {
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.midiOutQueue().drain ([&] (const MidiIo::NoteEvent& ev) { if (ev.on) ++fromSeq; });
        }
        e.setPlaying (false);

        //  Y apagada, ni un byte.
        e.setMidiOutEnabled (false);
        for (int p = 0; p < 4; ++p) e.postNoteOn (p, 1.0f);
        b.clear(); e.renderNextBlock (b, 0, 512);
        int whenOff = 0;
        e.midiOutQueue().drain ([&] (const MidiIo::NoteEvent&) { ++whenOff; });

        std::printf ("%-34s %d notas  mapa %s  velocidad %s  secuenciador %d  apagada %d  %s\n",
                     "midi que sale", n, mapOk ? "ok" : "MAL", velOk ? "ok" : "MAL",
                     fromSeq, whenOff,
                     (countOk && mapOk && velOk && fromSeq > 0 && whenOff == 0) ? "OK" : "FALLA");
    }

    //  EL DELAY, Y EL BRILLO QUE LE QUEDA A LA OCTAVA REPETICION.
    //
    //  La linea interpolaba lineal, y una interpolacion lineal de un retardo
    //  fraccionario no es una aproximacion: es un paso bajo cuya frecuencia de
    //  corte depende de la parte fraccionaria. Con una pasada da igual; con
    //  0.9 de realimentacion el error se COMPONE, y la cola se apaga en agudos
    //  mucho antes de lo que dice el mando. De oido eso suena a "delay
    //  analogico" y por eso nadie lo llama fallo - hasta que se mide contra el
    //  numero que el mando promete.
    //
    //  Se mete un tono agudo, se dejan pasar ocho repeticiones y se compara su
    //  nivel con el que la realimentacion sola predice. Lo que sobra es lo que
    //  se come la interpolacion.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
        //  33.34375 ms x 48 kHz = 1600.5 muestras: media muestra EXACTA de parte
        //  fraccionaria, que es el peor caso de la interpolacion. El primer
        //  intento uso 100 ms, que son 4800 muestras clavadas - fraccion cero -
        //  y ahi hasta la interpolacion lineal es exacta: la prueba daba -0.0 dB
        //  con las dos y no estaba midiendo nada. Primero se duda de la prueba.
        e.setDlyMix (1.0f); e.setDlyTime (33.34375f); e.setDlyFb (0.9f);
        e.setPadGain (0, 1.0f);
        e.setPadSend (0, 3, 1.0f);
        //  8 kHz: bastante agudo para que la perdida se vea, bastante por
        //  debajo de Nyquist para que no sea la propia banda del generador.
        e.publishSample (0, makeSample (48000.0, 0.02, 8000.0f, false));

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.postNoteOn (0, 1.0f);

        //  100 ms de retardo a 48 kHz son 4800 muestras: 9.4 bloques de 512.
        //  Se mira el pico dentro de la ventana de cada repeticion.
        double rep1 = 0.0, rep8 = 0.0;
        bool nan = false;
        for (int blk = 0; blk < 100; ++blk)
        {
            b.clear();
            e.renderNextBlock (b, 0, 512);
            double pk = 0.0;
            for (int i = 0; i < 512; ++i)
            {
                const float v = b.getSample (0, i);
                if (! std::isfinite (v)) nan = true;
                pk = juce::jmax (pk, (double) std::abs (v));
            }
            //  Ventanas centradas en cada repeticion: 33.34 ms de separacion.
            const double at = (double) (blk * 512) / 48000.0;
            if (at > 0.030 && at < 0.066) rep1 = juce::jmax (rep1, pk);
            if (at > 0.233 && at < 0.270) rep8 = juce::jmax (rep8, pk);
        }

        //  Lo que la realimentacion sola dice que tiene que quedar: 0.9^7.
        const double ideal = std::pow (0.9, 7.0);
        const double got   = (rep1 > 1.0e-9) ? rep8 / rep1 : 0.0;
        const double lostDb = 20.0 * std::log10 (juce::jmax (1.0e-9, got / ideal));

        //  Menos de 3 dB perdidos en siete pasadas por la linea. Con
        //  interpolacion lineal esto se iba mucho mas abajo.
        std::printf ("%-34s rep8/rep1 %.3f (ideal %.3f)   interp %+.1f dB   NaN %s   %s\n",
                     "delay 8 repeticiones a 8 kHz", got, ideal, lostDb,
                     nan ? "SI" : "no",
                     (! nan && lostDb > -3.0) ? "OK" : "FALLA");
    }

    //  LA REVERB, MEDIDA. Un cambio de algoritmo de cola no se juzga de oido
    //  en una sesion: se le mete un impulso y se mira cuanto tarda en caer 60
    //  dB, si crece en vez de caer, y si produce NaN. Una FDN mal escalada se
    //  descubre aqui y no en un directo.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
        //  Solo el bus de reverb: mezcla al maximo y un pad que le manda todo.
        e.setRevMix (1.0f); e.setRevSize (0.6f); e.setRevDamp (0.4f);
        //  La ganancia del pad, que por defecto es cero: la primera version de
        //  esta sonda no la ponia y midio una reverb muda durante tres
        //  intentos. Primero se duda de la prueba.
        e.setPadGain (0, 1.0f);
        e.setPadSend (0, 5, 1.0f);
        e.publishSample (0, makeSample (48000.0, 0.05, 400.0f));

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.postNoteOn (0, 1.0f);
        b.clear(); e.renderNextBlock (b, 0, 512);

        double first = 0.0, t60 = -1.0, peak = 0.0;
        bool nan = false, grew = false;
        for (int blk = 0; blk < 600; ++blk)
        {
            b.clear();
            e.renderNextBlock (b, 0, 512);
            double rms = 0.0;
            for (int i = 0; i < 512; ++i) { const float v = b.getSample (0, i); rms += (double) v * v; }
            rms = std::sqrt (rms / 512.0);
            if (! std::isfinite (rms)) { nan = true; break; }
            peak = juce::jmax (peak, rms);
            if (blk == 1) first = rms;
            if (blk > 40 && rms > peak * 1.05) grew = true;
            if (t60 < 0.0 && blk > 4 && first > 0.0 && rms < first * 0.001)
                t60 = (double) (blk * 512) / 48000.0;
        }
        std::printf ("%-34s T60 %.2f s   pico %.4f   NaN %s   crece %s   %s\n",
                     "reverb FDN (impulso)", t60, peak,
                     nan ? "SI" : "no", grew ? "SI" : "no",
                     (! nan && ! grew && t60 > 0.15 && t60 < 12.0) ? "OK" : "FALLA");
    }

    //  QUITAR RUIDO, medido y no mirado.
    //
    //  Una limpieza se juzga por dos numeros a la vez, y por eso hay dos
    //  sondas: cuanto baja el suelo donde solo hay ruido, y cuanto sobrevive
    //  el sonido donde si hay algo. Solo el primero se puede sacar con un
    //  silenciador, y solo el segundo con no hacer nada.
    {
        constexpr double fs = 48000.0;
        constexpr int len = 48000;               // un segundo
        juce::AudioBuffer<float> b (1, len);
        juce::Random rnd (20260811);
        float* d = b.getWritePointer (0);

        //  Medio segundo de siseo solo, y medio de siseo con un tono encima.
        //  El tono a 0.5 y el ruido a 0.03 son -24 dB de relacion, que es una
        //  grabacion de telefono mala pero no perdida.
        for (int i = 0; i < len; ++i)
        {
            const float noise = 0.03f * (rnd.nextFloat() * 2.0f - 1.0f);
            const float tone  = (i >= len / 2)
                                  ? 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / fs)
                                  : 0.0f;
            d[i] = noise + tone;
        }

        auto rms = [] (const juce::AudioBuffer<float>& buf, int from, int n)
        {
            double acc = 0.0;
            for (int i = 0; i < n; ++i) { const double v = buf.getSample (0, from + i); acc += v * v; }
            return std::sqrt (acc / juce::jmax (1, n));
        };

        //  Lejos de la costura: la ventana que cae a caballo entre el silencio
        //  y el tono tiene las dos cosas dentro, y medirla ahi seria medir el
        //  desenfoque de la ventana en vez de la limpieza.
        const double floorBefore = rms (b, 2000, 20000);
        const double toneBefore  = rms (b, len / 2 + 4000, 20000);

        const auto t0 = std::chrono::steady_clock::now();
        Denoise::process (b, 0.6f);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

        const double floorAfter = rms (b, 2000, 20000);
        const double toneAfter  = rms (b, len / 2 + 4000, 20000);

        const double cut  = 20.0 * std::log10 (juce::jmax (1.0e-9, floorAfter) / juce::jmax (1.0e-9, floorBefore));
        const double keep = 20.0 * std::log10 (juce::jmax (1.0e-9, toneAfter)  / juce::jmax (1.0e-9, toneBefore));

        bool nan = false;
        for (int i = 0; i < len; ++i) if (! std::isfinite (b.getSample (0, i))) { nan = true; break; }

        //  Pide 12 dB de suelo fuera y menos de 1.5 dB perdidos en el tono. La
        //  segunda condicion es la que importa: una limpieza que baja 40 dB y
        //  se lleva el sonido por delante no es una limpieza.
        std::printf ("%-34s suelo %+.1f dB   tono %+.2f dB   NaN %s   %.0f ms/s   %s\n",
                     "quitar ruido (siseo + tono)", cut, keep,
                     nan ? "SI" : "no", ms,
                     (! nan && cut < -12.0 && keep > -1.5) ? "OK" : "FALLA");
    }

    //  Y QUE NO SUBA EL PICO, NUNCA.
    //
    //  Una resta espectral solo puede ATENUAR: si el pico sale mas alto que el
    //  que entro, algo esta amplificando. Y lo estaba: la division por la suma
    //  de ventanas del solape-suma no tenia suelo, y en el PRIMER salto solo
    //  hay una ventana encima - o sea que la suma vale una fraccion de lo que
    //  vale en regimen, y dividir por una fraccion multiplica. La app lo
    //  anunciaba tal cual, "pico +5.2 dB", despues de limpiar.
    //
    //  No lo veia nadie porque el banco medía el SUELO y el TONO en mitad de la
    //  señal, lejos de los bordes, que es justo donde estaba el fallo.
    {
        const double rate = 48000.0;
        const int len = (int) (rate * 1.5);
        juce::AudioBuffer<float> b (1, len);
        juce::Random rng (5150);
        for (int i = 0; i < len; ++i)
        {
            const float t = (float) i / (float) rate;
            b.setSample (0, i, 0.05f * (rng.nextFloat() * 2.0f - 1.0f)
                             + 0.45f * std::sin (2.0f * juce::MathConstants<float>::pi * 330.0f * t));
        }
        const float before = b.getMagnitude (0, len);
        Denoise::process (b, 0.6f);
        const float after = b.getMagnitude (0, len);

        //  Y donde esta el pico: si el borde amplifica, cae en las primeras
        //  muestras y no en mitad de la señal.
        int at = 0; float pk = 0.0f;
        for (int i = 0; i < len; ++i) if (std::abs (b.getSample (0, i)) > pk) { pk = std::abs (b.getSample (0, i)); at = i; }

        const double d = 20.0 * std::log10 (juce::jmax (1.0e-9f, after) / juce::jmax (1.0e-9f, before));
        std::printf ("%-34s pico %+.2f dB   maximo en %.0f ms   %s\n",
                     "quitar ruido (no amplifica)", d, 1000.0 * at / rate,
                     (d <= 0.5) ? "OK" : "FALLA");
    }

    //  Y LO QUE PIDE DE MEMORIA, que es la otra mitad y no estaba medida.
    //
    //  La version anterior guardaba el espectrograma entero - magnitud, real e
    //  imaginaria - asi que el consumo crecia con la DURACION: 92 MB por
    //  minuto de audio, y la app deja grabar cinco. Eso no lanza bad_alloc en
    //  un telefono, lo mata el sistema, y por eso no lo veia ninguna prueba
    //  que solo mirara el sonido. Aqui se limpia una muestra de cinco minutos
    //  - el tope de setRecordLimit - y se mira cuanto crece el proceso.
    {
        auto rssKb = [] () -> long
        {
            //  VmHWM: el maximo que ha llegado a ocupar, no el de ahora. El de
            //  ahora ya ha soltado los vectores cuando se pregunta.
            std::ifstream f ("/proc/self/status");
            std::string line;
            while (std::getline (f, line))
                if (line.rfind ("VmHWM:", 0) == 0)
                    return std::atol (line.c_str() + 6);
            return -1;
        };

        const double rate = 48000.0;
        const int    len  = (int) (300.0 * rate);        // cinco minutos, mono
        juce::AudioBuffer<float> b (1, len);
        juce::Random rng (77);
        for (int i = 0; i < len; ++i)
            b.setSample (0, i, 0.02f * (rng.nextFloat() * 2.0f - 1.0f)
                             + 0.30f * std::sin (2.0f * juce::MathConstants<float>::pi
                                                 * 440.0f * (float) i / (float) rate));

        const long before = rssKb();
        const auto t0 = std::chrono::steady_clock::now();
        Denoise::process (b, 0.6f);
        const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const long after = rssKb();

        //  El propio buffer son 55 MB y ya estaban antes de llamar. Lo que se
        //  mide es lo que anade el algoritmo: tiene que ser CONSTANTE, no
        //  proporcional a los cinco minutos.
        const double addedMb = (after - before) / 1024.0;
        bool nan = false;
        for (int i = 0; i < len; i += 97) if (! std::isfinite (b.getSample (0, i))) { nan = true; break; }

        std::printf ("%-34s +%.1f MB sobre 55 MB de muestra   NaN %s   %.1f s   %s\n",
                     "quitar ruido (5 min, memoria)", addedMb,
                     nan ? "SI" : "no", s,
                     (! nan && addedMb < 32.0) ? "OK" : "FALLA");
    }

    //  MUESTRAS HOSTILES.
    //
    //  El unico dato que entra en esta app desde fuera es un fichero de audio
    //  que ha elegido la persona, y no tiene por que estar bien: una cabecera
    //  que miente sobre la frecuencia, un WAV cortado a la mitad, o valores que
    //  no son numeros. El cargador ya rechaza por tamano, por duracion y por
    //  memoria - eso esta medido en su sitio -, pero lo que llega al MOTOR
    //  despues de pasar esos filtros no lo miraba nadie.
    //
    //  Lo que se comprueba es lo que un fichero hostil no puede conseguir:
    //  colgar una voz para siempre, o envenenar la salida con NaN. Lo segundo
    //  es lo grave: un NaN en un pad se propaga por el bus, por el saturador y
    //  por el master, y la app se queda muda hasta que se reinicia. Un fichero
    //  capaz de eso es un fichero que apaga el instrumento.
    {
        struct Case { const char* what; double rate; int chans; int len; int fill; };
        //  fill: 0 seno normal, 1 NaN, 2 infinito, 3 valores enormes
        static const Case cases[] =
        {
            { "frecuencia 0",        0.0,      2, 4410, 0 },
            { "frecuencia negativa", -44100.0, 2, 4410, 0 },
            { "frecuencia enorme",   1.0e9,    2, 4410, 0 },
            { "una sola muestra",    44100.0,  1,    1, 0 },
            { "cuatro muestras",     44100.0,  2,    4, 0 },
            { "lleno de NaN",        44100.0,  2, 4410, 1 },
            { "lleno de infinito",   44100.0,  2, 4410, 2 },
            { "valores enormes",     44100.0,  2, 4410, 3 },
        };

        //  DOS VECES: CON EL LIMITADOR Y SIN EL.
        //
        //  Esta tanda solo ejercia el motor VIVO, que siempre lleva el
        //  limitador puesto - y la barrera de no-finitos vivia DENTRO de ese
        //  mismo `if`. O sea que el unico camino que apaga el limitador,
        //  Exporter.h en el motor del rebote, era el unico que no estaba
        //  medido, y es justo el que ESCRIBE A DISCO. Un NaN alli no se oye:
        //  se guarda.
        bool allOk = true;
        for (const bool limiter : { true, false })
        for (const auto& c : cases)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setPolyphony (8, 2);
            e.setPadGain (0, 1.0f);
            e.setSafetyLimiter (limiter);

            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize (juce::jmax (1, c.chans), juce::jmax (1, c.len));
            for (int ch = 0; ch < sb->buffer.getNumChannels(); ++ch)
                for (int i = 0; i < c.len; ++i)
                {
                    const float v = c.fill == 1 ? std::numeric_limits<float>::quiet_NaN()
                                  : c.fill == 2 ? std::numeric_limits<float>::infinity()
                                  : c.fill == 3 ? 1.0e30f
                                  : 0.5f * std::sin (0.05f * (float) i);
                    sb->buffer.setSample (ch, i, v);
                }
            sb->sourceSampleRate = c.rate;
            e.publishSample (0, sb);

            juce::AudioBuffer<float> b (2, 512);
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.postNoteOn (0, 1.0f);

            bool nan = false;
            //  Doscientos bloques son 2.1 s a 48 kHz: mucho mas que la muestra
            //  mas larga de la lista, asi que al final NO puede quedar nada
            //  sonando. Si queda, es una voz colgada.
            for (int blk = 0; blk < 200; ++blk)
            {
                b.clear();
                e.renderNextBlock (b, 0, 512);
                for (int ch = 0; ch < 2 && ! nan; ++ch)
                    for (int i = 0; i < 512; ++i)
                        if (! std::isfinite (b.getSample (ch, i))) { nan = true; break; }
            }

            const bool stuck = e.getPadPosition01 (0) >= 0.0f;
            const bool ok = ! nan && ! stuck;
            allOk = allOk && ok;
            std::printf ("%-24s %-9s NaN %-3s  voz colgada %-3s  %s\n",
                         c.what, limiter ? "[limit]" : "[rebote]",
                         nan ? "SI" : "no", stuck ? "SI" : "no", ok ? "OK" : "FALLA");
        }
        std::printf ("%-34s %s\n", "muestras hostiles",
                     allOk ? "ninguna cuelga ni envenena la salida, con y sin limitador"
                           : "HAY FALLOS");
    }

    //  EL FILTRO DEL PAD, con TRES numeros a la vez o no dice nada.
    //
    //  Cuanto quita arriba, cuanto respeta abajo, y cuanto cuesta cuando esta
    //  abierto. Solo el primero lo saca un silenciador, solo el segundo lo saca
    //  no hacer nada, y sin el tercero no hay forma de saber si el camino
    //  "sin filtro" es de verdad el de antes: la unica prueba de que abierto
    //  es gratis es que la salida salga IDENTICA bit a bit, y eso es lo que
    //  una comparacion por nivel -"casi lo mismo, 0.1 dB"- deja pasar.
    {
        //  Un seno limpio, sin el ruido que lleva makeSample: aqui se mide una
        //  banda, y un 20% de ruido blanco encima pone energia en todas.
        auto pureTone = [] (double sr, double secs, float hz)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * secs);
            sb->buffer.setSize (2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                                 * hz * (float) i / (float) sr));
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  Nivel eficaz en regimen, saltandose el ataque: el filtro tarda unos
        //  ciclos en llenar sus integradores y medir desde la primera muestra
        //  mezcla el transitorio con lo que se quiere medir.
        auto runTone = [&pureTone] (float hz, float cutoff, float reso,
                                    juce::AudioBuffer<float>& keep, bool* nanOut)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
            e.setPadGain (0, 1.0f);
            e.setPadCutoff (0, cutoff);
            e.setPadReso   (0, reso);
            e.publishSample (0, pureTone (48000.0, 1.0, hz));

            juce::AudioBuffer<float> b (2, 512);
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.postNoteOn (0, 1.0f);

            keep.setSize (1, 512 * 40, false, true, true);
            double acc = 0.0; int cnt = 0;
            for (int blk = 0; blk < 40; ++blk)
            {
                b.clear();
                e.renderNextBlock (b, 0, 512);
                keep.copyFrom (0, blk * 512, b, 0, 0, 512);
                for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample (0, i);
                    if (! std::isfinite (v)) { if (nanOut) *nanOut = true; continue; }
                    if (blk >= 10) { acc += (double) v * v; ++cnt; }
                }
            }
            return cnt > 0 ? std::sqrt (acc / (double) cnt) : 0.0;
        };

        bool nan = false;
        juce::AudioBuffer<float> openA, openB, lowOn, lowOff, hiOn, hiOff, resOn, resOff;

        //  200 Hz y 6 kHz contra un corte de 800: uno esta dos octavas por
        //  debajo y el otro casi tres por encima.
        const double lo0 = runTone (200.0f,  AudioEngine::kFiltOpenHz, 0.0f, lowOff, &nan);
        const double lo1 = runTone (200.0f,  800.0f, 0.0f, lowOn,  &nan);
        const double hi0 = runTone (6000.0f, AudioEngine::kFiltOpenHz, 0.0f, hiOff,  &nan);
        const double hi1 = runTone (6000.0f, 800.0f, 0.0f, hiOn,   &nan);

        const double cutDb  = 20.0 * std::log10 (juce::jmax (1.0e-9, hi1) / juce::jmax (1.0e-9, hi0));
        const double keepDb = 20.0 * std::log10 (juce::jmax (1.0e-9, lo1) / juce::jmax (1.0e-9, lo0));

        //  ABIERTO ES GRATIS, bit a bit. Dos corridas del mismo tono con el
        //  corte arriba tienen que dar exactamente el mismo bloque; si el
        //  filtro se colara, la diferencia seria pequenisima y REAL.
        runTone (1000.0f, AudioEngine::kFiltOpenHz, 0.0f, openA, &nan);
        runTone (1000.0f, AudioEngine::kFiltOpenHz, 0.0f, openB, &nan);
        int differ = 0;
        for (int i = 0; i < openA.getNumSamples(); ++i)
            if (openA.getSample (0, i) != openB.getSample (0, i)) ++differ;

        //  Y LA RESONANCIA RESUENA. Un tono justo en el corte con Q alta tiene
        //  que salir MAS ALTO que con Q baja, o el mando no hace nada y nadie
        //  se entera: es la mitad del filtro que un barrido no ensena.
        const double r0 = runTone (800.0f, 800.0f, 0.0f, resOff, &nan);
        const double r1 = runTone (800.0f, 800.0f, 1.0f, resOn,  &nan);
        const double resDb = 20.0 * std::log10 (juce::jmax (1.0e-9, r1) / juce::jmax (1.0e-9, r0));

        const bool ok = ! nan && cutDb < -20.0 && keepDb > -1.5 && differ == 0 && resDb > 6.0;
        std::printf ("%-34s 6 kHz %+.1f dB   200 Hz %+.2f dB   reson %+.1f dB   abierto %s   NaN %s   %s\n",
                     "filtro del pad (corte 800 Hz)", cutDb, keepDb, resDb,
                     differ == 0 ? "identico" : "CAMBIA",
                     nan ? "SI" : "no", ok ? "OK" : "FALLA");
    }

    //  DE DONDE SALE EL CRISP.
    //
    //  La sintetica normaliza por sonoridad y luego dobla lo que pase de
    //  kKnee con una tanh. Doblar es un WAVESHAPER: cada muestra por encima
    //  del codo sale con armonicos que no estaban, y el codo esta en 0.55
    //  mientras los picos medidos llegan a 0.69 - o sea que los golpes mas
    //  fuertes de la fabrica pasan SIEMPRE por el doblador, no en un pico
    //  raro. Aqui se mide cuanto: que fraccion de cada sonido se dobla, y
    //  cuanta distorsion armonica deja en un tono puro, que es donde se oye.
    {
        double worstFrac = 0.0; int worstIdx = 0;
        double sumFrac = 0.0; int bent = 0;
        double peakRawMax = 0.0;

        for (int i = 0; i < Kits::kNumSounds; ++i)
        {
            auto sb = Kits::render (i);
            const int n = sb->buffer.getNumSamples();
            const float* d = sb->buffer.getReadPointer (0);

            //  Lo que SALE ya esta doblado, asi que el doblador no se puede
            //  medir mirando su propia salida: se cuentan las muestras que
            //  quedaron por encima del codo, que son exactamente las que
            //  pasaron por la tanh.
            long over = 0; double pk = 0.0;
            for (int k = 0; k < n; ++k)
            {
                const double a = std::abs ((double) d[k]);
                if (a > 0.55) ++over;
                pk = juce::jmax (pk, a);
            }
            const double frac = 100.0 * (double) over / juce::jmax (1, n);
            sumFrac += frac;
            peakRawMax = juce::jmax (peakRawMax, pk);
            if (frac > 0.0) ++bent;
            if (frac > worstFrac) { worstFrac = frac; worstIdx = i; }
        }

        std::printf ("%-34s %d de %d doblados   peor %.2f%% (#%d)   media %.2f%%   pico %.3f\n",
                     "fabrica: cuanto se dobla", bent, Kits::kNumSounds,
                     worstFrac, worstIdx + 1, sumFrac / Kits::kNumSounds, peakRawMax);
    }

    //  Y SI CHASQUEAN, que es otra cosa distinta de si distorsionan.
    //
    //  PRIMERO SE DUDA DE LA PRUEBA, y esta ya mintio una vez. El primer
    //  intento comparaba cada salto con la MEDIANA de los saltos del sonido
    //  entero, y saco quince culpables de sesenta y cuatro con un ZAP a 1567
    //  veces su mediana. No habia tal chasquido: un ZAP dura 200 ms dentro de
    //  un fichero de 1000, asi que cuatro quintos del buffer son silencio y la
    //  mediana valia 0.0001. Un salto de 0.199 entre dos muestras es
    //  exactamente lo que da una banda de 3.2 kHz con amplitud 0.5 - es la
    //  senal, no un corte - pero contra una mediana de silencio parecia un
    //  disparo.
    //
    //  Lo que separa un escalon de una senal aguda es el nivel de AL LADO: una
    //  senal de banda limitada no puede saltar mas de lo que vale, porque su
    //  pendiente maxima es 2*pi*f/fs veces su amplitud y f no pasa de Nyquist.
    //  Asi que el salto se compara con el pico de los 5 ms que lo rodean, y el
    //  listón se pone en el DOBLE de ese pico, que es el peor caso posible -
    //  una alternancia a Nyquist. Lo que se pasa de ahi no cabe en ninguna
    //  banda: es un corte.
    {
        int culpables = 0;
        double peorRatio = 0.0; int peorIdx = 0; double peorMs = 0.0, peorSalto = 0.0;
        constexpr int kWin = 240;        // 5 ms a 48 kHz

        for (int i = 0; i < Kits::kNumSounds; ++i)
        {
            auto sb = Kits::render (i);
            const int n = sb->buffer.getNumSamples();
            const float* d = sb->buffer.getReadPointer (0);
            if (n < 4 * kWin) continue;

            double peor = 0.0; int donde = 0;
            for (int k = 1; k < n; ++k)
            {
                const double dd = std::abs ((double) d[k] - (double) d[k - 1]);
                if (dd < 0.01) continue;                 // por debajo de -40 dB no se oye un escalon

                double loc = 0.0;
                for (int j = juce::jmax (0, k - kWin); j < juce::jmin (n, k + kWin); ++j)
                    loc = juce::jmax (loc, std::abs ((double) d[j]));

                const double ratio = dd / juce::jmax (1.0e-6, 2.0 * loc);
                if (ratio > peor) { peor = ratio; donde = k; }
            }

            if (peor > peorRatio) { peorRatio = peor; peorIdx = i; peorMs = 1000.0 * donde / Kits::kRate; peorSalto = peor; }
            if (peor > 1.0)
            {
                ++culpables;
                if (culpables <= 8)
                    std::printf ("    %-8s salto %.2f veces lo que cabe, en %.1f ms\n",
                                 Kits::table()[i].name, peor, 1000.0 * donde / Kits::kRate);
            }
        }

        juce::ignoreUnused (peorSalto);
        std::printf ("%-34s %d de %d con escalon   peor %.2f de lo que cabe (#%d %s) en %.1f ms   %s\n",
                     "fabrica: chasquidos", culpables, Kits::kNumSounds,
                     peorRatio, peorIdx + 1, Kits::table()[peorIdx].name, peorMs,
                     culpables == 0 ? "OK" : "FALLA");
    }

    //  Y LO QUE DE VERDAD SUENA: la fabrica tocando un patron normal.
    //
    //  Las medidas de arriba usan dieciseis senos en fase, que es un caso
    //  hostil a proposito y ya se sabe que mete el saturador del master en
    //  accion permanente. Eso no dice nada de si la maquina distorsiona
    //  TOCANDO, que es la pregunta. Aqui suenan los sonidos de fabrica, en el
    //  patron que toca cualquiera - bombo a negras, caja al dos y al cuatro,
    //  charles a corcheas y un bajo - y se mira cuanto tiene que doblar el
    //  master. Un instrumento que satura en su patron mas simple suena a
    //  crispado y no hay mando que lo arregle.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (48, 8);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadGain (p, 1.0f);
            e.publishSample (p, Kits::render (p));
        }

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);

        //  Cuatro pistas a la vez, que es un patron y no una prueba de carga.
        const int kicks[]  = { 0, 4, 8, 12 };
        const int snares[] = { 4, 12 };
        const int hats[]   = { 0, 2, 4, 6, 8, 10, 12, 14 };
        const int bass[]   = { 0, 3, 8, 11 };
        for (int st : kicks)  e.setStep (0, st, 0, true);
        for (int st : snares) e.setStep (0, st, 1, true);
        for (int st : hats)   e.setStep (0, st, 2, true);
        for (int st : bass)   e.setStep (0, st, 6, true);
        e.setBpm (120.0f);
        e.setPlaying (true);

        double pk = 0.0; long hot = 0, tot = 0; bool nan = false;
        for (int blk = 0; blk < 400; ++blk)          // ~4.3 s
        {
            b.clear();
            e.renderNextBlock (b, 0, 512);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = b.getSample (ch, i);
                    if (! std::isfinite (v)) { nan = true; continue; }
                    const double a = std::abs ((double) v);
                    pk = juce::jmax (pk, a);
                    if (a > 0.944) ++hot;            // el codo del saturador
                    ++tot;
                }
        }
        const double pct = 100.0 * (double) hot / juce::jmax (1.0, (double) tot);
        std::printf ("%-34s pico %.3f   satura %.2f%%   NaN %s   %s\n",
                     "fabrica tocando un patron", pk, pct, nan ? "SI" : "no",
                     (! nan && pct < 0.01 && pk < 0.99) ? "OK" : "FALLA");
    }

    //  ALIASING AL SUBIR EL TONO, que es la otra forma de sonar crispado.
    //
    //  Leer mas rapido que la fuente sube el espectro entero y lo que pasa de
    //  Nyquist vuelve PLEGADO: parciales que no son armonicos de nada, o sea
    //  un silbido metalico encima de la nota. Voice tiene un paso bajo de UN
    //  polo para eso, que a 6 dB por octava es poca pared.
    //
    //  Se mide con un seno solo: a +12 semitonos, un seno de 5 kHz de una
    //  fuente a 48 kHz deberia salir a 10 kHz y nada mas. Todo lo que aparezca
    //  LEJOS de 10 kHz es material plegado, y se mide como la energia fuera de
    //  una ventana estrecha alrededor del tono esperado, en dB por debajo del
    //  tono. Un seno puro no tiene armonicos que confundir con el pliegue.
    {
        auto tone = [] (double sr, double secs, float hz)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * secs);
            sb->buffer.setSize (1, n);
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                             * hz * (float) i / (float) sr));
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  Goertzel: la energia en UNA frecuencia, sin montar una FFT. Se usa
        //  para el tono esperado y para un barrido de sondas, que es todo lo
        //  que hace falta aqui.
        auto power = [] (const float* d, int n, double sr, double hz)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / sr;
            const double c = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (int i = 0; i < n; ++i) { const double s0 = d[i] + c * s1 - s2; s2 = s1; s1 = s0; }
            return s1 * s1 + s2 * s2 - c * s1 * s2;
        };

        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
        e.setPadGain (0, 1.0f);
        e.setPadPitch (0, 12.0f);                 // una octava arriba: delta = 2
        e.publishSample (0, tone (48000.0, 1.0, 5000.0f));

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.postNoteOn (0, 1.0f);

        juce::AudioBuffer<float> cap (1, 512 * 30);
        for (int blk = 0; blk < 30; ++blk)
        {
            b.clear();
            e.renderNextBlock (b, 0, 512);
            cap.copyFrom (0, blk * 512, b, 0, 0, 512);
        }
        //  Sin el ataque ni el final: solo el regimen.
        const float* d = cap.getReadPointer (0) + 512 * 5;
        const int n = 512 * 20;

        const double wanted = power (d, n, 48000.0, 10000.0);
        double worst = 0.0; double worstHz = 0.0;
        for (double hz = 200.0; hz < 22000.0; hz += 100.0)
        {
            if (std::abs (hz - 10000.0) < 400.0) continue;      // el tono y su falda
            const double p = power (d, n, 48000.0, hz);
            if (p > worst) { worst = p; worstHz = hz; }
        }

        const double db = 10.0 * std::log10 (juce::jmax (1.0e-12, worst) / juce::jmax (1.0e-12, wanted));
        std::printf ("%-34s +12 st: pliegue peor %+.1f dB en %.0f Hz   %s\n",
                     "aliasing al subir el tono", db, worstHz,
                     db < -40.0 ? "OK" : "FLOJO");
    }

    //  EL TROCEADO POR GOLPES, contra un break del que se sabe la verdad.
    //
    //  No se puede medir un detector de golpes con una muestra de verdad,
    //  porque nadie sabe donde estan sus golpes con precision de milisegundo -
    //  y "suena bien" no es una medida. Asi que se fabrica el break: dieciseis
    //  golpes en posiciones CONOCIDAS y deliberadamente irregulares, que es lo
    //  que hace inutil el corte en trozos iguales, con tres timbres distintos y
    //  colas que se solapan. Se cuenta cuantos encuentra dentro de 15 ms, y
    //  cuantos se inventa.
    //
    //  Y con la cola encima a proposito: el charles del contratiempo cae
    //  mientras el bombo aun suena, que es el caso que un detector por ENERGIA
    //  no ve - la energia ahi baja - y el que separa el flujo espectral de una
    //  media movil.
    {
        constexpr double sr = 48000.0;
        constexpr int    len = (int) (sr * 4.0);
        juce::AudioBuffer<float> b (1, len);
        b.clear();
        float* d = b.getWritePointer (0);
        juce::Random rng (8080);

        //  Irregulares a proposito: 0, 0.31, 0.47, 0.72... nada cae en una
        //  rejilla de dieciseisavos.
        const double at[16] = { 0.00, 0.31, 0.47, 0.72, 0.95, 1.18, 1.33, 1.61,
                                1.88, 2.06, 2.29, 2.55, 2.71, 2.98, 3.22, 3.49 };
        int truth[16];
        for (int k = 0; k < 16; ++k)
        {
            truth[k] = (int) (at[k] * sr);
            const int kind = k % 3;                 // bombo, caja, charles
            const double f0 = kind == 0 ? 55.0 : kind == 1 ? 210.0 : 0.0;
            const double dec = kind == 0 ? 0.28 : kind == 1 ? 0.16 : 0.05;
            double ph = 0.0;
            const int n = juce::jmin (len - truth[k], (int) (sr * dec * 4.0));
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) i / sr;
                const double e = std::exp (-t / dec);
                double v;
                if (kind == 2) v = 0.7 * (rng.nextDouble() * 2.0 - 1.0) * e;
                else
                {
                    ph += 2.0 * juce::MathConstants<double>::pi * f0 * (1.0 + 1.2 * std::exp (-t / 0.02)) / sr;
                    v = (0.8 * std::sin (ph) + (kind == 1 ? 0.5 * (rng.nextDouble() * 2.0 - 1.0) : 0.0)) * e;
                }
                d[truth[k] + i] += (float) (0.45 * v);
            }
        }

        const auto hits = Onsets::detect (b, sr);

        const int tol = (int) (sr * 0.015);
        int found = 0;
        for (int k = 0; k < 16; ++k)
            for (int h : hits)
                if (std::abs (h - truth[k]) <= tol) { ++found; break; }

        int spurious = 0;
        for (int h : hits)
        {
            bool real = false;
            for (int k = 0; k < 16; ++k) if (std::abs (h - truth[k]) <= tol) real = true;
            if (! real) ++spurious;
        }

        //  Y QUE NO CHASQUEEN, que es la otra mitad y la que el conteo no ve:
        //  un corte en una muestra cualquiera deja un escalon de continua al
        //  principio del trozo. Se mide el valor absoluto en el punto de corte.
        double peorCorte = 0.0;
        for (int h : hits) peorCorte = juce::jmax (peorCorte, (double) std::abs (d[juce::jlimit (0, len - 1, h)]));

        //  Cuales se pierden, no solo cuantos: un detector que falla siempre
        //  los charles y uno que falla dos al azar no son el mismo detector.
        juce::String perdidos;
        for (int k = 0; k < 16; ++k)
        {
            bool hit = false;
            for (int h : hits) if (std::abs (h - truth[k]) <= tol) hit = true;
            if (! hit) perdidos += juce::String (k) + "(" + (k % 3 == 0 ? "bombo" : k % 3 == 1 ? "caja" : "charles") + ") ";
        }
        //  El corte se juzga CONTRA EL PICO de la muestra, no contra un numero
        //  absoluto, y con el liston en el 20%. El primer intento pedia 0.02 a
        //  secas, que no salia de ningun sitio: lo que decide si un corte
        //  chasquea no es su amplitud sino el ESCALON que deja, y el escalon lo
        //  cubre la envolvente de ataque del pad - 2 ms por defecto, que a
        //  48 kHz son 96 muestras de rampa desde cero. Un corte al 20% del pico
        //  con 2 ms de rampa encima no es un clic; uno al 80% si.
        double picoMuestra = 0.0;
        for (int i = 0; i < len; ++i) picoMuestra = juce::jmax (picoMuestra, (double) std::abs (d[i]));
        const double corteRel = peorCorte / juce::jmax (1.0e-9, picoMuestra);

        //  Y UN SOLO GOLPE TIENE QUE DAR UN SOLO CORTE, que es la otra mitad
        //  de la prueba y la que faltaba. Medido en la app: un bombo suelto de
        //  la fabrica -un seno de 55 Hz con envolvente de tono, 950 ms- salia
        //  con SIETE golpes. No es un fallo del umbral: un tono que baja de
        //  frecuencia va METIENDO energia en bins nuevos mientras cae, y eso es
        //  flujo espectral positivo de verdad. Lo que lo separa de un ataque es
        //  que un ataque es ANCHO de banda y un barrido grave no, asi que el
        //  flujo se pesa por frecuencia. Sin esta prueba, trocear por golpes un
        //  bombo daba siete trozos de bombo.
        int solo = 0;
        {
            const int n1 = (int) (sr * 0.95);
            juce::AudioBuffer<float> b1 (1, n1);
            float* d1 = b1.getWritePointer (0);
            double ph = 0.0;
            for (int i = 0; i < n1; ++i)
            {
                const double t = (double) i / sr;
                const double f = 55.0 * (1.0 + 1.6 * std::exp (-t / 0.03));
                ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
                d1[i] = (float) (0.9 * std::sin (ph) * std::exp (-t / 0.38));
            }
            solo = (int) Onsets::detect (b1, sr).size();
        }

        const bool ok = found >= 15 && spurious <= 3 && corteRel < 0.20 && solo == 1;
        std::printf ("%-34s %d/16 golpes   %d inventados   corte peor %.0f%% del pico   bombo solo %d   %s  %s\n",
                     "trocear por golpes", found, spurious, 100.0 * corteRel, solo,
                     ok ? "OK" : "FALLA", perdidos.toRawUTF8());
    }

    //  EL FUNDIDO DE LOS BORDES, con TRES numeros o no dice nada.
    //
    //  Cuanto baja el escalon del corte, cuanto respeta lo de dentro, y cuanto
    //  cuesta cuando esta a cero. El tercero es el que no se puede medir por
    //  nivel: un fundido que dice estar apagado y deja una multiplicacion por
    //  0.999 en el bucle interior sale como "casi igual" en dB y es un cambio
    //  real en cada muestra de la app. Se compara bit a bit.
    {
        //  Un trozo cortado por el peor sitio posible: la cresta de un seno.
        //  Ahi el corte seco deja un escalon del tamano de la amplitud entera,
        //  que es exactamente el clic del que se queja quien trocea a mano.
        auto corte = [] (double sr, double secs, float hz)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * secs);
            sb->buffer.setSize (1, n);
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (0, i, 0.8f * std::sin (juce::MathConstants<float>::twoPi
                                                             * hz * (float) i / (float) sr));
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        auto corre = [&corte] (float ms, juce::AudioBuffer<float>& cap)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (8, 2);
            e.setPadGain (0, 1.0f);
            //  Ataque a cero: lo que se mide es el borde del RECORTE, y un
            //  ataque de 2 ms taparia justo lo que se quiere ver.
            e.setPadAttack (0, 0.0f);
            e.setPadFadeIn (0, ms);
            e.setPadFadeOut (0, ms);
            auto sb = corte (48000.0, 0.5, 220.0f);
            //  El recorte empieza en la cresta: un cuarto de periodo de 220 Hz
            //  a 48 kHz son 54 muestras.
            e.publishSample (0, sb);
            e.setPadStart (0, 54);
            e.setPadEnd   (0, 54 + 4800);

            juce::AudioBuffer<float> b (2, 256);
            b.clear(); e.renderNextBlock (b, 0, 256);
            e.postNoteOn (0, 1.0f);

            cap.setSize (1, 256 * 24, false, true, true);
            for (int blk = 0; blk < 24; ++blk)
            {
                b.clear();
                e.renderNextBlock (b, 0, 256);
                cap.copyFrom (0, blk * 256, b, 0, 0, 256);
            }
        };

        juce::AudioBuffer<float> seco, suave, seco2;
        corre (0.0f, seco);
        corre (0.0f, seco2);
        corre (5.0f, suave);

        //  EL ESCALON ES LA PRIMERA MUESTRA, no el salto mas grande de los
        //  primeros diez milisegundos.
        //
        //  El primer intento medía eso segundo y daba -17.2 dB con el fundido
        //  puesto, que parecia flojo. No lo era: en diez milisegundos de un seno
        //  de 220 Hz a 0.8 el salto entre dos muestras seguidas vale 0.023 por
        //  su propia pendiente, y eso es la SENAL. Lo que hace clic es la
        //  discontinuidad contra el silencio de antes, o sea cuanto vale la
        //  primera muestra que sale: cortar en la cresta la deja en 0.8 - un
        //  escalon de fondo de escala - y con el borde suavizado vale lo que
        //  valga la ventana en su primera muestra.
        auto escalon = [] (const juce::AudioBuffer<float>& b)
        {
            const float* d = b.getReadPointer (0);
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (std::abs (d[i]) > 1.0e-7f) return (double) std::abs (d[i]);
            return 0.0;
        };
        //  Y lo de DENTRO, lejos de los dos bordes: tiene que quedar intacto.
        auto dentro = [] (const juce::AudioBuffer<float>& b)
        {
            const float* d = b.getReadPointer (0);
            double m = 0.0;
            for (int i = 2000; i < 4000; ++i) m = juce::jmax (m, (double) std::abs (d[i]));
            return m;
        };

        const double eSeco  = escalon (seco);
        const double eSuave = escalon (suave);
        const double dSeco  = dentro (seco);
        const double dSuave = dentro (suave);

        int differ = 0;
        for (int i = 0; i < seco.getNumSamples(); ++i)
            if (seco.getSample (0, i) != seco2.getSample (0, i)) ++differ;

        const double bajaDb  = 20.0 * std::log10 (juce::jmax (1.0e-9, eSuave) / juce::jmax (1.0e-9, eSeco));
        const double pierdeDb = 20.0 * std::log10 (juce::jmax (1.0e-9, dSuave) / juce::jmax (1.0e-9, dSeco));

        const bool ok = bajaDb < -30.0 && pierdeDb > -0.5 && differ == 0;
        std::printf ("%-34s escalon %+.1f dB   dentro %+.2f dB   seco identico %s   %s\n",
                     "fundido del recorte (5 ms)", bajaDb, pierdeDb,
                     differ == 0 ? "si" : "NO", ok ? "OK" : "FALLA");
    }

    //  EL CARRIL SILENCIADO Y EL TRAMO EN BUCLE.
    //
    //  Las dos herramientas de la ficha CANCION que no se pueden juzgar
    //  mirando la pantalla, porque lo que cambian es lo que SUENA. Y las dos
    //  se miden igual: se anota que pads dispara el transporte, que es lo
    //  unico que distingue "el carril esta apagado" de "el carril esta
    //  apagado en el dibujo".
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 4);
        for (int p = 0; p < 4; ++p)
        {
            e.setPadGain (p, 0.8f);
            e.publishSample (p, makeSample (48000.0, 0.05, 220.0f * (float) (p + 1)));
        }
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        //  Cuatro patrones, uno por carril: el patron n dispara el pad n en su
        //  paso 0. Asi el pad que suena DICE que carril lo mando.
        for (int bank = 0; bank < 4; ++bank)
        {
            e.clearPattern (bank);
            e.setPatternLength (bank, 16);
            e.setStep (bank, 0, bank, true);
        }
        for (int ln = 0; ln < 4; ++ln)
            for (int bar = 0; bar < 4; ++bar)
                e.setSongCell (ln, bar, ln + 1);

        e.setSongLength (4);
        e.setSongMode (true);
        e.setBpm (240.0);          // deprisa, para que cuatro compases quepan

        //  Cuantos bloques dura un compas: 16 pasos de semicorchea a 240 BPM.
        const double stepSec = (60.0 / 240.0) * 0.25;
        const int blocksPerBar = (int) (16.0 * stepSec * 48000.0 / 256.0) + 1;

        auto corre = [&] (int bars) noexcept
        {
            std::uint64_t visto = 0;
            e.setPlaying (true);
            for (int i = 0; i < blocksPerBar * bars; ++i)
            {
                e.renderNextBlock (b, 0, 256);
                visto |= e.fetchTriggered();
            }
            e.setPlaying (false);
            e.renderNextBlock (b, 0, 256);
            e.fetchTriggered();
            return visto;
        };

        const auto todos = corre (5);
        e.setSongLaneMute (2, true);
        const auto conMudo = corre (5);
        e.setSongLaneMute (2, false);

        const bool mudoOk = (todos & 0xFu) == 0xFu && (conMudo & (1u << 2)) == 0
                         && (conMudo & 0xBu) == 0xBu;
        std::printf ("%-34s suenan %X   con el carril 3 mudo %X   %s\n",
                     "carril de cancion silenciado", (unsigned) (todos & 0xFu),
                     (unsigned) (conMudo & 0xFu), mudoOk ? "OK" : "FALLA");

        //  EL BUCLE. Con [0,2) puesto, los carriles 3 y 4 -que solo tienen
        //  bloque en sus compases- siguen sonando porque su bloque esta en
        //  todos los compases; lo que hay que mirar es el COMPAS que reporta
        //  el motor, que nunca puede pasar de 1.
        e.setSongLoop (0, 2);
        int peorCompas = -1;
        e.setPlaying (true);
        for (int i = 0; i < blocksPerBar * 8; ++i)
        {
            e.renderNextBlock (b, 0, 256);
            peorCompas = juce::jmax (peorCompas, e.getSongBar());
        }
        e.setPlaying (false);
        e.renderNextBlock (b, 0, 256);

        //  Y sin bucle tiene que llegar al ultimo, o la prueba de arriba
        //  pasaria igual con un transporte que no avanza.
        e.clearSongLoop();
        int sinBucle = -1;
        e.setPlaying (true);
        for (int i = 0; i < blocksPerBar * 8; ++i)
        {
            e.renderNextBlock (b, 0, 256);
            sinBucle = juce::jmax (sinBucle, e.getSongBar());
        }
        e.setPlaying (false);

        //  EL BLOQUE MANDA SOBRE EL PATRON.
        //
        //  Un patron de 16 pasos -un compas- puesto en un bloque de DOS
        //  compases tiene que sonar los dos, dando la vuelta; y puesto en un
        //  bloque de uno, sonar uno. Antes la longitud la ponia el patron y un
        //  bloque no podia durar otra cosa, asi que acortarlo obligaba a
        //  acortar el patron - o sea a cambiarlo en los demas sitios donde
        //  estuviera puesto. Se cuenta cuantas veces dispara su pad.
        e.clearSongLoop();
        for (int ln = 0; ln < 4; ++ln)
            for (int bar = 0; bar < 4; ++bar)
                e.setSongCell (ln, bar, 0);

        auto disparos = [&] (int compases) noexcept
        {
            e.setSongCell (0, 0, 1);                  // patron 1 en el carril 0
            for (int b2 = 1; b2 < compases; ++b2)
                e.setSongCell (0, b2, AudioEngine::kContinued);
            for (int b2 = compases; b2 < 4; ++b2)
                e.setSongCell (0, b2, 0);

            e.setSongLength (4);
            int n = 0;
            //  UNA SOLA VUELTA. blocksPerBar redondea hacia arriba, asi que
            //  cuatro compases de bloques se pasan de largo, la cancion da la
            //  vuelta y el bloque dispara otra vez: la primera version conto
            //  esa de mas y saco 2 donde tenia que salir 1. Se para en cuanto
            //  el compas vuelve a cero habiendo pasado del cero.
            bool salido = false;
            e.setPlaying (true);
            for (int i = 0; i < blocksPerBar * 5; ++i)
            {
                e.renderNextBlock (b, 0, 256);
                //  La vuelta se mira ANTES de contar: el bloque en el que la
                //  cancion vuelve al compas cero ya trae el disparo de la
                //  segunda pasada, y contarlo daba 2 donde tenia que dar 1.
                if (e.getSongBar() > 0) salido = true;
                else if (salido) break;
                if (e.fetchTriggered() & 1u) ++n;
            }
            e.setPlaying (false);
            e.renderNextBlock (b, 0, 256);
            e.fetchTriggered();
            return n;
        };

        const int uno = disparos (1);
        const int dos = disparos (2);
        const bool largoOk = uno == 1 && dos == 2;
        std::printf ("%-34s bloque de 1 compas suena %d vez   de 2 suena %d   %s\n",
                     "largo propio del bloque", uno, dos, largoOk ? "OK" : "FALLA");

        const bool bucleOk = peorCompas == 1 && sinBucle == 3;
        std::printf ("%-34s con bucle [0,2) llega al %d   sin bucle al %d   %s\n",
                     "bucle de un tramo", peorCompas, sinBucle, bucleOk ? "OK" : "FALLA");
    }

    //  EL ACORDE. Un paso con cuatro notas tiene que disparar el pad CUATRO
    //  veces en el mismo instante, y no una: es lo unico que separa un piano
    //  roll de un mando de afinacion. Se cuenta por VOCES vivas y no por
    //  disparos, porque triggeredMask es un bit por pad y cuatro disparos del
    //  mismo pad ponen el mismo bit - la prueba obvia habria dicho que si sin
    //  mirar nada.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 8);
        e.setPadGain (0, 0.8f);
        e.publishSample (0, makeSample (48000.0, 1.0, 220.0f));
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        auto vivas = [&] (bool conAcorde) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);
            e.setStepNote (0, 0, 0, 0);
            e.clearStepExtras (0, 0, 0);
            if (conAcorde)
            {
                e.setStepExtra (0, 0, 0, 0, 4, true);    // tercera mayor
                e.setStepExtra (0, 0, 0, 1, 7, true);    // quinta
                e.setStepExtra (0, 0, 0, 2, 12, true);   // octava
            }
            e.setBpm (120.0);
            e.setPlaying (true);
            int pico = 0;
            for (int i = 0; i < 40; ++i)
            {
                e.renderNextBlock (b, 0, 256);
                pico = juce::jmax (pico, e.getActiveVoiceCount());
            }
            e.setPlaying (false);
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 256);
            return pico;
        };

        const int sola = vivas (false);
        const int acorde = vivas (true);
        const bool ok = sola == 1 && acorde == 4;
        std::printf ("%-34s una nota %d voz   acorde de cuatro %d voces   %s\n",
                     "acorde en un paso", sola, acorde, ok ? "OK" : "FALLA");
    }

    //  OIR UNA TECLA DEL PIANO ROLL. Dos cosas a la vez o no vale:
    //
    //  que SUENE en la nota que se toca -el comando ya llevaba el semitono y
    //  handleCommand lo tenia clavado a cero, asi que la unica forma de oir un
    //  do sostenido desde la interfaz era setPadPitch- y que el pad siga
    //  afinado donde estaba. Solo la primera la pasa el codigo viejo, y encima
    //  desafinado el primer golpe: postNoteOn lee lo ALMACENADO, asi que el
    //  orden de setPadPitch y postNoteOn no es el orden en que el audio los ve.
    //
    //  La nota se mide en la DURACION y no en cruces por cero: el seno del
    //  banco lleva un 20% de ruido encima a proposito, y contar cruces sobre
    //  ruido cuenta el ruido - la primera version de esta prueba dijo x0.86
    //  con el motor ya arreglado. En CINTA, que es como nace un pad, subir una
    //  octava recorre la fuente al doble de velocidad: la voz dura la mitad.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (16, 4);
        e.setPadGain (0, 0.9f);
        e.publishSample (0, makeSample (48000.0, 1.0, 220.0f));
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        e.setPadPitch (0, 3.0f);                 // el pad esta afinado en +3
        const float antes = e.getPadPitch (0);

        auto dura = [&] (int semis) noexcept
        {
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 256);
            e.postNoteOnAt (0, semis, 0.9f);
            int bloques = 0;
            for (int i = 0; i < 400; ++i)
            {
                e.renderNextBlock (b, 0, 256);
                if (e.getActiveVoiceCount() > 0) bloques = i + 1;
                else if (i > 2) break;
            }
            return bloques;
        };

        const int grave = dura (0);
        const int agudo = dura (12);
        const float despues = e.getPadPitch (0);
        //  Una octava exacta es la mitad de tiempo; se acepta 1.9-2.1 porque el
        //  final de la voz cae dentro de un bloque de 256 y no en su borde.
        const double razon = agudo > 0 ? (double) grave / (double) agudo : 0.0;
        const bool ok = razon > 1.9 && razon < 2.1 && std::abs (despues - antes) < 0.001f;
        std::printf ("%-34s +0 dura %d bloques   +12 dura %d (x%.2f)   pad sigue en %.0f   %s\n",
                     "audicion del piano roll", grave, agudo, razon, despues, ok ? "OK" : "FALLA");
    }

    //  EL BUCLE NO PUEDE CHASQUEAR EN CADA VUELTA.
    //
    //  Al dar la vuelta la senal salta del ultimo dato al primero, y eso es un
    //  escalon: un chasquido por vuelta. Se mide como los chasquidos de la
    //  fabrica - el salto entre dos muestras seguidas contra el pico de lo que
    //  hay alrededor - porque "suena mal" no es una medida y un bucle siempre
    //  tiene saltos legitimos dentro.
    //
    //  La fuente es media onda de 110 Hz: al volver, el ultimo dato esta en el
    //  maximo y el primero en cero, que es el peor escalon posible.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (8, 4);
        e.setPadGain (0, 0.9f);
        auto* sb = new SampleBuffer();
        const int n = 4800;                     // 100 ms
        sb->buffer.setSize (2, n);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (ch, i, 0.7f * std::sin (juce::MathConstants<float>::pi
                                                              * (float) i / (float) n));
        sb->sourceSampleRate = 48000.0;
        e.publishSample (0, SampleBuffer::Ptr (sb));
        e.setPadLoop (0, true);
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        e.postNoteOn (0, 1.0f);
        float peor = 0.0f, pico = 0.0f, prev = 0.0f;
        for (int i = 0; i < 120; ++i)
        {
            e.renderNextBlock (b, 0, 256);
            const auto* d = b.getReadPointer (0);
            for (int k = 0; k < 256; ++k)
            {
                if (i > 4) peor = juce::jmax (peor, std::abs (d[k] - prev));
                pico = juce::jmax (pico, std::abs (d[k]));
                prev = d[k];
            }
        }
        //  El liston: el salto mas grande que una senal de banda limitada puede
        //  dar entre dos muestras seguidas a este nivel. Un escalon de vuelta
        //  vale el pico entero; un fundido de 3 ms lo deja en centesimas.
        const float razon = pico > 0.0001f ? peor / pico : 0.0f;
        const bool ok = razon < 0.10f;
        std::printf ("%-34s salto peor %.4f de un pico de %.3f (%.1f%%)   %s\n",
                     "bucle sin chasquido", peor, pico, 100.0f * razon, ok ? "OK" : "FALLA");
    }

    //  EL LARGO DE LA NOTA. Una nota no es un cuadrado: dura lo que dice el
    //  paso, y eso se mide contando cuantos bloques sigue viva la voz.
    //
    //  En CUARTOS de paso, que es la otra mitad: medido en pasos, lo mas corto
    //  que se puede escribir es la rejilla. Se comprueban los dos extremos -
    //  que un largo de 2 pasos dure el doble que uno de 1, y que medio paso
    //  dure menos que uno - porque solo el primero lo pasa un largo que no
    //  hace nada mas que redondear.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 64); e.setPolyphony (16, 4);
        e.setPadGain (0, 0.9f);
        //  Sin decaimiento: una muestra que se apaga sola mide su envolvente y
        //  no el largo. Cuatro segundos, de sobra para cualquier paso.
        e.publishSample (0, makeSample (48000.0, 4.0, 220.0f, false));
        juce::AudioBuffer<float> b (2, 64);
        runBlocks (e, b, 64, 4);

        auto vive = [&] (int cuartos) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);
            e.setStepLen (0, 0, 0, cuartos);
            e.setBpm (120.0);
            e.setPlaying (true);

            int bloques = 0, vistos = 0;
            for (int i = 0; i < 400; ++i)
            {
                e.renderNextBlock (b, 0, 64);
                if (e.getActiveVoiceCount() > 0) { bloques = i + 1; ++vistos; }
                else if (vistos > 0) break;      // ya sono y ya se solto
            }
            e.setPlaying (false);
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 64);
            return bloques;
        };

        //  Un paso a 120 BPM en semicorcheas son 6000 muestras, o sea 93
        //  bloques de 64. Se compara la RAZON y no el instante: cuando arranca
        //  el transporte respecto al primer bloque no es lo que esto mide.
        const int medio = vive (2);      // medio paso
        const int uno   = vive (4);      // un paso
        const int dos   = vive (8);      // dos pasos
        const double r1 = medio > 0 ? (double) uno / (double) medio : 0.0;
        const double r2 = uno   > 0 ? (double) dos / (double) uno   : 0.0;
        const bool ok = r1 > 1.5 && r1 < 2.5 && r2 > 1.5 && r2 < 2.5;
        std::printf ("%-34s medio paso %d bloques   uno %d (x%.2f)   dos %d (x%.2f)   %s\n",
                     "largo de la nota", medio, uno, r1, dos, r2, ok ? "OK" : "FALLA");
    }

    //  EL EMPUJON DE UN PASO. HUMANIZAR escribe cuanto se aparta cada golpe de
    //  la rejilla, y lo que hay que comprobar es que el motor lo OBEDECE: un
    //  empujon que no mueve nada es un numero guardado, no un groove. Se mide
    //  en muestras contando cuantos bloques tarda en sonar el pad.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 64); e.setPolyphony (16, 4);
        e.setPadGain (0, 0.9f);
        e.publishSample (0, makeSample (48000.0, 0.2, 440.0f));
        juce::AudioBuffer<float> b (2, 64);
        runBlocks (e, b, 64, 4);

        auto cuando = [&] (int centesimas) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 4, 0, true);          // un golpe en el paso 4
            e.setStepNudge (0, 4, 0, centesimas);
            e.setBpm (120.0);
            e.setPlaying (true);
            int bloque = -1;
            for (int i = 0; i < 400 && bloque < 0; ++i)
            {
                e.renderNextBlock (b, 0, 64);
                if (e.fetchTriggered() & 1u) bloque = i;
            }
            e.setPlaying (false);
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 64);
            e.fetchTriggered();
            return bloque;
        };

        //  Un paso a 120 BPM en semicorcheas son 6000 muestras; 25 centesimas
        //  son 1500, o sea unos 23 bloques de 64. Se compara la DIFERENCIA y
        //  no el instante absoluto: cuando arranca el transporte respecto al
        //  primer bloque depende de cosas que no son esta prueba.
        const int recto = cuando (0);
        const int tarde = cuando (25);
        const int diff  = tarde - recto;
        const bool ok = recto >= 0 && tarde >= 0 && diff >= 20 && diff <= 26;
        std::printf ("%-34s recto en el bloque %d   +25%% en el %d   diferencia %d (esperada 23)   %s\n",
                     "empujon de un paso", recto, tarde, diff, ok ? "OK" : "FALLA");
    }

    //  EL BLOQUEO DEL CORTE, paso a paso. Un filtro por pad es un ajuste; un
    //  filtro que cambia en cada paso es una linea de bajo que se abre y se
    //  cierra sola. Se mide por ENERGIA ALTA: el mismo golpe con el paso
    //  bloqueado a 200 Hz tiene que perder los agudos, y con el paso abierto
    //  no. Comparar el nivel entero no valdria - un filtro paso bajo con el
    //  corte alto baja el total muy poco y "casi lo mismo" es lo que deja
    //  pasar un bloqueo que no hace nada.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 128); e.setPolyphony (16, 4);
        e.setPadGain (0, 0.9f);
        //  Ruido, no un tono: para medir cuanto agudo queda hace falta que
        //  haya agudo que quitar en todas las frecuencias.
        {
            auto* sb = new SampleBuffer();
            const int n = 24000;
            juce::Random rng (12345);
            sb->buffer.setSize (2, n);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (ch, i, 0.5f * (rng.nextFloat() * 2.0f - 1.0f));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, SampleBuffer::Ptr (sb));
        }
        juce::AudioBuffer<float> b (2, 128);
        runBlocks (e, b, 128, 4);

        auto agudos = [&] (int porCiento) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);
            e.setStepLock (0, 0, 0, porCiento);
            e.setPadCutoff (0, AudioEngine::kFiltOpenHz);
            e.setPadReso (0, 0.0f);
            e.setBpm (120.0);
            e.setPlaying (true);

            //  La energia de la DIFERENCIA entre muestras, que es un paso alto
            //  de primer orden: lo que sobrevive a el son los agudos, y no
            //  hace falta una transformada para saber si siguen ahi.
            double alta = 0.0; float ant = 0.0f; int n = 0;
            for (int i = 0; i < 24; ++i)
            {
                e.renderNextBlock (b, 0, 128);
                if (i < 4) continue;                 // deja que el golpe empiece
                const float* d = b.getReadPointer (0);
                for (int k = 0; k < 128; ++k) { const float dif = d[k] - ant; alta += (double) dif * dif; ant = d[k]; ++n; }
            }
            e.setPlaying (false);
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 128);
            e.fetchTriggered();
            return std::sqrt (alta / juce::jmax (1, n));
        };

        const double abierto = agudos (AudioEngine::kNoLock);
        //  200 Hz es el 33 % del recorrido de 20 Hz a 20 kHz por octavas.
        const double cerrado = agudos (AudioEngine::hzToLock (200.0f));
        const double caidaDb = 20.0 * std::log10 (juce::jmax (1.0e-9, cerrado)
                                                / juce::jmax (1.0e-9, abierto));
        const bool ok = caidaDb < -20.0;
        std::printf ("%-34s sin bloqueo %.4f   con 200 Hz %.4f   %+.1f dB de agudos   %s\n",
                     "bloqueo del corte por paso", abierto, cerrado, caidaDb, ok ? "OK" : "FALLA");
    }

    //  LOS OTROS CUATRO BLOQUEOS: ataque, caida, inicio y pan.
    //
    //  Los cuatro se miden CONTRA EL MISMO PASO SIN BLOQUEAR y no contra un
    //  numero absoluto, que es lo unico que separa "el bloqueo hace algo" de
    //  "el pad ya sonaba asi". Y el pan se mide en DOS instantes, que es donde
    //  estaba el fallo: retarget vuelve a leer el pan del pad una vez por
    //  bloque para que un fader de la mesa mueva lo que suena, asi que sin
    //  Voice::panPropio el bloqueo duraba 128 muestras y luego se deshacia.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 128); e.setPolyphony (16, 4);
        e.setPadGain (0, 0.9f);
        //  Media muestra en silencio y media con tono: asi el bloqueo de
        //  INICIO se mide por lo unico que no admite discusion - si empieza en
        //  la mitad, suena desde el primer bloque; si no, no suena nada.
        const int n = 48000;
        {
            auto* sb = new SampleBuffer();
            sb->buffer.setSize (2, n);
            sb->buffer.clear();
            for (int ch = 0; ch < 2; ++ch)
                for (int i = n / 2; i < n; ++i)
                    sb->buffer.setSample (ch, i, 0.5f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                                 * 440.0 * (double) i / 48000.0));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, SampleBuffer::Ptr (sb));
        }
        juce::AudioBuffer<float> b (2, 128);
        runBlocks (e, b, 128, 4);

        //  El pad, sin bloqueos, esta en el centro, ataque corto y empieza por
        //  el principio: cualquier diferencia que salga es del paso.
        e.setPadPan (0, 0.0f);
        e.setPadAttack (0, 1.0f);
        e.setPadRelease (0, 40.0f);
        e.setPadStart (0, 0);
        e.setPadEnd (0, n);

        //  Devuelve la energia por canal en una ventana de bloques [desde,hasta)
        //  contada desde que arranca el transporte.
        struct Med { double izq, der; };
        auto correr = [&] (int cual, int pct, int desde, int hasta,
                           int cual2 = -1, int pct2 = 0) -> Med
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);
            for (int c = 0; c < AudioEngine::kNumPLocks; ++c)
                e.setStepPLock (0, 0, 0, c, AudioEngine::kNoPLock);
            if (cual  >= 0) e.setStepPLock (0, 0, 0, cual,  pct);
            if (cual2 >= 0) e.setStepPLock (0, 0, 0, cual2, pct2);
            e.setBpm (120.0);
            e.setPlaying (true);
            double li = 0.0, de = 0.0; int cont = 0;
            for (int i = 0; i < hasta; ++i)
            {
                e.renderNextBlock (b, 0, 128);
                if (i < desde) continue;
                const float* L = b.getReadPointer (0);
                const float* R = b.getReadPointer (1);
                for (int k = 0; k < 128; ++k) { li += (double) L[k] * L[k]; de += (double) R[k] * R[k]; }
                ++cont;
            }
            e.setPlaying (false);
            e.postPanic();
            for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 128);
            e.fetchTriggered();
            const double d = (double) juce::jmax (1, cont) * 128.0;
            return { std::sqrt (li / d), std::sqrt (de / d) };
        };

        //  INICIO. El paso arranca en la mitad de la muestra, que es donde
        //  empieza el tono. Sin bloqueo, esos mismos bloques son silencio.
        {
            const auto sin = correr (-1, 0, 0, 12);
            const auto con = correr (AudioEngine::plockInicio, 50, 0, 12);
            const bool ok = sin.izq < 0.001 && con.izq > 0.05;
            std::printf ("%-34s sin bloqueo %.4f   al 50%% %.4f   %s\n",
                         "bloqueo de inicio", sin.izq, con.izq, ok ? "OK" : "FALLA");
        }

        //  PAN. Todo a la izquierda, y medido DOS veces: en el primer bloque y
        //  veinte bloques despues. La segunda es la que fallaba.
        {
            //  Y CON EL INICIO BLOQUEADO A LA MITAD, que es donde empieza el
            //  tono: la muestra tiene medio segundo de silencio delante -187
            //  bloques- asi que sin esto los dos canales miden cero y el banco
            //  declara que el pan no hace nada. Primero se duda de la prueba.
            const int mitad = AudioEngine::plockInicio;
            const auto pron = correr (AudioEngine::plockPan, 0, 0,  2,  mitad, 50);
            const auto tard = correr (AudioEngine::plockPan, 0, 20, 40, mitad, 50);
            const auto cen  = correr (mitad, 50, 20, 40);
            //  La ventana tardia es la que importa: es la que el pan del pad
            //  habria deshecho un bloque despues de nacer.
            const double ratioT = tard.izq / juce::jmax (1.0e-9, tard.der);
            const double ratioC = cen.izq  / juce::jmax (1.0e-9, cen.der);
            const bool ok = pron.izq >= pron.der && ratioT > 20.0
                              && ratioC > 0.9 && ratioC < 1.1;
            //  En dB, que el canal derecho se queda en cero exacto y el
            //  cociente sale en cientos de millones: un numero que no se puede
            //  leer no es un resultado.
            std::printf ("%-34s L/R al centro %.2f   bloqueado a la izquierda %+.0f dB   %s\n",
                         "bloqueo de pan", ratioC,
                         20.0 * std::log10 (juce::jlimit (1.0e-9, 1.0e9, ratioT)),
                         ok ? "OK" : "FALLA");
        }

        //  ATAQUE. Doscientos milisegundos son 75 bloques de 128, asi que en
        //  los primeros veinte -34 ms- la envolvente va por el 17 % y lo que
        //  suena tiene que ser MUCHO menos que con el ataque del pad, que es
        //  de un milisegundo. Se mide desde el bloque 47, que es donde el tono
        //  de la muestra empieza a sonar con el inicio tambien bloqueado.
        {
            auto conAtaque = [&] (int pct) -> double
            {
                e.setSongMode (false);
                e.clearPattern (0);
                e.setPatternLength (0, 16);
                e.setStep (0, 0, 0, true);
                for (int c = 0; c < AudioEngine::kNumPLocks; ++c)
                    e.setStepPLock (0, 0, 0, c, AudioEngine::kNoPLock);
                //  Inicio a la mitad para que haya senal desde el primer
                //  bloque: medir un ataque sobre silencio no mide nada.
                e.setStepPLock (0, 0, 0, AudioEngine::plockInicio, 50);
                if (pct >= 0) e.setStepPLock (0, 0, 0, AudioEngine::plockAtaque, pct);
                e.setBpm (120.0);
                e.setPlaying (true);
                double en = 0.0; int cont = 0;
                for (int i = 0; i < 10; ++i)
                {
                    e.renderNextBlock (b, 0, 128);
                    const float* L = b.getReadPointer (0);
                    for (int k = 0; k < 128; ++k) { en += (double) L[k] * L[k]; ++cont; }
                }
                e.setPlaying (false);
                e.postPanic();
                for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 128);
                e.fetchTriggered();
                return std::sqrt (en / juce::jmax (1, cont));
            };
            const double corto = conAtaque (-1);
            const double largo = conAtaque (100);          // 200 ms
            const double db = 20.0 * std::log10 (juce::jmax (1.0e-9, largo)
                                               / juce::jmax (1.0e-9, corto));
            const bool ok = db < -12.0;
            std::printf ("%-34s pad 1 ms %.4f   paso 200 ms %.4f   %+.1f dB   %s\n",
                         "bloqueo de ataque", corto, largo, db, ok ? "OK" : "FALLA");
        }

        //  CAIDA. Se mide por lo que dura la cola DESPUES de soltar, asi que el
        //  paso lleva tambien largo -sin el, la nota no se suelta nunca y no
        //  hay caida que medir-. Se cuentan bloques con voz viva.
        {
            auto vive = [&] (int pct) -> int
            {
                e.setSongMode (false);
                e.clearPattern (0);
                e.setPatternLength (0, 16);
                e.setStep (0, 0, 0, true);
                for (int c = 0; c < AudioEngine::kNumPLocks; ++c)
                    e.setStepPLock (0, 0, 0, c, AudioEngine::kNoPLock);
                e.setStepLen (0, 0, 0, 2);                 // medio paso
                if (pct >= 0) e.setStepPLock (0, 0, 0, AudioEngine::plockCaida, pct);
                e.setBpm (120.0);
                e.setPlaying (true);
                int vivos = 0;
                for (int i = 0; i < 120; ++i)
                {
                    e.renderNextBlock (b, 0, 128);
                    if (e.getActiveVoiceCount() > 0) ++vivos;
                }
                e.setPlaying (false);
                e.postPanic();
                for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 128);
                e.fetchTriggered();
                e.setStepLen (0, 0, 0, AudioEngine::kLenSuelto);
                return vivos;
            };
            const int corta = vive (0);                    // 1 ms
            const int larga = vive (100);                  // 800 ms
            const bool ok = larga > corta + 10;
            std::printf ("%-34s caida 1 ms %d bloques   800 ms %d   %s\n",
                         "bloqueo de caida", corta, larga, ok ? "OK" : "FALLA");
        }
    }

    //  EL ENVIO POR DEFECTO, que es un numero con consecuencias y no una
    //  preferencia. Estaba en uno -los 64 pads a tope en los seis efectos- y
    //  ahora en cero, y lo que hay que demostrar no es que el getter devuelva
    //  cero: es que un efecto ABIERTO no se oye hasta que alguien sube el
    //  envio de ese pad.
    //
    //  Se mide con la cola del delay y no con el nivel total, que es donde la
    //  prueba obvia se equivocaria: el pad suena igual en las dos corridas
    //  -el camino seco no pasa por el envio- asi que comparar picos daria "casi
    //  lo mismo" con el envio abierto y con el envio cerrado. Lo que separa las
    //  dos es lo que suena DESPUES de que la muestra se acabe.
    {
        auto cola = [] (float envio)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
            e.setDlyMix (1.0f); e.setDlyTime (100.0f); e.setDlyFb (0.5f);
            e.setPadGain (0, 1.0f);
            //  Menos de cero significa "no se toca": asi la corrida de control
            //  es exactamente la maquina recien encendida y no una que alguien
            //  ha puesto a cero, que no es lo mismo aunque de el mismo numero.
            if (envio >= 0.0f) e.setPadSend (0, 3, envio);
            e.publishSample (0, makeSample (48000.0, 0.05, 400.0f));

            juce::AudioBuffer<float> b (2, 512);
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.postNoteOn (0, 1.0f);

            //  50 ms de muestra a 48 kHz son 2400 muestras: la ventana empieza
            //  en el bloque 8 -4096 muestras, la muestra ya acabada- y llega
            //  hasta el 40, que cubre tres repeticiones de 100 ms.
            double pico = 0.0;
            for (int blk = 0; blk < 40; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, 512);
                if (blk < 8) continue;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        pico = juce::jmax (pico, (double) std::abs (b.getSample (ch, i)));
            }
            return pico;
        };

        const double defecto = cola (-1.0f);
        const double abierto = cola (1.0f);
        const bool ok = defecto < 1.0e-4 && abierto > 0.01;
        std::printf ("%-34s cola por defecto %.5f   con el envio a 1 %.4f   %s\n",
                     "envio por defecto cerrado", defecto, abierto, ok ? "OK" : "FALLA");
    }

    //  EL MASTER DE LA PERSONA CONTRA EL AVISO DEL SISTEMA.
    //
    //  Los dos escribian en el mismo numero, y con eso la primera notificacion
    //  se lleva por delante el nivel que hayas dejado puesto: el aviso baja, el
    //  aviso pasa, y el "vuelve a 1.0" te sube el master AL MAXIMO aunque
    //  estuviera a la mitad. Se comprueban las tres cosas de golpe, que por
    //  separado cualquiera de ellas pasa con la version rota.
    {
        AudioEngine e;
        const float puesto = 0.50f;
        e.setMasterUser (puesto);
        const float solo    = e.getMasterGain();
        e.setDucked (true);
        const float bajado  = e.getMasterGain();
        e.setDucked (false);
        const float vuelto  = e.getMasterGain();

        //  Y AL REVES TAMBIEN: mover el mando MIENTRAS el aviso suena no puede
        //  cancelar el aviso. Es el mismo fallo por el otro lado.
        e.setDucked (true);
        e.setMasterUser (0.80f);
        const float durante = e.getMasterGain();
        e.setDucked (false);

        const bool ok = std::abs (solo    - puesto) < 1.0e-6f
                     && std::abs (bajado  - puesto * AudioEngine::kDuckGain) < 1.0e-6f
                     && std::abs (vuelto  - puesto) < 1.0e-6f
                     && std::abs (durante - 0.80f * AudioEngine::kDuckGain) < 1.0e-6f;
        std::printf ("%-34s puesto %.2f   con aviso %.3f   al volver %.2f   %s\n",
                     "el aviso no se come el master", solo, bajado, vuelto, ok ? "OK" : "FALLA");
    }

    //  Y NO VIAJA AL REBOTE. Bajar el master para no despertar a nadie no puede
    //  salir impreso en el fichero que exportas, que es un fallo que solo se
    //  descubre cuando ya has mandado el tema. El motor del rebote se construye
    //  con copyStateFrom y ahi el master NO esta: se comprueba, porque "no
    //  esta" es exactamente la clase de cosa que alguien anade sin querer.
    {
        AudioEngine vivo, rebote;
        vivo.setMasterUser (0.25f);
        rebote.copyStateFrom (vivo);
        const bool ok = std::abs (rebote.getMasterGain() - 1.0f) < 1.0e-6f;
        std::printf ("%-34s en vivo %.2f   en el rebote %.2f   %s\n",
                     "el master no viaja al rebote", vivo.getMasterGain(),
                     rebote.getMasterGain(), ok ? "OK" : "FALLA");
    }

    //  EL MARGEN QUE LA MAQUINA NO PODIA USAR.
    //
    //  El master estaba acotado en la unidad y los dieciseis canales llegan a
    //  +12: para subir el conjunto entero habia que subir dieciseis faders uno
    //  a uno, y eso mueve la mezcla porque cada uno entra en el bus por su
    //  lado. El tope de arriba se comprueba con TRES cifras y no con una,
    //  porque "sube" lo cumple tambien un mando que se pasa de rosca:
    //
    //    - que la unidad siga siendo la unidad, o sea que nada se movio abajo,
    //    - que +12 dB llegue de verdad al motor - un jlimit olvidado en 1.0
    //      dejaria el mando subiendo en la pantalla y el sonido quieto,
    //    - y que POR ENCIMA se acote, que un mando sin techo es como se manda
    //      un infinito al bus.
    //
    //  Y el aviso del sistema tiene que seguir atenuando DESDE ahi: el objetivo
    //  es el producto, asi que con el master arriba el aviso baja lo mismo en
    //  proporcion. Sin esa cuarta cifra, subir el tope habria dejado la
    //  notificacion sonando a todo volumen.
    {
        AudioEngine e;
        const float tope = AudioEngine::kMasterMaxGain;

        e.setMasterUser (1.0f);
        const float unidad = e.getMasterGain();
        e.setMasterUser (tope);
        const float arriba = e.getMasterGain();
        e.setMasterUser (tope * 4.0f);          // muy por encima: tiene que acotar
        const float pasado = e.getMasterGain();
        e.setMasterUser (tope);
        e.setDucked (true);
        const float avisado = e.getMasterGain();
        e.setDucked (false);

        const bool ok = std::abs (unidad - 1.0f) < 1.0e-6f
                     && std::abs (arriba - tope) < 1.0e-5f
                     && std::abs (pasado - tope) < 1.0e-5f
                     && std::abs (avisado - tope * AudioEngine::kDuckGain) < 1.0e-5f;
        std::printf ("%-34s unidad %.2f   tope %.2f (+%.1f dB)   pasado %.2f   con aviso %.2f   %s\n",
                     "el master llega a donde un canal", unidad, arriba,
                     20.0f * std::log10 (arriba), pasado, avisado, ok ? "OK" : "FALLA");
    }

    return 0;
}

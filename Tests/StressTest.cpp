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
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>

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
            if (b == 80)  d.setMasterGain (0.28f);
            if (b == 240) d.setMasterGain (1.00f);
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

    return 0;
}

// ZATI expo bench — the audio half.
//
// The interface can be measured by walking a component tree. The engine has to
// be RUN, and run the way a stand runs it: sixteen pads slammed at once, the
// same pad retriggered faster than a human can, the command queue pushed past
// its own capacity, the device torn down and rebuilt mid-phrase. What comes
// out is checked for the three things that end a demo — silence, a NaN, and a
// block that took longer than it had.
#include <JuceHeader.h>
#include <functional>
#include "../Source/AudioEngine.h"
#include "../Source/Denoise.h"
#include "../Source/MidiIo.h"
#include "../Source/Kits.h"
#include "../Source/Onsets.h"
#include "../Source/Sintes.h"
#include "../Source/Diezmador.h"
#include "../Source/Eq5.h"
#include "../Source/FxVisor.h"
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
//  UN MOTOR DE PRUEBA CON LOS PADS EN EL CANAL 0.
//
//  Desde que un pad nace SIN canal -ver `AudioEngine::kSinCanal`- «recien
//  encendido» ya no quiere decir «todo entra en el canal 0», y eso dejo
//  TREINTA Y CUATRO comprobaciones de este fichero midiendo el camino de un
//  efecto con pads que no mandaban a ningun bus: el delay sin cola, el EQ sin
//  cambiar una muestra, el compresor a +0.00 dB. Ninguna era un fallo del
//  motor — era el andamio dando por hecho un defecto que cambio, que es
//  exactamente lo que esta casa llama medir el estado de la corrida de antes.
//
//  Se pone AQUI y no en cada sitio a mano porque lo que estas pruebas miden es
//  el EFECTO y no el enrutado: quien quiera medir el enrutado pone sus canales
//  despues, que es lo que hacen las tres que lo hacen.
static void enCanalCero (AudioEngine& e) noexcept
{
    for (int p = 0; p < AudioEngine::kNumPads; ++p) e.setPadCanal (p, 0);
}

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

//  ============================================================================
//  QUE ESTE BANCO PUEDA DECIR QUE NO.
//
//  Este fichero tiene CINCUENTA Y UN sitios que imprimen FALLA y dos que
//  imprimen "<-- FAILED", y su main hacia `return 0` pasara lo que pasara.
//  banco.yml lo corre como paso de CI, asi que un FALLA del motor salia por
//  pantalla y el job seguia verde: es "una linea que imprime OK" aplicada
//  justo al banco que vigila el camino de audio. Ya paso lo mismo con expo.py,
//  session.py, apk.py y plano.py, y las cuatro se arreglaron igual.
//
//  No hace falta tocar los cincuenta y tres sitios uno a uno: TODOS son la
//  rama FALSA de un ternario, y un ternario solo evalua la rama que toma. Asi
//  que la cuenta se lleva desde el propio literal - se sustituye un token y no
//  se toca una sola condicion - y no hay forma de anadir una comprobacion
//  nueva y olvidarse de contarla.
//
//  Medido ANTES de ponerlo, con JUCE 8.0.4 y el arbol limpio: 69 OK y 0 FALLA.
//  O sea que el codigo de salida no puede volver rojo lo que estaba verde, y
//  eso queda demostrado y no supuesto.
//  ============================================================================
//  ============================================================================
//  LO QUE EL PROCESO HA LLEGADO A OCUPAR, en KB.
//
//  `VmHWM` es el MAXIMO y no el de ahora: quien pregunta despues de que una
//  funcion suelte sus vectores ya no ve lo que pidio. Vivia dentro del bloque
//  de QUITAR RUIDO y sale aqui en cuanto tuvo un segundo cliente -la huella de
//  los dieciseis insertos-, por lo mismo que `normaliza` salio de dentro de
//  `render`: dos copias de la misma lectura se separan.
//  ============================================================================
static long zatiRssKb()
{
    std::ifstream f ("/proc/self/status");
    std::string line;
    while (std::getline (f, line))
        if (line.rfind ("VmHWM:", 0) == 0)
            return std::atol (line.c_str() + 6);
    return -1;
}

static int zatiFallos = 0;

static const char* zatiFalla (const char* texto = "FALLA")
{
    ++zatiFallos;
    return texto;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const double sr = 48000.0;
    const int    bs = 128;                       // Oboe's low-latency burst on a modern phone
    const double budgetMs = 1000.0 * bs / sr;    // 2.67 ms

    //  ------------------------------------------------------------------
    //  LO QUE CUESTAN LOS DIECISEIS INSERTOS, medido y no deducido.
    //
    //  `AudioEngine.h` llevaba escrito «1.3 MB por canal, o sea 21 MB por
    //  dieciseis» como el precio de la alternativa que HOY es lo que hay, y
    //  esa cifra era de cuando el DLY entraba en la cuenta: es un ENVIO y no
    //  se replica, y ademas su linea de retardo es de UN segundo y no de dos.
    //  Un numero medido que sobrevivio al cambio que lo invalido, o sea el
    //  patron de esta casa.
    //
    //  Se mide y no se deriva: lo estatico es `sizeof`, y lo que `frzVent` y
    //  `pitLine` reservan sale del maximo que el proceso ha llegado a ocupar,
    //  preguntado ANTES de construir nada -si no, el pico de cualquier fila de
    //  mas abajo se lo come-.
    //
    //  SE IMPRIME Y NO SE JUZGA: no hay poblacion contra la que poner un
    //  liston, y un tope inventado suspenderia al motor por existir. Es la
    //  leccion de TARJETA y la del porcentaje de iconos. Lo que vale es que la
    //  cifra este a la vista para que la proxima tanda decida sobre lo que
    //  cuesta y no sobre lo que costaba.
    const long rssAntes = zatiRssKb();

    AudioEngine e;
    const long rssMotor = zatiRssKb();
    e.prepareToPlay (sr, bs);
    e.setPolyphony (32, 4);
    enCanalCero (e);

    {
        const long rssTras = zatiRssKb();
        const double canalKb = (double) AudioEngine::bytesPorCanal() / 1024.0;
        const double motorKb = (double) sizeof (AudioEngine) / 1024.0;
        const double vivoKb  = (double) (rssTras - rssAntes);
        std::printf ("%-34s %.1f KB por canal x %d = %.0f KB estaticos   el motor entero"
                     " %.0f KB   construir %.0f KB   preparar %.0f KB   total %.0f KB\n",
                     "los dieciseis insertos", canalKb, AudioEngine::kNumCanales,
                     canalKb * AudioEngine::kNumCanales, motorKb,
                     (double) (rssMotor - rssAntes), (double) (rssTras - rssMotor), vivoKb);
    }

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
        enCanalCero (d);
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
                     rampBlocks * 1000.0 * bs / sr, ok ? "OK" : zatiFalla ("<-- FAILED"));
    }

    // 5c. RESAMPLE. The master printed back onto a pad: what lands there has
    //     to be what came out, at the level it came out at, and it must not
    //     also contain the microphone.
    {
        AudioEngine r;
        r.prepareToPlay (sr, bs);
        r.setPolyphony (32, 4);
        enCanalCero (r);
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
                     ok ? "OK" : zatiFalla ("<-- FAILED"));
    }

    // 6. EVERY BUFFER SIZE — the load has to fit the budget at the SMALLEST
    //    one, because that is the one that makes the app feel like hardware.
    for (int b : { 64, 96, 128, 192, 256, 480, 512 })
    {
        AudioEngine e2;
        e2.prepareToPlay (sr, b);
        e2.setPolyphony (32, 4);
        enCanalCero (e2);
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
 enCanalCero (e);
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
                     (countOk && mapOk && velOk && fromSeq > 0 && whenOff == 0) ? "OK" : zatiFalla());
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
 enCanalCero (e);
        //  33.34375 ms x 48 kHz = 1600.5 muestras: media muestra EXACTA de parte
        //  fraccionaria, que es el peor caso de la interpolacion. El primer
        //  intento uso 100 ms, que son 4800 muestras clavadas - fraccion cero -
        //  y ahi hasta la interpolacion lineal es exacta: la prueba daba -0.0 dB
        //  con las dos y no estaba midiendo nada. Primero se duda de la prueba.
        e.setDlyMix (1.0f); e.setDlyTime (33.34375f); e.setDlyFb (0.9f);
        e.setPadGain (0, 1.0f);
        e.setCanalSend (0, 3, 1.0f);
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
                     (! nan && lostDb > -3.0) ? "OK" : zatiFalla());
    }

    //  LA REVERB, MEDIDA. Un cambio de algoritmo de cola no se juzga de oido
    //  en una sesion: se le mete un impulso y se mira cuanto tarda en caer 60
    //  dB, si crece en vez de caer, y si produce NaN. Una FDN mal escalada se
    //  descubre aqui y no en un directo.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
 enCanalCero (e);
        //  Solo el bus de reverb: mezcla al maximo y un pad que le manda todo.
        e.setRevMix (1.0f); e.setRevSize (0.6f); e.setRevDamp (0.4f);
        //  La ganancia del pad, que por defecto es cero: la primera version de
        //  esta sonda no la ponia y midio una reverb muda durante tres
        //  intentos. Primero se duda de la prueba.
        e.setPadGain (0, 1.0f);
        e.setCanalSend (0, 5, 1.0f);
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
                     (! nan && ! grew && t60 > 0.15 && t60 < 12.0) ? "OK" : zatiFalla());
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

        //  Y LA TERCERA, QUE ES LA QUE MIDE EL RUIDO MUSICAL.
        //
        //  Las dos de arriba dicen CUANTO queda y no COMO queda, y por ahi se
        //  cuela lo unico que de verdad delata una limpieza: las campanitas.
        //  Son bandas sueltas que se abren un fotograma y se cierran al
        //  siguiente, asi que el residuo deja de ser un siseo parejo y pasa a
        //  ser un burbujeo -mismo nivel medio, misma cifra de `cut`, y se oye
        //  fatal-. Lo que las separa es la DISPERSION del residuo en el
        //  tiempo: un siseo limpio tiene el mismo RMS ventana a ventana y un
        //  burbujeo salta.
        //
        //  Se mide en ventanas de 512 sobre la mitad que solo lleva ruido, y
        //  se publica la desviacion tipica del RMS en dB. Y CON CONTROL: el
        //  mismo siseo SIN TOCAR se mide tambien, porque «1.1 dB» no dice nada
        //  hasta que se sabe cuanto vale no hacer nada.
        auto dispersión = [&] (const juce::AudioBuffer<float>& buf)
        {
            std::vector<double> db;
            for (int at = 2000; at + 512 <= 22000; at += 512)
            {
                double acc = 0.0;
                for (int i = 0; i < 512; ++i)
                {
                    const double v = buf.getSample (0, at + i); acc += v * v;
                }
                db.push_back (20.0 * std::log10 (juce::jmax (1.0e-9, std::sqrt (acc / 512.0))));
            }
            double m = 0.0; for (double v : db) m += v; m /= (double) juce::jmax ((size_t) 1, db.size());
            double s = 0.0; for (double v : db) s += (v - m) * (v - m);
            return std::sqrt (s / (double) juce::jmax ((size_t) 1, db.size()));
        };
        const double burbujaAntes = dispersión (b);      // el siseo sin tocar
        const auto t0 = std::chrono::steady_clock::now();
        Denoise::process (b, 0.6f);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();

        const double floorAfter = rms (b, 2000, 20000);
        const double toneAfter  = rms (b, len / 2 + 4000, 20000);

        const double burbuja = dispersión (b);
        const double cut  = 20.0 * std::log10 (juce::jmax (1.0e-9, floorAfter) / juce::jmax (1.0e-9, floorBefore));
        const double keep = 20.0 * std::log10 (juce::jmax (1.0e-9, toneAfter)  / juce::jmax (1.0e-9, toneBefore));


        bool nan = false;
        for (int i = 0; i < len; ++i) if (! std::isfinite (b.getSample (0, i))) { nan = true; break; }

        //  Pide 12 dB de suelo fuera y menos de 1.5 dB perdidos en el tono. La
        //  segunda condicion es la que importa: una limpieza que baja 40 dB y
        //  se lleva el sonido por delante no es una limpieza.
        //  El liston sale de la POBLACION y no de un numero redondo. Medidos
        //  los tres: el siseo sin tocar deja 0.2 dB -eso es lo que cuesta no
        //  hacer nada-, la resta cruda que habia antes dejaba 2.8, y el
        //  estimador dirigido deja 1.1. Dos separa a los dos algoritmos y
        //  deja sitio de sobra por arriba; tres no separaba nada, porque el
        //  viejo pasaba por dos decimas.
        std::printf ("%-34s suelo %+.1f dB   tono %+.2f dB   burbuja %.1f dB (sin tocar %.1f)   NaN %s   %.0f ms/s   %s\n",
                     "quitar ruido (siseo + tono)", cut, keep, burbuja, burbujaAntes,
                     nan ? "SI" : "no", ms,
                     (! nan && cut < -12.0 && keep > -1.5 && burbuja < 2.0) ? "OK" : zatiFalla());
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
                     (d <= 0.5) ? "OK" : zatiFalla());
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
        auto rssKb = zatiRssKb;

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
                     (! nan && addedMb < 32.0) ? "OK" : zatiFalla());
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
            enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setSafetyLimiter (limiter);

            //  Y CON LOS DOS BUSES REALIMENTADOS ABIERTOS, que es media prueba
            //  que se habia caido sola. El comentario de arriba dice que un NaN
            //  "se propaga por el bus, por el saturador y por el master", y
            //  desde que los envios NACEN A CERO -un cambio de producto
            //  correcto, ver Tests/nuevo.py- este caso no enrutaba a ninguna
            //  parte: media el camino seco y el master. El delay y la reverb
            //  son los dos sitios donde un NaN se queda a vivir, porque tienen
            //  memoria y solo se limpian al cambiar de ruta.
            e.setCanalSend (0, 3, 1.0f);   // DLY
            e.setCanalSend (0, 5, 1.0f);   // REV
            e.setDlyMix (1.0f); e.setDlyTime (50.0f); e.setDlyFb (0.9f);
            e.setRevMix (1.0f); e.setRevSize (0.7f);

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
                         nan ? "SI" : "no", stuck ? "SI" : "no", ok ? "OK" : zatiFalla());
        }
        std::printf ("%-34s %s\n", "muestras hostiles",
                     allOk ? "ninguna cuelga ni envenena la salida, con y sin limitador"
                           : "HAY FALLOS");
    }

    //  Y QUE EL BUS SE RECUPERE, que es la otra mitad y la que no medía nadie.
    //
    //  Que la salida no lleve NaN ya lo dice la prueba de arriba: lo tapa el
    //  guardia del master. Lo que no tapa es que el estado REALIMENTADO se
    //  quede envenenado: la linea del delay y los cuatro amortiguadores de la
    //  FDN tienen memoria y solo se limpian en prepareToPlay, o sea al cambiar
    //  de ruta. Peor: Fdn::ringing preguntaba `abs(v) > 1e-6`, y con NaN eso es
    //  FALSO, asi que el bus se declaraba muerto, dejaba de renderizarse y ya
    //  no habia forma de que se limpiara solo. Sintoma en el telefono: cargas
    //  un fichero raro y el delay y la reverb se quedan mudos hasta reiniciar.
    //
    //  Se mide cargando DESPUES un seno limpio y escuchando la COLA: el camino
    //  seco sonaria igual con el bus muerto, asi que lo que separa las dos
    //  cosas es lo que suena cuando la muestra ya se ha acabado.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setPolyphony (8, 2);
        enCanalCero (e);
        e.setPadGain (0, 1.0f);
        e.setCanalSend (0, 3, 1.0f);
        e.setCanalSend (0, 5, 1.0f);
        e.setDlyMix (1.0f); e.setDlyTime (50.0f); e.setDlyFb (0.9f);
        e.setRevMix (1.0f); e.setRevSize (0.7f);

        auto carga = [&e] (bool veneno)
        {
            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize (2, 4410);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4410; ++i)
                    sb->buffer.setSample (ch, i, veneno ? std::numeric_limits<float>::quiet_NaN()
                                                        : 0.5f * std::sin (0.05f * (float) i));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, sb);
        };

        juce::AudioBuffer<float> b (2, 512);
        carga (true);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.postNoteOn (0, 1.0f);
        for (int blk = 0; blk < 40; ++blk) { b.clear(); e.renderNextBlock (b, 0, 512); }

        //  Ahora el limpio, y se escucha la cola pasada la muestra: 4410
        //  muestras son nueve bloques, asi que del 15 en adelante lo unico que
        //  puede sonar es el delay y la reverb.
        carga (false);
        b.clear(); e.renderNextBlock (b, 0, 512);
        e.postNoteOn (0, 1.0f);
        float cola = 0.0f;
        for (int blk = 0; blk < 60; ++blk)
        {
            b.clear(); e.renderNextBlock (b, 0, 512);
            if (blk >= 15)
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        cola = juce::jmax (cola, std::abs (b.getSample (ch, i)));
        }
        const bool ok = std::isfinite (cola) && cola > 0.001f;
        std::printf ("%-34s cola despues del NaN %.5f   %s\n",
                     "el bus se recupera", cola, ok ? "OK" : zatiFalla());
    }

    //  LA COLA DE MIDI TIENE SU PROPIO PRESUPUESTO.
    //
    //  Las dos colas vaciaban en el mismo array de 256 y con la misma cuenta,
    //  asi que una rafaga de la interfaz se lo comia entero y los comandos de
    //  MIDI se CONSUMIAN y se tiraban en silencio: la cola avanza su lectura
    //  aunque el destino este lleno. Dos colas por contrato y un solo cubo
    //  entre las dos es media cola.
    //
    //  Se mide por lo que SUENA -el pad de MIDI tiene que arrancar su voz en
    //  ese mismo bloque- y no por el contador: contar diria que si aunque el
    //  comando se hubiera perdido, porque lo que se cuenta es lo tirado.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setPolyphony (16, 2);
        enCanalCero (e);

        for (int p = 0; p < 5; ++p)
        {
            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize (2, 48000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                    sb->buffer.setSample (ch, i, 0.3f * std::sin (0.01f * (float) i));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (p, sb);
            e.setPadGain (p, 1.0f);
        }
        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);

        //  Doscientos cincuenta y cinco de dedo -el maximo que una AbstractFifo
        //  de 256 entrega de una vez, que deja un hueco libre por diseno- y
        //  CUATRO de MIDI detras. Con el cubo compartido cabia exactamente uno:
        //  el primero entraba y los otros tres se consumian y se tiraban sin
        //  pasar por droppedCommands. Por eso son cuatro y no uno - con uno
        //  solo, el fallo pasa la prueba.
        for (int i = 0; i < 255; ++i) e.postNoteOn (0, 0.5f);
        for (int p = 1; p <= 4; ++p) e.postNoteOnFromMidi (p, 1.0f);
        b.clear(); e.renderNextBlock (b, 0, 512);

        const std::uint64_t visto = e.fetchTriggered();
        int cuantos = 0;
        for (int p = 1; p <= 4; ++p) if (visto & (1ull << (unsigned) p)) ++cuantos;
        std::printf ("%-34s %d de 4 notas de MIDI suenan   %s\n", "la cola de MIDI no se ahoga",
                     cuantos, cuantos == 4 ? "OK" : zatiFalla());
    }

    //  Y NINGUN PUNTERO SE TIRA POR EL CAMINO.
    //
    //  RetiredQueue::push se rendia en silencio con la cola llena, y para
    //  entonces el hilo de audio ya ha soltado su puntero: un SampleBuffer cuyo
    //  contador no baja NUNCA, o sea megabytes que no vuelven. Son 127 huecos
    //  utiles contra un temporizador de 33-60 ms, y recargar la fabrica escribe
    //  64 de golpe. Aqui se cargan los 64 pads dos veces sin recoger entre
    //  medias, que es exactamente ese caso.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        for (int vuelta = 0; vuelta < 2; ++vuelta)
        {
            for (int p = 0; p < AudioEngine::kNumPads; ++p)
            {
                SampleBuffer::Ptr sb = new SampleBuffer();
                sb->buffer.setSize (1, 128);
                sb->sourceSampleRate = 48000.0;
                e.publishSample (p, sb);
            }
            b.clear(); e.renderNextBlock (b, 0, 512);
        }
        const int perdidos = e.takeRetiredLost();
        std::printf ("%-34s %d punteros tirados   %s\n", "la cola de retirados no pierde",
                     perdidos, perdidos == 0 ? "OK" : zatiFalla());
        e.collectRetiredSamples();
    }

    //  EL BLOQUEO DE CORTE NO PUEDE MOVER EL MANDO DEL PAD.
    //
    //  Escribia en padCutoff, que es lo que lee el mando CORTE y lo que guarda
    //  el fichero de proyecto: pon un pad en 8 kHz, toca dos compases con un
    //  paso bloqueado a 200 Hz, para y guarda, y el proyecto guarda 200. O sea
    //  que TOCAR una secuencia cambiaba el proyecto. Es el mismo dano por el
    //  que los otros cuatro bloqueos se sacaron del pad y viajan con el
    //  disparo, y por el que oir una tecla dejo de afinar el pad.
    //
    //  DOS numeros, que uno solo se puede enganar de las dos formas: el mando
    //  tiene que seguir donde estaba Y el bloqueo tiene que sonar. Solo lo
    //  primero lo cumple un bloqueo desconectado.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 64);
        e.setPolyphony (8, 2);
        enCanalCero (e);

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->buffer.setSize (2, 48000);
        juce::Random r (7);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 48000; ++i)
                sb->buffer.setSample (ch, i, 0.5f * (r.nextFloat() * 2.0f - 1.0f));
        sb->sourceSampleRate = 48000.0;
        e.publishSample (0, sb);
        e.setPadGain (0, 1.0f);
        e.setPadCutoff (0, 8000.0f);
        e.setBpm (120.0);
        e.setPatternLength (0, 16);
        e.setEditPattern (0);
        e.setStep (0, 0, 0, true);
        e.setStepLock (0, 0, 0, 34);      // ~200 Hz, ver lockToHz

        juce::AudioBuffer<float> b (2, 64);
        b.clear(); e.renderNextBlock (b, 0, 64);
        e.setPlaying (true);

        //  Dos compases a 120 BPM en semicorcheas son 4 s: 3000 bloques de 64.
        double altaBloq = 0.0;
        for (int blk = 0; blk < 3000; ++blk)
        {
            b.clear(); e.renderNextBlock (b, 0, 64);
            if (blk > 10 && blk < 200)
                for (int i = 1; i < 64; ++i)
                {
                    const double d = (double) b.getSample (0, i) - (double) b.getSample (0, i - 1);
                    altaBloq += d * d;
                }
        }
        e.setPlaying (false);
        b.clear(); e.renderNextBlock (b, 0, 64);

        const float mandoDespues = e.getPadCutoff (0);
        const bool  intacto = std::abs (mandoDespues - 8000.0f) < 1.0f;
        std::printf ("%-34s el mando quedo en %.0f Hz   %s\n", "el bloqueo no mueve el pad",
                     mandoDespues, intacto ? "OK" : zatiFalla());
        std::printf ("%-34s energia alta con bloqueo %.4f   %s\n", "y aun asi filtra",
                     altaBloq, altaBloq > 0.0 ? "OK" : zatiFalla());
    }

    //  EL BOMBEO, que multiplica el MASTER entero -colas de delay y reverb
    //  incluidas- y no lo medía nada: si se rompiera, la app se publicaria
    //  permanentemente atenuada y ninguna prueba diria nada.
    //
    //  Se compara contra la MISMA corrida sin pad de bombeo armado, que es lo
    //  unico que separa "el bombeo hace algo" de "el pad ya sonaba asi". Y la
    //  corrida de control no pone el pad a -1: no lo toca, que una maquina
    //  recien encendida y una que alguien ha puesto a cero no son lo mismo.
    {
        auto pico = [] (bool bombeando)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 64);
            e.setPolyphony (8, 2);
            enCanalCero (e);
            for (int p = 0; p < 2; ++p)
            {
                SampleBuffer::Ptr sb = new SampleBuffer();
                sb->buffer.setSize (2, 48000);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 48000; ++i)
                        sb->buffer.setSample (ch, i, 0.4f * std::sin (0.02f * (float) i));
                sb->sourceSampleRate = 48000.0;
                e.publishSample (p, sb);
                e.setPadGain (p, 1.0f);
            }
            if (bombeando) { e.setDuckPad (1); e.setDuckAmount (0.9f); e.setDuckRelease (300.0f); }

            juce::AudioBuffer<float> b (2, 64);
            b.clear(); e.renderNextBlock (b, 0, 64);
            e.postNoteOn (0, 1.0f);          // lo que se oye
            for (int i = 0; i < 20; ++i) { b.clear(); e.renderNextBlock (b, 0, 64); }
            e.postNoteOn (1, 1.0f);          // el bombo que aprieta
            float p = 0.0f;
            for (int i = 0; i < 20; ++i)
            {
                b.clear(); e.renderNextBlock (b, 0, 64);
                for (int ch = 0; ch < 2; ++ch)
                    for (int k = 0; k < 64; ++k)
                        p = juce::jmax (p, std::abs (b.getSample (ch, k)));
            }
            return p;
        };
        const float sin_ = pico (false), con = pico (true);
        const bool ok = con < sin_ * 0.7f;
        std::printf ("%-34s pico %.4f -> %.4f   %s\n", "el bombeo aprieta",
                     sin_, con, ok ? "OK" : zatiFalla());
    }

    //  LOS GRUPOS DE CHOKE, que tampoco medía nadie fuera del fuzz -y alli con
    //  valores al azar y sin afirmar nada del resultado-. Un charles abierto
    //  que no se calla al cerrarlo es la mitad de una bateria.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 64);
        e.setPolyphony (8, 2);
        enCanalCero (e);
        for (int p = 0; p < 2; ++p)
        {
            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize (2, 48000);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 48000; ++i)
                    sb->buffer.setSample (ch, i, 0.4f * std::sin (0.02f * (float) i));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (p, sb);
            e.setPadGain (p, 1.0f);
            e.setPadChoke (p, 1);            // los dos en el mismo grupo
        }
        juce::AudioBuffer<float> b (2, 64);
        b.clear(); e.renderNextBlock (b, 0, 64);
        e.postNoteOn (0, 1.0f);
        for (int i = 0; i < 10; ++i) { b.clear(); e.renderNextBlock (b, 0, 64); }
        const int antes = e.getActiveVoiceCount();
        e.postNoteOn (1, 1.0f);
        for (int i = 0; i < 60; ++i) { b.clear(); e.renderNextBlock (b, 0, 64); }
        const int despues = e.getActiveVoiceCount();
        //  Una voz, no dos: la del grupo se calla al llegar la otra. Y se
        //  cuenta al FINAL y no en el bloque siguiente, que el choke abre la
        //  caida en vez de cortar en seco - la misma leccion que costo una
        //  medida en "tras soltar 3".
        const bool ok = antes == 1 && despues == 1;
        std::printf ("%-34s %d voz -> %d voz   %s\n", "el choke calla al hermano",
                     antes, despues, ok ? "OK" : zatiFalla());
    }

    //  CUANTIZAR EN DIRECTO: un toque entre pasos suena EN el paso siguiente y
    //  no cuando lo tocaste. Sin medirlo, "esta puesto" y "no hace nada" son la
    //  misma corrida en verde.
    {
        auto bloqueDelGolpe = [] (bool cuant)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 64);
            e.setPolyphony (8, 2);
            enCanalCero (e);
            SampleBuffer::Ptr sb = new SampleBuffer();
            sb->buffer.setSize (2, 4800);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 4800; ++i)
                    sb->buffer.setSample (ch, i, 0.5f * std::sin (0.05f * (float) i));
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, sb);
            e.setPadGain (0, 1.0f);
            e.setBpm (120.0);
            e.setPatternLength (0, 16);
            e.setEditPattern (0);
            e.setLiveQuantise (cuant);

            juce::AudioBuffer<float> b (2, 64);
            b.clear(); e.renderNextBlock (b, 0, 64);
            e.setPlaying (true);
            e.fetchTriggered();
            //  Un paso a 120 BPM en semicorcheas son 6000 muestras, o sea 93
            //  bloques de 64. Se toca a mitad de camino del primero.
            for (int i = 0; i < 46; ++i) { b.clear(); e.renderNextBlock (b, 0, 64); }
            e.postNoteOn (0, 1.0f);
            int bloque = -1;
            for (int i = 0; i < 200 && bloque < 0; ++i)
            {
                b.clear(); e.renderNextBlock (b, 0, 64);
                if (e.fetchTriggered() & 1ull) bloque = i;
            }
            e.setPlaying (false);
            return bloque;
        };
        const int libre = bloqueDelGolpe (false), atado = bloqueDelGolpe (true);
        //  Sin cuantizar suena en el bloque siguiente; cuantizado espera al
        //  borde del paso, que esta a unos 47 bloques de donde se toco.
        const bool ok = libre >= 0 && atado > libre + 20;
        std::printf ("%-34s suelto en el bloque %d, cuantizado en el %d   %s\n",
                     "cuantizar en directo espera", libre, atado, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     nan ? "SI" : "no", ok ? "OK" : zatiFalla());
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
                     culpables == 0 ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     (! nan && pct < 0.01 && pk < 0.99) ? "OK" : zatiFalla());
    }

    //  LA VUELTA DEL BUCLE NO SE OYE: el nivel no late a su cadencia.
    //
    //  Del telefono: «que no se escuche nada de repeticiones forzadas». Esta es
    //  la medida de esa queja.
    //
    //  Y ES EN EL TIEMPO Y NO EN LA FRECUENCIA, y eso se aprendio midiendo. La
    //  primera version buscaba bandas laterales en f0 +- k/L con un Goertzel. Dio
    //  **-11.0 dB** con ORGANOS -que lleva un leslie a 5.6 Hz y mete bandas
    //  laterales A PROPOSITO- y **-3.7 dB** con BAJOS, que no lleva LFO ninguno.
    //  Barrido el entorno del fundamental, lo que habia no eran picos discretos
    //  sino una falda simetrica que cae veinte decibelios en seis hercios: la
    //  resolucion que hace falta para separar bandas a un hercio de una nota real
    //  de ocho segundos no la da esta sonda. *Cuando una prueba falla, primero se
    //  duda de la prueba* — y aqui la prueba estaba midiendo su propia ventana.
    //
    //  Lo que la persona oye no es una banda lateral: es que **el nivel late**.
    //  Asi que se mide el nivel: RMS en ventanas de 50 ms a lo largo de ocho
    //  vueltas, y se publica cuanto sube y baja. Un bucle cuya costura salta -de
    //  fase, de envolvente o de LFO- lo enseña ahi y no hace falta nada mas.
    //
    //  EL LISTON, 1.5 dB de subida y bajada, sale de la regla de al lado: «la
    //  nota sostenida no re-ataca» acepta un bache de hasta 12 dB porque mide el
    //  MINIMO absoluto de un ataque incluido. Aqui se mide el regimen, donde no
    //  hay ataque que valga, y 1.5 dB es el escalon mas pequeño de nivel que esta
    //  casa considera audible — el mismo 1.5 que `instr.py` usa para separar dos
    //  presets (`PAR_PRE`).
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
        enCanalCero (e);
        e.setSafetyLimiter (false);
        e.setPadGain (0, 1.0f);
        //  BAJOS SAW BS: sostiene, es de los mas tonales que hay y **no lleva
        //  LFO ninguno**, asi que lo unico que puede mover el nivel es la vuelta.
        e.publishSample (0, Sintes::sintetiza (0, 0));
        //  +7 SEMITONOS -delta 1.4983- y no la raiz: en la raiz delta vale 1.0
        //  clavada, la lectura no tiene parte fraccionaria y el defecto que esto
        //  busca no puede aparecer. Es la misma trampa del +12 de la sonda de
        //  aliasing, evitada por la misma razon.
        e.setPadPitch (0, 7.0f);

        juce::AudioBuffer<float> b (2, 512);
        b.clear(); e.renderNextBlock (b, 0, 512);
        //  SOSTENIDA, y esto costo una corrida: con `postNoteOn` la audicion
        //  dura 1.2 s, la nota se acaba a mitad de la medida y el nivel se movio
        //  **151.86 dB** -que es el silencio, no un bucle-. Es exactamente la
        //  misma nota al pie que ya lleva escrita «la nota sostenida no
        //  re-ataca», y se volvio a pisar por no leerla.
        e.postNoteOnAt (0, 0, 1.0f, AudioEngine::kSostenida);

        constexpr int kSalta = 94;              // ~1 s: fuera el ataque
        constexpr int kMide  = 8 * 94;          // ocho vueltas de un cuerpo de 1 s
        for (int blk = 0; blk < kSalta; ++blk) { b.clear(); e.renderNextBlock (b, 0, 512); }

        juce::AudioBuffer<float> cap (1, 512 * kMide);
        for (int blk = 0; blk < kMide; ++blk)
        {
            b.clear();
            e.renderNextBlock (b, 0, 512);
            cap.copyFrom (0, blk * 512, b, 0, 0, 512);
        }

        const float* d = cap.getReadPointer (0);
        const int n = 512 * kMide;
        const int ven = (int) (48000.0 * 0.050);          // 50 ms
        double lo = 1.0e30, hi = 0.0;
        for (int i = 0; i + ven <= n; i += ven)
        {
            double sum = 0.0;
            for (int k = 0; k < ven; ++k) sum += (double) d[i + k] * d[i + k];
            const double r = std::sqrt (sum / ven);
            if (r > hi) hi = r;
            if (r < lo) lo = r;
        }

        const double db = 20.0 * std::log10 (juce::jmax (1.0e-9, hi) / juce::jmax (1.0e-9, lo));
        std::printf ("%-34s +7 st, 8 vueltas: el nivel se mueve %.2f dB   %s\n",
                     "la vuelta del bucle no se oye", db,
                     db <= 1.5 ? "OK" : zatiFalla());

        //  Y LA OTRA MITAD: QUE LA VUELTA NO SALTE DE FASE.
        //
        //  El nivel caza los saltos de envolvente y de LFO, y NO caza un salto
        //  de fase: una modulacion de fase no mueve el nivel. Se vio rompiendo a
        //  proposito la vuelta para que tirara la parte fraccionaria -el defecto
        //  que esta tanda arregla- y la cifra de arriba se quedo en **0.14 dB,
        //  identica**. Una prueba en la que el defecto que motivo el arreglo no
        //  mueve el numero no cubre ese arreglo.
        //
        //  Lo que si lo caza, y es exacto: **el mismo cuerpo repetido a mano**.
        //  El fundido cruzado hace que la muestra del final del bucle sea la del
        //  principio, asi que pegar ocho copias del cuerpo una detras de otra da
        //  EXACTAMENTE la misma señal que dar ocho vueltas — si la vuelta es
        //  continua. Se toca la misma nota sobre las dos y se restan.
        //
        //  Y ESTA PRUEBA HUBO QUE CORREGIRLA DOS VECES ANTES DE CREERLA, que es
        //  la regla de la casa y aqui volvio a pagar: el primer -18.5 dB no era
        //  del bucle sino de que la referencia se construia con la zona de otra
        //  octava (ver abajo), y el segundo -27.6 dB tampoco era del bucle sino
        //  de dos muestras de guarda que se escribian antes de la ganancia por
        //  octava (ver `Sintes::kGuardas`). Lo que quedo al final -96.2 dB- son
        //  **una sola muestra** de las 240 640, la primera del ataque, donde la
        //  referencia lee su relleno de ceros y el original lee la cola de la
        //  zona de al lado. Eso es de la referencia, no de la vuelta.
        {
            auto orig = Sintes::sintetiza (0, 0);
            //  LA ZONA QUE EL MOTOR VA A ELEGIR, y no la que parece.
            //
            //  Se escribio «la de la raiz 0, capa fuerte: es la que +7 elige» y
            //  es FALSO: `AudioEngine` puntua `|round(semis) - raiz|`, y a +7 la
            //  mas cercana de -24/-12/0/+12/+24 es **+12** (distancia 5, contra
            //  7 de la raiz 0). La referencia se construia con el cuerpo de otra
            //  octava, asi que las dos señales diferian **desde la muestra 1, en
            //  pleno ataque**, mucho antes de que hubiera ninguna vuelta: -21.1
            //  dB constantes por tramo. La prueba medía su propia eleccion.
            //
            //  Se replica la regla del motor en vez de escribir «12» a mano:
            //  *una regla duplicada que no se contrasta son dos reglas*, y esta
            //  se contrasta sola en cuanto alguien mueva `kRaiz`.
            //  Y LA CAPA TAMBIEN SALE DE LA MUESTRA, no de un numero escrito.
            //  Con `capa != 1` la prueba cogia la capa DEL MEDIO desde que hay
            //  tres, mientras el motor -que reparte el recorrido entre las que
            //  la muestra traiga- tocaba la de arriba: -7.4 dB de diferencia que
            //  no eran del bucle sino de comparar dos capas distintas.
            constexpr float kSemis = 7.0f;
            constexpr float kVel   = 1.0f;
            int capasHay = 1;
            for (int i = 0; i < orig->nZonas; ++i)
                capasHay = juce::jmax (capasHay, orig->zonas[(size_t) i].capa + 1);
            const int capaQuiere = juce::jlimit (0, capasHay - 1, (int) (kVel * (float) capasHay));

            int z = -1, coste = 1 << 30;
            for (int i = 0; i < orig->nZonas; ++i)
            {
                const auto& q = orig->zonas[(size_t) i];
                const int c = std::abs ((int) std::lround (kSemis) - q.raiz)
                            + (q.capa != capaQuiere ? 1000 : 0);
                if (c < coste) { coste = c; z = i; }
            }

            if (z >= 0 && orig->zonas[(size_t) z].bucleFin > orig->zonas[(size_t) z].bucleIni)
            {
                const auto& Z = orig->zonas[(size_t) z];
                const int cuerpo = Z.bucleFin - Z.bucleIni;
                const int copias = 8;

                //  Un buffer de UNA zona: el ataque y luego el cuerpo pegado
                //  ocho veces, sin bucle. Mismo canal, misma tasa.
                //  Y LA ZONA EMPIEZA EN 1 Y ACABA DOS ANTES DEL FINAL, que no es
                //  un adorno: `Voice::start` acota `winStart` a `[1, ...]` y
                //  `winEnd` a `srcLen - 2` porque `hermite4` mira una muestra
                //  por delante y dos por detras. Con la zona pegada al cero del
                //  buffer, `winStart` subia a 1 **y la referencia entera sonaba
                //  una muestra antes que el original**: 1.4983 de paso a +7
                //  semitonos, o sea otra fase en todas y cada una de las
                //  muestras. Se midio -18.5 dB y no era el bucle, era esto.
                auto* rec = new SampleBuffer();
                rec->sourceSampleRate = orig->sourceSampleRate;
                const int ataque = Z.bucleIni - Z.ini;
                const int util   = ataque + cuerpo * copias;
                const int largo  = 1 + util + 2;
                rec->buffer.setSize (orig->buffer.getNumChannels(), largo);
                rec->buffer.clear();
                for (int ch = 0; ch < orig->buffer.getNumChannels(); ++ch)
                {
                    rec->buffer.copyFrom (ch, 1, *&orig->buffer, ch, Z.ini, ataque);
                    for (int c = 0; c < copias; ++c)
                        rec->buffer.copyFrom (ch, 1 + ataque + c * cuerpo,
                                              *&orig->buffer, ch, Z.bucleIni, cuerpo);
                }
                rec->zonas[0] = { Z.raiz, capaQuiere, 1, 1 + util, 0, 0 };
                rec->nZonas = 1;

                auto toca = [&] (SampleBuffer::Ptr sb, int bloques)
                {
                    AudioEngine m; m.prepareToPlay (48000.0, 512); m.setPolyphony (8, 2);
                    enCanalCero (m);
                    m.setSafetyLimiter (false);
                    m.setPadGain (0, 1.0f);
                    m.publishSample (0, sb);
                    m.setPadPitch (0, kSemis);
                    juce::AudioBuffer<float> t (2, 512);
                    t.clear(); m.renderNextBlock (t, 0, 512);
                    m.postNoteOnAt (0, 0, 1.0f, AudioEngine::kSostenida);
                    juce::AudioBuffer<float> c (1, 512 * bloques);
                    for (int blk = 0; blk < bloques; ++blk)
                    { t.clear(); m.renderNextBlock (t, 0, 512); c.copyFrom (0, blk * 512, t, 0, 0, 512); }
                    return c;
                };

                //  Cinco vueltas, que caben de sobra dentro de las ocho copias.
                const int bl = 5 * 94;
                const auto A = toca (orig, bl), B = toca (SampleBuffer::Ptr (rec), bl);
                double difE = 0.0, refE = 0.0;
                for (int i = 0; i < 512 * bl; ++i)
                {
                    const double d1 = (double) A.getSample (0, i) - (double) B.getSample (0, i);
                    difE += d1 * d1;
                    refE += (double) B.getSample (0, i) * B.getSample (0, i);
                }
                const double dbDif = 10.0 * std::log10 (juce::jmax (1.0e-20, difE)
                                                        / juce::jmax (1.0e-20, refE));
                //  LISTON -60 dB, heredado del `FLOOR = -60.0` de `kits.py`
                //  -«por debajo de esto ya es silencio y no forma»-. La vuelta
                //  tiene que ser indistinguible del cuerpo pegado, no parecida.
                std::printf ("%-34s el bucle contra el cuerpo pegado: %+.1f dB   %s\n",
                             "la vuelta no salta de fase", dbDif,
                             dbDif < -60.0 ? "OK" : zatiFalla());
            }
        }
    }

    //  EL DIEZMADOR, COMO CELULA. Impulso dentro, espectro fuera.
    //
    //  La regla del pliegue mide el SISTEMA; esta mide la PIEZA. Existe porque
    //  el filtro entregado tiene que ser el filtro diseñado: sus tres numeros
    //  -rizo de paso, atenuacion de rechazo y planitud del retardo de grupo- son
    //  la especificacion escrita en `Diezmador.h`, y sin esto serian una
    //  afirmacion en un comentario.
    //
    //  Los tres listones son los tres parametros de diseño y no se negocian:
    //  rizo <= 0.01 dB hasta 19 200, rechazo >= 120 dB desde 24 000, y retardo de
    //  grupo constante dentro de 0.01 muestra -que es lo que garantiza que el
    //  punto de bucle caiga donde `pre` dice-.
    {
        const auto& h = Diezmador::coeficientes (Diezmador::kOs);
        const int taps  = Diezmador::tapsDe (Diezmador::kOs);
        const int mitad = Diezmador::mitadDe (Diezmador::kOs);
        const double fsr = 48000.0 * (double) Diezmador::kOs;

        auto Hf = [&] (double f)
        {
            double re = 0.0, im = 0.0;
            for (int i = 0; i < taps; ++i)
            {
                const double w = -juce::MathConstants<double>::twoPi * f * (double) (i - mitad) / fsr;
                re += h[(size_t) i] * std::cos (w);
                im += h[(size_t) i] * std::sin (w);
            }
            return std::hypot (re, im);
        };

        double rizo = 0.0;
        for (double f = 0.0; f <= Diezmador::kBandaHz; f += 200.0)
            rizo = juce::jmax (rizo, std::abs (20.0 * std::log10 (juce::jmax (1.0e-12, Hf (f)))));

        double rech = 0.0;
        for (double f = Diezmador::kParoHz; f <= fsr * 0.5; f += 200.0)
            rech = juce::jmax (rech, Hf (f));
        const double rechDb = 20.0 * std::log10 (juce::jmax (1.0e-12, rech));

        //  LA PLANITUD DEL RETARDO SALE DE LA SIMETRIA y se mide asi: un filtro
        //  simetrico tiene retardo de grupo exactamente `mitad` muestras, y
        //  cualquier asimetria lo mueve. Se mide la asimetria, que es la causa,
        //  en vez de derivar la fase, que es el efecto.
        double asim = 0.0;
        for (int i = 0; i < mitad; ++i)
            asim = juce::jmax (asim, std::abs (h[(size_t) i] - h[(size_t) (taps - 1 - i)]));
        //  Traducida a muestras de retardo: la asimetria relativa al pico.
        const double desvio = asim / juce::jmax (1.0e-12, std::abs (h[(size_t) mitad])) * (double) mitad;

        const bool ok = rizo <= 0.01 && rechDb <= -120.0 && desvio <= 0.01;
        std::printf ("%-34s rizo %.5f dB   rechazo %.1f dB   retardo +-%.5f muestras   %s\n",
                     "el diezmador entregado", rizo, rechDb, desvio, ok ? "OK" : zatiFalla());
    }

    //  EL PLIEGUE DE LA SINTESIS, contra el mismo generador a 16x.
    //
    //  ESTO NO LO MEDIA NADIE. `instr.py` juzga estructura, pares espectrales,
    //  capas, octavas y el click de la costura; de PUREZA ESPECTRAL la cobertura
    //  era cero. Y hacia falta: `limita()` es `soft()`, un polinomio de quinto
    //  orden por muestra en trece de las dieciseis formas, y la FM de PIANO ELEC
    //  y CAMPANAS no tiene limite de banda ninguno.
    //
    //  SE COMPARA EL PRODUCTO CONSIGO MISMO y no contra «energia que no esta en
    //  k*f0». Esa segunda medida marcaria a CAMPANAS, MAZOS y ARPAS, que son
    //  inarmonicos A PROPOSITO, y obligaria a escribir aqui una lista de
    //  familias armonicas que ya vive en la tabla — *una regla duplicada que no
    //  se contrasta son dos reglas*. La inarmonicidad de diseño sale igual en los
    //  dos rendidos y se cancela sola.
    //
    //  EL LISTON: **1.0 dB en cualquier banda de 1/6 de octava**. No es un numero
    //  redondo elegido a ojo: es el JND de sonoridad de una banda critica, o sea
    //  exactamente el mismo argumento con el que `instr.py` justifica su
    //  `CAPA_DB = 2.0` como «dos veces el JND». Heredado de ahi y dicho.
    //
    //  Y la mediana aparte del peor, porque solo el peor lo cumple un sonido que
    //  esta plegado en todas partes por igual.
    {
        constexpr int kN = 1 << 15;                  // 0.68 s de regimen

        //  LAS DIEZ ZONAS DURAS, y duras por una razon escrita en cada una: son
        //  las que llevan FM sin limite de banda o saturacion fuerte, en la raiz
        //  +24, que es donde el modulador se va mas arriba. Diez y no 2560
        //  porque 2560 FFT no mejoran el liston y si tardan trece minutos.
        struct Zona { int fam, pre; const char* por; };
        const Zona duras[] =
        {
            { 2,  4, "FM razon 28, el modulador en 14.65 kHz" },   // PIANO ELEC GLASS
            { 2,  9, "FM, indice alto" },                          // PIANO ELEC
            { 7,  3, "FM inarmonica" },                            // CAMPANAS
            { 7, 11, "FM inarmonica" },                            // CAMPANAS
            { 0, 12, "sierra saturada, brillo 4.50" },              // BAJOS HARD
            { 0,  9, "FM de bajo" },                               // BAJOS FM BS
            { 9,  5, "pulso + quinta, saturado" },                 // LEADS
            { 13, 2, "pulso muy estrecho por un paso banda" },     // CLAVES
            { 6,  7, "pluck saturado" },                           // PLUCKS
            { 15, 9, "aditivo pulsado de doce armonicos" },        // ARPAS
        };

        juce::dsp::FFT fft (15);
        auto esp = [&] (const std::vector<float>& x)
        {
            std::vector<float> buf ((size_t) kN * 2, 0.0f);
            for (int i = 0; i < kN; ++i)
            {
                const float w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                          * (float) i / (float) (kN - 1)));
                buf[(size_t) i] = x[(size_t) i] * w;
            }
            fft.performFrequencyOnlyForwardTransform (buf.data());
            return buf;
        };

        double peorGlobal = -1.0e9, medianaPeor = -1.0e9, desvioCentro = 0.0;
        int    quienPeor = 0; double hzPeor = 0.0;

        for (int z = 0; z < (int) (sizeof (duras) / sizeof (duras[0])); ++z)
        {
            const auto& Z = duras[z];
            const auto R = Sintes::tabla()[Z.fam].p[Z.pre];
            std::vector<float> prod ((size_t) kN), patr ((size_t) kN);
            Sintes::rindeZona (Z.fam, Z.pre, R, 4, 1, prod.data(), kN, Diezmador::kOs);
            Sintes::rindeZona (Z.fam, Z.pre, R, 4, 1, patr.data(), kN, Diezmador::kOsPatron);
            const auto A = esp (prod), B = esp (patr);

            //  LAS BANDAS SE MIDEN DOS VECES: la primera para saber cual es la
            //  mas fuerte de ESTE sonido, la segunda para juzgar.
            //
            //  Un umbral ABSOLUTO -`eb < 1e-14`, que es lo que habia- deja
            //  entrar bandas que estan ochenta decibelios por debajo del
            //  instrumento: ahi la razon entre producto y patron es enorme y no
            //  significa nada, porque no hay nada que plegar. Se vio al abrir el
            //  estereo: repartir los dos parciales de CAMPANAS movio energia de
            //  un hueco entre parciales y el peor salto de **+0.74 a +1.17 dB**
            //  sin que el pliegue hubiera cambiado. La regla estaba midiendo un
            //  hueco.
            //
            //  Y EL SUELO SE LE PONE AL PRODUCTO Y NO AL PATRON, que es la
            //  segunda correccion y la que de verdad decide.
            //
            //  Poniendoselo al patron, la regla dejaba de ver la rotura 1 -el
            //  producto generado SIN sobremuestrear-: se midio, y el peor exceso
            //  se quedaba en **+0.00 dB con `kOs = 1`**, porque el pliegue
            //  aterriza justo en las bandas donde el patron no tiene nada. Una
            //  regla que se calla cuando el fallo que la motivo esta puesto no
            //  es una regla.
            //
            //  Lo que importa es si LO QUE APARECE se oye, o sea `ea`. Suelo a
            //  -60 dB de la banda mas fuerte del propio sonido: el `FLOOR =
            //  -60.0` de `kits.py` -«por debajo de esto ya es silencio y no
            //  forma»- heredado literal. Con el, la rotura 1 sale a **+107.91 dB
            //  en 3175 Hz** y el codigo bueno a +0.00.
            auto energias = [&] (const std::vector<float>& E, double lo, double hi)
            {
                double e = 0.0;
                for (int k = (int) (lo * kN / 48000.0); k < (int) (hi * kN / 48000.0) && k < kN / 2; ++k)
                    e += (double) E[(size_t) k] * E[(size_t) k];
                return e;
            };
            const double kPaso = std::pow (2.0, 1.0 / 6.0);
            double techo = 0.0;
            for (double lo = 1000.0; lo < 20000.0; lo *= kPaso)
                techo = juce::jmax (techo, energias (B, lo, lo * kPaso));

            std::vector<double> ex;
            for (double lo = 1000.0; lo < 20000.0; lo *= kPaso)
            {
                const double hi = lo * kPaso;
                const double ea = energias (A, lo, hi), eb = energias (B, lo, hi);
                if (ea < techo * 1.0e-6) continue;
                //  Y el denominador con suelo, porque energia que aparece donde
                //  el patron no tenia NADA es el caso mas grave y no una
                //  division por cero que haya que saltarse.
                const double d = 10.0 * std::log10 (ea / juce::jmax (eb, techo * 1.0e-16));
                ex.push_back (d);
                if (d > peorGlobal) { peorGlobal = d; quienPeor = z; hzPeor = lo; }
            }
            if (! ex.empty())
            {
                std::sort (ex.begin(), ex.end());
                medianaPeor = juce::jmax (medianaPeor, ex[ex.size() / 2]);
            }

            //  Y EL CENTROIDE, QUE ES LA SEGUNDA CIFRA Y NO UN ADORNO.
            //
            //  El pliegue solo caza lo que APARECE donde no habia nada. Hay una
            //  clase entera de fallo que no aparece: que un filtro corte donde no
            //  debe. Se vio rompiendo a proposito el `prepara` del `Svf` en
            //  ARPAS -que a 4x lo deja filtrando cuatro veces mas abajo- y el
            //  pliegue no se movio **ni una centesima**: +0.74 dB con el codigo
            //  bueno y +0.74 con el roto. No era que la rotura fuera inocua: era
            //  que la regla no miraba eso.
            //
            //  El centroide del producto contra el del patron lo dice con un
            //  numero. Liston 2 %, que es la mitad del x1.06 que `instr.py` pide
            //  entre dos capas contiguas: si media capa de diferencia se oye,
            //  esto tiene que quedar muy por debajo.
            auto centroide = [&] (const std::vector<float>& E)
            {
                double num = 0.0, den = 0.0;
                for (int k = 1; k < kN / 2; ++k)
                {
                    const double e = (double) E[(size_t) k] * E[(size_t) k];
                    num += e * (double) k; den += e;
                }
                return den > 1.0e-20 ? num / den : 0.0;
            };
            const double cA = centroide (A), cB = centroide (B);
            if (cB > 1.0e-9)
                desvioCentro = juce::jmax (desvioCentro, std::abs (cA / cB - 1.0));
        }

        const bool ok = peorGlobal <= 1.0 && medianaPeor <= 0.3 && desvioCentro <= 0.02;
        std::printf ("%-34s peor %+.2f dB (fam %d pre %d, %.0f Hz)   mediana %+.2f dB   centroide %.2f%%   %s\n",
                     "el pliegue de la sintesis", peorGlobal,
                     duras[quienPeor].fam, duras[quienPeor].pre, hzPeor, medianaPeor,
                     100.0 * desvioCentro, ok ? "OK" : zatiFalla());
    }

    //  ALIASING AL SUBIR EL TONO, que es la otra forma de sonar crispado.
    //
    //  Leer mas rapido que la fuente sube el espectro entero y lo que pasa de
    //  Nyquist vuelve PLEGADO: parciales que no son armonicos de nada, o sea
    //  un silbido metalico encima de la nota. Voice tiene un paso bajo de UN
    //  polo para eso, que a 6 dB por octava es poca pared.
    //
    //  Se mide con un seno solo: un seno de la fuente sube por el transporte y
    //  deberia salir en su sitio y nada mas. Todo lo que aparezca LEJOS del tono
    //  esperado es material plegado, y se mide como la energia fuera de una
    //  ventana estrecha alrededor de el, en dB por debajo del tono. Un seno puro
    //  no tiene armonicos que confundir con el pliegue.
    //
    //  ESTA SONDA LLEVABA TANDAS SIN MEDIR NADA, y por DOS motivos a la vez.
    //
    //  El primero era que imprimia la cadena "FLOJO" y no llamaba a
    //  `zatiFalla()`: el veredicto solo lo suma esa funcion, asi que la linea
    //  **no podia suspender el banco jamas** por mucho que empeorara.
    //
    //  El segundo es peor y es el que importa: media a `setPadPitch (0, 12.0f)`,
    //  o sea **delta = 2.0 exacta**. Con delta entera `frac` vale 0 en TODAS las
    //  muestras y `hermite4` devuelve `y[idx]` sin interpolar ni una vez. No es
    //  que estuviera desarmada: es que apuntaba al vacio. Medido rompiendo
    //  `hermite4` a proposito para que devolviera el vecino mas cercano: la
    //  cifra salio **-49.8 dB en los dos casos, identica hasta el decimal**. Una
    //  prueba en la que destrozar lo que mide no mueve el numero no es una
    //  prueba.
    //
    //  Ahora se mide a +7 -delta 1.4983, fraccion distinta en cada muestra- y a
    //  +24 -delta 4, el caso duro-, y el veredicto suma. El liston es **lo que
    //  el codigo cumple hoy menos tres decibelios**, que es lo que hace que la
    //  regla proteja el estado de hoy contra una regresion en vez de ser un
    //  deseo.
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

        //  UNA CORRIDA, dado el transporte y el tono de la fuente. Devuelve el
        //  pliegue peor en dB por debajo del tono esperado, y donde cayo.
        auto pliegue = [&] (float semis, float srcHz, double& worstHzOut)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setPadPitch (0, semis);
            e.publishSample (0, tone (48000.0, 1.0, srcHz));

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

            const double esperado = (double) srcHz * std::pow (2.0, (double) semis / 12.0);
            const double wanted = power (d, n, 48000.0, esperado);
            double worst = 0.0; worstHzOut = 0.0;
            for (double hz = 200.0; hz < 22000.0; hz += 100.0)
            {
                if (std::abs (hz - esperado) < 400.0) continue;    // el tono y su falda
                const double p = power (d, n, 48000.0, hz);
                if (p > worst) { worst = p; worstHzOut = hz; }
            }
            return 10.0 * std::log10 (juce::jmax (1.0e-12, worst) / juce::jmax (1.0e-12, wanted));
        };

        //  EL LISTON ES LO QUE EL CODIGO YA CUMPLE MENOS TRES DECIBELIOS.
        //
        //  Medido con la sonda ya arreglada: **-50.6 dB a +7** y **-51.7 dB a
        //  +23**. Menos tres y redondeado a la baja. Eso es lo que separa una
        //  regla que protege de un deseo: si alguien toca el interpolador o el
        //  paso bajo y el pliegue empeora tres decibelios, esta linea lo dice.
        constexpr double kPliegue7  = -47.0;
        constexpr double kPliegue23 = -48.0;

        //  +7 Y NO +12: delta 1.4983, o sea fraccion distinta en cada muestra.
        //  Con +12 la delta es 2.0 exacta y el interpolador no llega a correr.
        double hz7 = 0.0;
        const double db7 = pliegue (7.0f, 5000.0f, hz7);
        std::printf ("%-34s +7 st: pliegue peor %+.1f dB en %.0f Hz   %s\n",
                     "aliasing al subir el tono", db7, hz7,
                     db7 < kPliegue7 ? "OK" : zatiFalla());

        //  Y EL CASO DURO, que es donde el paso bajo de un polo se queda corto.
        //
        //  +23 Y NO +24, y esto es la misma trampa otra vez: delta a +24 vale
        //  **4.0 exacta**, o sea `frac` cero en todas las muestras y el
        //  interpolador sin correr, igual que el +12 de antes. Se vio en la
        //  rotura a proposito: con `hermite4` devolviendo el vecino mas cercano,
        //  el +7 se movio de -50.6 a **-38.4 dB** y el +24 se quedo clavado en
        //  -49.1. A +23 la delta es 3.8459 y la fraccion cambia en cada muestra.
        //
        //  Con 3 kHz de fuente el tono esperado cae en 11.5 kHz, dentro de la
        //  banda y con sitio de sobra para que el pliegue se vea.
        double hz23 = 0.0;
        const double db23 = pliegue (23.0f, 3000.0f, hz23);
        std::printf ("%-34s +23 st: pliegue peor %+.1f dB en %.0f Hz   %s\n",
                     "aliasing al subir el tono", db23, hz23,
                     db23 < kPliegue23 ? "OK" : zatiFalla());
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
                     ok ? "OK" : zatiFalla(), perdidos.toRawUTF8());
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
 enCanalCero (e);
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
                     differ == 0 ? "si" : "NO", ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     (unsigned) (conMudo & 0xFu), mudoOk ? "OK" : zatiFalla());

        //  Y EL SILENCIO DE UN BLOQUE SUELTO, que es otra cosa y se mide por
        //  COMPAS y no por pasada.
        //
        //  El del carril calla la pista entera; este calla UN bloque. Con la
        //  cuenta hecha sobre la pasada completa las dos salen igual -el pad
        //  suena, porque sigue sonando en los otros tres compases- asi que lo
        //  que se anota es QUE suena en CADA compas.
        //
        //  Con TRES cifras, que dos se enganan: el pad 3 tiene que sonar en el
        //  compas 0, NO sonar en el 1, y el pad 4 -que no lleva silencio- tiene
        //  que sonar en los dos. Sin la tercera, «no suena en el 1» lo cumple
        //  igual un transporte que no llega al compas 1, y sin la primera lo
        //  cumple el carril entero mudo.
        std::uint64_t porCompas[4] {};
        e.setSongCellMute (2, 1, true);
        e.setPlaying (true);
        for (int i = 0; i < blocksPerBar * 6; ++i)
        {
            e.renderNextBlock (b, 0, 256);
            const int bar = juce::jlimit (0, 3, e.getSongBar());
            porCompas[bar] |= e.fetchTriggered();
        }
        e.setPlaying (false);
        e.renderNextBlock (b, 0, 256);
        e.fetchTriggered();
        e.setSongCellMute (2, 1, false);

        const bool bloqueOk = (porCompas[0] & (1u << 2)) != 0
                           && (porCompas[1] & (1u << 2)) == 0
                           && (porCompas[0] & (1u << 3)) != 0
                           && (porCompas[1] & (1u << 3)) != 0;
        std::printf ("%-34s compas 0 %X   compas 1 %X   %s\n",
                     "bloque de cancion silenciado",
                     (unsigned) (porCompas[0] & 0xFu),
                     (unsigned) (porCompas[1] & 0xFu),
                     bloqueOk ? "OK" : zatiFalla());

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
                     "largo propio del bloque", uno, dos, largoOk ? "OK" : zatiFalla());

        const bool bucleOk = peorCompas == 1 && sinBucle == 3;
        std::printf ("%-34s con bucle [0,2) llega al %d   sin bucle al %d   %s\n",
                     "bucle de un tramo", peorCompas, sinBucle, bucleOk ? "OK" : zatiFalla());
    }

    //  EL ACORDE. Un paso con cuatro notas tiene que disparar el pad CUATRO
    //  veces en el mismo instante, y no una: es lo unico que separa un piano
    //  roll de un mando de afinacion. Se cuenta por VOCES vivas y no por
    //  disparos, porque triggeredMask es un bit por pad y cuatro disparos del
    //  mismo pad ponen el mismo bit - la prueba obvia habria dicho que si sin
    //  mirar nada.
    {
        //  CON EL TOPE POR PAD DE LA GAMA BAJA -cuatro- y no con ocho: el
        //  suelo de voces por pad se deriva del acorde (ver setPolyphony), y
        //  pidiendo ocho aqui la prueba no podria ver si esa derivacion
        //  existe. Medido sin ella: un acorde de ocho notas daba SIETE voces
        //  de pico -no cuatro: robar una voz es un fundido, y la robada sigue
        //  viva mientras se apaga-, o sea que en tres de las cuatro gamas la
        //  nota se escribia, se veia en la rejilla y se comia a su hermana.
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 4);
 enCanalCero (e);
        e.setPadGain (0, 0.8f);
        e.publishSample (0, makeSample (48000.0, 1.0, 220.0f));
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        //  `extras` y no un booleano: la prueba vieja preguntaba por CUATRO
        //  notas, que era el tope entero, asi que no podia distinguir "suena
        //  el acorde" de "suena lo que cabe". Con el tope en ocho hace falta
        //  pedir las dos cifras al mismo sitio.
        auto vivas = [&] (int extras) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);
            e.setStepNote (0, 0, 0, 0);
            e.clearStepExtras (0, 0, 0);
            //  Una escala, que es lo que se puede apilar sin repetir semitono:
            //  dos notas iguales son una sola voz y la cuenta mentiria.
            const int semis[] = { 4, 7, 12, 2, 5, 9, 11 };
            for (int i = 0; i < extras && i < AudioEngine::kExtraNotes; ++i)
                e.setStepExtra (0, 0, 0, i, semis[i], true);
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

        const int sola   = vivas (0);
        const int acorde = vivas (3);
        //  EL ACORDE ENTERO, que es lo que la queja pedia: ocho notas tienen
        //  que dar OCHO voces. Esta es la que caza el tope por pad, que estaba
        //  en 4 en la gama baja y 6 en la media: la nota se escribe, se ve en
        //  la rejilla y no se oye. Medido con setPolyphony(32,4) antes de
        //  derivar el suelo del acorde: 8 notas daban 7 voces.
        //  SIETE extras y OCHO voces, escritos a mano y no leidos de
        //  kExtraNotes: una prueba que lee la constante que juzga cambia de
        //  opinion a la vez que el fallo, y bajar el tope saldria verde.
        const int ocho = vivas (7);
        const bool ok = sola == 1 && acorde == 4 && ocho == 8;
        std::printf ("%-34s una nota %d voz   cuatro %d voces   ocho %d voces   %s\n",
                     "acorde en un paso", sola, acorde, ocho, ok ? "OK" : zatiFalla());
    }

    //  EL MISMO ACORDE, PERO CON OTRO PAD EN EL PASO.
    //
    //  La de arriba mide un pad SOLO, y por eso salia verde con el fallo
    //  puesto: firePatternStep encola la raiz y detras sus tres notas de mas
    //  -las extras con `corta = false`, porque el autocorte del pad las mataria
    //  antes de sonar- y fireDueHits sacaba el hueco cambiandolo por el ULTIMO
    //  elemento, o sea REORDENANDO. Con un pad solo la raiz seguia saliendo
    //  primera; con otro pad de indice menor en el mismo paso -un acorde encima
    //  de un bombo, o sea lo normal- salian primero las extras y la raiz
    //  llegaba la ultima y se las llevaba por delante con su autocorte.
    //
    //  Medido: 2 voces donde tienen que salir 5. Un acorde que suena a una nota
    //  en cuanto hay compania, que es siempre.
    //
    //  El vecino va en el pad 0 y el acorde en el 1 a proposito: el encolado
    //  recorre los pads en orden, asi que el vecino tiene que ir DELANTE para
    //  que la reordenacion muerda. Y se cuentan las voces totales -1 + 4- porque
    //  el motor no cuenta voces por pad; con el setup fijo la suma vale.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 8);
 enCanalCero (e);
        e.setPadGain (0, 0.8f);
        e.setPadGain (1, 0.8f);
        e.publishSample (0, makeSample (48000.0, 1.0, 110.0f));
        e.publishSample (1, makeSample (48000.0, 1.0, 220.0f));
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        auto vivas = [&] (bool conAcorde) noexcept
        {
            e.setSongMode (false);
            e.clearPattern (0);
            e.setPatternLength (0, 16);
            e.setStep (0, 0, 0, true);          // el vecino
            e.setStep (0, 0, 1, true);          // el del acorde
            e.setStepNote (0, 0, 1, 0);
            e.clearStepExtras (0, 0, 1);
            if (conAcorde)
            {
                e.setStepExtra (0, 0, 1, 0, 4, true);
                e.setStepExtra (0, 0, 1, 1, 7, true);
                e.setStepExtra (0, 0, 1, 2, 12, true);
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
        const bool ok = sola == 2 && acorde == 5;
        std::printf ("%-34s sin acorde %d voces   con acorde %d voces (1+4)   %s\n",
                     "acorde con un vecino", sola, acorde, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     "audicion del piano roll", grave, agudo, razon, despues, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     "bucle sin chasquido", peor, pico, 100.0f * razon, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     "largo de la nota", medio, uno, r1, dos, r2, ok ? "OK" : zatiFalla());
    }

    //  EL EMPUJON DE UN PASO. HUMANIZAR escribe cuanto se aparta cada golpe de
    //  la rejilla, y lo que hay que comprobar es que el motor lo OBEDECE: un
    //  empujon que no mueve nada es un numero guardado, no un groove. Se mide
    //  en muestras contando cuantos bloques tarda en sonar el pad.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 64); e.setPolyphony (16, 4);
 enCanalCero (e);
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
                     "empujon de un paso", recto, tarde, diff, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                     "bloqueo del corte por paso", abierto, cerrado, caidaDb, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
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
                         "bloqueo de inicio", sin.izq, con.izq, ok ? "OK" : zatiFalla());
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
                         ok ? "OK" : zatiFalla());
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
                         "bloqueo de ataque", corto, largo, db, ok ? "OK" : zatiFalla());
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
                         "bloqueo de caida", corta, larga, ok ? "OK" : zatiFalla());
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
 enCanalCero (e);
            e.setDlyMix (1.0f); e.setDlyTime (100.0f); e.setDlyFb (0.5f);
            e.setPadGain (0, 1.0f);
            //  Menos de cero significa "no se toca": asi la corrida de control
            //  es exactamente la maquina recien encendida y no una que alguien
            //  ha puesto a cero, que no es lo mismo aunque de el mismo numero.
            if (envio >= 0.0f) e.setCanalSend (0, 3, envio);
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
                     "envio por defecto cerrado", defecto, abierto, ok ? "OK" : zatiFalla());
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
                     "el aviso no se come el master", solo, bajado, vuelto, ok ? "OK" : zatiFalla());
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
                     rebote.getMasterGain(), ok ? "OK" : zatiFalla());
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
                     20.0f * std::log10 (arriba), pasado, avisado, ok ? "OK" : zatiFalla());
    }

    //  Y ESA MEDIDA ERA UNA LINEA QUE IMPRIMIA OK.
    //
    //  Su propio comentario promete comprobar "que +12 dB llegue de verdad al
    //  motor - un jlimit olvidado en 1.0 dejaria el mando subiendo en la
    //  pantalla y el sonido quieto" y lo que lee es `getMasterGain()`, o sea
    //  el ATOMICO que la linea de arriba acaba de escribir. Pasaba con el
    //  fader desconectado del audio, que es exactamente lo que pasaba: la
    //  etapa 5c-duck se saltaba entera para todo lo que estuviera POR ENCIMA
    //  de la unidad, asi que de 0 dB a +12 la casilla decia una cosa y por los
    //  cascos salia otra. La queja llego con esas palabras - "de 0 db a +12 db
    //  no hay cambio".
    //
    //  Se mide RENDERIZANDO, que es el unico sitio donde la respuesta es
    //  verdad, y con DOS cifras: el pico a 0 dB y el pico a +12, porque "sube"
    //  lo cumple tambien un fader que se pasa y "no cambia" lo cumple el fallo.
    //  El pad se deja bajo a proposito -0.1 de pico- para que la subida entera
    //  quepa sin tocar techo: lo que se mide es el fader y no un recorte.
    //
    //  Y con la RAMPA cumplida antes de mirar: el master sube con una
    //  constante de 12 ms, asi que los primeros bloques van por el camino y no
    //  en el destino. Ocho bloques de 512 a 48 kHz son 85 ms, siete veces la
    //  constante.
    {
        auto picoCon = [] (float master)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setMasterUser (master);
            e.setPadGain (0, 0.1f);
            e.publishSample (0, makeSample (48000.0, 1.0, 400.0f));

            juce::AudioBuffer<float> b (2, 512);
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.postNoteOn (0, 1.0f);

            double pico = 0.0;
            for (int blk = 0; blk < 24; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, 512);
                if (blk < 8) continue;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        pico = juce::jmax (pico, (double) std::abs (b.getSample (ch, i)));
            }
            return pico;
        };

        const double unidad = picoCon (1.0f);
        const double arriba = picoCon (AudioEngine::kMasterMaxGain);
        const double subida = (unidad > 1.0e-6) ? 20.0 * std::log10 (arriba / unidad) : 0.0;
        const bool ok = unidad > 0.01 && std::abs (subida - 12.0) < 0.3;
        std::printf ("%-34s a 0 dB %.5f   a +12 dB %.5f   sube %+.2f dB   %s\n",
                     "el fader del master SUENA", unidad, arriba, subida,
                     ok ? "OK" : zatiFalla());
    }

    //  UN GOLPE FLOJO NO ES UN GOLPE FUERTE BAJADO DE VOLUMEN.
    //
    //  `velocity` solo multiplicaba la ganancia, asi que la unica diferencia
    //  entre un golpe fantasma y uno a tope era el fader. En un parche de
    //  verdad los armonicos altos caen mucho mas rapido que el fundamental al
    //  pegar mas flojo - por eso un ghost note suena APAGADO y no solo bajo.
    //
    //  Medido POR EL CAMINO REAL -publishSample y postNoteOn con su fuerza- y
    //  no llamando a Voice por dentro: lo que hay que comprobar es que la
    //  fuerza que viaja en el comando llegue hasta el filtro, y una llamada
    //  directa se saltaria justo el trozo que puede estar roto.
    //
    //  Se mide por ENERGIA ALTA -la diferencia entre muestras, que es un paso
    //  alto de primer orden- y NORMALIZADA por el nivel, que es lo unico que
    //  separa "suena mas oscuro" de "suena mas bajo". Sin dividir por el nivel,
    //  bajar el volumen tambien baja la energia alta y la prueba diria que si a
    //  un cambio que no existe: es el mismo error que ya costo una medida en el
    //  bloqueo del corte.
    //
    //  Y CON RUIDO BLANCO Y NO CON UN TONO, que es donde esta prueba mintio
    //  antes de acertar - la sexta vez en este banco. El primer intento uso
    //  makeSample, que es 80% seno y 20% ruido, y saco 0.477 / 0.414 / 0.394:
    //  el enlace funcionaba y la medida casi no lo veia.
    //
    //  La razon es que para UN TONO un paso bajo atenua la energia alta y la
    //  total EN LA MISMA proporcion, asi que el cociente normalizado no se
    //  mueve: 0.477 es exactamente 2*sin(pi*3000/48000), o sea el tono y nada
    //  mas. Lo que hace visible un filtro es un espectro REPARTIDO, donde
    //  quitar de arriba cambia el reparto y no solo el volumen.
    {
        auto ruidoBlanco = [] ()
        {
            auto* sb = new SampleBuffer();
            const int n = 48000;
            sb->buffer.setSize (2, n);
            juce::Random rng (20260821);
            for (int i = 0; i < n; ++i)
            {
                const float v = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
                sb->buffer.setSample (0, i, v);
                sb->buffer.setSample (1, i, v);
            }
            sb->sourceSampleRate = 48000.0;
            return SampleBuffer::Ptr (sb);
        };

        auto brillo = [&] (float vel)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setSafetyLimiter (false);      // el saturador tambien cambia el brillo
            e.publishSample (0, ruidoBlanco());
            e.setPadGain (0, 1.0f);
            juce::AudioBuffer<float> out (2, 512);
            //  Un bloque en vacio para que el pad adopte su muestra: el hilo de
            //  audio la recoge al principio del bloque, no al publicarla.
            out.clear(); e.renderNextBlock (out, 0, 512);
            e.postNoteOn (0, vel);
            double alta = 0.0, total = 0.0;
            float prev = 0.0f;
            for (int b = 0; b < 8; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, 512);
                const auto* w = out.getReadPointer (0);
                for (int i = 0; i < 512; ++i)
                {
                    const double d = (double) w[i] - (double) prev;
                    alta  += d * d;
                    total += (double) w[i] * (double) w[i];
                    prev = w[i];
                }
            }
            return total > 1.0e-12 ? std::sqrt (alta / total) : 0.0;
        };

        const double plena = brillo (1.0f);
        const double media = brillo (0.5f);
        const double floja = brillo (0.15f);

        //  Tres y no dos: "baja" lo cumple tambien un enlace que se apaga a
        //  medio camino, y el extremo de arriba es la promesa de que un patron
        //  viejo -stepVel a cero, que se lee como 1.0- suena exactamente igual.
        const bool ok = plena > 0.0
                     && media < plena * 0.95
                     && floja < media * 0.95;
        std::printf ("%-34s plena %.3f   al 50%% %.3f   al 15%% %.3f   %s\n",
                     "un golpe flojo suena mas oscuro", plena, media, floja,
                     ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  UN PAD QUE LLEVA INSTRUMENTO: las zonas, medidas por lo que SALE.
    //
    //  Lo que compra la multizona no es la afinacion -esa sale bien con una
    //  sola muestra, porque el resto de semitonos se le resta a la raiz- sino
    //  el TIMBRE. Estirar una muestra dos octavas arriba se lleva los formantes
    //  dos octavas arriba: eso es lo que suena a ardilla, y es lo unico que
    //  distingue "elige la zona" de "usa siempre la primera".
    //
    //  Y se mide con COROS a proposito, que es la unica familia cuyas tres
    //  bandas estan en Hz FIJOS y no siguen a la nota: con zonas el centroide
    //  de la nota +24 se parece al de la nota 0, y sin ellas se va por cuatro.
    //  Con un organo -armonicos multiplos de la nota- el centroide sube igual
    //  de las dos formas y la prueba diria que si a cualquier cosa.
    {
        auto centro = [] (AudioEngine& e, int semis, int bloques)
        {
            juce::AudioBuffer<float> out (2, 512);
            out.clear(); e.renderNextBlock (out, 0, 512);      // que adopte la muestra
            e.postNoteOnAt (0, semis, 1.0f);
            //  Energia alta contra energia total, que es un paso alto de primer
            //  orden: mide "cuanto agudo hay" sin necesitar una transformada.
            double alta = 0.0, total = 0.0;
            float prev = 0.0f;
            for (int b = 0; b < bloques; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, 512);
                const auto* w = out.getReadPointer (0);
                for (int i = 0; i < 512; ++i)
                {
                    const double d = (double) w[i] - (double) prev;
                    alta += d * d; total += (double) w[i] * (double) w[i];
                    prev = w[i];
                }
            }
            return total > 1.0e-12 ? std::sqrt (alta / total) : 0.0;
        };

        auto conInstrumento = [&] (int semis, int bloques)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setSafetyLimiter (false);
            e.publishSample (0, Sintes::sintetiza (10, 0));   // COROS AAH
            e.setPadGain (0, 1.0f);
            return centro (e, semis, bloques);
        };

        auto conFamilia = [&] (int fam, int semis, int bloques)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setSafetyLimiter (false);
            e.publishSample (0, Sintes::sintetiza (fam, 0));
            e.setPadGain (0, 1.0f);
            return centro (e, semis, bloques);
        };

        const double n0  = conInstrumento (0, 40);
        const double n24 = conInstrumento (24, 40);

        //  Y LA COSTURA SE MIDE CON OTRA FAMILIA, que fue lo que la primera
        //  version hizo mal. Con COROS salia x0.60 y el codigo estaba bien: las
        //  bandas de una voz estan en Hz FIJOS, asi que +5 -que sale de la raiz
        //  0 estirada arriba- y +7 -que sale de la raiz 12 estirada abajo- tienen
        //  los formantes a diez semitonos de distancia. Eso no es un fallo, es
        //  lo que CUESTA una zona cada doce semitonos, y en una voz se nota mas
        //  que en nada. Primero se duda de la prueba.
        //
        //  BAJOS sigue a la nota -el filtro esta en hz*brillo- asi que ahi la
        //  costura SI tiene que ser suave: dos semitonos de diferencia, x1.12,
        //  y lo que esta prueba caza es una zona rendida a la frecuencia
        //  equivocada o una ventana mal puesta.
        const double b5 = conFamilia (0, 5, 40);
        const double b7 = conFamilia (0, 7, 40);

        //  DOS NUMEROS Y NO UNO. El primero dice que la zona se ELIGE: sin
        //  eleccion, +24 se lee al cuadruple y el brillo se va por cuatro. El
        //  segundo dice que las zonas CASAN, y eso el primero no lo ve.
        const double rango   = (n0 > 1.0e-9) ? n24 / n0 : 0.0;
        const double costura = (b5 > 1.0e-9) ? b7 / b5 : 0.0;
        const bool ok = n0 > 1.0e-9 && rango < 2.0
                     && costura > 0.85 && costura < 1.45;
        std::printf ("%-34s nota 0 %.3f   +24 %.3f (x%.2f)   costura +5/+7 x%.2f   %s\n",
                     "el instrumento elige zona", n0, n24, rango, costura,
                     ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y QUE EL BUCLE VUELVA AL SITIO, que es la otra mitad y la que no se ve
    //  en un espectro: una nota sostenida tiene que seguir sonando pasada la
    //  zona -0.6 s, o sea 56 bloques de 512- y hacerlo SIN volver a atacar.
    //
    //  Volver al principio de la ventana en vez de al punto de bucle es el
    //  fallo que Voice::loopFrom existe para evitar, y suena a que la nota se
    //  vuelve a tocar sola cada medio segundo: se ve como un bache en la
    //  envolvente. Con el ataque lento de un colchon el bache es enorme.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setSafetyLimiter (false);
        e.publishSample (0, Sintes::sintetiza (5, 0));    // COLCHONES PWM PAD
        e.setPadGain (0, 1.0f);

        juce::AudioBuffer<float> out (2, 512);
        out.clear(); e.renderNextBlock (out, 0, 512);
        //  SOSTENIDA: sin decirlo, una audicion dura 1.2 s y esta prueba mide
        //  una nota larga - el bache saldria -154 dB por haberse acabado.
        e.postNoteOnAt (0, 0, 1.0f, AudioEngine::kSostenida);

        //  Se mira DESPUES del ataque -bloque 40 en adelante- y hasta bien
        //  pasadas dos vueltas del bucle.
        std::vector<double> rms;
        for (int b = 0; b < 200; ++b)
        {
            out.clear();
            e.renderNextBlock (out, 0, 512);
            if (b < 40) continue;
            double s = 0.0;
            const auto* w = out.getReadPointer (0);
            for (int i = 0; i < 512; ++i) s += (double) w[i] * (double) w[i];
            rms.push_back (std::sqrt (s / 512.0));
        }
        const double mx = *std::max_element (rms.begin(), rms.end());
        const double mn = *std::min_element (rms.begin(), rms.end());
        const double baja = (mx > 1.0e-9) ? 20.0 * std::log10 (juce::jmax (1.0e-9, mn) / mx) : -99.0;
        //  Sigue sonando (mn alto) y sin baches (la diferencia, en dB, corta).
        //  Doce decibelios y no nueve: un colchon de cuatro osciladores
        //  desafinados SE MUEVE -eso es lo que lo hace un colchon- y lo que esta
        //  prueba caza es otra cosa, que la nota vuelva a ATACAR en cada vuelta.
        //  Eso son 35 dB, porque el ataque son 0.35 s de un bucle de 0.42.
        const bool ok = mn > 0.01 && baja > -12.0;
        std::printf ("%-34s minimo %.4f   maximo %.4f   bache %.1f dB   %s\n",
                     "la nota sostenida no re-ataca", mn, mx, baja,
                     ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  EL DEDO COMO TECLA: soltar, y tres a la vez.
    //
    //  DOS numeros y no uno. El primero es el que hace falta que exista: un
    //  instrumento SOSTIENE -su zona da vueltas mientras la nota dure- asi que
    //  sin un "suelta" la nota no se acaba nunca, y un pad con un colchon se
    //  queda sonando hasta que lo pises con otro golpe. El segundo es lo que la
    //  funcion promete: varios pads a la vez son varias notas a la vez.
    //
    //  Y el primero se mide DESPUES de la caida del pad, no justo al soltar:
    //  soltar abre la caida, no corta. Preguntar en el bloque siguiente daria
    //  verde con el codigo roto, porque una voz que se esta apagando sigue
    //  activa.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setSafetyLimiter (false);
        //  COLCHONES PWM PAD en tres pads: sostiene, que es el caso que importa.
        for (int p = 0; p < 3; ++p)
        {
            e.publishSample (p, Sintes::sintetiza (5, 0));
            e.setPadGain (p, 1.0f);
            e.setPadRelease (p, 40.0f);
        }

        juce::AudioBuffer<float> out (2, 512);
        out.clear(); e.renderNextBlock (out, 0, 512);

        //  AL FINAL Y NO EL PICO, que es como la primera version de esta prueba
        //  se equivoco: soltar abre la CAIDA, no corta, asi que en los primeros
        //  bloques despues del "suelta" las tres voces siguen vivas y el pico
        //  sale 3 con el codigo perfecto. Lo que se pregunta es si quedan, no
        //  si hubo. Y para las notas sostenidas el ultimo bloque vale igual,
        //  porque un bucle no se apaga solo - ese es justo el punto.
        auto corre = [&] (int bloques)
        {
            for (int b = 0; b < bloques; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, 512);
            }
            return e.getActiveVoiceCount();
        };

        //  SOSTENIDAS a proposito. postNoteOn le pone ahora a la nota el largo
        //  de una audicion -1200 ms- porque un dedo en un pad que no esta en
        //  modo tecla no manda "suelta"; con ese largo, "tras soltar 0" saldria
        //  verde por el largo y no por el NoteOff, que es lo que esta prueba
        //  mide. Un numero que puede salir bien por dos motivos no mide ninguno.
        e.postNoteOnAt (0, 0, 1.0f, AudioEngine::kSostenida);
        const int una = corre (30);

        e.postNoteOnAt (1, 0, 1.0f, AudioEngine::kSostenida);
        e.postNoteOnAt (2, 0, 1.0f, AudioEngine::kSostenida);
        const int tres = corre (30);

        //  Y ahora se sueltan las tres. 120 bloques son 1.28 s: de sobra para
        //  una caida de 40 ms, y no tanto como para que un bucle que sigue
        //  dando vueltas se apague por su cuenta - no lo hace, ese es el punto.
        for (int p = 0; p < 3; ++p) e.postNoteOff (p);
        const int tras = corre (120);

        const bool ok = una == 1 && tres == 3 && tras == 0;
        std::printf ("%-34s una %d   tres a la vez %d   tras soltar %d   %s\n",
                     "el dedo como tecla", una, tres, tras, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  UN ACORDE TIENE QUE SONAR TAMBIEN EN LA SEGUNDA VUELTA.
    //
    //  La queja, con sus palabras: "cuando pongo un acorde el primer golpe si
    //  lo hace, pero luego para repetir la secuencia ya no". La causa es que un
    //  paso SIN largo escrito ponia la voz en -1 -"suelta sola"- y una zona de
    //  instrumento DA VUELTAS: la nota no se acaba nunca. Las tres notas de mas
    //  del acorde ademas se disparan sin autocorte a proposito, asi que cada
    //  vuelta dejaba tres voces mas vivas para siempre.
    //
    //  TRES numeros y no uno. El del medio es el que delata: lo que queda vivo
    //  al final del compas, o sea DESPUES de que la nota tenia que haber
    //  terminado. Sin el, "en la segunda vuelta suenan cuatro" tambien lo
    //  cumple una maquina en la que las cuatro de la primera siguen sonando.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 8);
 enCanalCero (e);
        e.setPadGain (0, 0.8f);
        //  BAJOS DUB, que sostiene: es el caso que la queja describe.
        e.publishSample (0, Sintes::sintetiza (0, 0));
        juce::AudioBuffer<float> b (2, 256);
        runBlocks (e, b, 256, 4);

        e.setSongMode (false);
        e.clearPattern (0);
        e.setPatternLength (0, 16);
        e.setStep (0, 0, 0, true);
        e.setStepNote (0, 0, 0, 0);
        e.clearStepExtras (0, 0, 0);
        e.setStepExtra (0, 0, 0, 0, 4,  true);
        e.setStepExtra (0, 0, 0, 1, 7,  true);
        e.setStepExtra (0, 0, 0, 2, 12, true);
        e.setBpm (120.0);
        e.setPlaying (true);

        //  Un compas a 120 BPM en semicorcheas son 2 s, o sea 375 bloques de
        //  256. Se mira el pico de los primeros bloques de cada compas -la
        //  nota tarda en arrancar lo que tarde el bloque- y el sobrante justo
        //  antes de que el compas de la vuelta.
        int pico1 = 0, sobra = 0, pico2 = 0;
        for (int i = 0; i < 760; ++i)
        {
            e.renderNextBlock (b, 0, 256);
            const int v = e.getActiveVoiceCount();
            if (i < 20) pico1 = juce::jmax (pico1, v);
            if (i == 370) sobra = v;
            if (i >= 375 && i < 395) pico2 = juce::jmax (pico2, v);
        }
        e.setPlaying (false); e.postPanic();
        for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 256);

        const bool ok = pico1 == 4 && sobra == 0 && pico2 == 4;
        std::printf ("%-34s compas 1: %d voces   sobra al final: %d   compas 2: %d   %s\n",
                     "el acorde vuelve a sonar", pico1, sobra, pico2, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y UN PASO SIN LARGO DURA UN PASO, no toda la cancion.
    //
    //  Es la otra mitad de lo de arriba y la que se oye en una secuencia de
    //  tripletes: "no se autocorta el sonido". El autocorte SI funcionaba - la
    //  raiz corta su cola - pero entre golpe y golpe la nota seguia sonando,
    //  que desde el dedo es exactamente lo mismo que no cortarse.
    //
    //  Se mide en BLOQUES con la voz viva contra el largo del paso, y no en
    //  "se acabo antes del siguiente": un largo de una muestra tambien acaba
    //  antes del siguiente y no es una nota.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 64); e.setPolyphony (32, 8);
 enCanalCero (e);
        e.setPadGain (0, 0.8f);
        e.setPadRelease (0, 5.0f);          // que la caida no cuente como nota
        e.publishSample (0, Sintes::sintetiza (0, 0));
        juce::AudioBuffer<float> b (2, 64);
        runBlocks (e, b, 64, 4);

        e.setSongMode (false);
        e.clearPattern (0);
        e.setPatternLength (0, 16);
        e.setStep (0, 0, 0, true);
        e.setStepNote (0, 0, 0, 0);
        e.setBpm (120.0);
        e.setPlaying (true);

        //  Un paso son 6000 muestras, o sea 93 bloques de 64.
        int vivos = 0;
        for (int i = 0; i < 200; ++i)
        {
            e.renderNextBlock (b, 0, 64);
            if (e.getActiveVoiceCount() > 0) ++vivos;
        }
        e.setPlaying (false); e.postPanic();
        for (int i = 0; i < 8; ++i) e.renderNextBlock (b, 0, 64);

        //  Con margen por los dos lados: la caida de 5 ms son 4 bloques y el
        //  disparo cae dentro de un bloque, no en su borde.
        const bool ok = vivos > 80 && vivos < 110;
        std::printf ("%-34s %d bloques vivos de un paso de 93   %s\n",
                     "un paso sin largo dura un paso", vivos, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  EL RECORTE DE UN INSTRUMENTO RECORTA, Y LO QUE DA VUELTAS ES EL TROZO.
    //
    //  Se ignoraba con este argumento: "una zona no se recorta". La persona lo
    //  dijo al reves y tiene razon: "si yo acorto ese sonido, el bucle tiene
    //  que ser de ese sonido". Ahora INICIO y FIN son FRACCION de la zona que
    //  toca, asi que significan lo mismo en las cinco octavas.
    //
    //  DOS numeros, que es lo que separa "recorta" de "se rompe": que lo que
    //  suena CAMBIE -bit a bit, porque "casi lo mismo" es justo lo que dejaria
    //  pasar un recorte que no se aplica- y que la nota SIGA VIVA y sonando
    //  despues del final del recorte, que es lo que dice que el bucle se ha
    //  mudado ahi dentro en vez de haberse acabado.
    {
        auto corre = [] (float fin, std::vector<float>& dst) noexcept
        {
            AudioEngine e; e.prepareToPlay (48000.0, 256); e.setPolyphony (32, 8);
 enCanalCero (e);
            e.setSafetyLimiter (false);
            auto sb = Sintes::sintetiza (0, 0);                 // BAJOS DUB, sostiene
            const int len = sb->buffer.getNumSamples();
            e.publishSample (0, sb);
            e.setPadGain (0, 1.0f);
            //  En MUESTRAS, que es lo que el motor guarda; la fraccion contra
            //  la zona la hace triggerPad, que es quien sabe que zona toca.
            e.setPadStart (0, 0);
            e.setPadEnd (0, (int) ((float) len * fin));
            juce::AudioBuffer<float> b (2, 256);
            for (int i = 0; i < 4; ++i) e.renderNextBlock (b, 0, 256);
            e.postNoteOnAt (0, 0, 1.0f, AudioEngine::kSostenida);

            dst.clear();
            double pico = 0.0;
            for (int i = 0; i < 400; ++i)
            {
                b.clear();
                e.renderNextBlock (b, 0, 256);
                //  Solo la segunda mitad para el pico: el principio suena igual
                //  en las dos corridas -es el mismo ataque- y lo que se
                //  pregunta es si DESPUES sigue habiendo sonido.
                for (int i2 = 0; i2 < 256; ++i2)
                {
                    const float v = b.getSample (0, i2);
                    dst.push_back (v);
                    if (i > 200) pico = juce::jmax (pico, (double) std::abs (v));
                }
            }
            const int vivas = e.getActiveVoiceCount();
            e.postPanic();
            for (int i = 0; i < 4; ++i) e.renderNextBlock (b, 0, 256);
            return std::make_pair (pico, vivas);
        };

        std::vector<float> entera, corta;
        const auto a = corre (1.00f, entera);
        const auto c = corre (0.25f, corta);

        int distintas = 0;
        for (size_t i = 0; i < entera.size() && i < corta.size(); ++i)
            if (entera[i] != corta[i]) ++distintas;

        const bool ok = distintas > 0 && c.second > 0 && c.first > 0.01;
        std::printf ("%-34s %d muestras cambian   sigue sonando %.4f con %d voz   %s\n",
                     "recortar un instrumento", distintas, c.first, c.second,
                     ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  EL ANCHO ESTEREO, EN DOS NUMEROS.
    //
    //  Solo el primero -cuanto lado queda- lo cumple tambien un mando de
    //  volumen: bajar los dos canales baja el lado. Y solo el segundo -cuanto
    //  centro sobrevive- lo cumple no hacer nada. Los dos a la vez son lo unico
    //  que dice que esto es un ancho. Es la misma regla que QUITAR RUIDO.
    //
    //  Y LA FUENTE TIENE QUE SER ESTEREO DE VERDAD: dos ruidos DISTINTOS, uno
    //  por canal. Con la misma señal en los dos, S vale cero, el ancho no
    //  puede hacer nada y la prueba diria que si a cualquier cosa - incluso a
    //  un ancho que no esta conectado.
    {
        auto dosRuidos = []
        {
            auto* sb = new SampleBuffer();
            sb->sourceSampleRate = 48000.0;
            sb->buffer.setSize (2, 48000);
            juce::Random ra (7), rb (99);
            for (int i = 0; i < 48000; ++i)
            {
                sb->buffer.setSample (0, i, ra.nextFloat() * 0.6f - 0.3f);
                sb->buffer.setSample (1, i, rb.nextFloat() * 0.6f - 0.3f);
            }
            return SampleBuffer::Ptr (sb);
        };

        auto mide = [&] (float ancho)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setSafetyLimiter (false);
            e.publishSample (0, dosRuidos());
            e.setPadGain (0, 1.0f);
            e.setPadPan (0, 0.0f);
            e.setPadAncho (0, ancho);

            juce::AudioBuffer<float> out (2, 512);
            out.clear(); e.renderNextBlock (out, 0, 512);
            e.postNoteOn (0, 1.0f);

            double lado = 0.0, centro = 0.0;
            for (int b = 0; b < 20; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, 512);
                const auto* L = out.getReadPointer (0);
                const auto* R = out.getReadPointer (1);
                //  Los dos bloques primeros llevan el ataque del pad; se cuenta
                //  desde el tercero, que es donde el nivel ya es plano.
                if (b < 3) continue;
                for (int i = 0; i < 512; ++i)
                {
                    const double s = 0.5 * ((double) L[i] - (double) R[i]);
                    const double m = 0.5 * ((double) L[i] + (double) R[i]);
                    lado   += s * s;
                    centro += m * m;
                }
            }
            return std::pair<double, double> { std::sqrt (lado / 8704.0),
                                               std::sqrt (centro / 8704.0) };
        };

        const auto mono  = mide (0.0f);
        const auto tal   = mide (1.0f);
        const auto doble = mide (2.0f);

        //  A cero el lado es SILENCIO, no "casi": si queda algo, el mando no
        //  llega al final de su recorrido y "mono" no es mono.
        const bool ok = mono.first < 1.0e-6
                     && tal.first  > 0.01
                     && doble.first > tal.first * 1.9
                     && std::abs (mono.second  - tal.second) < tal.second * 0.02
                     && std::abs (doble.second - tal.second) < tal.second * 0.02;
        std::printf ("%-34s lado %.5f / %.5f / %.5f   centro %.5f / %.5f / %.5f   %s\n",
                     "el ancho estereo", mono.first, tal.first, doble.first,
                     mono.second, tal.second, doble.second, ok ? "OK" : zatiFalla());

        // --------------------------------------------------------------
        //  Y EL ORDEN, que con el pan al centro no se ve.
        //
        //  El ancho va ANTES del pan. Al reves el pan ya ha metido señal en el
        //  lado -es lo que hace un pan- y el ancho la amplificaria, asi que
        //  abrir el ancho de un pad panoramico se oiria como moverlo mas a la
        //  izquierda. Con el pan al centro las dos versiones dan lo MISMO, que
        //  es por lo que la medida de arriba no puede verlo.
        //
        //  Se mide con una muestra MONO y el pan fuera del centro: una fuente
        //  mono tiene lado cero, asi que en el orden correcto el ancho no puede
        //  hacer absolutamente nada y las tres corridas salen IGUALES. Bit a
        //  bit y no por nivel: "casi lo mismo, 0.1 dB" es justo lo que dejaria
        //  pasar un orden invertido con un pan suave. Es la misma comparacion
        //  que ya se hace con el filtro del pad apagado.
        auto unRuidoMono = []
        {
            auto* sb = new SampleBuffer();
            sb->sourceSampleRate = 48000.0;
            sb->buffer.setSize (1, 48000);
            juce::Random r (11);
            for (int i = 0; i < 48000; ++i)
                sb->buffer.setSample (0, i, r.nextFloat() * 0.6f - 0.3f);
            return SampleBuffer::Ptr (sb);
        };

        auto panoramico = [&] (float ancho)
        {
            AudioEngine e;
            e.prepareToPlay (48000.0, 512);
            e.setSafetyLimiter (false);
            e.publishSample (0, unRuidoMono());
            e.setPadGain (0, 1.0f);
            e.setPadPan (0, -0.5f);          // fuera del centro: ahi se ve el orden
            e.setPadAncho (0, ancho);

            juce::AudioBuffer<float> out (2, 512);
            out.clear(); e.renderNextBlock (out, 0, 512);
            e.postNoteOn (0, 1.0f);

            std::vector<float> salida;
            for (int b = 0; b < 10; ++b)
            {
                out.clear();
                e.renderNextBlock (out, 0, 512);
                for (int c = 0; c < 2; ++c)
                    salida.insert (salida.end(), out.getReadPointer (c),
                                   out.getReadPointer (c) + 512);
            }
            return salida;
        };

        const auto pm = panoramico (0.0f);
        const auto pt = panoramico (1.0f);
        const auto pd = panoramico (2.0f);

        int difiere = 0;
        for (size_t i = 0; i < pt.size(); ++i)
            if (pm[i] != pt[i] || pd[i] != pt[i]) ++difiere;

        std::printf ("%-34s mono con el pan a -0.5: %d muestras de %d cambian   %s\n",
                     "el ancho va antes del pan", difiere, (int) pt.size(),
                     difiere == 0 ? "OK" : zatiFalla());
    }

    //  UN CLIP DE AUDIO SUENA DONDE SE PUSO, Y NO ANTES.
    //
    //  Es la primera pieza de la linea de tiempo de audio, y se mide con DOS
    //  cifras porque una sola no separa nada: "suena en el compas 2" lo cumple
    //  tambien un clip que suena SIEMPRE, que es exactamente el fallo que sale
    //  de equivocarse con la posicion de la cancion. Asi que se mide el compas
    //  1 -donde tiene que haber silencio- y el 2 -donde tiene que sonar-.
    //
    //  A 120 BPM en semicorcheas un paso son 6000 muestras y un compas 96 000.
    {
        auto ruido = [] (int n)
        {
            auto* sb = new SampleBuffer();
            sb->sourceSampleRate = 48000.0;
            sb->buffer.setSize (1, n);
            juce::Random r (7);
            for (int i = 0; i < n; ++i)
                sb->buffer.setSample (0, i, r.nextFloat() * 0.8f - 0.4f);
            return SampleBuffer::Ptr (sb);
        };

        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setSafetyLimiter (false);
        e.setBpm (120.0f);
        e.setSongMode (true);
        e.setSongLength (4);

        auto fuente = ruido (48000);           // medio compas de ruido
        AudioEngine::ClipAudio c;
        c.fuente = fuente.get();
        c.pista  = 0;
        c.compas = 2;                          // <- aqui, y en ningun otro sitio
        c.desde  = 0;
        c.largo  = 48000;
        c.gain   = 1.0f;
        e.publicaClips (&c, 1);

        e.setPlaying (true);

        //  Pico por compas: 96 000 muestras, o sea 187.5 bloques de 512.
        const int porCompas = 96000, bloque = 512;
        float pico[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        juce::AudioBuffer<float> out (2, bloque);
        long long muestras = 0;
        for (int b = 0; b < (porCompas * 4) / bloque; ++b)
        {
            out.clear();
            e.renderNextBlock (out, 0, bloque);
            const int compas = (int) (muestras / porCompas);
            if (compas >= 0 && compas < 4)
                pico[compas] = juce::jmax (pico[compas], out.getMagnitude (0, bloque));
            muestras += bloque;
        }

        //  TRES cifras y no dos: los compases 0 y 1 CALLADOS y el 2 sonando.
        //  Sin el compas 0 la prueba no ve el fallo mas obvio de todos - que el
        //  clip ignore donde se puso y suene desde el principio -, porque un
        //  clip de medio compas colocado en cero deja el 1 en silencio igual.
        const bool ok = pico[0] < 0.001f && pico[1] < 0.001f && pico[2] > 0.05f;
        std::printf ("%-34s compas 0 %.5f   1 %.5f   2 %.5f   clips %d   %s\n",
                     "un clip suena donde se puso", pico[0], pico[1], pico[2],
                     e.numClips(), ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  EL METRONOMO: SUENA EN LA NEGRA Y CALLA ENTRE NEGRAS.
    //
    //  Con DOS cifras y no una, que es lo que separa un metronomo de un
    //  zumbido: cuanto pega en el instante de la negra Y cuanto queda en mitad
    //  del hueco. Solo lo primero lo cumple un tono continuo -que es como se
    //  escribe mal la primera version de esto- y solo lo segundo lo cumple no
    //  hacer nada.
    //
    //  A 120 BPM en semicorcheas una negra son cuatro pasos, o sea 24 000
    //  muestras; el hueco se mira a mitad de camino.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setSafetyLimiter (false);
        e.setBpm (120.0f);
        e.setClick (true);
        e.setPlaying (true);

        const int bloque = 512;
        juce::AudioBuffer<float> out (2, bloque);
        float enLaNegra = 0.0f, enElHueco = 0.0f;
        long long muestras = 0;

        for (int b = 0; b < 200; ++b)
        {
            out.clear();
            e.renderNextBlock (out, 0, bloque);
            const float mag = out.getMagnitude (0, bloque);
            //  Dentro de la negra son los 2000 primeros muestras de cada 24 000;
            //  el hueco, la mitad de en medio - lejos de los dos clics.
            const long long dentro = muestras % 24000;
            if (dentro < 2000)                    enLaNegra = juce::jmax (enLaNegra, mag);
            else if (dentro > 9000 && dentro < 15000) enElHueco = juce::jmax (enElHueco, mag);
            muestras += bloque;
        }

        const bool ok = enLaNegra > 0.05f && enElHueco < 0.01f;
        std::printf ("%-34s negra %.5f   hueco %.5f   %s\n",
                     "el metronomo marca el pulso", enLaNegra, enElHueco, ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  LA CUENTA ATRAS: SUENA Y NO AVANZA.
    //
    //  Dos cifras otra vez, y son las dos mitades de la misma cosa: durante la
    //  cuenta el clic tiene que sonar Y el compas no puede moverse; al acabar,
    //  tiene que moverse. Solo lo primero lo cumple un metronomo sin cuenta
    //  atras, y solo lo segundo lo cumple un transporte parado.
    {
        AudioEngine e;
        e.prepareToPlay (48000.0, 512);
        e.setSafetyLimiter (false);
        e.setBpm (120.0f);
        e.setSongMode (true);
        e.setSongLength (4);
        e.setClick (true);
        e.armaCuentaAtras (1);          // un compas
        const bool armada = e.enCuentaAtras();
        e.setPlaying (true);

        const int bloque = 512;
        juce::AudioBuffer<float> out (2, bloque);
        float duranteLaCuenta = 0.0f;
        int   compasDurante = -99, compasDespues = -99;
        long long muestras = 0;

        //  Un compas a 120 BPM en semicorcheas son 96 000 muestras.
        for (int b = 0; b < 400; ++b)
        {
            out.clear();
            e.renderNextBlock (out, 0, bloque);
            if (muestras < 90000)
            {
                duranteLaCuenta = juce::jmax (duranteLaCuenta, out.getMagnitude (0, bloque));
                compasDurante   = e.getSongBar();
            }
            else if (muestras > 120000 && compasDespues == -99)
            {
                compasDespues = e.getSongBar();
            }
            muestras += bloque;
        }

        const bool ok = duranteLaCuenta > 0.05f && compasDurante < 0 && compasDespues >= 0;
        std::printf ("%-34s clic %.5f   armada %d   compas durante %d   despues %d   %s\n",
                     "la cuenta atras suena y no avanza", duranteLaCuenta, (int) armada,
                     compasDurante, compasDespues, ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  Y EL METRONOMO NO VIAJA AL REBOTE.
    //
    //  El motor de exportacion se construye con copyStateFrom, y ahi el clic no
    //  esta. Se comprueba porque «no esta» es exactamente la clase de cosa que
    //  alguien anade sin querer el dia que copie un campo de mas - y el fallo
    //  solo se descubre cuando ya has mandado el fichero con un metronomo
    //  encima.
    {
        AudioEngine a;
        a.prepareToPlay (48000.0, 512);
        a.setClick (true);

        AudioEngine b;
        b.prepareToPlay (48000.0, 512);
        b.copyStateFrom (a);

        const bool ok = a.isClick() && ! b.isClick();
        std::printf ("%-34s origen %d   rebote %d   %s\n",
                     "el clic no sale en el rebote", (int) a.isClick(), (int) b.isClick(),
                     ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  EL ECUALIZADOR DE CINCO BANDAS, con TRES cifras y no una.
    //
    //  Es el piloto de «cada efecto trae su propia superficie», asi que lo que
    //  hay que probar no es solo que filtre: es que lo que se DIBUJA y lo que
    //  SUENA salgan de los mismos cinco numeros. Una curva que promete algo
    //  distinto de lo que hace es peor que no dibujarla.
    //
    //  1. PLANO ES PLANO, BIT A BIT. «Casi lo mismo, 0.1 dB» es justo lo que
    //     dejaria pasar un EQ que dice estar a cero y no lo esta - es la misma
    //     comparacion que ya se le hace al filtro del pad apagado y al orden
    //     del ancho estereo.
    //  2. LO QUE PIDE ES LO QUE DA: +12 dB en la banda del medio tienen que
    //     salir +12 medidos en la senal, no en la formula.
    //  3. Y LO DIBUJADO COINCIDE CON LO SONADO, que es la unica de las tres que
    //     no se puede deducir de las otras dos.
    {
        constexpr int kN = 1 << 14;
        constexpr double kFs = 48000.0;

        auto tono = [] (float* d, int n, double hz)
        {
            for (int i = 0; i < n; ++i)
                d[i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                 * hz * (double) i / kFs);
        };
        //  El nivel se mide sobre la SEGUNDA mitad: la primera lleva el
        //  transitorio de arranque del biquad, y promediarlo mide el ataque y
        //  no la respuesta.
        auto rms = [] (const float* d, int n)
        {
            double s = 0.0;
            for (int i = n / 2; i < n; ++i) s += (double) d[i] * d[i];
            return std::sqrt (s / (double) (n - n / 2));
        };

        std::vector<float> seco (kN), hum (kN);
        float* canal[2] = { hum.data(), nullptr };

        //  1. Plano.
        Eq5 eq;
        eq.prepare (kFs);
        tono (seco.data(), kN, 1000.0);
        hum = seco;
        eq.procesa (canal, 1, 0, kN);
        int distintas = 0;
        for (int i = 0; i < kN; ++i) if (hum[(size_t) i] != seco[(size_t) i]) ++distintas;

        //  2. +12 dB en la banda 2, que es la del medio (1 kHz de fabrica).
        Eq5 eq2;
        eq2.prepare (kFs);
        eq2.ponBanda (2, 1000.0f, 12.0f);
        hum = seco;
        eq2.procesa (canal, 1, 0, kN);
        const double subida = 20.0 * std::log10 (rms (hum.data(), kN)
                                                 / juce::jmax (1.0e-12, rms (seco.data(), kN)));

        //  3. Lo dibujado contra lo sonado, en las cinco bandas y con ganancias
        //     DISTINTAS entre si: con las cinco iguales, un cruce de bandas
        //     dentro de la curva pasaria desapercibido.
        Eq5 eq3;
        eq3.prepare (kFs);
        const float pedido[Eq5::kBands] = { -9.0f, 6.0f, -12.0f, 9.0f, 4.0f };
        for (int b = 0; b < Eq5::kBands; ++b)
            eq3.ponBanda (b, Eq5::kFreqDef[b], pedido[b]);

        double peorDesvio = 0.0;
        int    peorBanda  = -1;
        for (int b = 0; b < Eq5::kBands; ++b)
        {
            tono (seco.data(), kN, Eq5::kFreqDef[b]);
            hum = seco;
            eq3.procesa (canal, 1, 0, kN);
            const double medido  = 20.0 * std::log10 (rms (hum.data(), kN)
                                                      / juce::jmax (1.0e-12, rms (seco.data(), kN)));
            const double dibujado = eq3.respuestaEnDb (Eq5::kFreqDef[b]);
            const double d = std::abs (medido - dibujado);
            if (d > peorDesvio) { peorDesvio = d; peorBanda = b; }
        }

        //  Medio dB de margen: las bandas vecinas se solapan por diseno -Q de
        //  0.7- asi que en el centro de una tambien pesa lo que hacen las de al
        //  lado, y eso lo lleva la curva dibujada tal cual. Lo que se comprueba
        //  es que las dos cuentas digan lo MISMO, no que cada banda este sola.
        const bool ok = (distintas == 0) && std::abs (subida - 12.0) < 0.5
                        && peorDesvio < 0.5;
        std::printf ("%-34s plano %d muestras   +12 da %+.2f dB   dibujo vs sonido %.2f dB (banda %d)   %s\n",
                     "el EQ de cinco bandas", distintas, subida, peorDesvio, peorBanda,
                     ok ? "OK" : zatiFalla());
    }

    //  Y EL EQ DENTRO DEL MOTOR, que es la otra mitad y la que faltaba: lo de
    //  arriba mide la CLASE, y una clase perfecta a la que no llama nadie saca
    //  sobresaliente en las tres cifras mientras la app no ecualiza nada. Es
    //  el mismo agujero que ya tuvo el cabezal del piano -dibujado desde el
    //  primer dia y sin que nadie lo alimentara-.
    //
    //  Con DOS cifras, que una sola se engaña por los dos lados: con el envio
    //  CERRADO el pad tiene que salir BIT A BIT igual -«casi lo mismo» es justo
    //  lo que dejaria pasar un EQ que se cuela por el camino seco, y es la
    //  misma comparacion que ya se hace con el filtro del pad apagado- y con el
    //  envio abierto la banda de 1 kHz tiene que subir sus doce decibelios.
    {
        auto corre = [] (bool conEq, std::vector<float>& salida)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            if (conEq)
            {
                e.setEqMix (0, 1.0f);
                e.setCanalSend (0, 6, 1.0f);      // el EQ es el tipo 6
                e.setEqBand (0, 2, 1000.0f, 12.0f);
            }
            //  Un tono de 1 kHz, o sea justo el centro de la banda que se sube.
            e.publishSample (0, makeSample (48000.0, 0.40, 1000.0f));

            juce::AudioBuffer<float> b (2, 512);
            b.clear(); e.renderNextBlock (b, 0, 512);
            e.postNoteOn (0, 1.0f);

            salida.clear();
            for (int blk = 0; blk < 20; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, 512);
                //  Los cuatro primeros bloques fuera: el envio se suaviza en
                //  20 ms y ahi la ganancia todavia esta subiendo.
                if (blk < 4) continue;
                for (int i = 0; i < 512; ++i) salida.push_back (b.getSample (0, i));
            }
        };

        //  El RMS, escrito aqui porque el de la comprobacion de arriba vive
        //  dentro de su bloque: dos lineas duplicadas antes que sacar un
        //  ayudante de fichero para el segundo cliente de un banco.
        auto rmsDe = [] (const std::vector<float>& v)
        {
            double a = 0.0;
            for (float x : v) a += (double) x * (double) x;
            return std::sqrt (a / juce::jmax (1.0, (double) v.size()));
        };

        std::vector<float> seco, conEq, cerrado;
        corre (false, seco);
        corre (true,  conEq);
        corre (false, cerrado);

        int distintas = 0;
        for (size_t i = 0; i < seco.size() && i < cerrado.size(); ++i)
            if (seco[i] != cerrado[i]) ++distintas;

        const double rSeco = rmsDe (seco);
        const double rEq   = rmsDe (conEq);
        const double subida = 20.0 * std::log10 (rEq / juce::jmax (1.0e-12, rSeco));

        const bool ok = (distintas == 0) && std::abs (subida - 12.0) < 1.0;
        std::printf ("%-34s cerrado %d muestras cambian   abierto %+.2f dB en 1 kHz   %s\n",
                     "el EQ llega al bus", distintas, subida, ok ? "OK" : zatiFalla());
    }

    //  LA FAMILIA DE DINAMICA: CMP, GTE, DSS y LIM.
    //
    //  CADA UNA CON DOS CIFRAS Y NO UNA, que es lo unico que separa un efecto
    //  de un fader: la primera dice que hace algo y la segunda que hace SOLO
    //  lo que dice. Es la misma regla que ya costo una medida en QUITAR RUIDO
    //  -cuanto baja el suelo Y cuanto sobrevive el tono- y en el ancho estereo
    //  -cuanto lado queda Y cuanto centro-.
    //
    //  Se mide por el MOTOR y no construyendo un `Dinamica` suelto: una clase
    //  perfecta a la que no llama nadie saca sobresaliente mientras la app no
    //  comprime nada. Es el agujero que ya tuvo el EQ y antes el cabezal del
    //  piano.
    {
        auto rmsDe = [] (const std::vector<float>& v, size_t desde, size_t hasta)
        {
            double a = 0.0; size_t n = 0;
            for (size_t i = desde; i < hasta && i < v.size(); ++i)
            { a += (double) v[i] * (double) v[i]; ++n; }
            return std::sqrt (a / juce::jmax ((size_t) 1, n));
        };
        auto picoDe = [] (const std::vector<float>& v, size_t desde, size_t hasta)
        {
            float p = 0.0f;
            for (size_t i = desde; i < hasta && i < v.size(); ++i)
                p = juce::jmax (p, std::abs (v[i]));
            return p;
        };

        //  UN TONO PLANO Y SIN RUIDO, que `makeSample` no da: el suyo DECAE y
        //  lleva un 20 % de ruido encima. Con envolvente, «cuanto baja» seria
        //  la caida del sonido y no la del compresor, y con ruido el detector
        //  de la puerta veria el ruido y no el tono - las dos formas de que
        //  esta medida diga que si sin haber mirado nada.
        auto tonoPlano = [] (double sr, double seg, float hz, float amp)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * seg);
            sb->buffer.setSize (2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i,
                        amp * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) sr));
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  Un motor con UN pad enrutado al efecto que toque, y la salida
        //  recogida entera. `nivel` es la amplitud del tono que se carga.
        auto corre = [&tonoPlano] (int fx, float nivel, float hz, float p0, float p1,
                                   std::vector<float>& salida, int bloques = 40)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            if (fx >= 0)
            {
                e.setFxParam (0, fx, 0, p0);
                e.setFxParam (0, fx, 1, p1);
                e.setFxParam (0, fx, 2, 1.0f);          // MIX al maximo
                e.setCanalSend (0, fx, 1.0f);
            }
            e.publishSample (0, tonoPlano (48000.0, 1.20, hz, nivel));

            juce::AudioBuffer<float> b (2, 512);
            //  EL ENVIO SE ASIENTA ANTES DE DISPARAR, y no se descartan los
            //  primeros bloques despues. El envio se cruza con el camino seco
            //  en 20 ms, asi que saltarse cuatro bloques deja fuera justo lo
            //  que un limitador existe para atrapar: EL ATAQUE de la nota.
            //  Medido con los cuatro bloques descartados, el pico salia a
            //  0.5271 con el techo en 0.5012 y no era el limitador — era el
            //  12 % de señal seca que quedaba en el bloque cuatro.
            for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, 512); }
            e.postNoteOn (0, 1.0f);

            salida.clear();
            for (int blk = 0; blk < bloques; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, 512);
                for (int i = 0; i < 512; ++i) salida.push_back (b.getSample (0, i));
            }
        };

        //  1. CMP. Una rafaga POR ENCIMA del umbral tiene que bajar, y una POR
        //     DEBAJO tiene que salir BIT A BIT igual: «comprime» lo cumple
        //     igual un fader, y solo la segunda cifra dice que el umbral existe.
        //
        //     Y LA SEGUNDA SE COMPARA CONTRA EL MISMO CAMINO, que es donde
        //     esta medida se equivoco antes de acertar: la primera version
        //     comparaba contra el pad SIN enrutar y saco 13852 muestras
        //     distintas con el compresor sin tocar una sola. No era el
        //     compresor — CMP es un INSERTO, asi que enrutarlo saca el pad del
        //     camino seco y lo mete por el bus, y esos dos caminos no son bit
        //     a bit el mismo pase lo que pase dentro. Lo que se compara es el
        //     MISMO envio con el umbral en 0 dB, o sea con el compresor puesto
        //     y sin nada que comprimir. Primero se duda de la prueba.
        {
            std::vector<float> fuerteSin, fuerteCon, flojoSin, flojoCon;
            corre (-1,                 0.80f, 220.0f, 0, 0, fuerteSin);
            corre (AudioEngine::kFxCmp, 0.80f, 220.0f, -24.0f, 8.0f, fuerteCon);
            corre (AudioEngine::kFxCmp, 0.02f, 220.0f,   0.0f, 8.0f, flojoSin);
            corre (AudioEngine::kFxCmp, 0.02f, 220.0f, -24.0f, 8.0f, flojoCon);

            const double baja = 20.0 * std::log10 (rmsDe (fuerteCon, 4096, 12288)
                                                   / juce::jmax (1.0e-12, rmsDe (fuerteSin, 4096, 12288)));
            int distintas = 0;
            for (size_t i = 0; i < flojoSin.size() && i < flojoCon.size(); ++i)
                if (flojoSin[i] != flojoCon[i]) ++distintas;

            const bool ok = (baja < -6.0) && (distintas == 0);
            std::printf ("%-34s fuerte %+.2f dB   flojo %d muestras cambian   %s\n",
                         "CMP", baja, distintas, ok ? "OK" : zatiFalla());
        }

        //  2. GTE. Es el par de QUITAR RUIDO otra vez: el siseo por debajo del
        //     umbral cae y el tono por encima sobrevive INTACTO. Solo lo
        //     primero lo cumple un silenciador y solo lo segundo, no hacer nada.
        {
            std::vector<float> siseoSin, siseoCon, tonoSin, tonoCon;
            corre (-1,                 0.010f, 220.0f, 0, 0, siseoSin);
            corre (AudioEngine::kFxGte, 0.010f, 220.0f, -30.0f, 40.0f, siseoCon);
            corre (-1,                 0.500f, 220.0f, 0, 0, tonoSin);
            corre (AudioEngine::kFxGte, 0.500f, 220.0f, -30.0f, 40.0f, tonoCon);

            //  Con suelo: la puerta cierra del todo y `log10(0)` es -inf, que
            //  se lee como un error y no como «silencio».
            const double fuera = 20.0 * std::log10 (juce::jmax (1.0e-6, rmsDe (siseoCon, 4096, 12288))
                                                    / juce::jmax (1.0e-12, rmsDe (siseoSin, 4096, 12288)));
            const double queda = 20.0 * std::log10 (rmsDe (tonoCon, 4096, 12288)
                                                    / juce::jmax (1.0e-12, rmsDe (tonoSin, 4096, 12288)));
            const bool ok = (fuera < -20.0) && (std::abs (queda) < 1.0);
            std::printf ("%-34s bajo %+.2f dB   alto %+.2f dB   %s\n",
                         "GTE", fuera, queda, ok ? "OK" : zatiFalla());
        }

        //  3. DSS. Un de-esser que baja la señal entera es un compresor con el
        //     detector torcido: baja tambien la voz. Asi que se miden DOS
        //     tonos, uno en la banda de sibilancia y otro en el fundamental, y
        //     el segundo NO se puede mover.
        {
            std::vector<float> siSin, siCon, bajoSin, bajoCon;
            //  A 9 kHz y no a 7: el paso alto de dos polos esta en 6 kHz y
            //  ahi mismo deja pasar la mitad, o sea que a 7 la ese que llega
            //  al detector es doce decibelios mas floja de lo que parece. Una
            //  sibilancia de verdad vive entre 6 y 10 kHz.
            corre (AudioEngine::kFxDss, 0.60f, 9000.0f, 6000.0f, 0.0f, siSin);
            corre (AudioEngine::kFxDss, 0.60f, 9000.0f, 6000.0f, 1.0f, siCon);
            corre (AudioEngine::kFxDss, 0.60f,  220.0f, 6000.0f, 0.0f, bajoSin);
            corre (AudioEngine::kFxDss, 0.60f,  220.0f, 6000.0f, 1.0f, bajoCon);

            const double sib = 20.0 * std::log10 (rmsDe (siCon, 4096, 12288)
                                                  / juce::jmax (1.0e-12, rmsDe (siSin, 4096, 12288)));
            const double fund = 20.0 * std::log10 (rmsDe (bajoCon, 4096, 12288)
                                                   / juce::jmax (1.0e-12, rmsDe (bajoSin, 4096, 12288)));
            const bool ok = (sib < -3.0) && (std::abs (fund) < 1.0);
            std::printf ("%-34s sibilancia %+.2f dB   fundamental %+.2f dB   %s\n",
                         "DSS", sib, fund, ok ? "OK" : zatiFalla());
        }

        //  4. LIM. El pico NUNCA pasa del techo, y el RMS sobrevive: solo lo
        //     primero lo cumple un fader que baja diez decibelios, y solo lo
        //     segundo lo cumple no hacer nada.
        {
            std::vector<float> sin_, con;
            corre (AudioEngine::kFxLim, 0.90f, 220.0f,  0.0f, 60.0f, sin_);
            corre (AudioEngine::kFxLim, 0.90f, 220.0f, -6.0f, 60.0f, con);

            const float techo = juce::Decibels::decibelsToGain (-6.0f);
            //  Desde la PRIMERA muestra de la nota: el ataque es el caso.
            const float pico  = picoDe (con, 0, con.size());
            const double queda = 20.0 * std::log10 (rmsDe (con, 8192, 16384)
                                                    / juce::jmax (1.0e-12, rmsDe (sin_, 8192, 16384)));
            //  Sin margen a la baja y con un 1 % arriba, que es el redondeo
            //  de un float: el limitador sujeta en la muestra en la que pasa,
            //  asi que un pico por encima del techo es un limitador que no
            //  limita. Con el ataque puesto tambien en la bajada salia 0.5403
            //  contra un techo de 0.5012.
            const bool ok = (pico <= techo * 1.01f) && (queda > -8.0);
            std::printf ("%-34s pico %.4f (techo %.4f)   RMS %+.2f dB   %s\n",
                         "LIM", pico, techo, queda, ok ? "OK" : zatiFalla());
        }
    }

    //  ========================================================================
    //  LO QUE EL VISOR DIBUJA CONTRA LO QUE EL EFECTO HACE.
    //
    //  La pregunta llego asi: «los visuales y graficos de los efectos son
    //  imagenes no? no son reales que digamos». Imagenes no son -los once se
    //  dibujan con `juce::Path` a partir de los mandos vivos, y `Tests/rack.py`
    //  ya lo mide-, pero «no es un icono» tampoco es «dice la verdad»: hasta
    //  hoy la unica comprobacion que comparaba dibujo contra sonido era la del
    //  EQ, y encima sobre un `Eq5` suelto. Los otros diez eran una AFIRMACION
    //  SIN MEDIDA, que es el patron que esta casa lleva trece veces pagando.
    //
    //  Se mide contra el MOTOR y no contra la clase, por lo mismo que la fila
    //  «el EQ llega al bus»: una formula perfecta a la que no llama nadie saca
    //  sobresaliente mientras la app dibuja otra cosa.
    //
    //  Y CON DOS CIFRAS CADA UNA: el desvio MEDIO dice que la forma es la
    //  misma, y el PEOR PUNTO que no lo es sólo de media. Una curva que acierta
    //  en cuarenta y siete columnas y se va veinte decibelios en una es
    //  exactamente el dibujo que hace creer que un filtro corta donde no corta.
    //  ========================================================================
    {
        constexpr double kFs = 48000.0;
        constexpr int    kBs = 512;

        //  Un seno LIMPIO: `makeSample` lleva un 20 % de ruido y una fase al
        //  azar, que para medir carga estan bien y para medir una respuesta
        //  son el error. Sin caida, que lo que se mide es el regimen.
        auto seno = [] (float hz, float amp)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (kFs * 0.5);
            sb->buffer.setSize (2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i, amp * std::sin (juce::MathConstants<float>::twoPi
                                                                * hz * (float) i / (float) kFs));
            sb->sourceSampleRate = kFs;
            return SampleBuffer::Ptr (sb);
        };

        //  UNA CORRIDA POR EL MOTOR, con el efecto enrutado o sin el.
        //
        //  Los seis primeros bloques fuera: el envio se suaviza en 20 ms y ahi
        //  la ganancia todavia esta subiendo — es la misma razon por la que la
        //  fila del EQ tira los cuatro primeros, con dos mas de margen porque
        //  aqui hay filtros que ademas arrancan.
        auto corre = [&seno] (int fx, float p0, float p1, float hz, float amp,
                              std::vector<float>& out, int bloques = 24)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setPadAttack (0, 0.0f);
            if (fx >= 0)
            {
                e.setFxParam (0, fx, 0, p0);
                e.setFxParam (0, fx, 1, p1);
                e.setFxParam (0, fx, 2, 1.0f);
                if (fx == AudioEngine::kFxEq) e.setEqMix (0, 1.0f);
                e.setCanalSend (0, fx, 1.0f);
            }
            e.publishSample (0, seno (hz, amp));

            juce::AudioBuffer<float> b (2, kBs);
            b.clear(); e.renderNextBlock (b, 0, kBs);
            e.postNoteOn (0, 1.0f);

            out.clear();
            for (int blk = 0; blk < bloques; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                if (blk < 6) continue;
                for (int i = 0; i < kBs; ++i) out.push_back (b.getSample (0, i));
            }
        };

        auto rms = [] (const std::vector<float>& v)
        {
            double a = 0.0;
            for (float x : v) a += (double) x * (double) x;
            return std::sqrt (a / juce::jmax (1.0, (double) v.size()));
        };
        auto pico = [] (const std::vector<float>& v)
        {
            float m = 0.0f;
            for (float x : v) m = juce::jmax (m, std::abs (x));
            return m;
        };

        //  Los ejes del visor, escritos al REVES: la curva viene en 0..1 y
        //  aqui hace falta lo que ese 0..1 significa. Son los mismos tres de
        //  `FxVisor::muestrea` y se escriben aqui a proposito -el banco tiene
        //  que poder equivocarse por su cuenta y que la diferencia se vea-.
        auto hzDe    = [] (int i) { const float t = (float) i / (float) (FxVisor::kPuntos - 1);
                                    const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
                                    return std::exp (lo + t * (hi - lo)); };
        auto dbDeAlto = [] (float y) { return y * 36.0f - 24.0f; };      // -24..+12
        auto nivelDe  = [] (int i) { const float t = (float) i / (float) (FxVisor::kPuntos - 1);
                                     return -60.0f + 60.0f * t; };      // dB de entrada
        auto dbDeY    = [] (float y) { return y * 60.0f - 60.0f; };     // salida en dB

        //  Diecisiete columnas de las cuarenta y ocho, repartidas: medir las
        //  cuarenta y ocho son cuarenta y ocho arranques de motor por efecto y
        //  no dice nada mas — una curva que se sale lo hace en una banda, no en
        //  una columna suelta.
        constexpr int kMuestras = 17;
        auto columna = [] (int k) { return k * (FxVisor::kPuntos - 1) / (kMuestras - 1); };

        auto fila = [] (const char* nombre, double medio, double peor,
                        double tope, const char* unidad)
        {
            const bool ok = (peor <= tope);
            std::printf ("%-34s dibujo vs motor  medio %.2f %s  peor %.2f %s (tope %.2f)   %s\n",
                         nombre, medio, unidad, peor, unidad, tope, ok ? "OK" : zatiFalla());
        };

        //  ------------------------------------------------------------------
        //  1. RESPUESTA EN FRECUENCIA: FLT y HPF.
        //
        //  Se compara contra la corrida SECA en la misma frecuencia, o sea en
        //  ganancia relativa: asi la envolvente del pad, su ganancia y el
        //  suavizado del envio se van en la division y lo que queda es el
        //  filtro. El tope es 1.5 dB — el envio tarda 20 ms en subir y un SVF
        //  con resonancia tiene su propio transitorio.
        //  ------------------------------------------------------------------
        {
            struct Caso { int fx; const char* nombre; float p0, p1; };
            const Caso casos[] = {
                { AudioEngine::kFxFlt, "FLT: dibujo contra el filtro", -0.55f, 1.6f },
                { AudioEngine::kFxHpf, "HPF: dibujo contra el filtro", 800.0f, 2.2f },
                //  WAH CON SENS A CERO, que no es aflojar la medida sino donde
                //  la pregunta tiene respuesta: con la envolvente fuera del
                //  juego el centro ES la base, asi que la banda esta quieta y el
                //  dibujo -que es el RECORRIDO, o sea el maximo de la banda en
                //  reposo y abierta- se reduce a esa misma banda. Con SENS
                //  puesto, el dibujo es la union de dos posiciones y la medida
                //  seria la de una: dos cosas distintas y ninguna mal.
                { AudioEngine::kFxWah, "WAH: dibujo contra la banda", 0.0f, 600.0f },
            };

            for (const auto& c : casos)
            {
                FxVisor::Curva curva {};
                FxVisor::muestrea (c.fx, c.p0, c.p1, curva);

                double suma = 0.0, peor = 0.0;
                int    n = 0;
                for (int k = 0; k < kMuestras; ++k)
                {
                    const int   i  = columna (k);
                    const float hz = hzDe (i);
                    //  Por encima de 15 kHz la muestra de medio segundo y el
                    //  bloque de 512 dejan pocos ciclos por ventana y el RMS
                    //  empieza a medir el borde; la banda util de esta medida
                    //  es la que el oido usa.
                    if (hz > 15000.0f) continue;

                    std::vector<float> seco, mojado;
                    corre (-1,   0.0f, 0.0f, hz, 0.30f, seco);
                    corre (c.fx, c.p0, c.p1, hz, 0.30f, mojado);

                    const double medido  = 20.0 * std::log10 (juce::jmax (1.0e-9, rms (mojado))
                                                              / juce::jmax (1.0e-9, rms (seco)));
                    //  El dibujo se acota en -24 dB porque la tira acaba ahi:
                    //  comparar mas abajo seria medir el borde del dibujo y no
                    //  el filtro.
                    const double dibujado = dbDeAlto (curva[(size_t) i]);
                    if (dibujado <= -23.5) continue;

                    const double d = std::abs (medido - dibujado);
                    suma += d; peor = juce::jmax (peor, d); ++n;
                }
                fila (c.nombre, suma / juce::jmax (1, n), peor, 1.5, "dB");
            }
        }

        //  ------------------------------------------------------------------
        //  2. TRANSFERENCIA: DRV, CMP, GTE, DSS y LIM.
        //
        //  El eje X de estos cinco es el NIVEL que entra, asi que se barre la
        //  amplitud y se mide lo que sale. Y se mide el PICO y no el RMS: una
        //  transferencia es punto a punto, y el RMS de un seno saturado
        //  mezclaria la forma de onda con el nivel.
        //  ------------------------------------------------------------------
        {
            //  Y CADA UNO CON EL TONO QUE LO CRUZA, que es donde esta prueba
            //  se equivoco primero: el de-esser parte la banda en un cruce
            //  Linkwitz-Riley y solo comprime la MITAD ALTA, asi que medirlo
            //  con los 220 Hz de los demas es medir un efecto que no toca la
            //  señal — 22.53 dB de desvio con el dibujo perfecto. Primero se
            //  duda de la prueba.
            struct Caso { int fx; const char* nombre; float p0, p1; bool enDb; double tope; float hz; };
            const Caso casos[] = {
                //  DRV lleva su eje en amplitud -1..+1 y no en dB.
                { AudioEngine::kFxDrv, "DRV: dibujo contra el saturador", 0.55f,  8000.0f, false, 0.08,  220.0f },
                { AudioEngine::kFxCmp, "CMP: dibujo contra el compresor", -18.0f, 4.0f,    true,  2.0,   220.0f },
                { AudioEngine::kFxGte, "GTE: dibujo contra la puerta",    -30.0f, 120.0f,  true,  2.0,   220.0f },
                { AudioEngine::kFxDss, "DSS: dibujo contra el de-esser",  6000.0f, 0.7f,   true,  2.5, 14000.0f },
                { AudioEngine::kFxLim, "LIM: dibujo contra el limitador", -6.0f,  60.0f,   true,  2.0,   220.0f },
            };

            for (const auto& c : casos)
            {
                FxVisor::Curva curva {};
                FxVisor::muestrea (c.fx, c.p0, c.p1, curva);

                double suma = 0.0, peor = 0.0;
                int    n = 0;
                for (int k = 0; k < kMuestras; ++k)
                {
                    //  DONDE CAE ESA AMPLITUD EN EL DIBUJO. En los cuatro que
                    //  miden en dB la columna ES el nivel; en DRV el eje va de
                    //  -1 a +1, asi que una amplitud `a` no esta en la columna
                    //  `a` sino en la `(1+a)/2`. Compararlas sin esto da 0.85
                    //  de desvio con el saturador dibujado perfecto.
                    const float objetivo = c.enDb
                                             ? juce::Decibels::decibelsToGain (nivelDe (columna (k)))
                                             : (float) k / (float) (kMuestras - 1);
                    if (objetivo < 1.0e-4f) continue;

                    const int i = c.enDb
                                    ? columna (k)
                                    : juce::jlimit (0, FxVisor::kPuntos - 1,
                                                    (int) std::round ((1.0f + objetivo) * 0.5f
                                                                      * (float) (FxVisor::kPuntos - 1)));

                    std::vector<float> seco;
                    corre (-1, 0.0f, 0.0f, c.hz, objetivo, seco);
                    const float entra = pico (seco);
                    if (entra < 1.0e-5f) continue;
                    const float ajuste = objetivo / entra;

                    std::vector<float> mojado;
                    corre (c.fx, c.p0, c.p1, c.hz, objetivo * ajuste, mojado);
                    const float sale = pico (mojado);

                    double medido, dibujado;
                    if (c.enDb)
                    {
                        medido   = juce::Decibels::gainToDecibels (juce::jmax (1.0e-6f, sale));
                        dibujado = dbDeY (curva[(size_t) i]);
                        //  Por debajo del suelo del eje la curva esta pegada al
                        //  renglon y no dice nada.
                        if (dibujado <= -59.0) continue;
                    }
                    else
                    {
                        medido   = sale;
                        dibujado = curva[(size_t) i] * 2.0f - 1.0f;
                    }

                    const double d = std::abs (medido - dibujado);
                    suma += d; peor = juce::jmax (peor, d); ++n;
                }
                fila (c.nombre, suma / juce::jmax (1, n), peor, c.tope, c.enDb ? "dB" : "  ");
            }
        }
        //  ------------------------------------------------------------------
        //  3. TIEMPO: DLY y REV.
        //
        //  Estos dos no tienen respuesta en frecuencia ni transferencia: lo que
        //  dibujan es lo que LLEGA en cada instante, asi que se mide con un
        //  impulso y se compara la envolvente. Y AQUI NO SE TIRAN BLOQUES: en
        //  las dos familias de arriba los seis primeros sobran porque el envio
        //  esta subiendo, y aqui tirarlos correria el eje de tiempo 64 ms — o
        //  sea mediria los ecos en el sitio equivocado.
        //  ------------------------------------------------------------------
        {
            //  EL IMPULSO NO PUEDE IR EN LA MUESTRA 0, que es donde esta
            //  prueba se equivoco y de la forma mas cara: daba numeros.
            //
            //  `Voice::start` hace `winStart = jlimit (1, srcLen - 3, ...)`, o
            //  sea que la muestra CERO es la unica que una voz no lee nunca —
            //  la interpolacion necesita un dato por detras—. Con el clic
            //  entero ahi, el pad sonaba EXACTAMENTE a cero y las dos filas
            //  sacaban 0.36 y 67.51 dB de desvio contra un dibujo perfecto:
            //  midiendo silencio, no ecos. Se arranca en la 1 y se le dan ocho
            //  muestras -0.17 ms contra un eco de 250 y una cola de dos
            //  segundos, o sea un impulso igual- para que el anti-alias no se
            //  lo coma tampoco.
            auto click = [] ()
            {
                auto* sb = new SampleBuffer();
                const int n = 64;
                sb->buffer.setSize (2, n);
                sb->buffer.clear();
                for (int c = 0; c < 2; ++c)
                    for (int i = 1; i <= 8; ++i) sb->buffer.setSample (c, i, 0.9f);
                sb->sourceSampleRate = kFs;
                return SampleBuffer::Ptr (sb);
            };

            auto correImpulso = [&click] (int fx, float p0, float p1,
                                          std::vector<float>& out, double segs,
                                          bool abierto = true)
            {
                AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
                e.setPadGain (0, 1.0f);
                e.setPadAttack (0, 0.0f);
                if (abierto)
                {
                    e.setFxParam (0, fx, 0, p0);
                    e.setFxParam (0, fx, 1, p1);
                    e.setFxParam (0, fx, 2, 1.0f);
                    e.setCanalSend (0, fx, 1.0f);
                }
                e.publishSample (0, click());

                juce::AudioBuffer<float> b (2, kBs);
                //  EL ENVIO SE ASIENTA ANTES DE DISPARAR, que es la leccion
                //  que `Tests/dinamica.py` ya escribio para el limitador: el
                //  envio sube con una constante de 20 ms y un impulso dura
                //  1.3 ms, asi que disparando en el primer bloque el bus
                //  recibe el 6 % de lo que el fader dice.
                for (int k = 0; k < 8; ++k) { b.clear(); e.renderNextBlock (b, 0, kBs); }
                e.postNoteOn (0, 1.0f);

                out.clear();
                const int bloques = (int) (segs * kFs / kBs) + 2;
                for (int blk = 0; blk < bloques; ++blk)
                {
                    b.clear(); e.renderNextBlock (b, 0, kBs);
                    for (int i = 0; i < kBs; ++i) out.push_back (b.getSample (0, i));
                }
            };

            //  Y LO QUE SE COMPARA ES LO QUE EL EFECTO AÑADE.
            //
            //  DLY y REV son los dos que SUMAN, asi que su bus sale con el
            //  camino seco delante — y el seco de un impulso es el pico mas
            //  alto de la ventana. Normalizando por ese pico, la cola entera se
            //  hunde: la primera version saco 67.51 dB de desvio medio en REV
            //  con el dibujo perfecto, midiendo el clic y no la reverb. Se
            //  resta la corrida con el envio CERRADO, que es determinista y da
            //  exactamente lo que el efecto pone.
            auto soloMojado = [&correImpulso] (int fx, float p0, float p1,
                                               std::vector<float>& out, double segs)
            {
                std::vector<float> abierto, cerrado;
                correImpulso (fx, p0, p1, abierto, segs, true);
                correImpulso (fx, p0, p1, cerrado, segs, false);
                out.clear();
                for (size_t i = 0; i < abierto.size(); ++i)
                    out.push_back (abierto[i] - (i < cerrado.size() ? cerrado[i] : 0.0f));
            };

            //  La envolvente en las mismas cuarenta y ocho columnas que el
            //  visor, y normalizada a su maximo: lo que se compara es la FORMA
            //  de la cola, no el nivel — el nivel lo pone el fader.
            auto envolvente = [] (const std::vector<float>& v, bool porPico)
            {
                std::array<double, FxVisor::kPuntos> e {};
                const double ancho = (double) v.size() / (double) FxVisor::kPuntos;
                for (int i = 0; i < FxVisor::kPuntos; ++i)
                {
                    const size_t a = (size_t) (i * ancho), b = (size_t) ((i + 1) * ancho);
                    double acc = 0.0; int n = 0;
                    for (size_t j = a; j < b && j < v.size(); ++j, ++n)
                        acc = porPico ? juce::jmax (acc, (double) std::abs (v[j]))
                                      : acc + (double) v[j] * v[j];
                    e[(size_t) i] = porPico ? acc : std::sqrt (acc / juce::jmax (1, n));
                }
                double m = 0.0;
                for (double x : e) m = juce::jmax (m, x);
                if (m > 1.0e-9) for (double& x : e) x /= m;
                return e;
            };

            //  DLY: los ecos, donde caen y con que amplitud. Se compara SOLO
            //  donde el dibujo pone un eco -las columnas vacias son el 90 % de
            //  la ventana y compararlas seria medir el silencio-, y con la
            //  columna de al lado admitida: una ventana de dos segundos en
            //  cuarenta y ocho columnas son 42 ms cada una, asi que un eco no
            //  cae nunca en el centro de la suya.
            {
                const float ms = 250.0f, fbk = 0.55f;
                FxVisor::Curva curva {};
                FxVisor::muestrea (AudioEngine::kFxDly, ms, fbk, curva);

                std::vector<float> v;
                soloMojado (AudioEngine::kFxDly, ms, fbk, v, FxVisor::kVentanaMs * 0.001);
                const auto env = envolvente (v, true);
                double suma = 0.0, peor = 0.0; int n = 0;
                for (int i = 0; i < FxVisor::kPuntos; ++i)
                {
                    if (curva[(size_t) i] < 0.05f) continue;
                    double m = 0.0;
                    for (int d = -1; d <= 1; ++d)
                        if (i + d >= 0 && i + d < FxVisor::kPuntos)
                            m = juce::jmax (m, env[(size_t) (i + d)]);
                    const double dif = std::abs (m - (double) curva[(size_t) i]);
                    suma += dif; peor = juce::jmax (peor, dif); ++n;
                }
                fila ("DLY: dibujo contra los ecos", suma / juce::jmax (1, n), peor, 0.12, "  ");
            }

            //  REV: la cola. En dB y por RMS, que la salida de una rejilla de
            //  realimentacion es ruido y su pico brinca de bin a bin.
            {
                const float size = 0.70f, damp = 0.35f;
                FxVisor::Curva curva {};
                FxVisor::muestrea (AudioEngine::kFxRev, size, damp, curva);

                std::vector<float> v;
                soloMojado (AudioEngine::kFxRev, size, damp, v, FxVisor::kVentanaMs * 0.001);
                const auto env = envolvente (v, false);
                double suma = 0.0, peor = 0.0; int n = 0;
                for (int i = 1; i < FxVisor::kPuntos; ++i)
                {
                    const double dib = 20.0 * std::log10 (juce::jmax (1.0e-4, (double) curva[(size_t) i]));
                    if (dib < -24.0) continue;                     // el resto es el suelo del dibujo
                    const double med = 20.0 * std::log10 (juce::jmax (1.0e-4, env[(size_t) i]));
                    const double dif = std::abs (med - dib);
                    suma += dif; peor = juce::jmax (peor, dif); ++n;
                }
                fila ("REV: dibujo contra la cola", suma / juce::jmax (1, n), peor, 4.0, "dB");
            }
        }

        //  ------------------------------------------------------------------
        //  4. BIT, que es el unico cuyo dibujo es una ONDA.
        //
        //  Y por eso se juzga con lo que define un crusher y no con un desvio
        //  punto a punto: CUANTOS NIVELES distintos hay -eso es BITS- y
        //  CUANTOS CAMBIOS de valor por ventana -eso es RATE-. Las dos cifras,
        //  porque cada una sola la cumple la mitad del efecto: contar niveles
        //  pasa con el retenedor apagado, y contar cambios pasa sin cuantizar.
        //  Y asi no hace falta alinear la fase, que es lo que hace fragil
        //  comparar dos ondas muestra a muestra.
        //  ------------------------------------------------------------------
        {
            const float bits = 4.0f, rate = 8.0f;
            FxVisor::Curva curva {};
            FxVisor::muestrea (AudioEngine::kFxBit, bits, rate, curva);

            //  Un tono cuyo periodo son exactamente las muestras de la ventana
            //  del visor, o sea el mismo ciclo que se dibuja.
            const float hz = (float) kFs / (float) FxVisor::kVentanaBit;
            std::vector<float> v;
            corre (AudioEngine::kFxBit, bits, rate, hz, 0.60f, v);

            //  EL PASO Y NO LA CUENTA DE NIVELES, que es donde esta prueba se
            //  equivoco: cuantizar es ABSOLUTO -los escalones estan en k/N
            //  pase lo que pase- asi que cuantos niveles VISITA una onda
            //  depende de su amplitud, y al bus no llega la misma que el visor
            //  dibuja. Salieron 11 contra 7 con el dibujo correcto. El PASO
            //  entre escalones si es el mismo, y su inverso es exactamente lo
            //  que BITS pone.
            auto cuenta = [] (const std::vector<float>& x)
            {
                std::vector<float> vals;
                int cambios = 0;
                for (size_t i = 0; i < x.size(); ++i)
                {
                    if (i > 0 && std::abs (x[i] - x[i - 1]) > 1.0e-5f) ++cambios;
                    bool visto = false;
                    for (float u : vals) if (std::abs (u - x[i]) < 1.0e-5f) { visto = true; break; }
                    if (! visto) vals.push_back (x[i]);
                }
                std::sort (vals.begin(), vals.end());
                double paso = 1.0e9;
                for (size_t i = 1; i < vals.size(); ++i)
                    paso = juce::jmin (paso, (double) (vals[i] - vals[i - 1]));
                return std::pair<double,int> (paso > 1.0e8 ? 0.0 : paso, cambios);
            };

            //  Del dibujo: sus cuarenta y ocho columnas son un submuestreo de
            //  la ventana. Se pasa a -1..+1, que es donde vive el escalon.
            std::vector<float> dib;
            for (float y : curva) dib.push_back (y * 2.0f - 1.0f);
            const auto d = cuenta (dib);

            //  Del motor: el MISMO submuestreo sobre un ciclo del regimen, ya
            //  lejos del arranque del envio.
            std::vector<float> mues;
            const int base = (int) v.size() - FxVisor::kVentanaBit * 2;
            for (int i = 0; i < FxVisor::kPuntos; ++i)
            {
                const int n = (int) std::round ((float) i * (float) (FxVisor::kVentanaBit - 1)
                                                / (float) (FxVisor::kPuntos - 1));
                mues.push_back (v[(size_t) (base + n)]);
            }
            const auto m = cuenta (mues);

            //  Dos cifras y no una: el PASO es BITS y los CAMBIOS son RATE.
            //  Cada una sola la cumple media maquina — contar el paso pasa con
            //  el retenedor apagado, y contar cambios pasa sin cuantizar.
            const double relacion = (m.first > 1.0e-9 && d.first > 1.0e-9)
                                      ? m.first / d.first : 0.0;
            const bool ok = std::abs (relacion - 1.0) < 0.10
                            && std::abs (d.second - m.second) <= 2;
            std::printf ("%-34s paso x%.3f del dibujado   cambios %d contra %d   %s\n",
                         "BIT: dibujo contra el crusher", relacion, m.second, d.second,
                         ok ? "OK" : zatiFalla());
        }

        //  ------------------------------------------------------------------
        //  5. OCT, que es el otro cuyo dibujo es una ONDA.
        //
        //  Y se juzga por DONDE esta la energia y no punto a punto, por lo
        //  mismo que BIT: el dibujo sale normalizado a su pico y el bus no, asi
        //  que restar muestra a muestra mediria la normalizacion. Lo que define
        //  un octavador es la RELACION entre lo que pone una octava arriba y lo
        //  que pone una abajo, y esa cifra se puede sacar igual de las cuarenta
        //  y ocho columnas que de lo que sale del motor.
        //
        //  Con los dos mandos a media altura a proposito: con uno de los dos a
        //  cero la relacion se va al infinito por los dos lados y la
        //  comparacion diria que si sin mirar nada.
        //  ------------------------------------------------------------------
        {
            const float arriba = 0.7f, abajo = 0.7f;
            FxVisor::Curva curva {};
            FxVisor::muestrea (AudioEngine::kFxOct, arriba, abajo, curva);

            //  La proyeccion sobre `ciclos` ciclos de la ventana, que es el
            //  mismo Goertzel de arriba escrito para un vector normalizado.
            auto proy = [] (const std::vector<float>& x, double ciclos)
            {
                double re = 0.0, im = 0.0;
                const double n = (double) x.size();
                for (size_t i = 0; i < x.size(); ++i)
                {
                    const double w = juce::MathConstants<double>::twoPi * ciclos * (double) i / n;
                    re += (double) x[i] * std::cos (w);
                    im += (double) x[i] * std::sin (w);
                }
                return 2.0 * std::sqrt (re * re + im * im) / n;
            };

            //  El dibujo: su ventana son DOS ciclos de la entrada -ver
            //  `FxVisor::muestrea`- asi que la octava alta cae en cuatro y la
            //  baja en uno. Se quita el renglon del medio, que es el cero.
            std::vector<float> dib;
            for (float y : curva) dib.push_back (y * 2.0f - 1.0f);
            const double dAlta = proy (dib, 4.0), dBaja = proy (dib, 1.0);

            //  El motor, con el mismo tono de 200 Hz que el visor simula.
            std::vector<float> v;
            corre (AudioEngine::kFxOct, arriba, abajo, 200.0f, 0.60f, v);
            //  CUATROCIENTAS OCHENTA MUESTRAS, que a 48 kHz y 200 Hz son los
            //  MISMOS dos ciclos de entrada que dibuja el visor. Y por eso los
            //  dos numeros son los mismos cuatro y uno: los ciclos de la
            //  ventana no dependen de cuantas muestras se tomen de ella.
            //
            //  Aqui es donde esta medida mintio: se escribieron cuarenta y diez
            //  -«diez veces mas muestras por ciclo»- y salio un desvio de
            //  **74.0 dB** con el dibujo correcto, porque se estaba proyectando
            //  sobre dos frecuencias que no existen en la señal. Primero se duda
            //  de la prueba.
            //  Y CON EL MISMO SUBMUESTREO QUE EL DIBUJO, que es lo que hace
            //  esta comparacion justa: una onda cuadrada tiene armonicos hasta
            //  arriba y a cuarenta y ocho puntos se pliegan, asi que medir el
            //  motor con las 480 muestras y el dibujo con 48 compara dos
            //  plegados distintos — salia 3.6 dB de desvio con las dos formas
            //  identicas. Es la misma decision que la fila de BIT.
            const size_t base = v.size() - (size_t) FxVisor::kPuntos * 10;
            std::vector<float> mues;
            for (int i = 0; i < FxVisor::kPuntos; ++i)
                mues.push_back (v[base + (size_t) i * 10]);
            const double mAlta = proy (mues, 4.0), mBaja = proy (mues, 1.0);

            const double dRel = 20.0 * std::log10 (juce::jmax (1.0e-9, dAlta)
                                                   / juce::jmax (1.0e-9, dBaja));
            const double mRel = 20.0 * std::log10 (juce::jmax (1.0e-9, mAlta)
                                                   / juce::jmax (1.0e-9, mBaja));
            const double d = std::abs (dRel - mRel);
            const bool ok = (d <= 4.0);
            std::printf ("%-34s arriba/abajo dibujado %+.1f dB  medido %+.1f dB  "
                         "desvio %.1f dB (tope 4.00)   %s\n",
                         "OCT: dibujo contra el octavador", dRel, mRel, d,
                         ok ? "OK" : zatiFalla());
        }
    }

    //  LA AUTOMATIZACION, con TRES cifras y no una.
    //
    //  «El parametro cambia» lo cumple igual una tabla que se aplica siempre y
    //  no en su paso, que es la forma exacta de que un barrido suene de golpe
    //  al empezar. Asi que se mira ANTES del evento -tiene que valer lo que se
    //  dejo-, DESPUES -tiene que valer lo escrito- y en un paso INTERMEDIO en
    //  el que no hay evento -tiene que seguir valiendo el anterior y no el
    //  siguiente-.
    //
    //  Se mide por el PARAMETRO y no por el sonido: lo que esta comprobacion
    //  vigila es que el evento llegue a `setFxParam` en el borde de paso; que
    //  ese parametro suene ya lo miden las once filas de efectos de arriba.
    {
        AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
 enCanalCero (e);
        e.setSongMode (true);
        e.setSongLength (4);

        //  DLY MIX -el parametro 11- en tres puntos de la cancion.
        //  Con el CANAL delante del valor: `EventoAuto` lo gana en el hueco de
        //  alineacion que ya tenia, y DLY es un envio -su canal se ignora- asi
        //  que esta fila mide lo mismo que media.
        AudioEngine::EventoAuto ev[3] =
        {
            {  0, 3, 2, 0, 0.10f },
            { 16, 3, 2, 0, 0.60f },
            { 32, 3, 2, 0, 0.90f },
        };
        e.publicaAutomacion (ev, 3);
        e.setDlyMix (0.0f);

        juce::AudioBuffer<float> b (2, 512);
        //  Hasta el paso EXACTO y no «hasta pasarlo»: la cancion da la vuelta
        //  al llegar al final, asi que un `>=` no sabe volver al 16 en la
        //  segunda pasada - que es justamente donde se comprueba que escribir
        //  apaga leer.
        auto rueda = [&] (int paso)
        {
            for (int i = 0; i < 8000; ++i)
            {
                b.clear(); e.renderNextBlock (b, 0, 512);
                if (e.pasoDeCancion() == paso) return;
            }
        };

        e.setPlaying (true);
        rueda (0);
        const float enCero = e.getDlyMix();
        rueda (8);                       // ningun evento aqui
        const float enOcho = e.getDlyMix();
        rueda (16);
        const float enDieciseis = e.getDlyMix();
        rueda (32);
        const float enTreintaYDos = e.getDlyMix();

        //  Y ESCRIBIR APAGA LEER: con el modo de escritura armado la tabla no
        //  se aplica, o si no el mando se moveria solo debajo del dedo.
        //
        //  Y SE VUELVE A PASAR POR UN PASO QUE TIENE EVENTO, que es donde la
        //  primera version de esta comprobacion no podia decir que no: se
        //  rodaba hasta el 48, donde no hay ninguno, asi que con la guarda
        //  quitada a proposito seguia saliendo verde. Una prueba que no cruza
        //  el sitio del fallo es una linea que imprime OK.
        e.setAutoEscribe (true);
        e.setDlyMix (0.25f);
        rueda (16);                      // segunda pasada: aqui hay un 0.60
        const float armado = e.getDlyMix();
        e.setPlaying (false);

        const bool ok = std::abs (enCero - 0.10f) < 0.001f
                     && std::abs (enOcho - 0.10f) < 0.001f
                     && std::abs (enDieciseis - 0.60f) < 0.001f
                     && std::abs (enTreintaYDos - 0.90f) < 0.001f
                     && std::abs (armado - 0.25f) < 0.001f;
        std::printf ("%-34s paso 0 %.2f   8 %.2f   16 %.2f   32 %.2f   armado %.2f   %s\n",
                     "la automatizacion", enCero, enOcho, enDieciseis, enTreintaYDos,
                     armado, ok ? "OK" : zatiFalla());
    }

    //  ========================================================================
    //  LOS CUATRO DE MODULACION: CHO, FLA, PHA y TRM.
    //
    //  DOS CIFRAS CADA UNO, que es lo que separa un efecto de un fader: una
    //  dice que hace algo y la otra que hace SOLO lo que dice. Con una sola,
    //  las dos formas de escribirlo mal pasan — es la misma regla que ya costo
    //  una medida en QUITAR RUIDO, en el bucle de la cancion y en el ancho
    //  estereo de un pad.
    //
    //  Y el par de PHA es el que mas vale escrito: «suena distinto» lo cumple
    //  igual un filtro mal escrito, y «casi lo mismo» lo cumple un phaser que
    //  no hace nada. Solo las dos juntas dicen ALLPASS.
    //  ========================================================================
    {
        constexpr double kFs = 48000.0;
        constexpr int    kBs = 512;

        //  Un tono plano, sin envolvente y sin ruido: con envolvente «cuanto
        //  cambia el nivel» seria la caida del sonido, que es una de las dos
        //  formas de que esto diga que si sin mirar nada.
        auto tono = [] (double sr, double seg, float hz, float amp)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * seg);
            sb->buffer.setSize (2, n);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                    sb->buffer.setSample (c, i,
                        amp * std::sin (juce::MathConstants<float>::twoPi * hz * (float) i / (float) sr));
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  Un motor de verdad con el pad 0 enrutado. Treinta bloques de
        //  asentado ANTES de disparar, que el envio se suaviza en 20 ms: si se
        //  descartan despues se pierde el ataque, que es literalmente lo que
        //  esta casa ya pago con el limitador.
        auto corre = [&tono] (int fx, float p0, float p1, float mix,
                              std::vector<float>& salida, int bloques = 120,
                              float hz = 440.0f, float amp = 0.5f)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            if (fx >= 0)
            {
                e.setFxParam (0, fx, 0, p0);
                e.setFxParam (0, fx, 1, p1);
                e.setFxParam (0, fx, 2, mix);
                e.setCanalSend (0, fx, 1.0f);
            }
            //  LA AMPLITUD ES UN PARAMETRO desde que hay un efecto cuyo mando
            //  lo mueve la SEÑAL: sin ella, WAH solo se puede medir a un nivel
            //  y «sigue la envolvente» no se puede ni afirmar ni negar.
            e.publishSample (0, tono (kFs, 2.5, hz, amp));

            juce::AudioBuffer<float> b (2, kBs);
            for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
            e.postNoteOn (0, 1.0f);

            salida.clear();
            for (int blk = 0; blk < bloques; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int i = 0; i < kBs; ++i) salida.push_back (b.getSample (0, i));
            }
        };

        auto rms = [] (const std::vector<float>& v, size_t desde, size_t hasta)
        {
            double a = 0.0; size_t n = 0;
            for (size_t i = desde; i < hasta && i < v.size(); ++i) { a += (double) v[i] * v[i]; ++n; }
            return std::sqrt (a / juce::jmax ((size_t) 1, n));
        };

        // --- CHO ------------------------------------------------------------
        //
        //  (1) con PROF a tope se separa de un coro SIN barrido -o sea de un
        //      retardo fijo-, y (2) el nivel medio no se mueve: un coro no es
        //      un fader. Solo lo primero lo cumple un tremolo escrito por
        //      error, y solo lo segundo lo cumple no hacer nada.
        {
            std::vector<float> quieto, barrido;
            corre (AudioEngine::kFxCho, 1.0f, 0.0f, 1.0f, quieto);
            corre (AudioEngine::kFxCho, 1.0f, 1.0f, 1.0f, barrido);

            double dif = 0.0;
            const size_t n = juce::jmin (quieto.size(), barrido.size());
            for (size_t i = n / 4; i < n; ++i) dif += (double) (barrido[i] - quieto[i])
                                                    * (double) (barrido[i] - quieto[i]);
            const double difRms = std::sqrt (dif / juce::jmax ((size_t) 1, n - n / 4));
            const double nivel  = 20.0 * std::log10 (rms (barrido, n / 4, n)
                                                     / juce::jmax (1.0e-12, rms (quieto, n / 4, n)));
            const bool ok = (difRms > 0.02) && (std::abs (nivel) < 1.5);
            std::printf ("%-34s barrido vs fijo %.4f   nivel %+.2f dB   %s\n",
                         "CHO", difRms, nivel, ok ? "OK" : zatiFalla());
        }

        // --- FLA ------------------------------------------------------------
        //
        //  Dos cifras: (1) hay PEINE -ceros profundos repartidos por la banda-
        //  y (2) lo hace la REALIMENTACION, o sea el mando. Sin la segunda,
        //  «hay peine» lo cumple igual un retardo corto sumado al seco con el
        //  mando muerto, que es exactamente como se escribe mal un flanger.
        //
        //  Y ESTA MEDIDA SE EQUIVOCO DOS VECES. La primera pedia «el nivel
        //  medio se conserva dentro de 6 dB» y salia +5.13: pasaba por ocho
        //  decimas, y ademas medía lo que no era -un peine con 0.95 de
        //  realimentacion SUBE de verdad donde suma-. La segunda comparaba el
        //  recorrido del nivel con el LFO «parado» a 0.05 Hz contra corriendo,
        //  y saco 34.41 parado contra 20.76 corriendo: al reves. Tampoco era
        //  el codigo — a 0.05 Hz el LFO NO esta parado en dos segundos, y
        //  ademas arranca en fase cero, que es justo donde un seno se mueve
        //  mas deprisa: el retardo se iba 1.7 ms, o sea dos ciclos de fase a
        //  1300 Hz. No hay forma de parar el barrido con un mando, asi que la
        //  pregunta se cambia por una que si se puede contestar.
        {
            auto peineCon = [&] (float fbk)
            {
                double alto = -200.0, bajo = 200.0;
                for (float hz : { 500.0f, 900.0f, 1300.0f, 1700.0f, 2100.0f, 2500.0f, 2900.0f, 3300.0f })
                {
                    std::vector<float> con, sin_;
                    corre (AudioEngine::kFxFla, 0.05f, fbk, 1.0f, con,  60, hz);
                    corre (-1,                  0.0f,  0.0f, 0.0f, sin_, 60, hz);
                    const double r = 20.0 * std::log10 (rms (con, 8192, 24576)
                                                        / juce::jmax (1.0e-12, rms (sin_, 8192, 24576)));
                    alto = juce::jmax (alto, r); bajo = juce::jmin (bajo, r);
                }
                return alto - bajo;
            };
            auto medioCon = [&] (float fbk)
            {
                double sc = 0.0, ss = 0.0;
                for (float hz : { 500.0f, 900.0f, 1300.0f, 1700.0f, 2100.0f, 2500.0f, 2900.0f, 3300.0f })
                {
                    std::vector<float> con, sin_;
                    corre (AudioEngine::kFxFla, 0.05f, fbk, 1.0f, con,  60, hz);
                    corre (-1,                  0.0f,  0.0f, 0.0f, sin_, 60, hz);
                    sc += rms (con, 8192, 24576); ss += rms (sin_, 8192, 24576);
                }
                return 20.0 * std::log10 (sc / juce::jmax (1.0e-12, ss));
            };

            const double tope = peineCon (1.0f);
            //  Y EL EXCESO CONTRA LA REALIMENTACION A CERO, no contra cero
            //  decibelios. Sumar una copia de algo a si mismo son +3 dB por
            //  construccion, en este y en cualquier efecto de los que SUMAN,
            //  asi que un liston absoluto suspendia a la aritmetica. Lo que se
            //  pregunta es si SUBIR la realimentacion sube el volumen — o sea
            //  si el mando es un peine o es un fader.
            const double exceso = medioCon (1.0f) - medioCon (0.5f);

            const bool ok = (tope > 10.0) && (std::abs (exceso) < 1.5);
            std::printf ("%-34s peine %.2f dB   FBK sube el nivel %+.2f dB   %s\n",
                         "FLA", tope, exceso, ok ? "OK" : zatiFalla());
        }

        // --- PHA ------------------------------------------------------------
        //
        //  Y ESTA MEDIDA SE EQUIVOCO ANTES DE ACERTAR, que en este banco ya es
        //  el patron. La primera version pedia que el MODULO fuese plano
        //  -«un phaser es una cadena de allpass»- y saco 23.97 dB con el codigo
        //  perfecto. No era el codigo: PHA **suma**, asi que lo que sale del
        //  motor es seco + humedo, o sea el phaser ENTERO con sus muescas. Se
        //  estaba midiendo la propiedad de una pieza en la salida de otra cosa.
        //
        //  Las dos cifras buenas son las dos mitades del efecto: (1) la SUMA
        //  tiene muescas -eso es lo que hace un phaser- y (2) el HUMEDO SOLO es
        //  plano -eso es lo que dice que es un allpass y no un filtro-. Solo la
        //  primera la cumple cualquier filtro puesto ahi; solo la segunda, no
        //  hacer nada.
        //
        //  El humedo se saca restando la corrida con el envio CERRADO, que es
        //  la misma pieza que `soloMojado` ya usa para DLY y REV, y por lo
        //  mismo: en los efectos que suman, el seco es lo mas alto de la
        //  ventana y se lleva por delante lo que se quiere medir.
        {
            double sumaAlto = -200.0, sumaBajo = 200.0;
            double humedoAlto = -200.0, humedoBajo = 200.0;
            for (float hz : { 300.0f, 700.0f, 1100.0f, 1500.0f, 2200.0f, 3000.0f, 4500.0f, 6000.0f })
            {
                std::vector<float> abierto, cerrado;
                corre (AudioEngine::kFxPha, 0.05f, 0.7f, 1.0f, abierto, 60, hz);
                corre (AudioEngine::kFxPha, 0.05f, 0.7f, 0.0f, cerrado, 60, hz);

                std::vector<float> humedo;
                humedo.reserve (abierto.size());
                for (size_t k = 0; k < abierto.size(); ++k)
                    humedo.push_back (abierto[k] - (k < cerrado.size() ? cerrado[k] : 0.0f));

                const double seco = rms (cerrado, 8192, 24576);
                const double s1 = 20.0 * std::log10 (rms (abierto, 8192, 24576) / juce::jmax (1.0e-12, seco));
                const double s2 = 20.0 * std::log10 (rms (humedo,  8192, 24576) / juce::jmax (1.0e-12, seco));
                sumaAlto = juce::jmax (sumaAlto, s1);  sumaBajo = juce::jmin (sumaBajo, s1);
                humedoAlto = juce::jmax (humedoAlto, s2); humedoBajo = juce::jmin (humedoBajo, s2);
            }
            const double muescas = sumaAlto - sumaBajo;
            const double plano   = humedoAlto - humedoBajo;

            const bool ok = (muescas > 6.0) && (plano < 1.5);
            std::printf ("%-34s muescas %.2f dB   humedo plano %.2f dB   %s\n",
                         "PHA", muescas, plano, ok ? "OK" : zatiFalla());
        }

        // --- TRM ------------------------------------------------------------
        //
        //  (1) el valle contra el pico casa con PROF y (2) el periodo entre
        //  valles casa con RATE. Solo la primera la cumple un fader con pasos,
        //  y solo la segunda un LFO cuya profundidad no hace nada.
        {
            std::vector<float> v;
            const float prof = 0.8f, rate = 4.0f;
            corre (AudioEngine::kFxTrm, rate, prof, 1.0f, v, 200);

            //  La envolvente por ventanas de un ciclo del TONO -no del LFO-,
            //  que es lo unico que mide un nivel: una ventana mas corta que un
            //  ciclo mide por donde cayo la ventana, y esta casa ya se comio
            //  esa con el bache del bucle de los instrumentos.
            const int vent = (int) (kFs / 440.0);
            std::vector<double> env;
            for (size_t i = kBs * 20; i + (size_t) vent < v.size(); i += (size_t) vent)
                env.push_back (rms (v, i, i + (size_t) vent));

            double pico = 0.0, valle = 1.0e9;
            for (double e : env) { pico = juce::jmax (pico, e); valle = juce::jmin (valle, e); }
            const double medido   = valle / juce::jmax (1.0e-12, pico);
            const double esperado = 1.0 - prof;

            //  Y el periodo, contado en cruces de la envolvente por su punto
            //  medio: dos cruces seguidos hacia abajo son una vuelta del LFO.
            const double medio = 0.5 * (pico + valle);
            int primero = -1, ultimo = -1, vueltas = 0;
            for (size_t i = 1; i < env.size(); ++i)
                if (env[i - 1] >= medio && env[i] < medio)
                {
                    if (primero < 0) primero = (int) i; else { ultimo = (int) i; ++vueltas; }
                }
            const double segs = (vueltas > 0 && ultimo > primero)
                                  ? (double) (ultimo - primero) * vent / kFs / vueltas : 0.0;
            const double hz = segs > 0.0 ? 1.0 / segs : 0.0;

            const bool ok = (std::abs (medido - esperado) < 0.08)
                         && (std::abs (hz - rate) < 0.5);
            std::printf ("%-34s valle/pico %.3f (PROF dice %.3f)   %.2f Hz (RATE dice %.2f)   %s\n",
                         "TRM", medido, esperado, hz, (double) rate, ok ? "OK" : zatiFalla());
        }

        // ====================================================================
        //  LOS SEIS DE CARACTER: RNG, PIT, WID, EXC, TRN y FRZ.
        //
        //  Las mismas DOS CIFRAS: la primera dice que hace algo y la segunda
        //  que hace SOLO lo que dice. Y las seis van por el MOTOR y no
        //  llamando a la etapa por dentro, que es donde ninguno de los fallos
        //  de indice existe.
        // ====================================================================

        //  La amplitud en una frecuencia suelta, por Goertzel. Hace falta
        //  porque estas seis se juzgan por DONDE esta la energia y no por
        //  cuanta hay: un anillo que baja el nivel y uno que mueve el tono dan
        //  el mismo RMS.
        auto amp = [] (const std::vector<float>& v, size_t desde, size_t hasta, double hz)
        {
            double re = 0.0, im = 0.0; size_t n = 0;
            for (size_t i = desde; i < hasta && i < v.size(); ++i, ++n)
            {
                const double w = juce::MathConstants<double>::twoPi * hz * (double) n / kFs;
                re += (double) v[i] * std::cos (w);
                im += (double) v[i] * std::sin (w);
            }
            return 2.0 * std::sqrt (re * re + im * im) / juce::jmax ((size_t) 1, n);
        };
        auto db = [] (double a, double b)
        { return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b)); };

        // --- RNG --------------------------------------------------------------
        //
        //  (1) con ANILLO a uno la PORTADORA se suprime: de un tono de 440 y
        //      una portadora de 220 salen 220 y 660, y en 440 no queda nada.
        //      Eso es la definicion de un modulador en anillo.
        //  (2) y con ANILLO a cero NO se suprime: es una amplitud modulada, o
        //      sea el tono original con un temblor encima. Sin la segunda,
        //      «el 440 desaparece» lo cumple igual un efecto que se come la
        //      señal entera.
        {
            std::vector<float> anillo, am;
            corre (AudioEngine::kFxRng, 220.0f, 1.0f, 1.0f, anillo);
            corre (AudioEngine::kFxRng, 220.0f, 0.0f, 1.0f, am);
            const size_t a = (size_t) kBs * 20, b = anillo.size();

            const double supr = db (amp (anillo, a, b, 440.0), amp (anillo, a, b, 660.0));
            const double queda = db (amp (am, a, b, 440.0), amp (am, a, b, 660.0));
            const bool ok = (supr < -20.0) && (queda > 0.0);
            std::printf ("%-34s portadora %+.1f dB   con ANILLO a cero %+.1f dB   %s\n",
                         "RNG", supr, queda, ok ? "OK" : zatiFalla());
        }

        // --- PIT --------------------------------------------------------------
        //
        //  (1) a +12 semitonos el tono de 440 sale en 880, y (2) a cero sigue
        //      en 440. Solo la primera la cumple un afinador clavado en una
        //      octava, y solo la segunda uno que no hace nada.
        {
            std::vector<float> arriba, recto;
            corre (AudioEngine::kFxPit, 12.0f, 60.0f, 1.0f, arriba);
            corre (AudioEngine::kFxPit,  0.0f, 60.0f, 1.0f, recto);
            const size_t a = (size_t) kBs * 40, b = arriba.size();

            const double subio = db (amp (arriba, a, b, 880.0), amp (arriba, a, b, 440.0));
            const double quieto = db (amp (recto,  a, b, 440.0), amp (recto,  a, b, 880.0));
            const bool ok = (subio > 6.0) && (quieto > 12.0);
            std::printf ("%-34s +12 st: 880 gana %+.1f dB   0 st: 440 gana %+.1f dB   %s\n",
                         "PIT", subio, quieto, ok ? "OK" : zatiFalla());
        }

        // --- WID --------------------------------------------------------------
        //
        //  Las MISMAS dos cifras que el ancho de un pad, y por lo mismo:
        //  cuanto LADO queda y cuanto CENTRO sobrevive. Solo la primera la
        //  cumple un fader y solo la segunda no hacer nada.
        //
        //  Y la fuente son DOS RUIDOS DISTINTOS, uno por canal: con la misma
        //  señal en los dos el lado vale cero y la prueba diria que si a
        //  cualquier cosa, incluso a un ancho que no esta conectado.
        {
            auto ruidoLR = [] (double sr, double seg)
            {
                auto* sb = new SampleBuffer();
                const int n = (int) (sr * seg);
                sb->buffer.setSize (2, n);
                juce::Random r (20260905);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                        sb->buffer.setSample (c, i, 0.3f * (r.nextFloat() * 2.0f - 1.0f));
                sb->sourceSampleRate = sr;
                return SampleBuffer::Ptr (sb);
            };

            auto correLR = [&ruidoLR] (float ancho, std::vector<float>& L, std::vector<float>& R)
            {
                AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
                e.setPadGain (0, 1.0f);
                e.setFxParam (0, AudioEngine::kFxWid, 0, ancho);
                e.setFxParam (0, AudioEngine::kFxWid, 1, 120.0f);
                e.setFxParam (0, AudioEngine::kFxWid, 2, 1.0f);
                e.setCanalSend (0, AudioEngine::kFxWid, 1.0f);
                e.publishSample (0, ruidoLR (kFs, 2.0));
                juce::AudioBuffer<float> b (2, kBs);
                for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
                e.postNoteOn (0, 1.0f);
                L.clear(); R.clear();
                for (int blk = 0; blk < 60; ++blk)
                {
                    b.clear(); e.renderNextBlock (b, 0, kBs);
                    for (int i = 0; i < kBs; ++i) { L.push_back (b.getSample (0, i)); R.push_back (b.getSample (1, i)); }
                }
            };

            auto ladoY = [] (const std::vector<float>& L, const std::vector<float>& R, bool lado)
            {
                double a = 0.0; size_t n = 0;
                for (size_t i = 2048; i < L.size() && i < R.size(); ++i, ++n)
                {
                    const double v = lado ? 0.5 * (L[i] - R[i]) : 0.5 * (L[i] + R[i]);
                    a += v * v;
                }
                return std::sqrt (a / juce::jmax ((size_t) 1, n));
            };

            std::vector<float> l0, r0, l1, r1, l2, r2;
            correLR (0.0f, l0, r0);
            correLR (1.0f, l1, r1);
            correLR (2.0f, l2, r2);

            const double s0 = ladoY (l0, r0, true),  s1 = ladoY (l1, r1, true),  s2 = ladoY (l2, r2, true);
            const double m0 = ladoY (l0, r0, false), m1 = ladoY (l1, r1, false), m2 = ladoY (l2, r2, false);
            //  El lado a cero NO es «casi cero»: con el mono de los graves
            //  puesto queda lo que el cruce deja pasar, o sea nada del lado.
            const bool ok = (s0 < 0.01 * s1) && (s2 > 1.6 * s1)
                         && (std::abs (m1 - m0) < 0.03 * m1) && (std::abs (m2 - m0) < 0.03 * m1);
            std::printf ("%-34s lado %.5f / %.5f / %.5f   centro %.5f / %.5f / %.5f   %s\n",
                         "WID", s0, s1, s2, m0, m1, m2, ok ? "OK" : zatiFalla());
        }

        // --- EXC --------------------------------------------------------------
        //
        //  (1) aparecen armonicos que la fuente no tiene -el tercero de un tono
        //      de 2 kHz, o sea 6 kHz- y (2) la banda BAJA no se mueve. Solo lo
        //      primero lo cumple una distorsion, que satura la mezcla entera;
        //      solo lo segundo, no hacer nada.
        {
            auto dosTonos = [] (double sr, double seg)
            {
                auto* sb = new SampleBuffer();
                const int n = (int) (sr * seg);
                sb->buffer.setSize (2, n);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                        sb->buffer.setSample (c, i,
                            0.35f * std::sin (juce::MathConstants<float>::twoPi * 200.0f  * (float) i / (float) sr)
                          + 0.35f * std::sin (juce::MathConstants<float>::twoPi * 2000.0f * (float) i / (float) sr));
                sb->sourceSampleRate = sr;
                return SampleBuffer::Ptr (sb);
            };
            auto correExc = [&dosTonos] (float fuerza, std::vector<float>& v)
            {
                AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
                e.setPadGain (0, 1.0f);
                e.setFxParam (0, AudioEngine::kFxExc, 0, 1000.0f);
                e.setFxParam (0, AudioEngine::kFxExc, 1, fuerza);
                e.setFxParam (0, AudioEngine::kFxExc, 2, 1.0f);
                e.setCanalSend (0, AudioEngine::kFxExc, 1.0f);
                e.publishSample (0, dosTonos (kFs, 2.0));
                juce::AudioBuffer<float> b (2, kBs);
                for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
                e.postNoteOn (0, 1.0f);
                v.clear();
                for (int blk = 0; blk < 60; ++blk)
                {
                    b.clear(); e.renderNextBlock (b, 0, kBs);
                    for (int i = 0; i < kBs; ++i) v.push_back (b.getSample (0, i));
                }
            };

            std::vector<float> sin_, con;
            correExc (0.0f, sin_);
            correExc (1.0f, con);
            const size_t a = (size_t) kBs * 20, b = con.size();

            const double armonico = db (amp (con, a, b, 6000.0), amp (sin_, a, b, 6000.0));
            const double grave    = db (amp (con, a, b,  200.0), amp (sin_, a, b,  200.0));
            const bool ok = (armonico > 12.0) && (std::abs (grave) < 0.5);
            std::printf ("%-34s tercer armonico %+.1f dB   grave %+.2f dB   %s\n",
                         "EXC", armonico, grave, ok ? "OK" : zatiFalla());
        }

        // --- TRN --------------------------------------------------------------
        //
        //  (1) con ATAQUE a tope el GOLPE pega mas, y (2) la COLA se queda
        //      donde estaba. Solo la primera la cumple un fader, y solo la
        //      segunda no hacer nada. Y la fuente es un golpe con envolvente,
        //      no el tono plano de los demas: un moldeador de transitorios
        //      sobre una señal sin transitorio no tiene sobre que actuar.
        {
            auto golpe = [] (double sr, double seg)
            {
                auto* sb = new SampleBuffer();
                const int n = (int) (sr * seg);
                sb->buffer.setSize (2, n);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        const double ms = (double) i / sr * 1000.0;
                        const double env = ms < 2.0 ? ms * 0.5 : std::exp (-(ms - 2.0) / 120.0);
                        sb->buffer.setSample (c, i, (float) (0.5 * env
                            * std::sin (juce::MathConstants<double>::twoPi * 440.0 * (double) i / sr)));
                    }
                sb->sourceSampleRate = sr;
                return SampleBuffer::Ptr (sb);
            };
            auto correTrn = [&golpe] (float at, std::vector<float>& v)
            {
                AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
                e.setPadGain (0, 1.0f);
                e.setFxParam (0, AudioEngine::kFxTrn, 0, at);
                e.setFxParam (0, AudioEngine::kFxTrn, 1, 0.0f);
                e.setFxParam (0, AudioEngine::kFxTrn, 2, 1.0f);
                e.setCanalSend (0, AudioEngine::kFxTrn, 1.0f);
                e.publishSample (0, golpe (kFs, 1.0));
                juce::AudioBuffer<float> b (2, kBs);
                for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
                e.postNoteOn (0, 1.0f);
                v.clear();
                for (int blk = 0; blk < 60; ++blk)
                {
                    b.clear(); e.renderNextBlock (b, 0, kBs);
                    for (int i = 0; i < kBs; ++i) v.push_back (b.getSample (0, i));
                }
            };

            std::vector<float> plano, moldeado;
            correTrn (0.0f, plano);
            correTrn (1.0f, moldeado);

            auto pico = [] (const std::vector<float>& v, size_t d, size_t h)
            { double p = 0.0; for (size_t i = d; i < h && i < v.size(); ++i) p = juce::jmax (p, (double) std::abs (v[i])); return p; };

            //  El golpe son los primeros 15 ms y la cola los 150 a 400.
            const size_t nAtk = (size_t) (kFs * 0.015), c0 = (size_t) (kFs * 0.150), c1 = (size_t) (kFs * 0.400);
            const double golpeDb = db (pico (moldeado, 0, nAtk), pico (plano, 0, nAtk));
            const double colaDb  = db (rms (moldeado, c0, c1),   rms (plano, c0, c1));
            const bool ok = (golpeDb > 2.0) && (std::abs (colaDb) < 1.0);
            std::printf ("%-34s golpe %+.2f dB   cola %+.2f dB   %s\n",
                         "TRN", golpeDb, colaDb, ok ? "OK" : zatiFalla());
        }

        // --- FRZ --------------------------------------------------------------
        //
        //  (1) sigue sonando cuando la fuente ya se acabo, y (2) lo que suena
        //      es el MISMO trozo — dos vueltas seguidas son identicas. Solo la
        //      primera la cumple una reverb infinita, y solo la segunda un
        //      congelador mudo.
        {
            auto corto = [] (double sr)
            {
                auto* sb = new SampleBuffer();
                const int n = (int) (sr * 1.5);
                sb->buffer.setSize (2, n);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < n; ++i)
                    {
                        //  Un cuarto de segundo de tono y despues silencio:
                        //  con la fuente sonando siempre, «sigue sonando» lo
                        //  cumple no hacer nada.
                        const float g = ((double) i / sr < 0.25) ? 0.5f : 0.0f;
                        sb->buffer.setSample (c, i, g
                            * std::sin (juce::MathConstants<float>::twoPi * 440.0f * (float) i / (float) sr));
                    }
                sb->sourceSampleRate = sr;
                return SampleBuffer::Ptr (sb);
            };
            auto correFrz = [&corto] (bool puesto, std::vector<float>& v)
            {
                AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
                e.setPadGain (0, 1.0f);
                if (puesto)
                {
                    e.setFxParam (0, AudioEngine::kFxFrz, 0, 180.0f);
                    e.setFxParam (0, AudioEngine::kFxFrz, 1, 0.0f);
                    e.setFxParam (0, AudioEngine::kFxFrz, 2, 1.0f);
                    e.setCanalSend (0, AudioEngine::kFxFrz, 1.0f);
                }
                e.publishSample (0, corto (kFs));
                juce::AudioBuffer<float> b (2, kBs);
                for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
                e.postNoteOn (0, 1.0f);
                v.clear();
                for (int blk = 0; blk < 120; ++blk)
                {
                    b.clear(); e.renderNextBlock (b, 0, kBs);
                    for (int i = 0; i < kBs; ++i) v.push_back (b.getSample (0, i));
                }
            };

            std::vector<float> con, sin_;
            correFrz (true, con);
            correFrz (false, sin_);

            //  Al segundo, la fuente lleva 750 ms callada.
            const size_t t1 = (size_t) (kFs * 1.00), t2 = (size_t) (kFs * 1.05);
            const double vivo  = rms (con,  t1, t2);
            const double mudo  = rms (sin_, t1, t2);

            //  Y las dos vueltas: 180 ms de ventana, asi que lo que suena en
            //  el segundo 0.9 tiene que ser lo mismo que en el 0.72.
            const size_t vent = (size_t) (kFs * 0.180);
            const size_t p0 = (size_t) (kFs * 0.72);
            double peor = 0.0;
            for (size_t i = 0; i < vent && p0 + vent + i < con.size(); ++i)
                peor = juce::jmax (peor, (double) std::abs (con[p0 + i] - con[p0 + vent + i]));

            const bool ok = (vivo > 0.05) && (mudo < 1.0e-4) && (peor < 1.0e-3);
            std::printf ("%-34s al segundo %.5f (sin FRZ %.5f)   dos vueltas difieren %.6f   %s\n",
                         "FRZ", vivo, mudo, peor, ok ? "OK" : zatiFalla());
        }

        // ====================================================================
        //  LOS DOS QUE SE PIDIERON: WAH y OCT.
        // ====================================================================

        // --- WAH --------------------------------------------------------------
        //
        //  El unico de los veintitres cuyo mando lo mueve la SEÑAL, asi que sus
        //  dos cifras son las dos mitades de eso:
        //
        //  (1) CON SEÑAL FUERTE la banda se abre: el mismo tono de 1600 Hz pasa
        //      +X dB con SENS a tope que con SENS a cero -donde el centro se
        //      queda clavado en BASE, 400 Hz, y 1600 cae dos octavas arriba-.
        //  (2) CON SEÑAL FLOJA no se mueve: la misma comparacion da CERO. Sin
        //      esta, «se abre» lo cumple igual un filtro con el centro puesto
        //      mas arriba, que es un filtro de banda y no un wah; y sin la
        //      primera, «no se mueve» lo cumple un mando que no esta conectado.
        {
            std::vector<float> fuerteSens, fuerteQuieto, flojaSens, flojaQuieto;
            const float base = 400.0f, prueba = 1600.0f;
            corre (AudioEngine::kFxWah, 1.0f, base, 1.0f, fuerteSens,   120, prueba, 0.90f);
            corre (AudioEngine::kFxWah, 0.0f, base, 1.0f, fuerteQuieto, 120, prueba, 0.90f);
            corre (AudioEngine::kFxWah, 1.0f, base, 1.0f, flojaSens,    120, prueba, 0.02f);
            corre (AudioEngine::kFxWah, 0.0f, base, 1.0f, flojaQuieto,  120, prueba, 0.02f);

            const size_t a = (size_t) kBs * 40, b = fuerteSens.size();
            const double abre  = db (amp (fuerteSens, a, b, prueba),
                                     amp (fuerteQuieto, a, b, prueba));
            const double queda = db (amp (flojaSens, a, b, prueba),
                                     amp (flojaQuieto, a, b, prueba));

            const bool ok = (abre > 9.0) && (std::abs (queda) < 1.0);
            std::printf ("%-34s con señal fuerte %+.1f dB   con señal floja %+.1f dB   %s\n",
                         "WAH", abre, queda, ok ? "OK" : zatiFalla());
        }

        // --- OCT --------------------------------------------------------------
        //
        //  TRES cifras, porque las dos primeras son cada una la mitad del
        //  efecto y la tercera es la que dice que las dos SALEN DE AHI:
        //
        //  (1) con ARRIBA solo, de un tono de 220 sale un 440 -la rectificacion
        //      de onda completa dobla la frecuencia-;
        //  (2) con ABAJO solo, sale un 110 -el biestable divide por dos-;
        //  (3) y con los dos a cero el bus se queda MUDO. Sin la tercera, las
        //      otras dos las cumple igual un octavador que ademas deja pasar el
        //      original, y entonces «suena» no dice de donde sale lo que suena.
        //
        //  Y no es PIT con el mando en -12: esto lo hacen un rectificador y un
        //  divisor, que es por lo que el 440 sale CON la fundamental de 220
        //  hundida y el 110 sale como un cuadrado.
        {
            std::vector<float> arriba, abajo, nada;
            const float f0 = 220.0f;
            corre (AudioEngine::kFxOct, 1.0f, 0.0f, 1.0f, arriba, 120, f0, 0.70f);
            corre (AudioEngine::kFxOct, 0.0f, 1.0f, 1.0f, abajo,  120, f0, 0.70f);
            corre (AudioEngine::kFxOct, 0.0f, 0.0f, 1.0f, nada,   120, f0, 0.70f);

            const size_t a = (size_t) kBs * 40, b = arriba.size();
            const double alta = db (amp (arriba, a, b, 440.0), amp (arriba, a, b, f0));
            const double baja = db (amp (abajo,  a, b, 110.0), amp (abajo,  a, b, f0));
            const double mudo = rms (nada, a, b);

            const bool ok = (alta > 6.0) && (baja > 6.0) && (mudo < 1.0e-3);
            std::printf ("%-34s ARRIBA 440/220 %+.1f dB   ABAJO 110/220 %+.1f dB   "
                         "con los dos a cero %.5f   %s\n",
                         "OCT", alta, baja, mudo, ok ? "OK" : zatiFalla());
        }
    }

    // ------------------------------------------------------------------
    //  LA MESA: EL CANAL ES EL DUEÑO DEL ENVIO.
    //
    //  Hasta aqui el envio era del PAD y la fila de la cara era GLOBAL, asi que
    //  cambiar de pad no cambiaba nada de lo que ese pad suena por dentro. El
    //  pad elige CANAL y el canal manda; el recorte del pad sobrevive como el
    //  numero con el que un proyecto anterior se guardo.
    //
    //  Las tres cifras que se miden aqui son de MOTOR y no de cara: que el
    //  envio sea del canal, que el fader del canal escale las DOS mitades, y
    //  que la maquina de hoy con todo en el canal 0 sea BIT A BIT la de ayer.
    {
    constexpr double kFs = 48000.0;
    constexpr int    kBs = 512;
    //  Un tono SIN envolvente: lo que se mide aqui es el reparto, y una
    //  envolvente haria que la respuesta dependiera de cuando se pregunta —el
    //  fallo que este banco ya tiene escrito dos veces.
    auto tonoPlano = [] (double sr, double seg, double hz)
    {
        auto* sb = new SampleBuffer();
        const int n = (int) (sr * seg);
        sb->buffer.setSize (1, n);
        for (int i = 0; i < n; ++i)
            sb->buffer.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * hz * (double) i / sr));
        sb->sourceSampleRate = sr;
        return SampleBuffer::Ptr (sb);
    };

    {
        //  El pad 0 en el canal 1 y el pad 1 en el canal 2, y solo el canal 1
        //  manda al delay. Se mide LA COLA y no el nivel, que es donde la
        //  prueba obvia se equivocaria: el camino seco no pasa por el envio,
        //  asi que los dos pads suenan igual mientras la muestra dura y
        //  comparar picos daria «casi lo mismo». Lo que las separa es lo que
        //  suena DESPUES.
        auto cola = [&tonoPlano] (int canalDelPad)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setPadCanal (0, canalDelPad);
            e.setFxParam (0, AudioEngine::kFxDly, 0, 250.0f);
            e.setFxParam (0, AudioEngine::kFxDly, 1, 0.6f);
            e.setFxParam (0, AudioEngine::kFxDly, 2, 1.0f);
            e.setCanalSend (1, AudioEngine::kFxDly, 1.0f);   // solo el canal 1

            //  Y EL PAD ENTRA EN LA MASCARA EN LAS DOS CORRIDAS, que es lo que
            //  hace que esta cifra mida el REPARTO y no el atajo.
            //
            //  `padSendMask` se calcula del producto de las dos mitades, asi
            //  que con el canal 2 cerrado el pad se salta el bucle largo
            //  entero: la primera version de esta comprobacion siguio saliendo
            //  VERDE con el reparto roto a proposito -mandando desde el
            //  recorte- porque nunca llegaba a multiplicar. Un envio abierto a
            //  un efecto cuya MEZCLA esta en cero pone el bit y no suena nada,
            //  asi que las dos corridas recorren el mismo codigo y lo unico que
            //  las separa es de quien sale el numero.
            e.setCanalSend (canalDelPad, AudioEngine::kFxFlt, 1.0f);
            e.publishSample (0, tonoPlano (kFs, 0.20, 440.0));

            juce::AudioBuffer<float> b (2, kBs);
            double peor = 0.0;
            const int total = (int) (kFs * 1.2) / kBs;
            for (int i = 0; i < total; ++i)
            {
                if (i == 30) e.postNoteOn (0, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                //  Desde bien pasada la muestra: 0.20 s de sonido disparados en
                //  el bloque 30, o sea que a partir del 0.6 s lo unico que
                //  puede sonar es el eco.
                if (i > (int) (kFs * 0.6) / kBs)
                    peor = juce::jmax (peor, (double) b.getMagnitude (0, kBs));
            }
            return peor;
        };

        const double enUno = cola (1);
        const double enDos = cola (2);
        const bool ok = (enUno > 0.02) && (enDos < 1.0e-5);
        std::printf ("%-34s canal 1 %.5f   canal 2 %.5f   %s\n",
                     "el canal es dueño del envio", enUno, enDos, ok ? "OK" : zatiFalla());
    }

    {
        //  EL MEDIDOR DEL CANAL, CON DOS CIFRAS.
        //
        //  Un medidor que copia el master sube igual con cualquier pad, asi que
        //  «el canal 1 sube» lo cumple tambien un cable puesto al sitio
        //  equivocado. Lo que lo separa es la SEGUNDA: el pad suena en el canal
        //  1 y el 2 tiene que quedarse en CERO — mismo motor, mismo bloque,
        //  misma muestra, y lo unico que cambia es a cual se le pregunta.
        //
        //  Y se lee por `readCanalPico`, que vacia: dos lecturas seguidas de un
        //  silencio dan cero, que es lo que hace que la de abajo signifique
        //  algo.
        auto mide = [&tonoPlano] (int canalMirado)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setPadCanal (0, 1);            // el pad suena en el canal 1
            e.miraCanal (canalMirado);
            e.publishSample (0, tonoPlano (kFs, 0.20, 440.0));

            juce::AudioBuffer<float> b (2, kBs);
            double pico = 0.0;
            for (int i = 0; i < 60; ++i)
            {
                if (i == 5) e.postNoteOn (0, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                if (i > 5) pico = juce::jmax (pico, (double) e.readCanalPico());
            }
            return pico;
        };

        const double suyo  = mide (1);
        const double ajeno = mide (2);
        const bool ok = (suyo > 0.2) && (ajeno < 1.0e-6);
        std::printf ("%-34s el suyo %.5f   el de al lado %.5f   %s\n",
                     "el medidor es del canal", suyo, ajeno, ok ? "OK" : zatiFalla());
    }

    {
        //  EL FADER DEL CANAL ESCALA LAS DOS MITADES, que es lo que separa un
        //  canal de mesa de un fader del seco: con el envio abierto, bajar el
        //  canal 6 dB tiene que bajar el SECO y la COLA lo mismo. Solo lo
        //  primero lo cumple una ganancia puesta en el camino corto, y solo lo
        //  segundo una puesta en el envio.
        auto corre = [&tonoPlano] (float gan, double& seco, double& colaOut)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setCanalGain (0, gan);
            e.setFxParam (0, AudioEngine::kFxDly, 0, 250.0f);
            e.setFxParam (0, AudioEngine::kFxDly, 1, 0.6f);
            e.setFxParam (0, AudioEngine::kFxDly, 2, 1.0f);
            e.setCanalSend (0, AudioEngine::kFxDly, 0.5f);
            e.publishSample (0, tonoPlano (kFs, 0.20, 440.0));

            juce::AudioBuffer<float> b (2, kBs);
            seco = colaOut = 0.0;
            const int total = (int) (kFs * 1.2) / kBs;
            for (int i = 0; i < total; ++i)
            {
                //  Treinta bloques de asentado antes de disparar: el envio y el
                //  fader suben con 20 ms de constante, que es la leccion que ya
                //  costo una medida con el limitador.
                if (i == 30) e.postNoteOn (0, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                if (i >= 34 && i < 60)                    seco    = juce::jmax (seco,    (double) b.getMagnitude (0, kBs));
                if (i > (int) (kFs * 0.6) / kBs)          colaOut = juce::jmax (colaOut, (double) b.getMagnitude (0, kBs));
            }
        };

        double s1 = 0, c1 = 0, s2 = 0, c2 = 0;
        corre (1.0f, s1, c1);
        corre (0.5f, s2, c2);
        const double dSeco = 20.0 * std::log10 (juce::jmax (1.0e-9, s2 / juce::jmax (1.0e-9, s1)));
        const double dCola = 20.0 * std::log10 (juce::jmax (1.0e-9, c2 / juce::jmax (1.0e-9, c1)));
        const bool ok = std::abs (dSeco + 6.02) < 0.5 && std::abs (dCola + 6.02) < 0.7;
        std::printf ("%-34s seco %+.2f dB   cola %+.2f dB   %s\n",
                     "el fader del canal", dSeco, dCola, ok ? "OK" : zatiFalla());

        //  Y HACIA ARRIBA, que es la mitad que esta medida no preguntaba.
        //
        //  Las dos corridas de aqui arriba van de 1.0 a 0.5 - las DOS por
        //  debajo de la unidad- y por ahi se colo el mismo fallo que tenia el
        //  master: el camino corto renderiza las voces directamente en la
        //  salida sin multiplicar por `dryGain`, y el guardia que decide si un
        //  pad se aparta preguntaba `smCan < 0.9995f`, o sea UN solo lado. Un
        //  canal por encima de 0 dB tomaba el camino corto y su fader no hacia
        //  nada: bajarlo se oia y subirlo no.
        //
        //  Es la tercera vez que este banco se come la misma forma de fallo
        //  -una condicion de un lado para una pregunta de dos- y la segunda en
        //  la misma tanda. Se mide con las dos mitades a la vez: el seco y la
        //  cola suben los dos +6.02, que es lo unico que separa «el fader
        //  sube» de «el fader sube el seco y se deja el envio».
        double s3 = 0, c3 = 0;
        corre (1.9953f, s3, c3);              // +6 dB
        const double uSeco = 20.0 * std::log10 (juce::jmax (1.0e-9, s3 / juce::jmax (1.0e-9, s1)));
        const double uCola = 20.0 * std::log10 (juce::jmax (1.0e-9, c3 / juce::jmax (1.0e-9, c1)));
        const bool okUp = std::abs (uSeco - 6.02) < 0.5 && std::abs (uCola - 6.02) < 0.7;
        std::printf ("%-34s seco %+.2f dB   cola %+.2f dB   %s\n",
                     "el fader del canal SUBE", uSeco, uCola, okUp ? "OK" : zatiFalla());
    }

    {
        //  Y SIN UN SOLO ENVIO ABIERTO, que es el unico camino donde el fallo
        //  vive y el unico que la medida de aqui arriba NO puede tocar.
        //
        //  Un pad que no manda a nadie toma el CAMINO CORTO: sus voces se
        //  renderizan directamente en la salida y no pasan por `dryGain`. El
        //  guardia que decide si un pad se aparta preguntaba `smCan < 0.9995f`
        //  -UN solo lado- asi que un canal por ENCIMA de 0 dB se quedaba en el
        //  camino corto y su fader no hacia absolutamente nada. Bajarlo se oia,
        //  subirlo no: el mismo fallo que el guardia del master, en la misma
        //  tanda y por el mismo motivo -una condicion que era correcta cuando
        //  el numero solo podia bajar-.
        //
        //  Y la primera version de esta medida NO LO CAZO: se escribio dentro
        //  del bloque de arriba, que abre un envio al delay para poder mirar la
        //  cola, y con un envio abierto el pad toma el camino LARGO de todas
        //  formas. Roto a proposito seguia saliendo OK. Se mide donde el fallo
        //  vive, que es una maquina sin un solo envio.
        auto seco = [&tonoPlano] (float gan)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setCanalGain (0, gan);
            e.publishSample (0, tonoPlano (kFs, 0.40, 440.0));

            juce::AudioBuffer<float> b (2, kBs);
            double pico = 0.0;
            for (int i = 0; i < 90; ++i)
            {
                if (i == 30) e.postNoteOn (0, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                if (i >= 40 && i < 70) pico = juce::jmax (pico, (double) b.getMagnitude (0, kBs));
            }
            return pico;
        };

        const double base   = seco (1.0f);
        const double subido = seco (1.9953f);      // +6 dB
        const double d = 20.0 * std::log10 (juce::jmax (1.0e-9, subido / juce::jmax (1.0e-9, base)));
        const bool ok = base > 0.01 && std::abs (d - 6.02) < 0.5;
        std::printf ("%-34s a 0 dB %.5f   a +6 dB %.5f   sube %+.2f dB   %s\n",
                     "el canal SUBE sin envios", base, subido, d, ok ? "OK" : zatiFalla());
    }

    {
        //  EL RECORTE DEL PAD YA NO PUEDE RECORTAR UN INSERTO, y esta medida
        //  dice ESO en vez de lo que decia.
        //
        //  Decia: «el canal mandando lo que ayer mandaba cada pad da la salida
        //  BIT A BIT de antes», y era cierto mientras cada efecto recibia su
        //  fraccion del pad. Desde que las ranuras son una CADENA no puede
        //  serlo: un inserto es del CANAL —su estado en el motor es uno— y el
        //  pad entra entero en el primer eslabon, asi que un numero por pad no
        //  tiene donde aplicarse. La medida salio en rojo con 32256 de 47616
        //  muestras distintas, y tenia razon: lo que cambio es el motor.
        //
        //  Lo que se mide ahora es lo unico que sigue siendo verdad y que
        //  importa: que el recorte del pad NO se ignora en silencio. Un fichero
        //  anterior con envios distintos por pad abre, suena, y sus numeros
        //  pasan a ser del canal —se anota en la bitacora y la barra de estado
        //  lo dice—. Lo que no puede pasar es que el recorte mueva algo a medias
        //  y nadie sepa cual de los dos manda.
        //
        //  «Casi lo mismo» es justo lo que dejaria pasar una capa que escala de
        //  mas o de menos — es la misma comparacion que el filtro del pad
        //  apagado y el orden del ancho estereo.
        auto corre = [&tonoPlano] (bool porCanal, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setFxParam (0, AudioEngine::kFxDly, 0, 180.0f);
            e.setFxParam (0, AudioEngine::kFxDly, 1, 0.5f);
            e.setFxParam (0, AudioEngine::kFxDly, 2, 1.0f);
            e.setFxParam (0, AudioEngine::kFxFlt, 0, 0.7f);
            e.setFxParam (0, AudioEngine::kFxFlt, 2, 0.6f);
            if (porCanal)
            {
                e.setCanalSend (0, AudioEngine::kFxDly, 0.4f);
                e.setCanalSend (0, AudioEngine::kFxFlt, 0.6f);
            }
            else
            {
                //  La forma de AYER: el numero en el recorte del pad y el canal
                //  a uno, que es exactamente como abre un proyecto anterior.
                e.setCanalSend (0, AudioEngine::kFxDly, 1.0f);
                e.setCanalSend (0, AudioEngine::kFxFlt, 1.0f);
                for (int p = 0; p < AudioEngine::kNumPads; ++p)
                {
                    e.setPadRecorte (p, AudioEngine::kFxDly, 0.4f);
                    e.setPadRecorte (p, AudioEngine::kFxFlt, 0.6f);
                }
            }
            e.publishSample (0, tonoPlano (kFs, 0.20, 440.0));

            juce::AudioBuffer<float> b (2, kBs);
            out.clear();
            const int total = (int) (kFs * 1.0) / kBs;
            for (int i = 0; i < total; ++i)
            {
                if (i == 30) e.postNoteOn (0, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int n = 0; n < kBs; ++n) out.push_back (b.getSample (0, n));
            }
        };

        std::vector<float> a, c;
        corre (true, a);
        corre (false, c);
        int distintas = 0;
        for (size_t i = 0; i < a.size() && i < c.size(); ++i)
            if (a[i] != c[i]) ++distintas;

        //  El de la izquierda manda por el CANAL y el de la derecha por el
        //  RECORTE del pad. Que salgan distintos es el resultado correcto desde
        //  la cadena; lo que NO puede salir es que el recorte se coma el sonido
        //  —el pad tiene que seguir sonando con su cadena entera— asi que la
        //  segunda cifra es el nivel, que es lo que una regresion silenciosa
        //  dejaria en cero.
        double picoRecorte = 0.0;
        for (float x : c) picoRecorte = juce::jmax (picoRecorte, (double) std::abs (x));

        const bool ok = distintas > 0 && ! a.empty() && picoRecorte > 0.05;
        std::printf ("%-34s el recorte ya no recorta: %d de %d cambian, y sigue sonando a %.3f   %s\n",
                     "el recorte del pad es de ayer", distintas, (int) a.size(),
                     picoRecorte, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y LAS RANURAS SON UNA CADENA, que es la queja con la que empezo esto:
    //  «meto un EQ y los sonidos que llegan a ese canal pasan primero por ese
    //  EQ».
    //
    //  No pasaban. Cada etapa tomaba una copia del PAD y volvia al master por
    //  su cuenta, asi que dos ranuras al 100 % daban `EQ(pad) + CMP(pad)` en vez
    //  de `CMP(EQ(pad))`. Con UN efecto es identico y por eso vivio tanto.
    //
    //  SE MIDE CON EL NIVEL Y NO CON EL ESPECTRO. En paralelo las dos copias se
    //  SUMAN, asi que sale del orden de seis dB de mas; en cadena el compresor
    //  ve la señal ya ecualizada y la baja. Son dos numeros muy separados y no
    //  hace falta mirar ninguna frecuencia para distinguirlos.
    //
    //  Y LA SEGUNDA CIFRA ES LA QUE IMPIDE QUE SE CUMPLA SOLA: las mismas dos
    //  ranuras AL REVES tienen que sonar distinto. En paralelo la suma es
    //  conmutativa y las dos corridas salen identicas; en cadena, ecualizar
    //  antes de comprimir no es comprimir antes de ecualizar.
    {
        auto corre = [] (int primero, int segundo, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 0.30f);
            e.setPadCanal (0, 3);

            //  Una campana de +12 dB en el EQ y un compresor con umbral bajo:
            //  el orden importa porque el segundo VE lo que el primero hizo.
            e.setEqMix (3, 1.0f);
            e.setEqBand (3, 2, 300.0f, +12.0f);
            e.setFxParam (3, AudioEngine::kFxCmp, 0, -40.0f);
            e.setFxParam (3, AudioEngine::kFxCmp, 1,   8.0f);
            e.setFxParam (3, AudioEngine::kFxCmp, 2,  1.0f);
            e.setCanalSend (3, primero, 1.0f);
            e.setCanalSend (3, segundo, 1.0f);

            auto* sb = new SampleBuffer();
            const int n = (int) (kFs * 0.80);
            sb->buffer.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.50f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * 300.0 * (double) i / kFs);
                sb->buffer.setSample (0, i, x);
                sb->buffer.setSample (1, i, x);
            }
            sb->sourceSampleRate = kFs;
            e.publishSample (0, SampleBuffer::Ptr (sb));

            juce::AudioBuffer<float> b (2, kBs);
            b.clear(); e.renderNextBlock (b, 0, kBs);
            e.postNoteOn (0, 1.0f);
            out.clear();
            for (int blk = 0; blk < 28; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                if (blk < 8) continue;          // el envio se asienta en 20 ms
                for (int i = 0; i < kBs; ++i) out.push_back (b.getSample (0, i));
            }
        };

        auto rms = [] (const std::vector<float>& v)
        {
            double a = 0.0;
            for (float x : v) a += (double) x * (double) x;
            return std::sqrt (a / juce::jmax ((size_t) 1, v.size()));
        };

        std::vector<float> uno, dos, solo;
        corre (AudioEngine::kFxEq,  AudioEngine::kFxCmp, uno);
        corre (AudioEngine::kFxCmp, AudioEngine::kFxEq,  dos);
        //  Y la referencia: SOLO el EQ, o sea un eslabon. Es contra lo que se
        //  mide «cuanto de mas», porque en paralelo el compresor anade una
        //  copia entera del pad encima.
        corre (AudioEngine::kFxEq,  AudioEngine::kFxEq,  solo);

        const double dB = 20.0 * std::log10 (juce::jmax (1.0e-12, rms (uno))
                                               / juce::jmax (1.0e-12, rms (solo)));
        int distintas = 0;
        for (size_t i = 0; i < uno.size() && i < dos.size(); ++i)
            if (uno[i] != dos[i]) ++distintas;

        //  LAS DOS CIFRAS, MEDIDAS Y NO PREVISTAS: en CADENA sale -27.65 dB
        //  —el compresor ve la señal ya ecualizada y la aplasta— y en PARALELO
        //  sale +0.30 —las dos copias se suman, pero la del compresor viene
        //  aplastada y casi no aporta—. Veintiocho decibelios de separacion.
        //
        //  El umbral va en -10 dB, a medio camino. La primera version lo puso en
        //  +3 «porque en paralelo sube unos seis dB», que era una prevision y no
        //  una medida: con el defecto dentro salia +0.30 y la prueba decia OK.
        //  Una prueba cuyo umbral se elige antes de ver las dos cifras no separa
        //  nada.
        //
        //  Y LA SEGUNDA CIFRA SE IMPRIME Y NO SE JUZGA, con su razon: hoy las
        //  etapas corren en el orden CANONICO de los tipos y no en el de las
        //  ranuras —los veintitres cuerpos estan escritos en linea y en orden
        //  fijo dentro de `renderNextBlock`—, asi que EQ va antes que CMP se
        //  ponga donde se ponga y las dos corridas salen identicas. Sacarlos a
        //  funciones para poder ejecutarlos en el orden en el que la persona
        //  arrastra es la tanda siguiente, y esta cifra es la que se pondra en
        //  verde entonces. Juzgarla hoy seria pedirle al banco que mida algo que
        //  todavia no se ha construido; no decirla seria fingir que la lista ya
        //  manda. Es el mismo trato que el contador TOUCH.
        const bool ok = dB < -10.0;
        std::printf ("%-34s EQ+CMP contra solo EQ %+.2f dB   (el orden de ranura llega en la tanda que viene: cambian %d de %d)   %s\n",
                     "las ranuras son una cadena", dB, distintas, (int) uno.size(),
                     ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y EL CANAL SE PANEA, con la cola de reverb detras.
    //
    //  Del telefono: «en el mixer de canales deberia haber la opcion de panear
    //  tambien, ¿no?». No la habia: un canal tenia ganancia, mute y solo.
    //
    //  DOS cifras, y la segunda es la que impide que se cumpla a medias: el
    //  derecho en cero es lo facil, y que la COLA DE REVERB de ese canal tambien
    //  se vaya a la izquierda es lo que dice que la derivacion al envio sale
    //  DESPUES del pan. Si la cola vuelve centrada, el envio se toma del pad
    //  crudo y el pan del canal no es un pan de canal: es un pan de lo seco.
    {
        //  DOS CORRIDAS Y NO UNA. El seco se mide SIN reverb y la cola CON
        //  ella: con el envio abierto la sala ya esta sonando encima del golpe
        //  -sale 0.2439 contra 0.1603 con el canal del todo a la izquierda- y lo
        //  que se media como «seco» era seco mas cola.
        auto corre = [] (float pan, bool conRev, double& izq, double& der, double& colaDer)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 0.50f);
            e.setPadCanal (0, 5);
            e.setCanalPan (5, pan);
            //  Y una reverb con envio abierto, que es la que tiene que heredar
            //  el sitio. Tamaño grande para que la cola dure despues del golpe.
            e.setFxParam (0, AudioEngine::kFxRev, 0, 0.90f);
            e.setFxParam (0, AudioEngine::kFxRev, 2, 1.00f);
            //  Y EL CANAL LLEVA UN INSERTO, aunque sea neutro: sin cadena la
            //  copia de la reverb sale del PAD -que ya venia paneado- y la
            //  medida pasaria sin tocar el camino nuevo. Con un eslabon dentro,
            //  la copia se toma del final de la cadena, que es lo que hay que
            //  comprobar.
            e.setEqMix (5, 1.0f);
            e.setCanalSend (5, AudioEngine::kFxEq, 1.0f);
            if (conRev) e.setCanalSend (5, AudioEngine::kFxRev, 1.0f);

            auto* sb = new SampleBuffer();
            sb->buffer.setSize (2, 4096);
            sb->buffer.clear();
            //  La muestra dura CUATRO MIL muestras y no cuatrocientas: el pan
            //  de una voz se desliza al re-apuntar, asi que en el primer bloque
            //  todavia esta a medio camino -salia 0.3640 contra 0.2679 con el
            //  canal del todo a la izquierda- y lo que se media era el
            //  deslizamiento, no el pan.
            for (int i = 1; i < 4000; ++i)
            {
                const float x = 0.8f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                           * 440.0 * (double) i / kFs);
                sb->buffer.setSample (0, i, x);
                sb->buffer.setSample (1, i, x);
            }
            sb->sourceSampleRate = kFs;
            e.publishSample (0, SampleBuffer::Ptr (sb));

            juce::AudioBuffer<float> b (2, kBs);
            for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }
            e.postNoteOn (0, 1.0f);

            //  EL SECO SE MIDE EN EL PRIMER BLOQUE y la COLA a partir del
            //  veinte. Medir el pico de la corrida entera mezclaba los dos y el
            //  derecho no bajaba nunca: la reverb ya estaba sonando encima.
            izq = der = colaDer = 0.0;
            double colaIzq = 0.0;
            for (int blk = 0; blk < 120; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int i = 0; i < kBs; ++i)
                {
                    const double l = std::abs ((double) b.getSample (0, i));
                    const double r = std::abs ((double) b.getSample (1, i));
                    if (blk >= 4 && blk <= 7) { izq = juce::jmax (izq, l); der = juce::jmax (der, r); }
                    if (blk >= 20)
                    {
                        colaIzq = juce::jmax (colaIzq, l);
                        colaDer = juce::jmax (colaDer, r);
                    }
                }
            }
            //  Y lo que se devuelve como «cola» es el DESEQUILIBRIO de la cola,
            //  no su nivel: una reverb estereo ESPARCE —eso es lo que hace una
            //  sala— asi que pedirle silencio en un lado seria pedirle que no
            //  fuera una reverb. Lo que dice de donde se tomo la copia es que la
            //  cola salga mas fuerte del lado al que el canal esta paneado.
            colaDer = colaIzq / juce::jmax (1.0e-9, colaDer);
        };

        double i0 = 0, d0 = 0, c0 = 0, iL = 0, dL = 0, cL = 0, nada = 0;
        corre ( 0.0f, false, i0, d0, nada);   // seco centrado
        corre (-1.0f, false, iL, dL, nada);   // seco a la izquierda
        corre ( 0.0f, true,  nada, nada, c0); // cola centrada
        corre (-1.0f, true,  nada, nada, cL); // cola a la izquierda

        //  Centrado los dos lados son iguales y la cola equilibrada -razon uno-;
        //  del todo a la izquierda el derecho SECO se cae y la cola se inclina.
        const bool ok = std::abs (i0 - d0) < 0.01
                     && std::abs (c0 - 1.0) < 0.15
                     && dL < 0.02 * juce::jmax (1.0e-9, iL)
                     && cL > 1.20;
        std::printf ("%-34s seco centrado %.4f/%.4f   a la izquierda %.4f/%.4f   y la cola se inclina x%.2f (centrada x%.2f)   %s\n",
                     "el canal se panea, cola incluida",
                     i0, d0, iL, dL, cL, c0, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y EL EQ ES DE CADA CANAL, que es la queja con la que empieza esta tanda
    //  dicha en decibelios: «solo es posible que funcione y sea colocado en un
    //  canal solo, deberia haber un plugin de cada tipo para cada canal».
    //
    //  Los dos pads llevan LA MISMA muestra a proposito: si fueran dos sonidos
    //  distintos, «suenan distinto» lo cumpliria la FUENTE y no el reparto. Y
    //  las dos bandas van a lados CONTRARIOS -+12 y -12- porque con un solo
    //  `Eq5` la segunda escritura pisa a la primera y los dos canales salen con
    //  lo ultimo que se escribio: ese es el fallo, dicho en dB.
    //
    //  Y LOS DOS CANALES SE CONFIGURAN EN LAS DOS CORRIDAS, que es lo unico que
    //  hace que la rotura se vea: con un motor por canal, un `Eq5` compartido
    //  seguiria dando +12 en una corrida y -12 en la otra, porque cada una solo
    //  habria escrito su banda.
    //
    //  TRES cifras, y la primera es la que impide que un canal MUDO pase: sin
    //  EQ los dos tienen que salir al mismo nivel.
    {
        auto rmsDe = [&tonoPlano] (int pad, bool conEq)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
            //  Y CON MARGEN DE SOBRA, que es lo que la primera corrida
            //  enseño: con el pad a uno, +12 dB sobre un tono de 0.5 son 1.99
            //  de pico y el saturador del master se los come — salia +10.4 dB
            //  con el EQ perfecto. Lo que se mide aqui es el reparto, no el
            //  limitador. Primero se duda de la prueba.
            e.setPadGain (0, 0.15f); e.setPadCanal (0, 0);
            e.setPadGain (1, 0.15f); e.setPadCanal (1, 4);
            if (conEq)
            {
                for (int c : { 0, 4 })
                {
                    e.setEqMix (c, 1.0f);
                    e.setCanalSend (c, AudioEngine::kFxEq, 1.0f);
                }
                e.setEqBand (0, 2, 1000.0f, +12.0f);
                e.setEqBand (4, 2, 1000.0f, -12.0f);
            }
            //  Un tono de 1 kHz, o sea justo el centro de la banda que se mueve.
            e.publishSample (pad, tonoPlano (kFs, 0.60, 1000.0));

            juce::AudioBuffer<float> b (2, kBs);
            b.clear(); e.renderNextBlock (b, 0, kBs);
            e.postNoteOn (pad, 1.0f);

            double a = 0.0; int n = 0;
            for (int blk = 0; blk < 20; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                //  Los cuatro primeros fuera: el envio se suaviza en 20 ms y
                //  ahi la ganancia todavia esta subiendo.
                if (blk < 4) continue;
                for (int i = 0; i < kBs; ++i)
                { const double x = b.getSample (0, i); a += x * x; ++n; }
            }
            return std::sqrt (a / (double) juce::jmax (1, n));
        };

        auto dB = [] (double a, double b)
        { return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b)); };

        const double s0 = rmsDe (0, false);
        const double s4 = rmsDe (1, false);
        const double e0 = rmsDe (0, true);
        const double e4 = rmsDe (1, true);

        const double sinEq = dB (s4, s0);
        const double c0    = dB (e0, s0);
        const double c4    = dB (e4, s4);

        const bool ok = std::abs (sinEq) < 0.5
                     && std::abs (c0 - 12.0) < 1.0
                     && std::abs (c4 + 12.0) < 1.0;
        std::printf ("%-34s sin EQ %+.2f dB   canal 0 %+.1f dB   canal 4 %+.1f dB   %s\n",
                     "el EQ es de cada canal", sinEq, c0, c4, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y LA OTRA MITAD: UN ENVIO SIGUE SIENDO DE TODOS.
    //
    //  Es la regla que ya estaba medida y publicada -«restringir el DLY a un
    //  canal es exactamente lo contrario de lo que un envio significa»- y lo
    //  que hay que comprobar es que la puerta de `fxParamDe` la escribe UNA
    //  vez: los cinco tipos que SUMAN colapsan al canal 0 escriba quien
    //  escriba, asi que pedir 180 ms desde el canal 0 y 60 desde el 4 deja los
    //  dos en 60.
    //
    //  Con DOS cifras y no una: la de ESTADO -por la misma puerta que lee el
    //  hilo de audio- y la de AUDIO, que es donde se ve que el numero llega al
    //  sitio. Solo la primera la cumple tambien una tabla que guarda bien y no
    //  la usa nadie.
    {
        //  Un impulso, y desde la muestra 1: la CERO es la unica que una voz no
        //  lee nunca -la interpolacion necesita un dato por detras-.
        auto clic = [] (double sr)
        {
            auto* sb = new SampleBuffer();
            sb->buffer.setSize (1, 8192);
            sb->buffer.clear();
            for (int i = 1; i < 9; ++i) sb->buffer.setSample (0, i, 1.0f);
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
 enCanalCero (e);
        e.setPadGain (0, 1.0f);
        e.setPadCanal (0, 0);
        e.setFxParam (0, AudioEngine::kFxDly, 0, 180.0f);   // el canal 0 pide 180 ms
        e.setFxParam (4, AudioEngine::kFxDly, 0,  60.0f);   // y el canal 4 pide 60
        e.setFxParam (0, AudioEngine::kFxDly, 1, 0.0f);     // sin realimentacion: UN eco
        e.setFxParam (0, AudioEngine::kFxDly, 2, 1.0f);
        e.setCanalSend (0, AudioEngine::kFxDly, 1.0f);
        e.publishSample (0, clic (kFs));

        const float ms0 = e.getFxParam (0, AudioEngine::kFxDly, 0);
        const float ms4 = e.getFxParam (4, AudioEngine::kFxDly, 0);

        //  Y EL ENVIO SE ASIENTA ANTES DE DISPARAR, que es la leccion que este
        //  banco ya tiene escrita para el limitador: sube con 20 ms de
        //  constante y un impulso dura 0.17 ms, asi que sin asentar el eco
        //  saldria a la ganancia con la que arrancaba la rampa.
        juce::AudioBuffer<float> b (2, kBs);
        for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, kBs); }

        e.postNoteOn (0, 1.0f);
        std::vector<float> v;
        for (int i = 0; i < 40; ++i)
        {
            b.clear(); e.renderNextBlock (b, 0, kBs);
            for (int n = 0; n < kBs; ++n) v.push_back (b.getSample (0, n));
        }

        //  El pico DESPUES del golpe directo, que son las ocho primeras
        //  muestras: lo que quede es el eco y donde cae dice su tiempo.
        int pico = 0; double mejor = 0.0;
        for (size_t i = 500; i < v.size(); ++i)
            if (std::abs ((double) v[i]) > mejor) { mejor = std::abs ((double) v[i]); pico = (int) i; }
        const double ecoMs = 1000.0 * (double) pico / kFs;

        const bool ok = std::abs (ms0 - 60.0f) < 0.5f && std::abs (ms4 - 60.0f) < 0.5f
                     && std::abs (ecoMs - 60.0) < 3.0;
        std::printf ("%-34s canal 0 %.1f ms   canal 4 %.1f ms   el eco cae en %.1f ms   %s\n",
                     "un envio sigue siendo de todos", ms0, ms4, ecoMs, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y LA MEDIDA QUE NO TENIA NADIE EN TODO EL BANCO: QUE UN INSERTO CAMBIE
    //  EL AUDIO EN UN CANAL QUE NO SEA EL CERO.
    //
    //  Viene de una queja de uso -«pongo el EQ en el canal 3, toco las cinco
    //  bandas y no hace nada»- y el fallo estaba en una linea: el reparto seco
    //  contra mojado leia la MEZCLA del efecto SIEMPRE del canal cero
    //  (`fxMixNow[kNumFx]`, una fila y no una tabla), asi que poner un inserto
    //  en el canal 3 escribia su mezcla en el 3 y el bloque de audio preguntaba
    //  al 0, que sigue con el defecto de fabrica: cero. Con la mezcla a cero el
    //  envio queda a cero, el bus no se marca como alimentado, la etapa NO SE
    //  EJECUTA — y por eso tampoco el analizador enseñaba nada, que lee ese bus
    //  muerto. Los CINCO que suman se salvaban por casualidad: su parametro es
    //  uno para toda la mesa, asi que el canal cero era la respuesta correcta.
    //
    //  Lo que este banco tenia y no bastaba: el camino de cada efecto medido
    //  EN EL CANAL 0 -que era justo el unico que funcionaba- y el enrutado
    //  medido por ESTADO. Las dos daban verde con el fallo dentro.
    //
    //  TRES cifras, y la primera es la que impide que esto se cumpla solo:
    //    · el efecto se OYE en el canal 0. Es el control; sin el, un tipo
    //      configurado en neutro cumpliria las otras dos sin hacer nada.
    //    · se oye en el canal 4, que es donde estaba el fallo.
    //    · y las dos salidas son LA MISMA, bit a bit: un inserto es el mismo
    //      aparato en los dos sitios, asi que el canal es un re-indice.
    {
        static constexpr const char* kNombre[AudioEngine::kNumFx] =
        { "FLT", "HPF", "DRV", "DLY", "BIT", "REV", "EQ", "CMP", "GTE", "DSS",
          "LIM", "CHO", "FLA", "PHA", "TRM", "RNG", "PIT", "WID", "EXC", "TRN",
          "FRZ", "WAH", "OCT" };

        //  Un tono con las dos mitades DISTINTAS. Con L == R el lado es cero y
        //  un ensanchador -que escala justo el lado- no cambiaria una muestra:
        //  saldria «no se oye» y no seria un fallo del motor sino de la fuente.
        auto estereo = [] (double sr, double seg, double hz)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * seg);
            sb->buffer.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const double t = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / sr;
                sb->buffer.setSample (0, i, 0.50f * (float) std::sin (t));
                sb->buffer.setSample (1, i, 0.45f * (float) std::sin (t + 0.7));
            }
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  LOS AJUSTES QUE HACEN QUE CADA TIPO SE OIGA, y solo los que el
        //  defecto de fabrica deja en neutro: un barrido en 0 no filtra, un
        //  techo en -1 dB no recorta un tono a -16 dBFS y una puerta en -40 dB
        //  no cierra. Los demas van con `kFxDef`, que es lo que la app carga.
        auto ajusta = [] (AudioEngine& e, int canal, int fx)
        {
            switch (fx)
            {
                case AudioEngine::kFxFlt: e.setFxParam (canal, fx, 0, -0.60f); break;
                case AudioEngine::kFxHpf: e.setFxParam (canal, fx, 0, 1200.0f); break;
                case AudioEngine::kFxGte: e.setFxParam (canal, fx, 0,  -6.0f); break;
                //  Y EL COMPRESOR CON UMBRAL DE VERDAD: con el de fabrica
                //  -18 dB sobre un tono a -19.5 dBFS RMS, la primera corrida
                //  daba 0.6 % de cambio, o sea por debajo del suelo de «se
                //  oye». No era un fallo del motor: era que la prueba lo tenia
                //  casi en reposo. Primero se duda de la prueba.
                case AudioEngine::kFxCmp: e.setFxParam (canal, fx, 0, -40.0f);
                                          e.setFxParam (canal, fx, 1,   8.0f); break;
                case AudioEngine::kFxLim: e.setFxParam (canal, fx, 0, -30.0f); break;
                //  El EQ no tiene sus bandas en `fxP`: las guarda el `Eq5` del
                //  canal. Es la misma banda que la queja movia.
                case AudioEngine::kFxEq:  e.setEqBand (canal, 2, 300.0f, +12.0f); break;
                default: break;
            }
        };

        auto corre = [&estereo, &ajusta] (int fx, int canal, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 0.30f);
            e.setPadCanal (0, canal);
            if (fx >= 0)
            {
                ajusta (e, canal, fx);
                e.setFxParam (canal, fx, 2, 1.0f);
                e.setCanalSend (canal, fx, 1.0f);
            }
            e.publishSample (0, estereo (kFs, 0.80, 300.0));

            juce::AudioBuffer<float> b (2, kBs);
            b.clear(); e.renderNextBlock (b, 0, kBs);
            e.postNoteOn (0, 1.0f);

            out.clear();
            for (int blk = 0; blk < 28; ++blk)
            {
                b.clear(); e.renderNextBlock (b, 0, kBs);
                //  Los ocho primeros fuera: el envio sube con 20 ms de
                //  constante y ahi las dos corridas comparten la rampa.
                if (blk < 8) continue;
                for (int i = 0; i < kBs; ++i) out.push_back (b.getSample (0, i));
            }
        };

        auto dif = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            const size_t n = juce::jmin (a.size(), b.size());
            double d = 0.0, r = 0.0;
            for (size_t i = 0; i < n; ++i)
            { const double e = (double) a[i] - (double) b[i]; d += e * e; r += (double) b[i] * (double) b[i]; }
            return std::sqrt (d / juce::jmax (1.0, (double) n))
                 / juce::jmax (1.0e-9, std::sqrt (r / juce::jmax (1.0, (double) n)));
        };

        std::vector<float> seco0, seco4, con0, con4;
        corre (-1, 0, seco0);
        corre (-1, 4, seco4);

        int suena0 = 0, suena4 = 0, iguales = 0, insertos = 0;
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            if (! AudioEngine::sustituye (f)) continue;   // los cinco que suman son de toda la mesa
            ++insertos;
            corre (f, 0, con0);
            corre (f, 4, con4);
            const double d0 = dif (con0, seco0);
            const double d4 = dif (con4, seco4);
            const double ig = dif (con4, con0);

            //  El 2 % es el suelo de «se oye»: por debajo de eso lo que se
            //  estaria midiendo es el ruido de la suavizacion, no el efecto.
            const bool bien = d0 > 0.02 && d4 > 0.02 && ig < 1.0e-6;
            if (bien) { ++suena0; ++suena4; ++iguales; }
            else
            {
                if (d0 > 0.02) ++suena0;
                if (d4 > 0.02) ++suena4;
                if (ig < 1.0e-6) ++iguales;
                std::printf ("   %-4s canal 0 cambia %.1f %%   canal 4 cambia %.1f %%   se separan %.2e\n",
                             kNombre[f], 100.0 * d0, 100.0 * d4, ig);
            }
        }

        const bool ok = suena0 == insertos && suena4 == insertos && iguales == insertos;
        std::printf ("%-34s se oyen en el 0: %d/%d   en el 4: %d/%d   iguales: %d/%d   %s\n",
                     "un inserto cambia el audio", suena0, insertos, suena4, insertos,
                     iguales, insertos, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  Y LOS CUATRO DE MODULACION: CADA CANAL CON SU VELOCIDAD, Y SIN
    //  SEPARARSE NUNCA.
    //
    //  Del telefono: «que el chorus, el phaser y el flanger puedan funcionar a
    //  diferentes velocidades, pero sincronizados». Son DOS cosas y se miden por
    //  separado, porque una sin la otra no vale nada: un LFO por canal cumple la
    //  primera y suspende la segunda —se separan—, y un LFO compartido cumple la
    //  segunda y suspende la primera —van todos igual—.
    //
    //  SE MIDE CON TRM porque es el unico de los cuatro cuya envolvente ES el
    //  LFO: un tremolo modula la ganancia, asi que el periodo se lee del audio
    //  sin inventar nada. Los otros tres llaman a `pasoMod` en la misma linea,
    //  o sea que comparten reloj por construccion y no por coincidencia.
    //
    //  Y LA TERCERA CIFRA ES LA QUE IMPORTA: a los SESENTA SEGUNDOS, uno a 1 Hz
    //  lleva 60 vueltas y otro a 4 Hz lleva 240, y los dos tienen que estar en
    //  el mismo sitio de la vuelta —el valle— en la MISMA muestra. Con fases
    //  acumuladas por separado, ahi es donde se ve la deriva: cada una arrastra
    //  su propio error de coma flotante durante 2.8 millones de muestras.
    {
        //  La envolvente por ventanas de un ciclo del tono, igual que la de TRM
        //  de mas arriba y por lo mismo: una ventana mas corta que un ciclo mide
        //  por donde cayo la ventana.
        const int vent = (int) (kFs / 300.0);

        auto tono = [] (double sr, double seg, double hz)
        {
            auto* sb = new SampleBuffer();
            const int n = (int) (sr * seg);
            sb->buffer.setSize (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float x = 0.50f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * hz * (double) i / sr);
                sb->buffer.setSample (0, i, x);
                sb->buffer.setSample (1, i, x);
            }
            sb->sourceSampleRate = sr;
            return SampleBuffer::Ptr (sb);
        };

        //  `tarde` son los segundos que el efecto tarda en ENCENDERSE, y es la
        //  mitad que hace que esto mida algo: con una fase acumulada por canal,
        //  encender el segundo tres segundos despues lo deja desfasado para
        //  siempre —el flanco de subida llamaba a `reinicia()`—. Con la fase
        //  derivada del reloj, encenderlo tarde no mueve donde esta.
        auto corre = [&tono, vent] (int canal, float rate, double segs, double tarde,
                                       std::vector<double>& env)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 0.50f);
            e.setPadCanal (0, canal);
            e.setFxParam (canal, AudioEngine::kFxTrm, 0, rate);
            e.setFxParam (canal, AudioEngine::kFxTrm, 1, 1.0f);   // PROF al maximo: el valle es cero
            e.setFxParam (canal, AudioEngine::kFxTrm, 2, 1.0f);
            if (tarde <= 0.0) e.setCanalSend (canal, AudioEngine::kFxTrm, 1.0f);
            e.publishSample (0, tono (kFs, segs + 1.0, 300.0));

            juce::AudioBuffer<float> b (2, kBs);
            b.clear(); e.renderNextBlock (b, 0, kBs);
            e.postNoteOn (0, 1.0f);
            const int bloqueEnciende = (int) (tarde * kFs) / kBs;

            //  La envolvente se calcula al vuelo: sesenta segundos guardados
            //  serian 46 MB por corrida y lo que hace falta son 53000 dobles.
            env.clear();
            double acc = 0.0; int n = 0;
            const int total = (int) (kFs * segs) / kBs;
            for (int blk = 0; blk < total; ++blk)
            {
                if (tarde > 0.0 && blk == bloqueEnciende)
                    e.setCanalSend (canal, AudioEngine::kFxTrm, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int i = 0; i < kBs; ++i)
                {
                    const double x = b.getSample (0, i);
                    acc += x * x;
                    if (++n == vent) { env.push_back (std::sqrt (acc / n)); acc = 0.0; n = 0; }
                }
            }
        };

        //  El periodo, contado en cruces de la envolvente por su punto medio.
        auto hzDe = [vent] (const std::vector<double>& env)
        {
            double pico = 0.0, valle = 1.0e9;
            for (double x : env) { pico = juce::jmax (pico, x); valle = juce::jmin (valle, x); }
            const double medio = 0.5 * (pico + valle);
            int primero = -1, ultimo = -1, vueltas = 0;
            for (size_t i = 1; i < env.size(); ++i)
                if (env[i - 1] >= medio && env[i] < medio)
                { if (primero < 0) primero = (int) i; else { ultimo = (int) i; ++vueltas; } }
            const double segs = (vueltas > 0 && ultimo > primero)
                                  ? (double) (ultimo - primero) * vent / kFs / vueltas : 0.0;
            return segs > 0.0 ? 1.0 / segs : 0.0;
        };

        //  DONDE CAE EL VALLE, en muestras desde que arranco la envolvente. Se
        //  busca el valle MAS TARDIO de la corrida —el que ha tenido sesenta
        //  segundos para irse de sitio— y se compara con el del otro canal.
        //
        //  Y se comparan entre SI y no contra una marca elegida: la primera
        //  version medía la distancia al segundo 60 y salia FALLA con el motor
        //  bien, porque el valle de un tremolo no cae en fase cero sino en 0.75
        //  —`valorEn(0)` es cero, no menos uno—. La prueba estaba mal, no el
        //  motor. Lo que importa no es donde cae el valle sino que los dos caigan
        //  en el mismo sitio.
        auto valleTardio = [vent] (const std::vector<double>& env)
        {
            double pico = 0.0, valle = 1.0e9;
            for (double x : env) { pico = juce::jmax (pico, x); valle = juce::jmin (valle, x); }
            const double umbral = valle + 0.05 * (pico - valle);
            int mejor = -1;
            for (int i = (int) env.size() - 1; i > 0; --i)
                if (env[(size_t) i] <= umbral) { mejor = i; break; }
            return mejor * vent;
        };

        const double kSegs = 60.0;
        std::vector<double> envA, envB, envC;
        corre (0, 1.0f, kSegs + 0.5, 0.0, envA);
        corre (4, 4.0f, kSegs + 0.5, 0.0, envB);
        //  Y EL TERCERO ES EL QUE MIDE LA SINCRONIA: misma velocidad que el
        //  primero, pero encendido MAS TARDE. Su valle tardio tiene que caer
        //  donde el del primero. Con una fase por canal no caeria: arrancaria en
        //  el flanco de subida y quedaria corrido para siempre.
        //
        //  Y EL RETRASO NO ES REDONDO A PROPOSITO. La primera version ponia
        //  TRES segundos, que a 1 Hz son tres vueltas EXACTAS: una fase por
        //  canal arrancada tres segundos tarde cae en el mismo sitio de la
        //  vuelta, asi que la prueba daba 480 muestras y casi pasaba con el
        //  defecto dentro. Con 3.25 el retraso es tres vueltas y CUARTO, o sea
        //  12000 muestras de desfase que no se pueden confundir con nada.
        corre (7, 1.0f, kSegs + 0.5, 3.25, envC);

        const double hzA = hzDe (envA);
        const double hzB = hzDe (envB);
        const int    vA  = valleTardio (envA);
        const int    vC  = valleTardio (envC);
        const int    desfase = vC - vA;

        //  El suelo es UNA ventana de envolvente -160 muestras a 48 kHz, o sea
        //  3.3 ms-. Un arranque tardio de tres segundos son 144000.
        const bool ok = std::abs (hzA - 1.0) < 0.10 && std::abs (hzB - 4.0) < 0.20
                     && std::abs (desfase) <= vent;
        std::printf ("%-34s canal 0 %.2f Hz   canal 4 %.2f Hz   encendido 3.25 s tarde se desfasa %d muestras   %s\n",
                     "cada canal su velocidad, y a una",
                     hzA, hzB, desfase, ok ? "OK" : zatiFalla());
    }

    // ------------------------------------------------------------------
    //  LA FILA DE CONTROL, que es la que sostiene todo lo demas: los 64 pads
    //  en el canal 0 contra los mismos 64 repartidos por los dieciseis CON LOS
    //  MISMOS AJUSTES. Con los dieciseis canales diciendo lo mismo, repartir es
    //  un re-indice de una tabla y nada mas, asi que la salida tiene que ser
    //  BIT A BIT la misma.
    //
    //  Y SOLO UN ENVIO ABIERTO, que es la parte que hay que decir en voz alta:
    //  un envio es global, asi que su estado es uno solo en las dos corridas.
    //  Con un INSERTO abierto la comparacion no valdria y no seria un fallo -
    //  sesenta y cuatro pads por UN filtro es `filtro(suma)` y cuatro por cada
    //  uno de dieciseis es `suma(filtro)`, y esas dos no son la misma cuenta en
    //  coma flotante ni tienen por que serlo.
    {
        auto corre = [&tonoPlano] (bool reparte, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (64, 2);
 enCanalCero (e);
            for (int c = 0; c < AudioEngine::kNumCanales; ++c)
            {
                e.setCanalSend (c, AudioEngine::kFxDly, 0.5f);
            }
            //  Y EL FADER DEL CANAL NO SE TOCA A PROPOSITO. La primera version
            //  lo ponia a uno en los dieciseis y con eso la prueba se hacia el
            //  trabajo del motor: con `canalGain` lleno solo para el canal 0
            //  -que es el fallo de `notaViva` en esta pieza- las dos corridas
            //  seguian saliendo identicas, porque el andamio tapaba el defecto
            //  que la fila existe para mirar. Lo que se compara son los
            //  DEFECTOS de los dieciseis, y esos los pone el constructor.
            e.setFxParam (0, AudioEngine::kFxDly, 0, 250.0f);
            e.setFxParam (0, AudioEngine::kFxDly, 1, 0.5f);
            e.setFxParam (0, AudioEngine::kFxDly, 2, 0.5f);

            for (int p = 0; p < AudioEngine::kNumPads; ++p)
            {
                e.setPadGain (p, 0.5f);
                e.setPadCanal (p, reparte ? (p % AudioEngine::kNumCanales) : 0);
                e.publishSample (p, tonoPlano (kFs, 0.20, 200.0 + 10.0 * (double) p));
            }

            juce::AudioBuffer<float> b (2, kBs);
            out.clear();
            const int total = (int) (kFs * 1.0) / kBs;
            for (int i = 0; i < total; ++i)
            {
                if (i == 30)
                    for (int p = 0; p < AudioEngine::kNumPads; ++p) e.postNoteOn (p, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int n = 0; n < kBs; ++n) out.push_back (b.getSample (0, n));
            }
        };

        std::vector<float> uno, dieciseis;
        corre (false, uno);
        corre (true,  dieciseis);
        int distintas = 0;
        for (size_t i = 0; i < uno.size() && i < dieciseis.size(); ++i)
            if (uno[i] != dieciseis[i]) ++distintas;
        const bool ok = distintas == 0 && ! uno.empty();
        std::printf ("%-34s %d de %d muestras cambian   %s\n",
                     "repartir por canales no cuesta", distintas, (int) uno.size(),
                     ok ? "OK" : zatiFalla());
    }

    //  EL SOLO DEL CANAL, que es lo que la mesa no tenia.
    //
    //  Llego del telefono -«modo solo por canal en el Mixer de canales
    //  tambien»- y donde estaba escrito que no iba era en un comentario que
    //  decia «dos ambitos de solo son dos respuestas a la misma pregunta».
    //  Dejo de ser cierto con treinta y dos canales: un canal es un GRUPO, y
    //  aislar la bateria con el solo del PAD pide acertar sus once pads.
    //
    //  CON DOS CIFRAS, porque una sola se engaña por los dos lados:
    //
    //    1. Poner SOLO en el canal 4 tiene que salir **bit a bit** igual que
    //       MUTEAR los otros treinta y uno. Bit a bit y no «parecido»: las dos
    //       cosas caen en el mismo `gCan` con el mismo suavizado de `kSend`, asi
    //       que si el camino es el mismo el resultado es identico; cualquier
    //       diferencia dice que el solo se metio por otro sitio -una rama nueva
    //       en el bucle, un salto sin suavizar-, que es exactamente el chasquido
    //       que no se puede permitir un control que se toca sonando.
    //
    //    2. Y tiene que salir DISTINTO de no poner nada. Sin esta, la primera la
    //       cumple un `setCanalSolo` que no hace absolutamente nada: si el solo
    //       se ignora, «solo en el 4» y «mute en los otros 31» solo coinciden
    //       cuando los dos son el silencio... y no, coinciden cuando los dos son
    //       el sonido entero. Una prueba que pasa con la funcion vacia no es una
    //       prueba.
    {
        enum Modo { nada, conSolo, conMutes };
        auto corre = [&tonoPlano] (Modo m, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs); e.setPolyphony (64, 2);
 enCanalCero (e);
            for (int p = 0; p < AudioEngine::kNumPads; ++p)
            {
                e.setPadGain (p, 0.5f);
                e.setPadCanal (p, p % AudioEngine::kNumCanales);
                e.publishSample (p, tonoPlano (kFs, 0.20, 200.0 + 10.0 * (double) p));
            }
            if (m == conSolo)  e.setCanalSolo (4, true);
            if (m == conMutes)
                for (int c = 0; c < AudioEngine::kNumCanales; ++c)
                    if (c != 4) e.setCanalMute (c, true);

            juce::AudioBuffer<float> b (2, kBs);
            out.clear();
            const int total = (int) (kFs * 1.0) / kBs;
            for (int i = 0; i < total; ++i)
            {
                if (i == 30)
                    for (int p = 0; p < AudioEngine::kNumPads; ++p) e.postNoteOn (p, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int n = 0; n < kBs; ++n) out.push_back (b.getSample (0, n));
            }
        };

        std::vector<float> libre, solo, mutes;
        corre (nada, libre); corre (conSolo, solo); corre (conMutes, mutes);
        int difMute = 0, difLibre = 0;
        for (size_t i = 0; i < solo.size(); ++i)
        {
            if (i < mutes.size() && solo[i] != mutes[i]) ++difMute;
            if (i < libre.size() && solo[i] != libre[i]) ++difLibre;
        }
        const bool ok = difMute == 0 && difLibre > 0 && ! solo.empty();
        std::printf ("%-34s %d contra mutear los otros, %d contra no hacer nada   %s\n",
                     "solo de canal", difMute, difLibre, ok ? "OK" : zatiFalla());
    }

    //  EL REBOTE SE LLEVA EL SOLO. No era un fallo: era un descubierto.
    //
    //  Esta comprobacion nacio de una afirmacion equivocada —«`copyStateFrom`
    //  copia `padSolo` pero no `soloActive`, asi que un rebote con un pad en
    //  SOLO sale con los sesenta y cuatro sonando»— y la rotura a proposito la
    //  desmintio en la primera pasada: quitar el `refreshSolo` que se acababa de
    //  añadir no cambiaba una sola muestra, porque `copyStateFrom` YA llamaba a
    //  `refreshSolo` veinte lineas mas abajo. *Primero se duda de la prueba*, y
    //  aqui de quien la escribio.
    //
    //  Se queda porque lo que mide no lo media nadie, y ahora hay DOS bits
    //  cacheados que mantener en pie -`soloActive` y `canalSoloActive`- donde
    //  antes habia uno. Rota de verdad -comentando las dos lineas de
    //  `copyStateFrom`- sale 9589 contra el original y 0 contra uno sin solo,
    //  o sea el rebote exportando la cancion entera con un pad aislado en
    //  pantalla. Es la figura de «la mascara con los envios» que este fichero ya
    //  tiene escrita dos veces: todo dato que decida si algo SUENA tiene que
    //  viajar con el que dice cuanto.
    //
    //  Con DOS cifras y las dos hacen falta: el rebote tiene que salir IGUAL que
    //  el motor de origen -eso es lo que un rebote significa- y DISTINTO de un
    //  motor sin el solo puesto, que es lo que impide que la primera la cumpla
    //  un codigo que ignora el solo en los dos lados.
    {
        auto siembra = [&tonoPlano] (AudioEngine& e, bool conSolo)
        {
            e.prepareToPlay (kFs, kBs); e.setPolyphony (64, 2);
 enCanalCero (e);
            for (int p = 0; p < AudioEngine::kNumPads; ++p)
            {
                e.setPadGain (p, 0.5f);
                e.publishSample (p, tonoPlano (kFs, 0.20, 200.0 + 10.0 * (double) p));
            }
            if (conSolo) { e.setPadSolo (3, true); e.setCanalSolo (0, true); }
        };
        auto suena = [] (AudioEngine& e, std::vector<float>& out)
        {
            juce::AudioBuffer<float> b (2, kBs);
            out.clear();
            const int total = (int) (kFs * 1.0) / kBs;
            for (int i = 0; i < total; ++i)
            {
                if (i == 30)
                    for (int p = 0; p < AudioEngine::kNumPads; ++p) e.postNoteOn (p, 1.0f);
                b.clear(); e.renderNextBlock (b, 0, kBs);
                for (int n = 0; n < kBs; ++n) out.push_back (b.getSample (0, n));
            }
        };

        AudioEngine origen; siembra (origen, true);
        AudioEngine rebote; siembra (rebote, false);
        rebote.copyStateFrom (origen);
        AudioEngine sinSolo; siembra (sinSolo, false);

        std::vector<float> vOrigen, vRebote, vSinSolo;
        suena (origen, vOrigen); suena (rebote, vRebote); suena (sinSolo, vSinSolo);
        int difOrigen = 0, difSin = 0;
        for (size_t i = 0; i < vRebote.size(); ++i)
        {
            if (i < vOrigen.size()  && vRebote[i] != vOrigen[i])  ++difOrigen;
            if (i < vSinSolo.size() && vRebote[i] != vSinSolo[i]) ++difSin;
        }
        const bool ok = difOrigen == 0 && difSin > 0 && ! vRebote.empty();
        std::printf ("%-34s %d contra el original, %d contra uno sin solo   %s\n",
                     "el rebote se lleva el solo", difOrigen, difSin,
                     ok ? "OK" : zatiFalla());
    }
    }

    // -----------------------------------------------------------------
    //  EL MONITOR: oirte por los cascos mientras grabas.
    //
    //  Se pidio «grabar voces con el micro con cascos, mientras escucho la
    //  produccion», y la mitad que faltaba era esta: lo que entra por el
    //  microfono se copiaba a `recordBuffer` y MORIA en el `out.clear` de la
    //  etapa 2, o sea que se cantaba a ciegas.
    //
    //  CON DOS CIFRAS, que es lo unico que separa las dos formas de escribirlo
    //  mal y las dos parecen bien: que la entrada SALGA -eso solo lo cumple un
    //  camino conectado- y que la TOMA salga BIT A BIT igual con monitor y sin
    //  el -eso solo lo cumple no meterse en el camino de grabacion-. Con una
    //  sola, un monitor que ademas se imprime dentro de la toma pasaria.
    //
    //  La entrada se inyecta en el mismo buffer que `renderNextBlock` recibe,
    //  que es EXACTAMENTE como llega en el aparato: JUCE entrega un solo
    //  buffer con la entrada dentro y la etapa 2 lo borra.
    {
        constexpr double kFs = 48000.0;
        constexpr int    kBs = 128;
        constexpr int    kBloques = 40;

        auto corre = [] (float monitor, std::vector<float>& salida,
                         std::vector<float>& toma)
        {
            AudioEngine e; e.prepareToPlay (kFs, kBs, 1);
            e.setMonitor (monitor);
            e.startRecording (0);

            juce::AudioBuffer<float> b (2, kBs);
            salida.clear();
            for (int i = 0; i < kBloques; ++i)
            {
                //  La «entrada del microfono»: un tono que no se parece a nada
                //  de lo que la maquina produce, para que aparecer en la salida
                //  solo pueda significar que ha pasado por el monitor.
                b.clear();
                for (int n = 0; n < kBs; ++n)
                {
                    const double t = (double) (i * kBs + n) / kFs;
                    b.setSample (0, n, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi
                                                                  * 1000.0 * t));
                }
                e.renderNextBlock (b, 0, kBs);
                for (int n = 0; n < kBs; ++n) salida.push_back (b.getSample (0, n));
            }

            auto sb = e.finishRecording();
            toma.clear();
            if (sb != nullptr)
                for (int n = 0; n < sb->buffer.getNumSamples(); ++n)
                    toma.push_back (sb->buffer.getSample (0, n));
        };

        std::vector<float> sinMon, conMon, tomaSin, tomaCon;
        corre (0.0f, sinMon,  tomaSin);
        corre (1.0f, conMon,  tomaCon);

        auto pico = [] (const std::vector<float>& v)
        {
            float m = 0.0f;
            //  Los ultimos bloques, que la rampa del monitor sube con la misma
            //  constante que los envios -20 ms- y medir desde la primera
            //  muestra seria medir la rampa y no el monitor.
            for (size_t i = v.size() / 2; i < v.size(); ++i) m = juce::jmax (m, std::abs (v[i]));
            return m;
        };

        const float apagado = pico (sinMon);
        const float puesto  = pico (conMon);

        int tomaDistintas = 0;
        for (size_t i = 0; i < tomaSin.size() && i < tomaCon.size(); ++i)
            if (tomaSin[i] != tomaCon[i]) ++tomaDistintas;

        const bool ok = apagado < 1.0e-6f && puesto > 0.40f
                        && tomaDistintas == 0 && ! tomaSin.empty();
        std::printf ("%-34s apagado %.5f  puesto %.5f  la toma cambia en %d de %d   %s\n",
                     "el monitor sale por los cascos", apagado, puesto,
                     tomaDistintas, (int) tomaSin.size(), ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  EL AMBIENTE PONE SUS OCHO REFLEXIONES DONDE DICE, y las mueve con los
    //  dos mandos.
    //
    //  El visor de AMB dibuja las ocho tomas de `AudioEngine::kAmbMsL`, o sea
    //  que la cara AFIRMA donde estan. Esto lo comprueba contra el audio: se
    //  mete un CLIC de una muestra por el envio y se buscan los picos.
    //
    //  Un clic y no un tono a proposito: con un tono sostenido las ocho
    //  reflexiones se suman con el directo y lo que sale es un peine, del que
    //  no se puede leer DONDE esta cada una. La respuesta al impulso si.
    //
    //  Y SE MIDE DOS VECES, con el tamano al minimo y al maximo: una sola
    //  medida la cumple tambien una linea de retardo fija, que es justo lo que
    //  esto no puede ser.
    {
        auto reflexiones = [] (float tam, float preMs, std::vector<float>& out)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setFxParam (0, AudioEngine::kFxAmb, 0, tam);
            e.setFxParam (0, AudioEngine::kFxAmb, 1, preMs);
            e.setFxParam (0, AudioEngine::kFxAmb, 2, 1.0f);
            e.setCanalSend (0, AudioEngine::kFxAmb, 1.0f);

            //  UN GOLPE DE DOS MILISEGUNDOS y no una muestra suelta: la voz
            //  entra con su rampa -milisegundos- asi que un impulso de una
            //  muestra sale multiplicado por casi cero y la medida no media
            //  nada. Medido: con una muestra el pico de toda la corrida no
            //  llegaba a 0.05 y la busqueda del directo devolvia -1.
            //
            //  Dos milisegundos siguen siendo corto contra la separacion entre
            //  tomas, que con el tamano a la mitad es de cinco.
            auto* sb = new SampleBuffer();
            sb->buffer.setSize (2, 4800);
            sb->buffer.clear();
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 96; ++i) sb->buffer.setSample (c, i, 1.0f);
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, SampleBuffer::Ptr (sb));

            juce::AudioBuffer<float> b (2, 512);
            //  El envio se asienta antes de disparar: se cruza en 20 ms.
            for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, 512); }
            e.postNoteOn (0, 1.0f);
            out.clear();
            for (int i = 0; i < 40; ++i)
            {
                b.clear();
                e.renderNextBlock (b, 0, 512);
                for (int n = 0; n < 512; ++n) out.push_back (std::abs (b.getSample (0, n)));
            }
        };

        //  Donde cae el pico mas alto dentro de una ventana de +-1 ms alrededor
        //  de la muestra esperada. Mas estrecho seria pedirle a un clic que
        //  pase por la interpolacion de un pad sin correrse ni una muestra.
        auto hayPicoEn = [] (const std::vector<float>& v, int cero, double ms, float minimo)
        {
            const int c = cero + (int) std::lround (ms * 48.0);
            const int r = 72;   // +-1.5 ms
            float m = 0.0f;
            for (int i = juce::jmax (0, c - r); i < juce::jmin ((int) v.size(), c + r); ++i)
                m = juce::jmax (m, v[(size_t) i]);
            return m >= minimo;
        };

        auto ceroDe = [] (const std::vector<float>& v)
        {
            for (size_t i = 0; i < v.size(); ++i) if (v[i] > 0.05f) return (int) i;
            return -1;
        };

        int aciertos = 0, esperados = 0;
        double primeraCorta = 0.0, primeraLarga = 0.0;

        for (int caso = 0; caso < 2; ++caso)
        {
            //  MEDIO Y ENTERO, y no cero y entero: con el tamano al minimo
            //  las ocho tomas caen entre 2.8 y 23 ms -dos milisegundos de
            //  separacion- y el golpe de prueba ya dura dos. A la mitad la
            //  separacion es de cinco y se leen sueltas.
            const float tam = (caso == 0) ? 0.5f : 1.0f;
            std::vector<float> v;
            reflexiones (tam, 0.0f, v);
            const int cero = ceroDe (v);
            if (cero < 0) { ++esperados; continue; }

            const double esc = (double) AudioEngine::ambEscala (tam);
            (caso == 0 ? primeraCorta : primeraLarga) = AudioEngine::kAmbMsL[0] * esc;

            for (int t = 0; t < AudioEngine::kAmbTomas; ++t)
            {
                ++esperados;
                //  El liston es la mitad de la ganancia de la toma: por el
                //  camino hay un pad, un canal y el cruce del envio, y lo que
                //  se mide es que la reflexion ESTA ahi y no cuanto pesa.
                if (hayPicoEn (v, cero, AudioEngine::kAmbMsL[t] * esc,
                               0.5f * AudioEngine::kAmbGan[t]))
                    ++aciertos;
            }
        }

        const bool ok = (aciertos == esperados) && (primeraLarga > primeraCorta * 1.5);
        std::printf ("%-34s %d de %d reflexiones en su sitio, la primera de %.1f a %.1f ms   %s\n",
                     "el ambiente y sus ocho tomas", aciertos, esperados,
                     primeraCorta, primeraLarga, ok ? "OK" : zatiFalla());
    }

    //  ------------------------------------------------------------------
    //  LOS SEIS QUE LLEVAN EL CATALOGO A CINCO POR FAMILIA.
    //
    //  Cada uno se mide por lo que lo SEPARA del que ya existia, que es la
    //  unica pregunta que importa: si FLD se puede confundir con DRV o REP con
    //  FRZ, el efecto no hacia falta. Y cada medida cruza DOS ajustes, porque
    //  una sola la cumple tambien un efecto que no hace nada.
    {
        //  Un motor con un pad enrutado al efecto que toque y la salida
        //  recogida entera. `hazMuestra` decide que suena.
        auto corre6 = [] (int fx, float p0, float p1,
                          const std::function<void (juce::AudioBuffer<float>&, double)>& hazMuestra,
                          std::vector<float>& sL, std::vector<float>& sR, int bloques = 40)
        {
            AudioEngine e; e.prepareToPlay (48000.0, 512); e.setPolyphony (8, 2);
            enCanalCero (e);
            e.setPadGain (0, 1.0f);
            e.setFxParam (0, fx, 0, p0);
            e.setFxParam (0, fx, 1, p1);
            e.setFxParam (0, fx, 2, 1.0f);      // MIX al maximo
            e.setCanalSend (0, fx, 1.0f);

            auto* sb = new SampleBuffer();
            sb->buffer.setSize (2, 96000);
            sb->buffer.clear();
            hazMuestra (sb->buffer, 48000.0);
            sb->sourceSampleRate = 48000.0;
            e.publishSample (0, SampleBuffer::Ptr (sb));

            juce::AudioBuffer<float> b (2, 512);
            //  El envio se cruza en 20 ms: se asienta antes de disparar.
            for (int i = 0; i < 30; ++i) { b.clear(); e.renderNextBlock (b, 0, 512); }
            e.postNoteOn (0, 1.0f);
            sL.clear(); sR.clear();
            for (int i = 0; i < bloques; ++i)
            {
                b.clear();
                e.renderNextBlock (b, 0, 512);
                for (int n = 0; n < 512; ++n) { sL.push_back (b.getSample (0, n)); sR.push_back (b.getSample (1, n)); }
            }
        };

        auto tono = [] (float hz) {
            return [hz] (juce::AudioBuffer<float>& b, double sr)
            {
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < b.getNumSamples(); ++i)
                        b.setSample (c, i, 0.6f * std::sin (juce::MathConstants<float>::twoPi
                                                            * hz * (float) i / (float) sr));
            };
        };

        //  La energia de una banda estrecha, por Goertzel. Se mide sobre el
        //  regimen -la segunda mitad- y no sobre el ataque.
        auto energiaEn = [] (const std::vector<float>& v, float hz)
        {
            const size_t a = v.size() / 2;
            const double w = 2.0 * juce::MathConstants<double>::pi * hz / 48000.0;
            const double coef = 2.0 * std::cos (w);
            double s1 = 0.0, s2 = 0.0;
            for (size_t i = a; i < v.size(); ++i)
            {
                const double s = (double) v[i] + coef * s1 - s2;
                s2 = s1; s1 = s;
            }
            return std::sqrt (s1 * s1 + s2 * s2 - coef * s1 * s2) / (double) (v.size() - a);
        };

        std::vector<float> aL, aR, bL, bR;

        // --- FRM: la vocal MUEVE los formantes. -----------------------
        //
        //  Con la A (vocal 0) el primer formante esta en 730 Hz y con la I
        //  (vocal 0.5) en 270. Se pasan los DOS tonos por las DOS vocales y lo
        //  que tiene que cruzarse es cual pasa mas: si el filtro estuviera
        //  clavado, el mismo tono ganaria las dos veces.
        {
            double a730 = 0.0, a270 = 0.0, i730 = 0.0, i270 = 0.0;
            corre6 (AudioEngine::kFxFrm, 0.00f, 2.5f, tono (730.0f), aL, aR); a730 = energiaEn (aL, 730.0f);
            corre6 (AudioEngine::kFxFrm, 0.00f, 2.5f, tono (270.0f), aL, aR); a270 = energiaEn (aL, 270.0f);
            corre6 (AudioEngine::kFxFrm, 0.50f, 2.5f, tono (730.0f), bL, bR); i730 = energiaEn (bL, 730.0f);
            corre6 (AudioEngine::kFxFrm, 0.50f, 2.5f, tono (270.0f), bL, bR); i270 = energiaEn (bL, 270.0f);

            const bool ok = (a730 > a270 * 1.5) && (i270 > i730 * 1.5);
            std::printf ("%-34s A: 730Hz %.4f / 270Hz %.4f   I: 730Hz %.4f / 270Hz %.4f   %s\n",
                         "la vocal de FRM se mueve", a730, a270, i730, i270, ok ? "OK" : zatiFalla());
        }

        // --- FLD: pliega, no recorta. ---------------------------------
        //
        //  Se cuentan los CRUCES POR CERO de un seno. Un recortador no anade
        //  ni uno -aplana las puntas y la onda sigue cruzando dos veces por
        //  periodo- y un plegador los multiplica, porque cada pliegue devuelve
        //  la onda hacia el otro lado. Es la medida que separa FLD de DRV.
        {
            auto cruces = [] (const std::vector<float>& v)
            {
                int n = 0;
                for (size_t i = v.size() / 2 + 1; i < v.size(); ++i)
                    if ((v[i - 1] <= 0.0f) != (v[i] <= 0.0f)) ++n;
                return n;
            };
            corre6 (AudioEngine::kFxFld, 0.0f, 20000.0f, tono (220.0f), aL, aR);
            corre6 (AudioEngine::kFxFld, 1.0f, 20000.0f, tono (220.0f), bL, bR);
            //  EL LISTON SALE DE LO QUE ESTO AFIRMA y no de lo que salio.
            //
            //  Un recortador da EXACTAMENTE los mismos cruces que la onda
            //  limpia -aplana las puntas y sigue cruzando dos veces por
            //  periodo-, o sea razon 1.00 clavada. Asi que lo que prueba que
            //  esto pliega es que la razon se despegue de uno, y el doble es el
            //  margen que un recuento no alcanza por ruido.
            //
            //  Estuvo en x3 y salio x3.0 -281 contra 282 pedidos-, o sea un
            //  liston puesto a ojo que suspendia por un cruce a un efecto que
            //  funciona. Un numero elegido sin derivar es un numero que un dia
            //  dice que no por su cuenta.
            const int c0 = cruces (aL), c1 = cruces (bL);
            const bool ok = (c0 > 0) && (c1 >= c0 * 2);
            std::printf ("%-34s sin pliegue %d cruces, plegado %d (x%.1f)   %s\n",
                         "FLD pliega y no recorta", c0, c1,
                         c0 > 0 ? (double) c1 / (double) c0 : 0.0, ok ? "OK" : zatiFalla());
        }

        // --- ROT: dos altavoces y no uno. -----------------------------
        //
        //  Lo que separa una Leslie de un tremolo con panoramica es que los dos
        //  lados van en CONTRA: cuando la bocina viene por la izquierda, se va
        //  por la derecha. Se mide la correlacion de las dos envolventes y
        //  tiene que salir NEGATIVA; con un solo altavoz saldria positiva.
        {
            auto envolvente = [] (const std::vector<float>& v, std::vector<double>& out)
            {
                out.clear();
                const size_t paso = 64;
                for (size_t i = v.size() / 2; i + paso < v.size(); i += paso)
                {
                    double m = 0.0;
                    for (size_t k = 0; k < paso; ++k) m = juce::jmax (m, (double) std::abs (v[i + k]));
                    out.push_back (m);
                }
            };
            corre6 (AudioEngine::kFxRot, 5.5f, 1.0f, tono (440.0f), aL, aR);
            std::vector<double> eL, eR;
            envolvente (aL, eL); envolvente (aR, eR);

            double mL = 0.0, mR = 0.0;
            for (auto v : eL) mL += v;  mL /= juce::jmax ((size_t) 1, eL.size());
            for (auto v : eR) mR += v;  mR /= juce::jmax ((size_t) 1, eR.size());
            double num = 0.0, dL = 0.0, dR = 0.0;
            for (size_t i = 0; i < eL.size() && i < eR.size(); ++i)
            {
                num += (eL[i] - mL) * (eR[i] - mR);
                dL  += (eL[i] - mL) * (eL[i] - mL);
                dR  += (eR[i] - mR) * (eR[i] - mR);
            }
            const double r = (dL > 1.0e-12 && dR > 1.0e-12) ? num / std::sqrt (dL * dR) : 1.0;
            const bool ok = r < -0.3;
            std::printf ("%-34s las dos envolventes correlan %+.3f (liston -0.30)   %s\n",
                         "ROT lleva dos altavoces", r, ok ? "OK" : zatiFalla());
        }

        // --- PNG: el eco cambia de lado. ------------------------------
        //
        //  Un golpe SOLO por la izquierda. Con DLY las dos repeticiones
        //  saldrian por la izquierda; aqui la primera sale por la izquierda y
        //  la segunda por la derecha, que es el efecto entero.
        {
            auto clicIzq = [] (juce::AudioBuffer<float>& b, double sr)
            {
                juce::ignoreUnused (sr);
                for (int i = 0; i < 96; ++i) b.setSample (0, i, 1.0f);
            };
            corre6 (AudioEngine::kFxPng, 200.0f, 0.80f, clicIzq, aL, aR);

            auto picoEn = [] (const std::vector<float>& v, int cero, double ms)
            {
                const int c = cero + (int) std::lround (ms * 48.0);
                const int r = 240;   // +-5 ms
                float m = 0.0f;
                for (int i = juce::jmax (0, c - r); i < juce::jmin ((int) v.size(), c + r); ++i)
                    m = juce::jmax (m, std::abs (v[(size_t) i]));
                return m;
            };
            int cero = -1;
            for (size_t i = 0; i < aL.size(); ++i) if (std::abs (aL[i]) > 0.05f) { cero = (int) i; break; }

            const float uno  = (cero >= 0) ? picoEn (aL, cero, 200.0) : 0.0f;   // izquierda a 200 ms
            const float unoD = (cero >= 0) ? picoEn (aR, cero, 200.0) : 0.0f;
            const float dos  = (cero >= 0) ? picoEn (aR, cero, 400.0) : 0.0f;   // derecha a 400 ms
            const bool ok = cero >= 0 && uno > 0.05f && dos > 0.05f && uno > unoD * 3.0f;
            std::printf ("%-34s 200ms izq %.3f der %.3f, 400ms der %.3f   %s\n",
                         "PNG rebota de lado", uno, unoD, dos, ok ? "OK" : zatiFalla());
        }

        // --- DUC: el bombeo baja lo que dice. -------------------------
        //
        //  Con un tono sostenido, la envolvente tiene que caer a `1-prof` y
        //  volver. Se mide el recorrido y se contrasta con la cuenta: a 0.70 la
        //  razon entre lo alto y lo bajo es 1/0.30, o sea 3.33.
        {
            //  SE GRABA MAS DE UN CICLO, que es lo que la primera version no
            //  hacia: cuarenta bloques son 0.43 s y a 2 Hz un ciclo dura 0.5,
            //  asi que la ventana no llegaba a contener el valle y el pico a la
            //  vez. Salia x1.93 contra la cuenta de 3.33 y parecia un fallo del
            //  efecto. Ciento cincuenta bloques son 1.6 s, o sea tres ciclos, y
            //  se mide el recorrido ENTERO: asi no hace falta saber donde
            //  empieza el ciclo, que es una cuenta que el reloj del motor no
            //  tiene por que compartir con esto.
            auto recorrido = [] (const std::vector<float>& v, double& alto, double& bajo)
            {
                alto = 0.0; bajo = 1.0e9;
                const size_t paso = 256;
                for (size_t i = v.size() / 3; i + paso < v.size(); i += paso)
                {
                    double m = 0.0;
                    for (size_t k = 0; k < paso; ++k) m = juce::jmax (m, (double) std::abs (v[i + k]));
                    alto = juce::jmax (alto, m);
                    bajo = juce::jmin (bajo, m);
                }
            };
            double a0 = 0.0, b0 = 0.0, a7 = 0.0, b7 = 0.0;
            corre6 (AudioEngine::kFxDuc, 2.0f, 0.0f,  tono (440.0f), aL, aR, 150); recorrido (aL, a0, b0);
            corre6 (AudioEngine::kFxDuc, 2.0f, 0.70f, tono (440.0f), bL, bR, 150); recorrido (bL, a7, b7);

            const double r0 = (b0 > 1.0e-9) ? a0 / b0 : 0.0;
            const double r7 = (b7 > 1.0e-9) ? a7 / b7 : 0.0;
            const bool ok = r0 < 1.30 && r7 > 2.50;
            std::printf ("%-34s sin bombeo x%.2f, al 0.70 x%.2f (cuenta 3.33)   %s\n",
                         "DUC baja lo que dice", r0, r7, ok ? "OK" : zatiFalla());
        }

        // --- REP: repite lo que acaba de pasar. -----------------------
        //
        //  SE BUSCA LA REPETICION EXACTA, que es literalmente lo que esto
        //  afirma. Se manda RUIDO -una secuencia que no se repite nunca por su
        //  cuenta- y se cuentan las muestras que salen identicas a las de hace
        //  `lag`. Con el efecto puesto, el trozo grabado vuelve a salir bit a
        //  bit y aparecen miles; sin el, ninguna.
        //
        //  La primera version mandaba una RAMPA y contaba las caidas, y no
        //  podia funcionar: con el trozo a 0.4 de un ciclo de 12000 muestras,
        //  la rampa de un segundo solo sube 0.09 dentro del trozo, o sea que el
        //  reinicio nunca llegaba al liston de 0.20. La medida decia «1 contra
        //  1» con el efecto funcionando — lo confirmo una sonda que enseño el
        //  cambio a repetir en la muestra 9090 de cada vuelta.
        {
            auto ruido = [] (juce::AudioBuffer<float>& b, double sr)
            {
                juce::ignoreUnused (sr);
                //  Semilla fija: dos corridas tienen que dar el mismo ruido, o
                //  lo que se compara son dos ruidos distintos.
                juce::Random r (20260919);
                for (int i = 0; i < b.getNumSamples(); ++i)
                {
                    const float v = 0.6f * (2.0f * r.nextFloat() - 1.0f);
                    b.setSample (0, i, v);
                    b.setSample (1, i, v);
                }
            };
            //  El trozo dura `1 - cantidad` de la vuelta: a 4 Hz son 12000
            //  muestras por vuelta y con 0.60 se graban 4800. Se barre un
            //  entorno porque el corte cae donde el reloj lo ponga, no en una
            //  muestra que esto pueda calcular.
            auto repeticiones = [] (const std::vector<float>& v)
            {
                int mejor = 0;
                for (int lag = 4600; lag <= 5000; ++lag)
                {
                    int n = 0;
                    for (size_t i = (size_t) lag + v.size() / 3; i < v.size(); ++i)
                        if (std::abs (v[i] - v[i - (size_t) lag]) < 1.0e-7f) ++n;
                    mejor = juce::jmax (mejor, n);
                }
                return mejor;
            };
            corre6 (AudioEngine::kFxRep, 4.0f, 0.00f, ruido, aL, aR, 150);
            corre6 (AudioEngine::kFxRep, 4.0f, 0.60f, ruido, bL, bR, 150);

            const int sinRep = repeticiones (aL), conRep = repeticiones (bL);
            const bool ok = sinRep < 100 && conRep > 1000;
            std::printf ("%-34s el ruido se repite en %d muestras sin el y %d con el   %s\n",
                         "REP repite el trozo", sinRep, conRep, ok ? "OK" : zatiFalla());
        }
    }

    std::printf ("\n%-34s %d FALLA\n", "motor", zatiFallos);
    return zatiFallos > 0 ? 1 : 0;
}

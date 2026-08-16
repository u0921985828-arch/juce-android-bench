// ZATI — el banco de CPU, por etapas.
//
// "Optimizar la CPU" sin medir es elegir a ojo cual de veinte bucles es el
// caro, y en este motor la respuesta no es la que parece: el sitio donde mas
// codigo hay -el reparto de envios, 16x6 por bloque- cuesta menos que una
// sola voz en modo TONO, que hace una autocorrelacion de 192 muestras unas
// cuarenta veces por grano. Por eso este banco no da un numero, da una
// TABLA: cada etapa aislada contra el mismo silencio, y la diferencia es lo
// que cuesta esa etapa y nada mas.
//
// La regla de lectura es el presupuesto: a 48 kHz con rafagas de 128 muestras
// un bloque tiene 2.67 ms para salir. Lo que se imprime como "carga" es
// cuanto de ese bloque se gasta. Un telefono no es este ordenador, asi que el
// valor absoluto no dice si va a ir; lo que dice es CUAL ETAPA se lleva el
// presupuesto, y esa proporcion si viaja.
//
//   cmake -B build -DZATI_BENCH=ON && cmake --build build --target Cpu
//   ./build/Cpu_artefacts/Cpu
#include <JuceHeader.h>
#include "../Source/AudioEngine.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using Clock = std::chrono::steady_clock;

//  Dieciseis senos en fase suman dieciseis veces un seno, que ningun sampler
//  toca y que mete el saturador del master en accion permanente - medir eso
//  seria medir la senal de prueba. Cada pad con su fase y su suelo de ruido.
static SampleBuffer::Ptr makeSample (double sr, double seconds, float freq)
{
    auto* sb = new SampleBuffer();
    const int n = (int) (sr * seconds);
    juce::Random rng ((juce::int64) (freq * 1000.0f));
    const float phase = rng.nextFloat() * juce::MathConstants<float>::twoPi;
    sb->buffer.setSize (2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
        {
            const float env  = std::exp (-1.2f * (float) i / (float) n);
            const float tone = std::sin (phase + juce::MathConstants<float>::twoPi * freq * (float) i / (float) sr);
            sb->buffer.setSample (c, i, 0.6f * env * (0.8f * tone + 0.2f * (rng.nextFloat() * 2.0f - 1.0f)));
        }
    sb->sourceSampleRate = sr;
    return SampleBuffer::Ptr (sb);
}

static constexpr double kSr   = 48000.0;
static constexpr int    kBs   = 128;
static constexpr double kBudget = 1000.0 * (double) kBs / kSr;   // 2.67 ms

//  La MEDIANA de los bloques, no la media: en un portatil con gobernador de
//  frecuencia y otros procesos, la media la mueve un solo bloque interrumpido
//  y dos corridas seguidas del MISMO codigo salen un 30 % distintas. La
//  mediana no se entera de esos, y es la que hace comparables dos filas de
//  esta tabla. El peor bloque se imprime aparte porque es otra pregunta
//  -si algo se atasca- y no se responde con el mismo numero.
struct Medida { double medianaMs = 0, p95Ms = 0, peorMs = 0; double pico = 0; };

static Medida corre (AudioEngine& e, juce::AudioBuffer<float>& buf, int nBlocks,
                     const std::function<void (int)>& antes = {})
{
    std::vector<double> t;
    t.reserve ((size_t) nBlocks);
    Medida m;

    for (int b = 0; b < nBlocks; ++b)
    {
        if (antes) antes (b);
        const auto t0 = Clock::now();
        e.renderNextBlock (buf, 0, kBs);
        t.push_back (std::chrono::duration<double, std::milli> (Clock::now() - t0).count());

        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < kBs; ++i)
                m.pico = juce::jmax (m.pico, (double) std::abs (buf.getSample (c, i)));
    }

    std::sort (t.begin(), t.end());
    m.medianaMs = t[t.size() / 2];
    m.p95Ms     = t[(size_t) ((double) t.size() * 0.95)];
    m.peorMs    = t.back();
    return m;
}

static double base = 0.0;   // el silencio, para restarlo

static void fila (const char* nombre, const Medida& m, bool restaBase = true)
{
    const double neto = restaBase ? juce::jmax (0.0, m.medianaMs - base) : m.medianaMs;
    std::printf ("%-32s %7.3f ms  neto %7.3f  p95 %7.3f  peor %7.3f  carga %5.1f%%  pico %.3f\n",
                 nombre, m.medianaMs, neto, m.p95Ms, m.peorMs,
                 100.0 * m.medianaMs / kBudget, m.pico);
}

//  Un motor recien montado con dieciseis pads cargados y nada mas puesto.
//  Cada etapa parte de aqui para que la diferencia sea suya y no de lo que
//  arrastraba la etapa anterior.
static void prepara (AudioEngine& e, bool tono = false)
{
    e.prepareToPlay (kSr, kBs);
    e.setPolyphony (32, 4);
    for (int p = 0; p < 16; ++p)
    {
        e.setPadGain (p, 0.85f);
        e.setPadKeepLength (p, tono);
        e.publishSample (p, makeSample (44100.0, 2.0, 110.0f * (float) (p + 1)));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    juce::AudioBuffer<float> buf (2, kBs);

    std::printf ("ZATI  banco de CPU por etapas   %.0f Hz  rafaga %d  presupuesto %.2f ms\n\n",
                 kSr, kBs, kBudget);

    //  0. EL SILENCIO. Con los pads cargados y ninguna voz viva. Es lo que
    //     cuesta el bloque por existir -limpiar, mirar las colas, repartir
    //     envios- y es la linea de la que cuelgan todas las demas.
    {
        AudioEngine e; prepara (e);
        corre (e, buf, 64);
        const auto m = corre (e, buf, 2000);
        base = m.medianaMs;
        fila ("silencio (16 pads cargados)", m, false);
    }

    std::printf ("\n-- las voces --------------------------------------------------------\n");

    //  1. CINTA A TONO NATURAL. delta = 1.0 exacto, que es el camino corto:
    //     ni interpolacion ni granos.
    {
        AudioEngine e; prepara (e);
        for (int p = 0; p < 16; ++p) { e.setPadPitch (p, 0.0f); e.setPadLoop (p, true); }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 voces cinta, 0 st", corre (e, buf, 2000));
    }

    //  2. CINTA TRANSPORTADA. Mismo camino pero con Hermite de 4 puntos por
    //     muestra y por canal: cuatro multiplicaciones y tres sumas donde
    //     antes habia una lectura.
    {
        AudioEngine e; prepara (e);
        for (int p = 0; p < 16; ++p) { e.setPadPitch (p, 7.0f); e.setPadLoop (p, true); }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 voces cinta, +7 st", corre (e, buf, 2000));
    }

    //  3. TONO. La sospecha principal: dos granos por muestra -o sea DOS
    //     Hermite por canal- mas la busqueda WSOLA cada medio grano.
    {
        AudioEngine e; prepara (e, true);
        for (int p = 0; p < 16; ++p) { e.setPadPitch (p, 7.0f); e.setPadLoop (p, true); }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 voces TONO, +7 st", corre (e, buf, 2000));
    }

    //  3b. Una sola voz en tono, para saber cuanto cuesta UNA y poder dividir.
    {
        AudioEngine e; prepara (e, true);
        e.setPadPitch (0, 7.0f); e.setPadLoop (0, true);
        corre (e, buf, 64, [&e] (int b) { if (b == 0) e.postNoteOn (0, 0.9f); });
        fila ("1 voz TONO, +7 st", corre (e, buf, 2000));
    }

    std::printf ("\n-- el filtro del pad ------------------------------------------------\n");

    //  4. FILTRO POR PAD. Saca al pad del camino directo y lo manda al
    //     scratch: limpiar, renderizar, filtrar, y sumar a mano. Cuesta el
    //     filtro Y el desvio, y esta fila los mide juntos porque juntos es
    //     como ocurren.
    {
        AudioEngine e; prepara (e);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
            e.setPadCutoff (p, 1200.0f); e.setPadReso (p, 0.5f);
        }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 pads con filtro", corre (e, buf, 2000));
    }

    std::printf ("\n-- los seis efectos -------------------------------------------------\n");

    //  5. CADA BUS POR SEPARADO. Los dieciseis pads mandando a uno solo, al
    //     maximo. Un bus que nadie alimenta ni suena no se limpia siquiera,
    //     asi que esta es la unica forma de saber lo que cuesta encendido.
    struct Fx { const char* nombre; int idx; std::function<void (AudioEngine&)> pon; };
    const Fx efectos[] =
    {
        { "FILTRO barrido", 0, [] (AudioEngine& e) { e.setFltSweep (0.4f); e.setFltReso (0.6f); e.setFltMix (1.0f); } },
        { "PASA ALTOS",     1, [] (AudioEngine& e) { e.setHpFreq (400.0f); e.setHpReso (0.5f);  e.setHpMix  (1.0f); } },
        { "SATURACION",     2, [] (AudioEngine& e) { e.setDrvTone (4000.0f);                    e.setDrvMix (1.0f); } },
        { "ECO",            3, [] (AudioEngine& e) { e.setDlyTime (280.0f); e.setDlyFb (0.45f); e.setDlyMix (1.0f); } },
        { "CRUJIDO",        4, [] (AudioEngine& e) { e.setCrushBits (6.0f); e.setCrushRate (0.3f); e.setCrushMix (1.0f); } },
        { "REVERB",         5, [] (AudioEngine& e) { e.setRevSize (0.7f);  e.setRevDamp (0.4f);  e.setRevMix (1.0f); } }
    };

    for (const auto& fx : efectos)
    {
        AudioEngine e; prepara (e);
        fx.pon (e);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
            e.setPadSend (p, fx.idx, 1.0f);
        }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        char nombre[64];
        std::snprintf (nombre, sizeof (nombre), "16 pads -> %s", fx.nombre);
        fila (nombre, corre (e, buf, 2000));
    }

    //  6. LOS SEIS A LA VEZ. No es la suma de los seis: el pad se renderiza
    //     UNA vez al scratch y se reparte, asi que el desvio se paga una sola
    //     vez. La diferencia entre esta fila y esa suma es lo que ahorra el
    //     reparto.
    {
        AudioEngine e; prepara (e);
        for (const auto& fx : efectos) fx.pon (e);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
            for (int f = 0; f < 6; ++f) e.setPadSend (p, f, 0.5f);
        }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 pads -> los SEIS", corre (e, buf, 2000));
    }

    std::printf ("\n-- el transporte ----------------------------------------------------\n");

    //  7. SECUENCIADOR. Un patron lleno a 120 BPM parte el bloque en cada
    //     paso, y ese troceo tiene su propio coste aparte de las voces que
    //     dispara.
    {
        AudioEngine e; prepara (e);
        for (int p = 0; p < 16; ++p) e.setPadPitch (p, 0.0f);
        for (int st = 0; st < 16; ++st)
            for (int p = 0; p < 16; ++p)
                e.setStep (0, st, p, true);
        e.setBpm (120.0);
        e.setPlaying (true);
        corre (e, buf, 256);
        fila ("patron lleno 16x16, 120 BPM", corre (e, buf, 4000));
    }

    //  8. TODO A LA VEZ. Lo que hace una persona en un directo: secuencia
    //     corriendo, filtros abiertos, envios puestos, y algunos pads en
    //     tono. Es la unica fila que se compara con el presupuesto de
    //     verdad; las de arriba son para saber a quien culpar.
    {
        AudioEngine e; prepara (e);
        for (const auto& fx : efectos) fx.pon (e);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadKeepLength (p, p < 4);          // cuatro en tono, como en una sesion
            e.setPadPitch (p, p < 4 ? 5.0f : 0.0f);
            e.setPadCutoff (p, p % 3 == 0 ? 2500.0f : 20000.0f);
            e.setPadReso (p, 0.3f);
            e.setPadSend (p, p % 6, 0.45f);
        }
        for (int st = 0; st < 16; ++st)
            for (int p = 0; p < 16; ++p)
                if ((st + p) % 3 == 0) e.setStep (0, st, p, true);
        e.setBpm (128.0);
        e.setPlaying (true);
        corre (e, buf, 256);
        fila ("SESION REAL (todo a la vez)", corre (e, buf, 4000), false);
    }

    std::printf ("\nneto = mediana menos el silencio. carga = mediana sobre el presupuesto del bloque.\n");
    return 0;
}

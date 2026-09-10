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

//  Los nombres, en el idioma del banco y no en el de la cara: `fxDefs` vive en
//  `MainComponent` y esto es una consola. Tres letras y su indice bastan para
//  leer la tabla, y el orden es el de `kFxDef`.
static const char* nombreDeFx (int f) noexcept
{
    static const char* n[] = { "FLT filtro", "HPF paso alto", "DRV saturacion",
                               "DLY eco", "BIT crujido", "REV reverb", "EQ cinco bandas",
                               "CMP compresor", "GTE puerta", "DSS de-esser", "LIM limitador",
                               "CHO coro", "FLA flanger", "PHA phaser", "TRM tremolo",
                               //  Y LOS SEIS DE CARACTER, que faltaban desde
                               //  que entraron: el `static_assert` lo decia y
                               //  nadie lo leyo porque `Cpu.cpp` no corre en el
                               //  CI -mide por RELOJ- y `cpu.py` es otra prueba.
                               //  Un banco que no compila no falla: no esta. Es
                               //  el mismo fallo que ya tuvieron `StressTest`,
                               //  `Soak` y este mismo con `ZatiData`.
                               "RNG ring mod", "PIT pitch", "WID ancho",
                               "EXC excitador", "TRN transitorios", "FRZ congelador" };
    static_assert (sizeof (n) / sizeof (n[0]) == (size_t) AudioEngine::kNumFx,
                   "nombreDeFx tiene que tener una fila por tipo");
    return juce::isPositiveAndBelow (f, (int) (sizeof (n) / sizeof (n[0]))) ? n[f] : "?";
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

    //  0b. EL SILENCIO CON UN EFECTO ABIERTO, que es el estado en el que esta
    //      la maquina la mayor parte del tiempo en cuanto alguien toca el
    //      delay - y el que esta fila existe para separar del de arriba.
    //
    //      El reparto de envios se salta un pad cuando no manda a ningun
    //      efecto, y esa es la razon de ser de `padSendMask`. La condicion
    //      llevaba ademas un `! anyFxOpen`, o sea que bastaba abrir UN efecto
    //      para que los SESENTA Y CUATRO pads volvieran al bucle largo -seis
    //      cargas atomicas y seis pasos de suavizado cada uno- aunque ninguno
    //      tuviera un solo envio abierto. Sin la fila, ese coste se escondia
    //      entre dos medidas que no lo miraban.
    {
        AudioEngine e; prepara (e);
        e.setDlyTime (280.0f); e.setDlyFb (0.45f); e.setDlyMix (1.0f);
        corre (e, buf, 64);
        fila ("silencio con UN efecto abierto", corre (e, buf, 2000));
    }

    //  0c. EL SILENCIO CON UN PAD ARMADO PARA EL BOMBEO. La etapa del bombeo
    //      entraba a su bucle por muestra si habia envolvente O si habia un
    //      pad armado, y con el pad armado y quieto la envolvente vale cero:
    //      se multiplicaba la salida entera por uno, en los dos canales, en
    //      cada bloque y para siempre.
    {
        AudioEngine e; prepara (e);
        e.setDuckPad (0);
        corre (e, buf, 64);
        fila ("silencio con el bombeo armado", corre (e, buf, 2000));
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

    std::printf ("\n-- los efectos -------------------------------------------------\n");

    //  5. CADA BUS POR SEPARADO. Los dieciseis pads mandando a uno solo, al
    //     maximo. Un bus que nadie alimenta ni suena no se limpia siquiera,
    //     asi que esta es la unica forma de saber lo que cuesta encendido.
    //  Y LA TABLA SALE DE `kFxDef` Y NO ESCRITA A MANO, que es como estaba y
    //  como se habia quedado en SEIS: ni el EQ ni los cuatro de dinamica
    //  tenian fila, o sea cinco etapas del hilo de audio que nadie medía —y
    //  el rotulo de la seccion decia «los seis efectos» con once en la tabla—.
    //  *Un numero que nadie mira se publica*, y aqui ni se calculaba.
    //
    //  Cada uno se pone con SUS defectos y el MIX al maximo: los defectos son
    //  los que la maquina trae, asi que la fila dice lo que cuesta ese efecto
    //  tal y como lo enciende una persona.
    for (int f = 0; f < AudioEngine::kNumFx; ++f)
    {
        AudioEngine e; prepara (e);
        e.setFxParam (0, f, 0, AudioEngine::kFxDef[f][0]);
        e.setFxParam (0, f, 1, AudioEngine::kFxDef[f][1]);
        e.setFxParam (0, f, 2, 1.0f);
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
        }
        //  Los dieciseis pads nacen en el canal 0, asi que UNA llamada abre el
        //  envio de los dieciseis: eso es lo que la mesa hace por el reparto.
        e.setCanalSend (0, f, 1.0f);
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        char nombre[64];
        std::snprintf (nombre, sizeof (nombre), "16 pads -> %s", nombreDeFx (f));
        fila (nombre, corre (e, buf, 2000));
    }

    //  6. TODOS A LA VEZ. No es la suma: el pad se renderiza UNA vez al
    //     scratch y se reparte, asi que el desvio se paga una sola vez. La
    //     diferencia entre esta fila y esa suma es lo que ahorra el reparto.
    {
        AudioEngine e; prepara (e);
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            e.setFxParam (0, f, 0, AudioEngine::kFxDef[f][0]);
            e.setFxParam (0, f, 1, AudioEngine::kFxDef[f][1]);
            e.setFxParam (0, f, 2, 1.0f);
        }
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
        }
        for (int f = 0; f < AudioEngine::kNumFx; ++f) e.setCanalSend (0, f, 0.5f);
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 pads -> TODOS", corre (e, buf, 2000));
    }

    //  Y LA MISMA CARGA CON LOS PADS REPARTIDOS POR DIECISEIS CANALES, que es
    //  la unica pregunta que la mesa anade al hilo de audio: el reparto por
    //  bloque pasa de leer `padSend[p][f]` a leer `canalSend[padCanal[p]][f] x
    //  padRecorte[p][f]`, o sea la misma forma con un indice mas. Se lee
    //  CONTRA la fila de arriba -los mismos dieciseis pads con los mismos
    //  envios, todos en el canal 0- y no contra un numero absoluto, que es como
    //  se leen todas las filas de este banco.
    {
        AudioEngine e; prepara (e);
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            e.setFxParam (0, f, 0, AudioEngine::kFxDef[f][0]);
            e.setFxParam (0, f, 1, AudioEngine::kFxDef[f][1]);
            e.setFxParam (0, f, 2, 1.0f);
        }
        for (int p = 0; p < 16; ++p)
        {
            e.setPadPitch (p, 0.0f); e.setPadLoop (p, true);
            e.setPadCanal (p, p);
        }
        for (int c = 0; c < AudioEngine::kNumCanales; ++c)
            for (int f = 0; f < AudioEngine::kNumFx; ++f) e.setCanalSend (c, f, 0.5f);
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 pads en 16 CANALES -> TODOS", corre (e, buf, 2000));
    }

    //  Y LO QUE CUESTA EL MEDIDOR DEL CANAL QUE SE MIRA.
    //
    //  El precio no es el barrido -que es un `findMinAndMax` sobre memoria que
    //  el filtro del pad acaba de tocar- sino que los pads de ESE canal toman
    //  el camino LARGO aunque no manden a nadie, que es justo lo que la mascara
    //  existe para evitar. Por eso se mide con los dieciseis pads en el canal 0
    //  y sin un solo envio abierto, que es el peor caso posible: los dieciseis
    //  medidos a la vez, cuando en la maquina son cuatro de sesenta y cuatro de
    //  media. Se lee CONTRA la fila de al lado -los mismos dieciseis pads sin
    //  mirar ningun canal- y no contra un numero absoluto.
    {
        AudioEngine e; prepara (e);
        for (int p = 0; p < 16; ++p) { e.setPadPitch (p, 0.0f); e.setPadLoop (p, true); }
        corre (e, buf, 64, [&e] (int b) { if (b == 0) for (int p = 0; p < 16; ++p) e.postNoteOn (p, 0.9f); });
        fila ("16 pads, sin mirar ningun canal", corre (e, buf, 2000));
        e.miraCanal (0);
        fila ("16 pads, los 16 en el canal MIRADO", corre (e, buf, 2000));
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
        //  Seis abiertos, que es lo que una sesion tiene puesto a la vez —no
        //  los quince: la fila de la cara son seis ranuras.
        for (int f = 0; f < 6; ++f)
        {
            e.setFxParam (0, f, 0, AudioEngine::kFxDef[f][0]);
            e.setFxParam (0, f, 1, AudioEngine::kFxDef[f][1]);
            e.setFxParam (0, f, 2, 1.0f);
        }
        for (int p = 0; p < 16; ++p)
        {
            e.setPadKeepLength (p, p < 4);          // cuatro en tono, como en una sesion
            e.setPadPitch (p, p < 4 ? 5.0f : 0.0f);
            e.setPadCutoff (p, p % 3 == 0 ? 2500.0f : 20000.0f);
            e.setPadReso (p, 0.3f);
            //  Seis canales con un efecto cada uno, que es lo que una sesion de
            //  verdad tiene desde que la mesa existe: el pad elige canal y el
            //  canal manda. Antes era `setPadSend (p, p % 6, ...)`, o sea la
            //  misma forma sin la capa que ahora reparte.
            e.setPadCanal (p, p % 6);
        }
        for (int c = 0; c < 6; ++c) e.setCanalSend (c, c, 0.45f);
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

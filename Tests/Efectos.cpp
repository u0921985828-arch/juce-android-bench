// ZATI — el banco de CALIDAD de los treinta efectos, medidos por igual.
//
// Cpu.cpp dice cuanto cuesta cada efecto; este dice como SUENA, con las mismas
// cinco preguntas para los treinta:
//
//   PLIEGUE   un seno de 4700 Hz: el pico mas alto que no es un armonico ni lo
//             que el efecto existe para hacer, en dB contra el pico mayor.
//   MOVER     el mando 1 y el 2 barridos de punta a punta en un segundo, de
//             bloque en bloque como un dedo, con un seno de 220 Hz: lo que sube
//             por encima de 8 kHz contra lo que da el mismo efecto quieto.
//   ENCENDER  el MIX de 0 a su valor de encendido de golpe, y vuelta: lo mismo.
//   NIVEL     ruido blanco: cuanto mas fuerte o mas flojo sale encendido.
//   COLA      diez segundos sin entrada: si se apaga, si se queda en un NaN.
//
//   cmake -B build-bench -DZATI_BENCH=ON && cmake --build build-bench --target Efectos
//   ./build-bench/Efectos_artefacts/Release/Efectos
#include <JuceHeader.h>
#include <memory>
#include "../Source/AudioEngine.h"
#include "../Source/FxPresets.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr double kSr = 48000.0;
static constexpr int    kBs = 128;
static constexpr int    kCanal = 1;   // el 0 no lleva insertos

struct Mando { const char* nombre; float lo0, hi0, lo1, hi1, onMix; };
//  Los rangos de la cara (MainComponent::fxDefs), copiados: esto es una consola.
static const Mando kMandos[AudioEngine::kNumFx] = {
    { "FLT", -1.0f, 1.0f, 0.3f, 4.0f, 1.00f },
    { "HPF", 20.0f, 20000.0f, 0.3f, 4.0f, 1.00f },
    { "DRV", 0.0f, 1.0f, 200.0f, 20000.0f, 0.80f },
    { "DLY", 20.0f, 1000.0f, 0.0f, 0.95f, 0.35f },
    { "BIT", 1.0f, 16.0f, 1.0f, 64.0f, 0.60f },
    { "REV", 0.0f, 1.0f, 0.0f, 1.0f, 0.30f },
    { "EQ", 0.4f, 3.0f, -12.0f, 12.0f, 1.00f },
    { "CMP", -60.0f, 0.0f, 1.0f, 20.0f, 1.00f },
    { "GTE", -80.0f, 0.0f, 5.0f, 500.0f, 1.00f },
    { "DSS", 2000.0f, 12000.0f, 0.0f, 1.0f, 1.00f },
    { "LIM", -24.0f, 0.0f, 5.0f, 500.0f, 1.00f },
    { "CHO", 0.05f, 8.0f, 0.0f, 1.0f, 0.50f },
    { "FLA", 0.05f, 8.0f, 0.0f, 1.0f, 0.50f },
    { "PHA", 0.05f, 8.0f, 0.0f, 1.0f, 0.50f },
    { "TRM", 0.20f, 20.0f, 0.0f, 1.0f, 1.00f },
    { "RNG", 20.0f, 4000.0f, 0.0f, 1.0f, 0.50f },
    { "PIT", -12.0f, 12.0f, 20.0f, 120.0f, 1.00f },
    { "WID", 0.0f, 2.0f, 20.0f, 400.0f, 1.00f },
    { "EXC", 1000.0f, 10000.0f, 0.0f, 1.0f, 1.00f },
    { "TRN", -1.0f, 1.0f, -1.0f, 1.0f, 1.00f },
    { "FRZ", 20.0f, 500.0f, 0.0f, 1.0f, 1.00f },
    { "WAH", 0.0f, 1.0f, 200.0f, 1200.0f, 1.00f },
    { "OCT", 0.0f, 1.0f, 0.0f, 1.0f, 1.00f },
    { "AMB", 0.0f, 1.0f, 0.0f, 120.0f, 0.30f },
    { "FRM", 0.0f, 1.0f, 0.3f, 4.0f, 1.00f },
    { "FLD", 0.0f, 1.0f, 200.0f, 20000.0f, 0.80f },
    { "ROT", 0.05f, 8.0f, 0.0f, 1.0f, 1.00f },
    { "PNG", 20.0f, 1000.0f, 0.0f, 0.95f, 0.35f },
    { "DUC", 0.05f, 8.0f, 0.0f, 1.0f, 1.00f },
    { "REP", 0.05f, 8.0f, 0.0f, 1.0f, 1.00f },
};

//  Un ciclo entero cabe en la muestra, asi que el bucle no tiene costura.
static SampleBuffer::Ptr seno (double hz, float amp)
{
    auto* sb = new SampleBuffer();
    const int n = 96000;
    sb->buffer.setSize (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kSr);
        sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

static SampleBuffer::Ptr ruido (float amp)
{
    auto* sb = new SampleBuffer();
    const int n = 96000;
    sb->buffer.setSize (2, n);
    juce::Random r (7);
    for (int i = 0; i < n; ++i)
    {
        const float v = amp * (r.nextFloat() * 2.0f - 1.0f);
        sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

static std::unique_ptr<AudioEngine> monta (int f, SampleBuffer::Ptr s, float mix)
{
    auto e = std::make_unique<AudioEngine>();
    e->prepareToPlay (kSr, kBs);
    e->setPolyphony (8, 2);
    e->setPadGain (0, 1.0f);
    e->setPadPitch (0, 0.0f);
    e->setPadLoop (0, true);
    e->setPadCanal (0, kCanal);
    e->publishSample (0, s);
    if (f >= 0)
    {
        e->setCanalSend (kCanal, f, 1.0f);
        e->setFxParam (kCanal, f, 2, mix);
    }
    return e;
}

//  Corre `n` muestras y las deja en `y` (canal izquierdo); `antes` mueve mandos por bloque.
static void corre (AudioEngine& e, int n, std::vector<float>& y,
                   const std::function<void (int)>& antes = {})
{
    juce::AudioBuffer<float> buf (2, kBs);
    y.assign ((size_t) n, 0.0f);
    for (int b = 0; b * kBs < n; ++b)
    {
        if (antes) antes (b);
        e.renderNextBlock (buf, 0, kBs);
        for (int i = 0; i < kBs && b * kBs + i < n; ++i)
            y[(size_t) (b * kBs + i)] = buf.getSample (0, i);
    }
}

//  Espectro de potencia con Blackman-Harris de cuatro terminos (lobulos a -92 dB).
static std::vector<double> espectro (const float* x, int orden)
{
    const int n = 1 << orden;
    juce::dsp::FFT fft (orden);
    std::vector<float> a ((size_t) (2 * n), 0.0f);
    for (int i = 0; i < n; ++i)
    {
        const double t = 2.0 * juce::MathConstants<double>::pi * i / (n - 1);
        const double w = 0.35875 - 0.48829 * std::cos (t) + 0.14128 * std::cos (2 * t) - 0.01168 * std::cos (3 * t);
        a[(size_t) i] = (float) (x[i] * w);
    }
    fft.performFrequencyOnlyForwardTransform (a.data());
    std::vector<double> p ((size_t) (n / 2));
    for (int k = 0; k < n / 2; ++k) p[(size_t) k] = (double) a[(size_t) k] * a[(size_t) k];
    return p;
}

static double dB (double p) { return 10.0 * std::log10 (juce::jmax (p, 1e-30)); }

//  Lo que el efecto existe para hacer con un seno: sus bases y lo ancha que es cada raya.
struct Esperado { std::vector<double> bases; double ancho; };
static Esperado esperado (int f, double f0)
{
    Esperado e { { f0 }, 40.0 };
    switch (f)
    {
        case AudioEngine::kFxCho: case AudioEngine::kFxFla: case AudioEngine::kFxPha:
        case AudioEngine::kFxTrm: case AudioEngine::kFxRot: case AudioEngine::kFxDuc:
        case AudioEngine::kFxRep: case AudioEngine::kFxWah: case AudioEngine::kFxFrz:
            e.ancho = 400.0; break;
        //  efectos#rev: la cola modulada abre la raya unas decenas de Hz con cada vuelta; eso es la sala, no un pliegue.
        case AudioEngine::kFxRev:
            e.ancho = 120.0; break;
        case AudioEngine::kFxPit:
            e.bases.push_back (f0 * std::pow (2.0, 7.0 / 12.0)); e.ancho = 400.0; break;
        case AudioEngine::kFxOct:
            e.bases.push_back (2.0 * f0); e.bases.push_back (0.5 * f0); e.ancho = 400.0; break;
        case AudioEngine::kFxRng:
            for (int j = 1; j <= 3; ++j) { e.bases.push_back (f0 + 220.0 * j); e.bases.push_back (std::abs (f0 - 220.0 * j)); }
            break;
        default: break;
    }
    return e;
}

struct Pliegue { double dB = -200.0, hz = 0.0; };
static Pliegue pliegue (int f)
{
    const double f0 = 4700.0;
    auto e = monta (f, seno (f0, 0.25f), 1.0f);
    e->postNoteOn (0, 1.0f);
    std::vector<float> y;
    //  FRZ captura al abrirse: se rearma con el tono ya asentado, o lo que se mide es el ataque del pad en bucle.
    const int rearma = f == AudioEngine::kFxFrz ? 24000 / kBs : -10;
    corre (*e, 48000 + 32768, y, [&] (int b)
    {
        if (b == rearma)     e->setCanalSend (kCanal, f, 0.0f);
        if (b == rearma + 2) e->setCanalSend (kCanal, f, 1.0f);
    });
    if (auto* v = std::getenv ("ZATI_FX_VOLCADO"))
    {
        juce::FileOutputStream o (juce::File (juce::String (v) + "-" + juce::String (f) + "-p.f32"));
        o.setPosition (0); o.truncate();
        o.write (y.data(), y.size() * sizeof (float));
    }
    const int orden = 15, n = 1 << orden;
    const auto p = espectro (y.data() + 48000, orden);
    const double hzBin = kSr / n;
    double pico = 0.0;
    for (int k = 2; k < n / 2; ++k) pico = juce::jmax (pico, p[(size_t) k]);
    const auto es = esperado (f, f0);
    Pliegue r;
    for (int k = (int) (40.0 / hzBin); k < n / 2 - 4; ++k)
    {
        const double hz = k * hzBin;
        bool suyo = false;
        for (double b : es.bases)
            for (int h = 1; h * b < kSr * 0.5 && ! suyo; ++h)
                suyo = std::abs (hz - h * b) < es.ancho + 6.0 * hzBin;
        if (suyo) continue;
        const double d = dB (p[(size_t) k] / juce::jmax (pico, 1e-30));
        if (d > r.dB) { r.dB = d; r.hz = hz; }
    }
    return r;
}

//  Energia por encima de 8 kHz contra la total, por trama de 2048, la peor.
//  Energia por encima de 8 kHz por trama de 2048, la peor, contra la de la trama del
//  seno seco: contra la de la propia trama un filtro que se come la señal haria
//  enorme cualquier resto.
static double gRefTrama = 1.0;
static double agudoPeor (const std::vector<float>& y, int desde)
{
    const int orden = 11, n = 1 << orden;
    double peor = 0.0;
    for (int s = desde; s + n <= (int) y.size(); s += n / 2)
    {
        const auto p = espectro (y.data() + s, orden);
        double hf = 0.0;
        for (int k = 1; k < n / 2; ++k)
            if (k * kSr / n > 8000.0) hf += p[(size_t) k];
        peor = juce::jmax (peor, hf / gRefTrama);
    }
    return peor;
}

static void midoReferencia()
{
    std::vector<float> y (2048);
    for (int i = 0; i < 2048; ++i) y[(size_t) i] = 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / kSr);
    const auto p = espectro (y.data(), 11);
    gRefTrama = 0.0;
    for (double v : p) gRefTrama += v;
}

//  Por debajo de esto un chasquido no se oye: energia sobre 8 kHz a -70 dB del seno seco.
static constexpr double kSuelo = -70.0;

static float enRango (float lo, float hi, float t)
{
    if (lo > 0.0f && hi / lo > 50.0f) return lo * std::pow (hi / lo, t);   // los de Hz van en octavas
    return lo + (hi - lo) * t;
}

//  El mando barrido contra el mismo efecto quieto en diecisiete sitios del barrido.
static double mover (int f, int par)
{
    const auto& m = kMandos[f];
    const float lo = par == 0 ? m.lo0 : m.lo1, hi = par == 0 ? m.hi0 : m.hi1;
    double quieto = 0.0;
    std::vector<float> y;
    //  Diecisiete sitios: con cinco, un BIT a 2.3 bits sonaba peor que cualquiera de los quietos sin chasquear.
    constexpr int kSitios = 16;
    for (int i = 0; i <= kSitios; ++i)
    {
        auto e = monta (f, seno (220.0, 0.25f), m.onMix);
        e->setFxParam (kCanal, f, par, enRango (lo, hi, (float) i / (float) kSitios));
        e->postNoteOn (0, 1.0f);
        corre (*e, 24000 + 8192, y);
        quieto = juce::jmax (quieto, agudoPeor (y, 24000));
    }
    auto e = monta (f, seno (220.0, 0.25f), m.onMix);
    e->setFxParam (kCanal, f, par, lo);
    e->postNoteOn (0, 1.0f);
    const int arranque = 24000 / kBs, barrido = 48000 / kBs;
    corre (*e, 24000 + 48000 * 2, y, [&] (int b)
    {
        const int k = b - arranque;
        if (k < 0) return;
        const float t = k < barrido ? (float) k / barrido : 1.0f - (float) (k - barrido) / barrido;
        e->setFxParam (kCanal, f, par, enRango (lo, hi, juce::jlimit (0.0f, 1.0f, t)));
    });
    if (auto* v = std::getenv ("ZATI_FX_VOLCADO"))
    {
        juce::FileOutputStream o (juce::File (juce::String (v) + "-" + juce::String (f) + "-" + juce::String (par) + ".f32"));
        o.setPosition (0); o.truncate();
        o.write (y.data(), y.size() * sizeof (float));
    }
    return dB (agudoPeor (y, 24000)) - juce::jmax (dB (quieto), kSuelo);
}

//  Cambiar de preset con el tono sonando: los ocho mandos saltan a la vez, que es lo que hace la cara.
static double preset (int f)
{
    const auto& m = kMandos[f];
    auto pon = [&] (AudioEngine& e, int k)
    {
        for (int pi = 0; pi < AudioEngine::kNumParFx; ++pi)
            if (pi != 2 && AudioEngine::tieneMando (f, pi)) e.setFxParam (kCanal, f, pi, FxPresets::valor (f, k, pi));
    };
    double quieto = 0.0, peor = -200.0;
    std::vector<float> y;
    for (int k = 0; k < FxPresets::cuantos(); ++k)
    {
        auto e = monta (f, seno (220.0, 0.25f), m.onMix);
        pon (*e, k);
        e->postNoteOn (0, 1.0f);
        corre (*e, 24000 + 24000, y);   // efectos#preset: la misma ventana sin cambio; los ecos del ataque no son chasquido
        quieto = juce::jmax (quieto, agudoPeor (y, 24000));
    }
    for (int k = 1; k < FxPresets::cuantos(); ++k)
    {
        auto e = monta (f, seno (220.0, 0.25f), m.onMix);
        pon (*e, k - 1);
        e->postNoteOn (0, 1.0f);
        corre (*e, 24000 + 24000, y, [&] (int b) { if (b == 24000 / kBs + 20) pon (*e, k); });
        peor = juce::jmax (peor, dB (agudoPeor (y, 24000)));
    }
    return peor - juce::jmax (dB (quieto), kSuelo);
}

static double encender (int f)
{
    const auto& m = kMandos[f];
    std::vector<float> y;
    auto e = monta (f, seno (220.0, 0.25f), m.onMix);
    e->postNoteOn (0, 1.0f);
    corre (*e, 24000 + 8192, y);
    const double quieto = agudoPeor (y, 24000);
    auto g = monta (f, seno (220.0, 0.25f), 0.0f);
    g->postNoteOn (0, 1.0f);
    const int on = 24000 / kBs, off = on + 24000 / kBs;
    corre (*g, 24000 * 3, y, [&] (int b)
    {
        if (b == on)  g->setFxParam (kCanal, f, 2, m.onMix);
        if (b == off) g->setFxParam (kCanal, f, 2, 0.0f);
    });
    return dB (agudoPeor (y, 20000)) - juce::jmax (dB (quieto), kSuelo);
}

static double rms (const std::vector<float>& y, int desde)
{
    double s = 0.0;
    for (size_t i = (size_t) desde; i < y.size(); ++i) s += (double) y[i] * y[i];
    return std::sqrt (s / juce::jmax ((size_t) 1, y.size() - (size_t) desde));
}

//  Ruido rosa (filtro de Kellet), RMS ~0.1 con amp 0.1.
static SampleBuffer::Ptr rosa (float amp)
{
    auto* sb = new SampleBuffer();
    const int n = 96000;
    sb->buffer.setSize (2, n);
    juce::Random r (7);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = r.nextFloat() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
        const float v = amp * 0.11f * (float) (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362); b6 = w * 0.115926;
        sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}
static double nivel (int f)
{
    std::vector<float> y;
    auto s = rosa (0.1f);   // efectos#nivel: rosa, que reparte la potencia como la musica; con blanco una banda pierde 20 dB que en musica no
    auto e = monta (-1, s, 0.0f);
    e->postNoteOn (0, 1.0f);
    corre (*e, 48000 * 2, y);
    const double seco = rms (y, 24000);
    auto g = monta (f, s, kMandos[f].onMix);
    g->postNoteOn (0, 1.0f);
    corre (*g, 48000 * 2, y);
    return 20.0 * std::log10 (juce::jmax (rms (y, 24000), 1e-12) / juce::jmax (seco, 1e-12));
}

struct Cola { double finalDb = -200.0; bool nan = false; };
static Cola cola (int f)
{
    std::vector<float> y;
    auto e = monta (f, ruido (0.1f), 1.0f);
    e->postNoteOn (0, 1.0f);
    corre (*e, 24000, y);
    e->postNoteOff (0);
    corre (*e, 48000 * 10, y);
    Cola c;
    for (float v : y) c.nan = c.nan || ! std::isfinite (v);
    double pk = 0.0;
    for (size_t i = y.size() - 4800; i < y.size(); ++i) pk = juce::jmax (pk, (double) std::abs (y[i]));
    c.finalDb = 20.0 * std::log10 (juce::jmax (pk, 1e-10));
    return c;
}

//  Los topes. BIT pliega por oficio y FRZ sostiene por oficio, y por eso no cuentan ahi.
//  efectos#rev: Freeverb y la FDN de cuatro daban 0.60 de densidad y 6.4 dB de picos; el ruido da 1.0 y 3.0.
static constexpr double kDensidadMin = 0.85, kPicosMax = 5.0;
//  efectos#pit: PIT trinaba 4.8 dB y FRZ 5.5 con un seno fijo; los que viven de modular o de repetir no cuentan.
static constexpr double kTrinoMax = 1.0;
static bool modulaPorOficio (int f)
{
    switch (f)
    {
        case AudioEngine::kFxDly: case AudioEngine::kFxRev: case AudioEngine::kFxCho: case AudioEngine::kFxFla:
        case AudioEngine::kFxPha: case AudioEngine::kFxTrm: case AudioEngine::kFxRot: case AudioEngine::kFxPng:
        case AudioEngine::kFxDuc: return true;
        default: return false;
    }
}
static constexpr double kApagadoMax = -60.0;
static constexpr double kPliegueMax = -60.0, kChasquidoMax = 6.0, kNivelMax = 4.0, kColaMax = -90.0;
static int fallos = 0;
template <typename... A> static void falla (const char* fmt, A... a)
{
    std::printf ("FALLA  "); std::printf (fmt, a...); std::printf ("\n"); ++fallos;
}

//  ---------------------------------------------------------------------------
//  LA SALA DE REV: la respuesta a un impulso, y dos cosas que el oido juzga de ella.
static SampleBuffer::Ptr impulso()
{
    auto* sb = new SampleBuffer();
    sb->buffer.setSize (2, 4096);
    sb->buffer.clear();
    sb->buffer.setSample (0, 400, 1.0f); sb->buffer.setSample (1, 400, 1.0f);   // pasado el arranque suave de la voz
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

static std::vector<float> respuesta (float tam, float amort)
{
    auto e = monta (AudioEngine::kFxRev, impulso(), 1.0f);
    e->setPadLoop (0, false);
    e->setFxParam (kCanal, AudioEngine::kFxRev, 0, tam);
    e->setFxParam (kCanal, AudioEngine::kFxRev, 1, amort);
    std::vector<float> y;
    corre (*e, 2048, y);
    e->postNoteOn (0, 1.0f);
    corre (*e, (int) (kSr * 2.5), y);
    if (auto* v = std::getenv ("ZATI_FX_VOLCADO"))
    {
        juce::FileOutputStream o (juce::File (juce::String (v) + "-ir-" + juce::String (tam, 2) + ".f32"));
        o.setPosition (0); o.truncate();
        o.write (y.data(), y.size() * sizeof (float));
    }
    return y;
}

//  Densidad de ecos normalizada (Abel y Huang): 1 es ruido gaussiano, una red de pocos ecos se queda muy debajo.
static double densidad (const std::vector<float>& h, double desdeS, double hastaS)
{
    int i0 = 0;
    while (i0 < (int) h.size() && std::abs (h[(size_t) i0]) < 1.0e-6f) ++i0;
    const int ven = (int) (0.020 * kSr);
    double suma = 0.0; int n = 0;
    for (int c = i0 + (int) (desdeS * kSr); c + ven / 2 < i0 + (int) (hastaS * kSr); c += ven / 4)
    {
        double e = 0.0;
        for (int k = c - ven / 2; k < c + ven / 2; ++k) e += (double) h[(size_t) k] * h[(size_t) k];
        const double sd = std::sqrt (e / ven);
        int fuera = 0;
        for (int k = c - ven / 2; k < c + ven / 2; ++k) if (std::abs (h[(size_t) k]) > sd) ++fuera;
        suma += ((double) fuera / ven) / 0.3173105;
        ++n;
    }
    return suma / juce::jmax (1, n);
}

//  Picos modales de la cola: en cada tramo, cuanto sobresale cada banda de la mediana de su sexto de octava,
//  en dB y promediado entre tramos; el ruido se queda en 3 dB y un modo que no cambia de tramo a tramo, no.
static double picos (const std::vector<float>& h)
{
    const int orden = 12, n = 1 << orden;
    const int desde = (int) (0.15 * kSr), tramos = 8;
    const double hz = kSr / n;
    const int k0 = (int) (200.0 / hz), k1 = (int) (8000.0 / hz);
    std::vector<double> d ((size_t) (k1 - k0), 0.0);
    for (int t = 0; t < tramos; ++t)
    {
        auto p = espectro (h.data() + desde + t * n, orden);
        std::vector<double> db (p.size());
        for (size_t k = 0; k < p.size(); ++k) db[k] = 10.0 * std::log10 (p[k] + 1.0e-30);
        for (int k = k0; k < k1; ++k)
        {
            std::vector<double> v (db.begin() + (int) (k / 1.122), db.begin() + (int) (k * 1.122) + 2);
            std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
            d[(size_t) (k - k0)] += (db[(size_t) k] - v[v.size() / 2]) / tramos;
        }
    }
    std::sort (d.begin(), d.end());
    return d[(size_t) (0.99 * (double) (d.size() - 1))];
}

//  Trino: un seno fijo debe salir con envolvente fija; la oscilacion de su RMS en 10 ms, en dB de pico a valle.
static double trino (int f, double hz)
{
    auto e = monta (f, seno (hz, 0.25f), 1.0f);
    e->postNoteOn (0, 1.0f);
    std::vector<float> y;
    corre (*e, 48000 + 24000, y);
    if (auto* v = std::getenv ("ZATI_FX_VOLCADO"))
    {
        juce::FileOutputStream o (juce::File (juce::String (v) + "-" + juce::String (f) + "-t" + juce::String ((int) hz) + ".f32"));
        o.setPosition (0); o.truncate();
        o.write (y.data(), y.size() * sizeof (float));
    }
    const int ven = (int) std::lround (kSr / hz * 2.0 * juce::jmax (1.0, std::round (0.005 * hz)));   // periodos pares: OCT baja a la mitad
    double lo = 1.0e30, hi = 0.0;
    for (int c = 24000; c + ven <= (int) y.size(); c += ven / 2)
    {
        double s = 0.0;
        for (int k = c; k < c + ven; ++k) s += (double) y[(size_t) k] * y[(size_t) k];
        lo = juce::jmin (lo, s); hi = juce::jmax (hi, s);
    }
    return 10.0 * std::log10 (hi / juce::jmax (lo, 1.0e-30));
}

//  Tempo: con enganche, donde cae el primer eco de un impulso, en ms despues del seco.
static double primerEco (int f, double bpm, float div)
{
    auto pico = [&] (int fx, int desde)
    {
        auto e = monta (fx, impulso(), 1.0f);
        e->setPadLoop (0, false);
        e->setBpm (bpm);
        if (fx >= 0) { e->setFxParam (kCanal, fx, 1, 0.0f); e->setFxParam (kCanal, fx, 3, div); }
        juce::AudioBuffer<float> buf (2, kBs);
        for (int b = 0; b < 16; ++b) e->renderNextBlock (buf, 0, kBs);
        e->postNoteOn (0, 1.0f);
        int donde = 0; float mx = 0.0f;
        for (int b = 0; b * kBs < (int) (kSr * 2.5); ++b)
        {
            e->renderNextBlock (buf, 0, kBs);
            for (int i = 0; i < kBs; ++i)
            {
                const float v = std::abs (buf.getSample (0, i)) + std::abs (buf.getSample (1, i));
                if (b * kBs + i > desde && v > mx) { mx = v; donde = b * kBs + i; }
            }
        }
        return donde;
    };
    const int seco = pico (-1, -1);
    return (pico (f, seco + (int) (0.002 * kSr)) - seco) * 1000.0 / kSr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    midoReferencia();
    const int solo = argc > 1 ? juce::String (argv[1]).getIntValue() : -1;
    std::printf ("ZATI  banco de calidad de efectos   %.0f Hz  rafaga %d\n", kSr, kBs);
    {
        //  efectos#apagado: apagado es MIX 0; la ranura sigue en la cadena y lo que entra tiene que salir igual.
        auto resto = [] (const std::vector<float>& a, const std::vector<float>& b)
        {
            double e = 0.0, r = 0.0;
            for (size_t i = 24000; i < a.size(); ++i) { e += (a[i] - b[i]) * (a[i] - b[i]); r += b[i] * b[i]; }
            return 10.0 * std::log10 (juce::jmax (e, 1e-30) / juce::jmax (r, 1e-30));
        };
        auto suena = [] (int fOff, int fOn)
        {
            auto e = monta (-1, ruido (0.25f), 0.0f);
            if (fOff >= 0) { e->setCanalSend (kCanal, fOff, 1.0f); e->setFxParam (kCanal, fOff, 2, 0.0f); }
            if (fOn  >= 0) { e->setCanalSend (kCanal, fOn,  1.0f); e->setFxParam (kCanal, fOn,  2, 1.0f); }
            e->postNoteOn (0, 1.0f);
            std::vector<float> y; corre (*e, 48000, y);
            return y;
        };
        const int otro = AudioEngine::kFxDrv;
        const auto seco = suena (-1, -1), soloOtro = suena (-1, otro);
        std::printf ("\nAPAGADO  resto solo dB  resto antes de DRV dB\n");
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            if (solo >= 0 && f != solo) continue;
            const double r1 = resto (suena (f, -1), seco);
            const double r2 = f == otro ? -200.0 : resto (suena (f, otro), soloOtro);
            std::printf ("%-4s     %12.1f  %21.1f\n", kMandos[f].nombre, r1, r2);
            if (r1 > kApagadoMax) falla ("%s apagado no deja pasar el seco: resto %.1f dB (tope %.0f)", kMandos[f].nombre, r1, kApagadoMax);
            if (r2 > kApagadoMax) falla ("%s apagado delante de DRV cambia lo que suena: resto %.1f dB (tope %.0f)", kMandos[f].nombre, r2, kApagadoMax);
        }
    }
    if (std::getenv ("ZATI_FX_APAGADO")) return fallos == 0 ? 0 : 1;
    std::printf ("%-4s %9s %8s %8s %8s %9s %7s %9s %7s %7s\n",
                 "fx", "pliegue", "en Hz", "mover1", "mover2", "encender", "nivel", "cola", "trino", "preset");
    for (int f = 0; f < AudioEngine::kNumFx; ++f)
    {
        if (solo >= 0 && f != solo) continue;
        const auto pl = pliegue (f);
        const double m1 = mover (f, 0), m2 = mover (f, 1), en = encender (f), nv = nivel (f);
        const auto co = cola (f);
        const double tr = juce::jmax (trino (f, 110.0), trino (f, 440.0), trino (f, 1760.0));
        const double ps = preset (f);
        std::printf ("%-4s %9.1f %8.0f %8.1f %8.1f %9.1f %7.1f %9.1f %7.2f %7.1f%s\n",
                     kMandos[f].nombre, pl.dB, pl.hz, m1, m2, en, nv, co.finalDb, tr, ps, co.nan ? "  NaN" : "");
        const char* n = kMandos[f].nombre;
        if (f != AudioEngine::kFxBit && pl.dB > kPliegueMax)
            falla ("%s pliega %.1f dB en %.0f Hz (tope %.0f)", n, pl.dB, pl.hz, kPliegueMax);
        if (m1 > kChasquidoMax) falla ("%s chasquea %.1f dB al mover el mando 1 (tope %.0f)", n, m1, kChasquidoMax);
        if (m2 > kChasquidoMax) falla ("%s chasquea %.1f dB al mover el mando 2 (tope %.0f)", n, m2, kChasquidoMax);
        if (ps > kChasquidoMax) falla ("%s chasquea %.1f dB al cambiar de preset (tope %.0f)", n, ps, kChasquidoMax);
        if (en > kChasquidoMax) falla ("%s chasquea %.1f dB al encender o apagar (tope %.0f)", n, en, kChasquidoMax);
        if (std::abs (nv) > kNivelMax) falla ("%s sale %+.1f dB encendido (tope +-%.0f)", n, nv, kNivelMax);
        if (co.nan) falla ("%s deja un NaN en la cola", n);
        if (! modulaPorOficio (f) && tr > kTrinoMax) falla ("%s trina %.2f dB con un seno fijo (tope %.1f)", n, tr, kTrinoMax);
        if (f != AudioEngine::kFxFrz && co.finalDb > kColaMax) falla ("%s sigue a %.1f dB a los 10 s", n, co.finalDb);
        std::fflush (stdout);
    }
    if (solo < 0 || solo == AudioEngine::kFxDly || solo == AudioEngine::kFxPng)
    {
        std::printf ("\nTEMPO  bpm  figura  espera ms  mide ms\n");
        for (int f : { AudioEngine::kFxDly, AudioEngine::kFxPng })
            for (auto [bpm, div] : { std::pair<double, float> { 120.0, 2.0f }, { 90.0, 4.0f }, { 140.0, 1.0f } })
            {
                const double espera = 60000.0 / bpm * div / 4.0, mide = primerEco (f, bpm, div);
                std::printf ("%-4s  %5.0f   1/%-3.0f %9.1f %8.1f\n", kMandos[f].nombre, bpm, 16.0 / div, espera, mide);
                if (std::abs (mide - espera) > 1.0)
                    falla ("%s al tempo: a %.0f bpm la figura 1/%.0f cae a %.1f ms y no a %.1f", kMandos[f].nombre, bpm, 16.0 / div, mide, espera);
            }
    }
    if (solo < 0 || solo == AudioEngine::kFxDly)
    {
        //  efectos#tempo: cada chip suena a la figura que dice; si no cabe en el segundo de linea, a su mitad.
        std::printf ("\nCHIPS  figura  espera ms  mide ms\n");
        const double bpm = 150.0;
        for (const auto& fg : AudioEngine::kFigurasSync)
        {
            if (fg.div <= 0.5f) continue;
            const juce::String n (fg.nombre);
            double negras = 4.0 / n.fromFirstOccurrenceOf ("/", false, false).getDoubleValue();
            if (n.endsWithChar ('.')) negras *= 1.5;
            double espera = 60000.0 / bpm * negras;
            while (espera > 1000.0) espera *= 0.5;
            const double mide = primerEco (AudioEngine::kFxDly, bpm, fg.div);
            std::printf ("       %-6s %9.1f %8.1f\n", fg.nombre, espera, mide);
            if (std::abs (mide - espera) > 1.0)
                falla ("el chip %s del enganche suena a %.1f ms y no a %.1f", fg.nombre, mide, espera);
        }
    }
    if (solo < 0 || solo == AudioEngine::kFxRev)
    {
        std::printf ("\nREV  tamano  densidad  picos dB\n");
        for (float tam : { 0.3f, 0.55f, 1.0f })
        {
            const auto h = respuesta (tam, 0.45f);
            const double de = densidad (h, 0.050, 0.150), pi = picos (h);
            std::printf ("     %5.2f  %8.2f  %8.1f\n", tam, de, pi);
            if (de < kDensidadMin) falla ("REV a %.2f: densidad %.2f a 50-150 ms (minimo %.2f)", tam, de, kDensidadMin);
            if (pi > kPicosMax)    falla ("REV a %.2f: la cola tiene picos de %.1f dB (tope %.1f)", tam, pi, kPicosMax);
        }
    }
    if (solo < 0 || solo == AudioEngine::kFxExc)
    {
        //  exc#armonicos: un agudo flojo a fuerza 1 gana armonicos, no +6 dB en su propia frecuencia.
        auto banda = [] (const std::vector<float>& y, double hz)
        {
            double re = 0.0, im = 0.0;
            for (size_t i = 24000; i < y.size(); ++i)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / kSr;
                re += y[i] * std::cos (w); im += y[i] * std::sin (w);
            }
            return std::sqrt (re * re + im * im) * 2.0 / (double) (y.size() - 24000);
        };
        std::vector<float> y;
        auto e = monta (-1, seno (6000.0, 0.03f), 0.0f);
        e->postNoteOn (0, 1.0f); corre (*e, 48000, y);
        const double seco = banda (y, 6000.0);
        auto g = monta (AudioEngine::kFxExc, seno (6000.0, 0.03f), 1.0f);
        g->setFxParam (kCanal, AudioEngine::kFxExc, 0, 3000.0f);
        g->setFxParam (kCanal, AudioEngine::kFxExc, 1, 1.0f);
        g->postNoteOn (0, 1.0f); corre (*g, 48000, y);
        const double fund = 20.0 * std::log10 (banda (y, 6000.0) / seco);
        const double arm  = 20.0 * std::log10 ((banda (y, 12000.0) + banda (y, 18000.0)) / seco);
        std::printf ("\nEXC  6 kHz a -30 dB, fuerza 1: la nota %+.1f dB, armonicos %.1f dBc\n", fund, arm);
        if (std::abs (fund) > 1.5) falla ("EXC sube %.1f dB la propia nota (tope 1.5): es un realce, no armonicos", fund);
        if (arm < -40.0) falla ("EXC no anade armonicos: %.1f dBc (minimo -40)", arm);
    }
    std::printf (fallos == 0 ? "OK  los %d efectos, sin pliegue, sin chasquido, a nivel y con la cola limpia\n"
                             : "%d FALLA\n", fallos == 0 ? AudioEngine::kNumFx : fallos);
    return fallos == 0 ? 0 : 1;
}

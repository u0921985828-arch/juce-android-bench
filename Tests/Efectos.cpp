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
#include <cstdlib>
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
    { "GEQ", -12.0f, 12.0f, 0.5f, 2.0f, 1.00f },   // analogico#
    { "PRE", 0.0f, 1.0f, 0.0f, 1.0f, 1.00f },
    { "CIN", 0.0f, 1.0f, 0.0f, 1.0f, 1.00f },
    { "SMP", 4000.0f, 48000.0f, 8.0f, 16.0f, 1.00f },   // sampling#
    { "CLP", 0.0f, 18.0f, 0.0f, 1.0f, 1.00f },
    { "VIN", 0.0f, 1.0f, 0.0f, 1.0f, 1.00f },
    { "GLU", -30.0f, 0.0f, 1.5f, 10.0f, 1.00f },
    { "OTT", 0.0f, 1.0f, 0.0f, 1.0f, 1.00f },
    { "STP", 0.05f, 8.0f, 0.0f, 1.0f, 1.00f },
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

//  UN GOLPE QUE SE ACABA: 100 ms de tono que decae y dos segundos de silencio
//  detras. Para la columna ACOPLE hace falta una fuente que PARE: con el seno
//  en bucle de `seno()` -96000 muestras sin costura, a proposito- el pad sigue
//  sonando a los treinta segundos y la medida sale +0.0 dB en los treinta y
//  nueve, que es lo que canto que la sonda no medía nada.
static SampleBuffer::Ptr golpe (double hz, float amp)
{
    auto* sb = new SampleBuffer();
    const int n = (int) (kSr * 2.0);
    sb->buffer.setSize (2, n);
    const int largo = (int) (kSr * 0.1);
    for (int i = 0; i < n; ++i)
    {
        const float env = i < largo ? std::exp (-3.0f * (float) i / (float) largo) : 0.0f;
        const float v = amp * env * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kSr);
        sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

//  sampling#: un tono flojo que a mitad salta a fuerte, como el primer golpe tras un silencio.
static SampleBuffer::Ptr salto (double hz, float bajo, float alto)
{
    auto* sb = new SampleBuffer();
    const int n = 96000;
    sb->buffer.setSize (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = (i < 48000 ? bajo : alto) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / kSr);
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

//  calidad#: el peldano con el que corre TODO el banco, puesto por ZATI_CALIDAD.
static int gCalidad = 0;

//  Y UNO QUE NO SE REPITE, para las reglas que miran si algo SOSTIENE: `monta`
//  pone el pad en BUCLE -lo necesitan las columnas que miden con tono
//  continuo-, asi que con una muestra corta la fuente vuelve a disparar cada
//  dos segundos y una cola que sostiene no se distingue de una que cae.
static std::unique_ptr<AudioEngine> montaUnGolpe (SampleBuffer::Ptr s)
{
    auto e = std::make_unique<AudioEngine>();
    e->setCalidad (gCalidad);
    e->prepareToPlay (kSr, kBs);
    e->setPolyphony (8, 2);
    e->setPadGain (0, 1.0f);
    e->setPadPitch (0, 0.0f);
    e->setPadLoop (0, false);
    e->setPadCanal (0, kCanal);
    e->publishSample (0, s);
    return e;
}

static std::unique_ptr<AudioEngine> monta (int f, SampleBuffer::Ptr s, float mix)
{
    auto e = std::make_unique<AudioEngine>();
    e->setCalidad (gCalidad);
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
        case AudioEngine::kFxVin:   // sampling#: el wow abre la raya como un coro
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

//  El suelo de lo que se oye en las reglas de acople y de proyecto.
static constexpr double kOidoProy = -80.0;

//  Cuanto mas despacio va la referencia de `mover`. Ocho: un salto por bloque
//  se hace ocho veces mas pequeno ahi, y dieciseis segundos de tirada caben
//  tanto el azar como un modulador de 0.05 Hz.
static constexpr int kLento = 8;

static float enRango (float lo, float hi, float t)
{
    if (lo > 0.0f && hi / lo > 50.0f) return lo * std::pow (hi / lo, t);   // los de Hz van en octavas
    return lo + (hi - lo) * t;
}

//  EL MANDO BARRIDO CONTRA EL MISMO MANDO BARRIDO OCHO VECES MAS DESPACIO.
//
//  La referencia eran DIECISIETE SITIOS QUIETOS y medía dos cosas que no son
//  el mando. Las dos, medidas:
//
//   · LA VENTANA. El quieto se miraba en 8192 muestras y el barrido en 96000:
//     once veces mas tiempo para que caiga el peor pico. Con `ZATI_FX_QUIETO=1`
//     -el mando PARADO en la tirada larga- salian VIN mando 2 **+1.1 dB**,
//     SMP 0.0, STP 0.0 y FRZ **+12.5**, o sea una FALLA, sin que nadie tocara
//     nada: un efecto con AZAR dentro -el crujido del vinilo- o un modulador de
//     0.05 Hz -veinte segundos de vuelta- no cabe en 8192 muestras.
//   · LA RESOLUCION. Diecisiete sitios no cubren un mando cuyo brillo no es
//     monotono. Con `ZATI_FX_SITIOS=256`, BIT bajo de **+1.3 a -0.7 dB** y SMP
//     de +0.4 a -0.1: el barrido no chasqueaba, VISITABA un sitio quieto peor
//     que los diecisiete mirados.
//
//  Subir las dos cosas a la vez -doscientos cincuenta y siete sitios de dos
//  segundos- son once horas de banco. Asi que la referencia es el MISMO
//  barrido a un octavo de velocidad: visita todos los valores -resolucion
//  infinita- y dura dieciseis segundos -ventana de sobra-, y lo que queda
//  medido es exactamente la pregunta: cuanto añade mover el mando DEPRISA.
//  Un mecanismo que salta por bloque salta ocho veces menos ahi, asi que lo
//  que sobresale es el salto y no el sitio.
//
//  `ZATI_FX_QUIETO=1` deja el mando parado y `ZATI_FX_SITIOS=N` vuelve a la
//  referencia vieja con N sitios: las dos sondas siguen puestas porque son las
//  que descubrieron esto.
static double mover (int f, int par)
{
    const auto& m = kMandos[f];
    const float lo = par == 0 ? m.lo0 : m.lo1, hi = par == 0 ? m.hi0 : m.hi1;
    std::vector<float> y;
    const int arranque = 24000 / kBs;

    //  Una tirada con el mando subiendo y bajando a la velocidad pedida.
    //  `lento` multiplica el tiempo del barrido; 1 es el dedo de verdad.
    auto tirada = [&] (int lento) noexcept
    {
        const int barrido = 48000 * lento / kBs;
        auto e = monta (f, seno (220.0, 0.25f), m.onMix);
        e->setFxParam (kCanal, f, par, lo);
        e->postNoteOn (0, 1.0f);
        corre (*e, 24000 + 48000 * 2 * lento, y, [&] (int b)
        {
            const int k = b - arranque;
            if (k < 0) return;
            const float t = k < barrido ? (float) k / barrido
                                        : 1.0f - (float) (k - barrido) / barrido;
            e->setFxParam (kCanal, f, par, enRango (lo, hi, juce::jlimit (0.0f, 1.0f, t)));
        });
        return agudoPeor (y, 24000);
    };

    double quieto = 0.0;
    if (auto* sv = std::getenv ("ZATI_FX_SITIOS"))
    {
        //  La referencia vieja, a mano: N sitios quietos.
        const int sitios = juce::jlimit (4, 4096, std::atoi (sv));
        const int quietoLargo = std::getenv ("ZATI_FX_CORTO") != nullptr ? 24000 + 8192
                                                                        : 24000 + 48000 * 2;
        for (int i = 0; i <= sitios; ++i)
        {
            auto e = monta (f, seno (220.0, 0.25f), m.onMix);
            e->setFxParam (kCanal, f, par, enRango (lo, hi, (float) i / (float) sitios));
            e->postNoteOn (0, 1.0f);
            corre (*e, quietoLargo, y);
            quieto = juce::jmax (quieto, agudoPeor (y, 24000));
        }
    }
    else
    {
        quieto = tirada (kLento);
    }

    double rapido = 0.0;
    if (std::getenv ("ZATI_FX_QUIETO") != nullptr)
    {
        //  SONDA: el mando parado en la tirada corta. Si esto no sale muy
        //  negativo, la columna esta midiendo la ventana y no el mando.
        auto e = monta (f, seno (220.0, 0.25f), m.onMix);
        e->setFxParam (kCanal, f, par, lo);
        e->postNoteOn (0, 1.0f);
        corre (*e, 24000 + 48000 * 2, y);
        rapido = agudoPeor (y, 24000);
    }
    else
    {
        rapido = tirada (1);
    }

    if (auto* v = std::getenv ("ZATI_FX_VOLCADO"))
    {
        juce::FileOutputStream o (juce::File (juce::String (v) + "-" + juce::String (f) + "-" + juce::String (par) + ".f32"));
        o.setPosition (0); o.truncate();
        o.write (y.data(), y.size() * sizeof (float));
    }
    //  SONDA `ZATI_FX_ABS`: los dos numeros ABSOLUTOS. La columna es una
    //  diferencia contra una referencia ACOTADA en `kSuelo`, asi que un 0.0 en
    //  un efecto limpio no quiere decir «chasquea»: quiere decir que el
    //  artefacto esta en -70 dB, que es el suelo de lo inaudible que el propio
    //  banco fijo. Sin estos dos numeros la columna se lee al reves.
    if (std::getenv ("ZATI_FX_ABS") != nullptr)
        std::fprintf (stderr, "ABS mover %-4s p%d  rapido %7.1f  lento %7.1f\n",
                      kMandos[f].nombre, par, dB (rapido), dB (quieto));
    return dB (rapido) - juce::jmax (dB (quieto), kSuelo);
}

//  EL CARACTER: SI EL EFECTO CAMBIA AL SUBIRLE EL NIVEL DE ENTRADA.
//
//  Esta es la pregunta del muelle hecha a los treinta y nueve, y la unica que
//  no estaba en ninguna columna: un APARATO suena distinto flojo que fuerte
//  -una valvula, una cinta, un muelle, un compresor-, mientras una FORMULA
//  lineal suena igual a cualquier nivel y solo mas alta. Es literalmente la
//  diferencia entre «sonido analogico» y «digital», y hasta hoy el banco no
//  tenia una cifra para ella.
//
//  Se mide con el MISMO seno de 220 Hz a dos niveles -30 dB aparte- y se mira
//  cuanta mugre (armonicos del 2 al 8, mas el resto de la banda) lleva la nota
//  en cada caso. La cuenta se normaliza por la fundamental, asi que subir el
//  volumen no cuenta como caracter: solo cuenta que la FORMA cambie.
//
//  Un numero alto no es un fallo y un cero tampoco: un filtro, un EQ o un
//  retardo SON lineales y tienen que dar cero; lo que seria un fallo es que un
//  aparato -las saturaciones, la dinamica, el muelle- diera cero, porque
//  entonces no es el aparato, es su formula.
static double caracter (int f)
{
    const auto& m = kMandos[f];
    std::vector<float> y;
    auto mugre = [&] (float amp)
    {
        auto e = monta (f, seno (220.0, amp), m.onMix);
        e->postNoteOn (0, 1.0f);
        corre (*e, 48000 + 32768, y);
        const int orden = 15, n = 1 << orden;
        const auto p = espectro (y.data() + 48000, orden);
        auto raya = [&] (double hz)
        {
            double s2 = 0.0;
            const int k0 = (int) std::floor ((hz - 30.0) * n / kSr), k1 = (int) std::ceil ((hz + 30.0) * n / kSr);
            for (int k = juce::jmax (1, k0); k <= k1 && k < n / 2; ++k) s2 += p[(size_t) k];
            return s2;
        };
        double h1 = raya (220.0), arm = 0.0;
        for (int k = 2; k <= 8; ++k) arm += raya (220.0 * k);
        return dB (arm / juce::jmax (1.0e-30, h1));
    };
    const double flojo = mugre (0.016f);   // -36 dBFS
    const double duro  = mugre (0.5f);     //  -6 dBFS
    return duro - flojo;
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
        if (std::getenv ("ZATI_FX_DEPURA")) std::printf ("    preset %d -> %d  %.1f dB\n", k - 1, k, dB (agudoPeor (y, 24000)) - juce::jmax (dB (quieto), kSuelo));
    }
    return peor - juce::jmax (dB (quieto), kSuelo);
}

static double encender (int f)
{
    const auto& m = kMandos[f];
    std::vector<float> y;
    //  LA MISMA VENTANA QUE LA TIRADA CON EL INTERRUPTOR, por lo mismo que en
    //  `mover`: el quieto se media en 8192 muestras y el encendido en 52000.
    //  Un ROT con 13.8 dB de trino -su propio vaiven- sale «chasqueando» solo
    //  porque la ventana larga ve el peor cuarto de su vuelta y la corta no.
    auto e = monta (f, seno (220.0, 0.25f), m.onMix);
    e->postNoteOn (0, 1.0f);
    corre (*e, 24000 + 52000, y);
    const double quieto = agudoPeor (y, 24000);
    auto g = monta (f, seno (220.0, 0.25f), 0.0f);
    g->postNoteOn (0, 1.0f);
    const int on = 24000 / kBs, off = on + 24000 / kBs;
    corre (*g, 24000 * 3, y, [&] (int b)
    {
        if (b == on)  g->setFxParam (kCanal, f, 2, m.onMix);
        if (b == off) g->setFxParam (kCanal, f, 2, 0.0f);
    });
    const double conmuta = agudoPeor (y, 20000);
    if (std::getenv ("ZATI_FX_ABS") != nullptr)
        std::fprintf (stderr, "ABS encen %-4s      conmuta %6.1f  quieto %7.1f\n",
                      kMandos[f].nombre, dB (conmuta), dB (quieto));
    return dB (conmuta) - juce::jmax (dB (quieto), kSuelo);
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
        case AudioEngine::kFxDuc:
        case AudioEngine::kFxVin: case AudioEngine::kFxStp: return true;   // sampling#: el wow y el freno
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

static std::vector<float> respuesta (float tam, float amort, float tipo = 0.0f)
{
    auto e = monta (AudioEngine::kFxRev, impulso(), 1.0f);
    e->setPadLoop (0, false);
    e->setFxParam (kCanal, AudioEngine::kFxRev, 0, tam);
    e->setFxParam (kCanal, AudioEngine::kFxRev, 1, amort);
    e->setFxParam (kCanal, AudioEngine::kFxRev, 4, tipo);   // muelle#: 0 SALA ... el tipo que se mide
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


//  ---------------------------------------------------------------------------
//  DISTINCION: cada preset de un tipo tiene que sonar a otra cosa que los otros cinco.
//  Un golpe de ruido y un grave, cuatro por segundo durante 1.5 s y despues silencio:
//  asi se ve lo que el efecto hace con el ataque, con el tono y con la cola.
static SampleBuffer::Ptr golpes()
{
    auto* sb = new SampleBuffer();
    const int n = (int) (1.5 * kSr);
    sb->buffer.setSize (2, n);
    juce::Random r (11);
    for (int i = 0; i < n; ++i)
    {
        const int k = i % (int) (kSr / 4);
        const double t = k / kSr;
        const float v = (float) (0.55 * std::exp (-t * 40.0) * (r.nextFloat() * 2.0 - 1.0)
                               + 0.40 * std::exp (-t * 12.0) * std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t));
        sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

//  efectos#presets: una nota sostenida con ruido debajo, para lo que mueve el sonido en el tiempo (modulacion, barrido).
static SampleBuffer::Ptr sostenido()
{
    auto* sb = new SampleBuffer();
    const int n = (int) (1.5 * kSr);
    sb->buffer.setSize (2, n);
    juce::Random r (23), r2 (29);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        double v = 0.0;
        //  efectos#presets: ZATI_FX_ACORDE=1 cambia la nota por un acorde de tres (caso reservado: estimulo no visto al afinar).
        static const bool acorde = std::getenv ("ZATI_FX_ACORDE") != nullptr;
        if (acorde)
            for (double f0 : { 196.0, 246.9, 293.7 })
                for (int h = 1; h <= 5; ++h) v += 0.6 * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t) / h;
        else
            for (int h = 1; h <= 8; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * t) / h;
        const double fade = juce::jmin (1.0, juce::jmin (t, 1.5 - t) / 0.005);
        //  efectos#presets: el ruido distinto en cada lado, que un ensanchador sobre una entrada mono no tiene que hacer.
        sb->buffer.setSample (0, i, (float) (fade * (0.18 * v + 0.06 * (r.nextFloat() * 2.0 - 1.0))));
        sb->buffer.setSample (1, i, (float) (fade * (0.18 * v + 0.06 * (r2.nextFloat() * 2.0 - 1.0))));
    }
    sb->sourceSampleRate = kSr;
    return SampleBuffer::Ptr (sb);
}

struct Huella { double banda[10]; double env[30]; double lado; double banda2[10]; double mod[6]; double barre[8]; double forma[50]; double lado2; };

//  efectos#presets: ZATI_FX_AJUSTE=fichero con lineas «f k pi valor» o «f k eq bandas» prueba presets sin compilar.
static const std::map<std::tuple<int, int, int>, float>& ajustes (std::map<int, juce::String>** eq = nullptr)
{
    static std::map<std::tuple<int, int, int>, float> m;
    static std::map<int, juce::String> bandas;
    static bool leido = false;
    if (! leido)
    {
        leido = true;
        if (const char* r = std::getenv ("ZATI_FX_AJUSTE"))
            for (auto& l : juce::StringArray::fromLines (juce::File (r).loadFileAsString()))
            {
                auto t = juce::StringArray::fromTokens (l.trim(), " ", "");
                if (t.size() < 4 || t[0].startsWith ("#")) continue;
                if (t[2] == "eq") bandas[t[1].getIntValue()] = t[3];
                else m[{ t[0].getIntValue(), t[1].getIntValue(), t[2].getIntValue() }] = t[3].getFloatValue();
            }
    }
    if (eq != nullptr) *eq = &bandas;
    return m;
}

//  efectos#tipos: el banco de TIPOS fuerza un parametro encima del preset.
static int gFuerzaPi = -1; static float gFuerzaV = 0.0f;

static void ponPreset (AudioEngine& e, int f, int k)
{
    std::map<int, juce::String>* eqAj = nullptr;
    const auto& aj = ajustes (&eqAj);
    for (int pi = 0; pi < AudioEngine::kNumParFx; ++pi)
    {
        const auto it = aj.find ({ f, k, pi });
        e.setFxParam (kCanal, f, pi, pi == gFuerzaPi ? gFuerzaV : it != aj.end() ? it->second : FxPresets::valor (f, k, pi));
    }
    juce::String bAj = f == AudioEngine::kFxEq && eqAj->count (k) ? (*eqAj)[k] : juce::String();
    if (const char* b = bAj.isNotEmpty() ? bAj.toRawUTF8() : FxPresets::bandas (f, k))
    {
        auto filas = juce::StringArray::fromTokens (b, ";", "");
        for (int i = 0; i < filas.size(); ++i)
        {
            auto t = juce::StringArray::fromTokens (filas[i], ":", "");
            if (t.size() < 4) continue;
            e.setEqBand (kCanal, i, t[0].getFloatValue(), t[1].getFloatValue());
            e.setEqTipo (kCanal, i, t[2].getIntValue());
            e.setEqQ    (kCanal, i, t[3].getFloatValue());
        }
    }
}

static Huella huella (int f, int k)
{
    const int n = 3 * (int) kSr;
    auto suena = [f, k, n] (SampleBuffer::Ptr st, std::vector<float>& L, std::vector<float>& R)
    {
        auto e = monta (f, st, 1.0f);
        e->setPadLoop (0, false);
        ponPreset (*e, f, k);
        e->postNoteOn (0, 1.0f);
        juce::AudioBuffer<float> buf (2, kBs);
        L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
        for (int b = 0; b * kBs < n; ++b)
        {
            e->renderNextBlock (buf, 0, kBs);
            for (int i = 0; i < kBs && b * kBs + i < n; ++i)
            { L[(size_t) (b * kBs + i)] = buf.getSample (0, i); R[(size_t) (b * kBs + i)] = buf.getSample (1, i); }
        }
    };
    std::vector<float> L, R;
    suena (golpes(), L, R);
    Huella h {};
    std::vector<float> m ((size_t) n);
    double em = 0.0, es = 0.0;
    for (int i = 0; i < n; ++i)
    {
        m[(size_t) i] = 0.5f * (L[(size_t) i] + R[(size_t) i]);
        const double sd = 0.5 * (L[(size_t) i] - R[(size_t) i]);
        em += m[(size_t) i] * m[(size_t) i]; es += sd * sd;
    }
    if (std::getenv ("ZATI_FX_DEPURA")) { float mx = 0; int nan = 0; for (float v : m) { if (! std::isfinite (v)) ++nan; else mx = juce::jmax (mx, std::abs (v)); }
        std::printf ("  [%d/%d] rms %.4f pico %.4f nan %d\n", f, k, std::sqrt (em / n), mx, nan); }
    h.lado = juce::jmax (-60.0, 10.0 * std::log10 (juce::jmax (es, 1e-30) / juce::jmax (em, 1e-30)));
    for (int w = 0; w < 30; ++w)
    {
        double e2 = 0.0;
        for (int i = w * n / 30; i < (w + 1) * n / 30; ++i) e2 += m[(size_t) i] * m[(size_t) i];
        h.env[w] = juce::jmax (-80.0, 10.0 * std::log10 (juce::jmax (e2 / (n / 30), 1e-30)));
    }
    //  efectos#presets: la forma de un golpe, 5 ms por marco y la media de los seis; lo que oye un compresor o una puerta.
    for (int j = 0; j < 50; ++j)
    {
        double e2 = 0.0;
        for (int g = 0; g < 6; ++g)
            for (int i = 0; i < 240; ++i) { const double v = m[(size_t) (g * 12000 + j * 240 + i)]; e2 += v * v; }
        h.forma[j] = juce::jmax (-80.0, 10.0 * std::log10 (juce::jmax (e2 / (6 * 240), 1e-30)));
    }
    const int orden = 17, N = 1 << orden;
    std::vector<float> x ((size_t) N, 0.0f);
    std::copy (m.begin(), m.begin() + juce::jmin (n, N), x.begin());
    const auto p = espectro (x.data(), orden);
    for (int b = 0; b < 10; ++b)
    {
        const double lo = 31.25 * std::pow (2.0, b), hi = lo * 2.0;
        double e2 = 0.0;
        for (int i = (int) (lo * N / kSr); i < (int) (hi * N / kSr) && i < N / 2; ++i) e2 += p[(size_t) i];
        h.banda[b] = juce::jmax (-80.0, dB (e2 / N));
    }

    //  La nota sostenida: su tono, cuanto late la envolvente por octavas de 0.8 a 50 Hz y cuanto se mueve cada banda.
    std::vector<float> L2, R2;
    suena (sostenido(), L2, R2);
    std::vector<float> m2 ((size_t) n);
    double em2 = 0.0, es2 = 0.0;
    for (int i = 0; i < n; ++i)
    {
        m2[(size_t) i] = 0.5f * (L2[(size_t) i] + R2[(size_t) i]);
        const double sd = 0.5 * (L2[(size_t) i] - R2[(size_t) i]);
        em2 += m2[(size_t) i] * m2[(size_t) i]; es2 += sd * sd;
    }
    h.lado2 = juce::jmax (-60.0, 10.0 * std::log10 (juce::jmax (es2, 1e-30) / juce::jmax (em2, 1e-30)));
    {
        std::vector<float> x2 ((size_t) N, 0.0f);
        std::copy (m2.begin(), m2.begin() + juce::jmin (n, N), x2.begin());
        const auto p2 = espectro (x2.data(), orden);
        for (int b = 0; b < 10; ++b)
        {
            const double lo = 31.25 * std::pow (2.0, b), hi = lo * 2.0;
            double e2 = 0.0;
            for (int i = (int) (lo * N / kSr); i < (int) (hi * N / kSr) && i < N / 2; ++i) e2 += p2[(size_t) i];
            h.banda2[b] = juce::jmax (-80.0, dB (e2 / N));
        }
    }
    {
        const int marco = 480, i0 = (int) (0.2 * kSr), nm = 128;
        std::vector<float> env ((size_t) (2 * nm), 0.0f);
        double media = 0.0;
        for (int j = 0; j < nm; ++j)
        {
            double e2 = 0.0;
            for (int i = 0; i < marco; ++i) { const double v = L2[(size_t) (i0 + j * marco + i)]; e2 += v * v; }   // efectos#presets: un lado, que un rotativo mueve el sonido de lado a lado
            env[(size_t) j] = (float) std::sqrt (e2 / marco); media += env[(size_t) j];
        }
        media = juce::jmax (1.0e-9, media / nm);
        for (int j = 0; j < nm; ++j) env[(size_t) j] = (float) (env[(size_t) j] / media - 1.0);
        juce::dsp::FFT fft (7);
        fft.performFrequencyOnlyForwardTransform (env.data());
        for (int b = 0; b < 6; ++b)
        {
            double e2 = 0.0;
            for (int i = 1 << b; i < (2 << b) && i < nm / 2; ++i) e2 += (double) env[(size_t) i] * env[(size_t) i];
            h.mod[b] = juce::jmax (-60.0, dB (e2 / nm));
        }
    }
    {
        const int orden3 = 11, N3 = 1 << orden3, i0 = (int) (0.2 * kSr), nf = 24;
        double suma[8] {}, suma2[8] {};
        for (int j = 0; j < nf; ++j)
        {
            const auto p3 = espectro (m2.data() + i0 + j * N3 / 2, orden3);
            for (int b = 0; b < 8; ++b)
            {
                const double lo = 125.0 * std::pow (2.0, b), hi = lo * 2.0;
                double e2 = 0.0;
                for (int i = (int) (lo * N3 / kSr); i < (int) (hi * N3 / kSr) && i < N3 / 2; ++i) e2 += p3[(size_t) i];
                const double d = juce::jmax (-80.0, dB (e2 / N3));
                suma[b] += d; suma2[b] += d * d;
            }
        }
        for (int b = 0; b < 8; ++b)
            h.barre[b] = std::sqrt (juce::jmax (0.0, suma2[b] / nf - std::pow (suma[b] / nf, 2.0)));
    }
    return h;
}

//  Lo que separa dos huellas, en dB: el peor de tono, envolvente y anchura.
static double distancia (const Huella& a, const Huella& b)
{
    //  El nivel global no cuenta: dos presets que solo suenan mas fuerte son el mismo con el fader movido.
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < 10; ++i) { ma += a.banda[i]; mb += b.banda[i]; }
    const double off = (ma - mb) / 10.0;
    double t = 0.0, v = 0.0;
    for (int i = 0; i < 10; ++i) t += std::pow (a.banda[i] - b.banda[i] - off, 2.0);
    for (int i = 0; i < 30; ++i) v += std::pow (a.env[i] - b.env[i] - off, 2.0);
    double fa = 0.0, fb = 0.0, fo = 0.0;
    for (int i = 0; i < 50; ++i) { fa += a.forma[i]; fb += b.forma[i]; }
    for (int i = 0; i < 50; ++i) fo += std::pow (a.forma[i] - b.forma[i] - (fa - fb) / 50.0, 2.0);
    double ma2 = 0.0, mb2 = 0.0;
    for (int i = 0; i < 10; ++i) { ma2 += a.banda2[i]; mb2 += b.banda2[i]; }
    const double off2 = (ma2 - mb2) / 10.0;
    double t2 = 0.0, md = 0.0, br = 0.0;
    for (int i = 0; i < 10; ++i) t2 += std::pow (a.banda2[i] - b.banda2[i] - off2, 2.0);
    for (int i = 0; i < 6; ++i)  md += std::pow (a.mod[i] - b.mod[i], 2.0);
    for (int i = 0; i < 8; ++i)  br += std::pow (a.barre[i] - b.barre[i], 2.0);
    //  efectos#presets: la latida se cuenta a la mitad; es dB de una envolvente y no de un espectro.
    if (std::getenv ("ZATI_FX_DEPURA"))
        std::printf ("      lado2 %.1f tono %.1f env %.1f lado %.1f forma %.1f tono2 %.1f late %.1f barre %.1f\n", std::abs (a.lado2 - b.lado2),
                     std::sqrt (t / 10.0), std::sqrt (v / 30.0),
                     std::abs (a.lado - b.lado), std::sqrt (fo / 50.0), std::sqrt (t2 / 10.0), 0.5 * std::sqrt (md / 6.0), std::sqrt (br / 8.0));
    return juce::jmax (juce::jmax (std::sqrt (t / 10.0), std::sqrt (v / 30.0), std::abs (a.lado - b.lado), std::sqrt (fo / 50.0)), std::abs (a.lado2 - b.lado2),
                       juce::jmax (std::sqrt (t2 / 10.0), 0.5 * std::sqrt (md / 6.0), std::sqrt (br / 8.0)));
}
static constexpr double kDistintoMin = 3.0;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    midoReferencia();
    const int solo = argc > 1 ? juce::String (argv[1]).getIntValue() : -1;
    //  calidad#: ZATI_CALIDAD fuerza el peldano (0 plena, 1 media, 2 baja) para
    //  que las nueve reglas se corran en los tres y no solo en el de arriba. Un
    //  peldano que no se mide es un peldano que un dia suena a lata.
    const int calidadFijada = [] { const char* v = std::getenv ("ZATI_CALIDAD");
                                   return v != nullptr ? juce::jlimit (0, 2, juce::String (v).getIntValue()) : 0; }();
    gCalidad = calidadFijada;
    if (calidadFijada > 0) std::printf ("CALIDAD forzada al peldano %d\n", calidadFijada);
    std::printf ("ZATI  banco de calidad de efectos   %.0f Hz  rafaga %d\n", kSr, kBs);
    //  efectos#tipos: cada tipo de un efecto, sobre el mismo preset, tiene que sonar a otra cosa que los demas.
    if (std::getenv ("ZATI_FX_TIPOS"))
    {
        struct Caso { int f, pi, n, k; };
        const Caso casos[] = { { AudioEngine::kFxRev, 4, 5, 2 }, { AudioEngine::kFxDrv, 4, 4, 3 }, { AudioEngine::kFxDly, 5, 3, 4 },
                               { AudioEngine::kFxTrm, 4, 4, 2 }, { AudioEngine::kFxFlt, 4, 2, 1 } };
        std::printf ("\nTIPOS  la pareja de tipos mas parecida sobre un preset, en dB (minimo %.0f)\n", kDistintoMin);
        for (const auto& c : casos)
        {
            if (solo >= 0 && c.f != solo) continue;
            std::vector<Huella> hs;
            for (int t = 0; t < c.n; ++t) { gFuerzaPi = c.pi; gFuerzaV = (float) t; hs.push_back (huella (c.f, c.k)); }
            gFuerzaPi = -1;
            double peor = 1e9; int pa = 0, pb = 0;
            for (int a = 0; a < c.n; ++a)
                for (int b = a + 1; b < c.n; ++b)
                {
                    const double d = distancia (hs[(size_t) a], hs[(size_t) b]);
                    if (solo >= 0) std::printf ("    tipo %d ~ tipo %d %5.1f\n", a, b, d);
                    if (d < peor) { peor = d; pa = a; pb = b; }
                }
            std::printf ("%-4s p%d sobre %-10s %6.1f  tipo %d ~ tipo %d\n", kMandos[c.f].nombre, c.pi, FxPresets::nombre (c.f, c.k), peor, pa, pb);
            if (peor < kDistintoMin) falla ("%s: los tipos %d y %d suenan casi igual (%.1f dB)", kMandos[c.f].nombre, pa, pb, peor);
            std::fflush (stdout);
        }
        return fallos == 0 ? 0 : 1;
    }
    if (std::getenv ("ZATI_FX_PRESETS"))
    {
        std::printf ("\nDISTINCION  la pareja mas parecida de los %d presets, en dB (minimo %.0f)\n",
                     FxPresets::cuantos(), kDistintoMin);
        int flojos = 0;
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            if (solo >= 0 && f != solo) continue;
            std::vector<Huella> hs;
            for (int k = 0; k < FxPresets::cuantos(); ++k) hs.push_back (huella (f, k));
            double peor = 1e9; int pa = 0, pb = 0, bajo = 0;
            for (int a = 0; a < (int) hs.size(); ++a)
                for (int b = a + 1; b < (int) hs.size(); ++b)
                {
                    const double d = distancia (hs[(size_t) a], hs[(size_t) b]);
                    if (solo >= 0) std::printf ("    %-10s ~ %-10s %5.1f\n", FxPresets::nombre (f, a), FxPresets::nombre (f, b), d);
                    if (d < kDistintoMin) ++bajo;
                    if (d < peor) { peor = d; pa = a; pb = b; }
                }
            double nivel = 0.0; for (double v : hs[1].banda) nivel += v / 10.0;
            std::printf ("%-4s  %6.1f  %s ~ %s   nivel %5.1f  parejas bajo el minimo %d de 15\n", kMandos[f].nombre, peor,
                         FxPresets::nombre (f, pa), FxPresets::nombre (f, pb), nivel, bajo);
            if (bajo > 0) { ++flojos; falla ("%s: %d parejas de presets suenan casi igual (%s ~ %s, %.1f dB)",
                                             kMandos[f].nombre, bajo, FxPresets::nombre (f, pa), FxPresets::nombre (f, pb), peor); }
            std::fflush (stdout);
        }
        std::printf ("tipos con presets que se confunden: %d\n", flojos);
        return fallos == 0 ? 0 : 1;
    }
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
        //  efectos#apagado: con ZATI_FX_PAREJAS, cada apagado junto a cada uno de los otros 29 encendidos.
        if (std::getenv ("ZATI_FX_PAREJAS"))
        {
            int malas = 0;
            double peor = -400.0; int pf = -1, pg = -1;
            for (int g = 0; g < AudioEngine::kNumFx; ++g)
            {
                const auto soloG = suena (-1, g);
                for (int f = 0; f < AudioEngine::kNumFx; ++f)
                {
                    if (f == g || (solo >= 0 && f != solo)) continue;
                    const double r = resto (suena (f, g), soloG);
                    if (r > peor) { peor = r; pf = f; pg = g; }
                    if (r > kApagadoMax) { ++malas; falla ("%s apagado junto a %s encendido cambia lo que suena: resto %.1f dB", kMandos[f].nombre, kMandos[g].nombre, r); }
                }
            }
            std::printf ("PAREJAS  peor %.1f dB (%s apagado con %s)  malas %d\n", peor,
                         pf >= 0 ? kMandos[pf].nombre : "-", pg >= 0 ? kMandos[pg].nombre : "-", malas);
        }
    }
    {
        //  efectos#retorno: un canal manda a RETORNO A (canal 15, con REV) o B (canal 16, con DLY) y lo humedo sale una vez, de la sala del retorno.
        constexpr int kRetA = AudioEngine::kCanalRetorno[0], kRetB = AudioEngine::kCanalRetorno[1];
        auto ruidoDe = [] (int semilla)
        {
            auto* sb = new SampleBuffer();
            sb->buffer.setSize (2, 96000);
            juce::Random r (semilla);
            for (int i = 0; i < 96000; ++i)
            {
                const float v = 0.25f * (r.nextFloat() * 2.0f - 1.0f);
                sb->buffer.setSample (0, i, v); sb->buffer.setSample (1, i, v);
            }
            sb->sourceSampleRate = kSr;
            return SampleBuffer::Ptr (sb);
        };
        //  canales[k] < 0: el pad k no suena. envia[k]: A, B o -1.
        auto suena = [&] (std::array<int, 2> canales, std::array<int, 2> envia, bool retMudo)
        {
            auto e = monta (-1, ruido (0.25f), 0.0f);
            e->setSafetyLimiter (false);   // la suma de dos ruidos no puede tocar el limitador o deja de ser una suma
            constexpr int kPad[2] = { 0, 1 };
            e->publishSample (kPad[1], ruidoDe (11));
            e->setPadGain (kPad[1], 1.0f); e->setPadLoop (kPad[1], true);
            for (int k = 0; k < 2; ++k) { e->setPadChoke (kPad[k], 0); e->setPadSelfCut (kPad[k], false); e->setPadPitch (kPad[k], 0.0f); }
            e->setCanalSend (kRetA, AudioEngine::kFxRev, 1.0f); e->setFxParam (kRetA, AudioEngine::kFxRev, 2, 1.0f);
            e->setCanalSend (kRetB, AudioEngine::kFxDly, 1.0f); e->setFxParam (kRetB, AudioEngine::kFxDly, 2, 1.0f);
            e->setCanalMute (kRetA, retMudo);
            for (int k = 0; k < 2; ++k)
            {
                if (canales[(size_t) k] < 0) continue;
                e->setPadCanal (kPad[k], canales[(size_t) k]);
                if (envia[(size_t) k] >= 0) e->setCanalEnvio (canales[(size_t) k], AudioEngine::kCanalRetorno[envia[(size_t) k]], 1.0f);
            }
            //  el primer bloque tras preparar se queda con un solo golpe (Tribunal 2026-09, 1.2): uno en vacio antes.
            { juce::AudioBuffer<float> b (2, kBs); e->renderNextBlock (b, 0, kBs); }
            for (int k = 0; k < 2; ++k)
                if (canales[(size_t) k] >= 0) e->postNoteOn (kPad[k], 1.0f);
            std::vector<float> y; corre (*e, 48000, y);
            return y;
        };
        auto menos = [] (std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] -= b[i]; return a; };
        auto mas   = [] (std::vector<float> a, const std::vector<float>& b) { for (size_t i = 0; i < a.size(); ++i) a[i] += b[i]; return a; };
        auto potDe = [] (const std::vector<float>& a, const std::vector<float>& ref)
        {
            double e = 0.0, r = 0.0;
            for (size_t i = 24000; i < a.size(); ++i) { e += a[i] * a[i]; r += ref[i] * ref[i]; }
            return 10.0 * std::log10 (juce::jmax (e, 1e-30) / juce::jmax (r, 1e-30));
        };
        auto resto = [&] (const std::vector<float>& a, const std::vector<float>& b) { return potDe (menos (a, b), b); };

        const auto seco1  = suena ({ 1, -1 }, { -1, -1 }, false);
        const auto envA1  = suena ({ 1, -1 }, { 0, -1 }, false);
        const auto mudoA1 = suena ({ 1, -1 }, { 0, -1 }, true);
        const auto seco20 = suena ({ 20, -1 }, { -1, -1 }, false);
        const auto envA20 = suena ({ 20, -1 }, { 0, -1 }, false);
        const auto envB1  = suena ({ 1, -1 }, { 1, -1 }, false);
        const auto seco2  = suena ({ -1, 2 }, { -1, -1 }, false);
        const auto envA2  = suena ({ -1, 2 }, { -1, 0 }, false);
        const auto seco12 = suena ({ 1, 2 }, { -1, -1 }, false);
        const auto envA12 = suena ({ 1, 2 }, { 0, 0 }, false);
        const auto humA1 = menos (envA1, seco1), humA20 = menos (envA20, seco20), humB1 = menos (envB1, seco1);
        const auto humA12 = menos (envA12, seco12), humSuma = mas (humA1, menos (envA2, seco2));

        const double sala = potDe (humA1, seco1), eco = potDe (humB1, seco1);
        const double mudo = resto (mudoA1, seco1), orden = resto (humA20, humA1), compartida = resto (humA12, humSuma);
        std::printf ("RETORNO  sala A %.1f dB  eco B %.1f dB  retorno mudo %.1f dB  desde el canal 21 %.1f dB  dos canales una sala %.1f dB\n",
                     sala, eco, mudo, orden, compartida);
        if (sala < -20.0) falla ("el envio a RETORNO A no llega a su REV: humedo %.1f dB", sala);
        if (eco  < -20.0) falla ("el envio a RETORNO B no llega a su DLY: humedo %.1f dB", eco);
        if (mudo > kApagadoMax) falla ("con RETORNO A en mute el envio sigue sonando: resto %.1f dB", mudo);
        if (orden > kApagadoMax) falla ("el canal 21 manda distinto que el 2 a la misma sala: resto %.1f dB", orden);
        if (compartida > kApagadoMax) falla ("dos canales en RETORNO A no son una sala compartida: resto %.1f dB", compartida);

        //  efectos#rutas: cualquier canal hace de retorno, en cadena y en cualquier orden de numero; lo que cerraria un circulo se niega.
        auto ruta = [&] (std::vector<std::array<int, 2>> envios, std::vector<std::array<int, 2>> fxDe, int calla)
        {
            auto e = monta (-1, ruido (0.25f), 0.0f);
            e->setSafetyLimiter (false);
            e->setPadChoke (0, 0); e->setPadSelfCut (0, false); e->setPadPitch (0, 0.0f);
            e->setPadCanal (0, 1);
            for (const auto& cf : fxDe) { e->setCanalSend (cf[0], cf[1], 1.0f); e->setFxParam (cf[0], cf[1], 2, 1.0f); }
            for (const auto& ab : envios) e->setCanalEnvio (ab[0], ab[1], 1.0f);
            if (calla >= 0) e->setCanalMute (calla, true);
            { juce::AudioBuffer<float> b (2, kBs); e->renderNextBlock (b, 0, kBs); }
            e->postNoteOn (0, 1.0f);
            std::vector<float> y; corre (*e, 48000, y);
            return y;
        };
        constexpr int kRev = AudioEngine::kFxRev, kDly = AudioEngine::kFxDly;
        const auto rSeco = ruta ({}, {}, -1);
        const auto r6    = ruta ({ { 1, 5 } },  { { 5, kRev } },  -1);
        const auto r15   = ruta ({ { 1, 14 } }, { { 14, kRev } }, -1);
        const auto r6m   = ruta ({ { 1, 5 } },  { { 5, kRev } },  5);
        const auto r8    = ruta ({ { 1, 7 } },  { { 7, kDly } },  -1);
        const auto r68   = ruta ({ { 1, 5 }, { 1, 7 } }, { { 5, kRev }, { 7, kDly } }, -1);
        const auto c1    = ruta ({ { 1, 9 } },           { { 9, kDly }, { 3, kRev } }, -1);
        const auto c2    = ruta ({ { 1, 9 }, { 9, 3 } }, { { 9, kDly }, { 3, kRev } }, -1);
        const auto h6 = menos (r6, rSeco);
        const double salaR = potDe (h6, rSeco), igual = resto (h6, menos (r15, rSeco));
        const double mudoR = resto (r6m, rSeco), dosRet = resto (menos (r68, rSeco), mas (h6, menos (r8, rSeco)));
        const double cadena = potDe (menos (c2, c1), rSeco);
        int circulos = 0;
        {
            auto e = monta (-1, ruido (0.25f), 0.0f);
            if (! e->setCanalEnvio (1, 9, 0.5f) || ! e->setCanalEnvio (9, 3, 0.5f)) ++circulos;   // una cadena abierta se acepta
            if (e->setCanalEnvio (3, 1, 0.5f) || e->getCanalEnvio (3, 1) > 0.0f) ++circulos;    // y cerrarla, no
            if (e->setCanalEnvio (4, 4, 0.5f) || e->getCanalEnvio (4, 4) > 0.0f) ++circulos;    // ni a si mismo
        }
        std::printf ("RUTAS  sala en el canal 6 %.1f dB  igual que en el 15 %.1f dB  mudo %.1f dB  dos retornos %.1f dB  cadena 2>10>4 %.1f dB  circulos mal %d\n",
                     salaR, igual, mudoR, dosRet, cadena, circulos);
        if (salaR < -20.0) falla ("el envio al canal 6 no llega a su REV: humedo %.1f dB", salaR);
        if (igual > kApagadoMax) falla ("mandar al canal 6 no suena igual que al 15 con la misma sala: resto %.1f dB", igual);
        if (mudoR > kApagadoMax) falla ("con el canal 6 en mute su envio sigue sonando: resto %.1f dB", mudoR);
        if (dosRet > kApagadoMax) falla ("un canal a dos retornos no es la suma de cada uno: resto %.1f dB", dosRet);
        if (cadena < -30.0) falla ("en la cadena 2>10>4 el canal 4 no recibe lo del 10: %.1f dB", cadena);
        if (circulos) falla ("los envios aceptan un circulo o niegan una cadena abierta: %d", circulos);

        //  mesa#envio: un canal sin efectos que recibe suena (el seco se suma) y su aguja lo dice; el que no recibe, no.
        const auto rSin = ruta ({ { 1, 7 } }, {}, -1);
        const double sinFx = potDe (menos (rSin, rSeco), rSeco);
        float agujaRecibe = 0.0f, agujaOtro = 0.0f;
        {
            auto e = monta (-1, ruido (0.25f), 0.0f);
            e->setPadChoke (0, 0); e->setPadSelfCut (0, false); e->setPadCanal (0, 1);
            e->setCanalEnvio (1, 7, 1.0f);
            e->miraMesa (true);
            { juce::AudioBuffer<float> b (2, kBs); e->renderNextBlock (b, 0, kBs); }
            e->postNoteOn (0, 1.0f);
            std::vector<float> y; corre (*e, 4800, y);
            agujaRecibe = e->readEnvioPico (7);
            agujaOtro = e->readEnvioPico (5);
        }
        std::printf ("ENVIO  canal 8 sin efectos %.1f dB  aguja del 8 %.3f  del 6 %.3f\n", sinFx, agujaRecibe, agujaOtro);
        if (sinFx < -10.0) falla ("lo enviado a un canal sin efectos no suena: %.1f dB", sinFx);
        if (agujaRecibe <= 0.01f || agujaOtro > 0.0f) falla ("la aguja del canal que recibe no se mueve (%.3f) o se mueve la de otro (%.3f)", agujaRecibe, agujaOtro);
    }
    {
        //  efectos#fasemod: dos de CHO, FLA, PHA y TRM en la misma cadena; el punto del visor de cada uno tiene que moverse con señal.
        constexpr int kMod[] = { AudioEngine::kFxCho, AudioEngine::kFxFla, AudioEngine::kFxPha, AudioEngine::kFxTrm };
        int quietos = 0, parejas = 0;
        for (int a : kMod)
            for (int b : kMod)
            {
                if (a == b) continue;
                auto e = monta (-1, ruido (0.25f), 0.0f);
                for (int f : { a, b }) { e->setCanalSend (kCanal, f, 1.0f); e->setFxParam (kCanal, f, 2, 1.0f); }
                e->postNoteOn (0, 1.0f);
                juce::AudioBuffer<float> blk (2, kBs);
                int vistos[2] = {};
                float antes[2] = { -2.0f, -2.0f };
                for (int k = 0; k < 375; ++k)
                {
                    blk.clear(); e->renderNextBlock (blk, 0, kBs);
                    if (k < 40) continue;
                    for (int j = 0; j < 2; ++j)
                    {
                        const float ph = e->getLfoFase (kCanal, j == 0 ? a : b);
                        if (ph > 0.0f && std::abs (ph - antes[j]) > 0.0f) ++vistos[j];
                        antes[j] = ph;
                    }
                }
                ++parejas;
                for (int j = 0; j < 2; ++j)
                    if (vistos[j] < 100)
                    {
                        ++quietos;
                        falla ("%s con %s en la cadena: el punto de %s se movio %d de 335 bloques con señal",
                               kMandos[a].nombre, kMandos[b].nombre, kMandos[j == 0 ? a : b].nombre, vistos[j]);
                    }
            }
        std::printf ("FASEMOD  parejas %d  puntos quietos con señal %d\n", parejas, quietos);
    }
    if (std::getenv ("ZATI_FX_APAGADO")) return fallos == 0 ? 0 : 1;
    //  Sonda: `ZATI_FX_CASO` salta a las reglas de abajo -el caso de verdad y
    //  la biseccion dentro de el- sin correr la tabla de los treinta y nueve.
    const bool soloCaso = std::getenv ("ZATI_FX_CASO") != nullptr;
    std::printf ("%-4s %9s %8s %8s %8s %9s %7s %9s %7s %7s %8s\n",
                 "fx", "pliegue", "en Hz", "mover1", "mover2", "encender", "nivel", "cola", "trino", "preset", "caracter");
    for (int f = 0; f < AudioEngine::kNumFx; ++f)
    {
        if (solo >= 0 && f != solo) continue;
        const auto pl = pliegue (f);
        const double m1 = mover (f, 0), m2 = mover (f, 1), en = encender (f), nv = nivel (f);
        const auto co = cola (f);
        const double tr = juce::jmax (trino (f, 110.0), trino (f, 440.0), trino (f, 1760.0));
        const double ps = preset (f);
        const double ca = caracter (f);
        std::printf ("%-4s %9.1f %8.0f %8.1f %8.1f %9.1f %7.1f %9.1f %7.2f %7.1f %8.1f%s\n",
                     kMandos[f].nombre, pl.dB, pl.hz, m1, m2, en, nv, co.finalDb, tr, ps, ca, co.nan ? "  NaN" : "");
        const char* n = kMandos[f].nombre;
        //  sampling#: SMP pliega por oficio como BIT (el aliasing del sampler ES su sonido) y STP barre la nota hasta cero.
        if (f != AudioEngine::kFxBit && f != AudioEngine::kFxSmp && f != AudioEngine::kFxStp && pl.dB > kPliegueMax)
            falla ("%s pliega %.1f dB en %.0f Hz (tope %.0f)", n, pl.dB, pl.hz, kPliegueMax);
        if (m1 > kChasquidoMax) falla ("%s chasquea %.1f dB al mover el mando 1 (tope %.0f)", n, m1, kChasquidoMax);
        if (m2 > kChasquidoMax) falla ("%s chasquea %.1f dB al mover el mando 2 (tope %.0f)", n, m2, kChasquidoMax);
        if (ps > kChasquidoMax) falla ("%s chasquea %.1f dB al cambiar de preset (tope %.0f)", n, ps, kChasquidoMax);
        if (en > kChasquidoMax) falla ("%s chasquea %.1f dB al encender o apagar (tope %.0f)", n, en, kChasquidoMax);
        if (std::abs (nv) > kNivelMax) falla ("%s sale %+.1f dB encendido (tope +-%.0f)", n, nv, kNivelMax);
        //  Y LOS QUE MODELAN UN APARATO TIENEN QUE CAMBIAR CON EL NIVEL.
        //
        //  Ver `caracter`. Estos ocho son los que, con sus valores de fabrica,
        //  SON un aparato y no una cuenta lineal: el drive, el pliegue, el
        //  realce, el wah, los dos compresores y la valvula y la cinta de la
        //  familia ANALOGICO. Medido hoy, de flojo a fuerte: FLD +92.5, CIN
        //  +54.9, GLU +45.0, DRV +41.1, WAH +35.7, PRE +29.3, CMP +24.1 y
        //  EXC +8.4. Un cero ahi querria decir que el aparato se quedo en su
        //  formula -que suena igual flojo que fuerte y solo mas alto-, que es
        //  exactamente lo que se oye como digital, y nada en el banco lo
        //  miraba. Cinco dB de tope: la mitad del mas flojo de los ocho.
        //
        //  Los demas dan cero A PROPOSITO y no se les pide nada: un filtro, un
        //  EQ, un retardo o un desplazador SON lineales, y CLP, DLY y REV lo
        //  son con sus valores de fabrica -el clipper no muerde hasta que se le
        //  sube el drive y el retardo trae el tipo DIGITAL-.
        {
            //  modulacion#: y los tres de la familia de modulacion, que
            //  entraron en esta lista el dia que dejaron de ser lineales: con
            //  su TIPO de fabrica -ANALOGICO- pasaron de 0.0 a +58 dB.
            static constexpr int kAparatos[] = { AudioEngine::kFxDrv, AudioEngine::kFxFld, AudioEngine::kFxExc,
                                                 AudioEngine::kFxWah, AudioEngine::kFxCmp, AudioEngine::kFxGlu,
                                                 AudioEngine::kFxPre, AudioEngine::kFxCin,
                                                 AudioEngine::kFxCho, AudioEngine::kFxFla, AudioEngine::kFxPha };
            for (int a : kAparatos)
                if (f == a && ca < 5.0)
                    falla ("%s no cambia con el nivel: %+.1f dB de mugre de flojo a fuerte (tope 5)", n, ca);
        }
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
        //  ===============================================================
        //  muelle# · UN MUELLE TIENE QUE SONAR A MUELLE, Y ESO SE MIDE
        //  ===============================================================
        //
        //  «El spring reverb me deja mucho que desear.» Un tanque de muelles de
        //  verdad hace tres cosas que ninguna otra reverb hace, y las tres se
        //  pueden medir en su respuesta al impulso:
        //
        //    1. DISPERSION. El muelle es un medio dispersivo: los agudos viajan
        //       mas despacio que los graves, asi que un golpe sale como un
        //       «boing» que BAJA de tono. Se mide con el centro de gravedad
        //       temporal de dos bandas: 3 kHz tiene que llegar MAS TARDE que
        //       300 Hz, y por un margen que se oiga -diez milisegundos no se
        //       oyen como boing, treinta si-.
        //    2. BANDA. Un tanque no pasa graves ni agudos: ni 60 Hz ni 8 kHz
        //       estan ahi. Una reverb de banda ancha con allpass suena a placa,
        //       no a muelle, y eso es exactamente la queja.
        //    3. REBOTES. La onda va y vuelve por el muelle, asi que la energia
        //       vuelve a aparecer cada vuelta del lazo (unos 37-43 ms).
        //
        //  Las tres son del TIPO MUELLE y no de la reverb en general: la sala
        //  tiene que seguir siendo de banda ancha y sin boing, asi que esto se
        //  mide con el tipo puesto y se comprueba que la SALA no lo cumple.
        {
            //  Centro de gravedad temporal de una banda, en ms desde el golpe:
            //  un paso de banda de segundo orden y la media de |y| pesada por
            //  el tiempo. Mas honesto que «el primer pico», que en una cola con
            //  ruido es el primer accidente.
            //  CUANDO LLEGA una banda, no donde esta su centro de gravedad.
            //
            //  El centro de gravedad de la cola entera no mide dispersion: mide
            //  AMORTIGUACION. Lo canto el testigo en la primera corrida -la
            //  SALA dio -53.9 ms, o sea «los agudos llegan antes», que es
            //  falso: lo que pasa es que se APAGAN antes y su energia queda
            //  toda al principio-. Lo que hay que medir es la LLEGADA: el
            //  instante en que la banda alcanza el 5 % de su energia dentro de
            //  los primeros 200 ms. En una sala las dos bandas llegan juntas;
            //  en un muelle los agudos llegan decenas de ms mas tarde.
            auto llegadaMs = [] (const std::vector<float>& h, double hz)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / kSr;
                const double q = 4.0, al = std::sin (w) / (2.0 * q);
                const double b0 = al, b2 = -al, a0 = 1.0 + al, a1 = -2.0 * std::cos (w), a2 = 1.0 - al;
                const size_t n = juce::jmin (h.size(), (size_t) (0.200 * kSr));
                std::vector<double> e (n, 0.0);
                double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    const double x = h[i];
                    const double y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                    x2 = x1; x1 = x; y2 = y1; y1 = y;
                    e[i] = y * y;
                }
                //  El primer cruce del cuarto de pico de la envolvente, que es
                //  la misma regla que usa StressTest con el `Fdn` a pelo. El 5 %
                //  de la energia acumulada medía amortiguacion, no llegada.
                const double c = 1.0 - std::exp (-1.0 / (0.005 * kSr));
                std::vector<double> env (n, 0.0);
                double v2 = 0.0, pico = 0.0;
                for (size_t i = 0; i < n; ++i)
                {
                    v2 += c * (e[i] - v2);
                    env[i] = v2;
                    pico = juce::jmax (pico, v2);
                }
                for (size_t i = 0; i < n; ++i)
                    if (env[i] >= 0.25 * pico) return 1000.0 * (double) i / kSr;
                return 0.0;
            };
            //  Cuanta energia hay en una banda, en dB sobre la de 1 kHz.
            auto bandaDb = [&] (const std::vector<float>& h, double hz, double ref)
            {
                auto energia = [&] (double f)
                {
                    const double w = 2.0 * juce::MathConstants<double>::pi * f / kSr;
                    const double q = 2.0, al = std::sin (w) / (2.0 * q);
                    const double b0 = al, b2 = -al, a0 = 1.0 + al, a1 = -2.0 * std::cos (w), a2 = 1.0 - al;
                    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0, s = 0.0;
                    for (float v : h)
                    {
                        const double x = v;
                        const double y = (b0 * x + b2 * x2 - a1 * y1 - a2 * y2) / a0;
                        x2 = x1; x1 = x; y2 = y1; y1 = y;
                        s += y * y;
                    }
                    return s;
                };
                return 10.0 * std::log10 (juce::jmax (1.0e-30, energia (hz)) / juce::jmax (1.0e-30, energia (ref)));
            };

            const auto hm = respuesta (0.55f, 0.45f, (float) 3);   // MUELLE
            const auto hs = respuesta (0.55f, 0.45f, (float) 0);   // SALA, el testigo
            const double dM = llegadaMs (hm, 3000.0) - llegadaMs (hm, 300.0);
            const double dS = llegadaMs (hs, 3000.0) - llegadaMs (hs, 300.0);
            const double g60 = bandaDb (hm, 60.0, 1000.0), a8k = bandaDb (hm, 8000.0, 1000.0);
            const double s60 = bandaDb (hs, 60.0, 1000.0), s8k = bandaDb (hs, 8000.0, 1000.0);
            std::printf ("\nMUELLE  60 Hz %+.1f dB (sala %+.1f)   8 kHz %+.1f dB (sala %+.1f)   [dispersion: ver StressTest]\n",
                         g60, s60, a8k, s8k);
            juce::ignoreUnused (dM, dS);
            //  LA DISPERSION NO SE PUEDE MEDIR AQUI, y decirlo es la mitad de la
            //  prueba: esto mide la salida del MASTER, donde el envio deja el
            //  SECO del pad -un impulso de banda ancha y sin dispersion- y el
            //  cuarto de pico de las dos bandas cae en ese impulso. Las dos
            //  bandas salian con -0.1 ms de diferencia con el tanque ya
            //  disperso. La dispersion la mide `StressTest` sobre el `Fdn` a
            //  pelo, sin seco; aqui se mide la BANDA, que el seco no tapa
            //  porque es la energia de toda la cola.
            if (g60 > -12.0) falla ("MUELLE: 60 Hz a %+.1f dB de 1 kHz (tope -12): un tanque no pasa graves", g60);
            if (a8k > -12.0) falla ("MUELLE: 8 kHz a %+.1f dB de 1 kHz (tope -12): un tanque no pasa agudos", a8k);
            //  El testigo se juzga por la DIFERENCIA: ver StressTest, donde la
            //  misma regla sobre el `Fdn` a pelo explica por que una red densa
            //  mide negativo (los graves tardan en formarse, no en llegar).

        }

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
    if (! soloCaso && (solo < 0 || solo == AudioEngine::kFxExc))
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
    //  analogico#: que cada uno de la septima familia haga lo que dice.
    if (! soloCaso && (solo < 0 || solo >= AudioEngine::kFxGeq))
    {
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
        auto db = [] (double a, double b) { return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b)); };
        std::vector<float> y;
        auto seco = [&] (double hz, float amp) { auto e = monta (-1, seno (hz, amp), 0.0f); e->postNoteOn (0, 1.0f); corre (*e, 48000, y); return banda (y, hz); };
        auto con = [&] (int f, double hz, float amp, const std::function<void (AudioEngine&)>& pon)
        { auto e = monta (f, seno (hz, amp), 1.0f); pon (*e); e->postNoteOn (0, 1.0f); corre (*e, 48000, y); };

        //  GEQ: el fader de 800 Hz a +12 sube 800 Hz unos 12 dB y deja quieto 6.4 kHz; a -12 lo baja.
        const double s800 = seco (800.0, 0.1f), s6k = seco (6400.0, 0.1f);
        con (AudioEngine::kFxGeq, 800.0, 0.1f, [] (AudioEngine& e) { e.setGeqBanda (kCanal, 3, 12.0f); });
        const double gSube = db (banda (y, 800.0), s800);
        con (AudioEngine::kFxGeq, 6400.0, 0.1f, [] (AudioEngine& e) { e.setGeqBanda (kCanal, 3, 12.0f); });
        const double gVecina = db (banda (y, 6400.0), s6k);
        con (AudioEngine::kFxGeq, 800.0, 0.1f, [] (AudioEngine& e) { e.setGeqBanda (kCanal, 3, -12.0f); });
        const double gBaja = db (banda (y, 800.0), s800);
        std::printf ("\nGEQ  fader de 800 Hz a +12: 800 Hz %+.1f dB, 6.4 kHz %+.1f dB; a -12: %+.1f dB\n", gSube, gVecina, gBaja);
        if (std::abs (gSube - 12.0) > 1.0 || std::abs (gVecina) > 0.5 || std::abs (gBaja + 12.0) > 1.0)
            falla ("GEQ: el fader de 800 Hz no sube y baja su banda 12 dB sin tocar la de 6.4 kHz");

        //  PRE: con calor la valvula da mas segundo armonico que tercero, y el calor es quien lo sube.
        const double s220 = seco (220.0, 0.3f);
        auto pre = [&] (float calor)
        {
            con (AudioEngine::kFxPre, 220.0, 0.3f, [calor] (AudioEngine& e)
                 { e.setFxParam (kCanal, AudioEngine::kFxPre, 0, 0.7f); e.setFxParam (kCanal, AudioEngine::kFxPre, 1, calor); });
            const double f0 = banda (y, 220.0);
            return std::array<double, 3> { db (f0, s220), db (banda (y, 440.0), f0), db (banda (y, 660.0), f0) };
        };
        const auto pc = pre (0.8f), pf = pre (0.0f);
        std::printf ("PRE  drive 0.7, calor 0.8: nota %+.1f dB, 2o %.1f dBc, 3o %.1f dBc; calor 0: 2o %.1f dBc\n", pc[0], pc[1], pc[2], pf[1]);
        if (pc[1] < -40.0 || pc[1] < pc[2] || pc[1] < pf[1] + 6.0 || std::abs (pc[0]) > 3.0)
            falla ("PRE: la valvula no da mas pares que impares, o el calor no los sube, o cambia el nivel");

        //  CIN: doce dB mas a la entrada salen entre 6 y 11 mas (comprime, no aplasta) y el tercero se queda por debajo de -20 dBc.
        auto cin = [&] (float amp)
        {
            con (AudioEngine::kFxCin, 220.0, amp, [] (AudioEngine& e) { e.setFxParam (kCanal, AudioEngine::kFxCin, 0, 0.8f); });
            const double f0 = banda (y, 220.0);
            return std::pair<double, double> { f0, db (banda (y, 660.0), f0) };
        };
        const auto c1 = cin (0.1f), c4 = cin (0.4f);
        const double sube = db (c4.first, c1.first);
        std::printf ("CIN  satura 0.8, +12 dB a la entrada: %+.1f dB a la salida; 3o %.1f dBc\n", sube, c4.second);
        if (sube < 6.0 || sube > 11.0 || c4.second > -20.0)
            falla ("CIN: la cinta no comprime con suavidad (+12 dB dan %+.1f) o rompe la onda (3o %.1f dBc)", sube, c4.second);
    }
    //  sampling#: que cada uno de los seis haga lo que dice.
    //  ACOPLE: NADA PUEDE CRECER SOLO.
    //
    //  Del telefono: «es un sonido pitido de acople». Con el monitor
    //  apagado Y encendido suena igual, asi que no es el microfono: algo
    //  del motor se engancha. La columna `cola` mide que la cola SE APAGA
    //  en diez segundos sin entrada, y eso NO es lo mismo: un lazo con
    //  ganancia justo por encima de uno tarda mucho mas en hacerse oir, y
    //  arranca de un golpe y no del silencio.
    //
    //  Asi que se mide el CRECIMIENTO: un golpe al principio, treinta
    //  segundos, y la energia de los dos ultimos contra la de los segundos
    //  cuatro a seis. Un efecto que decae da un numero muy negativo; uno
    //  que se engancha da positivo, y da igual lo despacio que lo haga.
    //  Cada tipo en sus DOS extremos de los dos mandos, que es donde vive
    //  la resonancia -la realimentacion del delay, el signo del flanger,
    //  la sala sin amortiguar-.
    {
        std::printf ("\nACOPLE  golpe y 60 s: lo mas alto de 40-60 s contra lo mas alto de 10-30 s\n");
        double peorTodos = -999.0; int peorF = -1;
        //  LA MAS ALTA DE SUS VENTANAS DE UN SEGUNDO, y no la energia del
        //  tramo entero. Dos arreglos en una cuenta, los dos cantados por
        //  la salida anterior:
        //
        //   · UN TREMOLO NO CRECE. Comparando dos tramos de dos segundos,
        //     TRM salia +40.3 dB: su LFO mas lento da una vuelta cada
        //     veinte segundos, asi que los dos tramos caian en partes
        //     distintas del vaiven. El maximo de las ventanas de un segundo
        //     de un tramo LARGO no se entera de la fase.
        //   · Y EL SILENCIO NO CRECE. Con los dos tramos callados, las dos
        //     energias dan el suelo del `jmax` y la resta sale +0.0 dB
        //     EXACTO: asi salieron «creciendo» un filtro, un EQ y un
        //     ecualizador grafico, que no tienen de donde. Por eso debajo
        //     hay una puerta: si lo ultimo no se oye, no se juzga.
        auto energia = [] (const std::vector<float>& y, double t0, double t1)
        {
            double peor = -300.0;
            for (double t = t0; t + 1.0 <= t1; t += 0.5)
            {
                double e2 = 0.0; size_t n = 0;
                for (size_t i = (size_t) (t * kSr); i < (size_t) ((t + 1.0) * kSr) && i < y.size(); ++i, ++n)
                    e2 += (double) y[i] * y[i];
                peor = juce::jmax (peor, 10.0 * std::log10 (juce::jmax (1.0e-30, e2 / juce::jmax ((size_t) 1, n))));
            }
            return peor;
        };
        //  El suelo de lo que se oye: por debajo de -80 dB la cola ya se
        //  fue y no hay nada que juzgar.
        constexpr double kOido = -80.0;
        //  Y LA SEGUNDA MITAD CONTRA LA PRIMERA, no el final contra el
        //  principio. Un resonador que se CARGA tambien sube: un delay con la
        //  realimentacion en 0.95 y un segundo de retardo tarda unas veinte
        //  vueltas en llegar a su peine, o sea medio minuto de subida que no es
        //  una inestabilidad -medido: DLY daba +3.5 dB, REP +4.8 y ROT +1.6-.
        //  Lo que separa las dos cosas es si SIGUE subiendo cuando ya tendria
        //  que haberse asentado, asi que la tirada dura un minuto y se compara
        //  50-60 s con 20-30 s.
        constexpr double kLargo = 60.0;
        //  Y LAS DOS VENTANAS SON DE VEINTE SEGUNDOS, que es UN PERIODO entero
        //  del modulador mas lento de la casa: ROT, DUC y STP bajan a 0.05 Hz,
        //  o sea una vuelta cada veinte segundos. Con ventanas de diez, los dos
        //  tramos caian en fases distintas del vaiven y los tres salian
        //  «creciendo» -ROT +2.5 dB, DUC +2.5, STP +1.5- con la cola plana.
        //  Es el mismo sesgo que ya se cazo en TRM, y la cura es la misma:
        //  medir un periodo entero y no un trozo.
        constexpr double kVentana = 20.0;
        for (int f = 0; f < AudioEngine::kNumFx; ++f)
        {
            if (solo >= 0 && f != solo) continue;   // para perseguir una cifra sin correr los 39
            const auto& m = kMandos[f];
            double peor = -999.0;
            for (int caso = 0; caso < 4; ++caso)
            {
                const int par = caso < 2 ? 0 : 1;
                const float lo = par == 0 ? m.lo0 : m.lo1, hi = par == 0 ? m.hi0 : m.hi1;
                auto e = montaUnGolpe (golpe (220.0, 0.5f));
                e->setCanalSend (kCanal, f, 1.0f);
                e->setFxParam (kCanal, f, 2, m.onMix);
                e->setFxParam (kCanal, f, par, (caso % 2) == 0 ? lo : hi);
                e->postNoteOn (0, 1.0f);
                std::vector<float> y;
                corre (*e, (int) (kSr * kLargo), y);
                const double fin = energia (y, kLargo - kVentana, kLargo);
                if (fin > kOido) peor = juce::jmax (peor, fin - energia (y, 10.0, 10.0 + kVentana));
            }
            //  Y LOS TIPOS, que es donde estaba el agujero.
            //
            //  El barrido de arriba mueve p0 y p1. El TIPO vive en otro
            //  mando -p4 en REV, DRV y TRM, p5 en DLY-, asi que el MUELLE
            //  de la reverb no se habia medido NUNCA contra el crecimiento:
            //  es un tanque con ocho allpass, su banda y un transductor
            //  DENTRO del lazo, o sea el sitio de esta casa donde una
            //  ganancia de lazo por encima de uno es mas facil de escribir.
            //  Del telefono: «tiene que ser algo del spring REVERB o asi,
            //  suena a efecto porque oscila».
            {
                const int parTipo = f == AudioEngine::kFxDly ? 5
                                  : (f == AudioEngine::kFxRev || f == AudioEngine::kFxDrv
                                     || f == AudioEngine::kFxTrm || f == AudioEngine::kFxFlt) ? 4 : -1;
                const int nTipos = f == AudioEngine::kFxRev ? 5 : f == AudioEngine::kFxDrv ? 4
                                 : f == AudioEngine::kFxDly ? 3 : f == AudioEngine::kFxTrm ? 4 : 2;
                if (parTipo >= 0)
                    for (int t = 0; t < nTipos; ++t)
                        for (int caso = 0; caso < 4; ++caso)
                        {
                            auto e = montaUnGolpe (golpe (220.0, 0.5f));
                            e->setCanalSend (kCanal, f, 1.0f);
                            e->setFxParam (kCanal, f, 2, m.onMix);
                            e->setFxParam (kCanal, f, parTipo, (float) t);
                            e->setFxParam (kCanal, f, 0, (caso & 1) ? m.hi0 : m.lo0);
                            e->setFxParam (kCanal, f, 1, (caso & 2) ? m.hi1 : m.lo1);
                            e->postNoteOn (0, 1.0f);
                            std::vector<float> y;
                            corre (*e, (int) (kSr * kLargo), y);
                            const double fin = energia (y, kLargo - kVentana, kLargo);
                            if (fin <= kOido) continue;          // se apago: nada que juzgar
                            const double cr = fin - energia (y, 10.0, 10.0 + kVentana);
                            if (cr > -6.0)
                                std::printf ("  %-4s tipo %d caso %d  %+7.1f dB  (final %.0f dB)\n",
                                             m.nombre, t, caso, cr, fin);
                            if (f != AudioEngine::kFxFrz && cr > 1.0)
                                falla ("%s tipo %d SIGUE CRECIENDO al minuto: %+.1f dB de la primera mitad a la segunda", m.nombre, t, cr);
                            peor = juce::jmax (peor, cr);
                        }
            }
            if (peor > peorTodos) { peorTodos = peor; peorF = f; }
            if (peor > -6.0 && peor > -900.0)
                std::printf ("  %-4s %+7.1f dB\n", m.nombre, peor);
            //  FRZ es un CONGELADOR: sostener es lo que hace, y por eso su
            //  `cola` tambien esta exenta desde que se escribio.
            if (f != AudioEngine::kFxFrz && peor > 1.0)
                falla ("%s SIGUE CRECIENDO al minuto: %+.1f dB de la primera mitad a la segunda", m.nombre, peor);
        }
        std::printf ("  el que mas crece: %s %+.1f dB\n",
                     peorF >= 0 ? kMandos[peorF].nombre : "-", peorTodos);
    }

    //  ===================================================================
    //  PROYECTO: LA COMBINACION, que es lo que hace un proyecto y no una fila
    //  ===================================================================
    //
    //  Del telefono, con el muelle YA arreglado: «sigue pasando». La columna
    //  ACOPLE mira UN efecto en UN canal, y un proyecto combina hasta seis
    //  ranuras por canal, dieciseis canales y los envios de unos a otros. Esa
    //  combinacion no la miraba nadie, y es justo donde un lazo se arma sin que
    //  ninguna pieza por separado este mal.
    //
    //  Cuatro montajes, con un SOLO golpe y un minuto, y la misma cuenta que
    //  ACOPLE -lo mas alto de 40-60 s contra lo mas alto de 10-30 s- mas el
    //  PICO final, porque un acople se oye por LO ALTO que esta y el limitador
    //  del master lo deja pegado al techo.
    if (solo < 0)
    {
        std::printf ("\nPROYECTO  un golpe y 60 s: crecimiento y pico final\n");
        auto energia = [] (const std::vector<float>& y, double t0, double t1)
        {
            double peor = -300.0;
            for (double t = t0; t + 1.0 <= t1; t += 0.5)
            {
                double e2 = 0.0; size_t n = 0;
                for (size_t i = (size_t) (t * kSr); i < (size_t) ((t + 1.0) * kSr) && i < y.size(); ++i, ++n)
                    e2 += (double) y[i] * y[i];
                peor = juce::jmax (peor, 10.0 * std::log10 (juce::jmax (1.0e-30, e2 / juce::jmax ((size_t) 1, n))));
            }
            return peor;
        };
        struct Montaje { const char* nombre; };
        //  Los cuatro, y por que cada uno:
        //   1. LA TIRA LLENA: seis efectos con realimentacion en el MISMO canal,
        //      que es lo que cabe en una fila de ranuras.
        //   2. DOS CANALES CON DELAY, uno mandando al otro: dos peines en serie.
        //   3. UNA CADENA DE CUATRO, 1>2>3>4, cada uno con su sala: el camino
        //      mas largo que los envios permiten sin cerrar circulo.
        //   4. LOS TREINTA Y NUEVE A LA VEZ en un canal, a su mezcla de
        //      encendido, que es el peor caso que la cara permite montar.
        for (int caso = 0; caso < 4; ++caso)
        {
            auto e = montaUnGolpe (golpe (220.0, 0.5f));
            auto pon = [&] (int c, int f, float mix, int p0 = -1, float v0 = 0.0f, int p1 = -1, float v1 = 0.0f)
            {
                e->setCanalSend (c, f, 1.0f);
                e->setFxParam (c, f, 2, mix);
                if (p0 >= 0) e->setFxParam (c, f, p0, v0);
                if (p1 >= 0) e->setFxParam (c, f, p1, v1);
            };
            const char* nombre = "";
            if (caso == 0)
            {
                nombre = "la tira llena";
                pon (kCanal, AudioEngine::kFxDly, 1.0f, 0, 1000.0f, 1, 0.95f);
                pon (kCanal, AudioEngine::kFxPng, 1.0f, 0, 1000.0f, 1, 0.95f);
                pon (kCanal, AudioEngine::kFxRev, 1.0f, 0, 1.0f,    1, 0.0f);
                e->setFxParam (kCanal, AudioEngine::kFxRev, 4, 3.0f);   // MUELLE
                pon (kCanal, AudioEngine::kFxFla, 1.0f, 1, 1.0f);
                pon (kCanal, AudioEngine::kFxRep, 1.0f);
                pon (kCanal, AudioEngine::kFxAmb, 1.0f, 0, 1.0f);
            }
            else if (caso == 1)
            {
                nombre = "dos canales con delay";
                pon (kCanal, AudioEngine::kFxDly, 1.0f, 0, 1000.0f, 1, 0.95f);
                pon (5, AudioEngine::kFxDly, 1.0f, 0, 700.0f, 1, 0.95f);
                e->setCanalEnvio (kCanal, 5, 1.0f);
            }
            else if (caso == 2)
            {
                nombre = "cadena de cuatro";
                for (int c : { kCanal, 5, 9, 13 })
                {
                    pon (c, AudioEngine::kFxRev, 1.0f, 0, 1.0f, 1, 0.0f);
                    pon (c, AudioEngine::kFxDly, 1.0f, 0, 800.0f, 1, 0.9f);
                }
                e->setCanalEnvio (kCanal, 5, 1.0f);
                e->setCanalEnvio (5, 9, 1.0f);
                e->setCanalEnvio (9, 13, 1.0f);
            }
            else
            {
                nombre = "los treinta y nueve";
                for (int f = 0; f < AudioEngine::kNumFx; ++f) pon (kCanal, f, kMandos[f].onMix);
            }
            e->postNoteOn (0, 1.0f);
            std::vector<float> y;
            corre (*e, (int) (kSr * 60.0), y);
            const double fin = energia (y, 40.0, 60.0), medio = energia (y, 10.0, 30.0);
            double pico = 0.0;
            for (size_t i = (size_t) (kSr * 40.0); i < y.size(); ++i) pico = juce::jmax (pico, (double) std::abs (y[i]));
            const double picoDb = 20.0 * std::log10 (juce::jmax (1.0e-12, pico));
            std::printf ("  %-24s crecimiento %+6.1f dB   pico final %7.1f dBFS%s\n",
                         nombre, fin > kOidoProy ? fin - medio : 0.0, picoDb,
                         fin > kOidoProy ? "" : "   (se apago)");
            if (fin > kOidoProy && fin - medio > 1.0)
                falla ("el montaje «%s» SIGUE CRECIENDO al minuto: %+.1f dB", nombre, fin - medio);
            //  Y un montaje que acaba pegado al techo con una sola nota es un
            //  acople aunque ya no crezca: el limitador lo sostiene ahi.
            if (picoDb > -3.0)
                falla ("el montaje «%s» acaba pegado al techo con un solo golpe: %.1f dBFS", nombre, picoDb);
        }

        //  Y LA BISECCION: que PAREJA lo hace. Los montajes de arriba dicen que
        //  pasa; esto dice con cuales dos, que es lo que se puede evitar o
        //  arreglar. Se miran los que tienen memoria con realimentacion -el
        //  resto no puede sostener- solos y de dos en dos, en el mismo canal.
        {
            static constexpr int kLazo[] = { AudioEngine::kFxDly, AudioEngine::kFxPng, AudioEngine::kFxRev,
                                             AudioEngine::kFxAmb, AudioEngine::kFxFla, AudioEngine::kFxCho,
                                             AudioEngine::kFxRep, AudioEngine::kFxFrz, AudioEngine::kFxStp };
            constexpr int n = (int) (sizeof (kLazo) / sizeof (kLazo[0]));
            std::printf ("  parejas que acaban por encima de -12 dBFS con un solo golpe:\n");
            int cuantas = 0;
            for (int i = 0; i < n; ++i)
                for (int j = i; j < n; ++j)
                {
                    auto e = montaUnGolpe (golpe (220.0, 0.5f));
                    auto pon = [&] (int f)
                    {
                        e->setCanalSend (kCanal, f, 1.0f);
                        e->setFxParam (kCanal, f, 2, 1.0f);
                        //  Cada uno en su esquina mas resonante, que es lo que
                        //  un dedo encuentra en dos segundos.
                        if (f == AudioEngine::kFxDly || f == AudioEngine::kFxPng)
                        { e->setFxParam (kCanal, f, 0, 1000.0f); e->setFxParam (kCanal, f, 1, 0.95f); }
                        if (f == AudioEngine::kFxRev) { e->setFxParam (kCanal, f, 0, 1.0f); e->setFxParam (kCanal, f, 1, 0.0f); }
                        if (f == AudioEngine::kFxAmb) e->setFxParam (kCanal, f, 0, 1.0f);
                        if (f == AudioEngine::kFxFla) e->setFxParam (kCanal, f, 1, 1.0f);
                    };
                    pon (kLazo[i]); if (j != i) pon (kLazo[j]);
                    e->postNoteOn (0, 1.0f);
                    std::vector<float> y;
                    corre (*e, (int) (kSr * 60.0), y);
                    double pico = 0.0;
                    for (size_t k = (size_t) (kSr * 40.0); k < y.size(); ++k) pico = juce::jmax (pico, (double) std::abs (y[k]));
                    const double db = 20.0 * std::log10 (juce::jmax (1.0e-12, pico));
                    if (db > -12.0)
                    {
                        ++cuantas;
                        std::printf ("    %-4s %-4s  %7.1f dBFS\n", kMandos[kLazo[i]].nombre,
                                     j == i ? "" : kMandos[kLazo[j]].nombre, db);
                    }
                }
            if (cuantas == 0) std::printf ("    ninguna\n");
        }

        //  NI UNA PAREJA, asi que hacen falta TRES o la cadena de canales. Las
        //  dos cosas se acotan aqui: el numero de eslabones que hace falta.
        {
            static constexpr int kLazo[] = { AudioEngine::kFxDly, AudioEngine::kFxPng, AudioEngine::kFxRev,
                                             AudioEngine::kFxAmb, AudioEngine::kFxFla, AudioEngine::kFxRep };
            auto resonante = [] (AudioEngine& e, int c, int f)
            {
                e.setCanalSend (c, f, 1.0f);
                e.setFxParam (c, f, 2, 1.0f);
                if (f == AudioEngine::kFxDly || f == AudioEngine::kFxPng)
                { e.setFxParam (c, f, 0, 1000.0f); e.setFxParam (c, f, 1, 0.95f); }
                if (f == AudioEngine::kFxRev) { e.setFxParam (c, f, 0, 1.0f); e.setFxParam (c, f, 1, 0.0f); }
                if (f == AudioEngine::kFxAmb) e.setFxParam (c, f, 0, 1.0f);
                if (f == AudioEngine::kFxFla) e.setFxParam (c, f, 1, 1.0f);
            };
            auto picoFinal = [] (const std::vector<float>& y)
            {
                double pico = 0.0;
                for (size_t k = (size_t) (kSr * 40.0); k < y.size(); ++k) pico = juce::jmax (pico, (double) std::abs (y[k]));
                return 20.0 * std::log10 (juce::jmax (1.0e-12, pico));
            };
            std::printf ("  cuantos eslabones hacen falta:\n");
            for (int n = 1; n <= 6; ++n)
            {
                auto e = montaUnGolpe (golpe (220.0, 0.5f));
                for (int i = 0; i < n; ++i) resonante (*e, kCanal, kLazo[i]);
                e->postNoteOn (0, 1.0f);
                std::vector<float> y; corre (*e, (int) (kSr * 60.0), y);
                std::printf ("    %d en el mismo canal        %7.1f dBFS\n", n, picoFinal (y));
            }
            static constexpr int kCad[] = { kCanal, 5, 9, 13 };
            for (int n = 2; n <= 4; ++n)
            {
                auto e = montaUnGolpe (golpe (220.0, 0.5f));
                for (int i = 0; i < n; ++i)
                {
                    resonante (*e, kCad[i], AudioEngine::kFxRev);
                    resonante (*e, kCad[i], AudioEngine::kFxDly);
                    if (i > 0) e->setCanalEnvio (kCad[i - 1], kCad[i], 1.0f);
                }
                e->postNoteOn (0, 1.0f);
                std::vector<float> y; corre (*e, (int) (kSr * 60.0), y);
                std::printf ("    %d canales encadenados      %7.1f dBFS\n", n, picoFinal (y));
            }
        }
    }

    //  ===================================================================
    //  EL CASO DE VERDAD: el proyecto que pita, tal cual
    //  ===================================================================
    //
    //  Cuatro medidas y cuatro sesgos despues, la persona mando SU proyecto.
    //  `Tests/proyectos/caso-acople.inc` son sus siete canales con efectos,
    //  los nueve parametros de cada uno, los envios a efecto, los dos envios
    //  entre canales, el fader y el mute de cada canal, generados del .xml y
    //  no escritos a mano. Se le da UN golpe a un pad de cada canal que manda
    //  a un retorno y se mira si sostiene.
    //
    //  Esto es lo contrario de un montaje inventado: si aqui no pita, lo que
    //  pita no esta en el estado de los efectos.
    if (solo < 0)
    {
        std::printf ("\nCASO  el proyecto de la persona, un golpe y 60 s\n");
        for (int padCanal : { 1, 9 })
        {
            auto e = std::make_unique<AudioEngine>();
            e->setCalidad (gCalidad);
            e->prepareToPlay (kSr, kBs);
            e->setPolyphony (8, 2);
            e->setPadGain (0, 1.0f);
            e->setPadPitch (0, 0.0f);
            e->setPadLoop (0, false);
            e->setPadCanal (0, padCanal);
            e->publishSample (0, golpe (220.0, 0.5f));
            #include "proyectos/caso-acople.inc"
            e->postNoteOn (0, 1.0f);
            std::vector<float> y;
            corre (*e, (int) (kSr * 60.0), y);
            double pico = 0.0, picoFin = 0.0;
            for (size_t k = 0; k < y.size(); ++k) pico = juce::jmax (pico, (double) std::abs (y[k]));
            for (size_t k = (size_t) (kSr * 40.0); k < y.size(); ++k) picoFin = juce::jmax (picoFin, (double) std::abs (y[k]));
            std::printf ("  golpe en el canal %-2d   pico %7.1f dBFS   pico a los 40-60 s %7.1f dBFS%s\n",
                         padCanal + 1, 20.0 * std::log10 (juce::jmax (1.0e-12, pico)),
                         20.0 * std::log10 (juce::jmax (1.0e-12, picoFin)),
                         picoFin > 0.25 ? "   <- SOSTIENE" : "");
            if (picoFin > 0.25)
                falla ("el proyecto de la persona sostiene a %.1f dBFS un minuto despues de un solo golpe en el canal %d",
                       20.0 * std::log10 (picoFin), padCanal + 1);
        }

        //  Y LA BISECCION DENTRO DE SU PATCH. Sostiene con el golpe en el canal
        //  2, que es el que manda 0.70 al 15 -el retorno con REV en MUELLE y
        //  PHA detras, los dos a mezcla 1.00-. Se apaga una cosa cada vez y la
        //  que lo mata es la culpable: eso es lo que hay que arreglar, y no lo
        //  que yo suponga.
        {
            static const char* kQue[] = { "tal cual", "sin PHA en el 15", "sin REV en el 15",
                                          "el REV del 15 en SALA", "sin el envio 2>15",
                                          "PHA del 15 a profundidad 0" };
            for (int v = 0; v < 6; ++v)
            {
                auto e = std::make_unique<AudioEngine>();
                e->setCalidad (gCalidad);
                e->prepareToPlay (kSr, kBs);
                e->setPolyphony (8, 2);
                e->setPadGain (0, 1.0f);
                e->setPadPitch (0, 0.0f);
                e->setPadLoop (0, false);
                e->setPadCanal (0, 1);
                e->publishSample (0, golpe (220.0, 0.5f));
                #include "proyectos/caso-acople.inc"
                if (v == 1) e->setFxParam (14, AudioEngine::kFxPha, 2, 0.0f);
                if (v == 2) e->setFxParam (14, AudioEngine::kFxRev, 2, 0.0f);
                if (v == 3) e->setFxParam (14, AudioEngine::kFxRev, 4, 0.0f);
                if (v == 4) e->setCanalEnvio (1, 14, 0.0f);
                if (v == 5) e->setFxParam (14, AudioEngine::kFxPha, 1, 0.0f);
                e->postNoteOn (0, 1.0f);
                std::vector<float> y;
                corre (*e, (int) (kSr * 60.0), y);
                double picoFin = 0.0;
                for (size_t k = (size_t) (kSr * 40.0); k < y.size(); ++k) picoFin = juce::jmax (picoFin, (double) std::abs (y[k]));
                std::printf ("    %-28s %7.1f dBFS%s\n", kQue[v],
                             20.0 * std::log10 (juce::jmax (1.0e-12, picoFin)),
                             picoFin > 0.25 ? "   sostiene" : "   se apaga");
            }
        }
    }

    if (! soloCaso && (solo < 0 || solo >= AudioEngine::kFxSmp))
    {
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
        auto db = [] (double a, double b) { return 20.0 * std::log10 (juce::jmax (1.0e-12, a) / juce::jmax (1.0e-12, b)); };
        //  Lo que no es el tono: la potencia total menos la del tono.
        auto resto = [&] (const std::vector<float>& y, double hz)
        {
            double ms = 0.0;
            for (size_t i = 24000; i < y.size(); ++i) ms += (double) y[i] * y[i];
            ms /= (double) (y.size() - 24000);
            const double a = banda (y, hz);
            return std::sqrt (juce::jmax (1.0e-24, ms - a * a * 0.5));
        };
        auto pico = [] (const std::vector<float>& y) { double p = 0.0; for (size_t i = 24000; i < y.size(); ++i) p = juce::jmax (p, (double) std::abs (y[i])); return p; };
        std::vector<float> y;
        auto seco = [&] (double hz, float amp) { auto e = monta (-1, seno (hz, amp), 0.0f); e->postNoteOn (0, 1.0f); corre (*e, 48000, y); return banda (y, hz); };
        auto con = [&] (int f, double hz, float amp, const std::function<void (AudioEngine&)>& pon, int n = 48000)
        { auto e = monta (f, seno (hz, amp), 1.0f); pon (*e); e->postNoteOn (0, 1.0f); corre (*e, n, y); };
        auto par = [] (int f, int pi, float v) { return [=] (AudioEngine& e) { e.setFxParam (kCanal, f, pi, v); }; };
        auto dos = [] (int f, float a, float b) { return [=] (AudioEngine& e) { e.setFxParam (kCanal, f, 0, a); e.setFxParam (kCanal, f, 1, b); }; };
        static constexpr int S = AudioEngine::kFxSmp, C = AudioEngine::kFxClp, V = AudioEngine::kFxVin,
                      G = AudioEngine::kFxGlu, O = AudioEngine::kFxOtt, P = AudioEngine::kFxStp;

        //  SMP: a 26 kHz un tono de 15 kHz no pasa el filtro de entrada; 1 kHz pasa entero; ocho bits dejan mucho mas resto que dieciseis.
        const double s1k = seco (1000.0, 0.25f), s15k = seco (15000.0, 0.25f);
        con (S, 1000.0, 0.25f, dos (S, 26040.0f, 12.0f));
        const double smpPasa = db (banda (y, 1000.0), s1k);
        con (S, 15000.0, 0.25f, dos (S, 26040.0f, 12.0f));
        const double smpCorta = db (banda (y, 15000.0), s15k);
        //  Los bits a 48 kHz, donde no hay retencion: lo que queda de resto es solo la cuantizacion.
        con (S, 1000.0, 0.25f, dos (S, 48000.0f, 8.0f));
        const double r8 = resto (y, 1000.0);
        con (S, 1000.0, 0.25f, dos (S, 48000.0f, 16.0f));
        const double r16 = resto (y, 1000.0);
        std::printf ("\nSMP  1 kHz %+.1f dB, 15 kHz %+.1f dB, resto 8 bits sobre 16: %+.1f dB\n", smpPasa, smpCorta, db (r8, r16));
        if (std::abs (smpPasa) > 1.5 || smpCorta > -24.0 || db (r8, r16) < 20.0)
            falla ("SMP: no pasa 1 kHz entero, deja pasar 15 kHz o los bits no ensucian");

        //  CLP: con 12 dB de drive el pico no pasa de -0.3 dBFS y suena mas fuerte que el seco.
        con (C, 220.0, 0.5f, dos (C, 12.0f, 0.0f));
        const double cPico = 20.0 * std::log10 (pico (y));
        double msC = 0.0; for (size_t i = 24000; i < y.size(); ++i) msC += (double) y[i] * y[i];
        const double cSube = 10.0 * std::log10 (msC / (double) (y.size() - 24000) / (0.5 * 0.5 * 0.5));
        std::printf ("CLP  drive 12: pico %.2f dBFS, %+.1f dB sobre el seco\n", cPico, cSube);
        if (cPico > -0.29 || cSube < 3.0) falla ("CLP: el pico pasa el techo (%.2f) o no sube (%+.1f)", cPico, cSube);
        //  Y con ruido a tope y forma dura, donde el filtro del 2x repica: el techo sigue siendo techo.
        { auto e = monta (C, ruido (0.9f), 1.0f); dos (C, 18.0f, 0.0f) (*e); e->postNoteOn (0, 1.0f); corre (*e, 48000, y); }
        const double cRuido = 20.0 * std::log10 (pico (y));
        std::printf ("CLP  drive 18 forma 0 con ruido: pico %.2f dBFS\n", cRuido);
        if (cRuido > -0.29) falla ("CLP: con ruido el pico pasa el techo (%.2f dBFS)", cRuido);

        //  PITIDO: UN MOTOR QUE TOCA PLAY CON EL PROYECTO VACIO TIENE QUE CALLAR.
        //
        //  Del telefono: «hay un pitido cuando le doy al play». Un pitido es un
        //  TONO sostenido, y un proyecto sin notas no tiene de donde sacarlo:
        //  si sale, lo esta generando el motor solo -un lazo que se engancha,
        //  un filtro que se queda sonando, el clic retriggereado-. Se mide el
        //  pico y las tres rayas mas altas del espectro de diez segundos con el
        //  transporte RODANDO y nada tocado.
        {
            auto suelta = [&] (bool conFx)
            {
                auto e = std::make_unique<AudioEngine>();
                e->prepareToPlay (kSr, kBs);
                e->setPolyphony (8, 2);
                if (conFx)
                    for (int f = 0; f < AudioEngine::kNumFx; ++f)
                    {
                        e->setCanalSend (kCanal, f, 1.0f);
                        e->setFxParam (kCanal, f, 2, kMandos[f].onMix);
                    }
                e->setPlaying (true);
                std::vector<float> y;
                corre (*e, (int) (kSr * 10), y);
                double pk = 0.0;
                for (size_t i = (size_t) (kSr * 2); i < y.size(); ++i) pk = juce::jmax (pk, (double) std::abs (y[i]));
                const int orden = 15, n = 1 << orden;
                const auto pp = espectro (y.data() + (size_t) (kSr * 5), orden);
                int k1 = 1, k2 = 1, k3 = 1;
                for (int k = 2; k < n / 2; ++k)
                {
                    if (pp[(size_t) k] > pp[(size_t) k1]) { k3 = k2; k2 = k1; k1 = k; }
                    else if (pp[(size_t) k] > pp[(size_t) k2]) { k3 = k2; k2 = k; }
                    else if (pp[(size_t) k] > pp[(size_t) k3]) k3 = k;
                }
                std::printf ("PITIDO %-12s pico %8.1f dBFS   rayas %6.0f %6.0f %6.0f Hz\n",
                             conFx ? "con los 39" : "sin efectos",
                             20.0 * std::log10 (juce::jmax (1.0e-12, pk)),
                             k1 * kSr / n, k2 * kSr / n, k3 * kSr / n);
                return pk;
            };
            std::printf ("\n");
            const double pkSin = suelta (false), pkCon = suelta (true);
            //  -90 dBFS es el ruido de la cuenta; un tono audible esta 60 dB por encima.
            if (pkSin > 1.0e-4) falla ("con PLAY y el proyecto vacio el motor saca %.1f dBFS (tope -80)",
                                       20.0 * std::log10 (pkSin));
            if (pkCon > 1.0e-4) falla ("con PLAY, vacio y los 39 efectos puestos, el motor saca %.1f dBFS (tope -80)",
                                       20.0 * std::log10 (pkCon));
        }

        //  MODULACION: cuanta mugre mete el color analogico de CHO, FLA y PHA
        //  a nivel de trabajo, y que en DIGITAL no mete NINGUNA.
        //
        //  Un BBD de verdad da del orden del uno por ciento de distorsion a su
        //  nivel nominal -o sea unos -40 dB-, no un diez por ciento: el color
        //  tiene que oirse como calidez y no como un efecto roto. Y DIGITAL
        //  tiene que dar lo de siempre, que es lo que deja volver atras a quien
        //  no lo quiera.
        {
            static constexpr int kMod[] = { AudioEngine::kFxCho, AudioEngine::kFxFla, AudioEngine::kFxPha };
            std::printf ("\nMODULACION  mugre del color a 0.25 de amplitud\n");
            for (int f : kMod)
            {
                auto mugre = [&] (int tipo)
                {
                    auto e = monta (f, seno (220.0, 0.25f), kMandos[f].onMix);
                    e->setFxParam (kCanal, f, AudioEngine::kParTipoMod, (float) tipo);
                    e->postNoteOn (0, 1.0f);
                    corre (*e, 48000 + 32768, y);
                    const int orden = 15, n = 1 << orden;
                    const auto pp = espectro (y.data() + 48000, orden);
                    auto raya = [&] (double hz)
                    {
                        double s2 = 0.0;
                        const int k0 = (int) std::floor ((hz - 30.0) * n / kSr), k1 = (int) std::ceil ((hz + 30.0) * n / kSr);
                        for (int k = juce::jmax (1, k0); k <= k1 && k < n / 2; ++k) s2 += pp[(size_t) k];
                        return s2;
                    };
                    double arm = 0.0;
                    for (int k = 2; k <= 8; ++k) arm += raya (220.0 * k);
                    return dB (arm / juce::jmax (1.0e-30, raya (220.0)));
                };
                const double ana = mugre (AudioEngine::kModAnalogico), dig = mugre (AudioEngine::kModDigital);
                std::printf ("%-4s ANALOGICO %7.1f dB   DIGITAL %7.1f dB\n", kMandos[f].nombre, ana, dig);
                //  La horquilla sale de la MEDIDA y no del plan: a 0.25 de
                //  amplitud -o sea -12 dBFS, nivel de trabajo- el color da
                //  CHO -50.3, FLA -49.8 y PHA -47.2 dB, que es un tercio del
                //  uno por ciento: por debajo de lo que da un BBD de verdad en
                //  su nominal, o sea calidez y no un efecto roto. La gracia es
                //  que crece con el nivel -la columna `caracter` lo mide: de
                //  0.0 a +58- y no que se note con el coro flojo.
                if (ana > -26.0) falla ("%s ANALOGICO ensucia %.1f dB a nivel de trabajo (tope -26)", kMandos[f].nombre, ana);
                if (ana < -56.0) falla ("%s ANALOGICO no ensucia nada: %.1f dB (pide -56 o mas)", kMandos[f].nombre, ana);
                if (dig > -60.0) falla ("%s DIGITAL no esta limpio: %.1f dB (tope -60)", kMandos[f].nombre, dig);
            }
        }

        //  CALOR: LOS ARMONICOS PARES DE LAS CINCO SATURACIONES.
        //
        //  La pregunta del muelle, hecha a la otra familia que modela un
        //  aparato fisico: una valvula, un transformador o un diodo no son
        //  simetricos, asi que sacan el SEGUNDO armonico -y el cuarto- y no
        //  solo los impares. Una curva impar pura -un `tanh`, un triangulo, una
        //  rodilla- saca tercero y quinto y el par se queda en el ruido de la
        //  cuenta: eso es exactamente lo que se oye como «digital» y lo que la
        //  palabra calidez nombra. Se mide con un seno de 220 Hz: el par (440 y
        //  880) y el impar (660) contra la fundamental.
        {
            static constexpr int kSat[] = { AudioEngine::kFxDrv, AudioEngine::kFxFld, C,
                                            AudioEngine::kFxPre, AudioEngine::kFxCin };
            //  Un empuje que ya ensucia de verdad en cada uno, en sus unidades.
            static const float kEmpuje[] = { 0.75f, 0.60f, 12.0f, 0.75f, 0.75f };
            std::printf ("\nCALOR  seno de 220 Hz, el par contra el impar\n");
            for (size_t k = 0; k < std::size (kSat); ++k)
            {
                const int f = kSat[k];
                con (f, 220.0, 0.5f, par (f, 0, kEmpuje[k]));
                const double h1 = banda (y, 220.0);
                const double h2 = banda (y, 440.0), h3 = banda (y, 660.0), h4 = banda (y, 880.0);
                const double pi2 = db (h2, h3);
                std::printf ("%-4s empuje %5.2f   2do %7.1f dB   3ro %7.1f dB   4to %7.1f dB   par-impar %+6.1f dB\n",
                             kMandos[f].nombre, kEmpuje[k], db (h2, h1), db (h3, h1), db (h4, h1), pi2);
                //  Y CADA UNO TIENE QUE SEGUIR SIENDO LO QUE ES, que es lo que
                //  no estaba guardado por nada. Medido hoy: PRE -0.3 dB -una
                //  valvula: par e impar a la par-, CIN -201.8 -una cinta: solo
                //  impar, que es su fisica-, y DRV -212.9, CLP -222.9 y FLD
                //  -167.0 -dos diodos en contrafase, una rodilla y un
                //  triangulo: simetricos por construccion-.
                //
                //  DE LA VALVULA YA SE ENCARGA LA SONDA `PRE` DE ABAJO -«la
                //  valvula no da mas pares que impares»-, asi que aqui PRE solo
                //  IMPRIME: es la fila contra la que se leen las otras cuatro, y
                //  escribir su tope otra vez seria la misma regla dos veces.
                //  Lo que no estaba guardado por nada es lo de enfrente: que las
                //  otras cuatro NO se vuelvan asimetricas. Una rodilla o un
                //  triangulo que empiezan a sacar segundo armonico es un sesgo
                //  que se colo, y el banco entero seguia en verde.
                if (f == AudioEngine::kFxCin && pi2 > -20.0)
                    falla ("CIN dejo de ser simetrica: par-impar %+.1f dB (la cinta pide -20 o menos)", pi2);
                if ((f == AudioEngine::kFxDrv || f == C || f == AudioEngine::kFxFld) && pi2 > -40.0)
                    falla ("%s dejo de ser simetrico: par-impar %+.1f dB (tope -40)", kMandos[f].nombre, pi2);
            }
        }

        //  VIN: el desgaste a tope se come 12 kHz; el wow a tope ensancha la raya de 1 kHz.
        const double s12k = seco (12000.0, 0.25f);
        con (V, 12000.0, 0.25f, dos (V, 1.0f, 0.0f));
        const double vCorta = db (banda (y, 12000.0), s12k);
        con (V, 1000.0, 0.25f, dos (V, 0.0f, 0.0f));
        const double vQuieto = banda (y, 1000.0);
        con (V, 1000.0, 0.25f, dos (V, 0.0f, 1.0f), 96000);
        const double vWow = db (banda (y, 1000.0), vQuieto);
        std::printf ("VIN  desgaste 1: 12 kHz %+.1f dB; wow 1: la raya de 1 kHz %+.1f dB\n", vCorta, vWow);
        if (vCorta > -12.0 || vWow > -3.0) falla ("VIN: el desgaste no estrecha la banda o el wow no mueve la nota");

        //  GLU: umbral -20 y 4:1; doce dB mas de entrada salen entre 2 y 6 mas.
        auto glu = [&] (float amp) { con (G, 220.0, amp, [] (AudioEngine& e) { e.setFxParam (kCanal, G, 0, -20.0f); e.setFxParam (kCanal, G, 1, 4.0f); }); return banda (y, 220.0); };
        const double gluSube = db (glu (0.4f), glu (0.1f));
        std::printf ("GLU  umbral -20, 4:1, +12 dB de entrada: %+.1f dB a la salida\n", gluSube);
        if (gluSube < 2.0 || gluSube > 6.0) falla ("GLU: +12 dB de entrada dan %+.1f (entre 2 y 6)", gluSube);

        //  OTT: a tope sube lo flojo (-40 dBFS) y baja lo fuerte (-6 dBFS), cuatro dB o mas cada uno.
        const double sFlojo = seco (1000.0, 0.01f), sFuerte = seco (1000.0, 0.5f);
        con (O, 1000.0, 0.01f, par (O, 0, 1.0f));
        const double oSube = db (banda (y, 1000.0), sFlojo);
        con (O, 1000.0, 0.5f, par (O, 0, 1.0f));
        const double oBaja = db (banda (y, 1000.0), sFuerte);
        std::printf ("OTT  cantidad 1: -40 dBFS %+.1f dB, -6 dBFS %+.1f dB\n", oSube, oBaja);
        if (oSube < 4.0 || oBaja > -4.0) falla ("OTT: no sube lo flojo (%+.1f) o no baja lo fuerte (%+.1f)", oSube, oBaja);
        //  Tras un segundo a -60 dBFS entra un golpe a -6: no puede salir mas fuerte que el golpe seco.
        { auto e = monta (O, salto (1000.0, 0.001f, 0.5f), 1.0f); par (O, 0, 1.0f) (*e); e->postNoteOn (0, 1.0f); corre (*e, 96000, y); }
        const double oGolpe = 20.0 * std::log10 (pico (y) / 0.5);
        std::printf ("OTT  golpe tras silencio: pico %+.1f dB sobre el golpe seco\n", oGolpe);
        if (oGolpe > 1.0) falla ("OTT: el primer golpe tras el silencio sale %+.1f dB por encima", oGolpe);

        //  STP: libre a 1 Hz con freno 0.5, en cada vuelta hay un tramo callado y el resto suena; a freno 0 es el seco.
        auto ventanas = [&] (double& bajoFrac, double& minDb)
        {
            const int w = 480; double mx = 0.0; std::vector<double> r;
            for (size_t i = 24000; i + (size_t) w <= y.size(); i += (size_t) w)
            { double s = 0.0; for (int k = 0; k < w; ++k) s += (double) y[i + (size_t) k] * y[i + (size_t) k]; r.push_back (std::sqrt (s / w)); mx = juce::jmax (mx, r.back()); }
            int bajos = 0; double mn = 1.0e9;
            for (double v : r) { if (db (v, mx) < -20.0) ++bajos; mn = juce::jmin (mn, v); }
            bajoFrac = (double) bajos / (double) r.size(); minDb = db (mn, mx);
        };
        auto stp = [&] (float freno) { con (P, 220.0, 0.25f, [freno] (AudioEngine& e)
            { e.setFxParam (kCanal, P, 3, 0.0f); e.setFxParam (kCanal, P, 0, 1.0f); e.setFxParam (kCanal, P, 1, freno); }, 24000 + 48000 * 3); };
        double bf = 0.0, md = 0.0, bf0 = 0.0, md0 = 0.0;
        stp (0.5f); ventanas (bf, md);
        stp (0.0f); ventanas (bf0, md0);
        std::printf ("STP  freno 0.5 a 1 Hz: %.0f %% de ventanas a -20 dB, la mas baja %.1f dB; freno 0: %.0f %%\n", bf * 100.0, md, bf0 * 100.0);
        if (bf < 0.03 || bf > 0.20 || md > -30.0 || bf0 > 0.0) falla ("STP: el freno no calla el final de la vuelta (%.0f %%, %.1f dB) o a cero no es el seco", bf * 100.0, md);
    }
    std::printf (fallos == 0 ? "OK  los %d efectos, sin pliegue, sin chasquido, a nivel y con la cola limpia\n"
                             : "%d FALLA\n", fallos == 0 ? AudioEngine::kNumFx : fallos);
    return fallos == 0 ? 0 : 1;
}

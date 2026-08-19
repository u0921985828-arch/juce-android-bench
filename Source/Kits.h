#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include <array>
#include <cmath>
#include <vector>

// ============================================================================
//  Kits — los sesenta y cuatro sonidos con los que la app abre.
//
//  ABRIR UNA CAJA DE RITMOS VACIA NO ES ABRIR UNA CAJA DE RITMOS. Lo primero
//  que hace cualquiera al abrir un sampler es GOLPEAR, y si el primer golpe no
//  suena la app ya ha perdido. Cuatro bancos de dieciseis, y cada banco una
//  maquina distinta: eso convierte "cambiar de banco" en una decision musical
//  y no en pasar de pagina.
//
//    A  ACUSTICA  la bateria de toda la vida. Donde va la mano sola.
//    B  MAQUINA   la caja de ritmos de los ochenta.
//    C  TEXTURA   lo que no es un golpe: siseos, subidas, impactos, viento.
//    D  TONOS     bajos, acordes, pinchazos y colchones.
//
//  SE SINTETIZAN, NO SE EMPAQUETAN: sesenta y cuatro WAV decentes son entre
//  ocho y quince megas dentro de un APK que pesa catorce, y cada muestra
//  grabada arrastra de quien es. Generarlos cuesta cero bytes de instalacion y
//  son nuestros.
//
// ----------------------------------------------------------------------------
//  LA PRIMERA VERSION SONABA MAL, Y SONABA MAL POR TRES COSAS CONCRETAS.
//
//  1. ALIAS. El metal eran seis ONDAS CUADRADAS crudas (sin (x) >= 0 ? 1 : -1)
//     y el diente de sierra era 2*frac(fase)-1, tambien crudo. Un flanco
//     vertical tiene armonicos hasta el infinito y a 44.1 kHz todo lo que pasa
//     de 22050 se DOBLA hacia abajo y cae donde le da la gana - inarmonico,
//     metalico, sucio. Eso no es "sonido de caja de ritmos vintage", es un
//     fallo de muestreo. Ahora los flancos van con PolyBLEP, que corrige el
//     salto con los dos puntos de alrededor y quita la mayor parte del doblez
//     por unas pocas multiplicaciones.
//
//  2. FILTROS DE UN POLO. Seis decibelios por octava no son un charles: son
//     ruido blanco un poco tapado. Un charles, una caja o un barrido necesitan
//     una campana con resonancia, y eso son DOS polos como minimo. Ahora hay
//     un filtro de variable de estado (TPT, estable a cualquier frecuencia) con
//     paso bajo, alto y banda, y la Q es un parametro de cada sonido.
//
//  3. NORMALIZAR AL PICO. Este es el que se oye como "mucha ganancia". Un
//     charles dura 40 ms y un bombo 400: si los dos se dejan con el mismo PICO,
//     el bombo mete diez veces mas energia al oido y la mezcla se descuadra
//     sola. Y con los sesenta y cuatro a 0.89 de pico, DOS pads a la vez ya
//     estan en 0 dBFS - de ahi que sonara saturado en cuanto se tocaba algo.
//
//     Ahora se iguala por SONORIDAD: se mide la energia con la ponderacion de
//     una K sencilla (un paso alto de cabeza mas una repisa de agudos, que es
//     lo que hace la norma de medida de sonoridad) y se lleva cada sonido al
//     mismo nivel, con el pico limitado despues. El resultado es que suenan
//     igual de fuertes AL OIDO, que es lo unico que importa, y que queda
//     margen de sobra para tocar cuatro a la vez.
//
//  Y dos cosas mas que se oian sin saber que eran: el saturador estaba puesto
//  a 2.2x en los bombos - eso es distorsion, no cuerpo - y las muestras
//  empezaban con un flanco vertical, que es un click en cada golpe. Ahora la
//  saturacion es suave y donde hace falta, y todo entra con una rampa de un
//  milisegundo.
// ============================================================================
namespace Kits
{
    static constexpr int kNumBanks    = 4;
    static constexpr int kPadsPerBank = 16;
    static constexpr int kNumSounds   = kNumBanks * kPadsPerBank;   // 64
    static constexpr double kRate     = 48000.0;

    //  A CUARENTA Y OCHO Y NO A CUARENTA Y CUATRO. El aparato abre a 48 kHz en
    //  practicamente cualquier movil, asi que a 44.1 cada golpe pasaba por el
    //  remuestreador de Voice - interpolacion de Hermite sobre un transitorio,
    //  que es justo donde peor se porta. A la misma frecuencia que el
    //  dispositivo, delta vale 1 y la muestra se lee TAL CUAL.
    static constexpr float kTargetLufsish = 0.055f;   // energia ponderada objetivo
    static constexpr float kCeiling       = 0.80f;    // techo de pico: margen para tocar varios
    static constexpr float kKnee          = 0.55f;    // donde empieza a doblarse en vez de cortarse

    inline const char* bankName (int bank)
    {
        static const char* n[kNumBanks] = { "ACUSTICA", "MAQUINA", "TEXTURA", "TONOS" };
        return n[juce::jlimit (0, kNumBanks - 1, bank)];
    }

    namespace detail
    {
        //  Semilla fija por sonido: tienen que ser LOS MISMOS en cada arranque
        //  y en cada telefono, o un proyecto guardado suena distinto al abrirlo.
        struct Rng
        {
            juce::Random r;
            explicit Rng (int seed) : r (seed) {}
            float operator()() noexcept { return r.nextFloat() * 2.0f - 1.0f; }
        };

        //  FILTRO DE VARIABLE DE ESTADO, topologia TPT.
        //
        //  Dos polos, resonancia de verdad y estable hasta muy cerca de
        //  Nyquist - que es donde un biquad directo se va de las manos y donde
        //  viven los barridos de esta tabla. Da los tres tipos a la vez, que es
        //  justo lo que hace falta cuando un mismo sonido quiere banda para el
        //  cuerpo y alto para el aire.
        struct Svf
        {
            float ic1 = 0.0f, ic2 = 0.0f, g = 0.0f, k = 2.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

            void set (double hz, float q) noexcept
            {
                const double f = juce::jlimit (20.0, kRate * 0.49, hz);
                g  = (float) std::tan (juce::MathConstants<double>::pi * f / kRate);
                k  = 1.0f / juce::jmax (0.05f, q);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            //  Devuelve los tres a la vez: paso bajo, banda y alto.
            void process (float x, float& lo, float& bp, float& hi) noexcept
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                lo = v2; bp = v1; hi = x - k * v1 - v2;
                if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0.0f; lo = bp = hi = 0.0f; }
            }
            float lp (float x) noexcept { float a,b,c; process (x,a,b,c); return a; }
            float bpf (float x) noexcept { float a,b,c; process (x,a,b,c); return b; }
            float hp (float x) noexcept { float a,b,c; process (x,a,b,c); return c; }
        };

        //  POLYBLEP: la correccion de un flanco.
        //
        //  Un salto vertical entre dos muestras trae armonicos hasta el
        //  infinito, y todo lo que pasa de Nyquist vuelve doblado y cae
        //  inarmonico. Sumar este polinomio en las dos muestras que rodean el
        //  salto redondea el flanco justo lo que hace falta: quita la mayor
        //  parte del doblez por tres multiplicaciones y una rama.
        inline float polyBlep (double t, double dt) noexcept
        {
            if (t < dt)            { const double x = t / dt;        return (float) (x + x - x * x - 1.0); }
            if (t > 1.0 - dt)      { const double x = (t - 1.0) / dt; return (float) (x * x + x + x + 1.0); }
            return 0.0f;
        }

        //  Diente de sierra y cuadrada, las dos limitadas en banda.
        inline float sawBl (double phase01, double inc) noexcept
        {
            return (float) (2.0 * phase01 - 1.0) - polyBlep (phase01, inc);
        }
        inline float sqrBl (double phase01, double inc) noexcept
        {
            float s = (phase01 < 0.5) ? 1.0f : -1.0f;
            s += polyBlep (phase01, inc);
            const double h = phase01 + 0.5 >= 1.0 ? phase01 - 0.5 : phase01 + 0.5;
            s -= polyBlep (h, inc);
            return s;
        }

        inline float env (float t, float tau) noexcept { return std::exp (-t / juce::jmax (0.0005f, tau)); }

        inline float ad (float t, float atk, float tau) noexcept
        {
            const float a = (atk <= 0.0f) ? 1.0f : juce::jmin (1.0f, t / atk);
            return a * env (juce::jmax (0.0f, t - atk), tau);
        }

        //  Saturacion SUAVE. tanh(x) con x pequeno es casi x; el problema era
        //  llamarlo con 2.2x, que ya es un distorsionador. Aqui redondea picos
        //  y no cambia el timbre.
        inline float soft (float x) noexcept
        {
            if (! std::isfinite (x)) return 0.0f;
            return x - (x * x * x) * 0.16666667f + (x * x * x * x * x) * 0.008f;
        }

        //  UN CUERPO CON MODOS, que es lo que separa un tom de un pitido.
        //
        //  Un parche no vibra a UNA frecuencia: vibra a varias que no guardan
        //  relacion entera y que se apagan a ritmos distintos - las agudas
        //  antes. Tres modos ya bastan para que el oido diga "membrana".
        struct Modes
        {
            double ph[3] {};
            static constexpr double ratio[3] = { 1.0, 1.593, 2.135 };   // modos de una membrana circular
            static constexpr float  amp[3]   = { 1.0f, 0.42f, 0.22f };
            float next (double hz, float t, float tau) noexcept
            {
                float s = 0.0f;
                for (int i = 0; i < 3; ++i)
                {
                    ph[i] += 2.0 * juce::MathConstants<double>::pi * hz * ratio[i] / kRate;
                    //  Cada modo con su propia caida: los agudos se van antes.
                    s += amp[i] * (float) std::sin (ph[i]) * env (t, tau / (1.0f + (float) i * 1.6f));
                }
                return s;
            }
        };

        //  METAL, ahora con cuadradas LIMITADAS EN BANDA. Seis frecuencias sin
        //  relacion armonica es lo que sonaba a platillo en una 808 y sigue
        //  siendolo; lo que estaba mal era como se generaban.
        //  DOS JUEGOS DE PARCIALES Y NO UNO, que es lo que separaba RIDE de
        //  CYM 808 en 0.99 de parecido: eran el mismo oscilador con otro
        //  filtro. El juego 0 son las seis frecuencias de la 808 -asi suena esa
        //  maquina y asi se queda-; el juego 1 es un platillo de verdad, que
        //  tiene mas parciales, mas arriba y peor repartidos, porque un disco
        //  de laton no vibra en seis modos sino en muchos.
        struct Metal
        {
            double ph[9] {};
            static constexpr double f808[9] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0, 0.0, 0.0, 0.0 };
            static constexpr double fLaton[9] = { 311.0, 437.7, 591.3, 728.9, 941.0, 1183.0, 1601.0, 2087.0, 2749.0 };
            int juego = 0;
            int cuantos() const noexcept { return juego == 0 ? 6 : 9; }
            float next (double mul) noexcept
            {
                const double* f = (juego == 0) ? f808 : fLaton;
                const int nf = cuantos();
                float s = 0.0f;
                for (int i = 0; i < nf; ++i)
                {
                    const double inc = f[i] * mul / kRate;
                    ph[i] += inc;
                    if (ph[i] >= 1.0) ph[i] -= 1.0;
                    s += sqrBl (ph[i], inc);
                }
                return s / (float) nf;
            }
        };
    }

//  ----------------------------------------------------------------------------
//  LA SEGUNDA VERSION: EL BANCO A Y EL BANCO B ERAN EL MISMO KIT DOS VECES.
//
//  La cabecera prometia "cada banco una maquina distinta" y la medida decia
//  otra cosa. Con un descriptor de 24 bandas de espectro mas 8 tramos de
//  envolvente -las dos mitades hacen falta: solo con espectro un charles
//  cerrado y uno abierto salen identicos, y solo con envolvente sale identico
//  cualquier par de golpes cortos- habia VEINTIOCHO pares por encima de 0.97:
//
//      SNARE / SD 808   0.991      HAT / CH 808     0.989
//      OPEN / OH 808    0.996      RIM / RIM 808    0.993
//      RIDE / CYM 808   0.991      BASS / SAW BS    0.996
//
//  No eran parecidos de familia: eran la misma receta con otro numero. Cambiar
//  de banco no era una decision musical, era pasar de pagina.
//
//  Y una segunda familia, peor: SHAKE, MARACA, TAMB, HISS, STATIC y SCRAPE
//  eran SEIS VECES el mismo generador -ruido por un filtro- con otro corte y
//  otra caida. De ahi que una maraca y un siseo midieran 0.998.
//
//  Asi que el banco A deja de compartir formas con el B. Las membranas
//  acusticas van por skin, los parches con bordon por wire, y lo que suena a
//  bolitas -maraca, shaker, pandereta- por grain. La 808 se queda con drum,
//  snare y hat tal cual, porque eso ES la 808: un seno con la afinacion
//  cayendo y ruido por un filtro. Lo que estaba mal no era la 808, era que la
//  bateria acustica fuese otra 808 con los numeros movidos.
//  ----------------------------------------------------------------------------

    enum Shape { drum, snare, hat, metal, clap, tone, chord, sweep, noiseHit, vinyl, fm,
                 grain, skin, wire };

    struct Recipe
    {
        const char* name;
        Shape shape;
        float hz;
        float decay;
        float p1;
        float p2;
        //  Cual de los dos juegos de parciales usa el metal: 0 la 808, 1 el
        //  laton. Solo lo miran metal y hat.
        int   juego;
        //  Cuanto se abre el filtro que sigue a la nota, multiplicando el de
        //  siempre. Solo lo miran tone y chord, y CERO significa "el de
        //  siempre" y no "cerrado del todo": los sesenta y cuatro renglones se
        //  escribieron sin este campo y una inicializacion de agregado deja a
        //  cero lo que no se nombra. Un valor por defecto que ademas es un
        //  valor valido es como se apagan sesenta sonidos de golpe.
        float brillo;
    };

    inline const Recipe* table()
    {
        static const Recipe t[kNumSounds] =
        {
            // --- A: ACUSTICA -------------------------------------------
            //  Membranas por skin y parches con bordon por wire: ni una sola
            //  forma compartida con el banco B, que es de donde venia que los
            //  dos bancos midieran lo mismo.
            { "KICK",   skin,      55.0f, 0.38f, 0.32f, 0.55f },
            { "SNARE",  wire,     195.0f, 0.19f, 0.30f, 1750.0f },
            { "HAT",    hat,     9500.0f, 0.042f, 1.6f, 0.0f,  1 },
            { "OPEN",   hat,     8600.0f, 0.30f, 1.4f, 0.0f,   1 },
            { "RIM",    wire,     440.0f, 0.048f, 0.58f, 2600.0f },
            { "TOM LO", skin,      95.0f, 0.36f, 0.26f, 0.34f },
            { "TOM MI", skin,     138.0f, 0.30f, 0.26f, 0.31f },
            { "TOM HI", skin,     192.0f, 0.25f, 0.26f, 0.28f },
            { "CLAP",   clap,       0.0f, 0.42f, 1.3f, 1150.0f },
            { "RIDE",   metal,      1.0f, 0.95f, 7000.0f, 1.2f, 1 },
            { "CRASH",  metal,      0.8f, 1.90f, 4200.0f, 0.9f, 1 },
            //  SHAKE y TAMB por granos: eran ruido filtrado, como HISS, y por
            //  eso una maraca y un siseo median 0.998.
            { "SHAKE",  grain,   6200.0f, 0.070f, 1400.0f, 0.0f },
            { "CONGA",  skin,     245.0f, 0.19f, 0.22f, 0.24f },
            { "COWBEL", metal,      2.4f, 0.28f, 2400.0f, 3.0f },
            { "TAMB",   grain,   5200.0f, 0.14f, 620.0f, 1.60f },
            { "SPLASH", metal,      1.3f, 1.15f, 5200.0f, 0.8f, 1 },

            // --- B: MAQUINA --------------------------------------------
            //  Y AQUI NO SE TOCA NADA: un seno con la afinacion cayendo y ruido
            //  por un filtro ES la 808. Lo que estaba mal no era este banco,
            //  era que el de al lado fuese otra vez este.
            { "BD 808", drum,      48.0f, 1.05f, 1.1f, 0.045f },
            { "SD 808", snare,    182.0f, 0.22f, 0.32f, 1300.0f },
            { "CH 808", hat,    10500.0f, 0.030f, 2.2f, 0.0f },
            { "OH 808", hat,     9800.0f, 0.36f, 2.0f, 0.0f },
            { "RIM 808",snare,    560.0f, 0.036f, 0.62f, 3400.0f },
            { "LT 808", drum,      82.0f, 0.58f, 0.6f, 0.070f },
            { "MT 808", drum,     116.0f, 0.50f, 0.6f, 0.062f },
            { "HT 808", drum,     162.0f, 0.44f, 0.6f, 0.055f },
            { "CLAP 9", clap,       0.0f, 0.13f, 3.2f, 1900.0f },
            { "CYM808", metal,      1.0f, 1.50f, 6000.0f, 0.7f },
            { "COW808", metal,      2.6f, 0.40f, 2600.0f, 3.4f },
            { "CLAVE",  tone,     2450.0f, 0.050f, 0.0f, 0.0f },
            { "MARACA", grain,   6800.0f, 0.034f, 2600.0f, 0.0f },
            { "SUB",    drum,      38.0f, 1.35f, 0.4f, 0.080f },
            { "ZAP",    sweep,   4000.0f, 0.20f, -1.0f, 3.0f },
            { "SNAP",   snare,    850.0f, 0.062f, 0.25f, 4600.0f },

            // --- C: TEXTURA --------------------------------------------
            { "VINYL",  vinyl,      0.0f, 1.60f, 0.0f, 0.0f },
            { "HISS",   hat,     5000.0f, 0.90f, 0.7f, 1.0f },
            { "RISER",  sweep,    260.0f, 1.30f, 1.0f, 2.5f },
            { "FALL",   sweep,   7000.0f, 0.85f, -1.0f, 2.0f },
            { "IMPACT", noiseHit,  62.0f, 1.05f, 0.6f, 0.0f },
            { "CLICK",  tone,    1900.0f, 0.011f, 0.0f, 0.0f },
            { "STATIC", hat,     2800.0f, 0.32f, 0.8f, 1.0f },
            { "WIND",   sweep,    700.0f, 1.80f, 0.4f, 4.0f },
            { "THUMP",  noiseHit,  46.0f, 0.52f, 0.25f, 0.0f },
            { "GLITCH", fm,       900.0f, 0.085f, 6.0f, 3.7f },
            //  SCRAPE tambien por granos, pero lentos y graves: un roce es una
            //  sucesion de enganchones, no un siseo con el filtro bajado - que
            //  es lo que era, y por eso median 0.993 el y STATIC.
            { "SCRAPE", grain,    780.0f, 0.90f, 28.0f, 0.75f },
            { "BOOM",   noiseHit,  38.0f, 1.55f, 0.75f, 0.0f },
            { "TICK",   tone,    5200.0f, 0.008f, 0.0f, 0.0f },
            { "SWELL",  sweep,    300.0f, 1.50f, 0.8f, 7.0f },
            { "CRACKL", vinyl,      0.0f, 0.80f, 1.0f, 0.0f },
            { "DRONE",  tone,      65.0f, 2.20f, 0.35f, 1.0f },

            // --- D: TONOS ----------------------------------------------
            //  BASS y SAW BS median 0.996: los dos a 55 Hz y los dos con el
            //  mismo filtro siguiendo la nota, asi que la sierra no llegaba a
            //  oirse. Ahora el bajo de sierra abre el filtro tres veces mas -
            //  para eso esta brillo - y va a su propia nota.
            { "BASS",   tone,      55.0f, 0.70f, 0.20f, 0.0f },
            { "SAW BS", tone,      73.42f, 0.62f, 1.0f, 0.0f, 0, 3.0f },
            { "SUB BS", tone,      36.71f, 0.90f, 0.0f, 1.0f },
            { "STAB",   chord,    220.0f, 0.32f, 0.0f, 0.0f },
            { "CHORD",  chord,    165.0f, 1.00f, 0.0f, 1.0f },
            { "MINOR",  chord,    147.0f, 1.00f, 1.0f, 1.0f },
            { "PAD",    chord,    110.0f, 2.20f, 0.0f, 1.0f },
            { "BELL",   fm,       660.0f, 1.35f, 3.2f, 2.01f },
            { "PLUCK",  fm,       440.0f, 0.32f, 4.0f, 1.0f },
            { "KEY",    fm,       330.0f, 0.68f, 2.2f, 3.0f },
            { "ORGAN",  tone,     220.0f, 0.90f, 0.25f, 1.0f },
            { "BRASS",  tone,     165.0f, 0.62f, 0.9f, 0.0f, 0, 1.8f },
            { "STRING", tone,     262.0f, 1.60f, 0.7f, 1.0f },
            { "FIFTH",  chord,    110.0f, 1.20f, 2.0f, 1.0f },
            { "LEAD",   fm,       523.0f, 0.48f, 1.6f, 1.0f },
            { "ARP",    tone,     392.0f, 0.26f, 0.6f, 0.0f },
        };
        return t;
    }

    inline SampleBuffer::Ptr render (int index)
    {
        using namespace detail;

        const auto& r = table()[juce::jlimit (0, kNumSounds - 1, index)];
        const int len = juce::jmax (1024, (int) (kRate * juce::jmin (3.0f, r.decay * 5.0f)));

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->sourceSampleRate = kRate;
        sb->buffer.setSize (1, len);
        float* d = sb->buffer.getWritePointer (0);

        Rng rnd (1000 + index * 37);
        Metal met;
        met.juego = juce::jlimit (0, 1, r.juego);
        //  Cero no es "cerrado del todo" sino "el de siempre": los sesenta y
        //  cuatro renglones se escribieron sin este campo. Ver Recipe.
        const float brillo = (r.brillo > 0.0f) ? r.brillo : 1.0f;
        Modes body, body2;
        Svf f1, f2, f3;
        double ph = 0.0, ph2 = 0.0, ph3 = 0.0, phs = 0.0;
        int nextClick = 0;
        float clickAmp = 0.0f;

        for (int n = 0; n < len; ++n)
        {
            const float t = (float) n / (float) kRate;
            float v = 0.0f;

            switch (r.shape)
            {
                case drum:
                {
                    //  La afinacion cae - eso ES el bombo - y el cuerpo lleva
                    //  modos, que es lo que lo separa de un pitido.
                    const double f = r.hz * (1.0 + r.p1 * std::exp (-t / r.p2));
                    ph += 2.0 * juce::MathConstants<double>::pi * f / kRate;
                    const float fund = (float) std::sin (ph) * env (t, r.decay);
                    const float mds  = body.next (r.hz * 2.6, t, r.decay * 0.5f) * 0.16f;
                    v = soft (1.15f * (fund + mds));
                    break;
                }

                case snare:
                {
                    //  Ruido por un PASO BANDA resonante mas el cuerpo con
                    //  modos. Con un solo polo esto era ruido blanco tapado.
                    f1.set (r.p2, 1.1f);
                    const float nz = f1.bpf (rnd()) * 2.4f;
                    const float bd = body.next (r.hz, t, r.decay * 0.7f);
                    v = ((1.0f - r.p1) * nz + r.p1 * bd) * env (t, r.decay);
                    break;
                }

                case hat:
                {
                    //  DOS filtros: el metal por un paso alto resonante. p1 es
                    //  la Q, o sea cuanto "sisea" contra cuanto "tintinea".
                    f1.set (r.hz, r.p1);
                    //  Y CUANTO METAL, segun el juego de parciales. Con la misma
                    //  mezcla para los dos, HAT y CH 808 median 2.9 dB de
                    //  diferencia -practicamente el mismo sonido- porque en los
                    //  dos mandaba el ruido. Un charles acustico son dos chapas
                    //  de laton: manda el metal y el ruido es solo el filo. La
                    //  808 es al reves y por eso suena a 808.
                    const float aire = (met.juego == 1) ? 0.22f : 0.55f;
                    const float src = (r.p2 > 0.5f) ? rnd()
                                                    : (aire * rnd() + (1.0f - aire) * met.next (1.0));
                    v = f1.hp (src) * 1.7f * env (t, r.decay);
                    break;
                }

                case metal:
                {
                    //  Cuadradas limitadas en banda por un paso banda ancho.
                    //  p1 = donde canta, p2 = cuanta campana tiene.
                    f1.set (r.p1, r.p2);
                    f2.set (r.p1 * 0.42, r.p2 * 0.8f);
                    const float m = met.next (r.hz);
                    v = (f1.bpf (m) * 1.4f + f2.bpf (m) * 0.8f) * env (t, r.decay);
                    break;
                }

                case clap:
                {
                    //  Cuatro manos que no llegan a la vez: tres golpes de
                    //  10 ms y una cola. Sin los tres primeros esto es un ruido
                    //  corto, no una palmada.
                    f1.set (r.p2, r.p1);
                    const float nz = f1.bpf (rnd()) * 2.2f;
                    float e = env (juce::jmax (0.0f, t - 0.026f), r.decay) * 0.85f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float dt = t - (float) k * 0.0085f;
                        if (dt >= 0.0f) e += env (dt, 0.005f);
                    }
                    v = nz * juce::jmin (2.4f, e) * 0.55f;
                    break;
                }

                case tone:
                {
                    //  p1 = cuanta sierra LIMITADA EN BANDA lleva encima del
                    //  seno; p2 = 1 lo hace sostenido en vez de percusivo. Y un
                    //  paso bajo que se cierra con la nota, que es lo que hace
                    //  que un bajo suene a bajo y no a zumbido.
                    const double inc = r.hz / kRate;
                    ph += inc; if (ph >= 1.0) ph -= 1.0;
                    const float s   = (float) std::sin (2.0 * juce::MathConstants<double>::pi * ph);
                    const float saw = sawBl (ph, inc);
                    const float mix = (1.0f - r.p1) * s + r.p1 * saw * 0.7f;
                    const float e   = (r.p2 > 0.5f) ? ad (t, 0.05f, r.decay) : env (t, r.decay);
                    f1.set (juce::jlimit (200.0, 16000.0, r.hz * brillo * (3.0 + 9.0 * (double) e)), 0.9f);
                    v = soft (1.25f * f1.lp (mix) * e);
                    break;
                }

                case chord:
                {
                    //  Tres notas, y las tres con un poco de sierra: tres senos
                    //  puros suenan a organillo de juguete.
                    const double third = (r.p1 < 0.5f) ? 1.2599 : (r.p1 < 1.5f ? 1.1892 : 1.4983);
                    const double i1 = r.hz / kRate, i2 = r.hz * third / kRate, i3 = r.hz * 1.4983 / kRate;
                    ph  += i1; if (ph  >= 1.0) ph  -= 1.0;
                    ph2 += i2; if (ph2 >= 1.0) ph2 -= 1.0;
                    ph3 += i3; if (ph3 >= 1.0) ph3 -= 1.0;
                    const float e = (r.p2 > 0.5f) ? ad (t, 0.09f, r.decay) : env (t, r.decay);
                    const float mix = 0.30f * (sawBl (ph, i1) + sawBl (ph2, i2) + sawBl (ph3, i3));
                    f1.set (juce::jlimit (300.0, 14000.0, r.hz * brillo * (4.0 + 7.0 * (double) e)), 0.8f);
                    v = soft (1.1f * f1.lp (mix) * e);
                    break;
                }

                case sweep:
                {
                    //  El filtro se mueve, y ahora es un paso banda con Q: con
                    //  un polo esto era ruido que se apagaba, no un barrido.
                    const float k = juce::jlimit (0.0f, 1.0f, t / juce::jmax (0.01f, r.decay));
                    const double hz = (r.p1 >= 0.0f) ? r.hz * std::pow (14.0, k)
                                                     : r.hz * std::pow (0.05, k);
                    f1.set (hz, r.p2);
                    const float nz = f1.bpf (rnd()) * 2.6f;
                    const float e = (r.p1 >= 0.0f) ? ad (t, r.decay * 0.75f, r.decay * 0.25f)
                                                   : env (t, r.decay);
                    v = nz * e;
                    break;
                }

                case noiseHit:
                {
                    //  Ruido ANCHO con un grave debajo: solo el ruido es un
                    //  aplauso y solo el grave es un bombo.
                    f1.set (1100.0, 0.7f);
                    const float nz = f1.lp (rnd());
                    ph += 2.0 * juce::MathConstants<double>::pi * r.hz * (1.0 + r.p1 * std::exp (-t / 0.07f)) / kRate;
                    v = soft (1.3f * (0.42f * nz + 0.85f * (float) std::sin (ph)) * env (t, r.decay));
                    break;
                }

                case vinyl:
                {
                    //  Siseo con chasquidos encima. El suelo es constante y lo
                    //  que se oye son los impulsos, que es lo que hace un disco.
                    f1.set (6500.0, 0.7f);
                    //  Y EL SUELO DE SISEO SEGUN LA DENSIDAD. Con el mismo suelo
                    //  para los dos, VINYL y CRACKL median 2.4 dB: mandaba el
                    //  siseo en los dos y los chasquidos casi no contaban. Un
                    //  disco viejo es siseo con algun chasquido; una crepitacion
                    //  son los chasquidos SIN el disco debajo.
                    float s = f1.lp (rnd()) * (r.p1 > 0.5f ? 0.006f : 0.14f);
                    if (n >= nextClick)
                    {
                        clickAmp = 0.5f + 0.5f * std::abs (rnd());
                        nextClick = n + 220 + (int) (std::abs (rnd()) * (r.p1 > 0.5f ? 950.0f : 3200.0f));
                    }
                    if (clickAmp > 0.001f) { s += clickAmp * rnd(); clickAmp *= 0.55f; }
                    v = s * ad (t, 0.008f, r.decay);
                    break;
                }

                case grain:
                {
                    //  GRANOS, que es lo que una maraca ES y un siseo no.
                    //
                    //  Una maraca no hace ruido: hace cuarenta bolitas chocando
                    //  contra la pared en cuarenta instantes distintos. Y eso no
                    //  se consigue filtrando ruido -por eso SHAKE y HISS median
                    //  0.998 de parecido, eran el mismo generador-, se consigue
                    //  disparando impulsos con el hueco entre ellos AL AZAR.
                    //  Regular seria un zumbido; irregular es un instrumento.
                    //
                    //  p1 = granos por segundo. p2 = cuanta sonaja lleva encima,
                    //  que es lo unico que separa una pandereta de un shaker: la
                    //  pandereta tiene chapas y las chapas son metal.
                    if (n >= nextClick)
                    {
                        clickAmp = 0.45f + 0.55f * std::abs (rnd());
                        const float medio = kRate / juce::jmax (20.0f, r.p1);
                        //  El hueco entre 0.35 y 1.65 veces el medio: dispersion
                        //  de sobra para que no se oiga el patron y no tanta como
                        //  para que se hagan huecos de silencio.
                        nextClick = n + juce::jmax (12, (int) (medio * (0.35f + 1.3f * std::abs (rnd()))));
                    }
                    if (clickAmp > 0.0005f)
                    {
                        f1.set (r.hz, 2.2f);
                        //  Cada grano es su propio golpecito de dos milisegundos,
                        //  no una muestra suelta: una muestra suelta es un click y
                        //  suena a error de fichero.
                        float g = f1.bpf (rnd() * clickAmp) * 3.0f;
                        clickAmp *= 0.9885f;
                        if (r.p2 > 0.01f)
                        {
                            f2.set (r.hz * 1.9, 1.4f);
                            g += f2.bpf (met.next (2.6)) * clickAmp * r.p2 * 2.2f;
                        }
                        v = g;
                    }
                    v *= env (t, r.decay);
                    break;
                }

                case skin:
                {
                    //  UN PARCHE, que no es un seno con la afinacion cayendo.
                    //
                    //  Eso ultimo es una 808 y esta bien que lo sea; el fallo era
                    //  que la bateria ACUSTICA fuese otra 808 con los numeros
                    //  movidos. Un tom de verdad son tres cosas: el golpe de la
                    //  baqueta -ruido corto y agudo, cinco milisegundos-, los
                    //  modos de la membrana, que no guardan relacion entera, y el
                    //  aire de la caja debajo.
                    //
                    //  p1 = cuanta baqueta. p2 = cuanto cae la afinacion, mucho
                    //  menos que en una maquina: un parche baja un semitono largo,
                    //  no una octava.
                    f1.set (juce::jlimit (400.0, 9000.0, (double) r.hz * 14.0), 0.9f);
                    const float palo = f1.bpf (rnd()) * r.p1 * env (t, 0.006f) * 2.6f;
                    const double caida = 1.0 + r.p2 * std::exp (-t / 0.045f);
                    const float parche = body.next (r.hz * caida, t, r.decay);
                    //  Y el aire: un segundo juego de modos una quinta por debajo
                    //  y mas corto, que es el cuerpo de la caja. Sin el, un tom
                    //  suena a timbal de juguete.
                    const float caja = body2.next (r.hz * 0.67 * caida, t, r.decay * 0.55f) * 0.30f;
                    v = soft (1.05f * (parche + caja + palo));
                    break;
                }

                case wire:
                {
                    //  UNA CAJA CON BORDON, y el bordon es la mitad que faltaba.
                    //
                    //  La caja acustica y la SD 808 median 0.991 porque las dos
                    //  eran ruido por un paso banda mas un cuerpo. La diferencia
                    //  de verdad esta en que una caja de verdad tiene MUELLES
                    //  debajo, y los muelles ni entran a la vez que el golpe ni
                    //  se apagan con el: siguen sonando despues, mas arriba y
                    //  mucho mas sucios. Dos ruidos con dos caidas distintas, no
                    //  uno.
                    //
                    //  p1 = cuanto parche contra cuanto muelle. p2 = donde canta
                    //  el parche.
                    f1.set (r.p2, 1.3f);
                    const float golpe = f1.bpf (rnd()) * 2.2f * env (t, r.decay * 0.45f);
                    f2.set (juce::jlimit (900.0, 12000.0, (double) r.p2 * 3.4), 0.8f);
                    //  El bordon entra tres milisegundos TARDE. Es poco y se oye:
                    //  es lo que hace que el golpe tenga dos partes en vez de una.
                    const float muelle = f2.hp (rnd()) * 1.8f * env (juce::jmax (0.0f, t - 0.003f), r.decay * 1.35f);
                    const float cuerpo = body.next (r.hz, t, r.decay * 0.6f) * 0.55f;
                    v = soft (1.1f * (r.p1 * cuerpo + (1.0f - r.p1) * (0.55f * golpe + 0.45f * muelle)));
                    break;
                }

                case fm:
                {
                    //  Dos operadores, y el indice CAE: un ataque brillante que
                    //  no se apaga suena a sintetizador barato.
                    ph2 += 2.0 * juce::MathConstants<double>::pi * r.hz * r.p2 / kRate;
                    const double mod = std::sin (ph2) * r.p1 * env (t, r.decay * 0.40f);
                    ph  += 2.0 * juce::MathConstants<double>::pi * r.hz / kRate;
                    v = (float) std::sin (ph + mod) * env (t, r.decay);
                    break;
                }
            }

            juce::ignoreUnused (phs);
            d[n] = std::isfinite (v) ? v : 0.0f;
        }

        // ------------------------------------------------------------------
        //  IGUALAR POR SONORIDAD, NO POR PICO, Y CON LA CURVA DE LA NORMA.
        //
        //  Al mismo PICO un charles de 40 ms y un bombo de 400 suenan a
        //  volumenes completamente distintos, porque el oido integra energia y
        //  no mira el maximo. Y con los sesenta y cuatro a 0.89 de pico, dos
        //  pads a la vez ya estaban en 0 dBFS: de ahi el "suena con mucha
        //  ganancia".
        //
        //  Se pondera con la curva K de BS.1770 - repisa de agudos de segundo
        //  orden mas paso alto de segundo orden - y los coeficientes son los
        //  publicados PARA 48 kHz, que es exactamente a lo que se genera aqui:
        //  a otra frecuencia habria que recalcularlos y dejarian de ser exactos.
        //
        //  Y se mide sobre la VENTANA DE 400 ms MAS SONORA, no sobre el fichero
        //  entero. Un vinilo de tres segundos con cuatro chasquidos sueltos
        //  tiene una energia media ridicula y cada chasquido revienta: medido
        //  entero salia "flojo" y se le subia el volumen hasta que los
        //  chasquidos pegaban. Lo que se compara es como suena EL GOLPE, que es
        //  lo que se oye al tocar el pad.
        {
            struct Biquad
            {
                double b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                double operator() (double x) noexcept
                {
                    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1; x1 = x; y2 = y1; y1 = y;
                    return std::isfinite (y) ? y : 0.0;
                }
            };
            //  Los dos de la norma, a 48 kHz.
            Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                          -1.69065929318241,  0.73248077421585 };
            Biquad hpf   { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };

            const int win = juce::jmin (len, (int) (kRate * 0.400));
            double run = 0.0, best = 0.0;
            std::vector<double> sq ((size_t) len);
            for (int n = 0; n < len; ++n)
            {
                const double k = hpf (shelf ((double) d[n]));
                sq[(size_t) n] = k * k;
                run += sq[(size_t) n];
                if (n >= win) run -= sq[(size_t) (n - win)];
                if (n >= win - 1) best = juce::jmax (best, run);
            }
            if (win >= len) best = juce::jmax (best, run);

            const float loud = (float) std::sqrt (best / juce::jmax (1, win));
            float g = (loud > 1.0e-7f) ? kTargetLufsish / loud : 1.0f;

            //  El techo, DESPUES de la sonoridad: al reves, el limitador
            //  decidiria cuanto suena cada cosa. 0.80 deja margen para tocar
            //  cuatro pads a la vez sin llegar al limitador del master, y lo
            //  que se pase se dobla en vez de cortarse.
            float peak = 0.0f;
            for (int n = 0; n < len; ++n) peak = juce::jmax (peak, std::abs (d[n]));

            const int aIn  = juce::jmin (len / 8, (int) (kRate * 0.001));
            const int aOut = juce::jmin (len / 4, (int) (kRate * 0.004));
            for (int n = 0; n < len; ++n)
            {
                float x = d[n] * g;
                //  Doblar, no cortar: un transitorio que se pasa del techo se
                //  redondea y conserva su sonoridad, en vez de arrastrar al
                //  sonido entero seis decibelios hacia abajo.
                if (std::abs (x) > kKnee)
                {
                    const float sgn = (x < 0.0f) ? -1.0f : 1.0f;
                    const float over = (std::abs (x) - kKnee) / (1.0f - kKnee);
                    x = sgn * (kKnee + (kCeiling - kKnee) * std::tanh (over));
                }
                if (n < aIn)         x *= (float) n / (float) aIn;
                if (n >= len - aOut) x *= (float) (len - n) / (float) aOut;
                d[n] = juce::jlimit (-0.99f, 0.99f, x);
            }
            juce::ignoreUnused (peak);
        }

        return sb;
    }
}

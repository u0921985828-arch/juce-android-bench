#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include <array>
#include <cmath>

// ============================================================================
//  Kits — los sesenta y cuatro sonidos con los que la app abre.
//
//  ABRIR UNA CAJA DE RITMOS VACIA NO ES ABRIR UNA CAJA DE RITMOS.
//
//  Hasta aqui Zati arrancaba con dieciseis huecos grises y nada que tocar
//  hasta que la persona fuera a buscar un fichero. Eso es un editor de
//  muestras, no un instrumento: lo primero que hace cualquiera al abrir un
//  sampler es GOLPEAR, y si el primer golpe no suena la app ya ha perdido.
//  Cuatro bancos de dieciseis dan sesenta y cuatro sonidos desde el primer
//  segundo, y cada banco es una maquina distinta - eso es lo que convierte
//  "cambiar de banco" en una decision musical y no en pasar de pagina.
//
//  Y SE SINTETIZAN, NO SE EMPAQUETAN. Sesenta y cuatro WAV decentes son entre
//  ocho y quince megas dentro del APK, que hoy pesa catorce enteros; ademas
//  cada muestra grabada arrastra de quien es, y una app que se publica no
//  puede llevar dentro un pack de procedencia dudosa. Generarlos cuesta CERO
//  bytes de instalacion, unos milisegundos de arranque, y son nuestros.
//
//  El precio esta declarado: un bombo sintetizado no es un bombo grabado. No
//  pretenden serlo. Son un punto de partida que suena bien y que invita a
//  reemplazarlo, que es exactamente lo que un sampler quiere que hagas.
//
//  Los CUATRO BANCOS, y por que estos cuatro:
//
//    A  ACUSTICA  - la bateria de toda la vida. Es donde va la mano sola.
//    B  MAQUINA   - la caja de ritmos de los ochenta: seno largo, ruido
//                   filtrado y metal de seis cuadradas. Otro genero entero.
//    C  TEXTURA   - lo que no es un golpe: chasquidos, siseos, subidas,
//                   impactos, viento. La materia prima de un ambiente.
//    D  TONOS     - bajos, acordes, pinchazos y colchones. Sin esto no se
//                   puede escribir una cancion, solo un ritmo.
//
//  Los NOMBRES no pasan por T(). KICK, SNARE, HAT y CLAP se llaman igual en
//  un estudio de Madrid, de Shanghai y de El Cairo: son la jerga del oficio,
//  como FLT o DLY. Traducirlos seria dificultar la lectura, no facilitarla.
// ============================================================================
namespace Kits
{
    static constexpr int kNumBanks    = 4;
    static constexpr int kPadsPerBank = 16;
    static constexpr int kNumSounds   = kNumBanks * kPadsPerBank;   // 64
    static constexpr double kRate     = 44100.0;

    inline const char* bankName (int bank)
    {
        static const char* n[kNumBanks] = { "ACUSTICA", "MAQUINA", "TEXTURA", "TONOS" };
        return n[juce::jlimit (0, kNumBanks - 1, bank)];
    }

    // ------------------------------------------------------------------
    //  Las piezas con las que se hacen. Nada de esto es sofisticado: un
    //  generador, dos filtros de un polo, una envolvente y un saturador
    //  bastan para sesenta y cuatro sonidos que se distinguen entre si, y
    //  lo que los distingue es como se combinan, no lo caro que sea cada uno.
    // ------------------------------------------------------------------
    namespace detail
    {
        struct Rng
        {
            //  Semilla fija: los sonidos tienen que ser LOS MISMOS en cada
            //  arranque y en cada telefono. Un ruido distinto cada vez
            //  significa que un proyecto guardado suena distinto al abrirlo.
            juce::Random r;
            explicit Rng (int seed) : r (seed) {}
            float operator()() noexcept { return r.nextFloat() * 2.0f - 1.0f; }
        };

        //  Un polo. El paso bajo es el que da el cuerpo y el paso alto es el
        //  que quita el barro; con los dos en serie sale una banda, que es de
        //  donde salen todas las cajas y todos los charles.
        struct OnePole
        {
            float z = 0.0f, a = 0.0f;
            void setLp (double hz) noexcept
            { a = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * (float) (hz / kRate)); }
            float lp (float x) noexcept { z += a * (x - z); return z; }
            float hp (float x) noexcept { z += a * (x - z); return x - z; }
        };

        //  Envolvente exponencial. tau en segundos; a los 5 tau ya es silencio.
        inline float env (float t, float tau) noexcept { return std::exp (-t / juce::jmax (0.0005f, tau)); }

        //  Ataque + caida, para lo que no empieza de golpe.
        inline float ad (float t, float atk, float tau) noexcept
        {
            const float a = (atk <= 0.0f) ? 1.0f : juce::jmin (1.0f, t / atk);
            return a * env (juce::jmax (0.0f, t - atk), tau);
        }

        //  El saturador de siempre, y aqui hace falta de verdad: un seno con
        //  la envolvente muy corta pega un pico que sin doblar suena a click.
        inline float sat (float x) noexcept
        {
            if (! std::isfinite (x)) return 0.0f;
            return std::tanh (x);
        }

        //  METAL: seis cuadradas a frecuencias que no guardan relacion
        //  armonica. Es como sonaba el platillo de una 808 y sigue siendo la
        //  unica forma barata de hacer algo que suene a metal y no a ruido.
        struct Metal
        {
            double ph[6] {};
            static constexpr double f[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            float next (double baseMul, double rate) noexcept
            {
                float s = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    ph[i] += 2.0 * juce::MathConstants<double>::pi * f[i] * baseMul / rate;
                    s += (std::sin (ph[i]) >= 0.0 ? 1.0f : -1.0f);
                }
                return s / 6.0f;
            }
        };
    }

    // ------------------------------------------------------------------
    //  La receta de un sonido. Todo lo que hace falta para generarlo cabe
    //  en una fila, y las sesenta y cuatro filas caben en una pantalla:
    //  asi se ve el kit ENTERO de un vistazo y se nota si falta un charles.
    // ------------------------------------------------------------------
    enum Shape
    {
        drum,       // seno con la afinacion cayendo: bombo y tom
        snare,      // ruido en banda + cuerpo afinado
        hat,        // ruido muy filtrado, corto o largo
        metal,      // las seis cuadradas: platillo, campana, cencerro
        clap,       // cuatro golpes de ruido muy juntos y una cola
        tone,       // nota con armonicos: bajo, pinchazo, cuerda
        chord,      // tres notas a la vez
        sweep,      // ruido con el filtro barriendo: subidas y bajadas
        noiseHit,   // impacto: ruido ancho con cuerpo grave
        vinyl,      // chasquidos aleatorios sobre siseo
        fm          // dos operadores: campana, cristal, mordente
    };

    struct Recipe
    {
        const char* name;
        Shape shape;
        float hz;        // frecuencia base
        float decay;     // segundos
        float p1;        // brillo, indice de FM, o cuanto cae la afinacion
        float p2;        // segundo parametro segun la forma
    };

    //  LOS SESENTA Y CUATRO. Cuatro filas de cuatro por banco, en el orden en
    //  el que la rejilla los ensena: el 01 abajo a la izquierda.
    inline const Recipe* table()
    {
        static const Recipe t[kNumSounds] =
        {
            // --- A: ACUSTICA -------------------------------------------
            { "KICK",   drum,      52.0f, 0.42f, 2.4f, 0.030f },
            { "SNARE",  snare,    190.0f, 0.20f, 0.55f, 1900.0f },
            { "HAT",    hat,     8000.0f, 0.045f, 0.0f, 0.0f },
            { "OPEN",   hat,     7200.0f, 0.34f, 0.0f, 0.0f },
            { "RIM",    snare,    420.0f, 0.055f, 0.30f, 3200.0f },
            { "TOM LO", drum,     92.0f, 0.34f, 0.9f, 0.055f },
            { "TOM MI", drum,    132.0f, 0.28f, 0.9f, 0.050f },
            { "TOM HI", drum,    186.0f, 0.24f, 0.9f, 0.045f },
            { "CLAP",   clap,       0.0f, 0.26f, 0.0f, 1500.0f },
            { "RIDE",   metal,      1.0f, 0.90f, 0.35f, 0.0f },
            { "CRASH",  metal,      0.8f, 1.70f, 0.70f, 0.0f },
            { "SHAKE",  hat,     6200.0f, 0.075f, 0.0f, 1.0f },
            { "CONGA",  drum,    240.0f, 0.20f, 0.6f, 0.040f },
            { "COWBEL", metal,     2.4f, 0.30f, 0.10f, 0.0f },
            { "TAMB",   hat,     9000.0f, 0.16f, 0.0f, 1.0f },
            { "SPLASH", metal,     1.3f, 1.10f, 0.60f, 0.0f },

            // --- B: MAQUINA (la caja de ritmos de los ochenta) ----------
            { "BD 808", drum,      45.0f, 1.10f, 0.9f, 0.055f },
            { "SD 808", snare,    180.0f, 0.24f, 0.40f, 1400.0f },
            { "CH 808", hat,     9500.0f, 0.032f, 0.0f, 0.0f },
            { "OH 808", hat,     9000.0f, 0.40f, 0.0f, 0.0f },
            { "RIM 808",snare,    550.0f, 0.040f, 0.15f, 4000.0f },
            { "LT 808", drum,      78.0f, 0.60f, 0.5f, 0.080f },
            { "MT 808", drum,     110.0f, 0.52f, 0.5f, 0.070f },
            { "HT 808", drum,     155.0f, 0.46f, 0.5f, 0.060f },
            { "CLAP 9", clap,       0.0f, 0.34f, 0.0f, 1100.0f },
            { "CYM808", metal,      1.0f, 1.40f, 0.85f, 0.0f },
            { "COW808", metal,      2.6f, 0.42f, 0.05f, 0.0f },
            { "CLAVE",  tone,     2400.0f, 0.055f, 0.0f, 0.0f },
            { "MARACA", hat,    11000.0f, 0.038f, 0.0f, 1.0f },
            { "SUB",    drum,      36.0f, 1.40f, 0.35f, 0.090f },
            { "ZAP",    sweep,   3000.0f, 0.22f, -1.0f, 0.0f },
            { "SNAP",   snare,    800.0f, 0.070f, 0.20f, 5000.0f },

            // --- C: TEXTURA --------------------------------------------
            { "VINYL",  vinyl,      0.0f, 1.60f, 0.0f, 0.0f },
            { "HISS",   hat,     4000.0f, 0.90f, 0.0f, 1.0f },
            { "RISER",  sweep,    300.0f, 1.20f, 1.0f, 0.0f },
            { "FALL",   sweep,   6000.0f, 0.90f, -1.0f, 0.0f },
            { "IMPACT", noiseHit,  70.0f, 1.10f, 0.5f, 0.0f },
            { "CLICK",  tone,    1800.0f, 0.012f, 0.0f, 0.0f },
            { "STATIC", hat,     2500.0f, 0.35f, 0.0f, 1.0f },
            { "WIND",   sweep,    800.0f, 1.80f, 0.4f, 1.0f },
            { "THUMP",  noiseHit,  48.0f, 0.55f, 0.2f, 0.0f },
            { "GLITCH", fm,       900.0f, 0.090f, 7.0f, 3.7f },
            { "SCRAPE", hat,     1400.0f, 0.30f, 0.0f, 1.0f },
            { "BOOM",   noiseHit,  40.0f, 1.60f, 0.7f, 0.0f },
            { "TICK",   tone,    5000.0f, 0.008f, 0.0f, 0.0f },
            { "SWELL",  sweep,    500.0f, 1.50f, 0.8f, 1.0f },
            { "CRACKL", vinyl,      0.0f, 0.80f, 1.0f, 0.0f },
            { "DRONE",  tone,      65.0f, 2.20f, 0.0f, 1.0f },

            // --- D: TONOS ----------------------------------------------
            { "BASS",   tone,      55.0f, 0.70f, 0.0f, 0.0f },
            { "SAW BS", tone,      55.0f, 0.60f, 1.0f, 0.0f },
            { "SUB BS", tone,      41.0f, 0.90f, 0.0f, 1.0f },
            { "STAB",   chord,    220.0f, 0.34f, 0.0f, 0.0f },
            { "CHORD",  chord,    165.0f, 1.00f, 0.0f, 1.0f },
            { "MINOR",  chord,    147.0f, 1.00f, 1.0f, 1.0f },
            { "PAD",    chord,    110.0f, 2.20f, 0.0f, 1.0f },
            { "BELL",   fm,       660.0f, 1.30f, 3.0f, 2.01f },
            { "PLUCK",  fm,       440.0f, 0.34f, 4.0f, 1.0f },
            { "KEY",    fm,       330.0f, 0.70f, 2.0f, 3.0f },
            { "ORGAN",  tone,     220.0f, 0.90f, 0.5f, 1.0f },
            { "BRASS",  tone,     165.0f, 0.65f, 1.0f, 0.0f },
            { "STRING", tone,     262.0f, 1.60f, 0.3f, 1.0f },
            { "FIFTH",  chord,    110.0f, 1.20f, 2.0f, 1.0f },
            { "LEAD",   fm,       523.0f, 0.50f, 1.5f, 1.0f },
            { "ARP",    tone,     392.0f, 0.28f, 0.7f, 0.0f },
        };
        return t;
    }

    // ------------------------------------------------------------------
    //  Y el generador. Un sonido, mono, a 44.1 kHz.
    // ------------------------------------------------------------------
    inline SampleBuffer::Ptr render (int index)
    {
        using namespace detail;

        const auto& r = table()[juce::jlimit (0, kNumSounds - 1, index)];

        //  Cinco constantes de tiempo y ya no queda nada audible; con un
        //  minimo, porque una muestra de cuatro datos no la puede leer nadie
        //  (la interpolacion de Voice necesita cuatro puntos).
        const int len = juce::jmax (1024, (int) (kRate * juce::jmin (3.0f, r.decay * 5.0f)));

        SampleBuffer::Ptr sb = new SampleBuffer();
        sb->sourceSampleRate = kRate;
        sb->buffer.setSize (1, len);
        float* d = sb->buffer.getWritePointer (0);

        //  La semilla sale del INDICE, no del reloj: el mismo pad suena igual
        //  en cada arranque y en cada telefono.
        Rng rnd (1000 + index * 37);
        Metal met;
        OnePole f1, f2, f3;
        double ph = 0.0, ph2 = 0.0, ph3 = 0.0;

        //  Chasquidos del vinilo: se decide DONDE estan antes de empezar, para
        //  que no dependan del orden en que se pidan los numeros.
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
                    //  La afinacion cae, y eso ES el bombo: un seno a
                    //  frecuencia fija es un pitido. p1 dice cuanto sube al
                    //  principio y p2 lo rapido que vuelve.
                    const double f = r.hz * (1.0 + r.p1 * std::exp (-t / r.p2));
                    ph += 2.0 * juce::MathConstants<double>::pi * f / kRate;
                    v = sat (2.2f * (float) std::sin (ph) * env (t, r.decay));
                    break;
                }

                case snare:
                {
                    //  Ruido en banda MAS un cuerpo afinado, y la proporcion
                    //  entre los dos es lo que separa una caja de un aro.
                    f1.setLp (r.p2);
                    f2.setLp (180.0);
                    const float nz = f2.hp (f1.lp (rnd()));
                    ph += 2.0 * juce::MathConstants<double>::pi * r.hz / kRate;
                    const float body = (float) std::sin (ph);
                    v = ((1.0f - r.p1) * nz * 1.6f + r.p1 * body) * env (t, r.decay);
                    break;
                }

                case hat:
                {
                    //  Ruido y paso alto. p2 = 1 lo deja mas ancho, que es la
                    //  diferencia entre un charles y una maraca.
                    f1.setLp (r.hz);
                    const float nz = (r.p2 > 0.5f) ? f1.lp (rnd()) * 2.0f : f1.hp (rnd());
                    v = nz * env (t, r.decay);
                    break;
                }

                case metal:
                {
                    //  Las seis cuadradas por un paso alto, y la caida decide
                    //  si es un cencerro, un ride o un crash.
                    f1.setLp (r.p1 > 0.3f ? 6000.0 : 2500.0);
                    v = f1.hp (met.next (r.hz, kRate)) * env (t, r.decay) * 0.8f;
                    break;
                }

                case clap:
                {
                    //  UNA palmada son cuatro manos que no llegan a la vez.
                    //  Tres golpes de 10 ms y una cola: sin los tres primeros
                    //  esto es un ruido corto, no una palmada.
                    f1.setLp (r.p2);
                    f2.setLp (400.0);
                    const float nz = f2.hp (f1.lp (rnd()));
                    float e = env (juce::jmax (0.0f, t - 0.028f), r.decay) * 0.9f;
                    for (int k = 0; k < 3; ++k)
                    {
                        const float dt = t - (float) k * 0.009f;
                        if (dt >= 0.0f) e += env (dt, 0.006f);
                    }
                    v = nz * juce::jmin (2.2f, e) * 0.7f;
                    break;
                }

                case tone:
                {
                    //  p1 = cuanto diente de sierra lleva encima del seno, que
                    //  es lo que separa un sub de un bajo con garra.
                    //  p2 = 1 lo hace sostenido en vez de percusivo.
                    ph += 2.0 * juce::MathConstants<double>::pi * r.hz / kRate;
                    const float s = (float) std::sin (ph);
                    const float saw = (float) (2.0 * (std::fmod (ph / (2.0 * juce::MathConstants<double>::pi), 1.0)) - 1.0);
                    const float mix = (1.0f - r.p1) * s + r.p1 * saw * 0.6f;
                    const float e = (r.p2 > 0.5f) ? ad (t, 0.06f, r.decay) : env (t, r.decay);
                    v = sat (1.4f * mix * e);
                    break;
                }

                case chord:
                {
                    //  Tres notas. p1 elige el intervalo: 0 mayor, 1 menor,
                    //  2 solo la quinta - que es la que no dice si esta
                    //  alegre o triste y por eso vale para todo.
                    const double third = (r.p1 < 0.5f) ? 1.2599 : (r.p1 < 1.5f ? 1.1892 : 1.4983);
                    ph  += 2.0 * juce::MathConstants<double>::pi * r.hz / kRate;
                    ph2 += 2.0 * juce::MathConstants<double>::pi * r.hz * third / kRate;
                    ph3 += 2.0 * juce::MathConstants<double>::pi * r.hz * 1.4983 / kRate;
                    const float e = (r.p2 > 0.5f) ? ad (t, 0.10f, r.decay) : env (t, r.decay);
                    v = 0.42f * (float) (std::sin (ph) + std::sin (ph2) + std::sin (ph3)) * e;
                    break;
                }

                case sweep:
                {
                    //  El filtro se mueve, y hacia donde se mueve lo es todo:
                    //  hacia arriba es tension, hacia abajo es final.
                    const float k = juce::jlimit (0.0f, 1.0f, t / juce::jmax (0.01f, r.decay));
                    const double hz = (r.p1 >= 0.0f) ? r.hz * std::pow (12.0, k)
                                                     : r.hz * std::pow (0.06, k);
                    f1.setLp (juce::jlimit (60.0, 16000.0, hz));
                    const float nz = f1.lp (rnd()) * 2.0f;
                    //  Sube y luego se va, en vez de empezar fuerte: una
                    //  subida que empieza al maximo no sube.
                    const float e = (r.p1 >= 0.0f) ? ad (t, r.decay * 0.75f, r.decay * 0.25f)
                                                   : env (t, r.decay);
                    v = nz * e * (r.p2 > 0.5f ? 0.7f : 1.0f);
                    break;
                }

                case noiseHit:
                {
                    //  Un impacto es ruido ANCHO con un grave debajo. Solo el
                    //  ruido es un aplauso; solo el grave es un bombo.
                    f1.setLp (900.0);
                    const float nz = f1.lp (rnd());
                    ph += 2.0 * juce::MathConstants<double>::pi * r.hz * (1.0 + r.p1 * std::exp (-t / 0.08f)) / kRate;
                    v = sat (1.8f * (0.55f * nz + 0.75f * (float) std::sin (ph)) * env (t, r.decay));
                    break;
                }

                case vinyl:
                {
                    //  Siseo con chasquidos encima, que es lo que un disco
                    //  hace de verdad: el suelo es constante y lo que se oye
                    //  son los impulsos.
                    f1.setLp (6000.0);
                    float s = f1.lp (rnd()) * 0.16f;
                    if (n >= nextClick)
                    {
                        clickAmp = 0.5f + 0.5f * std::abs (rnd());
                        nextClick = n + 200 + (int) (std::abs (rnd()) * (r.p1 > 0.5f ? 900.0f : 3000.0f));
                    }
                    if (clickAmp > 0.001f)
                    {
                        s += clickAmp * rnd();
                        clickAmp *= 0.55f;
                    }
                    v = s * ad (t, 0.01f, r.decay);
                    break;
                }

                case fm:
                {
                    //  Dos operadores. La relacion entre las dos frecuencias
                    //  decide si suena a campana (no entera) o a instrumento
                    //  (entera), y el indice cae con el tiempo porque un
                    //  ataque brillante que no se apaga suena a sintetizador
                    //  barato.
                    ph2 += 2.0 * juce::MathConstants<double>::pi * r.hz * r.p2 / kRate;
                    const double mod = std::sin (ph2) * r.p1 * env (t, r.decay * 0.45f);
                    ph  += 2.0 * juce::MathConstants<double>::pi * r.hz / kRate;
                    v = (float) std::sin (ph + mod) * env (t, r.decay);
                    break;
                }
            }

            d[n] = juce::jlimit (-0.99f, 0.99f, v);
        }

        //  NORMALIZAR Y QUITAR EL SALTO DEL FINAL.
        //
        //  Sin normalizar, un charles queda diez decibelios por debajo de un
        //  bombo y la persona cree que el pad esta roto. Y sin la rampa del
        //  final, la muestra acaba en un valor distinto de cero: un CLICK en
        //  cada golpe, que es el fallo que mas se oye y menos se ve.
        float peak = 0.0f;
        for (int n = 0; n < len; ++n) peak = juce::jmax (peak, std::abs (d[n]));
        const float g = (peak > 1.0e-6f) ? 0.89f / peak : 1.0f;

        const int fade = juce::jmin (len / 4, (int) (kRate * 0.004));
        for (int n = 0; n < len; ++n)
        {
            float x = d[n] * g;
            if (n >= len - fade) x *= (float) (len - n) / (float) fade;
            d[n] = x;
        }

        return sb;
    }
}

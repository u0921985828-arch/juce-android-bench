#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cmath>
#include "Sobre2x.h"

// ============================================================================
//  LA FAMILIA ANALOGICO: el EQ grafico de siete faders, el previo de valvula y
//  la cinta. Del telefono: «un ecualizador de faders mitico», «un preamp que de
//  esa calidez de la valvula, no por el ruido sino por el comportamiento» y
//  «la cinta, como un compresor de cinta pero que no come tanto: redondea la
//  onda, no la jode».
//
//  Las tres piezas siguen la regla de la casa en el hilo de audio: cero
//  reservas, cero cerrojos. Los coeficientes se recalculan por bloque, a mano,
//  con las formulas de RBJ, igual que `Eq5`.
// ============================================================================
namespace Analogico
{
    //  Un biquad en forma directa II transpuesta, sin estado compartido.
    struct Biquad
    {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1[2] {}, z2[2] {};

        void limpia() noexcept { z1[0] = z1[1] = z2[0] = z2[1] = 0.0f; }

        inline float paso (float x, int ch) noexcept
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }

        void campana (double fs, double hz, double q, double dB) noexcept
        {
            const double A = std::pow (10.0, dB / 40.0);
            const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * fs, hz) / fs;
            const double al = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
            const double c = std::cos (w0);
            const double a0 = 1.0 + al / A;
            b0 = (float) ((1.0 + al * A) / a0); b1 = (float) (-2.0 * c / a0); b2 = (float) ((1.0 - al * A) / a0);
            a1 = (float) (-2.0 * c / a0);       a2 = (float) ((1.0 - al / A) / a0);
        }

        void pasoBajo (double fs, double hz, double q) noexcept
        {
            const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * fs, hz) / fs;
            const double al = std::sin (w0) / (2.0 * juce::jmax (0.1, q));
            const double c = std::cos (w0);
            const double a0 = 1.0 + al;
            b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0);         a2 = (float) ((1.0 - al) / a0);
        }

        void estanteAlto (double fs, double hz, double dB) noexcept
        {
            const double A = std::pow (10.0, dB / 40.0);
            const double w0 = juce::MathConstants<double>::twoPi * juce::jlimit (10.0, 0.45 * fs, hz) / fs;
            const double c = std::cos (w0), s = std::sin (w0);
            const double al = s / 2.0 * std::sqrt (2.0);   // S = 1
            const double sa = 2.0 * std::sqrt (A) * al;
            const double a0 = (A + 1.0) - (A - 1.0) * c + sa;
            b0 = (float) (A * ((A + 1.0) + (A - 1.0) * c + sa) / a0);
            b1 = (float) (-2.0 * A * ((A - 1.0) + (A + 1.0) * c) / a0);
            b2 = (float) (A * ((A + 1.0) + (A - 1.0) * c - sa) / a0);
            a1 = (float) (2.0 * ((A - 1.0) - (A + 1.0) * c) / a0);
            a2 = (float) (((A + 1.0) - (A - 1.0) * c - sa) / a0);
        }
    };

    //  Un SVF de topologia preservada (Zavalishin): se le pueden mover la frecuencia y la Q cada bloque
    //  sin el golpe que da un biquad grave al que se le cambian los coeficientes.
    struct Svf
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.0f;
        float ic1[2] {}, ic2[2] {};
        void limpia() noexcept { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0f; }
        void pon (double fs, double hz, double q) noexcept
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, 0.45 * fs, hz) / fs);
            k  = (float) (1.0 / juce::jmax (0.1, q));
            a1 = (float) (1.0 / (1.0 + g * (g + k)));
            a2 = (float) g * a1;
            a3 = (float) g * a2;
        }
        //  lp y bp a la vez.
        inline void paso (float x, int ch, float& lp, float& bp) noexcept
        {
            const float v3 = x - ic2[ch];
            const float v1 = a1 * ic1[ch] + a2 * v3;
            const float v2 = ic2[ch] + a2 * ic1[ch] + a3 * v3;
            ic1[ch] = 2.0f * v1 - ic1[ch];
            ic2[ch] = 2.0f * v2 - ic2[ch];
            lp = v2; bp = v1;
        }
    };

    //  tanh racional (Pade 3/2): error < 0.3 % hasta |x| = 3 y pegada a +-1 despues; una division por muestra.
    inline float tanhR (float x) noexcept
    {
        if (x >  3.0f) return  1.0f;
        if (x < -3.0f) return -1.0f;
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    // ------------------------------------------------------------------------
    //  GEQ: siete bandas de octava, 100 Hz a 6.4 kHz, +-12 dB, el reparto de
    //  los pedales graficos de siete. Cinco bandas parametricas ya estan en EQ;
    //  esto es lo otro: posiciones fijas y un fader por banda.
    // ------------------------------------------------------------------------
    struct Geq7
    {
        static constexpr int   kBandas = 7;
        static constexpr float kHz[kBandas] = { 100.0f, 200.0f, 400.0f, 800.0f, 1600.0f, 3200.0f, 6400.0f };
        static constexpr float kTope = 12.0f;
        static constexpr double kQ = 1.4;   // una octava entre los -3 dB

        std::array<std::atomic<float>, kBandas> objetivo {};
        float actual[kBandas] {};
        Biquad bq[kBandas];
        double fsHecha = 0.0, anchoHecho = 1.0;

        void ponBanda (int b, float dB) noexcept
        {
            if (juce::isPositiveAndBelow (b, kBandas))
                objetivo[(size_t) b].store (std::isfinite (dB) ? juce::jlimit (-kTope, kTope, dB) : 0.0f,
                                            std::memory_order_relaxed);
        }
        float banda (int b) const noexcept
        { return juce::isPositiveAndBelow (b, kBandas) ? objetivo[(size_t) b].load (std::memory_order_relaxed) : 0.0f; }

        void limpia() noexcept { for (auto& f : bq) f.limpia(); }

        //  Por bloque: las bandas se deslizan hacia lo pedido (un fader movido no chasquea) y solo se
        //  recalcula lo que se movio.
        //  `ancho` 0.5..2 abre o cierra las siete campanas a la vez (la Q es kQ / ancho).
        void prepara (double fs, float ancho = 1.0f) noexcept
        {
            const double an = juce::jlimit (0.5, 2.0, std::isfinite (ancho) ? (double) ancho : 1.0);
            const bool otraFs = std::abs (fs - fsHecha) > 0.5 || std::abs (an - anchoHecho) > 1.0e-3;
            anchoHecho = an;
            for (int b = 0; b < kBandas; ++b)
            {
                const float t = objetivo[(size_t) b].load (std::memory_order_relaxed);
                float n = actual[b] + 0.35f * (t - actual[b]);
                if (std::abs (n - t) < 0.01f) n = t;
                if (otraFs || ! juce::exactlyEqual (n, actual[b]))
                    bq[b].campana (fs, kHz[b], kQ / an, n);
                actual[b] = n;
            }
            fsHecha = fs;
        }

        void procesa (float* const* w, int chans, int n) noexcept
        {
            for (int ch = 0; ch < juce::jmin (2, chans); ++ch)
                for (int i = 0; i < n; ++i)
                {
                    float x = w[ch][i];
                    for (int b = 0; b < kBandas; ++b)
                        if (! juce::exactlyEqual (actual[b], 0.0f)) x = bq[b].paso (x, ch);
                    w[ch][i] = x;
                }
        }
    };

    // ------------------------------------------------------------------------
    //  PRE: el previo de valvula. Un triodo no recorta: redondea, y lo hace
    //  DESIGUAL arriba y abajo porque trabaja con polarizacion, y de esa
    //  asimetria salen los armonicos PARES -el segundo sobre todo-, que son los
    //  que se oyen como calor y no como distorsion. La curva va normalizada a
    //  pendiente uno en el cero: una señal floja pasa al mismo nivel y lo que
    //  sube la valvula es la redondez de los picos, no el volumen.
    //
    //    p0 DRIVE   0..1  cuanto se empuja (0 a +24 dB dentro de la curva)
    //    p1 CALOR   0..1  la polarizacion: mas asimetria, mas pares
    // ------------------------------------------------------------------------
    struct Valvula
    {
        Sobre2x::Estado os[2];
        float dc[2] {}, dcX[2] {}, lp[2] {};
        float smDrive = 0.0f, smCalor = 0.0f;

        void limpia() noexcept
        {
            for (auto& e : os) e.limpia();
            dc[0] = dc[1] = dcX[0] = dcX[1] = lp[0] = lp[1] = 0.0f;
        }

        static float curva (float x, float g, float b, float t0, float inv) noexcept
        { return (tanhR (g * x + b) - t0) * inv; }

        //  La forma que suena, para el visor del plato: la misma cuenta que `procesa`.
        static float transfiere (float x, float drive, float calor) noexcept
        {
            const float g = std::pow (10.0f, 24.0f * juce::jlimit (0.0f, 1.0f, drive) / 20.0f) * 0.5f + 0.5f;
            const float b = 0.08f + 0.42f * juce::jlimit (0.0f, 1.0f, calor);
            const float t0 = tanhR (b);
            return curva (x, g, b, t0, 1.0f / juce::jmax (1.0e-3f, g * (1.0f - t0 * t0)));
        }

        void procesa (float* const* w, int chans, int n, double fs, float drive, float calor, bool a1x = false) noexcept   // calidad#: a1x = sin sobremuestreo
        {
            smDrive += 0.25f * (juce::jlimit (0.0f, 1.0f, drive) - smDrive);
            smCalor += 0.25f * (juce::jlimit (0.0f, 1.0f, calor) - smCalor);
            const float g = std::pow (10.0f, 24.0f * smDrive / 20.0f) * 0.5f + 0.5f;
            const float b = 0.08f + 0.42f * smCalor;
            const float t0 = tanhR (b);
            const float pend = g * (1.0f - t0 * t0);
            const float inv = 1.0f / juce::jmax (1.0e-3f, pend);
            //  El transformador de salida: los agudos de arriba caen un poco (18 kHz, un polo).
            const float cLp = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 18000.0f / (float) fs);
            const float cDc = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 8.0f / (float) fs);
            for (int ch = 0; ch < juce::jmin (2, chans); ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const auto cur = [g, b, t0, inv] (float v) noexcept { return curva (v, g, b, t0, inv); };
                    const float y = a1x ? Sobre2x::pasoA1x (os[ch], w[ch][i], cur)
                                        : Sobre2x::paso    (os[ch], w[ch][i], cur);
                    //  La asimetria deja continua: fuera con un paso alto de 8 Hz.
                    dc[ch] += cDc * (y - dc[ch]);
                    const float sinDc = y - dc[ch];
                    lp[ch] += cLp * (sinDc - lp[ch]);
                    w[ch][i] = lp[ch];
                }
        }
    };

    // ------------------------------------------------------------------------
    //  CIN: la cinta. Tres cosas que hace una grabadora y que juntas son «el
    //  sonido de cinta»:
    //    · satura SUAVE y simetrico, y antes de saturar los agudos se enfatizan
    //      y despues se devuelven: los agudos llegan antes al techo y se
    //      redondean, que es por que una cinta caliente suena oscura y no rota;
    //    · comprime POR PROGRAMA, lento y poco -como mucho unos 4 dB-: pega la
    //      mezcla sin comerse los golpes;
    //    · el golpe de cabeza (un poco mas de graves) y la caida de agudos, que
    //      dependen de la velocidad.
    //
    //    p0 SATURA     0..1  el nivel al que entra (0 a +18 dB)
    //    p1 VELOCIDAD  0..1  de 7.5 a 30 ips: golpe de 55 a 110 Hz, caida de 9 a 20 kHz
    // ------------------------------------------------------------------------
    struct Cinta
    {
        Sobre2x::Estado os[2];
        Biquad pre, post;
        Svf golpe, caida;
        float env[2] {};
        float smSat = -1.0f, smVel = -1.0f, gAntes = -1.0f;
        double fsHecha = 0.0;

        //  La forma que suena (sin el enfasis ni la compresion lenta), para el visor del plato.
        static float transfiere (float x, float sat) noexcept
        {
            const float g = std::pow (10.0f, 18.0f * juce::jlimit (0.0f, 1.0f, sat) / 20.0f);
            return tanhR (g * x) / g;
        }

        void limpia() noexcept
        {
            for (auto& e : os) e.limpia();
            pre.limpia(); post.limpia(); golpe.limpia(); caida.limpia();
            env[0] = env[1] = 0.0f;
            gAntes = -1.0f;
        }

        void procesa (float* const* w, int chans, int n, double fs, float sat, float vel, bool a1x = false) noexcept   // calidad#: a1x = sin sobremuestreo
        {
            sat = juce::jlimit (0.0f, 1.0f, sat);
            vel = juce::jlimit (0.0f, 1.0f, vel);
            const bool primera = smSat < 0.0f;
            smSat = primera ? sat : smSat + 0.25f * (sat - smSat);
            const float vAntes = smVel;
            smVel = primera ? vel : smVel + 0.25f * (vel - smVel);
            if (primera || std::abs (fs - fsHecha) > 0.5 || std::abs (smVel - vAntes) > 1.0e-4f)
            {
                const double hzGolpe = 55.0 * std::pow (2.0, (double) smVel);
                const double hzCaida = 9000.0 * std::pow (20000.0 / 9000.0, (double) smVel);
                pre.estanteAlto  (fs, 3000.0,  6.0);
                post.estanteAlto (fs, 3000.0, -6.0);
                golpe.pon (fs, hzGolpe, 0.8);
                caida.pon (fs, hzCaida, 0.6);
                fsHecha = fs;
            }
            //  El nivel de entrada se desliza muestra a muestra dentro del bloque: a saltos por bloque chasqueaba al cambiar de preset.
            const float g1 = std::pow (10.0f, 18.0f * smSat / 20.0f);
            const float g0 = gAntes > 0.0f ? gAntes : g1;
            gAntes = g1;
            const float dg = (g1 - g0) / (float) juce::jmax (1, n);
            //  La compresion de cinta: un detector lento (30 ms subiendo, 300 bajando) y un techo de unos 4 dB.
            const float cSube = 1.0f - std::exp (-1.0f / (0.030f * (float) fs));
            const float cBaja = 1.0f - std::exp (-1.0f / (0.300f * (float) fs));
            for (int ch = 0; ch < juce::jmin (2, chans); ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float g = g0 + dg * (float) i;
                    float x = pre.paso (w[ch][i], ch) * g;
                    const float a = std::abs (x);
                    env[ch] += (a > env[ch] ? cSube : cBaja) * (a - env[ch]);
                    const float gr = 1.0f / (1.0f + 0.6f * juce::jmax (0.0f, env[ch] - 0.25f));
                    x *= juce::jmax (0.63f, gr);
                    const auto th = [] (float v) noexcept { return tanhR (v); };
                    float y = a1x ? Sobre2x::pasoA1x (os[ch], x, th) : Sobre2x::paso (os[ch], x, th);
                    y = post.paso (y / g, ch);
                    float lp, bp;
                    golpe.paso (y, ch, lp, bp);
                    y += 0.19f * golpe.k * bp;          // +1.5 dB en el golpe de cabeza
                    caida.paso (y, ch, lp, bp);
                    w[ch][i] = lp;
                }
        }
    };
}

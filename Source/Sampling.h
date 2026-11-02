#pragma once

#include <JuceHeader.h>
#include <cstdint>
#include <vector>
#include "Analogico.h"
#include "Sobre2x.h"

// ============================================================================
//  LA FAMILIA SAMPLING, mas el compresor de bus de ANALOGICO. Del telefono:
//  «plugins mas adecuados a la produccion y el sampling», «que los plugins
//  esten preparados para producir ritmos de calidad». Seis piezas:
//
//    SMP  sampler vintage   12 bits a 26 kHz con su filtro de entrada y salida
//    CLP  clipper           mas pegada sin pasar del techo
//    VIN  vinilo            desgaste (banda, crujido, soplido) y wow
//    GLU  compresor de bus  el que empasta la bateria o la mezcla
//    OTT  multibanda        tres bandas, arriba y abajo a la vez
//    STP  freno de cinta    el final de cada vuelta del reloj se frena
//
//  Y LA OTRA MITAD DEL PEDIDO: «que no se nos vaya de madre el uso de CPU,
//  RAM y demas». Las ganancias de GLU y OTT se calculan cada 16 muestras y
//  van en rampa, la linea de VIN es fija y corta, y la cinta de STP se guarda
//  en 16 bits y con 0.5 s, que frena entero hasta 1.5 s.
//  Nada de esto reserva en el hilo de audio.
// ============================================================================
namespace Sampling
{
    using Analogico::Svf;
    using Analogico::tanhR;

    inline float dBaG (float dB) noexcept { return std::exp (dB * 0.11512925f); }
    inline float gAdB (float g)  noexcept { return 8.6858896f * std::log (juce::jmax (1.0e-9f, g)); }

    //  Un azar de 32 bits sin estado global: el mismo crujido en cada corrida, que es lo que deja medirlo.
    struct Azar
    {
        uint32_t s = 0x9E3779B9u;
        inline float uno() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) s * 2.3283064e-10f; }   // 0..1
        inline float par() noexcept { return 2.0f * uno() - 1.0f; }                                                   // -1..1
    };

    // ------------------------------------------------------------------------
    //  SMP: el sampler de los ochenta. Lo que lo hace sonar asi no son los
    //  bits solos -eso ya es BIT- sino el FILTRO: entra por un paso bajo de
    //  cuatro polos a la mitad de su frecuencia, se retiene y se cuantiza, y
    //  sale por otro suave que deja parte del aliasing. Por eso suena gordo y
    //  oscuro en vez de roto.
    //    p0 FRECUENCIA  4 000 .. 48 000 Hz (26 040 la del SP)
    //    p1 BITS        8 .. 16
    // ------------------------------------------------------------------------
    struct Sampler
    {
        Svf pre1, pre2, post;
        float hold[2] {};
        float fase = 0.0f, smHz = -1.0f, smBits = -1.0f, hzHecho = -1.0f;
        double fsHecha = 0.0;

        void limpia() noexcept
        {
            pre1.limpia(); pre2.limpia(); post.limpia();
            hold[0] = hold[1] = 0.0f; fase = 0.0f; smHz = smBits = -1.0f;
        }

        void procesa (float* const* w, int chans, int n, double fs, float hz, float bits) noexcept
        {
            hz   = juce::jlimit (4000.0f, 48000.0f, hz);
            bits = juce::jlimit (8.0f, 16.0f, bits);
            const bool primera = smHz < 0.0f;
            smHz   = primera ? hz   : smHz   + 0.25f * (hz - smHz);
            smBits = primera ? bits : smBits + 0.25f * (bits - smBits);
            if (std::abs (smHz - hzHecho) > 1.0f || std::abs (fs - fsHecha) > 0.5)
            {
                const double fc = juce::jmin (0.42 * smHz, 0.45 * fs);
                pre1.pon (fs, fc, 0.541);   // Butterworth de cuatro polos en dos SVF
                pre2.pon (fs, fc, 1.307);
                post.pon (fs, juce::jmin (0.45 * smHz, 0.45 * fs), 0.707);
                hzHecho = smHz; fsHecha = fs;
            }
            const float q = std::exp2 (smBits - 1.0f), iq = 1.0f / q;
            const float paso = (float) (smHz / fs);
            const int cs = juce::jmin (2, chans);
            for (int i = 0; i < n; ++i)
            {
                fase += paso;
                const bool toma = fase >= 1.0f;
                if (toma) fase -= std::floor (fase);
                for (int ch = 0; ch < cs; ++ch)
                {
                    float lp, bp, lp2;
                    pre1.paso (w[ch][i], ch, lp, bp);
                    pre2.paso (lp, ch, lp2, bp);
                    if (toma) hold[ch] = std::round (lp2 * q) * iq;
                    post.paso (hold[ch], ch, lp, bp);
                    w[ch][i] = lp;
                }
            }
        }
    };

    // ------------------------------------------------------------------------
    //  CLP: el clipper. Sube la entrada y corta en el techo (-0.3 dBFS): la
    //  bateria suena mas fuerte y el pico no pasa. FORMA va del corte seco
    //  (0) a una rodilla redonda (1), que es lo que distingue un clip que
    //  muerde de uno que solo aprieta. A 2x, como DRV, para que no pliegue.
    //    p0 DRIVE  0 .. +18 dB
    //    p1 FORMA  0 .. 1
    // ------------------------------------------------------------------------
    struct Clipper
    {
        static constexpr float kTecho = 0.96605f;   // -0.3 dB
        Sobre2x::Estado os[2];
        float gAntes = -1.0f, smForma = -1.0f;

        void limpia() noexcept { for (auto& e : os) e.limpia(); gAntes = smForma = -1.0f; }

        static float curva (float x, float t) noexcept
        {
            const float a = std::abs (x);
            if (a <= t) return x;
            const float y = t >= 0.999f ? 1.0f : t + (1.0f - t) * tanhR ((a - t) / (1.0f - t));
            return x < 0.0f ? -juce::jmin (1.0f, y) : juce::jmin (1.0f, y);
        }
        static float rodilla (float forma) noexcept { return 1.0f - 0.6f * juce::jlimit (0.0f, 1.0f, forma); }

        //  La forma que suena, para el visor del plato.
        static float transfiere (float x, float driveDb, float forma) noexcept
        { return kTecho * curva (x * dBaG (juce::jlimit (0.0f, 18.0f, driveDb)), rodilla (forma)); }

        void procesa (float* const* w, int chans, int n, float driveDb, float forma) noexcept
        {
            smForma = smForma < 0.0f ? forma : smForma + 0.25f * (forma - smForma);
            const float t = rodilla (smForma);
            const float g1 = dBaG (juce::jlimit (0.0f, 18.0f, driveDb));
            const float g0 = gAntes > 0.0f ? gAntes : g1;
            gAntes = g1;
            const float dg = (g1 - g0) / (float) juce::jmax (1, n);
            for (int ch = 0; ch < juce::jmin (2, chans); ++ch)
                for (int i = 0; i < n; ++i)
                {
                    const float x = w[ch][i] * (g0 + dg * (float) i);
                    const float y = Sobre2x::paso (os[ch], x, [t] (float v) noexcept { return curva (v, t); });
                    w[ch][i] = kTecho * juce::jlimit (-1.0f, 1.0f, y);   // sampling#: el filtro del 2x repica sobre el techo con ruido
                }
        }
    };

    // ------------------------------------------------------------------------
    //  VIN: el vinilo. DESGASTE estrecha la banda (graves fuera desde 25 Hz,
    //  agudos desde 18 kHz hasta 5 kHz) y mete crujido y soplido; WOW es el
    //  plato que no gira redondo (0.5 Hz) y su temblor (6.3 Hz). El crujido y
    //  el soplido SIGUEN A LA SEÑAL: en silencio el canal calla, que en un
    //  telefono un ruido de fondo constante es un fallo y no un efecto.
    //    p0 DESGASTE  0 .. 1
    //    p1 WOW       0 .. 1
    // ------------------------------------------------------------------------
    struct Vinilo
    {
        static constexpr int kLinea = 1024;   // 21 ms a 48 kHz; el wow pide 5 como mucho
        float linea[2][kLinea] {};
        int escribe = 0;
        Svf hp, lp, chis;
        Azar az;
        float env = 0.0f, smDes = -1.0f, smWow = -1.0f, desHecho = -1.0f;
        double fase1 = 0.0, fase2 = 0.0, fsHecha = 0.0;
        float retAntes = -1.0f;

        void limpia() noexcept
        {
            for (auto& l : linea) std::fill (std::begin (l), std::end (l), 0.0f);
            escribe = 0; hp.limpia(); lp.limpia(); chis.limpia();
            env = 0.0f; smDes = smWow = -1.0f; retAntes = -1.0f; az = {};
        }

        float retardo (double fs) const noexcept
        {
            //  El retardo medio crece con el wow y a cero es cero: sin wow no hay peine con el seco del MIX.
            const double m1 = std::sin (fase1), m2 = std::sin (fase2);
            return (float) (fs * smWow * (0.0026 + 0.00225 * m1 + 0.00025 * m2));
        }

        void procesa (float* const* w, int chans, int n, double fs, float desgaste, float wow) noexcept
        {
            desgaste = juce::jlimit (0.0f, 1.0f, desgaste);
            wow      = juce::jlimit (0.0f, 1.0f, wow);
            const bool primera = smDes < 0.0f;
            smDes = primera ? desgaste : smDes + 0.25f * (desgaste - smDes);
            smWow = primera ? wow      : smWow + 0.25f * (wow - smWow);
            if (std::abs (smDes - desHecho) > 1.0e-4f || std::abs (fs - fsHecha) > 0.5)
            {
                hp.pon (fs, 25.0 + 100.0 * smDes, 0.707);
                lp.pon (fs, 18000.0 * std::pow (0.28, (double) smDes), 0.707);
                chis.pon (fs, 2600.0, 0.9);
                desHecho = smDes; fsHecha = fs;
            }
            //  El wow se calcula en las dos puntas del bloque y se interpola: 6 Hz contra 375 bloques por segundo.
            const float r0 = retAntes >= 0.0f ? retAntes : retardo (fs);
            fase1 += juce::MathConstants<double>::twoPi * 0.55 * n / fs;
            fase2 += juce::MathConstants<double>::twoPi * 6.3  * n / fs;
            if (fase1 > 1.0e4) fase1 = std::fmod (fase1, juce::MathConstants<double>::twoPi);
            if (fase2 > 1.0e4) fase2 = std::fmod (fase2, juce::MathConstants<double>::twoPi);
            const float r1 = juce::jlimit (0.0f, (float) (kLinea - 4), retardo (fs));
            retAntes = r1;
            const float dr = (r1 - r0) / (float) juce::jmax (1, n);

            const float prob  = (float) ((1.5 + 40.0 * smDes * smDes) / fs);
            const float golpe = 0.30f * smDes, soplo = 0.0035f * smDes;
            const float cSube = 1.0f - std::exp (-1.0f / (0.005f * (float) fs));
            const float cBaja = 1.0f - std::exp (-1.0f / (0.400f * (float) fs));
            const int cs = juce::jmin (2, chans);
            for (int i = 0; i < n; ++i)
            {
                float a = std::abs (w[0][i]);
                if (cs > 1) a = juce::jmax (a, std::abs (w[1][i]));
                env += (a > env ? cSube : cBaja) * (a - env);
                const float sigue = juce::jmin (1.0f, env * 6.0f);
                const float crujido = az.uno() < prob ? golpe * sigue * (0.3f + 0.7f * az.uno()) * (az.uno() < 0.5f ? -1.0f : 1.0f) : 0.0f;
                const float ret = r0 + dr * (float) i;
                const int ent = (int) ret;
                const float fr = ret - (float) ent;
                for (int ch = 0; ch < cs; ++ch)
                {
                    linea[ch][escribe] = w[ch][i];
                    const int i0 = (escribe - ent) & (kLinea - 1), i1 = (i0 - 1) & (kLinea - 1);
                    float y = linea[ch][i0] + fr * (linea[ch][i1] - linea[ch][i0]);
                    float l, b;
                    hp.paso (y, ch, l, b);
                    y = y - hp.k * b - l;            // el paso alto del SVF
                    lp.paso (y, ch, l, b);
                    y = l;
                    chis.paso (crujido, ch, l, b);
                    w[ch][i] = y + b + soplo * sigue * az.par();
                }
                escribe = (escribe + 1) & (kLinea - 1);
            }
        }
    };

    //  La cuenta de un compresor en dB con rodilla de 6: cuanto baja (negativo) un nivel `lev` sobre `umbral`.
    inline float reduce (float lev, float umbral, float ratio) noexcept
    {
        const float sobre = lev - umbral, k = 1.0f / ratio - 1.0f;
        if (sobre <= -3.0f) return 0.0f;
        if (sobre < 3.0f) return k * (sobre + 3.0f) * (sobre + 3.0f) / 12.0f;
        return k * sobre;
    }

    // ------------------------------------------------------------------------
    //  GLU: el compresor de bus. Mira el pico de los dos canales juntos -una
    //  bateria no se abre de lado- y baja los dos igual; la rodilla es de 6 dB
    //  y la ganancia de compensacion sale sola del umbral y el ratio. Con el
    //  MIX a medias es la compresion en paralelo que se hace a la bateria.
    //    p0 UMBRAL  -30 .. 0 dB      p4 ATAQUE  0.1 .. 30 ms
    //    p1 RATIO   1.5 .. 10        p5 CAIDA   50 .. 1200 ms
    // ------------------------------------------------------------------------
    struct Bus
    {
        static constexpr int kTrozo = 16;
        float grDb = 0.0f, gAntes = 1.0f;
        std::atomic<float> gr { 0.0f };   // para el visor: lo que esta bajando ahora, en dB

        void limpia() noexcept { grDb = 0.0f; gAntes = 1.0f; gr.store (0.0f, std::memory_order_relaxed); }

        static float compensa (float umbral, float ratio) noexcept { return -0.35f * umbral * (1.0f - 1.0f / ratio); }

        void procesa (float* const* w, int chans, int n, double fs, float umbral, float ratio,
                      float ataqueMs, float caidaMs) noexcept
        {
            umbral = juce::jlimit (-30.0f, 0.0f, umbral);
            ratio  = juce::jlimit (1.5f, 10.0f, ratio);
            const float cA = 1.0f - std::exp (-(float) kTrozo / (juce::jlimit (0.1f, 30.0f, ataqueMs) * 0.001f * (float) fs));
            const float cC = 1.0f - std::exp (-(float) kTrozo / (juce::jlimit (50.0f, 1200.0f, caidaMs) * 0.001f * (float) fs));
            const float mk = compensa (umbral, ratio);
            const int cs = juce::jmin (2, chans);
            for (int d = 0; d < n; d += kTrozo)
            {
                const int m = juce::jmin (kTrozo, n - d);
                float pk = 0.0f;
                for (int ch = 0; ch < cs; ++ch)
                    for (int i = d; i < d + m; ++i) pk = juce::jmax (pk, std::abs (w[ch][i]));
                const float obj = reduce (gAdB (pk), umbral, ratio);
                grDb += (obj < grDb ? cA : cC) * (obj - grDb);
                const float g1 = dBaG (grDb + mk);
                const float dg = (g1 - gAntes) / (float) m;
                for (int ch = 0; ch < cs; ++ch)
                    for (int i = 0; i < m; ++i) w[ch][d + i] *= gAntes + dg * (float) (i + 1);
                gAntes = g1;
            }
            gr.store (grDb, std::memory_order_relaxed);
        }
    };

    // ------------------------------------------------------------------------
    //  OTT: el multibanda de arriba y abajo. Tres bandas (cortes de 120 Hz y
    //  2.5 kHz, Linkwitz-Riley de cuarto orden, que suman plano) y en cada una
    //  lo fuerte se baja y lo flojo se sube: todo queda denso y al frente, que
    //  es lo que se le hace a un bajo, a un pad o a una voz cortada. Lo que
    //  esta por debajo de -75 dB no se sube, o subiria el ruido y las colas.
    //    p0 CANTIDAD  0 .. 1   cuanto aprieta en los dos sentidos
    //    p1 TIEMPO    0 .. 1   de 4 veces mas rapido a 4 veces mas lento
    // ------------------------------------------------------------------------
    struct Multibanda
    {
        static constexpr int kTrozo = 16;
        Svf a1, b1, c1, a2, b2, c2;
        float grDb[3] {}, gAntes[3] { 1.0f, 1.0f, 1.0f };
        double fsHecha = 0.0;

        void limpia() noexcept
        {
            a1.limpia(); b1.limpia(); c1.limpia(); a2.limpia(); b2.limpia(); c2.limpia();
            for (int b = 0; b < 3; ++b) { grDb[b] = 0.0f; gAntes[b] = 1.0f; }
        }

        //  Cuanto mueve una banda a un nivel dado, en dB: negativo arriba del umbral de bajar, positivo debajo del de subir.
        static float mueve (float lev, float cantidad) noexcept
        {
            const float baja = reduce (lev, -24.0f, 1.0f + 9.0f * cantidad);
            const float bajo = -36.0f - lev;
            const float sube = bajo <= 0.0f ? 0.0f
                             : juce::jmin (bajo * (1.0f - 1.0f / (1.0f + 3.0f * cantidad)), 18.0f * cantidad)
                                 * juce::jlimit (0.0f, 1.0f, (lev + 75.0f) / 15.0f);
            return baja + sube;
        }

        //  Linkwitz-Riley de cuarto orden con tres SVF: grave y agudo de la misma pareja suman a un pasa todo.
        static inline void parte (Svf& a, Svf& b, Svf& c, float x, int ch, float& grave, float& agudo) noexcept
        {
            float l, bp;
            a.paso (x, ch, l, bp);
            const float h = x - a.k * bp - l;
            float l2, bp2;
            b.paso (l, ch, l2, bp2);
            grave = l2;
            float l3, bp3;
            c.paso (h, ch, l3, bp3);
            agudo = h - c.k * bp3 - l3;
        }

        void procesa (float* const* w, int chans, int n, double fs, float cantidad, float tiempo) noexcept
        {
            if (std::abs (fs - fsHecha) > 0.5)
            {
                for (auto* s : { &a1, &b1, &c1 }) s->pon (fs, 120.0, 0.7071);
                for (auto* s : { &a2, &b2, &c2 }) s->pon (fs, 2500.0, 0.7071);
                fsHecha = fs;
            }
            cantidad = juce::jlimit (0.0f, 1.0f, cantidad);
            const float esc = std::exp2 (4.0f * juce::jlimit (0.0f, 1.0f, tiempo) - 2.0f);   // x0.25 .. x4
            static constexpr float kAtaque[3] = { 13.5f, 22.4f, 13.5f }, kCaida[3] = { 132.0f, 282.0f, 43.0f };
            float cA[3], cC[3];
            for (int b = 0; b < 3; ++b)
            {
                cA[b] = 1.0f - std::exp (-(float) kTrozo / (kAtaque[b] * esc * 0.001f * (float) fs));
                cC[b] = 1.0f - std::exp (-(float) kTrozo / (kCaida[b]  * esc * 0.001f * (float) fs));
            }
            const int cs = juce::jmin (2, chans);
            float banda[3][2][kTrozo];
            for (int d = 0; d < n; d += kTrozo)
            {
                const int m = juce::jmin (kTrozo, n - d);
                float pk[3] {};
                for (int ch = 0; ch < cs; ++ch)
                    for (int i = 0; i < m; ++i)
                    {
                        float g, resto, me, ag;
                        parte (a1, b1, c1, w[ch][d + i], ch, g, resto);
                        parte (a2, b2, c2, resto, ch, me, ag);
                        banda[0][ch][i] = g; banda[1][ch][i] = me; banda[2][ch][i] = ag;
                        pk[0] = juce::jmax (pk[0], std::abs (g));
                        pk[1] = juce::jmax (pk[1], std::abs (me));
                        pk[2] = juce::jmax (pk[2], std::abs (ag));
                    }
                float g1[3], dg[3];
                for (int b = 0; b < 3; ++b)
                {
                    const float obj = mueve (gAdB (pk[b]), cantidad);
                    //  sampling#: lo subido se suelta en el acto al llegar un golpe; solo la bajada lleva ataque.
                    const bool suelta = obj < grDb[b] && grDb[b] > 0.0f;
                    if (suelta) grDb[b] = juce::jmax (obj, 0.0f);
                    grDb[b] += (obj < grDb[b] ? cA[b] : cC[b]) * (obj - grDb[b]);
                    g1[b] = dBaG (grDb[b]);
                    if (suelta) gAntes[b] = juce::jmin (gAntes[b], g1[b]);
                    dg[b] = (g1[b] - gAntes[b]) / (float) m;
                }
                for (int ch = 0; ch < cs; ++ch)
                    for (int i = 0; i < m; ++i)
                    {
                        const float k = (float) (i + 1);
                        w[ch][d + i] = banda[0][ch][i] * (gAntes[0] + dg[0] * k)
                                     + banda[1][ch][i] * (gAntes[1] + dg[1] * k)
                                     + banda[2][ch][i] * (gAntes[2] + dg[2] * k);
                    }
                for (int b = 0; b < 3; ++b) gAntes[b] = g1[b];
            }
        }
    };

    // ------------------------------------------------------------------------
    //  STP: el freno de cinta, AL TEMPO. Como REP, va con el reloj: en cada
    //  vuelta, la parte FRENO del final se frena hasta pararse (velocidad que
    //  cae en linea recta, como un motor que se suelta) y en la vuelta siguiente
    //  arranca en seco con un fundido de 4 ms. Se para al 80 % del tramo: el
    //  ultimo quinto es cinta parada, el hueco antes del uno. A FRENO cero pasa todo.
    //  La cinta se guarda en 16 bits y con 0.5 s: hasta que la nota se apaga
    //  (10 % de velocidad) se va 0.32 del tramo atras, asi que frena entero hasta
    //  1.5 s; uno mas largo se funde a silencio al llegar al tope de la cinta.
    //    p0 RATE    la vuelta (Hz o figura con el enganche)
    //    p1 FRENO   que parte del final de la vuelta se frena
    // ------------------------------------------------------------------------
    struct Freno
    {
        static constexpr double kSeg = 0.5;
        static constexpr float  kEsc = 16383.0f;   // +-2.0 en int16
        std::vector<int16_t> cinta[2];
        int largo = 0, escribe = 0, entra = 0, nEntra = 0;
        double atras = 0.0;
        float faseAnt = 1.0f, smFreno = -1.0f;

        //  Hilo de mensajes, desde `prepareToPlay`: lo unico que reserva.
        void prepara (double fs)
        {
            largo = (int) (fs * kSeg) + 4;
            for (auto& c : cinta) c.assign ((size_t) largo, 0);
            nEntra = juce::jmax (1, (int) (0.004 * fs));
            limpia();
        }
        void limpia() noexcept
        {
            for (auto& c : cinta) std::fill (c.begin(), c.end(), (int16_t) 0);
            escribe = 0; atras = 0.0; faseAnt = 1.0f; entra = 0; smFreno = -1.0f;
        }

        inline float lee (int ch, double pos) const noexcept
        {
            const int a = (int) std::floor (pos);
            const float fr = (float) (pos - (double) a);
            const int i0 = ((a % largo) + largo) % largo, i1 = (i0 + 1) % largo;
            const auto& c = cinta[(size_t) ch];
            return ((float) c[(size_t) i0] + fr * (float) (c[(size_t) i1] - c[(size_t) i0])) * (1.0f / kEsc);
        }

        void procesa (float* const* w, int chans, int n, float fase, float paso, float freno) noexcept
        {
            if (largo <= 4) return;
            freno = juce::jlimit (0.0f, 1.0f, freno);
            smFreno = smFreno < 0.0f ? freno : smFreno + 0.25f * (freno - smFreno);
            const float b = smFreno;
            const int cs = juce::jmin (2, chans);
            const double tope = (double) (largo - 4);
            for (int i = 0; i < n; ++i)
            {
                float ph = fase + paso * (float) i;
                ph -= std::floor (ph);
                if (ph < faseAnt - 0.5f && atras > 0.0) { atras = 0.0; entra = nEntra; }   // vuelta: a lo vivo
                faseAnt = ph;

                float v = 1.0f;
                //  Se para al 80 % del tramo y el 20 % que queda es cinta parada: el silencio antes del uno.
                if (b > 1.0e-3f && ph >= 1.0f - b) v = juce::jmax (0.0f, 1.0f - (ph - (1.0f - b)) / (0.8f * b));
                if (v < 1.0f) atras = juce::jmin (tope, atras + (double) (1.0f - v));
                else if (atras > 0.0) atras = juce::jmax (0.0, atras - 1.0);   // FRENO bajado a medias: alcanza a lo vivo al doble
                //  Al final del frenazo el sonido se va (debajo del 10 % de velocidad ya no se oye nota) y no queda un escalon.
                float a = juce::jmin (1.0f, v * 10.0f);
                if (entra > 0) { a *= 1.0f - (float) entra / (float) (nEntra + 1); --entra; }
                a *= (float) juce::jmin (1.0, (tope - atras) / (double) nEntra);   // sampling#: al tope de la cinta, silencio y no velocidad 1

                for (int ch = 0; ch < cs; ++ch)
                {
                    const float x = std::isfinite (w[ch][i]) ? w[ch][i] * kEsc : 0.0f;   // sampling#: NaN a int16 es indefinido
                    cinta[(size_t) ch][(size_t) escribe] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, x);
                }
                for (int ch = 0; ch < cs; ++ch)
                    w[ch][i] = (atras > 0.0 ? lee (ch, (double) escribe - atras) : w[ch][i]) * a;
                escribe = (escribe + 1) % largo;
            }
        }
    };
}

#pragma once

#include <JuceHeader.h>

// ============================================================================
//  EL ANALIZADOR DE ESPECTRO, UNA VEZ.
//
//  Esto vivia dentro de `EqCurve` y ahi era correcto mientras el unico cliente
//  fuera la curva grande. En cuanto los visores del plato pasaron a enseñar la
//  señal VIVA hubo un segundo -FLT y HPF dibujan respuesta en frecuencia, o
//  sea que su capa viva es un espectro- y copiarlo habria sido la misma regla
//  escrita dos veces: es la extraccion de `FxVisor::muestrea` otra vez, con
//  otra pieza y por lo mismo.
//
//  1024 a 48 kHz son 21 ms de ventana y bines de 47 Hz. Y no se baja a 256
//  aunque el visor del plato solo dibuje 48 columnas: el eje es LOGARITMICO,
//  asi que la primera decada -20 a 200 Hz- se lleva un tercio del ancho, y con
//  bines de 187 Hz ese tercio seria UN bin. La resolucion que hace falta la
//  manda la parte baja del eje y no cuantas columnas se pinten.
// ============================================================================
class Analizador
{
public:
    static constexpr int   kFft   = 1024;
    static constexpr int   kBines = kFft / 2;
    //  El suelo, en dB. Y las memorias arrancan AHI y no a cero, que cero es
    //  fondo de escala: a cero el analizador abre con las curvas pegadas al
    //  techo y bajando, que se lee como que la maquina esta saturando. Es el
    //  mismo fallo que el cero de `padAncho` — un valor por defecto que ademas
    //  es un valor valido.
    static constexpr float kPiso  = -78.0f;

    //  -33 / ln (0.75), que es el 0.25 por cuadro de siempre resuelto a los
    //  treinta cuadros por segundo contra los que se escribio. En MILISEGUNDOS
    //  porque el dibujo cuelga del vblank: por cuadro serian 115 ms a treinta y
    //  29 a ciento veinte, o sea un analizador que se lee distinto segun el
    //  panel.
    static constexpr double kTauCaidaMs = 115.0;

    Analizador() { suave.fill (kPiso); }

    using Bines = std::array<float, kBines>;

    //  Una ventana de Hann y la FFT. La ventana no es un adorno: sin ella el
    //  corte de los extremos mete faldones en TODOS los bines y el analizador
    //  sale con un suelo plano que no es el de la señal.
    //
    //  Y ATAQUE INSTANTANEO CON CAIDA LENTA, que es lo que hace legible un
    //  analizador: sin la caida lenta el dibujo tiembla treinta veces por
    //  segundo y no se puede leer un pico; sin el ataque rapido, un golpe de
    //  caja no llega a verse. Es el mismo par que gobierna un medidor.
    void analiza (const float* datos, int n, double dtMs) noexcept
    {
        if (datos == nullptr || n < kFft) return;
        const float k = 1.0f - (float) std::exp (-dtMs / kTauCaidaMs);

        for (int i = 0; i < kFft; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi
                                                     * (float) i / (float) (kFft - 1));
            buf[(size_t) i] = datos[n - kFft + i] * w;
        }
        std::fill (buf.begin() + kFft, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());

        for (int b = 0; b < kBines; ++b)
        {
            const float mag = buf[(size_t) b] * (2.0f / (float) kFft);
            const float dB  = juce::jmax (kPiso, juce::Decibels::gainToDecibels (mag, kPiso));
            float& s = suave[(size_t) b];
            s = (dB > s) ? dB : s + k * (dB - s);
        }
    }

    const Bines& bines() const noexcept { return suave; }

    //  Los dB de la banda que cae en `hz`, con el bin mas cercano. Lo que un
    //  dibujo de 48 columnas necesita y no los 512 bines: la interpolacion
    //  entre bines no dice nada que un pixel pueda enseñar.
    float enHz (float hz, double fs) const noexcept
    {
        const int b = juce::jlimit (0, kBines - 1,
                                    (int) std::lround ((double) hz * (double) kFft / juce::jmax (1.0, fs)));
        return suave[(size_t) b];
    }

private:
    juce::dsp::FFT fft { 10 };                       // 2^10 = 1024
    std::array<float, 2 * kFft> buf {};
    Bines suave {};
};

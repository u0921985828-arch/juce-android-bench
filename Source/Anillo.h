#pragma once
#include <JuceHeader.h>

//  rendimiento#1: retardo entero con tamano potencia de dos; mascara en vez de modulo.
//  Lee x[n-d] igual que DelayLine<None>, bit a bit, gastando algo mas de memoria.
struct Anillo
{
    void prepara (int canales, int maxRetardo)
    {
        maximo = juce::jmax (1, maxRetardo);
        tam    = juce::nextPowerOfTwo (maximo + 1);
        mask   = tam - 1;
        buf.setSize (juce::jmax (1, canales), tam);
        reset();
    }
    void reset() noexcept { buf.clear(); w = 0; }
    int maximoRetardo() const noexcept { return maximo; }
    float* canal (int ch) noexcept { return buf.getWritePointer (ch); }
    int posicion() const noexcept { return w; }
    int mascara() const noexcept { return mask; }
    void avanza() noexcept { w = (w - 1) & mask; }

private:
    juce::AudioBuffer<float> buf;
    int maximo = 1, tam = 2, mask = 1, w = 0;
};

//  rendimiento#1: DelayLine<Lagrange3rd> con la misma cuenta, sin modulo en cada muestra.
//  Mismo tamano y mismas operaciones en el mismo orden: sale bit a bit lo mismo.
class LineaLagrange
{
public:
    explicit LineaLagrange (int maxRetardo = 0) { total = juce::jmax (4, maxRetardo + 2); }

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        canales = juce::jmax (1, (int) spec.numChannels);
        buf.setSize (canales, total, false, false, true);
        reset();
    }
    void setMaximumDelayInSamples (int maxRetardo)
    {
        total = juce::jmax (4, maxRetardo + 2);
        buf.setSize (canales, total, false, false, true);
        reset();
    }
    int getMaximumDelayInSamples() const noexcept { return total - 2; }
    void reset() noexcept
    {
        escritura.fill (0); lectura.fill (0);
        buf.clear();
    }

    void setDelay (float nuevo) noexcept
    {
        const auto tope = (float) getMaximumDelayInSamples();
        retardo = juce::jlimit (0.0f, tope, nuevo);
        entero  = static_cast<int> (std::floor (retardo));
        frac    = retardo - (float) entero;
        if (frac < 2.0f && entero >= 1) { frac++; entero--; }
    }

    void pushSample (int ch, float x) noexcept
    {
        auto& w = escritura[(size_t) ch];
        buf.getWritePointer (ch)[w] = x;
        w = (w == 0) ? total - 1 : w - 1;
    }

    float popSample (int ch, float d = -1.0f, bool avanza = true) noexcept
    {
        if (d >= 0) setDelay (d);
        auto& r = lectura[(size_t) ch];
        int i1 = r + entero, i2 = i1 + 1, i3 = i2 + 1, i4 = i3 + 1;
        if (i4 >= total)
        {
            if (i1 >= total) i1 -= total;
            if (i2 >= total) i2 -= total;
            if (i3 >= total) i3 -= total;
            i4 -= total;
        }
        const float* s = buf.getReadPointer (ch);
        const float v1 = s[i1], v2 = s[i2], v3 = s[i3], v4 = s[i4];
        const float d1 = frac - 1.f, d2 = frac - 2.f, d3 = frac - 3.f;
        const float c1 = -d1 * d2 * d3 / 6.f;
        const float c2 = d2 * d3 * 0.5f;
        const float c3 = -d1 * d3 * 0.5f;
        const float c4 = d1 * d2 / 6.f;
        if (avanza) r = (r == 0) ? total - 1 : r - 1;
        return v1 * c1 + frac * (v2 * c2 + v3 * c3 + v4 * c4);
    }

private:
    static constexpr int kMaxCanales = 2;
    juce::AudioBuffer<float> buf;
    std::array<int, kMaxCanales> escritura {}, lectura {};
    int total = 4, canales = 1, entero = 0;
    float retardo = 0.0f, frac = 0.0f;
};

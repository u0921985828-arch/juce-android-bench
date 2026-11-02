#pragma once

#include <JuceHeader.h>
#include "Analogico.h"
#include "Accesible.h"
#include "Lang.h"
#include "ZatiLookAndFeel.h"

// ============================================================================
//  LA CARA DEL GEQ: siete faders verticales en el plato, como el EQ trae su
//  curva. Del telefono: «un ecualizador de faders mitico», y que en el hueco
//  del parametrico «se vean los decibelios y lo que toca». Por eso siete y no
//  diez: en 280 px de ancho diez faders son 24 px y no cabe ni el numero.
//
//  Cada columna dice arriba sus dB y abajo su frecuencia. Arrastrar en
//  vertical mueve la banda de la columna donde empezo el dedo (no la de al
//  lado si se tuerce), y el doble toque la vuelve a cero.
// ============================================================================
class GeqFaders : public juce::Component
{
public:
    static constexpr int kBandas = Analogico::Geq7::kBandas;

    std::function<void (int, float)> onBanda;     // banda, dB
    std::function<float (int)>       valor;       // lo que hay ahora en el motor
    std::function<void (bool)>       onTouch;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        acc.cuantas = [] { return kBandas * 25; };
        acc.dice = [this] (int i)
        {
            const int b = i / 25;
            return T ("Banda %1, %2 Hz: %3 dB", juce::String (b + 1),
                      juce::String (juce::roundToInt (Analogico::Geq7::kHz[b])))
                       .replace ("%3", juce::String (i % 25 - 12));
        };
        acc.pulsa = [this] (int i) { if (onBanda) onBanda (i / 25, (float) (i % 25 - 12)); repaint(); };
        return Accesible::crea (*this, acc);
    }

    GeqFaders() { setWantsKeyboardFocus (false); setOpaque (true); }

    //  La columna `b`, en coordenadas del componente. La usa el banco.
    juce::Rectangle<float> columna (int b) const
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        const float w = r.getWidth() / (float) kBandas;
        return { r.getX() + w * (float) b, r.getY(), w, r.getHeight() };
    }
    //  El tramo por el que corre el fader de la columna (sin los dos rotulos).
    juce::Rectangle<float> carril (int b) const
    {
        auto c = columna (b);
        c.removeFromTop (rotuloAlto());
        c.removeFromBottom (rotuloAlto());
        return c.reduced (0.0f, 6.0f);
    }
    static float rotuloAlto() noexcept { return (float) Metrics::fTiny + 6.0f; }

    float yDe (int b, float dB) const
    {
        const auto c = carril (b);
        return juce::jmap (juce::jlimit (-12.0f, 12.0f, dB), -12.0f, 12.0f, c.getBottom(), c.getY());
    }
    float dBDe (int b, float y) const
    {
        const auto c = carril (b);
        const float v = juce::jmap (y, c.getBottom(), c.getY(), -12.0f, 12.0f);
        return juce::jlimit (-12.0f, 12.0f, std::round (v * 2.0f) / 2.0f);   // medio dB por paso
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (ZatiColours::plate);
        auto r = getLocalBounds().toFloat();
        if (r.isEmpty()) return;
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, Metrics::radio);

        const auto letra = ZatiColours::monoFont (Metrics::fTiny, false);
        for (int b = 0; b < kBandas; ++b)
        {
            const auto col = columna (b);
            const auto ca  = carril (b);
            const float dB = valor ? valor (b) : 0.0f;

            //  La linea del cero, de punta a punta de la columna, y la ranura del fader.
            g.setColour (ZatiColours::lcdFg.withAlpha (0.18f));
            g.fillRect (col.getX() + 2.0f, yDe (b, 0.0f) - 0.5f, col.getWidth() - 4.0f, 1.0f);
            const float x = col.getCentreX();
            g.setColour (ZatiColours::lcdFg.withAlpha (0.30f));
            g.fillRoundedRectangle (x - 2.0f, ca.getY(), 4.0f, ca.getHeight(), 2.0f);

            //  Lo que sube o baja, del cero al pomo, y el pomo.
            const float y = yDe (b, dB), y0 = yDe (b, 0.0f);
            g.setColour (ZatiColours::amber.withAlpha (0.85f));
            g.fillRect (x - 2.0f, juce::jmin (y, y0), 4.0f, std::abs (y - y0));
            const float pw = juce::jmin (col.getWidth() - 8.0f, 30.0f), ph = 12.0f;
            g.setColour (b == tocada ? ZatiColours::amber : ZatiColours::lcdFg);
            g.fillRoundedRectangle (x - pw * 0.5f, y - ph * 0.5f, pw, ph, 3.0f);
            g.setColour (ZatiColours::screenBg);
            g.fillRect (x - pw * 0.5f + 4.0f, y - 0.5f, pw - 8.0f, 1.0f);

            //  Arriba los dB, abajo la frecuencia.
            g.setFont (letra);
            g.setColour (ZatiColours::lcdFg.withAlpha (b == tocada ? 1.0f : 0.80f));
            const juce::String txt = (dB > 0.04f ? "+" : "") + juce::String (dB, std::abs (dB - std::round (dB)) > 0.01f ? 1 : 0);
            g.drawText (Lang::ltr (txt), col.withHeight (rotuloAlto()), juce::Justification::centred, false);
            g.setColour (ZatiColours::lcdFg.withAlpha (0.70f));
            g.drawText (Lang::ltr (nombreHz (b)), col.withTrimmedTop (col.getHeight() - rotuloAlto()),
                        juce::Justification::centred, false);
        }
    }

    static juce::String nombreHz (int b)
    {
        const float hz = Analogico::Geq7::kHz[juce::jlimit (0, kBandas - 1, b)];
        return hz >= 1000.0f ? juce::String (hz / 1000.0f, hz < 10000.0f && std::fmod (hz, 1000.0f) > 1.0f ? 1 : 0) + "k"
                             : juce::String ((int) hz);
    }

    int bandaEn (float x) const
    {
        for (int b = 0; b < kBandas; ++b)
            if (x < columna (b).getRight()) return b;
        return kBandas - 1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        tocada = bandaEn (e.position.x);
        if (onTouch) onTouch (true);
        mueve (e.position.y);
    }
    void mouseDrag (const juce::MouseEvent& e) override { mueve (e.position.y); }
    void mouseUp (const juce::MouseEvent&) override
    {
        tocada = -1;
        if (onTouch) onTouch (false);
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = bandaEn (e.position.x);
        if (onBanda) onBanda (b, 0.0f);
        repaint();
    }

private:
    void mueve (float y)
    {
        if (tocada < 0) return;
        if (onBanda) onBanda (tocada, dBDe (tocada, y));
        repaint();
    }

    int tocada = -1;
    Accesible::Cursor acc;   // accesibilidad#0
};

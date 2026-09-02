#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Eq5.h"
#include "Lang.h"

// ============================================================================
//  LA CURVA DEL EQ, Y LA PRIMERA CARA PROPIA DE UN EFECTO.
//
//  Hasta aqui un efecto se tocaba con los tres mandos de la cara, que son de
//  TODOS: CTRL 1, CTRL 2 y MIX. Con dos mandos libres un ecualizador de cinco
//  bandas no cabe -hacen falta diez numeros- asi que este es el efecto que
//  obliga a que un efecto pueda traer su propia superficie y ponerla en el
//  plato donde viven los tres.
//
//
//  UNA CURVA Y NO CINCO FADERES, y es una MEDIDA y no un gusto.
//
//  El plato mide 86 px de alto en las siete pantallas -de 232 a 752 de ancho- y
//  el mando que hay dentro tiene 52 px. Cinco faderes verticales en 52 px no
//  son un recorrido: son un dedo apoyado, y un deslizador que se ajusta
//  ARRASTRANDO es el que menos puede permitirse ser corto -fallar el agarre no
//  es fallar un toque, es mover otra cosa-. Con una curva cada banda da sus DOS
//  numeros en un solo gesto: la ganancia en Y y la frecuencia en X.
//
//  Y es ademas el idioma que esta app ya tiene en cuatro sitios -la rejilla de
//  pasos, el piano, la linea de tiempo y el pad XY-: un LIENZO que se pinta
//  entero y se acierta con el dedo, no una fila de componentes.
//
//
//  SE COGE EL NODO MAS CERCANO, PERO SOLO DENTRO DE UN DEDO. Es la misma regla
//  que `WaveformDisplay` usa con las asas de recorte. A cinco bandas en la
//  pantalla mas estrecha les tocan 48 px de separacion, o sea por encima del
//  dedo minimo; el limite existe para que un toque en el aire no arrastre la
//  banda del otro extremo. Roto a proposito -sin el limite- un arrastre de
//  arriba abajo por el centro se lleva una banda a +12.00 dB.
//
//
//  Y NO GUARDA NADA. Como el pad XY: es una superficie, y quien tiene los diez
//  numeros es el `Eq5` del motor. Aqui solo se lee para pintar y se avisa de lo
//  que el dedo pide - realimentar un valor propio es como se llega a un mando
//  que se mueve solo.
// ============================================================================
class EqCurve : public juce::Component
{
public:
    EqCurve()
    {
        setWantsKeyboardFocus (false);
        setOpaque (false);
    }

    //  banda, frecuencia en Hz, ganancia en dB. Lo escribe quien lo reciba.
    std::function<void (int, float, float)> onBanda;
    std::function<void (bool)>              onTouch;

    //  De donde se lee lo que se pinta. Un puntero y no una copia: la curva que
    //  se dibuja tiene que ser la que SUENA, y una copia se queda vieja en
    //  cuanto el hilo de audio recalcula.
    void setFuente (const Eq5* e) { fuente = e; repaint(); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (r.isEmpty() || fuente == nullptr) return;

        //  El cristal, como el del espectro y el de la rejilla: la curva es un
        //  instrumento de lectura y se lee sobre fondo hundido.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, 3.0f);

        auto dentro = r.reduced (2.0f);
        const float w = dentro.getWidth(), h = dentro.getHeight();
        if (w < 8.0f || h < 8.0f) return;

        //  La linea de cero, que es lo que convierte una curva en una MEDIDA:
        //  sin ella no se sabe si lo que se ve sube o baja.
        const float y0 = dentro.getCentreY();
        g.setColour (ZatiColours::lcdFg.withAlpha (0.22f));
        g.drawHorizontalLine ((int) y0, dentro.getX(), dentro.getRight());

        //  Y las decadas, que es como se lee una escala logaritmica: sin las
        //  marcas, 1 kHz y 10 kHz caen en sitios que no dicen nada.
        for (float hz : { 100.0f, 1000.0f, 10000.0f })
        {
            const float x = dentro.getX() + xDe (hz) * w;
            g.setColour (ZatiColours::lcdFg.withAlpha (0.10f));
            g.drawVerticalLine ((int) x, dentro.getY(), dentro.getBottom());
        }

        //  LA CURVA, evaluada en el filtro de verdad y no aproximada. Un punto
        //  por pixel: mas no se ve y menos hace escalones en la pendiente de un
        //  estante.
        juce::Path p;
        const int n = juce::jmax (2, (int) w);
        for (int i = 0; i < n; ++i)
        {
            const float t  = (float) i / (float) (n - 1);
            const float hz = hzDe (t);
            const float dB = fuente->respuestaEnDb (hz);
            const float y  = y0 - (dB / Eq5::kGainMax) * (h * 0.5f) * 0.92f;
            const float x  = dentro.getX() + t * w;
            if (i == 0) p.startNewSubPath (x, y);
            else        p.lineTo (x, y);
        }
        g.setColour (ZatiColours::accent);
        g.strokePath (p, juce::PathStrokeType (1.8f));

        //  Y LOS CINCO NODOS. El que se esta arrastrando lleva anillo: sin el,
        //  con dos bandas juntas no se sabe cual se movio.
        for (int b = 0; b < Eq5::kBands; ++b)
        {
            const auto c = centroDe (b, dentro);
            const bool viva = (b == agarrada);
            g.setColour (ZatiColours::accent.withAlpha (viva ? 1.0f : 0.75f));
            g.fillEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
            if (viva)
            {
                g.setColour (ZatiColours::accent);
                g.drawEllipse (c.x - 8.0f, c.y - 8.0f, 16.0f, 16.0f, 1.5f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        agarrada = masCercana (e.position);
        if (onTouch) onTouch (agarrada >= 0);
        mueve (e);
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override { mueve (e); }
    void mouseUp (const juce::MouseEvent&) override
    {
        agarrada = -1;
        if (onTouch) onTouch (false);
        repaint();
    }

private:
    //  El eje X es LOGARITMICO, que es como se oye: repartido lineal, la mitad
    //  del ancho se gasta entre 10 y 20 kHz -donde casi no pasa nada- y todo lo
    //  que importa cae en el primer centimetro. Es la misma cuenta que ya hace
    //  el barrido de FLT.
    static float xDe (float hz) noexcept
    {
        const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
        return juce::jlimit (0.0f, 1.0f, (std::log (juce::jmax (1.0f, hz)) - lo) / (hi - lo));
    }
    static float hzDe (float t) noexcept
    {
        const float lo = std::log (Eq5::kFreqMin), hi = std::log (Eq5::kFreqMax);
        return std::exp (lo + juce::jlimit (0.0f, 1.0f, t) * (hi - lo));
    }

    juce::Point<float> centroDe (int b, juce::Rectangle<float> dentro) const
    {
        const float y0 = dentro.getCentreY();
        const float x  = dentro.getX() + xDe (fuente->freqDe (b)) * dentro.getWidth();
        const float y  = y0 - (fuente->gainDe (b) / Eq5::kGainMax)
                                * (dentro.getHeight() * 0.5f) * 0.92f;
        return { x, y };
    }

    int masCercana (juce::Point<float> p) const
    {
        if (fuente == nullptr) return -1;
        auto dentro = getLocalBounds().toFloat().reduced (2.0f);
        if (dentro.getWidth() < 8.0f) return -1;

        int mejor = -1; float mejorD = 1.0e9f;
        for (int b = 0; b < Eq5::kBands; ++b)
        {
            const float d = centroDe (b, dentro).getDistanceFrom (p);
            if (d < mejorD) { mejorD = d; mejor = b; }
        }
        //  Dentro de un dedo, como las asas del recorte. Sin el limite, un
        //  toque en el aire arrastraria la banda del otro extremo.
        return mejorD <= (float) Metrics::hit ? mejor : -1;
    }

    void mueve (const juce::MouseEvent& e)
    {
        if (agarrada < 0 || fuente == nullptr || ! onBanda) return;
        auto dentro = getLocalBounds().toFloat().reduced (2.0f);
        if (dentro.getWidth() < 8.0f || dentro.getHeight() < 8.0f) return;

        const float t  = (e.position.x - dentro.getX()) / dentro.getWidth();
        //  Y ACOTADA CONTRA SUS VECINAS, que no es cosa de esta cara: la regla
        //  de que dos bandas no se cruzan es del filtro y vive en Eq5. Escrita
        //  aqui tambien serian dos reglas, y un nodo que salta al otro lado del
        //  vecino no se puede arrastrar.
        const float hz = juce::jlimit (fuente->minDe (agarrada), fuente->maxDe (agarrada),
                                       hzDe (t));

        const float y0 = dentro.getCentreY();
        const float dB = juce::jlimit (-Eq5::kGainMax, Eq5::kGainMax,
                                       (y0 - e.position.y) / ((dentro.getHeight() * 0.5f) * 0.92f)
                                         * Eq5::kGainMax);

        onBanda (agarrada, hz, dB);
        repaint();
    }

    const Eq5* fuente = nullptr;
    int agarrada = -1;
};

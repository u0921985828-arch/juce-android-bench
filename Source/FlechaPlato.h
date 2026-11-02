#pragma once
#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"

//  plato#paginas: la flecha a cada lado del titulo del plato. Del telefono: «unas flechas a cada lado del texto, en el
//  color que pida la skin». Un chevron de trazo, del acento de la carcasa puesta, dentro de una caja de un dedo.
class FlechaPlato : public juce::Button
{
public:
    explicit FlechaPlato (bool derecha) : juce::Button (derecha ? "plato-siguiente" : "plato-anterior"), aDerecha (derecha)
    {
        setWantsKeyboardFocus (false);
    }

    void paintButton (juce::Graphics& g, bool encima, bool abajo) override
    {
        //  plato#paginas: del telefono, «que ocupen literal el mismo espacio que los caracteres, desde la parte mas baja a
        //  la mas alta» de las mayusculas: el chevron va de `capArriba` (lo alto de una E) a `capAbajo` (la linea base),
        //  que pone la maqueta con la misma cuenta con la que la costura dibuja el titulo.
        const auto r = getLocalBounds().toFloat();
        const float y0 = capAbajo > capArriba ? capArriba : r.getCentreY() - 5.0f;
        const float y1 = capAbajo > capArriba ? capAbajo  : r.getCentreY() + 5.0f;
        const float grosor = 2.0f, medio = grosor * 0.5f;     // el trazo no se sale de las dos lineas
        const float alto = (y1 - y0) - grosor;
        const float ancho = alto * 0.55f;
        const float cx = r.getCentreX() + (abajo ? (aDerecha ? 1.0f : -1.0f) : 0.0f);
        const float d = aDerecha ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (cx - d * ancho * 0.5f, y0 + medio);
        p.lineTo (cx + d * ancho * 0.5f, y0 + medio + alto * 0.5f);
        p.lineTo (cx - d * ancho * 0.5f, y0 + medio + alto);
        g.setColour (ZatiColours::accent.withAlpha (isEnabled() ? (encima || abajo ? 1.0f : 0.90f) : 0.25f));
        g.strokePath (p, juce::PathStrokeType (grosor, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    float capArriba = 0.0f, capAbajo = 0.0f;   // en coordenadas de la flecha

private:
    bool aDerecha;
};

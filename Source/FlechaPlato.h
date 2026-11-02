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
        const auto r = getLocalBounds().toFloat();
        //  plato#paginas: del telefono, «las flechas deben ocupar el mismo alto que el texto»: el chevron mide la altura de
        //  las mayusculas del titulo (`altoLetra`, la pone la maqueta) y no una fraccion de la caja.
        const float lado = 0.5f * (altoLetra > 0.0f ? altoLetra : juce::jmin (r.getWidth(), r.getHeight()) * 0.4f);
        const float cx = r.getCentreX() + (abajo ? (aDerecha ? 1.0f : -1.0f) : 0.0f), cy = r.getCentreY();
        const float d = aDerecha ? 1.0f : -1.0f;
        juce::Path p;
        p.startNewSubPath (cx - d * lado * 0.45f, cy - lado);
        p.lineTo (cx + d * lado * 0.55f, cy);
        p.lineTo (cx - d * lado * 0.45f, cy + lado);
        g.setColour (ZatiColours::accent.withAlpha (isEnabled() ? (encima || abajo ? 1.0f : 0.90f) : 0.25f));
        g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    float altoLetra = 0.0f;   // alto de las mayusculas del titulo, en px

private:
    bool aDerecha;
};

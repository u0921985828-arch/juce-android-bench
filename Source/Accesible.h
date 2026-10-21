#pragma once

#include <JuceHeader.h>
#include <functional>

//  accesibilidad#0: UN LIENZO QUE TALKBACK RECORRE CON UN CURSOR.
//
//  Las rejillas, el piano, la cancion, el XY, la curva del EQ y el teclado se
//  pintan enteros y no tienen hijos, asi que JUCE les daba un manejador
//  `unspecified` sin titulo ni acciones: una caja muda. Aqui cada uno se
//  presenta como un mando ajustable: deslizar arriba o abajo mueve un cursor
//  por sus posiciones -y se anuncia lo que hay debajo- y el toque doble hace
//  lo que haria el dedo en esa posicion.
namespace Accesible
{
    struct Cursor
    {
        std::function<int()>                cuantas;   // posiciones
        std::function<juce::String (int)>   dice;      // lo que se anuncia en una
        std::function<void (int)>           pulsa;     // el toque doble
        int pos = 0;

        int total() const { return cuantas ? juce::jmax (1, cuantas()) : 1; }
        int donde() const { return juce::jlimit (0, total() - 1, pos); }
    };

    class Valor : public juce::AccessibilityValueInterface
    {
    public:
        Valor (juce::Component& c, Cursor& k) : comp (c), cur (k) {}
        bool isReadOnly() const override { return false; }
        double getCurrentValue() const override { return (double) cur.donde(); }
        juce::String getCurrentValueAsString() const override { return cur.dice ? cur.dice (cur.donde()) : juce::String(); }
        void setValue (double v) override
        {
            cur.pos = juce::jlimit (0, cur.total() - 1, juce::roundToInt (v));
            if (auto* h = comp.getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        }
        void setValueAsString (const juce::String& s) override { setValue (s.getDoubleValue()); }
        AccessibleValueRange getRange() const override { return { { 0.0, (double) (cur.total() - 1) }, 1.0 }; }
    private:
        juce::Component& comp;
        Cursor& cur;
    };

    inline std::unique_ptr<juce::AccessibilityHandler> crea (juce::Component& c, Cursor& cur)
    {
        auto pulsa = [&c, &cur]
        {
            if (cur.pulsa) cur.pulsa (cur.donde());
            if (auto* h = c.getAccessibilityHandler()) h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
        };
        return std::make_unique<juce::AccessibilityHandler> (c, juce::AccessibilityRole::slider,
                   juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, pulsa),
                   juce::AccessibilityHandler::Interfaces { std::make_unique<Valor> (c, cur) });
    }
}

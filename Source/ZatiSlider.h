#pragma once
#include <JuceHeader.h>

//  Un deslizador que TalkBack recorre en 50 gestos y no en mil (accesibilidad#20).
//  El paso fino del arrastre no cambia: solo lo que mueve un gesto del lector.
class ZatiSlider : public juce::Slider
{
public:
    //  Con teclado fisico el mando se alcanza con Tab y se mueve con flechas (accesibilidad#7).
    template <typename... A> explicit ZatiSlider (A&&... a) : juce::Slider (std::forward<A> (a)...) { setWantsKeyboardFocus (true); }

    static constexpr int kGestos = 50;

    //  Un toque corto sin arrastre mueve un paso hacia donde se toca (accesibilidad#19, WCAG 2.5.7).
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp (e);
        const auto estilo = getSliderStyle();
        if (! isEnabled() || e.mouseWasDraggedSinceMouseDown() || e.getNumberOfClicks() > 1
            || e.getLengthOfMousePress() > 350 || estilo == IncDecButtons || isTwoValue() || isThreeValue()
            || ! getLocalBounds().contains (e.getPosition()))
            return;
        bool sube;
        if (isRotary())           sube = e.position.x > (float) getWidth() * 0.5f;
        else if (isHorizontal())  sube = e.position.x > (float) getPositionOfValue (getValue());
        else                      sube = e.position.y < (float) getPositionOfValue (getValue());
        ScopedDragNotification arrastre (*this);
        setValue (getValue() + (sube ? 1.0 : -1.0) * pasoAccesible (*this), juce::sendNotificationSync);
    }

    static double pasoAccesible (const juce::Slider& s)
    {
        const double largo = s.getRange().getLength();
        const double fino  = s.getInterval() > 0.0 ? s.getInterval() : largo * 0.01;
        if (fino <= 0.0 || largo / fino <= 100.0) return fino;
        return std::ceil (largo / kGestos / fino) * fino;
    }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        struct Valor final : juce::AccessibilityValueInterface
        {
            explicit Valor (juce::Slider& sl) : s (sl) {}
            bool isReadOnly() const override { return false; }
            double getCurrentValue() const override { return s.getValue(); }
            void setValue (double v) override
            {
                juce::Slider::ScopedDragNotification arrastre (s);
                s.setValue (v, juce::sendNotificationSync);
            }
            juce::String getCurrentValueAsString() const override { return s.getTextFromValue (s.getValue()); }
            void setValueAsString (const juce::String& t) override { setValue (s.getValueFromText (t)); }
            AccessibleValueRange getRange() const override
            { return { { s.getMinimum(), s.getMaximum() }, pasoAccesible (s) }; }
            juce::Slider& s;
        };
        struct Mando final : juce::AccessibilityHandler
        {
            explicit Mando (juce::Slider& sl)
                : juce::AccessibilityHandler (sl, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                              juce::AccessibilityHandler::Interfaces { std::make_unique<Valor> (sl) }),
                  s (sl) {}
            juce::String getHelp() const override { return s.getTooltip(); }
            juce::Slider& s;
        };
        return std::make_unique<Mando> (*this);
    }
};

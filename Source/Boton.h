#pragma once

#include <JuceHeader.h>
#include "Lang.h"

//  El boton de la casa: su estado se le dice al lector en el idioma de la app (accesibilidad#32).
class Boton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    { return std::make_unique<Lector> (*this); }

private:
    struct Valor final : juce::AccessibilityTextValueInterface
    {
        explicit Valor (juce::Button& b) : boton (b) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return boton.getToggleState() ? T ("Encendido") : T ("Apagado"); }
        void setValueAsString (const juce::String&) override {}
        juce::Button& boton;
    };

    struct Lector final : juce::AccessibilityHandler
    {
        explicit Lector (juce::Button& b)
            : juce::AccessibilityHandler (b, b.getRadioGroupId() != 0 ? juce::AccessibilityRole::radioButton : juce::AccessibilityRole::button,
                                          acciones (b), interfaces (b)),
              boton (b) {}

        juce::AccessibleState getCurrentState() const override
        {
            auto st = juce::AccessibilityHandler::getCurrentState();
            if (boton.isToggleable())
            {
                st = st.withCheckable();
                if (boton.getToggleState()) st = st.withChecked();
            }
            return st;
        }

        juce::String getTitle() const override
        {
            auto t = juce::AccessibilityHandler::getTitle();
            return t.isEmpty() ? boton.getButtonText() : t;
        }

        juce::String getHelp() const override { return boton.getTooltip(); }

        static juce::AccessibilityActions acciones (juce::Button& b)
        {
            auto a = juce::AccessibilityActions().addAction (juce::AccessibilityActionType::press, [&b] { b.triggerClick(); });
            if (b.isToggleable())
                a = a.addAction (juce::AccessibilityActionType::toggle, [&b]
                                 { if (b.isEnabled()) b.setToggleState (! b.getToggleState(), juce::sendNotification); });
            return a;
        }

        static Interfaces interfaces (juce::Button& b)
        {
            if (b.isToggleable()) return { std::make_unique<Valor> (b) };
            return {};
        }

        juce::Button& boton;
    };
};

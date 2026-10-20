#pragma once

#include <JuceHeader.h>

//  Un boton que se mantiene ofrece ese gesto al lector como accion propia (accesibilidad#3).
class AccionMantener : public juce::AccessibilityHandler
{
public:
    AccionMantener (juce::Button& b, std::function<void()>& mantener)
        : juce::AccessibilityHandler (b, juce::AccessibilityRole::button, acciones (b, mantener)),
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

private:
    static juce::AccessibilityActions acciones (juce::Button& b, std::function<void()>& mantener)
    {
        auto a = juce::AccessibilityActions()
                     .addAction (juce::AccessibilityActionType::press, [&b] { b.triggerClick(); })
                     .addAction (juce::AccessibilityActionType::showMenu, [&mantener] { if (mantener) mantener(); });
        if (b.isToggleable())
            a = a.addAction (juce::AccessibilityActionType::toggle, [&b]
                             { if (b.isEnabled()) b.setToggleState (! b.getToggleState(), juce::sendNotification); });
        return a;
    }

    juce::Button& boton;
};

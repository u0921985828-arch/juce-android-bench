#pragma once
#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"

//  Anillo del foco de teclado (accesibilidad#7, WCAG 2.4.7): solo cuando el
//  foco llega sin dedo, como :focus-visible; un toque no lo enciende.
class AnilloFoco : public juce::Component, public juce::FocusChangeListener
{
public:
    AnilloFoco() { setInterceptsMouseClicks (false, false); setAccessible (false); setWantsKeyboardFocus (false); }

    void sigue (juce::Component* c, bool dedoAbajo)
    {
        auto* padre = getParentComponent();
        objetivo = c;
        if (padre == nullptr || c == nullptr || dedoAbajo || c == this || ! c->isShowing()
            || ! padre->isParentOf (c) || c->getWidth() < 4 || c->getHeight() < 4)
        { setVisible (false); return; }
        setBounds (padre->getLocalArea (c, c->getLocalBounds()).expanded (grosor + 1));
        setVisible (true); toFront (false); repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (grosor * 0.5f);
        const float esquina = Metrics::radio + Metrics::filo;   // la del control, un punto mas fuera
        g.setColour (ZatiColours::screenBg); g.drawRoundedRectangle (r.expanded (Metrics::filo), esquina, Metrics::filo);
        g.setColour (ZatiColours::ink);      g.drawRoundedRectangle (r, esquina, (float) grosor);
    }

    void globalFocusChanged (juce::Component* c) override
    { sigue (c, juce::Desktop::getInstance().getNumDraggingMouseSources() > 0); }

    void vuelveASituar() { if (isVisible()) sigue (objetivo.getComponent(), false); }

    bool hitTest (int, int) override { return false; }

private:
    static constexpr int grosor = 2;
    juce::Component::SafePointer<juce::Component> objetivo;
};

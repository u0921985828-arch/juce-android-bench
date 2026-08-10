#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Lang.h"

//  UN PANEL XY, QUE ES LO QUE UN EFECTO NECESITA EN DIRECTO.
//
//  Los tres mandos de la cara sirven para AJUSTAR un efecto: los pones donde
//  quieres y ahi se quedan. Lo que no hacen es TOCARLO - un barrido de filtro
//  no se hace girando un mando con el pulgar mientras la otra mano dispara
//  pads, y menos aun dos parametros a la vez, que es exactamente lo que pide
//  un filtro (frecuencia y resonancia), un delay (tiempo y realimentacion) o
//  un crusher (bits y diezmado).
//
//  Asi que este componente no guarda nada. Es una superficie: el eje X es el
//  primer parametro del efecto y el eje Y el segundo, y lo unico que hace es
//  decir donde esta el dedo entre 0 y 1. Quien decide que significa eso es
//  MainComponent, que es quien conoce los rangos y los sesgos de cada efecto.
//
//  EL EJE Y VA AL REVES QUE LOS PIXELES. Arriba es 1. Lo escribo aqui porque
//  es la clase de detalle que se corrige tres veces: en pantalla la y crece
//  hacia abajo, y en un instrumento "mas" esta arriba.
class XyPad : public juce::Component
{
public:
    XyPad()
    {
        setWantsKeyboardFocus (false);
        setOpaque (false);
    }

    //  x e y llegan y salen en 0..1, con y=1 arriba.
    std::function<void (float, float)> onMove;
    std::function<void (bool)>         onTouch;      // true al apoyar, false al soltar

    void setAxisNames (juce::String xn, juce::String yn)
    {
        xName = std::move (xn); yName = std::move (yn); repaint();
    }
    void setAxisValues (juce::String xv, juce::String yv)
    {
        xValue = std::move (xv); yValue = std::move (yv); repaint();
    }
    //  Sin notificar: esto lo usa el dueno para reflejar el estado real de los
    //  parametros, y realimentarlo provocaria el bucle que mueve el mando solo.
    void setPosition (float x, float y)
    {
        px = juce::jlimit (0.0f, 1.0f, x);
        py = juce::jlimit (0.0f, 1.0f, y);
        repaint();
    }
    void setTouched (bool t) { if (touching != t) { touching = t; repaint(); } }
    bool isTouched() const noexcept { return touching; }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (r.isEmpty()) return;

        //  La superficie es una PANTALLA, no una tapa: es lo unico de esta app
        //  que se mira mientras se toca, igual que el LCD, y comparte su tinta
        //  para que se lea como el mismo material y no como un widget pegado.
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.2f);

        auto in = r.reduced (kInset);

        //  Reticula de cuartos. Cuatro por lado y no mas: la retícula esta
        //  para poder VOLVER a un sitio, no para leer un valor - el valor lo
        //  dicen los dos numeros de las esquinas.
        g.setColour (ZatiColours::lcdDim.withAlpha (0.30f));
        for (int i = 1; i < 4; ++i)
        {
            const float fx = in.getX() + in.getWidth()  * (float) i / 4.0f;
            const float fy = in.getY() + in.getHeight() * (float) i / 4.0f;
            g.drawLine (fx, in.getY(), fx, in.getBottom(), 1.0f);
            g.drawLine (in.getX(), fy, in.getRight(), fy, 1.0f);
        }
        g.setColour (ZatiColours::lcdDim.withAlpha (0.55f));
        g.drawRect (in, 1.0f);

        const float cx = in.getX() + in.getWidth()  * px;
        const float cy = in.getBottom() - in.getHeight() * py;

        //  La cruz cruza TODO el panel, no un circulito alrededor del dedo: el
        //  dedo tapa el punto en el que esta, y lo que hace falta ver es en que
        //  fila y en que columna te has quedado.
        g.setColour (ZatiColours::lcdFg.withAlpha (touching ? 0.55f : 0.30f));
        g.drawLine (in.getX(), cy, in.getRight(), cy, 1.0f);
        g.drawLine (cx, in.getY(), cx, in.getBottom(), 1.0f);

        const float rad = touching ? 9.0f : 6.0f;
        g.setColour (ZatiColours::accent);
        g.fillEllipse (cx - rad, cy - rad, rad * 2.0f, rad * 2.0f);
        g.setColour (ZatiColours::screenBg);
        g.fillEllipse (cx - rad * 0.42f, cy - rad * 0.42f, rad * 0.84f, rad * 0.84f);

        //  Los dos ejes, nombrados y con su cifra, en las esquinas donde no
        //  estorban a la mano: X abajo, que es la direccion en la que corre, e
        //  Y arriba a la izquierda, girado no - girar texto en cuatro idiomas
        //  con arabe dentro es como se pierde una tarde.
        g.setFont (ZatiColours::monoFont (Metrics::fMeta, true).withExtraKerningFactor (0.08f));
        g.setColour (ZatiColours::lcdDim);
        auto top = r.removeFromTop (kInset).toNearestInt().reduced (4, 0);
        g.drawText (yName + "  " + Lang::ltr (yValue), top, Lang::start());
        auto bot = r.removeFromBottom (kInset).toNearestInt().reduced (4, 0);
        g.drawText (xName + "  " + Lang::ltr (xValue), bot, Lang::start());
    }

    void mouseDown (const juce::MouseEvent& e) override { touching = true;  send (e); if (onTouch) onTouch (true); }
    void mouseDrag (const juce::MouseEvent& e) override { send (e); }
    void mouseUp   (const juce::MouseEvent&)   override { touching = false; repaint(); if (onTouch) onTouch (false); }

private:
    //  Los dos rotulos viven DENTRO del panel y hay que reservarles el alto, o
    //  la reticula se les mete debajo y el numero se lee sobre una linea.
    static constexpr float kInset = 16.0f;

    void send (const juce::MouseEvent& e)
    {
        auto in = getLocalBounds().toFloat().reduced (kInset);
        if (in.getWidth() <= 1.0f || in.getHeight() <= 1.0f) return;

        px = juce::jlimit (0.0f, 1.0f, (e.position.x - in.getX()) / in.getWidth());
        py = juce::jlimit (0.0f, 1.0f, (in.getBottom() - e.position.y) / in.getHeight());
        repaint();
        if (onMove) onMove (px, py);
    }

    juce::String xName { "X" }, yName { "Y" }, xValue, yValue;
    float px = 0.5f, py = 0.5f;
    bool  touching = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (XyPad)
};

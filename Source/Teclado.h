#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"

// ============================================================================
//  Teclado — una octava que se toca, para la ficha del instrumento.
//
//  POR QUE PINTADO Y NO TRECE TAPAS. Trece botones en una fila se reparten el
//  ancho, y en la pantalla mas estrecha de las siete eso son 21 px por tecla:
//  por debajo del dedo minimo y sin arreglo posible, porque trece por cuarenta
//  son 520 y la tarjeta mide 225. Un teclado NO es una fila de botones - es un
//  lienzo, como la rejilla de pasos y como el piano roll, y por eso se pinta y
//  se acierta con el raton, con las teclas negras ENCIMA de las blancas, que es
//  lo que hace que un teclado se lea como un teclado.
//
//  Y ARRASTRANDO, que es la otra mitad: un glissando es un solo gesto, y ademas
//  es el gesto con el que se prueba un preset. El arrastre es horizontal, asi
//  que no se pisa con el desplazamiento de la ficha - un Viewport de JUCE no
//  roba el arrastre de un hijo salvo que se le pida.
//
//  LO QUE SUENA ES EL PAD. Este control no sabe nada de sintesis: manda un
//  semitono y ya. Quien lo oye es postNoteOnAt, que es la puerta que existe
//  justo para esto - oir una nota sin AFINAR el pad, que es el fallo que ya
//  costo una medida en la audicion del piano roll.
// ============================================================================
class Teclado : public juce::Component
{
public:
    //  Trece y no doce: una octava y su raiz de arriba. Es la misma cuenta que
    //  hizo que el piano roll pasara de veinticinco filas a trece.
    static constexpr int kTeclas = 13;

    std::function<void (int semis)> onNota;

    void setBase (int semitonos) noexcept
    {
        base = juce::jlimit (-24, 12, semitonos);
        repaint();
    }
    int getBase() const noexcept { return base; }

    void setNotaViva (int semis) noexcept
    {
        if (viva == semis) return;
        viva = semis;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (r.getWidth() < 8.0f || r.getHeight() < 8.0f) return;

        //  Ocho blancas por octava y su raiz: las negras van encima y no
        //  ocupan sitio propio, que es como esta hecho un teclado de verdad.
        const float anchoB = r.getWidth() / 8.0f;
        const float altoN  = r.getHeight() * 0.62f;

        static const int blancas[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        static const int negras[5]  = { 1, 3, 6, 8, 10 };

        for (int i = 0; i < 8; ++i)
        {
            const auto k = juce::Rectangle<float> (r.getX() + (float) i * anchoB, r.getY(),
                                                   anchoB, r.getHeight()).reduced (0.8f, 0.0f);
            const bool on = (viva == base + blancas[i]);
            //  La tinta se MIDE contra la tapa, no se elige por su nombre: en
            //  GRAFITO y en LACA la tinta es clara y una tecla "blanca" pintada
            //  con blanco desapareceria.
            const auto cara = on ? ZatiColours::accent : ZatiColours::key;
            g.setColour (cara);
            g.fillRoundedRectangle (k, 2.0f);
            g.setColour (ZatiColours::groove (0.35f));
            g.drawRoundedRectangle (k, 2.0f, 1.0f);
        }

        for (int i = 0; i < 5; ++i)
        {
            //  La negra se apoya entre dos blancas: su centro es el borde.
            const int hueco = (i < 2) ? i + 1 : i + 2;     // 1,2  4,5,6
            const float cx = r.getX() + (float) hueco * anchoB;
            const auto k = juce::Rectangle<float> (cx - anchoB * 0.30f, r.getY(),
                                                   anchoB * 0.60f, altoN);
            const bool on = (viva == base + negras[i]);
            g.setColour (on ? ZatiColours::accent : ZatiColours::groove (0.85f));
            g.fillRoundedRectangle (k, 2.0f);
        }

        //  Y DONDE ESTA EL CERO, que sin eso una octava suelta no dice en que
        //  parte del rango esta. Es un punto y no un numero: un rotulo dentro
        //  de una tecla de 21 px no se lee en ninguna pantalla.
        if (base <= 0 && base + 12 >= 0)
        {
            int cual = 0;
            for (int i = 0; i < 8; ++i) if (blancas[i] == -base) cual = i;
            if (-base == blancas[cual])
            {
                const float cx = r.getX() + ((float) cual + 0.5f) * anchoB;
                g.setColour (ZatiColours::textOn (ZatiColours::key).withAlpha (0.55f));
                g.fillEllipse (cx - 2.0f, r.getBottom() - 8.0f, 4.0f, 4.0f);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { toca (e.position.x, e.position.y); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        //  Solo cuando CAMBIA de tecla: sin esto un arrastre de veinte pixeles
        //  dispara veinte veces la misma nota y la cola de comandos se llena de
        //  golpes que nadie pidio.
        const int n = notaEn (e.position.x, e.position.y);
        if (n != juce::jmax (-100, viva)) toca (e.position.x, e.position.y);
    }
    void mouseUp (const juce::MouseEvent&) override { setNotaViva (-100); }

private:
    int base = 0;
    int viva = -100;

    int notaEn (float x, float y) const noexcept
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        if (r.getWidth() < 8.0f) return -100;
        const float anchoB = r.getWidth() / 8.0f;
        const float altoN  = r.getHeight() * 0.62f;

        static const int blancas[8] = { 0, 2, 4, 5, 7, 9, 11, 12 };
        static const int negras[5]  = { 1, 3, 6, 8, 10 };

        //  LAS NEGRAS PRIMERO, que estan encima. Preguntar por las blancas
        //  antes seria un teclado en el que las negras no se pueden tocar.
        if (y - r.getY() < altoN)
            for (int i = 0; i < 5; ++i)
            {
                const int hueco = (i < 2) ? i + 1 : i + 2;
                const float cx = r.getX() + (float) hueco * anchoB;
                if (std::abs (x - cx) <= anchoB * 0.30f) return base + negras[i];
            }

        const int i = juce::jlimit (0, 7, (int) ((x - r.getX()) / anchoB));
        return base + blancas[i];
    }

    void toca (float x, float y)
    {
        const int n = notaEn (x, y);
        if (n == -100) return;
        setNotaViva (n);
        if (onNota) onNota (n);
    }
};

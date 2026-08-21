#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ZatiLookAndFeel.h"
#include "Lang.h"
#include <vector>

// ============================================================================
//  LA VISTA PREVIA DEL TROCEADO, que era la unica accion grande a ciegas.
//
//  AUTO CHOP sobrescribe hasta DIECISEIS pads y hasta ahora la ficha solo pedia
//  un numero y un modo: no enseñaba donde caian los cortes. Es la accion mas
//  destructiva de la app despues de BORRA TODO, y la unica de su tamano que se
//  hacia sin ver el resultado - EXPORTAR enseña el destino, CARGAR KIT confirma
//  el banco, y esta no enseñaba nada.
//
//  Aqui se ve la onda con una marca por corte, y las marcas se tocan:
//
//    - tocar entre dos marcas ANADE una donde se toco,
//    - tocar una marca y soltar sin mover la QUITA,
//    - arrastrarla la MUEVE.
//
//  Quitar y mover comparten el mismo gesto inicial y se separan al soltar, por
//  la distancia recorrida: un dedo que no se ha movido queria quitar. La
//  alternativa -dos zonas distintas en una marca de tres pixeles- no se puede
//  acertar en un telefono.
//
//  Y LA PRIMERA MARCA NO SE TOCA. El corte cero es el principio de la muestra:
//  no es un corte que alguien haya puesto, es donde empieza. Moverlo o quitarlo
//  dejaria el primer trozo empezando en el segundo golpe y la cabeza del sonido
//  fuera de todos los pads, sin nada que lo explique.
// ============================================================================
class ChopPreview : public juce::Component
{
public:
    //  Se avisa al dueno y NO se toca la lista aqui: los cortes son suyos -los
    //  usa applyAutoChop- y dos copias de la misma lista son dos sitios donde
    //  una se queda vieja. Este componente dibuja y traduce toques a muestras.
    std::function<void (int idx, int muestra)> onMueve;
    std::function<void (int muestra)>          onAnade;
    std::function<void (int idx)>              onQuita;

    void setFuente (SampleBuffer::Ptr sb)
    {
        if (fuente != sb) { fuente = sb; recalcula(); }
        repaint();
    }

    void setCortes (const std::vector<int>& c) { cortes = c; repaint(); }

    //  Lo que un trozo no puede bajar: por debajo de esto no es un corte, es un
    //  click. Es el mismo numero que el recorte minimo de un pad.
    static constexpr int kMinMuestras = 256;

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (ZatiColours::screenBg);
        g.fillRoundedRectangle (r, 3.0f);

        const int len = largo();
        if (len < 2 || picos.empty())
        {
            g.setColour (ZatiColours::inkDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, false));
            g.drawText (T ("sin muestra"), getLocalBounds(), juce::Justification::centred);
            return;
        }

        //  La onda, en columnas de minimo y maximo como la del pad: dibujar
        //  muestra a muestra a esta anchura es pintar lo mismo cien veces.
        const auto caja = getLocalBounds().reduced (2);
        const float mitad = (float) caja.getCentreY();
        const float alto  = (float) caja.getHeight() * 0.5f - 1.0f;
        g.setColour (ZatiColours::lcdFg.withAlpha (0.55f));
        for (int x = 0; x < (int) picos.size(); ++x)
        {
            const auto& p = picos[(size_t) x];
            const float y0 = mitad - p.max * alto;
            const float y1 = mitad - p.min * alto;
            g.drawLine ((float) (caja.getX() + x), y0, (float) (caja.getX() + x), y1, 1.0f);
        }

        //  Y las marcas. La cero se dibuja mas apagada porque no se puede
        //  tocar: un control que parece igual que sus vecinos y no responde
        //  como ellos se lee como un fallo.
        for (int i = 0; i < (int) cortes.size(); ++i)
        {
            const float x = (float) caja.getX()
                          + (float) caja.getWidth() * (float) cortes[(size_t) i] / (float) len;
            const bool fija = (i == 0);
            g.setColour (fija ? ZatiColours::inkDim.withAlpha (0.5f) : ZatiColours::accent);
            g.fillRect (x - (fija ? 0.5f : 1.0f), (float) caja.getY(),
                        fija ? 1.0f : 2.0f, (float) caja.getHeight());

            //  El numero del trozo, que es el pad al que va a parar. Sin el, la
            //  vista dice donde se corta y no que sale de cada corte.
            if (i < 99)
            {
                g.setColour (ZatiColours::textOn (ZatiColours::screenBg).withAlpha (0.75f));
                g.setFont (ZatiColours::monoFont (Metrics::fMeta - 1.0f, false));
                g.drawText (juce::String (i + 1), (int) x + 3, caja.getY() + 2, 22, 12,
                            juce::Justification::topLeft);
            }
        }
    }

    void resized() override { recalcula(); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        arrastrando = -1;
        movido = false;
        const int i = marcaEn (e.x);
        if (i > 0) arrastrando = i;         // la cero no se agarra
        partida = e.getPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (arrastrando < 0) return;
        if (e.getPosition().getDistanceFrom (partida) > 4) movido = true;
        if (movido && onMueve) onMueve (arrastrando, muestraEn (e.x));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (arrastrando > 0 && ! movido)   { if (onQuita) onQuita (arrastrando); }
        else if (arrastrando < 0 && ! movido) { if (onAnade) onAnade (muestraEn (e.x)); }
        arrastrando = -1;
        movido = false;
    }

private:
    struct Col { float min = 0.0f, max = 0.0f; };

    int largo() const { return fuente != nullptr ? fuente->buffer.getNumSamples() : 0; }

    int muestraEn (int x) const
    {
        const auto caja = getLocalBounds().reduced (2);
        const int len = largo();
        if (caja.getWidth() < 1 || len < 2) return 0;
        return juce::jlimit (0, len - 1,
                             (int) ((juce::int64) (x - caja.getX()) * len / caja.getWidth()));
    }

    //  Cual esta debajo del dedo. En PIXELES y no en muestras: el margen tiene
    //  que ser el mismo se vea un segundo o cuatro minutos, que es lo que hace
    //  que agarrar una marca se sienta igual en las dos.
    int marcaEn (int x) const
    {
        const auto caja = getLocalBounds().reduced (2);
        const int len = largo();
        if (len < 2) return -1;
        int mejor = -1, mejorD = kAgarre + 1;
        for (int i = 0; i < (int) cortes.size(); ++i)
        {
            const int mx = caja.getX() + (int) ((juce::int64) cortes[(size_t) i] * caja.getWidth() / len);
            const int d = std::abs (mx - x);
            if (d < mejorD) { mejorD = d; mejor = i; }
        }
        return mejorD <= kAgarre ? mejor : -1;
    }

    void recalcula()
    {
        picos.clear();
        const int len = largo();
        const auto caja = getLocalBounds().reduced (2);
        if (len < 2 || caja.getWidth() < 2 || fuente == nullptr) return;

        const auto* d = fuente->buffer.getReadPointer (0);
        picos.resize ((size_t) caja.getWidth());
        for (int x = 0; x < caja.getWidth(); ++x)
        {
            const int a = (int) ((juce::int64) x       * len / caja.getWidth());
            const int b = (int) ((juce::int64) (x + 1) * len / caja.getWidth());
            float mn = 0.0f, mx = 0.0f;
            for (int i = a; i < juce::jmax (a + 1, b) && i < len; ++i)
            {
                mn = juce::jmin (mn, d[i]);
                mx = juce::jmax (mx, d[i]);
            }
            picos[(size_t) x] = { mn, mx };
        }
    }

    //  Doce pixeles: menos que el dedo entero porque las marcas pueden estar
    //  juntas y un agarre de cuarenta se comeria a la vecina, y bastante mas
    //  que los dos que mide la raya.
    static constexpr int kAgarre = 12;

    SampleBuffer::Ptr fuente;
    std::vector<int>  cortes;
    std::vector<Col>  picos;
    juce::Point<int>  partida;
    int  arrastrando = -1;
    bool movido = false;
};

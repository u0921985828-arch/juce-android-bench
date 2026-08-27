#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"

// ============================================================================
//  UNA TAPA DE PAD, DIBUJADA EN UN SOLO SITIO.
//
//  Vivia entera dentro de PadButton::paintButton, que es donde tiene que estar
//  mientras haya un solo cliente. En cuanto el icono del lanzador paso a ser
//  una rejilla de pads hubo dos, y dos dibujos parecidos de la misma cosa se
//  separan: el sintoma habria sido «el icono ya no se parece a la app» sin
//  poder decir por que, que es exactamente lo que le paso al icono hecho a
//  mano que habia antes. Es el mismo movimiento que saco `normaliza` de dentro
//  de `render`.
//
//  Solo las capas ESTATICAS -bloque de profundidad, cuerpo, banda, numero y
//  borde-. Lo que depende del estado del boton (pulsado, sonando, destello,
//  seleccion, onda, nombre) se queda en PadButton, que es quien lo sabe: aqui
//  llegan ya decididos los dos colores que esos estados mueven.
//
//  Y con ESCALA, que es la unica forma de que las dos sean la misma tapa. Los
//  numeros -3 px de radio, 3 de bloque, 5 de banda, 9 y 6 de margen- se
//  eligieron sobre un pad de la cara, que mide unos 48 px de alto. La celda
//  del icono mide 146 a 1024, o sea tres veces mas: con los numeros fijos, la
//  banda de 5 px seria un pelo y las esquinas saldrian cuadradas. La cara pasa
//  1.0f y no se mueve un pixel; el icono pasa lo suyo y todo crece junto.
// ============================================================================
namespace PadArt
{
    //  Que banda lleva la tapa. Un pad cargado la lleva solida y de 5; uno
    //  vacio la lleva de contorno y de 2 -«el hueco esta y es de ese color,
    //  solo que no tiene nada dentro»-; y uno sonando no lleva ninguna, porque
    //  el cuerpo entero YA es el color.
    enum class Banda { ninguna, solida, fantasma };

    //  El alto para el que estan escritos los numeros de esta tapa.
    static constexpr float kAltoRef = 48.0f;

    //  LAS TRES MEDIDAS DE LA TAPA, con nombre y publicas.
    //
    //  Estaban dentro del cuerpo de `fondo` y `borde` como literales, y el
    //  icono las copiaba a mano: la misma regla escrita dos veces, que en esta
    //  casa siempre acaba igual. Lo pago la Z del icono, que se centraba
    //  contra una caja calculada con esos numeros copiados y contando ademas
    //  como sitio libre lo que el filo pinta - 7.875 de aire arriba contra
    //  6.375 abajo.
    //
    //  Quien dibuja la banda y el filo es este fichero, asi que quien dice
    //  cuanto miden es este fichero.
    static constexpr float kBandaAlto = 5.0f;    // la banda de un pad cargado
    static constexpr float kBandaY    = 1.0f;    // lo que baja desde el filo
    static constexpr float kBandaX    = 1.0f;    // y lo que se mete por los lados
    //  El filo se traza de 2 CENTRADO en la tapa metida 0.5, asi que lo que
    //  come hacia dentro es 0.5 + 2/2.
    static constexpr float kBordeGrosor = 2.0f;
    static constexpr float kBordeDentro = 0.5f + kBordeGrosor * 0.5f;   // 1.5

    //  EN DOS PASADAS, y no en una, porque en medio va lo que solo sabe el
    //  boton: la onda o el icono del instrumento, el numero y el nombre. El
    //  orden de la cara es fondo -> dibujo -> rotulos -> borde, y el borde
    //  tiene que ir el ULTIMO o se lo come el relleno de lo que venga detras.
    inline void fondo (juce::Graphics& g,
                       juce::Rectangle<float> r,
                       juce::Colour base,
                       juce::Colour frag,
                       Banda banda,
                       float escala,
                       bool conBloque = true)
    {
        const float lift = ZatiLookAndFeel::kCapLift * escala;
        const float rad  = 3.0f * escala;

        //  El bloque de profundidad: OSCURO y no con la tinta. Con
        //  ZatiColours::ink salia crema en LACA y hueso en GRAFITO, o sea un
        //  halo claro bajo cada uno de los dieciseis pads.
        if (conBloque)
        {
            g.setColour (ZatiColours::groove (0.32f));
            g.fillRoundedRectangle (r.translated (0.0f, lift), rad);
        }

        g.setColour (base);
        g.fillRoundedRectangle (r, rad);

        if (banda != Banda::ninguna)
        {
            const bool solida = (banda == Banda::solida);
            g.setColour (solida ? frag : frag.withAlpha (0.30f));
            g.fillRect (r.withHeight ((solida ? kBandaAlto : 2.0f) * escala)
                         .reduced (kBandaX * escala, 0.0f)
                         .withY (r.getY() + kBandaY * escala));
        }
    }

    inline void numero (juce::Graphics& g,
                        juce::Rectangle<float> r,
                        int n,
                        juce::Colour col,
                        float escala)
    {
        if (n < 0) return;
        g.setColour (col);
        g.setFont (ZatiColours::displayFont (juce::jmin (26.0f * escala, r.getHeight() * 0.30f)));
        g.drawText (juce::String (n).paddedLeft ('0', 2),
                    r.reduced (9.0f * escala, 6.0f * escala)
                     .removeFromTop (r.getHeight() * 0.42f),
                    juce::Justification::topLeft);
    }

    inline void borde (juce::Graphics& g,
                       juce::Rectangle<float> r,
                       juce::Colour edge,
                       bool cargado,
                       float escala)
    {
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f * escala), 3.0f * escala,
                                (cargado ? kBordeGrosor : 1.2f) * escala);
    }

    //  Los dos colores que un pad tiene EN REPOSO, que es el estado en el que
    //  el icono lo dibuja. Sacados aqui por lo mismo: PadButton los recalcula
    //  para sus estados, pero el punto de partida es este y es uno.
    inline juce::Colour cuerpoDe (juce::Colour frag, bool cargado)
    {
        return cargado ? frag.withMultipliedAlpha (0.62f)
                       : ZatiColours::padBg2.overlaidWith (frag.withAlpha (0.16f));
    }

    inline juce::Colour bordeDe (juce::Colour frag, bool cargado)
    {
        return cargado ? frag : ZatiColours::padBorder;
    }
}

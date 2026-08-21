#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "Zati.h"
#include "Iconos.h"

// ============================================================================
//  EL GRAFICO DESTACADO DE LA FICHA, dibujado por la propia app.
//
//  Google Play pide un banner de 1024x500 y NO puede ser una captura: es lo
//  primero que se ve en la ficha y una foto de la interfaz recortada a 1024x500
//  se lee como un error de maquetado.
//
//  Se dibuja aqui y no con una herramienta de imagenes por una razon concreta:
//  el chasis, la tinta, el acento y los ocho colores de zati son TOKENS que se
//  mueven - hay cuatro carcasas y la paleta ha cambiado tres veces en este
//  proyecto. Un banner hecho fuera se queda con la paleta del dia que se hizo,
//  y nadie se entera hasta que alguien compara la ficha con la app. Hecho con
//  ZatiColours, el banner ES la app: se regenera y sale al dia.
//
//  Solo del banco. No hay ningun camino desde la interfaz que llegue aqui.
// ============================================================================
namespace StoreArt
{
    inline juce::Image featureGraphic (int w = 1024, int h = 500)
    {
        juce::Image img (juce::Image::ARGB, w, h, true);
        juce::Graphics g (img);

        const auto b = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);

        //  El cuerpo, con el mismo degradado que la cara de la maquina.
        g.setGradientFill (juce::ColourGradient (ZatiColours::chassisTop, 0.0f, 0.0f,
                                                 ZatiColours::chassisBot, 0.0f, (float) h, false));
        g.fillRect (b);

        //  La tira de ocho colores, que es la firma de la caja: en la app va
        //  bajo el nombre y aqui hace de borde superior.
        {
            const float sw = (float) w / (float) Zati::kNumColours;
            for (int i = 0; i < Zati::kNumColours; ++i)
            {
                g.setColour (Zati::colour (i));
                g.fillRect (sw * (float) i, 0.0f, sw - 1.0f, 10.0f);
            }
        }

        //  Cada linea se MIDE contra el hueco que le queda y se encoge la
        //  fuente hasta que cabe. La primera version fiaba el ancho a la
        //  cuenta de los caracteres y salieron dos rotulos cortados con
        //  puntos suspensivos - "A..." por ARTiFACTS - en lo primero que se ve
        //  de la app en la tienda.
        auto drawFitted = [&g] (const juce::String& text, juce::Rectangle<float> box,
                                juce::Font font)
        {
            for (int i = 0; i < 12; ++i)
            {
                if (juce::GlyphArrangement::getStringWidth (font, text) <= box.getWidth()) break;
                font = font.withHeight (font.getHeight() * 0.94f);
            }
            g.setFont (font);
            g.drawText (text, box, juce::Justification::topLeft, false);
        };

        //  El texto se para antes del bloque de tapas. Sin el tope, la primera
        //  version escribia ARTiFACTS por debajo de la tapa 02.
        const float artW = (float) h * 0.30f * 2.14f;
        const float textX = (float) w * 0.055f;
        const float textW = (float) w - artW - textX * 2.0f - 24.0f;

        //  El nombre, con el mismo tipo y el mismo tracking que el de la cara.
        g.setColour (ZatiColours::ink);
        auto title = juce::Rectangle<float> (textX, (float) h * 0.20f, textW, (float) h * 0.30f);

        //  LA MARCA, la misma que lleva la cara de la maquina. Un banner con
        //  un dibujo que la app no tiene es un banner de otra app.
        {
            const float lado = (float) h * 0.13f;
            auto m = Iconos::marca();
            m.applyTransform (juce::AffineTransform::scale (lado / 24.0f)
                                  .translated (textX, title.getY() - lado - (float) h * 0.045f));
            g.fillPath (m);
        }
        //  El nombre COMPLETO, que es como se llama la app en la tienda. En la
        //  cara de la maquina sigue poniendo ZATI a secas: ahi es la marca
        //  serigrafiada en el chasis, y "ZATI SAMPLER" cruzado por la cabecera
        //  se comeria la fila entera para decir lo que la maquina ya es.
        drawFitted ("ZATI SAMPLER", title, ZatiColours::labelFont ((float) h * 0.26f, 0.20f));

        g.setColour (ZatiColours::inkDim);
        drawFitted (juce::String::fromUTF8 ("GROOVEBOX  \xc2\xb7  ARTiFACTS"),
                    title.translated (0.0f, (float) h * 0.30f),
                    ZatiColours::monoFont ((float) h * 0.058f, true).withExtraKerningFactor (0.16f));

        g.setColour (ZatiColours::ink.withAlpha (0.75f));
        drawFitted (juce::String::fromUTF8 ("64 PADS  \xc2\xb7  SECUENCIADOR  \xc2\xb7  6 EFECTOS"),
                    title.translated (0.0f, (float) h * 0.44f),
                    ZatiColours::monoFont ((float) h * 0.050f, false).withExtraKerningFactor (0.10f));

        //  Cuatro tapas de pad a la derecha, con su relieve: dicen lo que es
        //  la app sin tener que leer nada, y son el unico dibujo del banner.
        {
            const float side = (float) h * 0.30f;
            const float gap  = side * 0.14f;
            const float x0   = (float) w - (2.0f * side + gap) - (float) w * 0.055f;
            const float y0   = (float) h * 0.5f - (side + gap * 0.5f);

            for (int i = 0; i < 4; ++i)
            {
                const float x = x0 + (float) (i % 2) * (side + gap);
                const float y = y0 + (float) (i / 2) * (side + gap);
                const auto  c = Zati::colour (i * 2);

                //  La sombra se HUNDE, nunca se pinta con la tinta: en un
                //  chasis oscuro la tinta es clara y la sombra seria lo que
                //  mas destaca de la tapa. Misma regla que en la app.
                g.setColour (ZatiColours::groove (0.42f));
                g.fillRoundedRectangle (x, y + 4.0f, side, side, 6.0f);
                g.setColour (c);
                g.fillRoundedRectangle (x, y, side, side, 6.0f);
                g.setColour (ZatiColours::textOn (c).withAlpha (0.85f));
                g.setFont (ZatiColours::labelFont (side * 0.34f, 0.02f));
                g.drawText (juce::String (i + 1).paddedLeft ('0', 2),
                            juce::Rectangle<float> (x, y, side, side).reduced (side * 0.14f),
                            juce::Justification::topLeft);
            }
        }

        return img;
    }

    inline void writeFeature (const juce::String& path, int w, int h)
    {
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();
        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (featureGraphic (w, h), out);
        out.flush();
    }
}

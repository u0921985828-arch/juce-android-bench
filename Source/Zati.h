#pragma once

#include <JuceHeader.h>

// ============================================================================
//  Zati - el sistema de color de los fragmentos.
//
//  Cada trozo de una muestra es un *zati* (fragmento, en euskera) y lleva un
//  color de una paleta fija de ocho. El color nace en el corte y viaja la
//  cadena entera: CORTE -> PAD -> ONDA -> MANDOS.
//
//  Dos reglas, y las dos sostienen algo. El reparto sigue el ORDEN DEL CORTE y
//  nunca el azar: el zati 1 es siempre rojo, y asi el kit se memoriza - sabes
//  donde esta un trozo sin leer su numero. Un reparto aleatorio, o sacado del
//  propio audio, destruye eso: dos muestras parecidas caerian en colores casi
//  iguales, que es justo lo contrario de para lo que sirve.
//
//  Y la paleta NO sigue a la carcasa. Las carcasas reestilan el chasis y nada
//  mas. Si los colores de los fragmentos se movieran con ellas, cambiar de piel
//  romperia la memoria que la persona tiene de su kit.
//
//  El color nunca es la unica senal: los pads y los trozos de la onda llevan
//  ademas una franja y un numero, para que el instrumento siga siendo usable
//  con daltonismo. En un producto donde el color ES la informacion, eso no es
//  opcional.
// ============================================================================
namespace Zati
{
    static constexpr int kNumColours = 8;

    inline juce::Colour colour (int index)
    {
        static const juce::Colour palette[kNumColours] = {
            juce::Colour (0xffe8544a),   // 1 rojo
            juce::Colour (0xffee853a),   // 2 naranja
            juce::Colour (0xfff0be44),   // 3 ambar
            juce::Colour (0xff96cd5c),   // 4 verde
            juce::Colour (0xff4ac4a8),   // 5 turquesa
            juce::Colour (0xff4c96e8),   // 6 azul
            juce::Colour (0xff8c6ee0),   // 7 violeta
            juce::Colour (0xffd860b0),   // 8 magenta
        };
        return palette[((index % kNumColours) + kNumColours) % kNumColours];
    }

    inline const char* name (int index)
    {
        static const char* names[kNumColours] = {
            "ROJO", "NARANJA", "AMBAR", "VERDE", "TURQUESA", "AZUL", "VIOLETA", "MAGENTA"
        };
        return names[((index % kNumColours) + kNumColours) % kNumColours];
    }

    //  Dieciseis pads sobre ocho colores: la paleta se repite una vez, asi que
    //  el troceado de un banco entero se sigue leyendo de izquierda a derecha
    //  en el mismo orden fijo.
    inline int forPad (int pad) { return ((pad % kNumColours) + kNumColours) % kNumColours; }
}

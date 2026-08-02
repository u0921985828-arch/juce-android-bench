#pragma once

#include <JuceHeader.h>

// ============================================================================
//  Zati — the fragment colour system (UI-SPEC-04 §3).
//
//  Every slice of a sample is a *zati* (Basque: fragment) and carries one
//  colour from a fixed palette of eight. The colour is born at the cut and
//  travels the whole chain:
//
//      CUT -> PAD -> WAVEFORM -> KNOBS
//
//  Two rules the spec is emphatic about, both load-bearing:
//
//   * Assignment follows CUT ORDER, never chance. Zati 1 is always red, so
//     the kit becomes memorisable — you know where a slice is without reading
//     its number. A random or audio-derived mapping destroys that: two similar
//     samples would land on near-identical colours, the opposite of the point.
//
//   * The palette does NOT follow the skin. Skins restyle the chassis only.
//     If fragment colours moved with the skin, the user's memory of the kit
//     would break every time they changed it.
//
//  Colour is never the only signal: pads and waveform segments always carry a
//  stripe and a number too, so the instrument stays usable with colour vision
//  deficiency — in a product where colour IS the information, that is not
//  optional.
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

    // 16 pads over 8 colours: the palette repeats once, so a chop of a whole
    // bank still reads left-to-right in the same fixed order.
    inline int forPad (int pad) { return ((pad % kNumColours) + kNumColours) % kNumColours; }
}

#pragma once

#include <JuceHeader.h>

// ============================================================================
//  Lang — the app in four languages: Spanish, English, Chinese and Arabic.
//
//  The KEY of every string is the text that was already written in the source,
//  which is mostly Spanish with a few English words that came from the studio
//  vocabulary. That choice is deliberate:
//
//    * Nothing in the code turns into an opaque identifier. A line that reads
//      T ("Pad vacio - pulsa LOAD y toca el pad para cargarlo") still says what
//      it will put on screen, and a missing translation degrades to something
//      a person can read instead of to STR_PAD_EMPTY_042.
//    * Spanish is a lookup like any other rather than the identity, so the
//      handful of places where the Spanish text was itself an English word can
//      be fixed in the table without touching the code.
//
//  Two strings sometimes need the same key to mean different things - REV is
//  the reverse switch on a pad and also the reverb - so a key may carry a
//  context after a vertical bar: T ("REV|reverso"). The bar and everything
//  after it is stripped when there is no translation, so the fallback is still
//  the bare word.
//
//  Arguments go in as %1, %2, %3 rather than by concatenating fragments.
//  "Cortado en " + n + " trozos" only reads correctly in a language whose word
//  order happens to match Spanish's, and neither Chinese nor Arabic does.
//
//  What is NOT translated, in any language: the six effect abbreviations (ISO,
//  HPF, DRV, DLY, CRSH, REV) and the unit suffixes (ms, Hz, dB, bpm, st). Those
//  are read the same way on a mixer in Shanghai as on one in Madrid, and they
//  live in buttons four characters wide.
//
//  Arabic is translated but the layout is not mirrored: the machine face is a
//  fixed panel of pads and knobs whose positions are muscle memory, not a
//  column of text. The text reads right to left inside its box, which is what
//  the renderer does on its own.
// ============================================================================
class Lang
{
public:
    enum Id { es = 0, en, zh, ar, numLanguages };

    static Id  current() noexcept;
    static void set (Id newLanguage);

    //  The system's language if we speak it, Spanish otherwise.
    static Id  detect();

    static const char* code (Id id);          // "es" / "en" / "zh" / "ar"
    static const char* nativeName (Id id);    // as that language writes itself
    static bool isRightToLeft (Id id) noexcept { return id == ar; }

    //  Fence a run of NUMBERS or Latin around so Arabic does not reorder it.
    //
    //  "OUT " + "-inf" came out on screen as "خرج inf-" : bidi saw a minus at
    //  the boundary of a right-to-left run and moved it to the other end, so a
    //  reading of minus infinity turned into something that is not a number at
    //  all. Same for "-12 dB", for "4 cores - 15.7 GB", for a file name, for
    //  anything latin sitting inside a translated sentence.
    //
    //  U+2066 LEFT-TO-RIGHT ISOLATE ... U+2069 POP DIRECTIONAL ISOLATE says
    //  "this piece has its own direction and it does not take part in the
    //  bidi of what is around it". Outside Arabic it costs nothing: the
    //  characters are invisible and the string is returned untouched.
    static juce::String ltr (const juce::String& latinRun);

    //  Remembered between launches next to the rest of the ZATI folder. This
    //  is a preference, not project state: it does not belong in a song.
    static void loadPreference();
    static void savePreference();
};

//  Translate. The overloads substitute %1, %2, %3.
juce::String T (const juce::String& key);
juce::String T (const juce::String& key, const juce::String& a1);
juce::String T (const juce::String& key, const juce::String& a1, const juce::String& a2);
juce::String T (const juce::String& key, const juce::String& a1,
                const juce::String& a2, const juce::String& a3);

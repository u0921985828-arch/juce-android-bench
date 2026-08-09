#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "PadButton.h"

// ============================================================================
//  UiAudit — the interface measuring itself.
//
//  Everything an interface gets wrong at a size or in a language it was not
//  drawn in is measurable: a cap narrower than a finger, two controls that
//  overlap, a caption wider than the box it was put in. Judging those by eye
//  on a screenshot means one language and one screen at a time, and the two
//  that matter here - Chinese, which is wide per glyph, and Arabic, which is
//  right to left - are exactly the two nobody checks.
//
//  So the app measures itself instead. With ZATI_AUDIT set it lays out, walks
//  its own component tree, prints one JSON line per component and quits.
//  Bounds are in window coordinates, so overlaps and gaps between different
//  parents come out of the same arithmetic.
//
//  The line that earns this file: for anything that draws a caption we print
//  BOTH the pixels the text needs at its real font AND the pixels its box
//  gives it. Truncation stops being something you notice in a screenshot and
//  becomes a number that is negative.
//
//      ZATI_AUDIT=1              dump and quit
//      ZATI_SIZE=412x915         lay out at this size first
//      ZATI_LANG=es|en|zh|ar     in this language
//      ZATI_OPEN=pads|sec|...    with this sheet open
//      ZATI_CYCLE=3              suspend and resume this many times first
// ============================================================================
namespace UiAudit
{
    inline bool enabled()  { return juce::SystemStats::getEnvironmentVariable ("ZATI_AUDIT", {}).isNotEmpty(); }

    inline juce::String env (const char* k) { return juce::SystemStats::getEnvironmentVariable (k, {}); }

    inline juce::String esc (const juce::String& s)
    {
        juce::String o;
        for (auto c : s)
        {
            if (c == '"' || c == '\\') o << '\\' << (juce::juce_wchar) c;
            else if (c == '\n')        o << "\\n";
            else if (c < 32)           o << ' ';
            else                       o << (juce::juce_wchar) c;
        }
        return o;
    }

    //  What a component would need to draw its caption without clipping, and
    //  what it actually has. Sliders are excluded: their caption is a number
    //  in a box the layout sized on purpose.
    struct Caption { bool has = false; float needW = 0.0f; float haveW = 0.0f; juce::String text; };

    inline Caption captionOf (juce::Component& c)
    {
        Caption r;

        if (auto* tb = dynamic_cast<juce::TextButton*> (&c))
        {
            //  A pad's caption is its number in the display face and its
            //  sample name in a strip of its own; neither is measured here.
            if (dynamic_cast<PadButton*> (&c) != nullptr)
                return r;

            r.has  = true;
            r.text = tb->getButtonText();

            //  EXACTLY what ZatiLookAndFeel::drawButtonText will use. Asking
            //  the look-and-feel for getTextButtonFont would be measuring a
            //  font this app never draws with - it does not override that
            //  method - and every number would be wrong in the direction that
            //  says "it fits".
            const auto f = ZatiColours::monoFont (juce::jlimit (10.0f, 14.5f, (float) tb->getHeight() * 0.38f), true)
                             .withExtraKerningFactor (0.06f);
            r.needW = juce::GlyphArrangement::getStringWidth (f, r.text);
            //  ...and exactly the box it will fit that into.
            r.haveW = (float) tb->getWidth() - 2.0f * (float) juce::jlimit (3, 5, tb->getWidth() / 14);
        }
        else if (auto* l = dynamic_cast<juce::Label*> (&c))
        {
            r.has  = true;
            r.text = l->getText();
            r.needW = juce::GlyphArrangement::getStringWidth (l->getFont(), r.text);
            r.haveW = (float) l->getWidth() - 4.0f;
        }

        return r;
    }

    inline const char* kindOf (juce::Component& c)
    {
        if (dynamic_cast<juce::Slider*>     (&c) != nullptr) return "slider";
        if (dynamic_cast<juce::TextEditor*> (&c) != nullptr) return "editor";
        if (dynamic_cast<juce::Button*>     (&c) != nullptr) return "button";
        if (dynamic_cast<juce::Label*>      (&c) != nullptr) return "label";
        return "other";
    }

    //  Set by the app before a dump: how long the ENGINE thinks pad n is.
    inline std::function<int (int)> engineLength;

    inline void walk (juce::Component& c, juce::Component& root, const juce::String& path, int depth,
                      bool underSlider = false, bool underViewport = false)
    {
        if (! c.isVisible()) return;

        const auto abs = root.getLocalArea (&c, c.getLocalBounds());
        const auto cap = captionOf (c);
        const auto kind = kindOf (c);
        const bool interactive = (juce::String (kind) == "button" || juce::String (kind) == "slider"
                                  || juce::String (kind) == "editor");
        //  A slider's text box and its two keys are the slider; they are not
        //  three targets that happen to overlap, and measuring them as such
        //  buries the real findings under a hundred of its own making.
        const bool insideSlider = underSlider;
        //  Anything inside a viewport is ALLOWED past the window edge - that
        //  is what a scroll is. Reporting it as laid out off screen would
        //  bury the real overflows under the one case that is intentional.
        const bool scrolled = underViewport;

        juce::String line;
        line << "{\"path\":\"" << esc (path) << "\""
             << ",\"kind\":\"" << kind << "\""
             << ",\"depth\":" << depth
             << ",\"x\":" << abs.getX() << ",\"y\":" << abs.getY()
             << ",\"w\":" << abs.getWidth() << ",\"h\":" << abs.getHeight()
             << ",\"on\":" << (c.isEnabled() ? 1 : 0)
             << ",\"hit\":" << (interactive && ! insideSlider ? 1 : 0)
             << ",\"inSlider\":" << (insideSlider ? 1 : 0)
             << ",\"scrolled\":" << (scrolled ? 1 : 0);

        //  WHAT IS ON THE PAD. The one piece of state worth carrying in a
        //  layout dump: after leaving the app and coming back, is the sound
        //  still on the pad it was on? That is a question about the session,
        //  not about pixels, and it is the question this app keeps failing.
        if (auto* pb = dynamic_cast<PadButton*> (&c))
            line << ",\"pad\":1,\"loaded\":" << (pb->hasSample() ? 1 : 0)
                 << ",\"sample\":\"" << esc (pb->sampleName()) << "\"";

        //  ...and what the ENGINE thinks, which is the half that was missing.
        //  "loaded" is the tile's own belief; a pad can look perfectly loaded
        //  and be silent, and for every restored session it was. The dump now
        //  carries both, so the two can be compared instead of trusted.
        if (engineLength != nullptr)
            if (auto* pb = dynamic_cast<PadButton*> (&c))
                line << ",\"engine\":" << engineLength (pb->getIndex());

        if (cap.has && cap.text.isNotEmpty())
            line << ",\"text\":\"" << esc (cap.text) << "\""
                 << ",\"needW\":" << juce::String (cap.needW, 1)
                 << ",\"haveW\":" << juce::String (cap.haveW, 1);

        line << "}";
        std::cout << line << std::endl;

        const bool childUnderSlider   = underSlider   || dynamic_cast<juce::Slider*>   (&c) != nullptr;
        const bool childUnderViewport = underViewport || dynamic_cast<juce::Viewport*> (&c) != nullptr;
        int i = 0;
        for (auto* k : c.getChildren())
            walk (*k, root, path + "/" + juce::String (i++) + ":" + juce::String (typeid (*k).name()).getLastCharacters (14),
                  depth + 1, childUnderSlider, childUnderViewport);
    }

    inline void dump (juce::Component& root)
    {
        std::cout << "{\"root\":1,\"w\":" << root.getWidth() << ",\"h\":" << root.getHeight()
                  << ",\"lang\":\"" << env ("ZATI_LANG") << "\""
                  << ",\"open\":\"" << env ("ZATI_OPEN") << "\"}" << std::endl;
        walk (root, root, "root", 0);
    }
}

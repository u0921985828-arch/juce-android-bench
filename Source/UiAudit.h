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
//      ZATI_OPEN=pads|sec|paso|...  with this sheet open
//      ZATI_CYCLE=3              suspend and resume this many times first
//      ZATI_DEMO=1               con doce pads cargados y un patron escrito
//      ZATI_SHOT=x.png           saca una foto en vez de un volcado
//      ZATI_SHOT_SCALE=2.62      a esta escala
// ============================================================================
namespace UiAudit
{
    inline bool enabled()  { return juce::SystemStats::getEnvironmentVariable ("ZATI_AUDIT", {}).isNotEmpty(); }

    inline juce::String env (const char* k) { return juce::SystemStats::getEnvironmentVariable (k, {}); }

    //  UNA FOTO DEL COMPONENTE, a la escala que se pida.
    //
    //  A escala 1 la foto sale con los pixeles logicos - 412 de ancho - y Play
    //  pide como minimo 320 en el lado corto, asi que pasaria por los pelos y
    //  se veria como un movil de 2012. Pintar a escala 2.6 no estira una
    //  imagen: vuelve a dibujar los vectores, las fuentes y los degradados a
    //  1080 de ancho, que es lo que hace que un rotulo se lea en la ficha.
    inline void snapshot (juce::Component& c, const juce::String& path, float scale)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), false, scale);
        juce::File f (path);
        f.getParentDirectory().createDirectory();
        f.deleteFile();

        juce::FileOutputStream out (f);
        if (! out.openedOk()) return;
        juce::PNGImageFormat png;
        png.writeImageToStream (img, out);
        out.flush();
    }

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

    //  DE QUE PAD SALE EL AUDIO DE CADA UNO. El troceado no es un componente:
    //  son dieciseis pads apuntando al mismo buffer, y eso no se ve en un
    //  volcado del arbol - que es como se perdio al guardar y volver sin que
    //  ninguna prueba lo notara. Se imprime aparte. -1 = pad vacio.
    inline std::function<int (int)> padSource;

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

    // ========================================================================
    //  LAS MISMAS REGLAS, PERO DENTRO Y SIN VOLCADO.
    //
    //  expo.py mide 476 estados FIJOS: los que a alguien se le ocurrieron. Lo
    //  que rompe una interfaz de verdad es la combinacion - la ficha X abierta
    //  con el banco D, en arabe, en apaisado, despues de haber tocado nueve
    //  cosas - y esa no se enumera, se sortea. Sortearla desde fuera cuesta un
    //  arranque de proceso por estado, o sea un segundo y medio; desde dentro
    //  cuesta un repintado.
    //
    //  Solo dos de las cinco reglas, y son las dos que no dependen del idioma:
    //  hermanos que se pisan y controles que se salen de la ventana. Las de
    //  texto necesitan comparar dos idiomas en la misma ruta, que es cosa del
    //  script.
    struct Hallazgos { int solapes = 0, fuera = 0, mirados = 0; };

    inline void recoge (juce::Component& c, juce::Component& root,
                        juce::Array<juce::Rectangle<int>>& hermanos, Hallazgos& h,
                        bool underSlider = false, bool underViewport = false)
    {
        if (! c.isVisible()) return;

        const juce::String kind = kindOf (c);
        const bool interactive = (kind == "button" || kind == "slider" || kind == "editor");
        const auto abs = root.getLocalArea (&c, c.getLocalBounds());

        if (interactive && ! underSlider && abs.getWidth() > 0 && abs.getHeight() > 0)
        {
            ++h.mirados;
            if (! underViewport && ! root.getLocalBounds().contains (abs)) ++h.fuera;
        }

        //  Los hermanos se comparan entre ellos y no contra el arbol entero:
        //  una ficha encima de la cara es el diseno, no un solape.
        juce::Array<juce::Rectangle<int>> mios;
        for (auto* k : c.getChildren())
            recoge (*k, root, mios, h, underSlider || kind == "slider",
                    underViewport || dynamic_cast<juce::Viewport*> (&c) != nullptr);

        if (interactive && ! underSlider && abs.getWidth() > 0 && abs.getHeight() > 0)
        {
            for (const auto& otro : hermanos)
            {
                const auto in = otro.getIntersection (abs);
                if (in.getWidth() > 1 && in.getHeight() > 1) ++h.solapes;
            }
            hermanos.add (abs);
        }
    }

    inline Hallazgos check (juce::Component& root)
    {
        Hallazgos h;
        juce::Array<juce::Rectangle<int>> raiz;
        recoge (root, root, raiz, h);
        return h;
    }

    inline void dump (juce::Component& root)
    {
        std::cout << "{\"root\":1,\"w\":" << root.getWidth() << ",\"h\":" << root.getHeight()
                  << ",\"lang\":\"" << env ("ZATI_LANG") << "\""
                  << ",\"open\":\"" << env ("ZATI_OPEN") << "\"}" << std::endl;
        walk (root, root, "root", 0);

        if (padSource != nullptr)
        {
            std::cout << "{\"fuentes\":[";
            for (int i = 0; i < 64; ++i)
                std::cout << (i ? "," : "") << padSource (i);
            std::cout << "]}" << std::endl;
        }
    }
}

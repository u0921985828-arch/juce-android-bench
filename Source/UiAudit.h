#pragma once

#include <JuceHeader.h>
#include "ZatiLookAndFeel.h"
#include "PadButton.h"
#include <algorithm>
#include <chrono>
#include <typeinfo>
#include <vector>

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
//      ZATI_PAINT=60             cuanto cuesta un fotograma, por piezas
//      ZATI_SPIN=12              CPU del proceso con la cara abierta y quieta
// ============================================================================
namespace UiAudit
{
    inline bool enabled()  { return juce::SystemStats::getEnvironmentVariable ("ZATI_AUDIT", {}).isNotEmpty(); }

    inline juce::String env (const char* k) { return juce::SystemStats::getEnvironmentVariable (k, {}); }

    //  CUANTAS VECES SE HA PINTADO EL FONDO DE LA CARA.
    //
    //  El chasis solo se dibuja cuando hay que repintar la ventana entera, asi
    //  que contarlo cuenta fotogramas completos - que es la pregunta que
    //  importa en un telefono. Una ficha translucida que pide repaint() de si
    //  misma suma uno aqui aunque el fondo no haya cambiado, y ese es
    //  exactamente el desperdicio que se buscaba.
    inline int fondosPintados = 0;
    //  CUANTAS VECES SE HA MOVIDO EL CABEZAL DEL PIANO ROLL. La barra estaba
    //  dibujada desde el primer dia y no estaba viva: el temporizador solo
    //  alimentaba la rejilla de PASOS, asi que en la pagina del piano el
    //  cabezal se quedaba donde estuviera al entrar. Eso no se ve en un
    //  volcado de geometria -el componente esta ahi y mide lo mismo- y se
    //  cuenta aqui, que es la unica forma de que un cero se distinga de un
    //  "no lo he mirado".
    inline int cabezalPiano = 0;
    //  Y cuantas veces se ha ALIMENTADO la pagina del piano. Son dos cifras y
    //  no una porque en un escritorio sin tarjeta de sonido el transporte no
    //  avanza -el paso lo mueve la llamada de audio- y cabezalPiano saldria
    //  cero tanto con el fallo puesto como con el quitado. Lo que si se puede
    //  medir aqui es el eslabon que faltaba: que el temporizador llame a
    //  refreshPiano mientras la pagina del piano esta abierta. Cero es el
    //  fallo; el resto lo cuenta el motor, que es donde el paso avanza.
    inline int pianoTicks = 0;

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

    //  Y que pads llevan un paso puesto. Ver padSource: un patron tampoco es
    //  un componente.
    inline std::function<bool (int)> stepOn;

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
    //  El CULPABLE, no solo la cuenta. Un "8 solapes" manda a leer el
    //  maquetado entero; "ADELANTE sobre 2" apunta a la linea. Se guarda el
    //  primero, que en la practica es el que arrastra a los demas.
    struct Hallazgos { int solapes = 0, fuera = 0, mirados = 0; juce::String quien; };

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
                if (in.getWidth() > 1 && in.getHeight() > 1)
                {
                    ++h.solapes;
                    if (h.quien.isEmpty())
                    {
                        const auto cap = captionOf (c);
                        h.quien = (cap.text.isNotEmpty() ? cap.text : juce::String (kind))
                                + " @" + juce::String (abs.getX()) + "," + juce::String (abs.getY())
                                + " " + juce::String (abs.getWidth()) + "x" + juce::String (abs.getHeight());
                    }
                }
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

    //  LO QUE CUESTA UN FOTOGRAMA, y de quien es la culpa.
    //
    //  El motor se puede medir corriendo bloques; la cara no, porque no la
    //  ejecuta nadie: la pinta el sistema cuando le parece. Asi que aqui se
    //  pinta a mano N veces sobre una imagen del mismo tamano que la ventana
    //  y se cronometra - primero el arbol entero, que es lo que paga el
    //  telefono en un repintado completo, y despues cada hijo directo por
    //  separado, que es lo unico que dice A QUIEN cobrarselo.
    //
    //  Se pinta en escala 1 a proposito: lo que se busca es la PROPORCION
    //  entre componentes, y esa no cambia con la densidad de la pantalla,
    //  mientras que el numero absoluto de un portatil no vale para un movil.
    //
    //  La MEDIANA de los fotogramas y no la media, por lo mismo que en el
    //  banco de CPU: un solo fotograma interrumpido por el sistema mueve una
    //  media lo bastante como para invertir el orden de dos filas.
    inline void paintCost (juce::Component& root, int frames)
    {
        if (frames < 1) frames = 1;

        auto mide = [frames] (juce::Component& c, juce::Rectangle<int> clip = {}) -> double
        {
            const auto b = c.getLocalBounds();
            if (b.getWidth() < 1 || b.getHeight() < 1) return 0.0;

            juce::Image img (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
            std::vector<double> t;
            t.reserve ((size_t) frames);

            for (int i = 0; i < frames; ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                {
                    juce::Graphics g (img);
                    //  Recortado, si se pide: es exactamente lo que hace el
                    //  sistema cuando un componente pide repintarse solo un
                    //  trozo, y por tanto lo que cuesta de verdad un repintado
                    //  parcial - incluido lo que hay DETRAS de ese trozo, que
                    //  con una ficha translucida encima tambien hay que
                    //  volver a dibujar.
                    if (! clip.isEmpty()) g.reduceClipRegion (clip);
                    c.paintEntireComponent (g, false);
                }
                t.push_back (std::chrono::duration<double, std::milli> (
                                 std::chrono::steady_clock::now() - t0).count());
            }

            std::sort (t.begin(), t.end());
            return t[t.size() / 2];
        };

        //  Y el FONDO del propio componente raiz, sin sus hijos: el chasis, las
        //  placas y la rotulacion grabada. Se mide llamando a paint() a secas
        //  en vez de a paintEntireComponent, porque restarlo de la suma de los
        //  hijos daba un numero que incluia todo lo que no supimos atribuir.
        auto soloFondo = [frames] (juce::Component& c) -> double
        {
            juce::Image img (juce::Image::ARGB, juce::jmax (1, c.getWidth()),
                             juce::jmax (1, c.getHeight()), true);
            std::vector<double> t;
            t.reserve ((size_t) frames);
            for (int i = 0; i < frames; ++i)
            {
                const auto t0 = std::chrono::steady_clock::now();
                { juce::Graphics g (img); c.paint (g); }
                t.push_back (std::chrono::duration<double, std::milli> (
                                 std::chrono::steady_clock::now() - t0).count());
            }
            std::sort (t.begin(), t.end());
            return t[t.size() / 2];
        };

        const double total = mide (root);
        std::cout << "{\"pintado\":1,\"w\":" << root.getWidth() << ",\"h\":" << root.getHeight()
                  << ",\"fotogramas\":" << frames
                  << ",\"fondo_ms\":" << soloFondo (root)
                  //  Una banda de 30 px de alto en mitad de la ventana: el
                  //  tamano de lo unico que se mueve en una ficha mientras el
                  //  secuenciador rueda. Si esta fila es mucho mas barata que
                  //  total_ms, cada repaint() de ficha entera esta pagando el
                  //  fotograma completo para animar un renglon.
                  << ",\"banda30_ms\":" << mide (root, { 0, root.getHeight() / 2 - 15, root.getWidth(), 30 })
                  << ",\"total_ms\":" << total << "}" << std::endl;

        for (int i = 0; i < root.getNumChildComponents(); ++i)
        {
            auto* c = root.getChildComponent (i);
            if (c == nullptr || ! c->isVisible()) continue;
            std::cout << "{\"pieza\":\"" << esc (c->getName().isNotEmpty() ? c->getName()
                                                                          : juce::String (typeid (*c).name()))
                      << "\",\"w\":" << c->getWidth() << ",\"h\":" << c->getHeight()
                      << ",\"hijos\":" << c->getNumChildComponents()
                      << ",\"ms\":" << mide (*c) << "}" << std::endl;
        }
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

        if (stepOn != nullptr)
        {
            std::cout << "{\"pasos\":[";
            bool first = true;
            for (int i = 0; i < 64; ++i)
                if (stepOn (i)) { std::cout << (first ? "" : ",") << i; first = false; }
            std::cout << "]}" << std::endl;
        }
    }
}

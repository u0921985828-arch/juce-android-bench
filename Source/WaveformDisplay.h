#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ZatiLookAndFeel.h"
#include "Lang.h"

// ============================================================================
//  WaveformDisplay — the hero LCD screen: the selected pad's sample drawn as a
//  filled min/max envelope on a dark panel, with a tick ruler, blue trim
//  handles and name / sample-rate readouts. Message-thread only.
// ============================================================================
class WaveformDisplay : public juce::Component
{
public:
    void setSample (SampleBuffer::Ptr sb)
    {
        //  OTRA MUESTRA, OTRA VISTA. El zoom y el trozo que se esta mirando
        //  son de ESTA muestra: dejarlos puestos al cambiar de pad enseñaba un
        //  sesentaicuatroavo cualquiera del sonido nuevo, sin sus asas de
        //  recorte -que quedan fuera de pantalla- y sin nada que dijese por
        //  que. Se reinicia cuando cambia el buffer, no en cada llamada: si es
        //  la misma muestra, el zoom que habias puesto se queda donde estaba.
        if (sample != sb) { zoom = 1.0f; view0 = 0.0f; if (onZoomChanged) onZoomChanged (zoom); }
        sample = sb;
        computeMinMax();
        repaint();
    }

    void setInfo (const juce::String& name, double sampleRate, double seconds, int channels)
    {
        infoName = name;
        infoRight = (sampleRate > 0.0)
                      ? juce::String (sampleRate / 1000.0, 1) + "k " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
                        + (channels > 1 ? " STEREO " : " MONO ") + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " "
                        + juce::String (seconds, 2) + "s"
                      : juce::String();
        repaint();
    }

    void clear() { sample = nullptr; mins.clearQuick(); maxs.clearQuick(); infoName = {}; infoRight = {}; repaint(); }

    void setTrim (float startNorm, float endNorm)
    {
        start01 = juce::jlimit (0.0f, 1.0f, startNorm);
        end01   = juce::jlimit (0.0f, 1.0f, endNorm);
        repaint();
    }

    // One slice of the loaded source, owned by a pad. When several pads share
    // one buffer (what auto-chop produces) the display stops being "one pad's
    // sample" and becomes the map of the whole cut: every fragment drawn in
    // its own zati colour, which is the middle link of CUT -> PAD -> WAVEFORM
    // -> KNOBS.
    struct Segment
    {
        float start01 = 0.0f, end01 = 1.0f;
        juce::Colour colour;
        int padNumber = 0;
        bool selected = false;
    };

    void setSegments (juce::Array<Segment> segs)
    {
        segments = std::move (segs);
        repaint();
    }

    //  Trim by dragging the handles, which is the gesture the drawn handles
    //  have been promising all along. Editing the start of a sound by typing
    //  0.062 is the wrong instrument: you want to grab it and listen.
    std::function<void (float start01, float end01)> onTrimDragged;

    //  Tap anywhere that is not a handle and hear the sound from there. A
    //  waveform you can only look at is a picture; this is the difference
    //  between finding the downbeat by eye and finding it by ear.
    std::function<void (float pos01)> onAudition;

    //  EL ZOOM, HASTA x64.
    //
    //  La pantalla mide unos 300 px y una muestra de cinco segundos son 220500
    //  numeros: cada columna de pixeles resume 735 muestras. A esa escala un
    //  chasquido de dos milisegundos es medio pixel y el principio exacto de
    //  un golpe no se puede ver, asi que el recorte se pone de oido y a la
    //  tercera. Con x64 una columna son once muestras y el ataque se ve.
    //
    //  El zoom NO cambia el recorte ni lo que suena: cambia que trozo se
    //  dibuja. start01 y end01 siguen siendo del fichero entero, que es como
    //  los guarda el pad; lo unico que hay que traducir es donde cae cada uno
    //  en pantalla.
    static constexpr float kMaxZoom = 64.0f;

    void setZoom (float z, float centre01)
    {
        const float old = zoom;
        zoom = juce::jlimit (1.0f, kMaxZoom, z);
        if (zoom <= 1.0f) { view0 = 0.0f; }
        else
        {
            //  Se amplia alrededor de un punto, no del borde izquierdo: al
            //  ampliar desde el borde, lo que estabas mirando se va de la
            //  pantalla y hay que buscarlo otra vez.
            const float w = 1.0f / zoom;
            view0 = juce::jlimit (0.0f, 1.0f - w, centre01 - w * 0.5f);
        }
        if (! juce::approximatelyEqual (old, zoom)) computeMinMax();
        repaint();
        if (onZoomChanged) onZoomChanged (zoom);
    }

    float getZoom() const noexcept { return zoom; }

    //  El centro de lo que se esta viendo, que es el punto natural sobre el
    //  que ampliar cuando nadie ha dicho otra cosa.
    float viewCentre() const noexcept { return view0 + 0.5f / zoom; }

    std::function<void (float zoom)> onZoomChanged;

    //  Where the read head is, 0..1, or negative for nothing sounding.
    void setPlayhead (float pos01)
    {
        const float p = (pos01 >= 0.0f && pos01 <= 1.0f) ? pos01 : -1.0f;
        if (std::abs (p - playhead) < 0.0005f && (p < 0.0f) == (playhead < 0.0f)) return;
        playhead = p;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;
        const float t  = xToNorm ((float) e.x);
        const float ds = std::abs (t - start01), de = std::abs (t - end01);
        // Grab whichever handle is nearer, but only within a finger's width;
        // a tap in open water should not yank an edge across the sample.
        //  En pantalla, no en el fichero: con x64 dos asas separadas por una
        //  centesima estan a 190 px, y con el margen medido sobre el fichero
        //  entero cualquier toque cerca del centro agarraba una de las dos.
        const float grab = 24.0f / juce::jmax (1.0f, (float) waveArea().getWidth() * zoom);
        dragging = (juce::jmin (ds, de) > grab) ? 0 : (ds <= de ? 1 : 2);
        panFrom = t;
        panView = view0;
        panned  = false;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (sample == nullptr) return;

        //  Aguas abiertas y ampliado: el dedo ARRASTRA LA VISTA. Es el gesto
        //  que ya hace la rejilla de pads para cambiar de banco, y es el unico
        //  que no gasta pantalla en barras de desplazamiento.
        if (dragging == 0)
        {
            if (zoom <= 1.0f) return;
            const auto w = waveArea();
            const float moved = ((float) e.x - (float) e.getMouseDownX())
                                  / juce::jmax (1.0f, w.getWidth()) / zoom;
            if (std::abs (moved) > 0.0005f) panned = true;
            view0 = juce::jlimit (0.0f, 1.0f - 1.0f / zoom, panView - moved);
            computeMinMax();
            repaint();
            return;
        }

        const float t = xToNorm ((float) e.x);
        float s = start01, en = end01;
        if (dragging == 1) s  = juce::jlimit (0.0f, en - 0.005f, t);
        else               en = juce::jlimit (s + 0.005f, 1.0f, t);
        setTrim (s, en);
        if (onTrimDragged) onTrimDragged (s, en);
    }

    //  El toque suena AL SOLTAR y no al pulsar, porque el mismo dedo que suena
    //  es el que arrastra la vista: sonando en el mousedown, cada arrastre
    //  disparaba el pad antes de mover nada.
    //
    //  Open water: audition. On a chopped source the fragments are what is
    //  drawn, and the pad that owns the fragment under the finger is the one
    //  that should speak - so the tap is reported and the owner decides,
    //  rather than being assumed to be the selected pad.
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging == 0 && ! panned && sample != nullptr && onAudition)
            onAudition (xToNorm ((float) e.x));
        dragging = 0;
        panned = false;
    }

    //  La rueda amplia en el escritorio. En el telefono no existe, pero el
    //  banco se maqueta en el escritorio y probar el zoom sin ella es probarlo
    //  a traves de los tres botones y nada mas.
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wd) override
    {
        if (sample == nullptr || std::abs (wd.deltaY) < 1.0e-4f) return;
        setZoom (zoom * (wd.deltaY > 0.0f ? 1.25f : 0.8f), xToNorm ((float) e.x));
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // LCD panel.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff112232), b.getCentreX(), b.getY(),
                                                 ZatiColours::screenBg, b.getCentreX(), b.getBottom(), false));
        g.fillRoundedRectangle (b, 2.0f);

        const auto lcdFg = ZatiColours::lcdFg, lcdDim = ZatiColours::lcdDim;
        // Fallback trace when nothing carries a zati yet: the LCD's own
        // foreground, so an un-chopped sample stays achromatic like the rest
        // of the chassis instead of borrowing a hue it has not earned.
        const auto accent = ZatiColours::lcdFg;

        if (sample == nullptr || mins.isEmpty())
        {
            g.setColour (lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.18f));
            g.drawText (T ("TAP A PAD TO LOAD ITS WAVEFORM"), getLocalBounds(), juce::Justification::centred);
            g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
            g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
            return;
        }

        // Waveform region (between readout rows).
        auto wave = b.reduced (10.0f, 0.0f);
        wave.removeFromTop (22.0f);
        wave.removeFromBottom (20.0f);
        const float midY = wave.getCentreY();
        const float h    = wave.getHeight() * 0.5f - 2.0f;
        const int   W    = mins.size();

        // baseline
        g.setColour (activeColour (accent).withAlpha (0.28f));
        g.fillRect (wave.getX(), midY - 0.5f, wave.getWidth(), 1.0f);

        // filled envelope
        juce::Path top;
        for (int x = 0; x < W; ++x)
        {
            const float px = wave.getX() + (float) x / (float) (W - 1) * wave.getWidth();
            (x == 0 ? top.startNewSubPath (px, midY - maxs[x] * h) : top.lineTo (px, midY - maxs[x] * h));
        }
        for (int x = W - 1; x >= 0; --x)
        {
            const float px = wave.getX() + (float) x / (float) (W - 1) * wave.getWidth();
            top.lineTo (px, midY - mins[x] * h);
        }
        top.closeSubPath();

        if (segments.size() > 1)
        {
            // Chopped source: paint the envelope once per fragment, clipped to
            // that fragment's x range, so each slice carries its zati colour.
            for (const auto& s : segments)
            {
                const float x0 = normToX (s.start01);
                const float x1 = normToX (s.end01);
                if (x1 - x0 < 0.5f) continue;

                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (juce::Rectangle<float> (x0, wave.getY(),
                                                            x1 - x0, wave.getHeight()).toNearestInt());
                g.setColour (s.colour.withAlpha (s.selected ? 0.30f : 0.16f));
                g.fillPath (top);
                g.setColour (s.colour.withAlpha (s.selected ? 1.0f : 0.75f));
                g.strokePath (top, juce::PathStrokeType (s.selected ? 1.6f : 1.2f));
            }

            // Cut lines, in a neutral colour so they read as structure rather
            // than as another fragment.
            g.setColour (lcdDim.withAlpha (0.5f));
            for (int i = 1; i < segments.size(); ++i)
            {
                const float cx = normToX (segments[i].start01);
                for (float y = wave.getY(); y < wave.getBottom(); y += 5.0f)
                    g.fillRect (cx, y, 1.0f, 2.5f);
            }

            // Non-chromatic reinforcement: a 4px bar per fragment under the
            // wave, plus the pad number. Colour alone is never the signal.
            const float barY = wave.getBottom() + 1.0f;
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            for (const auto& s : segments)
            {
                const float x0 = normToX (s.start01);
                const float x1 = normToX (s.end01);
                if (x1 - x0 < 1.5f) continue;
                const float bx0 = juce::jlimit (wave.getX(), wave.getRight(), x0);
                const float bx1 = juce::jlimit (wave.getX(), wave.getRight(), x1);
                if (bx1 - bx0 < 1.0f) continue;

                g.setColour (s.colour.withAlpha (s.selected ? 1.0f : 0.7f));
                g.fillRect (bx0 + 0.5f, barY, juce::jmax (1.0f, bx1 - bx0 - 1.0f), 4.0f);

                if (bx1 - bx0 > 16.0f)
                {
                    g.setColour (s.selected ? lcdFg : lcdDim);
                    g.drawText (juce::String (s.padNumber).paddedLeft ('0', 2),
                                juce::Rectangle<float> (bx0, barY + 5.0f, bx1 - bx0, 9.0f),
                                juce::Justification::centred);
                }
            }
        }
        else
        {
            const juce::Colour one = segments.size() == 1 ? segments[0].colour : accent;
            g.setColour (one.withAlpha (0.18f));
            g.fillPath (top);
            g.setColour (one);
            g.strokePath (top, juce::PathStrokeType (1.3f));

            // dim trimmed-out regions
            //  Recortadas al area visible: ampliado, un asa cae a miles de
            //  pixeles fuera y la sombra que la acompana se dibujaria con
            //  ancho negativo o por encima de los rotulos.
            const float sx = normToX (start01);
            const float ex = normToX (end01);
            const float sxV = juce::jlimit (wave.getX(), wave.getRight(), sx);
            const float exV = juce::jlimit (wave.getX(), wave.getRight(), ex);
            g.setColour (ZatiColours::screenBg.withAlpha (0.62f));
            g.fillRect (wave.getX(), wave.getY(), sxV - wave.getX(), wave.getHeight());
            g.fillRect (exV, wave.getY(), wave.getRight() - exV, wave.getHeight());

            // trim handles
            g.setColour (one);
            for (float hx : { sx, ex })
            {
                if (hx < wave.getX() - 2.0f || hx > wave.getRight() + 2.0f) continue;
                g.drawLine (hx, wave.getY(), hx, wave.getBottom(), 1.6f);
                g.fillRect (hx - 3.0f, wave.getY(), 6.0f, 5.0f);
                g.fillRect (hx - 3.0f, wave.getBottom() - 5.0f, 6.0f, 5.0f);
            }
        }

        //  The read head, and behind it the ground it has covered. A line
        //  alone says where; the trail says how far through, which is what
        //  "progress" means and what a bare cursor never showed.
        if (playhead >= 0.0f)
        {
            const float px   = juce::jlimit (wave.getX(), wave.getRight(), normToX (playhead));
            const float from = juce::jlimit (wave.getX(), wave.getRight(),
                                             normToX (juce::jmin (start01, playhead)));

            g.setColour (lcdFg.withAlpha (0.16f));
            g.fillRect (from, wave.getY(), juce::jmax (0.0f, px - from), wave.getHeight());

            if (normToX (playhead) >= wave.getX() - 2.0f && normToX (playhead) <= wave.getRight() + 2.0f)
            {
                g.setColour (ZatiColours::playhead);
                g.fillRect (px - 1.0f, wave.getY() - 3.0f, 2.0f, wave.getHeight() + 6.0f);
                g.fillRect (px - 3.5f, wave.getY() - 5.0f, 7.0f, 3.0f);
            }
        }

        // tick ruler — only when the source is not already carrying the
        // per-fragment bars, which occupy the same strip.
        if (segments.size() <= 1)
        {
            g.setColour (lcdDim.withAlpha (0.45f));
            for (int k = 0; k <= 32; ++k)
            {
                const float tx = wave.getX() + wave.getWidth() * (float) k / 32.0f;
                const float th = (k % 4 == 0) ? 4.0f : 2.0f;
                g.fillRect (tx, wave.getBottom() + 6.0f, 1.0f, th);
            }
        }

        // readouts
        const auto infoFont = ZatiColours::monoFont (Metrics::fLabel, true).withExtraKerningFactor (0.08f);
        g.setFont (infoFont);
        auto top2 = b.reduced (11.0f, 7.0f).removeFromTop (13.0f);
        g.setColour (activeColour (accent));
        g.fillEllipse (top2.getX(), top2.getCentreY() - 3.0f, 6.0f, 6.0f);

        //  These two used to be drawn into the same strip, one flush left and
        //  one flush right, which works right up until the sample is called
        //  something long - and then the file name runs straight through the
        //  channel count and both become unreadable. Measure the right-hand
        //  readout, give it its room, and let the name have what is left.
        auto nameRow = top2.withTrimmedLeft (12.0f);
        if (infoRight.isNotEmpty())
        {
            auto rightRow = nameRow.removeFromRight (juce::GlyphArrangement::getStringWidth (infoFont, infoRight) + 8.0f);
            g.setColour (lcdDim);
            g.drawText (infoRight, rightRow, juce::Justification::topRight);
        }

        g.setColour (lcdFg);
        g.drawFittedText (infoName, nameRow.toNearestInt(), juce::Justification::topLeft, 1, 0.7f);

        // TRIM only makes sense for a single window; on a chopped source each
        // fragment carries its own, and the text would collide with their
        // numbers in the same strip.
        if (segments.size() <= 1)
        {
            auto bot = b.reduced (11.0f, 6.0f).removeFromBottom (12.0f);
            g.setColour (lcdDim);
            g.setFont (ZatiColours::monoFont (Metrics::fMeta, true));
            g.drawText (T ("TRIM") + " " + Lang::ltr (juce::String (start01, 2)
                                                + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 "))
                                                + juce::String (end01, 2)),
                        bot, juce::Justification::bottomLeft);
            //  Y a que aumento se esta mirando. Sin decirlo, una muestra
            //  ampliada y una muestra corta se dibujan igual, y no hay forma
            //  de saber cual de las dos se tiene delante.
            if (zoom > 1.005f)
                g.drawText (Lang::ltr ("x" + juce::String ((int) std::round (zoom))),
                            bot, juce::Justification::bottomRight);
        }

        g.setColour (ZatiColours::knobEdge.withAlpha (0.25f));
        g.drawRoundedRectangle (b.reduced (1.0f), 2.0f, 1.2f);
    }

    void resized() override { computeMinMax(); repaint(); }

private:
    juce::Rectangle<float> waveArea() const
    {
        auto w = getLocalBounds().toFloat().reduced (10.0f, 0.0f);
        w.removeFromTop (22.0f);
        w.removeFromBottom (20.0f);
        return w;
    }
    //  Pantalla -> fichero y fichero -> pantalla. Las dos pasan por la vista,
    //  y son las unicas dos que lo hacen: si alguna cuenta se salta estas dos
    //  funciones, dibuja bien a x1 y mal a cualquier otro zoom.
    float xToNorm (float x) const
    {
        auto w = waveArea();
        return juce::jlimit (0.0f, 1.0f,
                             view0 + (x - w.getX()) / juce::jmax (1.0f, w.getWidth()) / zoom);
    }
    float normToX (float t) const
    {
        auto w = waveArea();
        return w.getX() + (t - view0) * zoom * w.getWidth();
    }
    int dragging = 0;   // 0 none, 1 start, 2 end
    float panFrom = 0.0f, panView = 0.0f;
    bool  panned = false;
    float zoom = 1.0f;      // 1 = el fichero entero
    float view0 = 0.0f;     // borde izquierdo de lo que se ve, 0..1

    void computeMinMax()
    {
        mins.clearQuick(); maxs.clearQuick();
        if (sample == nullptr) return;
        auto& buf = sample->buffer;
        const int len = buf.getNumSamples();
        if (len < 1 || buf.getNumChannels() < 1) return;

        const int W = juce::jmax (1, getWidth() > 0 ? getWidth() - 20 : 320);
        const float* d = buf.getReadPointer (0);

        //  Solo el trozo que se ve, y RESUMIENDOLO OTRA VEZ. Dibujar la
        //  envolvente del fichero entero y estirarla seria un zoom de imagen:
        //  los mismos 300 valores mas gordos. Con el resumen hecho de nuevo
        //  sobre la ventana, a x64 cada columna son once muestras de verdad.
        const juce::int64 from = (juce::int64) (view0 * (float) len);
        const juce::int64 span = juce::jmax ((juce::int64) W,
                                             (juce::int64) ((float) len / zoom));

        for (int x = 0; x < W; ++x)
        {
            int a = (int) juce::jlimit ((juce::int64) 0, (juce::int64) (len - 1),
                                        from + (juce::int64) x * span / W);
            int e = (int) juce::jlimit ((juce::int64) 1, (juce::int64) len,
                                        from + (juce::int64) (x + 1) * span / W);
            if (e <= a) e = a + 1;
            if (e > len) e = len;
            float mn = 1.0f, mx = -1.0f;
            for (int i = a; i < e; ++i) { const float s = d[i]; mn = juce::jmin (mn, s); mx = juce::jmax (mx, s); }
            mins.add (juce::jlimit (-1.0f, 1.0f, mn));
            maxs.add (juce::jlimit (-1.0f, 1.0f, mx));
        }
    }

    juce::Colour activeColour (juce::Colour fallback) const
    {
        for (const auto& s : segments)
            if (s.selected) return s.colour;
        return segments.size() == 1 ? segments[0].colour : fallback;
    }

    juce::Array<Segment> segments;
    SampleBuffer::Ptr sample;
    juce::Array<float> mins, maxs;
    float start01 = 0.0f, end01 = 1.0f;
    float playhead = -1.0f;
    juce::String infoName, infoRight;
};

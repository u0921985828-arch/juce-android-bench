#pragma once

#include <JuceHeader.h>
#include "SampleBuffer.h"
#include "ZatiLookAndFeel.h"
#include "Zati.h"

// ============================================================================
//  PadButton — a sample pad tile: big index (Oswald), sample name (mono), and
//  a mini min/max waveform. States: empty / loaded / selected / playing, with
//  a trigger flash. Original ARTiFACTS "arctic" look. UI thread only.
// ============================================================================
class PadButton : public juce::Button,
                  private juce::Timer
{
public:
    explicit PadButton (int idx) : juce::Button (juce::String (idx + 1)), index (idx) {}

    //  HOLD A PAD TO EDIT IT.
    //
    //  Reaching a pad's own settings took a tap - which PLAYS it, in the
    //  middle of whatever you are recording - and then a second tap on PADS.
    //  Two actions, one of them audible, to answer "what is this pad doing".
    //  Holding the pad goes straight there and never triggers it: the press
    //  that opens the sheet is swallowed.
    //
    //  Fires AT the threshold with the finger still down, like the effect
    //  keys, because a gesture you only find out about on release is one
    //  nobody believes in.
    std::function<void()> onHold;
    static constexpr int kHoldMs = 420;

    //  start01/end01 are the pad's own trim window. After an auto-chop all
    //  sixteen pads point at ONE buffer with sixteen windows, so a sparkline
    //  drawn from the whole buffer made every tile identical — half the tile
    //  carrying no information at all. Each pad draws only its slice.
    void setSampleInfo (SampleBuffer::Ptr sb, const juce::String& name,
                        float start01 = 0.0f, float end01 = 1.0f)
    {
        loaded = (sb != nullptr);
        padName = name;
        buildSpark (sb.get(), start01, end01);
        repaint();
    }
    //  Sixteen envelopes redrawn on every flash is the one cost on this tile
    //  with no musical return, so an entry-level phone spends it on the voice
    //  pool instead (see DeviceTier). The tile keeps its number, its colour
    //  and its name.
    void setArtEnabled (bool shouldDrawArt)
    {
        if (art == shouldDrawArt) return;
        art = shouldDrawArt;
        if (! art) spark.clearQuick();
        repaint();
    }

    //  What is on this pad, for the bench (see UiAudit.h). "Did the sound
    //  survive leaving the app" is the one piece of state a layout dump has
    //  to carry, because it is the one the user keeps losing.
    bool hasSample() const noexcept { return loaded; }
    int  getIndex()  const noexcept { return index; }
    const juce::String& sampleName() const noexcept { return padName; }

    void setZati (int z) { if (zati != z) { zati = z; repaint(); } }
    int  getZati() const { return zati; }
    void setSelected (bool s) { if (selected != s) { selected = s; repaint(); } }
    void setPlaying  (bool p) { if (playing  != p) { playing  = p; repaint(); } }
    void setFlash    (float f) { flash = f; repaint(); }

    //  How hard the last strike was, 0.10 to 1.
    //
    //  A touchscreen has no strike force, so the pad has to get it from
    //  somewhere else. Pressure (MouseEvent::pressure) is the obvious answer
    //  and the wrong one: most Android panels report a constant, and a control
    //  that works on one phone and not the next is worse than none.
    //
    //  Position is what every pad instrument without real sensors uses, and it
    //  is the one thing a touchscreen always knows: low on the tile is soft,
    //  high is hard, the way a drum reads its own head. It also stays playable
    //  with one thumb, which pressure never was.
    float getLastVelocity() const noexcept { return lastVelocity; }

    //  ...and where the platform DOES report a real force, use it.
    //
    //  ON ANDROID TODAY IT NEVER DOES, and that is worth writing down rather
    //  than leaving as a surprise: juce_Windowing_android.cpp hands every
    //  touch event MouseInputSource::defaultPressure, which is 0.0f, and
    //  isPressureValid() is `pressure > 0 && pressure < 1`. So on a phone this
    //  branch is dead and the position rule below is what plays - which was
    //  the original design and is the one that works everywhere.
    //
    //  It stays because it costs one comparison, because it is correct on the
    //  platforms that do supply the figure (a stylus, a desktop tablet), and
    //  because the day JUCE passes MotionEvent.getPressure() through, this
    //  starts working with no other change. What it must not do is pretend:
    //  the announcement it triggers cannot appear on an Android build.
    //
    //  Both paths land on the same 0.35..1 range, so a pattern recorded where
    //  force is measured and played back where it is not is the same
    //  performance rather than a different one.
    void mouseDown (const juce::MouseEvent& e) override
    {
        held = false;
        startTimer (kHoldMs);

        const float h = (float) juce::jmax (1, getHeight());
        const float y = juce::jlimit (0.0f, 1.0f, (float) e.position.y / h);

        //  Struck at the top = 1, at the bottom = 0.35 rather than silence:
        //  the softest edge of the pad still has to make a sound, and a floor
        //  of 0.35 is about 9 dB of range, which is what a finger can aim for.
        const float byPosition = juce::jmap (1.0f - y, 0.35f, 1.0f);

        if (e.isPressureValid())
        {
            //  A finger at rest on a capacitive panel reads around 0.1-0.2 and
            //  a firm strike around 0.5-0.7; full scale is almost never seen,
            //  so mapping 0..1 straight through would make every hit soft.
            //  0.08..0.65 is the range a hand actually covers.
            const float f = juce::jlimit (0.0f, 1.0f, (e.pressure - 0.08f) / (0.65f - 0.08f));
            lastVelocity = juce::jmap (f, 0.35f, 1.0f);
            usedPressure = true;
        }
        else
        {
            lastVelocity = byPosition;
            usedPressure = false;
        }

        juce::Button::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! getLocalBounds().contains (e.getPosition()))
            stopTimer();
        juce::Button::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        stopTimer();
        if (held) { setState (buttonNormal); return; }   // the hold was the gesture
        juce::Button::mouseUp (e);
    }

    //  Which signal the last strike came from, so the app can say so once
    //  rather than leaving the player to guess why the pads got expressive.
    bool lastStrikeUsedPressure() const noexcept { return usedPressure; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        //  Same construction as every other cap in the instrument: the pad is
        //  a printed plate resting on a solid block of ink, and striking it
        //  moves the plate down onto the block. Sixteen of these are the
        //  largest thing on screen, so if they stayed flat while the buttons
        //  gained depth the two would read as belonging to different machines.
        const float lift = ZatiLookAndFeel::kCapLift;
        auto r = getLocalBounds().toFloat().reduced (0.5f).withTrimmedBottom (lift);
        const float rad = 3.0f;   // square, not rounded — matches the flat button caps

        if (down) r = r.translated (0.0f, lift);
        //  El bloque de profundidad, como el de cualquier tapa: OSCURO. Con
        //  ZatiColours::ink salia crema en LACA y hueso en GRAFITO, o sea un
        //  halo claro bajo cada uno de los dieciseis pads.
        else      { g.setColour (ZatiColours::groove (0.32f));
                    g.fillRoundedRectangle (r.translated (0.0f, lift), rad); }

        // A loaded pad wears its zati colour: 30% fill, full-strength border,
        // and a solid top stripe. The stripe plus the always-drawn number are
        // the non-chromatic reinforcement the spec requires — the pad must
        // still be readable when the hue is not.
        const juce::Colour frag = Zati::colour (zati);

        //  An EMPTY pad used to be sixteen identical grey squares: the whole
        //  face went colourless the moment a project was closed, and the grid
        //  said nothing about which pad was which until something was loaded
        //  into it. It carries its own colour now, at a twelfth of the
        //  strength a loaded one does - enough for the matrix to read as a
        //  spectrum from across a table, far too little to be mistaken for a
        //  pad that has a sound in it.
        //  A LOADED pad wears its colour. It was at 30% - a wash so pale that
        //  the zati system, which is the whole way you find a sound on this
        //  grid, said almost nothing. Now that the pads sit on a kraft plate
        //  instead of on paper the same colour has something to read against,
        //  so it can carry its real weight. Empty stays a whisper: the
        //  difference between "this pad IS turquoise" and "this pad would be
        //  turquoise" has to survive a glance.
        juce::Colour base   = loaded ? frag.withMultipliedAlpha (0.62f)
                                     : ZatiColours::padBg2.overlaidWith (frag.withAlpha (0.16f));
        juce::Colour edge   = loaded ? frag : ZatiColours::padBorder;
        //  The number on an empty pad was ink at 30%, on a tint that was barely
        //  there, on a plate the same value as everything else - three weak
        //  contrasts stacked. It is the only thing an empty pad has to say.
        //  MEDIDO CONTRA EL RELLENO QUE LLEVA DEBAJO, no contra la carcasa.
        //  Estos alfas se ajustaron con la tinta de PAPEL, que es casi negra
        //  sobre un fragmento de tono medio. En LACA la tinta es crema, y el
        //  mismo 0.92 sobre el mismo fragmento es un numero claro sobre un
        //  color claro. textOn elige el lado que contrasta y el alfa se queda
        //  diciendo lo que decia: cuanto pesa, no de que color es.
        juce::Colour idxCol = ZatiColours::textOn (base).withAlpha (loaded ? 0.92f : 0.72f);
        //  inkDim on a 30% fragment fill measured 3.25-4.02:1 across the eight
        //  colours — under the 4.5 needed for 9px text on every one of them.
        //  Ink at 0.75 clears it on the worst (5.84:1) and still reads as
        //  secondary against the pad's own numeral.
        juce::Colour nmCol  = ZatiColours::textOn (base).withAlpha (loaded ? 0.75f : 0.60f);
        juce::Colour sparkCol = loaded ? frag.darker (0.35f)
                                       : ZatiColours::textOn (base).withAlpha (0.30f);
        bool onAccent = false;

        if (playing || flash > 0.55f)
        {
            onAccent = true;
            base   = frag;
            edge   = frag.brighter (0.35f);
            //  Pick by measured contrast, not by a brightness threshold. The
            //  old 0.55 cut put red and violet on white at 3.6:1 and 3.9:1
            //  when ink beats white on all eight fragment colours — the
            //  threshold was simply the wrong test.
            idxCol = ZatiColours::bestOn (frag, ZatiColours::ink, juce::Colours::white);
            nmCol  = idxCol.withAlpha (0.9f);
            sparkCol = idxCol.withAlpha (0.85f);
        }
        else if (flash > 0.0f)
        {
            base = base.interpolatedWith (frag, juce::jlimit (0.0f, 1.0f, flash));
        }
        if (down) base = base.darker (0.06f);

        // Body — flat fill, no gradient/sheen.
        g.setColour (base);
        g.fillRoundedRectangle (r, rad);

        // Top stripe (5px): the zati's identity, independent of the fill.
        if (loaded && ! onAccent)
        {
            g.setColour (frag);
            g.fillRect (r.withHeight (5.0f).reduced (1.0f, 0.0f).withY (r.getY() + 1.0f));
        }
        else if (! loaded)
        {
            //  The same stripe, drawn as an outline instead of a fill: the
            //  slot is there and it is that colour, it just has nothing in it
            //  yet. Empty and full read as the same instrument.
            g.setColour (frag.withAlpha (0.30f));
            g.fillRect (r.withHeight (2.0f).reduced (1.0f, 0.0f).withY (r.getY() + 1.0f));
        }

        // Sparkline (behind the labels).
        if (loaded && spark.size() > 2)
        {
            auto wr = r.reduced (9.0f, 0.0f);
            const float cy = r.getCentreY() + 2.0f;
            const float halfH = 15.0f;
            const int n = (int) spark.size() / 2;
            g.setColour (sparkCol.withAlpha (onAccent ? 0.9f : 0.5f));
            juce::Path p;
            for (int i = 0; i < n; ++i)
            {
                const float x = wr.getX() + wr.getWidth() * (float) i / (float) (n - 1);
                const float yTop = cy - spark[(size_t) (i * 2 + 1)] * halfH;
                (i == 0) ? p.startNewSubPath (x, yTop) : p.lineTo (x, yTop);
            }
            for (int i = n - 1; i >= 0; --i)
            {
                const float x = wr.getX() + wr.getWidth() * (float) i / (float) (n - 1);
                const float yBot = cy - spark[(size_t) (i * 2)] * halfH;
                p.lineTo (x, yBot);
            }
            p.closeSubPath();
            g.fillPath (p);
        }

        // Index (Oswald) top-left.
        g.setColour (idxCol);
        g.setFont (ZatiColours::displayFont (juce::jmin (26.0f, r.getHeight() * 0.30f)));
        g.drawText (juce::String (index + 1).paddedLeft ('0', 2),
                    r.reduced (9.0f, 6.0f).removeFromTop (r.getHeight() * 0.42f),
                    juce::Justification::topLeft);

        // Name (mono) bottom.
        g.setColour (nmCol);
        g.setFont (ZatiColours::monoFont (Metrics::fMeta).withExtraKerningFactor (0.06f));
        //  Nothing where there is nothing. An em dash on every empty tile was
        //  sixteen marks that carried no information: the tile already says it
        //  is empty by having no waveform and no name.
        g.drawText (loaded ? padName.toUpperCase() : juce::String(),
                    r.reduced (10.0f, 7.0f).removeFromBottom (12.0f), juce::Justification::bottomLeft, true);

        // Border, then focus. A loaded pad's border is its zati; focus is a
        // second, achromatic ring outside it, so selection never overwrites
        // the fragment's colour with the chassis tone.
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), rad, loaded ? 2.0f : 1.2f);

        if (selected && ! onAccent)
        {
            //  El anillo de seleccion va DENTRO del pad, asi que se mide
            //  contra el pad y no contra la carcasa: en LACA la tinta es crema
            //  y sobre un fragmento claro el anillo desaparecia justo en el
            //  pad que estabas eligiendo.
            g.setColour (ZatiColours::textOn (base).withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (2.4f), juce::jmax (1.0f, rad - 1.5f), 1.4f);
        }

        // Playing: brighten the pad's own colour rather than adding another.
        if (playing)
        {
            g.setColour ((loaded ? frag : ZatiColours::ink).brighter (0.45f).withAlpha (0.6f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.8f);
        }
    }

private:
    void timerCallback() override
    {
        stopTimer();
        held = true;
        if (onHold) onHold();
    }

    bool held = false;

    void buildSpark (const SampleBuffer* sb, float start01, float end01)
    {
        spark.clearQuick();
        if (sb == nullptr || ! art) return;
        const int len = sb->buffer.getNumSamples();
        if (len < 4) return;

        const int a = juce::jlimit (0, len - 2, (int) (juce::jlimit (0.0f, 1.0f, start01) * (float) len));
        const int b = juce::jlimit (a + 1, len, (int) (juce::jlimit (0.0f, 1.0f, end01)   * (float) len));
        const int n = b - a;
        if (n < 2) return;

        // Normalise each slice to its own peak: a quiet tail slice would
        // otherwise draw as a flat line next to a loud transient one, and the
        // point of the tile art is telling them apart.
        const float* d = sb->buffer.getReadPointer (0);
        float peak = 0.0f;
        for (int i = a; i < b; ++i) peak = juce::jmax (peak, std::abs (d[i]));
        const float norm = peak > 1.0e-4f ? 0.95f / peak : 1.4f;

        const int cols = 44;
        for (int c = 0; c < cols; ++c)
        {
            const int i0 = a + (int) ((juce::int64) c       * n / cols);
            const int i1 = a + (int) ((juce::int64) (c + 1) * n / cols);
            float mn = 0.0f, mx = 0.0f;
            for (int i = i0; i < i1 && i < b; ++i) { mn = juce::jmin (mn, d[i]); mx = juce::jmax (mx, d[i]); }
            spark.add (juce::jlimit (-1.0f, 1.0f, mn * norm));
            spark.add (juce::jlimit (-1.0f, 1.0f, mx * norm));
        }
    }

    int index = 0;
    int zati = 0;                 // fragment colour, assigned by cut order
    bool  loaded = false, selected = false, playing = false;
    float lastVelocity = 1.0f;   // set by mouseDown, read by padClicked
    bool  usedPressure = false;  // ...and whether the panel gave a real force
    float flash = 0.0f;
    juce::String padName;
    juce::Array<float> spark;   // interleaved min,max per column
    bool art = true;            // whether this tile draws its waveform at all
};

#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// ============================================================================
//  ZatiLookAndFeel — ARTiFACTS design system (ZATI chassis).
//
//  The chassis is deliberately ACHROMATIC: white and greys, flat square-ish
//  caps (no gradient, no bevel), dark knobs, a dark LCD with warm grey text.
//  An active control announces itself by TONE — a near-black cap with light
//  text — never by hue.
//
//  That restraint is the point. All hue in this instrument belongs to the
//  zati fragment system (see Zati.h); if a pressed button or an armed effect
//  also glowed in a colour, it would compete with the fragments for the same
//  meaning and "one colour per zati" would stop being readable.
//
//  All components read tokens from ZatiColours — no hardcoded colour in a
//  component. Fonts: Oswald/JetBrains Mono, bundled.
// ============================================================================
namespace ZatiColours
{

    // --- Chassis / structure (white + grey) ---
    const juce::Colour chassisTop { 0xffffffff };
    const juce::Colour chassis    { 0xfff5f5f2 };
    const juce::Colour chassisBot { 0xffe8e8e3 };
    const juce::Colour panel      { 0xfff2f2ee };   // section card surface
    const juce::Colour panelDark  { 0xffe3e3dd };   // flat button cap (off state)
    const juce::Colour panelHi    { 0xffffffff };   // panel top bevel
    const juce::Colour panelLo    { 0xffcfcfc7 };   // panel shadow / soft border
    const juce::Colour key        { 0xffe3e3dd };   // flat button cap (off state)
    const juce::Colour keyLit     { 0xffd4d4cc };
    const juce::Colour screw      { 0xffb4b4ac };

    // --- Accent / semantic ------------------------------------------------
    //  The chassis is MONOCHROME on purpose. Hue belongs to the zati fragment
    //  system and nothing else: if a pressed button or an armed effect also
    //  glowed in a colour, that colour would compete with the fragments for
    //  the same meaning and the "one colour per zati" rule would stop reading.
    //  So an active control states itself with TONE — a near-black cap with
    //  light text — never with a hue.
    //
    //  Still mutable, because the skins shift that tone (see setSkin).
    inline juce::Colour amber       { 0xff1f1f1d };   // = accent (name kept for compat)
    inline juce::Colour amberBright { 0xff44443f };
    inline juce::Colour amberDim    { 0xff0d0d0c };
    inline juce::Colour accent      { 0xff1f1f1d };   // preferred names
    inline juce::Colour accentBright{ 0xff44443f };
    inline juce::Colour accentDim   { 0xff0d0d0c };

    //  The only two hues left in the chassis, and both are strictly semantic,
    //  never decorative: red = recording / live playhead, yellow = the step
    //  picked for editing. Neither is ever used to make something look nice.
    const juce::Colour red        { 0xffe0222c };
    const juce::Colour yellow     { 0xfff0b400 };

    inline int currentSkin = 0;
    inline const char* skinName (int i)
    {
        static const char* names[4] = { "TINTA", "GRAFITO", "ACERO", "PLOMO" };
        return names[((i % 4) + 4) % 4];
    }
    const juce::Colour ink        { 0xff1c1c1a };   // primary text on white/grey
    const juce::Colour inkDim     { 0xff6f6f68 };   // secondary text
    const juce::Colour inkLight   { 0xfff5f5f2 };   // text on dark surfaces (knobs/LCD chips)
    const juce::Colour white      { 0xffffffff };
    const juce::Colour cream      { 0xffffffff };   // name kept for compat = white
    const juce::Colour engrave    { 0xff6f6f68 };   // text-dim

    // --- LCD (dark, pops on the white face) ---
    const juce::Colour screenBg   { 0xff101010 };
    const juce::Colour lcdFg      { 0xffe6e6e2 };
    //  Raised from 0xff5c5c56: that was 2.83:1 on the LCD, below the 4.5
    //  minimum, and it carries real information (ruler, cut lines, fragment
    //  numbers, empty-state text), not decoration. Now 5.48:1.
    const juce::Colour lcdDim     { 0xff8a8a83 };

    // --- Pads (neutral when empty; a loaded pad wears its zati colour) ---
    const juce::Colour padTop     { 0xffe3e3dd };
    const juce::Colour padBg2     { 0xffeeeeea };
    const juce::Colour padBorder  { 0xffd0d0c8 };
    inline juce::Colour padLit    { 0xff1f1f1d };   // follows the skin tone

    //  Skins move the chassis TONE, not its hue. Four steps of ink, from
    //  near-black to a mid grey, so an active control reads at whatever
    //  contrast the room needs without ever borrowing a fragment's colour.
    inline void setSkin (int i)
    {
        currentSkin = ((i % 4) + 4) % 4;
        struct S { juce::uint32 a, ab, ad; };
        static constexpr S skins[4] = {
            { 0xff1f1f1d, 0xff44443f, 0xff0d0d0c },   // TINTA   (default, near-black)
            { 0xff333330, 0xff5b5b55, 0xff1c1c1a },   // GRAFITO
            { 0xff4a4a45, 0xff70706a, 0xff2e2e2b },   // ACERO
            { 0xff62625c, 0xff8a8a83, 0xff424240 },   // PLOMO
        };
        const auto s = skins[currentSkin];
        amber = accent = padLit    = juce::Colour (s.a);
        amberBright = accentBright = juce::Colour (s.ab);
        amberDim = accentDim       = juce::Colour (s.ad);
    }

    // --- Knob body (dark — physical-instrument contrast on a white face) ---
    const juce::Colour knobWell   { 0xffe3e3dd };
    const juce::Colour knobEdge   { 0xff3a3a3a };
    const juce::Colour knobBody1  { 0xff3c3c3c };
    const juce::Colour knobBody2  { 0xff1e1e1e };
    const juce::Colour knobBody3  { 0xff0e0e0e };

    // Bundled typefaces (Oswald display + JetBrains Mono). Cached once.
    inline juce::Typeface::Ptr face (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    inline juce::Typeface::Ptr oswaldBold ()   { static auto t = face (BinaryData::OswaldBold_ttf,   BinaryData::OswaldBold_ttfSize);   return t; }
    inline juce::Typeface::Ptr oswaldMedium () { static auto t = face (BinaryData::OswaldMedium_ttf, BinaryData::OswaldMedium_ttfSize); return t; }
    inline juce::Typeface::Ptr monoRegular ()  { static auto t = face (BinaryData::JetBrainsMonoRegular_ttf, BinaryData::JetBrainsMonoRegular_ttfSize); return t; }
    inline juce::Typeface::Ptr monoBold ()     { static auto t = face (BinaryData::JetBrainsMonoBold_ttf,    BinaryData::JetBrainsMonoBold_ttfSize);    return t; }

    // Condensed display face (Oswald) — big pad numerals / wordmark.
    inline juce::Font displayFont (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions().withTypeface (bold ? oswaldBold() : oswaldMedium()).withHeight (h));
    }
    // Monospaced UI/LCD face (JetBrains Mono).
    inline juce::Font monoFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions().withTypeface (bold ? monoBold() : monoRegular()).withHeight (h));
    }
}

// ============================================================================
//  Metrics — one scale for every screen.
//
//  The face had grown ten font sizes (8, 8.5, 9, 9.5, 10, 10.5, 11, 12, 12.5,
//  13.5) and gaps off any grid (6, 15, 18, 23, 34, 42, 92). That reads as
//  untidy even when you cannot name why. Everything here is a multiple of 4,
//  and every size comes from a closed list: if a value is not below, it is
//  wrong.
// ============================================================================
namespace Metrics
{
    // Spacing — multiples of 4 only.
    static constexpr int xs = 4, sm = 8, md = 12, lg = 16, xl = 24;

    // Controls.
    static constexpr int knobSm = 44, knobMd = 56, knobLg = 72;
    static constexpr int btn = 44;    // minimum comfortable touch target
    static constexpr int chip = 24;   // value readout
    static constexpr int tab = 32;    // module bar: it opens windows, it does not act
    static constexpr int row = 44;    // list row

    // Type — four sizes, each with one job.
    static constexpr float fMeta = 10.0f;   // units, secondary facts
    static constexpr float fLabel = 11.0f;  // control names
    static constexpr float fValue = 13.0f;  // readouts
    static constexpr float fDisplay = 20.0f;
    static constexpr float fTitle = 26.0f;
}

namespace ZatiColours
{

    //  Real WCAG contrast, not a brightness proxy. Perceived brightness is a
    //  different curve and it disagrees with the standard exactly where it
    //  matters — picking text over mid-tone fragment colours.
    inline float relativeLuminance (juce::Colour c)
    {
        auto ch = [] (float v)
        {
            v /= 255.0f;
            return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f);
        };
        return 0.2126f * ch ((float) c.getRed())
             + 0.7152f * ch ((float) c.getGreen())
             + 0.0722f * ch ((float) c.getBlue());
    }

    inline float contrastRatio (juce::Colour a, juce::Colour b)
    {
        const float la = relativeLuminance (a), lb = relativeLuminance (b);
        return (juce::jmax (la, lb) + 0.05f) / (juce::jmin (la, lb) + 0.05f);
    }

    //  The legible one of two candidates against a background.
    inline juce::Colour bestOn (juce::Colour bg, juce::Colour a, juce::Colour b)
    {
        return contrastRatio (a, bg) >= contrastRatio (b, bg) ? a : b;
    }

    // A recessed screw head with a slot.
    inline void drawScrew (juce::Graphics& g, float cx, float cy, float r)
    {
        g.setColour (juce::Colour (0xff0e0f10));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        juce::ColourGradient grad (juce::Colour (0xff3a3b3e), cx - r, cy - r,
                                   juce::Colour (0xff1a1b1d), cx + r, cy + r, false);
        g.setGradientFill (grad);
        g.fillEllipse (cx - r + 1.0f, cy - r + 1.0f, r * 2.0f - 2.0f, r * 2.0f - 2.0f);
        g.setColour (juce::Colour (0xff09090a));
        g.drawLine (cx - r * 0.5f, cy - r * 0.12f, cx + r * 0.5f, cy + r * 0.12f, 1.1f);
    }
}

class ZatiLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ZatiLookAndFeel()
    {
        // Value readouts as little dark LCD chips (guaranteed contrast on a white face).
        setColour (juce::Slider::textBoxTextColourId, ZatiColours::lcdFg);
        setColour (juce::Slider::textBoxBackgroundColourId, ZatiColours::screenBg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ZatiColours::amber);
        setColour (juce::Slider::backgroundColourId, ZatiColours::key.darker (0.08f));
        setColour (juce::Slider::thumbColourId, ZatiColours::amber);
        setColour (juce::Label::textColourId, ZatiColours::ink.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOffId, ZatiColours::ink.withAlpha (0.92f));
        setColour (juce::TextButton::textColourOnId, ZatiColours::ink);
        applyBrowserColours();
    }

    // The in-app sample browser reads these; re-applied on every skin change.
    void applyBrowserColours()
    {
        setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, ZatiColours::key);
        setColour (juce::FileBrowserComponent::currentPathBoxTextColourId,       ZatiColours::ink);
        setColour (juce::FileBrowserComponent::currentPathBoxArrowColourId,      ZatiColours::ink);
        setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId,    ZatiColours::screenBg);
        setColour (juce::FileBrowserComponent::filenameBoxTextColourId,          ZatiColours::lcdFg);
        setColour (juce::DirectoryContentsDisplayComponent::highlightColourId,       ZatiColours::accent);
        setColour (juce::DirectoryContentsDisplayComponent::textColourId,            ZatiColours::ink);
        setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId,
                   ZatiColours::accent.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink);
        setColour (juce::ListBox::backgroundColourId, ZatiColours::chassisTop);
        setColour (juce::ListBox::outlineColourId,    ZatiColours::panelLo);
        setColour (juce::ComboBox::backgroundColourId, ZatiColours::key);
        setColour (juce::ComboBox::textColourId,       ZatiColours::ink);
        setColour (juce::ComboBox::arrowColourId,      ZatiColours::ink);
        setColour (juce::ComboBox::outlineColourId,    ZatiColours::panelLo);
    }

    // ---- Browser row: flat, mono type, a coloured tick for directories.
    void drawFileBrowserRow (juce::Graphics& g, int w, int h,
                             const juce::File&, const juce::String& filename,
                             juce::Image* /*icon*/, const juce::String& fileSizeDescription,
                             const juce::String& /*fileTimeDescription*/,
                             bool isDirectory, bool isItemSelected,
                             int itemIndex, juce::DirectoryContentsDisplayComponent&) override
    {
        auto r = juce::Rectangle<int> (0, 0, w, h);

        if (isItemSelected)
        {
            g.setColour (ZatiColours::accent);
            g.fillRect (r);
        }
        else if (itemIndex % 2)
        {
            g.setColour (ZatiColours::ink.withAlpha (0.035f));
            g.fillRect (r);
        }

        const bool onAccent = isItemSelected;
        const juce::Colour fg = onAccent
            ? (ZatiColours::accent.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink)
            : ZatiColours::ink;

        // Kind marker: a filled square for folders, a hollow one for files.
        auto mark = r.removeFromLeft (h).reduced ((h - 9) / 2);
        g.setColour (isDirectory ? (onAccent ? fg : ZatiColours::accent) : fg.withAlpha (0.45f));
        if (isDirectory) g.fillRect (mark);
        else             g.drawRect (mark, 1);

        // Size on the right for files (folders have none).
        auto sizeArea = r.removeFromRight (72);
        if (! isDirectory && fileSizeDescription.isNotEmpty())
        {
            g.setColour (fg.withAlpha (0.55f));
            g.setFont (ZatiColours::monoFont (Metrics::fMeta));
            g.drawText (fileSizeDescription, sizeArea.reduced (6, 0), juce::Justification::centredRight);
        }

        g.setColour (fg);
        g.setFont (ZatiColours::monoFont (Metrics::fValue, isDirectory).withExtraKerningFactor (0.02f));
        g.drawFittedText (filename, r.reduced (6, 0), juce::Justification::centredLeft, 1, 0.9f);
    }

    // ---- Flat dark knob: plain rim, subtle body shade, white needle, blue tip dot.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAng, float endAng,
                           juce::Slider& s) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        const float ang = startAng + pos * (endAng - startAng);

        // Plain rim.
        g.setColour (ZatiColours::knobEdge.withAlpha (0.8f));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        // Body — a soft top-lit radial shade, flat enough to read as "flat".
        const float br = r - 3.0f;
        juce::ColourGradient body (ZatiColours::knobBody1, cx, cy - br * 0.6f,
                                   ZatiColours::knobBody3, cx, cy + br, true);
        body.addColour (0.6, ZatiColours::knobBody2);
        g.setGradientFill (body);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);

        // Needle (drawn) + orange marker dot at the tip.
        juce::Path p;
        p.addRoundedRectangle (-1.3f, -br + 3.0f, 2.6f, br * 0.62f, 1.0f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ZatiColours::white);
        g.fillPath (p);
        auto tip = juce::Point<float> (0.0f, -br + 4.5f)
                     .transformedBy (juce::AffineTransform::rotation (ang).translated (cx, cy));
        // The look-and-feel owns the SHAPE; the pointer colour is injected by
        // the component via rotarySliderFillColourId. That split is what stops
        // a new skin from ever being able to break the zati colour system.
        const auto tipCol = s.isColourSpecified (juce::Slider::rotarySliderFillColourId)
                              ? s.findColour (juce::Slider::rotarySliderFillColourId)
                              : ZatiColours::amber;
        g.setColour (tipCol);
        g.fillEllipse (tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f);
    }

    //  The +/- caps JUCE builds for an IncDecButtons slider are plain
    //  TextButtons wearing whatever the base look-and-feel last set, which
    //  here came out as a dark slate cap with dark text on it - a control you
    //  can see and cannot read. They sit against a value chip, so they take
    //  the chip's palette and read as one unit with it.
    //  A pan control is not a level: its rest position is the middle, not the
    //  left end, so a bar that fills from the left says the wrong thing about
    //  every value it shows. Sliders marked "pan" fill OUT FROM CENTRE and
    //  keep a centre mark drawn over the track, so how far off-centre a
    //  channel sits is readable without a number next to it.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h,
                           float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (! (bool) s.getProperties().getWithDefault ("pan", false))
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, minPos, maxPos, style, s);
            return;
        }

        const auto track = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 2.0f,
                                                   (float) w, 4.0f);
        g.setColour (s.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (track, 2.0f);

        const float centre = (float) x + (float) w * 0.5f;
        g.setColour (s.findColour (juce::Slider::trackColourId));
        g.fillRect (juce::Rectangle<float> (juce::jmin (centre, sliderPos), track.getY(),
                                            std::abs (sliderPos - centre), track.getHeight()));

        g.setColour (ZatiColours::inkDim.withAlpha (0.75f));
        g.fillRect (centre - 0.5f, (float) y + 1.0f, 1.0f, (float) h - 2.0f);

        const float r = juce::jmin (5.5f, (float) h * 0.42f);
        g.setColour (ZatiColours::ink);
        g.fillEllipse (sliderPos - r, track.getCentreY() - r, r * 2.0f, r * 2.0f);
    }

    juce::Button* createSliderButton (juce::Slider&, bool isIncrement) override
    {
        auto* b = new juce::TextButton (isIncrement ? "+" : "-", juce::String());
        b->setColour (juce::TextButton::buttonColourId,  ZatiColours::screenBg);
        b->setColour (juce::TextButton::textColourOffId, ZatiColours::lcdFg);
        b->setColour (juce::TextButton::textColourOnId,  ZatiColours::lcdFg);
        return b;
    }

    // ---- Flat button cap: solid fill, thin hairline border, no gradient/bevel.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = 8.0f;                                   // flat, softly rounded
        const bool on = b.getToggleState();

        auto base = backgroundColour;
        if (down)      base = base.brighter (0.10f);
        else if (over) base = base.brighter (0.05f);
        // A disabled cap reads as inert: desaturated and washed toward the face.
        if (! b.isEnabled())
            base = base.withSaturation (base.getSaturation() * 0.25f)
                       .interpolatedWith (ZatiColours::chassis, 0.55f);

        g.setColour (base);
        g.fillRoundedRectangle (r, rad);

        // Border. The old test sniffed the cap's HUE to decide whether it was
        // "accented" — meaningless now that the accent is monochrome, so it
        // compares against the accent tone directly. And the border must
        // contrast with the cap it sits on: for an active (near-black) cap
        // that means a LIGHT hairline, not a darker one.
        const bool accented = (base == ZatiColours::accent || base == ZatiColours::accentDim);
        if (on || accented)
        {
            const bool darkCap = base.getPerceivedBrightness() < 0.5f;
            g.setColour (darkCap ? ZatiColours::inkLight.withAlpha (0.55f)
                                 : juce::Colours::black.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.4f);
        }
        else
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto t = b.getButtonText();
        const auto off = b.findColour (juce::TextButton::textColourOffId);
        const auto on  = b.findColour (juce::TextButton::textColourOnId);

        // Pads: big Oswald numeral top, sample name (mono) along the bottom.
        if ((bool) b.getProperties().getWithDefault ("pad", false))
        {
            auto area = b.getLocalBounds();
            auto strip = area.removeFromBottom (16);
            g.setColour (off);
            g.setFont (ZatiColours::displayFont (juce::jmin (48.0f, (float) b.getHeight() * 0.44f)));
            g.drawText (t, area, juce::Justification::centred);

            const auto fn = b.getProperties().getWithDefault ("fn", juce::String()).toString();
            if (fn.isNotEmpty())
            {
                g.setColour (ZatiColours::inkDim);
                g.setFont (ZatiColours::monoFont (Metrics::fLabel).withExtraKerningFactor (0.02f));
                g.drawFittedText (fn, strip.reduced (6, 0), juce::Justification::centred, 1, 0.85f);
            }
            return;
        }

        g.setFont (ZatiColours::monoFont (juce::jlimit (10.0f, 14.5f, (float) b.getHeight() * 0.38f), true)
                     .withExtraKerningFactor (0.06f));
        //  Text colour is chosen when a button is built, but the cap it lands
        //  on can change afterwards - a button styled for a light cap and then
        //  given a dark one for its ON state ends up with dark text on dark,
        //  which is a label you cannot read at exactly the moment it matters.
        //  Decide against the cap that is actually being painted.
        const bool lit = b.getToggleState();
        auto col = lit ? on : off;
        const auto cap = b.findColour (lit ? juce::TextButton::buttonOnColourId
                                           : juce::TextButton::buttonColourId);

        if (std::abs (cap.getPerceivedBrightness() - col.getPerceivedBrightness()) < 0.30f)
            col = cap.getPerceivedBrightness() < 0.5f ? ZatiColours::inkLight : ZatiColours::ink;

        g.setColour (col.withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (t, b.getLocalBounds().reduced (5, 2), juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return ZatiColours::monoFont (Metrics::fValue, true);
    }
};

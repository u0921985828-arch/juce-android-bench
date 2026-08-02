#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// ============================================================================
//  ShardLookAndFeel — ARTiFACTS design system (COLORS skin).
//  White + grey base, flat square-ish caps (no gradient/bevel), the three
//  primaries — blue/red/yellow — as the only accent colours (blue is the
//  default "main" one), dark knobs with a blue position dot, a dark LCD with
//  warm grey text. A toy-sampler look, powerful underneath. All components
//  read tokens (ShardColours) — no hardcoded colour in components. Fonts:
//  Oswald/JetBrains Mono, bundled.
// ============================================================================
namespace ShardColours
{
    //  ARTiFACTS skin: COLORS — white/grey base, three primary accents.

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

    // --- Accent / semantic — the three primaries + white/grey base ---
    //  The accent set is MUTABLE: COLORS ships 4 skins (AZUL / ROJO /
    //  AMARILLO / TINTA) that swap only the accent colour — the white/grey
    //  chassis and the semantic red/yellow stay fixed. Components read these
    //  at paint time, so setSkin() + a full repaint restyles the machine.
    inline juce::Colour amber       { 0xff2f6fed };   // = accent (name kept for compat)
    inline juce::Colour amberBright { 0xff5f92f4 };
    inline juce::Colour amberDim    { 0xff1f52b8 };
    inline juce::Colour accent      { 0xff2f6fed };   // preferred names
    inline juce::Colour accentBright{ 0xff5f92f4 };
    inline juce::Colour accentDim   { 0xff1f52b8 };
    const juce::Colour red        { 0xffe0222c };   // true red — REC / destructive
    const juce::Colour yellow     { 0xfff0b400 };   // true yellow — selection / highlight

    inline int currentSkin = 0;
    inline const char* skinName (int i)
    {
        static const char* names[4] = { "AZUL", "ROJO", "AMARILLO", "TINTA" };
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
    const juce::Colour lcdDim     { 0xff5c5c56 };

    // --- Pads (white/grey flat, accent when loaded) ---
    const juce::Colour padTop     { 0xffe3e3dd };
    const juce::Colour padBg2     { 0xffeeeeea };
    const juce::Colour padBorder  { 0xffd0d0c8 };
    inline juce::Colour padLit    { 0xff2f6fed };   // follows the skin accent

    inline void setSkin (int i)
    {
        currentSkin = ((i % 4) + 4) % 4;
        struct S { juce::uint32 a, ab, ad; };
        static constexpr S skins[4] = {
            { 0xff2f6fed, 0xff5f92f4, 0xff1f52b8 },   // AZUL     (default)
            { 0xffe0222c, 0xffef5a62, 0xffab141c },   // ROJO
            { 0xffdda400, 0xfff5c832, 0xffa87d00 },   // AMARILLO (darkened for contrast)
            { 0xff2b2b28, 0xff55554f, 0xff101010 },   // TINTA    (near-black)
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

class ShardLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShardLookAndFeel()
    {
        // Value readouts as little dark LCD chips (guaranteed contrast on a white face).
        setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ShardColours::amber);
        setColour (juce::Slider::backgroundColourId, ShardColours::key.darker (0.08f));
        setColour (juce::Slider::thumbColourId, ShardColours::amber);
        setColour (juce::Label::textColourId, ShardColours::ink.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOffId, ShardColours::ink.withAlpha (0.92f));
        setColour (juce::TextButton::textColourOnId, ShardColours::ink);
        applyBrowserColours();
    }

    // The in-app sample browser reads these; re-applied on every skin change.
    void applyBrowserColours()
    {
        setColour (juce::FileBrowserComponent::currentPathBoxBackgroundColourId, ShardColours::key);
        setColour (juce::FileBrowserComponent::currentPathBoxTextColourId,       ShardColours::ink);
        setColour (juce::FileBrowserComponent::currentPathBoxArrowColourId,      ShardColours::ink);
        setColour (juce::FileBrowserComponent::filenameBoxBackgroundColourId,    ShardColours::screenBg);
        setColour (juce::FileBrowserComponent::filenameBoxTextColourId,          ShardColours::lcdFg);
        setColour (juce::DirectoryContentsDisplayComponent::highlightColourId,       ShardColours::accent);
        setColour (juce::DirectoryContentsDisplayComponent::textColourId,            ShardColours::ink);
        setColour (juce::DirectoryContentsDisplayComponent::highlightedTextColourId,
                   ShardColours::accent.getPerceivedBrightness() < 0.5f ? ShardColours::inkLight : ShardColours::ink);
        setColour (juce::ListBox::backgroundColourId, ShardColours::chassisTop);
        setColour (juce::ListBox::outlineColourId,    ShardColours::panelLo);
        setColour (juce::ComboBox::backgroundColourId, ShardColours::key);
        setColour (juce::ComboBox::textColourId,       ShardColours::ink);
        setColour (juce::ComboBox::arrowColourId,      ShardColours::ink);
        setColour (juce::ComboBox::outlineColourId,    ShardColours::panelLo);
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
            g.setColour (ShardColours::accent);
            g.fillRect (r);
        }
        else if (itemIndex % 2)
        {
            g.setColour (ShardColours::ink.withAlpha (0.035f));
            g.fillRect (r);
        }

        const bool onAccent = isItemSelected;
        const juce::Colour fg = onAccent
            ? (ShardColours::accent.getPerceivedBrightness() < 0.5f ? ShardColours::inkLight : ShardColours::ink)
            : ShardColours::ink;

        // Kind marker: a filled square for folders, a hollow one for files.
        auto mark = r.removeFromLeft (h).reduced ((h - 9) / 2);
        g.setColour (isDirectory ? (onAccent ? fg : ShardColours::accent) : fg.withAlpha (0.45f));
        if (isDirectory) g.fillRect (mark);
        else             g.drawRect (mark, 1);

        // Size on the right for files (folders have none).
        auto sizeArea = r.removeFromRight (72);
        if (! isDirectory && fileSizeDescription.isNotEmpty())
        {
            g.setColour (fg.withAlpha (0.55f));
            g.setFont (ShardColours::monoFont (9.5f));
            g.drawText (fileSizeDescription, sizeArea.reduced (6, 0), juce::Justification::centredRight);
        }

        g.setColour (fg);
        g.setFont (ShardColours::monoFont (12.0f, isDirectory).withExtraKerningFactor (0.02f));
        g.drawFittedText (filename, r.reduced (6, 0), juce::Justification::centredLeft, 1, 0.9f);
    }

    // ---- Flat dark knob: plain rim, subtle body shade, white needle, blue tip dot.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAng, float endAng,
                           juce::Slider&) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        const float ang = startAng + pos * (endAng - startAng);

        // Plain rim.
        g.setColour (ShardColours::knobEdge.withAlpha (0.8f));
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        // Body — a soft top-lit radial shade, flat enough to read as "flat".
        const float br = r - 3.0f;
        juce::ColourGradient body (ShardColours::knobBody1, cx, cy - br * 0.6f,
                                   ShardColours::knobBody3, cx, cy + br, true);
        body.addColour (0.6, ShardColours::knobBody2);
        g.setGradientFill (body);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);

        // Needle (drawn) + orange marker dot at the tip.
        juce::Path p;
        p.addRoundedRectangle (-1.3f, -br + 3.0f, 2.6f, br * 0.62f, 1.0f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::white);
        g.fillPath (p);
        auto tip = juce::Point<float> (0.0f, -br + 4.5f)
                     .transformedBy (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::amber);
        g.fillEllipse (tip.x - 2.6f, tip.y - 2.6f, 5.2f, 5.2f);
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
                       .interpolatedWith (ShardColours::chassis, 0.55f);

        g.setColour (base);
        g.fillRoundedRectangle (r, rad);

        // Border: accent when lit/triggered, otherwise a soft hairline.
        const float accentHue = ShardColours::amber.getHue();
        const bool accenty = std::abs (base.getHue() - accentHue) < 0.06f && base.getSaturation() > 0.28f;
        if (on || accenty)
        {
            g.setColour (ShardColours::amberBright.withAlpha (0.9f));
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
            g.setFont (ShardColours::displayFont (juce::jmin (48.0f, (float) b.getHeight() * 0.44f)));
            g.drawText (t, area, juce::Justification::centred);

            const auto fn = b.getProperties().getWithDefault ("fn", juce::String()).toString();
            if (fn.isNotEmpty())
            {
                g.setColour (ShardColours::inkDim);
                g.setFont (ShardColours::monoFont (11.0f).withExtraKerningFactor (0.02f));
                g.drawFittedText (fn, strip.reduced (6, 0), juce::Justification::centred, 1, 0.85f);
            }
            return;
        }

        g.setFont (ShardColours::monoFont (juce::jlimit (10.0f, 14.5f, (float) b.getHeight() * 0.38f), true)
                     .withExtraKerningFactor (0.06f));
        g.setColour ((b.getToggleState() ? on : off).withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.45f));
        g.drawFittedText (t, b.getLocalBounds().reduced (5, 2), juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return ShardColours::monoFont (13.5f, true);
    }
};

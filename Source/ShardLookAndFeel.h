#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// ============================================================================
//  ShardLookAndFeel — ARTiFACTS design system (FX-404 skin).
//  Dark near-black chassis, warm cream control surfaces, flat square-ish caps
//  (no gradient/bevel), orange primary accent + pink-magenta for record /
//  destructive actions, dark knobs with an orange position dot, a dark LCD
//  with warm grey text. All components read tokens (ShardColours) — no
//  hardcoded colour in components. Fonts: Oswald/JetBrains Mono, bundled.
// ============================================================================
namespace ShardColours
{
    //  ARTiFACTS skin: FX-404 — dark chassis, cream surfaces, flat orange.

    // --- Chassis / structure ---
    const juce::Colour chassisTop { 0xfff1e9d4 };   // cream surface gradient top
    const juce::Colour chassis    { 0xffe8dfc9 };
    const juce::Colour chassisBot { 0xffddd2b8 };
    const juce::Colour panel      { 0xffefe6d0 };   // section card surface
    const juce::Colour panelDark  { 0xff1c1c1c };   // flat button cap (off state)
    const juce::Colour panelHi    { 0xffffffff };   // panel top bevel
    const juce::Colour panelLo    { 0xffcabf9f };   // panel shadow / soft border
    const juce::Colour key        { 0xff1c1c1c };   // flat button cap (off state)
    const juce::Colour keyLit     { 0xff2a2a2a };
    const juce::Colour screw      { 0xff3a3a3a };

    // --- Accent / semantic ---
    const juce::Colour amber      { 0xffe8823c };   // orange accent — name kept for compat
    const juce::Colour amberBright { 0xfff5a565 };
    const juce::Colour amberDim   { 0xffb5642c };
    const juce::Colour accent      { 0xffe8823c };   // preferred names
    const juce::Colour accentBright { 0xfff5a565 };
    const juce::Colour accentDim   { 0xffb5642c };
    const juce::Colour red        { 0xffe0538f };   // pink-magenta — REC / destructive
    const juce::Colour yellow     { 0xffe0a318 };   // third primary — sequencer selection only
    const juce::Colour ink        { 0xff262117 };   // primary text on cream
    const juce::Colour inkDim     { 0xff8a8064 };   // secondary text on cream
    const juce::Colour inkLight   { 0xffe8823c };   // text on dark (flat button) caps — orange
    const juce::Colour white      { 0xfff1ece0 };   // near-white, for knob needles etc.
    const juce::Colour cream      { 0xffe8dfc9 };   // name kept for compat = chassis cream
    const juce::Colour engrave    { 0xff8a8064 };   // text-dim

    // --- LCD (stays dark — pops on a cream machine) ---
    const juce::Colour screenBg   { 0xff0a0a0a };
    const juce::Colour lcdFg      { 0xffd8d2c4 };   // warm grey-white
    const juce::Colour lcdDim     { 0xff5c584c };

    // --- Pads (flat dark, orange numerals) ---
    const juce::Colour padTop     { 0xff1c1c1c };
    const juce::Colour padBg2     { 0xff141414 };
    const juce::Colour padBorder  { 0xff333026 };
    const juce::Colour padLit     { 0xffe8823c };

    // --- Knob body (dark, flat-ish) ---
    const juce::Colour knobWell   { 0xff141414 };
    const juce::Colour knobEdge   { 0xff3a3a3a };
    const juce::Colour knobBody1  { 0xff2c2c2c };
    const juce::Colour knobBody2  { 0xff1c1c1c };
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
        // Value readouts as little dark LCD chips (guaranteed contrast on a cream face).
        setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ShardColours::amber);
        setColour (juce::Slider::backgroundColourId, ShardColours::key.brighter (0.08f));
        setColour (juce::Slider::thumbColourId, ShardColours::amber);
        setColour (juce::Label::textColourId, ShardColours::ink.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOffId, ShardColours::inkLight.withAlpha (0.95f));
        setColour (juce::TextButton::textColourOnId, ShardColours::ink);
    }

    // ---- Flat dark knob: plain rim, subtle body shade, white needle, orange tip dot.
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
        g.setColour (b.getToggleState() ? on : off);
        g.drawFittedText (t, b.getLocalBounds().reduced (5, 2), juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return ShardColours::monoFont (13.5f, true);
    }
};

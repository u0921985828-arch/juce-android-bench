#pragma once

#include <JuceHeader.h>
#include "BinaryData.h"

// ============================================================================
//  ShardLookAndFeel — ARTiFACTS design system (IVORY skin default).
//  Grey chassis, orange accent, SQUARE geometry, physical knobs (knurl + double
//  bevel + drawn needle), dark pads with orange numerals, cool-blue LCD. All
//  components read tokens (ShardColours) — no hardcoded colour in components.
//  Values from the ARTiFACTS style guide. Fonts: Oswald/JetBrains Mono are
//  approximated with a condensed display face + the default monospaced face
//  until the real families are bundled.
// ============================================================================
namespace ShardColours
{
    //  ARTiFACTS skin: ARCTIC-PRECISION — light "lab instrument", cold blue
    //  accent. Distinct from any dark/orange hardware. Light surfaces, navy ink,
    //  dark metallic knobs, a dark LCD with a blue trace.

    // --- Chassis / structure (light ice) ---
    const juce::Colour chassisTop { 0xfff4f8fc };
    const juce::Colour chassis    { 0xffe8eef5 };
    const juce::Colour chassisBot { 0xffd6e2ee };
    const juce::Colour panel      { 0xffe4eef8 };
    const juce::Colour panelDark  { 0xffdceafb };   // key surface
    const juce::Colour panelHi    { 0xffffffff };   // panel top bevel
    const juce::Colour panelLo    { 0xff9fb8d0 };   // panel shadow / soft border
    const juce::Colour key        { 0xffdceafb };
    const juce::Colour keyLit     { 0xffc8dcf0 };
    const juce::Colour screw      { 0xff8fa6bd };

    // --- Accent / semantic ---
    const juce::Colour amber      { 0xff4fa3ff };   // (accent blue) — name kept for compat
    const juce::Colour amberBright { 0xff7dbcff };
    const juce::Colour amberDim   { 0xff2f7ad1 };
    const juce::Colour accent      { 0xff4fa3ff };   // preferred names
    const juce::Colour accentBright { 0xff7dbcff };
    const juce::Colour accentDim   { 0xff2f7ad1 };
    const juce::Colour red        { 0xffc1123b };
    const juce::Colour yellow     { 0xffe0a318 };   // third primary — selection / highlight only
    const juce::Colour ink        { 0xff0d2438 };   // primary text on light
    const juce::Colour inkDim     { 0xff3f6a8f };   // secondary text
    const juce::Colour inkLight   { 0xfff4f8fc };   // text on dark surfaces
    const juce::Colour white      { 0xff0d2438 };   // name kept for compat = ink
    const juce::Colour cream      { 0xff0d2438 };   // name kept for compat = ink
    const juce::Colour engrave    { 0xff3f6a8f };   // text-dim

    // --- LCD (stays dark — pops on a light machine) ---
    const juce::Colour screenBg   { 0xff0a0e13 };
    const juce::Colour lcdFg      { 0xffdfe6f0 };
    const juce::Colour lcdDim     { 0xff4a5768 };

    // --- Pads (white -> ice) ---
    const juce::Colour padTop     { 0xffffffff };    // padbtn bg1
    const juce::Colour padBg2     { 0xffeaf2fb };
    const juce::Colour padBorder  { 0xffbcd4ee };
    const juce::Colour padLit     { 0xff4fa3ff };

    // --- Knob body (dark metallic) ---
    const juce::Colour knobWell   { 0xffc4d3e2 };
    const juce::Colour knobEdge   { 0xff7fa8cf };
    const juce::Colour knobBody1  { 0xff3c3e42 };
    const juce::Colour knobBody2  { 0xff1c1d1f };
    const juce::Colour knobBody3  { 0xff0a0a0b };

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
        // Value readouts as little dark LCD chips (guaranteed contrast on a light face).
        setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        setColour (juce::Slider::textBoxBackgroundColourId, ShardColours::screenBg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ShardColours::amber);
        setColour (juce::Slider::backgroundColourId, ShardColours::key.darker (0.08f));
        setColour (juce::Slider::thumbColourId, ShardColours::amber);
        setColour (juce::Label::textColourId, ShardColours::ink.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOffId, ShardColours::ink.withAlpha (0.92f));
        setColour (juce::TextButton::textColourOnId, ShardColours::ink);
    }

    // ---- Physical rotary knob: knurled ring, double bevel, drawn needle.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAng, float endAng,
                           juce::Slider&) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        const float ang = startAng + pos * (endAng - startAng);

        // Outer double bevel.
        g.setColour (ShardColours::padBorder);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (ShardColours::knobEdge.withAlpha (0.9f));
        g.drawEllipse (cx - r + 0.6f, cy - r + 0.6f, r * 2.0f - 1.2f, r * 2.0f - 1.2f, 1.1f);

        // Knurled ring: alternating light/dark teeth near the rim.
        const int teeth = 40;
        for (int i = 0; i < teeth; ++i)
        {
            const float a  = (float) i / (float) teeth * juce::MathConstants<float>::twoPi;
            const float ca = std::cos (a), sa = std::sin (a);
            g.setColour ((i % 2 == 0) ? juce::Colour (0xff4a4d52) : juce::Colour (0xff17181a));
            g.drawLine (cx + ca * (r - 1.5f), cy + sa * (r - 1.5f),
                        cx + ca * (r - 4.5f), cy + sa * (r - 4.5f), 1.4f);
        }

        // Body — radial gradient (top-lit).
        const float br = r - 5.5f;
        juce::ColourGradient body (ShardColours::knobBody1, cx, cy - br * 0.6f,
                                   ShardColours::knobBody3, cx, cy + br, true);
        body.addColour (0.55, ShardColours::knobBody2);
        g.setGradientFill (body);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.setColour (juce::Colour (0x33ffffff));                 // specular
        g.drawEllipse (cx - br + 1.6f, cy - br + 1.6f, br * 2.0f - 3.2f, br * 1.2f, 1.0f);
        g.setColour (ShardColours::padBorder.withAlpha (0.8f));
        g.drawEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f, 1.0f);

        // Needle (drawn) + accent marker at the tip.
        juce::Path p;
        p.addRoundedRectangle (-1.4f, -br + 2.5f, 2.8f, br * 0.7f, 1.0f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::white);
        g.fillPath (p);
        auto tip = juce::Point<float> (0.0f, -br + 3.0f)
                     .transformedBy (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::amber);
        g.fillEllipse (tip.x - 2.0f, tip.y - 2.0f, 4.0f, 4.0f);
    }

    // ---- Square button cap with a 2px bevel; accent fill/glow when active.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = 2.0f;                                   // square-ish
        const bool on = b.getToggleState();

        auto base = backgroundColour;
        if (down)      base = base.brighter (0.12f);
        else if (over) base = base.brighter (0.05f);

        juce::ColourGradient grad (base.brighter (0.12f), r.getX(), r.getY(),
                                   base.darker (0.28f),  r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, rad);

        // Bevel: top highlight + bottom shadow.
        g.setColour (juce::Colour (0x22ffffff));
        g.drawLine (r.getX() + rad, r.getY() + 0.9f, r.getRight() - rad, r.getY() + 0.9f, 1.2f);
        g.setColour (juce::Colour (0x50000000));
        g.drawLine (r.getX() + rad, r.getBottom() - 0.9f, r.getRight() - rad, r.getBottom() - 0.9f, 1.2f);

        // Border: accent when lit/triggered, otherwise a soft hairline.
        const float lum = base.getPerceivedBrightness();
        const float accentHue = ShardColours::amber.getHue();
        const bool accenty = std::abs (base.getHue() - accentHue) < 0.06f && base.getSaturation() > 0.28f;
        if (on || accenty)
        {
            g.setColour (ShardColours::amberDim.withAlpha (0.95f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.6f);
        }
        else
        {
            g.setColour (ShardColours::padBorder);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
        }
        juce::ignoreUnused (lum);
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

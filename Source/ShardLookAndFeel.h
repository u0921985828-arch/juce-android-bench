#pragma once

#include <JuceHeader.h>

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
    // --- Chassis / structure ---
    const juce::Colour chassisTop { 0xff4a4d52 };
    const juce::Colour chassis    { 0xff3a3c40 };
    const juce::Colour chassisBot { 0xff28292c };
    const juce::Colour panel      { 0xff3f4247 };
    const juce::Colour panelDark  { 0xff2c2e32 };
    const juce::Colour panelHi    { 0xff5a5d63 };   // panel top bevel
    const juce::Colour panelLo    { 0xff141517 };   // panel shadow / hard border
    const juce::Colour key        { 0xff1c1d20 };
    const juce::Colour keyLit     { 0xff2a2c30 };
    const juce::Colour screw      { 0xff1a1b1d };

    // --- Accent / semantic ---
    const juce::Colour amber      { 0xffff9a3d };   // (accent) — name kept for compat
    const juce::Colour amberBright { 0xffffbf6b };
    const juce::Colour amberDim   { 0xffc9772c };
    const juce::Colour red        { 0xffff3d6e };
    const juce::Colour white      { 0xffe8e9eb };
    const juce::Colour cream      { 0xffe8e9eb };    // name kept for compat
    const juce::Colour engrave    { 0xffaeb1b8 };    // text-dim

    // --- LCD ---
    const juce::Colour screenBg   { 0xff0a0e13 };
    const juce::Colour lcdFg      { 0xffdfe6f0 };
    const juce::Colour lcdDim     { 0xff4a5768 };

    // --- Pads ---
    const juce::Colour padTop     { 0xff26282b };    // padbtn bg1
    const juce::Colour padBg2     { 0xff111214 };
    const juce::Colour padBorder  { 0xff050506 };
    const juce::Colour padLit     { 0xffff9a3d };

    // --- Knob body ---
    const juce::Colour knobWell   { 0xff0a0a0b };
    const juce::Colour knobEdge   { 0xff45484d };
    const juce::Colour knobBody1  { 0xff3c3e42 };
    const juce::Colour knobBody2  { 0xff1c1d1f };
    const juce::Colour knobBody3  { 0xff0a0a0b };

    // Condensed display face (Oswald stand-in) — big pad numerals / wordmark.
    inline juce::Font displayFont (float h, bool bold = true)
    {
        return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain))
                 .withHorizontalScale (0.86f);
    }
    // Monospaced UI/LCD face (JetBrains Mono stand-in).
    inline juce::Font monoFont (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (h)
                             .withName (juce::Font::getDefaultMonospacedFontName())
                             .withStyle (bold ? "Bold" : "Regular"));
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
        setColour (juce::Slider::textBoxTextColourId, ShardColours::lcdFg);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ShardColours::amber);
        setColour (juce::Slider::backgroundColourId, ShardColours::key);
        setColour (juce::Slider::thumbColourId, ShardColours::amber);
        setColour (juce::Label::textColourId, ShardColours::white.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, ShardColours::white.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOnId, juce::Colour (0xff0a0b0d));
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

        // Border: accent when lit/triggered, otherwise a hard dark hairline.
        const float lum = base.getPerceivedBrightness();
        const bool accenty = base.getHue() > 0.03f && base.getHue() < 0.13f && base.getSaturation() > 0.3f;
        if (on || (accenty && lum > 0.45f))
        {
            g.setColour (ShardColours::amberBright.withAlpha (juce::jlimit (0.3f, 0.95f, lum)));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.6f);
        }
        else
        {
            g.setColour (ShardColours::padBorder);
            g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.2f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto t = b.getButtonText();
        const bool bigNum = t.length() <= 2 && t.containsOnly ("0123456789") && b.getHeight() > 60;

        if (bigNum)
            g.setFont (ShardColours::displayFont (juce::jmin (46.0f, (float) b.getHeight() * 0.42f)));
        else
            g.setFont (ShardColours::monoFont (juce::jlimit (10.0f, 14.5f, (float) b.getHeight() * 0.38f), true)
                         .withExtraKerningFactor (0.06f));

        g.setColour (b.getToggleState() ? b.findColour (juce::TextButton::textColourOnId)
                                        : b.findColour (juce::TextButton::textColourOffId));
        g.drawFittedText (t, b.getLocalBounds().reduced (5, 2), juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return ShardColours::monoFont (12.0f);
    }
};

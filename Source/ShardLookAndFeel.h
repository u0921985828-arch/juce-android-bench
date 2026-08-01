#pragma once

#include <JuceHeader.h>

// ============================================================================
//  ShardLookAndFeel — original "reimagined vintage hardware" identity.
//  Warm chassis, amber phosphor accents, tactile rotary knobs and embossed
//  button caps. Entirely our own palette + drawing — NOT a copy of any
//  hardware, and no borrowed effect/model names.
// ============================================================================
namespace ShardColours
{
    const juce::Colour chassisTop { 0xff221d15 };   // warm chassis (top-lit)
    const juce::Colour chassisBot { 0xff120f0a };
    const juce::Colour chassis    { 0xff17140f };
    const juce::Colour panel      { 0xff2c261c };   // raised sub-panels
    const juce::Colour panelHi    { 0xff473c2a };   // panel top bevel
    const juce::Colour panelLo    { 0xff0b0906 };   // panel bottom shadow
    const juce::Colour screenBg   { 0xff0c110d };
    const juce::Colour amber      { 0xffe6a94a };    // phosphor accent
    const juce::Colour amberBright { 0xffffd27e };
    const juce::Colour amberDim   { 0xff7a5a28 };
    const juce::Colour cream      { 0xfff1e9d4 };
    const juce::Colour engrave    { 0xff9a8f7c };    // engraved grey label
    const juce::Colour knobWell   { 0xff100d09 };
    const juce::Colour knobBody   { 0xff2f2a20 };
    const juce::Colour knobBodyLo { 0xff1c1813 };
    const juce::Colour knobEdge   { 0xff5a4d38 };
    const juce::Colour track      { 0xff35301f };
    const juce::Colour padTop     { 0xff2c2820 };
    const juce::Colour padLit     { 0xffe6a94a };
    const juce::Colour screw      { 0xff4a4437 };

    // A brushed metal screw head with a slot — chassis hardware cue.
    inline void drawScrew (juce::Graphics& g, float cx, float cy, float r)
    {
        juce::ColourGradient grad (juce::Colour (0xff6a6252), cx - r, cy - r,
                                   juce::Colour (0xff2a271f), cx + r, cy + r, false);
        g.setGradientFill (grad);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour (juce::Colour (0xff141109));
        g.drawEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);
        g.setColour (juce::Colour (0xff0d0b07));
        g.drawLine (cx - r * 0.55f, cy - r * 0.15f, cx + r * 0.55f, cy + r * 0.15f, 1.2f);
        g.setColour (juce::Colour (0x22ffffff));
        g.drawLine (cx - r * 0.55f, cy - r * 0.15f - 1.0f, cx + r * 0.55f, cy + r * 0.15f - 1.0f, 0.8f);
    }
}

class ShardLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShardLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ShardColours::cream);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::trackColourId, ShardColours::amber);
        setColour (juce::Label::textColourId, ShardColours::cream.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, ShardColours::cream.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }

    // ---- Rotary knob: recessed well, tick ring, top-lit body, bright pointer.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAng, float endAng,
                           juce::Slider&) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        const float ang = startAng + pos * (endAng - startAng);

        // Recessed well.
        g.setColour (ShardColours::knobWell);
        g.fillEllipse (cx - r, cy - r, r * 2.0f, r * 2.0f);

        // Tick marks around the dial.
        g.setColour (ShardColours::amber.withAlpha (0.30f));
        for (int t = 0; t <= 10; ++t)
        {
            const float a  = startAng + (float) t / 10.0f * (endAng - startAng);
            const float r0 = r - 1.0f, r1 = r - 4.0f;
            g.drawLine (cx + std::cos (a - juce::MathConstants<float>::halfPi) * r1,
                        cy + std::sin (a - juce::MathConstants<float>::halfPi) * r1,
                        cx + std::cos (a - juce::MathConstants<float>::halfPi) * r0,
                        cy + std::sin (a - juce::MathConstants<float>::halfPi) * r0, 1.0f);
        }

        // Value arc (bright amber) over a dim track.
        juce::Path track, val;
        track.addCentredArc (cx, cy, r - 3.0f, r - 3.0f, 0.0f, startAng, endAng, true);
        val.addCentredArc   (cx, cy, r - 3.0f, r - 3.0f, 0.0f, startAng, ang, true);
        g.setColour (ShardColours::track);
        g.strokePath (track, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (ShardColours::amber);
        g.strokePath (val, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Knob body — top-lit radial.
        const float br = r - 6.5f;
        juce::ColourGradient body (ShardColours::knobBody, cx, cy - br,
                                   ShardColours::knobBodyLo, cx, cy + br, false);
        g.setGradientFill (body);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.setColour (ShardColours::knobEdge);
        g.drawEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f, 1.3f);
        g.setColour (juce::Colour (0x30ffffff));      // subtle top glint
        g.drawEllipse (cx - br + 1.5f, cy - br + 1.5f, br * 2.0f - 3.0f, br * 1.2f, 1.0f);

        // Pointer.
        juce::Path p;
        p.addRoundedRectangle (-1.5f, -br + 2.5f, 3.0f, br * 0.6f, 1.5f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::cream);
        g.fillPath (p);
        g.setColour (ShardColours::amberBright);       // amber tip
        auto tip = juce::Point<float> (0.0f, -br + 3.5f).transformedBy (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.fillEllipse (tip.x - 1.6f, tip.y - 1.6f, 3.2f, 3.2f);
    }

    // ---- Button cap: rounded, bevelled, amber glow when lit/pressed.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour& backgroundColour,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const float rad = juce::jmin (7.0f, r.getHeight() * 0.28f);
        const bool on = b.getToggleState();

        auto base = backgroundColour;
        if (down) base = base.brighter (0.12f);
        else if (over) base = base.brighter (0.05f);

        // Body with a soft top-to-bottom bevel.
        juce::ColourGradient grad (base.brighter (0.10f), r.getX(), r.getY(),
                                   base.darker (0.22f),  r.getX(), r.getBottom(), false);
        g.setGradientFill (grad);
        g.fillRoundedRectangle (r, rad);

        // Bevel highlight (top) + shadow (bottom).
        g.setColour (juce::Colour (0x26ffffff));
        g.drawLine (r.getX() + rad, r.getY() + 0.8f, r.getRight() - rad, r.getY() + 0.8f, 1.0f);
        g.setColour (juce::Colour (0x40000000));
        g.drawLine (r.getX() + rad, r.getBottom() - 0.8f, r.getRight() - rad, r.getBottom() - 0.8f, 1.0f);

        // Lit / triggered glow: bright caps and amber-ish colours bloom.
        const float lum = base.getPerceivedBrightness();
        const bool amberish = base.getHue() > 0.05f && base.getHue() < 0.16f && base.getSaturation() > 0.25f;
        if (on || (amberish && lum > 0.42f))
        {
            g.setColour (ShardColours::amberBright.withAlpha (juce::jlimit (0.25f, 0.9f, lum)));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.6f);
        }
        else
        {
            g.setColour (ShardColours::amberDim.withAlpha (0.55f));
            g.drawRoundedRectangle (r.reduced (0.6f), rad, 1.0f);
        }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const float fs = juce::jlimit (10.5f, 20.0f, (float) b.getHeight() * 0.40f);
        g.setFont (juce::Font (juce::FontOptions (fs)).withExtraKerningFactor (0.06f));
        const bool on = b.getToggleState();
        g.setColour (on ? b.findColour (juce::TextButton::textColourOnId)
                        : b.findColour (juce::TextButton::textColourOffId));
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (5, 2),
                          juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.05f);
    }
};

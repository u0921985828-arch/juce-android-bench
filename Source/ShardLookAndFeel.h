#pragma once

#include <JuceHeader.h>

// ============================================================================
//  ShardLookAndFeel — original "reimagined vintage hardware" identity.
//  Warm near-black chassis, amber phosphor accents, tactile rotary knobs.
//  NOT a copy of any hardware — our own palette and knob drawing.
// ============================================================================
namespace ShardColours
{
    const juce::Colour chassis   { 0xff17140f };   // warm near-black
    const juce::Colour panel     { 0xff211d16 };
    const juce::Colour screenBg  { 0xff0c110d };
    const juce::Colour amber      { 0xffe6a94a };   // phosphor accent
    const juce::Colour amberDim  { 0xff7a5a28 };
    const juce::Colour cream     { 0xfff1e9d4 };
    const juce::Colour knobBody  { 0xff2a251d };
    const juce::Colour knobEdge  { 0xff554a38 };
    const juce::Colour track     { 0xff35301f };
    const juce::Colour padTop    { 0xff2b2a2f };
    const juce::Colour padLit    { 0xffe6a94a };
}

class ShardLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ShardLookAndFeel()
    {
        setColour (juce::Slider::textBoxTextColourId, ShardColours::cream);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, ShardColours::cream.withAlpha (0.85f));
        setColour (juce::TextButton::textColourOffId, ShardColours::cream.withAlpha (0.9f));
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                           float pos, float startAng, float endAng,
                           juce::Slider&) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
        const float r  = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f;
        const float cx = area.getCentreX();
        const float cy = area.getCentreY();
        const float ang = startAng + pos * (endAng - startAng);

        // Track arc + value arc.
        juce::Path track, val;
        track.addCentredArc (cx, cy, r - 2.0f, r - 2.0f, 0.0f, startAng, endAng, true);
        val.addCentredArc   (cx, cy, r - 2.0f, r - 2.0f, 0.0f, startAng, ang, true);
        g.setColour (ShardColours::track);
        g.strokePath (track, juce::PathStrokeType (3.0f));
        g.setColour (ShardColours::amber);
        g.strokePath (val, juce::PathStrokeType (3.0f));

        // Knob body.
        const float br = r - 6.0f;
        g.setColour (ShardColours::knobBody);
        g.fillEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.setColour (ShardColours::knobEdge);
        g.drawEllipse (cx - br, cy - br, br * 2.0f, br * 2.0f, 1.4f);

        // Pointer.
        juce::Path p;
        p.addRoundedRectangle (-1.6f, -br + 3.0f, 3.2f, br * 0.55f, 1.6f);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ShardColours::cream);
        g.fillPath (p);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return juce::Font (juce::FontOptions (13.0f)).withExtraKerningFactor (0.05f);
    }
};

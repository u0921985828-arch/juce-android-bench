#pragma once

#include <JuceHeader.h>
#include "Lang.h"
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

    // --- Chassis / structure -----------------------------------------------
    //  Paper, not plastic. The chassis was pure white and three neutral greys,
    //  which on a phone reads as a wireframe of an instrument rather than as
    //  one: nothing in the room it is held in is that colour. These are the
    //  same values warmed onto a kraft/bone axis - the substrate of the studio
    //  it belongs to - and nothing else about the system changes.
    //
    //  It costs the fragments nothing, which is the whole reason it is allowed:
    //  warm neutral is not a hue with a meaning, so it cannot compete with the
    //  zati colours for one.
    const juce::Colour chassisTop { 0xfff4efe3 };
    const juce::Colour chassis    { 0xffece6d8 };
    const juce::Colour chassisBot { 0xffe0d9c8 };
    const juce::Colour panel      { 0xffeee9dc };   // section card surface
    const juce::Colour panelDark  { 0xffded7c6 };   // flat button cap (off state)
    const juce::Colour panelHi    { 0xfffffdf7 };   // panel top bevel
    const juce::Colour panelLo    { 0xffc7bfac };   // panel shadow / soft border
    const juce::Colour key        { 0xffded7c6 };   // flat button cap (off state)
    const juce::Colour keyLit     { 0xffcfc7b4 };
    const juce::Colour screw      { 0xffb3ab98 };

    // --- Accent / semantic ------------------------------------------------
    //  The chassis is MONOCHROME on purpose. Hue belongs to the zati fragment
    //  system and nothing else: if a pressed button or an armed effect also
    //  glowed in a colour, that colour would compete with the fragments for
    //  the same meaning and the "one colour per zati" rule would stop reading.
    //  So an active control states itself with TONE — a near-black cap with
    //  light text — never with a hue.
    //
    //  Still mutable, because the skins shift that tone (see setSkin).
    inline juce::Colour amber       { 0xff26221b };   // = accent (name kept for compat)
    inline juce::Colour amberBright { 0xff4a4438 };
    inline juce::Colour amberDim    { 0xff14120e };
    inline juce::Colour accent      { 0xff26221b };   // preferred names
    inline juce::Colour accentBright{ 0xff4a4438 };
    inline juce::Colour accentDim   { 0xff14120e };

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
    const juce::Colour ink        { 0xff26221b };   // primary text on the paper
    const juce::Colour inkDim     { 0xff6b6355 };   // secondary text
    const juce::Colour inkLight   { 0xfff4efe3 };   // text on dark surfaces (knobs/LCD chips)
    const juce::Colour white      { 0xfffffdf7 };   // the highlight in an engraved line
    const juce::Colour cream      { 0xfffffdf7 };   // name kept for compat
    const juce::Colour engrave    { 0xff6b6355 };   // text-dim

    // --- LCD (dark, pops on the white face) ---
    const juce::Colour screenBg   { 0xff14120f };
    const juce::Colour lcdFg      { 0xffece7d9 };
    //  Raised from 0xff5c5c56: that was 2.83:1 on the LCD, below the 4.5
    //  minimum, and it carries real information (ruler, cut lines, fragment
    //  numbers, empty-state text), not decoration. Now 5.48:1.
    const juce::Colour lcdDim     { 0xff8b8375 };

    // --- Pads (neutral when empty; a loaded pad wears its zati colour) ---
    const juce::Colour padTop     { 0xffded7c6 };
    const juce::Colour padBg2     { 0xffe9e3d4 };
    const juce::Colour padBorder  { 0xffcdc5b2 };
    inline juce::Colour padLit    { 0xff26221b };   // follows the skin tone

    //  Skins move the chassis TONE, not its hue. Four steps of ink, from
    //  near-black to a mid grey, so an active control reads at whatever
    //  contrast the room needs without ever borrowing a fragment's colour.
    inline void setSkin (int i)
    {
        currentSkin = ((i % 4) + 4) % 4;
        struct S { juce::uint32 a, ab, ad; };
        static constexpr S skins[4] = {
            { 0xff26221b, 0xff4a4438, 0xff14120e },   // TINTA   (default, warm near-black)
            { 0xff3a352b, 0xff5f5849, 0xff23201a },   // GRAFITO
            { 0xff504839, 0xff756c58, 0xff332f26 },   // ACERO
            { 0xff6b6355, 0xff8b8375, 0xff474134 },   // PLOMO
        };
        const auto s = skins[currentSkin];
        amber = accent = padLit    = juce::Colour (s.a);
        amberBright = accentBright = juce::Colour (s.ab);
        amberDim = accentDim       = juce::Colour (s.ad);
    }

    // --- Knob body (dark — physical-instrument contrast on a white face) ---
    const juce::Colour knobWell   { 0xffded7c6 };
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

    //  The NAME of a thing, as opposed to its value.
    //
    //  Everything on the face was set in the same monospaced face, titles and
    //  readouts alike, which is why it read flat: one voice saying two
    //  different kinds of thing. Names go in the condensed display face,
    //  tracked wide the way lettering is silkscreened onto a panel; values
    //  stay monospaced, where digits line up under each other and a number
    //  that changes does not move the ones beside it.
    //
    //  Oswald is condensed, so it needs a couple of pixels of height to match
    //  the mono it sits next to.
    inline juce::Font labelFont (float h, float tracking = 0.18f)
    {
        //  No tracking in Arabic. Letter-spacing is what makes Latin lettering
        //  look silkscreened, and it is what BREAKS Arabic: the script is
        //  joined, and pushing the glyphs apart cuts every join - the word for
        //  pad came out as three loose letters, which is not a spaced word, it
        //  is a different thing that does not read.
        if (Lang::isRightToLeft (Lang::current()))
            return displayFont (h + 2.0f, true);

        return displayFont (h + 2.0f, true).withExtraKerningFactor (tracking);
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
    //  The floor for anything a finger has to hit inside a sheet. Rows of
    //  24 and 30 read fine on a desktop screenshot and are a coin toss on a
    //  phone; this is a phone. Where sixteen of something have to fit at
    //  once the card grows instead of the rows shrinking.
    static constexpr int hit = 40;
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
    //  How far a cap sits above the solid block it is printed on, and how far
    //  it travels when pressed. Everything that draws a cap or a label on one
    //  reads this, so the press stays a single number.
    static constexpr float kCapLift = 3.0f;

    //  Breathing room on the main face. These are the three numbers that
    //  decide whether the instrument looks like it is sitting on the screen or
    //  bursting out of it, so they are named rather than sprinkled: the margin
    //  to the glass, the gap between the big sections, and the gap between
    //  pads. All of it comes out of the LCD, which absorbs whatever is left.
    static constexpr int kFaceMargin = 14;
    static constexpr int kAir        = 10;
    static constexpr int kPadGap     = 8;

    //  ...plus extra room top and bottom, on top of the margin and of whatever
    //  the system bars are already taking. The two horizontal edges are held
    //  by the pad grid's width, so they need no help; the vertical ones are
    //  where a phone feels crowded.
    static constexpr int kEdgeV      = 10;

    //  The height of every band on the face, in one place. The rule they
    //  follow: what you WATCH and what you NAVIGATE with are as small as they
    //  can be read at, and every pixel that saves goes to what you PLAY with -
    //  the pads, the three knobs and the screen. A module bar as tall as a
    //  transport key is a menu claiming to be an instrument.
    static constexpr int kHeader    = 24;   // the wordmark strip
    static constexpr int kStrip     = 14;   // VU and the step LEDs: read, never touched
    static constexpr int kModule    = 26;   // PADS / SEC / SONG / MIX / SET - they open windows
    static constexpr int kTransport = 36;   // LOAD / REC / PLAY - they act
    static constexpr int kFxRow     = 34;   // the six effects
    static constexpr int kStatus    = 16;   // the line at the foot
    //  Text needs room above and below it or it reads as pinched against
    //  whatever is next to it. This is the padding inside every band that
    //  holds nothing but words: the status line, the section names in the
    //  sheets, the label over a control.
    static constexpr int kTextPad   = 3;
    static constexpr int kCtrlPlate = 86;   // CTRL 1-3 and their readouts
    static constexpr int kCtrlName  = 16;   // ...of which the name above
    static constexpr int kCtrlChip  = 18;   // ...and the readout below. The rest is knob.

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

        //  A knob drawn rather than shaded: a ring, a dial ruled with ticks,
        //  and a straight pointer. The old one was a dark radial gradient with
        //  a needle on it - a photograph of a knob. This is the drawing of
        //  one, which is what the rest of the instrument now looks like, and
        //  it also says more: the ticks give the eye something to read the
        //  position against instead of the needle alone.
        const float br = r - 2.0f;

        //  Ticks around the travel, brighter at the ends and at the centre
        //  detent so the three positions that matter are findable without
        //  looking at the number.
        constexpr int kTicks = 11;
        for (int i = 0; i < kTicks; ++i)
        {
            const float t  = (float) i / (float) (kTicks - 1);
            const float a  = startAng + t * (endAng - startAng);
            const bool  key = (i == 0 || i == kTicks - 1 || i == kTicks / 2);
            const float len = key ? 5.0f : 3.0f;

            const auto dir = juce::Point<float> (std::sin (a), -std::cos (a));
            const auto p1  = juce::Point<float> (cx, cy) + dir * r;
            const auto p2  = juce::Point<float> (cx, cy) + dir * (r - len);

            g.setColour (ZatiColours::ink.withAlpha (key ? 0.75f : 0.35f));
            g.drawLine ({ p1, p2 }, key ? 1.4f : 1.0f);
        }

        //  The dial face: hollow, so the chassis shows through and the knob
        //  stops being a dark blob in a light panel.
        const float dial = br - 6.0f;
        g.setColour (ZatiColours::panel);
        g.fillEllipse (cx - dial, cy - dial, dial * 2.0f, dial * 2.0f);
        g.setColour (ZatiColours::ink.withAlpha (0.85f));
        g.drawEllipse (cx - dial, cy - dial, dial * 2.0f, dial * 2.0f, 1.6f);

        // Pointer: a ruled line from the centre out to the rim.
        juce::Path p;
        p.addRectangle (-1.0f, -dial, 2.0f, dial);
        p.applyTransform (juce::AffineTransform::rotation (ang).translated (cx, cy));
        g.setColour (ZatiColours::ink);
        g.fillPath (p);

        auto tip = juce::Point<float> (0.0f, -dial + 1.0f)
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
        //  A cap is a printed shape sitting on the face, not a soft plastic
        //  key: the depth is a SOLID offset block underneath it, no blur and
        //  no bevel, and pressing moves the cap down onto it. Blurred shadows
        //  read as a phone app; a hard offset reads as an object that was
        //  screen-printed, which is the whole C40 idea.
        const float lift = kCapLift;
        const float rad  = 3.0f;                                  // drawn, not rounded off
        const bool  on   = b.getToggleState();

        auto full = b.getLocalBounds().toFloat().reduced (0.5f);
        auto r    = full.withTrimmedBottom (lift);

        auto base = backgroundColour;
        if (over && ! down) base = base.brighter (0.05f);
        // A disabled cap reads as inert: desaturated and washed toward the face.
        if (! b.isEnabled())
            base = base.withSaturation (base.getSaturation() * 0.25f)
                       .interpolatedWith (ZatiColours::chassis, 0.55f);

        if (down)
        {
            r = r.translated (0.0f, lift);          // pressed onto the block
        }
        else if (b.isEnabled())
        {
            g.setColour (ZatiColours::ink.withAlpha (0.42f));
            g.fillRoundedRectangle (r.translated (0.0f, lift), rad);
        }

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
            g.setColour (ZatiColours::ink.withAlpha (0.55f));
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

        //  The label belongs to the cap, not to the component: the cap sits
        //  kCapLift above the block it is printed on and travels down onto it
        //  when pressed, so text centred on the full bounds would float low at
        //  rest and stay put during the press - which reads as a wobble.
        auto area = b.getLocalBounds().reduced (5, 2);
        area = b.isDown() ? area.withTrimmedTop ((int) kCapLift)
                          : area.withTrimmedBottom ((int) kCapLift);

        g.drawFittedText (t, area, juce::Justification::centred, 2, 0.9f);
    }

    juce::Font getLabelFont (juce::Label&) override
    {
        return ZatiColours::monoFont (Metrics::fValue, true);
    }
};

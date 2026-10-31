#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/**
    The HOLOGRAM look: dark navy glass, thin glowing lines, each module in its
    own tint. Everything is drawn live as vectors, so it stays sharp at any
    window size. (Design notes: DevTools/Look/Strip/README.txt and
    strip_wireframe_hologram_digital.html - this follows that wireframe.)
*/
namespace Holo
{
    namespace Colours
    {
        inline const juce::Colour background { 0xff050b14 };
        inline const juce::Colour glass      { 0xff071423 };
        inline const juce::Colour ink        { 0xffd6f1fb };   // labels
        inline const juce::Colour dim        { 0xff6f8ea0 };   // units, captions
        inline const juce::Colour track      { 0xff1a3044 };   // the unlit part of a ring
        inline const juce::Colour cyan       { 0xff3fd8ff };   // the frame, INPUT / OUTPUT
        inline const juce::Colour value      { 0xffffffff };
        inline const juce::Colour warn       { 0xffff4a5a };   // INIT, BYPASS on, clip lamps
        inline const juce::Colour reduction  { 0xffff8a3d };   // gain reduction
        inline const juce::Colour selected   { 0xffffb35c };   // the page on the big screen, the preset name
        inline const juce::Colour hazard     { 0xfff2c230 };
        inline const juce::Colour offText    { 0xff7f8d99 };
        inline const juce::Colour offEdge    { 0xff33485a };
        inline const juce::Colour screen     { 0xff04121f };

        // What a light's colour means, everywhere
        inline const juce::Colour frequency  { 0xff4fb4ff };   // blue rings: frequencies
        inline const juce::Colour level      { 0xffff5a6a };   // red rings / bars: levels
        inline const juce::Colour gold       { 0xffffb547 };
        inline const juce::Colour delayMode  { 0xff4f8cff };   // a mode that adds delay and CPU (MASTER, oversampling)

        // Each module's tint
        inline const juce::Colour gate  { 0xff4ff08a };
        inline const juce::Colour amp   { 0xffffc24a };
        inline const juce::Colour eq    { 0xff36e2d0 };
        inline const juce::Colour ds    { 0xffb08cff };
        inline const juce::Colour comp  { 0xff5a9bff };
        inline const juce::Colour trans { 0xffff5fd2 };
        inline const juce::Colour slap  { 0xffff5c7a };
        inline const juce::Colour tape  { 0xffe8a06a };   // (oxide brown, lit)
        inline const juce::Colour clip  { 0xffff8a3d };

        inline const juce::Colour mid   { 0xff4fe3ff };    // the EQ's MID curve
        inline const juce::Colour side  { 0xffff5fd2 };    // ... and SIDE
    }

    /** Barlow Condensed (labels, values): weight 0 = medium, 1 = semi-bold, 2 = bold. */
    juce::Font text (float height, int weight = 1);

    /** Orbitron (the strip's name). */
    juce::Font display (float height);

    /** The width a piece of text takes in a font. */
    float widthOf (const juce::Font&, const juce::String&);

    /** A rectangle with its corners cut off. */
    juce::Path chamfered (juce::Rectangle<float>, float cut);

    /** A module's panel: tinted glass, a glowing outline, chamfered corners. */
    void drawModule (juce::Graphics&, juce::Rectangle<float>, juce::Colour tint, bool deepRed = false);

    /** A numbered section inside a module: "(1) HOW MUCH" in the top left. */
    void drawSection (juce::Graphics&, juce::Rectangle<float>, juce::Colour tint, int number, const juce::String& title,
                      bool deepRed = false);

    /** The section heading alone (the number in a dot, then the title). */
    void drawHeading (juce::Graphics&, juce::Point<float> topLeft, juce::Colour tint, int number, const juce::String& title);

    /** Text with a soft glow behind it. */
    void drawGlowText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Justification,
                       const juce::Font&, juce::Colour, float glow = 0.5f);

    /** A line (or path) drawn with a soft glow. */
    void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float thickness, float glow = 0.6f);
}

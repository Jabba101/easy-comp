#include "HoloLook.h"
#include "StripFonts.h"

namespace Holo
{
    namespace
    {
        struct Typefaces
        {
            juce::Typeface::Ptr barlow[3], orbitron;

            Typefaces()
            {
                barlow[0] = juce::Typeface::createSystemTypefaceFor (StripFonts::BarlowCondensedMedium_ttf, StripFonts::BarlowCondensedMedium_ttfSize);
                barlow[1] = juce::Typeface::createSystemTypefaceFor (StripFonts::BarlowCondensedSemiBold_ttf, StripFonts::BarlowCondensedSemiBold_ttfSize);
                barlow[2] = juce::Typeface::createSystemTypefaceFor (StripFonts::BarlowCondensedBold_ttf, StripFonts::BarlowCondensedBold_ttfSize);
                orbitron  = juce::Typeface::createSystemTypefaceFor (StripFonts::OrbitronBold_ttf, StripFonts::OrbitronBold_ttfSize);
            }
        };

        const Typefaces& typefaces()
        {
            static const Typefaces faces;
            return faces;
        }
    }

    juce::Font text (float height, int weight)
    {
        auto& face = typefaces().barlow[juce::jlimit (0, 2, weight)];
        return juce::Font (juce::FontOptions (face).withHeight (height));
    }

    juce::Font display (float height)
    {
        return juce::Font (juce::FontOptions (typefaces().orbitron).withHeight (height));
    }

    float widthOf (const juce::Font& font, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (font, s);
    }

    juce::Path chamfered (juce::Rectangle<float> r, float cut)
    {
        cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
        juce::Path p;
        p.startNewSubPath (r.getX() + cut, r.getY());
        p.lineTo (r.getRight() - cut, r.getY());
        p.lineTo (r.getRight(), r.getY() + cut);
        p.lineTo (r.getRight(), r.getBottom() - cut);
        p.lineTo (r.getRight() - cut, r.getBottom());
        p.lineTo (r.getX() + cut, r.getBottom());
        p.lineTo (r.getX(), r.getBottom() - cut);
        p.lineTo (r.getX(), r.getY() + cut);
        p.closeSubPath();
        return p;
    }

    void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour colour, float thickness, float glow)
    {
        if (glow > 0.0f)
        {
            g.setColour (colour.withMultipliedAlpha (0.10f * glow));
            g.strokePath (path, juce::PathStrokeType (thickness + 6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour.withMultipliedAlpha (0.22f * glow));
            g.strokePath (path, juce::PathStrokeType (thickness + 2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (colour);
        g.strokePath (path, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawModule (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, bool deepRed)
    {
        const auto shape = chamfered (r.reduced (0.5f), 9.0f);

        if (deepRed)
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5c0f24), r.getX(), r.getY(), juce::Colour (0xff3a0815), r.getX(), r.getBottom(), false));
        else
            g.setGradientFill (juce::ColourGradient (Colours::glass.interpolatedWith (tint, 0.10f), r.getX(), r.getY(),
                                                     juce::Colour (0xff050d18).interpolatedWith (tint, 0.05f), r.getX(), r.getBottom(), false));
        g.fillPath (shape);

        // (a faint inner glow along the edge)
        g.setColour (tint.withAlpha (deepRed ? 0.12f : 0.07f));
        g.strokePath (chamfered (r.reduced (4.0f), 7.0f), juce::PathStrokeType (6.0f));

        g.setColour (tint.withAlpha (0.10f));
        g.strokePath (shape, juce::PathStrokeType (5.0f));
        g.setColour (tint.withAlpha (deepRed ? 0.95f : 0.68f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));
    }

    void drawHeading (juce::Graphics& g, juce::Point<float> topLeft, juce::Colour tint, int number, const juce::String& title)
    {
        auto x = topLeft.x;

        if (number > 0)
        {
            const juce::Rectangle<float> dot (x, topLeft.y, 17.0f, 17.0f);
            g.setColour (tint);
            g.fillEllipse (dot);
            g.setColour (juce::Colour (0xff04101e));
            g.setFont (text (13.0f, 2));
            g.drawText (juce::String (number), dot, juce::Justification::centred, false);
            x += 23.0f;
        }

        drawGlowText (g, title, { x, topLeft.y - 1.0f, 400.0f, 19.0f }, juce::Justification::centredLeft, text (15.5f, 2), tint, 0.5f);
    }

    void drawSection (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour tint, int number, const juce::String& title, bool deepRed)
    {
        const auto shape = chamfered (r.reduced (0.5f), 6.0f);
        g.setColour (deepRed ? juce::Colour (0x5a140006) : juce::Colour (0x6b020810));
        g.fillPath (shape);
        g.setColour (tint.withAlpha (0.26f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        if (title.isNotEmpty())
            drawHeading (g, { r.getX() + 9.0f, r.getY() + 6.0f }, tint, number, title);
    }

    void drawGlowText (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, juce::Justification j,
                       const juce::Font& font, juce::Colour colour, float glow)
    {
        g.setFont (font);

        if (glow > 0.0f)
        {
            g.setColour (colour.withMultipliedAlpha (0.16f * glow));
            for (auto offset : { juce::Point<float> (-1.0f, 0.0f), { 1.0f, 0.0f }, { 0.0f, -1.0f }, { 0.0f, 1.0f } })
                g.drawText (s, r + offset, j, false);
        }

        g.setColour (colour);
        g.drawText (s, r, j, false);
    }
}

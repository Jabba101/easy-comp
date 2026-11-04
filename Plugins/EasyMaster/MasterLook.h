#pragma once

#include "Look.h"
#include "HoloLook.h"
#include "HoloControls.h"

//==============================================================================
/**
    Easy Master in the HOLOGRAM skin, to match Easy Strip: dark navy glass,
    thin glowing rings with the value inside, lit lamp buttons, Barlow
    Condensed lettering, each module in its own tint - and inside each
    module, every cluster of controls that works together on its own
    back-panel tone (like an AMEK console's panels).

    The colours below take the place of the standard Palette in Easy
    Master's window (same names, hologram values).
*/
namespace MasterSkin
{
    inline const juce::Colour background { 0xff04121f };   // the screens, recesses
    inline const juce::Colour panel      { 0xff0a1b2c };
    inline const juce::Colour panelEdge  { 0xff1a3044 };
    inline const juce::Colour knobBody   { 0xff0a1826 };
    inline const juce::Colour knobTrack  { 0xff1a3044 };
    inline const juce::Colour accent     { 0xffffb35c };   // amber: the page showing, the preset name
    inline const juce::Colour textBright { 0xffd6f1fb };
    inline const juce::Colour textDim    { 0xff6f8ea0 };
    inline const juce::Colour meterLow   { 0xff4ff08a };   // green
    inline const juce::Colour meterMid   { 0xffffb547 };   // gold
    inline const juce::Colour meterHigh  { 0xffff4a5a };   // red
    inline const juce::Colour meterOff   { 0xff33485a };
    inline const juce::Colour cyan       { 0xff3fd8ff };   // the frame: INPUT, METERS, COMPARE

    // Each module's tint (the same engines as in Easy Strip share its colour)
    inline const juce::Colour tamer      { 0xffb08cff };
    inline const juce::Colour eq         { 0xff36e2d0 };
    inline const juce::Colour multiband  { 0xffffa05a };
    inline const juce::Colour comp       { 0xff5a9bff };
    inline const juce::Colour punch      { 0xffff5fd2 };
    inline const juce::Colour width      { 0xff7ad0ff };
    inline const juce::Colour limiter    { 0xffe8eef5 };

    /** Barlow Condensed at a size matching the old font's (it's narrower, so a touch bigger). */
    inline juce::Font font (float size, bool bold = false)   { return Holo::text (size * 1.18f, bold ? 2 : 0); }

    /** A module's panel: tinted glass, glowing outline, its number in a dot and its name. */
    void drawModulePanel (juce::Graphics&, juce::Rectangle<int>, juce::Colour tint, const juce::String& number, const juce::String& caption);

    /** A cluster's back panel: tone 0 = light glass, 1 = dark, 2 = blue-tinted - neighbours always differ. */
    void drawCluster (juce::Graphics&, juce::Rectangle<float>, juce::Colour tint, int tone);

    /** The whole window's backdrop (gradient, glass, the faint projection grid) and the header strip. */
    void drawBackdrop (juce::Graphics&, juce::Rectangle<int> window, juce::Rectangle<int> header);
}

//==============================================================================
class MasterLook : public EasyLookAndFeel,
                   public EasySkin
{
public:
    MasterLook();

    // Knobs: a thin ring that glows in its colour, the value inside
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;

    // Pr, INIT, COPY...
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isMouseOver, bool isMouseDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOver, bool isMouseDown) override;

    // Menus
    juce::Font getPopupMenuFont() override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    // EasySkin
    void paintLightButton (juce::Graphics&, juce::Button&, juce::Colour lightColour, bool isMouseOver, bool isMouseDown) override;
    void paintHintBar (juce::Graphics&, juce::Component&, const juce::String& title, const juce::String& text) override;
    void paintPresetBox (juce::Graphics&, juce::Component&, const juce::String& shownText, bool isEmpty, bool isNaming, bool isMouseOver) override;

    /** The soft halo of every lit button inside `root`, spilling onto the panel (draw it under the controls). */
    static void paintButtonGlows (juce::Graphics&, juce::Component& root);
};

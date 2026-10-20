#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** All of the colours in one place - change these to re-skin every plugin. */
namespace Palette
{
    inline const juce::Colour background { 0xff15171c };
    inline const juce::Colour panel      { 0xff1e2127 };
    inline const juce::Colour panelEdge  { 0xff2a2e36 };
    inline const juce::Colour knobBody   { 0xff292d35 };
    inline const juce::Colour knobTrack  { 0xff353a44 };
    inline const juce::Colour accent     { 0xffffb444 };   // warm amber
    inline const juce::Colour textBright { 0xffeceef2 };
    inline const juce::Colour textDim    { 0xff8b919c };
    inline const juce::Colour meterLow   { 0xff6fd08c };   // green: gentle
    inline const juce::Colour meterMid   { 0xffffb444 };   // amber: firm
    inline const juce::Colour meterHigh  { 0xffff6b5b };   // red:   heavy
    inline const juce::Colour meterOff   { 0xff2a2e36 };
}

//==============================================================================
/** Flat, dark styling for the knobs, number boxes, menus and text boxes. */
class EasyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EasyLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getPopupMenuFont() override;
};

//==============================================================================
/** Draws one of the rounded panels with its caption: a number in amber and a
    name ("1  HOW MUCH"), or with no number, a centred name. */
void drawPanel (juce::Graphics&, juce::Rectangle<int> area, const juce::String& number, const juce::String& caption);

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

//==============================================================================
/** All of the plugin's colours in one place - change these to re-skin it. */
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
/** Flat, dark styling for the knobs, number boxes and preset menu. */
class EasyCompLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EasyCompLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;

    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
};

//==============================================================================
/** The vertical LED meter showing how many dB the compressor is turning the
    sound down right now. */
class GainReductionMeter : public juce::Component
{
public:
    /** Feed it the latest reduction in dB (a positive number), ~30 times a second. */
    void setGainReduction (float reductionDb);

    void paint (juce::Graphics&) override;

private:
    float shownDb = 0.0f;   // what's drawn: jumps up instantly, falls back smoothly
};

//==============================================================================
/** The strip along the bottom that explains whatever the mouse is over. */
class HintBar : public juce::Component
{
public:
    void setHint (const juce::String& newTitle, const juce::String& newText);
    void paint (juce::Graphics&) override;

private:
    juce::String title, text;
};

//==============================================================================
/** The plugin window. */
class EasyCompAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit EasyCompAudioProcessorEditor (EasyCompAudioProcessor&);
    ~EasyCompAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // These keep the hint bar in step with whatever the mouse is pointing at.
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showHintFor (const juce::Component* componentUnderMouse);
    void markPresetAsEdited();

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyCompAudioProcessor& audioProcessor;
    EasyCompLookAndFeel lookAndFeel;   // declared before the components that use it

    std::array<Knob, 6> knobs;         // Threshold, Ratio | Attack, Release | Make-up, Mix
    juce::ComboBox presetMenu;
    GainReductionMeter meter;
    HintBar hintBar;
    bool isLoadingPreset = false;

    // Areas worked out in resized() and drawn in paint()
    juce::Rectangle<int> titleArea, meterPanel;
    std::array<juce::Rectangle<int>, 3> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyCompAudioProcessorEditor)
};

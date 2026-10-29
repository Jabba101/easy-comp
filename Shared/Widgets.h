#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Look.h"

//==============================================================================
/** The strip along the bottom that explains whatever the mouse is over. */
class HintBar : public juce::Component
{
public:
    void setHint (const juce::String& newTitle, const juce::String& newText);
    void paint (juce::Graphics&) override;

    const juce::String& getTitle() const noexcept   { return title; }
    const juce::String& getText() const noexcept    { return text; }

private:
    juce::String title, text;
};

//==============================================================================
/** An on/off button with a little "LED" that lights up when it's on. */
class LightButton : public juce::Button
{
public:
    LightButton (const juce::String& buttonText, juce::Colour colourWhenOn);

    void paintButton (juce::Graphics&, bool isMouseOver, bool isMouseDown) override;

private:
    juce::Colour lightColour;
};

//==============================================================================
/** A set of light-up buttons where exactly one is on - for a setting with a
    few named choices (like REACTS TO: PEAKS / AVERAGE). Connected straight
    to its parameter, so it follows the DAW and presets too. */
class ChoiceSwitch : public juce::Component
{
public:
    ChoiceSwitch (juce::RangedAudioParameter& parameter, const juce::StringArray& optionNames, juce::Colour colourWhenOn);

    void resized() override;

    /** Called when you click a different option (not when a preset or the DAW changes it). */
    std::function<void()> onUserChange;

    /** Side by side instead of one above the other. */
    void setHorizontal (bool sideBySide)              { horizontal = sideBySide; resized(); }

    int getNumOptions() const noexcept                { return options.size(); }
    LightButton& getOption (int index) noexcept       { return *options[index]; }

private:
    juce::OwnedArray<LightButton> options;
    bool ignoreClicks = false, horizontal = false;
    juce::ParameterAttachment attachment;
};

//==============================================================================
/** The vertical LED meter showing how many dB the plugin is turning the
    sound down right now (0 at the top, 24 dB at the bottom). */
class GainReductionMeter : public juce::Component
{
public:
    /** Feed it the latest reduction in dB (a positive number), ~30 times a second. */
    void setGainReduction (float reductionDb);

    void paint (juce::Graphics&) override;

private:
    float shownDb = 0.0f;   // what's drawn: jumps up instantly, falls back smoothly
};

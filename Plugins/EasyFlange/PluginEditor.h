#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": two swings of the delay going back and forth - the
    left side (amber) and the right (green; WIDTH pulls them apart) - with
    the dot where it is now. Shorter delay = higher notches (FLANGER);
    longer = the doubled, chorused sound. */
class SwingDisplay : public juce::Component
{
public:
    void setSettings (const Flanger::Settings&);
    void setPosition (float cyclePosition);
    void setBypassed (bool isBypassed);
    void paint (juce::Graphics&) override;

private:
    Flanger::Settings settings;
    float position = 0.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Flange window: TYPE and SPEED on the left, the swing in the
    middle, HOW MUCH and OUTPUT on the right. */
class EasyFlangeAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyFlangeAudioProcessorEditor (EasyFlangeAudioProcessor&);

    enum KnobIndex { rateKnob, noteKnob, depthKnob, feedbackKnob, widthKnob, mixKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void showSyncedKnob (bool synced);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyFlangeAudioProcessor& flangeProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch typeSwitch, syncSwitch;
    juce::Label syncLabel;
    bool showingSynced = false;
    SwingDisplay display;

    std::array<juce::Rectangle<int>, 4> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyFlangeAudioProcessorEditor)
};

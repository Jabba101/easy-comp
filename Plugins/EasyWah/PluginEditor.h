#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the wah's filter - faintly at the heel and the toe,
    brightly where the pedal is right now - and under it the pedal itself,
    showing how far your playing (or the tempo) is moving it. */
class WahDisplay : public juce::Component
{
public:
    void setSettings (const AutoWah::Settings&);
    void setPedal (float low, float high);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    float getShownPedal() const noexcept   { return shownHigh; }

private:
    AutoWah::Settings settings;
    float shownLow = 0.0f, shownHigh = 0.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Wah window: HOW IT MOVES on the left, the filter in the middle,
    THE PEDAL and SOUND on the right. */
class EasyWahAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyWahAudioProcessorEditor (EasyWahAudioProcessor&);

    enum KnobIndex { sensitivityKnob, noteKnob, pedalKnob, fromKnob, toKnob, resonanceKnob, mixKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    WahDisplay& getDisplay()                   { return display; }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void showTempoParts (bool tempo);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyWahAudioProcessor& wahProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch modeSwitch, responseSwitch, directionSwitch;
    juce::Label modeLabel, responseLabel, directionLabel;
    bool showingTempo = false;
    WahDisplay display;
    float shownMakeUpDb = 0.0f;
    bool showingMakeUp = false;

    std::array<juce::Rectangle<int>, 3> groupPanels;
    juce::Rectangle<int> displayPanel, makeUpReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyWahAudioProcessorEditor)
};

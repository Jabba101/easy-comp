#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the filter's curve right now - the left side amber,
    the right green (WIDTH pulls them apart) - over the band the movement
    covers, and under it two cycles of the movement (SHAPE) with a dot where
    it is now. */
class FilterDisplay : public juce::Component
{
public:
    void setSettings (const ModFilter::Settings&, double sampleRate);
    void setNow (float position, float cutoffLeft, float cutoffRight);
    void setBypassed (bool isBypassed);
    void paint (juce::Graphics&) override;

private:
    ModFilter::Settings settings;
    double rate = 48000.0;
    float position = 0.0f;
    std::array<float, 2> cutoff { 1000.0f, 1000.0f };
    bool bypassed = false;
};

//==============================================================================
/** The Easy Filter window: TYPE and FILTER on the left, the display and HOW
    MUCH in the middle, SHAPE and SPEED on the right. */
class EasyFilterAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyFilterAudioProcessorEditor (EasyFilterAudioProcessor&);

    enum KnobIndex { cutoffKnob, resonanceKnob, rateKnob, noteKnob, depthKnob, widthKnob, mixKnob, numKnobs };
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

    EasyFilterAudioProcessor& filterProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch typeSwitch, shapeSwitch, syncSwitch;
    juce::Label syncLabel;
    bool showingSynced = false;
    FilterDisplay display;

    std::array<juce::Rectangle<int>, 5> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyFilterAudioProcessorEditor)
};

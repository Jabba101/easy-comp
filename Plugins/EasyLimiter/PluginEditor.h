#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last few seconds - what's going in after GAIN
    (grey, it can go past the ceiling), what comes out (amber, never past
    it), the ceiling as a line, and how much it turned down (red, hanging
    from the top). */
class LimiterDisplay : public juce::Component
{
public:
    LimiterDisplay();
    void addMoments (const EasyLimiterAudioProcessor::Moment*, int count);
    void setCeiling (float ceilingDb);
    void setBypassed (bool isBypassed);
    void paint (juce::Graphics&) override;

    static constexpr int numMoments = 600;   // 3 seconds of 5 ms moments

private:
    std::vector<EasyLimiterAudioProcessor::Moment> history;
    int next = 0;
    float ceilingDb = -0.3f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Limiter window: HOW HARD and CEILING on the left, the display
    in the middle, RELEASE and COMPARE on the right. */
class EasyLimiterAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyLimiterAudioProcessorEditor (EasyLimiterAudioProcessor&);

    enum KnobIndex { gainKnob, ceilingKnob, releaseKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    juce::Button& getMatchButton()             { return matchButton; }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void mouseDown (const juce::MouseEvent&) override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyLimiterAudioProcessor& limiterProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch releaseSwitch;
    LightButton matchButton { "MATCH", Palette::accent };
    std::unique_ptr<juce::ButtonParameterAttachment> matchAttachment;
    GainReductionMeter reductionMeter;
    juce::Label matchInfo, peakReadout;
    LimiterDisplay display;
    std::vector<EasyLimiterAudioProcessor::Moment> incoming;
    float shownPeak = 1000.0f;

    std::array<juce::Rectangle<int>, 4> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyLimiterAudioProcessorEditor)
};

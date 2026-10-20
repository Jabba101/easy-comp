#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** The meter showing how much it's turning the sound up (green, above the
    middle line) and down (red, below) right now. */
class GainChangeMeter : public juce::Component
{
public:
    /** Feed it the latest boost and cut (both as positive dB), ~30 times a second. */
    void setChange (float boostDb, float cutDb);

    void paint (juce::Graphics&) override;

private:
    float shownBoost = 0.0f, shownCut = 0.0f;   // jump up instantly, fall back smoothly
};

//==============================================================================
/** "WHAT IT'S DOING": the last couple of seconds of sound, before (grey)
    and after (amber), scrolling from right to left. */
class MomentDisplay : public juce::Component
{
public:
    MomentDisplay();

    void addMoments (const EasyTransientAudioProcessor::Moment* newMoments, int count);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    static constexpr int numMoments = 480;   // 2.4 seconds of 5 ms moments

private:
    juce::Rectangle<float> getPlotArea() const;

    std::vector<EasyTransientAudioProcessor::Moment> history;   // a ring: "next" is the oldest
    int next = 0;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Transient window: the shared header and help bar (from
    EasyWindow), with the knobs, the display and the meter in between. */
class EasyTransientAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyTransientAudioProcessorEditor (EasyTransientAudioProcessor&);

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyTransientAudioProcessor& shaperProcessor;

    std::array<Knob, 4> knobs;   // Punch, Sustain | Output, Mix
    static constexpr size_t outputKnob = 2;

    MomentDisplay display;
    GainChangeMeter meter;
    std::vector<EasyTransientAudioProcessor::Moment> incoming;

    bool  showingAutoGain = false;
    float shownAutoGainDb = 0.0f;

    juce::Rectangle<int> displayPanel, meterPanel;
    std::array<juce::Rectangle<int>, 2> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTransientAudioProcessorEditor)
};

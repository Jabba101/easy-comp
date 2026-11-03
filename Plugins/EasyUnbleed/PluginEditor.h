#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": band by band (low on the left, high on the right) -
    how loud the stem is (amber), how loud the other stems are (blue), and
    how much it's turning each band down (red, hanging from the top). The
    range outside FROM / TO is shaded: it's left alone. */
class UnbleedDisplay : public juce::Component
{
public:
    void setPicture (const EasyUnbleedAudioProcessor::Picture&, float fromHz, float toHz, bool bypassed);
    void paint (juce::Graphics&) override;

private:
    EasyUnbleedAudioProcessor::Picture picture;
    std::array<float, SpectralUnbleed::numBandsMax> shownStem {}, shownOthers {}, shownCut {};
    float fromHz = 20.0f, toHz = 20000.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Unbleed window: HOW MUCH and HOW on the left, the display in the
    middle, WHERE and LISTEN on the right. */
class EasyUnbleedAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyUnbleedAudioProcessorEditor (EasyUnbleedAudioProcessor&);

    enum KnobIndex { amountKnob, sensitivityKnob, smoothnessKnob, fromKnob, toKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    juce::Button& getListenButton()            { return listenButton; }
    juce::String getStatusText() const         { return status.getText(); }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

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

    EasyUnbleedAudioProcessor& unbleedProcessor;
    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    LightButton listenButton { "REMOVED ONLY", Palette::meterHigh };
    std::unique_ptr<juce::ButtonParameterAttachment> listenAttachment;
    juce::Label listenInfo, status;
    UnbleedDisplay display;

    std::array<juce::Rectangle<int>, 4> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyUnbleedAudioProcessorEditor)
};

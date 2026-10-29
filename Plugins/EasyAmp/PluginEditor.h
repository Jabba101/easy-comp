#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the amp's curve at the current DRIVE - how a sample
    going in (across) comes out (up). A straight diagonal is clean; the more
    it bends, the more it's distorting, and a curve that's flatter on one
    side than the other is the valve's lopsidedness (its warmth). The dot
    rides along it at the level your track is playing right now. */
class AmpCurveDisplay : public juce::Component
{
public:
    void setDrive (float driveAmount);              // 0..1
    void setInputPeak (float peak);                 // the loudest the track has just been (0..1+)
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    static constexpr float inputSpan = 0.6f;        // the drawing's edges: +-0.6, a little past a typical track's peaks

private:
    float drive = 0.0f, shownPeak = 0.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Amp window: the shared header and help bar (from EasyWindow),
    DRIVE in its own panel, the curve, TONE and TIGHT, and MIX. */
class EasyAmpAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyAmpAudioProcessorEditor (EasyAmpAudioProcessor&);

    // (public so the tests can find them)
    enum KnobIndex { driveKnob, toneKnob, tightKnob, mixKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }

    /** (Tests only: what the 30-times-a-second update does, right now.) */
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

    EasyAmpAudioProcessor& ampProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch qualitySwitch;
    juce::Label qualityLabel;
    AmpCurveDisplay display;

    float shownMatchDb = 0.0f;
    bool showingMatch = false;

    std::array<juce::Rectangle<int>, 3> groupPanels;
    juce::Rectangle<int> displayPanel, matchReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyAmpAudioProcessorEditor)
};

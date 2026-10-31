#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": two cycles of the movement - the volume (TREMOLO), or
    the left and right sides (AUTO-PAN) - with a dot riding along where it is
    right now. The beat lines show where it locks to the song. */
class MovementDisplay : public juce::Component
{
public:
    void setSettings (const Tremolo::Settings&);
    void setPosition (float cyclePosition);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

private:
    Tremolo::Settings settings;
    float position = 0.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Tremolo window: SPEED and HOW MUCH on the left, the movement in
    the middle, SHAPE and MODE on the right. */
class EasyTremoloAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyTremoloAudioProcessorEditor (EasyTremoloAudioProcessor&);

    // (public so the tests can find them)
    enum KnobIndex { rateKnob, noteKnob, depthKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }

    /** What the line under SYNC says, e.g. "1/8 note at 120 BPM = 4.00 pulses a second". */
    static juce::String describeRate (bool synced, int noteIndex, float rateHz, double bpm, bool tempoFromDAW);

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

    EasyTremoloAudioProcessor& tremoloProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch syncSwitch, shapeSwitch, modeSwitch;
    juce::Label syncLabel, rateInfo;
    bool showingSynced = false;
    MovementDisplay display;

    std::array<juce::Rectangle<int>, 4> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTremoloAudioProcessorEditor)
};

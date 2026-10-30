#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": a VU meter showing how hard the sound is hitting the
    tape (0 VU = the normal operating level; into the red = pushing it), and
    under it the machine's frequency response at the chosen SPEED - the head
    bump in the lows and how flat the top is. */
class TapeDisplay : public juce::Component
{
public:
    void setRecordVu (float vu);                    // in VU
    void setSpeed (TapeMachine::Speed);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    float getShownVu() const noexcept               { return shownVu; }

private:
    void paintMeter (juce::Graphics&, juce::Rectangle<float>);
    void paintResponse (juce::Graphics&, juce::Rectangle<float>);

    float shownVu = -40.0f;
    TapeMachine::Speed speed = TapeMachine::Speed::ips15;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Tape window: the shared header and help bar (from EasyWindow),
    DRIVE in its own panel, the VU meter and response, SPEED, and WOW &
    FLUTTER and HISS. */
class EasyTapeAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyTapeAudioProcessorEditor (EasyTapeAudioProcessor&);

    // (public so the tests can find them)
    enum KnobIndex { driveKnob, wowKnob, hissKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    ChoiceSwitch& getSpeedSwitch()             { return speedSwitch; }
    TapeDisplay& getDisplay()                  { return display; }

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

    EasyTapeAudioProcessor& tapeProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch speedSwitch, qualitySwitch;
    juce::Label speedLabel, qualityLabel;
    TapeDisplay display;

    float shownMatchDb = 0.0f;
    bool showingMatch = false;

    std::array<juce::Rectangle<int>, 3> groupPanels;
    juce::Rectangle<int> displayPanel, matchReadout;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTapeAudioProcessorEditor)
};

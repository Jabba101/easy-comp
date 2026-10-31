#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": your sound (the grey bar at the start) and the echoes
    that follow it - the left side above the line, the right side below -
    each one as loud as it will be. PING-PONG bounces from side to side;
    WIDE puts the right side's echoes later. The echoes glow while they're
    playing. */
class EchoDisplay : public juce::Component
{
public:
    void setSettings (const StereoDelay::Settings&);
    void setEchoLevel (float peak);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    float getSpanMs() const noexcept   { return spanMs; }

private:
    StereoDelay::Settings settings;
    float glow = 0.0f, spanMs = 2000.0f;
    bool bypassed = false;
};

//==============================================================================
/** The Easy Delay window: the shared header and help bar (from EasyWindow),
    WHEN (TIME / SYNC) and REPEATS (FEEDBACK, MODE) on the left, the echoes
    in the middle, TONE (LOW CUT, HIGH CUT, CHARACTER) and OUTPUT (DUCK, MIX)
    on the right. */
class EasyDelayAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyDelayAudioProcessorEditor (EasyDelayAudioProcessor&);

    // (public so the tests can find them)
    enum KnobIndex { timeKnob, noteKnob, feedbackKnob, lowCutKnob, highCutKnob, duckKnob, mixKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    EchoDisplay& getDisplay()                  { return display; }

    /** What the line under SYNC says, e.g. "1/8 note at 120 BPM = 250 ms". */
    static juce::String describeTime (bool synced, int noteIndex, float timeMs, double bpm, bool tempoFromDAW);

    /** (Tests only: what the 30-times-a-second update does, right now.) */
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

    EasyDelayAudioProcessor& delayProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch syncSwitch, modeSwitch, characterSwitch;
    juce::Label syncLabel, modeLabel, characterLabel, timeInfo;
    bool showingSynced = false;
    EchoDisplay display;

    std::array<juce::Rectangle<int>, 4> groupPanels;
    juce::Rectangle<int> displayPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyDelayAudioProcessorEditor)
};

#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last couple of seconds - your sound (grey) and the
    reverb (amber), scrolling from right to left. Every hit shows up as a
    spike with a flat-topped burst after it, stopping dead after LENGTH (the
    bar in the top corner shows how long that is).

    The scale moves to fit the sound, so the peaks stay in view whether the
    track is loud or quiet. */
class BurstDisplay : public juce::Component
{
public:
    BurstDisplay();

    void addMoments (const BitchSlapAudioProcessor::Moment* newMoments, int count);
    void setBypassed (bool isBypassed);
    void setLength (float lengthMs);

    void paint (juce::Graphics&) override;

    static constexpr int   numMoments = 480;     // 2.4 seconds of 5 ms moments
    static constexpr float momentMs   = 5.0f;
    static constexpr float rangeDb    = 60.0f;   // from the top edge to the bottom

private:
    juce::Rectangle<float> getPlotArea() const;
    void fitScale();

    std::vector<BitchSlapAudioProcessor::Moment> history;   // a ring: "next" is the oldest
    int next = 0;
    bool bypassed = false;
    float lengthMs = 300.0f;

    float topDb = 0.0f, targetTopDb = 0.0f;   // the scale glides from one to the other
    bool hasFitted = false;                   // (the first time, it jumps straight there)
};

//==============================================================================
/** The Bitch-Slap window: the shared header and help bar (from EasyWindow),
    with MIX, LENGTH and SYNC on the left and the display on the right. */
class BitchSlapAudioProcessorEditor : public EasyWindow
{
public:
    explicit BitchSlapAudioProcessorEditor (BitchSlapAudioProcessor&);

    /** What the line under SYNC says, e.g. "1/8 note at 120 BPM = 250 ms". */
    static juce::String describeLength (bool synced, int noteIndex, float lengthMs, double bpm, bool tempoFromDAW);

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

    BitchSlapAudioProcessor& slapProcessor;

    // MIX, LENGTH (in ms) and NOTE (LENGTH as a note: shown in LENGTH's place when SYNC is TEMPO)
    std::array<Knob, 3> knobs;
    static constexpr size_t mixKnob = 0, lengthKnob = 1, noteKnob = 2;

    juce::Label syncLabel;
    ChoiceSwitch syncSwitch;
    juce::Label lengthInfo;
    bool showingSynced = false;

    BurstDisplay display;
    std::vector<BitchSlapAudioProcessor::Moment> incoming;

    // Areas worked out in layoutBody() and drawn in paintBody()
    juce::Rectangle<int> displayPanel, mixScale;
    std::array<juce::Rectangle<int>, 2> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitchSlapAudioProcessorEditor)
};

#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last couple of seconds - your sound (grey) and the
    reverb (amber), scrolling from right to left. With BITCH-SLAP every hit
    shows up as a spike with a flat-topped burst after it, stopping dead
    after LENGTH; with the natural spaces, a tail fading away over DECAY.
    (The bar in the top corner shows how long LENGTH or DECAY is.)

    The scale moves to fit the sound, so the peaks stay in view whether the
    track is loud or quiet. */
class BurstDisplay : public juce::Component
{
public:
    BurstDisplay();

    void addMoments (const BitchSlapAudioProcessor::Moment* newMoments, int count);
    void setBypassed (bool isBypassed);
    void setLength (float lengthMs, const juce::String& name = "LENGTH");

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
    juce::String lengthName { "LENGTH" };

    float topDb = 0.0f, targetTopDb = 0.0f;   // the scale glides from one to the other
    bool hasFitted = false;                   // (the first time, it jumps straight there)
};

//==============================================================================
/** The Easy Verb window: the shared header and help bar (from EasyWindow),
    TYPE and MIX on the left, the display in the middle, and on the right
    LENGTH and SYNC (BITCH-SLAP) or DECAY, PRE-DELAY and TONE (the spaces). */
class BitchSlapAudioProcessorEditor : public EasyWindow
{
public:
    explicit BitchSlapAudioProcessorEditor (BitchSlapAudioProcessor&);

    juce::Button& getInitAllButton()           { return initAllButton; }
    juce::Slider& getKnob (size_t index)       { return knobs[index].slider; }
    bool isShowingSpaceKnobs() const noexcept  { return showingSpace; }

    /** What the line under SYNC says, e.g. "1/8 note at 120 BPM = 250 ms". */
    static juce::String describeLength (bool synced, int noteIndex, float lengthMs, double bpm, bool tempoFromDAW);

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void showKnobs (bool natural, bool synced);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    BitchSlapAudioProcessor& slapProcessor;

public:
    // MIX, LENGTH (in ms), NOTE (LENGTH as a note: shown in LENGTH's place when SYNC is TEMPO),
    // and the spaces' DECAY, PRE-DELAY and TONE
    static constexpr size_t mixKnob = 0, lengthKnob = 1, noteKnob = 2, decayKnob = 3, preDelayKnob = 4, toneKnob = 5;

private:
    std::array<Knob, 6> knobs;

    juce::TextButton initAllButton { "INIT" };
    ChoiceSwitch typeSwitch;
    juce::Label typeInfo;
    juce::Label syncLabel;
    ChoiceSwitch syncSwitch;
    juce::Label lengthInfo;
    bool showingSynced = false, showingSpace = false;

    BurstDisplay display;
    std::vector<BitchSlapAudioProcessor::Moment> incoming;

    // Areas worked out in layoutBody() and drawn in paintBody()
    juce::Rectangle<int> displayPanel, mixScale;
    std::array<juce::Rectangle<int>, 3> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitchSlapAudioProcessorEditor)
};

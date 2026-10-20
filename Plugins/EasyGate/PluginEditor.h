#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last 4 seconds, scrolling from right to left -
    what the gate hears (grey), what comes out (amber), the THRESHOLD line,
    and red shading wherever it turned the sound down.

    The scale stays put (0 dB at the top), so the THRESHOLD line always
    means the same thing: the gate opens wherever the grey goes over it. */
class GateDisplay : public juce::Component
{
public:
    GateDisplay();

    void addMoments (const EasyGateAudioProcessor::Moment* newMoments, int count);
    void setThreshold (float newThresholdDb);
    void setStatus (bool isBypassed, bool isListening, bool isSidechainMissing);

    void paint (juce::Graphics&) override;

    static constexpr int   numMoments = 800;      // 4 seconds of 5 ms moments
    static constexpr float topDb      = 0.0f;     // the scale: 0 dB at the top...
    static constexpr float bottomDb   = -84.0f;   // ...down to -84 dB

    /** Where the THRESHOLD line is drawn (for the tests). */
    float getThresholdY() const;

private:
    juce::Rectangle<float> getPlotArea() const;
    void drawBadge (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour, const juce::String& title, const juce::String& text) const;

    std::vector<EasyGateAudioProcessor::Moment> history;   // a ring: "next" is the oldest
    int next = 0;
    float thresholdDb = -40.0f;
    bool bypassed = false, listening = false, sidechainMissing = false;
};

//==============================================================================
/** The light: green while the gate is open, dimming as it closes. */
class GateLight : public juce::Component
{
public:
    /** Feed it ~30 times a second: whether it was open since last time, how far
        open it got (dB, 0 = fully), RANGE, and whether BYPASS is on. */
    void setState (bool wasOpen, float gainDb, float rangeDb, bool isBypassed);

    /** What it says under the light ("OPEN", "CLOSING", "CLOSED", "BYPASSED"). */
    const juce::String& getWord() const noexcept   { return word; }

    void paint (juce::Graphics&) override;

private:
    float brightness = 0.0f;
    juce::String word { "CLOSED" }, detail;
};

//==============================================================================
/** The Easy Gate window: the shared header and help bar (from EasyWindow),
    with the display and the light along the top and the knobs below. */
class EasyGateAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyGateAudioProcessorEditor (EasyGateAudioProcessor&);
    ~EasyGateAudioProcessorEditor() override;

    // (public so the parts can be found and tested)
    LightButton listenButton { "LISTEN", Palette::accent };

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

    EasyGateAudioProcessor& gateProcessor;

    std::array<Knob, 7> knobs;   // THRESHOLD, RANGE | ATTACK, HOLD, RELEASE | IGNORE BELOW, IGNORE ABOVE

    ChoiceSwitch openedBySwitch;
    juce::Label openedByLabel;

    GateDisplay display;
    GateLight light;
    std::vector<EasyGateAudioProcessor::Moment> incoming;

    juce::Rectangle<int> displayPanel, lightPanel;
    std::array<juce::Rectangle<int>, 3> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyGateAudioProcessorEditor)
};

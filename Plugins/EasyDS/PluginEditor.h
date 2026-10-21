#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last 4 seconds, scrolling from right to left - the
    whole sound (grey), the harsh band (amber), the THRESHOLD line (dashed),
    and red shading wherever it turned the sound down.

    The THRESHOLD line follows the rest of your sound up and down (THRESHOLD
    sets how far above or below it the line sits), so it means the same
    thing whether the singer is whispering or belting: it turns down
    wherever the amber goes over the line. The scale stays put (0 dB at the
    top). It also shows what FIND is doing, and what it found. */
class DSDisplay : public juce::Component
{
public:
    DSDisplay();

    void addMoments (const EasyDSAudioProcessor::Moment* newMoments, int count);
    void setThreshold (float newThresholdDb);
    void setStatus (bool isBypassed, bool isListening);

    /** What FIND is doing, for the badge in the middle (empty title = nothing to show). */
    void setFindMessage (const juce::String& title, const juce::String& text, float progress = -1.0f);

    void paint (juce::Graphics&) override;

    static constexpr int   numMoments = 800;      // 4 seconds of 5 ms moments
    static constexpr float topDb      = 0.0f;     // the scale: 0 dB at the top...
    static constexpr float bottomDb   = -72.0f;   // ...down to -72 dB

    // (for the tests)
    const juce::String& getFindTitle() const noexcept   { return findTitle; }
    const juce::String& getFindText() const noexcept    { return findText; }

private:
    juce::Rectangle<float> getPlotArea() const;
    void drawBadge (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour, const juce::String& title,
                    const juce::String& text, float progress) const;

    std::vector<EasyDSAudioProcessor::Moment> history;   // a ring: "next" is the oldest
    int next = 0;
    float thresholdDb = -6.0f;
    bool bypassed = false, listening = false;
    juce::String findTitle, findText;
    float findProgress = -1.0f;
};

//==============================================================================
/** The Easy DS window: the shared header and help bar (from EasyWindow), with
    the display and the meter along the top and the knobs below. */
class EasyDSAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyDSAudioProcessorEditor (EasyDSAudioProcessor&);
    ~EasyDSAudioProcessorEditor() override;

    // (public so the parts can be found and tested)
    LightButton listenButton { "LISTEN", Palette::accent };
    LightButton findButton   { "FIND", Palette::accent };
    DSDisplay display;

    /** FIND, as if its button were clicked (the tests use these). */
    void startFinding();
    void finishFinding();

    /** FIND's analysis of the frames collected so far, applied to FROM and TO
        if it found something. Returns what it found. */
    HarshnessFinder::Result applyFind (const std::vector<HarshnessFinder::Frame>& collected);

    /** How many seconds of sound FIND listens to before it decides. */
    static constexpr double findSeconds = 8.0;

    /** Collects what FIND has heard, and decides once it's heard enough.
        (Called 30 times a second; public for the tests.) */
    void findStep();

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void setParameter (const char* id, float value);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyDSAudioProcessor& dsProcessor;

    std::array<Knob, 4> knobs;   // FROM, TO | THRESHOLD, RANGE

    ChoiceSwitch turnsDownSwitch;
    juce::Label findLabel;

    GainReductionMeter meter;
    std::vector<EasyDSAudioProcessor::Moment> incoming;

    // FIND
    bool findingNow = false;
    std::vector<HarshnessFinder::Frame> collected, arriving;
    int soundFrames = 0, framesSinceSound = 0;
    double messageUntil = 0.0;   // (when the "found it" message goes away: milliseconds since the computer started)

    juce::Rectangle<int> displayPanel, meterPanel;
    std::array<juce::Rectangle<int>, 3> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyDSAudioProcessorEditor)
};

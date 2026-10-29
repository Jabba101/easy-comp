#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** SHAVED OFF: how many dB the clipper took off the loudest moment, as a
    column of LEDs (0 at the top, 12 dB at the bottom) and a number. Green
    up to 3 dB (usually unnoticeable), amber to 6 dB, red beyond. */
class ShavedMeter : public juce::Component
{
public:
    /** Feed it the latest amount shaved (positive dB), ~30 times a second. */
    void setShaved (float shavedDb);

    void paint (juce::Graphics&) override;

    static constexpr float maxDb = 12.0f;

private:
    float shownDb = 0.0f;   // jumps up instantly, falls back smoothly
};

//==============================================================================
/** "WHAT IT'S DOING": the last couple of seconds, scrolling from right to
    left. Grey = how loud it would have been (pushed, not clipped); amber =
    what comes out; red = what was shaved off; the dashed line = CEILING. */
class ClipDisplay : public juce::Component
{
public:
    ClipDisplay();

    void addMoments (const EasyClipAudioProcessor::Moment* newMoments, int count);
    void setCeiling (float ceilingDb);
    void setBypassed (bool isBypassed);

    void paint (juce::Graphics&) override;

    static constexpr int   numMoments = 480;     // 2.4 seconds of 5 ms moments
    static constexpr float topDb      = 12.0f;   // a fixed scale: +12 dB at the top,
    static constexpr float rangeDb    = 24.0f;   // 0 in the middle, -12 at the bottom

private:
    juce::Rectangle<float> getPlotArea() const;

    std::vector<EasyClipAudioProcessor::Moment> history;   // a ring: "next" is the oldest
    int next = 0;
    bool bypassed = false;
    float ceilingDb = 0.0f;
};

//==============================================================================
/** The Easy Clip window: the shared header and help bar (from EasyWindow),
    the SIMPLE / ADVANCED and OVERSAMPLING switches, the knobs in numbered
    panels, the display and the SHAVED OFF meter. ADVANCED adds panel 3,
    SOUND DESIGN, under the display. */
class EasyClipAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyClipAudioProcessorEditor (EasyClipAudioProcessor&);

    bool isShowingAdvanced() const noexcept   { return showingAdvanced; }

    // (public so the tests can find them)
    enum KnobIndex { pushKnob, ceilingKnob, softnessKnob, protectKnob, topKnob, bottomKnob, biasKnob, blendKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getLinkButton()              { return linkButton; }
    juce::Button& getAdvancedInitButton()      { return initButton; }
    juce::Button& getInitAllButton()           { return initAllButton; }

    /** (Tests only: treat changes to TOP and BOTTOM as made by hand, so LINK applies.) */
    bool linkEverything = false;

    /** (Tests only: what the 30-times-a-second update does, right now.) */
    void updateDisplaysNow()                  { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;

    void sideMoved (int moved, int other);   // LINK: TOP and BOTTOM move together

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyClipAudioProcessor& clipProcessor;

    std::array<Knob, numKnobs> knobs;
    LightButton linkButton { "LINK", Palette::accent };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttachment;
    juce::TextButton initButton { "INIT" }, initAllButton { "INIT" };

    ChoiceSwitch viewSwitch, qualitySwitch;
    juce::Label qualityLabel;

    ClipDisplay display;
    ShavedMeter meter;
    std::vector<EasyClipAudioProcessor::Moment> incoming;

    bool showingAdvanced = false, advancedLampLit = false;
    std::array<float, 2> lastSide { 0.0f, 0.0f };   // TOP and BOTTOM as last seen
    bool movingPartner = false;

    std::array<juce::Rectangle<int>, 3> groupPanels;
    juce::Rectangle<int> displayPanel, meterPanel, lampArea, linkGroup;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyClipAudioProcessorEditor)
};

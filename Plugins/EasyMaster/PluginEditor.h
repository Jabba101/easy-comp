#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last six seconds - into the limiter (grey), out
    (amber), the ceiling, and from the top how much the limiter (red) and
    the compressor (green = MID or both, blue = SIDE) are turning down. */
class MasterDisplay : public juce::Component
{
public:
    MasterDisplay();
    void addMoments (const EasyMasterAudioProcessor::Moment*, int count);
    void setState (float ceilingDb, bool midSide, bool compOn, bool bypassed);
    void paint (juce::Graphics&) override;

    static constexpr int numMoments = 600;   // 6 seconds of 10 ms moments

private:
    std::vector<EasyMasterAudioProcessor::Moment> history;
    int next = 0;
    float ceilingDb = -0.3f;
    bool midSide = false, compOn = true, bypassed = false;
};

//==============================================================================
/** The RESONANCE TAMER page's display: the mix a sixth of an octave at a
    time, and from the top how much each spot is turned down. */
class TamerDisplay : public juce::Component
{
public:
    void setPicture (const EasyMasterAudioProcessor::TamerPicture&, bool midSide, bool on, float fromHz, float toHz, bool bypassed);
    void paint (juce::Graphics&) override;

private:
    EasyMasterAudioProcessor::TamerPicture picture;
    std::array<std::array<float, ResonanceTamer::numBands>, 2> shownLevel {};
    bool midSide = false, on = true, bypassed = false;
    float fromHz = 200.0f, toHz = 16000.0f;
};

//==============================================================================
/** The readings: LUFS momentary / short-term / integrated, the highest true
    peak, PLR, stereo correlation, and how much each part turns down now.
    Click it to reset the integrated LUFS and the highest peak. */
class MasterMeters : public juce::Component
{
public:
    void setReadings (const EasyMasterAudioProcessor::Readings&, bool midSide, bool compOn, bool tamerOn);
    void paint (juce::Graphics&) override;
    std::function<void()> onReset;
    void mouseDown (const juce::MouseEvent&) override   { if (onReset) onReset(); }

private:
    EasyMasterAudioProcessor::Readings r;
    float shownMid = 0.0f, shownSide = 0.0f, shownLimit = 0.0f, shownTamer = 0.0f;
    bool midSide = false, compOn = true, tamerOn = true;
};

//==============================================================================
/** The Easy Master window: the chain along the top (click a stage to show
    its page: its own display and controls), INPUT on the left, the page in
    the middle, and the METERS, LIMITER and COMPARE always on the right. */
class EasyMasterAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor&);

    enum KnobIndex { trimKnob,
                     midThresholdKnob, midRatioKnob, midAttackKnob, midReleaseKnob, midMakeupKnob,
                     sideThresholdKnob, sideRatioKnob, sideAttackKnob, sideReleaseKnob, sideMakeupKnob,
                     mixKnob, ignoreKnob, gainKnob, ceilingKnob, releaseKnob,
                     tMidAmountKnob, tMidSensKnob, tMidSharpKnob, tSideAmountKnob, tSideSensKnob, tSideSharpKnob,
                     tSpeedKnob, tFromKnob, tToKnob, numKnobs };

    /** The pages the chain along the top switches between. */
    enum Page { tamerPage = 0, compPage, numPages };
    void showPage (int page);
    int getShownPage() const noexcept          { return shownPage; }

    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    bool isKnobShowing (int index) const       { return knobs[(size_t) index].slider.isVisible(); }
    bool isKnobUsable (int index) const        { return knobs[(size_t) index].slider.isEnabled(); }
    juce::Button& getInitAllButton()           { return initAllButton; }
    juce::Button& getSlotButton (int slot)     { return slot == 0 ? slotA : slotB; }
    juce::Button& getCopyButton()              { return copyButton; }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void showParts();
    void mouseDown (const juce::MouseEvent&) override;
    std::vector<juce::Component*> pageComponents (int page);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyMasterAudioProcessor& masterProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    LightButton compOnButton { "ON", Palette::meterLow };
    std::unique_ptr<juce::ButtonParameterAttachment> compOnAttachment;
    ChoiceSwitch modeSwitch, reactsSwitch, characterSwitch, releaseSwitch, listenSwitch;
    LightButton matchButton { "MATCH", Palette::accent };
    std::unique_ptr<juce::ButtonParameterAttachment> matchAttachment;
    LightButton slotA { "A", Palette::accent }, slotB { "B", Palette::accent };
    juce::TextButton copyButton { "COPY" };
    juce::Label matchInfo, inputWarning, listenLabel, abLabel;
    MasterDisplay display;
    TamerDisplay tamerDisplay;
    LightButton tamerOnButton { "ON", Palette::meterLow }, tamerListenButton { "LISTEN: REMOVED ONLY", Palette::meterMid };
    std::unique_ptr<juce::ButtonParameterAttachment> tamerOnAttachment, tamerListenAttachment;
    ChoiceSwitch tamerModeSwitch;
    juce::Rectangle<int> tamerMidName, tamerSideName, pagePanel, pageDisplayPanel;
    std::array<juce::Rectangle<float>, 9> chainBoxes;
    int shownPage = compPage;
    bool shownTamerMidSide = false, shownTamerOn = true;
    MasterMeters meters;
    std::vector<EasyMasterAudioProcessor::Moment> incoming;
    bool shownMidSide = false, shownCompOn = true;

    juce::Rectangle<int> midRowName, sideRowName, chainArea, metersPanel, inputPanel, limiterPanel, comparePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterAudioProcessorEditor)
};

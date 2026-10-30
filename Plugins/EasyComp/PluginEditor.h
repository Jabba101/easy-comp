#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** The compression curve: how loud the sound comes out (up the side) for each
    level going in (along the bottom), with a dot showing your sound as it plays. */
class CurveGraph : public juce::Component
{
public:
    /** Call ~30 times a second with the current settings, the loudest level the
        compressor heard since last time, and the current AUTO-GAIN amount.
        Only repaints when something changed. */
    void update (const Compressor::Settings& newSettings, float inputLevelDb, float newAutoGainDb);

    /** Whether it's listening to the sidechain (LISTENS TO), and whether
        nothing is coming into it. */
    void setSidechainStatus (bool isListeningToSidechain, bool isSidechainMissing);

    void paint (juce::Graphics&) override;

private:
    static constexpr float lowestDb  = -60.0f;   // the graph's range, both ways
    static constexpr float highestDb = 0.0f;

    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> toScreen (juce::Rectangle<float> plot, float inDb, float outDb) const;
    void drawBadge (juce::Graphics&, juce::Rectangle<float> plot, juce::Colour, const juce::String& title, const juce::String& text) const;

    Compressor::Settings settings;
    float autoGainDb = 0.0f;
    float dotLevelDb = Compressor::silenceDb;   // jumps up instantly, falls back smoothly
    bool listeningToSidechain = false, sidechainMissing = false;
};

//==============================================================================
/** The Easy Comp window: the shared header and help bar (from EasyWindow),
    with the knobs, the curve graph and the meter in between. */
class EasyCompAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyCompAudioProcessorEditor (EasyCompAudioProcessor&);

    // A / B: two sets of settings to compare (public so they can be tested)
    LightButton slotA { "A", Palette::accent }, slotB { "B", Palette::accent };
    juce::TextButton copyButton { "COPY A > B" };

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

    /** A setting with a few named choices: its name, and the light-up buttons. */
    struct Switch
    {
        Switch (juce::RangedAudioParameter& parameter, const juce::String& name, const juce::StringArray& options);

        juce::Label label;
        ChoiceSwitch choices;
    };

    void addSwitch (Switch&, const char* hint);

    EasyCompAudioProcessor& compProcessor;

    // Threshold, Ratio | Attack, Release | Make-up, Mix | Ignore Below
    std::array<Knob, 7> knobs;
    static constexpr size_t makeupKnob = 4;

    // REACTS TO and LISTENS TO (beside IGNORE BELOW); STYLE and LOW END (CHARACTER)
    Switch reactsTo, listensTo, style, lowEnd;

    CurveGraph graph;
    GainReductionMeter meter;

    // What the MAKE-UP box is showing while AUTO-GAIN is on
    bool  showingAutoGain = false;
    float shownAutoGainDb = 0.0f;

    // Areas worked out in layoutBody() and drawn in paintBody()
    juce::Rectangle<int> graphPanel, meterPanel;
    std::array<juce::Rectangle<int>, 5> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyCompAudioProcessorEditor)
};

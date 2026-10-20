#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** The vertical LED meter showing how many dB the compressor is turning the
    sound down right now. */
class GainReductionMeter : public juce::Component
{
public:
    /** Feed it the latest reduction in dB (a positive number), ~30 times a second. */
    void setGainReduction (float reductionDb);

    void paint (juce::Graphics&) override;

private:
    float shownDb = 0.0f;   // what's drawn: jumps up instantly, falls back smoothly
};

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

    void paint (juce::Graphics&) override;

private:
    static constexpr float lowestDb  = -60.0f;   // the graph's range, both ways
    static constexpr float highestDb = 0.0f;

    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> toScreen (juce::Rectangle<float> plot, float inDb, float outDb) const;

    Compressor::Settings settings;
    float autoGainDb = 0.0f;
    float dotLevelDb = Compressor::silenceDb;   // jumps up instantly, falls back smoothly
};

//==============================================================================
/** The Easy Comp window: the shared header and help bar (from EasyWindow),
    with the knobs, the curve graph and the meter in between. */
class EasyCompAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyCompAudioProcessorEditor (EasyCompAudioProcessor&);

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

    EasyCompAudioProcessor& compProcessor;

    // Threshold, Ratio | Attack, Release | Make-up, Mix | Ignore Below
    std::array<Knob, 7> knobs;
    static constexpr size_t makeupKnob = 4;

    // REACTS TO: PEAKS / AVERAGE (beside IGNORE BELOW)
    juce::Label reactsToLabel;
    ChoiceSwitch reactsToSwitch;

    CurveGraph graph;
    GainReductionMeter meter;

    // What the MAKE-UP box is showing while AUTO-GAIN is on
    bool  showingAutoGain = false;
    float shownAutoGainDb = 0.0f;

    // Areas worked out in layoutBody() and drawn in paintBody()
    juce::Rectangle<int> graphPanel, meterPanel;
    std::array<juce::Rectangle<int>, 4> groupPanels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyCompAudioProcessorEditor)
};

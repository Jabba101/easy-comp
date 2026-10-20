#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Compressor.h"
#include "Presets.h"

//==============================================================================
/**
    The plugin as your DAW sees it.

    This class is the plumbing: it creates the parameters (the knobs your DAW
    can see, save and automate), hands their values to the Compressor once per
    block of audio, and reports what's happening so the meter and the curve
    graph can show it. The sound-shaping maths lives in Compressor.h.
*/
class EasyCompAudioProcessor : public juce::AudioProcessor
{
public:
    EasyCompAudioProcessor();
    ~EasyCompAudioProcessor() override = default;

    //==== Audio ===============================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    //==== The plugin window ===================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    //==== Basic info for the DAW ==============================================
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    // Just one "program": the presets live in the plugin's own menu instead,
    // because some DAWs re-select program 0 when they load a project, which
    // would wipe your settings.
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    //==== Saving and loading with your DAW project ============================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==== Used by the plugin window ===========================================
    /** The parameters, plus the glue that keeps knobs and DAW in sync. */
    juce::AudioProcessorValueTreeState parameters;

    /** Sets all the knobs to one of the presets in Presets.h. */
    void loadPreset (int presetIndex);

    /** The most gain reduction (dB, as a positive number) since the meter last
        asked. Safe to call from the window while audio is playing. */
    float takeGainReductionForMeter() noexcept    { return meterReductionDb.exchange (0.0f); }

    /** The loudest level the compressor heard (dB) since the graph last asked.
        Safe to call from the window while audio is playing. */
    float takeInputLevelForGraph() noexcept       { return graphLevelDb.exchange (Compressor::silenceDb); }

    /** All the knob values right now, in the form the compressor uses. */
    Compressor::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Direct, lock-free access to each knob's current value from the audio thread.
    std::atomic<float>* thresholdValue = nullptr;
    std::atomic<float>* ratioValue     = nullptr;
    std::atomic<float>* attackValue    = nullptr;
    std::atomic<float>* releaseValue   = nullptr;
    std::atomic<float>* makeupValue    = nullptr;
    std::atomic<float>* mixValue       = nullptr;
    std::atomic<float>* lowCutValue    = nullptr;

    Compressor compressor;
    std::atomic<float> meterReductionDb { 0.0f };
    std::atomic<float> graphLevelDb { Compressor::silenceDb };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyCompAudioProcessor)
};

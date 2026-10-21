#pragma once

#include "EasyProcessor.h"
#include "Compressor.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Comp as your DAW sees it.

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds the compressor's knobs and its
    sidechain input, hands their values to the Compressor once per block of
    audio, and reports what's happening so the meter and the curve graph can
    show it. The sound-shaping maths lives in Effects/Compressor.h.
*/
class EasyCompAudioProcessor : public EasyProcessor
{
public:
    EasyCompAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** The most gain reduction (dB, as a positive number) since the meter last
        asked. Safe to call from the window while audio is playing. */
    float takeGainReductionForMeter() noexcept    { return meterReductionDb.exchange (0.0f); }

    /** The loudest level the compressor heard (dB) since the graph last asked. */
    float takeInputLevelForGraph() noexcept       { return graphLevelDb.exchange (Compressor::silenceDb); }

    /** How many dB AUTO-GAIN is adding right now (it keeps measuring even
        while it's switched off, so it's ready the moment you switch it on). */
    float getAutoGainDb() const noexcept          { return autoGainDb.load(); }

    /** True if LISTENS TO is set to SIDECHAIN. */
    bool usesSidechain() const noexcept           { return listensToValue->load() > 0.5f; }

    /** True while LISTENS TO is SIDECHAIN, your track is playing, but nothing
        has come into the sidechain for a while (so it can't compress). */
    bool isSidechainMissing() const noexcept      { return sidechainMissing.load(); }

    /** All the settings right now, in the form the compressor uses. */
    Compressor::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void presetLoaded() override                  { autoGainRestartRequested.store (true); }

    // Direct, lock-free access to each setting from the audio thread.
    std::atomic<float>* thresholdValue = nullptr;
    std::atomic<float>* ratioValue     = nullptr;
    std::atomic<float>* attackValue    = nullptr;
    std::atomic<float>* releaseValue   = nullptr;
    std::atomic<float>* makeupValue    = nullptr;
    std::atomic<float>* mixValue       = nullptr;
    std::atomic<float>* lowCutValue    = nullptr;
    std::atomic<float>* reactsToValue  = nullptr;
    std::atomic<float>* characterValue = nullptr;
    std::atomic<float>* lowEndValue    = nullptr;
    std::atomic<float>* listensToValue = nullptr;
    std::atomic<float>* autoGainValue  = nullptr;
    std::atomic<float>* bypassValue    = nullptr;

    Compressor compressor;
    std::atomic<float> meterReductionDb { 0.0f };
    std::atomic<float> graphLevelDb { Compressor::silenceDb };
    std::atomic<float> autoGainDb { 0.0f };
    std::atomic<bool>  autoGainRestartRequested { false };   // set when a preset loads, acted on by the audio

    // Noticing a sidechain with nothing coming in
    std::atomic<bool> sidechainMissing { false };
    double currentSampleRate = 44100.0;
    int samplesSinceTrackSound = 1 << 30, samplesSinceSidechainSound = 1 << 30;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyCompAudioProcessor)
};

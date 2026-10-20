#pragma once

#include "EasyProcessor.h"
#include "TransientShaper.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Trans as your DAW sees it. (It was called Easy Transient up to
    version 1.2.0. Your DAW recognises it by a code that hasn't changed, so
    projects that used Easy Transient open with Easy Trans.)

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds the shaper's knobs, hands their values
    to the TransientShaper once per block of audio, and notes how loud each
    moment was going in and coming out, so the window can draw it. The
    sound-shaping maths lives in Effects/TransientShaper.h.
*/
class EasyTransientAudioProcessor : public EasyProcessor
{
public:
    EasyTransientAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** One 5 ms moment of sound, for the "what it's doing" display. */
    struct Moment
    {
        float inDb  = TransientShaper::silenceDb;   // loudest level going in
        float outDb = TransientShaper::silenceDb;   // loudest level coming out
    };

    /** Hands over the moments recorded since last time (oldest first).
        Returns how many were written. Safe to call from the window. */
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** The most it turned the sound up / down (dB) since the meter last asked. */
    float takeBoostForMeter() noexcept      { return meterBoostDb.exchange (0.0f); }
    float takeCutForMeter() noexcept        { return meterCutDb.exchange (0.0f); }

    /** How many dB AUTO-GAIN is adding (or taking away) right now. */
    float getAutoGainDb() const noexcept    { return autoGainDb.load(); }

    /** All the settings right now, in the form the shaper uses. */
    TransientShaper::Settings getCurrentSettings() const noexcept;

    /** Your own presets stay where Easy Transient kept them. */
    juce::String getPresetFolderName() const override   { return "Easy Transient"; }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void presetLoaded() override            { autoGainRestartRequested.store (true); }

    std::atomic<float>* punchValue    = nullptr;
    std::atomic<float>* sustainValue  = nullptr;
    std::atomic<float>* outputValue   = nullptr;
    std::atomic<float>* mixValue      = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* bypassValue   = nullptr;

    TransientShaper shaper;
    std::atomic<float> meterBoostDb { 0.0f }, meterCutDb { 0.0f }, autoGainDb { 0.0f };
    std::atomic<bool>  autoGainRestartRequested { false };

    // The display's moments: filled by the audio, emptied by the window,
    // without either ever waiting for the other.
    int momentLength = 240, momentFilled = 0;
    float momentIn = 0.0f, momentOut = 0.0f;
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTransientAudioProcessor)
};

#pragma once

#include "EasyProcessor.h"
#include "ValveAmp.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Amp as your DAW sees it.

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds the amp's knobs, hands their values to
    the ValveAmp once per block of audio, and notes how loud the sound going
    in is, so the window can show where on the amp's curve it's sitting. The
    sound itself lives in Effects/ValveAmp.h.

    AUTO-GAIN starts switched ON here (in the other plugins it starts off):
    turning DRIVE up should change the tone, not the volume.
*/
class EasyAmpAudioProcessor : public EasyProcessor
{
public:
    EasyAmpAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** The loudest the sound going in has been since the window last asked (0..1+). */
    float takeInputPeak() noexcept           { return inputPeak.exchange (0.0f); }

    /** How many dB the level matching is adding (or taking away) right now. */
    float getMatchGainDb() const noexcept    { return matchGainDb.load(); }

    /** All the settings right now, in the form the amp uses. */
    ValveAmp::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* driveValue    = nullptr;
    std::atomic<float>* toneValue     = nullptr;
    std::atomic<float>* tightValue    = nullptr;
    std::atomic<float>* mixValue      = nullptr;
    std::atomic<float>* qualityValue  = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* bypassValue   = nullptr;

    ValveAmp amp;
    std::atomic<float> inputPeak { 0.0f }, matchGainDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyAmpAudioProcessor)
};

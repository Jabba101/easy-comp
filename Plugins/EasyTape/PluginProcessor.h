#pragma once

#include "EasyProcessor.h"
#include "TapeMachine.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Tape as your DAW sees it.

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds SPEED, DRIVE, WOW & FLUTTER and HISS,
    hands their values to the TapeMachine once per block of audio, and keeps
    the VU reading for the window. The sound itself lives in
    Effects/TapeMachine.h.

    AUTO-GAIN starts switched ON here (as in Easy Amp): DRIVE changes how
    hard the tape works, not the volume.
*/
class EasyTapeAudioProcessor : public EasyProcessor
{
public:
    EasyTapeAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** How loud the sound hitting the tape is, in VU (0 = the normal level), with a VU meter's ~300 ms swing. */
    float getRecordVu() const noexcept       { return recordVu.load(); }

    /** How many dB the level matching is adding (or taking away) right now. */
    float getMatchGainDb() const noexcept    { return matchGainDb.load(); }

    /** All the settings right now, in the form the tape machine uses. */
    TapeMachine::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* speedValue    = nullptr;
    std::atomic<float>* driveValue    = nullptr;
    std::atomic<float>* wowValue      = nullptr;
    std::atomic<float>* hissValue     = nullptr;
    std::atomic<float>* qualityValue  = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* bypassValue   = nullptr;

    TapeMachine machine;
    std::atomic<float> recordVu { -100.0f }, matchGainDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTapeAudioProcessor)
};

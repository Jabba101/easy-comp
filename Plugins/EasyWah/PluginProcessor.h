#pragma once

#include "EasyProcessor.h"
#include "AutoWah.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Wah as your DAW sees it: an automatic wah - following how hard you
    play, or rocking in time with the song.

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds the wah's knobs and switches, reads
    the song's tempo and position (for TEMPO), and hands them to the
    AutoWah once per block. The sound itself lives in Effects/AutoWah.h.

    AUTO-GAIN starts ON here: a wah takes level away, and it shouldn't
    sound quieter as a result.
*/
class EasyWahAudioProcessor : public EasyProcessor
{
public:
    EasyWahAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    AutoWah::Settings getCurrentSettings() const noexcept;

    /** Where the pedal has been since the window last asked (0 heel .. 1 toe). */
    std::pair<float, float> takePedalRange() noexcept
    {
        return { pedalLow.exchange (2.0f), pedalHigh.exchange (-1.0f) };
    }
    float getMakeUpDb() const noexcept       { return makeUpDb.load(); }
    double getTempo() const noexcept         { return tempo.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* modeValue = nullptr;
    std::atomic<float>* sensitivityValue = nullptr;
    std::atomic<float>* responseValue = nullptr;
    std::atomic<float>* directionValue = nullptr;
    std::atomic<float>* noteValue = nullptr;
    std::atomic<float>* pedalValue = nullptr;
    std::atomic<float>* fromValue = nullptr;
    std::atomic<float>* toValue = nullptr;
    std::atomic<float>* resonanceValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* autoGainValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    AutoWah wah;
    std::atomic<double> tempo { 120.0 };
    std::atomic<float> pedalLow { 2.0f }, pedalHigh { -1.0f }, makeUpDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyWahAudioProcessor)
};

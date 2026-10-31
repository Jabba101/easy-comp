#pragma once

#include "EasyProcessor.h"
#include "StereoDelay.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Delay as your DAW sees it: a stereo delay (echo).

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds TIME (or a note, following the song's tempo,
    with SYNC), FEEDBACK, MODE, LOW CUT, HIGH CUT, CHARACTER, DUCK and MIX,
    and hands them to the StereoDelay once per block of audio. The sound
    itself lives in Effects/StereoDelay.h.
*/
class EasyDelayAudioProcessor : public EasyProcessor
{
public:
    EasyDelayAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** Mono or stereo tracks - and a mono track with a stereo output, which gets stereo echoes. */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    /** How long the echoes go on after the sound stops (so your DAW doesn't cut them off). */
    double getTailLengthSeconds() const override;

    /** All the settings right now, in the form the delay uses (with SYNC worked out). */
    StereoDelay::Settings getCurrentSettings() const noexcept;

    /** The loudest your sound / the echoes were since the window last asked (gain). */
    float takeInputPeak() noexcept           { return inputPeak.exchange (0.0f); }
    float takeEchoPeak() noexcept            { return echoPeak.exchange (0.0f); }

    /** The song's tempo, as your DAW last said (120 until it says). */
    double getTempo() const noexcept         { return tempo.load(); }
    bool isTempoFromDAW() const noexcept     { return tempoFromDAW.load(); }

    /** How long a note is (ms) at a tempo, kept to what the delay can do. */
    static float timeForNote (int noteIndex, double beatsPerMinute) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* timeValue      = nullptr;
    std::atomic<float>* syncValue      = nullptr;
    std::atomic<float>* noteValue      = nullptr;
    std::atomic<float>* feedbackValue  = nullptr;
    std::atomic<float>* modeValue      = nullptr;
    std::atomic<float>* lowCutValue    = nullptr;
    std::atomic<float>* highCutValue   = nullptr;
    std::atomic<float>* characterValue = nullptr;
    std::atomic<float>* duckValue      = nullptr;
    std::atomic<float>* mixValue       = nullptr;
    std::atomic<float>* bypassValue    = nullptr;

    StereoDelay delay;
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> tempoFromDAW { false };
    std::atomic<float> inputPeak { 0.0f }, echoPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyDelayAudioProcessor)
};

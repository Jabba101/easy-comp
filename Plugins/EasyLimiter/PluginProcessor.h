#pragma once

#include "EasyProcessor.h"
#include "TruePeakLimiter.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Limiter as your DAW sees it: the clean, true-peak safety ceiling.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds GAIN, CEILING, RELEASE (AUTO or a time) and
    MATCH, tells your DAW about the 3 ms look-ahead, hands the settings to the
    TruePeakLimiter once per block, and notes each 5 ms moment (level in,
    level out, how much it turned down) for the window. The sound itself
    lives in Effects/TruePeakLimiter.h.
*/
class EasyLimiterAudioProcessor : public EasyProcessor
{
public:
    EasyLimiterAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;

    TruePeakLimiter::Settings getCurrentSettings() const noexcept;

    /** One 5 ms moment, for the window. */
    struct Moment
    {
        float inDb  = -100.0f;   // the loudest going in (after GAIN): may go past the ceiling
        float outDb = -100.0f;   // the loudest coming out (true peak, before MATCH)
        float reductionDb = 0.0f;
    };

    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** The highest true peak out since you last clicked the readout (dBTP). */
    float getHighestPeakDb() const noexcept   { return highestPeak.load(); }
    void resetHighestPeak() noexcept          { highestPeak.store (-100.0f); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* gainValue = nullptr;
    std::atomic<float>* ceilingValue = nullptr;
    std::atomic<float>* releaseModeValue = nullptr;
    std::atomic<float>* releaseValue = nullptr;
    std::atomic<float>* matchValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    TruePeakLimiter limiter;
    std::atomic<float> highestPeak { -100.0f };

    int momentLength = 240, momentFilled = 0;
    float momentIn = 0.0f, momentOut = -100.0f, momentReduction = 0.0f;
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyLimiterAudioProcessor)
};

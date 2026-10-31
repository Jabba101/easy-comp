#pragma once

#include "EasyProcessor.h"
#include "ModFilter.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Filter as your DAW sees it: the moving (modulated) filter.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds TYPE, CUTOFF, RESONANCE, DEPTH, SHAPE, RATE (or
    a note locked to the beat, with SYNC), WIDTH and MIX, reads the song's
    tempo and position, and hands them to the ModFilter once per block. The
    sound itself lives in Effects/ModFilter.h.
*/
class EasyFilterAudioProcessor : public EasyProcessor
{
public:
    EasyFilterAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** Mono or stereo tracks - and a mono track with a stereo output (so WIDTH works on it). */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    ModFilter::Settings getCurrentSettings() const noexcept;
    float getPosition() const noexcept            { return position.load(); }
    float getCutoffNow (int side) const noexcept  { return cutoffNow[(size_t) juce::jlimit (0, 1, side)].load(); }
    double getTempo() const noexcept              { return tempo.load(); }
    double getSampleRateForDisplay() const noexcept   { return rate.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* typeValue = nullptr;
    std::atomic<float>* cutoffValue = nullptr;
    std::atomic<float>* resonanceValue = nullptr;
    std::atomic<float>* depthValue = nullptr;
    std::atomic<float>* shapeValue = nullptr;
    std::atomic<float>* rateValue = nullptr;
    std::atomic<float>* syncValue = nullptr;
    std::atomic<float>* noteValue = nullptr;
    std::atomic<float>* widthValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    ModFilter filter;
    std::atomic<double> tempo { 120.0 }, rate { 48000.0 };
    std::atomic<float> position { 0.0f };
    std::array<std::atomic<float>, 2> cutoffNow { 1000.0f, 1000.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyFilterAudioProcessor)
};

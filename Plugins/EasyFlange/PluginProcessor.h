#pragma once

#include "EasyProcessor.h"
#include "Flanger.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Flange as your DAW sees it: flanger, chorus and vibrato.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds TYPE, RATE (or a note locked to the beat, with
    SYNC), DEPTH, FEEDBACK, WIDTH and MIX, reads the song's tempo and
    position, and hands them to the Flanger once per block. The sound itself
    lives in Effects/Flanger.h.
*/
class EasyFlangeAudioProcessor : public EasyProcessor
{
public:
    EasyFlangeAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** Mono or stereo tracks - and a mono track with a stereo output (so WIDTH works on it). */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    Flanger::Settings getCurrentSettings() const noexcept;
    float getPosition() const noexcept       { return position.load(); }
    double getTempo() const noexcept         { return tempo.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* typeValue = nullptr;
    std::atomic<float>* rateValue = nullptr;
    std::atomic<float>* syncValue = nullptr;
    std::atomic<float>* noteValue = nullptr;
    std::atomic<float>* depthValue = nullptr;
    std::atomic<float>* feedbackValue = nullptr;
    std::atomic<float>* widthValue = nullptr;
    std::atomic<float>* mixValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    Flanger flanger;
    std::atomic<double> tempo { 120.0 };
    std::atomic<float> position { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyFlangeAudioProcessor)
};

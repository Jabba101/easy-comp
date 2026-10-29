#pragma once

#include "EasyProcessor.h"
#include "Clipper.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Clip as your DAW sees it.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds the clipper's knobs, hands their values to the
    Clipper once per block of audio, and notes how loud each moment was
    pushed in and how loud it came out, so the window can draw it and show
    how much is being shaved off. The sound-shaping maths lives in
    Effects/Clipper.h.

    No AUTO-GAIN here: a clipper's whole job is to make things louder, and
    the CEILING must stay the loudest it can ever get.
*/
class EasyClipAudioProcessor : public EasyProcessor
{
public:
    EasyClipAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** One 5 ms moment of sound, for the "what it's doing" display. */
    struct Moment
    {
        float pushedDb = Clipper::silenceDb;   // loudest it would have been, pushed but not clipped
        float outDb    = Clipper::silenceDb;   // loudest it came out
    };

    /** Hands over the moments recorded since last time (oldest first).
        Returns how many were written. Safe to call from the window. */
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** The most it shaved off (dB) since the meter last asked. */
    float takeShavedForMeter() noexcept      { return meterShavedDb.exchange (0.0f); }

    /** All the settings right now, in the form the clipper uses. */
    Clipper::Settings getCurrentSettings() const noexcept;

    /** True while any ADVANCED setting isn't at its neutral value (the lamp by ADVANCED). */
    bool isAdvancedInUse() const noexcept;

    /** Puts the ADVANCED settings back to neutral (their own INIT button). */
    void resetAdvanced();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* pushValue        = nullptr;
    std::atomic<float>* ceilingValue     = nullptr;
    std::atomic<float>* softnessValue    = nullptr;
    std::atomic<float>* protectLowsValue = nullptr;
    std::atomic<float>* topValue         = nullptr;
    std::atomic<float>* bottomValue      = nullptr;
    std::atomic<float>* biasValue        = nullptr;
    std::atomic<float>* blendValue       = nullptr;
    std::atomic<float>* qualityValue     = nullptr;
    std::atomic<float>* bypassValue      = nullptr;

    Clipper clipper;
    std::vector<float> pushedLevels;
    std::atomic<float> meterShavedDb { 0.0f };

    // The display's moments: filled by the audio, emptied by the window,
    // without either ever waiting for the other.
    int momentLength = 240, momentFilled = 0;
    float momentPushed = 0.0f, momentOut = 0.0f;
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyClipAudioProcessor)
};

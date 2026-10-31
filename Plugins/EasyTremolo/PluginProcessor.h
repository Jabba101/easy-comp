#pragma once

#include "EasyProcessor.h"
#include "Tremolo.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Tremolo as your DAW sees it: the volume pulsing, or the sound moving
    side to side, in time with the song.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds RATE (or a note, locked to the song's beat, with
    SYNC), DEPTH, SHAPE and MODE, reads the song's tempo and position, and
    hands them to the Tremolo once per block. The sound itself lives in
    Effects/Tremolo.h.
*/
class EasyTremoloAudioProcessor : public EasyProcessor
{
public:
    EasyTremoloAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** Mono or stereo tracks - and a mono track with a stereo output (so AUTO-PAN works on it). */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    Tremolo::Settings getCurrentSettings() const noexcept;

    /** Where in its cycle the movement is (0..1), for the window. */
    float getPosition() const noexcept       { return position.load(); }

    double getTempo() const noexcept         { return tempo.load(); }
    bool isTempoFromDAW() const noexcept     { return tempoFromDAW.load(); }

    /** One cycle in Hz, for a note at a tempo. */
    static float rateForNote (int noteIndex, double beatsPerMinute) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* rateValue   = nullptr;
    std::atomic<float>* syncValue   = nullptr;
    std::atomic<float>* noteValue   = nullptr;
    std::atomic<float>* depthValue  = nullptr;
    std::atomic<float>* shapeValue  = nullptr;
    std::atomic<float>* modeValue   = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    Tremolo tremolo;
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> tempoFromDAW { false };
    std::atomic<float> position { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyTremoloAudioProcessor)
};

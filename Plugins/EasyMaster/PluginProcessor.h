#pragma once

#include "EasyProcessor.h"
#include "Compressor.h"
#include "TruePeakLimiter.h"
#include "LoudnessMeter.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Master as your DAW sees it: the mastering chain for the master bus.

    Stage 1: INPUT (trim) > COMPRESSOR (stereo, or MID and SIDE with their
    own settings) > LIMITER (the Easy Limiter engine: true-peak ceiling) >
    MATCH (the whole chain as loud as the original, for fair comparison) >
    LISTEN (stereo / mid / side / mono, for checking). Still to come, in
    their places in the chain: the resonance tamer, EQ, multiband, punch
    and width.

    BYPASS gives exactly the original, delayed by the same amount as the
    processed sound so nothing jumps in time. The meters read the master as
    it leaves the limiter (before MATCH and LISTEN, which are for your ears
    only): true peak, LUFS momentary / short-term / integrated, PLR,
    stereo correlation, and how much each part is turning down.
*/
class EasyMasterAudioProcessor : public EasyProcessor
{
public:
    EasyMasterAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    /** One 10 ms moment, for the window's history. */
    struct Moment
    {
        float inDb = -100.0f;        // the loudest going into the limiter (after GAIN): may pass the ceiling
        float outDb = -100.0f;       // the loudest coming out (true peak)
        float midCutDb = 0.0f;       // the compressor (MID, or both in STEREO)
        float sideCutDb = 0.0f;      // the compressor (SIDE)
        float limitCutDb = 0.0f;     // the limiter
    };
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    struct Readings
    {
        float momentary = -200.0f, shortTerm = -200.0f, integrated = -200.0f;   // LUFS
        float highestPeak = -100.0f;                                             // dBTP, since reset
        float correlation = 1.0f;                                                // -1 .. +1
        float midCut = 0.0f, sideCut = 0.0f, limitCut = 0.0f;                    // dB, right now
        float matchDb = 0.0f;                                                    // what MATCH is doing
    };
    Readings getReadings() const noexcept;
    void resetMeters() noexcept   { loudness.resetIntegrated(); highestPeak.store (-100.0f); }

    bool isMidSide() const noexcept   { return juce::roundToInt (compModeValue->load()) == MasterMode::midSide; }

    /** Every 200 ms: is the input clipping (above 0 dBFS)? For the INPUT warning. */
    float getInputPeakDb() const noexcept   { return inputPeak.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Compressor::Settings compressorSettings (bool side) const noexcept;
    TruePeakLimiter::Settings limiterSettings() const noexcept;
    void compress (juce::AudioBuffer<float>&, int numSamples) noexcept;

    std::atomic<float>* v (const char* id) { return parameters.getRawParameterValue (id); }
    std::atomic<float> *trimValue, *compOnValue, *compModeValue, *mixValue, *ignoreValue, *reactsValue, *characterValue,
                       *limitGainValue, *ceilingValue, *releaseModeValue, *releaseValue, *matchValue, *listenValue, *bypassValue;
    std::array<std::atomic<float>*, 5> midValues {}, sideValues {};

    double rate = 48000.0;
    Compressor midComp, sideComp;
    TruePeakLimiter limiter;
    LoudnessMeter loudness, originalLoudness;
    juce::AudioBuffer<float> mid, sides, original;
    juce::SmoothedValue<float> trimGain, matchGain, bypassFade;
    std::array<juce::SmoothedValue<float>, 4> listenFades;   // STEREO, MID, SIDE, MONO
    int playingMode = MasterMode::stereo;
    bool modeChanging = false;
    float modeFade = 1.0f;

    // BYPASS: the original, delayed like the processed sound
    std::array<std::vector<float>, 2> dryDelay;
    int dryPos = 0, latency = 0;

    // Correlation (over ~300 ms)
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0;
    std::atomic<float> correlation { 1.0f }, highestPeak { -100.0f }, inputPeak { -100.0f }, matchDb { 0.0f };
    std::atomic<float> nowMidCut { 0.0f }, nowSideCut { 0.0f }, nowLimitCut { 0.0f };
    float inputPeakHeld = 0.0f;
    int inputPeakCount = 0;

    int momentLength = 480, momentFilled = 0;
    Moment building;
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterAudioProcessor)
};

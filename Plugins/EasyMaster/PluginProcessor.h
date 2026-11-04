#pragma once

#include "EasyProcessor.h"
#include "Compressor.h"
#include "TruePeakLimiter.h"
#include "LoudnessMeter.h"
#include "ResonanceTamer.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Master as your DAW sees it: the mastering chain for the master bus.

    INPUT (trim) > RESONANCE TAMER (stereo, or MID and SIDE with their own
    settings: harsh, boxy, ringing spots turned down only while they stick
    out) > COMPRESSOR (stereo, or MID and SIDE with their
    own settings) > LIMITER (the Easy Limiter engine: true-peak ceiling) >
    MATCH (the whole chain as loud as the original, for fair comparison) >
    LISTEN (stereo / mid / side / mono, for checking). Still to come, in
    their places in the chain: EQ, multiband, punch and width.

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
        float tamerMidCut = 0.0f, tamerSideCut = 0.0f;                           // the tamer's deepest cut now (dB)
    };

    /** The resonance tamer's picture for its page: level and cut, a sixth of an octave at a time (part 0 = MID or both, 1 = SIDE).
        (Read straight from the audio thread's numbers: a frame may be half-updated, which only shows as a flicker.) */
    struct TamerPicture
    {
        std::array<std::array<float, ResonanceTamer::numBands>, 2> levelDb {}, cutDb {};
        std::array<float, ResonanceTamer::numBands> centreHz {};
    };
    TamerPicture getTamerPicture() const noexcept;
    bool isTamerMidSide() const noexcept   { return juce::roundToInt (tamerValues[1]->load()) == MasterMode::midSide; }
    Readings getReadings() const noexcept;
    void resetMeters() noexcept   { loudness.resetIntegrated(); highestPeak.store (-100.0f); }

    int shownPage = 0;   // (the window's page - kept while the window is closed; 0 = RESONANCE TAMER)

    bool isMidSide() const noexcept   { return juce::roundToInt (compModeValue->load()) == MasterMode::midSide; }

    /** Every 200 ms: is the input clipping (above 0 dBFS)? For the INPUT warning. */
    float getInputPeakDb() const noexcept   { return inputPeak.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Compressor::Settings compressorSettings (bool side) const noexcept;
    TruePeakLimiter::Settings limiterSettings() const noexcept;
    ResonanceTamer::Settings tamerSettings() const noexcept;
    void compress (juce::AudioBuffer<float>&, int numSamples) noexcept;

    std::atomic<float>* v (const char* id) { return parameters.getRawParameterValue (id); }
    std::atomic<float> *trimValue, *compOnValue, *compModeValue, *mixValue, *ignoreValue, *reactsValue, *characterValue,
                       *limitGainValue, *ceilingValue, *releaseModeValue, *releaseValue, *matchValue, *listenValue, *bypassValue;
    std::array<std::atomic<float>*, 5> midValues {}, sideValues {};
    std::array<std::atomic<float>*, 12> tamerValues {};
    std::array<std::atomic<float>, 2> nowTamerCut {};

    double rate = 48000.0;
    Compressor midComp, sideComp;
    TruePeakLimiter limiter;
    ResonanceTamer tamer;
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

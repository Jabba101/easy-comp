#pragma once

#include "EasyProcessor.h"
#include "Compressor.h"
#include "TruePeakLimiter.h"
#include "LoudnessMeter.h"
#include "ResonanceTamer.h"
#include "MasteringEQ.h"
#include "MultibandCompressor.h"
#include "MixPunch.h"
#include "StereoWidth.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Master as your DAW sees it: the mastering chain for the master bus.

    INPUT (trim) > RESONANCE TAMER (stereo, or MID and SIDE with their own
    settings: harsh, boxy, ringing spots turned down only while they stick
    out) > EQ (a full EQ for the middle and another for the sides, or one for
    both) > MULTIBAND (four bands, each compressed on its own; stereo, or mid
    and side with their own settings) > COMPRESSOR (stereo, or MID and SIDE with their
    own settings) > PUNCH (the Easy Trans engine: the hits' attack and tails,
    stereo or MID and SIDE) > WIDTH (the sides' level, and a mono low end) >
    LIMITER (the Easy Limiter engine: true-peak ceiling) > MATCH (the whole
    chain as loud as the original, for fair comparison) > LISTEN (stereo /
    mid / side / mono, for checking).

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
        float punchUpDb = 0.0f;      // PUNCH: the most it turned up (attacks, tails)
        float punchDownDb = 0.0f;    // PUNCH: the most it turned down
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
        float sideToMidDb = -100.0f;                                             // how loud the sides are next to the middle (~300 ms)
    };

    /** The master's last samples (every second one), for the WIDTH page's picture. Returns how many it gave. */
    int takeScope (float* left, float* right, int maxPoints) noexcept;
    static constexpr int scopeSize = 4096;

    /** The resonance tamer's picture for its page: level and cut, a sixth of an octave at a time (part 0 = MID or both, 1 = SIDE).
        (Read straight from the audio thread's numbers: a frame may be half-updated, which only shows as a flicker.) */
    struct TamerPicture
    {
        std::array<std::array<float, ResonanceTamer::numBands>, 2> levelDb {}, cutDb {};
        std::array<float, ResonanceTamer::numBands> centreHz {};
    };
    TamerPicture getTamerPicture() const noexcept;

    /** The multiband's picture since the window last asked: per band, the most it turned down and the loudest peak (part 0 = MID or both, 1 = SIDE). */
    struct MultibandPicture { std::array<std::array<float, 2>, 4> cutDb {}, peakDb {}; };
    MultibandPicture takeMultibandPicture() noexcept;
    bool isTamerMidSide() const noexcept   { return juce::roundToInt (tamerValues[1]->load()) == MasterMode::midSide; }
    Readings getReadings() const noexcept;
    void resetMeters() noexcept   { loudness.resetIntegrated(); highestPeak.store (-100.0f); }

    int shownPage = 0;
    std::array<juce::String, MasterModules::count> modulePresetNames;   // (which module preset each module's Pr loaded last - shown beside it)   // (the window's page - kept while the window is closed; 0 = RESONANCE TAMER)

    bool isMidSide() const noexcept   { return juce::roundToInt (compModeValue->load()) == MasterMode::midSide; }

    /** Every 200 ms: is the input clipping (above 0 dBFS)? For the INPUT warning. */
    float getInputPeakDb() const noexcept   { return inputPeak.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    Compressor::Settings compressorSettings (bool side) const noexcept;
    TruePeakLimiter::Settings limiterSettings() const noexcept;
    ResonanceTamer::Settings tamerSettings() const noexcept;
public:
    MasteringEQ::Settings eqSettings() const noexcept;   // (also for the EQ page's curve)
    MultibandCompressor::Settings multibandSettings() const noexcept;
    bool isMultibandMidSide() const noexcept   { return juce::roundToInt (mbModeValue->load()) == MasterMode::midSide; }
    bool isEQMidSide() const noexcept   { return juce::roundToInt (eqModeValue->load()) == MasterMode::midSide; }
    bool isPunchMidSide() const noexcept   { return juce::roundToInt (punchValues[1]->load()) == MasterMode::midSide; }
    StereoWidth::Settings widthSettings() const noexcept;
private:
    void compress (juce::AudioBuffer<float>&, int numSamples) noexcept;
    void punch (juce::AudioBuffer<float>&, int numSamples) noexcept;
    MixPunch::Settings punchSettings (int part) const noexcept;

    std::atomic<float>* v (const char* id) { return parameters.getRawParameterValue (id); }
    std::atomic<float> *trimValue, *compOnValue, *compModeValue, *mixValue, *ignoreValue, *reactsValue, *characterValue,
                       *limitGainValue, *ceilingValue, *releaseModeValue, *releaseValue, *matchValue, *listenValue, *bypassValue;
    std::array<std::atomic<float>*, 5> midValues {}, sideValues {};
    std::array<std::atomic<float>*, 12> tamerValues {};
    std::array<std::atomic<float>, 2> nowTamerCut {};
    std::atomic<float> *eqOnValue = nullptr, *eqModeValue = nullptr, *mbOnValue = nullptr, *mbModeValue = nullptr;
    std::array<std::array<std::array<std::atomic<float>*, 4>, 4>, 2> mbBandValues {};
    std::array<std::atomic<float>*, 4> mbBandOnValues {}, mbSoloValues {};
    std::array<std::atomic<float>*, 3> mbSplitValues {};
    std::array<std::array<std::atomic<float>, 2>, 4> mbCut {}, mbPeak {};
    std::array<std::array<std::atomic<float>*, 5>, 2> eqFreqValues {}, eqGainValues {};
    std::array<std::atomic<float>*, 2> eqLowCutValues {};
    std::array<std::atomic<float>*, 7> punchValues {};   // ON, MODE, MID PUNCH, MID SUSTAIN, SIDE PUNCH, SIDE SUSTAIN, MIX
    std::array<std::atomic<float>*, 3> widthValues {};   // ON, WIDTH, MONO BASS

    double rate = 48000.0;
    Compressor midComp, sideComp;
    TruePeakLimiter limiter;
    ResonanceTamer tamer;
    MasteringEQ eq;
    MultibandCompressor multiband;
    std::array<MixPunch, 2> shapers;   // PUNCH: 0 = both (STEREO) or the middle, 1 = the sides
    StereoWidth width;
    int punchPlayingMode = MasterMode::stereo;
    bool punchModeChanging = false;
    float punchFade = 1.0f;
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
    double sumLR = 0.0, sumLL = 0.0, sumRR = 0.0, sumMM = 0.0, sumSS = 0.0;
    std::atomic<float> sideToMid { -100.0f };
    juce::AbstractFifo scopeFifo { scopeSize };
    std::array<float, scopeSize> scopeL {}, scopeR {};
    bool scopeSkip = false;
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

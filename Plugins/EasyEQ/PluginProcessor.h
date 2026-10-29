#pragma once

#include "EasyProcessor.h"
#include "Equalizer.h"
#include "LinearPhaseEQ.h"
#include "Presets.h"

//==============================================================================
/**
    Easy EQ as your DAW sees it.

    The shared parts (BYPASS, AUTO-GAIN, presets, saving with your project)
    come from EasyProcessor. This adds the EQ's settings, runs the EQ
    (Effects/Equalizer.h) and, when MASTER is on, its linear-phase version
    (Effects/LinearPhaseEQ.h), and keeps a copy of the sound coming out for
    the window's spectrum analyser.

    MASTER changes the plugin's delay, so your DAW is told whenever it's
    switched (and only then). Switching crossfades between the two over 30 ms.
*/
class EasyEQAudioProcessor : public EasyProcessor,
                             private juce::AsyncUpdater
{
public:
    EasyEQAudioProcessor();
    ~EasyEQAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void reset() override;   // (clears the filters' memory: the DAW calls it when you jump in the song)
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** All the settings right now, in the form the EQ uses. */
    Equalizer::Settings getCurrentSettings() const noexcept;

    double getCurrentSampleRate() const noexcept   { return currentSampleRate; }

    /** LISTEN: hear only what band `band` covers while it's held (-1 = off). */
    void setListenBand (int band) noexcept         { listenBand.store (band); }
    int getListenBand() const noexcept             { return listenBand.load(); }

    /** How many dB AUTO-GAIN is adding (or taking away) right now. */
    float getAutoGainDb() const noexcept           { return autoGainDb.load(); }

    /** The analyser's feed: the sound coming out (left + right), the latest
        samples first. Copies up to `count` of the most recent into `destination`. */
    void copyRecentOutput (float* destination, int count) const noexcept;
    static constexpr int analyserLength = 4096;

    /** (Tests) True once MASTER's filter matches the latest settings. */
    bool isMasterUpToDate() const noexcept         { return linear.isUpToDate(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void handleAsyncUpdate() override;

    std::array<std::atomic<float>*, 4> freqValue {}, gainValue {}, targetValue {};
    std::atomic<float>* lowShelfValue  = nullptr;
    std::atomic<float>* highShelfValue = nullptr;
    std::atomic<float>* hpfValue       = nullptr;
    std::atomic<float>* hpfSlopeValue  = nullptr;
    std::atomic<float>* lpfValue       = nullptr;
    std::atomic<float>* lpfSlopeValue  = nullptr;
    std::atomic<float>* monoBassValue  = nullptr;
    std::atomic<float>* masterValue    = nullptr;
    std::atomic<float>* analyserValue  = nullptr;
    std::atomic<float>* autoGainValue  = nullptr;
    std::atomic<float>* bypassValue    = nullptr;

    Equalizer eq;
    LinearPhaseEQ linear;
    juce::AudioBuffer<float> original, linearCopy;

    // MASTER's crossfade, and the original delayed to match it
    juce::SmoothedValue<float> masterFade;
    std::vector<float> delayRing;
    int delayMask = 0, delayPos = 0, delayLength = 0;
    bool masterOn = false;

    std::atomic<int> listenBand { -1 };
    std::atomic<float> autoGainDb { 0.0f };
    double currentSampleRate = 48000.0;

    // The analyser's ring of recent output
    std::array<float, analyserLength> recent {};
    std::atomic<int> recentPos { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyEQAudioProcessor)
};

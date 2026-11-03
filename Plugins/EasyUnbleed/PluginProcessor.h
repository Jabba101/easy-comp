#pragma once

#include "EasyProcessor.h"
#include "SpectralUnbleed.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Unbleed as your DAW sees it: the stem on the main input, the other
    stems on the sidechain. The shared parts (BYPASS, presets, saving with
    your project) come from EasyProcessor. This adds AMOUNT, SENSITIVITY,
    SMOOTHNESS, FROM / TO and LISTEN, tells your DAW about the delay (one
    FFT), and copies the engine's per-band picture for the window. The
    sound itself lives in Effects/SpectralUnbleed.h.
*/
class EasyUnbleedAudioProcessor : public EasyProcessor
{
public:
    EasyUnbleedAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    SpectralUnbleed::Settings getCurrentSettings() const noexcept;

    /** For the window (safe to read while playing). */
    struct Picture
    {
        int numBands = 0;
        std::array<float, SpectralUnbleed::numBandsMax> centreHz {}, reductionDb {}, stemDb {}, othersDb {};
    };
    Picture getPicture() const noexcept;
    bool isSidechainMissing() const noexcept   { return sidechainMissing.load(); }
    bool isSidechainACopy() const noexcept     { return sidechainCopy.load(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* amountValue = nullptr;
    std::atomic<float>* sensitivityValue = nullptr;
    std::atomic<float>* smoothnessValue = nullptr;
    std::atomic<float>* fromValue = nullptr;
    std::atomic<float>* toValue = nullptr;
    std::atomic<float>* listenValue = nullptr;
    std::atomic<float>* bypassValue = nullptr;

    SpectralUnbleed engine;
    std::atomic<int> numBands { 0 };
    std::array<std::atomic<float>, SpectralUnbleed::numBandsMax> centres, reductions, stemLevels, otherLevels;
    std::atomic<bool> sidechainMissing { false }, sidechainCopy { false };
    int samplesSinceSidechain = 1 << 30, samplesSinceTrack = 1 << 30;
    double rate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyUnbleedAudioProcessor)
};

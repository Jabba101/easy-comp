#pragma once

#include "EasyProcessor.h"
#include "Gate.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Gate as your DAW sees it.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds the gate's knobs and its sidechain input, hands
    the knob values to the Gate once per block of audio, tells your DAW about
    the 2 ms look-ahead (so it can keep everything in time), and notes what
    happened in each moment, so the window can draw it. The sound-shaping
    maths lives in Effects/Gate.h.
*/
class EasyGateAudioProcessor : public EasyProcessor
{
public:
    EasyGateAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** One 5 ms moment, for the "what it's doing" display. */
    struct Moment
    {
        float keyDb  = Gate::silenceDb;   // the loudest the gate heard
        float outDb  = Gate::silenceDb;   // the loudest coming out
        float gainDb = 0.0f;              // how far it was turned down (0 = fully open)
        bool  open   = false;             // open (or holding) at some point
    };

    /** Hands over the moments recorded since last time (oldest first).
        Returns how many were written. Safe to call from the window. */
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** For the light: whether it was open at some point since the light last
        asked, and how far open it got (dB: 0 = fully open). */
    bool takeOpenForLight() noexcept         { return lightOpen.exchange (false); }
    float takeGainForLight() noexcept        { return lightGainDb.exchange (Gate::silenceDb); }

    /** True while OPENED BY is SIDECHAIN, your track is playing, but nothing
        has come into the sidechain for a while (so the gate can't open). */
    bool isSidechainMissing() const noexcept { return sidechainMissing.load(); }

    /** LISTEN: hear what the gate hears. It isn't saved with your project,
        and the window switches it off when it closes, so it can never be left
        on by accident. */
    void setListening (bool shouldListen) noexcept   { listening.store (shouldListen); }
    bool isListening() const noexcept                { return listening.load(); }

    /** True if OPENED BY is set to SIDECHAIN. */
    bool usesSidechain() const noexcept              { return openedByValue->load() > 0.5f; }

    /** All the settings right now, in the form the gate uses. */
    Gate::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushMoment (const Gate::Moment&) noexcept;

    std::atomic<float>* thresholdValue = nullptr;
    std::atomic<float>* rangeValue     = nullptr;
    std::atomic<float>* attackValue    = nullptr;
    std::atomic<float>* holdValue      = nullptr;
    std::atomic<float>* releaseValue   = nullptr;
    std::atomic<float>* lowCutValue    = nullptr;
    std::atomic<float>* highCutValue   = nullptr;
    std::atomic<float>* openedByValue  = nullptr;
    std::atomic<float>* bypassValue    = nullptr;

    Gate gate;
    std::atomic<bool>  listening { false }, lightOpen { false }, sidechainMissing { false };
    std::atomic<float> lightGainDb { Gate::silenceDb };

    // Noticing a sidechain with nothing coming in
    double currentSampleRate = 44100.0;
    int samplesSinceTrackSound = 1 << 30, samplesSinceSidechainSound = 1 << 30;

    // The display's moments: filled by the audio, emptied by the window,
    // without either ever waiting for the other.
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyGateAudioProcessor)
};

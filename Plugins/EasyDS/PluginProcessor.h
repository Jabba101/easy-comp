#pragma once

#include "EasyProcessor.h"
#include "DeEsser.h"
#include "HarshnessFinder.h"
#include "Presets.h"

//==============================================================================
/**
    Easy DS as your DAW sees it.

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds the de-esser's knobs, hands their values to the
    DeEsser once per block of audio, tells your DAW about the 2 ms look-ahead
    (so it can keep everything in time), notes what happened in each moment
    so the window can draw it, and runs FIND's listening while FIND is on.
    The sound-shaping maths lives in Effects/DeEsser.h, and FIND's in
    Effects/HarshnessFinder.h.
*/
class EasyDSAudioProcessor : public EasyProcessor
{
public:
    EasyDSAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** One 5 ms moment, for the "what it's doing" display (all in dB). */
    struct Moment
    {
        float soundDb = DeEsser::silenceDb;   // the loudest the whole sound got
        float bandDb  = DeEsser::silenceDb;   // the loudest the harsh band got
        float lineDb  = DeEsser::silenceDb;   // what the THRESHOLD line follows (see DeEsser::thresholdLineDb)
        float gainDb  = 0.0f;                 // how far it turned down (0 = not at all)
    };

    /** Hands over the moments recorded since last time (oldest first).
        Returns how many were written. Safe to call from the window. */
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** For the meter: the most it turned down since the meter last asked (dB, positive). */
    float takeReductionForMeter() noexcept   { return meterReductionDb.exchange (0.0f); }

    /** LISTEN: hear only what it takes out. It isn't saved with your project,
        and the window switches it off when it closes, so it can never be left
        on by accident. */
    void setListening (bool shouldListen) noexcept   { listening.store (shouldListen); }
    bool isListening() const noexcept                { return listening.load(); }

    /** FIND: while it's on, your track is listened to and a Frame is noted
        every 10 ms, for the window to collect (see HarshnessFinder). */
    void startFinding() noexcept;
    void stopFinding() noexcept                      { finding.store (false); }
    bool isFinding() const noexcept                  { return finding.load(); }
    int takeFrames (HarshnessFinder::Frame* destination, int maxFrames) noexcept;

    /** Whether FIND can measure this band at the current sample rate. */
    bool canFindBand (int band) const noexcept       { return finderListener.isUsable (band); }

    /** All the settings right now, in the form the de-esser uses. */
    DeEsser::Settings getCurrentSettings() const noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushMoment (const DeEsser::Moment&) noexcept;

    std::atomic<float>* fromValue      = nullptr;
    std::atomic<float>* toValue        = nullptr;
    std::atomic<float>* thresholdValue = nullptr;
    std::atomic<float>* rangeValue     = nullptr;
    std::atomic<float>* turnsDownValue = nullptr;
    std::atomic<float>* bypassValue    = nullptr;

    DeEsser deEsser;
    std::atomic<bool>  listening { false };
    std::atomic<float> meterReductionDb { 0.0f };

    // FIND
    HarshnessFinder::Listener finderListener;
    std::atomic<bool> finding { false }, finderNeedsReset { false };
    juce::AbstractFifo frameFifo { 1024 };
    std::array<HarshnessFinder::Frame, 1024> frames;

    // The display's moments: filled by the audio, emptied by the window,
    // without either ever waiting for the other.
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyDSAudioProcessor)
};

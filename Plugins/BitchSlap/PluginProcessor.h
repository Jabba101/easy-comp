#pragma once

#include "EasyProcessor.h"
#include "VerbEngine.h"
#include "Presets.h"

//==============================================================================
/**
    Easy Verb as your DAW sees it (called Bitch-Slap up to 1.22 - the gated
    burst is now one TYPE of it, alongside ROOM, CLUB, HALL and PLATE).

    The shared parts (BYPASS, presets, saving with your project) come from
    EasyProcessor. This adds TYPE, MIX, LENGTH and SYNC (BITCH-SLAP), DECAY,
    PRE-DELAY and TONE (the others), works out LENGTH from the song's tempo
    when SYNC is on, hands the settings to the VerbEngine once per block of
    audio, and notes how loud each moment was going in and how loud the
    reverb was, so the window can draw it. The sound itself lives in
    Effects/VerbEngine.h (and GatedReverb.h, NaturalReverb.h).
*/
class BitchSlapAudioProcessor : public EasyProcessor
{
public:
    BitchSlapAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using EasyProcessor::processBlock;   // (keeps JUCE's double-precision version visible - it's never used)
    juce::AudioProcessorEditor* createEditor() override;

    /** Mono or stereo tracks - and a mono track with a stereo output, which
        gets the full stereo burst. */
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    /** How long the reverb rings after the sound stops (so your DAW doesn't
        cut it off when it exports). */
    double getTailLengthSeconds() const override;

    /** One 5 ms moment of sound, for the "what it's doing" display. */
    struct Moment
    {
        float inDb  = -100.0f;   // the loudest your sound was going in
        float wetDb = -100.0f;   // the loudest the reverb was (as you hear it, after MIX)
    };

    /** Hands over the moments recorded since last time (oldest first).
        Returns how many were written. Safe to call from the window. */
    int takeMoments (Moment* destination, int maxMoments) noexcept;

    /** All the settings right now, in the form the reverb uses (with SYNC
        worked out). */
    VerbEngine::Settings getCurrentSettings() const noexcept;

    /** Your own presets stay where Bitch-Slap kept them. */
    juce::String getPresetFolderName() const override   { return "Bitch-Slap"; }

    /** The song's tempo, as your DAW last said (120 until it says). */
    double getTempo() const noexcept           { return tempo.load(); }
    bool isTempoFromDAW() const noexcept       { return tempoFromDAW.load(); }

    /** How long a note is (ms) at a tempo, kept to the lengths the reverb can do. */
    static float lengthForNote (int noteIndex, double beatsPerMinute) noexcept;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* mixValue    = nullptr;
    std::atomic<float>* lengthValue = nullptr;
    std::atomic<float>* syncValue   = nullptr;
    std::atomic<float>* noteValue   = nullptr;
    std::atomic<float>* bypassValue = nullptr;
    std::atomic<float>* typeValue = nullptr, *decayValue = nullptr, *preDelayValue = nullptr, *toneValue = nullptr;

    VerbEngine reverb;
    std::atomic<double> tempo { 120.0 };
    std::atomic<bool> tempoFromDAW { false };

    // The display's moments: filled by the audio, emptied by the window,
    // without either ever waiting for the other.
    int momentLength = 240, momentFilled = 0;
    float momentIn = 0.0f, momentWet = 0.0f;
    juce::AbstractFifo momentFifo { 1024 };
    std::array<Moment, 1024> moments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BitchSlapAudioProcessor)
};

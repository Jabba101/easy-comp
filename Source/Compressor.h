#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

//==============================================================================
/**
    The compressor itself: the part that changes the sound. No knobs, windows
    or DAW plumbing in here - just the maths.

    What happens to every single sample:

      1. DETECT   How loud is it right now? (the louder of left/right, in dB)
                  If IGNORE BELOW is on, the detector hears a copy with the
                  low end filtered out - the audio itself is never filtered.
      2. DECIDE   Compare that with Threshold and Ratio: how many dB *should*
                  it be turned down? This is called the "gain computer".
      3. SMOOTH   Move towards that amount, quickly or slowly, depending on
                  Attack (when it needs to turn down more) or Release (when
                  it's letting go again).
      4. APPLY    Turn the sound down by that amount, add Make-up gain, and
                  blend it with the untouched sound using Mix.

    Design choices (so you know what you're listening to):

      - Feed-forward and peak-sensing: it reacts to the actual peaks of the
        audio, which suits drums and their sharp hits.
      - Stereo-linked: both channels always get exactly the same gain change,
        so a loud hit on one side doesn't pull the stereo image around.
      - A fixed 6 dB "soft knee": compression fades in gradually, starting
        3 dB below the threshold, instead of switching on abruptly.
      - No look-ahead, so zero latency. That also means the Mix knob
        (parallel compression) lines up perfectly, with no phasing.
*/
class Compressor
{
public:
    static constexpr float silenceDb   = -120.0f;   // how "silence" is written in dB here
    static constexpr float lowCutOffHz = 20.0f;     // IGNORE BELOW at or under this = off

    /** The knob values. */
    struct Settings
    {
        float thresholdDb = -20.0f;  // -60 .. 0 dB
        float ratio       = 4.0f;    //   1 .. 20  (meaning 1:1 .. 20:1)
        float attackMs    = 10.0f;   // 0.1 .. 100 ms
        float releaseMs   = 100.0f;  //  10 .. 1000 ms
        float makeupDb    = 0.0f;    //   0 .. 24 dB
        float mix         = 1.0f;    //   0 (only the original) .. 1 (only the compressed)
        float lowCutHz    = 20.0f;   //  20 (off) .. 500 Hz - IGNORE BELOW
    };

    /** What happened during one block of audio, for the meter and the graph. */
    struct BlockResult
    {
        float maxReductionDb = 0.0f;        // most gain reduction (dB, positive)
        float maxLevelDb     = silenceDb;   // loudest level the detector heard (dB)
    };

    /** Call before audio starts, and whenever the sample rate changes. */
    void prepare (double newSampleRate, const Settings& initialSettings)
    {
        sampleRate = newSampleRate;

        // When a knob moves, glide to the new value over 50 ms instead of
        // jumping there. Jumps cause "zipper" noise, clicks and pops.
        constexpr double glideSeconds = 0.05;
        smoothedThreshold.reset (sampleRate, glideSeconds);
        smoothedRatio.reset (sampleRate, glideSeconds);
        smoothedMakeup.reset (sampleRate, glideSeconds);
        smoothedMix.reset (sampleRate, glideSeconds);

        setSettings (initialSettings, true);
        currentReductionDb = 0.0f;

        for (auto& filter : detectorFilters)
            filter.reset();
    }

    /** Hands over the latest knob values; call this once per block of audio.
        With jumpToNewValues = true they apply instantly instead of gliding. */
    void setSettings (const Settings& settings, bool jumpToNewValues = false)
    {
        const float makeupAsGain = juce::Decibels::decibelsToGain (settings.makeupDb);

        if (jumpToNewValues)
        {
            smoothedThreshold.setCurrentAndTargetValue (settings.thresholdDb);
            smoothedRatio.setCurrentAndTargetValue (settings.ratio);
            smoothedMakeup.setCurrentAndTargetValue (makeupAsGain);
            smoothedMix.setCurrentAndTargetValue (settings.mix);
        }
        else
        {
            smoothedThreshold.setTargetValue (settings.thresholdDb);
            smoothedRatio.setTargetValue (settings.ratio);
            smoothedMakeup.setTargetValue (makeupAsGain);
            smoothedMix.setTargetValue (settings.mix);
        }

        // Attack and release only change how fast step 3 moves, so turning
        // them can't click - they don't need a glide of their own.
        attackCoefficient  = timeToCoefficient (settings.attackMs);
        releaseCoefficient = timeToCoefficient (settings.releaseMs);

        // IGNORE BELOW: a 12 dB-per-octave low-cut on the detector only. Like
        // attack and release it never touches the audio directly, so turning
        // it can't click either.
        const bool wantLowCut = settings.lowCutHz > lowCutOffHz + 0.5f;

        if (wantLowCut)
        {
            // Butterworth shape (Q = 0.707): the standard, neutral filter curve.
            const double cutoff = std::min ((double) settings.lowCutHz, 0.45 * sampleRate);
            const double g  = std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
            const double k  = juce::MathConstants<double>::sqrt2;
            const double a1 = 1.0 / (1.0 + g * (g + k));

            lowCut = { (float) a1, (float) (g * a1), (float) (g * g * a1), (float) k };
        }
        else if (lowCutActive)
        {
            for (auto& filter : detectorFilters)
                filter.reset();   // start fresh next time it's switched on
        }

        lowCutActive = wantLowCut;
    }

    /** Compresses the audio in place. Reports the most gain reduction and the
        loudest detector level during this block, for the meter and graph. */
    BlockResult process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const int numDetected = std::min (numChannels, maxChannels);
        auto* const* channels = buffer.getArrayOfWritePointers();

        BlockResult result;
        float loudestPeak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. DETECT - the louder of the channels at this instant, in dB.
            float peak = 0.0f;

            for (int ch = 0; ch < numDetected; ++ch)
            {
                const float sample = channels[ch][i];
                const float heard  = lowCutActive ? detectorFilters[(size_t) ch].highPass (sample, lowCut)
                                                  : sample;
                peak = std::max (peak, std::abs (heard));
            }

            loudestPeak = std::max (loudestPeak, peak);
            const float levelDb = juce::Decibels::gainToDecibels (peak, silenceDb);

            // 2. DECIDE - how much should it be turned down right now?
            const float targetReductionDb = gainReductionFor (levelDb,
                                                              smoothedThreshold.getNextValue(),
                                                              smoothedRatio.getNextValue());

            // 3. SMOOTH - head towards that target at attack or release speed.
            const float coefficient = targetReductionDb > currentReductionDb ? attackCoefficient
                                                                              : releaseCoefficient;

            currentReductionDb = targetReductionDb + coefficient * (currentReductionDb - targetReductionDb);
            result.maxReductionDb = std::max (result.maxReductionDb, currentReductionDb);

            // 4. APPLY - "compressed" is the original turned down, plus make-up.
            //    Blending  original * (1 - mix) + compressed * mix  is the same
            //    as multiplying the original by one combined gain:
            const float compressedGain = juce::Decibels::decibelsToGain (-currentReductionDb)
                                           * smoothedMakeup.getNextValue();
            const float wet  = smoothedMix.getNextValue();
            const float gain = (1.0f - wet) + wet * compressedGain;

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
        }

        result.maxLevelDb = juce::Decibels::gainToDecibels (loudestPeak, silenceDb);
        return result;
    }

    /** The "gain computer": for a sound at levelDb, how many dB should it be
        turned down? This is Threshold and Ratio, with a soft knee. */
    static float gainReductionFor (float levelDb, float thresholdDb, float ratio) noexcept
    {
        const float overDb   = levelDb - thresholdDb;   // how far above the threshold (negative = below)
        const float slope    = 1.0f - 1.0f / ratio;     // 0 at 1:1 (no compression), 0.95 at 20:1
        const float halfKnee = kneeWidthDb * 0.5f;

        if (overDb <= -halfKnee)                        // clearly below: leave it alone
            return 0.0f;

        if (overDb >= halfKnee)                         // clearly above: the full ratio applies
            return slope * overDb;

        const float intoKnee = overDb + halfKnee;       // in the knee: ease in gradually
        return slope * intoKnee * intoKnee / (2.0f * kneeWidthDb);
    }

    /** The input -> output "curve" for a steady sound (so attack and release
        don't come into it): threshold, ratio and knee, plus make-up and mix.
        This is exactly what the plugin's graph draws. */
    static float staticOutputDb (float inputDb, const Settings& settings) noexcept
    {
        const float reductionDb    = gainReductionFor (inputDb, settings.thresholdDb, settings.ratio);
        const float compressedGain = juce::Decibels::decibelsToGain (settings.makeupDb - reductionDb);
        const float gain           = (1.0f - settings.mix) + settings.mix * compressedGain;

        return inputDb + juce::Decibels::gainToDecibels (gain, silenceDb);
    }

private:
    static constexpr float kneeWidthDb = 6.0f;
    static constexpr int   maxChannels = 8;

    /** Settings for the detector's low-cut filter. */
    struct LowCutCoefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;
    };

    /** A 12 dB-per-octave high-pass ("state variable") filter, one per channel.
        This design stays smooth and stable even while its knob is moving. */
    struct DetectorFilter
    {
        float s1 = 0.0f, s2 = 0.0f;

        float highPass (float x, const LowCutCoefficients& c) noexcept
        {
            const float v3 = x - s2;
            const float v1 = c.a1 * s1 + c.a2 * v3;
            const float v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return x - c.k * v1 - v2;
        }

        void reset() noexcept   { s1 = s2 = 0.0f; }
    };

    /** Turns a time in ms into the per-sample smoothing factor used in step 3.
        After that much time, the smoothing has covered about 63% of the way. */
    float timeToCoefficient (float milliseconds) const noexcept
    {
        const double samples = std::max (1.0, (double) milliseconds * 0.001 * sampleRate);
        return (float) std::exp (-1.0 / samples);
    }

    double sampleRate = 44100.0;

    juce::SmoothedValue<float> smoothedThreshold, smoothedRatio, smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedMakeup;

    float attackCoefficient  = 0.0f;
    float releaseCoefficient = 0.0f;
    float currentReductionDb = 0.0f;   // how many dB it's turning down right now (always >= 0)

    bool lowCutActive = false;
    LowCutCoefficients lowCut;
    std::array<DetectorFilter, (size_t) maxChannels> detectorFilters;
};

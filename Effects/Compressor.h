#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include "LoudnessMatcher.h"

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
      4. APPLY    Turn the sound down by that amount, add Make-up gain (or
                  AUTO-GAIN), blend it with the untouched sound using Mix, and
                  fade to the untouched sound when BYPASS is on.

    Everything in step 4 is one volume change per sample, applied equally to
    every channel. That's why the original sound is never filtered or delayed,
    and why BYPASS can fade between the two without any phasing.

    Design choices (so you know what you're listening to):

      - Feed-forward, and it reacts either to PEAKS (every sharp peak - suits
        drums and anything punchy) or to the AVERAGE level over about 15 ms
        (smoother and more natural on vocals, bass and whole mixes). The two
        are matched so a steady tone reads the same either way.
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
    static constexpr float silenceDb     = -120.0f;   // how "silence" is written in dB here
    static constexpr float lowCutOffHz   = 20.0f;     // IGNORE BELOW at or under this = off
    static constexpr float maxAutoGainDb = LoudnessMatcher::maxGainDb;

    /** The knob and button settings. */
    struct Settings
    {
        float thresholdDb = -20.0f;  // -60 .. 0 dB
        float ratio       = 4.0f;    //   1 .. 20  (meaning 1:1 .. 20:1)
        float attackMs    = 10.0f;   // 0.1 .. 100 ms
        float releaseMs   = 100.0f;  //  10 .. 1000 ms
        float makeupDb    = 0.0f;    //   0 .. 24 dB
        float mix         = 1.0f;    //   0 (only the original) .. 1 (only the compressed)
        float lowCutHz    = 20.0f;   //  20 (off) .. 500 Hz - IGNORE BELOW
        bool  reactToAverage = false; // REACTS TO: false = PEAKS, true = AVERAGE
        bool  autoGain    = false;   // AUTO-GAIN: match the volume coming out to the volume going in
        bool  bypassed    = false;   // BYPASS: let the original sound straight through
    };

    /** What happened during one block of audio, for the meter and the graph. */
    struct BlockResult
    {
        float maxReductionDb = 0.0f;        // most gain reduction actually applied (dB, positive)
        float maxLevelDb     = silenceDb;   // loudest level the detector heard (dB)
        float autoGainDb     = 0.0f;        // how much AUTO-GAIN would add right now (dB)
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
        smoothedAutoAmount.reset (sampleRate, glideSeconds);
        smoothedBypass.reset (sampleRate, 0.02);   // BYPASS fades over 20 ms

        autoGain.prepare (sampleRate, initialSettings.releaseMs);

        recentLoudnessByLevel.fill (0.0);
        levelOutlineDb       = silenceDb;
        outlineFallPerSample = (float) (150.0 / sampleRate);   // 150 dB per second
        levelHistoryKeep     = 1.0 - autoGain.getAveragingRate();

        averagingCoefficient = (float) (1.0 - std::exp (-1.0 / (0.015 * sampleRate)));   // AVERAGE: ~15 ms
        meanSquare = 0.0f;

        hasPreviousSettings = false;
        setSettings (initialSettings, true);
        currentReductionDb = 0.0f;

        for (auto& filter : detectorFilters)
            filter.reset();
    }

    /** Hands over the latest settings; call this once per block of audio.
        With jumpToNewValues = true they apply instantly instead of gliding. */
    void setSettings (const Settings& settings, bool jumpToNewValues = false)
    {
        const float makeupAsGain = juce::Decibels::decibelsToGain (settings.makeupDb);
        const float autoTarget   = settings.autoGain ? 1.0f : 0.0f;
        const float bypassTarget = settings.bypassed ? 1.0f : 0.0f;

        if (jumpToNewValues)
        {
            smoothedThreshold.setCurrentAndTargetValue (settings.thresholdDb);
            smoothedRatio.setCurrentAndTargetValue (settings.ratio);
            smoothedMakeup.setCurrentAndTargetValue (makeupAsGain);
            smoothedMix.setCurrentAndTargetValue (settings.mix);
            smoothedAutoAmount.setCurrentAndTargetValue (autoTarget);
            smoothedBypass.setCurrentAndTargetValue (bypassTarget);
        }
        else
        {
            smoothedThreshold.setTargetValue (settings.thresholdDb);
            smoothedRatio.setTargetValue (settings.ratio);
            smoothedMakeup.setTargetValue (makeupAsGain);
            smoothedMix.setTargetValue (settings.mix);
            smoothedAutoAmount.setTargetValue (autoTarget);
            smoothedBypass.setTargetValue (bypassTarget);
        }

        // Attack and release only change how fast step 3 moves, so turning
        // them can't click - they don't need a glide of their own.
        attackCoefficient  = timeToCoefficient (settings.attackMs);
        releaseCoefficient = timeToCoefficient (settings.releaseMs);
        autoGain.setReleaseTime (settings.releaseMs);

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
        reactToAverage = settings.reactToAverage;

        // If anything that shapes the compressed sound has moved, AUTO-GAIN
        // predicts the new loudness straight away from the curve (it can't
        // wait for the next drum hit to find out), then re-learns quickly by
        // listening.
        if (hasPreviousSettings && changesTheSound (settings, previousSettings))
            autoGain.soundChanged (predictedLoudnessChange (previousSettings, settings));

        previousSettings    = settings;
        hasPreviousSettings = true;
    }

    /** Makes AUTO-GAIN start again from 0 dB (used when a preset is loaded). */
    void restartAutoGain() noexcept   { autoGain.restart(); }

    /** Compresses the audio in place. Reports the most gain reduction and the
        loudest detector level during this block, for the meter and graph. */
    BlockResult process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const int numDetected = std::min (numChannels, maxChannels);
        auto* const* channels = buffer.getArrayOfWritePointers();

        BlockResult result;
        float loudestDetected = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. DETECT - the louder of the channels at this instant, in dB.
            //    (And, for AUTO-GAIN, how loud this instant sounds to our ears.)
            float  peak  = 0.0f;
            double heard = 0.0;

            for (int ch = 0; ch < numDetected; ++ch)
            {
                const float sample = channels[ch][i];
                const float forDetector = lowCutActive ? detectorFilters[(size_t) ch].highPass (sample, lowCut)
                                                       : sample;
                peak = std::max (peak, std::abs (forDetector));
                heard += autoGain.measure (ch, sample);
            }

            //    AVERAGE follows the level over ~15 ms instead of every peak.
            //    (x sqrt 2, so a steady tone reads the same as with PEAKS.)
            meanSquare += (peak * peak - meanSquare) * averagingCoefficient;
            const float detected = reactToAverage ? std::sqrt (2.0f * meanSquare) : peak;

            loudestDetected = std::max (loudestDetected, detected);
            const float levelDb = juce::Decibels::gainToDecibels (detected, silenceDb);

            //    (AUTO-GAIN also keeps a tally of how much loudness there has
            //    been recently at each level, following the outline of the hits.)
            levelOutlineDb = std::max (levelDb, levelOutlineDb - outlineFallPerSample);

            if (const int bin = (int) (levelOutlineDb - lowestTalliedDb); bin >= 0)
                recentLoudnessByLevel[(size_t) std::min (bin, levelBins - 1)] += heard;

            // 2. DECIDE - how much should it be turned down right now?
            const float targetReductionDb = gainReductionFor (levelDb,
                                                              smoothedThreshold.getNextValue(),
                                                              smoothedRatio.getNextValue());

            // 3. SMOOTH - head towards that target at attack or release speed.
            const float coefficient = targetReductionDb > currentReductionDb ? attackCoefficient
                                                                              : releaseCoefficient;

            currentReductionDb = targetReductionDb + coefficient * (currentReductionDb - targetReductionDb);

            // 4. APPLY
            const float reductionGain = juce::Decibels::decibelsToGain (-currentReductionDb);
            const float wet = smoothedMix.getNextValue();

            //    The compressed sound blended with the original by Mix
            //    (original * (1 - mix) + compressed * mix), without make-up...
            const float compressedOnly = (1.0f - wet) + wet * reductionGain;

            //    ...then either the MAKE-UP knob (added to the compressed part)...
            const float withMakeup = (1.0f - wet) + wet * reductionGain * smoothedMakeup.getNextValue();

            //    ...or AUTO-GAIN (whatever makes it exactly as loud as the original).
            const float withAutoGain = compressedOnly * autoGain.process (heard, compressedOnly);

            const float autoAmount = smoothedAutoAmount.getNextValue();   // 0 = MAKE-UP knob, 1 = AUTO-GAIN
            const float active     = withMakeup * (1.0f - autoAmount) + withAutoGain * autoAmount;

            //    BYPASS fades to a gain of exactly 1: your original sound.
            const float bypass = smoothedBypass.getNextValue();
            const float gain   = active * (1.0f - bypass) + bypass;

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;

            result.maxReductionDb = std::max (result.maxReductionDb, currentReductionDb * (1.0f - bypass));
        }

        // Older parts of the tally fade away over a few seconds.
        const double fade = std::pow (levelHistoryKeep, (double) numSamples);

        for (auto& loudness : recentLoudnessByLevel)
            loudness *= fade;

        result.maxLevelDb = juce::Decibels::gainToDecibels (loudestDetected, silenceDb);
        result.autoGainDb = autoGain.getGainDb();
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
        don't come into it): threshold, ratio and knee, plus make-up (or the
        current AUTO-GAIN amount) and mix. This is exactly what the graph draws. */
    static float staticOutputDb (float inputDb, const Settings& settings, float currentAutoGainDb = 0.0f) noexcept
    {
        const float reductionGain = juce::Decibels::decibelsToGain (-gainReductionFor (inputDb, settings.thresholdDb, settings.ratio));

        const float gain = settings.autoGain
            ? ((1.0f - settings.mix) + settings.mix * reductionGain) * juce::Decibels::decibelsToGain (currentAutoGainDb)
            : (1.0f - settings.mix) + settings.mix * reductionGain * juce::Decibels::decibelsToGain (settings.makeupDb);

        return inputDb + juce::Decibels::gainToDecibels (gain, silenceDb);
    }

private:
    static constexpr float kneeWidthDb = 6.0f;
    static constexpr int   maxChannels = LoudnessMatcher::maxChannels;

    /** How much louder (as a power ratio) the compressed sound would be with
        the "to" settings than with the "from" settings, judging by the curve
        and the recent tally of levels. Attack, release and IGNORE BELOW don't
        change the curve, so for those it says "no change" and AUTO-GAIN
        relies on listening to the next hits instead. */
    double predictedLoudnessChange (const Settings& from, const Settings& to) const noexcept
    {
        const auto compressedGain = [] (float levelDb, const Settings& s)
        {
            return (double) ((1.0f - s.mix) + s.mix * juce::Decibels::decibelsToGain (-gainReductionFor (levelDb, s.thresholdDb, s.ratio)));
        };

        double before = 0.0, after = 0.0;

        for (size_t bin = 0; bin < recentLoudnessByLevel.size(); ++bin)
        {
            const double loudness = recentLoudnessByLevel[bin];

            if (loudness <= 0.0)
                continue;

            const float levelDb = lowestTalliedDb + (float) bin + 0.5f;
            const double gainBefore = compressedGain (levelDb, from);
            const double gainAfter  = compressedGain (levelDb, to);
            before += loudness * gainBefore * gainBefore;
            after  += loudness * gainAfter  * gainAfter;
        }

        return before > 0.0 ? after / before : 1.0;
    }

    /** True if a setting that shapes the compressed sound has moved. */
    static bool changesTheSound (const Settings& a, const Settings& b) noexcept
    {
        const auto moved = [] (float x, float y) { return std::abs (x - y) > 0.001f; };

        return moved (a.thresholdDb, b.thresholdDb) || moved (a.ratio, b.ratio)
            || moved (a.attackMs, b.attackMs)       || moved (a.releaseMs, b.releaseMs)
            || moved (a.mix, b.mix)                 || moved (a.lowCutHz, b.lowCutHz)
            || a.reactToAverage != b.reactToAverage;
    }

    //==========================================================================
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

    //==========================================================================
    double sampleRate = 44100.0;

    juce::SmoothedValue<float> smoothedThreshold, smoothedRatio, smoothedMix, smoothedAutoAmount, smoothedBypass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedMakeup;

    float attackCoefficient  = 0.0f;
    float releaseCoefficient = 0.0f;
    float currentReductionDb = 0.0f;   // how many dB it's turning down right now (always >= 0)

    bool lowCutActive = false;
    LowCutCoefficients lowCut;
    std::array<DetectorFilter, (size_t) maxChannels> detectorFilters;

    bool  reactToAverage = false;
    float meanSquare = 0.0f, averagingCoefficient = 0.0f;   // for REACTS TO: AVERAGE

    LoudnessMatcher autoGain;

    // AUTO-GAIN's prediction: recent loudness, tallied by level in 1 dB steps
    static constexpr int   levelBins       = 96;        // from -86 dB...
    static constexpr float lowestTalliedDb = -86.0f;    // ...up to +10 dB
    std::array<double, (size_t) levelBins> recentLoudnessByLevel {};
    float  levelOutlineDb = silenceDb, outlineFallPerSample = 0.0f;
    double levelHistoryKeep = 1.0;

    Settings previousSettings;
    bool hasPreviousSettings = false;
};

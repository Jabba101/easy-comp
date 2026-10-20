#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include "FastMath.h"
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

    How it's organised, to use as little of your computer as possible: the
    steps above go through a short run of samples at a time (64), each step
    sweeping through all 64 before the next starts. Steps that carry on from
    the sample before (the filters and the smoothing) go one sample after
    another; the rest - mostly the decibel maths - is worked out four
    samples at once. The sound is exactly the same as doing every step for
    one sample at a time; it's just quicker to get there.

    And in digital silence (an empty stretch of track, or the DAW stopped),
    once the last sound has died away it doesn't work through the samples at
    all: it works out where everything will have got to by the end of the
    block in one go (see rest()), so a silent track costs next to nothing.

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
        tallyFade.setBase (1.0 - autoGain.getAveragingRate());

        averagingCoefficient = (float) (1.0 - std::exp (-1.0 / (0.015 * sampleRate)));   // AVERAGE: ~15 ms
        meanSquare = 0.0f;
        meanSquareFade.setBase (1.0 - (double) averagingCoefficient);

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
        // (Anything worked out from a knob is only worked out again when that knob has moved.)
        const bool fresh = ! hasPreviousSettings;
        const auto moved = [fresh] (float now, float before) { return fresh || now < before || now > before; };

        if (moved (settings.makeupDb, previousSettings.makeupDb))
            makeupAsGain = juce::Decibels::decibelsToGain (settings.makeupDb);

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
        if (moved (settings.attackMs, previousSettings.attackMs))
            attackCoefficient = timeToCoefficient (settings.attackMs);

        if (moved (settings.releaseMs, previousSettings.releaseMs))
        {
            releaseCoefficient = timeToCoefficient (settings.releaseMs);
            releaseFade.setBase ((double) releaseCoefficient);
        }

        autoGain.setReleaseTime (settings.releaseMs);

        // IGNORE BELOW: a 12 dB-per-octave low-cut on the detector only. Like
        // attack and release it never touches the audio directly, so turning
        // it can't click either.
        const bool wantLowCut = settings.lowCutHz > lowCutOffHz + 0.5f;

        if (wantLowCut && (! lowCutActive || moved (settings.lowCutHz, previousSettings.lowCutHz)))
        {
            // Butterworth shape (Q = 0.707): the standard, neutral filter curve.
            const double cutoff = std::min ((double) settings.lowCutHz, 0.45 * sampleRate);
            const double g  = std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
            const double k  = juce::MathConstants<double>::sqrt2;
            const double a1 = 1.0 / (1.0 + g * (g + k));

            lowCut = { (float) a1, (float) (g * a1), (float) (g * g * a1), (float) k };
        }
        else if (! wantLowCut && lowCutActive)
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
        const int numSamples = buffer.getNumSamples();

        BlockResult result;
        float loudestDetected = 0.0f;

        resting = restingAllowed && FastMath::isSilent (buffer) && canRest();

        if (resting)
        {
            rest (numSamples, result, loudestDetected);
        }
        else
        {
            for (int start = 0; start < numSamples; start += Work::size)
                processRun (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start,
                            std::min (Work::size, numSamples - start), result, loudestDetected);
        }

        // Older parts of the tally fade away over a few seconds.
        const double fade = tallyFade (numSamples);

        for (auto& loudness : recentLoudnessByLevel)
            loudness *= fade;

        result.maxLevelDb = FastMath::gainToDecibels (loudestDetected, silenceDb);
        result.autoGainDb = autoGain.getGainDb();
        return result;
    }

    /** Whether the last block was digital silence it could skip straight through. */
    bool isResting() const noexcept                     { return resting; }

    /** Skipping through silence is on unless this says otherwise (the tests
        use it to check that skipping gives the same sound as working through). */
    void allowResting (bool shouldAllow) noexcept       { restingAllowed = shouldAllow; }

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

    /** The same, for four samples at once. */
    static FastMath::Vec4 gainReductionFor (FastMath::Vec4 levelDb, FastMath::Vec4 thresholdDb, FastMath::Vec4 ratio) noexcept
    {
        using FastMath::Vec4;
        const auto one      = Vec4::broadcast (1.0f);
        const auto overDb   = levelDb - thresholdDb;
        const auto slope    = one - one / ratio;
        const auto halfKnee = Vec4::broadcast (kneeWidthDb * 0.5f);
        const auto intoKnee = overDb + halfKnee;
        const auto inKnee   = slope * intoKnee * intoKnee / Vec4::broadcast (2.0f * kneeWidthDb);

        return Vec4::select (overDb <= -halfKnee, Vec4::broadcast (0.0f),
                             Vec4::select (overDb >= halfKnee, slope * overDb, inKnee));
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

    //==========================================================================
    /** Room to work in, for one run of samples. */
    struct Work
    {
        static constexpr int size = 64;   // samples per run (a multiple of 4)

        float peak[size] {}, meanSquare[size] {}, detected[size] {}, levelDb[size] {}, target[size] {}, reduction[size] {},
              threshold[size] {}, ratio[size] {}, mix[size] {}, makeup[size] {}, autoAmount[size] {}, bypass[size] {},
              compressedOnly[size] {}, withMakeup[size] {}, autoGains[size] {}, gains[size] {}, shownReduction[size] {};
        double heard[size] {};
    };

    /** Steps 1 to 4 for numSamples (at most Work::size) samples from start. */
    void processRun (float* const* channels, int numChannels, int start, int numSamples,
                     BlockResult& result, float& loudestDetected) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numDetected = std::min (numChannels, maxChannels);
        const int numInFours  = FastMath::roundUpToFour (numSamples);

        // 1. DETECT - the louder of the channels at each instant. (And, for
        //    AUTO-GAIN, how loud each instant sounds to our ears.) One sample
        //    after another: the filters and AVERAGE carry on from the last one.
        //    (Mono and stereo, nearly always what's used, get their own versions.)
        if (numDetected == 2)       lowCutActive ? detect<2, true> (channels, start, numSamples, 2)
                                                 : detect<2, false> (channels, start, numSamples, 2);
        else if (numDetected == 1)  lowCutActive ? detect<1, true> (channels, start, numSamples, 1)
                                                 : detect<1, false> (channels, start, numSamples, 1);
        else                        lowCutActive ? detect<0, true> (channels, start, numSamples, numDetected)
                                                 : detect<0, false> (channels, start, numSamples, numDetected);

        //    The knobs at each sample (they only change while gliding to a new setting).
        const FastMath::KnobRun threshold  (smoothedThreshold,  w.threshold,  numSamples);
        const FastMath::KnobRun ratio      (smoothedRatio,      w.ratio,      numSamples);
        const FastMath::KnobRun mix        (smoothedMix,        w.mix,        numSamples);
        const FastMath::KnobRun makeup     (smoothedMakeup,     w.makeup,     numSamples);
        const FastMath::KnobRun autoAmount (smoothedAutoAmount, w.autoAmount, numSamples);   // 0 = MAKE-UP knob, 1 = AUTO-GAIN
        const FastMath::KnobRun bypass     (smoothedBypass,     w.bypass,     numSamples);

        // 2. DECIDE - how much should each sample be turned down? (Each sample
        //    on its own, so four at a time.) AVERAGE: x sqrt 2, so a steady
        //    tone reads the same as with PEAKS.
        const auto two = Vec4::broadcast (2.0f);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto detected = reactToAverage ? sqrt (two * Vec4::load (w.meanSquare + i)) : Vec4::load (w.peak + i);
            const auto levelDb  = FastMath::gainToDecibels (detected, silenceDb);
            detected.store (w.detected + i);
            levelDb.store (w.levelDb + i);
            gainReductionFor (levelDb, threshold.at (i), ratio.at (i)).store (w.target + i);
        }

        // 3. SMOOTH - head towards each target at attack or release speed, one
        //    sample after another. (AUTO-GAIN also keeps a tally of how much
        //    loudness there has been recently at each level, following the
        //    outline of the hits.) Worked on with copies, close at hand.
        {
            float outlineDb = levelOutlineDb, reductionDb = currentReductionDb, loudest = loudestDetected;
            const float outlineFall = outlineFallPerSample, attack = attackCoefficient, release = releaseCoefficient;

            // (The tally's current 1 dB step is kept close at hand while the
            // outline stays in it - usually for many samples in a row.)
            double* const tally = recentLoudnessByLevel.data();
            int step = -1;
            double stepTotal = 0.0;

            for (int i = 0; i < numSamples; ++i)
            {
                loudest = std::max (loudest, w.detected[i]);
                outlineDb = std::max (w.levelDb[i], outlineDb - outlineFall);

                if (const int bin = (int) (outlineDb - lowestTalliedDb); bin >= 0)
                {
                    const int thisStep = std::min (bin, levelBins - 1);

                    if (thisStep != step)
                    {
                        if (step >= 0)
                            tally[step] = stepTotal;

                        step = thisStep;
                        stepTotal = tally[step];
                    }

                    stepTotal += w.heard[i];
                }

                const float targetReductionDb = w.target[i];
                const float coefficient = targetReductionDb > reductionDb ? attack : release;

                reductionDb = targetReductionDb + coefficient * (reductionDb - targetReductionDb);
                w.reduction[i] = reductionDb;
            }

            if (step >= 0)
                tally[step] = stepTotal;

            levelOutlineDb = outlineDb;
            currentReductionDb = reductionDb;
            loudestDetected = loudest;
        }

        // 4. APPLY - four at a time again. The compressed sound blended with
        //    the original by Mix (original * (1 - mix) + compressed * mix),
        //    without make-up, and with the MAKE-UP knob (added to the
        //    compressed part)...
        const auto one = Vec4::broadcast (1.0f);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto reductionGain = FastMath::decibelsToGain (-Vec4::load (w.reduction + i), -100.0f);
            const auto wet = mix.at (i);
            ((one - wet) + wet * reductionGain).store (w.compressedOnly + i);
            ((one - wet) + wet * reductionGain * makeup.at (i)).store (w.withMakeup + i);
        }

        //    ...or with AUTO-GAIN instead (whatever makes it exactly as loud as the original)...
        autoGain.process (w.heard, w.compressedOnly, w.autoGains, numSamples);

        //    ...and BYPASS fades to a gain of exactly 1: your original sound.
        //    (Plus the most reduction, for the meter.)
        auto mostReduction = Vec4::broadcast (result.maxReductionDb);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto withAutoGain = Vec4::load (w.compressedOnly + i) * Vec4::load (w.autoGains + i);
            const auto howAuto      = autoAmount.at (i);
            const auto active       = Vec4::load (w.withMakeup + i) * (one - howAuto) + withAutoGain * howAuto;
            const auto bypassed     = bypass.at (i);
            const auto shown        = Vec4::load (w.reduction + i) * (one - bypassed);

            (active * (one - bypassed) + bypassed).store (w.gains + i);

            if (i + 4 <= numSamples)
                mostReduction = max (mostReduction, shown);
            else
                shown.store (w.shownReduction + i);   // (the last few: only some of the four are real)
        }

        result.maxReductionDb = FastMath::largestOf (mostReduction);

        for (int i = numSamples & ~3; i < numSamples; ++i)
            result.maxReductionDb = std::max (result.maxReductionDb, w.shownReduction[i]);

        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply (channels[ch] + start, w.gains, numSamples);
    }

    /** Step 1 for a run of samples. fixedChannels is 1 or 2 for mono or stereo
        (worked out specially), or 0 for any other number of channels. */
    template <int fixedChannels, bool withLowCut>
    void detect (float* const* channels, int start, int numSamples, int numDetected) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numDetected;

        // Copies of everything that carries on from sample to sample, close at hand
        DetectorFilter lowCutFilters[capacity];

        for (int ch = 0; ch < count; ++ch)
            lowCutFilters[ch] = detectorFilters[(size_t) ch];

        const auto coefficients = lowCut;
        const float averaging = averagingCoefficient;
        float squared = meanSquare;
        auto& w = work;

        // The louder of the channels, as the detector hears it
        const auto peakAt = [&] (int i) noexcept
        {
            float peak = 0.0f;

            for (int ch = 0; ch < count; ++ch)
            {
                const float sample = channels[ch][start + i];
                const float forDetector = withLowCut ? lowCutFilters[ch].highPass (sample, coefficients) : sample;
                peak = std::max (peak, std::abs (forDetector));
            }

            return peak;
        };

        if constexpr (fixedChannels == 2)
        {
            // Stereo: both ear filters at once
            LoudnessMatcher::EarFilterPair ears (autoGain.earFilter (0), autoGain.earFilter (1));

            for (int i = 0; i < numSamples; ++i)
            {
                const float peak = peakAt (i);
                w.heard[i] = ears.measure (channels[0][start + i], channels[1][start + i]);

                //    AVERAGE follows the level over ~15 ms instead of every peak.
                squared += (peak * peak - squared) * averaging;
                w.peak[i] = peak;
                w.meanSquare[i] = squared;
            }

            ears.storeInto (autoGain.earFilter (0), autoGain.earFilter (1));
        }
        else
        {
            LoudnessMatcher::EarFilter ears[capacity];

            for (int ch = 0; ch < count; ++ch)
                ears[ch] = autoGain.earFilter (ch);

            for (int i = 0; i < numSamples; ++i)
            {
                const float peak = peakAt (i);
                double heard = 0.0;

                for (int ch = 0; ch < count; ++ch)
                    heard += ears[ch].measure (channels[ch][start + i]);

                w.heard[i] = heard;

                //    AVERAGE follows the level over ~15 ms instead of every peak.
                squared += (peak * peak - squared) * averaging;
                w.peak[i] = peak;
                w.meanSquare[i] = squared;
            }

            for (int ch = 0; ch < count; ++ch)
                autoGain.earFilter (ch) = ears[ch];
        }

        for (int ch = 0; ch < count; ++ch)
            detectorFilters[(size_t) ch] = lowCutFilters[ch];

        meanSquare = squared;
    }

    //==========================================================================
    /** True if the block about to come (digital silence) can be skipped
        through with rest(), ending up exactly where working through it would:
        no knob is gliding, nothing is still ringing from the last sound, and
        nothing will be over the threshold. */
    bool canRest() const noexcept
    {
        if (smoothedThreshold.isSmoothing() || smoothedRatio.isSmoothing() || smoothedMix.isSmoothing()
             || smoothedMakeup.isSmoothing() || smoothedAutoAmount.isSmoothing() || smoothedBypass.isSmoothing())
            return false;

        if (! autoGain.canRest())
            return false;

        if (lowCutActive)
            for (auto& filter : detectorFilters)
                if (! filter.hasFallenSilent())
                    return false;

        // The detector only gets quieter from here (for AVERAGE, as it fades out),
        // so if even the first sample is below the knee, they all are.
        const float firstDetected = reactToAverage ? std::sqrt (2.0f * (meanSquare + (0.0f - meanSquare) * averagingCoefficient))
                                                   : 0.0f;
        const float overDb = FastMath::gainToDecibels (firstDetected, silenceDb) - smoothedThreshold.getTargetValue();
        return overDb <= -kneeWidthDb * 0.5f;
    }

    /** Moves on by numSamples of digital silence, just as working through them would. */
    void rest (int numSamples, BlockResult& result, float& loudestDetected) noexcept
    {
        // (What IGNORE BELOW's filters had left was under a billionth.)
        for (auto& filter : detectorFilters)
            filter.reset();

        // Nothing is over the threshold, so the compressor lets go at the
        // RELEASE speed (the meter shows the first, biggest, of those)...
        const float first = releaseCoefficient * currentReductionDb;
        result.maxReductionDb = std::max (result.maxReductionDb, first * (1.0f - smoothedBypass.getTargetValue()));
        currentReductionDb = (float) ((double) currentReductionDb * releaseFade (numSamples));

        // ...AVERAGE fades out (its loudest is the first sample)...
        if (reactToAverage)
            loudestDetected = std::sqrt (2.0f * (meanSquare + (0.0f - meanSquare) * averagingCoefficient));

        meanSquare = (float) ((double) meanSquare * meanSquareFade (numSamples));

        // ...the outline AUTO-GAIN's tally follows falls at its steady pace
        // (and there's no loudness to add to the tally)...
        levelOutlineDb = std::max (silenceDb, levelOutlineDb - (float) numSamples * outlineFallPerSample);

        // ...and AUTO-GAIN skips ahead too.
        autoGain.rest (numSamples);
    }

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

        /** True once it's stopped ringing (to under a billionth: -180 dB). */
        bool hasFallenSilent() const noexcept   { return std::abs (s1) < 1.0e-9f && std::abs (s2) < 1.0e-9f; }
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
    float makeupAsGain       = 1.0f;
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
    FastMath::RepeatedPower tallyFade;

    Settings previousSettings;
    bool hasPreviousSettings = false;

    Work work;
    bool resting = false, restingAllowed = true;
    FastMath::RepeatedPower releaseFade, meanSquareFade;   // (for rest)
};

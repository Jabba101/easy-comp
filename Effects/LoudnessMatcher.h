#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>

//==============================================================================
/**
    AUTO-GAIN: keeps the volume coming out the same as the volume going in.

    The effects here all change the sound by turning it up and down, sample
    by sample (a "gain"). This listens to the original, works out how loud the
    processed version would be, and says how much to turn it up so the two
    match - measured the way our ears judge loudness (the K-weighting from
    the broadcast loudness standard, ITU-R BS.1770).

    It deliberately listens over a few seconds: a quick-reacting version
    would undo the effect itself (e.g. turn every squashed hit back up).

    Once per sample:
        1. add up measure (channel, sample) for every channel of the ORIGINAL sound
        2. work out the effect's own gain for this sample
        3. multiply that gain by process (the total from step 1, the effect's gain)

    When a knob moves, call soundChanged() - ideally with a prediction of how
    much louder or quieter the effect will now be - so it re-learns quickly.
*/
class LoudnessMatcher
{
public:
    static constexpr float maxGainDb   = 24.0f;   // the most it will add (same as a MAKE-UP knob)
    static constexpr int   maxChannels = 8;

    /** releaseMs: how long the effect takes to let go after a change (for the
        compressor, its RELEASE knob). Re-learning skips that long, so the
        tail end of the old setting doesn't throw the measurement off. */
    void prepare (double newSampleRate, float releaseMs)
    {
        sampleRate = newSampleRate;

        loudnessRate     = rateFor (3.0);    // normally compares the last few seconds
        fastLoudnessRate = rateFor (0.2);    // just after a change: re-learns quickly
        autoRiseRate     = rateFor (0.3);    // turns up gently...
        relearnRiseRate  = rateFor (1.0);    // (even more gently while re-learning)
        autoFallRate     = rateFor (0.05);   // ...but turns down quickly, so it's never too loud for long
        relearnAverageLength = (int) (1.0 * sampleRate);
        currentReleaseMs     = releaseMs;

        setUpFilters();
        restart();
    }

    void setReleaseTime (float releaseMs) noexcept   { currentReleaseMs = releaseMs; }

    /** Start again from 0 dB and re-learn quickly. Used when a preset is
        loaded: a big change all at once, so rather than risk a second of the
        old (possibly much louder) setting, it starts quiet. */
    void restart() noexcept
    {
        loudnessIn = loudnessOut = 0.0;
        autoTargetDb = autoGainDb = 0.0;
        startRelearning();
    }

    /** A knob moved. predictedLoudnessChange: how much louder (as a power
        ratio) the effect is expected to be now; 1.0 if there's no way to tell. */
    void soundChanged (double predictedLoudnessChange = 1.0) noexcept
    {
        loudnessOut *= predictedLoudnessChange;
        startRelearning();
    }

    /** How loud one channel of the original sound is at this instant, as our ears weigh it. */
    double measure (int channel, float sample) noexcept
    {
        const double asHeard = filters[(size_t) channel].process ((double) sample);
        return asHeard * asHeard;
    }

    /** Once per sample: the total of measure() for this sample, and the
        effect's own gain. Returns the extra gain that matches the loudness. */
    float process (double heard, float effectGain) noexcept
    {
        const double heardAfter = heard * (double) effectGain * (double) effectGain;
        const bool relearning = relearnSamplesLeft > 0;
        const double rate = relearning ? fastLoudnessRate : loudnessRate;

        loudnessIn  += (heard      - loudnessIn)  * rate;
        loudnessOut += (heardAfter - loudnessOut) * rate;

        if (relearning)
        {
            // While re-learning, also keep plain totals over its last second
            // (the start is skipped: the effect is still letting go of the
            // old settings then, which would throw the average off)...
            if (relearnSamplesLeft <= relearnAverageLength)
            {
                relearnTotalIn  += heard;
                relearnTotalOut += heardAfter;
            }

            // ...and when re-learning ends, carry on from the average of that
            // whole second, so the last hit or two doesn't sway it.
            if (--relearnSamplesLeft == 0 && relearnTotalIn / (double) relearnAverageLength > quietestLoudness)
            {
                loudnessIn  = relearnTotalIn  / (double) relearnAverageLength;
                loudnessOut = relearnTotalOut / (double) relearnAverageLength;
            }
        }

        // In near-silence there's nothing to measure, so it keeps its last value.
        if (loudnessIn > quietestLoudness && loudnessOut > 0.0)
            autoTargetDb = juce::jlimit (0.0, (double) maxGainDb, 10.0 * std::log10 (loudnessIn / loudnessOut));

        const double riseRate = relearning ? relearnRiseRate : autoRiseRate;
        autoGainDb += (autoTargetDb - autoGainDb) * (autoTargetDb < autoGainDb ? autoFallRate : riseRate);

        return (float) std::pow (10.0, autoGainDb / 20.0);
    }

    /** How many dB it's adding right now. */
    float getGainDb() const noexcept               { return (float) autoGainDb; }

    /** The rate its slow averages move at (for anything that should fade at the same pace). */
    double getAveragingRate() const noexcept       { return loudnessRate; }

private:
    static constexpr double quietestLoudness = 1.0e-7;   // about -70 dB: quieter than this, it holds still

    void startRelearning() noexcept
    {
        // Skip three release times (at least 0.25 s, at most 3 s), then average one second.
        const double skipSeconds = juce::jlimit (0.25, 3.0, 3.0 * (double) currentReleaseMs * 0.001);
        relearnSamplesLeft = (int) (skipSeconds * sampleRate) + relearnAverageLength;
        relearnTotalIn = relearnTotalOut = 0.0;
    }

    /** A one-pole smoothing rate that covers ~63% of a change in the given time. */
    double rateFor (double seconds) const noexcept
    {
        return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
    }

    //==========================================================================
    /** The "K-weighting" filter from the broadcast loudness standard
        (ITU-R BS.1770): it shapes the sound roughly the way our ears weigh
        loudness - less deep bass, a little more top end. It's only used for
        measuring; the audio itself never goes through it. */
    struct Filter
    {
        struct Stage
        {
            double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
            double z1 = 0.0, z2 = 0.0;

            double process (double x) noexcept
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return y;
            }
        };

        Stage shelf, highPass;

        double process (double x) noexcept   { return highPass.process (shelf.process (x)); }
    };

    void setUpFilters()
    {
        const double pi = juce::MathConstants<double>::pi;

        // Stage 1: a gentle +4 dB lift above ~1.7 kHz (the effect of the head)
        Filter::Stage shelf;
        {
            const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const double k  = std::tan (pi * f0 / sampleRate);
            const double vh = std::pow (10.0, gainDb / 20.0);
            const double vb = std::pow (vh, 0.4996667741545416);
            const double a0 = 1.0 + k / q + k * k;

            shelf.b0 = (vh + vb * k / q + k * k) / a0;
            shelf.b1 = 2.0 * (k * k - vh) / a0;
            shelf.b2 = (vh - vb * k / q + k * k) / a0;
            shelf.a1 = 2.0 * (k * k - 1.0) / a0;
            shelf.a2 = (1.0 - k / q + k * k) / a0;
        }

        // Stage 2: rolls off the deepest bass (below ~38 Hz), which we barely hear as loudness
        Filter::Stage highPass;
        {
            const double f0 = 38.13547087602444, q = 0.5003270373238773;
            const double k  = std::tan (pi * f0 / sampleRate);
            const double a0 = 1.0 + k / q + k * k;

            highPass.b0 = 1.0;
            highPass.b1 = -2.0;
            highPass.b2 = 1.0;
            highPass.a1 = 2.0 * (k * k - 1.0) / a0;
            highPass.a2 = (1.0 - k / q + k * k) / a0;
        }

        for (auto& filter : filters)
        {
            filter.shelf    = shelf;
            filter.highPass = highPass;
        }
    }

    //==========================================================================
    double sampleRate = 44100.0;

    std::array<Filter, (size_t) maxChannels> filters;
    double loudnessIn = 0.0, loudnessOut = 0.0;     // running averages (squared, K-weighted)
    double autoTargetDb = 0.0, autoGainDb = 0.0;    // where it's heading, and where it is
    double loudnessRate = 0.0, fastLoudnessRate = 0.0, autoRiseRate = 0.0, relearnRiseRate = 0.0, autoFallRate = 0.0;
    int    relearnAverageLength = 0, relearnSamplesLeft = 0;
    double relearnTotalIn = 0.0, relearnTotalOut = 0.0;
    float  currentReleaseMs = 100.0f;
};

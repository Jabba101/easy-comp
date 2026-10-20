#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include "FastMath.h"

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

    For each run of samples:
        1. for every sample, add up measure (channel, sample) over the channels
           of the ORIGINAL sound (or use an EarFilterPair for stereo)
        2. work out the effect's own gain for every sample
        3. process() turns those into the extra gain for every sample, which
           the effect multiplies its own gain by

    When a knob moves, call soundChanged() - ideally with a prediction of how
    much louder or quieter the effect will now be - so it re-learns quickly.

    In digital silence (an empty stretch of track, or the DAW stopped) there's
    nothing to listen to, and once the last sound has died away it can skip
    ahead with rest() instead, which takes almost no time at all.
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

        rates.loudness     = rateFor (3.0);    // normally compares the last few seconds
        rates.fastLoudness = rateFor (0.2);    // just after a change: re-learns quickly
        rates.autoRise     = rateFor (0.3);    // turns up gently...
        rates.relearnRise  = rateFor (1.0);    // (even more gently while re-learning)
        rates.autoFall     = rateFor (0.05);   // ...but turns down quickly, so it's never too loud for long
        rates.relearnAverageLength = (int) (1.0 * sampleRate);
        currentReleaseMs = releaseMs;

        loudnessFade.setBase (1.0 - rates.loudness);
        riseFade.setBase (1.0 - rates.autoRise);
        fallFade.setBase (1.0 - rates.autoFall);

        setUpFilters();
        restart();
    }

    void setReleaseTime (float releaseMs) noexcept   { currentReleaseMs = releaseMs; }

    /** Whether it may also turn the sound DOWN (for effects that can make
        things louder, like a transient shaper). A compressor only ever makes
        things quieter, so by default it only turns up. */
    void allowTurningDown (bool shouldAllow) noexcept   { rates.lowestGainDb = shouldAllow ? -(double) maxGainDb : 0.0; }

    /** Start again from 0 dB and re-learn quickly. Used when a preset is
        loaded: a big change all at once, so rather than risk a second of the
        old (possibly much louder) setting, it starts quiet. */
    void restart() noexcept
    {
        tracking.loudnessIn = tracking.loudnessOut = 0.0;
        tracking.autoTargetDb = tracking.autoGainDb = 0.0;
        startRelearning();
    }

    /** A knob moved. predictedLoudnessChange: how much louder (as a power
        ratio) the effect is expected to be now; 1.0 if there's no way to tell. */
    void soundChanged (double predictedLoudnessChange = 1.0) noexcept
    {
        tracking.loudnessOut *= predictedLoudnessChange;
        startRelearning();
    }

    //==========================================================================
    /** The "K-weighting" filter from the broadcast loudness standard
        (ITU-R BS.1770), one per channel: it shapes the sound roughly the way
        our ears weigh loudness - less deep bass, a little more top end. It's
        only used for measuring; the audio itself never goes through it. */
    class EarFilter
    {
    public:
        /** How loud this channel of the original sound is at this instant, as our ears weigh it. */
        double measure (float sample) noexcept
        {
            const double asHeard = highPass.process (shelf.process ((double) sample));
            return asHeard * asHeard;
        }

        /** True once it's stopped ringing from the last sound (to under a
            millionth of a millionth: -240 dB). */
        bool hasFallenSilent() const noexcept   { return shelf.hasFallenSilent() && highPass.hasFallenSilent(); }

        void clear() noexcept                   { shelf.z1 = shelf.z2 = highPass.z1 = highPass.z2 = 0.0; }

        //======================================================================
        // (The filter's insides: two "biquad" stages, one after the other.)
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

            bool hasFallenSilent() const noexcept   { return std::abs (z1) < 1.0e-12 && std::abs (z2) < 1.0e-12; }
        };

        Stage shelf, highPass;
    };

    /** Two ear filters, left and right, worked on side by side: on Intel and
        AMD processors both at once, which is quicker. measure (l, r) gives
        exactly what left.measure (l) + right.measure (r) would. Take one
        from the matcher's filters for a run of samples, then put it back
        with storeInto(). */
    class EarFilterPair
    {
    public:
        EarFilterPair (const EarFilter& left, const EarFilter& right) noexcept
        {
           #if EASY_PLUGINS_SSE2
            shelf.load (left.shelf, right.shelf);
            highPass.load (left.highPass, right.highPass);
           #else
            l = left;
            r = right;
           #endif
        }

        void storeInto (EarFilter& left, EarFilter& right) const noexcept
        {
           #if EASY_PLUGINS_SSE2
            shelf.store (left.shelf, right.shelf);
            highPass.store (left.highPass, right.highPass);
           #else
            left = l;
            right = r;
           #endif
        }

        double measure (float left, float right) noexcept
        {
           #if EASY_PLUGINS_SSE2
            const __m128d x = _mm_cvtps_pd (_mm_unpacklo_ps (_mm_set_ss (left), _mm_set_ss (right)));
            const __m128d y = highPass.process (shelf.process (x));
            const __m128d squared = _mm_mul_pd (y, y);
            return _mm_cvtsd_f64 (squared) + _mm_cvtsd_f64 (_mm_unpackhi_pd (squared, squared));
           #else
            double heard = 0.0;
            heard += l.measure (left);
            heard += r.measure (right);
            return heard;
           #endif
        }

    private:
       #if EASY_PLUGINS_SSE2
        struct Stage
        {
            __m128d b0, b1, b2, a1, a2, z1, z2;   // (left in the first half of each, right in the second)

            __m128d process (__m128d x) noexcept
            {
                const __m128d y = _mm_add_pd (_mm_mul_pd (b0, x), z1);
                z1 = _mm_add_pd (_mm_sub_pd (_mm_mul_pd (b1, x), _mm_mul_pd (a1, y)), z2);
                z2 = _mm_sub_pd (_mm_mul_pd (b2, x), _mm_mul_pd (a2, y));
                return y;
            }

            void load (const EarFilter::Stage& left, const EarFilter::Stage& right) noexcept
            {
                b0 = _mm_set_pd (right.b0, left.b0);  b1 = _mm_set_pd (right.b1, left.b1);
                b2 = _mm_set_pd (right.b2, left.b2);  a1 = _mm_set_pd (right.a1, left.a1);
                a2 = _mm_set_pd (right.a2, left.a2);
                z1 = _mm_set_pd (right.z1, left.z1);  z2 = _mm_set_pd (right.z2, left.z2);
            }

            void store (EarFilter::Stage& left, EarFilter::Stage& right) const noexcept
            {
                _mm_storel_pd (&left.z1, z1);  _mm_storeh_pd (&right.z1, z1);
                _mm_storel_pd (&left.z2, z2);  _mm_storeh_pd (&right.z2, z2);
            }
        };

        Stage shelf, highPass;
       #else
        EarFilter l, r;
       #endif
    };

    /** The ear filter for one channel. (The effects take a copy of the ones
        they need for a run of samples, measure with the copies and put them
        back afterwards: having them close at hand is quicker.) */
    EarFilter& earFilter (int channel) noexcept     { return filters[(size_t) channel]; }

    /** How loud one channel of the original sound is at this instant, as our ears weigh it. */
    double measure (int channel, float sample) noexcept   { return filters[(size_t) channel].measure (sample); }

    /** For a run of samples: heard[i] is the total of measure() over the
        channels of the original sound at sample i, and effectGains[i] the
        effect's own gain there. Writes the extra gain that matches the
        loudness into gainsOut[i] (which needs room for roundUpToFour
        (numSamples) values: it's worked out four at a time). */
    void process (const double* heard, const float* effectGains, float* gainsOut, int numSamples) noexcept
    {
        // Listening goes one sample after another (each step carries on from
        // the last), on a copy of what it keeps track of, close at hand...
        auto t = tracking;
        const auto r = rates;

        for (int i = 0; i < numSamples; ++i)
            gainsOut[i] = (float) listen (t, r, heard[i], effectGains[i]);

        tracking = t;

        // ...but turning those dB into gains can be done four at a time.
        for (int i = 0; i < numSamples; i += 4)
            FastMath::decibelsToGain (FastMath::Vec4::load (gainsOut + i), -100.0f).store (gainsOut + i);
    }

    /** True if it can skip through digital silence with rest() and end up
        where listening would have: it isn't re-learning, and its measuring
        filters have stopped ringing from the last sound. */
    bool canRest() const noexcept
    {
        if (tracking.relearnSamplesLeft > 0)
            return false;

        for (auto& filter : filters)
            if (! filter.hasFallenSilent())
                return false;

        return true;
    }

    /** Moves on by numSamples of digital silence, just as listening to them would. */
    void rest (int numSamples) noexcept
    {
        // (Whatever the measuring filters had left was under a millionth of a millionth.)
        for (auto& filter : filters)
            filter.clear();

        // With nothing coming in, both averages fade at the same pace, so the
        // ratio between them - and so where AUTO-GAIN is heading - stays put...
        auto& t = tracking;
        const double fade = loudnessFade (numSamples);
        t.loudnessIn  *= fade;
        t.loudnessOut *= fade;

        // ...while AUTO-GAIN carries on gliding towards it.
        const double stillToGo = t.autoTargetDb < t.autoGainDb ? fallFade (numSamples) : riseFade (numSamples);
        t.autoGainDb = t.autoTargetDb + (t.autoGainDb - t.autoTargetDb) * stillToGo;
    }

    /** How many dB it's adding right now. */
    float getGainDb() const noexcept               { return (float) tracking.autoGainDb; }

    /** The rate its slow averages move at (for anything that should fade at the same pace). */
    double getAveragingRate() const noexcept       { return rates.loudness; }

private:
    /** Everything it keeps track of from one sample to the next. */
    struct Tracking
    {
        double loudnessIn = 0.0, loudnessOut = 0.0;           // running averages (squared, K-weighted)
        double autoTargetDb = 0.0, autoGainDb = 0.0;          // where it's heading, and where it is
        int    relearnSamplesLeft = 0;
        double relearnTotalIn = 0.0, relearnTotalOut = 0.0;
    };

    /** The rates it moves at (fixed for a given sample rate). */
    struct Rates
    {
        double loudness = 0.0, fastLoudness = 0.0, autoRise = 0.0, relearnRise = 0.0, autoFall = 0.0;
        int    relearnAverageLength = 0;
        double lowestGainDb = 0.0;   // 0 = only ever turns up (see allowTurningDown)
    };

    static constexpr double quietestLoudness = 1.0e-7;   // about -70 dB: quieter than this, it holds still
    static constexpr float  powerDecibelsPerOctave = 3.01029992f;   // 10 * log10 (2), for loudness (a power)

    /** One sample's worth of listening: the original's loudness (heard) and
        the effect's gain. Returns how many dB AUTO-GAIN adds for this sample. */
    static double listen (Tracking& t, const Rates& r, double heard, float effectGain) noexcept
    {
        const double heardAfter = heard * (double) effectGain * (double) effectGain;
        const bool relearning = t.relearnSamplesLeft > 0;
        const double rate = relearning ? r.fastLoudness : r.loudness;

        t.loudnessIn  += (heard      - t.loudnessIn)  * rate;
        t.loudnessOut += (heardAfter - t.loudnessOut) * rate;

        if (relearning)
        {
            // While re-learning, also keep plain totals over its last second
            // (the start is skipped: the effect is still letting go of the
            // old settings then, which would throw the average off)...
            if (t.relearnSamplesLeft <= r.relearnAverageLength)
            {
                t.relearnTotalIn  += heard;
                t.relearnTotalOut += heardAfter;
            }

            // ...and when re-learning ends, carry on from the average of that
            // whole second, so the last hit or two doesn't sway it.
            if (--t.relearnSamplesLeft == 0 && t.relearnTotalIn / (double) r.relearnAverageLength > quietestLoudness)
            {
                t.loudnessIn  = t.relearnTotalIn  / (double) r.relearnAverageLength;
                t.loudnessOut = t.relearnTotalOut / (double) r.relearnAverageLength;
            }
        }

        // In near-silence there's nothing to measure, so it keeps its last value.
        // (10 * log10 (in / out), worked out as the difference of two log2s: quicker
        // than dividing. Below 1e-30 the output counts as silent: the most gain.)
        if (t.loudnessIn > quietestLoudness && t.loudnessOut > 0.0)
            t.autoTargetDb = juce::jlimit (r.lowestGainDb, (double) maxGainDb,
                                           (double) (powerDecibelsPerOctave * (FastMath::log2 ((float) t.loudnessIn)
                                                                               - FastMath::log2 ((float) std::max (t.loudnessOut, 1.0e-30)))));

        const double riseRate = relearning ? r.relearnRise : r.autoRise;
        t.autoGainDb += (t.autoTargetDb - t.autoGainDb) * (t.autoTargetDb < t.autoGainDb ? r.autoFall : riseRate);
        return t.autoGainDb;
    }

    void startRelearning() noexcept
    {
        // Skip three release times (at least 0.25 s, at most 3 s), then average one second.
        const double skipSeconds = juce::jlimit (0.25, 3.0, 3.0 * (double) currentReleaseMs * 0.001);
        tracking.relearnSamplesLeft = (int) (skipSeconds * sampleRate) + rates.relearnAverageLength;
        tracking.relearnTotalIn = tracking.relearnTotalOut = 0.0;
    }

    /** A one-pole smoothing rate that covers ~63% of a change in the given time. */
    double rateFor (double seconds) const noexcept
    {
        return 1.0 - std::exp (-1.0 / (seconds * sampleRate));
    }

    void setUpFilters()
    {
        const double pi = juce::MathConstants<double>::pi;

        // Stage 1: a gentle +4 dB lift above ~1.7 kHz (the effect of the head)
        EarFilter::Stage shelf;
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
        EarFilter::Stage highPass;
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

    std::array<EarFilter, (size_t) maxChannels> filters;
    Tracking tracking;
    Rates    rates;
    float    currentReleaseMs = 100.0f;
    FastMath::RepeatedPower loudnessFade, riseFade, fallFade;   // (for rest)
};

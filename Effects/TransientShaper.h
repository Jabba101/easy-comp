#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include "FastMath.h"
#include "LoudnessMatcher.h"

//==============================================================================
/**
    The transient shaper: makes the start of each hit or note louder or
    softer (PUNCH), and its tail longer or shorter (SUSTAIN). No knobs,
    windows or DAW plumbing in here - just the maths.

    How it knows where a hit starts and where its tail is, every sample:

      0. OUTLINE  Each peak is held for 25 ms, so what follows sees the
                  outline of the sound, not every wiggle of the waveform
                  (otherwise a steady bass note would be "shaped" on every
                  cycle, which sounds like distortion).

      1. FOLLOW   Four "followers" track how loud the sound is, each at a
                  different speed:
                    - quick   jumps up at once, falls back over ~40 ms
                    - lazy    rises over ~12 ms, falls back over ~40 ms
                    - linger  jumps up at once, falls back over ~300 ms
                    - track   jumps up at once, falls back over ~30 ms

      2. SPOT     At the start of a hit, "quick" is already up while "lazy"
                  is still rising: the gap between them is the attack.
                  As a hit dies away, "track" follows it down while "linger"
                  hangs on: the gap between those is the tail.

      3. SHAPE    PUNCH turns the attack gap into a volume change (up for
                  more snap, down for softer), SUSTAIN does the same with
                  the tail gap (up for more ring, down for tighter). Small
                  gaps count for more than their size, so a strum, a piano
                  chord or a sung word responds about as readily as a drum
                  hit out of silence (without the drums going over the top).

      4. APPLY    One volume change per sample, the same on every channel,
                  then OUTPUT (or AUTO-GAIN), MIX and BYPASS as usual.

    With both knobs in the middle (0%) it changes nothing at all. It never
    filters or delays the sound, so there's no latency and MIX and BYPASS
    blend without phasing.

    Like the compressor, it goes through a short run of samples at a time,
    each step sweeping through the whole run: the followers one sample
    after another (each carries on from the last), the decibel maths four
    samples at once. The sound is exactly the same as doing every step for
    one sample at a time; it's just quicker. And in digital silence, once
    the last sound has faded below -60 dB, it skips ahead (see rest()).
*/
class TransientShaper
{
public:
    static constexpr float silenceDb   = -120.0f;
    static constexpr float maxChangeDb = 15.0f;    // the most it will turn any moment up or down

    struct Settings
    {
        float punch    = 0.0f;   // -1 .. +1 (the knob's -100% .. +100%)
        float sustain  = 0.0f;   // -1 .. +1
        float outputDb = 0.0f;   // -12 .. +12 dB
        float mix      = 1.0f;   //  0 (only the original) .. 1 (only the shaped sound)
        bool  autoGain = false;  // AUTO-GAIN: match the volume coming out to the volume going in
        bool  bypassed = false;  // BYPASS: let the original sound straight through
    };

    struct BlockResult
    {
        float maxBoostDb = 0.0f;   // the most it turned anything up (dB)
        float maxCutDb   = 0.0f;   // the most it turned anything down (dB, as a positive number)
        float autoGainDb = 0.0f;   // how much AUTO-GAIN would add right now (dB)
    };

    void prepare (double newSampleRate, const Settings& initialSettings)
    {
        sampleRate = newSampleRate;

        quickRise  = coefficientFor (0.5);    quickFall  = coefficientFor (40.0);
        lazyRise   = coefficientFor (12.0);   lazyFall   = coefficientFor (40.0);
        lingerFall = coefficientFor (300.0);  trackFall  = coefficientFor (30.0);
        changeSmoothing = coefficientFor (0.3);
        outlineFall  = coefficientFor (10.0);
        holdSamples  = juce::jmax (1, (int) std::round (0.025 * sampleRate));   // 25 ms: longer than half a cycle of 20 Hz

        outline = 0.0f;
        holdLeft = 0;
        quick = lazy = linger = track = 0.0f;
        currentChangeDb = 0.0f;

        outlineFade.setBase (outlineFall);
        quickFade.setBase (quickFall);
        lazyFade.setBase (lazyFall);
        lingerFade.setBase (lingerFall);
        trackFade.setBase (trackFall);
        changeFade.setBase (changeSmoothing);

        constexpr double glideSeconds = 0.05;
        smoothedPunch.reset (sampleRate, glideSeconds);
        smoothedSustain.reset (sampleRate, glideSeconds);
        smoothedOutput.reset (sampleRate, glideSeconds);
        smoothedMix.reset (sampleRate, glideSeconds);
        smoothedAutoAmount.reset (sampleRate, glideSeconds);
        smoothedBypass.reset (sampleRate, 0.02);   // BYPASS fades over 20 ms

        autoGain.prepare (sampleRate, 150.0f);   // the shaper settles within ~150 ms of a change
        autoGain.allowTurningDown (true);        // PUNCH and SUSTAIN can make things louder

        hasPreviousSettings = false;
        setSettings (initialSettings, true);
    }

    void setSettings (const Settings& settings, bool jumpToNewValues = false)
    {
        // (Only worked out again when the knob has moved.)
        if (! hasPreviousSettings || settings.outputDb < previousSettings.outputDb || settings.outputDb > previousSettings.outputDb)
            outputAsGain = juce::Decibels::decibelsToGain (settings.outputDb);

        const float autoTarget   = settings.autoGain ? 1.0f : 0.0f;
        const float bypassTarget = settings.bypassed ? 1.0f : 0.0f;

        if (jumpToNewValues)
        {
            smoothedPunch.setCurrentAndTargetValue (settings.punch);
            smoothedSustain.setCurrentAndTargetValue (settings.sustain);
            smoothedOutput.setCurrentAndTargetValue (outputAsGain);
            smoothedMix.setCurrentAndTargetValue (settings.mix);
            smoothedAutoAmount.setCurrentAndTargetValue (autoTarget);
            smoothedBypass.setCurrentAndTargetValue (bypassTarget);
        }
        else
        {
            smoothedPunch.setTargetValue (settings.punch);
            smoothedSustain.setTargetValue (settings.sustain);
            smoothedOutput.setTargetValue (outputAsGain);
            smoothedMix.setTargetValue (settings.mix);
            smoothedAutoAmount.setTargetValue (autoTarget);
            smoothedBypass.setTargetValue (bypassTarget);
        }

        // If the shaping changed, AUTO-GAIN re-learns quickly. (Unlike the
        // compressor's curve, there's no way to predict the new loudness
        // without hearing the next hits.)
        if (hasPreviousSettings && (std::abs (settings.punch - previousSettings.punch) > 0.001f
                                    || std::abs (settings.sustain - previousSettings.sustain) > 0.001f
                                    || std::abs (settings.mix - previousSettings.mix) > 0.001f))
            autoGain.soundChanged();

        previousSettings    = settings;
        hasPreviousSettings = true;
    }

    /** Makes AUTO-GAIN start again from 0 dB (used when a preset is loaded). */
    void restartAutoGain() noexcept   { autoGain.restart(); }

    BlockResult process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int numSamples = buffer.getNumSamples();
        BlockResult result;

        resting = restingAllowed && FastMath::isSilent (buffer) && canRest();

        if (resting)
        {
            rest (numSamples, result);
        }
        else
        {
            for (int start = 0; start < numSamples; start += Work::size)
                processRun (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start,
                            std::min (Work::size, numSamples - start), result);
        }

        result.autoGainDb = autoGain.getGainDb();
        return result;
    }

    /** Whether the last block was digital silence it could skip straight through. */
    bool isResting() const noexcept                     { return resting; }

    /** Skipping through silence is on unless this says otherwise (the tests
        use it to check that skipping gives the same sound as working through). */
    void allowResting (bool shouldAllow) noexcept       { restingAllowed = shouldAllow; }

private:
    static constexpr float curveLimitDb = 12.0f;   // the most 100% PUNCH or SUSTAIN can do from one gap
    static constexpr float curveSpanDb  = 8.0f;    // small gaps: 12 / 8 = 1.5 dB of change per dB of gap
    static constexpr float maxTailGapDb = 20.0f;   // (so a long tail can't be boosted without limit)

    static constexpr float octavesPerGapDb = 1.44269504f / curveSpanDb;   // (for e^-x, worked out as 2^(-x * log2 (e)))

    /** The volume change (dB) a gap makes at 100%: about 1.5 dB per dB for
        small gaps, levelling off towards 12 dB for big ones. That's
        12 * (1 - e^(-gap / 8)), for four gaps at once. */
    static FastMath::Vec4 shapeCurve (FastMath::Vec4 gapDb) noexcept
    {
        using FastMath::Vec4;
        return Vec4::broadcast (curveLimitDb) * (Vec4::broadcast (1.0f) - FastMath::exp2 (-gapDb * Vec4::broadcast (octavesPerGapDb)));
    }

    //==========================================================================
    /** Room to work in, for one run of samples. */
    struct Work
    {
        static constexpr int size = 64;   // samples per run (a multiple of 4)

        float quick[size] {}, lazy[size] {}, linger[size] {}, track[size] {}, changeTarget[size] {}, change[size] {},
              punch[size] {}, sustain[size] {}, output[size] {}, mix[size] {}, autoAmount[size] {}, bypass[size] {},
              effect[size] {}, autoGains[size] {}, gains[size] {}, shown[size] {};
        double heard[size] {};
    };

    /** Steps 0 to 4 for numSamples (at most Work::size) samples from start. */
    void processRun (float* const* channels, int numChannels, int start, int numSamples, BlockResult& result) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numDetected = std::min (numChannels, LoudnessMatcher::maxChannels);
        const int numInFours  = FastMath::roundUpToFour (numSamples);

        // 0. OUTLINE and 1. FOLLOW - one sample after another (each carries on
        //    from the last), for the louder channel at each instant. (Mono and
        //    stereo, nearly always what's used, get their own versions.)
        if (numDetected == 2)       followLevel<2> (channels, start, numSamples, 2);
        else if (numDetected == 1)  followLevel<1> (channels, start, numSamples, 1);
        else                        followLevel<0> (channels, start, numSamples, numDetected);

        //    The knobs at each sample (they only change while gliding to a new setting).
        const FastMath::KnobRun punch      (smoothedPunch,      w.punch,      numSamples);
        const FastMath::KnobRun sustain    (smoothedSustain,    w.sustain,    numSamples);
        const FastMath::KnobRun output     (smoothedOutput,     w.output,     numSamples);
        const FastMath::KnobRun mix        (smoothedMix,        w.mix,        numSamples);
        const FastMath::KnobRun autoAmount (smoothedAutoAmount, w.autoAmount, numSamples);   // 0 = OUTPUT knob, 1 = AUTO-GAIN
        const FastMath::KnobRun bypass     (smoothedBypass,     w.bypass,     numSamples);

        // 2. SPOT and 3. SHAPE - each sample on its own, so four at a time.
        const auto zero = Vec4::broadcast (0.0f), one = Vec4::broadcast (1.0f);
        const auto sixty = Vec4::broadcast (60.0f), ten = Vec4::broadcast (10.0f);

        for (int i = 0; i < numInFours; i += 4)
        {
            //    The attack gap and the tail gap, in dB
            const auto quickDb  = FastMath::gainToDecibels (Vec4::load (w.quick + i),  silenceDb);
            const auto lingerDb = FastMath::gainToDecibels (Vec4::load (w.linger + i), silenceDb);
            const auto attackGapDb = max (zero, quickDb - FastMath::gainToDecibels (Vec4::load (w.lazy + i), silenceDb));
            const auto tailGapDb   = min (Vec4::broadcast (maxTailGapDb),
                                          max (zero, lingerDb - FastMath::gainToDecibels (Vec4::load (w.track + i), silenceDb)));

            //    Near silence there's nothing worth shaping: fade the effect
            //    out between -50 and -60 dB, so it doesn't pump background noise.
            const auto attackWeight = Vec4::limit (zero, one, (quickDb + sixty) / ten);
            const auto tailWeight   = Vec4::limit (zero, one, (lingerDb + sixty) / ten);

            //    Turn the gaps into a volume change
            const auto changeDb = punch.at (i)   * shapeCurve (attackGapDb) * attackWeight
                                + sustain.at (i) * shapeCurve (tailGapDb)   * tailWeight;

            Vec4::limit (Vec4::broadcast (-maxChangeDb), Vec4::broadcast (maxChangeDb), changeDb).store (w.changeTarget + i);
        }

        //    (a touch of smoothing: one sample after another)
        {
            float current = currentChangeDb;
            const float smoothing = changeSmoothing;

            for (int i = 0; i < numSamples; ++i)
            {
                const float changeDb = w.changeTarget[i];
                current = changeDb + smoothing * (current - changeDb);
                w.change[i] = current;
            }

            currentChangeDb = current;
        }

        // 4. APPLY - four at a time. MIX: the original blended with the shaped sound...
        for (int i = 0; i < numInFours; i += 4)
        {
            const auto shaped = FastMath::decibelsToGain (Vec4::load (w.change + i), -100.0f);
            const auto wet    = mix.at (i);
            ((one - wet) + wet * shaped).store (w.effect + i);
        }

        //    ...AUTO-GAIN (listening goes one sample after another)...
        autoGain.process (w.heard, w.effect, w.autoGains, numSamples);

        //    ...then OUTPUT or AUTO-GAIN, and BYPASS (which fades to exactly 1).
        //    (Plus the most it turned up and down, for the meter.)
        auto mostBoost = Vec4::broadcast (result.maxBoostDb), mostCut = Vec4::broadcast (result.maxCutDb);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto effect       = Vec4::load (w.effect + i);
            const auto withOutput   = effect * output.at (i);
            const auto withAutoGain = effect * Vec4::load (w.autoGains + i);
            const auto howAuto      = autoAmount.at (i);
            const auto active       = withOutput * (one - howAuto) + withAutoGain * howAuto;
            const auto bypassed     = bypass.at (i);
            const auto shown        = Vec4::load (w.change + i) * mix.at (i) * (one - bypassed);

            (active * (one - bypassed) + bypassed).store (w.gains + i);

            if (i + 4 <= numSamples)
            {
                mostBoost = max (mostBoost, shown);
                mostCut   = max (mostCut, -shown);
            }
            else
            {
                shown.store (w.shown + i);   // (the last few: only some of the four are real)
            }
        }

        result.maxBoostDb = FastMath::largestOf (mostBoost);
        result.maxCutDb   = FastMath::largestOf (mostCut);

        for (int i = numSamples & ~3; i < numSamples; ++i)
        {
            result.maxBoostDb = std::max (result.maxBoostDb, w.shown[i]);
            result.maxCutDb   = std::max (result.maxCutDb, -w.shown[i]);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::multiply (channels[ch] + start, w.gains, numSamples);
    }

    /** Steps 0 and 1 for a run of samples. fixedChannels is 1 or 2 for mono or
        stereo (worked out specially), or 0 for any other number of channels. */
    template <int fixedChannels>
    void followLevel (float* const* channels, int start, int numSamples, int numDetected) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) LoudnessMatcher::maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numDetected;

        // Copies of everything that carries on from sample to sample, close at hand
        float o = outline, q = quick, la = lazy, li = linger, tr = track;
        int held = holdLeft;
        const int hold = holdSamples;
        const float oFall = outlineFall, qRise = quickRise, qFall = quickFall, laRise = lazyRise, laFall = lazyFall;
        const float liFall = lingerFall, trFall = trackFall;
        auto& w = work;

        // One sample's worth of steps 0 and 1, for the louder channel's level
        const auto followAt = [&] (int i, float level) noexcept
        {
            //    Hold each peak for 25 ms, then let it fall back
            if (level >= o)         { o = level; held = hold; }
            else if (held > 0)      { --held; }
            else                    { o = level + oFall * (o - level); }

            q  = follow (q,  o, qRise,  qFall);
            la = follow (la, o, laRise, laFall);
            li = follow (li, o, qRise,  liFall);
            tr = follow (tr, o, qRise,  trFall);

            w.quick[i]  = q;
            w.lazy[i]   = la;
            w.linger[i] = li;
            w.track[i]  = tr;
        };

        if constexpr (fixedChannels == 2)
        {
            // Stereo: both ear filters at once
            LoudnessMatcher::EarFilterPair ears (autoGain.earFilter (0), autoGain.earFilter (1));

            for (int i = 0; i < numSamples; ++i)
            {
                const float left = channels[0][start + i], right = channels[1][start + i];
                followAt (i, std::max (std::max (0.0f, std::abs (left)), std::abs (right)));
                w.heard[i] = ears.measure (left, right);
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
                float  level = 0.0f;
                double heard = 0.0;

                for (int ch = 0; ch < count; ++ch)
                {
                    const float sample = channels[ch][start + i];
                    level = std::max (level, std::abs (sample));
                    heard += ears[ch].measure (sample);
                }

                followAt (i, level);
                w.heard[i] = heard;
            }

            for (int ch = 0; ch < count; ++ch)
                autoGain.earFilter (ch) = ears[ch];
        }

        outline = o; quick = q; lazy = la; linger = li; track = tr;
        holdLeft = held;
    }

    //==========================================================================
    /** True if the block about to come (digital silence) can be skipped
        through with rest(), ending up exactly where working through it
        would: no knob is gliding, the last peak isn't being held any more,
        every follower is on its way down, and the shaping has nothing left
        to do (see SPOT: it stops below -60 dB). */
    bool canRest() const noexcept
    {
        if (smoothedPunch.isSmoothing() || smoothedSustain.isSmoothing() || smoothedOutput.isSmoothing()
             || smoothedMix.isSmoothing() || smoothedAutoAmount.isSmoothing() || smoothedBypass.isSmoothing())
            return false;

        if (! autoGain.canRest())
            return false;

        if (holdLeft > 0 && outline > 0.0f)
            return false;

        if (quick < outline || lazy < outline || linger < outline || track < outline)
            return false;

        // PUNCH works from the quick follower and SUSTAIN from the lingering one;
        // each changes nothing once its follower is below -60 dB, or if its
        // knob is in the middle. (The followers only fall from here, so once
        // they're below -60 dB they stay there.)
        const bool punchDone   = ! (std::abs (smoothedPunch.getTargetValue()) > 0.0f)
                                    || FastMath::gainToDecibels (quick, silenceDb) + 60.0f <= 0.0f;
        const bool sustainDone = ! (std::abs (smoothedSustain.getTargetValue()) > 0.0f)
                                    || FastMath::gainToDecibels (linger, silenceDb) + 60.0f <= 0.0f;
        return punchDone && sustainDone;
    }

    /** Moves on by numSamples of digital silence, just as working through them would. */
    void rest (int numSamples, BlockResult& result) noexcept
    {
        // The outline falls away at its own pace, and each follower follows
        // it down at its own. (Worked out in one go: after n samples, a
        // follower e falling at rate c behind an outline o falling at rate a
        // is at  c^n e + (1 - c) a o (a^n - c^n) / (a - c).)
        const double o = outline, a = outlineFall, an = outlineFade (numSamples);

        const auto fallen = [&] (float envelope, float fall, FastMath::RepeatedPower& fade)
        {
            const double c = fall, cn = fade (numSamples);
            double e = cn * (double) envelope;

            if (o > 0.0 && std::abs (a - c) > 0.0)
                e += (1.0 - c) * a * o * (an - cn) / (a - c);

            return (float) e;
        };

        quick  = fallen (quick,  quickFall,  quickFade);
        lazy   = fallen (lazy,   lazyFall,   lazyFade);
        linger = fallen (linger, lingerFall, lingerFade);
        track  = fallen (track,  trackFall,  trackFade);
        outline = (float) (o * an);

        if (outline < (std::numeric_limits<float>::min)())   // (what the processor would round to 0)
            outline = 0.0f;

        if (outline <= 0.0f && numSamples > 0)
            holdLeft = holdSamples;   // (silence "holds" silence)

        // The shaping has stopped, so what's left of the last change fades
        // out (the meters show the first, biggest, of those).
        const float first = changeSmoothing * currentChangeDb;
        const float shown = first * smoothedMix.getTargetValue() * (1.0f - smoothedBypass.getTargetValue());
        result.maxBoostDb = std::max (result.maxBoostDb, shown);
        result.maxCutDb   = std::max (result.maxCutDb, -shown);
        currentChangeDb = (float) ((double) currentChangeDb * changeFade (numSamples));

        autoGain.rest (numSamples);
    }

    //==========================================================================
    /** One step of a follower: rise towards a louder level at riseCoefficient,
        fall towards a quieter one at fallCoefficient. */
    static float follow (float envelope, float level, float riseCoefficient, float fallCoefficient) noexcept
    {
        const float coefficient = level > envelope ? riseCoefficient : fallCoefficient;
        return level + coefficient * (envelope - level);
    }

    /** The per-sample factor for a follower that covers ~63% of a change in the given time. */
    float coefficientFor (double milliseconds) const noexcept
    {
        const double samples = std::max (1.0, milliseconds * 0.001 * sampleRate);
        return (float) std::exp (-1.0 / samples);
    }

    double sampleRate = 44100.0;

    float quickRise = 0.0f, quickFall = 0.0f, lazyRise = 0.0f, lazyFall = 0.0f;
    float lingerFall = 0.0f, trackFall = 0.0f, changeSmoothing = 0.0f, outlineFall = 0.0f;
    float outline = 0.0f;
    int   holdSamples = 1, holdLeft = 0;
    float quick = 0.0f, lazy = 0.0f, linger = 0.0f, track = 0.0f;
    float currentChangeDb = 0.0f;
    float outputAsGain = 1.0f;

    juce::SmoothedValue<float> smoothedPunch, smoothedSustain, smoothedMix, smoothedAutoAmount, smoothedBypass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedOutput;

    LoudnessMatcher autoGain;

    Settings previousSettings;
    bool hasPreviousSettings = false;

    Work work;
    bool resting = false, restingAllowed = true;
    FastMath::RepeatedPower outlineFade, quickFade, lazyFade, lingerFade, trackFade, changeFade;   // (for rest)
};

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <cmath>
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
                  the tail gap (up for more ring, down for tighter).

      4. APPLY    One volume change per sample, the same on every channel,
                  then OUTPUT (or AUTO-GAIN), MIX and BYPASS as usual.

    With both knobs in the middle (0%) it changes nothing at all. It never
    filters or delays the sound, so there's no latency and MIX and BYPASS
    blend without phasing.
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
        const float outputAsGain = juce::Decibels::decibelsToGain (settings.outputDb);
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
        const int numChannels = buffer.getNumChannels();
        const int numSamples  = buffer.getNumSamples();
        const int numDetected = std::min (numChannels, LoudnessMatcher::maxChannels);
        auto* const* channels = buffer.getArrayOfWritePointers();

        BlockResult result;

        for (int i = 0; i < numSamples; ++i)
        {
            // 1. FOLLOW - how loud is it right now (the louder channel), at four speeds
            float  level = 0.0f;
            double heard = 0.0;

            for (int ch = 0; ch < numDetected; ++ch)
            {
                level = std::max (level, std::abs (channels[ch][i]));
                heard += autoGain.measure (ch, channels[ch][i]);
            }

            // 0. OUTLINE - hold each peak for 25 ms, then let it fall back
            if (level >= outline)       { outline = level; holdLeft = holdSamples; }
            else if (holdLeft > 0)      { --holdLeft; }
            else                        { outline = level + outlineFall * (outline - level); }

            quick  = follow (quick,  outline, quickRise, quickFall);
            lazy   = follow (lazy,   outline, lazyRise,  lazyFall);
            linger = follow (linger, outline, quickRise, lingerFall);
            track  = follow (track,  outline, quickRise, trackFall);

            // 2. SPOT - the attack gap and the tail gap, in dB
            const float quickDb  = juce::Decibels::gainToDecibels (quick,  silenceDb);
            const float lingerDb = juce::Decibels::gainToDecibels (linger, silenceDb);
            const float attackGapDb = std::max (0.0f, quickDb - juce::Decibels::gainToDecibels (lazy, silenceDb));
            const float tailGapDb   = std::min (maxTailGapDb, std::max (0.0f, lingerDb - juce::Decibels::gainToDecibels (track, silenceDb)));

            //    Near silence there's nothing worth shaping: fade the effect
            //    out between -50 and -60 dB, so it doesn't pump background noise.
            const float attackWeight = juce::jlimit (0.0f, 1.0f, (quickDb + 60.0f) / 10.0f);
            const float tailWeight   = juce::jlimit (0.0f, 1.0f, (lingerDb + 60.0f) / 10.0f);

            // 3. SHAPE - turn the gaps into a volume change
            float changeDb = smoothedPunch.getNextValue()   * punchScale   * attackGapDb * attackWeight
                           + smoothedSustain.getNextValue() * sustainScale * tailGapDb   * tailWeight;
            changeDb = juce::jlimit (-maxChangeDb, maxChangeDb, changeDb);

            currentChangeDb = changeDb + changeSmoothing * (currentChangeDb - changeDb);   // (a touch of smoothing)

            // 4. APPLY
            const float shaped = juce::Decibels::decibelsToGain (currentChangeDb);
            const float wet    = smoothedMix.getNextValue();
            const float effect = (1.0f - wet) + wet * shaped;            // MIX: original blended with shaped

            const float withOutput   = effect * smoothedOutput.getNextValue();
            const float withAutoGain = effect * autoGain.process (heard, effect);

            const float autoAmount = smoothedAutoAmount.getNextValue();   // 0 = OUTPUT knob, 1 = AUTO-GAIN
            const float active     = withOutput * (1.0f - autoAmount) + withAutoGain * autoAmount;

            const float bypass = smoothedBypass.getNextValue();
            const float gain   = active * (1.0f - bypass) + bypass;      // BYPASS fades to exactly 1

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;

            const float shownChangeDb = currentChangeDb * wet * (1.0f - bypass);
            result.maxBoostDb = std::max (result.maxBoostDb, shownChangeDb);
            result.maxCutDb   = std::max (result.maxCutDb, -shownChangeDb);
        }

        result.autoGainDb = autoGain.getGainDb();
        return result;
    }

private:
    static constexpr float punchScale   = 0.5f;    // 100% PUNCH = half the attack gap (up to ~+11 dB on a sharp hit)
    static constexpr float sustainScale = 0.6f;    // 100% SUSTAIN = 60% of the tail gap
    static constexpr float maxTailGapDb = 20.0f;   // (so a long tail can't be boosted without limit)

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

    juce::SmoothedValue<float> smoothedPunch, smoothedSustain, smoothedMix, smoothedAutoAmount, smoothedBypass;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedOutput;

    LoudnessMatcher autoGain;

    Settings previousSettings;
    bool hasPreviousSettings = false;
};

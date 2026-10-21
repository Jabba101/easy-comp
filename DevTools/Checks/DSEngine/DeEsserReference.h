#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "FastMath.h"

//==============================================================================
/**
    The de-esser itself: the part that changes the sound. No knobs, windows or
    DAW plumbing in here - just the maths.

    It tames harshness: the hissy "S" and "SH" sounds in a vocal, or the
    splashy, clangy peaks of cymbals and hi-hats. It turns down only the
    harsh moments, so everything else stays bright and clear.

    What happens to every single sample:

      1. LISTEN   It hears two things: the harsh band (everything above FROM,
                  or the band from FROM to TO) and the rest of the sound below
                  it, split steeply (24 dB per octave) so each hears only its
                  own part.
      2. DECIDE   How much louder is the harsh band than the rest, right now?
                  An S is all hiss and hardly any voice, so the harsh band is
                  much louder than the rest; in an ordinary word it's much
                  quieter. Comparing the two (not just the harsh band's own
                  level) is what keeps it working the same whether the singer
                  is whispering or belting. THRESHOLD sets a line that follows
                  the rest (never lower than -60 dB, so background hiss alone
                  never counts); wherever the harsh band goes over the line,
                  it's turned down by that much, never more than RANGE.
      3. SMOOTH   It moves quickly (an S is over in a moment) but smoothly,
                  along an S-shaped curve, so it can't click.
      4. APPLY    TURNS DOWN: HARSH BAND - only the harsh band dips while each
                  S passes: a shelf for everything above FROM, or a dip
                  centred between FROM and TO (about half as deep at FROM and
                  TO themselves). WHOLE SOUND - everything dips for the moment
                  of each S, the classic style. (Switching between them fades
                  over 20 ms, so it can't click either.)

    It looks 2 ms ahead, like Easy Gate: your sound is delayed by 2 ms (your
    DAW makes up for that by itself), so it's already turned down when each
    S starts. While it isn't turning anything down, your sound comes out bit
    for bit as it went in (just 2 ms later).

    Both channels always get exactly the same change, so the stereo image
    stays put.

    Organised like the other effects, to use as little of your computer as
    possible: a short run of samples at a time (64), sample after sample for
    the parts that carry on from one sample to the next, four samples at once
    for the rest. And in digital silence it skips straight through (see rest()).
*/
class DeEsserReference
{
public:
    static constexpr float  silenceDb        = -120.0f;   // how "silence" is written in dB here
    static constexpr float  toOffHz          = 20000.0f;  // TO at or over this = off: everything above FROM
    static constexpr double lookAheadSeconds = 0.002;     // how far ahead it looks
    static constexpr int    maxChannels      = 8;

    /** The knob and button settings. */
    struct Settings
    {
        float fromHz      = 5000.0f;    //  1000 .. 16000 Hz: where the harsh band starts
        float toHz        = 20000.0f;   //  2000 .. 20000 Hz (off): where it ends
        float thresholdDb = -6.0f;      // how loud the harsh band may get compared with the rest (dB)
        float rangeDb     = 8.0f;       // the most it turns down (dB)
        bool  wholeSound  = false;      // TURNS DOWN: false = just the harsh band, true = the whole sound
        bool  listen      = false;      // LISTEN: hear only what it takes out
        bool  bypassed    = false;      // BYPASS: let the original sound straight through
    };

    /** A few milliseconds of what happened, for the display. (Levels as
        powers: 1 = 0 dB, so 10 x log10 gives dB.) */
    struct Moment
    {
        float soundLevel = 0.0f;   // the loudest the sound got (the harsh band and the rest together)
        float bandLevel  = 0.0f;   // the loudest the harsh band got
        float lineLevel  = 0.0f;   // where the THRESHOLD line was, on average
        float gain       = 1.0f;   // the most it turned down, as a gain (1 = not at all; BYPASS counts as not at all)
    };

    //==========================================================================
    /** Call before audio starts, and whenever the sample rate changes.
        momentLength: how many samples each Moment for the display covers. */
    void prepare (double newSampleRate, int newMomentLength, const Settings& initialSettings)
    {
        sampleRate   = newSampleRate;
        lookAhead    = std::max (1, (int) std::lround (lookAheadSeconds * sampleRate));
        momentLength = std::max (1, newMomentLength);

        delayLines.assign ((size_t) (maxChannels * lookAhead), 0.0f);
        delayPosition = 0;

        // What it hears is followed up at once, and down over ~12 ms
        followUp   = (float) std::exp (-1.0 / (0.0003 * sampleRate));
        followDown = (float) std::exp (-1.0 / (0.012 * sampleRate));

        // It turns down over the 2 ms it looks ahead (so it's all the way down
        // just as each S arrives) and lets go over 40 ms, along the S-shaped curve.
        attackCoefficient  = timeToCoefficient ((float) (lookAheadSeconds * 1000.0));
        releaseCoefficient = timeToCoefficient (40.0f);

        for (auto* smoothed : { &smoothedBypass, &smoothedListen, &smoothedShelf, &smoothedDip, &smoothedWhole })
            smoothed->reset (sampleRate, 0.02);

        hasPreviousSettings = false;
        setSettings (initialSettings, true);

        bandFollower = restFollower = 0.0f;
        targetNow = 1.0f;
        errorA = errorB = 0.0f;

        for (auto* filters : allFilters())
            for (auto& f : *filters)
                f.reset();

        moment = {};
        momentFilled = 0;
        lineTotal = 0.0;
        quietSamples = 0;
    }

    /** Hands over the latest settings; call this once per block of audio.
        With jumpToNewValues = true the fades jump straight to the new setting. */
    void setSettings (const Settings& settings, bool jumpToNewValues = false)
    {
        // (Anything worked out from a knob is only worked out again when that knob has moved.)
        const bool fresh = ! hasPreviousSettings;
        const auto moved = [fresh] (float now, float before) { return fresh || now < before || now > before; };

        if (moved (settings.fromHz, previousSettings.fromHz) || moved (settings.toHz, previousSettings.toHz))
        {
            // The harsh band: everything above FROM, or (with TO set) the band
            // between them - whichever way round they're set, and at least a
            // third of an octave wide.
            const float low  = std::min (settings.fromHz, settings.toHz);
            const float high = std::max (settings.fromHz, settings.toHz);
            aboveOnly = high >= toOffHz - 0.5f;

            const float lowerHz = aboveOnly ? settings.fromHz : std::min (low, high / 1.26f);
            const float upperHz = aboveOnly ? toOffHz : std::max (high, low * 1.26f);

            // Hearing: a steep split at the lower edge (two filters in a row:
            // half the power each side of the edge, and 24 dB per octave
            // beyond it), and a gentler one at the upper edge.
            const float lowerWarp = warp (lowerHz);
            splitFirst  = coefficientsFor (lowerWarp, 1.84775907f);   // (2 cos (pi / 8): a 4th-order Butterworth split...
            splitSecond = coefficientsFor (lowerWarp, 0.76536686f);   //  ...and 2 cos (3 pi / 8))
            upperEdge   = coefficientsFor (warp (upperHz), juce::MathConstants<float>::sqrt2);

            // Everything above FROM is turned down with a high shelf, half way down at FROM...
            shelfWarp = lowerWarp;
            restingShelf = ShelfCoefficients::forGain (1.0f, shelfWarp);

            // ...and a band with a dip centred between FROM and TO, wide enough
            // that it's about half as deep (in dB) at FROM and TO themselves.
            const float octaves = std::log2 (upperHz / lowerHz);
            const float widthFactor = std::exp2 (octaves * 0.5f) - std::exp2 (-octaves * 0.5f);
            bellK = widthFactor / 0.627f;   // (1 / Q)
            bell = coefficientsFor (warp (std::sqrt (lowerHz * upperHz)), bellK);
        }

        if (moved (settings.thresholdDb, previousSettings.thresholdDb))
            lineFactor = juce::Decibels::decibelsToGain (settings.thresholdDb * 2.0f, -1000.0f);   // (THRESHOLD as a power ratio: 10 ^ (dB / 10))

        maxReductionDb = std::max (0.0f, settings.rangeDb);

        // The fades: BYPASS, LISTEN, and which way it turns down.
        const int mode = settings.wholeSound ? wholeMode : (aboveOnly ? shelfMode : dipMode);
        const std::pair<juce::SmoothedValue<float>*, float> targets[] = {
            { &smoothedBypass, settings.bypassed ? 1.0f : 0.0f },
            { &smoothedListen, settings.listen ? 1.0f : 0.0f },
            { &smoothedShelf,  mode == shelfMode ? 1.0f : 0.0f },
            { &smoothedDip,    mode == dipMode ? 1.0f : 0.0f },
            { &smoothedWhole,  mode == wholeMode ? 1.0f : 0.0f }
        };

        for (auto& [smoothed, target] : targets)
        {
            if (jumpToNewValues)
                smoothed->setCurrentAndTargetValue (target);
            else
                smoothed->setTargetValue (target);
        }

        currentMode = mode;
        previousSettings    = settings;
        hasPreviousSettings = true;
    }

    /** How many samples late the sound comes out (the look-ahead). Report this to the DAW. */
    int getLatencySamples() const noexcept    { return lookAhead; }

    /** De-esses the audio in place. onMoment (const Moment&) is called each
        time another Moment for the display is complete. Returns the most it
        turned down in this block (dB, as a positive number). */
    template <typename OnMoment>
    float process (juce::AudioBuffer<float>& audio, OnMoment&& onMoment) noexcept
    {
        const int numSamples  = audio.getNumSamples();
        const int numChannels = std::min (audio.getNumChannels(), maxChannels);

        const bool silent = FastMath::isSilent (audio);
        resting = restingAllowed && silent && canRest();
        mostTurnedDown = 1.0f;

        if (resting)
        {
            rest (numSamples, onMoment);
        }
        else
        {
            float* const* channels = audio.getArrayOfWritePointers();

            for (int start = 0; start < numSamples; start += Work::size)
                processRun (channels, numChannels, start, std::min (Work::size, numSamples - start), onMoment);
        }

        quietSamples = silent ? std::min (quietSamples + numSamples, 1 << 30) : 0;
        return -FastMath::gainToDecibels (mostTurnedDown, silenceDb);
    }

    /** The same, without the display's moments. */
    float process (juce::AudioBuffer<float>& audio) noexcept
    {
        return process (audio, [] (const Moment&) noexcept {});
    }

    /** Whether the last block was digital silence it could skip straight through. */
    bool isResting() const noexcept                     { return resting; }

    /** Skipping through silence is on unless this says otherwise (the tests
        use it to check that skipping gives the same sound as working through). */
    void allowResting (bool shouldAllow) noexcept       { restingAllowed = shouldAllow; }

    /** The THRESHOLD line never goes below this (as a power: -60 dB), so
        background hiss on its own is never treated as harsh. */
    static constexpr float quietestPower() noexcept     { return 1.0e-6f; }

private:
    //==========================================================================
    /** Room to work in, for one run of samples. */
    struct Work
    {
        static constexpr int size = 64;   // samples per run (a multiple of 4)

        float band[size] {}, rest[size] {}, bandFollowed[size] {}, restFollowed[size] {}, targets[size] {},
              gains[size] {}, bypass[size] {}, listen[size] {}, outGains[size] {}, originalGains[size] {}, shownGains[size] {},
              shelfWeights[size] {}, dipWeights[size] {}, wholeWeights[size] {};
        float original[maxChannels][size] {};
    };

    template <typename OnMoment>
    void processRun (float* const* channels, int numChannels, int start, int numSamples, OnMoment& onMoment) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numInFours = FastMath::roundUpToFour (numSamples);

        // 1. LISTEN - the harsh band and the rest, as heard now (before the delay)
        if (numChannels == 2)        hear<2> (channels, 2, start, numSamples);
        else if (numChannels == 1)   hear<1> (channels, 1, start, numSamples);
        else                         hear<0> (channels, numChannels, start, numSamples);

        follow (numSamples);

        // 2. DECIDE - how far over the THRESHOLD line is the harsh band? (Four
        //    samples at once.) Over it, it's turned down by that much - easing
        //    in over a 6 dB "knee" - but never more than RANGE.
        {
            const auto floor = Vec4::broadcast (quietestPower()), tiny = Vec4::broadcast (1.0e-30f);
            const auto lineMove = Vec4::broadcast (lineFactor), range = Vec4::broadcast (maxReductionDb);
            const auto halfKnee = Vec4::broadcast (kneeDb * 0.5f), zero = Vec4::broadcast (0.0f);
            const auto dbPerOctave = Vec4::broadcast (decibelsPerOctaveOfPower), twiceKnee = Vec4::broadcast (2.0f * kneeDb);

            for (int i = 0; i < numInFours; i += 4)
            {
                const auto band = max (Vec4::load (w.bandFollowed + i), tiny);
                const auto line = Vec4::load (w.restFollowed + i) * lineMove + floor;
                const auto overDb = dbPerOctave * (FastMath::log2 (band) - FastMath::log2 (line));

                const auto intoKnee = overDb + halfKnee;
                const auto eased = intoKnee * intoKnee / twiceKnee;
                const auto reduction = min (range, Vec4::select (overDb <= -halfKnee, zero, Vec4::select (overDb >= halfKnee, overDb, eased)));

                FastMath::decibelsToGain (-reduction, -1000.0f).store (w.targets + i);
            }
        }

        // 3. SMOOTH - quickly but smoothly, one sample after another
        smooth (numSamples);

        // 4. APPLY - the sound is delayed by the look-ahead (a copy kept for
        //    LISTEN and BYPASS), then turned down...
        for (int ch = 0; ch < numChannels; ++ch)
        {
            delay (channels[ch] + start, numSamples, delayLines.data() + (size_t) ch * (size_t) lookAhead);
            juce::FloatVectorOperations::copy (w.original[ch], channels[ch] + start, numSamples);
        }

        delayPosition = (delayPosition + numSamples) % lookAhead;

        if (numChannels == 2)        turnDown<2> (channels, 2, start, numSamples);
        else if (numChannels == 1)   turnDown<1> (channels, 1, start, numSamples);
        else                         turnDown<0> (channels, numChannels, start, numSamples);

        //    ...and LISTEN fades over to just what's been taken out; BYPASS fades
        //    to your original sound (2 ms later, like everything else).
        const FastMath::KnobRun bypass (smoothedBypass, w.bypass, numSamples);
        const FastMath::KnobRun listen (smoothedListen, w.listen, numSamples);
        const bool mixing = smoothedBypass.isSmoothing() || smoothedListen.isSmoothing()
                         || smoothedBypass.getTargetValue() > 0.0f || smoothedListen.getTargetValue() > 0.0f;
        const auto one = Vec4::broadcast (1.0f), two = Vec4::broadcast (2.0f);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto bypassed = bypass.at (i), listened = listen.at (i);
            (Vec4::load (w.gains + i) * (one - bypassed) + bypassed).store (w.shownGains + i);

            if (mixing)
            {
                // out = turned down x (1 - LISTEN) + (original - turned down) x LISTEN, then faded to the original by BYPASS
                ((one - bypassed) * (one - two * listened)).store (w.outGains + i);
                (listened * (one - bypassed) + bypassed).store (w.originalGains + i);
            }
        }

        if (mixing)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                float* const samples = channels[ch] + start;
                juce::FloatVectorOperations::multiply (samples, w.outGains, numSamples);
                juce::FloatVectorOperations::addWithMultiply (samples, w.original[ch], w.originalGains, numSamples);
            }
        }

        // And notes for the meter and the display.
        recordMoments (numSamples, onMoment);
    }

    //==========================================================================
    /** Step 1, one sample after another: the harsh band and the rest. Their
        power, the average over the channels. */
    template <int fixedChannels>
    void hear (float* const* channels, int numChannels, int start, int numSamples) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numChannels;
        auto& w = work;

        Filter firsts[capacity], lows[capacity], highs[capacity], tops[capacity];

        for (int ch = 0; ch < count; ++ch)
        {
            firsts[ch] = firstSplits[(size_t) ch];
            lows[ch]   = lowSplits[(size_t) ch];
            highs[ch]  = highSplits[(size_t) ch];
            tops[ch]   = upperSplits[(size_t) ch];
        }

        const auto first = splitFirst, second = splitSecond, top = upperEdge;
        const bool wholeTop = aboveOnly;
        const float scale = 1.0f / (float) count;

        for (int i = 0; i < numSamples; ++i)
        {
            float bandPower = 0.0f, restPower = 0.0f;

            for (int ch = 0; ch < count; ++ch)
            {
                const float x = channels[ch][start + i];

                float a1, a2;                                    // the first half of the split...
                firsts[ch].tick (x, first, a1, a2);
                const float lowHalf = a2, highHalf = x - first.k * a1 - a2;

                float r1, rest;                                  // ...the second: everything below the harsh band
                lows[ch].tick (lowHalf, second, r1, rest);

                float h1, h2;                                    // ...and everything above its lower edge
                highs[ch].tick (highHalf, second, h1, h2);
                const float above = highHalf - second.k * h1 - h2;

                float t1, upTo;                                  // ...up to TO (when it's set)
                tops[ch].tick (above, top, t1, upTo);

                const float band = wholeTop ? above : upTo;
                bandPower += band * band;
                restPower += rest * rest;
            }

            w.band[i] = bandPower * scale;
            w.rest[i] = restPower * scale;
        }

        for (int ch = 0; ch < count; ++ch)
        {
            firstSplits[(size_t) ch] = firsts[ch];
            lowSplits[(size_t) ch]   = lows[ch];
            highSplits[(size_t) ch]  = highs[ch];
            upperSplits[(size_t) ch] = tops[ch];
        }
    }

    /** ...followed: up at once, down over ~12 ms (both the same way, so they compare fairly). */
    void follow (int numSamples) noexcept
    {
        auto& w = work;
        float band = bandFollower, rest = restFollower;
        const float up = followUp, down = followDown;

        for (int i = 0; i < numSamples; ++i)
        {
            const float b = w.band[i], r = w.rest[i];
            band = b + (b > band ? up : down) * (band - b);
            rest = r + (r > rest ? up : down) * (rest - r);
            w.bandFollowed[i] = band;
            w.restFollowed[i] = rest;
        }

        bandFollower = band;
        restFollower = rest;
    }

    /** Step 3: the gain glides towards each sample's target along the S-shaped
        curve - quickly when it needs to turn down more, a little more gently
        when it's letting go. (As in Easy Gate, it's kept as how far it still
        has to go, so it arrives exactly - at a gain of exactly 1 when it isn't
        turning anything down, so your sound is untouched.) */
    void smooth (int numSamples) noexcept
    {
        auto& w = work;
        float a = errorA, b = errorB, target = targetNow;
        const float attack = attackCoefficient, release = releaseCoefficient;

        for (int i = 0; i < numSamples; ++i)
        {
            const float newTarget = w.targets[i];
            a += target - newTarget;    // (measured from the new target instead)
            b += target - newTarget;
            target = newTarget;

            const float coefficient = b > 0.0f ? attack : release;   // (still above the target: it needs to turn down more)
            a *= coefficient;
            b = coefficient * b + (1.0f - coefficient) * a;

            if (std::abs (a) < arrivedDistance && std::abs (b) < arrivedDistance)
                a = b = 0.0f;

            w.gains[i] = target + b;
        }

        errorA = a;
        errorB = b;
        targetNow = target;
    }

    /** Step 4, the turning down itself, one sample after another. All the
        ways of turning down always follow the sound (each one exactly "no
        change" while it isn't turning anything down), so fading from one to
        another is seamless. */
    template <int fixedChannels>
    void turnDown (float* const* channels, int numChannels, int start, int numSamples) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numChannels;
        auto& w = work;

        Filter shelves[capacity], dips[capacity];

        for (int ch = 0; ch < count; ++ch)
        {
            shelves[ch] = shelfFilters[(size_t) ch];
            dips[ch]    = dipFilters[(size_t) ch];
        }

        const bool fading = smoothedShelf.isSmoothing() || smoothedDip.isSmoothing() || smoothedWhole.isSmoothing();

        if (fading)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                w.shelfWeights[i] = smoothedShelf.getNextValue();
                w.dipWeights[i]   = smoothedDip.getNextValue();
                w.wholeWeights[i] = smoothedWhole.getNextValue();
            }
        }

        const ShelfCoefficients still = restingShelf;
        const Coefficients d = bell;
        const float k = bellK, warped = shelfWarp;
        const int mode = currentMode;
        const bool shelving = fading || mode == shelfMode;

        for (int i = 0; i < numSamples; ++i)
        {
            const float g = w.gains[i];
            const auto shelf = (shelving && g < 1.0f) ? ShelfCoefficients::forGain (g, warped) : still;
            const float dipBy = (g - 1.0f) * k;

            for (int ch = 0; ch < count; ++ch)
            {
                const float x = channels[ch][start + i];
                const float shelved = shelves[ch].shelf (x, shelf);

                float b1, b2;
                dips[ch].tick (x, d, b1, b2);
                const float dipped = x + dipBy * b1;
                const float whole = x * g;

                float out;

                if (! fading)
                    out = mode == shelfMode ? shelved : (mode == dipMode ? dipped : whole);
                else   // (written as changes, so it's still exactly x while nothing is turned down)
                    out = x + w.shelfWeights[i] * (shelved - x) + w.dipWeights[i] * (dipped - x) + w.wholeWeights[i] * (whole - x);

                channels[ch][start + i] = out;
            }
        }

        for (int ch = 0; ch < count; ++ch)
        {
            shelfFilters[(size_t) ch] = shelves[ch];
            dipFilters[(size_t) ch]   = dips[ch];
        }
    }

    /** Delays one channel's run of samples by the look-ahead. */
    void delay (float* samples, int numSamples, float* line) const noexcept
    {
        for (int done = 0, position = delayPosition; done < numSamples;)
        {
            const int chunk = std::min (numSamples - done, lookAhead - position);
            std::swap_ranges (samples + done, samples + done + chunk, line + position);
            done += chunk;
            position = (position + chunk) % lookAhead;
        }
    }

    //==========================================================================
    template <typename OnMoment>
    void recordMoments (int numSamples, OnMoment& onMoment) noexcept
    {
        auto& w = work;
        const float lineMove = lineFactor;

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);

            for (int i = done; i < done + length; ++i)
            {
                moment.soundLevel = std::max (moment.soundLevel, w.bandFollowed[i] + w.restFollowed[i]);
                moment.bandLevel  = std::max (moment.bandLevel, w.bandFollowed[i]);
                lineTotal += (double) (w.restFollowed[i] * lineMove + quietestPower());
                moment.gain = std::min (moment.gain, w.shownGains[i]);
            }

            mostTurnedDown = std::min (mostTurnedDown, moment.gain);
            done += length;
            finishMoment (length, onMoment);
        }
    }

    template <typename OnMoment>
    void finishMoment (int length, OnMoment& onMoment) noexcept
    {
        momentFilled += length;

        if (momentFilled >= momentLength)
        {
            moment.lineLevel = (float) (lineTotal / (double) momentFilled);
            onMoment (static_cast<const Moment&> (moment));
            moment = {};
            momentFilled = 0;
            lineTotal = 0.0;
        }
    }

    //==========================================================================
    /** True if the block about to come (digital silence) can be skipped
        through with rest(), ending up exactly where working through it would:
        nothing is fading or turned down, and the last sound has left the
        look-ahead, the followers and the filters. */
    bool canRest() const noexcept
    {
        for (auto* smoothed : { &smoothedBypass, &smoothedListen, &smoothedShelf, &smoothedDip, &smoothedWhole })
            if (smoothed->isSmoothing())
                return false;

        if (std::abs (errorA) > 0.0f || std::abs (errorB) > 0.0f || targetNow < 1.0f)
            return false;

        if (quietSamples < lookAhead || bandFollower > 1.0e-15f || restFollower > 1.0e-15f)
            return false;

        for (auto* filters : allFilters())
            for (auto& f : *filters)
                if (! f.hasFallenSilent())
                    return false;

        return true;
    }

    template <typename OnMoment>
    void rest (int numSamples, OnMoment& onMoment) noexcept
    {
        // (What was left in the filters and followers was under a billionth.)
        for (auto* filters : allFilters())
            for (auto& f : *filters)
                f.reset();

        bandFollower = restFollower = 0.0f;

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);

            for (int i = 0; i < length; ++i)   // (the same sum, the same way, as working through it)
                lineTotal += (double) (0.0f * lineFactor + quietestPower());

            done += length;
            finishMoment (length, onMoment);
        }
    }

    //==========================================================================
    /** A "state variable" filter: the design that stays smooth and stable
        even while it's being changed. */
    struct Coefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;
    };

    /** A high shelf, as a state variable filter plus how to mix its parts. */
    struct ShelfCoefficients
    {
        Coefficients c;
        float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

        /** Everything above the frequency 'warped' stands for turned down (or
            up) to this gain, reaching half way (in dB) at the frequency itself.
            At a gain of exactly 1 it's exactly "no change". */
        static forcedinline ShelfCoefficients forGain (float gain, float warped) noexcept
        {
            const float A = std::sqrt (gain);          // (A x A = the gain at the top)
            const float g = warped * std::sqrt (A);
            const float k = 1.41421356f;               // (the smooth, standard shape)
            ShelfCoefficients s;
            s.c.a1 = 1.0f / (1.0f + g * (g + k));
            s.c.a2 = g * s.c.a1;
            s.c.a3 = g * s.c.a2;
            s.c.k  = k;
            s.m0 = A * A;
            s.m1 = k * (1.0f - A) * A;
            s.m2 = 1.0f - A * A;
            return s;
        }
    };

    struct Filter
    {
        float s1 = 0.0f, s2 = 0.0f;

        forcedinline void tick (float x, const Coefficients& c, float& v1, float& v2) noexcept
        {
            const float v3 = x - s2;
            v1 = c.a1 * s1 + c.a2 * v3;
            v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
        }

        forcedinline float shelf (float x, const ShelfCoefficients& s) noexcept
        {
            float v1, v2;
            tick (x, s.c, v1, v2);
            return s.m0 * x + s.m1 * v1 + s.m2 * v2;
        }

        void reset() noexcept   { s1 = s2 = 0.0f; }

        /** True once it's stopped ringing (to under a billionth: -180 dB). */
        bool hasFallenSilent() const noexcept   { return std::abs (s1) < 1.0e-9f && std::abs (s2) < 1.0e-9f; }
    };

    using FilterSet = std::array<Filter, (size_t) maxChannels>;

    std::array<FilterSet*, 6> allFilters() noexcept
    {
        return { &firstSplits, &lowSplits, &highSplits, &upperSplits, &shelfFilters, &dipFilters };
    }

    std::array<const FilterSet*, 6> allFilters() const noexcept
    {
        return { &firstSplits, &lowSplits, &highSplits, &upperSplits, &shelfFilters, &dipFilters };
    }

    /** tan (pi f / fs): a frequency, the way the filters need it. */
    float warp (float hz) const noexcept
    {
        return (float) std::tan (juce::MathConstants<double>::pi * std::min ((double) hz, 0.45 * sampleRate) / sampleRate);
    }

    static Coefficients coefficientsFor (float warped, float k) noexcept
    {
        Coefficients c;
        c.a1 = 1.0f / (1.0f + warped * (warped + k));
        c.a2 = warped * c.a1;
        c.a3 = warped * c.a2;
        c.k  = k;
        return c;
    }

    float timeToCoefficient (float milliseconds) const noexcept
    {
        constexpr double timeConstantsTo99Percent = 6.6384;   // two smoothing steps in a row: (1 + t) e^-t = 1% at t = 6.64
        const double samples = std::max (0.25, (double) milliseconds * 0.001 * sampleRate / timeConstantsTo99Percent);
        return (float) std::exp (-1.0 / samples);
    }

    //==========================================================================
    static constexpr float kneeDb = 6.0f;
    static constexpr float decibelsPerOctaveOfPower = 3.01029996f;   // 10 x log10 (2)
    static constexpr float arrivedDistance = 1.0e-6f;
    static constexpr int shelfMode = 0, dipMode = 1, wholeMode = 2;

    double sampleRate = 44100.0;
    int lookAhead = 88, momentLength = 220;

    std::vector<float> delayLines;
    int delayPosition = 0;

    // The harsh band
    bool aboveOnly = true;
    float shelfWarp = 0.0f, bellK = 1.0f;
    Coefficients splitFirst, splitSecond, upperEdge, bell;
    ShelfCoefficients restingShelf;
    FilterSet firstSplits, lowSplits, highSplits, upperSplits, shelfFilters, dipFilters;

    // Listening, deciding and smoothing
    float followUp = 0.0f, followDown = 0.0f, bandFollower = 0.0f, restFollower = 0.0f;
    float lineFactor = 0.25f, maxReductionDb = 8.0f;
    float attackCoefficient = 0.0f, releaseCoefficient = 0.0f;
    float targetNow = 1.0f, errorA = 0.0f, errorB = 0.0f;
    int currentMode = shelfMode;

    juce::SmoothedValue<float> smoothedBypass, smoothedListen, smoothedShelf, smoothedDip, smoothedWhole;

    Settings previousSettings;
    bool hasPreviousSettings = false;

    Moment moment;
    int momentFilled = 0;
    double lineTotal = 0.0;

    Work work;
    int quietSamples = 0;
    float mostTurnedDown = 1.0f;
    bool resting = false, restingAllowed = true;
};

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
                  the rest - and, half as much, how loud your sound has been
                  over the last moment (300 ms), so a loud crash cymbal stands
                  out even when the kick hits with it. The line never goes
                  lower than -60 dB, so background hiss alone never counts.
                  Wherever the harsh band goes over the line, it's turned down
                  by that much, never more than RANGE.
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
    the parts that carry on from one sample to the next, four at once for the
    rest. In stereo even the filters work four at once (both channels, two
    filters each), with exactly the same sums as one at a time. And in
    digital silence it skips straight through (see rest()).
*/
class DeEsser
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
        float thresholdDb = -8.0f;      // how loud the harsh band may get compared with the rest (dB)
        float rangeDb     = 6.0f;       // the most it turns down (dB)
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
        float lineLevel  = 0.0f;   // what the THRESHOLD line follows (see below), on average
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
        followSlow = (float) std::exp (-1.0 / (0.3 * sampleRate));
        slowDecay.setBase ((double) followSlow);

        // It turns down over the 2 ms it looks ahead (so it's all the way down
        // just as each S arrives) and lets go over 40 ms, along the S-shaped curve.
        attackCoefficient  = timeToCoefficient ((float) (lookAheadSeconds * 1000.0));
        releaseCoefficient = timeToCoefficient (40.0f);

        for (auto* smoothed : { &smoothedBypass, &smoothedListen, &smoothedShelf, &smoothedDip, &smoothedWhole })
            smoothed->reset (sampleRate, 0.02);

        hasPreviousSettings = false;
        setSettings (initialSettings, true);

        bandFollower = restFollower = slowFollower = 0.0f;
        targetNow = 1.0f;
        errorA = errorB = 0.0f;
        clearFilters();

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

    /** Where the THRESHOLD line is (dB), when what it follows is at followsDb
        (a Moment's lineLevel): thresholdDb above that (or below, if it's
        negative), and never lower than -60 dB. It turns down wherever the
        harsh band goes over this. */
    static float thresholdLineDb (float followsDb, float thresholdDb) noexcept
    {
        return 10.0f * std::log10 (std::pow (10.0f, (followsDb + thresholdDb) * 0.1f) + quietestPower());
    }

private:
    //==========================================================================
    /** Room to work in, for one run of samples. */
    struct Work
    {
        static constexpr int size = 64;   // samples per run (a multiple of 4)

        float bandFollowed[size] {}, restFollowed[size] {}, slowFollowed[size] {}, lineFollowed[size] {}, sound[size] {}, targets[size] {},
              gains[size] {}, bypass[size] {}, listen[size] {}, outGains[size] {}, originalGains[size] {}, shownGains[size] {},
              shelfWeights[size] {}, dipWeights[size] {}, wholeWeights[size] {};

        // the shelf's settings at each sample, while it's turning down
        float shelfA1[size] {}, shelfA2[size] {}, shelfA3[size] {}, shelfM0[size] {}, shelfM1[size] {}, shelfM2[size] {};

        float original[maxChannels][size] {};
    };

    template <typename OnMoment>
    void processRun (float* const* channels, int numChannels, int start, int numSamples, OnMoment& onMoment) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numInFours = FastMath::roundUpToFour (numSamples);

        // 1. LISTEN - the harsh band and the rest, as heard now (before the delay)
        if (numChannels == 2)        hearStereo (channels, start, numSamples);
        else if (numChannels == 1)   hearEach<1> (channels, 1, start, numSamples);
        else                         hearEach<0> (channels, numChannels, start, numSamples);

        // 2. DECIDE - how far over the THRESHOLD line is the harsh band? (Four
        //    samples at once.) Over it, it's turned down by that much - easing
        //    in over a 6 dB "knee" - but never more than RANGE.
        float mostReduction = 0.0f;
        {
            const auto floor = Vec4::broadcast (quietestPower()), tiny = Vec4::broadcast (1.0e-30f);
            const auto lineMove = Vec4::broadcast (lineFactor), range = Vec4::broadcast (maxReductionDb);
            const auto halfKnee = Vec4::broadcast (kneeDb * 0.5f), zero = Vec4::broadcast (0.0f);
            const auto dbPerOctave = Vec4::broadcast (decibelsPerOctaveOfPower), twiceKnee = Vec4::broadcast (2.0f * kneeDb);
            auto most = zero;

            for (int i = 0; i < numInFours; i += 4)
            {
                const auto band = Vec4::load (w.bandFollowed + i), rest = Vec4::load (w.restFollowed + i);
                (band + rest).store (w.sound + i);

                // (what the line follows: half way, in dB, between the rest and the recent level)
                const auto follows = sqrt (rest * Vec4::load (w.slowFollowed + i));
                follows.store (w.lineFollowed + i);
                const auto line = follows * lineMove + floor;
                const auto overDb = dbPerOctave * FastMath::log2 (max (band, tiny) / line);

                const auto intoKnee = overDb + halfKnee;
                const auto eased = intoKnee * intoKnee / twiceKnee;
                const auto reduction = min (range, Vec4::select (overDb <= -halfKnee, zero, Vec4::select (overDb >= halfKnee, overDb, eased)));

                most = max (most, reduction);
                FastMath::decibelsToGain (-reduction, -1000.0f).store (w.targets + i);
            }

            mostReduction = FastMath::largestOf (most);   // (0: every target is exactly 1)
        }

        // 3. SMOOTH - quickly but smoothly, one sample after another
        const bool untouched = smooth (numSamples, mostReduction > 0.0f);

        // 4. APPLY - the sound is delayed by the look-ahead (a copy kept for
        //    LISTEN and BYPASS)...
        const bool mixing = smoothedBypass.isSmoothing() || smoothedListen.isSmoothing()
                         || smoothedBypass.getTargetValue() > 0.0f || smoothedListen.getTargetValue() > 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            delay (channels[ch] + start, numSamples, delayLines.data() + (size_t) ch * (size_t) lookAhead);

            if (mixing)
                juce::FloatVectorOperations::copy (w.original[ch], channels[ch] + start, numSamples);
        }

        delayPosition = (delayPosition + numSamples) % lookAhead;

        //    ...then turned down (while it isn't, the filters still follow
        //    the sound, but it isn't changed at all)...
        turnDown (channels, numChannels, start, numSamples, untouched);

        //    ...and LISTEN fades over to just what's been taken out; BYPASS fades
        //    to your original sound (2 ms later, like everything else).
        const FastMath::KnobRun bypass (smoothedBypass, w.bypass, numSamples);
        const FastMath::KnobRun listen (smoothedListen, w.listen, numSamples);
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
        power, the average over the channels.

        The split at the lower edge is two filters in a row. The band's upper
        edge (TO) is one more filter, which hears what was above the lower edge
        a sample earlier (a 50,000th of a second: nothing anyone could hear,
        and it means the first and last filters can work at the same time). */
    template <int fixedChannels>
    void hearEach (float* const* channels, int numChannels, int start, int numSamples) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numChannels;
        auto& w = work;

        Filter firsts[capacity], lows[capacity], highs[capacity], tops[capacity];
        float aboves[capacity];

        for (int ch = 0; ch < count; ++ch)
        {
            firsts[ch] = firstSplits[(size_t) ch];
            lows[ch]   = lowSplits[(size_t) ch];
            highs[ch]  = highSplits[(size_t) ch];
            tops[ch]   = upperSplits[(size_t) ch];
            aboves[ch] = previousAbove[(size_t) ch];
        }

        const Coefficients first = splitFirst, second = splitSecond, top = upperEdge;
        const bool wholeTop = aboveOnly;
        const float scale = 1.0f / (float) count;
        Followers followers { bandFollower, restFollower, slowFollower, followUp, followDown, followSlow };

        for (int i = 0; i < numSamples; ++i)
        {
            float bandPower = 0.0f, restPower = 0.0f;

            for (int ch = 0; ch < count; ++ch)
            {
                const float x = channels[ch][start + i];

                float a1, a2, t1, upTo;
                firsts[ch].tick (x, first, a1, a2);                // the first half of the split
                tops[ch].tick (aboves[ch], top, t1, upTo);         // (and the band up to TO)
                const float lowHalf = a2, highHalf = x - first.k * a1 - a2;

                float r1, rest, h1, h2;
                lows[ch].tick (lowHalf, second, r1, rest);         // the second half: everything below the harsh band...
                highs[ch].tick (highHalf, second, h1, h2);         // ...and everything above its lower edge
                aboves[ch] = highHalf - second.k * h1 - h2;

                const float band = wholeTop ? aboves[ch] : upTo;
                bandPower += band * band;
                restPower += rest * rest;
            }

            followers.follow (bandPower * scale, restPower * scale, w, i);
        }

        for (int ch = 0; ch < count; ++ch)
        {
            firstSplits[(size_t) ch] = firsts[ch];
            lowSplits[(size_t) ch]   = lows[ch];
            highSplits[(size_t) ch]  = highs[ch];
            upperSplits[(size_t) ch] = tops[ch];
            previousAbove[(size_t) ch] = aboves[ch];
        }

        bandFollower = followers.band;
        restFollower = followers.rest;
        slowFollower = followers.slow;
    }

    /** The same for stereo, four filters at once: the first half of the split
        and the upper edge, for both channels; then the second half (below
        and above the edge), for both channels. */
    void hearStereo (float* const* channels, int start, int numSamples) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const float* const left  = channels[0] + start;
        const float* const right = channels[1] + start;

        const Coefficients first = splitFirst, second = splitSecond, top = upperEdge;
        const auto a1A = Vec4::pairOf (first.a1, top.a1), a2A = Vec4::pairOf (first.a2, top.a2);
        const auto a3A = Vec4::pairOf (first.a3, top.a3), kA = Vec4::pairOf (first.k, top.k);
        const auto a1B = Vec4::broadcast (second.a1), a2B = Vec4::broadcast (second.a2);
        const auto a3B = Vec4::broadcast (second.a3), kB = Vec4::broadcast (second.k);
        const auto two = Vec4::broadcast (2.0f), half = Vec4::broadcast (0.5f);
        const auto useAbove = Vec4::broadcast (aboveOnly ? 1.0f : 0.0f) > half;
        Followers followers { bandFollower, restFollower, slowFollower, followUp, followDown, followSlow };

        Vec4 s1A = Vec4::of (firstSplits[0].s1, firstSplits[1].s1, upperSplits[0].s1, upperSplits[1].s1);
        Vec4 s2A = Vec4::of (firstSplits[0].s2, firstSplits[1].s2, upperSplits[0].s2, upperSplits[1].s2);
        Vec4 s1B = Vec4::of (lowSplits[0].s1, lowSplits[1].s1, highSplits[0].s1, highSplits[1].s1);
        Vec4 s2B = Vec4::of (lowSplits[0].s2, lowSplits[1].s2, highSplits[0].s2, highSplits[1].s2);
        Vec4 aboves = Vec4::of (0.0f, 0.0f, previousAbove[0], previousAbove[1]);

        for (int i = 0; i < numSamples; ++i)
        {
            // { left, right, what was above the edge (left, right) }
            const Vec4 inA = Vec4::lowerThenUpper (Vec4::of (left[i], right[i], 0.0f, 0.0f), aboves);
            const Vec4 v3A = inA - s2A;
            const Vec4 v1A = a1A * s1A + a2A * v3A;
            const Vec4 v2A = s2A + a2A * s1A + a3A * v3A;
            s1A = two * v1A - s1A;
            s2A = two * v2A - s2A;

            // { low half (left, right), high half (left, right) }
            const Vec4 inB = Vec4::lowerHalves (v2A, inA - kA * v1A - v2A);
            const Vec4 v3B = inB - s2B;
            const Vec4 v1B = a1B * s1B + a2B * v3B;
            const Vec4 v2B = s2B + a2B * s1B + a3B * v3B;
            s1B = two * v1B - s1B;
            s2B = two * v2B - s2B;
            aboves = inB - kB * v1B - v2B;

            // { the rest (left, right), the harsh band (left, right) }, squared and added up (the same sums as hearEach)
            const Vec4 parts = Vec4::lowerThenUpper (v2B, Vec4::select (useAbove, aboves, v2A));
            const Vec4 squares = parts * parts;
            followers.follow ((squares.get<2>() + squares.get<3>()) * 0.5f, (squares.get<0>() + squares.get<1>()) * 0.5f, w, i);
        }

        bandFollower = followers.band;
        restFollower = followers.rest;
        slowFollower = followers.slow;

        firstSplits[0] = { s1A.get<0>(), s2A.get<0>() };
        firstSplits[1] = { s1A.get<1>(), s2A.get<1>() };
        upperSplits[0] = { s1A.get<2>(), s2A.get<2>() };
        upperSplits[1] = { s1A.get<3>(), s2A.get<3>() };
        lowSplits[0]   = { s1B.get<0>(), s2B.get<0>() };
        lowSplits[1]   = { s1B.get<1>(), s2B.get<1>() };
        highSplits[0]  = { s1B.get<2>(), s2B.get<2>() };
        highSplits[1]  = { s1B.get<3>(), s2B.get<3>() };
        previousAbove[0] = aboves.get<2>();
        previousAbove[1] = aboves.get<3>();
    }

    /** ...followed: up at once, down over ~12 ms (both the same way, so they
        compare fairly). And the whole sound's recent level, which moves
        slowly (over 300 ms). (Worked out as each sample is heard, so it runs
        alongside the filters.) */
    struct Followers
    {
        float band, rest, slow;
        const float up, down, slowly;

        forcedinline void follow (float bandPower, float restPower, Work& w, int i) noexcept
        {
            band = bandPower + (bandPower > band ? up : down) * (band - bandPower);
            rest = restPower + (restPower > rest ? up : down) * (rest - restPower);
            const float total = bandPower + restPower;
            slow = total + slowly * (slow - total);
            w.bandFollowed[i] = band;
            w.restFollowed[i] = rest;
            w.slowFollowed[i] = slow;
        }
    };

    /** Step 3: the gain glides towards each sample's target along the S-shaped
        curve - quickly when it needs to turn down more, a little more gently
        when it's letting go. (As in Easy Gate, it's kept as how far it still
        has to go, so it arrives exactly - at a gain of exactly 1 when it isn't
        turning anything down, so your sound is untouched.) Returns true if
        the gain stayed at exactly 1 all the way through. */
    bool smooth (int numSamples, bool anyTargetBelowOne) noexcept
    {
        auto& w = work;

        if (! anyTargetBelowOne && ! (std::abs (errorA) > 0.0f) && ! (std::abs (errorB) > 0.0f) && ! (targetNow < 1.0f))
        {
            // (Nothing to turn down, and nothing still letting go: most of the time.)
            juce::FloatVectorOperations::fill (w.gains, 1.0f, numSamples);
            return true;
        }

        float a = errorA, b = errorB, target = targetNow;
        float lowest = 1.0f, highest = 1.0f;
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

            const float gain = target + b;
            w.gains[i] = gain;
            lowest  = std::min (lowest, gain);
            highest = std::max (highest, gain);
        }

        errorA = a;
        errorB = b;
        targetNow = target;
        return ! (lowest < 1.0f) && ! (highest > 1.0f);
    }

    //==========================================================================
    /** Step 4, the turning down itself, one sample after another. All the
        ways of turning down always follow the sound (each one exactly "no
        change" while it isn't turning anything down), so fading from one to
        another is seamless. */
    void turnDown (float* const* channels, int numChannels, int start, int numSamples, bool untouched) noexcept
    {
        auto& w = work;
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

        const bool shelving = ! untouched && (fading || currentMode == shelfMode);

        if (shelving)
            shelvesForRun (numSamples);

        if (numChannels == 2)        turnDownStereo (channels, start, numSamples, untouched, shelving, fading);
        else if (numChannels == 1)   turnDownEach<1> (channels, 1, start, numSamples, untouched, shelving, fading);
        else                         turnDownEach<0> (channels, numChannels, start, numSamples, untouched, shelving, fading);
    }

    /** The shelf's settings for each sample's gain, four samples at once.
        (At a gain of exactly 1, exactly restingShelf.) */
    void shelvesForRun (int numSamples) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const auto warped = Vec4::broadcast (shelfWarp), k = Vec4::broadcast (ShelfCoefficients::k);
        const auto one = Vec4::broadcast (1.0f), zero = Vec4::broadcast (0.0f);

        for (int i = 0; i < FastMath::roundUpToFour (numSamples); i += 4)
        {
            const auto A = sqrt (max (Vec4::load (w.gains + i), zero));   // (the same sums as ShelfCoefficients::forGain)
            const auto g = warped * sqrt (A);
            const auto a1 = one / (one + g * (g + k));
            const auto a2 = g * a1;
            a1.store (w.shelfA1 + i);
            a2.store (w.shelfA2 + i);
            (g * a2).store (w.shelfA3 + i);
            (A * A).store (w.shelfM0 + i);
            (k * (one - A) * A).store (w.shelfM1 + i);
            (one - A * A).store (w.shelfM2 + i);
        }
    }

    /** What comes out, from the sound (x) and each way of turning it down. */
    static forcedinline float mixOut (float x, float shelved, float dipped, float g, int mode, bool fading, const Work& w, int i) noexcept
    {
        if (! fading)
            return mode == shelfMode ? shelved : (mode == dipMode ? dipped : x * g);

        // (written as changes, so it's still exactly x while nothing is turned down)
        return x + w.shelfWeights[i] * (shelved - x) + w.dipWeights[i] * (dipped - x) + w.wholeWeights[i] * (x * g - x);
    }

    template <int fixedChannels>
    void turnDownEach (float* const* channels, int numChannels, int start, int numSamples, bool untouched, bool shelving, bool fading) noexcept
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

        const ShelfCoefficients still = restingShelf;
        const Coefficients d = bell;
        const float k = bellK;
        const int mode = currentMode;

        for (int i = 0; i < numSamples; ++i)
        {
            ShelfCoefficients shelf = still;

            if (shelving)
            {
                shelf.c.a1 = w.shelfA1[i];  shelf.c.a2 = w.shelfA2[i];  shelf.c.a3 = w.shelfA3[i];
                shelf.m0   = w.shelfM0[i];  shelf.m1   = w.shelfM1[i];  shelf.m2   = w.shelfM2[i];
            }

            const float g = w.gains[i];
            const float dipBy = (g - 1.0f) * k;

            for (int ch = 0; ch < count; ++ch)
            {
                float* const sample = channels[ch] + start + i;
                const float x = *sample;

                float v1, v2, b1, b2;
                shelves[ch].tick (x, shelf.c, v1, v2);
                dips[ch].tick (x, d, b1, b2);

                if (! untouched)
                {
                    const float shelved = shelf.m0 * x + shelf.m1 * v1 + shelf.m2 * v2;
                    const float dipped  = x + dipBy * b1;
                    *sample = mixOut (x, shelved, dipped, g, mode, fading, w, i);
                }
            }
        }

        for (int ch = 0; ch < count; ++ch)
        {
            shelfFilters[(size_t) ch] = shelves[ch];
            dipFilters[(size_t) ch]   = dips[ch];
        }
    }

    /** The same for stereo, four filters at once: the shelf for both channels
        and the dip for both channels. */
    void turnDownStereo (float* const* channels, int start, int numSamples, bool untouched, bool shelving, bool fading) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        float* const left  = channels[0] + start;
        float* const right = channels[1] + start;

        const ShelfCoefficients still = restingShelf;
        const Coefficients d = bell;
        const float k = bellK;
        const int mode = currentMode;
        const auto two = Vec4::broadcast (2.0f);

        Vec4 s1 = Vec4::of (shelfFilters[0].s1, shelfFilters[1].s1, dipFilters[0].s1, dipFilters[1].s1);
        Vec4 s2 = Vec4::of (shelfFilters[0].s2, shelfFilters[1].s2, dipFilters[0].s2, dipFilters[1].s2);

        const auto stillA1 = Vec4::pairOf (still.c.a1, d.a1), stillA2 = Vec4::pairOf (still.c.a2, d.a2), stillA3 = Vec4::pairOf (still.c.a3, d.a3);

        if (untouched)
        {
            // (The filters just follow the sound, ready for the next S.)
            for (int i = 0; i < numSamples; ++i)
            {
                const Vec4 x = Vec4::of (left[i], right[i], left[i], right[i]);
                const Vec4 v3 = x - s2;
                const Vec4 v1 = stillA1 * s1 + stillA2 * v3;
                const Vec4 v2 = s2 + stillA2 * s1 + stillA3 * v3;
                s1 = two * v1 - s1;
                s2 = two * v2 - s2;
            }
        }
        else for (int i = 0; i < numSamples; ++i)
        {
            const float xl = left[i], xr = right[i];
            const Vec4 x = Vec4::of (xl, xr, xl, xr);

            const Vec4 a1 = shelving ? Vec4::pairOf (w.shelfA1[i], d.a1) : stillA1;
            const Vec4 a2 = shelving ? Vec4::pairOf (w.shelfA2[i], d.a2) : stillA2;
            const Vec4 a3 = shelving ? Vec4::pairOf (w.shelfA3[i], d.a3) : stillA3;

            const Vec4 v3 = x - s2;
            const Vec4 v1 = a1 * s1 + a2 * v3;
            const Vec4 v2 = s2 + a2 * s1 + a3 * v3;
            s1 = two * v1 - s1;
            s2 = two * v2 - s2;

            const float g = w.gains[i];
            const float m0 = shelving ? w.shelfM0[i] : still.m0;
            const float m1 = shelving ? w.shelfM1[i] : still.m1;
            const float m2 = shelving ? w.shelfM2[i] : still.m2;

            // { shelved (left, right), dipped (left, right) }
            const Vec4 y = Vec4::pairOf (m0, 1.0f) * x + Vec4::pairOf (m1, (g - 1.0f) * k) * v1 + Vec4::pairOf (m2, 0.0f) * v2;

            left[i]  = mixOut (xl, y.get<0>(), y.get<2>(), g, mode, fading, w, i);
            right[i] = mixOut (xr, y.get<1>(), y.get<3>(), g, mode, fading, w, i);
        }

        shelfFilters[0] = { s1.get<0>(), s2.get<0>() };
        shelfFilters[1] = { s1.get<1>(), s2.get<1>() };
        dipFilters[0]   = { s1.get<2>(), s2.get<2>() };
        dipFilters[1]   = { s1.get<3>(), s2.get<3>() };
    }

    /** The numbers added up (four at a time, then the last few). */
    static float sumOf (const float* numbers, int count) noexcept
    {
        using FastMath::Vec4;
        auto total = Vec4::broadcast (0.0f);
        int i = 0;

        for (; i + 4 <= count; i += 4)
            total = total + Vec4::load (numbers + i);

        float sum = (total.get<0>() + total.get<1>()) + (total.get<2>() + total.get<3>());

        for (; i < count; ++i)
            sum += numbers[i];

        return sum;
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

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);

            moment.soundLevel = std::max (moment.soundLevel, juce::FloatVectorOperations::findMaximum (w.sound + done, length));
            moment.bandLevel  = std::max (moment.bandLevel, juce::FloatVectorOperations::findMaximum (w.bandFollowed + done, length));
            moment.gain       = std::min (moment.gain, juce::FloatVectorOperations::findMinimum (w.shownGains + done, length));
            lineTotal += (double) sumOf (w.lineFollowed + done, length);

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
        // (What was left in the filters and followers was under a billionth.
        // The recent level fades away just as it would have.)
        clearFilters();
        bandFollower = restFollower = 0.0f;
        slowFollower *= (float) slowDecay (numSamples);

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);
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
        static constexpr float k = 1.41421356f;   // (the smooth, standard shape)

        Coefficients c;
        float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f;

        /** Everything above the frequency 'warped' stands for turned down (or
            up) to this gain, reaching half way (in dB) at the frequency itself.
            At a gain of exactly 1 it's exactly "no change". */
        static ShelfCoefficients forGain (float gain, float warped) noexcept
        {
            const float A = std::sqrt (std::max (gain, 0.0f));   // (A x A = the gain at the top)
            const float g = warped * std::sqrt (A);
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

    void clearFilters() noexcept
    {
        for (auto* filters : allFilters())
            for (auto& f : *filters)
                f.reset();

        previousAbove.fill (0.0f);
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
    std::array<float, (size_t) maxChannels> previousAbove {};

    // Listening, deciding and smoothing
    float followUp = 0.0f, followDown = 0.0f, followSlow = 0.0f, bandFollower = 0.0f, restFollower = 0.0f, slowFollower = 0.0f;
    FastMath::RepeatedPower slowDecay;
    float lineFactor = 0.16f, maxReductionDb = 6.0f;
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

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "FastMath.h"

//==============================================================================
/**
    The gate itself: the part that changes the sound. No knobs, windows or
    DAW plumbing in here - just the maths.

    A gate lets your sound through while it's loud and turns it down in
    between, so a tom mic only lets you hear the toms (not the rest of the
    kit bleeding into it), and a guitar amp's hiss and hum disappear
    between phrases.

    What happens to every single sample:

      1. LISTEN   What does the gate hear? Usually your track itself; with
                  OPENED BY: SIDECHAIN, another track sent to it instead.
                  IGNORE BELOW and IGNORE ABOVE filter what it hears (never
                  your sound itself), so for example a kick bleeding into
                  the snare mic doesn't open the snare's gate.
      2. DECIDE   Open or closed? It opens the moment what it hears goes over
                  THRESHOLD. Once that has dropped a little below again
                  (4 dB, so a sound hovering around the threshold doesn't make
                  it flutter) and HOLD has passed, it closes.
      3. SMOOTH   It doesn't snap open and shut (that would click): it opens
                  over the ATTACK time and closes over the RELEASE time,
                  along a smooth S-shaped curve.
      4. APPLY    Your sound passes through untouched while it's open, and is
                  turned down by RANGE while it's closed - all the way to
                  silence at FULL.

    It looks 2 ms ahead: your sound is delayed by 2 ms (your DAW makes up for
    that by itself, so everything stays in time), while the gate listens
    without the delay. So it's already open when each hit arrives, and the
    very start of the hit - the stick or the beater - is never cut off.

    Both channels always get exactly the same change, so the stereo image
    stays put. While it's fully open your sound comes out bit for bit as it
    went in (just 2 ms later), and with RANGE at FULL the closed gate gives
    true digital silence, so any effects after it can rest too.

    Organised like the other effects, to use as little of your computer as
    possible: a short run of samples at a time (64), sample after sample for
    the parts that carry on from one sample to the next, four samples at once
    for the rest. And in digital silence, once everything has died away, it
    skips straight through (see rest()).
*/
class Gate
{
public:
    static constexpr float  silenceDb        = -120.0f;   // how "silence" is written in dB here
    static constexpr float  lowCutOffHz      = 20.0f;     // IGNORE BELOW at or under this = off
    static constexpr float  highCutOffHz     = 20000.0f;  // IGNORE ABOVE at or over this = off
    static constexpr float  fullRangeDb      = 80.0f;     // RANGE at or over this = FULL: silence while closed
    static constexpr float  hysteresisDb     = 4.0f;      // it closes this far below THRESHOLD
    static constexpr double lookAheadSeconds = 0.002;     // how far ahead it looks
    static constexpr double windowSeconds    = 0.010;     // "how loud right now" = the loudest of the last 10 ms
    static constexpr int    maxChannels      = 8;

    /** The knob and button settings. */
    struct Settings
    {
        float thresholdDb = -40.0f;    // -80 .. 0 dB
        float rangeDb     = 40.0f;     //   0 .. 80 dB (80 = FULL: silence)
        float attackMs    = 0.5f;      // 0.1 .. 50 ms
        float holdMs      = 50.0f;     //   0 .. 2000 ms
        float releaseMs   = 150.0f;    //   5 .. 2000 ms
        float lowCutHz    = 20.0f;     //  20 (off) .. 5000 Hz  - IGNORE BELOW
        float highCutHz   = 20000.0f;  //  50 .. 20000 (off) Hz - IGNORE ABOVE
        bool  listen      = false;     // LISTEN: hear what the gate hears, instead of your sound
        bool  bypassed    = false;     // BYPASS: let the original sound straight through
    };

    /** A few milliseconds of what happened, for the display. */
    struct Moment
    {
        float keyLevel = 0.0f;   // the loudest the gate heard (as a gain: 1 = 0 dB)
        float outLevel = 0.0f;   // the loudest coming out
        float gain     = 0.0f;   // how far open it got (1 = fully open; BYPASS counts as open)
        bool  open     = false;  // open (or holding) at some point
    };

    /** Where things stood at the end of a block of audio, for the light. */
    struct BlockResult
    {
        bool  open = false;      // open (or holding)
        float gain = 1.0f;       // how far open (1 = fully)
    };

    //==========================================================================
    /** Call before audio starts, and whenever the sample rate changes.
        momentLength: how many samples each Moment for the display covers. */
    void prepare (double newSampleRate, int newMomentLength, const Settings& initialSettings)
    {
        sampleRate   = newSampleRate;
        lookAhead    = std::max (1, (int) std::lround (lookAheadSeconds * sampleRate));
        window       = std::max (lookAhead, (int) std::lround (windowSeconds * sampleRate));
        momentLength = std::max (1, newMomentLength);

        delayLines.assign ((size_t) (maxChannels * lookAhead), 0.0f);
        delayPosition = 0;

        windowValues.assign ((size_t) window, 0.0f);
        windowSuffix.assign ((size_t) window + 1, 0.0f);
        windowCleared = false;
        clearWindow();

        // BYPASS and LISTEN fade over 20 ms instead of switching abruptly.
        smoothedBypass.reset (sampleRate, 0.02);
        smoothedListen.reset (sampleRate, 0.02);

        gateOpen = false;
        holdLeft = 0;
        hasPreviousSettings = false;
        setSettings (initialSettings, true);
        errorA = errorB = 0.0f;   // starting closed

        for (auto& filter : lowCutFilters)  filter.reset();
        for (auto& filter : highCutFilters) filter.reset();

        moment = {};
        momentFilled = 0;
        quietSamples = 0;
    }

    /** Hands over the latest settings; call this once per block of audio.
        With jumpToNewValues = true the fades jump straight to the new setting. */
    void setSettings (const Settings& settings, bool jumpToNewValues = false)
    {
        // (Anything worked out from a knob is only worked out again when that knob has moved.)
        const bool fresh = ! hasPreviousSettings;
        const auto moved = [fresh] (float now, float before) { return fresh || now < before || now > before; };

        if (moved (settings.thresholdDb, previousSettings.thresholdDb))
        {
            openLevel  = juce::Decibels::decibelsToGain (settings.thresholdDb, -1000.0f);
            closeLevel = juce::Decibels::decibelsToGain (settings.thresholdDb - hysteresisDb, -1000.0f);
        }

        if (moved (settings.rangeDb, previousSettings.rangeDb))
        {
            const float newClosedGain = closedGainFor (settings.rangeDb);

            // While it's closed, the gain is measured from the closed level, so
            // moving RANGE glides there (at the RELEASE speed) instead of jumping.
            if (! gateOpen && ! fresh)
            {
                errorA += closedGain - newClosedGain;
                errorB += closedGain - newClosedGain;
            }

            closedGain = newClosedGain;
        }

        // ATTACK, HOLD and RELEASE only change how it moves from one sample to
        // the next, so turning them can't click - they need no fade of their own.
        if (moved (settings.attackMs, previousSettings.attackMs))
            attackCoefficient = timeToCoefficient (settings.attackMs);

        if (moved (settings.releaseMs, previousSettings.releaseMs))
            releaseCoefficient = timeToCoefficient (settings.releaseMs);

        // HOLD counts from when the sound it hears drops below the close level.
        // It hears that 2 ms early (the look-ahead), and its "right now" is the
        // loudest of the last 10 ms, so the count is adjusted to match: HOLD
        // then means how long it stays open after your sound itself has dropped.
        // (So it always stays open for at least 8 ms: that's what stops the
        // gaps between the waves of a low note from making it flutter.)
        if (moved (settings.holdMs, previousSettings.holdMs))
            holdSamples = std::max (0, (int) std::lround (settings.holdMs * 0.001 * sampleRate) + lookAhead - window);

        // IGNORE BELOW / ABOVE: 12 dB-per-octave filters on what it hears. Like
        // the timings they never touch the audio itself, so they can't click.
        const bool wantLowCut = settings.lowCutHz > lowCutOffHz + 0.5f;

        if (wantLowCut && (! lowCutActive || moved (settings.lowCutHz, previousSettings.lowCutHz)))
            lowCut = butterworth (settings.lowCutHz);
        else if (! wantLowCut && lowCutActive)
            for (auto& filter : lowCutFilters)
                filter.reset();   // start fresh next time it's switched on

        lowCutActive = wantLowCut;

        const bool wantHighCut = settings.highCutHz < highCutOffHz - 0.5f;

        if (wantHighCut && (! highCutActive || moved (settings.highCutHz, previousSettings.highCutHz)))
            highCut = butterworth (settings.highCutHz);
        else if (! wantHighCut && highCutActive)
            for (auto& filter : highCutFilters)
                filter.reset();

        highCutActive = wantHighCut;

        const float bypassTarget = settings.bypassed ? 1.0f : 0.0f;
        const float listenTarget = settings.listen ? 1.0f : 0.0f;

        if (jumpToNewValues)
        {
            smoothedBypass.setCurrentAndTargetValue (bypassTarget);
            smoothedListen.setCurrentAndTargetValue (listenTarget);
        }
        else
        {
            smoothedBypass.setTargetValue (bypassTarget);
            smoothedListen.setTargetValue (listenTarget);
        }

        previousSettings    = settings;
        hasPreviousSettings = true;
    }

    /** How many samples late the sound comes out (the look-ahead). Report this to the DAW. */
    int getLatencySamples() const noexcept    { return lookAhead; }

    /** Gates the audio in place. key: what the gate listens to, or nullptr
        to listen to the audio itself. onMoment (const Moment&) is called each
        time another Moment for the display is complete. */
    template <typename OnMoment>
    BlockResult process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>* key, OnMoment&& onMoment) noexcept
    {
        const int numSamples     = audio.getNumSamples();
        const int numChannels    = std::min (audio.getNumChannels(), maxChannels);
        const bool ownKey        = key == nullptr;
        const int numKeyChannels = ownKey ? numChannels : std::min (key->getNumChannels(), maxChannels);
        jassert (ownKey || key->getNumSamples() >= numSamples);

        const bool silent = FastMath::isSilent (audio) && (ownKey || FastMath::isSilent (*key));
        resting = restingAllowed && silent && canRest();

        if (resting)
        {
            rest (numSamples, onMoment);
        }
        else
        {
            float* const* channels = audio.getArrayOfWritePointers();
            const float* const* keyChannels = ownKey ? channels : key->getArrayOfReadPointers();

            for (int start = 0; start < numSamples; start += Work::size)
                processRun (channels, numChannels, keyChannels, numKeyChannels, start,
                            std::min (Work::size, numSamples - start), onMoment);
        }

        quietSamples = silent ? std::min (quietSamples + numSamples, 1 << 30) : 0;
        return { gateOpen, currentGain() };
    }

    /** The same, without the display's moments. */
    BlockResult process (juce::AudioBuffer<float>& audio, const juce::AudioBuffer<float>* key = nullptr) noexcept
    {
        return process (audio, key, [] (const Moment&) noexcept {});
    }

    /** Whether the last block was digital silence it could skip straight through. */
    bool isResting() const noexcept                     { return resting; }

    /** Skipping through silence is on unless this says otherwise (the tests
        use it to check that skipping gives the same sound as working through). */
    void allowResting (bool shouldAllow) noexcept       { restingAllowed = shouldAllow; }

    /** How far down a closed gate turns the sound, as a gain (0 at FULL). */
    static float closedGainFor (float rangeDb) noexcept
    {
        return rangeDb >= fullRangeDb ? 0.0f : juce::Decibels::decibelsToGain (-rangeDb);
    }

private:
    //==========================================================================
    /** Room to work in, for one run of samples. */
    struct Work
    {
        static constexpr int size = 64;   // samples per run (a multiple of 4)

        float key[size] {}, gains[size] {}, open[size] {}, bypass[size] {}, listen[size] {},
              soundGains[size] {}, keyGains[size] {}, shownGains[size] {};
        float keyAudio[maxChannels][size] {};
    };

    /** Steps 1 to 4 for numSamples (at most Work::size) samples from start. */
    template <typename OnMoment>
    void processRun (float* const* channels, int numChannels, const float* const* keyChannels, int numKeyChannels,
                     int start, int numSamples, OnMoment& onMoment) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numInFours = FastMath::roundUpToFour (numSamples);
        const bool listening = numKeyChannels > 0 && (smoothedListen.isSmoothing() || smoothedListen.getTargetValue() > 0.0f);

        // 1. LISTEN - how loud is what it hears, at each instant? (For LISTEN,
        //    it also keeps what it heard, to play it to you.)
        hear (keyChannels, numKeyChannels, start, numSamples, listening);

        // 2 and 3. DECIDE whether it's open, and SMOOTH the way there.
        decide (numSamples);

        // 4. APPLY - the sound is delayed by the look-ahead...
        for (int ch = 0; ch < numChannels; ++ch)
            delay (channels[ch] + start, numSamples, delayLines.data() + (size_t) ch * (size_t) lookAhead);

        delayPosition = (delayPosition + numSamples) % lookAhead;

        //    ...and turned down while the gate is closed. LISTEN fades over to
        //    what the gate hears; BYPASS fades to a gain of exactly 1: your
        //    original sound (2 ms later, like everything else).
        const FastMath::KnobRun bypass (smoothedBypass, w.bypass, numSamples);
        const FastMath::KnobRun listen (smoothedListen, w.listen, numSamples);
        const auto one = Vec4::broadcast (1.0f);

        for (int i = 0; i < numInFours; i += 4)
        {
            const auto gate = Vec4::load (w.gains + i), bypassed = bypass.at (i), listened = listen.at (i);
            (gate * (one - listened) * (one - bypassed) + bypassed).store (w.soundGains + i);
            (gate * (one - bypassed) + bypassed).store (w.shownGains + i);   // (for the display: BYPASS turns nothing down)

            if (listening)
                (listened * (one - bypassed)).store (w.keyGains + i);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            float* const samples = channels[ch] + start;
            juce::FloatVectorOperations::multiply (samples, w.soundGains, numSamples);

            if (listening)
                juce::FloatVectorOperations::addWithMultiply (samples, w.keyAudio[std::min (ch, numKeyChannels - 1)], w.keyGains, numSamples);
        }

        // And notes for the display.
        recordMoments (channels, numChannels, start, numSamples, onMoment);
    }

    //==========================================================================
    /** Step 1 for a run of samples: the key, filtered by IGNORE BELOW and
        ABOVE if they're on, and the louder of its channels at each instant. */
    void hear (const float* const* keyChannels, int numKeyChannels, int start, int numSamples, bool keepKeyAudio) noexcept
    {
        if (numKeyChannels == 0)
        {
            std::fill (work.key, work.key + numSamples, 0.0f);   // (nothing is coming in: it stays closed)
            return;
        }

        if (! lowCutActive && ! highCutActive)
        {
            hearUnfiltered (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
            return;
        }

        // (Mono and stereo, nearly always what's used, get their own versions.)
        if (numKeyChannels == 2)       hearFiltered<2> (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
        else if (numKeyChannels == 1)  hearFiltered<1> (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
        else                           hearFiltered<0> (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
    }

    template <int fixedChannels>
    void hearFiltered (const float* const* keyChannels, int numKeyChannels, int start, int numSamples, bool keepKeyAudio) noexcept
    {
        if (lowCutActive && highCutActive)  hearFiltered<fixedChannels, true, true>  (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
        else if (lowCutActive)              hearFiltered<fixedChannels, true, false> (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
        else                                hearFiltered<fixedChannels, false, true> (keyChannels, numKeyChannels, start, numSamples, keepKeyAudio);
    }

    void hearUnfiltered (const float* const* keyChannels, int numKeyChannels, int start, int numSamples, bool keepKeyAudio) noexcept
    {
        using FastMath::Vec4;
        auto& w = work;
        const int numFours = numSamples & ~3;   // (only reads samples that are really there)

        for (int i = 0; i < numFours; i += 4)
        {
            auto loudest = abs (Vec4::load (keyChannels[0] + start + i));

            for (int ch = 1; ch < numKeyChannels; ++ch)
                loudest = max (loudest, abs (Vec4::load (keyChannels[ch] + start + i)));

            loudest.store (w.key + i);
        }

        for (int i = numFours; i < numSamples; ++i)
        {
            float loudest = std::abs (keyChannels[0][start + i]);

            for (int ch = 1; ch < numKeyChannels; ++ch)
                loudest = std::max (loudest, std::abs (keyChannels[ch][start + i]));

            w.key[i] = loudest;
        }

        if (keepKeyAudio)
            for (int ch = 0; ch < numKeyChannels; ++ch)
                juce::FloatVectorOperations::copy (w.keyAudio[ch], keyChannels[ch] + start, numSamples);
    }

    template <int fixedChannels, bool withLowCut, bool withHighCut>
    void hearFiltered (const float* const* keyChannels, int numKeyChannels, int start, int numSamples, bool keepKeyAudio) noexcept
    {
        constexpr size_t capacity = fixedChannels > 0 ? (size_t) fixedChannels : (size_t) maxChannels;
        const int count = fixedChannels > 0 ? fixedChannels : numKeyChannels;
        auto& w = work;

        // Copies of the filters, close at hand
        KeyFilter lows[capacity], highs[capacity];

        for (int ch = 0; ch < count; ++ch)
        {
            lows[ch]  = lowCutFilters[(size_t) ch];
            highs[ch] = highCutFilters[(size_t) ch];
        }

        const auto lowCoefficients = lowCut, highCoefficients = highCut;

        for (int i = 0; i < numSamples; ++i)
        {
            float loudest = 0.0f;

            for (int ch = 0; ch < count; ++ch)
            {
                float sample = keyChannels[ch][start + i];

                if constexpr (withLowCut)   sample = lows[ch].highPass (sample, lowCoefficients);
                if constexpr (withHighCut)  sample = highs[ch].lowPass (sample, highCoefficients);

                if (keepKeyAudio)
                    w.keyAudio[ch][i] = sample;

                loudest = std::max (loudest, std::abs (sample));
            }

            w.key[i] = loudest;
        }

        for (int ch = 0; ch < count; ++ch)
        {
            lowCutFilters[(size_t) ch]  = lows[ch];
            highCutFilters[(size_t) ch] = highs[ch];
        }
    }

    //==========================================================================
    /** Steps 2 and 3, one sample after another: open or closed, and the
        smooth way from one to the other.

        "How loud right now" is the loudest the key has been over the last
        10 ms. Just the level at this very instant would keep dropping to
        nothing as each wave of the sound crosses zero (dozens of times a
        second on a low note), and a gate following that would flutter. The
        loudest of the last 10 ms covers the gaps, even in the lowest notes. */
    void decide (int numSamples) noexcept
    {
        auto& w = work;

        // Copies of everything that carries on from sample to sample, close at hand
        float* const values = windowValues.data();
        float* const suffix = windowSuffix.data();
        const int length = window;
        int phase = windowPhase;
        float runningMax = windowRunningMax;

        bool isOpen = gateOpen;
        int holdCount = holdLeft;
        float a = errorA, b = errorB;
        const float openAt = openLevel, closeAt = closeLevel, closed = closedGain;
        const float attack = attackCoefficient, release = releaseCoefficient;
        const int holdTime = holdSamples;

        for (int i = 0; i < numSamples; ++i)
        {
            // The loudest of the last 10 ms, worked out without looking back
            // through all of them: time is split into 10 ms stretches, and the
            // loudest so far in this stretch is compared with the loudest from
            // the same point to the end of the stretch before (worked out once,
            // as each stretch finishes). Together they cover exactly 10 ms.
            const float heard = w.key[i];
            values[phase] = heard;
            runningMax = std::max (runningMax, heard);
            const float level = std::max (runningMax, suffix[phase + 1]);

            if (++phase == length)
            {
                float loudest = 0.0f;

                for (int j = length - 1; j >= 0; --j)
                {
                    loudest = std::max (loudest, values[j]);
                    suffix[j] = loudest;
                }

                phase = 0;
                runningMax = 0.0f;
            }

            // Open or closed?
            const bool wasOpen = isOpen;

            if (level >= openAt)
            {
                isOpen = true;
                holdCount = holdTime;
            }
            else if (isOpen)
            {
                if (level >= closeAt)     holdCount = holdTime;    // still loud enough: keep holding
                else if (holdCount > 0)   --holdCount;
                else                      isOpen = false;          // HOLD is over: close
            }

            // The gain is kept as how far it still has to go (a and b, for the
            // two smoothing steps). When it changes direction, that's measured
            // from the new destination instead.
            if (isOpen != wasOpen)
            {
                const float shift = isOpen ? closed - 1.0f : 1.0f - closed;
                a += shift;
                b += shift;
            }

            // Two smoothing steps in a row: an S-shaped curve, with no sudden
            // change of speed where it starts or ends, so it can't click.
            const float coefficient = isOpen ? attack : release;
            a *= coefficient;
            b = coefficient * b + (1.0f - coefficient) * a;

            if (std::abs (a) < arrivedDistance && std::abs (b) < arrivedDistance)
                a = b = 0.0f;   // (close enough to call it there: exactly open, or exactly closed)

            w.gains[i] = (isOpen ? 1.0f : closed) + b;
            w.open[i]  = isOpen ? 1.0f : 0.0f;
        }

        windowPhase = phase;
        windowRunningMax = runningMax;
        windowCleared = false;

        gateOpen = isOpen;
        holdLeft = holdCount;
        errorA = a;
        errorB = b;
    }

    /** Delays one channel's run of samples by the look-ahead (each sample
        swaps places with the one from 2 ms ago). */
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

    float currentGain() const noexcept   { return (gateOpen ? 1.0f : closedGain) + errorB; }

    //==========================================================================
    /** Adds a run of samples to the display's moments. */
    template <typename OnMoment>
    void recordMoments (float* const* channels, int numChannels, int start, int numSamples, OnMoment& onMoment) noexcept
    {
        auto& w = work;

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);

            moment.keyLevel = std::max (moment.keyLevel, juce::FloatVectorOperations::findMaximum (w.key + done, length));
            moment.gain     = std::max (moment.gain, juce::FloatVectorOperations::findMaximum (w.shownGains + done, length));
            moment.open     = moment.open || juce::FloatVectorOperations::findMaximum (w.open + done, length) > 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto range = juce::FloatVectorOperations::findMinAndMax (channels[ch] + start + done, length);
                moment.outLevel = std::max ({ moment.outLevel, -range.getStart(), range.getEnd() });
            }

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
            onMoment (static_cast<const Moment&> (moment));
            moment = {};
            momentFilled = 0;
        }
    }

    //==========================================================================
    /** True if the block about to come (digital silence) can be skipped
        through with rest(), ending up exactly where working through it would:
        it's closed and settled, nothing is fading, and the last sound has
        left the look-ahead, the 10 ms "right now" and the filters. */
    bool canRest() const noexcept
    {
        if (smoothedBypass.isSmoothing() || smoothedListen.isSmoothing())
            return false;

        if (gateOpen || holdLeft > 0 || std::abs (errorA) > 0.0f || std::abs (errorB) > 0.0f)
            return false;

        if (quietSamples < window)   // (the window is at least as long as the look-ahead)
            return false;

        for (auto& filter : lowCutFilters)
            if (! filter.hasFallenSilent())
                return false;

        for (auto& filter : highCutFilters)
            if (! filter.hasFallenSilent())
                return false;

        return true;
    }

    /** Moves on by numSamples of digital silence, just as working through them
        would. The audio stays as it is: silence in, silence out. */
    template <typename OnMoment>
    void rest (int numSamples, OnMoment& onMoment) noexcept
    {
        // (What the filters had left was under a billionth.)
        for (auto& filter : lowCutFilters)  filter.reset();
        for (auto& filter : highCutFilters) filter.reset();

        // The last 10 ms (and the look-ahead) hold nothing but silence.
        clearWindow();

        const float bypassed = smoothedBypass.getTargetValue();
        const float shownGain = closedGain * (1.0f - bypassed) + bypassed;

        for (int done = 0; done < numSamples;)
        {
            const int length = std::min (numSamples - done, momentLength - momentFilled);
            moment.gain = std::max (moment.gain, shownGain);
            done += length;
            finishMoment (length, onMoment);
        }
    }

    void clearWindow() noexcept
    {
        if (windowCleared)
            return;

        std::fill (windowValues.begin(), windowValues.end(), 0.0f);
        std::fill (windowSuffix.begin(), windowSuffix.end(), 0.0f);
        windowPhase = 0;
        windowRunningMax = 0.0f;
        windowCleared = true;
    }

    //==========================================================================
    /** Settings for one of the key filters. */
    struct FilterCoefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;
    };

    /** A 12 dB-per-octave filter ("state variable" design, the Butterworth
        shape: the standard, neutral curve), one per channel. It stays smooth
        and stable even while its knob is moving. */
    struct KeyFilter
    {
        float s1 = 0.0f, s2 = 0.0f;

        forcedinline float highPass (float x, const FilterCoefficients& c) noexcept
        {
            const float v3 = x - s2;
            const float v1 = c.a1 * s1 + c.a2 * v3;
            const float v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return x - c.k * v1 - v2;
        }

        forcedinline float lowPass (float x, const FilterCoefficients& c) noexcept
        {
            const float v3 = x - s2;
            const float v1 = c.a1 * s1 + c.a2 * v3;
            const float v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;
            return v2;
        }

        void reset() noexcept   { s1 = s2 = 0.0f; }

        /** True once it's stopped ringing (to under a billionth: -180 dB). */
        bool hasFallenSilent() const noexcept   { return std::abs (s1) < 1.0e-9f && std::abs (s2) < 1.0e-9f; }
    };

    FilterCoefficients butterworth (double hz) const noexcept
    {
        const double cutoff = std::min (hz, 0.45 * sampleRate);
        const double g  = std::tan (juce::MathConstants<double>::pi * cutoff / sampleRate);
        const double k  = juce::MathConstants<double>::sqrt2;
        const double a1 = 1.0 / (1.0 + g * (g + k));
        return { (float) a1, (float) (g * a1), (float) (g * g * a1), (float) k };
    }

    /** Turns a time in ms into the per-sample smoothing factor of step 3:
        after that much time, the S-shaped curve has gone 99% of the way. */
    float timeToCoefficient (float milliseconds) const noexcept
    {
        constexpr double timeConstantsTo99Percent = 6.6384;   // two smoothing steps in a row: (1 + t) e^-t = 1% at t = 6.64
        const double samples = std::max (0.25, (double) milliseconds * 0.001 * sampleRate / timeConstantsTo99Percent);
        return (float) std::exp (-1.0 / samples);
    }

    //==========================================================================
    static constexpr float arrivedDistance = 1.0e-6f;   // a millionth (-120 dB): "there"

    double sampleRate = 44100.0;
    int lookAhead = 88, window = 441, momentLength = 220;

    // The look-ahead: one short delay line per channel, all moving together
    std::vector<float> delayLines;
    int delayPosition = 0;

    // "How loud right now": the loudest of the last 10 ms (see decide())
    std::vector<float> windowValues, windowSuffix;
    int windowPhase = 0;
    float windowRunningMax = 0.0f;
    bool windowCleared = false;

    // Open or closed, and the gain on its way between them
    bool gateOpen = false;
    int holdLeft = 0, holdSamples = 0;
    float openLevel = 0.01f, closeLevel = 0.0063f, closedGain = 0.01f;
    float attackCoefficient = 0.0f, releaseCoefficient = 0.0f;
    float errorA = 0.0f, errorB = 0.0f;   // how far each smoothing step still has to go

    // IGNORE BELOW and IGNORE ABOVE
    bool lowCutActive = false, highCutActive = false;
    FilterCoefficients lowCut, highCut;
    std::array<KeyFilter, (size_t) maxChannels> lowCutFilters, highCutFilters;

    juce::SmoothedValue<float> smoothedBypass, smoothedListen;

    Settings previousSettings;
    bool hasPreviousSettings = false;

    Moment moment;
    int momentFilled = 0;

    Work work;
    int quietSamples = 0;
    bool resting = false, restingAllowed = true;
};

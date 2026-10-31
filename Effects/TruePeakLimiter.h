#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Easy Limiter's sound: a clean, transparent safety ceiling. Nothing about
    loudness targets - it stops overs, and pushes only as hard as GAIN asks.

      CEILING   the most the output may ever reach, in true peak (dBTP): the
                peaks BETWEEN the samples too, the ones that clip a converter
                or an MP3 even when the waveform looks fine. -0.3 by default.
      GAIN      turns the sound up into the ceiling (0 = only overs are
                touched).
      RELEASE   how quickly it lets go. AUTO (the default): quickly after a
                lone peak, more slowly while it's holding back a sustained
                passage - so it neither pumps nor distorts. Off: the RELEASE
                time you set.
      MATCH     for comparing by ear: takes GAIN back off what you hear, so
                the limited sound is as loud as the original (the louder one
                always sounds "better").

    How it stays clean:
      - It looks ahead (3 ms). Each peak is found before it arrives, and the
        gain glides down to exactly what that peak needs, arriving just in
        time - a smooth dip, never a corner (corners are what you hear as
        distortion, in the lows above all).
      - The peaks are found at 4x the sample rate (between the samples), so
        the ceiling is a true-peak ceiling.
      - Left and right are turned down together, so the stereo image never
        shifts.
      - Below the ceiling it does nothing at all: the sound passes untouched.

    The look-ahead delays the sound by 3 ms (plus a few samples for the
    peak finder); the plugin tells your DAW, which lines everything up.
*/
class TruePeakLimiter
{
public:
    static constexpr float minCeilingDb = -3.0f, maxCeilingDb = -0.1f;
    static constexpr float maxGainDb = 18.0f;
    static constexpr float minReleaseMs = 5.0f, maxReleaseMs = 1000.0f;
    static constexpr double lookaheadMs = 3.0, releaseHoldMs = 20.0;

    struct Settings
    {
        float gainDb      = 0.0f;
        float ceilingDb   = -0.3f;
        bool  autoRelease = true;
        float releaseMs   = 100.0f;
        bool  match       = false;
        bool  bypassed    = false;
    };

    //==========================================================================
    void prepare (double newSampleRate, int numChannels, const Settings& initial)
    {
        sampleRate = newSampleRate;
        channels = juce::jlimit (1, 2, numChannels);
        lookahead = juce::jmax (8, (int) std::lround (lookaheadMs * 1e-3 * sampleRate));
        latency = lookahead + finderDelay;

        makeFinder();
        for (auto& c : chans)
        {
            c.history.assign ((size_t) finderTaps, 0.0f);
            c.outHistory.assign ((size_t) finderTaps, 0.0f);
            c.delay.assign ((size_t) latency + 1, 0.0f);
            c.historyPos = 0;
            c.delayPos = 0;
        }
        lookaheadHold.setWindow (lookahead + 1);
        releaseHold.setWindow (juce::jmax (1, (int) std::lround (releaseHoldMs * 1e-3 * sampleRate)));
        gainDelay.assign ((size_t) latency + 1, 1.0f);
        gainDelayPos = 0;
        boxValues.assign ((size_t) lookahead, 1.0);
        boxSum = (double) lookahead;
        boxPos = 0;
        sampleCount = 0;
        released = 1.0f;
        sustainedDb = 0.0f;

        for (auto* v : { &gain, &matchGain, &bypassFade })
            v->reset (sampleRate, 0.05);
        settings = initial;
        setSettings (initial, true);
        reductionDb = 0.0f;
        outPeakDb = -100.0f;
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const float g = juce::Decibels::decibelsToGain (juce::jlimit (0.0f, maxGainDb, s.gainDb));
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };
        set (gain, g);
        set (matchGain, s.match ? 1.0f / g : 1.0f);
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
        // (aimed 0.1 dB under: turning the gain down moving reshapes the peaks between the samples very slightly,
        //  and this keeps even heavy limiting under the ceiling - while the hard stop below sits right at it)
        hardCeiling = juce::Decibels::decibelsToGain (juce::jlimit (minCeilingDb, maxCeilingDb, s.ceilingDb)) * 0.9995f;
        ceiling = hardCeiling * 0.9886f;
        manualRelease = coefficientFor (juce::jlimit (minReleaseMs, maxReleaseMs, s.releaseMs));
    }

    int getLatencySamples() const noexcept   { return latency; }

    /** Works on the buffer in place. numInputs: 1 for a mono track on a stereo output (copied to both sides first). */
    void process (juce::AudioBuffer<float>& buffer, int numInputs) noexcept
    {
        const int n = buffer.getNumSamples();
        const int outs = juce::jmin (channels, buffer.getNumChannels());
        if (n == 0 || outs == 0)
            return;
        for (int c = juce::jmax (1, numInputs); c < outs; ++c)
            buffer.copyFrom (c, 0, buffer, 0, 0, n);

        float* data[2] { buffer.getWritePointer (0), outs > 1 ? buffer.getWritePointer (1) : nullptr };
        float mostReduction = 1.0f, loudestOut = 0.0f;
        const float fastRelease = coefficientFor (30.0f), sustainCoef = coefficientFor (400.0f);

        for (int i = 0; i < n; ++i)
        {
            const float g = gain.getNextValue(), match = matchGain.getNextValue(), bypass = bypassFade.getNextValue();

            // 1. Find the peak (between the samples too) of what's coming in, after GAIN
            float peak = 0.0f;
            for (int c = 0; c < outs; ++c)
                peak = juce::jmax (peak, findPeak (chans[(size_t) c], data[c][i] * g, false));

            // 2. The gain that peak needs, and the release back up
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            float coef = manualRelease;
            if (settings.autoRelease)
            {
                // Quick after a lone peak; slower while it keeps holding a passage back (no pumping)
                const float nowDb = -juce::Decibels::gainToDecibels (released, -60.0f);
                sustainedDb += sustainCoef * (nowDb - sustainedDb);
                const float ms = 30.0f + 320.0f * juce::jlimit (0.0f, 1.0f, sustainedDb / 3.0f);
                coef = juce::jmin (fastRelease, coefficientFor (ms));
            }
            // (held for 20 ms before it starts to let go: within a low bass note's cycle the gain stays put - no ripple, no distortion)
            const float neededHeld = releaseHold.push (needed);
            released = juce::jmin (neededHeld, released + coef * (1.0f - released));

            // 3. Look ahead: the lowest gain due in the next 3 ms, smoothed so it glides down in time
            const float held = lookaheadHold.push (released);
            boxSum += (double) held - boxValues[(size_t) boxPos];
            boxValues[(size_t) boxPos] = held;
            boxPos = (boxPos + 1) % lookahead;
            if (++sampleCount % 65536 == 0)   // (recount now and then: no drift)
            {
                boxSum = 0.0;
                for (auto v : boxValues) boxSum += v;
            }
            const float smooth = juce::jmin (1.0f, (float) (boxSum / (double) lookahead));
            mostReduction = juce::jmin (mostReduction, smooth);

            // 4. The delayed sound, turned down by that gain (and by GAIN as it was when that sound came in)
            gainDelay[(size_t) gainDelayPos] = g;
            gainDelayPos = (gainDelayPos + 1) % (latency + 1);
            const float gThen = gainDelay[(size_t) gainDelayPos];
            for (int c = 0; c < outs; ++c)
            {
                auto& ch = chans[(size_t) c];
                const float in = data[c][i];
                ch.delay[(size_t) ch.delayPos] = in;
                const int readPos = (ch.delayPos + 1) % (latency + 1);   // (latency samples ago)
                ch.delayPos = readPos;
                const float dry = ch.delay[(size_t) readPos];

                float y = dry * gThen * smooth;
                y = juce::jlimit (-hardCeiling, hardCeiling, y);   // (never needed - the gain is already there - but never over, whatever happens)
                loudestOut = juce::jmax (loudestOut, findPeak (ch, y, true));
                y *= match;
                data[c][i] = y * (1.0f - bypass) + dry * bypass;
            }
        }

        reductionDb = -juce::Decibels::gainToDecibels (mostReduction, -60.0f);
        outPeakDb = juce::Decibels::gainToDecibels (loudestOut, -100.0f);
    }

    /** For the meters: the most it turned down in the last block (dB, positive), and the output's true peak (dBTP). */
    float getReductionDb() const noexcept    { return reductionDb; }
    float getOutputPeakDb() const noexcept   { return outPeakDb; }

    /** The true peak (dBTP) of a whole buffer: the same peak finder, for the tests and meters. */
    static float truePeakDb (const juce::AudioBuffer<float>& b)
    {
        TruePeakLimiter finder;
        finder.makeFinder();
        float most = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            Channel ch;
            ch.history.assign ((size_t) finderTaps, 0.0f);
            ch.outHistory.assign ((size_t) finderTaps, 0.0f);
            // (the buffer's own edges left out: cutting a sound off dead is a step no music makes)
            for (int i = 0; i < b.getNumSamples(); ++i)
            {
                const float peak = finder.findPeak (ch, b.getSample (c, i), false);
                if (i >= finderTaps)
                    most = juce::jmax (most, peak);
            }
        }
        return juce::Decibels::gainToDecibels (most, -200.0f);
    }

private:
    // The peak finder: a 4x interpolator (windowed sinc, 4 phases of 12 taps), like a true-peak meter
    static constexpr int phases = 4, tapsPerPhase = 12, finderTaps = tapsPerPhase, finderDelay = tapsPerPhase / 2;

    struct Channel
    {
        std::vector<float> history, outHistory, delay;
        int historyPos = 0, outHistoryPos = 0, delayPos = 0;
    };

    void makeFinder()
    {
        constexpr int total = phases * tapsPerPhase;
        const double centre = total / 2;   // (on a whole sample: phase 0 reads the samples themselves)
        const auto besselI0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 30; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        for (int k = 0; k < total; ++k)
        {
            const double t = (k - centre) / phases;
            const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
            const double x = (k - centre) / (centre + 1.0);
            const double window = besselI0 (7.0 * std::sqrt (juce::jmax (0.0, 1.0 - x * x))) / besselI0 (7.0);
            coefficients[(size_t) (k % phases)][(size_t) (k / phases)] = (float) (sinc * window);
        }
    }

    float findPeak (Channel& c, float x, bool output) noexcept
    {
        auto& h = output ? c.outHistory : c.history;
        auto& pos = output ? c.outHistoryPos : c.historyPos;
        h[(size_t) pos] = x;
        pos = (pos + 1) % finderTaps;
        float peak = 0.0f;
        for (int p = 0; p < phases; ++p)
        {
            float sum = 0.0f;
            for (int k = 0; k < tapsPerPhase; ++k)
                sum += coefficients[(size_t) p][(size_t) k] * h[(size_t) ((pos + k) % finderTaps)];
            peak = juce::jmax (peak, std::abs (sum));
        }
        return peak;
    }

    /** The lowest value in the last `window` samples (a running minimum, constant time). */
    struct RunningMinimum
    {
        std::vector<float> values;
        std::vector<long long> index;
        int head = 0, tail = 0, window = 1;
        long long count = 0;

        void setWindow (int w)
        {
            window = w;
            values.assign ((size_t) w + 2, 1.0f);
            index.assign ((size_t) w + 2, 0);
            head = tail = 0;
            count = 0;
        }

        float push (float value) noexcept
        {
            const int size = (int) values.size();
            while (head != tail && values[(size_t) ((tail - 1 + size) % size)] >= value)
                tail = (tail - 1 + size) % size;
            values[(size_t) tail] = value;
            index[(size_t) tail] = count;
            tail = (tail + 1) % size;
            while (index[(size_t) head] <= count - window)
                head = (head + 1) % size;
            ++count;
            return values[(size_t) head];
        }
    };

    float coefficientFor (float ms) const noexcept   { return (float) (1.0 - std::exp (-1.0 / (ms * 1e-3 * sampleRate))); }

    double sampleRate = 48000.0;
    int channels = 2, lookahead = 144, latency = 150;
    Settings settings;
    std::array<std::array<float, tapsPerPhase>, phases> coefficients {};
    std::array<Channel, 2> chans;

    RunningMinimum lookaheadHold, releaseHold;
    std::vector<float> gainDelay;
    int gainDelayPos = 0;
    std::vector<double> boxValues;
    double boxSum = 0.0;
    int boxPos = 0;
    long long sampleCount = 0;

    float ceiling = 0.955f, hardCeiling = 0.966f, manualRelease = 0.001f, released = 1.0f, sustainedDb = 0.0f;
    float reductionDb = 0.0f, outPeakDb = -100.0f;
    juce::SmoothedValue<float> gain, matchGain, bypassFade;
};

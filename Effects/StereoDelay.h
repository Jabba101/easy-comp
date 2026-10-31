#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "FastMath.h"
#include <array>
#include <cmath>
#include <vector>

//==============================================================================
/**
    Easy Delay's sound: a stereo delay (echo).

    What goes on, for each sample:

      - The sound goes into a delay line and comes out TIME later: the
        first echo.
      - Each echo is fed back in, a little quieter (FEEDBACK), so the
        repeats keep coming and fade away. At 0 you get one echo
        (slapback); at 50% each repeat is half as loud as the one before.
      - LOW CUT and HIGH CUT filter the echoes (12 dB / octave), and
        because they're inside the loop, every repeat gets a little
        thinner / darker than the one before - like real echo units.
        (Echoes with less low end don't muddy the mix.)
      - MODE: STEREO (each side echoes itself), PING-PONG (the echoes
        bounce left, right, left...) or WIDE (the right side's echoes come
        half as late again - with SYNC, e.g. 1/8 on the left and dotted
        1/8 on the right).
      - CHARACTER: CLEAN (digital: every repeat exact) or TAPE (a tape
        echo: the repeats saturate gently, wobble a little, and moving
        TIME bends the pitch like a tape machine speeding up or slowing
        down, instead of crossfading).
      - DUCK: the echoes duck while you're playing / singing and come up
        in the gaps, so a vocal stays clear but the echoes still fill the
        space between phrases.
      - MIX: 0% = dry, 50% = both full, 100% = echoes only (for a send).

    No delay added to the track itself (the DAW lines up nothing).
*/
class StereoDelay
{
public:
    enum class Mode { stereo = 0, pingPong = 1, wide = 2 };

    static constexpr float minTimeMs = 1.0f, maxTimeMs = 4000.0f;
    static constexpr float lowCutOffHz = 20.0f, highCutOffHz = 20000.0f;   // at these ends, the filter's off

    struct Settings
    {
        float timeMs     = 375.0f;
        Mode  mode       = Mode::stereo;
        float feedback   = 0.3f;              // 0 .. 0.95
        float lowCutHz   = 150.0f;
        float highCutHz  = 8000.0f;
        bool  tape       = false;
        float duck       = 0.0f;              // 0 .. 1
        float mix        = 0.25f;             // 0 .. 1
        bool  bypassed   = false;
    };

    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;

        int size = 1;
        while (size < (int) (sampleRate * maxTimeMs * 1.5 / 1000.0) + 16)
            size <<= 1;
        mask = size - 1;
        for (auto& line : lines)
            line.assign ((size_t) size, 0.0f);
        writePos = 0;

        dryGain.reset (sampleRate, 0.03);
        wetGain.reset (sampleRate, 0.03);
        feedbackGain.reset (sampleRate, 0.05);
        bypassFade.reset (sampleRate, 0.02);

        attack  = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.002)));
        release = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.15)));
        glide   = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.12)));
        fadeLength = juce::jmax (1, (int) (sampleRate * 0.04));
        wobbleStep = juce::MathConstants<double>::twoPi * 0.55 / sampleRate;

        settings = initial;
        lastFilterLow = lastFilterHigh = -1.0f;
        reset();
        setSettings (initial, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            heads[(size_t) ch].current = heads[(size_t) ch].next = targetSamples (ch);
            heads[(size_t) ch].fade = -1;
        }
    }

    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& f : filters)
            f = {};
        envelope = 0.0f;
        wobblePhase = 0.0;
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const float m = juce::jlimit (0.0f, 1.0f, s.mix);
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };

        set (dryGain, m <= 0.5f ? 1.0f : 2.0f * (1.0f - m));
        set (wetGain, m >= 0.5f ? 1.0f : 2.0f * m);
        set (feedbackGain, juce::jlimit (0.0f, 0.95f, s.feedback));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);

        if (std::abs (s.lowCutHz - lastFilterLow) > 0.01f || std::abs (s.highCutHz - lastFilterHigh) > 0.01f)
            designFilters (s.lowCutHz, s.highCutHz);
    }

    //==========================================================================
    /** numInputs: 1 for a mono track (with a stereo output, the echoes still go stereo). */
    void process (juce::AudioBuffer<float>& buffer, int numInputs) noexcept
    {
        const int n = buffer.getNumSamples();
        const int outputs = juce::jmin (2, buffer.getNumChannels());
        if (outputs == 0)
            return;

        float* out[2] { buffer.getWritePointer (0), outputs > 1 ? buffer.getWritePointer (1) : nullptr };
        const bool stereoIn = numInputs > 1 && outputs > 1;
        const auto mode = outputs > 1 ? settings.mode : Mode::stereo;
        const float duckDepthDb = -24.0f * juce::jlimit (0.0f, 1.0f, settings.duck);
        const bool tape = settings.tape;
        const float wobbleDepth = tape ? (float) (sampleRate * 0.00035) : 0.0f;   // +-0.35 ms
        const float target[2] { targetSamples (0), targetSamples (1) };
        float loudest = 0.0f;

        for (int i = 0; i < n; ++i)
        {
            const float inL = out[0][i];
            const float inR = outputs > 1 ? (stereoIn ? out[1][i] : inL) : inL;

            // The echoes coming out now (filtered: the filters are inside the loop)
            wobblePhase += wobbleStep;
            if (wobblePhase > juce::MathConstants<double>::twoPi) wobblePhase -= juce::MathConstants<double>::twoPi;
            const float wobble = wobbleDepth * (float) std::sin (wobblePhase);

            float echo[2] {};
            for (int ch = 0; ch < (outputs > 1 ? 2 : 1); ++ch)
            {
                float e = readHead (ch, target[ch], tape, wobble * (ch == 0 ? 1.0f : -0.8f));
                auto& f = filters[(size_t) ch];
                e = f.high.process (f.low.process (e));
                if (tape)
                    e = saturate (e);
                echo[ch] = e;
            }

            // Back into the lines
            const float fb = feedbackGain.getNextValue();
            float writeL, writeR;
            if (mode == Mode::pingPong)
            {
                writeL = 0.5f * (inL + inR) + echo[1] * fb;
                writeR = echo[0] * fb;
            }
            else
            {
                writeL = inL + echo[0] * fb;
                writeR = inR + echo[1] * fb;
            }
            lines[0][(size_t) writePos] = writeL;
            lines[1][(size_t) writePos] = writeR;
            writePos = (writePos + 1) & mask;

            // DUCK: the echoes step back while you play
            const float level = juce::jmax (std::abs (inL), std::abs (inR));
            envelope += (level > envelope ? attack : release) * (level - envelope);
            float duckGain = 1.0f;
            if (duckDepthDb < 0.0f)
            {
                const float envDb = FastMath::gainToDecibels (envelope);
                const float amount = juce::jlimit (0.0f, 1.0f, (envDb + 36.0f) / 24.0f);   // (from -36 dBFS: fully ducked by -12)
                duckGain = FastMath::decibelsToGain (duckDepthDb * amount);
            }

            const float dry = dryGain.getNextValue();
            const float wet = wetGain.getNextValue() * duckGain;
            const float bypass = bypassFade.getNextValue();

            const float yL = inL * dry + echo[0] * wet;
            out[0][i] = yL * (1.0f - bypass) + inL * bypass;
            loudest = juce::jmax (loudest, std::abs (echo[0] * wet));
            if (outputs > 1)
            {
                const float yR = inR * dry + echo[1] * wet;
                out[1][i] = yR * (1.0f - bypass) + inR * bypass;
                loudest = juce::jmax (loudest, std::abs (echo[1] * wet));
            }
        }

        echoPeak = loudest;
        duckNow = envelope;
    }

    /** How loud the echoes were in the last block (as heard, after MIX and DUCK). */
    float getEchoPeak() const noexcept   { return echoPeak; }

    //==========================================================================
    /** The echoes you'll hear, for the window's picture: when (ms) and how
        loud on each side (0..1), ignoring the filters. */
    struct Tap { float ms, left, right; };

    static std::vector<Tap> picture (const Settings& s, float untilMs = 3000.0f)
    {
        std::vector<Tap> taps;
        const float fb = juce::jlimit (0.0f, 0.95f, s.feedback);
        const float t = juce::jlimit (minTimeMs, maxTimeMs, s.timeMs);

        for (int k = 1; k <= 40; ++k)
        {
            const float gain = std::pow (fb, (float) (k - 1));
            if (gain < 0.02f)
                break;

            if (s.mode == Mode::pingPong)
            {
                if (t * k > untilMs) break;
                taps.push_back ({ t * k, (k % 2) == 1 ? gain : 0.0f, (k % 2) == 0 ? gain : 0.0f });
            }
            else if (s.mode == Mode::wide)
            {
                if (t * k <= untilMs)        taps.push_back ({ t * k, gain, 0.0f });
                if (t * 1.5f * k <= untilMs) taps.push_back ({ juce::jmin (maxTimeMs * k, t * 1.5f * k), 0.0f, gain });
                if (t * k > untilMs) break;
            }
            else
            {
                if (t * k > untilMs) break;
                taps.push_back ({ t * k, gain, gain });
            }
        }
        return taps;
    }

    /** How long the echoes take to die away (for the DAW's tail). */
    static double tailSeconds (const Settings& s)
    {
        const double fb = juce::jlimit (0.0, 0.95, (double) s.feedback);
        const double repeats = fb < 0.01 ? 1.0 : 1.0 + std::log (0.001) / std::log (fb);
        const double longest = s.timeMs * (s.mode == Mode::wide ? 1.5 : 1.0) * (s.mode == Mode::pingPong ? 2.0 : 1.0);
        return juce::jmin (30.0, repeats * longest / 1000.0 + 0.1);
    }

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

        float process (float x) noexcept
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    struct Filters { Biquad low, high; };   // low = LOW CUT (a high-pass), high = HIGH CUT (a low-pass)

    struct Head
    {
        float current = 0.0f, next = 0.0f;   // delay in samples
        int fade = -1;                       // >= 0: crossfading from current to next (CLEAN)
    };

    float targetSamples (int ch) const noexcept
    {
        float ms = juce::jlimit (minTimeMs, maxTimeMs, settings.timeMs);
        if (ch == 1 && settings.mode == Mode::wide)
            ms = juce::jmin (maxTimeMs * 1.5f, ms * 1.5f);
        return (float) (ms * sampleRate / 1000.0);
    }

    float read (int ch, float delaySamples) const noexcept
    {
        const float position = (float) writePos - juce::jlimit (1.0f, (float) mask - 4.0f, delaySamples);
        const float whole = std::floor (position);
        const float t = position - whole;
        const int p = (int) whole;
        const auto& line = lines[(size_t) ch];
        const float y0 = line[(size_t) ((p - 1) & mask)], y1 = line[(size_t) (p & mask)],
                    y2 = line[(size_t) ((p + 1) & mask)], y3 = line[(size_t) ((p + 2) & mask)];
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    // TIME moving: CLEAN crossfades to the new time; TAPE glides (the pitch bends)
    float readHead (int ch, float target, bool tape, float wobble) noexcept
    {
        auto& h = heads[(size_t) ch];

        if (tape)
        {
            h.fade = -1;
            h.current += glide * (target - h.current);
            return read (ch, h.current + wobble);
        }

        if (h.fade < 0 && std::abs (target - h.current) > 0.5f)
        {
            h.next = target;
            h.fade = 0;
        }

        if (h.fade < 0)
            return read (ch, h.current);

        const float t = (float) h.fade / (float) fadeLength;
        const float y = read (ch, h.current) * (1.0f - t) + read (ch, h.next) * t;
        if (++h.fade >= fadeLength)
        {
            h.current = h.next;
            h.fade = -1;
        }
        return y;
    }

    // The tape echo's gentle saturation (each repeat rounds a little more)
    static float saturate (float x) noexcept
    {
        const float a = juce::jmin (std::abs (x), 20.0f);
        const float t = 1.0f - 2.0f / (FastMath::exp2 (a * 2.88539008f) + 1.0f);
        return x < 0.0f ? -t : t;
    }

    void designFilters (float lowCutHz, float highCutHz) noexcept
    {
        lastFilterLow = lowCutHz;
        lastFilterHigh = highCutHz;

        const auto make = [this] (Biquad& f, double hz, bool highPass)
        {
            const double w = juce::MathConstants<double>::twoPi * juce::jmin (hz, sampleRate * 0.45) / sampleRate;
            const double c = std::cos (w), alpha = std::sin (w) / juce::MathConstants<double>::sqrt2;
            const double a0 = 1.0 + alpha;
            Biquad n;
            if (highPass) { n.b0 = (float) ((1 + c) / 2 / a0); n.b1 = (float) (-(1 + c) / a0); n.b2 = n.b0; }
            else          { n.b0 = (float) ((1 - c) / 2 / a0); n.b1 = (float) ((1 - c) / a0);  n.b2 = n.b0; }
            n.a1 = (float) (-2 * c / a0);
            n.a2 = (float) ((1 - alpha) / a0);
            f.b0 = n.b0; f.b1 = n.b1; f.b2 = n.b2; f.a1 = n.a1; f.a2 = n.a2;   // (keeps its state)
        };

        for (auto& f : filters)
        {
            if (lowCutHz <= lowCutOffHz + 0.5f) { f.low.b0 = 1; f.low.b1 = f.low.b2 = f.low.a1 = f.low.a2 = 0; }
            else                                  make (f.low, lowCutHz, true);

            if (highCutHz >= highCutOffHz - 1.0f) { f.high.b0 = 1; f.high.b1 = f.high.b2 = f.high.a1 = f.high.a2 = 0; }
            else                                    make (f.high, highCutHz, false);
        }
    }

    double sampleRate = 48000.0;
    Settings settings;
    std::array<std::vector<float>, 2> lines;
    int mask = 0, writePos = 0;
    std::array<Head, 2> heads;
    std::array<Filters, 2> filters;
    float lastFilterLow = -1.0f, lastFilterHigh = -1.0f;

    juce::SmoothedValue<float> dryGain, wetGain, feedbackGain, bypassFade;
    float envelope = 0.0f, attack = 0.0f, release = 0.0f, glide = 0.0f;
    int fadeLength = 1920;
    double wobblePhase = 0.0, wobbleStep = 0.0;
    float echoPeak = 0.0f, duckNow = 0.0f;
};

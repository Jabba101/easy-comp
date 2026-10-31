#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <complex>

//==============================================================================
/**
    Easy Filter's sound: a resonant filter whose cutoff is moved by an LFO -
    slow sweeps, or pulses locked to the song's beat - with the left and
    right sides able to move apart for wide stereo filtering.

      TYPE       LOW-PASS (24 dB/octave: takes the top off - the classic
                 synth sweep), HIGH-PASS (24 dB/octave: thins the bottom
                 out - the build-up before a drop), BAND-PASS (12 dB/octave
                 each side: only a band around the cutoff - telephone, wah-ish)
      CUTOFF     where the filter sits (20 Hz - 20 kHz): the middle of the
                 movement
      RESONANCE  a peak at the cutoff: from smooth to a singing, whistling
                 edge (it never self-oscillates)
      DEPTH      how far the LFO moves the cutoff: 0 = still, 100% = three
                 octaves up and down
      SHAPE      how it moves: SINE and TRIANGLE (smooth sweeps), RAMP UP and
                 RAMP DOWN (rising or falling every cycle - "pumping" when
                 synced), SQUARE (jumping between two settings - tempo
                 pulses), RANDOM (a new place every cycle - sample & hold)
      RATE       cycles a second, or with SYNC a note length (one cycle),
                 locked to the song's beat while it plays
      WIDTH      the right side's movement shifted against the left's: 0 =
                 together, 100% = half a cycle apart (one side opens as the
                 other closes - very wide). RANDOM picks separately for each
                 side.
      MIX        filtered sound against the original

    The jumps in SQUARE, the RAMPs and RANDOM are eased over about 6 ms,
    so they pulse without clicking. TYPE changes crossfade (10 ms). No delay
    is added to your sound.
*/
class ModFilter
{
public:
    enum class Type  { lowPass = 0, highPass = 1, bandPass = 2 };
    enum class Shape { sine = 0, triangle = 1, rampUp = 2, rampDown = 3, square = 4, random = 5 };

    static constexpr float minCutoffHz = 20.0f, maxCutoffHz = 20000.0f;
    static constexpr float minRateHz = 0.02f, maxRateHz = 20.0f;
    static constexpr float maxOctaves = 3.0f;   // DEPTH 100%: this many octaves each way

    struct Settings
    {
        Type   type       = Type::lowPass;
        float  cutoffHz   = 800.0f;
        float  resonance  = 0.3f;    // 0 .. 1
        float  depth      = 0.6f;    // 0 .. 1
        Shape  shape      = Shape::sine;
        bool   synced     = false;
        double cycleBeats = 4.0;
        float  rateHz     = 0.5f;
        float  width      = 0.0f;    // 0 .. 1
        float  mix        = 1.0f;    // 0 .. 1
        bool   bypassed   = false;
    };

    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        for (auto* v : { &logCutoff, &resonance, &depth, &width, &mix, &bypassFade })
            v->reset (sampleRate, 0.05);
        settings = initial;
        currentType = initial.type;
        fadeFrom = currentType;
        typeFade = 1.0f;
        cycles = 0.0;
        for (auto& ch : channels) ch = {};
        smoothing = (float) (1.0 - std::exp (-1.0 / (0.0018 * sampleRate)));   // (two of these in a row: an S-shaped move of about 6 ms)
        setSettings (initial, true);
        for (int c = 0; c < 2; ++c)
            channels[(size_t) c].lfo = channels[(size_t) c].lfoHalfway = lfoValue (initial.shape, c == 1 ? 0.5 * initial.width : 0.0, c);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };
        set (logCutoff, std::log2 (juce::jlimit (minCutoffHz, maxCutoffHz, s.cutoffHz)));
        set (resonance, juce::jlimit (0.0f, 1.0f, s.resonance));
        set (depth, juce::jlimit (0.0f, 1.0f, s.depth));
        set (width, juce::jlimit (0.0f, 1.0f, s.width));
        set (mix, juce::jlimit (0.0f, 1.0f, s.mix));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);

        if (s.type != currentType)
        {
            if (jump) { currentType = s.type; typeFade = 1.0f; }
            else      { fadeFrom = currentType; currentType = s.type; typeFade = 0.0f; }
        }
    }

    /** songBeats: where the song is (quarter notes) at the block's start while it plays, else < 0. */
    void process (juce::AudioBuffer<float>& buffer, int numInputs, double bpm, double songBeats) noexcept
    {
        const int n = buffer.getNumSamples();
        const int outputs = juce::jmin (2, buffer.getNumChannels());
        if (outputs == 0 || n == 0)
            return;

        double cyclesPerSample;
        if (settings.synced)
        {
            const double beats = juce::jmax (0.03125, settings.cycleBeats);
            cyclesPerSample = juce::jlimit (20.0, 999.0, bpm) / 60.0 / beats / sampleRate;
            if (songBeats >= 0.0)
                cycles = songBeats / beats;   // (locked to the song: the same place every time it plays there)
        }
        else
        {
            cyclesPerSample = juce::jlimit ((double) minRateHz, (double) maxRateHz, (double) settings.rateHz) / sampleRate;
        }

        float* out[2] { buffer.getWritePointer (0), outputs > 1 ? buffer.getWritePointer (1) : nullptr };
        const bool stereoIn = numInputs > 1 && outputs > 1;
        const float nyquistLimit = (float) juce::jmin ((double) maxCutoffHz, sampleRate * 0.45);
        const auto shape = settings.shape;
        const bool smoothShape = shape == Shape::sine || shape == Shape::triangle;
        const float fadeStep = 1.0f / (0.01f * (float) sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const float inL = out[0][i];
            const float inR = outputs > 1 ? (stereoIn ? out[1][i] : inL) : inL;
            const float centre = logCutoff.getNextValue(), res = resonance.getNextValue(), d = depth.getNextValue(),
                        w = width.getNextValue(), m = mix.getNextValue(), bypass = bypassFade.getNextValue();
            const float fade = typeFade;
            if (typeFade < 1.0f) typeFade = juce::jmin (1.0f, typeFade + fadeStep);

            cycles += cyclesPerSample;
            if (! settings.synced && cycles > 1.0e6) cycles -= 1.0e6;

            // The resonance: the second stage's damping (the first stays gentle), and a little less level as it rises
            const float k = 2.0f - 1.85f * res;
            const float kBand = 2.0f - 1.9f * res;
            const float level = 1.0f / (1.0f + 0.6f * res * res);

            const float in[2] { inL, inR };
            float y[2] {};
            for (int ch = 0; ch < outputs; ++ch)
            {
                auto& c = channels[(size_t) ch];
                const float target = lfoValue (shape, ch == 1 ? 0.5 * w : 0.0, ch);
                if (smoothShape)
                    c.lfo = c.lfoHalfway = target;
                else
                {
                    // Jumps eased in and out (starting gently as well as finishing gently), so a pulse never ticks
                    c.lfoHalfway += smoothing * (target - c.lfoHalfway);
                    c.lfo += smoothing * (c.lfoHalfway - c.lfo);
                }

                const float hz = juce::jlimit (minCutoffHz, nyquistLimit, std::exp2 (centre + d * maxOctaves * c.lfo));
                const float g = (float) std::tan (juce::MathConstants<double>::pi * hz / sampleRate);

                // Stage 1 (gentle), then the low-pass and high-pass second stages; the band-pass its own
                const auto s1 = c.first.process (in[ch], g, 1.848f);
                const float low  = c.low.process (s1.low, g, k).low;
                const float high = c.high.process (s1.high, g, k).high;
                const auto  bp   = c.band.process (in[ch], g, kBand);
                const float band = bp.band * kBand;   // (as loud as your sound at the peak, at any RESONANCE)

                const auto pick = [&] (Type t) { return t == Type::lowPass ? low : (t == Type::highPass ? high : band); };
                float filtered = pick (currentType);
                if (fade < 1.0f)
                    filtered = pick (fadeFrom) * (1.0f - fade) + filtered * fade;
                filtered *= level;

                const float mixed = in[ch] * (1.0f - m) + filtered * m;
                y[ch] = mixed * (1.0f - bypass) + in[ch] * bypass;
            }

            out[0][i] = y[0];
            if (out[1] != nullptr)
                out[1][i] = y[1];
        }

        position = (float) (cycles - std::floor (cycles));
        for (int ch = 0; ch < 2; ++ch)
            cutoffNow[(size_t) ch] = juce::jlimit (minCutoffHz, nyquistLimit,
                                                   std::exp2 (logCutoff.getCurrentValue() + depth.getCurrentValue() * maxOctaves * channels[(size_t) ch].lfo));
    }

    /** Where the cycle is (0..1), and each side's cutoff right now - for the window. */
    float getPosition() const noexcept               { return position; }
    float getCutoffNow (int side) const noexcept     { return cutoffNow[(size_t) juce::jlimit (0, 1, side)]; }

    /** The LFO's value (-1 .. +1) at a point in a cycle, for the window's drawing (RANDOM: a fixed pattern). */
    static float shapeAt (Shape shape, double cyclePosition, double cycleIndex = 0.0, int side = 0) noexcept
    {
        const double p = cyclePosition - std::floor (cyclePosition);
        switch (shape)
        {
            case Shape::triangle: return (float) (p < 0.5 ? -1.0 + 4.0 * p : 3.0 - 4.0 * p);
            case Shape::rampUp:   return (float) (-1.0 + 2.0 * p);
            case Shape::rampDown: return (float) (1.0 - 2.0 * p);
            case Shape::square:   return p < 0.5 ? 1.0f : -1.0f;
            case Shape::random:   return randomFor ((long long) std::floor (cycleIndex), side);
            case Shape::sine:
            default:              return (float) std::sin (juce::MathConstants<double>::twoPi * p);
        }
    }

    /** How loud the filter is at a frequency (dB), for drawing its curve. */
    static float responseDb (Type type, float cutoffHz, float res, float hz, double rate) noexcept
    {
        const double w = juce::MathConstants<double>::twoPi * hz / rate;
        const double g = std::tan (juce::MathConstants<double>::pi * juce::jmin ((double) cutoffHz, rate * 0.45) / rate);
        const std::complex<double> z = std::polar (1.0, w);
        // The TPT SVF's transfer functions (bilinear): s = (z - 1) / (z + 1) / g
        const auto s = (z - 1.0) / (z + 1.0) / g;
        const auto stage = [&] (double k, int out)
        {
            const auto den = s * s + k * s + 1.0;
            return out == 0 ? 1.0 / den : (out == 1 ? s * s / den : s / den);
        };
        const double k = 2.0 - 1.85 * res, kBand = 2.0 - 1.9 * res;
        const double level = 1.0 / (1.0 + 0.6 * res * res);
        std::complex<double> h;
        if (type == Type::lowPass)       h = stage (1.848, 0) * stage (k, 0);
        else if (type == Type::highPass) h = stage (1.848, 1) * stage (k, 1);
        else                             h = stage (kBand, 2) * kBand;
        return (float) (20.0 * std::log10 (std::abs (h) * level + 1e-12));
    }

private:
    struct Svf
    {
        float s1 = 0.0f, s2 = 0.0f;
        struct Out { float low, band, high; };

        Out process (float x, float g, float k) noexcept
        {
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float hp = (x - (g + k) * s1 - s2) * a1;
            const float v1 = g * hp;
            const float bp = v1 + s1;
            s1 = bp + v1;
            const float v2 = g * bp;
            const float lp = v2 + s2;
            s2 = lp + v2;
            return { lp, bp, hp };
        }
    };

    struct Channel
    {
        Svf first, low, high, band;
        float lfo = 0.0f, lfoHalfway = 0.0f;
    };

    static float randomFor (long long cycle, int side) noexcept
    {
        // The same "random" value every time the song passes this cycle (so a loop sounds the same each time)
        auto x = (unsigned long long) (cycle * 2 + side) + 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        x ^= x >> 31;
        return (float) ((double) (x >> 11) / (double) (1ull << 53)) * 2.0f - 1.0f;
    }

    float lfoValue (Shape shape, double offset, int side) const noexcept
    {
        const double at = cycles + offset;
        return shapeAt (shape, at, std::floor (at), side);
    }

    double sampleRate = 48000.0, cycles = 0.0;
    Settings settings;
    Type currentType = Type::lowPass, fadeFrom = Type::lowPass;
    float typeFade = 1.0f, smoothing = 0.01f, position = 0.0f;
    std::array<float, 2> cutoffNow { 800.0f, 800.0f };
    std::array<Channel, 2> channels;
    juce::SmoothedValue<float> logCutoff, resonance, depth, width, mix, bypassFade;
};

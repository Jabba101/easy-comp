#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "FastMath.h"
#include <array>
#include <cmath>
#include <complex>

//==============================================================================
/**
    Easy Wah's sound: a wah pedal that works itself.

    A wah is a narrow, resonant filter swept up and down - heel down it's
    dark and boomy, toe down it's bright and quacky. Here the "pedal" moves
    by itself:

      - AUTO: it follows how hard you play. Each note kicks the pedal
        forward and it falls back as the note dies away - the funk guitar /
        clavinet / bass "wow" (an envelope filter). SENSITIVITY: how far
        your playing pushes it (and how quietly you can play and still move
        it). RESPONSE: how quickly it jumps and falls back. DIRECTION: UP =
        harder playing sweeps brighter (the usual), DOWN = darker.
      - TEMPO: it rocks back and forth in time with the song (one sweep per
        note length), SENSITIVITY setting how far.
      - PEDAL: where it rests. With SENSITIVITY at 0 the pedal just stays
        there - the "cocked wah" sound (a fixed, nasal tone).
      - FROM / TO: the heel and toe positions (Hz); the sweep between them
        is spread the way we hear pitch.
      - RESONANCE: how sharp and vocal the peak is ("quack").
      - MIX: blends your original back in.

    The filter (a state-variable filter, recalculated every few samples as
    the pedal moves) can't jump or click however fast it sweeps. A wah
    takes level out (it only lets a narrow band through), so AUTO-GAIN
    (on by default) keeps the volume as it went in. No delay added.
*/
class AutoWah
{
public:
    enum class Mode      { automatic = 0, tempo = 1 };
    enum class Response  { snappy = 0, smooth = 1, lazy = 2 };
    enum class Direction { up = 0, down = 1 };

    static constexpr float minFromHz = 150.0f, maxFromHz = 1000.0f, minToHz = 600.0f, maxToHz = 5000.0f;

    struct Settings
    {
        Mode      mode        = Mode::automatic;
        float     sensitivity = 0.6f;    // 0 .. 1
        float     pedal       = 0.0f;    // rest position, 0 (heel) .. 1 (toe)
        float     fromHz      = 400.0f;
        float     toHz        = 2200.0f;
        float     resonance   = 0.6f;    // 0 .. 1
        Response  response    = Response::smooth;
        Direction direction   = Direction::up;
        double    cycleBeats  = 1.0;     // TEMPO: one sweep, in quarter notes
        float     mix         = 1.0f;
        bool      autoGain    = true;
        bool      bypassed    = false;
    };

    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        for (auto* v : { &mix, &bypassFade })
            v->reset (sampleRate, 0.02);
        restPosition.reset (sampleRate, 0.05);
        envelope = 0.0f;
        position = initial.pedal;
        phase = 0.0;
        for (auto& f : filters) f = {};
        for (auto& h : hear) h = {};
        listener = {};
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        const auto set = [jump] (juce::SmoothedValue<float>& v, float target)
        {
            if (jump) v.setCurrentAndTargetValue (target);
            else      v.setTargetValue (target);
        };
        set (mix, juce::jlimit (0.0f, 1.0f, s.mix));
        set (bypassFade, s.bypassed ? 1.0f : 0.0f);
        set (restPosition, juce::jlimit (0.0f, 1.0f, s.pedal));

        const double attackMs[]  { 2.0, 8.0, 30.0 };
        const double releaseMs[] { 90.0, 220.0, 550.0 };
        const int r = juce::jlimit (0, 2, (int) s.response);
        attack  = (float) (1.0 - std::exp (-1.0 / (sampleRate * attackMs[r] / 1000.0)));
        release = (float) (1.0 - std::exp (-1.0 / (sampleRate * releaseMs[r] / 1000.0)));
        glide   = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.004)));   // (the pedal itself: ~4 ms, so it never steps)
    }

    /** songBeats: where the song is (quarter notes) at the block's start while it plays, else < 0. */
    void process (juce::AudioBuffer<float>& buffer, double bpm, double songBeats) noexcept
    {
        const int n = buffer.getNumSamples();
        const int channels = juce::jmin (2, buffer.getNumChannels());
        if (channels == 0)
            return;

        const float s = juce::jlimit (0.0f, 1.0f, settings.sensitivity);
        const float k = 1.0f / (1.5f + 8.5f * juce::jlimit (0.0f, 1.0f, settings.resonance));   // 1/Q: Q from 1.5 to 10
        const float peakBoost = 1.5f + 2.5f * juce::jlimit (0.0f, 1.0f, settings.resonance);
        const float from = juce::jlimit (minFromHz, maxFromHz, settings.fromHz);
        const float to = juce::jmax (from * 1.2f, juce::jlimit (minToHz, maxToHz, settings.toHz));
        const float span = std::log2 (to / from);
        const bool tempo = settings.mode == Mode::tempo;
        const float sign = settings.direction == Direction::down ? -1.0f : 1.0f;

        double cyclesPerSample = 0.0;
        if (tempo)
        {
            const double beats = juce::jmax (1.0 / 32.0, settings.cycleBeats);
            cyclesPerSample = juce::jlimit (20.0, 999.0, bpm) / 60.0 / beats / sampleRate;
            if (songBeats >= 0.0)
                phase = std::fmod (songBeats / beats, 1.0);
        }

        const float hearCoefficient = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 60.0f / (float) sampleRate);   // (ignores only the deepest lows, as ears do)
        double heardIn = 0.0, heardOut = 0.0;
        float g = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
        float highest = 0.0f, lowest = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            float in[2] { buffer.getSample (0, i), channels > 1 ? buffer.getSample (1, i) : 0.0f };

            // Where the pedal is
            float target;
            const float rest = restPosition.getNextValue();
            if (tempo)
            {
                const float swing = 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * phase);
                phase += cyclesPerSample;
                if (phase >= 1.0) phase -= 1.0;
                target = rest + sign * s * swing;
            }
            else
            {
                const float level = juce::jmax (std::abs (in[0]), std::abs (in[1]));
                envelope += (level > envelope ? attack : release) * (level - envelope);
                const float db = FastMath::gainToDecibels (envelope);
                const float x = juce::jlimit (0.0f, 1.0f, (db + 24.0f * s + 48.0f) / 42.0f);   // (more SENSITIVITY: quieter playing still moves it)
                target = s > 0.0f ? rest + sign * x * (0.3f + 0.7f * s) : rest;
            }
            position += glide * (juce::jlimit (0.0f, 1.0f, target) - position);
            highest = juce::jmax (highest, position);
            lowest = juce::jmin (lowest, position);

            // The filter's settings follow the pedal (every 8 samples is plenty)
            if ((i & 7) == 0)
            {
                const float hz = juce::jmin (from * FastMath::exp2 (span * position), (float) sampleRate * 0.45f);
                g = std::tan (juce::MathConstants<float>::pi * hz / (float) sampleRate);
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            const float wet = mix.getNextValue();
            const float bypass = bypassFade.getNextValue();

            for (int ch = 0; ch < channels; ++ch)
            {
                auto& f = filters[(size_t) ch];
                // (the topology-preserving state-variable filter: stable however fast it moves)
                const float v3 = in[ch] - f.ic2;
                const float v1 = a1 * f.ic1 + a2 * v3;
                const float v2 = f.ic2 + a2 * f.ic1 + a3 * v3;
                f.ic1 = 2.0f * v1 - f.ic1;
                f.ic2 = 2.0f * v2 - f.ic2;
                const float band = k * v1 * peakBoost + 0.2f * v2;   // the resonant peak, with a little of the low end under it

                const float heardA = in[ch] - hear[(size_t) ch].in.lowPass (in[ch], hearCoefficient);
                heardIn += (double) heardA * heardA;
                const float heardB = band - hear[(size_t) ch].out.lowPass (band, hearCoefficient);
                heardOut += (double) heardB * heardB;

                const float matched = band * makeUp;
                const float y = in[ch] + (matched - in[ch]) * wet;
                buffer.setSample (ch, i, y * (1.0f - bypass) + in[ch] * bypass);
            }
        }

        listen (heardIn, heardOut, n);
        makeUp = settings.autoGain ? listener.trim : 1.0f;
        shownLow = lowest;
        shownHigh = highest;
    }

    /** The pedal's lowest and highest positions in the last block (0 heel .. 1 toe), for the window. */
    float getPedalLow() const noexcept    { return shownLow; }
    float getPedalHigh() const noexcept   { return shownHigh; }
    float getMakeUpDb() const noexcept    { return juce::Decibels::gainToDecibels (makeUp); }

    /** The wah's response (dB) at a frequency, with the pedal at `pedal`, for the window's drawing. */
    static float responseDb (const Settings& s, float pedal, double hz, double rate = 48000.0)
    {
        const float from = juce::jlimit (minFromHz, maxFromHz, s.fromHz);
        const float to = juce::jmax (from * 1.2f, juce::jlimit (minToHz, maxToHz, s.toHz));
        const double fc = from * std::pow ((double) to / from, (double) juce::jlimit (0.0f, 1.0f, pedal));
        const double k = 1.0 / (1.5 + 8.5 * s.resonance), boost = 1.5 + 2.5 * s.resonance;
        const double g = std::tan (juce::MathConstants<double>::pi * juce::jmin (fc, rate * 0.45) / rate);
        const double wg = std::tan (juce::MathConstants<double>::pi * juce::jmin (hz, rate * 0.49) / rate) / g;   // (warped, relative)
        const std::complex<double> sj (0.0, wg);
        const auto den = sj * sj + k * sj + 1.0;
        const auto h = (k * sj * boost + 0.2) / den;
        return (float) (20.0 * std::log10 (std::max (1.0e-9, std::abs (h))));
    }

private:
    struct OnePole
    {
        float z = 0.0f;
        float lowPass (float x, float c) noexcept   { z += c * (x - z); return z; }
    };
    struct Filter { float ic1 = 0.0f, ic2 = 0.0f; };
    struct Ears { OnePole in, out; };

    // Compares how loud it sounds going in and coming out (above the deepest lows), and
    // trims the level to match: quick down, gentle up, at most 18 dB either way
    // (a narrow wah on a drum loop can take that much out).
    void listen (double in, double out, int n) noexcept
    {
        auto& l = listener;
        if (in / n < 1.0e-9)
            return;
        const double rate = 1.0 - std::exp (-(double) n / (sampleRate * 1.0));
        l.in += rate * (in / n - l.in);
        l.out += rate * (out / n - l.out);
        const float wanted = juce::jlimit (0.125f, 8.0f, (float) std::sqrt (l.in / juce::jmax (l.out, 1.0e-20)));
        l.trim += (float) (wanted < l.trim ? rate * 2.0 : rate * 0.5) * (wanted - l.trim);
    }

    double sampleRate = 48000.0, phase = 0.0;
    Settings settings;
    juce::SmoothedValue<float> mix, bypassFade, restPosition;
    float envelope = 0.0f, position = 0.0f, attack = 0.0f, release = 0.0f, glide = 0.0f;
    std::array<Filter, 2> filters;
    std::array<Ears, 2> hear;
    struct Listener { double in = 0.0, out = 0.0; float trim = 1.0f; } listener;
    float makeUp = 1.0f, shownLow = 0.0f, shownHigh = 0.0f;
};

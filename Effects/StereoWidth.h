#pragma once

#include "Equalizer.h"
#include <array>

//==============================================================================
/**
    Easy Master's WIDTH: how wide the mix is, and a mono low end.

      WIDTH       The sides' level: 100% = as it is, 0% = mono, up to 200%.
                  The middle (kick, bass, snare, vocal) is never touched, so
                  widening never thins out the centre.
      MONO BASS   Everything below this frequency is taken out of the sides
                  (24 dB per octave): the bass and kick dead centre, tight on
                  any system and safe for vinyl. OFF at 20 Hz.

    Works on left and right in place, no delay. Settings glide (30 ms); the
    mono bass filter is recalculated every 32 samples while it moves, and
    fades in and out when it's switched on and off, so nothing clicks.
    Switched off it glides back to exactly the original.
*/
class StereoWidth
{
public:
    static constexpr float monoBassOffHz = 20.0f, maxWidth = 2.0f;

    struct Settings
    {
        float width = 1.0f;                 // 0 .. 2 (the knob's 0 .. 200%)
        float monoBassHz = monoBassOffHz;   // 20 = OFF
        bool bypassed = false;
    };

    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        for (auto& s : sections) s = {};
        width.reset (sampleRate, 0.03);
        monoAmount.reset (sampleRate, 0.03);
        monoHz.reset (sampleRate, 0.03);
        setSettings (initial, true);
        recalculate();
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        const bool monoOn = ! s.bypassed && s.monoBassHz > monoBassOffHz + 0.5f;
        const float w = s.bypassed ? 1.0f : juce::jlimit (0.0f, maxWidth, s.width);
        const float hz = juce::jmax (monoBassOffHz + 1.0f, s.monoBassHz);
        if (jump)
        {
            width.setCurrentAndTargetValue (w);
            monoAmount.setCurrentAndTargetValue (monoOn ? 1.0f : 0.0f);
            monoHz.setCurrentAndTargetValue (hz);
        }
        else
        {
            width.setTargetValue (w);
            monoAmount.setTargetValue (monoOn ? 1.0f : 0.0f);
            // (while it's off, the filter jumps straight to where it will be needed)
            if (monoAmount.getCurrentValue() <= 0.0f && ! monoOn) monoHz.setCurrentAndTargetValue (hz);
            else monoHz.setTargetValue (hz);
        }
    }

    /** True when it's doing nothing at all (WIDTH 100%, no mono bass, nothing gliding). */
    bool isNeutral() const noexcept
    {
        return ! width.isSmoothing() && ! monoAmount.isSmoothing() && std::abs (width.getCurrentValue() - 1.0f) < 1.0e-6f && monoAmount.getCurrentValue() <= 0.0f;
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (buffer.getNumChannels() < 2) return;
        const int n = buffer.getNumSamples();
        if (isNeutral())
        {
            // (keep the filter's memory fresh, so switching mono bass on later doesn't start from stale numbers)
            for (auto& s : sections) s.z1 = s.z2 = 0.0;
            return;
        }
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getWritePointer (1);
        for (int start = 0; start < n; start += 32)
        {
            const int len = juce::jmin (32, n - start);
            if (monoHz.isSmoothing()) { monoHz.skip (len); recalculate(); }
            for (int i = start; i < start + len; ++i)
            {
                const double m = 0.5 * ((double) left[i] + right[i]);
                double s = 0.5 * ((double) left[i] - right[i]);
                const double filtered = sections[1].run (sections[0].run (s));
                const double a = monoAmount.getNextValue();
                s = (s * (1.0 - a) + filtered * a) * width.getNextValue();
                left[i] = (float) (m + s);
                right[i] = (float) (m - s);
            }
        }
    }

    /** The response of the sides (not in dB) at a frequency - for the window. */
    static double sideResponseAt (const Settings& s, double hz, double sampleRate) noexcept
    {
        double g = s.bypassed ? 1.0 : s.width;
        if (! s.bypassed && s.monoBassHz > monoBassOffHz + 0.5f)
            g *= Equalizer::highPass (s.monoBassHz, Equalizer::q24a, sampleRate).magnitudeAt (hz, sampleRate)
               * Equalizer::highPass (s.monoBassHz, Equalizer::q24b, sampleRate).magnitudeAt (hz, sampleRate);
        return g;
    }

private:
    struct Section   // one biquad, transposed direct form II, in double
    {
        Equalizer::Coefficients c;
        double z1 = 0.0, z2 = 0.0;
        double run (double x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    void recalculate() noexcept
    {
        const double hz = monoHz.getCurrentValue();
        sections[0].c = Equalizer::highPass (hz, Equalizer::q24a, sampleRate);
        sections[1].c = Equalizer::highPass (hz, Equalizer::q24b, sampleRate);
    }

    double sampleRate = 48000.0;
    std::array<Section, 2> sections;
    juce::SmoothedValue<float> width, monoAmount, monoHz;
};

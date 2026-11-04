#pragma once

#include "Equalizer.h"
#include <array>

//==============================================================================
/**
    Easy Master's EQ: gentle, broad tone shaping for a whole mix - with a
    complete EQ for the MIDDLE and another for the SIDES.

      Per part: LOW CUT (12 dB/oct, OFF at 20 Hz), LOW SHELF, LOW MID,
      MID and HIGH MID bells, HIGH SHELF. Gentle, passive-style curves: the
      bells keep the same broad width whatever the gain (Q 0.55 - about two
      and a half octaves; no narrowing as you push, unlike Easy EQ), the
      shelves slope in slowly (Q 0.5). All of them are Easy EQ's "matched"
      filters, so the top end is right up to 20 kHz with no oversampling,
      and nothing is delayed.

      STEREO: the MID settings for the whole mix (exactly the same as
      equalising left and right). MID + SIDE: the middle and the sides each
      get their own set - e.g. a low cut on the sides only (tight, mono
      bass), air on the sides, a touch of low-mid out of the middle.

    Settings glide (30 ms), with the filters recalculated every 32 samples
    while they do. A band at 0 dB and a LOW CUT at OFF are skipped.
*/
class MasteringEQ
{
public:
    static constexpr int numBands = 5;     // LOW SHELF, LOW MID, MID, HIGH MID, HIGH SHELF
    static constexpr float lowCutOffHz = 20.0f, maxGainDb = 12.0f;

    struct Part
    {
        std::array<float, numBands> freqHz { 80.0f, 250.0f, 1000.0f, 3500.0f, 10000.0f };
        std::array<float, numBands> gainDb {};
        float lowCutHz = lowCutOffHz;
    };

    struct Settings
    {
        Part mid, side;
        bool midSide = false;
        bool bypassed = false;
    };

    static constexpr double bellQ = 0.55, shelfQ = 0.5;

    static Equalizer::Coefficients bell (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        return Equalizer::matched (1.0, A / bellQ, 1.0, 1.0, 1.0 / (A * bellQ), 1.0, hz, sampleRate);
    }
    static Equalizer::Coefficients lowShelf (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = std::sqrt (A) / shelfQ;
        return Equalizer::matched (A, A * k, A * A, A, k, 1.0, hz, sampleRate);
    }
    static Equalizer::Coefficients highShelf (double hz, double gainDb, double sampleRate) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0), k = std::sqrt (A) / shelfQ;
        return Equalizer::matched (A * A, A * k, A, 1.0, k, A, hz, sampleRate);
    }

    /** One band's (or the LOW CUT's, band = numBands) filter. */
    static Equalizer::Coefficients coefficients (const Part& p, int band, double sampleRate) noexcept
    {
        if (band == numBands)  return Equalizer::highPass (p.lowCutHz, Equalizer::q12, sampleRate);
        if (band == 0)         return lowShelf (p.freqHz[0], p.gainDb[0], sampleRate);
        if (band == numBands - 1) return highShelf (p.freqHz[(size_t) band], p.gainDb[(size_t) band], sampleRate);
        return bell (p.freqHz[(size_t) band], p.gainDb[(size_t) band], sampleRate);
    }

    /** A part's whole curve (not in dB) at a frequency - for the window. */
    static double responseAt (const Part& p, double hz, double sampleRate) noexcept
    {
        double g = 1.0;
        for (int b = 0; b < numBands; ++b)
            if (std::abs (p.gainDb[(size_t) b]) > 0.001f)
                g *= coefficients (p, b, sampleRate).magnitudeAt (hz, sampleRate);
        if (p.lowCutHz > lowCutOffHz + 0.5f)
            g *= coefficients (p, numBands, sampleRate).magnitudeAt (hz, sampleRate);
        return g;
    }

    //==========================================================================
    void prepare (double newSampleRate, const Settings& initial)
    {
        sampleRate = newSampleRate;
        for (auto& part : parts)
        {
            for (auto& s : part.sections) s = {};
            for (auto& v : part.freq) v.reset (sampleRate, 0.03);
            for (auto& v : part.gain) v.reset (sampleRate, 0.03);
            part.lowCut.reset (sampleRate, 0.03);
        }
        bypassFade.reset (sampleRate, 0.02);
        setSettings (initial, true);
        for (int p = 0; p < 2; ++p) recalculate (p);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        settings = s;
        for (int p = 0; p < 2; ++p)
        {
            // (STEREO: the sides follow the middle's settings)
            const auto& source = (p == 1 && s.midSide) ? s.side : s.mid;
            auto& part = parts[(size_t) p];
            const auto set = [jump] (juce::SmoothedValue<float>& v, float t) { if (jump) v.setCurrentAndTargetValue (t); else v.setTargetValue (t); };
            for (int b = 0; b < numBands; ++b)
            {
                set (part.freq[(size_t) b], source.freqHz[(size_t) b]);
                set (part.gain[(size_t) b], juce::jlimit (-maxGainDb, maxGainDb, source.gainDb[(size_t) b]));
            }
            set (part.lowCut, juce::jmax (lowCutOffHz, source.lowCutHz));
        }
        if (jump) bypassFade.setCurrentAndTargetValue (s.bypassed ? 1.0f : 0.0f);
        else      bypassFade.setTargetValue (s.bypassed ? 1.0f : 0.0f);
    }

    /** Works on left and right in place. */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const bool stereo = buffer.getNumChannels() >= 2;
        float* left = buffer.getWritePointer (0);
        float* right = stereo ? buffer.getWritePointer (1) : nullptr;

        for (int start = 0; start < n; start += 32)
        {
            const int len = juce::jmin (32, n - start);
            for (int p = 0; p < 2; ++p)
                if (parts[(size_t) p].isGliding())
                {
                    parts[(size_t) p].skip (len);
                    recalculate (p);
                }

            for (int i = start; i < start + len; ++i)
            {
                const float l = left[i], r = stereo ? right[i] : left[i];
                double m = stereo ? 0.5 * (l + r) : l, s = stereo ? 0.5 * (l - r) : 0.0;
                m = parts[0].run (m);
                if (stereo) s = parts[1].run (s);
                const float b = bypassFade.getNextValue();
                const float outL = (float) (m + s), outR = (float) (m - s);
                left[i] = b >= 1.0f ? l : outL * (1.0f - b) + l * b;
                if (stereo) right[i] = b >= 1.0f ? r : outR * (1.0f - b) + r * b;
            }
        }
    }

private:
    struct Section   // one biquad, transposed direct form II, in double
    {
        Equalizer::Coefficients c;
        double z1 = 0.0, z2 = 0.0;
        bool active = false;
        double run (double x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };

    struct PartState
    {
        std::array<Section, numBands + 1> sections;
        std::array<juce::SmoothedValue<float>, numBands> freq, gain;
        juce::SmoothedValue<float> lowCut;

        bool isGliding() const noexcept
        {
            for (auto& v : freq) if (v.isSmoothing()) return true;
            for (auto& v : gain) if (v.isSmoothing()) return true;
            return lowCut.isSmoothing();
        }
        void skip (int samples) noexcept
        {
            for (auto& v : freq) v.skip (samples);
            for (auto& v : gain) v.skip (samples);
            lowCut.skip (samples);
        }
        double run (double x) noexcept
        {
            for (auto& s : sections)
                if (s.active) x = s.run (x);
            return x;
        }
    };

    void recalculate (int p) noexcept
    {
        auto& part = parts[(size_t) p];
        Part now;
        for (int b = 0; b < numBands; ++b)
        {
            now.freqHz[(size_t) b] = part.freq[(size_t) b].getCurrentValue();
            now.gainDb[(size_t) b] = part.gain[(size_t) b].getCurrentValue();
        }
        now.lowCutHz = part.lowCut.getCurrentValue();
        for (int b = 0; b <= numBands; ++b)
        {
            auto& s = part.sections[(size_t) b];
            const bool active = b == numBands ? now.lowCutHz > lowCutOffHz + 0.5f : std::abs (now.gainDb[(size_t) b]) > 0.001f;
            if (! active)
            {
                if (s.active) { s.z1 = s.z2 = 0.0; }
                s.active = false;
                continue;
            }
            s.c = coefficients (now, b, sampleRate);
            s.active = true;
        }
    }

    Settings settings;
    double sampleRate = 48000.0;
    std::array<PartState, 2> parts;
    juce::SmoothedValue<float> bypassFade;
};

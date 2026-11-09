#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <complex>

//==============================================================================
/**
    PULTEC: a passive valve EQ in the spirit of the Pultec EQP-1A - broad,
    musical curves rather than surgical ones, through a valve make-up stage.

    LOW: one frequency (20, 30, 60 or 100 Hz), with BOOST and ATTEN on it.
    The famous trick is to turn up both at once: the attenuation's curve is
    gentler and starts higher, so together they give a big, round low end
    WITH a dip just above it - more weight, less mud.

    HIGH BOOST: a broad bell at 3k - 16k; BANDWIDTH from sharp (0) to very
    broad (10). HIGH ATTEN: a gentle shelf down above 5k, 10k or 20k, to
    smooth the very top (often used alongside the boost: lift the presence,
    tame the fizz).

    The knobs go 0 - 10 like the original (BOOST up to about +15 dB low /
    +18 dB high; ATTEN down to about -18 dB low / -16 dB high), so small
    numbers are already plenty on a master. The valve stage adds a faint
    2nd harmonic (well under 0.1% at normal levels). OFF is untouched.
*/
class PultecEQ
{
public:
    static constexpr int numLowFreqs = 4, numHighFreqs = 7, numAttenFreqs = 3;
    static float lowFreqHz (int i)    { static const float f[numLowFreqs]   { 20.0f, 30.0f, 60.0f, 100.0f }; return f[juce::jlimit (0, numLowFreqs - 1, i)]; }
    static float highFreqHz (int i)   { static const float f[numHighFreqs]  { 3000.0f, 4000.0f, 5000.0f, 8000.0f, 10000.0f, 12000.0f, 16000.0f }; return f[juce::jlimit (0, numHighFreqs - 1, i)]; }
    static float attenFreqHz (int i)  { static const float f[numAttenFreqs] { 5000.0f, 10000.0f, 20000.0f }; return f[juce::jlimit (0, numAttenFreqs - 1, i)]; }

    struct Settings
    {
        bool on = false;
        int lowFreq = 2;          // 60 Hz
        float lowBoost = 0.0f, lowAtten = 0.0f;           // 0..10
        int highFreq = 4;         // 10k
        float bandwidth = 5.0f, highBoost = 0.0f;         // 0..10
        int attenFreq = 2;        // 20k
        float highAtten = 0.0f;   // 0..10
    };

    // The response at one frequency (dB), for the window's curve and the tests
    float responseDb (double hz) const noexcept
    {
        double total = 0.0;
        for (auto* b : { &lowBoost, &lowCut, &highBell, &highCut }) total += b->magnitudeDb (hz, rate);
        return (float) total;
    }

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        configure (wanted);
        for (auto& c : channels) c = {};
        fade.reset (sampleRate, 0.02);
        fade.setCurrentAndTargetValue (wanted.on ? 1.0f : 0.0f);
    }

    void setSettings (const Settings& s) noexcept
    {
        const bool changed = s.lowFreq != wanted.lowFreq || s.highFreq != wanted.highFreq || s.attenFreq != wanted.attenFreq
                          || std::abs (s.lowBoost - wanted.lowBoost) > 1.0e-4f || std::abs (s.lowAtten - wanted.lowAtten) > 1.0e-4f
                          || std::abs (s.bandwidth - wanted.bandwidth) > 1.0e-4f || std::abs (s.highBoost - wanted.highBoost) > 1.0e-4f
                          || std::abs (s.highAtten - wanted.highAtten) > 1.0e-4f;
        wanted = s;
        if (changed) configure (s);   // (knob moves are small steps: the filters keep their memory, no clicks)
        fade.setTargetValue (s.on ? 1.0f : 0.0f);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! wanted.on && ! fade.isSmoothing() && fade.getCurrentValue() <= 0.0f) return;   // (OFF: untouched)
        const int n = buffer.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const float w = fade.getNextValue();
            for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            {
                float* x = buffer.getWritePointer (ch);
                auto& c = channels[(size_t) ch];
                double y = x[i];
                y = lowBoost.run (y, c.s[0]);
                y = lowCut.run (y, c.s[1]);
                y = highBell.run (y, c.s[2]);
                y = highCut.run (y, c.s[3]);
                // The valve make-up stage: a faint 2nd harmonic (its DC taken out)
                const double u = y * 1.4;
                double v = y + 0.02 * (u * u) / (1.0 + u * u) / 1.4;
                const double dc = v - c.dcIn + dcKeep * c.dcOut;
                c.dcIn = v; c.dcOut = dc; v = dc;
                x[i] = (float) (x[i] * (1.0f - w) + v * w);
            }
        }
    }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double run (double x, std::array<double, 2>& z) const noexcept
        {
            const double y = b0 * x + z[0];
            z[0] = b1 * x - a1 * y + z[1];
            z[1] = b2 * x - a2 * y;
            return y;
        }
        double magnitudeDb (double hz, double sr) const noexcept
        {
            const std::complex<double> zi = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / sr);
            const auto h = (b0 + b1 * zi + b2 * zi * zi) / (1.0 + a1 * zi + a2 * zi * zi);
            return 20.0 * std::log10 (std::abs (h) + 1.0e-12);
        }
    };

    static Biquad shelf (double sr, double hz, double db, double slope, bool high)
    {
        Biquad b;
        if (std::abs (db) < 1.0e-4 || hz >= sr * 0.49) return b;
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * hz / sr;
        const double cs = std::cos (w), sn = std::sin (w);
        const double alpha = sn / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double sq = 2.0 * std::sqrt (A) * alpha;
        double b0, b1, b2, a0, a1, a2;
        if (high)
        {
            b0 = A * ((A + 1) + (A - 1) * cs + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cs); b2 = A * ((A + 1) + (A - 1) * cs - sq);
            a0 = (A + 1) - (A - 1) * cs + sq;       a1 = 2 * ((A - 1) - (A + 1) * cs);      a2 = (A + 1) - (A - 1) * cs - sq;
        }
        else
        {
            b0 = A * ((A + 1) - (A - 1) * cs + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cs);  b2 = A * ((A + 1) - (A - 1) * cs - sq);
            a0 = (A + 1) + (A - 1) * cs + sq;       a1 = -2 * ((A - 1) + (A + 1) * cs);     a2 = (A + 1) + (A - 1) * cs - sq;
        }
        b.b0 = b0 / a0; b.b1 = b1 / a0; b.b2 = b2 / a0; b.a1 = a1 / a0; b.a2 = a2 / a0;
        return b;
    }

    static Biquad bell (double sr, double hz, double db, double q)
    {
        Biquad b;
        if (std::abs (db) < 1.0e-4 || hz >= sr * 0.49) return b;
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * hz / sr;
        const double alpha = std::sin (w) / (2.0 * q), cs = std::cos (w), a0 = 1 + alpha / A;
        b.b0 = (1 + alpha * A) / a0; b.b1 = -2 * cs / a0; b.b2 = (1 - alpha * A) / a0; b.a1 = -2 * cs / a0; b.a2 = (1 - alpha / A) / a0;
        return b;
    }

    void configure (const Settings& s)
    {
        const double f = lowFreqHz (s.lowFreq);
        // LOW: the boost a little resonant (a soft bump at the frequency); the cut gentler and starting higher
        // (fitted to the original's well-known curves: BOOST 5 + ATTEN 4 at 60 Hz gives about +3.5 dB at 40 Hz and -1.5 dB around 200)
        lowBoost = shelf (rate, f * 1.22, 1.535 * juce::jlimit (0.0f, 10.0f, s.lowBoost), 1.27, false);
        lowCut   = shelf (rate, f * 0.89, -1.8 * juce::jlimit (0.0f, 10.0f, s.lowAtten), 0.3, false);
        // HIGH BOOST: a bell, BANDWIDTH from sharp (Q 2.5) to very broad (Q 0.35)
        const double q = 2.5 * std::pow (0.35 / 2.5, juce::jlimit (0.0f, 10.0f, s.bandwidth) / 10.0);
        highBell = bell (rate, juce::jmin ((double) highFreqHz (s.highFreq), rate * 0.45), 1.8 * juce::jlimit (0.0f, 10.0f, s.highBoost), q);
        // HIGH ATTEN: a gentle shelf down
        highCut  = shelf (rate, juce::jmin ((double) attenFreqHz (s.attenFreq), rate * 0.45), -1.6 * juce::jlimit (0.0f, 10.0f, s.highAtten), 0.6, true);
        dcKeep = std::exp (-juce::MathConstants<double>::twoPi * 6.0 / rate);
    }

    struct Channel { std::array<std::array<double, 2>, 4> s {}; double dcIn = 0.0, dcOut = 0.0; };

    double rate = 48000.0, dcKeep = 0.999;
    Settings wanted;
    Biquad lowBoost, lowCut, highBell, highCut;
    std::array<Channel, 2> channels {};
    juce::SmoothedValue<float> fade { 0.0f };
};

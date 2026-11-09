#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    VARI-MU: a valve compressor in the spirit of the Fairchild 670 and the
    Manley Vari-Mu - the classic mastering glue.

    A vari-mu valve doesn't have a fixed ratio: the harder it's pushed, the
    harder it squeezes. Just over the THRESHOLD it barely touches the sound
    (about 1.5:1); 10 dB over, about 4:1; 20 dB over, about 6.5:1 - a very
    soft, smooth curve that's almost impossible to make sound grabby. Its
    speeds are slow and gentle, and the valves add a little warmth (a 2nd
    harmonic that grows with how much it's turning down).

    TIME: FAST (5 ms / 0.3 s), MEDIUM (15 ms / 0.8 s), SLOW (30 ms / 2 s),
    AUTO (15 ms, then a quick 0.3 s recovery after short peaks and a slow
    4 s one after long loud passages - the Fairchild's famous program-
    dependent setting). MAKE-UP and MIX (for parallel valve compression).
    Left and right are turned down together. OFF is untouched.
*/
class VariMu
{
public:
    enum Time { fast = 0, medium, slow, autoTime, numTimes };
    static const char* timeName (int t)   { static const char* n[numTimes] { "FAST", "MEDIUM", "SLOW", "AUTO" }; return n[juce::jlimit (0, numTimes - 1, t)]; }

    struct Settings
    {
        bool on = false;
        float thresholdDb = -18.0f;
        int time = autoTime;
        float makeupDb = 0.0f;
        float mix = 1.0f;   // 0..1
    };

    /** How much it turns down at a level `over` dB above the threshold (the vari-mu curve: the ratio grows as it's pushed). */
    static float reductionFor (float overDb) noexcept
    {
        // (a soft 6 dB knee into a ratio of 1.5 + over/4 - so 1.5:1 at the threshold, ~4:1 at 10 dB over, ~6.5:1 at 20)
        const float knee = 6.0f;
        float o = overDb;
        if (o < -knee * 0.5f) return 0.0f;
        if (o < knee * 0.5f) { const float t = o + knee * 0.5f; o = t * t / (2.0f * knee); }
        const float ratio = 1.5f + juce::jmax (0.0f, overDb) / 4.0f;
        return o * (1.0f - 1.0f / ratio);
    }

    void prepare (double sampleRate)
    {
        rate = sampleRate;
        envelope = 0.0; grFast = grSlow = 0.0f;
        for (auto& c : channels) c = {};
        fade.reset (sampleRate, 0.02);
        fade.setCurrentAndTargetValue (settings.on ? 1.0f : 0.0f);
        makeup.reset (sampleRate, 0.05);
        makeup.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (settings.makeupDb));
        detectKeep = std::exp (-1.0 / (0.005 * sampleRate));   // (an RMS-like 5 ms level)
        dcKeep = std::exp (-juce::MathConstants<double>::twoPi * 6.0 / sampleRate);
        configureTimes();
    }

    void setSettings (const Settings& s) noexcept
    {
        const bool timeChanged = s.time != settings.time;
        settings = s;
        fade.setTargetValue (s.on ? 1.0f : 0.0f);
        makeup.setTargetValue (juce::Decibels::decibelsToGain (s.makeupDb));
        if (timeChanged) configureTimes();
    }

    float getReductionDb() const noexcept   { return shownReduction.load(); }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        if (! settings.on && ! fade.isSmoothing() && fade.getCurrentValue() <= 0.0f) { shownReduction.store (0.0f); grFast = grSlow = 0.0f; return; }
        const int n = buffer.getNumSamples();
        const int chs = juce::jmin (2, buffer.getNumChannels());
        float* l = buffer.getWritePointer (0);
        float* r = chs > 1 ? buffer.getWritePointer (1) : l;
        float most = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            // The level (both sides together), then how far over the threshold
            const double sq = 0.5 * ((double) l[i] * l[i] + (double) r[i] * r[i]);
            envelope = detectKeep * envelope + (1.0 - detectKeep) * sq;
            const float levelDb = (float) (10.0 * std::log10 (envelope + 1.0e-20));
            const float target = reductionFor (levelDb - settings.thresholdDb);

            // The valves' speeds (AUTO: a quick recovery after short peaks, a slow one after long loud stretches)
            grFast += (target > grFast ? attackCoeff : releaseCoeff) * (target - grFast);
            float gr = grFast;
            if (settings.time == autoTime)
            {
                grSlow += (target > grSlow ? slowAttackCoeff : slowReleaseCoeff) * (target - grSlow);
                gr = juce::jmax (grFast, grSlow);
            }
            most = juce::jmax (most, gr);

            const float gain = juce::Decibels::decibelsToGain (-gr) * makeup.getNextValue();
            const float w = fade.getNextValue();
            const float wet = w * settings.mix;
            // The valves' warmth: a 2nd harmonic that grows with how hard it's working
            const double k = 0.006 + 0.003 * juce::jmin (gr, 12.0f);
            for (int ch = 0; ch < chs; ++ch)
            {
                float* x = ch == 0 ? l : r;
                auto& c = channels[(size_t) ch];
                const double y = x[i] * gain;
                const double u = y * 1.4;
                double v = y + k * (u * u) / (1.0 + u * u) / 1.4;
                const double dc = v - c.dcIn + dcKeep * c.dcOut;
                c.dcIn = v; c.dcOut = dc;
                x[i] = (float) (x[i] * (1.0f - wet) + dc * wet);
            }
        }
        shownReduction.store (most * settings.mix);
    }

private:
    void configureTimes() noexcept
    {
        static const float attackMs[numTimes]  { 5.0f, 15.0f, 30.0f, 15.0f };
        static const float releaseMs[numTimes] { 300.0f, 800.0f, 2000.0f, 300.0f };
        const auto coeff = [this] (float ms) { return (float) (1.0 - std::exp (-1.0 / (0.001 * ms * rate))); };
        attackCoeff = coeff (attackMs[settings.time]);
        releaseCoeff = coeff (releaseMs[settings.time]);
        slowAttackCoeff = coeff (1000.0f);    // (only long loud passages build up the slow part)
        slowReleaseCoeff = coeff (4000.0f);
    }

    struct Channel { double dcIn = 0.0, dcOut = 0.0; };

    Settings settings;
    double rate = 48000.0, envelope = 0.0, detectKeep = 0.99, dcKeep = 0.999;
    float grFast = 0.0f, grSlow = 0.0f;
    float attackCoeff = 0.01f, releaseCoeff = 0.001f, slowAttackCoeff = 0.0001f, slowReleaseCoeff = 0.00001f;
    std::array<Channel, 2> channels {};
    juce::SmoothedValue<float> fade { 0.0f }, makeup { 1.0f };
    std::atomic<float> shownReduction { 0.0f };
};

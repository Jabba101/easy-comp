#pragma once

#include "TransientShaper.h"
#include "Equalizer.h"
#include <array>
#include <vector>

//==============================================================================
/**
    Easy Master's PUNCH: the Easy Trans engine, made to hear the hits in a
    whole, finished mix.

    In a dense mix the sustained parts (guitars, pads, a held bass note) fill
    in the gaps a transient shaper listens for, so on the whole mix it hears
    almost nothing. So this one listens three ways at once - to the whole
    mix, to the lows only (under 150 Hz: the kick) and to the top only (above
    2.5 kHz: the snare's crack, the hats) - and follows whichever hears the
    hit most clearly at each moment. The change is then made to the whole
    sound (one volume change per sample, the same on every channel of the
    part), so nothing is filtered and nothing is delayed.

    PUNCH, SUSTAIN and MIX as in Easy Trans. With PUNCH and SUSTAIN at 0% it
    changes nothing at all; switched off it fades (20 ms) to exactly the
    original.
*/
class MixPunch
{
public:
    static constexpr double lowHz = 150.0, highHz = 2500.0;
    static constexpr int numListeners = 3;   // the whole mix, the lows, the top

    struct Settings
    {
        float punch = 0.0f, sustain = 0.0f;   // -1 .. +1
        float mix = 1.0f;                     //  0 .. 1
        bool bypassed = false;
    };
    struct BlockResult { float maxBoostDb = 0.0f, maxCutDb = 0.0f; };

    void prepare (double sampleRate, int maxBlock, int channels, const Settings& initial)
    {
        rate = sampleRate;
        numChannels = juce::jlimit (1, 2, channels);
        TransientShaper::Settings ts;
        ts.punch = initial.punch; ts.sustain = initial.sustain;
        for (auto& l : listeners) l.prepare (sampleRate, ts);
        for (auto& f : filters) for (auto& s : f) s = {};
        for (int c = 0; c < 2; ++c)
        {
            filters[0][(size_t) c * 2].c     = Equalizer::lowPass (lowHz, Equalizer::q24a, sampleRate);
            filters[0][(size_t) c * 2 + 1].c = Equalizer::lowPass (lowHz, Equalizer::q24b, sampleRate);
            filters[1][(size_t) c * 2].c     = Equalizer::highPass (highHz, Equalizer::q24a, sampleRate);
            filters[1][(size_t) c * 2 + 1].c = Equalizer::highPass (highHz, Equalizer::q24b, sampleRate);
        }
        for (auto& b : bands) b.setSize (2, juce::jmax (1, maxBlock));
        for (auto& c : changes) c.assign ((size_t) juce::jmax (1, maxBlock), 0.0f);
        mix.reset (sampleRate, 0.05);
        bypass.reset (sampleRate, 0.02);
        setSettings (initial, true);
    }

    void setSettings (const Settings& s, bool jump = false)
    {
        TransientShaper::Settings ts;
        ts.punch = s.punch; ts.sustain = s.sustain;
        for (auto& l : listeners) l.setSettings (ts, jump);
        if (jump) { mix.setCurrentAndTargetValue (s.mix); bypass.setCurrentAndTargetValue (s.bypassed ? 1.0f : 0.0f); }
        else      { mix.setTargetValue (s.mix);           bypass.setTargetValue (s.bypassed ? 1.0f : 0.0f); }
    }

    /** Works in place on the part (one channel - the middle or the sides - or left and right). */
    BlockResult process (juce::AudioBuffer<float>& buffer) noexcept
    {
        BlockResult result;
        const int most = bands[0].getNumSamples();
        for (int start = 0; start < buffer.getNumSamples(); start += most)
        {
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, juce::jmin (most, buffer.getNumSamples() - start));
            const auto r = processChunk (part);
            result.maxBoostDb = juce::jmax (result.maxBoostDb, r.maxBoostDb);
            result.maxCutDb = juce::jmax (result.maxCutDb, r.maxCutDb);
        }
        return result;
    }

private:
    BlockResult processChunk (juce::AudioBuffer<float>& buffer) noexcept
    {
        BlockResult result;
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (numChannels, buffer.getNumChannels());
        if (n <= 0) return result;
        if (! bypass.isSmoothing() && bypass.getCurrentValue() >= 1.0f)
        {
            // (off: keep listening so it's ready, but leave the sound alone)
            listen (buffer, n, chans);
            return result;
        }
        listen (buffer, n, chans);
        for (int i = 0; i < n; ++i)
        {
            // (whichever listener hears the most - up or down - decides)
            float change = changes[0][(size_t) i];
            for (int l = 1; l < numListeners; ++l)
                if (std::abs (changes[(size_t) l][(size_t) i]) > std::abs (change)) change = changes[(size_t) l][(size_t) i];
            const float wet = mix.getNextValue() * (1.0f - bypass.getNextValue());
            const float shownDb = change * wet;
            result.maxBoostDb = juce::jmax (result.maxBoostDb, shownDb);
            result.maxCutDb = juce::jmax (result.maxCutDb, -shownDb);
            const float gain = 1.0f + wet * (juce::Decibels::decibelsToGain (change) - 1.0f);
            for (int c = 0; c < chans; ++c) buffer.getWritePointer (c)[i] *= gain;
        }
        return result;
    }

    struct Section
    {
        Equalizer::Coefficients c;
        double z1 = 0.0, z2 = 0.0;
        float run (float x) noexcept
        {
            const double y = c.b0 * x + z1;
            z1 = c.b1 * x - c.a1 * y + z2;
            z2 = c.b2 * x - c.a2 * y;
            return (float) y;
        }
    };

    void listen (const juce::AudioBuffer<float>& buffer, int n, int chans) noexcept
    {
        listeners[0].analyse (buffer.getArrayOfReadPointers(), chans, n, changes[0].data());
        for (int band = 0; band < 2; ++band)
        {
            auto& copy = bands[(size_t) band];
            for (int c = 0; c < chans; ++c)
            {
                const float* in = buffer.getReadPointer (c);
                float* out = copy.getWritePointer (c);
                auto& a = filters[(size_t) band][(size_t) c * 2];
                auto& b = filters[(size_t) band][(size_t) c * 2 + 1];
                for (int i = 0; i < n; ++i) out[i] = b.run (a.run (in[i]));
            }
            listeners[(size_t) band + 1].analyse (copy.getArrayOfReadPointers(), chans, n, changes[(size_t) band + 1].data());
        }
    }

    double rate = 48000.0;
    int numChannels = 2;
    std::array<TransientShaper, numListeners> listeners;
    std::array<std::array<Section, 4>, 2> filters;   // (the lows, the top: two sections per channel)
    std::array<juce::AudioBuffer<float>, 2> bands;
    std::array<std::vector<float>, numListeners> changes;
    juce::SmoothedValue<float> mix, bypass;
};

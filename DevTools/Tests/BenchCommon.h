#pragma once
// Shared by the timing programs (not shipped): a drum loop, and "% of one core".
#include "TestHelpers.h"
#include <chrono>

namespace bench
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    inline juce::AudioBuffer<float> music (double seconds, int seed = 7, double tune = 220.0)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random random (seed);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, beat = std::fmod (t, 0.5);
            const float kick = (float) (0.9 * std::exp (-beat * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-beat * 40.0)) * beat));
            const double off = std::fmod (t + 0.25, 0.5);
            const float snare = (float) (0.5 * std::exp (-off * 25.0)) * (random.nextFloat() * 2.0f - 1.0f);
            const float bed = 0.08f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * tune * t) + 0.02f * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, kick + snare + bed);
            b.setSample (1, i, kick + 0.9f * snare + bed);
        }
        return b;
    }

    template <typename P> void setNamed (P& p, const juce::String& id, float value)
    {
        if (auto* prm = p.parameters.getParameter (id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        else
            std::cout << "  (no setting called " << id << ")\n";
    }

    /** % of one core. source: as many channels as the processor takes in (main, then any sidechain). */
    inline double measure (juce::AudioProcessor& p, const juce::AudioBuffer<float>& source, bool silent, double seconds = 12.0)
    {
        juce::MidiBuffer midi;
        const int channels = source.getNumChannels();
        juce::AudioBuffer<float> work (channels, block);
        const int blocks = (int) (seconds * sr / block), warm = (int) (2.0 * sr / block);
        int pos = 0;
        const auto run = [&] (int count, bool quiet)
        {
            for (int b = 0; b < count; ++b)
            {
                for (int ch = 0; ch < channels; ++ch)
                {
                    if (quiet) work.clear (ch, 0, block);
                    else       work.copyFrom (ch, 0, source, ch, pos, block);
                }
                pos = (pos + block) % (source.getNumSamples() - block);
                p.processBlock (work, midi);
            }
        };
        run (warm, false);
        if (silent) run (warm * 4, true);
        double best = 1.0e9;
        for (int pass = 0; pass < 3; ++pass)
        {
            const auto t0 = std::chrono::steady_clock::now();
            run (blocks, silent);
            best = juce::jmin (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
        }
        return 100.0 * best / (blocks * block / sr);
    }

    inline void line (const juce::String& name, double playing, double silent)
    {
        std::cout << "  " << name.paddedRight (' ', 40) << juce::String (playing, 2).paddedLeft (' ', 7) << " %"
                  << juce::String (silent, 2).paddedLeft (' ', 10) << " %\n";
    }
}

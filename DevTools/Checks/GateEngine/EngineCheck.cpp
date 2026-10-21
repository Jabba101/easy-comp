#include <juce_audio_basics/juce_audio_basics.h>
#include "Gate.h"
#include <iostream>

static float toDb (float g) { return juce::Decibels::gainToDecibels (g, -200.0f); }
static float fromDb (float d) { return juce::Decibels::decibelsToGain (d, -200.0f); }

int main()
{
    juce::ScopedNoDenormals nd;
    const double sr = 48000.0;
    Gate::Settings s;
    s.thresholdDb = -30.0f; s.rangeDb = 40.0f; s.attackMs = 0.5f; s.holdMs = 50.0f; s.releaseMs = 100.0f;

    // 1. A loud steady sine: once open, output == input delayed, bit for bit
    {
        Gate g; g.prepare (sr, 240, s);
        const int D = g.getLatencySamples();
        std::cout << "latency " << D << "\n";
        juce::AudioBuffer<float> in (2, 48000);
        for (int i = 0; i < in.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch) in.setSample (ch, i, 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * 100.0 * i / sr));
        juce::AudioBuffer<float> out (in);
        for (int start = 0; start < out.getNumSamples(); start += 512)
        {
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, start, std::min (512, out.getNumSamples() - start));
            g.process (v);
        }
        int mismatches = 0;
        for (int i = 4800; i < in.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
                if (out.getSample (ch, i) != in.getSample (ch, i - D)) ++mismatches;
        std::cout << "open sine: mismatches after 100 ms = " << mismatches << "\n";
        // first samples: the opening
        for (int i = D - 2; i < D + 30; i += 4) std::cout << "  i=" << i << " in(i-D)=" << (i >= D ? in.getSample (0, i - D) : 0.0f) << " out=" << out.getSample (0, i) << "\n";
    }

    // 2. Quiet noise below the threshold: turned down by 40 dB
    {
        Gate g; g.prepare (sr, 240, s);
        const int D = g.getLatencySamples();
        juce::AudioBuffer<float> in (2, 48000);
        juce::Random r (1);
        for (int i = 0; i < in.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch) in.setSample (ch, i, fromDb (-50.0f) * (r.nextFloat() * 2 - 1));
        juce::AudioBuffer<float> out (in);
        g.process (out);
        float worst = 0;
        for (int i = D; i < in.getNumSamples(); ++i)
            if (std::abs (in.getSample (0, i - D)) > 1e-6f)
                worst = std::max (worst, std::abs (out.getSample (0, i) / in.getSample (0, i - D) - fromDb (-40.0f)));
        std::cout << "noise below threshold: gain error " << worst << " (closed gain " << fromDb (-40.0f) << ")\n";
    }

    // 3. A hit over a noise floor: the very start of the hit comes through untouched
    {
        Gate g; g.prepare (sr, 240, s);
        const int D = g.getLatencySamples();
        juce::AudioBuffer<float> in (1, 96000);
        juce::Random r (2);
        const int hitAt = 48000;
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            float x = fromDb (-60.0f) * (r.nextFloat() * 2 - 1);
            if (i >= hitAt) { const double t = (i - hitAt) / sr; x += (float) (0.5 * std::exp (-t / 0.1) * std::sin (2 * juce::MathConstants<double>::pi * 150 * t)); }
            in.setSample (0, i, x);
        }
        juce::AudioBuffer<float> out (in);
        std::vector<Gate::Moment> moments;
        for (int start = 0; start < out.getNumSamples(); start += 256)
        {
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 1, start, std::min (256, out.getNumSamples() - start));
            g.process (v, nullptr, [&] (const Gate::Moment& m) { moments.push_back (m); });
        }
        float worstStart = 0;
        for (int i = hitAt; i < hitAt + 480; ++i)
            worstStart = std::max (worstStart, std::abs (toDb (std::abs (out.getSample (0, i + D))) - toDb (std::abs (in.getSample (0, i)))) * (std::abs (in.getSample (0, i)) > 1e-3f ? 1.0f : 0.0f));
        std::cout << "hit: worst level change in the first 10 ms = " << worstStart << " dB\n";
        // find when it closes: the tail falls below -34 dB at t = 0.1 * ln(0.5/0.02) ~= 0.32 s
        for (int ms = 250; ms <= 700; ms += 25)
        {
            const int i = hitAt + ms * 48;
            float inPk = 0, outPk = 0;
            for (int k = i; k < i + 240; ++k) { inPk = std::max (inPk, std::abs (in.getSample (0, k))); outPk = std::max (outPk, std::abs (out.getSample (0, k + D))); }
            std::cout << "  " << ms << " ms: in " << toDb (inPk) << " dB, out " << toDb (outPk) << " dB, change " << toDb (outPk) - toDb (inPk) << "\n";
        }
        std::cout << "moments: " << moments.size() << " (expect 400)\n";
        int openMoments = 0; for (auto& m : moments) openMoments += m.open ? 1 : 0;
        std::cout << "open moments: " << openMoments << "\n";
    }
    return 0;
}

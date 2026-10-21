#include <juce_audio_basics/juce_audio_basics.h>
#include "GatedReverb.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <map>
int main()
{
    std::mt19937 rng (7); std::normal_distribution<float> g;
    for (float lengthMs : { 300.0f, 1000.0f })
    {
        GatedReverb engine; GatedReverb::Settings s; s.lengthMs = lengthMs; s.mix = 0.4f; engine.prepare (48000, 2, s);
        juce::AudioBuffer<float> buffer (2, 64);
        std::vector<float> noise (96000); for (auto& v : noise) v = g (rng) * 0.2f;
        std::map<int, std::vector<double>> byPhase;   // phase within the 4096-sample cycle
        for (int b = 0; b < 30 * 750; ++b)
        {
            for (int c = 0; c < 2; ++c) std::memcpy (buffer.getWritePointer (c), noise.data() + (size_t) ((b * 64 + c * 777) % 90000), 256);
            auto t = std::chrono::steady_clock::now();
            engine.process (buffer, 2);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t).count();
            if (b > 3 * 750) byPhase[b % 64].push_back (us);
        }
        std::printf ("%g ms: median time by position in the 64-block cycle (us):\n", lengthMs);
        for (auto& [phase, v] : byPhase) { std::sort (v.begin(), v.end()); if (v[v.size()/2] > 5) std::printf ("  phase %2d: median %6.1f  95%% %6.1f  max %6.1f\n", phase, v[v.size()/2], v[(size_t)(v.size()*0.95)], v.back()); }
        double sum = 0; for (auto& [phase, v] : byPhase) sum += v[v.size()/2]; std::printf ("  typical phase median %.1f us\n", byPhase[5][byPhase[5].size()/2]);
    }
}

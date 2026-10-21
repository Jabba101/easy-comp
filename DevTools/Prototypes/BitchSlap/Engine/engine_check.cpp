// Bitch-Slap engine: checked against a plain, slow direct convolution.
#include <juce_audio_basics/juce_audio_basics.h>
#include "GatedReverb.h"
#include <chrono>
#include <cstdio>
#include <random>

static int failures = 0, checks = 0;
static void check (bool ok, const char* what, double value = 0.0)
{
    ++checks;
    if (! ok) ++failures;
    std::printf ("%s  %-70s %g\n", ok ? "ok  " : "FAIL", what, value);
}

// reference: tone-filtered direct convolution of the mono sum with the burst
static std::vector<std::vector<float>> reference (GatedReverb& engine, const std::vector<float>& mono, float lengthMs, int numCh, double rate)
{
    const int irLength = (int) std::ceil (lengthMs * 1e-3 * rate) + 2;
    std::vector<std::vector<float>> out ((size_t) numCh, std::vector<float> (mono.size(), 0.0f));
    for (int c = 0; c < numCh; ++c)
    {
        std::vector<float> h ((size_t) irLength);
        engine.renderBurst (c, lengthMs, h.data(), irLength);
        int firstNonZero = 0; while (firstNonZero < irLength && h[(size_t) firstNonZero] == 0.0f) ++firstNonZero;
        auto& y = out[(size_t) c];
        for (size_t n = 0; n < mono.size(); ++n)
        {
            double acc = 0.0;
            const int kMax = (int) std::min<size_t> ((size_t) irLength - 1, n);
            for (int k = firstNonZero; k <= kMax; ++k) acc += (double) h[(size_t) k] * mono[n - (size_t) k];
            y[n] = (float) acc;
        }
        auto filter = engine.makeToneFilter();
        filter.process (y.data(), (int) y.size());   // (the engine's own filters run in double, like these)
    }
    return out;
}

static double rms (const std::vector<float>& x) { double s = 0; for (auto v : x) s += (double) v * v; return std::sqrt (s / (double) x.size()); }

int main()
{
    const double rate = 48000.0;
    std::mt19937 rng (7);
    std::normal_distribution<float> g;

    // ---- 1. an impulse comes out as the burst
    for (float lengthMs : { 50.0f, 300.0f, 1000.0f })
    {
        GatedReverb engine;
        GatedReverb::Settings s; s.lengthMs = lengthMs; s.mix = 1.0f;
        engine.prepare (rate, 2, s);
        const int n = (int) (1.2 * rate);
        juce::AudioBuffer<float> buffer (2, n); buffer.clear();
        buffer.setSample (0, 100, 1.0f); buffer.setSample (1, 100, 1.0f);
        for (int start = 0; start < n; start += 512)
        {
            juce::AudioBuffer<float> span (buffer.getArrayOfWritePointers(), 2, start, std::min (512, n - start));
            engine.process (span, 2);
        }
        std::vector<float> mono ((size_t) n, 0.0f); mono[100] = 1.0f;
        auto ref = reference (engine, mono, lengthMs, 2, rate);
        double e2 = 0, s2 = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) { const double e = buffer.getSample (c, i) - ref[(size_t) c][(size_t) i]; e2 += e * e; s2 += (double) ref[(size_t) c][(size_t) i] * ref[(size_t) c][(size_t) i]; }
        char what[128]; std::snprintf (what, sizeof what, "impulse -> the burst, %g ms (error vs signal, dB)", lengthMs);
        check (10 * std::log10 (e2 / s2) < -110, what, 10 * std::log10 (e2 / s2));
        std::printf ("      stages %d, block %d, predelay %d\n", engine.getNumStages(), engine.getBlockSize(), engine.getPredelaySamples());
    }

    // ---- 2. real-ish material with gaps, odd block sizes, stereo in, mono and stereo out
    for (int numCh : { 2, 1 })
        for (float lengthMs : { 120.0f, 333.3f, 1000.0f })
        {
            GatedReverb engine;
            GatedReverb::Settings s; s.lengthMs = lengthMs; s.mix = 1.0f;
            engine.prepare (rate, numCh, s);
            const int n = (int) (3.0 * rate);
            juce::AudioBuffer<float> buffer (numCh, n);
            std::vector<float> mono ((size_t) n, 0.0f);
            for (int i = 0; i < n; ++i)
            {
                const bool on = (i / 9000) % 3 != 1 || i > 2 * 48000;   // gaps of digital silence
                for (int c = 0; c < numCh; ++c) buffer.setSample (c, i, on ? g (rng) * 0.3f * (c == 0 ? 1.0f : 0.5f) : 0.0f);
                mono[(size_t) i] = numCh == 2 ? 0.5f * (buffer.getSample (0, i) + buffer.getSample (1, i)) : buffer.getSample (0, i);
            }
            std::uniform_int_distribution<int> blockSizes (1, 700);
            for (int start = 0; start < n;)
            {
                const int len = std::min (n - start, blockSizes (rng));
                juce::AudioBuffer<float> span (buffer.getArrayOfWritePointers(), numCh, start, len);
                engine.process (span, numCh);
                start += len;
            }
            auto ref = reference (engine, mono, lengthMs, numCh, rate);
            double e2 = 0, s2 = 0;
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < n; ++i) { const double e = buffer.getSample (c, i) - ref[(size_t) c][(size_t) i]; e2 += e * e; s2 += (double) ref[(size_t) c][(size_t) i] * ref[(size_t) c][(size_t) i]; }
            char what[128]; std::snprintf (what, sizeof what, "noise with gaps, %d ch, %g ms, random blocks (error vs signal, dB)", numCh, lengthMs);
            check (10 * std::log10 (e2 / s2) < -110, what, 10 * std::log10 (e2 / s2));
        }

    // ---- 2b. changing LENGTH: each sound keeps the burst it started with
    for (double changeAfter : { 0.4, 0.2 })   // 0.2 s: while the first burst is still ringing
    {
        GatedReverb engine;
        GatedReverb::Settings s; s.lengthMs = 300.0f; s.mix = 1.0f;
        engine.prepare (rate, 2, s);
        const int n = (int) (2.0 * rate), t0 = 1000, change = t0 + (int) (changeAfter * rate), t2 = t0 + (int) (0.6 * rate);
        juce::AudioBuffer<float> buffer (2, n); buffer.clear();
        for (int c = 0; c < 2; ++c) { buffer.setSample (c, t0, 1.0f); buffer.setSample (c, t2, 0.7f); }
        for (int start = 0; start < n;)
        {
            const int len = std::min ({ 300, n - start, start < change ? change - start : 300 });
            if (start == change) { s.lengthMs = 150.0f; engine.setSettings (s); }
            juce::AudioBuffer<float> span (buffer.getArrayOfWritePointers(), 2, start, len);
            engine.process (span, 2);
            start += len;
        }
        double e2 = 0, s2 = 0;
        for (int c = 0; c < 2; ++c)
        {
            std::vector<float> ref ((size_t) n, 0.0f), h1 (15000), h2 (15000);
            engine.renderBurst (c, 300.0f, h1.data(), 15000);
            engine.renderBurst (c, 150.0f, h2.data(), 15000);
            for (int k = 0; k < 15000; ++k) { ref[(size_t) (t0 + k)] += h1[(size_t) k]; if (t2 + k < n) ref[(size_t) (t2 + k)] += 0.7f * h2[(size_t) k]; }
            auto f = engine.makeToneFilter(); f.process (ref.data(), n);
            for (int i = 0; i < n; ++i) { const double e = buffer.getSample (c, i) - ref[(size_t) i]; e2 += e * e; s2 += (double) ref[(size_t) i] * ref[(size_t) i]; }
        }
        char what[128]; std::snprintf (what, sizeof what, "LENGTH 300 -> 150 ms %.1f s after a hit: each hit keeps its own (error, dB)", changeAfter);
        check (10 * std::log10 (e2 / s2) < -110, what, 10 * std::log10 (e2 / s2));
    }

    // ---- 2c. sweeping LENGTH on a continuous sound: no clicks
    {
        double jumps[2] = { 0, 0 };
        for (int sweep = 0; sweep < 2; ++sweep)
        {
            GatedReverb engine;
            GatedReverb::Settings s; s.lengthMs = 300.0f; s.mix = 1.0f;
            engine.prepare (rate, 2, s);
            const int n = (int) (6.0 * rate);
            juce::AudioBuffer<float> buffer (2, n);
            std::mt19937 local (3);
            for (int i = 0; i < n; ++i) { const float v = g (local) * 0.2f * (float) ((i / 12000) % 2); buffer.setSample (0, i, v); buffer.setSample (1, i, v); }
            std::uniform_real_distribution<float> lengths (50.0f, 1000.0f);
            for (int start = 0; start < n; start += 480)
            {
                if (sweep == 1 && (start / 480) % 3 == 0) { s.lengthMs = lengths (local); engine.setSettings (s); }
                juce::AudioBuffer<float> span (buffer.getArrayOfWritePointers(), 2, start, std::min (480, n - start));
                engine.process (span, 2);
            }
            // the biggest sample-to-sample jump, relative to the loudness
            double biggest = 0, sq = 0;
            for (int i = 1; i < n; ++i) { biggest = std::max (biggest, (double) std::abs (buffer.getSample (0, i) - buffer.getSample (0, i - 1))); sq += (double) buffer.getSample (0, i) * buffer.getSample (0, i); }
            jumps[sweep] = biggest / std::sqrt (sq / n);
        }
        std::printf ("      biggest jump / rms: steady %.3f, sweeping %.3f\n", jumps[0], jumps[1]);
        check (jumps[1] < jumps[0] * 1.5, "sweeping LENGTH every 30 ms: no bigger jumps than steady", jumps[1] / jumps[0]);
    }

    // ---- 3. speed: continuous noise, stereo, 1 s burst (no silence to skip)
    for (float lengthMs : { 300.0f, 1000.0f })
        for (int block : { 64, 512 })
        {
            GatedReverb engine;
            GatedReverb::Settings s; s.lengthMs = lengthMs; s.mix = 0.4f;
            engine.prepare (rate, 2, s);
            juce::AudioBuffer<float> buffer (2, block);
            const int blocks = (int) (20.0 * rate / block);
            std::vector<float> noise (48000 * 2); for (auto& v : noise) v = g (rng) * 0.2f;
            double worstBlock = 0;
            std::vector<double> times;
            auto t0 = std::chrono::steady_clock::now();
            for (int b = 0; b < blocks; ++b)
            {
                for (int c = 0; c < 2; ++c) std::memcpy (buffer.getWritePointer (c), noise.data() + (size_t) ((b * block + c * 777) % 48000), sizeof (float) * (size_t) block);
                auto tb = std::chrono::steady_clock::now();
                engine.process (buffer, 2);
                const double took = std::chrono::duration<double> (std::chrono::steady_clock::now() - tb).count();
                if (b * block > 2 * 48000) { worstBlock = std::max (worstBlock, took); times.push_back (took); }
            }
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::sort (times.begin(), times.end());
            std::printf ("speed: %4g ms burst, block %3d: %.3f%% of one core; slowest block %.1f us, 99.9%% %.1f us, median %.1f us (block lasts %.0f us)\n",
                         lengthMs, block, 100.0 * secs / 20.0, worstBlock * 1e6, times[(size_t) (times.size() * 0.999)] * 1e6, times[times.size() / 2] * 1e6, block / rate * 1e6);
        }

    std::printf ("\n%d / %d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}

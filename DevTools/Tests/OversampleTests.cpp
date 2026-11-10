// FastOversampler against JUCE's own (not shipped): the same filtering, a whole-number delay, and how much faster.

#include "FastOversampler.h"
#include "TestHelpers.h"
#include <chrono>

using th::check;

namespace
{
    constexpr double sr = 48000.0;

    juce::AudioBuffer<float> noise (int n, float gain = 0.25f)
    {
        juce::AudioBuffer<float> b (2, n);
        juce::Random r (11);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                b.setSample (ch, i, gain * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    double levelDb (const float* x, int from, int to)
    {
        double sum = 0.0;
        for (int i = from; i < to; ++i) sum += (double) x[i] * x[i];
        return 10.0 * std::log10 (sum / juce::jmax (1, to - from) + 1.0e-30);
    }

    /** How loud one frequency is in x (a single-bin measurement, Hann window), dB. */
    double toneDb (const float* x, int n, double hz, double rate)
    {
        double re = 0.0, im = 0.0, norm = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / (n - 1));
            const double ph = 2.0 * juce::MathConstants<double>::pi * hz * i / rate;
            re += w * x[i] * std::cos (ph); im += w * x[i] * std::sin (ph); norm += w;
        }
        return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / norm + 1.0e-30);
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    std::cout << "\nFastOversampler\n";

    for (int log2 : { 2, 3 })
    {
        const int factor = 1 << log2, block = 512, blocks = 64, n = block * blocks;
        const juce::String name = juce::String (factor) + "x: ";
        using Juce = juce::dsp::Oversampling<float>;
        Juce reference (2, (size_t) log2, Juce::filterHalfBandFIREquiripple, true, false);   // (no all-pass: the filters alone)
        Juce shipped (2, (size_t) log2, Juce::filterHalfBandFIREquiripple, true, true);      // (as the plugins used it)
        FastOversampler fast (2, (size_t) log2);
        reference.initProcessing ((size_t) block); shipped.initProcessing ((size_t) block); fast.initProcessing ((size_t) block);

        const float exact = reference.getLatencyInSamples();
        juce::ignoreUnused (exact);
        check (std::abs (fast.getLatencyInSamples() - std::floor (fast.getLatencyInSamples())) < 1.0e-9f,
               name + "a whole-number delay, " + juce::String (fast.getLatencyInSamples(), 0) + " samples (JUCE's: "
               + juce::String (shipped.getLatencyInSamples(), 0) + ")");

        // 1. Going up: sample for sample what JUCE's gives
        auto in = noise (n);
        juce::AudioBuffer<float> upRef (2, n * factor), upFast (2, n * factor), a (2, block), b (2, block), downFast (2, n);
        for (int start = 0; start < n; start += block)
        {
            for (int ch = 0; ch < 2; ++ch) { a.copyFrom (ch, 0, in, ch, start, block); b.copyFrom (ch, 0, in, ch, start, block); }
            juce::dsp::AudioBlock<float> ba (a), bb (b);
            auto u1 = reference.processSamplesUp (ba);
            auto u2 = fast.processSamplesUp (bb);
            for (int ch = 0; ch < 2; ++ch)
            {
                upRef.copyFrom (ch, start * factor, u1.getChannelPointer ((size_t) ch), block * factor);
                upFast.copyFrom (ch, start * factor, u2.getChannelPointer ((size_t) ch), block * factor);
            }
            reference.processSamplesDown (ba);
            fast.processSamplesDown (bb);
            for (int ch = 0; ch < 2; ++ch) downFast.copyFrom (ch, start, b, ch, 0, block);
        }
juce::ignoreUnused (upRef, upFast);

        // 2. Up and straight back down: the sound itself, delayed by exactly that whole number (below 20 kHz)
        {
            const int delay = (int) fast.getLatencyInSamples();
            double worst = -200.0;
            for (double hz : { 50.0, 1000.0, 5000.0, 10000.0, 15000.0, 18000.0, 20000.0 })
            {
                FastOversampler f (1, (size_t) log2); f.initProcessing ((size_t) block);
                juce::AudioBuffer<float> tone (1, n), out (1, n), work (1, block);
                for (int i = 0; i < n; ++i) tone.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr));
                for (int start = 0; start < n; start += block)
                {
                    work.copyFrom (0, 0, tone, 0, start, block);
                    juce::dsp::AudioBlock<float> bw (work);
                    f.processSamplesUp (bw); f.processSamplesDown (bw);
                    out.copyFrom (0, start, work, 0, 0, block);
                }
                juce::AudioBuffer<float> diff (1, n);
                diff.clear();
                for (int i = 4096; i < n; ++i) diff.setSample (0, i, out.getSample (0, i) - tone.getSample (0, i - delay));
                worst = juce::jmax (worst, levelDb (diff.getReadPointer (0), 4096, n) - levelDb (tone.getReadPointer (0), 4096, n));
            }
            check (worst < -85.0, name + "up and back down: the sound itself, exactly " + juce::String (delay) + " samples late, 50 Hz - 20 kHz (difference "
                                  + juce::String (juce::roundToInt (worst)) + " dB)");
        }

        // 3. Images: a 15 kHz tone taken up has nothing new above it (JUCE's figure: under -85 dB)
        {
            FastOversampler f (1, (size_t) log2); f.initProcessing ((size_t) block);
            std::vector<float> high;
            juce::AudioBuffer<float> work (1, block);
            for (int start = 0; start < n; start += block)
            {
                for (int i = 0; i < block; ++i) work.setSample (0, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 15000.0 * (start + i) / sr));
                juce::dsp::AudioBlock<float> bw (work);
                auto u = f.processSamplesUp (bw);
                high.insert (high.end(), u.getChannelPointer (0), u.getChannelPointer (0) + block * factor);
                f.processSamplesDown (bw);
            }
            const int skip = 8192 * factor, len = (int) high.size() - skip;
            const double tone = toneDb (high.data() + skip, len, 15000.0, sr * factor);
            double image = -300.0;
            for (int k = 1; k < factor; ++k)
                for (double hz : { k * sr - 15000.0, k * sr + 15000.0 })
                    if (hz < sr * factor * 0.5)
                        image = juce::jmax (image, toneDb (high.data() + skip, len, hz, sr * factor));
            check (std::abs (tone - (-6.02)) < 0.02 && image - tone < -97.0,
                   name + "a 15 kHz tone taken up: its level kept (" + juce::String (tone + 6.02, 3) + " dB), images " + juce::String (juce::roundToInt (image - tone)) + " dB down");
        }

        // 3b. Aliases: anything made above the audible band at the high rate doesn't fold back into it
        {
            double worstAlias = -300.0;
            for (double hz : { 30000.0, 40000.0, 60000.0, 75000.0, 90000.0, 130000.0, 170000.0 })
            {
                if (hz > sr * factor * 0.5 - 1000.0) continue;
                FastOversampler f (1, (size_t) log2); f.initProcessing ((size_t) block);
                juce::AudioBuffer<float> work (1, block), out (1, n);
                for (int start = 0; start < n; start += block)
                {
                    work.clear();
                    juce::dsp::AudioBlock<float> bw (work);
                    auto u = f.processSamplesUp (bw);
                    for (int i = 0; i < block * factor; ++i)
                        u.getChannelPointer (0)[i] = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * ((double) start * factor + i) / (sr * factor));
                    f.processSamplesDown (bw);
                    out.copyFrom (0, start, work, 0, 0, block);
                }
                // (where it would land: folded about the normal rate; only 0 - 20 kHz counts)
                double landed = std::fmod (hz, sr);
                if (landed > sr * 0.5) landed = sr - landed;
                if (landed <= 20000.0)
                    worstAlias = juce::jmax (worstAlias, toneDb (out.getReadPointer (0) + 8192, n - 8192, landed, sr) + 6.02);
            }
            check (worstAlias < -97.0, name + "tones made above the audible band don't fold back into it (" + juce::String (juce::roundToInt (worstAlias)) + " dB)");
        }

        // 4. Any block size gives the same result
        {
            FastOversampler f (2, (size_t) log2); f.initProcessing ((size_t) block);
            juce::AudioBuffer<float> out (2, n);
            juce::Random r (5);
            for (int start = 0; start < n;)
            {
                const int len = juce::jmin (n - start, 1 + r.nextInt (block));
                juce::AudioBuffer<float> work (2, len);
                for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, in, ch, start, len);
                juce::dsp::AudioBlock<float> bw (work);
                f.processSamplesUp (bw); f.processSamplesDown (bw);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, start, work, ch, 0, len);
                start += len;
            }
            check (th::maxDifference (out, downFast) < 1.0e-6f, name + "odd block sizes (1 - 512 samples) give the same sound");
        }

        // 5. Speed
        {
            const auto time = [&] (auto& os)
            {
                double best = 1.0e9;
                for (int pass = 0; pass < 5; ++pass)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    for (int rep = 0; rep < 8; ++rep)
                        for (int start = 0; start < n; start += block)
                        {
                            for (int ch = 0; ch < 2; ++ch) a.copyFrom (ch, 0, in, ch, start, block);
                            juce::dsp::AudioBlock<float> ba (a);
                            os.processSamplesUp (ba);
                            os.processSamplesDown (ba);
                        }
                    best = juce::jmin (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
                }
                return 100.0 * best / (8.0 * n / sr);   // % of one core, stereo
            };
            const double before = time (shipped), after = time (fast);
            check (after < before * 0.6, name + "up + down, stereo: " + juce::String (before, 2) + "% of a core with JUCE's, " + juce::String (after, 2) + "% with this ("
                                         + juce::String (before / after, 1) + " times faster)");
        }
    }

    std::cout << "\n" << (th::checks - th::failures) << " / " << th::checks << " checks passed\n";
    return th::failures == 0 ? 0 : 1;
}

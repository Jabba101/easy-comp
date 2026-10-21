// Quick speed checks for Bitch-Slap's engine choices.
#include <juce_dsp/juce_dsp.h>
#include "pffft.h"
#include <chrono>
#include <cstdio>
#include <random>

using Clock = std::chrono::steady_clock;

static double secondsSince (Clock::time_point t) { return std::chrono::duration<double> (Clock::now() - t).count(); }

static void benchJuceFFT (int order)
{
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * (size_t) n);
    std::mt19937 rng (1); std::normal_distribution<float> g;
    const int reps = juce::jmax (200, 20000000 / (n * order));
    double best = 1e9;
    for (int trial = 0; trial < 5; ++trial)
    {
        auto t = Clock::now();
        for (int r = 0; r < reps; ++r)
        {
            for (int i = 0; i < n; ++i) data[(size_t) i] = g (rng) * 0.0f + (float) (i & 7);
            fft.performRealOnlyForwardTransform (data.data(), true);
            fft.performRealOnlyInverseTransform (data.data());
        }
        best = juce::jmin (best, secondsSince (t) / reps);
    }
    std::printf ("JUCE  FFT %5d: fwd+inv %8.2f us\n", n, best * 1e6);
}

static void benchPffft (int n)
{
    PFFFT_Setup* setup = pffft_new_setup (n, PFFFT_REAL);
    float* in   = (float*) pffft_aligned_malloc ((size_t) n * sizeof (float));
    float* out  = (float*) pffft_aligned_malloc ((size_t) n * sizeof (float));
    float* work = (float*) pffft_aligned_malloc ((size_t) n * sizeof (float));
    float* acc  = (float*) pffft_aligned_malloc ((size_t) n * sizeof (float));
    for (int i = 0; i < n; ++i) { in[i] = (float) (i & 7); acc[i] = 0; }
    const int reps = std::max (200, 20000000 / (n * 10));
    double best = 1e9, bestMac = 1e9;
    for (int trial = 0; trial < 5; ++trial)
    {
        auto t = Clock::now();
        for (int r = 0; r < reps; ++r)
        {
            pffft_transform (setup, in, out, work, PFFFT_FORWARD);
            pffft_transform (setup, out, in, work, PFFFT_BACKWARD);
            in[0] *= 1.0f / (float) n;
        }
        best = std::min (best, secondsSince (t) / reps);
        t = Clock::now();
        for (int r = 0; r < reps * 4; ++r)
            pffft_zconvolve_accumulate (setup, out, in, acc, 1.0f);
        bestMac = std::min (bestMac, secondsSince (t) / (reps * 4));
    }
    std::printf ("PFFFT     %5d: fwd+inv %8.2f us   one complex MAC pass %6.2f us  (%s)\n", n, best * 1e6, bestMac * 1e6, pffft_simd_arch());
    pffft_aligned_free (in); pffft_aligned_free (out); pffft_aligned_free (work); pffft_aligned_free (acc);
    pffft_destroy_setup (setup);
}

static void benchJuceConvolution (int headSize, int irMs, int block, double rate)
{
    const int irLength = (int) (irMs * 1e-3 * rate);
    juce::AudioBuffer<float> ir (2, irLength);
    std::mt19937 rng (3); std::normal_distribution<float> g;
    for (int c = 0; c < 2; ++c) for (int i = 0; i < irLength; ++i) ir.setSample (c, i, g (rng) * 0.01f);

    juce::dsp::Convolution conv (juce::dsp::Convolution::NonUniform { headSize });
    conv.loadImpulseResponse (std::move (ir), rate, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    conv.prepare ({ rate, (juce::uint32) block, 2 });

    juce::AudioBuffer<float> io (2, block);
    const int blocks = (int) (10.0 * rate / block);
    auto t = Clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        for (int c = 0; c < 2; ++c) for (int i = 0; i < block; ++i) io.setSample (c, i, g (rng) * 0.1f);
        juce::dsp::AudioBlock<float> ab (io);
        conv.process (juce::dsp::ProcessContextReplacing<float> (ab));
    }
    const double s = secondsSince (t);
    std::printf ("JUCE Convolution head %4d, IR %4d ms, block %4d: %.2f%% of one core\n", headSize, irMs, block, 100.0 * s / 10.0);
}

int main()
{
    for (int order : { 7, 10, 13 }) benchJuceFFT (order);
    for (int n : { 128, 1024, 8192 }) benchPffft (n);
    for (int head : { 0, 256, 1024 })
        for (int block : { 64, 512 })
            benchJuceConvolution (head, 1000, block, 48000.0);
    benchJuceConvolution (1024, 300, 512, 48000.0);
    return 0;
}

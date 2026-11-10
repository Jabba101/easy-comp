#pragma once

#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <cstring>

//==============================================================================
/**
    OVERSAMPLING for the things that distort (AMP, TAPE, CLIP, INFLATE): up to
    4 or 8 times the sample rate, the curve is applied there, and back down -
    so the new harmonics a curve makes don't fold back as harsh aliasing.

    Linear-phase half-band equiripple FIR filters, stage by stage (2x, 4x,
    8x). Compared with JUCE's own top-quality oversampler, which the plugins
    used before:

    - CLEANER. Every stage keeps images and aliases 100 dB down (JUCE's: 75 to
      90 dB, and only 55 - 70 dB in its last 8x stage), and the audible band
      is flat to about a ten-thousandth of a dB.

    - FASTER, several times over. JUCE's works one sample at a time and moves
      its whole memory along after each; this one works a block at a time on
      memory laid out in a row, which the processor can do many samples at
      once. And the later stages are designed as gently as their job allows
      (they only separate bands an octave or more apart), so they need few taps.

    - A WHOLE-NUMBER DELAY, EXACTLY. The filters' delay isn't a whole number
      of samples; JUCE rounds it up with an all-pass (which bends the phase a
      touch at the very top). This one adds a few samples of plain delay at
      the high rate instead: the result is perfectly linear phase, so mixing
      it with the untouched sound (CLIP's BLEND, the strip's BYPASS) lines up
      exactly at every frequency.

    Use: initProcessing (largest block); then per block processSamplesUp
    (block) -> work on what it returns (factor times as many samples) ->
    processSamplesDown (block) writes the result back into your block.
*/
class FastOversampler
{
public:
    static constexpr float stopbandDb = -100.0f;

    /** factorLog2: 2 = 4x, 3 = 8x (1 = 2x). */
    FastOversampler (size_t numChannelsToUse, size_t factorLog2)
        : numChannels (numChannelsToUse), factor ((size_t) 1 << factorLog2)
    {
        jassert (factorLog2 >= 1 && factorLog2 <= 4);
        double fractional = 0.0;   // (the filters' delay, in samples at the normal rate)
        for (size_t n = 0; n < factorLog2; ++n)
        {
            // The first stage is the steep one: it passes everything up to 0.45 of the sample rate (21.6 kHz at 48 kHz)
            // and stops everything from 0.55. The later stages only have to keep that band clear of its copies
            // an octave or more above, so they can be much gentler (far fewer taps) at the same -100 dB.
            // (the design method lands a little short of what's asked for very gentle filters, so those ask for more:
            // OversampleTests measures what they actually do)
            const float transition = n == 0 ? 0.05f : (n == 1 ? 0.22f : 0.30f);
            const float asked = n <= 1 ? stopbandDb : stopbandDb - 15.0f;
            stages.emplace_back (numChannels,
                                 *juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (transition, asked),
                                 *juce::dsp::FilterDesign<float>::designFIRLowpassHalfBandEquirippleMethod (transition, asked));
            fractional += (double) (stages.back().up.delay + stages.back().down.delay) / (double) ((size_t) 2 << n);
        }

        // Round the delay up to a whole number with plain delay at the high rate
        const double steps = fractional * (double) factor;   // (always a whole number of high-rate samples)
        const auto highRate = (size_t) std::llround (steps);
        jassert (std::abs (steps - (double) highRate) < 1.0e-6);
        extraDelay = (factor - highRate % factor) % factor;
        latency = (highRate + extraDelay) / factor;
        delayLines.assign (numChannels, std::vector<float> (extraDelay, 0.0f));
    }

    void initProcessing (size_t maximumBlockSize)
    {
        maxBlock = maximumBlockSize;
        size_t n = maximumBlockSize;
        for (auto& s : stages)
        {
            s.prepare (n);
            n *= 2;
        }
        top.setSize ((int) numChannels, (int) (maximumBlockSize * factor), false, true, false);
        reset();
    }

    /** In samples at the normal rate: always a whole number. */
    float getLatencyInSamples() const noexcept   { return (float) latency; }
    size_t getOversamplingFactor() const noexcept   { return factor; }

    void reset() noexcept
    {
        for (auto& s : stages) s.reset();
        for (auto& d : delayLines) std::fill (d.begin(), d.end(), 0.0f);
        top.clear();
    }

    /** Returns the sound at the high rate (factor times as many samples), to be worked on in place. */
    juce::dsp::AudioBlock<float> processSamplesUp (const juce::dsp::AudioBlock<float>& input) noexcept
    {
        const size_t n = input.getNumSamples(), channels = juce::jmin (numChannels, input.getNumChannels());
        jassert (n <= maxBlock);
        for (size_t ch = 0; ch < channels; ++ch)
        {
            const float* source = input.getChannelPointer (ch);
            size_t length = n;
            for (size_t s = 0; s < stages.size(); ++s)
            {
                float* target = s + 1 == stages.size() ? top.getWritePointer ((int) ch) : stages[s].scratch[ch].data();
                stages[s].up.process (ch, source, target, length);
                source = target;
                length *= 2;
            }
        }
        return juce::dsp::AudioBlock<float> (top.getArrayOfWritePointers(), channels, n * factor);
    }

    /** Takes the high-rate sound (what processSamplesUp returned) back down, into `output`. */
    void processSamplesDown (juce::dsp::AudioBlock<float>& output) noexcept
    {
        const size_t n = output.getNumSamples(), channels = juce::jmin (numChannels, output.getNumChannels());
        jassert (n <= maxBlock);
        for (size_t ch = 0; ch < channels; ++ch)
        {
            float* high = top.getWritePointer ((int) ch);
            size_t length = n * factor;

            // The few samples of plain delay that make the total a whole number
            if (extraDelay > 0)
            {
                auto& line = delayLines[ch];
                if (length >= extraDelay)
                {
                    float keep[16];
                    for (size_t i = 0; i < extraDelay; ++i) keep[i] = high[length - extraDelay + i];
                    std::memmove (high + extraDelay, high, (length - extraDelay) * sizeof (float));
                    for (size_t i = 0; i < extraDelay; ++i) { high[i] = line[i]; line[i] = keep[i]; }
                }
                else
                {
                    for (size_t i = 0; i < length; ++i)
                    {
                        const float out = line[0];
                        for (size_t k = 0; k + 1 < extraDelay; ++k) line[k] = line[k + 1];
                        line[extraDelay - 1] = high[i];
                        high[i] = out;
                    }
                }
            }

            const float* source = high;
            for (size_t s = stages.size(); s-- > 0;)
            {
                length /= 2;
                float* target = s == 0 ? output.getChannelPointer (ch) : stages[s - 1].scratch[ch].data();
                stages[s].down.process (ch, source, target, length);
                source = target;
            }
        }
    }

private:
    //==========================================================================
    /** A half-band filter's useful parts: every other tap is zero (bar the middle one), and it's
        symmetric - so only a quarter of the taps need multiplying. */
    struct HalfBand
    {
        explicit HalfBand (const juce::dsp::FIR::Coefficients<float>& c)
        {
            const auto* fir = c.getRawCoefficients();
            const size_t N = c.getFilterOrder() + 1, centreIndex = N / 2;
            jassert (N % 4 == 3);   // (the design always gives 4m + 3 taps: zeros at the odd ones, bar the centre)
            count = (N + 1) / 2;    // the non-zero taps, without the centre (an even number, symmetric)
            for (size_t j = 0; j < count / 2; ++j)
                taps.push_back (fir[2 * j]);
            centre = fir[centreIndex];
            delay = (N - 1) / 2;    // the delay, in samples at the HIGHER of its two rates
        }

        std::vector<float> taps;    // the first half of the non-zero taps
        float centre = 0.5f;
        size_t count = 0, delay = 0;
    };

    //==========================================================================
    struct Up : public HalfBand
    {
        using HalfBand::HalfBand;

        void prepare (size_t numChannels, size_t maxInput)
        {
            history.assign (numChannels, std::vector<float> (count - 1 + maxInput, 0.0f));
            sums.assign (maxInput, 0.0f);
        }

        void reset() noexcept   { for (auto& h : history) std::fill (h.begin(), h.end(), 0.0f); }

        /** n samples in, 2n out. */
        void process (size_t ch, const float* in, float* out, size_t n) noexcept
        {
            auto* w = history[ch].data();           // (the last count-1 inputs, then this block)
            const size_t past = count - 1;
            std::memcpy (w + past, in, n * sizeof (float));

            // The filtered samples: each tap pair swept across the whole block (the processor does these several at a time)
            float* __restrict acc = sums.data();   // (__restrict: nothing else points here, so several samples can go at once)
            std::fill (acc, acc + n, 0.0f);
            for (size_t j = 0; j < taps.size(); ++j)
            {
                const float g = taps[j];
                const float* __restrict a = w + j;
                const float* __restrict b = w + (count - 1 - j);
                for (size_t i = 0; i < n; ++i)
                    acc[i] += g * (a[i] + b[i]);
            }

            // ... interleaved with the plain (delayed) ones; twice as loud, as there are twice as many samples
            const float* middle = w + (count / 2);   // (x[i - m], where count = 2m + 2)
            const float c2 = 2.0f * centre;
            for (size_t i = 0; i < n; ++i)
            {
                out[2 * i] = 2.0f * acc[i];
                out[2 * i + 1] = c2 * middle[i];
            }

            std::memmove (w, w + n, past * sizeof (float));
        }

        std::vector<std::vector<float>> history;
        std::vector<float> sums;
    };

    //==========================================================================
    struct Down : public HalfBand
    {
        using HalfBand::HalfBand;

        void prepare (size_t numChannels, size_t maxOutput)
        {
            even.assign (numChannels, std::vector<float> (count - 1 + maxOutput, 0.0f));
            odd.assign (numChannels, std::vector<float> (count / 2 + maxOutput, 0.0f));
        }

        void reset() noexcept
        {
            for (auto& h : even) std::fill (h.begin(), h.end(), 0.0f);
            for (auto& h : odd)  std::fill (h.begin(), h.end(), 0.0f);
        }

        /** 2n samples in, n out. */
        void process (size_t ch, const float* in, float* out, size_t n) noexcept
        {
            auto* e = even[ch].data();   // (the last count-1 even-numbered inputs, then this block's)
            auto* o = odd[ch].data();    // (the last count/2 odd-numbered inputs, then this block's)
            const size_t pastEven = count - 1, pastOdd = count / 2;
            for (size_t i = 0; i < n; ++i)
            {
                e[pastEven + i] = in[2 * i];
                o[pastOdd + i] = in[2 * i + 1];
            }

            // The middle tap works on the odd-numbered samples (delayed); the rest on the even-numbered ones
            float* __restrict result = out;
            for (size_t i = 0; i < n; ++i)
                result[i] = centre * o[i];
            for (size_t j = 0; j < taps.size(); ++j)
            {
                const float g = taps[j];
                const float* __restrict a = e + j;
                const float* __restrict b = e + (count - 1 - j);
                for (size_t i = 0; i < n; ++i)
                    result[i] += g * (a[i] + b[i]);
            }

            std::memmove (e, e + n, pastEven * sizeof (float));
            std::memmove (o, o + n, pastOdd * sizeof (float));
        }

        std::vector<std::vector<float>> even, odd;
    };

    //==========================================================================
    struct Stage
    {
        Stage (size_t channels, const juce::dsp::FIR::Coefficients<float>& upTaps, const juce::dsp::FIR::Coefficients<float>& downTaps)
            : up (upTaps), down (downTaps), numChannels (channels) {}

        /** maxInput: the most samples at this stage's LOWER rate. */
        void prepare (size_t maxInput)
        {
            up.prepare (numChannels, maxInput);
            down.prepare (numChannels, maxInput);
            scratch.assign (numChannels, std::vector<float> (maxInput * 2, 0.0f));
        }

        void reset() noexcept   { up.reset(); down.reset(); }

        Up up;
        Down down;
        size_t numChannels;
        std::vector<std::vector<float>> scratch;   // this stage's higher-rate sound (unless it's the last stage: then `top`)
    };

    size_t numChannels, factor, maxBlock = 0, extraDelay = 0, latency = 0;
    std::vector<Stage> stages;
    std::vector<std::vector<float>> delayLines;
    juce::AudioBuffer<float> top;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FastOversampler)
};

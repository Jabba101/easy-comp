#pragma once

#include "Equalizer.h"
#include <juce_dsp/juce_dsp.h>
#include <atomic>

//==============================================================================
/**
    MASTER: the same EQ, in linear phase.

    An ordinary (minimum-phase) EQ shifts the timing of the frequencies it
    boosts or cuts by a tiny amount - part of the "analogue" sound, but on a
    drum bus or a finished mix it can soften a hit's attack. Linear phase
    keeps every frequency perfectly in time, at the cost of a delay (your
    DAW lines everything up for it) and more computer power.

    How: a background thread builds one long filter (an impulse response)
    with exactly the EQ's curve - for the middle and the sides separately -
    whenever a setting changes: it asks Equalizer::responseAt for the curve
    at every frequency step, turns that into a zero-phase filter with an
    inverse FFT, centres it and smooths its ends (a Hann window). JUCE's
    Convolution then runs it, and crossfades from the old filter to the new
    one, so turning a knob never clicks.

    Length: 4096 samples at 44.1/48 kHz (a 43-46 ms delay - half of it),
    8192 at 88.2/96 kHz, 16384 above, so the lows are just as precise at any
    sample rate.
*/
class LinearPhaseEQ : private juce::Thread
{
public:
    LinearPhaseEQ() : juce::Thread ("Easy EQ MASTER filter designer") {}
    ~LinearPhaseEQ() override   { stopThread (2000); }

    void prepare (double newSampleRate, int maximumBlockSize, const Equalizer::Settings& initial)
    {
        stopThread (2000);

        sampleRate = newSampleRate;
        length = sampleRate <= 50000.0 ? 4096 : (sampleRate <= 100000.0 ? 8192 : 16384);
        order = juce::roundToInt (std::log2 ((double) length));
        blockSize = juce::jmax (32, maximumBlockSize);

        midSide.setSize (2, blockSize);

        // The first filter, straight away - loaded before prepare(), so the
        // convolution has it ready from the very first sample
        {
            const juce::SpinLock::ScopedLockType lock (settingsLock);
            wanted = initial;
        }
        juce::AudioBuffer<float> first;
        design (initial, first);
        convolution.loadImpulseResponse (std::move (first), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        convolution.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        version.store (0);
        designed.store (0);
        pendingReady.store (false);

        startThread (juce::Thread::Priority::low);
    }

    /** The delay MASTER adds (half the filter's length). */
    int getLatencySamples() const noexcept   { return length / 2; }

    /** Hands over the latest settings (from the audio thread: never waits). */
    void setSettings (const Equalizer::Settings& s) noexcept
    {
        const juce::SpinLock::ScopedTryLockType lock (settingsLock);

        if (lock.isLocked() && differs (s, wanted))
        {
            wanted = s;
            version.fetch_add (1);
            notify();
        }
    }

    /** The linear-phase EQ on the buffer, in place (1 or 2 channels). Delayed by getLatencySamples(). */
    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        // A newly designed filter waiting? Hand it to the convolution (it crossfades).
        if (pendingReady.load (std::memory_order_acquire))
        {
            convolution.loadImpulseResponse (std::move (pending), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            pendingReady.store (false, std::memory_order_release);
        }

        const int channels = juce::jmin (2, buffer.getNumChannels());
        const bool stereo = channels == 2;

        for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
        {
            const int n = juce::jmin (blockSize, buffer.getNumSamples() - start);
            float* left = buffer.getWritePointer (0, start);
            float* right = stereo ? buffer.getWritePointer (1, start) : nullptr;
            float* mid = midSide.getWritePointer (0);
            float* side = midSide.getWritePointer (1);

            for (int i = 0; i < n; ++i)
            {
                mid[i]  = stereo ? 0.5f * (left[i] + right[i]) : left[i];
                side[i] = stereo ? 0.5f * (left[i] - right[i]) : 0.0f;
            }

            juce::dsp::AudioBlock<float> block (midSide.getArrayOfWritePointers(), 2, (size_t) n);
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));

            for (int i = 0; i < n; ++i)
            {
                left[i] = stereo ? mid[i] + side[i] : mid[i];
                if (stereo)
                    right[i] = mid[i] - side[i];
            }
        }
    }

    void reset() noexcept   { convolution.reset(); }

    /** (For the tests) True once the filter for the latest settings is in use. */
    bool isUpToDate() const noexcept   { return designed.load() == version.load() && ! pendingReady.load(); }

    /** Builds the filter for some settings: channel 0 = the middle, 1 = the sides. */
    void design (const Equalizer::Settings& s, juce::AudioBuffer<float>& out) const
    {
        juce::dsp::FFT fft (order);
        std::vector<float> spectrum ((size_t) length * 2, 0.0f);
        out.setSize (2, length, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);

            // The wanted gain at every frequency step, with no phase (zero-phase)
            for (int k = 0; k <= length / 2; ++k)
            {
                const double hz = juce::jmax (1.0, (double) k * sampleRate / length);
                spectrum[(size_t) (2 * k)] = (float) Equalizer::responseAt (s, hz, sampleRate, ch == 0);
            }

            fft.performRealOnlyInverseTransform (spectrum.data());

            // Centre it (so it's symmetric: linear phase), smooth the ends
            float* h = out.getWritePointer (ch);
            for (int n = 0; n < length; ++n)
            {
                const int from = (n + length / 2) % length;
                const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / length);
                h[n] = (float) (spectrum[(size_t) from] * window);
            }
        }
    }

private:
    static bool moved (float a, float b) noexcept   { return std::abs (a - b) > 1.0e-6f; }

    static bool differs (const Equalizer::Settings& a, const Equalizer::Settings& b) noexcept
    {
        for (int i = 0; i < Equalizer::numBands; ++i)
        {
            const auto& x = a.bands[(size_t) i];
            const auto& y = b.bands[(size_t) i];
            if (moved (x.freqHz, y.freqHz) || moved (x.gainDb, y.gainDb) || x.shelf != y.shelf || x.target != y.target)
                return true;
        }

        return moved (a.hpfHz, b.hpfHz) || moved (a.lpfHz, b.lpfHz) || moved (a.monoBassHz, b.monoBassHz)
            || a.hpfSteep != b.hpfSteep || a.lpfSteep != b.lpfSteep;
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            wait (50);

            const int latest = version.load();
            if (latest == designed.load() || pendingReady.load (std::memory_order_acquire))
                continue;

            Equalizer::Settings s;
            {
                const juce::SpinLock::ScopedLockType lock (settingsLock);
                s = wanted;
            }

            juce::AudioBuffer<float> next;
            design (s, next);

            pending = std::move (next);
            pendingReady.store (true, std::memory_order_release);
            designed.store (latest);
        }
    }

    double sampleRate = 48000.0;
    int length = 4096, order = 12, blockSize = 512;

    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> midSide, pending;
    std::atomic<bool> pendingReady { false };
    std::atomic<int> version { 0 }, designed { 0 };

    juce::SpinLock settingsLock;
    Equalizer::Settings wanted;
};

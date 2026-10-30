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

    Length (PRECISION): HIGH = 4096 samples at 44.1/48 kHz (a 43-46 ms
    delay - half of it), 8192 at 88.2/96 kHz, 16384 above, so the lows are
    just as precise at any sample rate. MEDIUM is half that, LOW a quarter:
    less delay, and less "pre-ring" (the faint smear a linear-phase EQ puts
    just BEFORE each hit), but the very lowest frequencies are shaped less
    exactly (a band at 30 Hz comes out broader). Changing it changes the
    delay: the new filter takes over (crossfaded) once it's built.
*/
class LinearPhaseEQ : private juce::Thread
{
public:
    LinearPhaseEQ() : juce::Thread ("Easy EQ MASTER filter designer") {}
    ~LinearPhaseEQ() override   { stopThread (2000); }

    enum Precision { low = 0, medium = 1, high = 2 };

    /** The longest filter at a sample rate (HIGH). */
    static int maximumLength (double rate) noexcept   { return rate <= 50000.0 ? 4096 : (rate <= 100000.0 ? 8192 : 16384); }
    static int lengthFor (double rate, int precision) noexcept   { return maximumLength (rate) >> (2 - juce::jlimit (0, 2, precision)); }

    void prepare (double newSampleRate, int maximumBlockSize, const Equalizer::Settings& initial, int precision = high)
    {
        stopThread (2000);

        sampleRate = newSampleRate;
        length = lengthFor (sampleRate, precision);
        wantedLength.store (length);
        blockSize = juce::jmax (32, maximumBlockSize);

        midSide.setSize (2, blockSize);

        // The first filter, straight away - loaded before prepare(), so the
        // convolution has it ready from the very first sample
        {
            const juce::SpinLock::ScopedLockType lock (settingsLock);
            wanted = initial;
        }
        juce::AudioBuffer<float> first;
        design (initial, first, length);
        convolution.loadImpulseResponse (std::move (first), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
        convolution.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
        version.store (0);
        designed.store (0);
        pendingReady.store (false);

        startThread (juce::Thread::Priority::low);
    }

    /** The delay MASTER adds right now (half the length of the filter in use). */
    int getLatencySamples() const noexcept   { return length / 2; }

    /** PRECISION (from the audio thread: never waits). The delay changes once the new filter is in use. */
    void setPrecision (int precision) noexcept
    {
        const int newLength = lengthFor (sampleRate, precision);
        if (newLength != wantedLength.load())
        {
            wantedLength.store (newLength);
            version.fetch_add (1);
            notify();
        }
    }

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
    /** Hands a newly designed filter (if one's waiting) to the convolution, which crossfades to it.
        Called by process(); call it yourself while MASTER is off, so the delay still follows PRECISION. */
    void takeNewFilter() noexcept
    {
        if (pendingReady.load (std::memory_order_acquire))
        {
            convolution.loadImpulseResponse (std::move (pending), sampleRate, juce::dsp::Convolution::Stereo::yes,
                                             juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
            length = pendingLength;
            pendingReady.store (false, std::memory_order_release);
        }
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept
    {
        takeNewFilter();

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
    void design (const Equalizer::Settings& s, juce::AudioBuffer<float>& out, int filterLength) const
    {
        juce::dsp::FFT fft (juce::roundToInt (std::log2 ((double) filterLength)));
        std::vector<float> spectrum ((size_t) filterLength * 2, 0.0f);
        out.setSize (2, filterLength, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
        {
            std::fill (spectrum.begin(), spectrum.end(), 0.0f);

            // The wanted gain at every frequency step, with no phase (zero-phase)
            for (int k = 0; k <= filterLength / 2; ++k)
            {
                const double hz = juce::jmax (1.0, (double) k * sampleRate / filterLength);
                spectrum[(size_t) (2 * k)] = (float) Equalizer::responseAt (s, hz, sampleRate, ch == 0);
            }

            fft.performRealOnlyInverseTransform (spectrum.data());

            // Centre it (so it's symmetric: linear phase), smooth the ends
            float* h = out.getWritePointer (ch);
            for (int n = 0; n < filterLength; ++n)
            {
                const int from = (n + filterLength / 2) % filterLength;
                const double window = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * n / filterLength);
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

            const int newLength = wantedLength.load();
            juce::AudioBuffer<float> next;
            design (s, next, newLength);

            pending = std::move (next);
            pendingLength = newLength;
            pendingReady.store (true, std::memory_order_release);
            designed.store (latest);
        }
    }

    double sampleRate = 48000.0;
    int length = 4096, pendingLength = 4096, blockSize = 512;
    std::atomic<int> wantedLength { 4096 };

    juce::dsp::Convolution convolution;
    juce::AudioBuffer<float> midSide, pending;
    std::atomic<bool> pendingReady { false };
    std::atomic<int> version { 0 }, designed { 0 };

    juce::SpinLock settingsLock;
    Equalizer::Settings wanted;
};

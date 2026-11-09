#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    LEARN: gain staging done for you.

    While it listens (and the DAW plays) it measures the track coming in:
    its average level - 400 ms at a time, leaving out silence and the quiet
    gaps between hits, the way a VU meter would be read on the busy parts -
    and its loudest peak. Then it works out the TRIM that puts the average
    at -18 dBFS (a console's 0 VU, where the strip's modules are designed
    to sit) without letting the peaks go above -6 dBFS (room for a hot
    snare or a spiky bass). That's set once; nothing rides the level after.

    It finishes on its own after 30 seconds of playing (once it's heard at
    least a second of the track - a tom track may be mostly silence), or
    when asked (from 3 seconds on). The audio thread calls process(); start(), finish() and
    the result are safe from the window.
*/
class GainStager
{
public:
    static constexpr float targetAverageDb = -18.0f, maxPeakDb = -6.0f;
    static constexpr float maxTrimDb = 24.0f;
    static constexpr double listenSeconds = 30.0, minimumSeconds = 3.0;

    struct Result
    {
        bool valid = false;
        float trimDb = 0.0f;       // the TRIM to set
        float averageDb = -100.0f; // what came in (before TRIM): average of the busy parts, dBFS RMS
        float peakDb = -100.0f;    // ... and its loudest peak, dBFS
        bool peakLimited = false;  // true: the peaks, not the average, decided it
    };

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate;
        blockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
    }

    /** Starts listening afresh. */
    void start() noexcept   { restart.store (true); listening.store (true); }

    /** Stops listening. True if it heard enough to give a result (3 s of playing, with some sound in it). */
    bool finish() noexcept
    {
        listening.store (false);
        return heardSeconds() >= minimumSeconds && soundBlocks.load() >= 5;
    }

    void cancel() noexcept  { listening.store (false); }

    bool isListening() const noexcept   { return listening.load(); }
    double heardSeconds() const noexcept { return heardBlocks.load() * 0.1; }   // (seconds of the DAW playing while it listened)

    /** True once, when it finished by itself (30 s played) - for the strip to set TRIM. */
    bool takeFinished() noexcept         { return finishedByItself.exchange (false); }

    /** The audio thread: the track as it comes in (before TRIM). `playing`: is the DAW playing? */
    void process (const float* const* channels, int numChannels, int numSamples, bool playing) noexcept
    {
        if (! listening.load() || ! playing || numChannels <= 0) return;
        if (restart.exchange (false))
        {
            filled = count = 0; sum = 0.0; peak = 0.0f; blocks.fill (0.0); heardBlocks.store (0); soundBlocks.store (0);
            for (auto& w : windows) w = 0.0;
        }
        for (int i = 0; i < numSamples; ++i)
        {
            double square = 0.0;
            for (int c = 0; c < numChannels; ++c)
            {
                const float x = channels[c][i];
                square += (double) x * x;
                peak = juce::jmax (peak, std::abs (x));
            }
            sum += square / numChannels;
            if (++filled >= blockLength)
            {
                blocks[(size_t) (count % 4)] = sum / blockLength;
                sum = 0.0; filled = 0;
                ++count;
                if (count >= 4 && count - 4 < maxWindows)
                    windows[(size_t) (count - 4)] = 0.25 * (blocks[0] + blocks[1] + blocks[2] + blocks[3]);
                heardBlocks.store (heardBlocks.load() + 1);
                if (blocks[(size_t) ((count - 1) % 4)] > silence) soundBlocks.store (soundBlocks.load() + 1);
                if (heardSeconds() >= listenSeconds && soundBlocks.load() >= 10)   // (30 s played - and at least a second of the track heard)
                {
                    listening.store (false);
                    finishedByItself.store (true);
                    return;
                }
            }
        }
        peakShared.store (peak);
        windowCount.store (juce::jmin (maxWindows, juce::jmax (0, count - 3)));
    }

    /** What it heard, and the TRIM that stages it (call once it's finished). */
    Result getResult() const noexcept
    {
        Result r;
        const int n = windowCount.load();
        // The busy parts: above -60 dBFS, and no more than 20 dB under the average of those
        double e = 0.0; int c = 0;
        for (int i = 0; i < n; ++i) if (windows[(size_t) i] > silence) { e += windows[(size_t) i]; ++c; }
        if (c == 0) return r;
        const double gate = (e / c) * 0.01;   // (-20 dB)
        e = 0.0; c = 0;
        for (int i = 0; i < n; ++i) if (windows[(size_t) i] > juce::jmax (gate, silence)) { e += windows[(size_t) i]; ++c; }
        r.averageDb = (float) (10.0 * std::log10 (e / c));
        r.peakDb = juce::Decibels::gainToDecibels (peakShared.load(), -100.0f);
        const float byAverage = targetAverageDb - r.averageDb, byPeak = maxPeakDb - r.peakDb;
        r.peakLimited = byPeak < byAverage;
        r.trimDb = juce::jlimit (-maxTrimDb, maxTrimDb, std::round (juce::jmin (byAverage, byPeak) * 10.0f) / 10.0f);
        r.valid = true;
        return r;
    }

private:
    static constexpr int maxWindows = 3000;         // 5 minutes of 400 ms windows every 100 ms (a sparse track may take a while)
    static constexpr double silence = 1.0e-6;       // -60 dBFS

    double rate = 48000.0;
    int blockLength = 4800, filled = 0, count = 0;
    double sum = 0.0;
    float peak = 0.0f;
    std::array<double, 4> blocks {};
    std::array<double, maxWindows> windows {};
    std::atomic<int> windowCount { 0 }, heardBlocks { 0 }, soundBlocks { 0 };
    std::atomic<float> peakShared { 0.0f };
    std::atomic<bool> listening { false }, restart { false }, finishedByItself { false };
};

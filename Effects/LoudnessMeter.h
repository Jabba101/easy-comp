#pragma once

#include "LoudnessMatcher.h"
#include <array>
#include <atomic>
#include <cmath>

//==============================================================================
/**
    LUFS: loudness the way streaming services measure it (ITU-R BS.1770 /
    EBU R128), for the Easy Strip's output meter.

      SHORT-TERM  the last 3 seconds
      INTEGRATED  the whole song so far (until reset): 400 ms blocks every
                  100 ms, ignoring silence (below -70 LUFS) and the quiet
                  parts (more than 10 LU below the rest), as the standard
                  says - so a quiet intro doesn't drag the number down.

    The ear filter (K-weighting) is the one AUTO-GAIN already uses.
    Every 100 ms the audio thread adds one block; reading the numbers is
    safe from the window at any time.
*/
class LoudnessMeter
{
public:
    static constexpr float silentLufs = -70.0f;

    void prepare (double sampleRate)
    {
        ears.prepare (sampleRate, 100.0f);
        blockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
        resetAll();
    }

    /** Clears the integrated reading (the "click to reset"). Safe from any thread: it happens at the next block. */
    void resetIntegrated() noexcept   { resetRequested.store (true); }

    void process (const juce::AudioBuffer<float>& buffer) noexcept
    {
        const int channels = juce::jmin (2, buffer.getNumChannels());

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
                sum += ears.measure (ch, buffer.getSample (ch, i));

            if (++filled >= blockLength)
                finishBlock();
        }
    }

    float getShortTermLufs() const noexcept    { return shortTerm.load(); }
    float getIntegratedLufs() const noexcept   { return integrated.load(); }

private:
    static float lufsOf (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-20 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -200.0f;
    }

    void resetAll() noexcept
    {
        blocks.fill (0.0);
        position = filled = blocksHeard = blocksSinceReset = 0;
        sum = 0.0;
        histogram.fill (0);
        histogramEnergy.fill (0.0);
        shortTerm.store (-200.0f);
        integrated.store (-200.0f);
        resetRequested.store (false);
    }

    void finishBlock() noexcept
    {
        if (resetRequested.exchange (false))
        {
            histogram.fill (0);
            histogramEnergy.fill (0.0);
            integrated.store (-200.0f);
            blocksSinceReset = 0;
        }

        blocks[(size_t) position] = sum / blockLength;
        position = (position + 1) % numBlocks;
        sum = 0.0;
        filled = 0;
        blocksHeard = juce::jmin (blocksHeard + 1, numBlocks);
        ++blocksSinceReset;

        const auto averageOfLast = [&] (int count)
        {
            count = juce::jmax (1, juce::jmin (count, blocksHeard));   // (only what's been heard so far)
            double total = 0.0;
            for (int k = 1; k <= count; ++k)
                total += blocks[(size_t) ((position - k + numBlocks) % numBlocks)];
            return total / count;
        };

        shortTerm.store (lufsOf (averageOfLast (30)));

        // The integrated reading: each 400 ms gating block, into a histogram (0.1 LU steps)
        // (only whole 400 ms blocks heard since the last reset count)
        const double gatingBlock = averageOfLast (4);
        const float blockLufs = lufsOf (gatingBlock);

        if (blocksSinceReset >= 4 && blockLufs > silentLufs)
        {
            const int bin = juce::jlimit (0, numBins - 1, (int) ((blockLufs - silentLufs) * 10.0f));
            ++histogram[(size_t) bin];
            histogramEnergy[(size_t) bin] += gatingBlock;

            // The relative gate: 10 LU below the average of everything above -70
            double energy = 0.0; long count = 0;
            for (int b = 0; b < numBins; ++b) { energy += histogramEnergy[(size_t) b]; count += histogram[(size_t) b]; }
            const float gate = lufsOf (energy / (double) count) - 10.0f;

            energy = 0.0; count = 0;
            for (int b = juce::jmax (0, (int) ((gate - silentLufs) * 10.0f)); b < numBins; ++b)
            {
                energy += histogramEnergy[(size_t) b];
                count += histogram[(size_t) b];
            }

            if (count > 0)
                integrated.store (lufsOf (energy / (double) count));
        }
    }

    static constexpr int numBlocks = 30;                  // 3 s of 100 ms blocks
    static constexpr int numBins = 750;                   // -70 .. +5 LUFS in 0.1 LU steps

    LoudnessMatcher ears;                                 // (only its ear filters are used)
    std::array<double, numBlocks> blocks {};
    std::array<long, numBins> histogram {};
    std::array<double, numBins> histogramEnergy {};
    int blockLength = 4800, position = 0, filled = 0, blocksHeard = 0, blocksSinceReset = 0;
    double sum = 0.0;
    std::atomic<float> shortTerm { -200.0f }, integrated { -200.0f };
    std::atomic<bool> resetRequested { false };
};

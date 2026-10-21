// Runs every built-in preset over realistic material and reports what the
// meter would show. Not shipped - a tool for tuning the presets.
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "TestHelpers.h"
#include <algorithm>
#include <cstdio>
#include <deque>
#include <map>
#include <vector>

namespace check
{
    constexpr double sampleRate = 48000.0;
    constexpr int block = 480;            // 10 ms
    constexpr int blocksPerWindow = 25;   // 250 ms: about what you'd read off the meter per hit / per word

    inline juce::AudioBuffer<float> loadWav (const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));

        if (reader == nullptr)
        {
            std::printf ("  !! can't read %s\n", file.getFullPathName().toRawUTF8());
            return {};
        }

        juce::AudioBuffer<float> audio ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        return audio;
    }

    /** Which materials each preset is checked on: lines of "plugin | preset | material, material". */
    inline std::map<juce::String, juce::StringArray> readMap (const juce::File& file, const juce::String& plugin)
    {
        std::map<juce::String, juce::StringArray> result;
        juce::StringArray lines;
        file.readLines (lines);

        for (auto& line : lines)
        {
            if (line.trim().isEmpty() || line.trim().startsWith ("#"))
                continue;

            auto parts = juce::StringArray::fromTokens (line, "|", "");
            if (parts.size() != 3 || parts[0].trim() != plugin)
                continue;

            auto materials = juce::StringArray::fromTokens (parts[2], ",", "");
            materials.trim();
            materials.removeEmptyStrings();
            result[parts[1].trim()] = materials;
        }

        return result;
    }

    inline float percentile (std::vector<float> values, float p)
    {
        if (values.empty())
            return 0.0f;

        std::sort (values.begin(), values.end());
        const auto index = (size_t) juce::jlimit (0, (int) values.size() - 1, (int) std::round (p * (float) (values.size() - 1)));
        return values[index];
    }

    struct Run
    {
        std::vector<float> inPeakDb;      // per block
        std::vector<float> meterA;        // per block: gain reduction, or boost
        std::vector<float> meterB;        // per block: (transient) cut
        juce::AudioBuffer<float> output;
    };

    /** Feeds the material through in 10 ms blocks, reading the meters after each. */
    template <typename Processor, typename ReadMeters>
    Run process (Processor& p, const juce::AudioBuffer<float>& input, ReadMeters readMeters)
    {
        Run run;
        run.output = input;
        juce::MidiBuffer midi;

        for (int start = 0; start + block <= run.output.getNumSamples(); start += block)
        {
            run.inPeakDb.push_back (th::toDb (std::max (input.getMagnitude (0, start, block),
                                                        input.getMagnitude (input.getNumChannels() - 1, start, block))));
            juce::AudioBuffer<float> view (run.output.getArrayOfWritePointers(), run.output.getNumChannels(), start, block);
            p.processBlock (view, midi);
            const auto [a, b] = readMeters (p);
            run.meterA.push_back (a);
            run.meterB.push_back (b);
        }

        return run;
    }

    struct Windows
    {
        std::vector<float> maxA, maxB, minA;   // over the loud windows only
        float loudDb = -100.0f;
        float busyFraction = 0.0f;             // share of blocks where meter A shows more than 0.5 dB
    };

    inline Windows windows (const Run& run)
    {
        std::vector<float> windowPeaks;
        const size_t numWindows = run.inPeakDb.size() / blocksPerWindow;

        for (size_t w = 0; w < numWindows; ++w)
            windowPeaks.push_back (*std::max_element (run.inPeakDb.begin() + (long) (w * blocksPerWindow),
                                                      run.inPeakDb.begin() + (long) ((w + 1) * blocksPerWindow)));

        Windows result;
        result.loudDb = percentile (windowPeaks, 0.9f);

        for (size_t w = 1; w < numWindows; ++w)   // (skip the very first window: everything is settling)
        {
            if (windowPeaks[w] < result.loudDb - 10.0f)
                continue;

            const auto from = (long) (w * blocksPerWindow), to = (long) ((w + 1) * blocksPerWindow);
            result.maxA.push_back (*std::max_element (run.meterA.begin() + from, run.meterA.begin() + to));
            result.maxB.push_back (*std::max_element (run.meterB.begin() + from, run.meterB.begin() + to));
            result.minA.push_back (*std::min_element (run.meterA.begin() + from, run.meterA.begin() + to));
        }

        int busy = 0;
        for (auto v : run.meterA)
            busy += v > 0.5f ? 1 : 0;

        result.busyFraction = run.meterA.empty() ? 0.0f : (float) busy / (float) run.meterA.size();
        return result;
    }

    inline float peakDb (const juce::AudioBuffer<float>& b)
    {
        float m = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            m = std::max (m, b.getMagnitude (ch, 0, b.getNumSamples()));
        return th::toDb (m);
    }

    /** Loudness change (dB) over [fromFraction, 1] of the material. */
    inline float loudnessChange (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, float fromFraction = 0.0f)
    {
        const int start = (int) ((float) in.getNumSamples() * fromFraction);
        return (float) (th::loudnessDb (out, start, out.getNumSamples()) - th::loudnessDb (in, start, in.getNumSamples()));
    }

    struct Material
    {
        juce::String name;
        juce::AudioBuffer<float> audio;
    };

    inline const Material* findMaterial (std::deque<Material>& cache, const juce::File& folder, const juce::String& name)
    {
        for (auto& m : cache)
            if (m.name == name)
                return &m;

        auto file = folder.getChildFile (name + ".wav");
        if (! file.existsAsFile())
        {
            std::printf ("  !! no material called %s\n", name.toRawUTF8());
            return nullptr;
        }

        cache.push_back ({ name, loadWav (file) });
        return &cache.back();
    }
}

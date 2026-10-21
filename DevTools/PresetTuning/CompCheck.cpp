// Easy Comp: what the meter shows for each preset, on the kind of track it's meant for.
//   CompCheck <material folder> <presets.tsv> [--tune] [name filter]
// The presets file: category | name | threshold | ratio | attack | release | make-up | mix | ignore below
//                   | reacts to (peaks/average) | style (modern/vintage) | low end (normal/thrust)
//                   | target GR | materials (comma separated; "track<key" = key sent to the sidechain) | tip
#include "PluginProcessor.h"
#include "Check.h"

namespace
{
    struct Row
    {
        Preset preset;
        float target = 0.0f;
        juce::StringArray materials;
        juce::StringArray fields;
    };

    std::vector<Row> readRows (const juce::File& file)
    {
        std::vector<Row> rows;
        juce::StringArray lines;
        file.readLines (lines);

        for (auto& line : lines)
        {
            if (line.trim().isEmpty() || line.trim().startsWith ("#"))
                continue;

            auto f = juce::StringArray::fromTokens (line, "|", "");
            f.trim();
            if (f.size() != 15)
            {
                std::printf ("  !! bad line (%d fields): %s\n", f.size(), line.toRawUTF8());
                continue;
            }

            Row row;
            row.fields = f;
            row.preset.category = f[0];
            row.preset.name     = f[1];
            row.preset.tip      = f[14];
            row.preset.values   = { { CompIDs::threshold, f[2].getFloatValue() }, { CompIDs::ratio,   f[3].getFloatValue() },
                                    { CompIDs::attack,    f[4].getFloatValue() }, { CompIDs::release, f[5].getFloatValue() },
                                    { CompIDs::makeup,    f[6].getFloatValue() }, { CompIDs::mix,     f[7].getFloatValue() },
                                    { CompIDs::lowCut,    f[8].getFloatValue() },
                                    { CompIDs::reactsTo,  f[9] == "average" ? 1.0f : 0.0f },
                                    { CompIDs::character, f[10] == "vintage" ? 1.0f : 0.0f },
                                    { CompIDs::lowEnd,    f[11] == "thrust" ? 1.0f : 0.0f } };
            row.target = f[12].getFloatValue();
            row.materials = juce::StringArray::fromTokens (f[13], ",", "");
            row.materials.trim();
            row.materials.removeEmptyStrings();
            rows.push_back (std::move (row));
        }

        return rows;
    }

    void setValue (Preset& preset, const char* id, float value)
    {
        for (auto& [key, v] : preset.values)
            if (key == id)
                v = value;
    }

    float getValue (const Preset& preset, const char* id)
    {
        for (auto& [key, v] : preset.values)
            if (key == id)
                return v;
        return 0.0f;
    }

    /** A track, and (for sidechain presets) what's sent to the sidechain. */
    struct Material
    {
        juce::String name;
        const check::Material* track = nullptr;
        const check::Material* key = nullptr;
    };

    check::Run runPreset (const Preset& preset, const Material& material, bool autoGain)
    {
        EasyCompAudioProcessor p;
        const auto& audio = material.track->audio;

        if (material.key == nullptr)
        {
            th::prepare (p, check::sampleRate, check::block);
            p.loadPreset (preset);
            th::setParam (p, CommonIDs::autoGain, autoGain ? 1.0f : 0.0f);
            return check::process (p, audio, [] (EasyCompAudioProcessor& q) { return std::make_pair (q.takeGainReductionForMeter(), 0.0f); });
        }

        // The track on channels 0-1, the sidechain on 2-3 (the key loops if it's shorter)
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (check::sampleRate, check::block);
        p.prepareToPlay (check::sampleRate, check::block);
        p.loadPreset (preset);
        th::setParam (p, CommonIDs::autoGain, autoGain ? 1.0f : 0.0f);
        th::setParam (p, CompIDs::listensTo, 1.0f);

        const auto& key = material.key->audio;
        juce::AudioBuffer<float> work (4, audio.getNumSamples());

        for (int ch = 0; ch < 2; ++ch)
        {
            work.copyFrom (ch, 0, audio, std::min (ch, audio.getNumChannels() - 1), 0, audio.getNumSamples());

            for (int i = 0; i < audio.getNumSamples(); ++i)
                work.setSample (2 + ch, i, key.getSample (std::min (ch, key.getNumChannels() - 1), i % key.getNumSamples()));
        }

        auto run = check::process (p, work, [] (EasyCompAudioProcessor& q) { return std::make_pair (q.takeGainReductionForMeter(), 0.0f); });

        // (What the windows judge loudness by: the sidechain - it's what drives the compressor. The
        // output is just your track.)
        run.inPeakDb.clear();
        for (int start = 0; start + check::block <= work.getNumSamples(); start += check::block)
            run.inPeakDb.push_back (th::toDb (std::max (work.getMagnitude (2, start, check::block), work.getMagnitude (3, start, check::block))));

        juce::AudioBuffer<float> trackOut (2, work.getNumSamples());
        trackOut.copyFrom (0, 0, run.output, 0, 0, work.getNumSamples());
        trackOut.copyFrom (1, 0, run.output, 1, 0, work.getNumSamples());
        run.output = trackOut;
        return run;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
        return 1;

    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File materialFolder (argv[1]);
    auto rows = readRows (juce::File (argv[2]));
    const bool tune = argc > 3 && juce::String (argv[3]) == "--tune";
    const juce::String only = argc > (tune ? 4 : 3) ? juce::String (argv[tune ? 4 : 3]) : juce::String();
    std::deque<check::Material> cache;

    std::printf ("%-34s %-19s | thresh | GR: median   90%%  floor  busy | louder  out peak | AUTO-GAIN\n", "preset", "material");

    for (auto& row : rows)
    {
        if (only.isNotEmpty() && ! row.preset.name.containsIgnoreCase (only) && ! row.preset.category.containsIgnoreCase (only))
            continue;

        std::vector<Material> materialRuns;
        for (auto& name : row.materials)
        {
            const auto trackName = name.upToFirstOccurrenceOf ("<", false, false).trim();
            const auto keyName = name.fromFirstOccurrenceOf ("<", false, false).trim();
            Material m { name, check::findMaterial (cache, materialFolder, trackName),
                         keyName.isEmpty() ? nullptr : check::findMaterial (cache, materialFolder, keyName) };

            if (m.track != nullptr && (keyName.isEmpty() || m.key != nullptr))
                materialRuns.push_back (m);
        }

        std::vector<const Material*> materials;
        for (auto& m : materialRuns)
            materials.push_back (&m);

        if (materials.empty())
        {
            std::printf ("%-34s (no material)\n", row.preset.name.toRawUTF8());
            continue;
        }

        if (tune && row.target > 0.0f)
        {
            // Find the THRESHOLD that gives the target gain reduction (median over the loud parts) ...
            const auto averageMedian = [&] (float threshold)
            {
                auto trial = row.preset;
                setValue (trial, CompIDs::threshold, threshold);
                float total = 0.0f;
                for (auto* m : materials)
                    total += check::percentile (check::windows (runPreset (trial, *m, false)).maxA, 0.5f);
                return total / (float) materials.size();
            };

            float lo = -60.0f, hi = 0.0f;
            for (int i = 0; i < 14; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                (averageMedian (mid) > row.target ? lo : hi) = mid;
            }

            const float threshold = std::round (0.5f * (lo + hi) * 2.0f) / 2.0f;
            setValue (row.preset, CompIDs::threshold, threshold);

            // ... then the MAKE-UP that keeps the loudness about the same (searched, so it
            // also works for parallel presets, where MAKE-UP only lifts the squashed part)
            const auto averageChange = [&] (float makeup)
            {
                auto trial = row.preset;
                setValue (trial, CompIDs::makeup, makeup);
                float total = 0.0f;
                for (auto* m : materials)
                    total += check::loudnessChange (m->track->audio, runPreset (trial, *m, false).output);
                return total / (float) materials.size();
            };

            float down = 0.0f, up = 24.0f;
            for (int i = 0; i < 10; ++i)
            {
                const float mid = 0.5f * (down + up);
                (averageChange (mid) < 0.0f ? down : up) = mid;
            }

            setValue (row.preset, CompIDs::makeup, std::round ((down + up) * 0.5f * 2.0f) / 2.0f);
        }

        for (auto* m : materials)
        {
            const auto plain   = runPreset (row.preset, *m, false);
            const auto matched = runPreset (row.preset, *m, true);
            const auto w = check::windows (plain);
            const float median = check::percentile (w.maxA, 0.5f);

            std::printf ("%-34s %-19s | %6.1f |     %6.1f %5.1f %5.1f  %3.0f%% | %+6.1f %8.1f | %+6.2f %s\n",
                         row.preset.name.toRawUTF8(), m->name.toRawUTF8(), getValue (row.preset, CompIDs::threshold),
                         median, check::percentile (w.maxA, 0.9f), check::percentile (w.minA, 0.5f), 100.0f * w.busyFraction,
                         check::loudnessChange (m->track->audio, plain.output), check::peakDb (plain.output),
                         check::loudnessChange (m->track->audio, matched.output, 0.4f),
                         row.target > 0.0f && std::abs (median - row.target) > 1.5f ? "  <-- off target" : "");
        }

        if (tune && row.target > 0.0f)
        {
            auto f = row.fields;
            f.set (2, juce::String (getValue (row.preset, CompIDs::threshold), 1));
            f.set (6, juce::String (getValue (row.preset, CompIDs::makeup), 1));
            std::printf ("    tuned: %s\n", f.joinIntoString (" | ").toRawUTF8());
        }
    }

    return 0;
}

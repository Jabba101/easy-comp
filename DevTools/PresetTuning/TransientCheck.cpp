// Easy Transient: how much each preset lifts the hits and cuts the tails, on the kind of track it's meant for.
#include "PluginProcessor.h"
#include "Check.h"

namespace
{
    struct Row
    {
        Preset preset;
        juce::StringArray materials;
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
            if (f.size() != 8)
            {
                std::printf ("  !! bad line (%d fields): %s\n", f.size(), line.toRawUTF8());
                continue;
            }

            Row row;
            row.preset.category = f[0];
            row.preset.name     = f[1];
            row.preset.tip      = f[7];
            row.preset.values   = { { TransientIDs::punch,  f[2].getFloatValue() }, { TransientIDs::sustain, f[3].getFloatValue() },
                                    { TransientIDs::output, f[4].getFloatValue() }, { TransientIDs::mix,     f[5].getFloatValue() } };
            row.materials = juce::StringArray::fromTokens (f[6], ",", "");
            row.materials.trim();
            row.materials.removeEmptyStrings();
            rows.push_back (std::move (row));
        }

        return rows;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
        return 1;

    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File materialFolder (argv[1]);
    const auto rows = readRows (juce::File (argv[2]));
    const juce::String only = argc > 3 ? juce::String (argv[3]) : juce::String();
    std::deque<check::Material> cache;

    std::printf ("%-38s %-19s | boost: med  90%% | cut: med  90%% | louder  peak chg | AUTO-GAIN\n", "preset", "material");

    for (auto& row : rows)
    {
        const auto& preset = row.preset;

        if (only.isNotEmpty() && ! preset.name.containsIgnoreCase (only) && ! preset.category.containsIgnoreCase (only))
            continue;

        for (auto& materialName : row.materials)
        {
            const auto* material = check::findMaterial (cache, materialFolder, materialName);
            if (material == nullptr)
                continue;

            const auto runWith = [&] (bool autoGain)
            {
                EasyTransientAudioProcessor p;
                th::prepare (p, check::sampleRate, check::block);
                p.loadPreset (preset);
                th::setParam (p, CommonIDs::autoGain, autoGain ? 1.0f : 0.0f);
                return check::process (p, material->audio,
                                       [] (EasyTransientAudioProcessor& q) { return std::make_pair (q.takeBoostForMeter(), q.takeCutForMeter()); });
            };

            const auto plain   = runWith (false);
            const auto matched = runWith (true);
            const auto w = check::windows (plain);

            std::printf ("%-38s %-19s |      %5.1f %5.1f |    %5.1f %5.1f | %+6.1f %+8.1f | %+6.2f\n",
                         preset.name.toRawUTF8(), materialName.toRawUTF8(),
                         check::percentile (w.maxA, 0.5f), check::percentile (w.maxA, 0.9f),
                         check::percentile (w.maxB, 0.5f), check::percentile (w.maxB, 0.9f),
                         check::loudnessChange (material->audio, plain.output),
                         check::peakDb (plain.output) - check::peakDb (material->audio),
                         check::loudnessChange (material->audio, matched.output, 0.4f));
        }
    }

    return 0;
}

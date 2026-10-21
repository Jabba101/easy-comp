// Easy DS: what each preset does to the harsh moments (S sounds, harsh cymbal
// hits) and to everything else, on the kind of track it's meant for. Each
// material has a label for every 5 ms: 0 other, 1 ordinary, 2 harsh.
// Not shipped - a tool for tuning the presets.
#include "DeEsser.h"
#include "HarshnessFinder.h"
#include "Check.h"
#include <juce_core/juce_core.h>

namespace
{
    struct Row
    {
        juce::String category, name, tip;
        DeEsser::Settings settings;
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
            if (f.size() != 9)
            {
                std::printf ("  !! bad line (%d fields): %s\n", f.size(), line.toRawUTF8());
                continue;
            }

            Row row;
            row.category = f[0];
            row.name = f[1];
            row.settings.fromHz      = f[2].getFloatValue();
            row.settings.toHz        = f[3].getFloatValue();
            row.settings.thresholdDb = f[4].getFloatValue();
            row.settings.rangeDb     = f[5].getFloatValue();
            row.settings.wholeSound  = f[6].trim().equalsIgnoreCase ("whole");
            row.materials = juce::StringArray::fromTokens (f[7], ",", "");
            row.materials.trim();
            row.materials.removeEmptyStrings();
            row.tip = f[8];
            rows.push_back (row);
        }

        return rows;
    }

    std::vector<int> readLabels (const juce::File& file)
    {
        std::vector<int> labels;
        const auto json = juce::JSON::parse (file);
        if (auto* array = json["labels"].getArray())
            for (auto& v : *array)
                labels.push_back ((int) v);
        return labels;
    }

    /** Energy per 5 ms frame above a frequency (a steep high-pass), of channel 0, shifted 'delay' samples earlier. */
    std::vector<double> bandEnergy (const juce::AudioBuffer<float>& audio, float fromHz, float toHz, int delay)
    {
        juce::IIRFilter hp1, hp2, lp1;
        hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (check::sampleRate, fromHz, 0.5412));
        hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (check::sampleRate, fromHz, 1.3066));
        lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (check::sampleRate, std::min (toHz, 21000.0f)));
        const bool limited = toHz < DeEsser::toOffHz - 1.0f;

        const int n = audio.getNumSamples();
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = 0.5f * (audio.getSample (0, i) + audio.getSample (audio.getNumChannels() - 1, i));

        hp1.processSamples (x.data(), n);
        hp2.processSamples (x.data(), n);
        if (limited) lp1.processSamples (x.data(), n);

        std::vector<double> frames ((size_t) (n / 240), 0.0);
        for (int i = delay; i < n; ++i)
        {
            const auto f = (size_t) ((i - delay) / 240);
            if (f < frames.size())
                frames[f] += (double) x[(size_t) i] * x[(size_t) i];
        }
        return frames;
    }

    std::vector<double> fullEnergy (const juce::AudioBuffer<float>& audio, int delay)
    {
        const int n = audio.getNumSamples();
        std::vector<double> frames ((size_t) (n / 240), 0.0);
        for (int i = delay; i < n; ++i)
        {
            const auto f = (size_t) ((i - delay) / 240);
            const double v = 0.5 * ((double) audio.getSample (0, i) + audio.getSample (audio.getNumChannels() - 1, i));
            if (f < frames.size())
                frames[f] += v * v;
        }
        return frames;
    }

    double changeDb (const std::vector<double>& in, const std::vector<double>& out, const std::vector<int>& labels, int wanted)
    {
        double a = 0.0, b = 0.0;
        for (size_t i = 0; i < std::min ({ in.size(), out.size(), labels.size() }); ++i)
            if (labels[i] == wanted) { a += in[i]; b += out[i]; }
        return a > 0.0 ? 10.0 * std::log10 ((b + 1.0e-30) / a) : 0.0;
    }

    juce::String findBand (const juce::AudioBuffer<float>& audio)
    {
        HarshnessFinder::Listener listener;
        listener.prepare (check::sampleRate);
        std::vector<HarshnessFinder::Frame> frames;
        int sound = 0;

        for (int start = 0; start + 480 <= audio.getNumSamples() && sound < 800; start += 480)
        {
            juce::AudioBuffer<float> view (const_cast<float* const*> (audio.getArrayOfReadPointers()), audio.getNumChannels(), start, 480);
            listener.process (view, [&] (const HarshnessFinder::Frame& f) { frames.push_back (f); sound += f.total > 1.0e-6f ? 1 : 0; });
        }

        const auto r = HarshnessFinder::analyse (frames, [&] (int b) { return listener.isUsable (b); });

        if (r.outcome == HarshnessFinder::Result::Outcome::found)
            return juce::String (r.fromHz / 1000.0f, 1) + "-" + (r.toHz >= DeEsser::toOffHz - 1.0f ? juce::String ("up") : juce::String (r.toHz / 1000.0f, 1)) + "k";

        return r.outcome == HarshnessFinder::Result::Outcome::nothingStoodOut ? "nothing" : "too little";
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::ScopedNoDenormals noDenormals;
    const juce::File here ("/home/claude/presetcheck");
    const auto rows = readRows (here.getChildFile (argc > 1 ? argv[1] : "presets_ds.tsv"));
    const juce::File folder = here.getChildFile ("ds_material");
    std::deque<check::Material> cache;
    std::map<juce::String, juce::String> found;

    std::printf ("%-34s %-17s | %-27s | %-34s | %s\n", "preset", "material", "HARSH: avg  p90  band dB", "ORDINARY: avg  >1dB%  >3dB%  hf dB", "find");

    // SWEEP=1: try each preset's materials at a range of THRESHOLD settings instead
    if (juce::SystemStats::getEnvironmentVariable ("SWEEP", {}).isNotEmpty())
    {
        std::vector<Row> swept;
        for (auto& row : rows)
            for (float t = -20.0f; t <= 12.0f; t += 2.0f)
            {
                Row r = row;
                r.settings.thresholdDb = t;
                r.name = row.name.substring (0, 26) + " T" + juce::String ((int) t);
                swept.push_back (r);
            }
        const_cast<std::vector<Row>&> (rows) = swept;
    }

    for (auto& row : rows)
    {
        for (auto& materialName : row.materials)
        {
            const auto* material = check::findMaterial (cache, folder, materialName);
            if (material == nullptr) continue;
            const auto labels = readLabels (folder.getChildFile (materialName + ".json"));

            DeEsser d;
            d.prepare (check::sampleRate, 240, row.settings);
            juce::AudioBuffer<float> out (material->audio);
            std::vector<float> reduction, ratio;

            for (int start = 0; start < out.getNumSamples(); start += check::block)
            {
                juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), start, std::min (check::block, out.getNumSamples() - start));
                d.process (view, [&] (const DeEsser::Moment& m)
                {
                    reduction.push_back (-th::toDb (m.gain));
                    ratio.push_back (10.0f * std::log10 ((m.bandLevel + 1.0e-30f) / (m.lineLevel + 1.0e-30f)));
                });
            }

            std::vector<float> harsh, ordinary, harshRatio, ordinaryRatio;
            for (size_t i = 0; i < std::min (reduction.size(), labels.size()); ++i)
            {
                if (labels[i] == 2) { harsh.push_back (reduction[i]); harshRatio.push_back (ratio[i]); }
                if (labels[i] == 1) { ordinary.push_back (reduction[i]); ordinaryRatio.push_back (ratio[i]); }
            }

            const auto mean = [] (const std::vector<float>& v) { double s = 0; for (auto x : v) s += x; return v.empty() ? 0.0 : s / (double) v.size(); };
            const auto share = [] (const std::vector<float>& v, float over) { int c = 0; for (auto x : v) c += x > over ? 1 : 0; return v.empty() ? 0.0 : 100.0 * c / (double) v.size(); };

            const int D = d.getLatencySamples();
            const auto bandIn = bandEnergy (material->audio, row.settings.fromHz, row.settings.toHz, 0);
            const auto bandOut = bandEnergy (out, row.settings.fromHz, row.settings.toHz, D);
            const auto hfIn = bandEnergy (material->audio, 3000.0f, DeEsser::toOffHz, 0);
            const auto hfOut = bandEnergy (out, 3000.0f, DeEsser::toOffHz, D);

            if (found.find (materialName) == found.end())
                found[materialName] = findBand (material->audio);

            std::printf ("%-34s %-17s | %6.1f %5.1f %7.1f        | %8.2f %6.1f %6.1f %7.2f        | %s\n",
                         row.name.toRawUTF8(), materialName.toRawUTF8(),
                         mean (harsh), check::percentile (harsh, 0.9f), changeDb (bandIn, bandOut, labels, 2),
                         mean (ordinary), share (ordinary, 1.0f), share (ordinary, 3.0f), changeDb (hfIn, hfOut, labels, 1),
                         found[materialName].toRawUTF8());

            if (juce::SystemStats::getEnvironmentVariable ("RATIOS", {}).isNotEmpty())
                std::printf ("      band - followed, dB:  harsh p10 %5.1f  p50 %5.1f  p90 %5.1f   |  ordinary p50 %5.1f  p90 %5.1f  p97 %5.1f  p99 %5.1f\n",
                             check::percentile (harshRatio, 0.1f), check::percentile (harshRatio, 0.5f), check::percentile (harshRatio, 0.9f),
                             check::percentile (ordinaryRatio, 0.5f), check::percentile (ordinaryRatio, 0.9f), check::percentile (ordinaryRatio, 0.97f),
                             check::percentile (ordinaryRatio, 0.99f));
        }
    }

    return 0;
}

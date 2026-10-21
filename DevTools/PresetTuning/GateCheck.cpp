// Easy Gate: what each preset keeps and what it removes, on the kind of track it's meant for.
// Each material comes as the mix plus its two parts (keep / other), so both can be measured.
#include "Gate.h"
#include "Check.h"
#include <juce_core/juce_core.h>

namespace
{
    struct Row
    {
        juce::String category, name, tip;
        Gate::Settings settings;
        juce::StringArray materials;
        bool sidechain = false;   // (keyed from the 'keyed by' material instead)
        juce::String keyMaterial;
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
            if (f.size() != 11)
            {
                std::printf ("  !! bad line (%d fields): %s\n", f.size(), line.toRawUTF8());
                continue;
            }

            Row row;
            row.category = f[0];
            row.name = f[1];
            row.settings.thresholdDb = f[2].getFloatValue();
            row.settings.rangeDb     = f[3].getFloatValue();
            row.settings.attackMs    = f[4].getFloatValue();
            row.settings.holdMs      = f[5].getFloatValue();
            row.settings.releaseMs   = f[6].getFloatValue();
            row.settings.lowCutHz    = f[7].getFloatValue();
            row.settings.highCutHz   = f[8].getFloatValue();
            auto materials = juce::StringArray::fromTokens (f[9], ",", "");
            materials.trim();
            materials.removeEmptyStrings();
            for (auto& m : materials)
            {
                if (m.contains ("<"))   // "room<snare": the room, keyed by the snare's close mic
                {
                    row.sidechain = true;
                    row.keyMaterial = m.fromFirstOccurrenceOf ("<", false, false).trim();
                    m = m.upToFirstOccurrenceOf ("<", false, false).trim();
                }
                row.materials.add (m);
            }
            row.tip = f[10];
            rows.push_back (std::move (row));
        }

        return rows;
    }

    struct Material
    {
        juce::AudioBuffer<float> mix, keep, other;
        std::vector<double> onsets;
    };

    Material load (const juce::File& folder, const juce::String& name)
    {
        Material m;
        m.mix   = check::loadWav (folder.getChildFile (name + ".wav"));
        m.keep  = check::loadWav (folder.getChildFile (name + ".keep.wav"));
        m.other = check::loadWav (folder.getChildFile (name + ".other.wav"));
        const auto json = juce::JSON::parse (folder.getChildFile (name + ".json"));
        if (auto* onsets = json.getProperty ("onsets", {}).getArray())
            for (auto& t : *onsets)
                m.onsets.push_back ((double) t);
        return m;
    }

    double energy (const juce::AudioBuffer<float>& b, int from, int to)
    {
        double sum = 0.0;
        for (int i = std::max (0, from); i < std::min (to, b.getNumSamples()); ++i)
            sum += (double) b.getSample (0, i) * (double) b.getSample (0, i);
        return sum;
    }

    double db (double ratio)   { return 10.0 * std::log10 (std::max (ratio, 1.0e-30)); }

    struct Result
    {
        double removedDb = 0.0;       // the bleed / noise, between the wanted sounds
        double deepDb = 0.0;          // ...once the wanted sound has been silent for 300 ms
        double keptDb = 0.0;          // the wanted sound, overall
        double onsetDb = 0.0;         // the first 10 ms of each wanted sound: energy change, the worst one
        double onsetWorstDb = 0.0;    // ...and the worst single sample in them
        double tailKeptToDb = 0.0;    // how far an isolated sound dies away (below its own peak) before the gate takes 3 dB off it (median)
        int falseOpens = 0, missed = 0, openings = 0, wanted = 0, tails = 0;
    };

    Result measure (const Row& row, const Material& m, const Material* key)
    {
        constexpr double sr = check::sampleRate;
        Gate gate;
        const int momentLength = 240;
        gate.prepare (sr, momentLength, row.settings);
        const int D = gate.getLatencySamples();
        const int n = m.mix.getNumSamples();

        // The keep and other parts go through together, with the mix (or the key's mix) as what the gate hears,
        // so both get exactly the gain the mix would.
        juce::AudioBuffer<float> parts (4, n);
        parts.copyFrom (0, 0, m.keep, 0, 0, n);  parts.copyFrom (1, 0, m.keep, 1, 0, n);
        parts.copyFrom (2, 0, m.other, 0, 0, n); parts.copyFrom (3, 0, m.other, 1, 0, n);
        const auto& heard = key != nullptr ? key->mix : m.mix;

        std::vector<Gate::Moment> moments;
        for (int start = 0; start < n; start += check::block)
        {
            const int len = std::min (check::block, n - start);
            juce::AudioBuffer<float> view (parts.getArrayOfWritePointers(), 4, start, len);
            juce::AudioBuffer<float> keyView (const_cast<float* const*> (heard.getArrayOfReadPointers()), 2, start, len);
            gate.process (view, &keyView, [&] (const Gate::Moment& mo) { moments.push_back (mo); });
        }

        juce::AudioBuffer<float> keepOut (1, n), otherOut (1, n);
        keepOut.clear(); otherOut.clear();
        for (int i = 0; i + D < n; ++i)   // (lined back up with the input)
        {
            keepOut.setSample (0, i, parts.getSample (0, i + D));
            otherOut.setSample (0, i, parts.getSample (2, i + D));
        }

        Result r;

        // Where the wanted sound is silent (under -50 dB of its peak, in 5 ms frames): "between the hits"
        const float keepPeak = m.keep.getMagnitude (0, 0, n);
        double otherIn = 0.0, otherOutE = 0.0, deepIn = 0.0, deepOut = 0.0;
        int silentFrames = 0;
        const auto justBeforeAHit = [&] (int f)   // (where the look-ahead is already opening up for it)
        {
            for (auto onset : m.onsets)
                if ((double) (f + momentLength) / sr > onset - 0.01 && (double) f / sr < onset)
                    return true;
            return false;
        };
        for (int f = 0; f + momentLength <= n; f += momentLength)
        {
            if (m.keep.getMagnitude (0, f, momentLength) < keepPeak * 0.00316f && ! justBeforeAHit (f))
            {
                otherIn += energy (m.other, f, f + momentLength);
                otherOutE += energy (otherOut, f, f + momentLength);
                if (++silentFrames > 60)   // (300 ms)
                {
                    deepIn += energy (m.other, f, f + momentLength);
                    deepOut += energy (otherOut, f, f + momentLength);

                    if (std::getenv ("GATE_DEBUG") != nullptr && energy (otherOut, f, f + momentLength) > 1.0e-3 * energy (m.other, f, f + momentLength))
                        std::printf ("    deep frame at %.3f s: keep %.1f dB, other %.1f dB -> %.1f dB\n", f / sr,
                                     20.0 * std::log10 (m.keep.getMagnitude (0, f, momentLength) + 1e-12),
                                     20.0 * std::log10 (m.other.getMagnitude (0, f, momentLength) + 1e-12),
                                     20.0 * std::log10 (otherOut.getMagnitude (0, f, momentLength) + 1e-12));
                }
            }
            else
            {
                silentFrames = 0;
            }
        }
        r.removedDb = db (otherOutE / std::max (otherIn, 1e-30));
        r.deepDb = deepIn > 0.0 ? db (deepOut / deepIn) : 0.0;
        r.keptDb = db (energy (keepOut, 0, n - D) / std::max (energy (m.keep, 0, n - D), 1e-30));

        // Each wanted sound: its start, and how far down its tail is kept
        std::vector<double> tails;
        for (size_t k = 0; k < m.onsets.size(); ++k)
        {
            const int from = (int) (m.onsets[k] * sr);
            const int to = k + 1 < m.onsets.size() ? (int) (m.onsets[k + 1] * sr) : n - D;
            if (to - from < 480) continue;
            ++r.wanted;

            double worst = 0.0;
            for (int i = from; i < from + 480; ++i)
                if (std::abs (m.keep.getSample (0, i)) > keepPeak * 0.01f)
                    worst = std::min (worst, 20.0 * std::log10 (std::abs (keepOut.getSample (0, i) / m.keep.getSample (0, i)) + 1e-30));
            r.onsetWorstDb = std::min (r.onsetWorstDb, worst);
            r.onsetDb = std::min (r.onsetDb, db (energy (keepOut, from, from + 480) / std::max (energy (m.keep, from, from + 480), 1e-30)));

            // the tail of a sound with room to die away: walk along in 5 ms frames until the gate has taken 3 dB off
            if (to - from >= (int) (0.4 * sr))
            {
                const float hitPeak = m.keep.getMagnitude (0, from, std::min (2400, to - from));
                double keptTo = 0.0;
                for (int f = from; f + momentLength <= to; f += momentLength)
                {
                    const float in = m.keep.getMagnitude (0, f, momentLength), out = keepOut.getMagnitude (0, f, momentLength);
                    if (in < hitPeak * 0.0001f) break;
                    if (out < in * 0.708f) break;
                    keptTo = 20.0 * std::log10 (in / hitPeak);
                }
                tails.push_back (keptTo);
                ++r.tails;
            }

            // was it open at the start?
            const int moment = from / momentLength;   // (the moments are in the gate's own time: what it hears isn't delayed)
            bool openAtStart = false;
            for (int j = std::max (0, moment - 1); j <= moment + 2 && j < (int) moments.size(); ++j)
                openAtStart = openAtStart || moments[(size_t) j].open;
            if (! openAtStart) ++r.missed;
        }
        r.tailKeptToDb = tails.empty() ? 0.0 : (double) check::percentile (std::vector<float> (tails.begin(), tails.end()), 0.5f);

        // Openings, and how many had no wanted sound starting in them (bleed or noise opened it)
        for (size_t j = 0; j < moments.size(); ++j)
        {
            if (! moments[j].open || (j > 0 && moments[j - 1].open)) continue;
            ++r.openings;
            const double t = (double) (j * momentLength) / sr;   // (the moments are in the gate's own time: the key isn't delayed)
            bool explained = false;
            for (auto onset : m.onsets)
                explained = explained || (onset > t - 0.03 && onset < t + 0.03);
            if (! explained) ++r.falseOpens;
        }

        return r;
    }
}

int main (int argc, char* argv[])
{
    if (argc < 3)
        return 1;

    juce::ScopedJuceInitialiser_GUI juce;
    const juce::File folder (argv[1]);
    const auto rows = readRows (juce::File (argv[2]));
    const juce::String only = argc > 3 ? juce::String (argv[3]) : juce::String();
    std::map<juce::String, Material> cache;
    const auto get = [&] (const juce::String& name) -> const Material& {
        if (cache.find (name) == cache.end()) cache[name] = load (folder, name);
        return cache[name];
    };

    std::printf ("%-34s %-15s| removed  deep | kept  start (worst) tail to | opened false missed/wanted\n", "preset", "material");

    for (auto& row : rows)
    {
        if (only.isNotEmpty() && ! row.name.containsIgnoreCase (only))
            continue;

        for (auto& name : row.materials)
        {
            const auto& m = get (name);
            const Material* key = row.sidechain ? &get (row.keyMaterial) : nullptr;
            const auto r = measure (row, m, key);
            std::printf ("%-34s %-15s| %6.1f %6.1f | %5.2f %5.2f (%6.1f) %6.1f%s | %4d %5d %4d/%d\n", row.name.toRawUTF8(),
                         (name + (row.sidechain ? "<" + row.keyMaterial : juce::String())).toRawUTF8(),
                         r.removedDb, r.deepDb, r.keptDb, r.onsetDb, r.onsetWorstDb, r.tailKeptToDb, r.tails > 0 ? " " : "*",
                         r.openings, r.falseOpens, r.missed, r.wanted);
        }
    }

    return 0;
}

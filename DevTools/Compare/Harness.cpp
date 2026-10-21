// Side-by-side harness for the Easy Comp 1.6 update (CHARACTER, THRUST, sidechain).
//
// "before" = the effects as released (Easy Comp 1.5.1: snapshot in /home/claude/opt/v151-src)
// "after"  = the effects as they are now (/home/claude/EasyPlugins)
//
//   Harness compare            runs every scenario through both and reports how far apart the outputs are
//   Harness bench              times both on the same material (min of several runs)
//   Harness profile comp|trans before|after seconds   runs one of them alone (for callgrind)

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <type_traits>
#include <tuple>
#include <vector>
#if defined (__SSE2__) || defined (_M_X64)
 #include <emmintrin.h>
 #include <xmmintrin.h>
#endif

namespace before
{
    #include "/home/claude/opt/v151-src/Effects/Compressor.h"
    #include "/home/claude/opt/v151-src/Effects/TransientShaper.h"
}

namespace after
{
    #include "/home/claude/EasyPlugins/Effects/Compressor.h"
    #include "/home/claude/EasyPlugins/Effects/TransientShaper.h"
}

using Clock = std::chrono::steady_clock;
static double secondsOf (Clock::duration d) { return std::chrono::duration<double> (d).count(); }

//==============================================================================
struct CompParams
{
    float threshold = -20.0f, ratio = 4.0f, attack = 10.0f, release = 100.0f, makeup = 0.0f, mix = 1.0f, lowcut = 20.0f;
    bool average = false, autoGain = false, bypass = false;
};

struct TransParams
{
    float punch = 0.0f, sustain = 0.0f, output = 0.0f, mix = 1.0f;
    bool autoGain = false, bypass = false;
};

static bool forceVintage = false, forceThrust = false;   // (for the VINTAGE / THRUST resting checks)

template <class S> S makeSettings (const CompParams& p)
{
    S s;
    s.thresholdDb = p.threshold; s.ratio = p.ratio; s.attackMs = p.attack; s.releaseMs = p.release;
    s.makeupDb = p.makeup; s.mix = p.mix; s.lowCutHz = p.lowcut; s.reactToAverage = p.average;
    s.autoGain = p.autoGain; s.bypassed = p.bypass;
    if constexpr (requires { s.vintage; s.thrust; })
    {
        s.vintage = forceVintage;
        s.thrust = forceThrust;
    }
    return s;
}

template <class S> S makeSettings (const TransParams& p)
{
    S s;
    s.punch = p.punch; s.sustain = p.sustain; s.outputDb = p.output; s.mix = p.mix;
    s.autoGain = p.autoGain; s.bypassed = p.bypass;
    return s;
}

template <class R> auto meterValues (const R& r) -> decltype (r.maxReductionDb, std::array<float, 3>())  { return { r.maxReductionDb, r.maxLevelDb, r.autoGainDb }; }
template <class R> auto meterValues (const R& r) -> decltype (r.maxBoostDb, std::array<float, 3>())      { return { r.maxBoostDb, r.maxCutDb, r.autoGainDb }; }

template <class P> using Script = std::function<void (long long start, int length, P& params, bool& restart)>;
template <class T> struct Identity { using type = T; };

//==============================================================================
struct RunResult
{
    double maxAbsDiff = 0.0, peak = 0.0, maxGainDiffDb = 0.0, maxMeterDiffDb = 0.0;
    double secondsBefore = 0.0, secondsAfter = 0.0, audioSeconds = 0.0;
    long long whereDiff = -1, whereGain = -1, whereMeter = -1;
    double sampleRate = 48000.0;

    double diffDb() const   { return maxAbsDiff > 0.0 ? 20.0 * std::log10 (maxAbsDiff / std::max (peak, 1.0e-30)) : -999.0; }
};

template <class EB, class EA, class P>
RunResult runPair (const juce::AudioBuffer<float>& input, double sampleRate, int block, P params, const typename Identity<Script<P>>::type& script = {})
{
    juce::ScopedNoDenormals noDenormals;
    auto b = std::make_unique<EB>();
    auto a = std::make_unique<EA>();
    b->prepare (sampleRate, makeSettings<typename EB::Settings> (params));
    a->prepare (sampleRate, makeSettings<typename EA::Settings> (params));

    juce::AudioBuffer<float> outB (input), outA (input);
    const int numChannels = input.getNumChannels();
    const long long n = input.getNumSamples();

    RunResult r;
    r.audioSeconds = (double) n / sampleRate;
    r.sampleRate = sampleRate;

    for (long long start = 0; start < n; start += block)
    {
        const int length = (int) std::min<long long> (block, n - start);
        bool restart = false;

        if (script)
            script (start, length, params, restart);

        b->setSettings (makeSettings<typename EB::Settings> (params));
        a->setSettings (makeSettings<typename EA::Settings> (params));

        if (restart)
        {
            b->restartAutoGain();
            a->restartAutoGain();
        }

        juce::AudioBuffer<float> viewB (outB.getArrayOfWritePointers(), numChannels, (int) start, length);
        juce::AudioBuffer<float> viewA (outA.getArrayOfWritePointers(), numChannels, (int) start, length);

        const auto t0 = Clock::now();
        const auto resultB = b->process (viewB);
        const auto t1 = Clock::now();
        const auto resultA = a->process (viewA);
        const auto t2 = Clock::now();
        r.secondsBefore += secondsOf (t1 - t0);
        r.secondsAfter  += secondsOf (t2 - t1);

        const auto mb = meterValues (resultB), ma = meterValues (resultA);

        for (size_t k = 0; k < 3; ++k)
            if ((double) std::abs (mb[k] - ma[k]) > r.maxMeterDiffDb)
            {
                r.maxMeterDiffDb = (double) std::abs (mb[k] - ma[k]);
                r.whereMeter = start + (long long) k * 1000000000LL;
            }
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* x = outB.getReadPointer (ch);
        const float* y = outA.getReadPointer (ch);

        for (long long i = 0; i < n; ++i)
        {
            const double ref = x[i], got = y[i];
            if (std::abs (got - ref) > r.maxAbsDiff) { r.maxAbsDiff = std::abs (got - ref); r.whereDiff = i; }
            r.peak = std::max (r.peak, std::abs (ref));

            if (std::abs (ref) > 1.0e-4)
            {
                const double g = got == 0.0 ? 999.0 : std::abs (20.0 * std::log10 (std::abs (got / ref)));
                if (g > r.maxGainDiffDb) { r.maxGainDiffDb = g; r.whereGain = i; }
            }
        }
    }

    return r;
}

//==============================================================================
struct Group
{
    juce::String name;
    int runs = 0;
    double worstDiffDb = -999.0, worstGainDb = 0.0, worstMeterDb = 0.0;
    double secondsBefore = 0.0, secondsAfter = 0.0, audioSeconds = 0.0;
    juce::String worstRun, worstGainRun, worstMeterRun;

    void add (const RunResult& r, const juce::String& runName)
    {
        ++runs;
        auto at = [&] (long long where) { return juce::String (where >= 1000000000LL ? "meter " + juce::String ((int) (where / 1000000000LL)) + " " : "")
                                                 + juce::String ((double) (where % 1000000000LL) / r.sampleRate, 4) + " s"; };
        if (r.diffDb() > worstDiffDb) { worstDiffDb = r.diffDb(); worstRun = runName + " at " + at (r.whereDiff); }
        if (r.maxGainDiffDb > worstGainDb) { worstGainDb = r.maxGainDiffDb; worstGainRun = runName + " at " + at (r.whereGain); }
        if (r.maxMeterDiffDb > worstMeterDb) { worstMeterDb = r.maxMeterDiffDb; worstMeterRun = runName + " at " + at (r.whereMeter); }
        secondsBefore += r.secondsBefore;
        secondsAfter  += r.secondsAfter;
        audioSeconds  += r.audioSeconds;
    }

    bool passes() const   { return worstDiffDb <= -100.0 && worstGainDb <= 0.001 && worstMeterDb <= 0.01; }

    void print() const
    {
        std::printf ("%-44s %4d runs  diff %7.1f dB  gain %.6f dB  meters %.5f dB  speed x%.2f  %s\n",
                     name.toRawUTF8(), runs, worstDiffDb, worstGainDb, worstMeterDb,
                     secondsAfter > 0.0 ? secondsBefore / secondsAfter : 0.0, passes() ? "ok" : "<<< CHECK");
        if (worstDiffDb > -999.0)
            std::printf ("%-44s      (largest difference: %s; gain: %s; meters: %s)\n", "", worstRun.toRawUTF8(), worstGainRun.toRawUTF8(), worstMeterRun.toRawUTF8());
    }
};

//==============================================================================
static const juce::File materialDir ("/home/claude/presetcheck/material");

static juce::AudioBuffer<float> loadWav (const juce::String& name, double maxSeconds = 1.0e9)
{
    static std::map<juce::String, juce::AudioBuffer<float>> cache;
    const auto key = name + "@" + juce::String (maxSeconds);

    if (auto found = cache.find (key); found != cache.end())
        return found->second;

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (materialDir.getChildFile (name + ".wav")));

    if (reader == nullptr)
    {
        std::printf ("missing material %s\n", name.toRawUTF8());
        std::exit (2);
    }

    const int length = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (maxSeconds * reader->sampleRate));
    juce::AudioBuffer<float> buffer ((int) reader->numChannels, length);
    reader->read (&buffer, 0, length, 0, true, true);
    cache[key] = buffer;
    return buffer;
}

struct Gapped
{
    juce::AudioBuffer<float> audio;
    std::vector<std::pair<long long, long long>> gaps;   // (start, length) in samples
};

// The material cut into pieces with digital silence in between: abrupt cuts (or short
// fades), gaps from 1 sample up to 5 seconds.
static Gapped makeGapped (const juce::AudioBuffer<float>& source, bool fades)
{
    const std::vector<std::pair<int, int>> plan = {   // (audio samples, silence samples) at 48 kHz
        { 72000, 1 }, { 24000, 7 }, { 24000, 300 }, { 24000, 256 }, { 24000, 2400 }, { 48000, 9600 },
        { 48000, 24000 }, { 48000, 96000 }, { 48000, 240000 }, { 96000, 0 } };

    long long total = 0;
    for (auto& [audio, silence] : plan) total += audio + silence;

    Gapped g;
    g.audio.setSize (source.getNumChannels(), (int) total);
    g.audio.clear();

    long long out = 0, in = 0;
    const int fadeLength = 480;

    for (auto& [audio, silence] : plan)
    {
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            for (int i = 0; i < audio; ++i)
            {
                float gain = 1.0f;
                if (fades && i >= audio - fadeLength) gain = (float) (audio - i) / (float) fadeLength;
                g.audio.setSample (ch, (int) (out + i), source.getSample (ch, (int) ((in + i) % source.getNumSamples())) * gain);
            }

        in += audio;
        out += audio;

        if (silence > 0)
            g.gaps.emplace_back (out, silence);

        out += silence;
    }

    return g;
}

// Awkward signals: DC that stops dead, a square wave, very quiet noise, denormal-sized
// values, a hot sine, each followed by silence.
static juce::AudioBuffer<float> makeEdgeMaterial (int channels)
{
    const double sr = 48000.0;
    std::vector<float> mono;
    auto add = [&] (double seconds, std::function<float (int)> f) { const int n = (int) (seconds * sr); for (int i = 0; i < n; ++i) mono.push_back (f (i)); };
    juce::Random rng (3);

    add (1.0, [] (int) { return 0.5f; });
    add (1.0, [] (int) { return 0.0f; });
    add (1.0, [] (int i) { return (i / 240) % 2 == 0 ? 0.9f : -0.9f; });
    add (2.0, [] (int) { return 0.0f; });
    add (2.0, [&] (int) { return 1.0e-7f * (rng.nextFloat() * 2.0f - 1.0f); });
    add (1.0, [] (int) { return 0.0f; });
    add (1.0, [] (int i) { return (float) (0.7 * std::sin (i * 0.05) * std::exp (-(i % 12000) / 2000.0)); });
    add (0.5, [&] (int) { return 1.0e-40f * (rng.nextFloat() * 2.0f - 1.0f); });
    add (1.0, [] (int) { return 0.0f; });
    add (1.0, [] (int i) { return (float) (1.995 * std::sin (i * 0.03)); });
    add (3.0, [] (int) { return 0.0f; });
    add (1.0, [] (int i) { return -0.0f * (float) i; });
    add (1.0, [] (int i) { return (float) (0.3 * std::sin (i * 0.11)); });

    juce::AudioBuffer<float> b (channels, (int) mono.size());
    for (int ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < mono.size(); ++i)
            b.setSample (ch, (int) i, mono[i] * (ch == 0 ? 1.0f : 0.8f));
    return b;
}

static juce::AudioBuffer<float> makeLongSilence (const juce::AudioBuffer<float>& source)
{
    const std::vector<std::pair<int, int>> plan = { { 144000, 2880000 }, { 144000, 960000 }, { 96000, 0 } };   // 3 s, 60 s silence, 3 s, 20 s, 2 s
    long long total = 0;
    for (auto& [a, s] : plan) total += a + s;
    juce::AudioBuffer<float> b (source.getNumChannels(), (int) total);
    b.clear();
    long long out = 0, in = 0;
    for (auto& [audio, silence] : plan)
    {
        for (int ch = 0; ch < source.getNumChannels(); ++ch)
            for (int i = 0; i < audio; ++i)
                b.setSample (ch, (int) (out + i), source.getSample (ch, (int) ((in + i) % source.getNumSamples())));
        in += audio;
        out += audio + silence;
    }
    return b;
}

static juce::AudioBuffer<float> monoOf (const juce::AudioBuffer<float>& b)
{
    juce::AudioBuffer<float> m (1, b.getNumSamples());
    m.copyFrom (0, 0, b, 0, 0, b.getNumSamples());
    return m;
}

//==============================================================================
struct CompPreset  { juce::String category, name; CompParams params; juce::StringArray materials; };
struct TransPreset { juce::String category, name; TransParams params; juce::StringArray materials; };

static juce::StringArray splitRow (const juce::String& line)
{
    juce::StringArray fields;
    fields.addTokens (line, "|", "");
    for (auto& f : fields) f = f.trim();
    return fields;
}

static juce::StringArray materialList (const juce::String& field)
{
    juce::StringArray m;
    m.addTokens (field, ",", "");
    for (auto& s : m) s = s.trim();
    m.removeEmptyStrings();
    return m;
}

static std::vector<CompPreset> loadCompPresets()
{
    std::vector<CompPreset> presets;
    juce::StringArray lines;
    juce::File ("/home/claude/presetcheck/presets_comp.tsv").readLines (lines);

    for (auto& line : lines)
    {
        if (line.trim().isEmpty() || line.trimStart().startsWith ("#")) continue;
        const auto f = splitRow (line);
        if (f.size() < 15) continue;
        // (Only the presets 1.5.1 could play: MODERN, NORMAL, no sidechain.)
        if (f[10] != "modern" || f[11] != "normal" || f[13].containsChar ('<')) continue;
        CompPreset p;
        p.category = f[0]; p.name = f[1];
        p.params.threshold = f[2].getFloatValue(); p.params.ratio = f[3].getFloatValue();
        p.params.attack = f[4].getFloatValue();    p.params.release = f[5].getFloatValue();
        p.params.makeup = f[6].getFloatValue();    p.params.mix = f[7].getFloatValue() / 100.0f;
        p.params.lowcut = f[8].getFloatValue();    p.params.average = f[9].equalsIgnoreCase ("average");
        p.materials = materialList (f[13]);
        presets.push_back (p);
    }
    return presets;
}

static std::vector<TransPreset> loadTransPresets()
{
    std::vector<TransPreset> presets;
    juce::StringArray lines;
    juce::File ("/home/claude/presetcheck/presets_transient.tsv").readLines (lines);

    for (auto& line : lines)
    {
        if (line.trim().isEmpty() || line.trimStart().startsWith ("#")) continue;
        const auto f = splitRow (line);
        if (f.size() < 7) continue;
        TransPreset p;
        p.category = f[0]; p.name = f[1];
        p.params.punch = f[2].getFloatValue() / 100.0f; p.params.sustain = f[3].getFloatValue() / 100.0f;
        p.params.output = f[4].getFloatValue();         p.params.mix = f[5].getFloatValue() / 100.0f;
        p.materials = materialList (f[6]);
        presets.push_back (p);
    }
    return presets;
}

//==============================================================================
using CompB  = before::Compressor;
using CompA  = after::Compressor;
using TransB = before::TransientShaper;
using TransA = after::TransientShaper;

// The current version with skipping through silence turned off.
struct CompNoRest  : after::Compressor      { CompNoRest()  { allowResting (false); } };
struct TransNoRest : after::TransientShaper { TransNoRest() { allowResting (false); } };

template <class P>
static Script<P> atTimes (std::vector<std::pair<long long, std::function<void (P&, bool&)>>> events)
{
    auto done = std::make_shared<std::vector<bool>> (events.size(), false);
    return [events, done] (long long start, int length, P& p, bool& restart)
    {
        for (size_t k = 0; k < events.size(); ++k)
            if (! (*done)[k] && events[k].first < start + length)
            {
                events[k].second (p, restart);
                (*done)[k] = true;
            }
    };
}

template <class CompB, class CompA, class TransB, class TransA>
static int compareAll (bool quick, const juce::String& filter)
{
    auto want = [&] (const char* name) { return filter.isEmpty() || juce::String (name).containsIgnoreCase (filter); };
    std::vector<Group> groups;
    const double sr = 48000.0;
    const double presetSeconds = quick ? 4.0 : 10.0;

    // ---- Easy Comp -----------------------------------------------------------
    const auto compPresets = loadCompPresets();
    if (want ("Comp: every preset on its material"))
    {
        Group g { "Comp: every preset on its material" };
        for (auto& p : compPresets)
            for (auto& m : p.materials)
                for (bool autoGain : { false, true })
                {
                    auto params = p.params; params.autoGain = autoGain;
                    g.add (runPair<CompB, CompA> (loadWav (m, presetSeconds), sr, 256, params), p.name + " / " + m + (autoGain ? " / auto" : ""));
                }
        groups.push_back (g);
    }
    if (want ("Comp: every preset, with gaps of silence"))
    {
        Group g { "Comp: every preset, with gaps of silence" };
        for (auto& p : compPresets)
            for (bool autoGain : { false, true })
            {
                auto params = p.params; params.autoGain = autoGain;
                const auto gapped = makeGapped (loadWav (p.materials[0], 20.0), autoGain);
                g.add (runPair<CompB, CompA> (gapped.audio, sr, 256, params), p.name + (autoGain ? " / auto, fades" : " / cuts"));
            }
        groups.push_back (g);
    }
    if (want ("Comp: knob moves and presets during silence"))
    {
        Group g { "Comp: knob moves and presets during silence" };
        const auto drums = makeGapped (loadWav ("drumbus_rock", 20.0), false);
        const auto vocal = makeGapped (loadWav ("vocal_pop", 20.0), true);
        CompParams base; base.threshold = -30; base.ratio = 4; base.attack = 5; base.release = 400; base.lowcut = 150; base.autoGain = true;
        CompParams avg = base; avg.average = true; avg.release = 1000; avg.mix = 0.6f;

        for (auto* material : { &drums, &vocal })
            for (auto params : { base, avg })
            {
                const auto& gaps = material->gaps;
                const long long g5 = gaps[8].first, g2 = gaps[7].first, g05 = gaps[6].first, g02 = gaps[5].first;
                using E = std::pair<long long, std::function<void (CompParams&, bool&)>>;

                g.add (runPair<CompB, CompA> (material->audio, sr, 256, params, atTimes<CompParams> ({
                    E { g5 + 48000,  [] (CompParams& p, bool&) { p.threshold += 10.0f; } },
                    E { g5 + 144000, [] (CompParams& p, bool&) { p.threshold -= 10.0f; p.ratio = 8.0f; } } })), "threshold/ratio in gap");

                g.add (runPair<CompB, CompA> (material->audio, sr, 256, params, atTimes<CompParams> ({
                    E { g2 + 24000,  [] (CompParams&, bool& r) { r = true; } },
                    E { g5 + 235000, [] (CompParams&, bool& r) { r = true; } } })), "preset load in gap");

                g.add (runPair<CompB, CompA> (material->audio, sr, 256, params, atTimes<CompParams> ({
                    E { g5 + 48000, [] (CompParams& p, bool&) { p.bypass = true; } },
                    E { g5 + 96000, [] (CompParams& p, bool&) { p.bypass = false; } },
                    E { g05 + 100,  [] (CompParams& p, bool&) { p.bypass = true; } },
                    E { g05 + 23990, [] (CompParams& p, bool&) { p.bypass = false; } } })), "bypass in gap");

                g.add (runPair<CompB, CompA> (material->audio, sr, 256, params, atTimes<CompParams> ({
                    E { g02 + 10,    [] (CompParams& p, bool&) { p.autoGain = ! p.autoGain; } },
                    E { g5 + 1000,   [] (CompParams& p, bool&) { p.autoGain = ! p.autoGain; } },
                    E { g5 + 50000,  [] (CompParams& p, bool&) { p.release = 30.0f; p.mix = 0.3f; p.makeup = 6.0f; } },
                    E { g5 + 239000, [] (CompParams& p, bool&) { p.lowcut = 20.0f; p.average = ! p.average; } } })), "auto-gain/release/mix in gap");

                auto rng = std::make_shared<juce::Random> (5);
                g.add (runPair<CompB, CompA> (material->audio, sr, 128, params, [rng] (long long, int, CompParams& p, bool& restart)
                {
                    if (rng->nextInt (20) == 0)
                    {
                        p.threshold = -40.0f + 35.0f * rng->nextFloat(); p.ratio = 1.0f + 10.0f * rng->nextFloat();
                        p.makeup = 12.0f * rng->nextFloat(); p.mix = rng->nextFloat(); p.release = 10.0f + 500.0f * rng->nextFloat();
                        p.lowcut = rng->nextBool() ? 20.0f : 40.0f + 300.0f * rng->nextFloat();
                    }
                    if (rng->nextInt (400) == 0) p.average = ! p.average;
                    if (rng->nextInt (500) == 0) p.bypass = ! p.bypass;
                    if (rng->nextInt (500) == 0) p.autoGain = ! p.autoGain;
                    if (rng->nextInt (800) == 0) restart = true;
                }), "random automation");
            }
        groups.push_back (g);
    }
    if (want ("Comp: block sizes, rates, mono"))
    {
        Group g { "Comp: block sizes, rates, mono" };
        const auto drums = makeGapped (loadWav ("drumbus_rock", 20.0), false);
        CompParams base; base.threshold = -28; base.ratio = 5; base.attack = 3; base.release = 150; base.lowcut = 90; base.autoGain = true;
        CompParams avg = base; avg.average = true;
        for (int block : { 1, 17, 64, 333, 1024, 4096 })
            for (auto params : { base, avg })
                g.add (runPair<CompB, CompA> (drums.audio, sr, block, params), "block " + juce::String (block));
        for (double rate : { 44100.0, 96000.0, 192000.0 })
            g.add (runPair<CompB, CompA> (drums.audio, rate, 512, base), "rate " + juce::String (rate));
        g.add (runPair<CompB, CompA> (monoOf (drums.audio), sr, 480, avg), "mono");
        groups.push_back (g);
    }
    if (want ("Comp: awkward signals and long silences"))
    {
        Group g { "Comp: awkward signals and long silences" };
        CompParams base; base.threshold = -30; base.ratio = 6; base.attack = 1; base.release = 1000; base.lowcut = 300; base.autoGain = true;
        CompParams avg = base; avg.average = true; avg.lowcut = 20;
        CompParams plain;
        for (auto params : { base, avg, plain })
        {
            g.add (runPair<CompB, CompA> (makeEdgeMaterial (2), sr, 256, params), "edge material");
            g.add (runPair<CompB, CompA> (makeLongSilence (loadWav ("drumbus_breaks", 20.0)), sr, 512, params), "long silence");
        }
        groups.push_back (g);
    }

    // ---- Easy Transient --------------------------------------------------------
    const auto transPresets = loadTransPresets();
    if (want ("Transient: every preset on its material"))
    {
        Group g { "Transient: every preset on its material" };
        for (auto& p : transPresets)
            for (auto& m : p.materials)
                for (bool autoGain : { false, true })
                {
                    auto params = p.params; params.autoGain = autoGain;
                    g.add (runPair<TransB, TransA> (loadWav (m, presetSeconds), sr, 256, params), p.name + " / " + m + (autoGain ? " / auto" : ""));
                }
        groups.push_back (g);
    }
    if (want ("Transient: every preset, with gaps of silence"))
    {
        Group g { "Transient: every preset, with gaps of silence" };
        for (auto& p : transPresets)
            for (bool autoGain : { false, true })
            {
                auto params = p.params; params.autoGain = autoGain;
                const auto gapped = makeGapped (loadWav (p.materials[0], 20.0), autoGain);
                g.add (runPair<TransB, TransA> (gapped.audio, sr, 256, params), p.name + (autoGain ? " / auto, fades" : " / cuts"));
            }
        groups.push_back (g);
    }
    if (want ("Transient: knob moves and presets during silence"))
    {
        Group g { "Transient: knob moves and presets during silence" };
        const auto drums = makeGapped (loadWav ("drumbus_rock", 20.0), false);
        const auto piano = makeGapped (loadWav ("piano_pop", 20.0), true);
        TransParams base; base.punch = 0.6f; base.sustain = 0.5f; base.autoGain = true;
        TransParams cut = base; cut.punch = -0.8f; cut.sustain = -0.7f; cut.mix = 0.7f; cut.output = 3.0f;

        for (auto* material : { &drums, &piano })
            for (auto params : { base, cut })
            {
                const auto& gaps = material->gaps;
                const long long g5 = gaps[8].first, g2 = gaps[7].first, g05 = gaps[6].first;
                using E = std::pair<long long, std::function<void (TransParams&, bool&)>>;

                g.add (runPair<TransB, TransA> (material->audio, sr, 256, params, atTimes<TransParams> ({
                    E { g5 + 48000,  [] (TransParams& p, bool&) { p.punch = -p.punch; } },
                    E { g5 + 200000, [] (TransParams& p, bool&) { p.sustain = 1.0f; p.output = -6.0f; } } })), "punch/sustain in gap");

                g.add (runPair<TransB, TransA> (material->audio, sr, 256, params, atTimes<TransParams> ({
                    E { g2 + 24000,  [] (TransParams&, bool& r) { r = true; } },
                    E { g5 + 235000, [] (TransParams&, bool& r) { r = true; } } })), "preset load in gap");

                g.add (runPair<TransB, TransA> (material->audio, sr, 256, params, atTimes<TransParams> ({
                    E { g5 + 48000, [] (TransParams& p, bool&) { p.bypass = true; } },
                    E { g5 + 96000, [] (TransParams& p, bool&) { p.bypass = false; } },
                    E { g05 + 100,  [] (TransParams& p, bool&) { p.bypass = true; } },
                    E { g05 + 23990, [] (TransParams& p, bool&) { p.bypass = false; } },
                    E { g5 + 120000, [] (TransParams& p, bool&) { p.autoGain = ! p.autoGain; } } })), "bypass/auto-gain in gap");

                auto rng = std::make_shared<juce::Random> (8);
                g.add (runPair<TransB, TransA> (material->audio, sr, 128, params, [rng] (long long, int, TransParams& p, bool& restart)
                {
                    if (rng->nextInt (20) == 0)
                    {
                        p.punch = rng->nextFloat() * 2.0f - 1.0f; p.sustain = rng->nextFloat() * 2.0f - 1.0f;
                        p.output = 24.0f * rng->nextFloat() - 12.0f; p.mix = rng->nextFloat();
                    }
                    if (rng->nextInt (500) == 0) p.bypass = ! p.bypass;
                    if (rng->nextInt (500) == 0) p.autoGain = ! p.autoGain;
                    if (rng->nextInt (800) == 0) restart = true;
                }), "random automation");
            }
        groups.push_back (g);
    }
    if (want ("Transient: block sizes, rates, mono"))
    {
        Group g { "Transient: block sizes, rates, mono" };
        const auto drums = makeGapped (loadWav ("drumbus_rock", 20.0), false);
        TransParams base; base.punch = 0.7f; base.sustain = -0.4f; base.autoGain = true;
        for (int block : { 1, 17, 64, 333, 1024, 4096 })
            g.add (runPair<TransB, TransA> (drums.audio, sr, block, base), "block " + juce::String (block));
        for (double rate : { 44100.0, 96000.0, 192000.0 })
            g.add (runPair<TransB, TransA> (drums.audio, rate, 512, base), "rate " + juce::String (rate));
        g.add (runPair<TransB, TransA> (monoOf (drums.audio), sr, 480, base), "mono");
        groups.push_back (g);
    }
    if (want ("Transient: awkward signals and long silences"))
    {
        Group g { "Transient: awkward signals and long silences" };
        TransParams base; base.punch = 1.0f; base.sustain = 1.0f; base.autoGain = true;
        TransParams neg; neg.punch = -1.0f; neg.sustain = -1.0f;
        TransParams zero;
        for (auto params : { base, neg, zero })
        {
            g.add (runPair<TransB, TransA> (makeEdgeMaterial (2), sr, 256, params), "edge material");
            g.add (runPair<TransB, TransA> (makeLongSilence (loadWav ("drumbus_breaks", 20.0)), sr, 512, params), "long silence");
        }
        groups.push_back (g);
    }

    int failures = 0;
    std::printf ("\n");
    for (auto& g : groups)
    {
        g.print();
        if (! g.passes()) ++failures;
    }
    std::printf ("\n%s\n", failures == 0 ? "ALL WITHIN TOLERANCE" : "SOME OUTSIDE TOLERANCE");
    return failures == 0 ? 0 : 1;
}

//==============================================================================
// Speed: each effect alone on the same 60 s of material, best of several runs.
template <class E, class P>
static double nsPerSample (const juce::AudioBuffer<float>& material, P params, int block = 256)
{
    juce::ScopedNoDenormals noDenormals;
    auto e = std::make_unique<E>();
    e->prepare (48000.0, makeSettings<typename E::Settings> (params));
    juce::AudioBuffer<float> work (material);
    const int n = work.getNumSamples();
    const auto t0 = Clock::now();
    for (int start = 0; start < n; start += block)
    {
        const int length = std::min (block, n - start);
        e->setSettings (makeSettings<typename E::Settings> (params));
        juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), start, length);
        const auto r = e->process (view);
        juce::ignoreUnused (r);
    }
    return 1.0e9 * secondsOf (Clock::now() - t0) / (double) n;
}

static juce::AudioBuffer<float> looped (const juce::AudioBuffer<float>& src, double seconds)
{
    juce::AudioBuffer<float> b (src.getNumChannels(), (int) (seconds * 48000.0));
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, src.getSample (ch, i % src.getNumSamples()));
    return b;
}

static void benchAll (int repeats)
{
    const auto drums = looped (loadWav ("drumbus_rock"), 60.0);
    const auto vocal = looped (loadWav ("vocal_pop"), 60.0);
    juce::AudioBuffer<float> silence (2, 48000 * 60);
    silence.clear();

    const auto comp = loadCompPresets();
    const auto trans = loadTransPresets();
    auto findComp  = [&] (const char* name) { for (auto& p : comp)  if (p.name == name) return p.params; return CompParams(); };
    auto findTrans = [&] (const char* name) { for (auto& p : trans) if (p.name == name) return p.params; return TransParams(); };

    struct Case { const char* label; std::function<double (bool after)> run; };
    std::vector<Case> cases;

    for (bool autoGain : { false, true })
    {
        auto glue = findComp ("Drum Bus - Glue");     glue.autoGain = autoGain;
        auto vox  = findComp ("Vocal - Pop & R&B");   vox.autoGain = autoGain;
        auto punchier = findTrans ("Drum Bus - Punchier"); punchier.autoGain = autoGain;
        cases.push_back ({ autoGain ? "Comp  Drum Bus - Glue, AUTO-GAIN on " : "Comp  Drum Bus - Glue, AUTO-GAIN off",
                           [=, &drums] (bool a) { return a ? nsPerSample<CompA> (drums, glue) : nsPerSample<CompB> (drums, glue); } });
        cases.push_back ({ autoGain ? "Comp  Vocal - Pop & R&B, AUTO-GAIN on " : "Comp  Vocal - Pop & R&B, AUTO-GAIN off",
                           [=, &vocal] (bool a) { return a ? nsPerSample<CompA> (vocal, vox) : nsPerSample<CompB> (vocal, vox); } });
        cases.push_back ({ autoGain ? "Trans Drum Bus - Punchier, AUTO-GAIN on " : "Trans Drum Bus - Punchier, AUTO-GAIN off",
                           [=, &drums] (bool a) { return a ? nsPerSample<TransA> (drums, punchier) : nsPerSample<TransB> (drums, punchier); } });
    }
    {
        auto glue = findComp ("Drum Bus - Glue"); glue.autoGain = true;
        auto punchier = findTrans ("Drum Bus - Punchier"); punchier.autoGain = true;
        cases.push_back ({ "Comp  silence", [=, &silence] (bool a) { return a ? nsPerSample<CompA> (silence, glue) : nsPerSample<CompB> (silence, glue); } });
        cases.push_back ({ "Trans silence", [=, &silence] (bool a) { return a ? nsPerSample<TransA> (silence, punchier) : nsPerSample<TransB> (silence, punchier); } });
    }

    std::printf ("%-44s %12s %12s %8s   (ns per stereo sample; %% of one core at 48 kHz)\n", "", "before", "after", "faster");
    for (auto& c : cases)
    {
        double bestB = 1e30, bestA = 1e30;
        for (int r = 0; r < repeats; ++r)
        {
            bestB = std::min (bestB, c.run (false));
            bestA = std::min (bestA, c.run (true));
        }
        std::printf ("%-44s %6.1f %4.2f%% %6.1f %4.2f%%  x%5.2f\n", c.label, bestB, bestB * 48000.0 * 1e-7, bestA, bestA * 48000.0 * 1e-7, bestB / bestA);
    }
}


//==============================================================================
int main (int argc, char** argv)
{
    const juce::String mode = argc > 1 ? argv[1] : "compare";

    const bool quick = argc > 2 && juce::String (argv[2]) == "quick";
    const auto filter = argc > 3 ? juce::String (argv[3]) : juce::String();

    if (mode == "compare")         // the original against now
        return compareAll<CompB, CompA, TransB, TransA> (quick, filter);

    if (mode == "compare-rest")    // now, without and with skipping silence
        return compareAll<CompNoRest, CompA, TransNoRest, TransA> (quick, filter);

    if (mode == "compare-rest-vintage" || mode == "compare-rest-thrust" || mode == "compare-rest-both")
    {
        forceVintage = mode != "compare-rest-thrust";
        forceThrust  = mode != "compare-rest-vintage";
        return compareAll<CompNoRest, CompA, TransNoRest, TransA> (quick, filter.isEmpty() ? juce::String ("Comp") : filter);
    }

    if (mode == "thrust-response")
    {
        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            const auto shape = after::Compressor::designThrust (rate);
            std::printf ("%6.0f:", rate);
            for (double hz : { 20.0, 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 10000.0, 16000.0, 20000.0 })
                if (hz < rate * 0.5)
                    std::printf ("  %g:%+.1f", hz, after::Compressor::thrustResponseDb (shape, hz, rate));
            std::printf ("\n");
        }
        return 0;
    }

    if (mode == "bench-modes")
    {
        const auto drums = looped (loadWav ("drumbus_rock"), 60.0);
        const auto vocal = looped (loadWav ("vocal_pop"), 60.0);
        const auto comp = loadCompPresets();
        auto findComp = [&] (const char* name) { for (auto& p : comp) if (p.name == name) return p.params; return CompParams(); };
        const int repeats = argc > 2 ? juce::String (argv[2]).getIntValue() : 5;

        for (auto [label, material, preset] : { std::tuple { "drum bus", &drums, "Drum Bus - Glue" },
                                                std::tuple { "vocal",    &vocal, "Vocal - Pop & R&B" } })
            for (bool autoGain : { false, true })
            {
                auto params = findComp (preset);
                params.autoGain = autoGain;
                std::printf ("%-9s AUTO-GAIN %-3s", label, autoGain ? "on" : "off");

                for (int m = 0; m < 4; ++m)
                {
                    forceVintage = (m & 1) != 0;
                    forceThrust  = (m & 2) != 0;
                    double best = 1e30;
                    for (int r = 0; r < repeats; ++r)
                        best = std::min (best, nsPerSample<CompA> (*material, params));
                    std::printf ("   %s %5.1f ns (%.3f%%)", m == 0 ? "MODERN" : m == 1 ? "VINTAGE" : m == 2 ? "THRUST" : "V+T", best, best * 48000.0 * 1e-7);
                }
                std::printf ("\n");
            }

        forceVintage = forceThrust = false;
        return 0;
    }

    if (mode == "bench")
    {
        benchAll (argc > 2 ? juce::String (argv[2]).getIntValue() : 5);
        return 0;
    }

    if (mode == "profile" && argc >= 5)
    {
        const juce::String which = argv[2], version = argv[3];
        const double seconds = juce::String (argv[4]).getDoubleValue();
        const auto material = looped (loadWav ("drumbus_rock"), seconds);
        CompParams cp; cp.threshold = -26; cp.ratio = 3; cp.attack = 20; cp.release = 150; cp.makeup = 2; cp.lowcut = 20; cp.autoGain = true;
        TransParams tp; tp.punch = 0.4f; tp.sustain = -0.2f; tp.autoGain = true;
        double ns = 0.0;
        if (which == "comp")  ns = version == "after" ? nsPerSample<CompA> (material, cp)  : nsPerSample<CompB> (material, cp);
        else                  ns = version == "after" ? nsPerSample<TransA> (material, tp) : nsPerSample<TransB> (material, tp);
        std::printf ("%.1f ns per sample\n", ns);
        return 0;
    }

    std::printf ("usage: Harness compare [quick] | bench [repeats] | profile comp|trans before|after seconds\n");
    return 1;
}

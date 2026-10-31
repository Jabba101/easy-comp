// Easy Strip: the modules inside it, the plumbing, INPUT / OUTPUT, LUFS and the window (not shipped).
#include "TestHelpers.h"
#include "../EasyPlugins/Plugins/EasyStrip/PluginProcessor.h"
#include "../EasyPlugins/Plugins/EasyStrip/PluginEditor.h"
#include "../EasyPlugins/Plugins/EasyStrip/HoloSlabs.h"
#include "../EasyPlugins/Plugins/EasyStrip/StripPresets.h"
#include "../EasyPlugins/Plugins/EasyComp/PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace th;
using S = EasyStripAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    void prepareStereo (juce::AudioProcessor& p, bool withSidechain)
    {
        auto layout = p.getBusesLayout();
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (layout.inputBuses.size() > 1)
            layout.inputBuses.getReference (1) = withSidechain ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::disabled();
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }

    // Runs a processor on a 2-channel signal (with a sidechain behind it - silent unless given - if it has one switched on)
    juce::AudioBuffer<float> process (juce::AudioProcessor& p, const juce::AudioBuffer<float>& input, std::function<void (int)> before = {},
                                      const juce::AudioBuffer<float>* sidechain = nullptr)
    {
        const int total = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> work (juce::jmax (2, total), input.getNumSamples());
        work.clear();
        for (int ch = 0; ch < 2; ++ch) work.copyFrom (ch, 0, input, ch, 0, input.getNumSamples());

        juce::MidiBuffer midi;
        for (int start = 0; start < work.getNumSamples(); start += block)
        {
            if (before) before (start);
            const int n = std::min (block, work.getNumSamples() - start);
            for (int ch = 2; ch < work.getNumChannels(); ++ch)
            {
                if (sidechain != nullptr) work.copyFrom (ch, start, *sidechain, ch - 2, start, n);
                else                      work.clear (ch, start, n);
            }
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), work.getNumChannels(), start, n);
            p.processBlock (view, midi);
        }

        juce::AudioBuffer<float> out (2, input.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, 0, work, ch, 0, work.getNumSamples());
        return out;
    }

    // How far a is from b delayed by `delay` samples
    float differenceDelayed (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int delay, float gain = 1.0f)
    {
        float d = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const float expected = i >= delay ? b.getSample (ch, i - delay) * gain : 0.0f;
                d = std::max (d, std::abs (a.getSample (ch, i) - expected));
            }
        return d;
    }

    void setStrip (S& s, const juce::String& id, float value)
    {
        auto* p = s.parameters.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void switchOn (S& s, int m, bool on)   { setStrip (s, StripIDs::moduleID (m, CommonIDs::bypass), on ? 0.0f : 1.0f); }

    // The delays of the modules before m and after it, in the order the sound goes
    std::pair<int, int> latencyAround (S& s, int m, const S::Order& o = S::orderFor (false, false, false, false))
    {
        int before = 0, after = 0;
        bool seen = false;
        for (auto k : o)
        {
            if (k == m) { seen = true; continue; }
            (seen ? after : before) += s.getModule (k).getLatencySamples();
        }
        return { before, after };
    }

    std::vector<std::unique_ptr<EasyProcessor>> standalones()
    {
        std::vector<std::unique_ptr<EasyProcessor>> v;
        v.push_back (std::make_unique<EasyGateAudioProcessor>());
        v.push_back (std::make_unique<EasyAmpAudioProcessor>());
        v.push_back (std::make_unique<EasyEQAudioProcessor>());
        v.push_back (std::make_unique<EasyDSAudioProcessor>());
        v.push_back (std::make_unique<EasyCompAudioProcessor>());
        v.push_back (std::make_unique<EasyTransientAudioProcessor>());
        v.push_back (std::make_unique<BitchSlapAudioProcessor>());
        v.push_back (std::make_unique<EasyClipAudioProcessor>());
        v.push_back (std::make_unique<EasyTapeAudioProcessor>());
        v.push_back (std::make_unique<EasyDelayAudioProcessor>());
        v.push_back (std::make_unique<EasyTremoloAudioProcessor>());
        v.push_back (std::make_unique<EasyWahAudioProcessor>());
        v.push_back (std::make_unique<EasyFlangeAudioProcessor>());
        return v;
    }

    int moduleLatencySum (S& s)
    {
        int t = 0;
        for (int m = 0; m < S::numModules; ++m) t += s.getModule (m).getLatencySamples();
        return t;
    }

    juce::AudioBuffer<float> music (double seconds)
    {
        auto b = drumLoop (sr, seconds, -8.0f, -10.0f, -22.0f);
        auto tone = sine (sr, 220.0, 0.15f, seconds);
        auto tone2 = sine (sr, 3150.0, 0.05f, seconds);
        for (int ch = 0; ch < 2; ++ch) { b.addFrom (ch, 0, tone, ch, 0, b.getNumSamples()); b.addFrom (ch, 0, tone2, ch, 0, b.getNumSamples()); }
        // a little "s" now and then, for the de-esser
        juce::Random rng (11);
        for (int i = 0; i < b.getNumSamples(); ++i)
            if ((i / 6000) % 4 == 3)
                for (int ch = 0; ch < 2; ++ch) b.addSample (ch, i, 0.08f * (rng.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    //==========================================================================
    void testAllOff()
    {
        std::cout << "\n[every module off]\n";
        S s; prepareStereo (s, true);
        const auto in = music (2.0);
        const auto out = process (s, in);
        const int latency = s.getLatencySamples();

        check (latency == moduleLatencySum (s), "the strip's delay is all the modules' delays added up (" + juce::String (latency) + " samples)");
        check (differenceDelayed (out, in, latency) < 1.0e-6f, "with everything off, the sound comes out untouched (just on time)");

        bool allSkipped = true;
        for (int m = 0; m < S::numModules; ++m) allSkipped = allSkipped && s.isModuleSkipped (m) && ! s.isModuleOn (m);
        check (allSkipped, "switched-off modules aren't run at all");
    }

    //==========================================================================
    template <typename Standalone>
    void compareModule (int m, Standalone& alone, int presetIndex)
    {
        S s;
        const auto& presets = alone.getBuiltInPresets();
        const int index = juce::jmin (presetIndex, (int) presets.size() - 1);
        alone.loadPreset (presets[(size_t) index]);
        s.getModule (m).loadPreset (s.getModule (m).getBuiltInPresets()[(size_t) index]);   // (as if chosen in its window)
        switchOn (s, m, true);
        prepareStereo (s, true);
        prepareStereo (alone, true);

        // (the modules before it delay what it hears; the ones after delay what it makes)
        const auto [before, after] = latencyAround (s, m);

        const auto in = music (3.0);
        juce::AudioBuffer<float> heard (2, in.getNumSamples());
        heard.clear();
        for (int ch = 0; ch < 2; ++ch) heard.copyFrom (ch, before, in, ch, 0, in.getNumSamples() - before);

        const auto expected = process (alone, heard);
        const auto out = process (s, in);
        const float diff = differenceDelayed (out, expected, after);
        check (diff < 1.0e-5f && rms (expected, 0, 0, expected.getNumSamples()) > 1.0e-3f,
               juce::String (StripModules::names[m]) + " in the strip sounds identical to Easy " + StripModules::names[m]
                   + " (preset \"" + presets[(size_t) index].name + "\", difference " + juce::String (toDb (diff), 1) + " dB)");
    }

    void testModulesMatchStandalones()
    {
        std::cout << "\n[each module = its standalone plugin]\n";
        { EasyGateAudioProcessor a;      compareModule (0, a, 3); }
        { EasyAmpAudioProcessor a;       compareModule (1, a, 4); }
        { EasyEQAudioProcessor a;        compareModule (2, a, 5); }
        { EasyDSAudioProcessor a;        compareModule (3, a, 2); }
        { EasyCompAudioProcessor a;      compareModule (4, a, 6); }
        { EasyTransientAudioProcessor a; compareModule (5, a, 3); }
        { BitchSlapAudioProcessor a;     compareModule (6, a, 2); }
        { BitchSlapAudioProcessor a;     compareModule (6, a, 29); }   // (a live space: "Snare - Club Gig")
        { EasyClipAudioProcessor a;      compareModule (7, a, 5); }
        { EasyTapeAudioProcessor a;      compareModule (8, a, 4); }
        { EasyDelayAudioProcessor a;     compareModule (9, a, 5); }
        { EasyTremoloAudioProcessor a;   compareModule (10, a, 1); }
        { EasyWahAudioProcessor a;       compareModule (11, a, 1); }
        { EasyFlangeAudioProcessor a;    compareModule (12, a, 2); }
    }

    void testSidechainAndChain()
    {
        std::cout << "\n[sidechain, and everything on at once]\n";
        const auto in = music (3.0);
        const auto key = drumLoop (sr, 3.0, -3.0f, -60.0f, -60.0f);   // (just a kick)

        {   // COMP listening to the sidechain
            S s;
            EasyCompAudioProcessor alone;
            for (auto* target : { (EasyProcessor*) &alone, &s.getModule (4) })
            {
                target->loadPreset (target->getBuiltInPresets()[2]);
                setParam (*target, CompIDs::listensTo, 1.0f);
                setParam (*target, CompIDs::threshold, -30.0f);
            }
            switchOn (s, 4, true);
            prepareStereo (s, true); prepareStereo (alone, true);

            const auto [before, after] = latencyAround (s, 4);
            juce::AudioBuffer<float> heard (2, in.getNumSamples()), keyHeard (2, in.getNumSamples());
            heard.clear(); keyHeard.clear();
            for (int ch = 0; ch < 2; ++ch) { heard.copyFrom (ch, before, in, ch, 0, in.getNumSamples() - before);
                                             keyHeard.copyFrom (ch, 0, key, ch, 0, in.getNumSamples()); }

            const auto expected = process (alone, heard, {}, &keyHeard);
            const auto out = process (s, in, {}, &key);
            const auto unkeyed = process (alone, heard);
            check (differenceDelayed (out, expected, after) < 1.0e-5f && maxDifference (expected, unkeyed) > 1.0e-2f,
                   "the sidechain reaches COMP, just as in Easy Comp (and it makes a difference)");
        }

        {   // All thirteen on, against the thirteen standalones one after another
            S s;
            auto chain = standalones();
            const int choice[] { 2, 1, 3, 1, 3, 1, 1, 2, 2, 5, 1, 1, 2 };
            for (int m = 0; m < S::numModules; ++m)
            {
                chain[(size_t) m]->loadPreset (chain[(size_t) m]->getBuiltInPresets()[(size_t) choice[m]]);
                s.getModule (m).loadPreset (s.getModule (m).getBuiltInPresets()[(size_t) choice[m]]);
                switchOn (s, m, true);
                prepareStereo (*chain[(size_t) m], true);
            }
            prepareStereo (s, true);

            auto expected = in;
            for (auto m : S::orderFor (false, false, false, false)) expected = process (*chain[(size_t) m], expected);
            const auto out = process (s, in);
            const float diff = maxDifference (out, expected);
            check (diff < 1.0e-5f && s.getLatencySamples() == moduleLatencySum (s),
                   "all thirteen on: identical to the thirteen plugins one after another, TAPE first (difference " + juce::String (toDb (diff), 1) + " dB)");
        }
    }

    //==========================================================================
    void testSettingsBothWays()
    {
        std::cout << "\n[settings: strip <-> module]\n";
        S s; prepareStereo (s, true);
        const auto in = music (0.2);

        auto& comp = s.getModule (4);
        auto* moduleSetting = dynamic_cast<juce::RangedAudioParameter*> (comp.getParameters()[0]);
        auto* stripSetting = s.parameters.getParameter (StripIDs::moduleID (4, moduleSetting->paramID));
        check (stripSetting != nullptr && stripSetting->getName (64).startsWith ("Comp "), "the DAW sees \"" + (stripSetting ? stripSetting->getName (64) : juce::String()) + "\"");

        stripSetting->setValueNotifyingHost (0.8f);   // (automation)
        process (s, in);
        check (near (moduleSetting->getValue(), 0.8f, 1.0e-6f), "automation in the DAW moves the module");

        moduleSetting->setValueNotifyingHost (0.3f);   // (a knob in the module's window)
        check (near (stripSetting->getValue(), 0.3f, 1.0e-6f), "a knob in the module's window moves the strip's setting (so the DAW records it)");
        process (s, in);
        check (near (moduleSetting->getValue(), 0.3f, 1.0e-6f), "...and isn't pushed back to the old value");

        // A preset (or INIT) in the module's window
        comp.loadPreset (comp.getBuiltInPresets()[4]);
        bool allFollowed = true;
        for (auto* p : comp.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (auto* twin = s.parameters.getParameter (StripIDs::moduleID (4, r->paramID)))
                    allFollowed = allFollowed && near (twin->getValue(), r->getValue(), 1.0e-6f);
        check (allFollowed, "a preset chosen in the module's window updates every one of the strip's settings");

        // The module's own BYPASS = its ON switch
        comp.parameters.getParameter (CommonIDs::bypass)->setValueNotifyingHost (0.0f);
        check (s.isModuleOn (4), "the module's own BYPASS button switches it on and off in the strip too");

        // Shared OVERSAMPLING
        setStrip (s, StripIDs::oversampling, 3.0f);   // 8x
        process (s, in);
        check ((int) getParam (s.getModule (1), "quality") == 3 && (int) getParam (s.getModule (7), "quality") == 3
                   && (int) getParam (s.getModule (8), "quality") == 3,
               "OVERSAMPLING sets AMP's, TAPE's and CLIP's together");
        check (s.parameters.getParameter (StripIDs::moduleID (1, "quality")) == nullptr, "...and they don't have their own copies in the DAW's list");

        // Every module starts switched off
        S fresh;
        bool allOff = true;
        for (int m = 0; m < S::numModules; ++m) allOff = allOff && ! fresh.isModuleOn (m);
        check (allOff, "a new strip starts with every module switched off");
    }

    //==========================================================================
    void testSwitching()
    {
        std::cout << "\n[switching modules on and off]\n";
        S s; prepareStereo (s, true);
        const auto in = sine (sr, 300.0, 0.3f, 3.0);

        juce::AudioBuffer<float> out;
        bool skippedWhileOff = false, runningWhileOn = false;
        out = process (s, in, [&] (int start)
        {
            if (start == 24576) switchOn (s, 5, true);    // TRANS on at 0.5 s
            if (start == 24576 + 4 * block) runningWhileOn = ! s.isModuleSkipped (5);
            if (start == 73728) switchOn (s, 5, false);   // off at 1.5 s
            if (start == 73728 + 12 * block) skippedWhileOff = s.isModuleSkipped (5);
        });

        float jump = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i) jump = std::max (jump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        const float sineStep = 0.3f * (float) (juce::MathConstants<double>::twoPi * 300.0 / sr);
        check (runningWhileOn && skippedWhileOff, "switched on it runs; 40 ms after switching off it's skipped");
        check (jump < sineStep * 1.6f, "no clicks switching on or off (biggest step " + juce::String (jump / sineStep, 2) + "x the sine's own)");
        check (s.getLatencySamples() == moduleLatencySum (s), "the delay stays the same either way");
    }

    //==========================================================================
    void testInputOutput()
    {
        std::cout << "\n[INPUT, OUTPUT, BYPASS]\n";
        S s;
        setStrip (s, StripIDs::trim, 6.0f);
        setStrip (s, StripIDs::polarity, 1.0f);
        setStrip (s, StripIDs::level, -3.0f);
        prepareStereo (s, true);
        const auto in = music (1.0);
        auto out = process (s, in);
        const int latency = s.getLatencySamples();
        check (differenceDelayed (out, in, latency, -fromDb (3.0f)) < 1.0e-5f, "TRIM +6, polarity flipped, LEVEL -3: exactly -(+3 dB)");

        const float inPeak = s.takeInputPeak(), outPeak = s.takeOutputPeak();
        check (near (toDb (inPeak), toDb (peak (in, 0, 0, in.getNumSamples())) + 6.0f, 0.05f) && near (toDb (outPeak), toDb (inPeak) - 3.0f, 0.05f),
               "the INPUT meter reads after TRIM, OUTPUT after LEVEL");

        // The whole strip's BYPASS: the original, on time
        switchOn (s, 1, true); switchOn (s, 4, true); switchOn (s, 7, true);
        setStrip (s, CommonIDs::bypass, 1.0f);
        prepareStereo (s, true);
        out = process (s, in);
        juce::AudioBuffer<float> tail (2, in.getNumSamples() - 4800);
        juce::AudioBuffer<float> inTail (2, tail.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) { tail.copyFrom (ch, 0, out, ch, 4800, tail.getNumSamples()); inTail.copyFrom (ch, 0, in, ch, 4800 - s.getLatencySamples(), tail.getNumSamples()); }
        check (maxDifference (tail, inTail) < 1.0e-6f, "BYPASS (the whole strip): the original sound, on time, with modules on");
    }

    //==========================================================================
    void testLatencyChange()
    {
        std::cout << "\n[the delay changes with the EQ's MASTER]\n";
        S s; prepareStereo (s, true);
        const int before = s.getLatencySamples();
        setStrip (s, StripIDs::moduleID (2, EQIDs::master), 1.0f);
        process (s, music (0.3));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        process (s, music (0.1));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        const int after = s.getLatencySamples();
        check (after > before + 1000 && after == moduleLatencySum (s), "MASTER on: the strip tells the DAW its new delay (" + juce::String (before) + " -> " + juce::String (after) + ")");

        S s2; setStrip (s2, StripIDs::moduleID (2, EQIDs::master), 1.0f); prepareStereo (s2, true);
        const auto in = music (2.0);
        const auto out = process (s2, in);
        check (differenceDelayed (out, in, s2.getLatencySamples()) < 1.0e-6f, "...and everything stays on time");
    }

    void testPrecision()
    {
        std::cout << "\n[EQ MASTER PRECISION]\n";
        int delays[3] {};
        for (int precision = 0; precision < 3; ++precision)
        {
            S s;
            setStrip (s, StripIDs::moduleID (2, EQIDs::master), 1.0f);
            setStrip (s, StripIDs::moduleID (2, EQIDs::precision), (float) precision);
            switchOn (s, 2, true);
            s.getModule (2).loadPreset (s.getModule (2).getBuiltInPresets()[5]);
            prepareStereo (s, true);
            delays[precision] = s.getModule (2).getLatencySamples();
        }
        check (delays[0] == 512 && delays[1] == 1024 && delays[2] == 2048,
               "LOW / MEDIUM / HIGH: " + juce::String (delays[0]) + " / " + juce::String (delays[1]) + " / " + juce::String (delays[2]) + " samples of delay at 48 kHz");

        // Changing it while playing: the new delay is reported once the new filter's in use, and the sound lines up again
        S s;
        setStrip (s, StripIDs::moduleID (2, EQIDs::master), 1.0f);
        switchOn (s, 2, true);
        prepareStereo (s, true);
        setStrip (s, StripIDs::moduleID (2, EQIDs::precision), 0.0f);
        for (int i = 0; i < 20 && s.getModule (2).getLatencySamples() != 512; ++i)
        {
            process (s, music (0.05));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        }
        process (s, music (0.05));   // (the strip notices at its next block)
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (s.getModule (2).getLatencySamples() == 512 && s.getLatencySamples() == moduleLatencySum (s),
               "switched to LOW while playing: the strip reports the shorter delay (" + juce::String (s.getLatencySamples()) + ")");
        const auto in = music (1.0);
        const auto out = process (s, in);
        juce::AudioBuffer<float> tail (2, 24000), inTail (2, 24000);
        for (int ch = 0; ch < 2; ++ch) { tail.copyFrom (ch, 0, out, ch, 24000, 24000); inTail.copyFrom (ch, 0, in, ch, 24000 - s.getLatencySamples(), 24000); }
        check (maxDifference (tail, inTail) < 1.0e-4f, "...and with the EQ flat everything is on time again");

        // Easy EQ on its own too
        EasyEQAudioProcessor alone;
        setParam (alone, EQIDs::master, 1.0f);
        setParam (alone, EQIDs::precision, 1.0f);
        prepareStereo (alone, false);
        check (alone.getLatencySamples() == 1024, "Easy EQ itself: MEDIUM = 1024 samples (21 ms)");
    }

    //==========================================================================
    void testPresetsAndAB()
    {
        std::cout << "\n[strip presets, A / B]\n";
        S s;
        const auto& presets = s.getBuiltInPresets();
        check (presets.size() >= 60, "a comprehensive set of strip presets (" + juce::String ((int) presets.size()) + ")");
        {
            juce::StringArray categories;
            bool masterOnlyInMastering = true;
            for (auto& preset : presets)
            {
                if (preset.category.isNotEmpty()) categories.addIfNotAlreadyThere (preset.category);
                if (preset.name.startsWith ("Master - ") != (preset.category == "Mastering")) masterOnlyInMastering = false;
            }
            bool tidy = masterOnlyInMastering && categories.contains ("Mix Bus") && categories.contains ("Mastering");
            for (auto& c : categories) if (c.startsWith ("Mix Bus") && c != "Mix Bus") tidy = false;
            check (tidy, "full-mix presets in two lists - Mix Bus (genres and lo-fi included) and Mastering: " + categories.joinIntoString (", "));
        }

        // Every recipe found all its module presets, and loads exactly what it says
        bool allGood = true;
        juce::StringArray problems;
        juce::StringArray names;
        for (auto& preset : presets)
        {
            if (names.contains (preset.name)) { allGood = false; problems.add ("duplicate " + preset.name); }
            names.add (preset.name);
            if (preset.tip.isEmpty()) { allGood = false; problems.add ("no tip: " + preset.name); }
        }
        for (auto& recipe : StripPresetRecipes::all())
        {
            S t;
            const Preset* p = nullptr;
            for (auto& q : t.getBuiltInPresets()) if (q.name == recipe.name) p = &q;
            if (p == nullptr) { allGood = false; problems.add ("missing " + juce::String (recipe.name)); continue; }
            t.loadPreset (*p);
            prepareStereo (t, true);
            process (t, music (0.02));

            std::array<bool, S::numModules> wanted {};
            for (auto& use : recipe.uses)
            {
                wanted[(size_t) use.module] = true;
                const Preset* modulePreset = nullptr;
                for (auto& q : t.getModule (use.module).getBuiltInPresets()) if (q.name == use.preset) modulePreset = &q;
                if (modulePreset == nullptr) { allGood = false; problems.add (juce::String (recipe.name) + ": no module preset " + use.preset); continue; }
                for (auto& [id, value] : modulePreset->values)
                {
                    bool overridden = false;
                    for (auto& set : recipe.sets) if (set.module == use.module && id == set.id) overridden = true;
                    if (! overridden && ! near (getParam (t.getModule (use.module), id.toRawUTF8()), value, 0.02f + std::abs (value) * 0.002f))
                    { allGood = false; problems.add (juce::String (recipe.name) + ": " + id); }
                }
            }
            for (int m = 0; m < S::numModules; ++m)
                if (t.isModuleOn (m) != wanted[(size_t) m]) { allGood = false; problems.add (juce::String (recipe.name) + ": module " + juce::String (m) + " on/off"); }

            const auto out = process (t, music (1.0));
            if (! allFinite (out) || peak (out, 0, 0, out.getNumSamples()) > 4.0f) { allGood = false; problems.add (juce::String (recipe.name) + ": level"); }
        }
        // Knobs a recipe sets on top land exactly
        bool setsLand = true;
        for (auto& recipe : StripPresetRecipes::all())
        {
            if (recipe.sets.empty()) continue;
            S t;
            for (auto& q : t.getBuiltInPresets()) if (q.name == recipe.name) t.loadPreset (q);
            prepareStereo (t, true);
            process (t, music (0.02));
            for (auto& set : recipe.sets)
                if (! near (getParam (t.getModule (set.module), set.id), set.value, 0.02f + std::abs (set.value) * 0.002f))
                { setsLand = false; problems.add (juce::String (recipe.name) + ": set " + set.id); }
            for (auto* swap : recipe.order)
                if (getParam (t, swap) < 0.5f) { setsLand = false; problems.add (juce::String (recipe.name) + ": order " + swap); }
        }
        check (setsLand, "the genre presets' own knob settings and ORDER land exactly" + (setsLand ? juce::String() : " (" + problems.joinIntoString ("; ").substring (0, 400) + ")"));

        check (allGood, "every strip preset switches on its modules with their own presets, and sounds sane"
                          + (allGood ? juce::String() : " (" + problems.joinIntoString ("; ").substring (0, 500) + ")"));

        // A / B for the whole strip
        S ab; prepareStereo (ab, true);
        const Preset* snare = nullptr;
        for (auto& q : ab.getBuiltInPresets()) if (q.name == "Snare - Rock") snare = &q;
        ab.loadPreset (*snare);
        process (ab, music (0.05));
        ab.switchToSlot (1);
        process (ab, music (0.05));
        const bool bCopied = ab.isModuleOn (0) && ab.isModuleOn (4);
        switchOn (ab, 0, false);
        setStrip (ab, StripIDs::moduleID (4, CompIDs::threshold), -35.0f);
        process (ab, music (0.05));
        ab.switchToSlot (0);
        process (ab, music (0.05));
        const bool aBack = ab.isModuleOn (0) && ! near (getParam (ab.getModule (4), CompIDs::threshold), -35.0f, 0.1f);
        ab.switchToSlot (1);
        process (ab, music (0.05));
        const bool bBack = ! ab.isModuleOn (0) && near (getParam (ab.getModule (4), CompIDs::threshold), -35.0f, 0.1f);
        check (bCopied && aBack && bBack, "strip A / B: two whole strips (modules on / off and every setting), switched back and forth");

        juce::MemoryBlock saved;
        ab.getStateInformation (saved);
        S restored;
        restored.setStateInformation (saved.getData(), (int) saved.getSize());
        prepareStereo (restored, true);
        process (restored, music (0.05));
        restored.switchToSlot (0);
        process (restored, music (0.05));
        check (restored.isModuleOn (0), "...both kept with the project");

        // A / B for Comp (inside the strip, and Easy Comp itself)
        S c; prepareStereo (c, true);
        auto& comp = c.getModule (4);
        setParam (comp, CompIDs::threshold, -12.0f);
        comp.switchToSlot (1);
        setParam (comp, CompIDs::threshold, -30.0f);
        comp.switchToSlot (0);
        process (c, music (0.05));
        check (near (getParam (comp, CompIDs::threshold), -12.0f, 0.05f)
               && near (c.parameters.getParameter (StripIDs::moduleID (4, CompIDs::threshold))->convertFrom0to1 (
                            c.parameters.getParameter (StripIDs::moduleID (4, CompIDs::threshold))->getValue()), -12.0f, 0.05f),
               "Comp A / B: switching back gives A's settings, in the module and the strip");

        EasyCompAudioProcessor alone;
        std::unique_ptr<juce::AudioProcessorEditor> editor (alone.createEditorAndMakeActive());
        auto* window = dynamic_cast<EasyCompAudioProcessorEditor*> (editor.get());
        setParam (alone, CompIDs::ratio, 2.0f);
        window->slotB.onClick();
        setParam (alone, CompIDs::ratio, 8.0f);
        window->slotA.onClick();
        check (near (getParam (alone, CompIDs::ratio), 2.0f, 0.05f) && window->slotA.getToggleState(), "Easy Comp's own window: A / B buttons work");
        alone.editorBeingDeleted (editor.get());
    }

    void testStripAutoGain()
    {
        std::cout << "\n[AUTO-GAIN for the whole strip]\n";
        const auto runWith = [] (float transOutputDb, bool clipOn, bool autoOn)
        {
            S s;
            switchOn (s, 5, true);
            setParam (s.getModule (5), TransientIDs::output, transOutputDb);
            if (clipOn) switchOn (s, 7, true);
            setStrip (s, CommonIDs::autoGain, autoOn ? 1.0f : 0.0f);
            prepareStereo (s, true);
            const auto in = music (8.0);
            const auto out = process (s, in);
            const int from = 6 * 48000, to = 8 * 48000;
            return (float) (loudnessDb (out, from, to) - loudnessDb (in, from - s.getLatencySamples(), to - s.getLatencySamples()));
        };

        const float louderOff = runWith (8.0f, false, false), louderOn = runWith (8.0f, false, true);
        check (louderOff > 6.0f && std::abs (louderOn) < 1.0f, "a module making it 8 dB louder: AUTO-GAIN brings it back (" + juce::String (louderOff, 1) + " -> " + juce::String (louderOn, 1) + " dB)");
        const float quieterOn = runWith (-8.0f, false, true);
        check (std::abs (quieterOn) < 1.0f, "...and 8 dB quieter: turned back up (" + juce::String (quieterOn, 1) + " dB)");
        const float withClip = runWith (-8.0f, true, true);
        check (withClip < -6.0f, "with CLIP on it never turns up (so CLIP's CEILING holds): " + juce::String (withClip, 1) + " dB");
        S s;
        check (s.parameters.getParameter (CommonIDs::autoGain) != nullptr && getParam (s, CommonIDs::autoGain) < 0.5f
               && ! s.getPresetParameterIDs().contains (CommonIDs::autoGain), "starts off, and presets / A / B leave it alone");
    }

    void testOrder()
    {
        std::cout << "\n[ORDER]\n";
        check (S::describe (S::orderFor (false, false, false, false)) == "TAPE > GATE > WAH > AMP > EQ > DS > COMP > TRANS > FLANGE > TREMOLO > DELAY > VERB > CLIP",
               "standard: " + S::describe (S::orderFor (false, false, false, false)));
        check (S::describe (S::orderFor (true, false, false, false)) == "TAPE > GATE > WAH > AMP > DS > COMP > EQ > TRANS > FLANGE > TREMOLO > DELAY > VERB > CLIP", "EQ AFTER COMP");
        check (S::describe (S::orderFor (false, true, false, false)) == "TAPE > GATE > WAH > DS > AMP > EQ > COMP > TRANS > FLANGE > TREMOLO > DELAY > VERB > CLIP", "DE-ESS BEFORE DRIVE");
        check (S::describe (S::orderFor (false, false, true, false)) == "TAPE > GATE > WAH > EQ > DS > COMP > AMP > TRANS > FLANGE > TREMOLO > DELAY > VERB > CLIP", "DRIVE AFTER COMP");
        check (S::describe (S::orderFor (false, false, false, true)) == "TAPE > GATE > WAH > AMP > EQ > DS > TRANS > COMP > FLANGE > TREMOLO > DELAY > VERB > CLIP", "SHAPE BEFORE COMP");
        check (S::describe (S::orderFor (false, false, false, false, true)) == "GATE > WAH > AMP > EQ > DS > COMP > TRANS > FLANGE > TREMOLO > DELAY > VERB > TAPE > CLIP", "TAPE AT THE END");
        bool allValid = true;
        for (int mask = 0; mask < 32; ++mask)
        {
            auto o = S::orderFor (mask & 1, mask & 2, mask & 4, mask & 8, mask & 16);
            auto sorted = o; std::sort (sorted.begin(), sorted.end());
            for (int i = 0; i < S::numModules; ++i) allValid = allValid && sorted[(size_t) i] == i;
            if (mask & 16) allValid = allValid && o[0] == 0 && o[1] == 11 && o[7] == 12 && o[8] == 10 && o[9] == 9 && o[10] == 6 && o[11] == 8 && o[12] == 7;
            else           allValid = allValid && o[0] == 8 && o[1] == 0 && o[2] == 11 && o[8] == 12 && o[9] == 10 && o[10] == 9 && o[11] == 6 && o[12] == 7;
            if (mask & 2) allValid = allValid && std::find (o.begin(), o.end(), 3) < std::find (o.begin(), o.end(), 1);
        }
        check (allValid, "all 32 combinations: every module exactly once; TAPE first (or just before CLIP), then GATE and WAH; FLANGE, TREMOLO, DELAY, VERB and CLIP last");

        // All five switches on: identical to the standalones run in that order
        {
            S s;
            auto alone = standalones();
            const int choice[] { 2, 1, 3, 1, 3, 1, 1, 2, 2, 5, 1, 1, 2 };
            for (int m = 0; m < S::numModules; ++m)
            {
                alone[(size_t) m]->loadPreset (alone[(size_t) m]->getBuiltInPresets()[(size_t) choice[m]]);
                s.getModule (m).loadPreset (s.getModule (m).getBuiltInPresets()[(size_t) choice[m]]);
                switchOn (s, m, true);
                prepareStereo (*alone[(size_t) m], true);
            }
            for (auto* id : StripIDs::orderSwitches) setStrip (s, id, 1.0f);
            prepareStereo (s, true);

            const auto in = music (3.0);
            auto expected = in;
            for (auto m : S::orderFor (true, true, true, true, true))
                expected = process (*alone[(size_t) m], expected);
            const auto out = process (s, in);
            const float diff = maxDifference (out, expected);
            check (diff < 1.0e-5f, "all five switches on: identical to the plugins run in that order (" + juce::String (toDb (diff), 1) + " dB)");
        }

        // Changing it while playing: a short dip, no click, then the new order
        {
            S s; switchOn (s, 1, true); switchOn (s, 4, true);
            s.getModule (1).loadPreset (s.getModule (1).getBuiltInPresets()[3]);
            prepareStereo (s, true);
            const auto in = sine (sr, 200.0, 0.2f, 1.5);
            const auto out = process (s, in, [&] (int start) { if (start == 24576) setStrip (s, StripIDs::driveAfterComp, 1.0f); });
            float jump = 0.0f;
            for (int i = 24576; i < 24576 + 4800; ++i) jump = std::max (jump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            float before = 0.0f;   // (the biggest normal step, in the old order and the new one)
            for (int i = 12000; i < 24000; ++i) before = std::max (before, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            for (int i = 48000; i < 60000; ++i) before = std::max (before, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            check (jump <= before * 1.5f + 1.0e-4f && s.getWantedOrder() == S::orderFor (false, false, true, false),
                   "switched while playing: no click (biggest step " + juce::String (jump / juce::jmax (1.0e-6f, before), 2) + "x normal)");
        }

        // Presets and A / B carry the order
        {
            S s;
            for (auto& p : s.getBuiltInPresets()) if (p.name == "Vocal - Punk Shout") s.loadPreset (p);
            const bool presetSets = getParam (s, StripIDs::deEssBeforeDrive) > 0.5f;
            for (auto& p : s.getBuiltInPresets()) if (p.name == "Vocal - Pop") s.loadPreset (p);
            const bool otherClears = s.isStandardOrder();
            s.switchToSlot (1);
            setStrip (s, StripIDs::eqAfterComp, 1.0f);
            s.switchToSlot (0);
            const bool aStandard = s.isStandardOrder();
            s.switchToSlot (1);
            check (presetSets && otherClears && aStandard && getParam (s, StripIDs::eqAfterComp) > 0.5f,
                   "presets set the order (Vocal - Punk Shout: DE-ESS BEFORE DRIVE; others: standard), and A / B keeps one each");
        }
    }

    //==========================================================================
    void testLufs()
    {
        std::cout << "\n[LUFS]\n";
        S s; prepareStereo (s, true);
        process (s, sine (sr, 997.0, 0.1f, 4.0));   // -20 dBFS on both sides = -20 LUFS
        check (near (s.getShortTermLufs(), -20.0f, 0.15f), "short-term: a -20 dBFS tone reads -20 LUFS (" + juce::String (s.getShortTermLufs(), 2) + ")");
        check (near (s.getIntegratedLufs(), -20.0f, 0.15f), "integrated: -20 LUFS (" + juce::String (s.getIntegratedLufs(), 2) + ")");

        process (s, sine (sr, 997.0, 0.001f, 4.0));   // a quiet part (-60)
        check (near (s.getIntegratedLufs(), -20.0f, 0.4f), "a quiet part doesn't drag the integrated number down (" + juce::String (s.getIntegratedLufs(), 2) + ")");
        check (s.getShortTermLufs() < -55.0f, "...while short-term follows it");

        s.resetIntegratedLufs();
        process (s, sine (sr, 997.0, 0.0316f, 3.0));   // -30
        check (near (s.getIntegratedLufs(), -30.0f, 0.2f), "click to reset: starts again (" + juce::String (s.getIntegratedLufs(), 2) + ")");

        // Against the independent BS.1770 measurement, on music
        S s2; prepareStereo (s2, true);
        const auto m = music (6.0);
        process (s2, m);
        const double reference = -0.691 + loudnessDb (m, (int) (3.0 * sr), m.getNumSamples());
        check (near (s2.getShortTermLufs(), (float) reference, 0.3f), "short-term matches an independent measurement on music (" + juce::String (s2.getShortTermLufs(), 2) + " vs " + juce::String (reference, 2) + ")");
    }

    //==========================================================================
    void testSaving()
    {
        std::cout << "\n[saving with the project]\n";
        juce::MemoryBlock saved;
        {
            S s;
            switchOn (s, 4, true);
            s.getModule (4).loadPreset (s.getModule (4).getBuiltInPresets()[3]);
            setStrip (s, StripIDs::trim, -4.5f);
            setStrip (s, StripIDs::oversampling, 2.0f);
            s.getStateInformation (saved);
        }

        S s;
        s.setStateInformation (saved.getData(), (int) saved.getSize());
        prepareStereo (s, true);
        process (s, music (0.1));

        auto& comp = s.getModule (4);
        check (s.isModuleOn (4) && ! s.isModuleOn (0), "which modules are on comes back");
        check (near (getParam (s, StripIDs::trim), -4.5f, 0.01f) && (int) getParam (s, StripIDs::oversampling) == 2, "TRIM and OVERSAMPLING come back");
        check (comp.getPresetName() == comp.getBuiltInPresets()[3].name, "the module's preset name comes back (\"" + comp.getPresetName() + "\")");

        bool same = true;
        for (auto& [id, value] : comp.getBuiltInPresets()[3].values)
            same = same && near (getParam (comp, id.toRawUTF8()), value, 1.0e-3f);
        check (same, "the module's settings come back, in the module itself");
    }

    //==========================================================================
    void useSettings (int skin, float scale = 1.0f, bool ticker = true)
    {
        static const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("easystrip-test.settings");
        file.deleteFile();
        StripSettings::useFileForTesting (file);
        StripSettings st;
        st.skin = skin; st.scale = scale; st.ticker = ticker;
        st.save();
    }

    void testClassicWindow (const juce::File& outDir)
    {
        std::cout << "\n[window: CLASSIC skin]\n";
        useSettings (StripSettings::classic, 1.0f);
        S s; prepareStereo (s, true);
        switchOn (s, 4, true); switchOn (s, 2, true);
        process (s, music (1.0));

        std::unique_ptr<juce::AudioProcessorEditor> editor (s.createEditorAndMakeActive());
        auto* outer = dynamic_cast<EasyStripAudioProcessorEditor*> (editor.get());
        auto* window = outer != nullptr ? outer->getClassic() : nullptr;
        check (window != nullptr && window->getModuleWindow() != nullptr && outer->getHologram() == nullptr,
               "CLASSIC (chosen in SETUP): opens showing the first module's own window");
        if (window == nullptr) { s.editorBeingDeleted (editor.get()); return; }

        bool everyWindow = true;
        for (int m = 0; m < S::numModules; ++m)
        {
            window->tabs[(size_t) m].onClick();
            auto* shown = window->getModuleWindow();
            everyWindow = everyWindow && shown != nullptr && window->getShownModule() == m
                          && window->getLocalBounds().contains (shown->getBoundsInParent());
        }
        check (everyWindow, "each tab shows that module's own window, fitted inside");

        window->onLamps[6]->onClick();
        check (s.isModuleOn (6), "an ON lamp switches its module on");
        window->onLamps[6]->onClick();
        check (! s.isModuleOn (6), "...and off");

        setStrip (s, StripIDs::trim, 5.0f);
        window->initAllButton.onClick();
        bool allOff = true;
        for (int m = 0; m < S::numModules; ++m) allOff = allOff && ! s.isModuleOn (m);
        check (allOff && near (getParam (s, StripIDs::trim), 0.0f, 0.01f), "INIT: every module off, TRIM back to 0");

        switchOn (s, 4, true); switchOn (s, 2, true);
        window->tabs[4].onClick();
        for (int i = 0; i < 20; ++i) { process (s, music (0.1)); window->updateDisplaysNow(); }
        check (window->shortTermReadout.getText().contains ("LUFS"), "shows S and I LUFS (" + window->shortTermReadout.getText() + ", " + window->integratedReadout.getText() + ")");
        savePng (*editor, outDir.getChildFile ("strip-classic.png"));

        window->hologramButton.onClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        check (outer->getHologram() != nullptr && outer->getClassic() == nullptr, "HOLOGRAM LOOK switches to the HOLOGRAM skin");
        s.editorBeingDeleted (editor.get());
    }

    // Presses the mouse on a component and drags it (in its own coordinates)
    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, bool doubleClick = false)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto event = [&] (juce::Point<float> pos, int clicks)
        {
            return juce::MouseEvent (source, pos, {}, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &c, &c, now, from, now, clicks, false);
        };
        if (doubleClick) { c.mouseDoubleClick (event (from, 2)); return; }
        c.mouseDown (event (from, 1));
        c.mouseDrag (event (to, 1));
        c.mouseUp (event (to, 1));
    }

    void testHologram (const juce::File& outDir)
    {
        std::cout << "\n[window: HOLOGRAM skin]\n";
        useSettings (StripSettings::hologram, 1.0f);
        S s; prepareStereo (s, true);
        for (int m : { 0, 2, 3, 4, 5, 7 }) switchOn (s, m, true);
        s.getModule (2).loadPreset (s.getModule (2).getBuiltInPresets()[5]);
        s.getModule (4).loadPreset (s.getModule (4).getBuiltInPresets()[6]);
        process (s, music (1.0));

        std::unique_ptr<juce::AudioProcessorEditor> editor (s.createEditorAndMakeActive());
        auto* outer = dynamic_cast<EasyStripAudioProcessorEditor*> (editor.get());
        auto* view = outer != nullptr ? outer->getHologram() : nullptr;
        check (view != nullptr && editor->getWidth() == 1800 && editor->getHeight() == 1030, "HOLOGRAM by default; 1800 x 1030 at 100%");
        if (view == nullptr) { if (editor) s.editorBeingDeleted (editor.get()); return; }

        // Every setting of every module has its control (the standalone's controls, all of them)
        juce::StringArray missing;
        const juce::StringArray notKnobs { CommonIDs::bypass, CommonIDs::autoGain, "quality", "view", "analyser", "link", "master",
                                           "openedby", "turnsdown", "precision", "reactsto", "character", "thrust", "listensto", "sync",
                                           "lowshelf", "highshelf", "hpfslope", "lpfslope", "lowtarget", "lowmidtarget", "highmidtarget", "hightarget", "speed", "mode", "character", "shape", "response", "direction", "type" };
        for (int m = 0; m < S::numModules; ++m)
            for (auto* p : s.getModule (m).getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                    if (! notKnobs.contains (r->paramID) && view->findControl (m, r->paramID) == nullptr)
                        missing.add (juce::String (StripModules::names[m]) + "/" + r->paramID);
        check (missing.isEmpty(), "every knob of every module is on the panel" + (missing.isEmpty() ? juce::String() : " (missing: " + missing.joinIntoString (", ") + ")"));

        // Nothing sticks out of its panel, and nothing overlaps inside a module
        view->updateNow();
        bool tidy = true;
        juce::StringArray problems;
        for (auto& slab : view->slabs)
        {
            std::vector<juce::Rectangle<int>> placed;
            for (auto* child : slab->getChildren())
            {
                if (! child->isVisible()) continue;
                const auto b = child->getBounds();
                if (! slab->getLocalBounds().contains (b)) { tidy = false; problems.add (juce::String (StripModules::names[slab->getModule()]) + " outside: " + juce::String (b.toString())); }
                for (auto& other : placed)
                    if (other.intersects (b)) { tidy = false; problems.add (juce::String (StripModules::names[slab->getModule()]) + " overlap: " + b.toString() + " / " + other.toString()); }
                placed.push_back (b);
            }
        }
        check (tidy, "inside every module, nothing overlaps or sticks out" + (tidy ? juce::String() : " (" + problems.joinIntoString ("; ").substring (0, 400) + ")"));

        // Hints: the standalones' own words
        auto* threshold = view->findControl (4, CompIDs::threshold);
        view->mouseEnter (hoverEvent (threshold));
        check (view->getTickerText() == view->moduleHint (4, "THRESHOLD") && view->getTickerText().isNotEmpty(),
               "hovering Comp's THRESHOLD shows Easy Comp's own hint on the ticker");

        // Dragging, double-click to reset, typing a value
        auto* compThreshold = view->findControl (4, CompIDs::threshold);
        const float before = getParam (s.getModule (4), CompIDs::threshold);
        drag (*compThreshold, { 28.0f, 100.0f }, { 28.0f, 136.0f });
        process (s, music (0.05));
        const float after = getParam (s.getModule (4), CompIDs::threshold);
        check (after < before - 1.0f && near (s.parameters.getParameter (StripIDs::moduleID (4, CompIDs::threshold))->getValue(),
                                              s.getModule (4).parameters.getParameter (CompIDs::threshold)->getValue(), 1.0e-5f),
               "dragging THRESHOLD down lowers it (" + juce::String (before, 1) + " -> " + juce::String (after, 1) + " dB), and the DAW sees it");

        drag (*compThreshold, { 28.0f, 30.0f }, { 28.0f, 30.0f }, true);
        const float defaultThreshold = s.getModule (4).parameters.getParameter (CompIDs::threshold)->convertFrom0to1 (
                                         s.getModule (4).parameters.getParameter (CompIDs::threshold)->getDefaultValue());
        check (near (getParam (s.getModule (4), CompIDs::threshold), defaultThreshold, 0.05f), "double-click the bar: back to its default");

        auto* highFreq = dynamic_cast<Holo::Ring*> (view->findControl (2, EQIDs::freq[3]));
        highFreq->getParameter().setValueNotifyingHost (highFreq->getParameter().convertTo0to1 (5000.0f));
        const auto centre = juce::Point<float> (highFreq->getWidth() * 0.5f, highFreq->getHeight() - 58.0f * 0.46f);
        drag (*highFreq, centre, centre, true);
        auto* entry = dynamic_cast<juce::TextEditor*> (view->getChildren().getLast());
        for (auto* c : view->getChildren()) if (auto* t = dynamic_cast<juce::TextEditor*> (c)) entry = t;
        const bool opened = entry != nullptr && entry->isVisible();
        if (opened) { entry->setText ("10"); entry->onReturnKey(); }
        check (opened && near (getParam (s.getModule (2), EQIDs::freq[3]), 10000.0f, 1.0f),
               "double-click a number to type: \"10\" on HIGH's frequency gives 10 kHz");

        // Power, INIT, presets
        auto& tapePower = view->slabs[8]->power;
        tapePower.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        process (s, music (0.05));
        check (s.isModuleOn (8), "a module's name button switches it on (TAPE)");

        s.getModule (5).parameters.getParameter (TransientIDs::punch)->setValueNotifyingHost (0.9f);
        view->slabs[5]->init.onClick();
        check (near (getParam (s.getModule (5), TransientIDs::punch), 0.0f, 0.5f) && s.isModuleOn (4), "a module's INIT resets just that module");

        // Clip ADVANCED appears under the EQ
        setParam (s.getModule (7), ClipIDs::view, 1.0f);
        view->updateNow();
        check (view->clipAdvanced->isVisible(), "Clip on ADVANCED: SOUND DESIGN appears under the EQ");

        // LINK: TOP and BOTTOM keep their gap
        setParam (s.getModule (7), ClipIDs::top, -2.0f);
        setParam (s.getModule (7), ClipIDs::bottom, -5.0f);
        setParam (s.getModule (7), ClipIDs::link, 1.0f);
        view->findControl (7, ClipIDs::top)->setFromUser (s.getModule (7).parameters.getParameter (ClipIDs::top)->convertTo0to1 (-4.0f));
        check (near (getParam (s.getModule (7), ClipIDs::top), -4.0f, 0.06f) && near (getParam (s.getModule (7), ClipIDs::bottom), -7.0f, 0.06f),
               "LINK keeps the gap between TOP and BOTTOM");

        // The EQ page draws dB where the scale says (+12 on the +12 line)
        {
            auto& eqp = s.getModule (2);
            setParam (eqp, EQIDs::gain[2], 0.0f);
            const float zeroY = view->getScreen().eqDotPosition (2).y;
            setParam (eqp, EQIDs::gain[2], 12.0f);
            const float plusY = view->getScreen().eqDotPosition (2).y;
            setParam (eqp, EQIDs::gain[2], -12.0f);
            const float minusY = view->getScreen().eqDotPosition (2).y;
            const float plotHalf = (view->getScreen().getHeight() - 12.0f - 40.0f) * 0.5f;   // (the plot's half height = 15 dB)
            check (near (zeroY - plusY, plotHalf * 12.0f / 15.0f, 2.0f) && near (minusY - zeroY, plotHalf * 12.0f / 15.0f, 2.0f),
                   "the EQ page: a +12 / -12 dB band sits on the +12 / -12 lines (" + juce::String (zeroY - plusY, 1) + " / "
                       + juce::String (minusY - zeroY, 1) + " px, wanted " + juce::String (plotHalf * 0.8f, 1) + ")");
            setParam (eqp, EQIDs::gain[2], 0.0f);
        }

        // The page buttons sit over the big screen; DISPLAY BRIGHTNESS lives in SETUP
        {
            const auto screenArea = view->getScreen().getBounds();
            bool over = true;
            int shown = 0;
            for (auto& b : view->pageButtons)
                if (b->isVisible())
                {
                    ++shown;
                    over = over && b->getX() >= screenArea.getX() && b->getRight() <= screenArea.getRight();
                }
            check (over && shown == 8 && ! view->pageButtons[6]->isVisible() && ! view->pageButtons[9]->isVisible() && ! view->pageButtons[10]->isVisible() && ! view->pageButtons[11]->isVisible() && ! view->pageButtons[12]->isVisible(),
                   "the DISPLAY FOCUS buttons (8: TAPE in; the rack's effects aren't) all sit above the big screen");
        }

        // Pages and live displays
        for (int i = 0; i < 25; ++i) { process (s, music (0.05)); view->updateNow(); }
        for (int m = 0; m < S::numModules; ++m)
        {
            if (StripModules::isRackEffect (m)) continue;
            view->pageButtons[(size_t) m]->onClick();
            check (view->getScreen().getPage() == m, juce::String ("page button ") + StripModules::tabs[m] + " shows it on the big screen");
        }
        switchOn (s, 8, true);
        loadPresetNamed (s.getModule (8), "Drum Bus - Hit the Tape Hard");
        view->pageButtons[8]->onClick();
        for (int i = 0; i < 20; ++i) { process (s, music (0.05)); view->updateNow(); }
        savePng (*editor, outDir.getChildFile ("strip-hologram-tape.png"));

        // WEAPON SELECT: the rack in place of the left-hand modules, and back
        {
            view->weaponSelect.onClick();
            const auto rackArea = view->getRack()->getBounds();
            const auto& slapSlab = *view->slabs[6];
            const auto& delaySlab = *view->slabs[9];
            const auto& tremSlab = *view->slabs[10];
            const auto& wahSlab = *view->slabs[11];
            const auto& flangeSlab = *view->slabs[12];
            bool swapped = view->isRackShown() && view->getRack()->isVisible() && ! view->getScreen().isVisible() && slapSlab.isVisible()
                           && rackArea.contains (slapSlab.getBounds()) && delaySlab.isVisible() && rackArea.contains (delaySlab.getBounds())
                           && delaySlab.getRight() < slapSlab.getX()
                           && tremSlab.isVisible() && rackArea.contains (tremSlab.getBounds()) && tremSlab.getRight() < delaySlab.getX()
                           && wahSlab.isVisible() && rackArea.contains (wahSlab.getBounds()) && wahSlab.getRight() < tremSlab.getX()
                           && flangeSlab.isVisible() && rackArea.contains (flangeSlab.getBounds()) && wahSlab.getRight() < flangeSlab.getX() && flangeSlab.getRight() < tremSlab.getX();
            for (int m : { 0, 1, 2, 3, 4 }) swapped = swapped && ! view->slabs[(size_t) m]->isVisible();
            for (int m : { 5, 7, 8 })       swapped = swapped && view->slabs[(size_t) m]->isVisible() && ! rackArea.intersects (view->slabs[(size_t) m]->getBounds());
            for (auto& b : view->pageButtons) swapped = swapped && ! b->isVisible();
            check (swapped, "WEAPON SELECT: the rack replaces GATE, AMP, the screen, EQ, DS and COMP; WAH, FLANGE, TREMOLO, DELAY and VERB are in it; TRANS, TAPE, CLIP stay");

            switchOn (s, 6, true);
            switchOn (s, 9, true);
            loadPresetNamed (s.getModule (9), "Synth - Synthwave Ping-Pong");
            switchOn (s, 10, true);
            loadPresetNamed (s.getModule (10), "Synth Pad - Trance Gate");
            switchOn (s, 11, true);
            loadPresetNamed (s.getModule (11), "Guitar - Funk Envelope");
            switchOn (s, 12, true);
            loadPresetNamed (s.getModule (12), "Guitar - Jet Flanger");
            for (int i = 0; i < 10; ++i) { process (s, music (0.05)); view->updateNow(); }
            savePng (*editor, outDir.getChildFile ("strip-hologram-rack.png"));
            loadPresetNamed (s.getModule (6), "Vocal - Club Stage");
            for (int i = 0; i < 4; ++i) { process (s, music (0.05)); view->updateNow(); }
            check (view->findControl (6, SlapIDs::decay) != nullptr && view->findControl (6, SlapIDs::decay)->isVisible()
                     && ! view->findControl (6, SlapIDs::length)->isVisible(),
                   "VERB on CLUB: DECAY, PRE-DELAY and TONE take LENGTH and SYNC's place in its bay");
            savePng (*editor, outDir.getChildFile ("strip-hologram-rack-verb.png"));
            view->mouseEnter (hoverEvent (view->findControl (6, SlapIDs::mix)));
            check (view->getTickerText() == view->moduleHint (6, "MIX") && view->getTickerText().isNotEmpty(),
                   "in the rack, VERB's controls still explain themselves (Easy Verb's own hints)");

            view->weaponSelect.onClick();
            bool back = ! view->isRackShown() && ! view->getRack()->isVisible() && view->getScreen().isVisible() && ! slapSlab.isVisible() && ! delaySlab.isVisible() && ! tremSlab.isVisible() && ! wahSlab.isVisible() && ! flangeSlab.isVisible();
            for (int m : { 0, 1, 2, 3, 4, 5, 7, 8 }) back = back && view->slabs[(size_t) m]->isVisible();
            check (back && s.isModuleOn (6), "WEAPON SELECT again: back to the strip (VERB keeps playing)");
            view->updateNow();
            check (view->weaponSelect.getToggleState(), "...and WEAPON SELECT stays lit while any of the EASY FX is on");
            for (int m : { 6, 9, 10, 11, 12 }) switchOn (s, m, false);
            view->updateNow();
            check (! view->weaponSelect.getToggleState(), "...and goes dark once they're all off");
        }

        // The track's name, top left, following the DAW
        {
            juce::AudioProcessor::TrackProperties props;
            props.name = "Snare Top";
            s.updateTrackProperties (props);
            view->updateNow();
            const bool first = view->getShownTrackName() == "Snare Top";
            props.name = "Snare Top (Rimshot)";
            s.updateTrackProperties (props);
            view->updateNow();
            check (first && view->getShownTrackName() == "Snare Top (Rimshot)", "the track's name shows top left, and follows a rename in the DAW");
            const auto preset = view->presetPrevious.getBounds();
            check (preset.getX() > 200 && view->orderButton.getRight() > view->oversampling->getX() - 90 && view->orderButton.getRight() < view->oversampling->getX() - 60,
                   "the presets to ORDER sit right, ORDER as close to the OVERSAMPLING lettering as its buttons are, the name to their left");
        }
        view->pageButtons[2]->onClick();
        switchOn (s, 11, true);   // (WAH on: WEAPON SELECT lit in the picture)
        process (s, music (0.3)); view->updateNow();
        savePng (*editor, outDir.getChildFile ("strip-hologram.png"));
        view->pageButtons[4]->onClick();
        for (int i = 0; i < 10; ++i) { process (s, music (0.05)); view->updateNow(); }
        savePng (*editor, outDir.getChildFile ("strip-hologram-comp.png"));

        // SETUP: size
        view->showSetup (true);
        check (view->isSetupShowing(), "SETUP opens");
        view->getSettings().scale = 0.8f;
        outer->applySettings();
        check (editor->getWidth() == 1440 && editor->getHeight() == 824, "SETUP size 80%: 1440 x 824 (" + juce::String (editor->getWidth()) + " x " + juce::String (editor->getHeight()) + ")");
        savePng (*editor, outDir.getChildFile ("strip-hologram-80.png"));
        view->showSetup (false);

        view->getSettings().ticker = false;
        outer->applySettings();
        check (outer->getHologram() != nullptr && editor->getHeight() == juce::roundToInt (974 * 0.8f), "ticker off: the window gets shorter");
        s.editorBeingDeleted (editor.get());
    }

    void testHologramSpeed()
    {
        std::cout << "\n[window speed]\n";
        useSettings (StripSettings::hologram, 1.0f);
        S s; prepareStereo (s, true);
        for (int m = 0; m < S::numModules; ++m) switchOn (s, m, true);
        std::unique_ptr<juce::AudioProcessorEditor> editor (s.createEditorAndMakeActive());
        auto* view = dynamic_cast<EasyStripAudioProcessorEditor*> (editor.get())->getHologram();
        juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 10; ++i)
        {
            process (s, music (0.033));
            view->updateNow();
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);
        }
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / 10.0;
        std::cout << "    whole window drawn: " << ms << " ms per frame (software drawing; the plugin only redraws what moves)\n";
        s.editorBeingDeleted (editor.get());
    }

    //==========================================================================
    void testSpeed()
    {
        std::cout << "\n[speed]\n";
        const auto in = music (10.0);
        const auto timeIt = [&] (S& s)
        {
            prepareStereo (s, true);
            const auto t0 = std::chrono::steady_clock::now();
            process (s, in);
            return std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 10.0 * 100.0;
        };

       #if JUCE_DEBUG
        std::cout << "    (speed is only checked in a Release build)\n";
        return;
       #endif
        S off;
        const double offCpu = timeIt (off);
        S on;
        for (int m = 0; m < S::numModules; ++m) switchOn (on, m, true);
        const double onCpu = timeIt (on);
        std::cout << "    all off: " << offCpu << "% of one core,  all on (4x): " << onCpu << "%\n";
        check (offCpu < 0.5, "everything switched off costs almost nothing");
        check (onCpu < 25.0, "everything switched on stays light enough to use on many tracks");
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String (argv[2]) == "genres")
    {
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        const auto load = [&] (const char* name)
        {
            std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (juce::File ("/home/claude/presetcheck/material").getChildFile (name)));
            juce::AudioBuffer<float> b (2, (int) std::min<long long> ((long long) r->lengthInSamples, 48000LL * 20));
            r->read (&b, 0, b.getNumSamples(), 0, true, true);
            return b;
        };
        const auto band = [] (const juce::AudioBuffer<float>& b, int start, int end, double lo, double hi)
        {
            double sum = 0;
            for (int ch = 0; ch < 2; ++ch)
            {
                juce::IIRFilter a, c, d, e;
                if (lo > 0) { a.setCoefficients (juce::IIRCoefficients::makeHighPass (48000, lo)); c.setCoefficients (juce::IIRCoefficients::makeHighPass (48000, lo)); }
                if (hi > 0) { d.setCoefficients (juce::IIRCoefficients::makeLowPass (48000, hi)); e.setCoefficients (juce::IIRCoefficients::makeLowPass (48000, hi)); }
                for (int i = 0; i < end; ++i)
                {
                    float x = b.getSample (ch, i);
                    if (lo > 0) x = c.processSingleSampleRaw (a.processSingleSampleRaw (x));
                    if (hi > 0) x = e.processSingleSampleRaw (d.processSingleSampleRaw (x));
                    if (i >= start) sum += (double) x * x;
                }
            }
            return 10.0 * std::log10 (sum / (end - start) + 1e-20);
        };
        const std::vector<std::pair<const char*, const char*>> jobs {
            { "Midnight Jazz", "mix_jazz.wav" }, { "Electric Jazz", "mix_jazz.wav" }, { "Heavy Jazz", "mix_jazz.wav" },
            { "Americana", "mix_acoustic.wav" }, { "Industrial Bass", "mix_edm.wav" }, { "Darksynth", "mix_edm.wav" },
            { "Cyberpunk", "mix_edm.wav" }, { "Synthwave", "mix_edm.wav" }, { "Retrowave", "mix_edm.wav" },
            { "Heavy Rock", "mix_rock.wav" }, { "Heavy Metal", "mix_rock.wav" }, { "Neo Punk", "mix_rock.wav" },
            { "New Wave", "mix_song.wav" }, { "Gospel", "mix_song.wav" } };
        std::cout << "preset              mix        LUFS in/out   peak   low<90   mid300-3k  high>6k (vs the loudness change)  LOW-MID HIGH-MID  sides\n";
        std::vector<std::tuple<const char*, const char*, int>> runs;
        const bool solo = argc > 3;
        for (auto& [name, file] : jobs)
        {
            if (solo && juce::String (name) != juce::String (argv[3])) continue;
            runs.push_back ({ name, file, -1 });
            if (solo) for (int m = 0; m < S::numModules; ++m) runs.push_back ({ name, file, m });
        }
        for (auto& [name, file, without] : runs)
        {
            auto in = load (file);
            S s;
            for (auto& q : s.getBuiltInPresets()) if (q.name == name) s.loadPreset (q);
            if (without >= 0) { if (! s.isModuleOn (without)) continue; switchOn (s, without, false); std::cout << "  without " << StripModules::names[without] << ":\n"; }
            prepareStereo (s, true);
            const auto out = process (s, in);
            const int lat = s.getLatencySamples(), a = 48000 * 4, z = in.getNumSamples() - lat;
            juce::AudioBuffer<float> aligned (2, z - a);
            for (int ch = 0; ch < 2; ++ch) aligned.copyFrom (ch, 0, out, ch, a + lat, z - a);
            juce::AudioBuffer<float> orig (2, z - a);
            for (int ch = 0; ch < 2; ++ch) orig.copyFrom (ch, 0, in, ch, a, z - a);
            const double lin = -0.691 + loudnessDb (orig, 0, orig.getNumSamples()), lout = -0.691 + loudnessDb (aligned, 0, aligned.getNumSamples());
            const double all = lout - lin;   // (each band's change relative to the loudness change)
            const auto rel = [&] (double lo, double hi) { return band (aligned, 0, aligned.getNumSamples(), lo, hi) - band (orig, 0, orig.getNumSamples(), lo, hi) - all; };
            juce::AudioBuffer<float> sideIn (1, orig.getNumSamples()), sideOut (1, orig.getNumSamples());
            double sIn = 0, sOut = 0, mIn = 0, mOut = 0;
            for (int i = 0; i < orig.getNumSamples(); ++i)
            {
                const double si = 0.5 * (orig.getSample (0, i) - orig.getSample (1, i)), mi = 0.5 * (orig.getSample (0, i) + orig.getSample (1, i));
                const double so = 0.5 * (aligned.getSample (0, i) - aligned.getSample (1, i)), mo = 0.5 * (aligned.getSample (0, i) + aligned.getSample (1, i));
                sIn += si * si; sOut += so * so; mIn += mi * mi; mOut += mo * mo;
            }
            const double sides = 10 * std::log10 ((sOut / (mOut + 1e-20)) / (sIn / (mIn + 1e-20) + 1e-20) + 1e-20);
            std::printf ("%-18s %-17s %6.1f / %5.1f  %5.1f   %+5.1f    %+5.1f      %+5.1f   %+5.1f  %+5.1f    %+5.1f\n", name, file, lin, lout,
                         toDb (juce::jmax (peak (aligned, 0, 0, aligned.getNumSamples()), peak (aligned, 1, 0, aligned.getNumSamples()))),
                         rel (0, 90), rel (300, 3000), rel (6000, 0), rel (0, 90) - rel (300, 3000), rel (6000, 0) - rel (300, 3000), sides);
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[2]) == "params")
    {
        S s;
        for (int m = 0; m < S::numModules; ++m)
        {
            std::cout << "=== " << StripModules::names[m] << "\n";
            for (auto* p : s.getModule (m).getParameters())
                if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
                {
                    const auto& range = r->getNormalisableRange();
                    std::cout << r->paramID << " [" << range.start << " .. " << range.end << "] default " << r->convertFrom0to1 (r->getDefaultValue());
                    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (r)) std::cout << " {" << c->choices.joinIntoString (", ") << "}";
                    std::cout << "\n";
                }
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[2]) == "presets")
    {
        S s;
        for (int m = 0; m < S::numModules; ++m)
        {
            std::cout << "=== " << StripModules::names[m] << "\n";
            for (auto& p : s.getModule (m).getBuiltInPresets())
                std::cout << p.category << " | " << p.name << "\n";
        }
        return 0;
    }
    if (argc > 2 && juce::String (argv[2]) == "hints")
    {
        S s;
        for (int m = 0; m < S::numModules; ++m)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (s.getModule (m).createEditorAndMakeActive());
            std::cout << "=== " << StripModules::names[m] << "\n";
            juce::StringArray seen;
            for (auto& [t, x] : dynamic_cast<EasyWindow*> (e.get())->getAllHints())
                if (! seen.contains (t)) { seen.add (t); std::cout << t << " | " << x << "\n"; }
            s.getModule (m).editorBeingDeleted (e.get());
        }
        return 0;
    }
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testAllOff();
    testModulesMatchStandalones();
    testSidechainAndChain();
    testSettingsBothWays();
    testSwitching();
    testInputOutput();
    testLatencyChange();
    testPrecision();
    testPresetsAndAB();
    testStripAutoGain();
    testOrder();
    testLufs();
    testSaving();
    testClassicWindow (outDir);
    testHologram (outDir);
    testHologramSpeed();
    testSpeed();
    return finish();
}

// Easy Strip: the modules inside it, the plumbing, INPUT / OUTPUT, LUFS and the window (not shipped).
#include "TestHelpers.h"
#include "../EasyPlugins/Plugins/EasyStrip/PluginProcessor.h"
#include "../EasyPlugins/Plugins/EasyStrip/PluginEditor.h"
#include "../EasyPlugins/Plugins/EasyStrip/HoloSlabs.h"
#include "../EasyPlugins/Plugins/EasyStrip/StripPresets.h"
#include "../EasyPlugins/Plugins/EasyComp/PluginEditor.h"

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
        int before = 0, after = 0;
        for (int k = 0; k < S::numModules; ++k)
            (k < m ? before : after) += k == m ? 0 : s.getModule (k).getLatencySamples();

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
        { EasyClipAudioProcessor a;      compareModule (7, a, 5); }
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

            int before = 0, after = 0;
            for (int k = 0; k < S::numModules; ++k) (k < 4 ? before : after) += k == 4 ? 0 : s.getModule (k).getLatencySamples();
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

        {   // All eight on, against the eight standalones one after another
            S s;
            std::vector<std::unique_ptr<EasyProcessor>> chain;
            chain.push_back (std::make_unique<EasyGateAudioProcessor>());
            chain.push_back (std::make_unique<EasyAmpAudioProcessor>());
            chain.push_back (std::make_unique<EasyEQAudioProcessor>());
            chain.push_back (std::make_unique<EasyDSAudioProcessor>());
            chain.push_back (std::make_unique<EasyCompAudioProcessor>());
            chain.push_back (std::make_unique<EasyTransientAudioProcessor>());
            chain.push_back (std::make_unique<BitchSlapAudioProcessor>());
            chain.push_back (std::make_unique<EasyClipAudioProcessor>());

            const int choice[] { 2, 1, 3, 1, 3, 1, 1, 2 };
            for (int m = 0; m < S::numModules; ++m)
            {
                chain[(size_t) m]->loadPreset (chain[(size_t) m]->getBuiltInPresets()[(size_t) choice[m]]);
                s.getModule (m).loadPreset (s.getModule (m).getBuiltInPresets()[(size_t) choice[m]]);
                switchOn (s, m, true);
                prepareStereo (*chain[(size_t) m], true);
            }
            prepareStereo (s, true);

            auto expected = in;
            for (auto& module : chain) expected = process (*module, expected);
            const auto out = process (s, in);
            const float diff = maxDifference (out, expected);
            check (diff < 1.0e-5f && s.getLatencySamples() == moduleLatencySum (s),
                   "all eight on: identical to the eight plugins one after another (difference " + juce::String (toDb (diff), 1) + " dB)");
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
        check ((int) getParam (s.getModule (1), "quality") == 3 && (int) getParam (s.getModule (7), "quality") == 3,
               "OVERSAMPLING sets AMP's and CLIP's together");
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

            std::array<bool, 8> wanted {};
            for (auto& use : recipe.uses)
            {
                wanted[(size_t) use.module] = true;
                const Preset* modulePreset = nullptr;
                for (auto& q : t.getModule (use.module).getBuiltInPresets()) if (q.name == use.preset) modulePreset = &q;
                if (modulePreset == nullptr) { allGood = false; problems.add (juce::String (recipe.name) + ": no module preset " + use.preset); continue; }
                for (auto& [id, value] : modulePreset->values)
                    if (! near (getParam (t.getModule (use.module), id.toRawUTF8()), value, 0.02f + std::abs (value) * 0.002f))
                    { allGood = false; problems.add (juce::String (recipe.name) + ": " + id); }
            }
            for (int m = 0; m < 8; ++m)
                if (t.isModuleOn (m) != wanted[(size_t) m]) { allGood = false; problems.add (juce::String (recipe.name) + ": module " + juce::String (m) + " on/off"); }

            const auto out = process (t, music (1.0));
            if (! allFinite (out) || peak (out, 0, 0, out.getNumSamples()) > 4.0f) { allGood = false; problems.add (juce::String (recipe.name) + ": level"); }
        }
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
                                           "lowshelf", "highshelf", "hpfslope", "lpfslope", "lowtarget", "lowmidtarget", "highmidtarget", "hightarget" };
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
        auto& slapPower = view->slabs[6]->power;
        slapPower.triggerClick();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        process (s, music (0.05));
        check (s.isModuleOn (6), "a module's name button switches it on");

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

        // The page buttons sit over the big screen; DISPLAY BRIGHTNESS lives in SETUP
        {
            const auto screenArea = view->getScreen().getBounds();
            bool over = true;
            for (auto& b : view->pageButtons)
                over = over && b->getX() >= screenArea.getX() && b->getRight() <= screenArea.getRight();
            check (over, "the DISPLAY FOCUS buttons all sit above the big screen");
        }

        // Pages and live displays
        for (int i = 0; i < 25; ++i) { process (s, music (0.05)); view->updateNow(); }
        for (int m = 0; m < S::numModules; ++m)
        {
            view->pageButtons[(size_t) m]->onClick();
            check (view->getScreen().getPage() == m, juce::String ("page button ") + StripModules::tabs[m] + " shows it on the big screen");
        }
        view->pageButtons[2]->onClick();
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
    testLufs();
    testSaving();
    testClassicWindow (outDir);
    testHologram (outDir);
    testHologramSpeed();
    testSpeed();
    return finish();
}

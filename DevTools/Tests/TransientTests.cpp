// Verification harness for Easy Trans - called Easy Transient up to 1.2.0 (not shipped).

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestHelpers.h"

using namespace th;
using P = EasyTransientAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    /** Onset and tail of a single hit, in vs out (dB changes). */
    struct HitChange { float onsetDb, tailDb; };

    HitChange shapeHit (float punch, float sustain, double decaySeconds = 0.08)
    {
        P p;
        setParam (p, TransientIDs::punch, punch);
        setParam (p, TransientIDs::sustain, sustain);
        prepare (p, sr, 480);

        // A second of silence, then the hit (so the followers start from rest)
        juce::AudioBuffer<float> in (2, 2 * second);
        in.clear();
        const auto hit = singleHit (sr, 1.0, -6.0f, decaySeconds);
        for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, second, hit, ch, 0, second);

        const auto out = run (p, in, 480);
        const int onsetEnd = second + 240, tailStart = second + 7200, tailEnd = second + 14400;   // 0-5 ms; 150-300 ms
        return { toDb (peak (out, 0, second, onsetEnd)) - toDb (peak (in, 0, second, onsetEnd)),
                 toDb (rms (out, 0, tailStart, tailEnd)) - toDb (rms (in, 0, tailStart, tailEnd)) };
    }

    //==========================================================================
    void testParameters()
    {
        std::cout << "\n[parameters]\n";
        P p;
        check (p.getParameters().size() == 6, "6 settings visible to the DAW (4 knobs, BYPASS, AUTO-GAIN)");
        check (p.getBypassParameter() == p.parameters.getParameter (CommonIDs::bypass), "BYPASS is the DAW's own bypass switch");
        check (getParam (p, TransientIDs::punch) == 0.0f && getParam (p, TransientIDs::sustain) == 0.0f
                 && getParam (p, TransientIDs::output) == 0.0f && getParam (p, TransientIDs::mix) == 100.0f,
               "defaults: PUNCH 0%, SUSTAIN 0%, OUTPUT 0 dB, MIX 100%");

        auto text = [&] (const char* id, float v) { auto* prm = p.parameters.getParameter (id); return prm->getText (prm->convertTo0to1 (v), 0); };
        check (text (TransientIDs::punch, 40.0f) == "+40%" && text (TransientIDs::punch, -35.0f) == "-35%" && text (TransientIDs::punch, 0.0f) == "0%",
               "PUNCH reads '+40%', '-35%', '0%'");
        check (text (TransientIDs::output, 3.0f) == "+3.0 dB" && text (TransientIDs::output, -6.0f) == "-6.0 dB", "OUTPUT reads '+3.0 dB', '-6.0 dB'");
        auto* punch = p.parameters.getParameter (TransientIDs::punch);
        check (near (punch->convertFrom0to1 (0.5f), 0.0f, 1e-4f), "the knob's middle is exactly 0%");
        check (p.getLatencySamples() == 0, "zero latency");

        // The new name (it was Easy Transient), without losing anyone's own presets
        check (p.getName() == "Easy Trans", "your DAW lists it as Easy Trans");
        const auto folder = juce::File::createTempFile ("easy-transient-presets");
        folder.createDirectory();
        folder.getChildFile ("My Snare.xml").replaceWithText ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                                             "<EasyPreset plugin=\"Easy Transient\" name=\"My Snare\"><PARAM id=\"punch\" value=\"42\"/></EasyPreset>\n");
        PresetBrowser browser;
        browser.setPresets (p.getPresetFolderName(), {});
        browser.setUserPresetFolder (folder);
        const auto mine = browser.readUserPresets();
        check (p.getPresetFolderName() == "Easy Transient" && mine.size() == 1 && mine[0].name == "My Snare" && near (mine[0].values[0].second, 42.0f, 1e-4f),
               "presets you saved with Easy Transient are still found (they stay in Documents > Easy Plugins > Easy Transient)");
        folder.deleteRecursively();
    }

    void testShaping()
    {
        std::cout << "\n[PUNCH and SUSTAIN]\n";
        {
            P p; prepare (p, sr, 512);
            const auto loop = drumLoop (sr, 4.0);
            check (maxDifference (loop, run (p, loop, 512)) == 0.0f, "both knobs in the middle: the sound comes out bit for bit untouched");
        }

        const auto morePunch = shapeHit (100.0f, 0.0f), lessPunch = shapeHit (-100.0f, 0.0f);
        check (morePunch.onsetDb > 3.0f && std::abs (morePunch.tailDb) < 1.0f,
               "PUNCH +100%: the start of the hit gets louder (+" + juce::String (morePunch.onsetDb, 1) + " dB), the tail barely changes ("
                 + juce::String (morePunch.tailDb, 2) + " dB)");
        check (lessPunch.onsetDb < -3.0f && std::abs (lessPunch.tailDb) < 1.0f,
               "PUNCH -100%: the start gets softer (" + juce::String (lessPunch.onsetDb, 1) + " dB), the tail barely changes ("
                 + juce::String (lessPunch.tailDb, 2) + " dB)");

        const auto moreTail = shapeHit (0.0f, 100.0f), lessTail = shapeHit (0.0f, -100.0f);
        check (moreTail.tailDb > 3.0f && std::abs (moreTail.onsetDb) < 1.0f,
               "SUSTAIN +100%: the tail gets louder (+" + juce::String (moreTail.tailDb, 1) + " dB), the start barely changes ("
                 + juce::String (moreTail.onsetDb, 2) + " dB)");
        check (lessTail.tailDb < -3.0f && std::abs (lessTail.onsetDb) < 1.0f,
               "SUSTAIN -100%: the tail gets shorter (" + juce::String (lessTail.tailDb, 1) + " dB), the start barely changes ("
                 + juce::String (lessTail.onsetDb, 2) + " dB)");

        const auto halfPunch = shapeHit (50.0f, 0.0f);
        check (halfPunch.onsetDb > 0.5f && halfPunch.onsetDb < morePunch.onsetDb, "PUNCH +50% does less than +100% (+"
                                                                                     + juce::String (halfPunch.onsetDb, 1) + " dB)");

        // A steady note has no hits, so there's nothing to shape - and it mustn't
        // be wobbled cycle by cycle (that would sound like distortion on bass)
        for (double freq : { 40.0, 110.0, 1000.0 })
        {
            P p; setParam (p, TransientIDs::punch, 100.0f); setParam (p, TransientIDs::sustain, 100.0f);
            prepare (p, sr, 512);
            const auto in = sine (sr, freq, fromDb (-6.0f), 2.0);
            const auto out = run (p, in, 512);
            const float change = toDb (rms (out, 0, second, 2 * second)) - toDb (rms (in, 0, second, 2 * second));
            float wobble = 0.0f;
            for (int i = second; i < 2 * second; ++i)
                if (std::abs (in.getSample (0, i)) > 0.05f)
                    wobble = std::max (wobble, std::abs (toDb (std::abs (out.getSample (0, i) / in.getSample (0, i)))));
            check (std::abs (change) < 0.1f && wobble < 0.1f, "a steady " + juce::String ((int) freq) + " Hz note is left alone at +100% PUNCH and SUSTAIN (level "
                                                                + juce::String (change, 3) + " dB, wobble " + juce::String (wobble, 3) + " dB)");
        }

        // Background noise between hits isn't pumped up
        {
            P p; setParam (p, TransientIDs::sustain, 100.0f); setParam (p, TransientIDs::punch, 100.0f);
            prepare (p, sr, 512);
            juce::AudioBuffer<float> hiss (2, 2 * second);
            juce::Random rng (5);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < hiss.getNumSamples(); ++i)
                    hiss.setSample (ch, i, fromDb (-75.0f) * (rng.nextFloat() * 2.0f - 1.0f));
            const auto out = run (p, hiss, 512);
            const float change = toDb (rms (out, 0, second / 2, 2 * second)) - toDb (rms (hiss, 0, second / 2, 2 * second));
            check (std::abs (change) < 0.5f, "quiet background hiss isn't pumped up (" + juce::String (change, 2) + " dB)");
        }

        // Stereo-linked, mono works, buffer size and sample rate don't matter
        {
            P p; setParam (p, TransientIDs::punch, 80.0f); setParam (p, TransientIDs::sustain, -60.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 2.0);
            in.applyGain (1, 0, in.getNumSamples(), 0.2f);
            const auto out = run (p, in, 512);
            float worst = 0.0f;
            for (int i = 0; i < in.getNumSamples(); ++i)
                if (std::abs (in.getSample (1, i)) > 1e-3f)
                    worst = std::max (worst, std::abs (out.getSample (0, i) / in.getSample (0, i) - out.getSample (1, i) / in.getSample (1, i)));
            check (worst < 1e-3f, "both channels get exactly the same change (worst mismatch " + juce::String (worst) + ")");
        }
        {
            P a; setParam (a, TransientIDs::punch, 70.0f); setParam (a, TransientIDs::sustain, 40.0f); prepare (a, sr, 1024);
            P b; setParam (b, TransientIDs::punch, 70.0f); setParam (b, TransientIDs::sustain, 40.0f); prepare (b, sr, 1024);
            const auto loop = drumLoop (sr, 1.0);
            const auto outA = run (a, loop, 1024);
            juce::AudioBuffer<float> outB (loop);
            juce::MidiBuffer midi;
            for (int start = 0, size = 1; start < outB.getNumSamples(); start += size, size = (size * 7) % 997 + 1)
            {
                const int n = std::min (size, outB.getNumSamples() - start);
                juce::AudioBuffer<float> view (outB.getArrayOfWritePointers(), 2, start, n);
                b.processBlock (view, midi);
            }
            check (maxDifference (outA, outB) < 1e-5f, "the result doesn't depend on the DAW's buffer size (max diff " + juce::String (maxDifference (outA, outB)) + ")");
        }
        {
            float reference = 0.0f;
            bool same = true;
            juce::String values;
            for (double rate : { 48000.0, 44100.0, 96000.0, 192000.0 })
            {
                P p; setParam (p, TransientIDs::punch, 100.0f); prepare (p, rate, 512);
                juce::AudioBuffer<float> in (2, (int) (rate * 1.5));
                in.clear();
                const auto hit = singleHit (rate, 0.5, -6.0f, 0.08);
                for (int ch = 0; ch < 2; ++ch) in.copyFrom (ch, (int) rate, hit, ch, 0, hit.getNumSamples());
                const auto out = run (p, in, 512);
                const int onsetEnd = (int) rate + (int) (rate * 0.005);
                const float onset = toDb (peak (out, 0, (int) rate, onsetEnd)) - toDb (peak (in, 0, (int) rate, onsetEnd));
                if (reference == 0.0f) reference = onset;
                same = same && near (onset, reference, 1.0f);
                values << juce::String (onset, 1) << " ";
            }
            check (same, "PUNCH does the same at 44.1, 48, 96 and 192 kHz (+dB at the onset: " + values.trim() + ")");
        }
        {
            P p; setParam (p, TransientIDs::punch, 100.0f); setParam (p, TransientIDs::sustain, 100.0f); setParam (p, TransientIDs::output, 12.0f);
            prepare (p, 44100.0, 64);
            juce::AudioBuffer<float> nasty (2, 44100);
            juce::Random rng (42);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < nasty.getNumSamples(); ++i)
                    nasty.setSample (ch, i, i < 11025 ? (rng.nextFloat() * 2.0f - 1.0f) * 4.0f : (i < 22050 ? 0.0f : (i % 4410 == 0 ? 1.0f : 1.0e-38f)));
            const auto out = run (p, nasty, 64);
            check (allFinite (out) && peak (out, 0, 11025 + 4410, 22050) == 0.0f, "extreme settings and nasty input: no NaN/inf, and silence stays silent");
        }
    }

    void testBypassAndAutoGain()
    {
        std::cout << "\n[BYPASS and AUTO-GAIN]\n";
        const auto loop = drumLoop (sr, 12.0);
        {
            P p; loadPresetNamed (p, "Kick - More Click"); setParam (p, CommonIDs::autoGain, 1.0f); setParam (p, CommonIDs::bypass, 1.0f);
            prepare (p, sr, 512);
            check (maxDifference (loop, run (p, loop, 512)) == 0.0f, "bypassed: the original, bit for bit");
            check (p.takeBoostForMeter() == 0.0f && p.takeCutForMeter() == 0.0f, "bypassed: the meter reads 0");
        }

        auto matched = [&] (float punch, float sustain)
        {
            P p; setParam (p, TransientIDs::punch, punch); setParam (p, TransientIDs::sustain, sustain); setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 480);
            const auto out = run (p, loop, 480);
            return std::make_pair (loudnessDb (out, 6 * second, 12 * second) - loudnessDb (loop, 6 * second, 12 * second), p.getAutoGainDb());
        };

        for (auto [punch, sustain] : { std::pair<float, float> { 60.0f, 0.0f }, { 0.0f, 60.0f }, { -60.0f, 0.0f }, { 0.0f, -60.0f }, { 100.0f, 100.0f } })
        {
            const auto [diff, autoDb] = matched (punch, sustain);
            check (std::abs (diff) < 0.5, "PUNCH " + juce::String ((int) punch) + "%, SUSTAIN " + juce::String ((int) sustain)
                                            + "%: AUTO-GAIN matches the loudness to " + juce::String (diff, 2) + " dB (it's " + (autoDb < 0.0f ? "taking away " : "adding ")
                                            + juce::String (std::abs (autoDb), 1) + " dB)");
        }

        {
            P a, b;
            for (auto* p : { &a, &b }) { setParam (*p, TransientIDs::punch, 50.0f); setParam (*p, CommonIDs::autoGain, 1.0f); }
            setParam (b, TransientIDs::output, 12.0f);
            prepare (a, sr, 512); prepare (b, sr, 512);
            check (maxDifference (run (a, loop, 512), run (b, loop, 512)) == 0.0f, "with AUTO-GAIN on, the OUTPUT knob has no effect");
        }
        {
            P p; setParam (p, TransientIDs::punch, 100.0f); setParam (p, TransientIDs::mix, 0.0f); setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 512);
            check (maxDifference (loop, run (p, loop, 512)) == 0.0f, "MIX 0%: exactly the original, even with AUTO-GAIN on");
        }
        {
            P p; setParam (p, TransientIDs::output, -6.0f); prepare (p, sr, 512);
            const auto in = drumLoop (sr, 2.0);
            const auto out = run (p, in, 512);
            check (near (toDb (rms (out, 0, second, 2 * second)) - toDb (rms (in, 0, second, 2 * second)), -6.0f, 0.02f), "OUTPUT -6 dB turns it down 6 dB");
        }

        // Moving knobs and switching while playing: no clicks
        {
            const auto in = sine (sr, 300.0, fromDb (-6.0f), 3.0);
            P p; setParam (p, TransientIDs::punch, 50.0f); prepare (p, sr, 256);
            const auto out = run (p, in, 256, [&] (int start)
            {
                setParam (p, CommonIDs::bypass, (start / 12288) % 2 == 0 ? 0.0f : 1.0f);
                setParam (p, CommonIDs::autoGain, (start / 20480) % 2 == 0 ? 0.0f : 1.0f);
                setParam (p, TransientIDs::output, (start / 8192) % 2 == 0 ? 0.0f : 6.0f);
            });
            float worst = 0.0f, prevGain = -1.0f;
            for (int i = 4800; i < in.getNumSamples(); ++i)
            {
                const float x = in.getSample (0, i);
                if (std::abs (x) < 0.05f) { prevGain = -1.0f; continue; }
                const float gain = out.getSample (0, i) / x;
                if (prevGain > 0.0f) worst = std::max (worst, std::abs (gain - prevGain));
                prevGain = gain;
            }
            check (worst < 0.005f, "flicking BYPASS, AUTO-GAIN and OUTPUT while playing: no clicks (largest step " + juce::String (worst, 5) + ")");
        }
        {
            const auto drums = drumLoop (sr, 4.0);
            P fixed; setParam (fixed, TransientIDs::punch, 100.0f); setParam (fixed, TransientIDs::sustain, 100.0f); prepare (fixed, sr, 256);
            const auto fixedOut = run (fixed, drums, 256);
            P moving; prepare (moving, sr, 256);
            int blockCount = 0;
            const auto movingOut = run (moving, drums, 256, [&] (int)
            {
                const float phase = (float) (blockCount++ % 60) / 60.0f;
                const float sweep = -100.0f + 200.0f * (phase < 0.5f ? phase * 2.0f : 2.0f - phase * 2.0f);
                setParam (moving, TransientIDs::punch, sweep);
                setParam (moving, TransientIDs::sustain, -sweep);
            });
            float stepFixed = 0.0f, stepMoving = 0.0f;
            for (int i = 1; i < drums.getNumSamples(); ++i)
            {
                stepFixed  = std::max (stepFixed,  std::abs (fixedOut.getSample (0, i)  - fixedOut.getSample (0, i - 1)));
                stepMoving = std::max (stepMoving, std::abs (movingOut.getSample (0, i) - movingOut.getSample (0, i - 1)));
            }
            check (allFinite (movingOut) && stepMoving <= stepFixed * 1.2f, "sweeping PUNCH and SUSTAIN end to end while drums play: no jumps beyond what the shaping itself does");
        }
    }

    void testDisplayAndState()
    {
        std::cout << "\n[display data, saving and presets]\n";
        {
            P p; prepare (p, sr, 512);
            run (p, sine (sr, 1000.0, fromDb (-6.0f), 1.0), 512);
            std::vector<P::Moment> moments (1000);
            const int count = p.takeMoments (moments.data(), (int) moments.size());
            check (count == 200, "one second of sound = 200 moments of 5 ms for the display (got " + juce::String (count) + ")");
            check (count > 10 && near (moments[(size_t) count - 1].inDb, -6.0f, 0.1f) && near (moments[(size_t) count - 1].outDb, -6.0f, 0.1f),
                   "a steady -6 dB tone shows as -6 dB in and out");
            check (p.takeMoments (moments.data(), 1000) == 0, "moments are only handed over once");
        }
        {
            P p; setParam (p, TransientIDs::punch, 100.0f); prepare (p, sr, 512);
            run (p, drumLoop (sr, 1.0), 512);
            std::vector<P::Moment> moments (1000);
            const int count = p.takeMoments (moments.data(), 1000);
            bool louderSomewhere = false;
            for (int i = 0; i < count; ++i) louderSomewhere = louderSomewhere || moments[(size_t) i].outDb > moments[(size_t) i].inDb + 2.0f;
            check (louderSomewhere, "with PUNCH up, the display shows the hits coming out louder");
        }
        {
            // The display's scale follows the level, so the peaks never run off the top
            MomentDisplay display;
            display.setSize (600, 300);
            const auto feed = [&] (float peakDb, int updates)   // ~30 updates a second, 7 moments each
            {
                std::vector<P::Moment> moments (7);
                for (int u = 0; u < updates; ++u)
                {
                    for (int i = 0; i < 7; ++i)
                    {
                        const float level = (u * 7 + i) % 100 == 0 ? peakDb : peakDb - 30.0f;   // a hit every half second
                        moments[(size_t) i] = { level, level };
                    }
                    display.addMoments (moments.data(), 7);
                }
            };
            feed (-1.0f, 120);                            // a loud mix bus: peaks at -1 dB
            const float loud = display.getTopDb() - -1.0f;
            feed (-30.0f, 150);                           // a quiet track
            const float quiet = display.getTopDb() - -30.0f;
            feed (5.0f, 15);                              // PUNCH pushes the peaks over 0 dB
            const float boosted = display.getTopDb() - 5.0f;
            check (loud >= 12.0f && loud <= 21.0f && quiet >= 12.0f && quiet <= 21.0f && boosted >= 12.0f,
                   "the display's scale follows the level: the peaks sit " + juce::String (loud, 0) + " dB below the top on a loud mix, "
                     + juce::String (quiet, 0) + " dB on a quiet track, and half a second after PUNCH lifts them over 0 dB, "
                     + juce::String (boosted, 0) + " dB");
        }
        {
            P a;
            setParam (a, TransientIDs::punch, 33.0f); setParam (a, TransientIDs::sustain, -44.0f); setParam (a, TransientIDs::output, 2.5f);
            setParam (a, TransientIDs::mix, 75.0f); setParam (a, CommonIDs::autoGain, 1.0f);
            loadPresetNamed (a, "Snare - Less Ring");
            a.markPresetEdited();
            setParam (a, TransientIDs::mix, 80.0f);
            juce::MemoryBlock state;
            a.getStateInformation (state);
            P b;
            b.setStateInformation (state.getData(), (int) state.getSize());
            check (near (getParam (b, TransientIDs::sustain), -35.0f, 1e-3f) && near (getParam (b, TransientIDs::mix), 80.0f, 1e-3f)
                     && getParam (b, CommonIDs::autoGain) > 0.5f && b.getPresetName() == "Snare - Less Ring" && b.isPresetEdited(),
                   "settings and 'Snare - Less Ring (edited)' survive saving and reopening the project");
        }
        {
            P p;
            bool allOk = std::size (transientPresets) == p.getBuiltInPresets().size();
            for (auto& preset : transientPresets)
            {
                loadPresetNamed (p, preset.name);
                allOk = allOk && near (getParam (p, TransientIDs::punch), preset.punchPercent, 0.51f)
                              && near (getParam (p, TransientIDs::sustain), preset.sustainPercent, 0.51f)
                              && near (getParam (p, TransientIDs::output), preset.outputDb, 0.051f)
                              && near (getParam (p, TransientIDs::mix), preset.mixPercent, 0.51f)
                              && p.getPresetName() == preset.name;
            }
            check (allOk, "all " + juce::String ((int) std::size (transientPresets)) + " presets load exactly");
            const auto [textsOk, textsReport] = presetTextsReport (P().getBuiltInPresets());
            check (textsOk, textsReport);
        }
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyWindow*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 600, "window is 1100 x 600, with the shared header");
        if (window == nullptr) return;

        std::vector<juce::Slider*> sliders;
        MomentDisplay* display = nullptr;
        GainChangeMeter* meter = nullptr;
        for (auto* child : editor->getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child)) sliders.push_back (s);
            if (auto* d = dynamic_cast<MomentDisplay*> (child)) display = d;
            if (auto* m = dynamic_cast<GainChangeMeter*> (child)) meter = m;
        }
        check (sliders.size() == 4 && display != nullptr && meter != nullptr, "4 knobs, the display and the meter present");
        if (sliders.size() != 4 || display == nullptr || meter == nullptr) return;

        bool allInside = true, noOverlap = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds())) allInside = false;
            for (auto& other : boxes) if (other.intersects (child->getBounds())) noOverlap = false;
            boxes.push_back (child->getBounds());
        }
        check (allInside && noOverlap, "everything fits inside the window without overlapping");
        check (sliders[0]->getTextFromValue (sliders[0]->getValue()) == "0%" && sliders[2]->getTextFromValue (sliders[2]->getValue()) == "0.0 dB",
               "PUNCH reads '0%', OUTPUT '0.0 dB'");

        sliders[0]->setValue (35.0, juce::sendNotificationSync);
        check (near (getParam (p, TransientIDs::punch), 35.0f, 1e-3f), "turning PUNCH changes the setting");

        auto& browser = window->presetBrowser;
        const auto presetFolder = juce::File::createTempFile ("easy-transient-presets");
        browser.setUserPresetFolder (presetFolder);
        const auto menu = browser.buildMenu();
        int snareId = 0;
        bool underDrums = false;
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text == "Drums" && it.getItem().subMenu != nullptr)
                for (juce::PopupMenu::MenuItemIterator sub (*it.getItem().subMenu); sub.next();)
                    if (sub.getItem().text == "Snare - More Crack") { snareId = sub.getItem().itemID; underDrums = true; }
        check (underDrums, "the preset menu has its own Drums sub-menu");
        browser.handleMenuResult (snareId);
        check (near (getParam (p, TransientIDs::punch), 35.0f, 0.01f) && browser.getDisplayedText() == "Snare - More Crack", "choosing 'Snare - More Crack' loads it");
        editor->mouseExit (hoverEvent (editor.get()));
        check (window->hintBar.getTitle() == "Snare - More Crack", "the help bar shows its tip");
        editor->mouseEnter (hoverEvent (sliders[1]));
        check (window->hintBar.getTitle() == "SUSTAIN", "hovering SUSTAIN explains it");
        editor->mouseEnter (hoverEvent (display));
        check (window->hintBar.getTitle() == "WHAT IT'S DOING", "hovering the display explains it");
        presetFolder.deleteRecursively();

        {
            P q; setParam (q, TransientIDs::punch, 100.0f); setParam (q, TransientIDs::sustain, 60.0f); setParam (q, CommonIDs::autoGain, 1.0f);
            prepare (q, sr, 480);
            run (q, drumLoop (sr, 8.0), 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            juce::Slider* output = nullptr;
            int index = 0;
            for (auto* child : ed->getChildren())
                if (auto* s = dynamic_cast<juce::Slider*> (child))
                    if (index++ == 2) output = s;
            const auto text = output != nullptr ? output->getTextFromValue (output->getValue()) : juce::String();
            check (output != nullptr && ! output->isEnabled() && text.startsWith ("Auto -"), "with loud PUNCH and AUTO-GAIN on, OUTPUT is faded and shows '" + text + "'");
        }

        // Renders: play some drums first, so the display has something to show
        auto render = [&] (const char* fileName, const juce::String& presetName, bool autoGain, bool bypass, int hoverIndex,
                           float levelDb = 0.0f)
        {
            P q; prepare (q, sr, 480);
            if (presetName.isNotEmpty()) loadPresetNamed (q, presetName);
            setParam (q, CommonIDs::autoGain, autoGain ? 1.0f : 0.0f);
            setParam (q, CommonIDs::bypass, bypass ? 1.0f : 0.0f);
            const auto drums = [&] (double seconds) { auto b = drumLoop (sr, seconds); b.applyGain (fromDb (levelDb)); return b; };
            run (q, drums (4.0), 480);
            std::vector<P::Moment> discard (1024);
            while (q.takeMoments (discard.data(), 1024) > 0) {}
            run (q, drums (2.4), 480);
            q.takeBoostForMeter(); q.takeCutForMeter();
            run (q, drums (0.012), 480);   // the start of a kick, for the meter
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            std::vector<juce::Component*> parts;
            for (auto* child : ed->getChildren()) parts.push_back (child);
            if (hoverIndex >= 0)
            {
                int sliderNumber = 0;
                for (auto* child : ed->getChildren())
                    if (dynamic_cast<juce::Slider*> (child) != nullptr && sliderNumber++ == hoverIndex)
                        ed->mouseEnter (hoverEvent (child));
            }
            savePng (*ed, outDir.getChildFile (fileName));
        };

        render ("transient_default.png", "", false, false, -1);
        render ("transient_snare_crack.png", "Snare - More Crack", false, false, 0);
        render ("transient_kick_tighter.png", "Kick - Tighter", true, false, 1);
        render ("transient_bypassed.png", "Room Mics - Bigger Room", false, true, -1);
        render ("transient_loud_bus.png", "Drum Bus - Punchier", false, false, -1, 5.5f);   // peaks at about -0.5 dB, pushed over 0
        render ("transient_quiet.png", "Snare - More Crack", false, false, -1, -24.0f);     // a quiet track
    }
}

namespace
{
    void testEfficiency()
    {
        std::cout << "\n[efficiency: skipping through silence, speed]\n";

        const auto gapped = withGaps (drumLoop (sr, 6.0));

        struct Case { const char* name; TransientShaper::Settings settings; };
        std::vector<Case> cases;
        { TransientShaper::Settings s; s.punch = 0.6f; s.sustain = 0.5f; cases.push_back ({ "more punch and sustain", s }); }
        { TransientShaper::Settings s; s.punch = -0.8f; s.sustain = -0.7f; s.mix = 0.7f; s.outputDb = 3.0f; s.autoGain = true;
          cases.push_back ({ "softer and tighter, MIX 70%, AUTO-GAIN", s }); }
        { TransientShaper::Settings s; cases.push_back ({ "knobs in the middle", s }); }

        for (auto& c : cases)
        {
            const auto r = restReport<TransientShaper> (gapped, sr, 256, c.settings);
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005 && r.blocksRested > 0,
                   juce::String ("skipping silence sounds the same as working through it (") + c.name + "): "
                   + juce::String (r.differenceDb, 1) + " dB apart, levels within " + juce::String (r.worstLevelDb, 6) + " dB; skipped "
                   + juce::String (r.blocksRested) + " of " + juce::String (r.blocks) + " blocks");
        }

        {
            auto settings = cases[1].settings;
            const auto r = restReport<TransientShaper> (gapped, sr, 256, settings, [] (int start, TransientShaper::Settings& s, bool& restart)
            {
                if (start >= 180000 && start < 180256) { s.punch = 0.9f; s.mix = 1.0f; }
                if (start >= 200000 && start < 200256) restart = true;
                if (start >= 230000 && start < 230256) { s.bypassed = true; s.autoGain = false; }
                if (start >= 250000 && start < 250256) s.bypassed = false;
            });
            check (r.differenceDb <= -110.0 && r.worstLevelDb <= 0.0005,
                   "...and with knobs moved and a preset loaded during the silence: " + juce::String (r.differenceDb, 1) + " dB apart");
        }

       #if JUCE_DEBUG
        check (true, "(the speed checks only run in an optimised build)");
       #else
        {
            P p;
            loadPresetNamed (p, "Drum Bus - Punchier");
            setParam (p, CommonIDs::autoGain, 1.0f);
            prepare (p, sr, 256);
            const double busy = coreShare (p, drumLoop (sr, 30.0));
            check (busy < 0.40, "drum bus, AUTO-GAIN on: " + juce::String (busy, 3) + "% of one core (budget 0.40%)");

            P q;
            loadPresetNamed (q, "Drum Bus - Punchier");
            prepare (q, sr, 256);
            juce::AudioBuffer<float> silence (2, 48000 * 30);
            silence.clear();
            const double idle = coreShare (q, silence);
            check (idle < 0.03, "digital silence: " + juce::String (idle, 4) + "% of one core (budget 0.03%)");
        }
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testParameters();
    testShaping();
    testBypassAndAutoGain();
    testDisplayAndState();
    testEfficiency();
    testWindow (outDir);
    return finish();
}

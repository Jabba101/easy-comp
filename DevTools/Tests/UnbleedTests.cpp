// Easy Unbleed: the ghost cleaner and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_audio_formats/juce_audio_formats.h>

using namespace th;
using P = EasyUnbleedAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;
    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    bool prepareUnbleed (P& p, double rate, int block, int sidechainChannels = 2)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (sidechainChannels == 0 ? juce::AudioChannelSet::disabled() : juce::AudioChannelSet::canonicalChannelSet (sidechainChannels));
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        const bool ok = p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);
        return ok;
    }

    /** The stem through the plugin, the other stems in its sidechain, as a DAW does it. */
    juce::AudioBuffer<float> runUnbleed (P& p, const juce::AudioBuffer<float>& stem, const juce::AudioBuffer<float>* others, int block = 512)
    {
        const int n = stem.getNumSamples();
        juce::AudioBuffer<float> work (4, n);
        work.clear();
        for (int c = 0; c < 2; ++c)
        {
            work.copyFrom (c, 0, stem, std::min (c, stem.getNumChannels() - 1), 0, n);
            if (others != nullptr) work.copyFrom (c + 2, 0, *others, std::min (c, others->getNumChannels() - 1), 0, n);
        }
        juce::MidiBuffer midi;
        for (int start = 0; start < n; start += block)
        {
            juce::AudioBuffer<float> view (work.getArrayOfWritePointers(), 4, start, std::min (block, n - start));
            p.processBlock (view, midi);
        }
        juce::AudioBuffer<float> out (2, n);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, work, c, 0, n);
        return out;
    }

    juce::String stemFolder = "unbleed";
    juce::AudioBuffer<float> load (const juce::String& name)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (juce::File ("/home/claude/presetcheck/" + stemFolder + "/" + name + ".wav").createInputStream().release(), true));
        if (r == nullptr) { std::cout << "  (couldn't read " << name << ")\n"; return {}; }
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        return b;
    }

    float worstDifferenceDelayed (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, int delay)
    {
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = delay; i < out.getNumSamples(); ++i)
                worst = std::max (worst, std::abs (out.getSample (c, i) - in.getSample (c, i - delay)));
        return worst;
    }

    //==========================================================================
    void testBasics()
    {
        std::cout << "\n[the basics]\n";
        {
            P p;
            check (p.getBusCount (true) == 2 && p.getBus (true, 1)->getName() == "Sidechain", "two inputs: the stem, and the sidechain for the other stems");
            bool layouts = true;
            for (int side : { 0, 1, 2 }) { P q; layouts = layouts && prepareUnbleed (q, sr, 512, side); }
            check (layouts, "the sidechain can be mono, stereo or switched off");
        }
        {
            // The display's level scale: a sine peaking at -6 dBFS should read about -6 dB
            SpectralUnbleed u; u.prepare (sr, {});
            juce::AudioBuffer<float> t (2, 48000), side (2, 48000); side.clear();
            for (int i = 0; i < 48000; ++i) for (int c = 0; c < 2; ++c) t.setSample (c, i, 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / sr));
            for (int st = 0; st < 48000; st += 512)
            {
                juce::AudioBuffer<float> a (t.getArrayOfWritePointers(), 2, st, 512), b (side.getArrayOfWritePointers(), 2, st, 512);
                u.process (a, &b);
            }
            float loudest = -200.0f;
            for (int b = 0; b < u.getNumBands(); ++b) loudest = std::max (loudest, u.getBandLevelDb (b, false));
            check (std::abs (loudest + 6.0f) < 2.0f, "the display reads true levels: a -6 dB tone shows as " + juce::String (loudest, 1) + " dB");
        }
        juce::String lat;
        bool latOk = true;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            P q; prepareUnbleed (q, rate, 512);
            lat << q.getLatencySamples() << " ";
            latOk = latOk && q.getLatencySamples() > rate * 0.03 && q.getLatencySamples() < rate * 0.06;
        }
        check (latOk, "tells your DAW its delay (one analysis slice, ~43 ms): " + lat.trim() + " samples at 44.1, 48, 96 kHz");

        const auto stem = drumLoop (sr, 3.0);
        {
            P p; prepareUnbleed (p, sr, 512);
            const auto out = runUnbleed (p, stem, nullptr);
            const float diff = worstDifferenceDelayed (out, stem, p.getLatencySamples());
            check (diff < 1.0e-4f, "nothing in the sidechain: the stem passes untouched (just delayed; " + juce::String (toDb (diff), 0) + " dB)");
        }
        {
            P p; set (p, CommonIDs::bypass, 1.0f); prepareUnbleed (p, sr, 512);
            const auto others = drumLoop (sr, 3.0, -2.0f, -2.0f, -10.0f);
            check (worstDifferenceDelayed (runUnbleed (p, stem, &others), stem, p.getLatencySamples()) == 0.0f, "BYPASS: exactly the stem (still delayed, so nothing jumps in time)");
        }
        {
            // A copy of the stem sent to its own sidechain must never cancel it
            P p; set (p, UnbleedIDs::amount, 40.0f); set (p, UnbleedIDs::sensitivity, 100.0f); prepareUnbleed (p, sr, 512);
            const auto out = runUnbleed (p, stem, &stem);
            const float diff = worstDifferenceDelayed (out, stem, p.getLatencySamples());
            check (p.isSidechainACopy() && rms (out, 0, (int) sr, out.getNumSamples()) > rms (stem, 0, (int) sr, stem.getNumSamples()) * 0.97f,
                   "a copy of the stem in its own sidechain: noticed and ignored - the stem is safe (" + juce::String (toDb (diff), 0) + " dB difference)");
        }
    }

    //==========================================================================
    /** The simulated UVR-style stems: what's left of the ghost in the gaps, and how much of the real part is lost. */
    struct Score { double ghostDb = 0.0, damageDb = 0.0, sdrBefore = 0.0, sdrAfter = 0.0; };

    Score score (const juce::String& stem, const std::function<void (P&)>& setUp)
    {
        const char* names[] { "vocals", "drums", "bass", "other" };
        const auto est = load (stem + "_est"), tru = load (stem + "_true");
        if (est.getNumSamples() == 0) return {};
        const int n = est.getNumSamples();
        juce::AudioBuffer<float> others (2, n); others.clear();
        for (auto* name : names)
            if (stem != name) { const auto o = load (juce::String (name) + "_est"); for (int c = 0; c < 2; ++c) others.addFrom (c, 0, o, c, 0, n); }

        P p; prepareUnbleed (p, est.getNumSamples() > 0 && stemFolder == "real" ? 44100.0 : sr, 512); setUp (p);
        const auto out = runUnbleed (p, est, &others);
        // The same gains on the true part alone (what it does to the real sound): the bleed and the true part, each
        // through an identical copy that hears the estimate - done with a twin that's fed (estimate + true) as 4 channels
        // isn't possible through the plugin, so measure damage on the frames where the true part is playing instead.
        const int d = p.getLatencySamples();
        double t2 = 0, e0 = 0, e1 = 0, gapB = 0, gapA = 0, playB = 0, playA = 0, env = 0, peak = 0;
        std::vector<double> envs ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double v = 0.5 * (tru.getSample (0, i) * tru.getSample (0, i) + tru.getSample (1, i) * tru.getSample (1, i));
            env = 0.999 * env + 0.001 * v; envs[(size_t) i] = std::sqrt (env); peak = std::max (peak, envs[(size_t) i]);
        }
        for (int i = 0; i + d < n; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const double t = tru.getSample (c, i), e = est.getSample (c, i), o = out.getSample (c, i + d);
                t2 += t * t; e0 += (e - t) * (e - t); e1 += (o - t) * (o - t);
                if (envs[(size_t) i] < peak * 0.0178) { gapB += (e - t) * (e - t); gapA += (o - t) * (o - t); }   // (the true part silent: all that's there is ghost)
                else if (envs[(size_t) i] > peak * 0.1) { playB += e * e; playA += o * o; }                      // (the part playing well: should be untouched)
            }
        Score s;
        s.ghostDb = 10.0 * std::log10 ((gapA + 1e-20) / (gapB + 1e-20));
        s.damageDb = 10.0 * std::log10 (playA / playB);
        s.sdrBefore = 10.0 * std::log10 (t2 / e0);
        s.sdrAfter = 10.0 * std::log10 (t2 / e1);
        return s;
    }

    void testStems()
    {
        std::cout << "\n[simulated UVR-style stems: vocal pop, rock drums, fingered bass, distorted guitar]\n";
        const auto defaults = [] (P&) {};
        const auto v = score ("vocals", defaults), dr = score ("drums", defaults), o = score ("other", defaults), b = score ("bass", defaults);
        check (v.ghostDb < -3.0, "vocals: the ghost in the pauses down " + juce::String (-v.ghostDb, 1) + " dB (default settings)");
        check (dr.ghostDb < -1.0 && o.ghostDb < -1.0, "drums and guitars: the ghost in their gaps down " + juce::String (-dr.ghostDb, 1) + " / " + juce::String (-o.ghostDb, 1) + " dB");
        check (std::abs (v.damageDb) < 1.0 && std::abs (dr.damageDb) < 1.0 && std::abs (o.damageDb) < 1.0 && std::abs (b.damageDb) < 1.0,
               "while the parts play, they keep their level (vocals " + juce::String (v.damageDb, 2) + ", drums " + juce::String (dr.damageDb, 2) + ", bass "
               + juce::String (b.damageDb, 2) + ", guitars " + juce::String (o.damageDb, 2) + " dB)");
        check (v.sdrAfter > v.sdrBefore - 2.0 && dr.sdrAfter > dr.sdrBefore - 1.0, "overall accuracy costs under 2 dB on these made-up stems (vocals " + juce::String (v.sdrBefore, 2) + " -> "
               + juce::String (v.sdrAfter, 2) + ", drums " + juce::String (dr.sdrBefore, 2) + " -> " + juce::String (dr.sdrAfter, 2) + " dB)");

        const auto top = score ("vocals", [] (P& p) { set (p, UnbleedIDs::from, 3000.0f); });
        check (top.ghostDb > v.ghostDb, "FROM 3 kHz: only the top end is cleaned (" + juce::String (-top.ghostDb, 1) + " dB, less than everywhere)");

        // REMOVED ONLY: what's taken out, and the cleaned stem, add back up to the stem
        {
            const auto est = load ("vocals_est");
            juce::AudioBuffer<float> others (2, est.getNumSamples()); others.clear();
            for (auto* name : { "drums", "bass", "other" }) { const auto x = load (juce::String (name) + "_est"); for (int c = 0; c < 2; ++c) others.addFrom (c, 0, x, c, 0, x.getNumSamples()); }
            P a; prepareUnbleed (a, sr, 512);
            P l; set (l, UnbleedIDs::listen, 1.0f); prepareUnbleed (l, sr, 512);
            const auto clean = runUnbleed (a, est, &others), removed = runUnbleed (l, est, &others);
            juce::AudioBuffer<float> sum (clean);
            for (int c = 0; c < 2; ++c) sum.addFrom (c, 0, removed, c, 0, sum.getNumSamples());
            const float diff = worstDifferenceDelayed (sum, est, a.getLatencySamples());
            check (diff < 1.0e-3f && rms (removed, 0, 0, removed.getNumSamples()) > 1.0e-4f, "REMOVED ONLY: exactly what it takes out (cleaned + removed = the stem)");
        }
    }

    /** The user's own song: the original multitracks against UVR5's split (Kim Vocal 2 vocals, htdemucs
        drums / bass / other) - 44.1 kHz, 52 s, the vocal sings for 22 s then a long instrumental. */
    void testRealStems()
    {
        std::cout << "\n[your song: UVR5 stems against the original multitracks]\n";
        stemFolder = "real";
        if (load ("vocals_est").getNumSamples() == 0) { stemFolder = "unbleed"; return; }
        const auto v = score ("vocals", [] (P&) {});
        check (v.sdrAfter > v.sdrBefore + 1.0 && v.ghostDb < -2.0, "the vocal (Kim Vocal 2): accuracy " + juce::String (v.sdrBefore, 2) + " -> " + juce::String (v.sdrAfter, 2)
               + " dB, the ghost in the gaps down " + juce::String (-v.ghostDb, 1) + " dB (default settings)");
        check (std::abs (v.damageDb) < 1.0, "while singing, the vocal keeps its level (" + juce::String (v.damageDb, 2) + " dB)");
        const auto strong = score ("vocals", [] (P& p) { loadPresetNamed (p, "Vocal - Strong Cleanup"); });
        check (strong.ghostDb < v.ghostDb - 2.0 && strong.sdrAfter > v.sdrAfter, "'Vocal - Strong Cleanup' goes further: the ghost down " + juce::String (-strong.ghostDb, 1)
               + " dB, accuracy " + juce::String (strong.sdrAfter, 2) + " dB");
        bool safe = true; juce::String levels;
        for (auto* name : { "drums", "bass", "other" })
        {
            const auto x = score (name, [] (P&) {});
            safe = safe && x.sdrAfter > x.sdrBefore - 0.3;
            levels << name << " " << juce::String (x.sdrAfter - x.sdrBefore, 2) << "  ";
        }
        check (safe, "parts that never stop (drums, bass, other) are left essentially alone (accuracy change: " + levels.trim() + " dB)");

        // An AI song: Mureka's own stems as the answer sheet, the same UVR5 split; 155 s
        std::cout << "\n[a Mureka song: UVR5 stems against Mureka's own stems]\n";
        stemFolder = "mureka";
        if (load ("vocals_est").getNumSamples() > 0)
        {
            const auto m = score ("vocals", [] (P&) {});
            check (m.ghostDb < -10.0 && m.sdrAfter > m.sdrBefore - 0.5, "the vocal: the ghost between phrases down " + juce::String (-m.ghostDb, 1) + " dB, accuracy "
                   + juce::String (m.sdrBefore, 2) + " -> " + juce::String (m.sdrAfter, 2) + " dB (default settings)");
            check (std::abs (m.damageDb) < 1.0, "while singing, the vocal keeps its level (" + juce::String (m.damageDb, 2) + " dB)");
        }
        stemFolder = "unbleed";
    }

    //==========================================================================
    void testParts (const juce::File& outDir)
    {
        std::cout << "\n[saving, presets, window]\n";
        {
            P p; juce::MemoryBlock state;
            set (p, UnbleedIDs::amount, 33.0f); set (p, UnbleedIDs::from, 2500.0f);
            p.getStateInformation (state);
            P r; r.setStateInformation (state.getData(), (int) state.getSize());
            check (getParam (r, UnbleedIDs::amount) == 33.0f && getParam (r, UnbleedIDs::from) == 2500.0f, "saved with the project and loaded back");
            const auto report = presetTextsReport (p.getBuiltInPresets());
            check (report.first, report.second);
        }

        const char* names[] { "drums", "bass", "other" };
        const auto est = load ("vocals_est");
        juce::AudioBuffer<float> others (2, est.getNumSamples()); others.clear();
        for (auto* name : names) { const auto x = load (juce::String (name) + "_est"); for (int c = 0; c < 2; ++c) others.addFrom (c, 0, x, c, 0, x.getNumSamples()); }

        P p; prepareUnbleed (p, sr, 512);
        loadPresetNamed (p, "Vocal - Strong Cleanup");
        // (into a moment where the vocal pauses)
        juce::AudioBuffer<float> part (2, (int) (sr * 5.2)), partOthers (2, part.getNumSamples());
        for (int c = 0; c < 2; ++c) { part.copyFrom (c, 0, est, c, 0, part.getNumSamples()); partOthers.copyFrom (c, 0, others, c, 0, part.getNumSamples()); }
        runUnbleed (p, part, &partOthers);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyUnbleedAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 640, "window is 1100 x 640, with the shared header");
        if (window == nullptr) return;
        for (int i = 0; i < 6; ++i) window->updateDisplaysNow();

        bool fits = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds())) fits = false;
            for (auto& other : boxes) if (other.intersects (child->getBounds())) fits = false;
            boxes.push_back (child->getBounds());
        }
        check (fits, "everything fits inside the window without overlapping");
        savePng (*editor, outDir.getChildFile ("unbleed.png"));

        {
            P q; prepareUnbleed (q, sr, 512);
            runUnbleed (q, drumLoop (sr, 2.5), nullptr);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto* w = dynamic_cast<EasyUnbleedAudioProcessorEditor*> (ed.get());
            w->updateDisplaysNow();
            check (w->getStatusText().contains ("sidechain"), "no sidechain: the window says to send the other stems (\"" + w->getStatusText() + "\")");
        }

        window->getInitAllButton().onClick();
        check (p.getPresetName() == "Default settings", "INIT: back to 'Default settings'");
        bool hints = true;
        for (int k = 0; k < EasyUnbleedAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hints = false;
        }
        check (hints, "every knob explains itself in the help bar");
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; prepareUnbleed (p, sr, 256);
        const auto stem = drumLoop (sr, 10.0), others = drumLoop (sr, 10.0, -4.0f, -4.0f, -12.0f);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        runUnbleed (p, stem, &others, 256);
        const double share = (juce::Time::getMillisecondCounterHiRes() - start) / 10000.0 * 100.0;
        check (share < 2.0, "stereo stem + stereo sidechain, 48 kHz: " + juce::String (share, 2) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    testBasics();
    testStems();
    testRealStems();
    testParts (outDir);
    testSpeed();
    return finish();
}

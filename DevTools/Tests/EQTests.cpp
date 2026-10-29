// Easy EQ: the EQ, MASTER, and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>

using namespace th;
using P = EasyEQAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    double amplitudeAt (const juce::AudioBuffer<float>& b, int ch, int start, int length, double freq)
    {
        double re = 0, im = 0, wsum = 0;
        for (int i = 0; i < length; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (length - 1));
            const double ph = juce::MathConstants<double>::twoPi * freq * i / sr;
            re += w * b.getSample (ch, start + i) * std::cos (ph);
            im -= w * b.getSample (ch, start + i) * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    // The gain (dB) the plugin gives a tone, measured
    double measuredDb (P& p, double hz, float left = 0.25f, float right = 0.25f)
    {
        const int n = (int) sr;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr);
            b.setSample (0, i, x * left); b.setSample (1, i, x * right);
        }
        p.reset();
        auto out = run (p, b, 512);
        const double inAmp = amplitudeAt (b, 0, 16000, 24000, hz);
        return 20.0 * std::log10 (amplitudeAt (out, 0, 16000, 24000, hz) / inAmp);
    }

    double heardDb (const juce::AudioBuffer<float>& b, int start)
    {
        double sum = 0;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = start; i < b.getNumSamples(); ++i)
                sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
        return 10.0 * std::log10 (sum / (b.getNumChannels() * (b.getNumSamples() - start)) + 1e-30);
    }

    juce::AudioBuffer<float> noise (double seconds, float level, int seed = 5)
    {
        juce::Random r (seed);
        juce::AudioBuffer<float> b (2, (int) (sr * seconds));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (ch, i, level * (r.nextFloat() * 2.0f - 1.0f));
        return b;
    }

    void waitForMaster (P& p)
    {
        for (int i = 0; i < 200 && ! p.isMasterUpToDate(); ++i)
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        // (the convolution then takes its own moment to swap the filter in)
        auto quiet = noise (0.3, 0.0f);
        run (p, quiet, 512);
        std::this_thread::sleep_for (std::chrono::milliseconds (300));
        run (p, quiet, 512);
    }

    //==========================================================================
    void testFlatAndFilters()
    {
        std::cout << "\n[flat, the bands, the filters]\n";
        P p; prepare (p, sr, 512);
        check (p.getLatencySamples() == 0, "no delay while MASTER is off");

        auto loop = drumLoop (sr, 2.0, -3.0f, -6.0f, -18.0f);
        auto out = run (p, loop, 512);
        check (maxDifference (loop, out) == 0.0f, "default settings: the original comes out exactly");

        // A band, measured against the curve the screen draws
        bool allMatch = true;
        juce::String report;
        struct Case { int band; float hz, db; bool shelf; double at; };
        for (auto c : { Case { 1, 300.0f, 6.0f, false, 300.0 }, Case { 2, 3000.0f, -9.0f, false, 3000.0 }, Case { 0, 100.0f, 9.0f, true, 40.0 },
                        Case { 3, 8000.0f, 6.0f, true, 15000.0 }, Case { 3, 12000.0f, 12.0f, false, 12000.0 } })
        {
            P q; prepare (q, sr, 512);
            set (q, EQIDs::freq[c.band], c.hz); set (q, EQIDs::gain[c.band], c.db);
            if (c.band == 0) set (q, EQIDs::lowShelf, c.shelf ? 1.0f : 0.0f);
            if (c.band == 3) set (q, EQIDs::highShelf, c.shelf ? 1.0f : 0.0f);
            prepare (q, sr, 512);
            const double measured = measuredDb (q, c.at);
            const double drawn = juce::Decibels::gainToDecibels (Equalizer::responseAt (q.getCurrentSettings(), c.at, sr, true));
            if (std::abs (measured - drawn) > 0.05) allMatch = false;
            report << juce::String (c.at, 0) << " Hz " << juce::String (measured, 2) << " dB; ";
        }
        check (allMatch, "every band does exactly what the screen draws: " + report);

        // Matched filters: a 12 kHz bell is the same shape as the analogue one (no cramping)
        const auto c = Equalizer::bell (12000.0, 12.0, 48000.0);
        const double q = Equalizer::qForGain (12.0), A = std::pow (10.0, 12.0 / 40.0);
        double worst = 0.0;
        for (double hz = 2000.0; hz <= 20000.0; hz *= 1.05)
        {
            const std::complex<double> s (0.0, hz / 12000.0);
            const double analogue = std::abs ((s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0));
            worst = std::max (worst, std::abs (20.0 * std::log10 (c.magnitudeAt (hz, 48000.0) / analogue)));
        }
        check (worst < 0.5, "a +12 dB bell at 12 kHz matches the analogue curve within " + juce::String (worst, 2) + " dB up to 20 kHz (no cramping)");

        // Proportional Q: narrower the harder it's pushed
        const auto widthOctaves = [] (double db)
        {
            Equalizer::Settings s; s.bands[1] = { 400.0f, (float) db, false, Equalizer::Target::stereo };
            double lo = 400.0, hi = 400.0;
            const double half = std::abs (db) * 0.5;
            while (std::abs (juce::Decibels::gainToDecibels (Equalizer::responseAt (s, lo, sr, true))) > half) lo /= 1.005;
            while (std::abs (juce::Decibels::gainToDecibels (Equalizer::responseAt (s, hi, sr, true))) > half) hi *= 1.005;
            return std::log2 (hi / lo);
        };
        const double w2 = widthOctaves (2.0), w12 = widthOctaves (12.0), wCut = widthOctaves (-12.0);
        check (w2 > 1.3 && w2 < 2.2 && w12 > 0.25 && w12 < 0.5 && std::abs (wCut - w12) < 0.01,
               "proportional Q: " + juce::String (w2, 2) + " octaves wide at +2 dB, " + juce::String (w12, 2) + " at +12 dB (cuts the same as boosts)");

        // The filters
        P h; set (h, EQIDs::hpf, 100.0f); set (h, EQIDs::hpfSlope, 1.0f); prepare (h, sr, 512);
        const double at50 = measuredDb (h, 50.0), at100 = measuredDb (h, 100.0), at400 = measuredDb (h, 400.0);
        check (std::abs (at50 + 24.1) < 0.6 && std::abs (at100 + 3.0) < 0.2 && std::abs (at400) < 0.05,
               "HPF 100 Hz, 24 dB/oct: " + juce::String (at50, 1) + " dB an octave below, " + juce::String (at100, 1) + " dB at 100 Hz, flat above");
        P l; set (l, EQIDs::lpf, 5000.0f); prepare (l, sr, 512);
        const double l10k = measuredDb (l, 10000.0), l5k = measuredDb (l, 5000.0);
        check (std::abs (l10k + 12.3) < 1.0 && std::abs (l5k + 3.0) < 0.2, "LPF 5 kHz, 12 dB/oct: " + juce::String (l5k, 1) + " dB at 5 kHz, " + juce::String (l10k, 1) + " dB an octave above");
    }

    //==========================================================================
    void testMidSide()
    {
        std::cout << "\n[mid-side, MONO BASS]\n";
        P p; set (p, EQIDs::freq[1], 500.0f); set (p, EQIDs::gain[1], 6.0f); set (p, EQIDs::target[1], 1.0f); prepare (p, sr, 512);
        const double midOnMid = measuredDb (p, 500.0, 0.25f, 0.25f), midOnSide = measuredDb (p, 500.0, 0.25f, -0.25f);
        check (std::abs (midOnMid - 6.0) < 0.05 && std::abs (midOnSide) < 0.05, "LOW MID on MID: the middle +6 dB (" + juce::String (midOnMid, 2) + "), the sides untouched (" + juce::String (midOnSide, 2) + ")");

        P s; set (s, EQIDs::freq[2], 3000.0f); set (s, EQIDs::gain[2], -6.0f); set (s, EQIDs::target[2], 2.0f); prepare (s, sr, 512);
        const double sideOnSide = measuredDb (s, 3000.0, 0.25f, -0.25f), sideOnMid = measuredDb (s, 3000.0, 0.25f, 0.25f);
        check (std::abs (sideOnSide + 6.0) < 0.05 && std::abs (sideOnMid) < 0.05, "HIGH MID on SIDE: the sides -6 dB, the middle untouched");

        P st; set (st, EQIDs::freq[2], 3000.0f); set (st, EQIDs::gain[2], -6.0f); prepare (st, sr, 512);
        check (std::abs (measuredDb (st, 3000.0, 0.25f, 0.0f) + 6.0) < 0.05, "ST: the same on everything (a sound only on the left gets -6 dB too)");

        P m; set (m, EQIDs::monoBass, 120.0f); prepare (m, sr, 512);
        const double sideLow = measuredDb (m, 40.0, 0.25f, -0.25f), midLow = measuredDb (m, 40.0, 0.25f, 0.25f), sideHigh = measuredDb (m, 1000.0, 0.25f, -0.25f);
        check (sideLow < -30.0 && std::abs (midLow) < 0.05 && std::abs (sideHigh) < 0.1,
               "MONO BASS 120 Hz: 40 Hz on the sides " + juce::String (sideLow, 1) + " dB (gone), in the middle untouched; the sides' 1 kHz untouched");

        // Mono track: SIDE does nothing, MID is everything
        P mono; mono.setPlayConfigDetails (1, 1, sr, 512); set (mono, EQIDs::gain[2], 6.0f); set (mono, EQIDs::target[2], 2.0f); mono.prepareToPlay (sr, 512);
        auto tone = th::sine (sr, 3000.0, 0.25f, 1.0, 1);
        auto mo = run (mono, tone, 512);
        check (std::abs (20 * std::log10 (amplitudeAt (mo, 0, 16000, 24000, 3000.0) / amplitudeAt (tone, 0, 16000, 24000, 3000.0))) < 0.05,
               "on a mono track, a SIDE band does nothing");
    }

    //==========================================================================
    void testMovingKnobs()
    {
        std::cout << "\n[moving knobs, LISTEN, AUTO-GAIN]\n";
        auto tone = th::sine (sr, 200.0, 0.3f, 2.0);
        P p; prepare (p, sr, 256);
        auto out = run (p, tone, 256, [&] (int start)
        {
            const bool second = start > (int) sr;
            set (p, EQIDs::gain[0], second ? 12.0f : -12.0f);
            set (p, EQIDs::lowShelf, second ? 0.0f : 1.0f);
            set (p, EQIDs::hpf, second ? 150.0f : 20.0f);
            set (p, EQIDs::target[1], second ? 2.0f : 0.0f);
        });
        float jump = 0.0f;
        for (int i = 1; i < out.getNumSamples(); ++i)
            jump = std::max (jump, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        const float normalJump = 0.3f * 4.0f * (float) std::sin (juce::MathConstants<double>::pi * 200.0 / sr) * 2.0f;
        check (allFinite (out) && jump < normalJump, "switching BELL/SHELF, the HPF and big gain jumps while it plays: no clicks (largest step " + juce::String (jump, 4) + ")");

        // LISTEN: just what the band covers
        P l; set (l, EQIDs::freq[2], 2000.0f); prepare (l, sr, 512);
        l.setListenBand (2);
        auto heard = run (l, noise (2.0, 0.25f), 512);
        const double near = amplitudeAt (heard, 0, 48000, 24000, 2000.0), far = amplitudeAt (heard, 0, 48000, 24000, 200.0);
        check (20 * std::log10 (far / near) < -20.0, "LISTEN on HIGH MID at 2 kHz: 200 Hz is " + juce::String (20 * std::log10 (far / near), 1) + " dB below 2 kHz");

        // AUTO-GAIN: a big boost comes out at the original loudness
        P a; set (a, EQIDs::gain[1], 9.0f); set (a, EQIDs::gain[2], 6.0f); set (a, CommonIDs::autoGain, 1.0f); prepare (a, sr, 512);
        auto loop = drumLoop (sr, 8.0, -6.0f, -9.0f, -20.0f);
        auto ao = run (a, loop, 512);
        const double change = heardDb (ao, (int) sr * 5) - heardDb (loop, (int) sr * 5);
        check (std::abs (change) < 1.0, "AUTO-GAIN on, LOW MID +9 and HIGH MID +6: comes out " + juce::String (change, 2) + " dB from the original loudness");
        P off; prepare (off, sr, 512);
        check (getParam (off, CommonIDs::autoGain) < 0.5f, "AUTO-GAIN starts off");

        P y; set (y, EQIDs::gain[1], 9.0f); set (y, CommonIDs::bypass, 1.0f); prepare (y, sr, 512);
        auto yo = run (y, loop, 512);
        check (maxDifference (loop, yo) == 0.0f, "BYPASS: exactly the original");
    }

    //==========================================================================
    void testMaster()
    {
        std::cout << "\n[MASTER: linear phase]\n";
        P p; set (p, EQIDs::master, 1.0f);
        set (p, EQIDs::freq[1], 250.0f); set (p, EQIDs::gain[1], -6.0f);
        set (p, EQIDs::freq[2], 4000.0f); set (p, EQIDs::gain[2], 6.0f);
        set (p, EQIDs::hpf, 40.0f);
        prepare (p, sr, 512);
        const int latency = p.getLatencySamples();
        check (latency == 2048, "MASTER on: the DAW is told about the delay (" + juce::String (latency) + " samples, " + juce::String (latency / 48.0, 1) + " ms)");

        // An impulse comes out symmetric around the delay: linear phase
        juce::AudioBuffer<float> impulse (2, 16384); impulse.clear();
        impulse.setSample (0, 1000, 1.0f); impulse.setSample (1, 1000, 1.0f);
        auto io = run (p, impulse, 512);
        int peak = 0; float best = 0;
        for (int i = 0; i < io.getNumSamples(); ++i) if (std::abs (io.getSample (0, i)) > best) { best = std::abs (io.getSample (0, i)); peak = i; }
        double asymmetry = 0, energy = 0;
        for (int k = 1; k < 1500; ++k)
        {
            asymmetry += std::abs (io.getSample (0, peak + k) - io.getSample (0, peak - k));
            energy += std::abs (io.getSample (0, peak + k));
        }
        check (peak == 1000 + latency && asymmetry / energy < 0.01, "a click comes out exactly at the delay, and symmetric (linear phase): asymmetry " + juce::String (asymmetry / energy * 100.0, 3) + "%");

        // Same curve as the normal EQ
        bool same = true;
        juce::String report;
        for (double hz : { 60.0, 250.0, 1000.0, 4000.0, 12000.0 })
        {
            P m; set (m, EQIDs::freq[1], 250.0f); set (m, EQIDs::gain[1], -6.0f); set (m, EQIDs::freq[2], 4000.0f); set (m, EQIDs::gain[2], 6.0f);
            set (m, EQIDs::hpf, 40.0f); prepare (m, sr, 512);
            const double minimum = measuredDb (m, hz);
            const int n = (int) sr * 2;
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i) { const float x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sr); b.setSample (0, i, x); b.setSample (1, i, x); }
            p.reset();
            auto out = run (p, b, 512);
            const double linearDb = 20 * std::log10 (amplitudeAt (out, 0, 40000, 32768, hz) / amplitudeAt (b, 0, 40000, 32768, hz));
            if (std::abs (linearDb - minimum) > 0.3) same = false;
            report << juce::String (hz, 0) << " Hz " << juce::String (minimum, 2) << " / " << juce::String (linearDb, 2) << "; ";
        }
        check (same, "the same curve as without MASTER (normal / MASTER): " + report);

        // Turning a knob while MASTER is on: the new curve arrives
        set (p, EQIDs::gain[2], -6.0f);
        waitForMaster (p);
        p.reset();
        const int n = (int) sr * 2;
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i) { const float x = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * 4000.0 * i / sr); b.setSample (0, i, x); b.setSample (1, i, x); }
        auto out = run (p, b, 512);
        const double after = 20 * std::log10 (amplitudeAt (out, 0, 40000, 32768, 4000.0) / amplitudeAt (b, 0, 40000, 32768, 4000.0));
        check (std::abs (after + 6.0) < 0.4, "HIGH MID turned from +6 to -6 while MASTER is on: the new filter is built and swapped in (" + juce::String (after, 2) + " dB)");

        // BYPASS in MASTER lines up with the delay
        P y; set (y, EQIDs::master, 1.0f); set (y, EQIDs::gain[1], 6.0f); set (y, CommonIDs::bypass, 1.0f); prepare (y, sr, 512);
        auto loop = drumLoop (sr, 1.0, -6.0f, -9.0f, -20.0f);
        auto yo = run (y, loop, 512);
        float diff = 0.0f;
        for (int i = latency; i < loop.getNumSamples(); ++i) diff = std::max (diff, std::abs (yo.getSample (0, i) - loop.getSample (0, i - latency)));
        check (diff == 0.0f, "BYPASS with MASTER on: the original, delayed the same");

        // Switching MASTER while playing: no blast, nothing broken
        P sw; prepare (sw, sr, 512);
        auto so = run (sw, drumLoop (sr, 3.0, -6.0f, -9.0f, -20.0f), 512, [&] (int start) { set (sw, EQIDs::master, start > (int) sr ? 1.0f : 0.0f); });
        float loudest = 0.0f;
        for (int i = 0; i < so.getNumSamples(); ++i) loudest = std::max (loudest, std::abs (so.getSample (0, i)));
        check (allFinite (so) && loudest < 1.0f, "MASTER switched on while it plays: a short crossfade, nothing loud or broken");
    }

    //==========================================================================
    void testPluginParts()
    {
        std::cout << "\n[settings, presets]\n";
        P p; prepare (p, sr, 512);
        check (p.getParameters().size() == 23, "23 settings: 4 bands x 3, 2 shapes, the filters, MONO BASS, MASTER, the analyser, AUTO-GAIN, BYPASS");
        const auto [ok, report] = presetTextsReport (p.getBuiltInPresets());
        check (ok, report);

        bool allFine = true;
        auto loop = drumLoop (sr, 2.0, -6.0f, -9.0f, -20.0f);
        for (auto& preset : p.getBuiltInPresets())
        {
            P q; q.loadPreset (preset); prepare (q, sr, 512);
            auto out = run (q, loop, 512);
            if (! allFinite (out)) allFine = false;
            for (auto& [id, value] : preset.values)
            {
                auto* prm = q.parameters.getParameter (id);
                if (prm == nullptr || std::abs (prm->convertFrom0to1 (prm->getValue()) - value) > 0.5f) { allFine = false; std::cout << "    " << preset.name << ": " << id << " " << value << "\n"; }
            }
        }
        check (allFine, "every preset loads exactly (every value inside its knob's range) and plays");

        juce::MemoryBlock state;
        set (p, EQIDs::gain[2], 4.5f); set (p, EQIDs::target[3], 2.0f); set (p, EQIDs::master, 1.0f);
        p.getStateInformation (state);
        P r; r.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (r, EQIDs::gain[2]) == 4.5f && getParam (r, EQIDs::target[3]) == 2.0f && getParam (r, EQIDs::master) > 0.5f, "saved with the project and loaded back");
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Vocal - Clear & Present");
        auto loop = drumLoop (sr, 2.0, -6.0f, -9.0f, -20.0f);
        run (p, loop, 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyEQAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 800, "window is 1100 x 800, with the shared header");
        if (window == nullptr) return;

        bool fits = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds())) { fits = false; std::cout << "    outside: " << child->getName() << "\n"; }
            for (auto& other : boxes) if (other.intersects (child->getBounds())) fits = false;
            boxes.push_back (child->getBounds());
        }
        check (fits, "everything fits inside the window without overlapping");

        bool doubleClickToType = true;
        for (int k = 0; k < EasyEQAudioProcessorEditor::numKnobs; ++k)
            for (auto* child : window->getKnob (k).getChildren())
                if (auto* box = dynamic_cast<juce::Label*> (child))
                    if (box->isEditableOnSingleClick() || ! box->isEditableOnDoubleClick()) doubleClickToType = false;
        check (doubleClickToType, "every number: double-click to type a value");

        for (int i = 0; i < 12; ++i) { run (p, loop, 512); window->updateDisplaysNow(); }
        savePng (*editor, outDir.getChildFile ("eq.png"));

        // Dragging a dot
        auto& display = window->getDisplay();
        const auto from = display.dotPosition (2);
        const auto to = from.translated (40.0f, -30.0f);
        const auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        const auto event = [&] (juce::Point<float> pos, juce::Point<float> down)
        {
            return juce::MouseEvent (source, pos, {}, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation,
                                     juce::MouseInputSource::defaultRotation, juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY,
                                     &display, &display, now, down, now, 1, false);
        };
        const float gainBefore = getParam (p, EQIDs::gain[2]), freqBefore = getParam (p, EQIDs::freq[2]);
        display.mouseDown (event (from, from));
        display.mouseDrag (event (to, from));
        display.mouseUp (event (to, from));
        check (getParam (p, EQIDs::gain[2]) > gainBefore + 1.0f && getParam (p, EQIDs::freq[2]) > freqBefore * 1.1f, "dragging HIGH MID's dot up and right: more gain, higher frequency");
        display.mouseDoubleClick (event (display.dotPosition (2), display.dotPosition (2)));
        check (getParam (p, EQIDs::gain[2]) == 0.0f, "double-clicking a dot: back to 0 dB");

        // LISTEN while held
        auto& listen = window->getListenButton (1);
        listen.setState (juce::Button::buttonDown);
        const bool held = p.getListenBand() == 1;
        listen.setState (juce::Button::buttonNormal);
        check (held && p.getListenBand() == -1, "LISTEN is on only while the button is held");

        window->getInitAllButton().onClick();
        check (getParam (p, EQIDs::gain[1]) == 0.0f && getParam (p, EQIDs::hpf) == Equalizer::hpfOffHz && p.getPresetName() == "Default settings", "INIT: back to flat");

        // A mid-side preset draws two curves
        loadPresetNamed (p, "Mix Bus - Wider (Side Air)");
        window->updateDisplaysNow();
        savePng (*editor, outDir.getChildFile ("eq_midside.png"));
    }

    //==========================================================================
    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[speed]\n";
        auto loop = drumLoop (sr, 10.0, -6.0f, -9.0f, -20.0f);
        {
            P p; loadPresetNamed (p, "Vocal - Clear & Present"); set (p, EQIDs::hpfSlope, 1.0f); set (p, EQIDs::monoBass, 100.0f); prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            check (share < 1.0, "every band and filter in use, stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
        }
        {
            P p; loadPresetNamed (p, "Vocal - Clear & Present"); set (p, EQIDs::master, 1.0f); prepare (p, sr, 256);
            const double share = coreShare (p, loop);
            check (share < 6.0, "MASTER (linear phase): " + juce::String (share, 3) + "% of one core");
        }
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testFlatAndFilters();
    testMidSide();
    testMovingKnobs();
    testMaster();
    testPluginParts();
    testWindow (outDir);
    testSpeed();
    return finish();
}

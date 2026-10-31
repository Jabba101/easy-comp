// Easy Tremolo: the tremolo / auto-pan and the plugin around it (not shipped).
#include "TestHelpers.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace th;
using P = EasyTremoloAudioProcessor;

namespace
{
    constexpr double sr = 48000.0;

    void set (P& p, const char* id, float v)   { setParam (p, id, v); }

    // A DAW that's playing at 120 BPM, from beat 0
    struct Song : public juce::AudioPlayHead
    {
        double beats = 0.0;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setBpm (120.0);
            info.setIsPlaying (true);
            info.setPpqPosition (beats);
            return info;
        }
    };

    juce::AudioBuffer<float> steady (double seconds, float level = 0.5f)
    {
        juce::AudioBuffer<float> b (2, (int) (sr * seconds));
        for (int ch = 0; ch < 2; ++ch)
            juce::FloatVectorOperations::fill (b.getWritePointer (ch), level, b.getNumSamples());
        return b;
    }

    // Runs with the song playing (the play head moves along with the audio)
    juce::AudioBuffer<float> runPlaying (P& p, const juce::AudioBuffer<float>& in, Song& song, int block = 512)
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer midi;
        p.setPlayHead (&song);
        for (int start = 0; start < out.getNumSamples(); start += block)
        {
            const int n = std::min (block, out.getNumSamples() - start);
            song.beats = start / sr * 2.0;   // 120 BPM = 2 beats a second
            juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, n);
            p.processBlock (view, midi);
        }
        p.setPlayHead (nullptr);
        return out;
    }

    //==========================================================================
    void testMovement()
    {
        std::cout << "\n[the movement]\n";
        {
            P p; prepare (p, sr, 512);
            check (p.getLatencySamples() == 0, "no delay added");
            check (getParam (p, TremoloIDs::sync) == 1.0f && getParam (p, TremoloIDs::depth) == 50.0f, "starts on 1/8-note pulses (SYNC TEMPO), DEPTH 50%");
        }
        {
            P p; set (p, TremoloIDs::sync, 0.0f); set (p, TremoloIDs::rate, 2.0f); set (p, TremoloIDs::depth, 100.0f); prepare (p, sr, 512);
            auto out = run (p, steady (2.0), 512);
            float lo = 1.0f, hi = 0.0f;
            int firstTop = -1, secondTop = -1;
            for (int i = 4800; i < out.getNumSamples() - 1; ++i)
            {
                const float v = out.getSample (0, i) / 0.5f;
                lo = std::min (lo, v); hi = std::max (hi, v);
                if (v > 0.99999f && out.getSample (0, i - 1) / 0.5f <= 0.99999f) { if (firstTop < 0) firstTop = i; else if (secondTop < 0 && i - firstTop > 1000) secondTop = i; }
            }
            check (lo < 0.001f && hi <= 1.00001f && hi > 0.999f, "DEPTH 100%: right down to silence and back up to your sound - never louder");
            check (secondTop > 0 && std::abs ((secondTop - firstTop) - 24000) < 60, "RATE 2 Hz: a pulse every half second (" + juce::String (secondTop - firstTop) + " samples)");
        }
        {
            P p; set (p, TremoloIDs::depth, 0.0f); prepare (p, sr, 512);
            auto in = drumLoop (sr, 1.0);
            check (maxDifference (run (p, in, 512), in) < 1.0e-6f, "DEPTH 0: your sound, untouched");
            P q; set (q, CommonIDs::bypass, 1.0f); prepare (q, sr, 512);
            check (maxDifference (run (q, in, 512), in) == 0.0f, "BYPASS: your sound exactly");
        }
    }

    //==========================================================================
    void testLockedToTheBeat()
    {
        std::cout << "\n[locked to the beat]\n";
        const auto gainOnBeats = [] (int shape)
        {
            P p; set (p, TremoloIDs::sync, 1.0f); set (p, TremoloIDs::note, (float) TremoloNotes::n4); set (p, TremoloIDs::depth, 100.0f);
            set (p, TremoloIDs::shape, (float) shape);
            prepare (p, sr, 512);
            Song song;
            auto out = runPlaying (p, steady (3.0), song, 500);
            float worst = 0.0f, mean = 0.0f;
            for (int beat = 2; beat < 6; ++beat)   // (a beat every 24000 samples)
            {
                const float g = out.getSample (0, beat * 24000) / 0.5f;
                worst = std::max (worst, shape == 3 ? g : 1.0f - g);
                mean += g / 4.0f;
            }
            return std::pair<float, float> { worst, mean };
        };
        const auto sine = gainOnBeats (0), pump = gainOnBeats (3);
        check (sine.first < 0.01f, "SINE, 1/4 note: full volume exactly on every beat while the song plays (off by " + juce::String (sine.first, 4) + ")");
        check (pump.first < 0.05f, "PUMP, 1/4 note: dips right down on every beat, like a kick pumping a compressor (" + juce::String (pump.second, 3) + ")");

        // SQUARE: chopped, but the edges never click
        {
            P p; set (p, TremoloIDs::sync, 1.0f); set (p, TremoloIDs::note, (float) TremoloNotes::n32); set (p, TremoloIDs::depth, 100.0f);
            set (p, TremoloIDs::shape, 2.0f);
            prepare (p, sr, 512);
            auto out = run (p, steady (1.0), 512);
            float worst = 0.0f;
            for (int i = 2000; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
            check (worst < 0.5f * 0.03f, "SQUARE at 1/32 notes, 100%: a hard chop with no clicks (largest step " + juce::String (worst / 0.5f * 100.0f, 2) + "% of the level)");
        }
    }

    //==========================================================================
    void testAutoPan()
    {
        std::cout << "\n[AUTO-PAN]\n";
        P p; set (p, TremoloIDs::sync, 0.0f); set (p, TremoloIDs::rate, 1.0f); set (p, TremoloIDs::depth, 100.0f); set (p, TremoloIDs::mode, 1.0f);
        prepare (p, sr, 512);
        auto out = run (p, steady (2.0), 512);
        bool opposite = true, oneFull = true;
        float lMin = 1.0f, rMin = 1.0f;
        for (int i = 4800; i < out.getNumSamples(); i += 37)
        {
            const float l = out.getSample (0, i) / 0.5f, r = out.getSample (1, i) / 0.5f;
            oneFull = oneFull && std::max (l, r) > 0.999f;
            lMin = std::min (lMin, l); rMin = std::min (rMin, r);
            if (l < 0.5f && r < 0.999f) opposite = false;
        }
        check (opposite && oneFull && lMin < 0.01f && rMin < 0.01f, "100%: the sound swings all the way from one side to the other (one side always at full)");

        P mono; set (mono, TremoloIDs::mode, 1.0f); prepare (mono, sr, 512, 1);
        auto mOut = run (mono, th::sine (sr, 220.0, 0.3f, 1.0, 1), 512);
        check (allFinite (mOut), "works on a mono track (as a tremolo)");

        P s;
        juce::MemoryBlock state;
        set (s, TremoloIDs::shape, 3.0f); set (s, TremoloIDs::note, 12.0f); set (s, TremoloIDs::mode, 1.0f);
        s.getStateInformation (state);
        P r; r.setStateInformation (state.getData(), (int) state.getSize());
        check (getParam (r, TremoloIDs::shape) == 3.0f && getParam (r, TremoloIDs::note) == 12.0f && getParam (r, TremoloIDs::mode) == 1.0f,
               "saved with the project and loaded back");

        const auto report = presetTextsReport (s.getBuiltInPresets());
        check (report.first, report.second);
    }

    //==========================================================================
    void testWindow (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        P p; prepare (p, sr, 512);
        loadPresetNamed (p, "Electric Piano - Classic Auto-Pan");
        run (p, steady (0.3), 512);

        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        auto* window = dynamic_cast<EasyTremoloAudioProcessorEditor*> (editor.get());
        check (window != nullptr && editor->getWidth() == 1100 && editor->getHeight() == 640, "window is 1100 x 640, with the shared header");
        if (window == nullptr) return;

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

        window->updateDisplaysNow();
        check (window->getKnob (EasyTremoloAudioProcessorEditor::rateKnob).isVisible() && ! window->getKnob (EasyTremoloAudioProcessorEditor::noteKnob).isVisible(),
               "SYNC OFF: RATE in Hz");
        savePng (*editor, outDir.getChildFile ("tremolo.png"));

        window->getInitAllButton().onClick();
        window->updateDisplaysNow();
        check (p.getPresetName() == "Default settings" && window->getKnob (EasyTremoloAudioProcessorEditor::noteKnob).isVisible(),
               "INIT: back to 'Default settings' (the note knob, SYNC on TEMPO)");

        bool hintsOk = true;
        for (int k = 0; k < EasyTremoloAudioProcessorEditor::numKnobs; ++k)
        {
            window->mouseEnter (hoverEvent (&window->getKnob (k)));
            if (window->hintBar.getText().length() < 40) hintsOk = false;
        }
        check (hintsOk, "every knob explains itself in the help bar");
    }

    void testSpeed()
    {
       #if ! JUCE_DEBUG
        std::cout << "\n[computer power]\n";
        P p; loadPresetNamed (p, "Synth Pad - Trance Gate"); prepare (p, sr, 256);
        const double share = coreShare (p, drumLoop (sr, 10.0));
        check (share < 0.5, "stereo, 48 kHz: " + juce::String (share, 3) + "% of one core");
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());

    testMovement();
    testLockedToTheBeat();
    testAutoPan();
    testWindow (outDir);
    testSpeed();
    return finish();
}

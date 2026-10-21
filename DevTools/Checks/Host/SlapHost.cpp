// Loads the built Bitch-Slap VST3 the way a DAW does and checks it from the
// outside: what the DAW sees (names, settings, delay, tail, buses), that a hit
// gets its burst at the right time and length, MIX at 100%, and saving and
// re-opening the settings. (Not shipped.)
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>

static int failures = 0;
static void check (bool ok, const juce::String& what) { if (! ok) ++failures; std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n"; }

static juce::AudioProcessorParameter* find (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters()) if (prm->getName (40) == name) return prm;
    return nullptr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::VST3PluginFormat format;
    if (argc < 2) return 1;

    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, argv[1]);
    if (found.isEmpty()) { std::cout << "nothing found\n"; return 1; }
    auto& d = *found[0];
    std::cout << d.name << " | maker: " << d.manufacturerName << " | version " << d.version << " | category " << d.category
              << " | uid " << juce::String::toHexString (d.uniqueId) << "\n";
    check (d.name == "Bitch-Slap" && d.manufacturerName == "Aardvark Easy Suite" && d.version == "1.0.0", "named Bitch-Slap, by Aardvark Easy Suite, 1.0.0");

    juce::String error;
    auto p = format.createInstanceFromDescription (d, 48000.0, 512, error);
    if (p == nullptr) { std::cout << "couldn't open: " << error << "\n"; return 1; }

    std::cout << "  settings the DAW sees (" << p->getParameters().size() << "):";
    for (auto* prm : p->getParameters()) std::cout << " [" << prm->getName (40) << " = " << prm->getCurrentValueAsText() << "]";
    std::cout << "\n";
    auto* mix = find (*p, "Mix");
    auto* length = find (*p, "Length");
    auto* sync = find (*p, "Sync");
    auto* note = find (*p, "Note");
    check (mix != nullptr && length != nullptr && sync != nullptr && note != nullptr && find (*p, "Bypass") != nullptr,
           "MIX, LENGTH, SYNC, NOTE and BYPASS are there");
    if (mix == nullptr || length == nullptr || sync == nullptr || note == nullptr) return 1;
    check (length->getCurrentValueAsText() == "300 ms" && note->getCurrentValueAsText() == "1/8 note" && mix->getCurrentValueAsText() == "35%",
           "...reading 35%, 300 ms, 1/8 note");

    // Buses: stereo, mono, and mono in / stereo out
    const auto tryLayout = [&] (int ins, int outs)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::canonicalChannelSet (ins));
        layout.outputBuses.add (juce::AudioChannelSet::canonicalChannelSet (outs));
        return p->checkBusesLayoutSupported (layout);
    };
    check (tryLayout (2, 2) && tryLayout (1, 1) && tryLayout (1, 2) && ! tryLayout (2, 1), "buses: stereo, mono, mono in / stereo out (not stereo in / mono out)");

    p->prepareToPlay (48000.0, 512);
    check (p->getLatencySamples() == 0, "no delay added (latency " + juce::String (p->getLatencySamples()) + ")");
    check (p->getTailLengthSeconds() > 0.3 && p->getTailLengthSeconds() < 0.4, "tail " + juce::String (p->getTailLengthSeconds(), 3) + " s (LENGTH + 50 ms)");

    // A hit: its burst starts 2 ms later and stops 300 ms after the hit
    const auto runHit = [&]
    {
        const int n = 48000;
        juce::AudioBuffer<float> io (2, n);
        io.clear();
        juce::MidiBuffer midi;
        { juce::AudioBuffer<float> silence (2, 512); for (int k = 0; k < 200; ++k) { silence.clear(); p->processBlock (silence, midi); } }
        io.setSample (0, 1000, 1.0f); io.setSample (1, 1000, 1.0f);
        for (int start = 0; start < n; start += 512)
        {
            juce::AudioBuffer<float> view (io.getArrayOfWritePointers(), 2, start, std::min (512, n - start));
            p->processBlock (view, midi);
        }
        return io;
    };

    mix->setValueNotifyingHost (1.0f);   // 100%: the reverb on its own
    const auto out = runHit();
    int first = 1001, last = 0;
    while (first < out.getNumSamples() && std::abs (out.getSample (0, first)) < 1e-6f) ++first;
    const float level = out.getMagnitude (0, 1000 + 400, 2000);
    for (int i = out.getNumSamples() - 1; i > 1000; --i) if (std::abs (out.getSample (0, i)) > 0.01f * level) { last = i; break; }
    check (std::abs (out.getSample (0, 1000)) < 1e-6f, "MIX 100%: the hit itself is gone (reverb only)");
    check (first - 1000 >= 96 && first - 1000 <= 100, "the burst starts 2 ms after the hit (" + juce::String ((first - 1000) / 48.0, 1) + " ms)");
    check (std::abs ((last - 1000) / 48.0 - 300.0) < 4.0, "...and stops 300 ms after it (" + juce::String ((last - 1000) / 48.0, 1) + " ms)");

    // Saving and re-opening
    length->setValueNotifyingHost (length->getValueForText ("555"));
    sync->setValueNotifyingHost (1.0f);
    juce::MemoryBlock state;
    p->getStateInformation (state);
    auto q = format.createInstanceFromDescription (d, 48000.0, 512, error);
    q->setStateInformation (state.getData(), (int) state.getSize());
    check (find (*q, "Length")->getCurrentValueAsText() == "555 ms" && find (*q, "Sync")->getCurrentValueAsText() == "Tempo"
             && find (*q, "Mix")->getCurrentValueAsText() == "100%",
           "a saved project re-opens with its settings (Length " + find (*q, "Length")->getCurrentValueAsText() + ", Sync "
             + find (*q, "Sync")->getCurrentValueAsText() + ")");

    p->releaseResources();
    std::cout << (failures == 0 ? "ALL OK\n" : "FAILURES\n");
    return failures == 0 ? 0 : 1;
}

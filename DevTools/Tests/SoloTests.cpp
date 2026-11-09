// Verification harness for the single plugins' HOLOGRAM look (not shipped).
// Built once per plugin: SOLO_HEADER / SOLO_PROCESSOR / SOLO_MODULE name it.
// Renders the window to PNG files (argv[1] = the folder).

#include SOLO_HEADER
#include "HoloSolo.h"
#include "EasyWindow.h"
#include "TestHelpers.h"

using th::check;

namespace
{
    void snapshot (juce::Component& c, const juce::File& file, float scale = 1.0f)
    {
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }

    void pump (int ms)   { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** Something with hits in it, so the meters and the screen have something to show. */
    juce::AudioBuffer<float> drums (double sr, double seconds)
    {
        const int n = (int) (sr * seconds);
        juce::AudioBuffer<float> b (2, n);
        juce::Random random (7);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr, beat = std::fmod (t, 0.5);
            const float kick = (float) (0.9 * std::exp (-beat * 18.0) * std::sin (2.0 * juce::MathConstants<double>::pi * (55.0 + 90.0 * std::exp (-beat * 40.0)) * beat));
            const double off = std::fmod (t + 0.25, 0.5);
            const float snare = (float) (0.5 * std::exp (-off * 25.0)) * (random.nextFloat() * 2.0f - 1.0f);
            const float bed = 0.08f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * t) + 0.02f * (random.nextFloat() * 2.0f - 1.0f);
            b.setSample (0, i, kick + snare + bed);
            b.setSample (1, i, kick + 0.9f * snare + bed);
        }
        return b;
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();

    const auto settingsFile = juce::File::createTempFile (".settings");
    SoloSettings::useFileForTesting (settingsFile);

    SOLO_PROCESSOR p;
   #if SOLO_MODULE == 15   // (Easy Unbleed: the stem, plus the other stems on the sidechain)
    {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.inputBuses.add (juce::AudioChannelSet::stereo());
        layout.outputBuses.add (juce::AudioChannelSet::stereo());
        p.setBusesLayout (layout);
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
    }
   #else
    th::prepare (p, 48000.0, 512);
   #endif
    const juce::String name = p.getName();
    std::cout << "\n" << name << " - HOLOGRAM look\n";

    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
    auto* solo = dynamic_cast<Holo::SoloEditor*> (editor.get());
    check (solo != nullptr, "the window is the single plugins' window");
    if (solo == nullptr)
        return 1;
    check (solo->getHologram() != nullptr && solo->getClassic() == nullptr, "HOLOGRAM is the default look");

    auto& view = *solo->getHologram();
    check (view.getSlab().getModule() == SOLO_MODULE, "it shows its own module's panel");
    check (view.getSlab().getNumChildComponents() > 4, "the panel has its controls");
    check (view.getWidth() == view.nativeSize().x && view.getHeight() == view.nativeSize().y, "laid out at its native size");
    check (solo->getWidth() == view.nativeSize().x, "the window opens at 100%");

    check (view.getTickerText().isNotEmpty(), "the ticker says something to start with");

    // Play something, let it draw
    {
        auto audio = drums (48000.0, 4.0);
        const int channels = p.getTotalNumInputChannels();
        if (channels > 2)   // (the sidechain: a tone the stem has a little bleed of, plus a bass line)
        {
            juce::AudioBuffer<float> wide (channels, audio.getNumSamples());
            for (int i = 0; i < audio.getNumSamples(); ++i)
            {
                const double t = i / 48000.0;
                const float other = 0.5f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * t)
                                  + 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1760.0 * t);
                for (int ch = 0; ch < 2; ++ch)
                {
                    wide.setSample (ch, i, audio.getSample (ch, i) * 0.6f + 0.05f * other);
                    wide.setSample (2 + ch, i, other);
                }
            }
            audio = std::move (wide);
        }
        juce::MidiBuffer midi;
        for (int start = 0; start + 512 <= audio.getNumSamples(); start += 512)
        {
            juce::AudioBuffer<float> block (audio.getArrayOfWritePointers(), audio.getNumChannels(), start, 512);
            p.processBlock (block, midi);
            if ((start / 512) % 3 == 0)
                view.updateNow();
        }
        view.updateNow();
    }
    snapshot (*solo, out.getChildFile (name.removeCharacters (" -") + "_hologram.png"), 1.0f);

    // SETUP: CLASSIC and back
    {
        auto s = SoloSettings::load();
        check (s.skin == StripSettings::hologram, "the settings file starts at HOLOGRAM");
    }
    {
        SoloSettings s;
        s.skin = StripSettings::classic;
        s.save();
        std::unique_ptr<juce::AudioProcessorEditor> second (p.createEditor());
        auto* classicSolo = dynamic_cast<Holo::SoloEditor*> (second.get());
        check (classicSolo != nullptr && classicSolo->getClassic() != nullptr && classicSolo->getHologram() == nullptr, "CLASSIC (chosen in SETUP) shows the classic window");
        auto* window = classicSolo != nullptr ? dynamic_cast<EasyWindow*> (classicSolo->getClassic()) : nullptr;
        check (window != nullptr && window->lookButton.isVisible(), "... with a HOLOGRAM button in its header");
        if (window != nullptr)
        {
            snapshot (*classicSolo, out.getChildFile (name.removeCharacters (" -") + "_classic.png"), 1.0f);
            window->lookButton.triggerClick();
            pump (200);
            check (classicSolo->getHologram() != nullptr && classicSolo->getClassic() == nullptr, "HOLOGRAM switches it back");
            check (SoloSettings::load().skin == StripSettings::hologram, "... and is remembered");
        }
    }

    // The window scales (fixed shape)
    {
        solo->setSize (view.nativeSize().x * 3 / 2, view.nativeSize().y * 3 / 2);
        check (std::abs (view.getTransform().mat00 - 1.5f) < 0.01f, "dragged bigger, the panel scales with it");
    }

    editor.reset();
    settingsFile.deleteFile();
    std::cout << "\n" << (th::checks - th::failures) << " / " << th::checks << " checks passed\n";
    return th::failures == 0 ? 0 : 1;
}

#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <chrono>
#include <cstdio>

// Feeds 120 s of audio through the plugin (stereo, 48 kHz, 256-sample blocks) and
// returns the share of one CPU core it used. Pass an empty buffer for silence.
template <typename P>
double measure (const juce::String& preset, bool autoGain, const juce::AudioBuffer<float>& material, int block = 256)
{
    P p;
    p.setPlayConfigDetails (2, 2, 48000.0, block);
    p.prepareToPlay (48000.0, block);
    for (auto& pr : p.getBuiltInPresets()) if (pr.name == preset) p.loadPreset (pr);
    if (auto* prm = p.parameters.getParameter ("autogain")) prm->setValueNotifyingHost (autoGain ? 1.0f : 0.0f);
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiBuffer midi;
    constexpr double seconds = 120.0;
    const int blocks = (int) (seconds * 48000.0 / block);
    int pos = 0;
    double busy = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, material.getNumSamples() > 0 ? material.getSample (ch, (pos + i) % material.getNumSamples()) : 0.0f);
        if (material.getNumSamples() > 0) pos = (pos + block) % material.getNumSamples();
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (buf, midi);
        busy += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    }
    return 100.0 * busy / seconds;
}

inline juce::AudioBuffer<float> loadMaterial (const char* path)
{
    juce::AudioFormatManager f; f.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (f.createReaderFor (juce::File (path)));
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, true);
    return b;
}

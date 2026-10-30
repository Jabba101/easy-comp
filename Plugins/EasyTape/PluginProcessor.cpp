#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyTapeAudioProcessor::createParameterLayout()
{
    const auto& defaults = tapePresets[0];   // "Default settings"

    juce::NormalisableRange<float> driveRange (TapeMachine::minDriveDb, TapeMachine::maxDriveDb, 0.1f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TapeIDs::speed, 1 }, "Speed",
                                                              juce::StringArray { "15 ips", "30 ips", "Cassette" }, defaults.speed));
    layout.add (makeKnob (TapeIDs::drive, 1, "Drive",          driveRange,   defaults.driveDb,     Format::signedDecibels));
    layout.add (makeKnob (TapeIDs::wow,   1, "Wow & Flutter",  percentRange, defaults.wowPercent,  Format::percent));
    layout.add (makeKnob (TapeIDs::hiss,  1, "Hiss",           percentRange, defaults.hissPercent, Format::percent));

    // AUTO: 4x while you mix, 8x when your DAW exports (renders) the song.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TapeIDs::quality, 1 }, "Oversampling",
                                                              juce::StringArray { "4x Mixing / 8x Export", "Off", "4x", "8x" }, 0));

    // AUTO-GAIN, starting ON (the shared one starts off), then BYPASS
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { CommonIDs::autoGain, 1 }, "Auto-Gain", true));
    addCommonParameters (layout, false);
    return layout;
}

//==============================================================================
EasyTapeAudioProcessor::EasyTapeAudioProcessor()
    : EasyProcessor ("EasyTapeSettings", createParameterLayout(), makeTapePresets(), tapePresetIDs())
{
    speedValue    = parameters.getRawParameterValue (TapeIDs::speed);
    driveValue    = parameters.getRawParameterValue (TapeIDs::drive);
    wowValue      = parameters.getRawParameterValue (TapeIDs::wow);
    hissValue     = parameters.getRawParameterValue (TapeIDs::hiss);
    qualityValue  = parameters.getRawParameterValue (TapeIDs::quality);
    autoGainValue = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue   = parameters.getRawParameterValue (CommonIDs::bypass);

    // Tell the DAW the delay straight away.
    machine.prepare (44100.0, 2, getCurrentSettings());
    setLatencySamples (machine.getLatencySamples());
}

//==============================================================================
void EasyTapeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    machine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), getCurrentSettings());
    setLatencySamples (machine.getLatencySamples());
    recordVu.store (-100.0f);
}

void EasyTapeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    machine.setSettings (getCurrentSettings());
    machine.process (buffer);

    matchGainDb.store (machine.getMatchGainDb());
    recordVu.store (machine.getRecordLevelVu());
}

TapeMachine::Settings EasyTapeAudioProcessor::getCurrentSettings() const noexcept
{
    TapeMachine::Settings settings;
    settings.speed      = (TapeMachine::Speed) juce::jlimit (0, 2, (int) speedValue->load());
    settings.driveDb    = driveValue->load();
    settings.wowFlutter = wowValue->load() / 100.0f;
    settings.hiss       = hissValue->load() / 100.0f;
    settings.autoGain   = autoGainValue->load() > 0.5f;
    settings.bypassed   = bypassValue->load() > 0.5f;

    const int chosen = juce::jlimit (0, 3, (int) qualityValue->load());
    settings.quality = chosen == 0 ? (isNonRealtime() ? TapeMachine::Quality::times8 : TapeMachine::Quality::times4)
                                   : (TapeMachine::Quality) (chosen - 1);
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyTapeAudioProcessor::createEditor()
{
    return new EasyTapeAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin would be one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyTapeAudioProcessor();
}
#endif

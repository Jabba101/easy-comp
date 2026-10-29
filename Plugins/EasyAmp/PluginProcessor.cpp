#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyAmpAudioProcessor::createParameterLayout()
{
    const auto& defaults = ampPresets[0];   // "Default settings"

    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> toneRange (-100.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> tightRange (ValveAmp::tightOffHz, 400.0f, 1.0f);
    tightRange.setSkewForCentre (100.0f);

    const auto tightText = [] (float hz)  { return hz <= ValveAmp::tightOffHz + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto tightFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? ValveAmp::tightOffHz : Format::hertzFromText (text);
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (AmpIDs::drive, 1, "Drive", percentRange, defaults.drivePercent, Format::percent));
    layout.add (makeKnob (AmpIDs::tone,  1, "Tone",  toneRange,    defaults.tonePercent,  Format::signedPercent));
    layout.add (makeKnob (AmpIDs::tight, 1, "Tight", tightRange,   defaults.tightHz,      tightText, tightFromText));
    layout.add (makeKnob (AmpIDs::mix,   1, "Mix",   percentRange, defaults.mixPercent,   Format::percent));

    // AUTO: 4x while you mix, 8x when your DAW exports (renders) the song.
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { AmpIDs::quality, 1 }, "Oversampling",
                                                              juce::StringArray { "4x Mixing / 8x Export", "Off", "4x", "8x" }, 0));

    // AUTO-GAIN, starting ON (the shared one starts off), then BYPASS
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { CommonIDs::autoGain, 3 }, "Auto-Gain", true));
    addCommonParameters (layout, false);
    return layout;
}

//==============================================================================
EasyAmpAudioProcessor::EasyAmpAudioProcessor()
    : EasyProcessor ("EasyAmpSettings", createParameterLayout(), makeAmpPresets(), ampPresetIDs())
{
    driveValue    = parameters.getRawParameterValue (AmpIDs::drive);
    toneValue     = parameters.getRawParameterValue (AmpIDs::tone);
    tightValue    = parameters.getRawParameterValue (AmpIDs::tight);
    mixValue      = parameters.getRawParameterValue (AmpIDs::mix);
    qualityValue  = parameters.getRawParameterValue (AmpIDs::quality);
    autoGainValue = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue   = parameters.getRawParameterValue (CommonIDs::bypass);

    // Tell the DAW the delay straight away (it's the same at any sample rate).
    amp.prepare (44100.0, 2, getCurrentSettings());
    setLatencySamples (amp.getLatencySamples());
}

//==============================================================================
void EasyAmpAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    amp.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), getCurrentSettings());
    setLatencySamples (amp.getLatencySamples());
    inputPeak.store (0.0f);
}

void EasyAmpAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    storeIfBigger (inputPeak, buffer.getMagnitude (0, buffer.getNumSamples()));

    amp.setSettings (getCurrentSettings());
    amp.process (buffer);
    matchGainDb.store (amp.getMatchGainDb());
}

ValveAmp::Settings EasyAmpAudioProcessor::getCurrentSettings() const noexcept
{
    ValveAmp::Settings settings;
    settings.drive    = driveValue->load() / 100.0f;   // the knobs show %, the maths wants 0..1
    settings.tone     = toneValue->load() / 100.0f;
    settings.tightHz  = tightValue->load();
    settings.mix      = mixValue->load() / 100.0f;
    settings.autoGain = autoGainValue->load() > 0.5f;
    settings.bypassed = bypassValue->load() > 0.5f;

    const int chosen = juce::jlimit (0, 3, (int) qualityValue->load());
    settings.quality = chosen == 0 ? (isNonRealtime() ? ValveAmp::Quality::times8 : ValveAmp::Quality::times4)
                                   : (ValveAmp::Quality) (chosen - 1);
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyAmpAudioProcessor::createEditor()
{
    return new EasyAmpAudioProcessorEditor (*this);
}

// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyAmpAudioProcessor();
}

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyWahAudioProcessor::createParameterLayout()
{
    const auto& defaults = wahPresets[0];   // "Default settings"

    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> fromRange (AutoWah::minFromHz, AutoWah::maxFromHz, 1.0f);
    fromRange.setSkewForCentre (400.0f);
    juce::NormalisableRange<float> toRange (AutoWah::minToHz, AutoWah::maxToHz, 1.0f);
    toRange.setSkewForCentre (2000.0f);

    juce::StringArray noteNames;
    for (auto& note : WahNotes::lengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { WahIDs::mode, 1 }, "Mode",
                                                              juce::StringArray { "Auto", "Tempo" }, defaults.mode));
    layout.add (makeKnob (WahIDs::sensitivity, 1, "Sensitivity", percentRange, defaults.sensitivityPercent, Format::percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { WahIDs::response, 1 }, "Response",
                                                              juce::StringArray { "Snappy", "Smooth", "Lazy" }, defaults.response));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { WahIDs::direction, 1 }, "Direction",
                                                              juce::StringArray { "Up", "Down" }, defaults.direction));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { WahIDs::note, 1 }, "Note", noteNames, defaults.note));
    layout.add (makeKnob (WahIDs::pedal, 1, "Pedal", percentRange, defaults.pedalPercent, Format::percent));
    layout.add (makeKnob (WahIDs::from, 1, "From", fromRange, defaults.fromHz, Format::hertz, Format::hertzFromText));
    layout.add (makeKnob (WahIDs::to,   1, "To",   toRange,   defaults.toHz,   Format::hertz, Format::hertzFromText));
    layout.add (makeKnob (WahIDs::resonance, 1, "Resonance", percentRange, defaults.resonancePercent, Format::percent));
    layout.add (makeKnob (WahIDs::mix, 1, "Mix", percentRange, defaults.mixPercent, Format::percent));

    // AUTO-GAIN, starting ON (the shared one starts off), then BYPASS
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { CommonIDs::autoGain, 1 }, "Auto-Gain", true));
    addCommonParameters (layout, false);
    return layout;
}

//==============================================================================
EasyWahAudioProcessor::EasyWahAudioProcessor()
    : EasyProcessor ("EasyWahSettings", createParameterLayout(), makeWahPresets(), wahPresetIDs())
{
    modeValue        = parameters.getRawParameterValue (WahIDs::mode);
    sensitivityValue = parameters.getRawParameterValue (WahIDs::sensitivity);
    responseValue    = parameters.getRawParameterValue (WahIDs::response);
    directionValue   = parameters.getRawParameterValue (WahIDs::direction);
    noteValue        = parameters.getRawParameterValue (WahIDs::note);
    pedalValue       = parameters.getRawParameterValue (WahIDs::pedal);
    fromValue        = parameters.getRawParameterValue (WahIDs::from);
    toValue          = parameters.getRawParameterValue (WahIDs::to);
    resonanceValue   = parameters.getRawParameterValue (WahIDs::resonance);
    mixValue         = parameters.getRawParameterValue (WahIDs::mix);
    autoGainValue    = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue      = parameters.getRawParameterValue (CommonIDs::bypass);
}

AutoWah::Settings EasyWahAudioProcessor::getCurrentSettings() const noexcept
{
    AutoWah::Settings s;
    s.mode        = (AutoWah::Mode) juce::jlimit (0, 1, juce::roundToInt (modeValue->load()));
    s.sensitivity = sensitivityValue->load() / 100.0f;
    s.response    = (AutoWah::Response) juce::jlimit (0, 2, juce::roundToInt (responseValue->load()));
    s.direction   = (AutoWah::Direction) juce::jlimit (0, 1, juce::roundToInt (directionValue->load()));
    s.cycleBeats  = WahNotes::lengths[juce::jlimit (0, WahNotes::count - 1, juce::roundToInt (noteValue->load()))].beats;
    s.pedal       = pedalValue->load() / 100.0f;
    s.fromHz      = fromValue->load();
    s.toHz        = toValue->load();
    s.resonance   = resonanceValue->load() / 100.0f;
    s.mix         = mixValue->load() / 100.0f;
    s.autoGain    = autoGainValue->load() > 0.5f;
    s.bypassed    = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyWahAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    wah.prepare (sampleRate, getCurrentSettings());
}

void EasyWahAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    double songBeats = -1.0;
    if (auto* transport = getPlayHead())
        if (const auto info = transport->getPosition())
        {
            if (const auto bpm = info->getBpm())
                if (*bpm >= 20.0 && *bpm <= 999.0)
                    tempo.store (*bpm);
            if (info->getIsPlaying())
                if (const auto ppq = info->getPpqPosition())
                    songBeats = juce::jmax (0.0, *ppq);
        }

    wah.setSettings (getCurrentSettings());
    wah.process (buffer, tempo.load(), songBeats);

    makeUpDb.store (wah.getMakeUpDb());
    float low = pedalLow.load();
    while (wah.getPedalLow() < low && ! pedalLow.compare_exchange_weak (low, wah.getPedalLow())) {}
    storeIfBigger (pedalHigh, wah.getPedalHigh());
}

//==============================================================================
juce::AudioProcessorEditor* EasyWahAudioProcessor::createEditor()
{
    return new EasyWahAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyWahAudioProcessor();
}
#endif

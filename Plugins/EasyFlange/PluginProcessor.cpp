#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyFlangeAudioProcessor::createParameterLayout()
{
    const auto& defaults = flangePresets[0];   // "Default settings"

    juce::NormalisableRange<float> rateRange (Flanger::minRateHz, Flanger::maxRateHz, 0.01f);
    rateRange.setSkewForCentre (0.8f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> feedbackRange (-90.0f, 90.0f, 1.0f);

    const auto rateText = [] (float hz) { return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz"; };
    const auto rateFromText = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); };

    juce::StringArray noteNames;
    for (auto& note : FlangeNotes::lengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FlangeIDs::type, 1 }, "Type",
                                                              juce::StringArray { "Flanger", "Chorus", "Vibrato" }, defaults.type));
    layout.add (makeKnob (FlangeIDs::rate, 1, "Rate", rateRange, defaults.rateHz, rateText, rateFromText));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FlangeIDs::sync, 1 }, "Sync",
                                                              juce::StringArray { "Off", "Tempo" }, defaults.sync));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FlangeIDs::note, 1 }, "Note", noteNames, defaults.note));
    layout.add (makeKnob (FlangeIDs::depth,    1, "Depth",    percentRange,  defaults.depthPercent,    Format::percent));
    layout.add (makeKnob (FlangeIDs::feedback, 1, "Feedback", feedbackRange, defaults.feedbackPercent, Format::signedPercent));
    layout.add (makeKnob (FlangeIDs::width,    1, "Width",    percentRange,  defaults.widthPercent,    Format::percent));
    layout.add (makeKnob (FlangeIDs::mix,      1, "Mix",      percentRange,  defaults.mixPercent,      Format::percent));

    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyFlangeAudioProcessor::EasyFlangeAudioProcessor()
    : EasyProcessor ("EasyFlangeSettings", createParameterLayout(), makeFlangePresets(), flangePresetIDs())
{
    typeValue     = parameters.getRawParameterValue (FlangeIDs::type);
    rateValue     = parameters.getRawParameterValue (FlangeIDs::rate);
    syncValue     = parameters.getRawParameterValue (FlangeIDs::sync);
    noteValue     = parameters.getRawParameterValue (FlangeIDs::note);
    depthValue    = parameters.getRawParameterValue (FlangeIDs::depth);
    feedbackValue = parameters.getRawParameterValue (FlangeIDs::feedback);
    widthValue    = parameters.getRawParameterValue (FlangeIDs::width);
    mixValue      = parameters.getRawParameterValue (FlangeIDs::mix);
    bypassValue   = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool EasyFlangeAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

Flanger::Settings EasyFlangeAudioProcessor::getCurrentSettings() const noexcept
{
    Flanger::Settings s;
    s.type       = (Flanger::Type) juce::jlimit (0, 2, juce::roundToInt (typeValue->load()));
    s.synced     = juce::roundToInt (syncValue->load()) == 1;
    s.cycleBeats = FlangeNotes::lengths[juce::jlimit (0, FlangeNotes::count - 1, juce::roundToInt (noteValue->load()))].beats;
    s.rateHz     = rateValue->load();
    s.depth      = depthValue->load() / 100.0f;
    s.feedback   = feedbackValue->load() / 100.0f;
    s.width      = widthValue->load() / 100.0f;
    s.mix        = mixValue->load() / 100.0f;
    s.bypassed   = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyFlangeAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    flanger.prepare (sampleRate, getCurrentSettings());
}

void EasyFlangeAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

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

    const int numInputs = juce::jmax (1, juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()));
    flanger.setSettings (getCurrentSettings());
    flanger.process (buffer, numInputs, tempo.load(), songBeats);
    position.store (flanger.getPosition());
}

//==============================================================================
juce::AudioProcessorEditor* EasyFlangeAudioProcessor::createEditor()
{
    return new EasyFlangeAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyFlangeAudioProcessor();
}
#endif

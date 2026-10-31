#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyTremoloAudioProcessor::createParameterLayout()
{
    const auto& defaults = tremoloPresets[0];   // "Default settings"

    juce::NormalisableRange<float> rateRange (Tremolo::minRateHz, Tremolo::maxRateHz, 0.01f);
    rateRange.setSkewForCentre (4.0f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);

    const auto rateText = [] (float hz) { return juce::String (hz, hz < 10.0f ? 2 : 1) + " Hz"; };
    const auto rateFromText = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); };

    juce::StringArray noteNames;
    for (auto& note : TremoloNotes::lengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (TremoloIDs::rate, 1, "Rate", rateRange, defaults.rateHz, rateText, rateFromText));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TremoloIDs::sync, 1 }, "Sync",
                                                              juce::StringArray { "Off", "Tempo" }, defaults.sync));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TremoloIDs::note, 1 }, "Note", noteNames, defaults.note));
    layout.add (makeKnob (TremoloIDs::depth, 1, "Depth", percentRange, defaults.depthPercent, Format::percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TremoloIDs::shape, 1 }, "Shape",
                                                              juce::StringArray { "Sine", "Triangle", "Square", "Pump" }, defaults.shape));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { TremoloIDs::mode, 1 }, "Mode",
                                                              juce::StringArray { "Tremolo", "Auto-Pan" }, defaults.mode));

    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyTremoloAudioProcessor::EasyTremoloAudioProcessor()
    : EasyProcessor ("EasyTremoloSettings", createParameterLayout(), makeTremoloPresets(), tremoloPresetIDs())
{
    rateValue   = parameters.getRawParameterValue (TremoloIDs::rate);
    syncValue   = parameters.getRawParameterValue (TremoloIDs::sync);
    noteValue   = parameters.getRawParameterValue (TremoloIDs::note);
    depthValue  = parameters.getRawParameterValue (TremoloIDs::depth);
    shapeValue  = parameters.getRawParameterValue (TremoloIDs::shape);
    modeValue   = parameters.getRawParameterValue (TremoloIDs::mode);
    bypassValue = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool EasyTremoloAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

float EasyTremoloAudioProcessor::rateForNote (int noteIndex, double beatsPerMinute) noexcept
{
    const auto& note = TremoloNotes::lengths[juce::jlimit (0, TremoloNotes::count - 1, noteIndex)];
    return (float) (juce::jlimit (20.0, 999.0, beatsPerMinute) / 60.0 / note.beats);
}

Tremolo::Settings EasyTremoloAudioProcessor::getCurrentSettings() const noexcept
{
    Tremolo::Settings s;
    s.synced     = juce::roundToInt (syncValue->load()) == 1;
    s.cycleBeats = TremoloNotes::lengths[juce::jlimit (0, TremoloNotes::count - 1, juce::roundToInt (noteValue->load()))].beats;
    s.rateHz     = rateValue->load();
    s.depth      = depthValue->load() / 100.0f;
    s.shape      = (Tremolo::Shape) juce::jlimit (0, 3, juce::roundToInt (shapeValue->load()));
    s.mode       = (Tremolo::Mode) juce::jlimit (0, 1, juce::roundToInt (modeValue->load()));
    s.bypassed   = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyTremoloAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    tremolo.prepare (sampleRate, getCurrentSettings());
}

void EasyTremoloAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // A mono track with a stereo output: copy it across first, so AUTO-PAN has two sides to move between
    if (getTotalNumInputChannels() == 1 && buffer.getNumChannels() > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());

    // The song's tempo and where it is (to lock to the beat while it plays)
    double songBeats = -1.0;
    if (auto* transport = getPlayHead())
        if (const auto info = transport->getPosition())
        {
            if (const auto bpm = info->getBpm())
                if (*bpm >= 20.0 && *bpm <= 999.0)
                {
                    tempo.store (*bpm);
                    tempoFromDAW.store (true);
                }
            if (info->getIsPlaying())
                if (const auto ppq = info->getPpqPosition())
                    songBeats = juce::jmax (0.0, *ppq);
        }

    tremolo.setSettings (getCurrentSettings());
    tremolo.process (buffer, tempo.load(), songBeats);
    position.store (tremolo.getPosition());
}

//==============================================================================
juce::AudioProcessorEditor* EasyTremoloAudioProcessor::createEditor()
{
    return new EasyTremoloAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyTremoloAudioProcessor();
}
#endif

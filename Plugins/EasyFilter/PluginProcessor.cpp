#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyFilterAudioProcessor::createParameterLayout()
{
    const auto& defaults = filterPresets[0];   // "Default settings"

    juce::NormalisableRange<float> cutoffRange (ModFilter::minCutoffHz, ModFilter::maxCutoffHz, 1.0f);
    cutoffRange.setSkewForCentre (1000.0f);
    juce::NormalisableRange<float> rateRange (ModFilter::minRateHz, ModFilter::maxRateHz, 0.01f);
    rateRange.setSkewForCentre (1.0f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);

    const auto rateText = [] (float hz) { return juce::String (hz, hz < 1.0f ? 2 : 1) + " Hz"; };
    const auto rateFromText = [] (const juce::String& t) { return t.retainCharacters ("0123456789.").getFloatValue(); };

    juce::StringArray noteNames;
    for (auto& note : FilterNotes::lengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FilterIDs::type, 1 }, "Type",
                                                              juce::StringArray { "Low-Pass", "High-Pass", "Band-Pass" }, defaults.type));
    layout.add (makeKnob (FilterIDs::cutoff,    1, "Cutoff",    cutoffRange,  defaults.cutoffHz,         Format::hertz, Format::hertzFromText));
    layout.add (makeKnob (FilterIDs::resonance, 1, "Resonance", percentRange, defaults.resonancePercent, Format::percent));
    layout.add (makeKnob (FilterIDs::depth,     1, "Depth",     percentRange, defaults.depthPercent,     Format::percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FilterIDs::shape, 1 }, "Shape",
                                                              juce::StringArray { "Sine", "Triangle", "Ramp Up", "Ramp Down", "Square", "Random" }, defaults.shape));
    layout.add (makeKnob (FilterIDs::rate, 1, "Rate", rateRange, defaults.rateHz, rateText, rateFromText));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FilterIDs::sync, 1 }, "Sync",
                                                              juce::StringArray { "Off", "Tempo" }, defaults.sync));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { FilterIDs::note, 1 }, "Note", noteNames, defaults.note));
    layout.add (makeKnob (FilterIDs::width, 1, "Width", percentRange, defaults.widthPercent, Format::percent));
    layout.add (makeKnob (FilterIDs::mix,   1, "Mix",   percentRange, defaults.mixPercent,   Format::percent));

    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyFilterAudioProcessor::EasyFilterAudioProcessor()
    : EasyProcessor ("EasyFilterSettings", createParameterLayout(), makeFilterPresets(), filterPresetIDs())
{
    typeValue      = parameters.getRawParameterValue (FilterIDs::type);
    cutoffValue    = parameters.getRawParameterValue (FilterIDs::cutoff);
    resonanceValue = parameters.getRawParameterValue (FilterIDs::resonance);
    depthValue     = parameters.getRawParameterValue (FilterIDs::depth);
    shapeValue     = parameters.getRawParameterValue (FilterIDs::shape);
    rateValue      = parameters.getRawParameterValue (FilterIDs::rate);
    syncValue      = parameters.getRawParameterValue (FilterIDs::sync);
    noteValue      = parameters.getRawParameterValue (FilterIDs::note);
    widthValue     = parameters.getRawParameterValue (FilterIDs::width);
    mixValue       = parameters.getRawParameterValue (FilterIDs::mix);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool EasyFilterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

ModFilter::Settings EasyFilterAudioProcessor::getCurrentSettings() const noexcept
{
    ModFilter::Settings s;
    s.type       = (ModFilter::Type) juce::jlimit (0, 2, juce::roundToInt (typeValue->load()));
    s.cutoffHz   = cutoffValue->load();
    s.resonance  = resonanceValue->load() / 100.0f;
    s.depth      = depthValue->load() / 100.0f;
    s.shape      = (ModFilter::Shape) juce::jlimit (0, 5, juce::roundToInt (shapeValue->load()));
    s.synced     = juce::roundToInt (syncValue->load()) == 1;
    s.cycleBeats = FilterNotes::lengths[juce::jlimit (0, FilterNotes::count - 1, juce::roundToInt (noteValue->load()))].beats;
    s.rateHz     = rateValue->load();
    s.width      = widthValue->load() / 100.0f;
    s.mix        = mixValue->load() / 100.0f;
    s.bypassed   = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyFilterAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    rate.store (sampleRate);
    filter.prepare (sampleRate, getCurrentSettings());
}

void EasyFilterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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
    filter.setSettings (getCurrentSettings());
    filter.process (buffer, numInputs, tempo.load(), songBeats);
    position.store (filter.getPosition());
    cutoffNow[0].store (filter.getCutoffNow (0));
    cutoffNow[1].store (filter.getCutoffNow (1));
}

//==============================================================================
juce::AudioProcessorEditor* EasyFilterAudioProcessor::createEditor()
{
    return new EasyFilterAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyFilterAudioProcessor();
}
#endif

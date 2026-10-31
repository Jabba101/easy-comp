#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyDelayAudioProcessor::createParameterLayout()
{
    const auto& defaults = delayPresets[0];   // "Default settings"

    juce::NormalisableRange<float> timeRange (StereoDelay::minTimeMs, StereoDelay::maxTimeMs, 1.0f);
    timeRange.setSkewForCentre (300.0f);
    juce::NormalisableRange<float> feedbackRange (0.0f, 95.0f, 1.0f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> lowCutRange (StereoDelay::lowCutOffHz, 1000.0f, 1.0f);
    lowCutRange.setSkewForCentre (200.0f);
    juce::NormalisableRange<float> highCutRange (1000.0f, StereoDelay::highCutOffHz, 1.0f);
    highCutRange.setSkewForCentre (5000.0f);

    const auto lowCutText  = [] (float hz) { return hz <= StereoDelay::lowCutOffHz + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto highCutText = [] (float hz) { return hz >= StereoDelay::highCutOffHz - 1.0f ? juce::String ("Off") : Format::hertz (hz); };
    const auto lowCutFromText  = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? StereoDelay::lowCutOffHz : Format::hertzFromText (t); };
    const auto highCutFromText = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? StereoDelay::highCutOffHz : Format::hertzFromText (t); };

    juce::StringArray noteNames;
    for (auto& note : DelayNotes::lengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (DelayIDs::time, 1, "Time", timeRange, defaults.timeMs, Format::milliseconds));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DelayIDs::sync, 1 }, "Sync",
                                                              juce::StringArray { "Off", "Tempo" }, defaults.sync));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DelayIDs::note, 1 }, "Note", noteNames, defaults.note));
    layout.add (makeKnob (DelayIDs::feedback, 1, "Feedback", feedbackRange, defaults.feedbackPercent, Format::percent));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DelayIDs::mode, 1 }, "Mode",
                                                              juce::StringArray { "Stereo", "Ping-Pong", "Wide" }, defaults.mode));
    layout.add (makeKnob (DelayIDs::lowCut, 1, "Low Cut", lowCutRange, defaults.lowCutHz, lowCutText, lowCutFromText));
    layout.add (makeKnob (DelayIDs::highCut, 1, "High Cut", highCutRange, defaults.highCutHz, highCutText, highCutFromText));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { DelayIDs::character, 1 }, "Character",
                                                              juce::StringArray { "Clean", "Tape" }, defaults.tape));
    layout.add (makeKnob (DelayIDs::duck, 1, "Duck", percentRange, defaults.duckPercent, Format::percent));
    layout.add (makeKnob (DelayIDs::mix,  1, "Mix",  percentRange, defaults.mixPercent,  Format::percent));

    addCommonParameters (layout, false);   // BYPASS (echoes add sound, so AUTO-GAIN makes no sense)
    return layout;
}

//==============================================================================
EasyDelayAudioProcessor::EasyDelayAudioProcessor()
    : EasyProcessor ("EasyDelaySettings", createParameterLayout(), makeDelayPresets(), delayPresetIDs())
{
    timeValue      = parameters.getRawParameterValue (DelayIDs::time);
    syncValue      = parameters.getRawParameterValue (DelayIDs::sync);
    noteValue      = parameters.getRawParameterValue (DelayIDs::note);
    feedbackValue  = parameters.getRawParameterValue (DelayIDs::feedback);
    modeValue      = parameters.getRawParameterValue (DelayIDs::mode);
    lowCutValue    = parameters.getRawParameterValue (DelayIDs::lowCut);
    highCutValue   = parameters.getRawParameterValue (DelayIDs::highCut);
    characterValue = parameters.getRawParameterValue (DelayIDs::character);
    duckValue      = parameters.getRawParameterValue (DelayIDs::duck);
    mixValue       = parameters.getRawParameterValue (DelayIDs::mix);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool EasyDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

double EasyDelayAudioProcessor::getTailLengthSeconds() const
{
    return StereoDelay::tailSeconds (getCurrentSettings());
}

float EasyDelayAudioProcessor::timeForNote (int noteIndex, double beatsPerMinute) noexcept
{
    const auto& note = DelayNotes::lengths[juce::jlimit (0, DelayNotes::count - 1, noteIndex)];
    const double ms = note.beats * 60000.0 / juce::jlimit (20.0, 999.0, beatsPerMinute);
    return (float) juce::jlimit ((double) StereoDelay::minTimeMs, (double) StereoDelay::maxTimeMs, ms);
}

StereoDelay::Settings EasyDelayAudioProcessor::getCurrentSettings() const noexcept
{
    StereoDelay::Settings s;
    s.timeMs    = juce::roundToInt (syncValue->load()) == 1 ? timeForNote (juce::roundToInt (noteValue->load()), tempo.load())
                                                            : timeValue->load();
    s.mode      = (StereoDelay::Mode) juce::jlimit (0, 2, juce::roundToInt (modeValue->load()));
    s.feedback  = feedbackValue->load() / 100.0f;
    s.lowCutHz  = lowCutValue->load();
    s.highCutHz = highCutValue->load();
    s.tape      = juce::roundToInt (characterValue->load()) == 1;
    s.duck      = duckValue->load() / 100.0f;
    s.mix       = mixValue->load() / 100.0f;
    s.bypassed  = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyDelayAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    delay.prepare (sampleRate, getCurrentSettings());
}

void EasyDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // The song's tempo, for SYNC (kept from last time if the DAW doesn't say)
    if (auto* transport = getPlayHead())
        if (const auto position = transport->getPosition())
            if (const auto bpm = position->getBpm())
                if (*bpm >= 20.0 && *bpm <= 999.0)
                {
                    tempo.store (*bpm);
                    tempoFromDAW.store (true);
                }

    const int numInputs = juce::jmax (1, juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()));
    float in = 0.0f;
    for (int ch = 0; ch < numInputs; ++ch)
        in = juce::jmax (in, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
    storeIfBigger (inputPeak, in);

    delay.setSettings (getCurrentSettings());
    delay.process (buffer, numInputs);
    storeIfBigger (echoPeak, delay.getEchoPeak());
}

//==============================================================================
juce::AudioProcessorEditor* EasyDelayAudioProcessor::createEditor()
{
    return new EasyDelayAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyDelayAudioProcessor();
}
#endif

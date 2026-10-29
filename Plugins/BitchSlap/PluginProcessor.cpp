#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout BitchSlapAudioProcessor::createParameterLayout()
{
    const auto& defaults = slapPresets[0];   // "Default settings"

    juce::NormalisableRange<float> mixRange (0.0f, 100.0f, 1.0f);

    // (The skew puts 300 ms at the knob's halfway point, where the useful lengths are.)
    juce::NormalisableRange<float> lengthRange (GatedReverb::minLengthMs, GatedReverb::maxLengthMs, 1.0f);
    lengthRange.setSkewForCentre (300.0f);

    juce::StringArray noteNames;
    for (auto& note : noteLengths)
        noteNames.add (note.name);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (SlapIDs::mix,    1, "Mix",    mixRange,    defaults.mixPercent, Format::percent));
    layout.add (makeKnob (SlapIDs::length, 1, "Length", lengthRange, defaults.lengthMs,   Format::milliseconds));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SlapIDs::sync, 1 }, "Sync",
                                                              juce::StringArray { "Off", "Tempo" }, defaults.sync));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { SlapIDs::note, 1 }, "Note",
                                                              noteNames, defaults.note));

    addCommonParameters (layout, false);   // BYPASS (a reverb adds sound, so AUTO-GAIN makes no sense)
    return layout;
}

//==============================================================================
BitchSlapAudioProcessor::BitchSlapAudioProcessor()
    : EasyProcessor ("BitchSlapSettings", createParameterLayout(), makeSlapPresets(), slapPresetIDs())
{
    mixValue    = parameters.getRawParameterValue (SlapIDs::mix);
    lengthValue = parameters.getRawParameterValue (SlapIDs::length);
    syncValue   = parameters.getRawParameterValue (SlapIDs::sync);
    noteValue   = parameters.getRawParameterValue (SlapIDs::note);
    bypassValue = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool BitchSlapAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

double BitchSlapAudioProcessor::getTailLengthSeconds() const
{
    return (getCurrentSettings().lengthMs + 50.0) / 1000.0;
}

float BitchSlapAudioProcessor::lengthForNote (int noteIndex, double beatsPerMinute) noexcept
{
    const auto& note = noteLengths[juce::jlimit (0, numNoteLengths - 1, noteIndex)];
    const double ms = note.beats * 60000.0 / juce::jlimit (20.0, 999.0, beatsPerMinute);
    return (float) juce::jlimit ((double) GatedReverb::minLengthMs, (double) GatedReverb::maxLengthMs, ms);
}

GatedReverb::Settings BitchSlapAudioProcessor::getCurrentSettings() const noexcept
{
    GatedReverb::Settings settings;
    settings.mix = mixValue->load() / 100.0f;   // the knob shows %, the maths wants 0..1
    settings.lengthMs = juce::roundToInt (syncValue->load()) == syncTempo
                          ? lengthForNote (juce::roundToInt (noteValue->load()), tempo.load())
                          : lengthValue->load();
    settings.bypassed = bypassValue->load() > 0.5f;
    return settings;
}

//==============================================================================
void BitchSlapAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    reverb.prepare (sampleRate, getMainBusNumOutputChannels(), getCurrentSettings());

    momentLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));   // 5 ms
    momentFilled = 0;
    momentIn = momentWet = 0.0f;
}

void BitchSlapAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    reverb.setSettings (getCurrentSettings());

    // A stretch at a time, ending where each 5 ms moment of the display ends.
    // (Doesn't change the sound: the reverb works the same either way.)
    const int numInputs  = juce::jmax (1, juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()));
    const int numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples;)
    {
        const int length = juce::jmin (numSamples - start, momentLength - momentFilled);

        float inPeak = 0.0f;
        for (int channel = 0; channel < numInputs; ++channel)
            inPeak = juce::jmax (inPeak, buffer.getMagnitude (channel, start, length));

        juce::AudioBuffer<float> stretch (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, length);
        const auto result = reverb.process (stretch, numInputs);

        momentIn  = juce::jmax (momentIn, inPeak);
        momentWet = juce::jmax (momentWet, result.wetPeak);
        momentFilled += length;

        if (momentFilled >= momentLength)
        {
            // (If the window isn't reading them - e.g. it's closed - the moment is simply dropped.)
            if (momentFifo.getFreeSpace() > 0)
            {
                const auto write = momentFifo.write (1);
                moments[(size_t) write.startIndex1] = { juce::Decibels::gainToDecibels (momentIn, -100.0f),
                                                        juce::Decibels::gainToDecibels (momentWet, -100.0f) };
            }

            momentFilled = 0;
            momentIn = momentWet = 0.0f;
        }

        start += length;
    }
}

int BitchSlapAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];

    return written;
}

//==============================================================================
juce::AudioProcessorEditor* BitchSlapAudioProcessor::createEditor()
{
    return new BitchSlapAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BitchSlapAudioProcessor();
}
#endif

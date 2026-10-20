#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyTransientAudioProcessor::createParameterLayout()
{
    const auto& defaults = transientPresets[0];   // "Default settings"

    juce::NormalisableRange<float> shapeRange (-100.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> outputRange (-12.0f, 12.0f, 0.1f);
    juce::NormalisableRange<float> mixRange (0.0f, 100.0f, 1.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (makeKnob (TransientIDs::punch,   1, "Punch",   shapeRange,  defaults.punchPercent,   Format::signedPercent));
    layout.add (makeKnob (TransientIDs::sustain, 1, "Sustain", shapeRange,  defaults.sustainPercent, Format::signedPercent));
    layout.add (makeKnob (TransientIDs::output,  1, "Output",  outputRange, defaults.outputDb,       Format::signedDecibels));
    layout.add (makeKnob (TransientIDs::mix,     1, "Mix",     mixRange,    defaults.mixPercent,     Format::percent));

    addCommonParameters (layout);   // BYPASS and AUTO-GAIN
    return layout;
}

//==============================================================================
EasyTransientAudioProcessor::EasyTransientAudioProcessor()
    : EasyProcessor ("EasyTransientSettings", createParameterLayout(), makeTransientPresets(), transientPresetIDs())
{
    punchValue    = parameters.getRawParameterValue (TransientIDs::punch);
    sustainValue  = parameters.getRawParameterValue (TransientIDs::sustain);
    outputValue   = parameters.getRawParameterValue (TransientIDs::output);
    mixValue      = parameters.getRawParameterValue (TransientIDs::mix);
    autoGainValue = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue   = parameters.getRawParameterValue (CommonIDs::bypass);
}

//==============================================================================
void EasyTransientAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    shaper.prepare (sampleRate, getCurrentSettings());
    meterBoostDb.store (0.0f);
    meterCutDb.store (0.0f);
    autoGainDb.store (0.0f);
    autoGainRestartRequested.store (false);

    momentLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));   // 5 ms
    momentFilled = 0;
    momentIn = momentOut = 0.0f;
}

void EasyTransientAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    shaper.setSettings (getCurrentSettings());

    if (autoGainRestartRequested.exchange (false))
        shaper.restartAutoGain();   // a preset was just loaded

    // Work through the block in 5 ms moments, noting how loud each was going
    // in and coming out (for the display). This doesn't change the sound:
    // the shaper works sample by sample either way.
    const int numSamples = buffer.getNumSamples();

    for (int start = 0; start < numSamples;)
    {
        const int length = juce::jmin (numSamples - start, momentLength - momentFilled);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, length);

        momentIn = juce::jmax (momentIn, part.getMagnitude (0, length));
        const auto result = shaper.process (part);
        momentOut = juce::jmax (momentOut, part.getMagnitude (0, length));

        storeIfBigger (meterBoostDb, result.maxBoostDb);
        storeIfBigger (meterCutDb, result.maxCutDb);
        autoGainDb.store (result.autoGainDb);

        momentFilled += length;
        start += length;

        if (momentFilled >= momentLength)
        {
            // (If the window isn't reading them - e.g. it's closed - the moment is simply dropped.)
            if (momentFifo.getFreeSpace() > 0)
            {
                const auto write = momentFifo.write (1);
                moments[(size_t) write.startIndex1] = { juce::Decibels::gainToDecibels (momentIn, TransientShaper::silenceDb),
                                                        juce::Decibels::gainToDecibels (momentOut, TransientShaper::silenceDb) };
            }

            momentFilled = 0;
            momentIn = momentOut = 0.0f;
        }
    }
}

int EasyTransientAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];

    return written;
}

TransientShaper::Settings EasyTransientAudioProcessor::getCurrentSettings() const noexcept
{
    TransientShaper::Settings settings;
    settings.punch    = punchValue->load() / 100.0f;     // the knobs show %, the maths wants -1..+1
    settings.sustain  = sustainValue->load() / 100.0f;
    settings.outputDb = outputValue->load();
    settings.mix      = mixValue->load() / 100.0f;
    settings.autoGain = autoGainValue->load() > 0.5f;
    settings.bypassed = bypassValue->load() > 0.5f;
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyTransientAudioProcessor::createEditor()
{
    return new EasyTransientAudioProcessorEditor (*this);
}

// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyTransientAudioProcessor();
}

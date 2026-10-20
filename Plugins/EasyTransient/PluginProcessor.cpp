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

    // The display shows how loud each 5 ms moment was going in and coming
    // out. So: note how loud each stretch of the block is going in, shape
    // the sound (in one go - quicker than a stretch at a time, and in
    // silence it can usually skip straight through), then note how loud each
    // stretch came out. (Very long blocks are taken a few dozen moments at a
    // time.) None of this changes the sound: the shaper works sample by
    // sample either way.
    const int numSamples = buffer.getNumSamples();
    constexpr int maxStretches = 32;

    for (int spanStart = 0; spanStart < numSamples;)
    {
        // The stretches: from one moment's end to the next
        int lengths[maxStretches];
        float levelsIn[maxStretches];
        int numStretches = 0, spanLength = 0;

        for (int filled = momentFilled; spanStart + spanLength < numSamples && numStretches < maxStretches;)
        {
            const int length = juce::jmin (numSamples - (spanStart + spanLength), momentLength - filled);
            lengths[numStretches] = length;
            levelsIn[numStretches] = buffer.getMagnitude (spanStart + spanLength, length);
            ++numStretches;
            spanLength += length;
            filled = 0;
        }

        juce::AudioBuffer<float> span (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), spanStart, spanLength);
        const auto result = shaper.process (span);

        storeIfBigger (meterBoostDb, result.maxBoostDb);
        storeIfBigger (meterCutDb, result.maxCutDb);
        autoGainDb.store (result.autoGainDb);

        for (int k = 0, start = spanStart; k < numStretches; start += lengths[k++])
        {
            momentIn  = juce::jmax (momentIn, levelsIn[k]);
            momentOut = juce::jmax (momentOut, buffer.getMagnitude (start, lengths[k]));
            momentFilled += lengths[k];

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

        spanStart += spanLength;
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

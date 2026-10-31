#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyLimiterAudioProcessor::createParameterLayout()
{
    const auto& defaults = limiterPresets[0];   // "Default settings"

    juce::NormalisableRange<float> gainRange (0.0f, TruePeakLimiter::maxGainDb, 0.1f);
    juce::NormalisableRange<float> ceilingRange (TruePeakLimiter::minCeilingDb, TruePeakLimiter::maxCeilingDb, 0.1f);
    juce::NormalisableRange<float> releaseRange (TruePeakLimiter::minReleaseMs, TruePeakLimiter::maxReleaseMs, 1.0f);
    releaseRange.setSkewForCentre (100.0f);

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (LimiterIDs::gain,    1, "Gain",    gainRange,    defaults.gainDb,    Format::signedDecibels));
    layout.add (makeKnob (LimiterIDs::ceiling, 1, "Ceiling", ceilingRange, defaults.ceilingDb,
                          [] (float db) { return juce::String (db, 1) + " dBTP"; }));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { LimiterIDs::releaseMode, 1 }, "Release Mode",
                                                              juce::StringArray { "Auto", "Set" }, defaults.releaseMode));
    layout.add (makeKnob (LimiterIDs::release, 1, "Release", releaseRange, defaults.releaseMs, Format::milliseconds));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { LimiterIDs::match, 1 }, "Match", false));

    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyLimiterAudioProcessor::EasyLimiterAudioProcessor()
    : EasyProcessor ("EasyLimiterSettings", createParameterLayout(), makeLimiterPresets(), limiterPresetIDs())
{
    gainValue        = parameters.getRawParameterValue (LimiterIDs::gain);
    ceilingValue     = parameters.getRawParameterValue (LimiterIDs::ceiling);
    releaseModeValue = parameters.getRawParameterValue (LimiterIDs::releaseMode);
    releaseValue     = parameters.getRawParameterValue (LimiterIDs::release);
    matchValue       = parameters.getRawParameterValue (LimiterIDs::match);
    bypassValue      = parameters.getRawParameterValue (CommonIDs::bypass);
}

bool EasyLimiterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input  = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return input == output || (input == juce::AudioChannelSet::mono() && output == juce::AudioChannelSet::stereo());
}

TruePeakLimiter::Settings EasyLimiterAudioProcessor::getCurrentSettings() const noexcept
{
    TruePeakLimiter::Settings s;
    s.gainDb      = gainValue->load();
    s.ceilingDb   = ceilingValue->load();
    s.autoRelease = juce::roundToInt (releaseModeValue->load()) == 0;
    s.releaseMs   = releaseValue->load();
    s.match       = matchValue->load() > 0.5f;
    s.bypassed    = bypassValue->load() > 0.5f;
    return s;
}

//==============================================================================
void EasyLimiterAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    limiter.prepare (sampleRate, juce::jmax (1, getMainBusNumOutputChannels()), getCurrentSettings());
    setLatencySamples (limiter.getLatencySamples());
    momentLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.005));
    momentFilled = 0;
}

void EasyLimiterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto settings = getCurrentSettings();
    limiter.setSettings (settings);

    const int numInputs = juce::jmax (1, juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()));
    const int numSamples = buffer.getNumSamples();
    const float gainLinear = juce::Decibels::decibelsToGain (settings.gainDb);

    // A stretch at a time, ending where each 5 ms moment of the display ends (the sound is the same either way)
    for (int start = 0; start < numSamples;)
    {
        const int length = juce::jmin (numSamples - start, momentLength - momentFilled);

        float inPeak = 0.0f;
        for (int c = 0; c < numInputs; ++c)
            inPeak = juce::jmax (inPeak, buffer.getMagnitude (c, start, length));

        juce::AudioBuffer<float> stretch (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, length);
        limiter.process (stretch, numInputs);

        momentIn = juce::jmax (momentIn, inPeak * gainLinear);
        momentOut = juce::jmax (momentOut, limiter.getOutputPeakDb());
        momentReduction = juce::jmax (momentReduction, limiter.getReductionDb());
        highestPeak.store (juce::jmax (highestPeak.load(), limiter.getOutputPeakDb()));
        momentFilled += length;

        if (momentFilled >= momentLength)
        {
            if (momentFifo.getFreeSpace() > 0)
            {
                const auto write = momentFifo.write (1);
                moments[(size_t) write.startIndex1] = { juce::Decibels::gainToDecibels (momentIn, -100.0f), momentOut, momentReduction };
            }
            momentFilled = 0;
            momentIn = 0.0f;
            momentOut = -100.0f;
            momentReduction = 0.0f;
        }

        start += length;
    }
}

int EasyLimiterAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;
    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];
    return written;
}

//==============================================================================
juce::AudioProcessorEditor* EasyLimiterAudioProcessor::createEditor()
{
    return new EasyLimiterAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyLimiterAudioProcessor();
}
#endif

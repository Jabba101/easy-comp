#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyCompAudioProcessor::createParameterLayout()
{
    const auto& defaults = compressorPresets[0];   // "Default settings"

    // Knob ranges. "Skew" makes a knob spend more of its turn on the useful
    // values: e.g. the ratio knob's halfway point is 4:1, not 10.5:1.
    juce::NormalisableRange<float> thresholdRange (-60.0f, 0.0f, 0.1f);

    juce::NormalisableRange<float> ratioRange (1.0f, 20.0f, 0.1f);
    ratioRange.setSkewForCentre (4.0f);

    juce::NormalisableRange<float> attackRange (0.1f, 100.0f, 0.01f);
    attackRange.setSkewForCentre (10.0f);

    juce::NormalisableRange<float> releaseRange (10.0f, 1000.0f, 1.0f);
    releaseRange.setSkewForCentre (100.0f);

    juce::NormalisableRange<float> makeupRange (0.0f, 24.0f, 0.1f);
    juce::NormalisableRange<float> mixRange (0.0f, 100.0f, 1.0f);

    juce::NormalisableRange<float> lowCutRange (Compressor::lowCutOffHz, 500.0f, 1.0f);
    lowCutRange.setSkewForCentre (100.0f);

    const auto lowCutText = [] (float hz)
    {
        return hz <= Compressor::lowCutOffHz + 0.5f ? juce::String ("Off") : juce::String (juce::roundToInt (hz)) + " Hz";
    };

    const auto lowCutFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? Compressor::lowCutOffHz : text.getFloatValue();
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (CompIDs::threshold, 1, "Threshold",    thresholdRange, defaults.thresholdDb, Format::decibels));
    layout.add (makeKnob (CompIDs::ratio,     1, "Ratio",        ratioRange,     defaults.ratio,       Format::ratio));
    layout.add (makeKnob (CompIDs::attack,    1, "Attack",       attackRange,    defaults.attackMs,    Format::milliseconds));
    layout.add (makeKnob (CompIDs::release,   1, "Release",      releaseRange,   defaults.releaseMs,   Format::milliseconds));
    layout.add (makeKnob (CompIDs::makeup,    1, "Make-up Gain", makeupRange,    defaults.makeupDb,    Format::signedDecibels));
    layout.add (makeKnob (CompIDs::mix,       1, "Mix",          mixRange,       defaults.mixPercent,  Format::percent));
    layout.add (makeKnob (CompIDs::lowCut,    2, "Ignore Below", lowCutRange,    defaults.lowCutHz,    lowCutText, lowCutFromText));

    addCommonParameters (layout);   // BYPASS and AUTO-GAIN

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { CompIDs::reactsTo, 4 }, "Reacts To",
                                                              juce::StringArray { "Peaks", "Average" }, (int) defaults.reactsTo));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { CompIDs::character, 5 }, "Style",
                                                              juce::StringArray { "Modern", "Vintage" }, (int) defaults.style));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { CompIDs::lowEnd, 5 }, "Low End",
                                                              juce::StringArray { "Normal", "Thrust" }, (int) defaults.lowEnd));
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { CompIDs::listensTo, 5 }, "Listens To",
                                                              juce::StringArray { "This Track", "Sidechain" }, (int) ListensTo::listensToTrack));
    return layout;
}

//==============================================================================
EasyCompAudioProcessor::EasyCompAudioProcessor()
    : EasyProcessor ("EasyCompSettings", createParameterLayout(), makeCompressorPresets(), compressorPresetIDs(), true)
{
    thresholdValue = parameters.getRawParameterValue (CompIDs::threshold);
    ratioValue     = parameters.getRawParameterValue (CompIDs::ratio);
    attackValue    = parameters.getRawParameterValue (CompIDs::attack);
    releaseValue   = parameters.getRawParameterValue (CompIDs::release);
    makeupValue    = parameters.getRawParameterValue (CompIDs::makeup);
    mixValue       = parameters.getRawParameterValue (CompIDs::mix);
    lowCutValue    = parameters.getRawParameterValue (CompIDs::lowCut);
    reactsToValue  = parameters.getRawParameterValue (CompIDs::reactsTo);
    characterValue = parameters.getRawParameterValue (CompIDs::character);
    lowEndValue    = parameters.getRawParameterValue (CompIDs::lowEnd);
    listensToValue = parameters.getRawParameterValue (CompIDs::listensTo);
    autoGainValue  = parameters.getRawParameterValue (CommonIDs::autoGain);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);
}

//==============================================================================
void EasyCompAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    compressor.prepare (sampleRate, getCurrentSettings());
    meterReductionDb.store (0.0f);
    graphLevelDb.store (Compressor::silenceDb);
    autoGainDb.store (0.0f);
    autoGainRestartRequested.store (false);
    sidechainMissing.store (false);
    samplesSinceTrackSound = samplesSinceSidechainSound = 1 << 30;
}

void EasyCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // avoids CPU spikes from ultra-tiny numbers

    // Your track (in, and once compressed, out), and the sidechain - with no
    // channels if the DAW has switched it off (or, to be safe, hasn't sent it).
    auto track = getBusBuffer (buffer, false, 0);
    const bool sidechainSent = getBusCount (true) > 1
                            && buffer.getNumChannels() >= getChannelIndexInProcessBlockBuffer (true, 1, 0) + getChannelCountOfBus (true, 1);
    auto sidechain = sidechainSent ? getBusBuffer (buffer, true, 1)
                                   : juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(), 0, buffer.getNumSamples());
    const bool wantsSidechain = usesSidechain();

    compressor.setSettings (getCurrentSettings());

    if (autoGainRestartRequested.exchange (false))
        compressor.restartAutoGain();   // a preset was just loaded

    // LISTENS TO: SIDECHAIN with nothing coming in can't compress, so the
    // window says so (once your track has been playing for a moment without it).
    if (wantsSidechain)
    {
        const int n = buffer.getNumSamples();
        const bool sidechainHasSound = sidechain.getNumChannels() > 0 && ! FastMath::isSilent (sidechain);
        samplesSinceTrackSound     = FastMath::isSilent (track) ? juce::jmin (samplesSinceTrackSound + n, 1 << 30) : 0;
        samplesSinceSidechainSound = sidechainHasSound ? 0 : juce::jmin (samplesSinceSidechainSound + n, 1 << 30);

        const int patience = (int) (1.5 * currentSampleRate);
        sidechainMissing.store (samplesSinceSidechainSound > patience && samplesSinceTrackSound < patience);
    }
    else
    {
        sidechainMissing.store (false);
        samplesSinceSidechainSound = 1 << 30;
    }

    const auto result = compressor.process (track, wantsSidechain ? &sidechain : nullptr);

    // Keep the biggest values until the meter and graph next ask for them.
    storeIfBigger (meterReductionDb, result.maxReductionDb);
    storeIfBigger (graphLevelDb, result.maxLevelDb);
    autoGainDb.store (result.autoGainDb);
}

Compressor::Settings EasyCompAudioProcessor::getCurrentSettings() const noexcept
{
    Compressor::Settings settings;
    settings.thresholdDb = thresholdValue->load();
    settings.ratio       = ratioValue->load();
    settings.attackMs    = attackValue->load();
    settings.releaseMs   = releaseValue->load();
    settings.makeupDb    = makeupValue->load();
    settings.mix         = mixValue->load() / 100.0f;   // the knob shows %, the maths wants 0..1
    settings.lowCutHz    = lowCutValue->load();
    settings.reactToAverage = reactsToValue->load() > 0.5f;
    settings.vintage     = characterValue->load() > 0.5f;
    settings.thrust      = lowEndValue->load() > 0.5f;
    settings.autoGain    = autoGainValue->load() > 0.5f;
    settings.bypassed    = bypassValue->load() > 0.5f;
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyCompAudioProcessor::createEditor()
{
    return new EasyCompAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyCompAudioProcessor();
}
#endif

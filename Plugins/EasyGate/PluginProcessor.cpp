#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyGateAudioProcessor::createParameterLayout()
{
    const auto& defaults = gatePresets[0];   // "Default settings"

    // Knob ranges. "Skew" makes a knob spend more of its turn on the useful
    // values: e.g. the RELEASE knob's halfway point is 150 ms, not 1000 ms.
    juce::NormalisableRange<float> thresholdRange (-80.0f, 0.0f, 0.1f);
    juce::NormalisableRange<float> rangeRange (0.0f, Gate::fullRangeDb, 0.1f);

    juce::NormalisableRange<float> attackRange (0.1f, 50.0f, 0.01f);
    attackRange.setSkewForCentre (2.0f);

    juce::NormalisableRange<float> holdRange (0.0f, 2000.0f, 1.0f);
    holdRange.setSkewForCentre (100.0f);

    juce::NormalisableRange<float> releaseRange (5.0f, 2000.0f, 1.0f);
    releaseRange.setSkewForCentre (150.0f);

    juce::NormalisableRange<float> lowCutRange (Gate::lowCutOffHz, 5000.0f, 1.0f);
    lowCutRange.setSkewForCentre (250.0f);

    juce::NormalisableRange<float> highCutRange (50.0f, Gate::highCutOffHz, 1.0f);
    highCutRange.setSkewForCentre (2000.0f);

    // RANGE reads "Full" at the top (silence while closed); the filters read "Off" at their ends.
    const auto rangeText = [] (float db)  { return db >= Gate::fullRangeDb - 0.05f ? juce::String ("Full") : Format::decibels (db); };
    const auto rangeFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("full") ? Gate::fullRangeDb : std::abs (text.getFloatValue());   // ("-40" means 40 too)
    };

    const auto lowCutText = [] (float hz)  { return hz <= Gate::lowCutOffHz + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto lowCutFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? Gate::lowCutOffHz : Format::hertzFromText (text);
    };

    const auto highCutText = [] (float hz)  { return hz >= Gate::highCutOffHz - 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto highCutFromText = [] (const juce::String& text)
    {
        return text.trim().equalsIgnoreCase ("off") ? Gate::highCutOffHz : Format::hertzFromText (text);
    };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (GateIDs::threshold, 1, "Threshold",    thresholdRange, defaults.thresholdDb, Format::decibels));
    layout.add (makeKnob (GateIDs::range,     1, "Range",        rangeRange,     defaults.rangeDb,     rangeText, rangeFromText));
    layout.add (makeKnob (GateIDs::attack,    1, "Attack",       attackRange,    defaults.attackMs,    Format::milliseconds));
    layout.add (makeKnob (GateIDs::hold,      1, "Hold",         holdRange,      defaults.holdMs,      Format::milliseconds));
    layout.add (makeKnob (GateIDs::release,   1, "Release",      releaseRange,   defaults.releaseMs,   Format::milliseconds));
    layout.add (makeKnob (GateIDs::lowCut,    1, "Ignore Below", lowCutRange,    defaults.lowCutHz,    lowCutText, lowCutFromText));
    layout.add (makeKnob (GateIDs::highCut,   1, "Ignore Above", highCutRange,   defaults.highCutHz,   highCutText, highCutFromText));

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { GateIDs::openedBy, 1 }, "Opened By",
                                                              juce::StringArray { "This Track", "Sidechain" }, (int) OpenedBy::thisTrack));

    addCommonParameters (layout, false);   // BYPASS (a gate doesn't need AUTO-GAIN)
    return layout;
}

//==============================================================================
EasyGateAudioProcessor::EasyGateAudioProcessor()
    : EasyProcessor ("EasyGateSettings", createParameterLayout(), makeGatePresets(), gatePresetIDs(), true)
{
    thresholdValue = parameters.getRawParameterValue (GateIDs::threshold);
    rangeValue     = parameters.getRawParameterValue (GateIDs::range);
    attackValue    = parameters.getRawParameterValue (GateIDs::attack);
    holdValue      = parameters.getRawParameterValue (GateIDs::hold);
    releaseValue   = parameters.getRawParameterValue (GateIDs::release);
    lowCutValue    = parameters.getRawParameterValue (GateIDs::lowCut);
    highCutValue   = parameters.getRawParameterValue (GateIDs::highCut);
    openedByValue  = parameters.getRawParameterValue (GateIDs::openedBy);
    bypassValue    = parameters.getRawParameterValue (CommonIDs::bypass);

    // The look-ahead, until the DAW says which sample rate it's using
    setLatencySamples ((int) std::lround (Gate::lookAheadSeconds * 44100.0));
}

//==============================================================================
void EasyGateAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
    gate.prepare (sampleRate, juce::jmax (1, juce::roundToInt (sampleRate * 0.005)), getCurrentSettings());   // 5 ms moments

    // The sound comes out 2 ms late (the look-ahead): telling the DAW lets it
    // play this track 2 ms early to make up for it, so everything stays in time.
    setLatencySamples (gate.getLatencySamples());

    lightOpen.store (false);
    lightGainDb.store (Gate::silenceDb);
    sidechainMissing.store (false);
    samplesSinceTrackSound = samplesSinceSidechainSound = 1 << 30;
}

void EasyGateAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // avoids CPU spikes from ultra-tiny numbers

    // Your track (in, and once gated, out), and the sidechain - with no
    // channels if the DAW has switched it off (or, to be safe, hasn't sent it).
    auto track = getBusBuffer (buffer, false, 0);
    const bool sidechainSent = buffer.getNumChannels() >= getChannelIndexInProcessBlockBuffer (true, 1, 0) + getChannelCountOfBus (true, 1);
    auto sidechain = sidechainSent ? getBusBuffer (buffer, true, 1)
                                   : juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(), 0, buffer.getNumSamples());
    const bool wantsSidechain = usesSidechain();

    gate.setSettings (getCurrentSettings());

    // OPENED BY: SIDECHAIN with nothing coming in can't open, so the window
    // says so (once your track has been playing for a moment without it).
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

    const auto result = gate.process (track, wantsSidechain ? &sidechain : nullptr,
                                      [this] (const Gate::Moment& moment) { pushMoment (moment); });

    if (result.open)
        lightOpen.store (true);

    storeIfBigger (lightGainDb, FastMath::gainToDecibels (result.gain, Gate::silenceDb));
}

void EasyGateAudioProcessor::pushMoment (const Gate::Moment& gateMoment) noexcept
{
    const Moment moment { FastMath::gainToDecibels (gateMoment.keyLevel, Gate::silenceDb),
                          FastMath::gainToDecibels (gateMoment.outLevel, Gate::silenceDb),
                          FastMath::gainToDecibels (gateMoment.gain, Gate::silenceDb),
                          gateMoment.open };

    // (So even a quick hit between two looks shows on the light.)
    if (moment.open)
        lightOpen.store (true);

    storeIfBigger (lightGainDb, moment.gainDb);

    // (If the window isn't reading them - e.g. it's closed - the moment is simply dropped.)
    if (momentFifo.getFreeSpace() > 0)
    {
        const auto write = momentFifo.write (1);
        moments[(size_t) write.startIndex1] = moment;
    }
}

int EasyGateAudioProcessor::takeMoments (Moment* destination, int maxMoments) noexcept
{
    const auto read = momentFifo.read (juce::jmin (maxMoments, momentFifo.getNumReady()));
    int written = 0;

    for (int i = 0; i < read.blockSize1; ++i)  destination[written++] = moments[(size_t) (read.startIndex1 + i)];
    for (int i = 0; i < read.blockSize2; ++i)  destination[written++] = moments[(size_t) (read.startIndex2 + i)];

    return written;
}

Gate::Settings EasyGateAudioProcessor::getCurrentSettings() const noexcept
{
    Gate::Settings settings;
    settings.thresholdDb = thresholdValue->load();
    settings.rangeDb     = rangeValue->load();
    settings.attackMs    = attackValue->load();
    settings.holdMs      = holdValue->load();
    settings.releaseMs   = releaseValue->load();
    settings.lowCutHz    = lowCutValue->load();
    settings.highCutHz   = highCutValue->load();
    settings.listen      = listening.load();
    settings.bypassed    = bypassValue->load() > 0.5f;
    return settings;
}

//==============================================================================
juce::AudioProcessorEditor* EasyGateAudioProcessor::createEditor()
{
    return new EasyGateAudioProcessorEditor (*this);
}

#if ! EASY_STRIP_MODULE   // (inside the Easy Strip, this plugin is one of its modules, not a plugin of its own)
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyGateAudioProcessor();
}
#endif

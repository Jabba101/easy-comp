#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include <optional>

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
    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { CompIDs::worksOn, 6 }, "Works On",
                                                              juce::StringArray { "Stereo", "Mid", "Side", "Mid+Side" }, (int) worksOnStereo));
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
    worksOnValue   = parameters.getRawParameterValue (CompIDs::worksOn);
}

//==============================================================================
void EasyCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    compressor.prepare (sampleRate, getCurrentSettings());
    sideCompressor.prepare (sampleRate, getCurrentSettings());
    middle.setSize (1, juce::jmax (1, samplesPerBlock));
    sides.setSize (1, juce::jmax (1, samplesPerBlock));
    playingWorksOn = getWorksOn();
    changeFade = 1.0f;
    changingDown = false;
    midSideBypass.reset (sampleRate, 0.02);
    midSideBypass.setCurrentAndTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);
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
    sideCompressor.setSettings (getCurrentSettings());

    if (autoGainRestartRequested.exchange (false))
    {
        compressor.restartAutoGain();   // a preset was just loaded
        sideCompressor.restartAutoGain();
    }

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

    if (playingWorksOn == worksOnStereo)   // (so a switch to MID / SIDE starts from the right place)
        midSideBypass.setCurrentAndTargetValue (bypassValue->load() > 0.5f ? 1.0f : 0.0f);

    // WORKS ON: STEREO as always; the others split the track into middle and sides
    const auto wanted = track.getNumChannels() >= 2 ? getWorksOn() : worksOnStereo;   // (a mono track has no sides)
    if (wanted != playingWorksOn && ! changingDown)
        changingDown = true;   // (dip, switch, come back up)

    Compressor::BlockResult result;
    if (playingWorksOn == worksOnStereo)
        result = compressor.process (track, wantsSidechain ? &sidechain : nullptr);
    else
        result = processMidSide (track, wantsSidechain ? &sidechain : nullptr, playingWorksOn);

    if (changingDown || changeFade < 1.0f)
    {
        // A 10 ms dip to silence and back, switching at the bottom (no click, however different the two sound)
        const int n = track.getNumSamples();
        const float step = 1.0f / (0.01f * (float) currentSampleRate);
        for (int i = 0; i < n; ++i)
        {
            changeFade = juce::jlimit (0.0f, 1.0f, changeFade + (changingDown ? -step : step));
            const float level = changeFade * changeFade * (3.0f - 2.0f * changeFade);   // (an S-curve: no corners)
            for (int c = 0; c < track.getNumChannels(); ++c)
                track.getWritePointer (c)[i] *= level;
            if (changingDown && changeFade <= 0.0f)
            {
                playingWorksOn = wanted;
                changingDown = false;
            }
        }
    }

    // Keep the biggest values until the meter and graph next ask for them.
    storeIfBigger (meterReductionDb, result.maxReductionDb);
    storeIfBigger (graphLevelDb, result.maxLevelDb);
    autoGainDb.store (result.autoGainDb);
}

Compressor::BlockResult EasyCompAudioProcessor::processMidSide (juce::AudioBuffer<float>& track, const juce::AudioBuffer<float>* key, WorksOn mode) noexcept
{
    Compressor::BlockResult result;
    const int total = track.getNumSamples();
    auto settings = getCurrentSettings();
    const bool bypassed = settings.bypassed;
    settings.bypassed = false;   // (BYPASS is done here instead, on left and right: exactly your track when it's on)
    compressor.setSettings (settings);
    sideCompressor.setSettings (settings);
    midSideBypass.setTargetValue (bypassed ? 1.0f : 0.0f);

    float* left = track.getWritePointer (0);
    float* right = track.getWritePointer (1);

    for (int start = 0; start < total; start += middle.getNumSamples())
    {
        const int n = juce::jmin (middle.getNumSamples(), total - start);
        float* m = middle.getWritePointer (0);
        float* s = sides.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            m[i] = 0.5f * (left[start + i] + right[start + i]);
            s[i] = 0.5f * (left[start + i] - right[start + i]);
        }

        juce::AudioBuffer<float> mid (middle.getArrayOfWritePointers(), 1, 0, n);
        juce::AudioBuffer<float> side (sides.getArrayOfWritePointers(), 1, 0, n);
        std::optional<juce::AudioBuffer<float>> keyPart;   // (no allocation: it points into the sidechain)
        if (key != nullptr && key->getNumChannels() > 0)
            keyPart.emplace (const_cast<float* const*> (key->getArrayOfReadPointers()), key->getNumChannels(), start, n);

        const auto merge = [&result] (const Compressor::BlockResult& r)
        {
            result.maxReductionDb = juce::jmax (result.maxReductionDb, r.maxReductionDb);
            result.maxLevelDb = juce::jmax (result.maxLevelDb, r.maxLevelDb);
            result.autoGainDb = r.autoGainDb;
        };
        if (mode == worksOnMid || mode == worksOnMidSide)   merge (compressor.process (mid, keyPart ? &*keyPart : nullptr));
        if (mode == worksOnSide || mode == worksOnMidSide)  merge ((mode == worksOnSide ? compressor : sideCompressor).process (side, keyPart ? &*keyPart : nullptr));

        for (int i = 0; i < n; ++i)
        {
            const float b = midSideBypass.getNextValue();
            const float l = m[i] + s[i], r = m[i] - s[i];
            left[start + i]  = b >= 1.0f ? left[start + i]  : l * (1.0f - b) + left[start + i] * b;
            right[start + i] = b >= 1.0f ? right[start + i] : r * (1.0f - b) + right[start + i] * b;
        }
    }
    return result;
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

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"
#include "FastMath.h"

juce::AudioProcessorValueTreeState::ParameterLayout EasyUnbleedAudioProcessor::createParameterLayout()
{
    const auto& d = unbleedPresets[0];   // "Default settings"
    juce::NormalisableRange<float> amountRange (0.0f, 40.0f, 0.5f);
    juce::NormalisableRange<float> percentRange (0.0f, 100.0f, 1.0f);
    juce::NormalisableRange<float> hzRange (20.0f, 20000.0f, 1.0f);
    hzRange.setSkewForCentre (1000.0f);

    const auto fromText = [] (float hz) { return hz <= unbleedFromOff + 0.5f ? juce::String ("Off") : Format::hertz (hz); };
    const auto toText   = [] (float hz) { return hz >= unbleedToOff - 1.0f ? juce::String ("Off") : Format::hertz (hz); };
    const auto fromParse = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? unbleedFromOff : Format::hertzFromText (t); };
    const auto toParse   = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("off") ? unbleedToOff : Format::hertzFromText (t); };

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // (The numbers after the IDs say which release added each setting.)
    layout.add (makeKnob (UnbleedIDs::amount,      1, "Amount",      amountRange,  d.amountDb,           [] (float v) { return juce::String (v, 1) + " dB"; }));
    layout.add (makeKnob (UnbleedIDs::sensitivity, 1, "Sensitivity", percentRange, d.sensitivityPercent, Format::percent));
    layout.add (makeKnob (UnbleedIDs::smoothness,  1, "Smoothness",  percentRange, d.smoothnessPercent,  Format::percent));
    layout.add (makeKnob (UnbleedIDs::from,        1, "From",        hzRange,      d.fromHz,             fromText, fromParse));
    layout.add (makeKnob (UnbleedIDs::to,          1, "To",          hzRange,      d.toHz,               toText, toParse));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { UnbleedIDs::listen, 1 }, "Listen To Removed", false));
    addCommonParameters (layout, false);   // BYPASS
    return layout;
}

//==============================================================================
EasyUnbleedAudioProcessor::EasyUnbleedAudioProcessor()
    : EasyProcessor ("EasyUnbleedSettings", createParameterLayout(), makeUnbleedPresets(), unbleedPresetIDs(), true)
{
    amountValue      = parameters.getRawParameterValue (UnbleedIDs::amount);
    sensitivityValue = parameters.getRawParameterValue (UnbleedIDs::sensitivity);
    smoothnessValue  = parameters.getRawParameterValue (UnbleedIDs::smoothness);
    fromValue        = parameters.getRawParameterValue (UnbleedIDs::from);
    toValue          = parameters.getRawParameterValue (UnbleedIDs::to);
    listenValue      = parameters.getRawParameterValue (UnbleedIDs::listen);
    bypassValue      = parameters.getRawParameterValue (CommonIDs::bypass);
    for (auto* group : { &centres, &reductions, &stemLevels, &otherLevels })
        for (auto& v : *group) v.store (0.0f);
}

SpectralUnbleed::Settings EasyUnbleedAudioProcessor::getCurrentSettings() const noexcept
{
    SpectralUnbleed::Settings s;
    s.amountDb    = amountValue->load();
    s.sensitivity = sensitivityValue->load() / 100.0f;
    s.smoothness  = smoothnessValue->load() / 100.0f;
    s.fromHz      = fromValue->load();
    s.toHz        = toValue->load();
    s.listen      = listenValue->load() > 0.5f;
    s.bypassed    = bypassValue->load() > 0.5f;
    return s;
}

void EasyUnbleedAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    rate = sampleRate;
    engine.prepare (sampleRate, getCurrentSettings());
    setLatencySamples (engine.getLatencySamples());
    samplesSinceSidechain = samplesSinceTrack = 1 << 30;
    sidechainMissing.store (false);
}

void EasyUnbleedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto track = getBusBuffer (buffer, false, 0);
    const bool sidechainSent = getBusCount (true) > 1
                            && buffer.getNumChannels() >= getChannelIndexInProcessBlockBuffer (true, 1, 0) + getChannelCountOfBus (true, 1);
    auto side = sidechainSent ? getBusBuffer (buffer, true, 1) : juce::AudioBuffer<float> (buffer.getArrayOfWritePointers(), 0, buffer.getNumSamples());

    // (no sidechain: the window says so, once the stem has been playing for a moment without one)
    const int n = buffer.getNumSamples();
    samplesSinceTrack = FastMath::isSilent (track) ? juce::jmin (samplesSinceTrack + n, 1 << 30) : 0;
    samplesSinceSidechain = (side.getNumChannels() > 0 && ! FastMath::isSilent (side)) ? 0 : juce::jmin (samplesSinceSidechain + n, 1 << 30);
    const int patience = (int) (1.5 * rate);
    sidechainMissing.store (samplesSinceSidechain > patience && samplesSinceTrack < patience);

    engine.setSettings (getCurrentSettings());
    engine.process (track, side.getNumChannels() > 0 ? &side : nullptr);

    numBands.store (engine.getNumBands());
    for (int b = 0; b < engine.getNumBands(); ++b)
    {
        centres[(size_t) b].store (engine.getBandCentreHz (b));
        reductions[(size_t) b].store (engine.getBandReductionDb (b));
        stemLevels[(size_t) b].store (engine.getBandLevelDb (b, false));
        otherLevels[(size_t) b].store (engine.getBandLevelDb (b, true));
    }
    sidechainCopy.store (engine.isSidechainACopy());
}

EasyUnbleedAudioProcessor::Picture EasyUnbleedAudioProcessor::getPicture() const noexcept
{
    Picture p;
    p.numBands = numBands.load();
    for (int b = 0; b < p.numBands; ++b)
    {
        p.centreHz[(size_t) b] = centres[(size_t) b].load();
        p.reductionDb[(size_t) b] = reductions[(size_t) b].load();
        p.stemDb[(size_t) b] = stemLevels[(size_t) b].load();
        p.othersDb[(size_t) b] = otherLevels[(size_t) b].load();
    }
    return p;
}

juce::AudioProcessorEditor* EasyUnbleedAudioProcessor::createEditor()
{
    return new EasyUnbleedAudioProcessorEditor (*this);
}

// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyUnbleedAudioProcessor();
}

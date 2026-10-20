#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    // Helpers so values read nicely, both in the plugin and in your DAW's
    // automation lanes: "-20.0 dB", "4.0 : 1", "10.0 ms", "100%"...
    juce::String limitLength (const juce::String& text, int maximumLength)
    {
        return maximumLength > 0 ? text.substring (0, maximumLength) : text;
    }

    juce::String formatLowCut (float hz)
    {
        if (hz <= Compressor::lowCutOffHz + 0.5f)
            return "Off";

        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    /** Keeps the bigger of the stored value and a new one, without locking
        (so the audio thread never has to wait for the plugin window). */
    void storeIfBigger (std::atomic<float>& store, float newValue) noexcept
    {
        float previous = store.load();

        while (newValue > previous && ! store.compare_exchange_weak (previous, newValue))
        {
        }
    }

    juce::String formatMilliseconds (float ms)
    {
        if (ms < 1.0f)
            return juce::String (ms, 2) + " ms";

        if (ms < 10.0f)
            return juce::String (ms, 1) + " ms";

        return juce::String (juce::roundToInt (ms)) + " ms";
    }

    std::unique_ptr<juce::AudioParameterFloat> makeParameter (const char* parameterID,
                                                              const juce::String& name,
                                                              juce::NormalisableRange<float> range,
                                                              float defaultValue,
                                                              std::function<juce::String (float)> valueToText)
    {
        auto attributes = juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([valueToText] (float value, int maximumLength)
                                          {
                                              return limitLength (valueToText (value), maximumLength);
                                          })
            // Lets you type "-15" or "-15 dB" into a value box.
            .withValueFromStringFunction ([] (const juce::String& text)
                                          {
                                              return text.getFloatValue();
                                          });

        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { parameterID, 1 },
                                                            name, range, defaultValue, attributes);
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout EasyCompAudioProcessor::createParameterLayout()
{
    const auto& defaults = drumPresets[0];   // the first preset doubles as the default setting

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

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (makeParameter (ParamIDs::threshold, "Threshold", thresholdRange, defaults.thresholdDb,
                               [] (float v) { return juce::String (v, 1) + " dB"; }));

    layout.add (makeParameter (ParamIDs::ratio, "Ratio", ratioRange, defaults.ratio,
                               [] (float v) { return juce::String (v, 1) + " : 1"; }));

    layout.add (makeParameter (ParamIDs::attack, "Attack", attackRange, defaults.attackMs,
                               formatMilliseconds));

    layout.add (makeParameter (ParamIDs::release, "Release", releaseRange, defaults.releaseMs,
                               formatMilliseconds));

    layout.add (makeParameter (ParamIDs::makeup, "Make-up Gain", makeupRange, defaults.makeupDb,
                               [] (float v) { return (v >= 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }));

    layout.add (makeParameter (ParamIDs::mix, "Mix", mixRange, defaults.mixPercent,
                               [] (float v) { return juce::String (juce::roundToInt (v)) + "%"; }));

    // Added in 1.1. The "2" is a version number for your DAW; it lets
    // projects saved with 1.0 open cleanly (they get this knob set to Off).
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { ParamIDs::lowCut, 2 }, "Ignore Below", lowCutRange, defaults.lowCutHz,
        juce::AudioParameterFloatAttributes()
            .withStringFromValueFunction ([] (float value, int maximumLength)
                                          {
                                              return limitLength (formatLowCut (value), maximumLength);
                                          })
            .withValueFromStringFunction ([] (const juce::String& text)
                                          {
                                              if (text.trim().equalsIgnoreCase ("off"))
                                                  return Compressor::lowCutOffHz;

                                              return text.getFloatValue();
                                          })));

    return layout;
}

//==============================================================================
EasyCompAudioProcessor::EasyCompAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "EasyCompSettings", createParameterLayout())
{
    thresholdValue = parameters.getRawParameterValue (ParamIDs::threshold);
    ratioValue     = parameters.getRawParameterValue (ParamIDs::ratio);
    attackValue    = parameters.getRawParameterValue (ParamIDs::attack);
    releaseValue   = parameters.getRawParameterValue (ParamIDs::release);
    makeupValue    = parameters.getRawParameterValue (ParamIDs::makeup);
    mixValue       = parameters.getRawParameterValue (ParamIDs::mix);
    lowCutValue    = parameters.getRawParameterValue (ParamIDs::lowCut);
}

//==============================================================================
void EasyCompAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    compressor.prepare (sampleRate, getCurrentSettings());
    meterReductionDb.store (0.0f);
    graphLevelDb.store (Compressor::silenceDb);
}

bool EasyCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Works on mono or stereo tracks, as long as the input matches the output.
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return output == layouts.getMainInputChannelSet();
}

void EasyCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;   // avoids CPU spikes from ultra-tiny numbers

    // Standard housekeeping: silence any output channels that have no input.
    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    compressor.setSettings (getCurrentSettings());
    const auto result = compressor.process (buffer);

    // Keep the biggest values until the meter and graph next ask for them.
    storeIfBigger (meterReductionDb, result.maxReductionDb);
    storeIfBigger (graphLevelDb, result.maxLevelDb);
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
    return settings;
}

//==============================================================================
void EasyCompAudioProcessor::loadPreset (int presetIndex)
{
    if (! juce::isPositiveAndBelow (presetIndex, (int) drumPresets.size()))
        return;

    const auto& preset = drumPresets[(size_t) presetIndex];

    const auto setKnob = [this] (const char* parameterID, float value)
    {
        if (auto* parameter = parameters.getParameter (parameterID))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
            parameter->endChangeGesture();
        }
    };

    setKnob (ParamIDs::threshold, preset.thresholdDb);
    setKnob (ParamIDs::ratio,     preset.ratio);
    setKnob (ParamIDs::attack,    preset.attackMs);
    setKnob (ParamIDs::release,   preset.releaseMs);
    setKnob (ParamIDs::makeup,    preset.makeupDb);
    setKnob (ParamIDs::mix,       preset.mixPercent);
    setKnob (ParamIDs::lowCut,    preset.lowCutHz);
}

//==============================================================================
juce::AudioProcessorEditor* EasyCompAudioProcessor::createEditor()
{
    return new EasyCompAudioProcessorEditor (*this);
}

//==============================================================================
void EasyCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Stores the knob positions inside your DAW project.
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void EasyCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restores the knob positions when your DAW project is opened.
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto savedSettings = juce::ValueTree::fromXml (*xml);

    // Projects saved with version 1.0 have no IGNORE BELOW setting. Open
    // them with it switched off, so they sound exactly as they did.
    if (! savedSettings.getChildWithProperty ("id", ParamIDs::lowCut).isValid())
    {
        juce::ValueTree lowCutSetting ("PARAM");
        lowCutSetting.setProperty ("id", ParamIDs::lowCut, nullptr);
        lowCutSetting.setProperty ("value", Compressor::lowCutOffHz, nullptr);
        savedSettings.appendChild (lowCutSetting, nullptr);
    }

    parameters.replaceState (savedSettings);
}

//==============================================================================
// JUCE calls this to create the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EasyCompAudioProcessor();
}

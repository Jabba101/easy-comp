#include "EasyProcessor.h"

EasyProcessor::EasyProcessor (const juce::String& stateName,
                              juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout,
                              std::vector<Preset> presets,
                              juce::StringArray presetIDs)
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateName, std::move (parameterLayout)),
      builtInPresets (std::move (presets)),
      presetParameterIDs (std::move (presetIDs))
{
    bypassParameter = dynamic_cast<juce::AudioParameterBool*> (parameters.getParameter (CommonIDs::bypass));
    jassert (bypassParameter != nullptr);   // every plugin must call addCommonParameters()
}

void EasyProcessor::addCommonParameters (juce::AudioProcessorValueTreeState::ParameterLayout& layout)
{
    // (The "3" is a version number for your DAW: these arrived in Easy Comp 1.2.)
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { CommonIDs::autoGain, 3 }, "Auto-Gain", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { CommonIDs::bypass, 3 }, "Bypass", false));
}

bool EasyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Works on mono or stereo tracks, as long as the input matches the output.
    const auto& output = layouts.getMainOutputChannelSet();

    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;

    return output == layouts.getMainInputChannelSet();
}

//==============================================================================
// Presets
//==============================================================================
void EasyProcessor::loadPreset (const Preset& preset)
{
    for (auto& id : presetParameterIDs)
    {
        auto* parameter = parameters.getParameter (id);

        if (parameter == nullptr)
            continue;

        // The preset's value, or the default if this preset doesn't mention it
        float normalised = parameter->getDefaultValue();

        for (auto& [presetID, value] : preset.values)
            if (presetID == id)
                normalised = parameter->convertTo0to1 (value);

        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (normalised);
        parameter->endChangeGesture();
    }

    {
        const juce::ScopedLock lock (presetLock);
        presetName   = preset.name;
        presetEdited = false;
    }

    presetLoaded();
}

std::vector<std::pair<juce::String, float>> EasyProcessor::getCurrentPresetValues() const
{
    std::vector<std::pair<juce::String, float>> values;

    for (auto& id : presetParameterIDs)
        if (auto* value = parameters.getRawParameterValue (id))
            values.emplace_back (id, value->load());

    return values;
}

juce::String EasyProcessor::getPresetName() const
{
    const juce::ScopedLock lock (presetLock);
    return presetName;
}

bool EasyProcessor::isPresetEdited() const
{
    const juce::ScopedLock lock (presetLock);
    return presetEdited;
}

void EasyProcessor::markPresetEdited()
{
    const juce::ScopedLock lock (presetLock);

    if (presetName.isNotEmpty())
        presetEdited = true;
}

//==============================================================================
// Saving and loading with your project
//==============================================================================
void EasyProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Stores the knob positions (and which preset they came from) in your DAW project.
    auto state = parameters.copyState();

    {
        const juce::ScopedLock lock (presetLock);
        state.setProperty ("presetName", presetName, nullptr);
        state.setProperty ("presetEdited", presetEdited, nullptr);
    }

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EasyProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restores the knob positions when your DAW project is opened.
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    auto savedSettings = juce::ValueTree::fromXml (*xml);

    // Projects saved with an older version are missing the newer settings.
    // Open them with those at their defaults (newer features switched off),
    // so they sound exactly as they did.
    for (auto* parameter : getParameters())
    {
        if (auto* setting = dynamic_cast<juce::RangedAudioParameter*> (parameter))
        {
            if (! savedSettings.getChildWithProperty ("id", setting->paramID).isValid())
            {
                juce::ValueTree missing ("PARAM");
                missing.setProperty ("id", setting->paramID, nullptr);
                missing.setProperty ("value", setting->convertFrom0to1 (setting->getDefaultValue()), nullptr);
                savedSettings.appendChild (missing, nullptr);
            }
        }
    }

    parameters.replaceState (savedSettings);

    // (Set after the knobs, which may briefly mark the preset as edited.)
    const juce::ScopedLock lock (presetLock);
    presetName   = savedSettings.getProperty ("presetName", juce::String()).toString();
    presetEdited = (bool) savedSettings.getProperty ("presetEdited", false);
}

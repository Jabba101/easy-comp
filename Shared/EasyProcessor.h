#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PresetBrowser.h"

//==============================================================================
/** Parameter IDs that every plugin here has. */
namespace CommonIDs
{
    inline constexpr const char* autoGain = "autogain";
    inline constexpr const char* bypass   = "bypass";
}

//==============================================================================
/**
    What all the Easy plugins have in common, as your DAW sees them: their
    settings (parameters), BYPASS and AUTO-GAIN, presets, and saving and
    loading with your project. Each plugin builds on this and adds its own
    knobs and sound.
*/
class EasyProcessor : public juce::AudioProcessor
{
public:
    /** stateName: the name the settings are saved under in projects (never change it).
        withSidechain: also take a second input that another track can be sent to. */
    EasyProcessor (const juce::String& stateName,
                   juce::AudioProcessorValueTreeState::ParameterLayout parameterLayout,
                   std::vector<Preset> builtInPresets,
                   juce::StringArray presetParameterIDs,
                   bool withSidechain = false);

    /** The plugin's settings, plus the glue that keeps knobs and DAW in sync. */
    juce::AudioProcessorValueTreeState parameters;

    /** Adds the BYPASS button, and the AUTO-GAIN button unless withAutoGain is
        false, to a parameter layout. Every plugin calls this. */
    static void addCommonParameters (juce::AudioProcessorValueTreeState::ParameterLayout&, bool withAutoGain = true);

    /** True if this plugin has an AUTO-GAIN button. */
    bool hasAutoGain() const    { return parameters.getParameter (CommonIDs::autoGain) != nullptr; }

    //==== Presets =============================================================
    const std::vector<Preset>& getBuiltInPresets() const noexcept   { return builtInPresets; }
    const juce::StringArray& getPresetParameterIDs() const noexcept { return presetParameterIDs; }

    /** The name your own presets are kept under (Documents > Easy Plugins > this).
        It stays the same even if the plugin is renamed, so they're always found. */
    virtual juce::String getPresetFolderName() const                { return JucePlugin_Name; }

    /** Sets the knobs to a preset (BYPASS and AUTO-GAIN are left alone). Any
        knob the preset doesn't mention goes back to its default. */
    void loadPreset (const Preset&);

    /** The current knob settings, in the form a preset stores them. */
    std::vector<std::pair<juce::String, float>> getCurrentPresetValues() const;

    juce::String getPresetName() const;
    bool isPresetEdited() const;
    void markPresetEdited();   // called when a knob moves after a preset was loaded

    //==== Bypass ==============================================================
    // Hands the BYPASS button to the DAW too, so the DAW's own bypass button
    // and the one in the plugin window are the same switch.
    juce::AudioProcessorParameter* getBypassParameter() const override   { return bypassParameter; }

    //==== Saving and loading with your DAW project ============================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==== Basic info for the DAW ==============================================
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    bool hasEditor() const override                     { return true; }
    void releaseResources() override                    {}
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return false; }
    bool producesMidi() const override                  { return false; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    // Just one "program": the presets live in the plugin's own menu instead,
    // because some DAWs re-select program 0 when they load a project, which
    // would wipe your settings.
    int getNumPrograms() override                               { return 1; }
    int getCurrentProgram() override                            { return 0; }
    void setCurrentProgram (int) override                       {}
    const juce::String getProgramName (int) override            { return {}; }
    void changeProgramName (int, const juce::String&) override  {}

    using AudioProcessor::processBlock;

protected:
    /** Called after a preset has been loaded (e.g. to restart AUTO-GAIN). */
    virtual void presetLoaded() {}

private:
    static BusesProperties makeBuses (bool withSidechain);

    std::vector<Preset> builtInPresets;
    juce::StringArray presetParameterIDs;
    juce::AudioParameterBool* bypassParameter = nullptr;

    // Which preset is loaded, kept with the project. (Guarded by a lock
    // because some DAWs save projects from a different thread.)
    juce::CriticalSection presetLock;
    juce::String presetName;
    bool presetEdited = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyProcessor)
};

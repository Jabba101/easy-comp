#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>
#include <vector>

//==============================================================================
/** One preset: where it sits in the menu, what it's called, a one-line tip
    for the help bar, and the knob settings (parameter ID -> value). */
struct Preset
{
    juce::String category;   // e.g. "Drums"; empty = at the top of the menu
    juce::String name;       // e.g. "Kick - Punch"
    juce::String tip;        // shown in the help bar while this preset is loaded
    std::vector<std::pair<juce::String, float>> values;
};

//==============================================================================
/**
    The preset box in the top right. Clicking it opens a menu sorted into
    instrument sub-menus, plus "My presets" - the ones you've saved yourself,
    kept as small files in Documents > Easy Plugins > (plugin name).

    It doesn't change any settings itself: it calls onPresetChosen, and the
    plugin does the rest.
*/
class PresetBrowser : public juce::Component
{
public:
    PresetBrowser();

    void setPresets (const juce::String& pluginName, std::vector<Preset> builtInPresets);

    /** Called when you pick a preset from the menu (built in, or your own). */
    std::function<void (const Preset&)> onPresetChosen;

    /** Asked for the current knob settings when you save a preset. */
    std::function<std::vector<std::pair<juce::String, float>>()> getCurrentValues;

    /** Called after you've saved a preset, with its name. */
    std::function<void (const juce::String&)> onPresetSaved;

    /** What the box shows: the preset's name, plus "(edited)" once you move a knob. */
    void showCurrent (const juce::String& presetName, bool edited);
    juce::String getDisplayedText() const   { return shownName.isEmpty() ? juce::String ("Presets...")
                                                                         : shownName + (shownEdited ? " (edited)" : ""); }

    /** Finds a preset by name - built in first, then your own. */
    const Preset* findPreset (const juce::String& name) const;

    //==== Your own presets ====================================================
    juce::File getUserPresetFolder() const;
    void setUserPresetFolder (const juce::File& folder)   { userFolderOverride = folder; }   // (for testing)
    std::vector<Preset> readUserPresets() const;

    /** Saves the current settings under this name (with " (mine)" added if a
        built-in preset already has the name). Returns the name it was saved
        under, or nothing if it couldn't be saved. */
    juce::String saveUserPreset (const juce::String& name);

    //==== The menu (public so it can be tested) ===============================
    enum MenuIds { firstBuiltIn = 1, firstUser = 1001, saveItem = 2001, openFolderItem = 2002 };
    juce::PopupMenu buildMenu();
    void handleMenuResult (int itemId);

    /** Shows the little "type a name" box for saving a preset. */
    void startNaming();
    bool isNaming() const noexcept                        { return nameEditor.isVisible(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void finishNaming (bool save);

    juce::String pluginName;
    std::vector<Preset> builtIn, user;
    juce::File userFolderOverride;

    juce::String shownName;
    bool shownEdited = false;
    juce::TextEditor nameEditor;
};

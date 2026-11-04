#pragma once

#include "PluginEditor.h"
#include "HoloControls.h"
#include "PresetBrowser.h"

//==============================================================================
/**
    Easy Master in the HOLOGRAM skin, laid out like Easy Strip's console
    (at 1800 x 1030; the window scales it).

    Header: the name, the presets, A / B, INIT (everything), BYPASS. Under
    it, DISPLAY FOCUS: what the big screen shows. Then INPUT on the left,
    the RESONANCE TAMER and the big screen, and under them the big bay -
    the EQ or the MULTIBAND (the tabs top left switch between them). On the
    right the COMPRESSOR, PUNCH and WIDTH, then the METERS, LIMITER and
    COMPARE. The ticker along the bottom explains whatever the mouse is over,
    and shows a value while it's moved.

    Every module is its own tinted panel: its name lit when it's on (click to
    switch it), INIT and Pr (its own presets) beside it, STEREO / MID+SIDE
    where it has one, and its controls in numbered sections, each holding
    only what works together. MID controls are green, SIDE blue - both
    always on screen (in STEREO the SIDE ones rest, dimmed).
*/
namespace MasterHolo
{
    class Console;

    //==========================================================================
    /** One module's panel, like Easy Strip's slabs. */
    class Module : public juce::Component
    {
    public:
        /** presetModule: its MasterModules index (for INIT and Pr), or -1 for none.
            onID: the setting its lit name switches (nullptr: always lit, e.g. the LIMITER). */
        Module (Console&, int presetModule, juce::Colour tint, const juce::String& name, const char* onID);
        ~Module() override;

        void paint (juce::Graphics&) override;
        virtual void refresh() {}

        const juce::String& getName() const noexcept   { return name; }
        int getPresetModule() const noexcept           { return presetModule; }

        Holo::Button power, init, pr;
        const juce::Colour tint;

        static const juce::Colour midTint, sideTint;

    protected:
        juce::RangedAudioParameter& param (const char* id);
        Holo::Ring& ring (const char* id, const juce::String& title, Holo::Format, const juce::String& unit, Holo::Ring::Size,
                          juce::Colour, const juce::String& hintTitle, const juce::String& hintText);
        Holo::Bar& bar (const char* id, const juce::String& title, Holo::Format, const juce::String& unit, juce::Colour, bool centreZero,
                        const juce::String& hintTitle, const juce::String& hintText);
        Holo::Choice& choice (const char* id, const juce::StringArray& names, const juce::String& hintTitle, const juce::String& hintText,
                              bool sideBySide, juce::Colour = {});
        Holo::Button& toggle (const char* id, const juce::String& text, const juce::String& hintTitle, const juce::String& hintText,
                              Holo::Button::Style = Holo::Button::Style::plain, juce::Colour = {});
        juce::Label& caption (const juce::String& text, juce::Colour = Holo::Colours::ink, float size = 14.0f);
        void hint (juce::Component&, const juce::String& title, const juce::String& text);

        /** The top row: [left reserved] [mode] ... [name] ... [INIT][Pr]; returns what's left below. */
        juce::Rectangle<int> placeHeader (juce::Rectangle<int> area, juce::Component* mode = nullptr, int modeWidth = 0, int leftReserved = 0);

        /** A numbered section's box (drawn behind its controls); returns its inside. */
        juce::Rectangle<int> section (juce::Rectangle<int>, int number, const juce::String& title);
        void clearSections()   { sections.clear(); }
        static void centreIn (juce::Component&, juce::Rectangle<int> column, int top = -1);

        /** STEREO: the SIDE controls rest (dimmed); the MID ones take the module's own colour and do the whole mix. */
        void showParts (bool midSide, std::initializer_list<Holo::ParamControl*> midControls, std::initializer_list<juce::Component*> sideControls);

        Console& console;
        EasyMasterAudioProcessor& processor;
        const int presetModule;
        juce::String name;
        std::vector<std::unique_ptr<juce::Component>> parts;
        std::vector<std::unique_ptr<Holo::ButtonLink>> links;

    private:
        struct Section { juce::Rectangle<float> area; int number; juce::String title; };
        std::vector<Section> sections;
    };

    //==========================================================================
    class Console : public juce::Component,
                    public Holo::Host,
                    private juce::Timer
    {
        EasyMasterAudioProcessor& processor;   // (first: the displays below are made from it)

    public:
        explicit Console (EasyMasterAudioProcessor&);
        ~Console() override;

        static constexpr int width = 1800, height = 1030;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        //==== Host ===============================================================
        void showValue (const juce::String& title, const juce::String& valueWithUnits) override;
        void valueShown() override;
        void typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
                        std::function<void (const juce::String&)> onEntered) override;
        void settingMoved (juce::RangedAudioParameter&) override;

        //==== For the modules, and the tests =====================================
        EasyMasterAudioProcessor& getProcessor() noexcept   { return processor; }
        void addHint (juce::Component&, const juce::String& title, const juce::String& text);
        void registerControl (const juce::String& parameterID, Holo::ParamControl&);
        Holo::ParamControl* findControl (const juce::String& parameterID) const;
        const juce::String& getTickerTitle() const noexcept   { return tickerTitle; }
        const juce::String& getTickerText() const noexcept    { return tickerText; }
        void updateNow()                                       { timerCallback(); }
        void markEdited();

        /** DISPLAY FOCUS: what the big screen shows. */
        enum Focus { tamerFocus = 0, eqFocus, mbFocus, compFocus, punchFocus, widthFocus, numFocus };
        void showFocus (int focus);
        int getFocus() const noexcept   { return focus; }

        /** The big bay: 0 = EQ, 1 = MULTIBAND. */
        void showBay (int bay);
        int getBay() const noexcept     { return bay; }

        /** A module's own presets (Pr) and INIT (nullptr). */
        void applyModulePreset (int module, const Preset*);
        void showModulePresetMenu (int module, juce::Component& under);
        PresetBrowser& getModuleBrowser (int module)   { return *moduleBrowsers[(size_t) module]; }

        /** The whole chain's presets. */
        void loadMainPreset (const Preset&);
        void showMainPresetMenu (juce::Component& under);
        void stepPreset (int direction);
        juce::String getPresetText() const;

        /** The module whose INIT / Pr belong to MasterModules index m (nullptr if none). */
        Module* getModule (int masterModule) const;
        const std::vector<std::unique_ptr<Module>>& getModules() const noexcept   { return modules; }

        // (public so the parts can be found and tested)
        std::array<std::unique_ptr<Holo::Button>, numFocus> focusButtons;
        std::array<std::unique_ptr<Holo::Button>, 2> bayTabs;
        Holo::Button initAll { "INIT", Holo::Colours::warn, Holo::Button::Style::init };
        Holo::Button bypass { "BYPASS", Holo::Colours::warn };
        Holo::Button presetPrevious { "<", Holo::Colours::selected }, presetNext { ">", Holo::Colours::selected };
        Holo::Button slotA { "A", Holo::Colours::selected }, slotB { "B", Holo::Colours::selected }, copySlot { "A > B", Holo::Colours::selected };
        std::unique_ptr<juce::Component> presetWindow;
        MasterDisplay compDisplay;
        TamerDisplay tamerDisplay;
        EQDisplay eqDisplay;
        MultibandDisplay mbDisplay;
        PunchDisplay punchDisplay;
        WidthDisplay widthDisplay;
        MasterMeters meters;
        Holo::BigMeter inputMeter;
        std::unique_ptr<Holo::Ring> trim;
        juce::Label inputState;
        juce::Component* getScreenComponent (int f) const;

    private:
        void timerCallback() override;
        void showHintFor (juce::Component*);
        void setTicker (const juce::String& title, const juce::String& text, juce::Colour);
        void announce (const juce::String& title, const juce::String& text, int seconds);
        juce::Colour tintFor (juce::Component*) const;

        std::vector<std::unique_ptr<Module>> modules;
        int focus = compFocus, bay = 0;

        struct Hint { juce::Component* component; juce::String title, text; };
        std::vector<Hint> hints;
        std::map<juce::String, Holo::ParamControl*> controls;
        juce::Component::SafePointer<juce::Component> hovered;

        juce::String tickerTitle, tickerText;
        juce::Colour tickerColour { Holo::Colours::cyan };
        bool showingValue = false;
        double announceUntil = 0.0;

        PresetBrowser mainPresets;
        std::array<std::unique_ptr<PresetBrowser>, MasterModules::count> moduleBrowsers;

        std::unique_ptr<Holo::ButtonLink> bypassLink;
        juce::TextEditor entry;
        std::function<void (const juce::String&)> onEntry;

        juce::Rectangle<int> headerArea, focusRow, inputColumn, screenArea, metersArea, tickerArea;
        std::vector<EasyMasterAudioProcessor::Moment> incoming;
        std::vector<float> scopeLeft, scopeRight;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Console)
    };
}

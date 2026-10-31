#pragma once

#include "HoloScreen.h"
#include "StripSettings.h"
#include "PresetBrowser.h"

namespace Holo
{
    class Slab;
    class ClipAdvanced;

    //==========================================================================
    /**
        The whole Easy Strip in the HOLOGRAM skin, laid out at 100% (1800 wide);
        the window scales it to the size chosen in SETUP (or dragged).

        Header: the strip's presets, OVERSAMPLING, INIT (the whole strip),
        BYPASS, SETUP. Under it, the page buttons for the big screen. Then
        INPUT, the modules around the screen, OUTPUT, and the ticker (hints and
        values) along the bottom.

        WEAPON SELECT swaps GATE, AMP, the screen, EQ, DS and COMP for the
        effects rack (seven bays: WAH, FLANGE, FILTER, TREMOLO, DELAY and VERB so far).
        TRANS, TAPE, CLIP, INPUT / OUTPUT and the header stay. Pressing it
        again brings the strip back.

        Every module's controls are its standalone plugin's controls (same
        names, same numbered sections, the same hints word for word), and each
        module has its own power button, INIT and Pr (presets) in the same place.
    */
    class View : public juce::Component,
                 public Host,
                 private juce::Timer
    {
    public:
        View (EasyStripAudioProcessor&, StripSettings&);
        ~View() override;

        static constexpr int width = 1800;
        static int heightFor (bool withTicker)   { return withTicker ? 1030 : 974; }

        /** The window calls this when SETUP changes something that needs it (size, skin...). */
        std::function<void()> onSettingsChanged;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        //==== Host ===============================================================
        void showValue (const juce::String& title, const juce::String& valueWithUnits) override;
        void valueShown() override;
        void typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
                        std::function<void (const juce::String&)> onEntered) override;
        void settingMoved (juce::RangedAudioParameter&) override;

        //==== Hints ==============================================================
        /** The hint shown on the ticker while the mouse is over `c` (or anything in it). */
        void addHint (juce::Component& c, const juce::String& title, const juce::String& text);

        /** The standalone module's own hint, word for word (by its title there, e.g. "THRESHOLD"). */
        juce::String moduleHint (int module, const juce::String& title) const;

        //==== For the parts, and tests ===========================================
        EasyStripAudioProcessor& getStrip() noexcept               { return strip; }
        StripSettings& getSettings() noexcept                      { return settings; }
        Screen& getScreen() noexcept                               { return screen; }
        ParamControl* findControl (int module, const juce::String& parameterID) const;   // module -1 = the strip's own
        void registerControl (int module, const juce::String& parameterID, ParamControl&);
        const juce::String& getTickerText() const noexcept         { return tickerText; }
        void showPage (int module);
        void showPresetMenu (int module, juce::Component& under);   // a module's Pr button
        void showSetup (bool show);
        bool isSetupShowing() const;
        void updateNow()                                            { timerCallback(); }   // (tests)
        void setClipAdvancedShown (bool);

        /** WEAPON SELECT: the effects rack in place of the left-hand modules (or back). */
        void showRack (bool);
        bool isRackShown() const noexcept                           { return rackShown; }
        juce::Component* getRack() const noexcept                   { return rack.get(); }

        static constexpr int numBays = 7, wahBay = 0, flangeBay = 1, filterBay = 2, tremBay = 3, delayBay = 4, slapBay = 5;
        static constexpr const char* bayNames[numBays] { "AUTO-WAH", "FLANGE / CHORUS", "MOD FILTER", "TREMOLO", "STEREO DELAY", "REVERB", "LIMITER" };

        /** The track name shown top left (for the tests). */
        const juce::String& getShownTrackName() const noexcept   { return shownTrackName; }

        /** A message on the ticker for a while (FIND's progress, WEAPON SELECT...). */
        void announce (const juce::String& title, const juce::String& text, int seconds);

        std::array<std::unique_ptr<Button>, StripModules::numModules> pageButtons;   // (by module; the rack effects' aren't shown)
        Button initAllButton { "INIT", Colours::warn, Button::Style::init };
        Button bypassButton { "BYPASS", Colours::warn };
        Button setupButton { "SETUP", Colours::cyan };
        Button presetPrevious { "<", Colours::selected }, presetNext { ">", Colours::selected };
        Button orderButton { "ORDER", Colours::selected };   // the four swaps, as a menu
        void showOrderMenu();
        Button slotA { "A", Colours::selected }, slotB { "B", Colours::selected }, copySlot { "A > B", Colours::selected };   // the strip's A / B
        Button weaponSelect { "WEAPON SELECT", Colours::hazard };
        Button polarityButton { juce::String::fromUTF8 ("\xc3\x98"), Colours::cyan };
        Button autoGainButton { "AUTO|GAIN", Colours::gate, Button::Style::square };   // the whole strip's
        juce::Label autoGainReadout;
        std::unique_ptr<Choice> oversampling, meterMode;
        BigMeter inputMeter, outputMeter;
        std::vector<std::unique_ptr<Slab>> slabs;   // in signal order
        std::unique_ptr<ClipAdvanced> clipAdvanced;

    private:
        void timerCallback() override;
        void paintBackdrop (juce::Graphics&);
        void paintStandby (juce::Graphics&);
        void paintTicker (juce::Graphics&);
        void showHintFor (juce::Component*);
        void setTicker (const juce::String& title, const juce::String& text, juce::Colour);
        void loadStripPreset (const Preset&);
        void stepPreset (int direction);
        juce::Colour tintFor (juce::Component*) const;
        void collectHints();

        EasyStripAudioProcessor& strip;
        StripSettings& settings;
        std::unique_ptr<juce::Component> backdrop, standby, ticker;   // (created first: behind everything)
        std::unique_ptr<juce::Component> glowLayer, trackName;
        juce::Rectangle<int> trackNameArea;
        juce::String shownTrackName;
        void paintTrackName (juce::Graphics&);
        void paintHazardFrame (juce::Graphics&);
        Screen screen;
        juce::String shownPresetText;

        struct Hint { juce::Component* component; juce::String title, text; };
        std::vector<Hint> hints;
        std::array<std::map<juce::String, juce::String>, StripModules::numModules> standaloneHints;
        std::map<juce::String, ParamControl*> controls;
        juce::Component::SafePointer<juce::Component> hovered;

        // The ticker
        juce::String tickerTitle, tickerText;
        juce::Colour tickerColour { Colours::cyan };
        float tickerScroll = 0.0f;
        int tickerPause = 0;
        bool showingValue = false;
        double announceUntil = 0.0;
        int lastAudioCount = -1, quietFrames = 0;
        juce::Rectangle<int> tickerArea;

        // The header's preset window (the strip's presets)
        PresetBrowser stripPresets;
        juce::Rectangle<int> presetWindow;
        struct PresetWindow;
        std::unique_ptr<juce::Component> presetWindowComponent;

        // INPUT / OUTPUT
        std::unique_ptr<Ring> trim, level;
        std::unique_ptr<ButtonLink> polarityLink, bypassLink, autoGainLink;
        std::unique_ptr<juce::Component> lufsShortTerm, lufsIntegrated;
        juce::Rectangle<int> inputColumn, outputColumn, logoArea, standbyArea, weaponArea, headerArea, viewRow;
        float sweep = 0.0f;

        // Typing a value
        juce::TextEditor entry;
        std::function<void (const juce::String&)> onEntry;

        // SETUP
        std::unique_ptr<juce::Component> setupPanel;

        // Per-module preset menus (each module's own list, as in its standalone)
        std::array<std::unique_ptr<PresetBrowser>, StripModules::numModules> modulePresets;

        // WEAPON SELECT
        std::unique_ptr<juce::Component> rack;
        bool rackShown = false;
        void layoutRack();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (View)
    };
}

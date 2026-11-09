#pragma once

#include "HoloSlabs.h"
#include "StripSettings.h"
#include "PresetBrowser.h"

namespace Holo
{
    //==========================================================================
    /**
        A standalone plugin in the HOLOGRAM look: exactly the panel ("slab")
        the Easy Strip shows for it - same controls, same numbered sections,
        same hints - with its page of the big screen beside or above it, a
        header (the name, the presets, SETUP) and the ticker along the bottom.
    */
    class SoloView : public juce::Component,
                     public Host,
                     public SlabHost,
                     private juce::Timer
    {
    public:
        SoloView (EasyProcessor&, int module, std::map<juce::String, juce::String> standaloneHints, SoloSettings&);
        ~SoloView() override;

        /** The size at 100% (it depends on the module, and on whether the ticker shows). */
        juce::Point<int> nativeSize() const noexcept   { return native; }

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

        //==== SlabHost ===========================================================
        EasyProcessor& getModule (int) override                  { return processor; }
        bool isModuleOn (int) const override;
        int getMainBusNumInputChannels() const override          { return processor.getMainBusNumInputChannels(); }
        void addHint (juce::Component&, const juce::String& title, const juce::String& text) override;
        juce::String moduleHint (int module, const juce::String& title) const override;
        void registerControl (int module, const juce::String& parameterID, ParamControl&) override;
        void announce (const juce::String& title, const juce::String& text, int seconds) override;
        void showPresetMenu (int module, juce::Component& under) override;
        void setClipAdvancedShown (bool) override;
        Screen& getScreen() override                             { return screen; }

        //==== For tests ==========================================================
        Slab& getSlab() noexcept                                 { return *slab; }
        bool hasScreen() const noexcept                          { return screen.isVisible(); }
        ParamControl* findControl (const juce::String& parameterID) const;
        const juce::String& getTickerText() const noexcept       { return tickerText; }
        void updateNow()                                          { timerCallback(); }

        Button presetPrevious { "<", Colours::selected }, presetNext { ">", Colours::selected };
        Button setupButton { "SETUP", Colours::cyan };

    private:
        void timerCallback() override;
        void paintBackdrop (juce::Graphics&);
        void paintTicker (juce::Graphics&);
        void showHintFor (juce::Component*);
        void setTicker (const juce::String& title, const juce::String& text, juce::Colour);
        void stepPreset (int direction);
        void showSetupMenu();
        void layout();

        EasyProcessor& processor;
        const int module;
        SoloSettings& settings;
        std::map<juce::String, juce::String> standaloneHints;
        juce::Colour tint;

        std::unique_ptr<juce::Component> backdrop, ticker, glowLayer, presetWindow;
        Screen screen;
        std::unique_ptr<Slab> slab;
        std::unique_ptr<Slab> clipAdvanced;   // (Easy Clip only: its ADVANCED row)
        PresetBrowser presets;
        juce::String shownPresetText;

        juce::Point<int> native;
        juce::Rectangle<int> headerArea, titleArea, tickerArea, standbyArea, logoArea;
        void paintStandby (juce::Graphics&, juce::Rectangle<float>);
        bool twoRowHeader = false;

        struct Hint { juce::Component* component; juce::String title, text; };
        std::vector<Hint> hints;
        std::map<juce::String, ParamControl*> controls;
        juce::Component::SafePointer<juce::Component> hovered;

        juce::String tickerTitle, tickerText;
        juce::Colour tickerColour { Colours::cyan };
        float tickerScroll = 0.0f;
        int tickerPause = 0;
        bool showingValue = false;
        double announceUntil = 0.0;

        juce::TextEditor entry;
        std::function<void (const juce::String&)> onEntry;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloView)
    };

    //==========================================================================
    /**
        A standalone plugin's window: the HOLOGRAM look (SoloView, any size -
        drag the corner or pick one in SETUP) or its CLASSIC window, chosen in
        SETUP and remembered for every Easy plugin.
    */
    class SoloEditor : public juce::AudioProcessorEditor
    {
    public:
        using ClassicFactory = std::function<std::unique_ptr<juce::AudioProcessorEditor>()>;

        SoloEditor (EasyProcessor&, int module, ClassicFactory);
        ~SoloEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        SoloView* getHologram() const noexcept                   { return hologram.get(); }
        juce::AudioProcessorEditor* getClassic() const noexcept  { return classic.get(); }

        void applySettings();

    private:
        juce::Point<int> nativeSize() const;

        EasyProcessor& processor;
        const int module;
        ClassicFactory makeClassic;
        SoloSettings settings;
        std::unique_ptr<SoloView> hologram;
        std::unique_ptr<juce::AudioProcessorEditor> classic;
        int builtSkin = -1;
        bool builtTicker = true, applying = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloEditor)
    };

    /** The hints of a standalone's CLASSIC window, by title (the slab shows the same words). */
    std::map<juce::String, juce::String> collectHints (juce::AudioProcessorEditor&);
}

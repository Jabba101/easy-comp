#pragma once

#include "EasyProcessor.h"
#include "Look.h"
#include "Widgets.h"

//==============================================================================
/**
    The parts of the window every Easy plugin shares: the title, the BYPASS
    and AUTO-GAIN buttons (AUTO-GAIN only in plugins that have it), the
    preset box, and the help bar that explains whatever the mouse is over.
    Each plugin adds its own knobs and displays in the space in between (see
    layoutBody and paintBody).
*/
class EasyWindow : public juce::AudioProcessorEditor,
                   private juce::Timer
{
public:
    EasyWindow (EasyProcessor&, const juce::String& title, const juce::String& subtitle,
                const juce::String& gettingStartedHint);
    ~EasyWindow() override;

    void paint (juce::Graphics&) final;
    void resized() final;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** Every hint in the window (title and text), in the order they were added -
        so another window (the Easy Strip) can show exactly the same words. */
    std::vector<std::pair<juce::String, juce::String>> getAllHints() const
    {
        std::vector<std::pair<juce::String, juce::String>> all;
        for (auto& hint : hints)
            all.push_back (hint.titleAndText());
        return all;
    }

    /** The line under the title. */
    const juce::String& getSubtitle() const noexcept   { return subtitle; }

    // (public so the parts can be found and tested)
    LightButton bypassButton   { "BYPASS",    Palette::meterHigh };
    LightButton autoGainButton { "AUTO-GAIN", Palette::meterLow };
    PresetBrowser presetBrowser;
    HintBar hintBar;

protected:
    /** Call at the very end of your constructor, once everything is added. */
    void finishSetup (int width, int height);

    /** Lay out and draw your own part of the window (between the header and the help bar). */
    virtual void layoutBody (juce::Rectangle<int> area) = 0;
    virtual void paintBody (juce::Graphics&) {}

    /** Called 30 times a second: update meters and graphs here. */
    virtual void refreshLiveDisplays() {}

    /** A plugin without AUTO-GAIN can put a button of its own in the header,
        where AUTO-GAIN would be (add it to the window as usual first). */
    void putInHeader (juce::Component& button)    { headerButton = &button; }

    //==== The help bar ========================================================
    /** What the help bar says while the mouse is over this component (or anything inside it). */
    void addHint (juce::Component&, const juce::String& title, const juce::String& text);

    /** The same, for text that can change (e.g. depending on a button). */
    void addHint (juce::Component&, std::function<std::pair<juce::String, juce::String>()> titleAndText);

    //==== Presets =============================================================
    /** Call when one of your knobs moves, so the preset box can show "(edited)". */
    void knobMoved();

    EasyProcessor& easyProcessor;
    EasyLookAndFeel lookAndFeel;   // (declared before anything that uses it)

private:
    void timerCallback() override;
    void showHintFor (const juce::Component*);
    void refreshPresetDisplay();

    struct Hint
    {
        juce::Component* component;
        std::function<std::pair<juce::String, juce::String>()> titleAndText;
    };

    juce::String title, subtitle, gettingStarted;
    std::vector<Hint> hints;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment, autoGainAttachment;
    juce::Rectangle<int> titleArea;
    juce::Component* headerButton = nullptr;
    juce::Component::SafePointer<juce::Component> hovered;   // (clears itself if that part is rebuilt)
    bool loadingPreset = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyWindow)
};

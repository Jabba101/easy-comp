#include "EasyWindow.h"

namespace
{
    constexpr const char* bypassHint =
        "Switches the plugin off so you hear your original sound - click on and off to compare. "
        "Tip: switch AUTO-GAIN on first, so you're comparing the sound itself, not one being louder than the other.";

    constexpr const char* bypassHintWithoutAutoGain =
        "Switches the plugin off so you hear your original sound - click on and off to compare.";

    constexpr const char* autoGainHint =
        "Keeps the volume coming out the same as the volume going in, so BYPASS compares the sound, not the "
        "loudness. It listens over a few seconds and adjusts smoothly.";

    constexpr const char* presetHint =
        "Starting points sorted by instrument. Pick one, then fine-tune - the help bar shows a tip for each preset. "
        "Save your own settings from the bottom of this menu.";
}

EasyWindow::EasyWindow (EasyProcessor& p, const juce::String& titleText, const juce::String& subtitleText,
                        const juce::String& gettingStartedHint)
    : AudioProcessorEditor (&p), easyProcessor (p),
      title (titleText), subtitle (subtitleText), gettingStarted (gettingStartedHint)
{
    // BYPASS and AUTO-GAIN (if this plugin has it), connected to their settings
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        easyProcessor.parameters, CommonIDs::bypass, bypassButton);
    addAndMakeVisible (bypassButton);
    addHint (bypassButton, "BYPASS", easyProcessor.hasAutoGain() ? bypassHint : bypassHintWithoutAutoGain);

    if (easyProcessor.hasAutoGain())
    {
        autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            easyProcessor.parameters, CommonIDs::autoGain, autoGainButton);
        addAndMakeVisible (autoGainButton);
        addHint (autoGainButton, "AUTO-GAIN", autoGainHint);
    }

    // Presets
    presetBrowser.setPresets (easyProcessor.getPresetFolderName(), easyProcessor.getBuiltInPresets());

    presetBrowser.onPresetChosen = [this] (const Preset& preset)
    {
        const juce::ScopedValueSetter<bool> loading (loadingPreset, true);
        easyProcessor.loadPreset (preset);
        refreshPresetDisplay();
    };

    presetBrowser.getCurrentValues = [this] { return easyProcessor.getCurrentPresetValues(); };

    presetBrowser.onPresetSaved = [this] (const juce::String& name)
    {
        // The saved preset becomes the current one (unedited)
        for (auto& saved : presetBrowser.readUserPresets())
            if (saved.name == name)
                presetBrowser.onPresetChosen (saved);
    };

    addAndMakeVisible (presetBrowser);
    addHint (presetBrowser, "PRESETS", presetHint);

    addAndMakeVisible (hintBar);
}

EasyWindow::~EasyWindow()
{
    // (By now each plugin's own knobs have already gone, so this mustn't
    // touch them. Their hover listeners went with them.)
    stopTimer();
    setLookAndFeel (nullptr);
}

void EasyWindow::finishSetup (int width, int height)
{
    // Applied last, once everything has been added, so every part of the
    // window picks up the custom look (including the knobs' value boxes).
    setLookAndFeel (&lookAndFeel);

    setSize (width, height);
    refreshPresetDisplay();
    showHintFor (nullptr);
    refreshLiveDisplays();
    startTimerHz (30);
}

//==============================================================================
void EasyWindow::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    auto area = titleArea;
    g.setColour (Palette::textBright);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText (title, area.removeFromTop (30), juce::Justification::bottomLeft, false);

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText (subtitle, area, juce::Justification::centredLeft, false);

    paintBody (g);
}

void EasyWindow::resized()
{
    auto area = getLocalBounds().reduced (20);

    // Header: title on the left; BYPASS, AUTO-GAIN and the presets on the right
    auto header = area.removeFromTop (50);
    presetBrowser.setBounds (header.removeFromRight (370).withSizeKeepingCentre (370, 34));   // room for long preset names
    header.removeFromRight (10);

    if (auto* second = easyProcessor.hasAutoGain() ? &autoGainButton : headerButton)
    {
        second->setBounds (header.removeFromRight (136).withSizeKeepingCentre (136, 34));
        header.removeFromRight (10);
    }

    bypassButton.setBounds (header.removeFromRight (116).withSizeKeepingCentre (116, 34));
    titleArea = header;
    area.removeFromTop (14);

    // Help bar along the bottom
    hintBar.setBounds (area.removeFromBottom (66));
    area.removeFromBottom (14);

    layoutBody (area);
}

//==============================================================================
void EasyWindow::addHint (juce::Component& component, const juce::String& hintTitle, const juce::String& text)
{
    addHint (component, [hintTitle, text] { return std::make_pair (hintTitle, text); });
}

void EasyWindow::addHint (juce::Component& component, std::function<std::pair<juce::String, juce::String>()> titleAndText)
{
    component.addMouseListener (this, true);   // hovering it (or anything inside it) updates the help bar
    hints.push_back ({ &component, std::move (titleAndText) });
}

void EasyWindow::mouseEnter (const juce::MouseEvent& event)
{
    hovered = event.eventComponent;
    showHintFor (hovered.getComponent());
}

void EasyWindow::mouseExit (const juce::MouseEvent&)
{
    hovered = nullptr;
    showHintFor (nullptr);
}

void EasyWindow::showHintFor (const juce::Component* component)
{
    if (component != nullptr)
    {
        for (auto& hint : hints)
        {
            if (component == hint.component || hint.component->isParentOf (component))
            {
                const auto [hintTitle, text] = hint.titleAndText();
                hintBar.setHint (hintTitle, text);
                return;
            }
        }
    }

    // Over nothing in particular: the current preset's tip, or how to get started.
    if (const auto* preset = presetBrowser.findPreset (easyProcessor.getPresetName()))
        hintBar.setHint (preset->name, preset->tip);
    else
        hintBar.setHint ("GETTING STARTED", gettingStarted);
}

//==============================================================================
void EasyWindow::knobMoved()
{
    if (loadingPreset)
        return;

    easyProcessor.markPresetEdited();
    refreshPresetDisplay();
}

void EasyWindow::refreshPresetDisplay()
{
    presetBrowser.showCurrent (easyProcessor.getPresetName(), easyProcessor.isPresetEdited());
}

void EasyWindow::timerCallback()
{
    refreshPresetDisplay();
    showHintFor (hovered.getComponent());   // keeps hints that can change (and preset tips) up to date
    refreshLiveDisplays();
}

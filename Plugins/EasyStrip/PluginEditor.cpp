#include "PluginEditor.h"

namespace
{
    constexpr int windowWidth = 1500, windowHeight = 960;

    const juce::String stripGettingStarted =
        "Every module starts switched off. Click a module's ON lamp to switch it on, and its tab to open its window "
        "(the same window, presets and INIT as the standalone plugin). Switched-off modules cost nothing.";

    const juce::String onHint =
        "Switches this module on or off. Off, it isn't run at all - the sound just passes straight through "
        "(on time: the strip's delay never changes). Same as the BYPASS button in the module's own window.";

    const juce::String tabHint = "Opens this module's own window below.";

    const juce::String initAllHint =
        "Resets the whole strip: every module switched off and back to its defaults, TRIM and LEVEL to 0 dB. "
        "(Each module's own INIT, in its window, resets just that module.)";

    const juce::String trimHint =
        "The level going into the strip. Aim for the INPUT meter peaking around -6 dB - "
        "the gate, amp and compressor react to how loud this is.";

    const juce::String polarityHint =
        "Flips the sound upside down. Use it when two microphones on one source (e.g. snare top and bottom) "
        "sound thin together: whichever setting sounds fuller is right.";

    const juce::String levelHint = "The level coming out of the strip, after everything else.";

    const juce::String oversamplingHint =
        "How carefully AMP and CLIP work (one setting for both). 4x / 8x: 4x while you mix, 8x when you export - "
        "the best of both. OFF: lightest on your computer, a little harsher.";

    const juce::String lufsHint =
        "Loudness the way streaming services measure it. S: the last 3 seconds. I: the whole song since you "
        "last reset it - click it to start again. (Spotify / YouTube aim for about -14 LUFS.)";

    juce::String lufsText (const juce::String& prefix, float lufs)
    {
        return prefix + (lufs <= -69.95f ? juce::String ("--.-") : juce::String (lufs, 1)) + " LUFS";
    }
}

//==============================================================================
void LevelMeter::setLevel (float gain)
{
    const float db = juce::Decibels::gainToDecibels (gain, -100.0f);
    shownDb = db > shownDb ? db : juce::jmax (db, shownDb - 1.5f);   // (falls back ~45 dB a second)

    if (db >= holdDb)          { holdDb = db; holdFor = 45; }
    else if (--holdFor < 0)    { holdDb = juce::jmax (db, holdDb - 0.5f); }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    g.setColour (Palette::panel.darker (0.5f));
    g.fillRoundedRectangle (area, 3.0f);

    const auto yFor = [&] (float db) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f); };

    const float top = yFor (shownDb);
    g.setGradientFill (juce::ColourGradient (Palette::meterHigh, 0.0f, yFor (0.0f), Palette::meterLow, 0.0f, yFor (-18.0f), false));
    g.fillRect (area.withTop (top).reduced (3.0f, 0.0f));

    if (holdDb > -60.0f)
    {
        g.setColour (holdDb > 0.0f ? Palette::meterHigh : Palette::textBright);
        g.fillRect (area.getX() + 3.0f, yFor (holdDb) - 1.0f, area.getWidth() - 6.0f, 2.0f);
    }

    g.setColour (Palette::textDim.withAlpha (0.6f));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
        g.fillRect (area.getRight() - 6.0f, yFor (db), 6.0f, 1.0f);
}

//==============================================================================
EasyStripAudioProcessorEditor::EasyStripAudioProcessorEditor (EasyStripAudioProcessor& p)
    : EasyWindow (p, "AARDVARK EASY STRIP", "Every Easy plugin in one channel strip", stripGettingStarted),
      oversamplingSwitch (*p.parameters.getParameter (StripIDs::oversampling), { "4x / 8x", "OFF", "4x", "8x" }, Palette::accent),
      strip (p)
{
    // The modules: a tab and an ON lamp each
    for (int m = 0; m < EasyStripAudioProcessor::numModules; ++m)
    {
        auto& tab = tabs[(size_t) m];
        tab.setButtonText (StripModules::tabs[m]);
        tab.setClickingTogglesState (false);
        tab.setColour (juce::TextButton::buttonColourId, Palette::panel);
        tab.setColour (juce::TextButton::buttonOnColourId, Palette::accent.withAlpha (0.35f));
        tab.onClick = [this, m] { showModule (m); };
        addAndMakeVisible (tab);
        addHint (tab, StripModules::tabs[m], tabHint);

        auto lamp = std::make_unique<LightButton> ("ON", Palette::meterLow);
        lamp->setClickingTogglesState (false);
        lamp->onClick = [this, m]
        {
            auto* power = strip.parameters.getParameter (StripIDs::moduleID (m, CommonIDs::bypass));
            power->beginChangeGesture();
            power->setValueNotifyingHost (strip.isModuleOn (m) ? 1.0f : 0.0f);
            power->endChangeGesture();
            onLamps[(size_t) m]->setToggleState (strip.isModuleOn (m), juce::dontSendNotification);
        };
        addAndMakeVisible (*lamp);
        addHint (*lamp, juce::String (StripModules::tabs[m]) + " ON", onHint);
        onLamps[(size_t) m] = std::move (lamp);
    }

    // Header: the whole strip's INIT
    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.onClick = [this]
    {
        presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front());   // (every module off, at its defaults)
    };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    // INPUT and OUTPUT
    const auto setUpKnob = [this] (juce::Slider& knob, juce::Label& label, const juce::String& name, const char* id,
                                   std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>& attachment,
                                   const juce::String& hint)
    {
        knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        knob.getProperties().set ("fillFromCentre", true);
        addAndMakeVisible (knob);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, id, knob);
        knob.onValueChange = [this] { knobMoved(); };

        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);

        addHint (knob, name, hint);
        addHint (label, name, hint);
    };
    setUpKnob (trimKnob, trimLabel, "TRIM", StripIDs::trim, trimAttachment, trimHint);
    setUpKnob (levelKnob, levelLabel, "LEVEL", StripIDs::level, levelAttachment, levelHint);

    addAndMakeVisible (polarityButton);
    polarityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (easyProcessor.parameters, StripIDs::polarity, polarityButton);
    addHint (polarityButton, "POLARITY", polarityHint);

    addAndMakeVisible (inputMeter);
    addHint (inputMeter, "INPUT", "How loud the sound is going in, after TRIM (0 dB at the top mark).");
    addAndMakeVisible (outputMeter);
    addHint (outputMeter, "OUTPUT", "How loud the sound is coming out, after LEVEL. Keep the peaks below 0 dB.");

    for (auto* readout : { &shortTermReadout, &integratedReadout })
    {
        readout->setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        readout->setJustificationType (juce::Justification::centred);
        readout->setColour (juce::Label::textColourId, Palette::textBright);
        readout->setInterceptsMouseClicks (true, false);
        addAndMakeVisible (*readout);
        addHint (*readout, "LUFS", lufsHint);
    }

    oversamplingSwitch.setHorizontal (true);
    oversamplingLabel.setText ("OVERSAMPLING (AMP, CLIP)", juce::dontSendNotification);
    oversamplingLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    oversamplingLabel.setColour (juce::Label::textColourId, Palette::textDim);
    oversamplingLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (oversamplingLabel);
    addAndMakeVisible (oversamplingSwitch);
    addHint (oversamplingLabel, "OVERSAMPLING", oversamplingHint);
    addHint (oversamplingSwitch, "OVERSAMPLING", oversamplingHint);

    addAndMakeVisible (moduleHolder);

    finishSetup (windowWidth, windowHeight);

    for (auto* knob : { &trimKnob, &levelKnob })
        for (auto* child : knob->getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);   // (double-click the number to type one)

    showModule (0);
}

EasyStripAudioProcessorEditor::~EasyStripAudioProcessorEditor()
{
    closeModuleWindow();
}

void EasyStripAudioProcessorEditor::closeModuleWindow()
{
    if (moduleWindow != nullptr)
    {
        moduleHolder.removeChildComponent (moduleWindow.get());
        strip.getModule (shownModule).editorBeingDeleted (moduleWindow.get());
        moduleWindow.reset();
    }
}

void EasyStripAudioProcessorEditor::showModule (int m)
{
    if (m == shownModule)
        return;

    closeModuleWindow();

    shownModule = m;
    moduleWindow.reset (strip.getModule (m).createEditorAndMakeActive());

    if (moduleWindow != nullptr)
    {
        moduleHolder.addAndMakeVisible (*moduleWindow);
        resized();
    }

    for (int i = 0; i < EasyStripAudioProcessor::numModules; ++i)
        tabs[(size_t) i].setToggleState (i == m, juce::dontSendNotification);
}

//==============================================================================
void EasyStripAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    // The header: INIT where AUTO-GAIN would be, small, just left of the presets
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    bypassButton.setBounds (bypassButton.getBounds().withX (initAllButton.getX() - 40 - bypassButton.getWidth()));

    inputPanel = area.removeFromLeft (150);
    area.removeFromLeft (14);
    outputPanel = area.removeFromRight (170);
    area.removeFromRight (14);
    modulePanel = area;

    const auto placeKnob = [] (juce::Slider& knob, juce::Label& label, juce::Rectangle<int>& column)
    {
        label.setBounds (column.removeFromTop (20));
        knob.setBounds (column.removeFromTop (120).reduced (10, 0));
    };

    auto input = inputPanel.reduced (12).withTrimmedTop (24);
    placeKnob (trimKnob, trimLabel, input);
    input.removeFromTop (10);
    polarityButton.setBounds (input.removeFromTop (32).withSizeKeepingCentre (60, 32));
    input.removeFromTop (16);
    inputMeter.setBounds (input.withSizeKeepingCentre (28, input.getHeight()));

    auto output = outputPanel.reduced (12).withTrimmedTop (24);
    placeKnob (levelKnob, levelLabel, output);
    output.removeFromTop (10);
    integratedReadout.setBounds (output.removeFromBottom (26));
    shortTermReadout.setBounds (output.removeFromBottom (26));
    output.removeFromBottom (10);
    outputMeter.setBounds (output.withSizeKeepingCentre (28, output.getHeight()));

    // The tabs, each with its ON lamp underneath
    auto middle = modulePanel.reduced (10);
    auto tabRow = middle.removeFromTop (34);
    auto lampRow = middle.removeFromTop (30);
    const int tabWidth = tabRow.getWidth() / EasyStripAudioProcessor::numModules;
    for (int m = 0; m < EasyStripAudioProcessor::numModules; ++m)
    {
        tabs[(size_t) m].setBounds (tabRow.removeFromLeft (tabWidth).reduced (3, 0));
        onLamps[(size_t) m]->setBounds (lampRow.removeFromLeft (tabWidth).withSizeKeepingCentre (56, 24));
    }

    middle.removeFromTop (8);
    auto switchRow = middle.removeFromTop (30);
    oversamplingSwitch.setBounds (switchRow.removeFromRight (300));
    switchRow.removeFromRight (10);
    oversamplingLabel.setBounds (switchRow.removeFromRight (220));
    middle.removeFromTop (8);
    moduleArea = middle;

    // The module's own window, shrunk to fit
    if (moduleWindow != nullptr)
    {
        const int w = moduleWindow->getWidth(), h = moduleWindow->getHeight();
        const float scale = juce::jmin (1.0f, (float) moduleArea.getWidth() / (float) w, (float) moduleArea.getHeight() / (float) h);
        const float x = (float) moduleArea.getX() + ((float) moduleArea.getWidth() - (float) w * scale) * 0.5f;
        const float y = (float) moduleArea.getY();
        moduleWindow->setBounds (0, 0, w, h);
        moduleHolder.setBounds (0, 0, w, h);
        moduleHolder.setTransform (juce::AffineTransform::scale (scale).translated (x, y));
    }
}

void EasyStripAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    drawPanel (g, inputPanel, {}, "INPUT");
    drawPanel (g, outputPanel, {}, "OUTPUT");
    drawPanel (g, modulePanel, {}, {});
}

void EasyStripAudioProcessorEditor::refreshLiveDisplays()
{
    inputMeter.setLevel (strip.takeInputPeak());
    outputMeter.setLevel (strip.takeOutputPeak());
    shortTermReadout.setText (lufsText ("S  ", strip.getShortTermLufs()), juce::dontSendNotification);
    integratedReadout.setText (lufsText ("I  ", strip.getIntegratedLufs()), juce::dontSendNotification);

    for (int m = 0; m < EasyStripAudioProcessor::numModules; ++m)
        onLamps[(size_t) m]->setToggleState (strip.isModuleOn (m), juce::dontSendNotification);
}

void EasyStripAudioProcessorEditor::mouseDown (const juce::MouseEvent& event)
{
    if (event.eventComponent == &integratedReadout)
        strip.resetIntegratedLufs();
}

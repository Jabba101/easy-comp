#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    const std::array<KnobInfo, EasyUnbleedAudioProcessorEditor::numKnobs> knobInfo
    {{
        { UnbleedIDs::amount, "AMOUNT",
          "How far it turns a ghost down. 15-25 dB makes bleed hard to hear; 30-40 dB is near-silence in the gaps. More than "
          "the bleed needs just makes the gaps sound empty." },
        { UnbleedIDs::sensitivity, "SENSITIVITY",
          "How readily something counts as a ghost. Low: only in clear gaps, with the other stems much louder. High: also in "
          "quieter passages - check with REMOVED ONLY that none of this stem's own sound is going." },
        { UnbleedIDs::smoothness, "SMOOTHNESS",
          "How gently it turns down. Low: tight and quick (drums). High: smooth, no fluttering (vocals, pads). It always "
          "opens straight away when this stem comes back in." },
        { UnbleedIDs::from, "FROM",
          "Works only above this frequency (Off = from the very bottom). E.g. 3 kHz to clean just the hi-hat and cymbal hiss "
          "out of a vocal." },
        { UnbleedIDs::to, "TO",
          "Works only below this frequency (Off = to the very top). E.g. 400 Hz to clean just the low end of a bass stem." },
    }};

    const std::array<const char*, 4> groupNames { "HOW MUCH", "HOW", "WHERE", "LISTEN" };

    constexpr const char* listenHint =
        "Hear only what it's taking out. It should be the other instruments' ghost - if you hear this stem's own sound "
        "(breaths, word endings, note tails), lower SENSITIVITY or AMOUNT. Remember to switch it off.";
    constexpr const char* displayHint =
        "Band by band, low to high: amber = this stem, blue = the other stems (the sidechain), red from the top = how much "
        "it's turning that band down right now. The shaded range is outside FROM / TO.";
    constexpr const char* statusHint =
        "Easy Unbleed needs the other stems sent to its sidechain. Copies of this same stem must never go there - if one "
        "does, it notices and does nothing, so the stem is safe.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS and REMOVED ONLY stay as they are.";
    constexpr const char* gettingStartedHint =
        "Put it on a separated stem and send the OTHER stems to its sidechain. Pick a preset, then check with REMOVED ONLY. "
        "Hover over anything for help.";

    constexpr float topDb = 6.0f, rangeDb = 72.0f, cutScaleDb = 40.0f;
}

//==============================================================================
void UnbleedDisplay::setPicture (const EasyUnbleedAudioProcessor::Picture& p, float from, float to, bool isBypassed)
{
    picture = p;
    fromHz = from; toHz = to; bypassed = isBypassed;
    for (int b = 0; b < p.numBands; ++b)
    {
        // (levels fall back gently, so the bars are readable; cuts follow at once)
        const auto follow = [] (float& shown, float now) { shown = now > shown ? now : shown + 0.3f * (now - shown); };
        follow (shownStem[(size_t) b], p.stemDb[(size_t) b]);
        follow (shownOthers[(size_t) b], p.othersDb[(size_t) b]);
        shownCut[(size_t) b] = p.reductionDb[(size_t) b];
    }
    repaint();
}

void UnbleedDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);
    const auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (30.0f).withTrimmedBottom (34.0f);
    const int bands = juce::jmax (1, picture.numBands);
    const float slot = plot.getWidth() / (float) bands;
    const auto levelY = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - (topDb - rangeDb)) / rangeDb); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (levelY (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 32.0f, levelY (db) - 7.0f, 28.0f, 14.0f), juce::Justification::centredRight, false);
    }

    for (int b = 0; b < picture.numBands; ++b)
    {
        const float x = plot.getX() + slot * (float) b;
        const float hz = picture.centreHz[(size_t) b];
        const bool inRange = hz >= fromHz * 0.89f && hz <= toHz * 1.12f;
        if (! inRange)
        {
            g.setColour (Palette::panel.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), slot, plot.getHeight()));
        }
        // The other stems (behind), this stem (in front)
        const float w = juce::jmax (2.0f, slot - 3.0f);
        const float yo = levelY (shownOthers[(size_t) b]), ys = levelY (shownStem[(size_t) b]);
        g.setColour (Palette::meterLow.interpolatedWith (juce::Colours::blue, 0.6f).withAlpha (0.55f));
        g.fillRect (juce::Rectangle<float> (x + 1.0f, yo, w, plot.getBottom() - yo));
        g.setColour (Palette::accent.withAlpha (0.85f));
        g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.25f, ys, w * 0.5f, plot.getBottom() - ys));
        // How much it's turning down, from the top
        const float cut = juce::jlimit (0.0f, 1.0f, shownCut[(size_t) b] / cutScaleDb);
        if (cut > 0.005f)
        {
            g.setColour (Palette::meterHigh.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (x + 1.0f, plot.getY(), w, plot.getHeight() * 0.5f * cut));
        }
    }

    // Frequency labels
    g.setColour (Palette::textDim);
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        int nearest = 0;
        for (int b = 0; b < picture.numBands; ++b)
            if (std::abs (std::log (picture.centreHz[(size_t) b] / hz)) < std::abs (std::log (picture.centreHz[(size_t) nearest] / hz))) nearest = b;
        const float x = plot.getX() + slot * ((float) nearest + 0.5f);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 3.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    // Key
    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 19.0f, plot.getWidth(), 14.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float width)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (width), juce::Justification::centredLeft, false);
        key.removeFromLeft (10.0f);
    };
    item (Palette::accent, "this stem", 64.0f);
    item (Palette::meterLow.interpolatedWith (juce::Colours::blue, 0.6f), "the other stems", 104.0f);
    item (Palette::meterHigh, "turned down (from the top)", 200.0f);

    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (area);
        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (area.getCentre());
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (badge, 8.0f);
        g.setColour (Palette::meterHigh);
        g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);
        auto lines = badge.reduced (10.0f, 9.0f);
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.drawText ("BYPASSED", lines.removeFromTop (lines.getHeight() * 0.55f), juce::Justification::centred, false);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.drawText ("You're hearing the original stem", lines, juce::Justification::centred, false);
    }
}

//==============================================================================
EasyUnbleedAudioProcessorEditor::EasyUnbleedAudioProcessorEditor (EasyUnbleedAudioProcessor& p)
    : EasyWindow (p, "EASY UNBLEED", "Cleans other instruments' ghost out of a separated stem - hover over anything for help", gettingStartedHint),
      unbleedProcessor (p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (i == amountKnob ? 15.0f : 13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);
        knob.slider.onValueChange = [this] { knobMoved(); };
        addHint (knob.slider, knobInfo[i].name, knobInfo[i].hint);
        addHint (knob.label, knobInfo[i].name, knobInfo[i].hint);
    }

    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    listenButton.setClickingTogglesState (true);
    listenAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (UnbleedIDs::listen), listenButton);
    addAndMakeVisible (listenButton);
    addHint (listenButton, "REMOVED ONLY", listenHint);

    listenInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    listenInfo.setColour (juce::Label::textColourId, Palette::textDim);
    listenInfo.setJustificationType (juce::Justification::centred);
    listenInfo.setText ("Hear only what it takes out: it should be other instruments, never this stem's own sound.", juce::dontSendNotification);
    addAndMakeVisible (listenInfo);
    addHint (listenInfo, "REMOVED ONLY", listenHint);

    status.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    status.setColour (juce::Label::textColourId, Palette::meterHigh);
    status.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (status);
    addHint (status, "SIDECHAIN", statusHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyUnbleedAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
}

void EasyUnbleedAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    bypassButton.setBounds (bypassButton.getBounds().withX (initAllButton.getX() - 34 - bypassButton.getWidth()));

    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
    const auto placeKnob = [&] (Knob& knob, juce::Rectangle<int> column, int knobSize)
    {
        auto c = column.withSizeKeepingCentre (column.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
        knob.label.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (spacing);
        knob.slider.setBounds (c.withSizeKeepingCentre (knobSize + 20, c.getHeight()));
    };
    const auto knobSizeFor = [&] (juce::Rectangle<int> c) { return juce::jmin (c.getWidth() - 30, c.getHeight() - nameHeight - spacing - valueBoxHeight); };

    auto left = area.removeFromLeft (270);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (270);
    area.removeFromRight (gap);
    displayPanel = area;
    {
        auto inside = displayPanel.reduced (12, 12).withTrimmedTop (24);
        status.setBounds (inside.removeFromBottom (22));
        inside.removeFromBottom (6);
        display.setBounds (inside);
    }

    groupPanels[0] = left.removeFromTop ((left.getHeight() - gap) / 2);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    {
        auto inside = groupPanels[0].reduced (10, 12).withTrimmedTop (26);
        placeKnob (knobs[amountKnob], inside, knobSizeFor (inside));
        inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto a = inside.removeFromLeft (inside.getWidth() / 2);
        placeKnob (knobs[sensitivityKnob], a, knobSizeFor (a));
        placeKnob (knobs[smoothnessKnob], inside, knobSizeFor (inside));
    }

    groupPanels[2] = right.removeFromTop ((right.getHeight() - gap) / 2);
    right.removeFromTop (gap);
    groupPanels[3] = right;
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        auto a = inside.removeFromLeft (inside.getWidth() / 2);
        placeKnob (knobs[fromKnob], a, knobSizeFor (a));
        placeKnob (knobs[toKnob], inside, knobSizeFor (inside));
        inside = groupPanels[3].reduced (14, 12).withTrimmedTop (30);
        listenButton.setBounds (inside.removeFromTop (34).withSizeKeepingCentre (150, 34));
        inside.removeFromTop (6);
        listenInfo.setBounds (inside);
    }
}

void EasyUnbleedAudioProcessorEditor::refreshLiveDisplays()
{
    const auto s = unbleedProcessor.getCurrentSettings();
    display.setPicture (unbleedProcessor.getPicture(), s.fromHz, s.toHz, s.bypassed);

    juce::String text;
    if (unbleedProcessor.isSidechainACopy())
        text = "The sidechain is a copy of this stem - ignored, nothing is removed";
    else if (unbleedProcessor.isSidechainMissing())
        text = "Nothing in the sidechain: send the OTHER stems to it";
    if (text != status.getText())
        status.setText (text, juce::dontSendNotification);
}

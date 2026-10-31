#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    const std::array<KnobInfo, EasyFlangeAudioProcessorEditor::numKnobs> knobInfo
    {{
        { FlangeIDs::rate, "RATE",
          "How fast it swings, in swings a second (Hz). Flanger: slow (0.1-0.5) for a big jet sweep. Chorus: 0.3-1. Vibrato: "
          "4-7 for a singer's vibrato. With SYNC on TEMPO you pick a note length instead, locked to the beat." },

        { FlangeIDs::note, "RATE",
          "How long one full swing takes, as a note length at your song's tempo - locked to the beat while the song plays. "
          "1, 2 or 4 bars give the classic slow flanger sweep through a section." },

        { FlangeIDs::depth, "DEPTH",
          "How far it swings. Flanger: how far the whoosh travels. Chorus: how much the copies drift (more = more "
          "shimmer, too much = seasick). Vibrato: how wide the pitch wobble is." },

        { FlangeIDs::feedback, "FEEDBACK",
          "FLANGER only: feeds the copy back in. Above 0 = sharper, ringing, metallic; below 0 = hollow and tubular. 0 = a "
          "gentle, smooth flange." },

        { FlangeIDs::width, "WIDTH",
          "The left and right sides swinging apart: 0 = the same on both sides, 100% = opposite each other - very wide. "
          "(Needs a stereo track or a stereo output.)" },

        { FlangeIDs::mix, "MIX",
          "0% = your sound only, 50% = an even blend (the deepest flanging, the classic chorus), 100% = the swinging copy "
          "only (use this for VIBRATO)." },
    }};

    const std::array<const char*, 4> groupNames { "TYPE", "SPEED", "HOW MUCH", "OUTPUT" };

    constexpr const char* typeHint =
        "FLANGER: a very short, swept delay - the jet-plane whoosh. CHORUS: two slightly drifting copies - wider, thicker, "
        "shimmering. VIBRATO: the pitch itself wobbles (set MIX to 100%).";
    constexpr const char* syncHint =
        "OFF: set RATE in swings a second. TEMPO: set it as a note length (up to 8 bars) locked to your song's beat.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS stays as it is.";
    constexpr const char* displayHint =
        "Two swings of the delay going back and forth: left side amber, right side green (WIDTH pulls them apart). The dot "
        "is where it is now.";
    constexpr const char* gettingStartedHint =
        "Pick a preset, or choose FLANGER, CHORUS or VIBRATO and turn DEPTH and MIX up. Double-click a knob to reset it, or "
        "its number to type a value.";
}

//==============================================================================
void SwingDisplay::setSettings (const Flanger::Settings& s)
{
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    if (s.type != settings.type || moved (s.depth, settings.depth) || moved (s.width, settings.width))
    {
        settings = s;
        repaint();
    }
}

void SwingDisplay::setPosition (float p)
{
    if (std::abs (p - position) > 0.002f) { position = p; repaint(); }
}

void SwingDisplay::setBypassed (bool b)
{
    if (bypassed != b) { bypassed = b; repaint(); }
}

void SwingDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);
    const auto plot = area.reduced (18.0f, 22.0f).withTrimmedLeft (34.0f);

    const float maxMs = settings.type == Flanger::Type::chorus ? 20.0f : 8.0f;
    const auto yOf = [&] (float ms) { return plot.getBottom() - plot.getHeight() * ms / maxMs; };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float ms = 0.0f; ms <= maxMs + 0.01f; ms += maxMs / 4.0f)
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (ms)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String (ms, 0) + " ms", juce::Rectangle<float> (plot.getX() - 46.0f, yOf (ms) - 7.0f, 42.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (Palette::panelEdge);
    g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());

    const auto delayAt = [&] (double p) { return Flanger::delayMsAt (settings.type, settings.depth, 0.5f - 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * p)); };

    for (int side = 1; side >= 0; --side)
    {
        juce::Path path;
        const double offset = side == 1 ? 0.5 * settings.width : 0.0;
        for (int i = 0; i <= 300; ++i)
        {
            const double x = i / 300.0;
            const juce::Point<float> pt (plot.getX() + plot.getWidth() * (float) x, yOf (delayAt (x * 2.0 + offset)));
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        g.setColour (side == 0 ? Palette::accent : Palette::meterLow);
        g.strokePath (path, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float x = plot.getX() + plot.getWidth() * position * 0.5f;
        g.setColour (Palette::textBright);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, yOf (delayAt (position + offset)) }));
    }

    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.setColour (Palette::accent);
    g.drawText ("LEFT", plot.withHeight (16.0f).translated (4.0f, -18.0f), juce::Justification::centredLeft, false);
    g.setColour (Palette::meterLow);
    g.drawText ("RIGHT", plot.withHeight (16.0f).translated (50.0f, -18.0f), juce::Justification::centredLeft, false);

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
        g.drawText ("You're hearing the original sound", lines, juce::Justification::centred, false);
    }
}

//==============================================================================
EasyFlangeAudioProcessorEditor::EasyFlangeAudioProcessorEditor (EasyFlangeAudioProcessor& p)
    : EasyWindow (p, "EASY FLANGE", "Flanger, chorus and vibrato - hover over anything for help", gettingStartedHint),
      flangeProcessor (p),
      typeSwitch (*p.parameters.getParameter (FlangeIDs::type), { "FLANGER", "CHORUS", "VIBRATO" }, Palette::accent),
      syncSwitch (*p.parameters.getParameter (FlangeIDs::sync), { "OFF", "TEMPO" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        if (i == feedbackKnob)
            knob.slider.getProperties().set ("fillFromCentre", true);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (i <= noteKnob ? 15.0f : 13.0f, juce::Font::bold)));
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

    addAndMakeVisible (typeSwitch);
    typeSwitch.onUserChange = [this] { knobMoved(); };
    addHint (typeSwitch, "TYPE", typeHint);

    syncLabel.setText ("SYNC", juce::dontSendNotification);
    syncLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    syncLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (syncLabel);
    addAndMakeVisible (syncSwitch);
    syncSwitch.onUserChange = [this] { knobMoved(); };
    addHint (syncLabel, "SYNC", syncHint);
    addHint (syncSwitch, "SYNC", syncHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    showSyncedKnob (juce::roundToInt (p.parameters.getRawParameterValue (FlangeIDs::sync)->load()) == 1);
    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyFlangeAudioProcessorEditor::showSyncedKnob (bool synced)
{
    showingSynced = synced;
    knobs[rateKnob].slider.setVisible (! synced);
    knobs[rateKnob].label.setVisible (! synced);
    knobs[noteKnob].slider.setVisible (synced);
    knobs[noteKnob].label.setVisible (synced);
}

void EasyFlangeAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
}

void EasyFlangeAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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

    auto left = area.removeFromLeft (300);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (300);
    area.removeFromRight (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    groupPanels[0] = left.removeFromTop (170);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    {
        auto inside = groupPanels[0].reduced (40, 12).withTrimmedTop (30);
        typeSwitch.setBounds (inside.withSizeKeepingCentre (inside.getWidth(), 3 * 36 - 4));
    }
    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto knobSpace = inside.removeFromLeft (inside.getWidth() * 3 / 5);
        const int size = juce::jmin (knobSpace.getWidth() - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[rateKnob], knobSpace, size);
        placeKnob (knobs[noteKnob], knobSpace, size);
        auto c = inside.withSizeKeepingCentre (inside.getWidth(), nameHeight + 6 + 2 * 32);
        syncLabel.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (6);
        syncSwitch.setBounds (c.withSizeKeepingCentre (juce::jmin (110, c.getWidth()), c.getHeight()));
    }

    const int half = (right.getHeight() - gap) / 2;
    groupPanels[2] = right.removeFromTop (half);
    right.removeFromTop (gap);
    groupPanels[3] = right;
    for (int pnl : { 2, 3 })
    {
        auto inside = groupPanels[(size_t) pnl].reduced (10, 12).withTrimmedTop (26);
        const int w = inside.getWidth() / 2;
        const int size = juce::jmin (w - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[pnl == 2 ? depthKnob : widthKnob], inside.removeFromLeft (w), size);
        placeKnob (knobs[pnl == 2 ? feedbackKnob : mixKnob], inside, size);
    }
}

void EasyFlangeAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = flangeProcessor.getCurrentSettings();
    if (settings.synced != showingSynced)
        showSyncedKnob (settings.synced);

    const bool flanger = settings.type == Flanger::Type::flanger;
    knobs[feedbackKnob].slider.setEnabled (flanger);
    knobs[feedbackKnob].label.setEnabled (flanger);
    knobs[feedbackKnob].slider.setAlpha (flanger ? 1.0f : 0.4f);
    knobs[feedbackKnob].label.setAlpha (flanger ? 1.0f : 0.4f);

    display.setSettings (settings);
    display.setPosition (flangeProcessor.getPosition());
    display.setBypassed (settings.bypassed);
}

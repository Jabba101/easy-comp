#include "PluginEditor.h"

namespace
{
    //==========================================================================
    // The plain-English explanations shown in the help bar. Edit freely!
    struct KnobInfo
    {
        const char* parameterID;
        const char* name;
        const char* hint;
    };

    const std::array<KnobInfo, EasyTapeAudioProcessorEditor::numKnobs> knobInfo
    {{
        { TapeIDs::drive, "DRIVE",
          "The record level: how hard the sound hits the tape. Watch the VU meter: around 0 VU is the subtle 'it's on tape' "
          "sound; +3 to +6 thickens and rounds the peaks; into the red is pushing it, the way engineers hit tape on drums. "
          "The volume stays put - only how hard the tape works changes." },

        { TapeIDs::wow, "WOW & FLUTTER",
          "The tape's speed wobbling: a slow drift in pitch (wow) and a fast shimmer (flutter). A good studio machine barely "
          "has any, so 0 is right for a real-tape sound. Turn it up (with CASSETTE) for a worn, warbly lo-fi tape." },

        { TapeIDs::hiss, "HISS",
          "Tape noise underneath your sound. 0 = none. A little gives a recording that 'printed to tape' air; a lot is the "
          "cassette-demo sound. It plays even in the quiet parts, as real tape does." },
    }};

    const std::array<const char*, 3> groupNames { "RECORD LEVEL", "MACHINE", "WEAR & TEAR" };

    constexpr const char* speedHint =
        "How fast the tape runs. 15 IPS: the multitrack machine - a deep, round head bump around 50 Hz and gentle softening of "
        "the highs when pushed. 30 IPS: the mastering machine - almost perfect, a smaller bump near 100 Hz, more headroom, a "
        "subtle touch. CASSETTE: lo-fi - less headroom, the highs squash early and roll off.";

    constexpr const char* initAllHint =
        "Puts every knob back to its default (the same as the 'Default settings' preset): 15 ips at the normal level, no "
        "wow & flutter, no hiss. BYPASS, AUTO-GAIN and OVERSAMPLING stay as they are.";

    constexpr const char* qualityHint =
        "Tape saturation creates new high harmonics; at the normal sample rate the highest ones can fold back down as harsh "
        "tones. 4x / 8x works at 4x while you mix and switches itself to 8x when your DAW exports the song. OFF uses the least "
        "computer power. The delay your DAW lines up for stays the same whichever you choose.";

    constexpr const char* displayHint =
        "Top: the VU meter - how hard your sound is hitting the tape (0 VU = the normal level; the red is pushing it). "
        "Bottom: the machine's frequency response at this SPEED, quietly: the head bump in the lows and how flat the top "
        "stays. Pushed harder, the loudest highs soften on top of this.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or play your track and turn DRIVE up while watching the VU meter: around 0 VU for a subtle tape sound, "
        "into the red to push it. Double-click a knob to reset it, or its number to type a value.";

    constexpr float vuMin = -20.0f, vuMax = 3.0f;
}

//==============================================================================
// The display
//==============================================================================
void TapeDisplay::setRecordVu (float vu)
{
    // (The reading already swings like a VU needle; this just smooths the steps between window updates.)
    const float target = juce::jlimit (vuMin - 2.0f, vuMax + 1.0f, vu);
    const float next = shownVu + (target - shownVu) * 0.6f;

    if (std::abs (next - shownVu) > 0.01f)
    {
        shownVu = next;
        repaint();
    }
}

void TapeDisplay::setSpeed (TapeMachine::Speed s)
{
    if (speed != s)
    {
        speed = s;
        repaint();
    }
}

void TapeDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

void TapeDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    paintMeter (g, area.removeFromTop (area.getHeight() * 0.56f).reduced (0.0f, 2.0f));
    area.removeFromTop (10.0f);
    paintResponse (g, area);

    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (getLocalBounds());

        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (getLocalBounds().toFloat().getCentre());
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

void TapeDisplay::paintMeter (juce::Graphics& g, juce::Rectangle<float> area)
{
    // The meter's face: warm, like the lit meters on a tape machine
    g.setColour (juce::Colour (0xff2b261c));
    g.fillRoundedRectangle (area, 6.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), 6.0f, 1.0f);

    juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (area.toNearestInt().reduced (1));

    const auto face = area.reduced (16.0f, 10.0f);
    const float radius = juce::jmin (face.getWidth() * 0.62f, face.getHeight() * 1.15f);
    const juce::Point<float> pivot (face.getCentreX(), face.getY() + radius + 6.0f);
    static constexpr float sweep = 0.78f;   // radians either side of straight up

    const auto angleOf = [] (float vu)
    {
        // A VU scale is in level, not dB: -20 sits near the left end, 0 about 70% across
        const float a = juce::Decibels::decibelsToGain (juce::jlimit (vuMin, vuMax, vu));
        const float lo = juce::Decibels::decibelsToGain (vuMin), hi = juce::Decibels::decibelsToGain (vuMax);
        return -sweep + 2.0f * sweep * (a - lo) / (hi - lo);
    };
    const auto at = [&] (float angle, float r)   { return pivot + juce::Point<float> (std::sin (angle) * r, -std::cos (angle) * r); };

    // The scale: black up to 0, red above
    for (int part = 0; part < 2; ++part)
    {
        juce::Path arc;
        const float from = angleOf (part == 0 ? vuMin : 0.0f), to = angleOf (part == 0 ? 0.0f : vuMax);
        arc.addCentredArc (pivot.x, pivot.y, radius * 0.86f, radius * 0.86f, 0.0f, from, to, true);
        g.setColour (part == 0 ? juce::Colour (0xffd9cfb3) : Palette::meterHigh);
        g.strokePath (arc, juce::PathStrokeType (part == 0 ? 1.5f : 4.0f));
    }

    g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    for (float mark : { -20.0f, -10.0f, -7.0f, -5.0f, -3.0f, -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f })
    {
        const float angle = angleOf (mark);
        g.setColour (mark > 0.0f ? Palette::meterHigh : juce::Colour (0xffd9cfb3));
        g.drawLine ({ at (angle, radius * 0.86f), at (angle, radius * 0.93f) }, 1.5f);

        const auto label = juce::Rectangle<float> (30.0f, 14.0f).withCentre (at (angle, radius * 1.01f));
        g.drawText (mark > 0.0f ? "+" + juce::String ((int) mark) : juce::String ((int) mark), label, juce::Justification::centred, false);
    }

    g.setColour (juce::Colour (0xffd9cfb3));
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText ("VU", juce::Rectangle<float> (60.0f, 20.0f).withCentre (at (0.0f, radius * 0.6f)), juce::Justification::centred, false);

    // The needle
    const float angle = angleOf (shownVu);
    g.setColour (shownVu > 0.0f ? Palette::meterHigh : Palette::accent);
    g.drawLine ({ at (angle, radius * 0.18f), at (angle, radius * 0.95f) }, 2.0f);

    // How hard, in words
    const juce::String state = shownVu < -12.0f ? "LOW LEVEL" : (shownVu < -3.0f ? "SUBTLE" : (shownVu < 0.5f ? "ON TAPE"
                             : (shownVu < 2.5f ? "PUSHED" : "HITTING IT HARD")));
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.setColour (shownVu > 0.5f ? Palette::meterHigh : Palette::accent);
    g.drawText (state, area.removeFromBottom (22.0f), juce::Justification::centred, false);
}

void TapeDisplay::paintResponse (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (34.0f, 10.0f).withTrimmedBottom (14.0f);
    constexpr float range = 4.0f;   // +-4 dB: real tape is that close to flat

    const auto xOf = [&] (double hz)   { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 20.0) / std::log (1000.0)); };
    const auto yOf = [&] (float db)    { return plot.getCentreY() - db / range * plot.getHeight() * 0.5f; };

    g.setFont (juce::Font (juce::FontOptions (10.5f)));
    for (double hz : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (30.0f, 12.0f).withCentre ({ xOf (hz), plot.getBottom() + 8.0f }), juce::Justification::centred, false);
    }

    for (float db : { -3.0f, 0.0f, 3.0f })
    {
        g.setColour (std::abs (db) < 0.01f ? Palette::textDim.withAlpha (0.6f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (28.0f, 12.0f).withCentre ({ plot.getX() - 16.0f, yOf (db) }),
                    juce::Justification::centred, false);
    }

    juce::Path curve;
    constexpr int points = 200;
    for (int i = 0; i < points; ++i)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) i / (points - 1));
        const float db = juce::jlimit (-range, range, TapeMachine::responseDb (speed, hz));
        const juce::Point<float> p (xOf (hz), yOf (db));
        if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
    }

    g.setColour (Palette::accent);
    g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// The Easy Tape window
//==============================================================================
EasyTapeAudioProcessorEditor::EasyTapeAudioProcessorEditor (EasyTapeAudioProcessor& p)
    : EasyWindow (p, "EASY TAPE", "Real tape weight and warmth - hover over anything for help", gettingStartedHint),
      tapeProcessor (p),
      speedSwitch (*p.parameters.getParameter (TapeIDs::speed), { "15 IPS", "30 IPS", "CASSETTE" }, Palette::accent),
      qualitySwitch (*p.parameters.getParameter (TapeIDs::quality), { "4x / 8x", "OFF", "4x", "8x" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (i == driveKnob ? 15.0f : 13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this] { knobMoved(); };

        addHint (knob.slider, knobInfo[i].name, knobInfo[i].hint);
        addHint (knob.label, knobInfo[i].name, knobInfo[i].hint);
    }

    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };   // "Default settings"
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    speedLabel.setText ("SPEED", juce::dontSendNotification);
    speedLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    speedLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (speedLabel);
    addAndMakeVisible (speedSwitch);
    speedSwitch.onUserChange = [this] { knobMoved(); };
    addHint (speedLabel, "SPEED", speedHint);
    addHint (speedSwitch, "SPEED", speedHint);

    qualitySwitch.setHorizontal (true);
    qualityLabel.setText ("OVERSAMPLING", juce::dontSendNotification);
    qualityLabel.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    qualityLabel.setColour (juce::Label::textColourId, Palette::textDim);
    qualityLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (qualityLabel);
    addAndMakeVisible (qualitySwitch);
    addHint (qualityLabel, "OVERSAMPLING", qualityHint);
    addHint (qualitySwitch, "OVERSAMPLING", qualityHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    finishSetup (1100, 640);

    // Double-click a number to type a value
    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

//==============================================================================
void EasyTapeAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");

    // The ends of DRIVE's turn
    const auto driveBounds = knobs[driveKnob].slider.getBounds();
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("GENTLE", juce::Rectangle<int> (driveBounds.getX() - 10, driveBounds.getBottom() - 46, 70, 16), juce::Justification::centredLeft, false);
    g.drawText ("HOT", juce::Rectangle<int> (driveBounds.getRight() - 80, driveBounds.getBottom() - 46, 90, 16), juce::Justification::centredRight, false);

    // What the level matching is doing
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.setColour (showingMatch ? Palette::meterLow : Palette::textDim);
    g.drawText (showingMatch ? "AUTO-GAIN " + juce::String (shownMatchDb >= 0.05f ? "+" : "") + juce::String (shownMatchDb, 1) + " dB"
                             : juce::String ("AUTO-GAIN OFF (the playback level still follows DRIVE)"),
                matchReadout, juce::Justification::centred, false);
}

void EasyTapeAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // The header: INIT small, right beside the preset box; AUTO-GAIN between
    // it and BYPASS, so a quick compare can't hit INIT by accident.
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    autoGainButton.setBounds (autoGainButton.getBounds().withX (initAllButton.getX() - 24 - autoGainButton.getWidth()));
    bypassButton.setBounds (bypassButton.getBounds().withX (autoGainButton.getX() - 10 - bypassButton.getWidth()));

    // The top row: OVERSAMPLING on the right
    auto row = area.removeFromTop (32);
    qualitySwitch.setBounds (row.removeFromRight (300));
    row.removeFromRight (10);
    qualityLabel.setBounds (row.removeFromRight (120));
    area.removeFromTop (gap);

    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;

    const auto placeKnob = [&] (Knob& knob, juce::Rectangle<int> column, int knobSize)
    {
        auto c = column.withSizeKeepingCentre (column.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
        knob.label.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (spacing);
        knob.slider.setBounds (c.withSizeKeepingCentre (knobSize + 20, c.getHeight()));
    };

    // Left: DRIVE, big
    groupPanels[0] = area.removeFromLeft (300);
    area.removeFromLeft (gap);
    {
        auto inside = groupPanels[0].reduced (12, 12).withTrimmedTop (26);
        placeKnob (knobs[driveKnob], inside, juce::jmin (200, inside.getHeight() - nameHeight - spacing - valueBoxHeight - 30));
    }

    // Right: MACHINE (SPEED) above WEAR & TEAR (WOW & FLUTTER, HISS)
    auto right = area.removeFromRight (300);
    area.removeFromRight (gap);
    const int panelHeight = (right.getHeight() - gap) / 2;
    groupPanels[1] = right.removeFromTop (panelHeight);
    right.removeFromTop (gap);
    groupPanels[2] = right;

    {
        auto inside = groupPanels[1].reduced (40, 12).withTrimmedTop (26);
        speedLabel.setBounds (inside.removeFromTop (nameHeight));
        inside.removeFromTop (spacing + 4);
        speedSwitch.setBounds (inside.withSizeKeepingCentre (inside.getWidth(), juce::jmin (inside.getHeight(), 3 * 40)));
    }
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        const int columnWidth = inside.getWidth() / 2;
        const int knobSize = juce::jmin (columnWidth - 20, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[wowKnob], inside.removeFromLeft (columnWidth), knobSize);
        placeKnob (knobs[hissKnob], inside, knobSize);
    }

    // The middle: the meter and the response
    displayPanel = area;
    auto inside = displayPanel.reduced (14, 12).withTrimmedTop (24);
    matchReadout = inside.removeFromBottom (18);
    inside.removeFromBottom (6);
    display.setBounds (inside);
}

//==============================================================================
void EasyTapeAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = tapeProcessor.getCurrentSettings();

    display.setRecordVu (tapeProcessor.getRecordVu());
    display.setSpeed (settings.speed);
    display.setBypassed (settings.bypassed);

    const float rounded = std::round (tapeProcessor.getMatchGainDb() * 10.0f) / 10.0f;
    if (settings.autoGain != showingMatch || std::abs (rounded - shownMatchDb) > 0.01f)
    {
        showingMatch = settings.autoGain;
        shownMatchDb = rounded;
        repaint (matchReadout);
    }
}

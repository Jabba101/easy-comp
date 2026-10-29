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

    const std::array<KnobInfo, EasyAmpAudioProcessorEditor::numKnobs> knobInfo
    {{
        { AmpIDs::drive, "DRIVE",
          "How hard the valve amp is pushed. 0 = clean (switched off). A little = warmth and thickness; around halfway the loud "
          "notes and hits start to break up; all the way = a growling, overdriven rock sound. AUTO-GAIN keeps the volume steady." },

        { AmpIDs::tone, "TONE",
          "Darker or brighter after the amp. Turn it down if the distortion sounds fizzy or harsh; up if it sounds dull or "
          "muffled. The middle changes nothing." },

        { AmpIDs::tight, "TIGHT",
          "Below this frequency your sound goes round the amp, clean. Keeps a bass's or kick's low end solid and stops "
          "distorted lows turning to mud. Try 80-120 Hz on bass and guitars. Off = everything goes through the amp." },

        { AmpIDs::mix, "MIX",
          "Blends your original sound back in. 100% = all amp. Lower it for parallel distortion: the clean sound keeps its "
          "punch and clarity, the amp adds grit and energy underneath." },
    }};

    const std::array<const char*, 3> groupNames { "HOW MUCH", "TONE", "OUTPUT" };

    constexpr const char* initAllHint =
        "Puts every knob back to its default (the same as the 'Default settings' preset): clean, nothing changed. "
        "BYPASS, AUTO-GAIN and OVERSAMPLING stay as they are.";

    constexpr const char* qualityHint =
        "Distortion creates new high harmonics; at the normal sample rate the highest ones fold back down as harsh, "
        "out-of-tune tones. AUTO works at 4x while you mix and 8x when your DAW exports the song. OFF uses the least "
        "computer power but sounds harsher. The delay your DAW lines up for stays the same whichever you choose.";

    constexpr const char* displayHint =
        "The amp's curve at this DRIVE: a sample going in (across) and what comes out (up). Straight = clean; the more it "
        "bends, the more it distorts; flatter on one side = the valve's lopsided warmth. The dot shows where your track is "
        "playing right now.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or play your track and turn DRIVE up: a little for warmth, more for grit, all the way for a growl. "
        "AUTO-GAIN keeps the volume steady. Double-click a knob to reset it, or its number to type a value.";
}

//==============================================================================
// The curve
//==============================================================================
void AmpCurveDisplay::setDrive (float driveAmount)
{
    if (std::abs (driveAmount - drive) > 0.0005f)
    {
        drive = driveAmount;
        repaint();
    }
}

void AmpCurveDisplay::setInputPeak (float peak)
{
    // Jumps up, falls back gently, so your eye can follow it
    const float next = peak >= shownPeak ? peak : juce::jmax (peak, shownPeak * 0.92f);

    if (std::abs (next - shownPeak) > 0.001f)
    {
        shownPeak = next;
        repaint();
    }
}

void AmpCurveDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

void AmpCurveDisplay::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float size = juce::jmin (area.getWidth(), area.getHeight() - 24.0f);
    const auto plot = juce::Rectangle<float> (size, size).withCentre ({ area.getCentreX(), area.getY() + size * 0.5f + 2.0f });

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot, 6.0f);

    // Grid: the middle lines, and the clean diagonal
    g.setColour (Palette::panelEdge);
    g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());
    {
        juce::Path diagonal;
        diagonal.startNewSubPath (plot.getBottomLeft());
        diagonal.lineTo (plot.getTopRight());
        juce::Path dashed;
        const float dashes[] = { 5.0f, 5.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, diagonal, dashes, 2);
        g.setColour (Palette::textDim.withAlpha (0.5f));
        g.fillPath (dashed);
    }

    // The curve, scaled so its loudest point reaches the edge (AUTO-GAIN
    // evens out the level; the shape is what matters)
    constexpr int points = 161;
    std::array<float, points> ys;
    float biggest = 1.0e-6f, slopeAtZero = 1.0e-6f;

    for (int i = 0; i < points; ++i)
    {
        const float x = inputSpan * (2.0f * (float) i / (float) (points - 1) - 1.0f);
        ys[(size_t) i] = ValveAmp::transfer (x, drive);
        biggest = juce::jmax (biggest, std::abs (ys[(size_t) i]));
    }

    slopeAtZero = std::abs (ValveAmp::transfer (0.001f, drive) - ValveAmp::transfer (-0.001f, drive)) / 0.002f;

    // Scaled to fill the height (AUTO-GAIN evens out the level; the shape is
    // what matters): clean, it lies on the diagonal; driven, it bends away.
    juce::ignoreUnused (slopeAtZero);
    const float scale = inputSpan / biggest;
    const auto toScreen = [&] (float x, float y)
    {
        return juce::Point<float> (plot.getCentreX() + x / inputSpan * plot.getWidth() * 0.5f,
                                   plot.getCentreY() - y * scale / inputSpan * plot.getHeight() * 0.5f);
    };

    juce::Path curve;
    for (int i = 0; i < points; ++i)
    {
        const float x = inputSpan * (2.0f * (float) i / (float) (points - 1) - 1.0f);
        const auto p = toScreen (x, ys[(size_t) i]);
        if (i == 0) curve.startNewSubPath (p); else curve.lineTo (p);
    }

    g.setColour (Palette::accent);
    g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Where the track is playing: a dot on each side at its latest peak
    if (shownPeak > 0.001f)
    {
        for (float sign : { 1.0f, -1.0f })
        {
            const float x = juce::jlimit (-inputSpan, inputSpan, sign * shownPeak);
            const auto p = toScreen (x, ValveAmp::transfer (x, drive));
            g.setColour (Palette::textBright);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
        }
    }

    // Labels
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("in", plot.withTrimmedLeft (plot.getWidth() - 24.0f).withY (plot.getCentreY() + 2.0f).withHeight (14.0f), juce::Justification::centredLeft, false);
    g.drawText ("out", plot.withX (plot.getCentreX() + 4.0f).withHeight (14.0f).withWidth (30.0f), juce::Justification::centredLeft, false);

    const juce::String state = drive < 0.001f ? "CLEAN" : (drive < 0.3f ? "WARM" : (drive < 0.6f ? "BREAKING UP" : (drive < 0.8f ? "CRUNCH" : "GROWL")));
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.setColour (drive < 0.001f ? Palette::textDim : Palette::accent);
    g.drawText (state, juce::Rectangle<float> (area.getX(), plot.getBottom() + 4.0f, area.getWidth(), 18.0f), juce::Justification::centred, false);

    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (plot);

        const auto badge = juce::Rectangle<float> (230.0f, 58.0f).withCentre (plot.getCentre());
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
// The Easy Amp window
//==============================================================================
EasyAmpAudioProcessorEditor::EasyAmpAudioProcessorEditor (EasyAmpAudioProcessor& p)
    : EasyWindow (p, "EASY AMP", "Valve warmth to overdriven growl - hover over anything for help", gettingStartedHint),
      ampProcessor (p),
      qualitySwitch (*p.parameters.getParameter (AmpIDs::quality), { "AUTO", "OFF", "4x", "8x" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);

        if (i == toneKnob)
            knob.slider.getProperties().set ("fillFromCentre", true);   // the middle is "no change"
        if (i == mixKnob)
            knob.slider.getProperties().set ("fillFromEnd", true);      // 100% (all amp) at the right-hand end

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
void EasyAmpAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");

    // The ends of DRIVE's turn
    const auto driveBounds = knobs[driveKnob].slider.getBounds();
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText ("CLEAN", juce::Rectangle<int> (driveBounds.getX() - 10, driveBounds.getBottom() - 46, 70, 16), juce::Justification::centredLeft, false);
    g.drawText ("OVERDRIVE", juce::Rectangle<int> (driveBounds.getRight() - 80, driveBounds.getBottom() - 46, 90, 16), juce::Justification::centredRight, false);

    // What the level matching is doing
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.setColour (showingMatch ? Palette::meterLow : Palette::textDim);
    g.drawText (showingMatch ? "AUTO-GAIN " + juce::String (shownMatchDb >= 0.05f ? "+" : "") + juce::String (shownMatchDb, 1) + " dB"
                             : juce::String ("AUTO-GAIN OFF (fixed level correction)"),
                matchReadout, juce::Justification::centred, false);
}

void EasyAmpAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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

    // Right: TONE (TONE, TIGHT) above OUTPUT (MIX)
    auto right = area.removeFromRight (300);
    area.removeFromRight (gap);
    const int panelHeight = (right.getHeight() - gap) / 2;
    groupPanels[1] = right.removeFromTop (panelHeight);
    right.removeFromTop (gap);
    groupPanels[2] = right;

    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        const int columnWidth = inside.getWidth() / 2;
        const int knobSize = juce::jmin (columnWidth, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[toneKnob], inside.removeFromLeft (columnWidth), knobSize);
        placeKnob (knobs[tightKnob], inside, knobSize);
    }
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        const int knobSize = juce::jmin (inside.getWidth() / 2, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[mixKnob], inside, knobSize);
    }

    // The middle: the curve
    displayPanel = area;
    auto inside = displayPanel.reduced (14, 12).withTrimmedTop (24);
    matchReadout = inside.removeFromBottom (18);
    inside.removeFromBottom (6);
    display.setBounds (inside);
}

//==============================================================================
void EasyAmpAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = ampProcessor.getCurrentSettings();

    display.setDrive (settings.drive);
    display.setInputPeak (ampProcessor.takeInputPeak());
    display.setBypassed (settings.bypassed);

    const float rounded = std::round (ampProcessor.getMatchGainDb() * 10.0f) / 10.0f;
    if (settings.autoGain != showingMatch || std::abs (rounded - shownMatchDb) > 0.01f)
    {
        showingMatch = settings.autoGain;
        shownMatchDb = rounded;
        repaint (matchReadout);
    }
}

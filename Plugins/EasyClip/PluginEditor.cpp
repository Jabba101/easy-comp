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

    const std::array<KnobInfo, EasyClipAudioProcessorEditor::numKnobs> knobInfo
    {{
        { ClipIDs::push, "PUSH",
          "Turns the sound up into the clipper. The more you push, the more of each peak gets shaved off and the louder it "
          "sounds. Watch SHAVED OFF: 1-3 dB on the loudest hits is usually unnoticeable; much more and you'll start to hear it." },

        { ClipIDs::ceiling, "CEILING",
          "The brick wall: nothing ever comes out louder than this. -1 dB is a safe choice for a finished song (it leaves room "
          "for the conversion streaming services do). On a single track, it sets how loud the loudest peaks can be." },

        { ClipIDs::softness, "SOFTNESS",
          "0% is a true brick wall: everything up to the ceiling is untouched and the peaks are cut flat - the cleanest and "
          "loudest. Turn it up to round the peaks off before they reach the ceiling: warmer and smoother, a little less loud." },

        { ClipIDs::protectLows, "PROTECT LOWS",
          "Below this frequency nothing is clipped: the low boom of a kick or a bass note stays clean, and only the clicks and "
          "peaks on top get shaved (a little harder, to make room). Set it just above the lowest note: 80-120 Hz on a kick, "
          "drum bus or full mix. Off = everything is clipped." },

        { ClipIDs::top, "TOP",
          "Lowers the ceiling for the upper half of the wave only. Clipping the two halves differently makes the clip "
          "lopsided, which adds grit and warmth (even harmonics). 0 = at the ceiling." },

        { ClipIDs::bottom, "BOTTOM",
          "Lowers the ceiling for the lower half of the wave only. With LINK on, TOP and BOTTOM move together and keep the gap "
          "between them. 0 = at the ceiling." },

        { ClipIDs::bias, "BIAS",
          "Makes the rounding lopsided: one half of the wave is rounded earlier and harder than the other. A warmer, "
          "valve-like edge. The middle changes nothing. (It adds a little rounding of its own, so it works at any SOFTNESS.)" },

        { ClipIDs::blend, "BLEND",
          "Mixes your original sound back in (parallel clipping). 100% = all clipped. Lower it to keep the original punch "
          "and add the clipped copy's loudness and energy underneath. The CEILING still holds." },
    }};

    const std::array<const char*, 3> groupNames { "HOW HARD", "HOW IT SOUNDS", "SOUND DESIGN" };

    constexpr const char* linkHint =
        "TOP and BOTTOM move together while this is on, keeping the gap between them: set each one first, then press LINK. "
        "When either reaches the end of its range, both stop. Switch it off to set them separately.";

    constexpr const char* initHint =
        "Puts the ADVANCED settings (TOP, BOTTOM, LINK, BIAS and BLEND) back to neutral. PUSH, CEILING, SOFTNESS and "
        "PROTECT LOWS stay as they are.";

    constexpr const char* initAllHint =
        "Puts every knob back to its default (the same as the 'Default settings' preset): nothing is clipped until you turn "
        "PUSH up. BYPASS, OVERSAMPLING and SIMPLE / ADVANCED stay as they are.";

    constexpr const char* viewHint =
        "SIMPLE shows the four main knobs. ADVANCED adds SOUND DESIGN: TOP, BOTTOM, BIAS and BLEND. Hidden settings keep "
        "working; the lamp stays lit while any ADVANCED setting isn't neutral, so nothing hidden can surprise you.";

    constexpr const char* qualityHint =
        "Clipping creates new high harmonics; at the normal sample rate the highest ones fold back down as harsh, out-of-tune "
        "tones. AUTO works at 4x while you mix and 8x when your DAW exports the song - the best of both. OFF uses the least "
        "computer power but sounds harsher. The delay your DAW lines up for stays the same whichever you choose.";

    constexpr const char* displayHint =
        "The last couple of seconds: grey = how loud it would have been after PUSH, amber = what comes out, red = what was "
        "shaved off. The dashed line is the CEILING - nothing goes above it.";

    constexpr const char* meterHint =
        "How much the loudest moment was shaved off, in dB. Up to about 3 dB (green) is usually unnoticeable. If it's "
        "regularly more, turn PUSH down - or the track down earlier in the chain - unless you want to hear the clipping.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or play the loudest part of your song and turn PUSH up until SHAVED OFF shows 1-3 dB on the loudest "
        "hits. Hover over anything for an explanation. Double-click a knob to reset it, or its number to type a value.";

    constexpr float meterFallPerUpdateDb = 0.4f;   // about 12 dB per second at 30 updates a second
}

//==============================================================================
// SHAVED OFF meter
//==============================================================================
void ShavedMeter::setShaved (float shavedDb)
{
    const float latest = juce::jlimit (0.0f, maxDb, shavedDb);
    const float nextDb = latest >= shownDb ? latest : juce::jmax (latest, shownDb - meterFallPerUpdateDb);

    if (std::abs (nextDb - shownDb) > 0.01f)
    {
        shownDb = nextDb;
        repaint();
    }
}

void ShavedMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    auto readout = area.removeFromBottom (24.0f);
    area.removeFromBottom (8.0f);

    const auto colourFor = [] (float db) { return db < 3.0f ? Palette::meterLow : (db < 6.0f ? Palette::meterMid : Palette::meterHigh); };

    const bool active = shownDb > 0.05f;
    g.setColour (active ? colourFor (shownDb) : Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (active ? "-" + juce::String (shownDb, 1) + " dB" : juce::String ("0.0 dB"), readout, juce::Justification::centred, false);

    area.reduce (0.0f, 6.0f);
    auto scale = area.removeFromLeft (area.getWidth() * 0.45f);
    const auto leds = area.withSizeKeepingCentre (juce::jmin (26.0f, area.getWidth()), area.getHeight());

    constexpr int numLeds = 16;   // 0.75 dB each
    constexpr float dbPerLed = maxDb / (float) numLeds;
    const float ledHeight = leds.getHeight() / (float) numLeds;

    for (int i = 0; i < numLeds; ++i)
    {
        const auto led = juce::Rectangle<float> (leds.getX(), leds.getY() + (float) i * ledHeight, leds.getWidth(), ledHeight).reduced (0.0f, 1.5f);
        const float startDb = (float) i * dbPerLed;
        g.setColour (shownDb > startDb + 0.1f ? colourFor (startDb) : Palette::meterOff);
        g.fillRoundedRectangle (led, 2.0f);
    }

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = 0; db <= (int) maxDb; db += 3)
    {
        const float y = leds.getY() + (float) db / maxDb * leds.getHeight();
        g.drawText (juce::String (db), scale.withY (y - 7.0f).withHeight (14.0f).withTrimmedRight (8.0f), juce::Justification::centredRight, false);
    }
}

//==============================================================================
// "What it's doing" display
//==============================================================================
ClipDisplay::ClipDisplay()
    : history ((size_t) numMoments)
{
}

void ClipDisplay::addMoments (const EasyClipAudioProcessor::Moment* newMoments, int count)
{
    if (count <= 0)
        return;

    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = newMoments[i];
        next = (next + 1) % numMoments;
    }

    repaint();
}

void ClipDisplay::setCeiling (float newCeilingDb)
{
    if (std::abs (newCeilingDb - ceilingDb) > 0.01f)
    {
        ceilingDb = newCeilingDb;
        repaint();
    }
}

void ClipDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

juce::Rectangle<float> ClipDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (20.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

void ClipDisplay::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();
    const float bottomDb = topDb - rangeDb;
    const auto yFor = [&] (float db) { return plot.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / rangeDb * plot.getHeight(); };
    const auto xFor = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };   // age 0 = now

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot.expanded (1.0f), 6.0f);

    // Grid and scale, every 6 dB from +12 to -12 (0 dB, the loudest a recording
    // can be before it clips, a little brighter)
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = 12; db >= -12; db -= 6)
    {
        const float y = yFor ((float) db);
        g.setColour (db == 0 ? Palette::textDim.withAlpha (0.55f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    g.drawText ("2 seconds ago", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 4.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 4.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    // The sound: pushed as a grey shape, out as an amber line, shaved in red
    juce::Path pushed, out;
    pushed.startNewSubPath (plot.getBottomRight());

    for (int age = 0; age < numMoments; ++age)
    {
        const auto& moment = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xFor (age);
        pushed.lineTo (x, yFor (moment.pushedDb));

        if (age == 0)
            out.startNewSubPath (x, yFor (moment.outDb));
        else
            out.lineTo (x, yFor (moment.outDb));
    }

    pushed.lineTo (plot.getBottomLeft());
    pushed.closeSubPath();

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Palette::textDim.withAlpha (0.35f));
        g.fillPath (pushed);

        const float sliver = juce::jmax (3.0f, plot.getWidth() / (float) (numMoments - 1) + 0.5f);

        for (int age = 0; age < numMoments; ++age)
        {
            const auto& moment = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
            const float yPushed = yFor (moment.pushedDb), yOut = yFor (moment.outDb);

            if (yOut - yPushed < 0.5f)
                continue;

            g.setColour (Palette::meterHigh.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (xFor (age) - sliver * 0.5f, yPushed, sliver, yOut - yPushed));
        }

        g.setColour (Palette::accent);
        g.strokePath (out, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The ceiling
        const float yCeiling = yFor (ceilingDb);
        juce::Path line;
        line.startNewSubPath (plot.getX(), yCeiling);
        line.lineTo (plot.getRight(), yCeiling);
        juce::Path dashed;
        const float dashes[] = { 6.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Palette::textBright.withAlpha (0.8f));
        g.fillPath (dashed);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("CEILING " + juce::String (ceilingDb, 1) + " dB",
                    juce::Rectangle<float> (plot.getRight() - 150.0f, yCeiling - 16.0f, 144.0f, 14.0f), juce::Justification::centredRight, false);
    }

    // A small key
    auto key = plot.reduced (10.0f).removeFromTop (16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    const auto keyItem = [&] (juce::Colour colour, float swatchHeight, const juce::String& text, float textWidth)
    {
        g.setColour (colour);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, swatchHeight));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (text, key.removeFromLeft (textWidth), juce::Justification::centredLeft, false);
        key.removeFromLeft (10.0f);
    };

    keyItem (Palette::textDim.withAlpha (0.6f), 10.0f, "pushed", 46.0f);
    keyItem (Palette::accent, 2.5f, "comes out", 64.0f);
    keyItem (Palette::meterHigh.withAlpha (0.8f), 10.0f, "shaved off", 66.0f);

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
// The Easy Clip window
//==============================================================================
EasyClipAudioProcessorEditor::EasyClipAudioProcessorEditor (EasyClipAudioProcessor& p)
    : EasyWindow (p, "EASY CLIP", "Shave the peaks so it can be louder - hover over anything for help", gettingStartedHint),
      clipProcessor (p),
      viewSwitch (*p.parameters.getParameter (ClipIDs::view), { "SIMPLE", "ADVANCED" }, Palette::accent),
      qualitySwitch (*p.parameters.getParameter (ClipIDs::quality), { "AUTO", "OFF", "4x", "8x" }, Palette::accent),
      incoming ((size_t) ClipDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);

        if (i == biasKnob)
            knob.slider.getProperties().set ("fillFromCentre", true);   // the middle is "no change"
        if (i == topKnob || i == bottomKnob || i == blendKnob)
            knob.slider.getProperties().set ("fillFromEnd", true);      // neutral at the right-hand end

        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this, i]
        {
            knobMoved();

            if (i == topKnob)     sideMoved (0, 1);
            if (i == bottomKnob)  sideMoved (1, 0);
        };

        addHint (knob.slider, knobInfo[i].name, knobInfo[i].hint);
        addHint (knob.label, knobInfo[i].name, knobInfo[i].hint);
    }

    lastSide = { (float) knobs[topKnob].slider.getValue(), (float) knobs[bottomKnob].slider.getValue() };

    linkAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (easyProcessor.parameters, ClipIDs::link, linkButton);
    linkButton.onClick = [this] { knobMoved(); };
    addAndMakeVisible (linkButton);
    addHint (linkButton, "LINK", linkHint);

    initButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initButton.onClick = [this] { clipProcessor.resetAdvanced(); };
    addAndMakeVisible (initButton);
    addHint (initButton, "INIT (ADVANCED)", initHint);

    // INIT for the whole plugin, in the header: every knob back to its
    // default (the same as the "Default settings" preset)
    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this]
    {
        presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front());   // "Default settings"
    };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    viewSwitch.setHorizontal (true);
    qualitySwitch.setHorizontal (true);
    viewSwitch.onUserChange = [this]
    {
        showingAdvanced = easyProcessor.parameters.getRawParameterValue (ClipIDs::view)->load() > 0.5f;
        resized();
        repaint();
    };
    addAndMakeVisible (viewSwitch);
    addHint (viewSwitch, "SIMPLE / ADVANCED", viewHint);

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

    addAndMakeVisible (meter);
    addHint (meter, "SHAVED OFF", meterHint);

    showingAdvanced = easyProcessor.parameters.getRawParameterValue (ClipIDs::view)->load() > 0.5f;

    finishSetup (1100, 640);

    // Double-click a number to type a value (a single click or a drag on it
    // moves the knob, like everywhere else on the knob).
    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

//==============================================================================
void EasyClipAudioProcessorEditor::sideMoved (int moved, int other)
{
    auto& movedSlider = knobs[(size_t) (topKnob + moved)].slider;
    auto& otherSlider = knobs[(size_t) (topKnob + other)].slider;
    const float now = (float) movedSlider.getValue();

    // Only your own moves are linked (a preset or the DAW sets each one itself).
    const bool yours = linkEverything || movedSlider.isMouseOverOrDragging (true) || movedSlider.hasKeyboardFocus (true);

    if (! movingPartner && yours && linkButton.getToggleState())
    {
        const float wanted = now - lastSide[(size_t) moved];
        const float partner = (float) otherSlider.getValue();
        const auto& range = movedSlider.getNormalisableRange();
        const float allowed = juce::jlimit ((float) range.start - partner, (float) range.end - partner, wanted);

        const juce::ScopedValueSetter<bool> guard (movingPartner, true);

        if (std::abs (allowed - wanted) > 1.0e-4f)   // the partner hit an end: this one stops there too
            movedSlider.setValue (lastSide[(size_t) moved] + allowed, juce::sendNotificationSync);

        otherSlider.setValue (partner + allowed, juce::sendNotificationSync);
    }

    lastSide[(size_t) moved] = (float) movedSlider.getValue();
    lastSide[(size_t) other] = (float) otherSlider.getValue();
}

//==============================================================================
void EasyClipAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    drawPanel (g, groupPanels[0], "1", groupNames[0]);
    drawPanel (g, groupPanels[1], "2", groupNames[1]);

    if (showingAdvanced)
    {
        drawPanel (g, groupPanels[2], "3", groupNames[2]);

        // TOP, LINK and BOTTOM work as a unit: a box round them says so
        g.setColour (Palette::panelEdge.brighter (0.15f));
        g.drawRoundedRectangle (linkGroup.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
    drawPanel (g, meterPanel, {}, "SHAVED OFF");

    // The lamp: lit while any ADVANCED setting isn't neutral
    const auto lamp = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) lampArea.getX() + 6.0f, (float) lampArea.getCentreY() });
    g.setColour (advancedLampLit ? Palette::accent : Palette::meterOff);
    g.fillEllipse (lamp);
    if (advancedLampLit)
    {
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.drawEllipse (lamp.expanded (3.0f), 2.0f);
    }

    g.setColour (advancedLampLit ? Palette::textBright : Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.drawText (advancedLampLit ? "ADVANCED SETTINGS IN USE" : "ADVANCED SETTINGS NEUTRAL",
                lampArea.withTrimmedLeft (20), juce::Justification::centredLeft, false);
}

void EasyClipAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // The header: INIT small, right beside the preset box (where the strip
    // has it, next to Pr), and BYPASS moved well clear of it so a quick
    // compare can't hit INIT by accident.
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    bypassButton.setBounds (bypassButton.getBounds().withX (initAllButton.getX() - 40 - bypassButton.getWidth()));

    // The top row: SIMPLE / ADVANCED and its lamp on the left, OVERSAMPLING on the right
    auto row = area.removeFromTop (32);
    viewSwitch.setBounds (row.removeFromLeft (240));
    row.removeFromLeft (16);
    lampArea = row.removeFromLeft (260);
    qualitySwitch.setBounds (row.removeFromRight (300));
    row.removeFromRight (10);
    qualityLabel.setBounds (row.removeFromRight (120));
    area.removeFromTop (gap);

    // Left: HOW HARD above HOW IT SOUNDS
    auto knobColumn = area.removeFromLeft (300);
    area.removeFromLeft (gap);
    const int panelHeight = (knobColumn.getHeight() - gap) / 2;

    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;

    const auto placeKnob = [&] (Knob& knob, juce::Rectangle<int> column, int knobSize)
    {
        auto c = column.withSizeKeepingCentre (column.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
        knob.label.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (spacing);
        knob.slider.setBounds (c);
    };

    for (int group = 0; group < 2; ++group)
    {
        groupPanels[(size_t) group] = knobColumn.removeFromTop (panelHeight);
        knobColumn.removeFromTop (gap);

        auto inside = groupPanels[(size_t) group].reduced (10, 12).withTrimmedTop (26);
        const int columnWidth = inside.getWidth() / 2;
        const int knobSize = juce::jmin (columnWidth, inside.getHeight() - nameHeight - spacing - valueBoxHeight);

        for (int k = 0; k < 2; ++k)
            placeKnob (knobs[(size_t) (group * 2 + k)], inside.removeFromLeft (columnWidth), knobSize);
    }

    // Right: the display and the meter; ADVANCED puts SOUND DESIGN underneath
    for (int i = topKnob; i <= blendKnob; ++i)
    {
        knobs[(size_t) i].slider.setVisible (showingAdvanced);
        knobs[(size_t) i].label.setVisible (showingAdvanced);
    }
    linkButton.setVisible (showingAdvanced);
    initButton.setVisible (showingAdvanced);

    if (showingAdvanced)
    {
        groupPanels[2] = area.removeFromBottom (panelHeight);
        area.removeFromBottom (gap);

        auto inside = groupPanels[2].reduced (12, 12).withTrimmedTop (26);
        initButton.setBounds (inside.removeFromRight (70).withSizeKeepingCentre (64, 30));
        inside.removeFromRight (10);

        const int knobSize = juce::jmin (100, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        const int columnWidth = 110;

        // TOP - LINK - BOTTOM together, then BIAS and BLEND
        linkGroup = inside.removeFromLeft (columnWidth * 2 + 84).withTrimmedTop (-4).withTrimmedBottom (-4);
        auto group = linkGroup.reduced (4, 0);
        placeKnob (knobs[topKnob], group.removeFromLeft (columnWidth), knobSize);
        linkButton.setBounds (group.removeFromLeft (76).withSizeKeepingCentre (70, 30));
        placeKnob (knobs[bottomKnob], group.removeFromLeft (columnWidth), knobSize);

        inside.removeFromLeft (gap);
        const int restWidth = inside.getWidth() / 2;
        placeKnob (knobs[biasKnob], inside.removeFromLeft (restWidth), knobSize);
        placeKnob (knobs[blendKnob], inside, knobSize);
    }

    meterPanel = area.removeFromRight (140);
    meter.setBounds (meterPanel.reduced (14, 12).withTrimmedTop (24));
    area.removeFromRight (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));
}

//==============================================================================
void EasyClipAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = clipProcessor.getCurrentSettings();

    meter.setShaved (clipProcessor.takeShavedForMeter());

    const int count = clipProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setCeiling (settings.ceilingDb);
    display.setBypassed (settings.bypassed);

    // The view can also change from a saved project or the DAW
    const bool advanced = easyProcessor.parameters.getRawParameterValue (ClipIDs::view)->load() > 0.5f;
    if (advanced != showingAdvanced)
    {
        showingAdvanced = advanced;
        resized();
        repaint();
    }

    const bool lit = clipProcessor.isAdvancedInUse();
    if (lit != advancedLampLit)
    {
        advancedLampLit = lit;
        repaint (lampArea.expanded (6));
    }

    // (Keep LINK's memory of TOP and BOTTOM in step with presets and the DAW.)
    if (! knobs[topKnob].slider.isMouseOverOrDragging (true) && ! knobs[bottomKnob].slider.isMouseOverOrDragging (true))
        lastSide = { (float) knobs[topKnob].slider.getValue(), (float) knobs[bottomKnob].slider.getValue() };
}

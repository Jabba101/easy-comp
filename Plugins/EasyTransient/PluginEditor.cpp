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

    const std::array<KnobInfo, 4> knobInfo
    {{
        { TransientIDs::punch, "PUNCH",
          "Turn up for snappier, more defined hits and notes - the start of each sound gets louder. Turn down to soften "
          "harsh attacks (spiky percussion, an over-bright strum). The middle changes nothing. More punch means taller peaks." },

        { TransientIDs::sustain, "SUSTAIN",
          "Turn up for more ring, room and body after each hit - the tail gets louder. Turn down for a tighter, drier sound: "
          "shortens boomy kicks, ringing toms and roomy recordings. Turning it up also raises background noise between hits." },

        { TransientIDs::output, "OUTPUT",
          "Turns the result up or down. Use it to match the volume with BYPASS on and off, so you judge the shape, not the "
          "loudness - or let AUTO-GAIN do it for you." },

        { TransientIDs::mix, "MIX",
          "Blends your original sound with the shaped one. 100% = fully shaped. Lower it for a subtler version of the "
          "same effect." },
    }};

    const std::array<const char*, 2> groupNames { "SHAPE", "OUTPUT" };

    constexpr const char* outputAutoHint =
        "AUTO-GAIN is on, so it's setting the volume for you - the box shows how much it's adding or taking away right now. "
        "Switch AUTO-GAIN off to set OUTPUT by hand again.";

    constexpr const char* displayHint =
        "The last couple of seconds of your sound: grey = the original, amber = after shaping. Green shows where it came out "
        "louder, red where quieter. The scale moves to fit your track, so the peaks always stay in view.";

    constexpr const char* meterHint =
        "How much it's turning the sound up (green, above the line) and down (red, below) right now. PUNCH works on the "
        "first few milliseconds of each hit; SUSTAIN on the tail after it.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or play your track and turn PUNCH and SUSTAIN either way from the middle (the middle changes nothing). "
        "Hover over anything for an explanation. Double-click a knob to reset it.";

    constexpr float meterFallPerUpdateDb = 0.75f;   // about 22 dB per second at 30 updates a second
}

//==============================================================================
// Gain change meter
//==============================================================================
void GainChangeMeter::setChange (float boostDb, float cutDb)
{
    // Jump up instantly so short hits show, then fall back gently so your eye can follow it.
    const auto fallBack = [] (float shown, float latest)
    {
        return latest >= shown ? latest : juce::jmax (latest, shown - meterFallPerUpdateDb);
    };

    const float nextBoost = fallBack (shownBoost, boostDb);
    const float nextCut   = fallBack (shownCut, cutDb);

    if (std::abs (nextBoost - shownBoost) > 0.01f || std::abs (nextCut - shownCut) > 0.01f)
    {
        shownBoost = nextBoost;
        shownCut   = nextCut;
        repaint();
    }
}

void GainChangeMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Readouts: boost at the top, cut at the bottom
    g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    const auto topReadout = area.removeFromTop (22.0f);
    const auto bottomReadout = area.removeFromBottom (22.0f);
    const auto readout = [] (float db, const char* sign) { return db > 0.05f ? sign + juce::String (db, 1) + " dB" : juce::String ("0.0 dB"); };
    g.setColour (shownBoost > 0.05f ? Palette::meterLow : Palette::textDim);
    g.drawText (readout (shownBoost, "+"), topReadout, juce::Justification::centred, false);
    g.setColour (shownCut > 0.05f ? Palette::meterHigh : Palette::textDim);
    g.drawText (readout (shownCut, "-"), bottomReadout, juce::Justification::centred, false);
    area.reduce (0.0f, 8.0f);

    // Scale on the left, LEDs on the right: 12 dB up, 12 dB down, 1.5 dB each
    auto scale = area.removeFromLeft (area.getWidth() * 0.45f);
    const auto leds = area.withSizeKeepingCentre (juce::jmin (26.0f, area.getWidth()), area.getHeight());
    constexpr int ledsEachWay = 8;
    constexpr float dbPerLed = 1.5f;
    const float ledHeight = leds.getHeight() / (float) (ledsEachWay * 2);
    const float middle = leds.getCentreY();

    for (int i = 0; i < ledsEachWay; ++i)
    {
        const float startDb = (float) i * dbPerLed;
        const auto up   = juce::Rectangle<float> (leds.getX(), middle - (float) (i + 1) * ledHeight, leds.getWidth(), ledHeight).reduced (0.0f, 1.5f);
        const auto down = juce::Rectangle<float> (leds.getX(), middle + (float) i * ledHeight, leds.getWidth(), ledHeight).reduced (0.0f, 1.5f);

        g.setColour (shownBoost > startDb + 0.25f ? Palette::meterLow : Palette::meterOff);
        g.fillRoundedRectangle (up, 2.0f);
        g.setColour (shownCut > startDb + 0.25f ? Palette::meterHigh : Palette::meterOff);
        g.fillRoundedRectangle (down, 2.0f);
    }

    g.setColour (Palette::textDim);
    g.drawHorizontalLine (juce::roundToInt (middle), leds.getX() - 4.0f, leds.getRight() + 4.0f);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = -12; db <= 12; db += 6)
    {
        const float y = middle - (float) db / 12.0f * (leds.getHeight() * 0.5f);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), scale.withY (y - 7.0f).withHeight (14.0f).withTrimmedRight (8.0f),
                    juce::Justification::centredRight, false);
    }
}

//==============================================================================
// "What it's doing" display
//==============================================================================
MomentDisplay::MomentDisplay()
    : history ((size_t) numMoments)
{
}

void MomentDisplay::addMoments (const EasyTransientAudioProcessor::Moment* newMoments, int count)
{
    if (count <= 0)
        return;

    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = newMoments[i];
        next = (next + 1) % numMoments;
    }

    fitScale();
    repaint();
}

void MomentDisplay::fitScale()
{
    // Keep the loudest moment of the last couple of seconds 12-21 dB below the
    // top edge - about a quarter of the way down - whatever the track's level.
    constexpr float headroomDb = 12.0f, stepDb = 6.0f;

    float loudest = TransientShaper::silenceDb;
    for (auto& moment : history)
        loudest = std::max ({ loudest, moment.inDb, moment.outDb });

    if (loudest < -90.0f)
        return;   // silence: leave the scale where it was

    // Make room at once when it gets louder; zoom back in only once it's clearly quieter
    const float needed = loudest + headroomDb;
    if (! hasFitted || needed > targetTopDb || needed < targetTopDb - stepDb - 3.0f)
        targetTopDb = juce::jlimit (-24.0f, 24.0f, std::ceil (needed / stepDb) * stepDb);

    // Glide there (quickly when making room, gently when zooming in), or jump the first time
    topDb = hasFitted ? topDb + juce::jlimit (-1.0f, 3.0f, targetTopDb - topDb) : targetTopDb;
    hasFitted = true;
}

void MomentDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

juce::Rectangle<float> MomentDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (20.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

void MomentDisplay::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();
    const float bottomDb = topDb - rangeDb;
    const auto yFor = [&] (float db) { return plot.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / rangeDb * plot.getHeight(); };
    const auto xFor = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };   // age 0 = now

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot.expanded (1.0f), 6.0f);

    // Grid and scale, every 12 dB. The 0 dB line - the loudest a recording can
    // be before it clips - is a little brighter.
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = 12 * (int) std::floor (topDb / 12.0f); (float) db > bottomDb; db -= 12)
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

    // The sound: original as a grey shape, shaped as an amber line
    juce::Path original, shaped;
    original.startNewSubPath (plot.getBottomRight());

    for (int age = 0; age < numMoments; ++age)
    {
        const auto& moment = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xFor (age);
        original.lineTo (x, yFor (moment.inDb));

        if (age == 0)
            shaped.startNewSubPath (x, yFor (moment.outDb));
        else
            shaped.lineTo (x, yFor (moment.outDb));
    }

    original.lineTo (plot.getBottomLeft());
    original.closeSubPath();

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Palette::textDim.withAlpha (0.35f));
        g.fillPath (original);

        g.setColour (Palette::accent);
        g.strokePath (shaped, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The difference on top, coloured: green where it came out louder, red
        // where quieter. (At least 3 pixels wide, so the brief lift PUNCH gives
        // the very start of each hit still shows.)
        const float sliver = juce::jmax (3.0f, plot.getWidth() / (float) (numMoments - 1) + 0.5f);

        for (int age = 0; age < numMoments; ++age)
        {
            const auto& moment = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
            const float yIn = yFor (moment.inDb), yOut = yFor (moment.outDb);

            if (std::abs (yIn - yOut) < 0.5f)
                continue;

            g.setColour ((yOut < yIn ? Palette::meterLow : Palette::meterHigh).withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float> (xFor (age) - sliver * 0.5f, juce::jmin (yIn, yOut), sliver, std::abs (yIn - yOut)));
        }
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

    keyItem (Palette::textDim.withAlpha (0.6f), 10.0f, "original", 52.0f);
    keyItem (Palette::accent, 2.5f, "after shaping", 84.0f);
    keyItem (Palette::meterLow.withAlpha (0.8f), 10.0f, "louder", 42.0f);
    keyItem (Palette::meterHigh.withAlpha (0.8f), 10.0f, "quieter", 48.0f);

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
// The Easy Trans window
//==============================================================================
EasyTransientAudioProcessorEditor::EasyTransientAudioProcessorEditor (EasyTransientAudioProcessor& p)
    : EasyWindow (p, "EASY TRANS", "Shape the punch and tail of every hit - hover over anything for help", gettingStartedHint),
      shaperProcessor (p),
      incoming ((size_t) MomentDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);

        if (i < 2)
            knob.slider.getProperties().set ("fillFromCentre", true);   // PUNCH and SUSTAIN: the middle is "no change"

        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this] { knobMoved(); };

        const auto hint = [this, i]
        {
            if (i == outputKnob && showingAutoGain)
                return std::make_pair (juce::String (knobInfo[i].name), juce::String (outputAutoHint));

            return std::make_pair (juce::String (knobInfo[i].name), juce::String (knobInfo[i].hint));
        };

        addHint (knob.slider, hint);
        addHint (knob.label, hint);
    }

    // While AUTO-GAIN is on, the OUTPUT box shows how much it's adding (or taking away).
    auto& outputSlider = knobs[outputKnob].slider;
    outputSlider.textFromValueFunction = [this, manualText = outputSlider.textFromValueFunction] (double value)
    {
        if (showingAutoGain)
            return "Auto " + juce::String (shownAutoGainDb >= 0.05f ? "+" : "") + juce::String (shownAutoGainDb, 1) + " dB";

        return manualText != nullptr ? manualText (value) : juce::String (value, 1);
    };

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    addAndMakeVisible (meter);
    addHint (meter, "GAIN CHANGE", meterHint);

    finishSetup (1100, 600);
}

//==============================================================================
void EasyTransientAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
    drawPanel (g, meterPanel, {}, "GAIN CHANGE");
}

void EasyTransientAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // Right-hand side: the meter, and the display beside it
    meterPanel = area.removeFromRight (140);
    meter.setBounds (meterPanel.reduced (14, 12).withTrimmedTop (24));
    area.removeFromRight (gap);

    auto knobColumn = area.removeFromLeft (250);
    area.removeFromLeft (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    // Left-hand side: SHAPE above OUTPUT
    const int panelHeight = (knobColumn.getHeight() - gap) / 2;

    for (size_t group = 0; group < groupPanels.size(); ++group)
    {
        groupPanels[group] = knobColumn.removeFromTop (panelHeight);
        knobColumn.removeFromTop (gap);

        auto inside = groupPanels[group].reduced (10, 12).withTrimmedTop (26);
        const int columnWidth = inside.getWidth() / 2;

        constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
        const int knobSize = juce::jmin (columnWidth, inside.getHeight() - nameHeight - spacing - valueBoxHeight);

        for (size_t k = 0; k < 2; ++k)
        {
            auto& knob = knobs[group * 2 + k];
            auto columnArea = inside.removeFromLeft (columnWidth)
                                    .withSizeKeepingCentre (columnWidth, nameHeight + spacing + knobSize + valueBoxHeight);

            knob.label.setBounds (columnArea.removeFromTop (nameHeight));
            columnArea.removeFromTop (spacing);
            knob.slider.setBounds (columnArea);
        }
    }
}

//==============================================================================
void EasyTransientAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = shaperProcessor.getCurrentSettings();
    const float autoGainDb = shaperProcessor.getAutoGainDb();

    meter.setChange (shaperProcessor.takeBoostForMeter(), shaperProcessor.takeCutForMeter());

    const int count = shaperProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setBypassed (settings.bypassed);

    // AUTO-GAIN takes over from OUTPUT: fade that knob and show what AUTO-GAIN is doing in its box.
    auto& outputSlider = knobs[outputKnob].slider;
    const float roundedDb = std::round (autoGainDb * 10.0f) / 10.0f;

    if (settings.autoGain != showingAutoGain)
    {
        showingAutoGain = settings.autoGain;
        shownAutoGainDb = roundedDb;
        outputSlider.setEnabled (! showingAutoGain);
        outputSlider.setColour (juce::Slider::textBoxTextColourId, showingAutoGain ? Palette::meterLow : Palette::textBright);
        outputSlider.updateText();
    }
    else if (showingAutoGain && std::abs (roundedDb - shownAutoGainDb) > 0.01f)
    {
        shownAutoGainDb = roundedDb;
        outputSlider.updateText();
    }
}

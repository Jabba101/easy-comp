#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    const std::array<KnobInfo, EasyLimiterAudioProcessorEditor::numKnobs> knobInfo
    {{
        { LimiterIDs::gain, "GAIN",
          "Turns the sound up into the ceiling. At 0 dB it only ever stops overs; each dB more is a dB louder, with the "
          "peaks held at the ceiling. Use MATCH to hear whether it's still clean." },

        { LimiterIDs::ceiling, "CEILING",
          "The most the output may ever reach, as true peak - the peaks between the samples too, the ones that clip a "
          "converter or a converted file even when the waveform looks fine. -0.3 dBTP by default." },

        { LimiterIDs::release, "RELEASE",
          "With RELEASE on SET: how quickly it lets go after turning down. Short (20-50 ms) keeps drums punchy but can "
          "distort the lows when pushed; long (300 ms+) is smooth and steady but less punchy." },
    }};

    const std::array<const char*, 4> groupNames { "HOW HARD", "CEILING", "RELEASE", "COMPARE" };

    constexpr const char* releaseHint =
        "AUTO: lets go quickly after a lone peak and more slowly while it's holding back a sustained passage - no pumping, "
        "no distortion. The best choice almost always. SET: the RELEASE time you choose.";
    constexpr const char* matchHint =
        "For comparing by ear: takes GAIN back off what you hear, so the limited sound is exactly as loud as the original. "
        "Click BYPASS on and off with MATCH on - if you can't tell them apart, it's clean. Remember to switch it off.";
    constexpr const char* meterHint = "How much it's turning the sound down right now (dB).";
    constexpr const char* peakHint =
        "The highest true peak that has come out since you last clicked here - it never passes the CEILING. Click to reset.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS and MATCH stay as they are.";
    constexpr const char* displayHint =
        "The last three seconds: grey = going in (after GAIN - it can go past the ceiling), amber = coming out (it never "
        "does), the dashed line = the CEILING, red from the top = how much it turned down.";
    constexpr const char* gettingStartedHint =
        "On its own it only stops overs (-0.3 dBTP). Turn up GAIN to push into it; use MATCH and BYPASS to check it's still "
        "clean. Hover over anything for help.";

    constexpr float topDb = 2.0f, bottomDb = -24.0f, reductionScale = 12.0f;
}

//==============================================================================
LimiterDisplay::LimiterDisplay() : history ((size_t) numMoments) {}

void LimiterDisplay::addMoments (const EasyLimiterAudioProcessor::Moment* m, int count)
{
    if (count <= 0) return;
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = m[i];
        next = (next + 1) % numMoments;
    }
    repaint();
}

void LimiterDisplay::setCeiling (float db)
{
    if (std::abs (db - ceilingDb) > 0.01f) { ceilingDb = db; repaint(); }
}

void LimiterDisplay::setBypassed (bool b)
{
    if (bypassed != b) { bypassed = b; repaint(); }
}

void LimiterDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    const auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    const auto xOf = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.drawText ("3 seconds ago", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    juce::Path in, out, outLine, cut;
    in.startNewSubPath (plot.getBottomRight());
    out.startNewSubPath (plot.getBottomRight());
    cut.startNewSubPath (plot.getTopRight());
    for (int age = 0; age < numMoments; ++age)
    {
        const auto& m = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xOf (age);
        in.lineTo (x, yOf (m.inDb));
        out.lineTo (x, yOf (m.outDb));
        if (age == 0) outLine.startNewSubPath (x, yOf (m.outDb)); else outLine.lineTo (x, yOf (m.outDb));
        cut.lineTo (x, plot.getY() + plot.getHeight() * 0.5f * juce::jlimit (0.0f, 1.0f, m.reductionDb / reductionScale));
    }
    in.lineTo (plot.getBottomLeft()); in.closeSubPath();
    out.lineTo (plot.getBottomLeft()); out.closeSubPath();
    cut.lineTo (plot.getTopLeft()); cut.closeSubPath();

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Palette::textDim.withAlpha (0.35f));
        g.fillPath (in);
        g.setColour (Palette::accent.withAlpha (0.18f));
        g.fillPath (out);
        g.setColour (Palette::accent);
        g.strokePath (outLine, juce::PathStrokeType (1.4f));
        g.setColour (Palette::meterHigh.withAlpha (0.5f));
        g.fillPath (cut);
    }

    // The ceiling
    {
        const float y = yOf (ceilingDb);
        juce::Path line;
        line.startNewSubPath (plot.getX(), y);
        line.lineTo (plot.getRight(), y);
        juce::Path dashed;
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Palette::textBright);
        g.fillPath (dashed);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("CEILING " + juce::String (ceilingDb, 1) + " dBTP", juce::Rectangle<float> (plot.getRight() - 200.0f, y + 3.0f, 196.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // Key (under the plot)
    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (10.0f);
    };
    item (Palette::textDim.withAlpha (0.6f), "in (after GAIN)", 92.0f);
    item (Palette::accent, "out", 30.0f);
    item (Palette::meterHigh.withAlpha (0.7f), "turned down", 90.0f);

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
EasyLimiterAudioProcessorEditor::EasyLimiterAudioProcessorEditor (EasyLimiterAudioProcessor& p)
    : EasyWindow (p, "EASY LIMITER", "A clean, transparent true-peak ceiling - hover over anything for help", gettingStartedHint),
      limiterProcessor (p),
      releaseSwitch (*p.parameters.getParameter (LimiterIDs::releaseMode), { "AUTO", "SET" }, Palette::accent),
      incoming ((size_t) LimiterDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 24);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (i == gainKnob ? 15.0f : 13.0f, juce::Font::bold)));
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

    addAndMakeVisible (releaseSwitch);
    releaseSwitch.onUserChange = [this] { knobMoved(); };
    addHint (releaseSwitch, "RELEASE", releaseHint);

    matchButton.setClickingTogglesState (true);
    matchAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (LimiterIDs::match), matchButton);
    addAndMakeVisible (matchButton);
    addHint (matchButton, "MATCH", matchHint);

    matchInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    matchInfo.setColour (juce::Label::textColourId, Palette::textDim);
    matchInfo.setJustificationType (juce::Justification::centred);
    matchInfo.setText ("Hear it at the original's loudness: then BYPASS on and off. Can't tell them apart? It's clean.", juce::dontSendNotification);
    addAndMakeVisible (matchInfo);
    addHint (matchInfo, "MATCH", matchHint);

    addAndMakeVisible (reductionMeter);
    addHint (reductionMeter, "TURNED DOWN", meterHint);

    peakReadout.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
    peakReadout.setColour (juce::Label::textColourId, Palette::textBright);
    peakReadout.setJustificationType (juce::Justification::centred);
    peakReadout.setInterceptsMouseClicks (false, false);   // (the window takes the click: see mouseDown)
    addAndMakeVisible (peakReadout);
    addHint (peakReadout, "HIGHEST PEAK OUT", peakHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyLimiterAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (peakReadout.getBounds().contains (e.getEventRelativeTo (this).getPosition()))
        limiterProcessor.resetHighestPeak();
}

void EasyLimiterAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.setColour (Palette::textDim);
    g.drawFittedText ("highest peak out\n(click to reset)", peakReadout.getBounds().translated (0, -34).withHeight (32), juce::Justification::centred, 2);
}

void EasyLimiterAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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
    const auto knobSizeFor = [&] (juce::Rectangle<int> column) { return juce::jmin (column.getWidth() - 30, column.getHeight() - nameHeight - spacing - valueBoxHeight); };

    auto left = area.removeFromLeft (290);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (290);
    area.removeFromRight (gap);
    displayPanel = area;
    {
        auto inside = displayPanel.reduced (12, 12).withTrimmedTop (24);
        auto side = inside.removeFromRight (64);
        inside.removeFromRight (10);
        display.setBounds (inside);
        reductionMeter.setBounds (side);
    }

    // Left: HOW HARD (GAIN), then CEILING
    const int leftTop = (left.getHeight() - gap) * 55 / 100;
    groupPanels[0] = left.removeFromTop (leftTop);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    {
        auto inside = groupPanels[0].reduced (10, 12).withTrimmedTop (26);
        placeKnob (knobs[gainKnob], inside, knobSizeFor (inside));
        auto c = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto readout = c.removeFromRight (120);
        placeKnob (knobs[ceilingKnob], c, knobSizeFor (c));
        peakReadout.setBounds (readout.withSizeKeepingCentre (readout.getWidth(), 30).translated (0, 18));
    }

    // Right: RELEASE (AUTO / SET and its time), then COMPARE (MATCH)
    groupPanels[2] = right.removeFromTop ((right.getHeight() - gap) * 55 / 100);
    right.removeFromTop (gap);
    groupPanels[3] = right;
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        auto switchArea = inside.removeFromLeft (96);
        releaseSwitch.setBounds (switchArea.withSizeKeepingCentre (90, 2 * 32 - 4));
        placeKnob (knobs[releaseKnob], inside, knobSizeFor (inside));

        auto compare = groupPanels[3].reduced (14, 12).withTrimmedTop (30);
        matchButton.setBounds (compare.removeFromTop (34).withSizeKeepingCentre (120, 34));
        compare.removeFromTop (6);
        matchInfo.setBounds (compare);
    }
}

void EasyLimiterAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = limiterProcessor.getCurrentSettings();
    const int count = limiterProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    float reduction = 0.0f;
    for (int i = 0; i < count; ++i) reduction = juce::jmax (reduction, incoming[(size_t) i].reductionDb);
    if (count > 0) reductionMeter.setGainReduction (reduction);
    display.setCeiling (settings.ceilingDb);
    display.setBypassed (settings.bypassed);

    const bool set = ! settings.autoRelease;
    knobs[releaseKnob].slider.setEnabled (set);
    knobs[releaseKnob].slider.setAlpha (set ? 1.0f : 0.4f);
    knobs[releaseKnob].label.setAlpha (set ? 1.0f : 0.4f);

    const float peak = limiterProcessor.getHighestPeakDb();
    if (std::abs (peak - shownPeak) > 0.01f)
    {
        shownPeak = peak;
        peakReadout.setText (peak <= -99.0f ? juce::String ("--") : juce::String (peak, 2) + " dBTP", juce::dontSendNotification);
    }
}

#include "PluginEditor.h"
#include "Parameters.h"

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

    const std::array<KnobInfo, EasyTremoloAudioProcessorEditor::numKnobs> knobInfo
    {{
        { TremoloIDs::rate, "RATE",
          "How fast it pulses, in pulses a second (Hz). 3-6 Hz is the classic amp tremolo; under 1 Hz a slow sway. With SYNC "
          "on TEMPO you pick a note length instead, and it locks to your song's beat." },

        { TremoloIDs::note, "RATE",
          "How long one pulse takes (down and back up), as a note length at your song's tempo - it locks to the beat. 1/8 = "
          "two pulses a beat; 1/16 = a fast chop; 1 bar = a slow sweep." },

        { TremoloIDs::depth, "DEPTH",
          "How far the volume dips (or how far the sound swings, on AUTO-PAN). 20-40% = gentle movement; 100% = right down "
          "to silence on every pulse (with SQUARE, a hard chop)." },
    }};

    const std::array<const char*, 4> groupNames { "SPEED", "HOW MUCH", "SHAPE", "MODE" };

    constexpr const char* syncHint =
        "OFF: set RATE in pulses a second. TEMPO: set it as a note length that follows your song's tempo and stays locked "
        "to the beat while the song plays.";
    constexpr const char* shapeHint =
        "SINE: smooth, like a vintage amp. TRIANGLE: a steadier rise and fall. SQUARE: chopped on and off (trance gate, "
        "stutter). PUMP: dips on every pulse and swells back - the pumping sidechain sound, without a sidechain.";
    constexpr const char* modeHint =
        "TREMOLO: the volume pulses. AUTO-PAN: the sound moves from side to side instead (needs a stereo track or a stereo "
        "output).";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS stays as it is.";
    constexpr const char* displayHint =
        "Two pulses of the movement: the volume (or the left and right sides, on AUTO-PAN), with the dot showing where it "
        "is right now. Never louder than your sound - tremolo only takes level away.";
    constexpr const char* gettingStartedHint =
        "Pick a preset, or set the speed (a note with SYNC on TEMPO) and turn DEPTH up. Try SQUARE for a chop, PUMP for "
        "pumping, AUTO-PAN for side to side. Double-click a knob to reset it, or its number to type a value.";
}

//==============================================================================
void MovementDisplay::setSettings (const Tremolo::Settings& s)
{
    if (s.shape != settings.shape || s.mode != settings.mode || std::abs (s.depth - settings.depth) > 1.0e-4f)
    {
        settings = s;
        repaint();
    }
}

void MovementDisplay::setPosition (float cyclePosition)
{
    if (std::abs (cyclePosition - position) > 0.002f)
    {
        position = cyclePosition;
        repaint();
    }
}

void MovementDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

void MovementDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    const auto plot = area.reduced (18.0f, 20.0f).withTrimmedLeft (22.0f).withTrimmedBottom (10.0f);
    const bool pan = settings.mode == Tremolo::Mode::autoPan;

    // Two cycles across; a line where each starts (on the beat, when synced)
    g.setColour (Palette::panelEdge);
    for (int c = 0; c <= 2; ++c)
        g.drawVerticalLine (juce::roundToInt (plot.getX() + plot.getWidth() * (float) c / 2.0f), plot.getY(), plot.getBottom());
    g.drawHorizontalLine (juce::roundToInt (plot.getY()), plot.getX(), plot.getRight());
    g.drawHorizontalLine (juce::roundToInt (plot.getBottom()), plot.getX(), plot.getRight());

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.setColour (Palette::textDim);
    g.drawText ("full", juce::Rectangle<float> (plot.getX() - 40.0f, plot.getY() - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);
    g.drawText ("silent", juce::Rectangle<float> (plot.getX() - 40.0f, plot.getBottom() - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight, false);

    const auto gainsAt = [&] (float p)
    {
        const float u = Tremolo::shapeAt (p, settings.shape);
        const float d = juce::jlimit (0.0f, 1.0f, settings.depth);
        if (! pan)
            return std::pair<float, float> { 1.0f - d * (1.0f - u), 1.0f - d * (1.0f - u) };
        const float swing = (2.0f * u - 1.0f) * d;
        return std::pair<float, float> { juce::jmin (1.0f, 1.0f - swing), juce::jmin (1.0f, 1.0f + swing) };
    };

    juce::Path left, right;
    constexpr int points = 400;
    for (int i = 0; i <= points; ++i)
    {
        const float x = (float) i / (float) points;
        const auto [gl, gr] = gainsAt (std::fmod (x * 2.0f, 1.0f));
        const juce::Point<float> pl (plot.getX() + plot.getWidth() * x, plot.getBottom() - plot.getHeight() * gl);
        const juce::Point<float> pr (plot.getX() + plot.getWidth() * x, plot.getBottom() - plot.getHeight() * gr);
        if (i == 0) { left.startNewSubPath (pl); right.startNewSubPath (pr); }
        else        { left.lineTo (pl); right.lineTo (pr); }
    }

    g.setColour (Palette::accent);
    g.strokePath (left, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (pan)
    {
        g.setColour (Palette::meterLow);
        g.strokePath (right, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.setColour (Palette::accent);
        g.drawText ("LEFT", plot.withHeight (16.0f).translated (0.0f, -18.0f), juce::Justification::centredLeft, false);
        g.setColour (Palette::meterLow);
        g.drawText ("RIGHT", plot.withHeight (16.0f).translated (50.0f, -18.0f), juce::Justification::centredLeft, false);
    }

    // Where it is right now (in the first cycle)
    const auto [gl, gr] = gainsAt (position);
    const float x = plot.getX() + plot.getWidth() * position * 0.5f;
    g.setColour (Palette::textBright);
    g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, plot.getBottom() - plot.getHeight() * gl }));
    if (pan)
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ x, plot.getBottom() - plot.getHeight() * gr }));

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
EasyTremoloAudioProcessorEditor::EasyTremoloAudioProcessorEditor (EasyTremoloAudioProcessor& p)
    : EasyWindow (p, "EASY TREMOLO", "Pulsing and side-to-side, in time - hover over anything for help", gettingStartedHint),
      tremoloProcessor (p),
      syncSwitch (*p.parameters.getParameter (TremoloIDs::sync), { "OFF", "TEMPO" }, Palette::accent),
      shapeSwitch (*p.parameters.getParameter (TremoloIDs::shape), { "SINE", "TRIANGLE", "SQUARE", "PUMP" }, Palette::accent),
      modeSwitch (*p.parameters.getParameter (TremoloIDs::mode), { "TREMOLO", "AUTO-PAN" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 24);
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
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
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    syncLabel.setText ("SYNC", juce::dontSendNotification);
    syncLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    syncLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (syncLabel);
    addHint (syncLabel, "SYNC", syncHint);

    for (auto [sw, name, hint] : { std::tuple<ChoiceSwitch*, const char*, const char*> { &syncSwitch, "SYNC", syncHint },
                                   { &shapeSwitch, "SHAPE", shapeHint }, { &modeSwitch, "MODE", modeHint } })
    {
        addAndMakeVisible (*sw);
        sw->onUserChange = [this] { knobMoved(); };
        addHint (*sw, name, hint);
    }

    rateInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    rateInfo.setColour (juce::Label::textColourId, Palette::textDim);
    rateInfo.setJustificationType (juce::Justification::centredTop);
    addAndMakeVisible (rateInfo);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    showSyncedKnob (juce::roundToInt (p.parameters.getRawParameterValue (TremoloIDs::sync)->load()) == 1);
    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyTremoloAudioProcessorEditor::showSyncedKnob (bool synced)
{
    showingSynced = synced;
    knobs[rateKnob].slider.setVisible (! synced);
    knobs[rateKnob].label.setVisible (! synced);
    knobs[noteKnob].slider.setVisible (synced);
    knobs[noteKnob].label.setVisible (synced);
}

juce::String EasyTremoloAudioProcessorEditor::describeRate (bool synced, int noteIndex, float rateHz, double bpm, bool tempoFromDAW)
{
    juce::String text;
    if (synced)
    {
        const auto& note = TremoloNotes::lengths[juce::jlimit (0, TremoloNotes::count - 1, noteIndex)];
        text << note.name << " at " << juce::roundToInt (bpm) << " BPM = " << juce::String (EasyTremoloAudioProcessor::rateForNote (noteIndex, bpm), 2) << " pulses a second";
    }
    else
    {
        text << juce::String (rateHz, 2) << " pulses a second (" << juce::String (60.0 * rateHz / juce::jmax (20.0, bpm), 1) << " a beat at "
             << juce::roundToInt (bpm) << " BPM)";
    }
    if (! tempoFromDAW)
        text << "\n(until your DAW sends its tempo)";
    return text;
}

//==============================================================================
void EasyTremoloAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
}

void EasyTremoloAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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

    auto left = area.removeFromLeft (320);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (240);
    area.removeFromRight (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    const int half = (left.getHeight() - gap) / 2;
    groupPanels[0] = left.removeFromTop (half);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    groupPanels[2] = right.removeFromTop (half + 40);
    right.removeFromTop (gap);
    groupPanels[3] = right;

    {   // 1 SPEED: RATE (or the note), SYNC beside it, what it comes to underneath
        auto inside = groupPanels[0].reduced (10, 12).withTrimmedTop (26);
        rateInfo.setBounds (inside.removeFromBottom (34));
        auto knobSpace = inside.removeFromLeft (inside.getWidth() * 3 / 5);
        const int size = juce::jmin (knobSpace.getWidth() - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[rateKnob], knobSpace, size);
        placeKnob (knobs[noteKnob], knobSpace, size);
        auto c = inside.withSizeKeepingCentre (inside.getWidth(), nameHeight + 6 + 2 * 32);
        syncLabel.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (6);
        syncSwitch.setBounds (c.withSizeKeepingCentre (juce::jmin (120, c.getWidth()), c.getHeight()));
    }
    {   // 2 HOW MUCH: DEPTH
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        placeKnob (knobs[depthKnob], inside, juce::jmin (170, inside.getHeight() - nameHeight - spacing - valueBoxHeight));
    }
    {   // 3 SHAPE, 4 MODE
        auto inside = groupPanels[2].reduced (20, 12).withTrimmedTop (30);
        shapeSwitch.setBounds (inside.withSizeKeepingCentre (juce::jmin (160, inside.getWidth()), 4 * 36));
        inside = groupPanels[3].reduced (20, 12).withTrimmedTop (30);
        modeSwitch.setBounds (inside.withSizeKeepingCentre (juce::jmin (160, inside.getWidth()), juce::jmin (inside.getHeight(), 2 * 36)));
    }
}

//==============================================================================
void EasyTremoloAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = tremoloProcessor.getCurrentSettings();
    const bool synced = settings.synced;
    const int note = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (TremoloIDs::note)->load());

    display.setSettings (settings);
    display.setPosition (tremoloProcessor.getPosition());
    display.setBypassed (settings.bypassed);

    if (synced != showingSynced)
        showSyncedKnob (synced);

    const auto info = describeRate (synced, note, settings.rateHz, tremoloProcessor.getTempo(), tremoloProcessor.isTempoFromDAW());
    if (info != rateInfo.getText())
        rateInfo.setText (info, juce::dontSendNotification);
}

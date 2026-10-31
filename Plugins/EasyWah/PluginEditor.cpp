#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    const std::array<KnobInfo, EasyWahAudioProcessorEditor::numKnobs> knobInfo
    {{
        { WahIDs::sensitivity, "SENSITIVITY",
          "How far your playing moves the pedal (and how quietly you can play and still move it). Set it so the loudest notes "
          "open the wah all the way. 0 = it doesn't move: the pedal stays where PEDAL puts it. On TEMPO: how far it rocks." },

        { WahIDs::note, "SWEEP",
          "How long one sweep takes (toe and back), as a note length at your song's tempo - locked to the beat while the song "
          "plays. 1/8 = the 'wakka-wakka' rhythm; 1 bar = a slow, swirling sweep." },

        { WahIDs::pedal, "PEDAL",
          "Where the pedal rests: 0% = heel down (dark), 100% = toe down (bright). Your playing pushes it from here. With "
          "SENSITIVITY at 0 it simply stays here - the 'cocked wah' sound." },

        { WahIDs::from, "FROM",
          "The heel position: the lowest, darkest the wah goes. 350-500 Hz for guitar, 150-250 Hz for bass." },

        { WahIDs::to, "TO",
          "The toe position: the highest, brightest the wah goes. 2-3 kHz for a classic wah; higher for a sharper quack." },

        { WahIDs::resonance, "RESONANCE",
          "How sharp and vocal the wah's peak is. Low = a gentle tone sweep; high = a narrow, quacking 'wow' (and more "
          "obvious)." },

        { WahIDs::mix, "MIX",
          "Blends your original back in. 100% = all wah. Lower it to keep your sound's body underneath (great on bass)." },
    }};

    const std::array<const char*, 3> groupNames { "HOW IT MOVES", "THE PEDAL", "SOUND" };

    constexpr const char* modeHint =
        "AUTO: the pedal follows how hard you play - each note pushes it forward and it falls back as the note dies away "
        "(an envelope filter). TEMPO: it rocks back and forth in time with your song.";
    constexpr const char* responseHint =
        "How quickly the pedal jumps forward and falls back. SNAPPY: tight and percussive (funk). SMOOTH: vocal, singing. "
        "LAZY: slow, swelling sweeps.";
    constexpr const char* directionHint =
        "UP: playing harder sweeps it brighter (the usual wah). DOWN: playing harder makes it darker - each note opens up "
        "as it fades, like a synth filter.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS and AUTO-GAIN stay as "
        "they are.";
    constexpr const char* displayHint =
        "The wah's filter: faintly at the heel and the toe, brightly where the pedal is right now. Underneath: the pedal - "
        "the bar shows how far it's moving.";
    constexpr const char* gettingStartedHint =
        "Pick a preset, or play and turn SENSITIVITY up until your loudest notes open the wah. Double-click a knob to reset "
        "it, or its number to type a value.";
}

//==============================================================================
void WahDisplay::setSettings (const AutoWah::Settings& s)
{
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    if (moved (s.fromHz, settings.fromHz) || moved (s.toHz, settings.toHz) || moved (s.resonance, settings.resonance) || moved (s.pedal, settings.pedal))
    {
        settings = s;
        repaint();
    }
}

void WahDisplay::setPedal (float low, float high)
{
    if (low > high) { low = high = shownHigh; }   // (nothing new: keep it)
    const float h = high >= shownHigh ? high : shownHigh + (high - shownHigh) * 0.35f;
    const float l = low <= shownLow ? low : shownLow + (low - shownLow) * 0.35f;
    if (std::abs (h - shownHigh) > 0.002f || std::abs (l - shownLow) > 0.002f)
    {
        shownHigh = h;
        shownLow = juce::jmin (l, h);
        repaint();
    }
}

void WahDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed) { bypassed = isBypassed; repaint(); }
}

void WahDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto inside = area.reduced (16.0f, 12.0f);
    auto pedalRow = inside.removeFromBottom (44.0f);
    auto plot = inside.withTrimmedLeft (26.0f).withTrimmedBottom (18.0f);

    constexpr float topDb = 24.0f, bottomDb = -24.0f;
    const auto xOf = [&] (double hz) { return plot.getX() + plot.getWidth() * (float) (std::log (hz / 50.0) / std::log (400.0)); };   // 50 Hz .. 20 kHz
    const auto yOf = [&] (float db)  { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (double hz : { 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (34.0f, 14.0f).withCentre ({ xOf (hz), plot.getBottom() + 9.0f }), juce::Justification::centred, false);
    }
    for (float db : { -12.0f, 0.0f, 12.0f })
    {
        g.setColour (std::abs (db) < 0.01f ? Palette::textDim.withAlpha (0.5f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0.0f ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 28.0f, yOf (db) - 7.0f, 24.0f, 14.0f), juce::Justification::centredRight, false);
    }

    const auto curve = [&] (float pedal)
    {
        juce::Path p;
        for (int i = 0; i <= 240; ++i)
        {
            const double hz = 50.0 * std::pow (400.0, i / 240.0);
            const juce::Point<float> pt (xOf (hz), yOf (AutoWah::responseDb (settings, pedal, hz)));
            if (i == 0) p.startNewSubPath (pt); else p.lineTo (pt);
        }
        return p;
    };

    g.setColour (Palette::textDim.withAlpha (0.45f));
    g.strokePath (curve (0.0f), juce::PathStrokeType (1.2f));
    g.strokePath (curve (1.0f), juce::PathStrokeType (1.2f));
    g.setColour (Palette::accent);
    g.strokePath (curve (shownHigh), juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The pedal: heel on the left, toe on the right; where it rests, and how far it's moving
    const auto track = pedalRow.reduced (60.0f, 14.0f);
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (track, 6.0f);
    const float restX = track.getX() + track.getWidth() * settings.pedal;
    g.setColour (Palette::accent.withAlpha (0.45f));
    g.fillRoundedRectangle (juce::Rectangle<float>::leftTopRightBottom (track.getX() + track.getWidth() * shownLow, track.getY(),
                                                                         track.getX() + track.getWidth() * shownHigh + 4.0f, track.getBottom()), 6.0f);
    g.setColour (Palette::textBright);
    g.fillRect (juce::Rectangle<float> (3.0f, track.getHeight() + 8.0f).withCentre ({ restX, track.getCentreY() }));
    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.setColour (Palette::textDim);
    g.drawText ("HEEL", pedalRow.withWidth (56.0f), juce::Justification::centredRight, false);
    g.drawText ("TOE", pedalRow.withTrimmedLeft (pedalRow.getWidth() - 56.0f), juce::Justification::centredLeft, false);

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
EasyWahAudioProcessorEditor::EasyWahAudioProcessorEditor (EasyWahAudioProcessor& p)
    : EasyWindow (p, "EASY WAH", "A wah that follows your playing - hover over anything for help", gettingStartedHint),
      wahProcessor (p),
      modeSwitch (*p.parameters.getParameter (WahIDs::mode), { "AUTO", "TEMPO" }, Palette::accent),
      responseSwitch (*p.parameters.getParameter (WahIDs::response), { "SNAPPY", "SMOOTH", "LAZY" }, Palette::accent),
      directionSwitch (*p.parameters.getParameter (WahIDs::direction), { "UP", "DOWN" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
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

    for (auto [label, sw, name, hint] : { std::tuple<juce::Label*, ChoiceSwitch*, const char*, const char*> { &modeLabel, &modeSwitch, "MODE", modeHint },
                                          { &responseLabel, &responseSwitch, "RESPONSE", responseHint },
                                          { &directionLabel, &directionSwitch, "DIRECTION", directionHint } })
    {
        label->setText (name, juce::dontSendNotification);
        label->setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        label->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*label);
        addAndMakeVisible (*sw);
        sw->onUserChange = [this] { knobMoved(); };
        addHint (*label, name, hint);
        addHint (*sw, name, hint);
    }
    modeSwitch.setHorizontal (true);
    directionSwitch.setHorizontal (true);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    showTempoParts (juce::roundToInt (p.parameters.getRawParameterValue (WahIDs::mode)->load()) == 1);
    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyWahAudioProcessorEditor::showTempoParts (bool tempo)
{
    // TEMPO: the sweep's note length in place of RESPONSE and DIRECTION (which only matter following your playing)
    showingTempo = tempo;
    knobs[noteKnob].slider.setVisible (tempo);
    knobs[noteKnob].label.setVisible (tempo);
    for (juce::Component* c : { (juce::Component*) &responseSwitch, (juce::Component*) &responseLabel,
                                (juce::Component*) &directionSwitch, (juce::Component*) &directionLabel })
        c->setVisible (! tempo);
}

void EasyWahAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");

    g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    g.setColour (showingMakeUp ? Palette::meterLow : Palette::textDim);
    g.drawText (showingMakeUp ? "AUTO-GAIN " + juce::String (shownMakeUpDb >= 0.05f ? "+" : "") + juce::String (shownMakeUpDb, 1) + " dB"
                              : juce::String ("AUTO-GAIN OFF"),
                makeUpReadout, juce::Justification::centred, false);
}

void EasyWahAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    autoGainButton.setBounds (autoGainButton.getBounds().withX (initAllButton.getX() - 24 - autoGainButton.getWidth()));
    bypassButton.setBounds (bypassButton.getBounds().withX (autoGainButton.getX() - 10 - bypassButton.getWidth()));

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
    auto displayInside = displayPanel.reduced (12, 12).withTrimmedTop (24);
    makeUpReadout = displayInside.removeFromBottom (18);
    displayInside.removeFromBottom (4);
    display.setBounds (displayInside);

    // 1 HOW IT MOVES: MODE; SENSITIVITY; RESPONSE and DIRECTION (or, on TEMPO, the sweep's note length)
    groupPanels[0] = left;
    {
        auto inside = left.reduced (14, 12).withTrimmedTop (26);
        auto row = inside.removeFromTop (nameHeight + 4 + 30);
        modeLabel.setBounds (row.removeFromTop (nameHeight));
        row.removeFromTop (4);
        modeSwitch.setBounds (row.withSizeKeepingCentre (juce::jmin (220, row.getWidth()), 30));
        inside.removeFromTop (8);
        placeKnob (knobs[sensitivityKnob], inside.removeFromTop (190), 130);
        inside.removeFromTop (6);
        auto bottom = inside;
        auto resp = bottom.removeFromTop (nameHeight + 4 + 30);
        responseLabel.setBounds (resp.removeFromTop (nameHeight));
        resp.removeFromTop (4);
        responseSwitch.setBounds (resp.withSizeKeepingCentre (juce::jmin (264, resp.getWidth()), 30));
        responseSwitch.setHorizontal (true);
        bottom.removeFromTop (10);
        auto dir = bottom.removeFromTop (nameHeight + 4 + 30);
        directionLabel.setBounds (dir.removeFromTop (nameHeight));
        dir.removeFromTop (4);
        directionSwitch.setBounds (dir.withSizeKeepingCentre (juce::jmin (180, dir.getWidth()), 30));
        placeKnob (knobs[noteKnob], inside, juce::jmin (100, inside.getHeight() - nameHeight - spacing - valueBoxHeight));
    }

    // 2 THE PEDAL: PEDAL above FROM and TO; 3 SOUND: RESONANCE, MIX
    const int topHeight = (right.getHeight() - gap) * 3 / 5;
    groupPanels[1] = right.removeFromTop (topHeight);
    right.removeFromTop (gap);
    groupPanels[2] = right;
    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto top = inside.removeFromTop (inside.getHeight() / 2);
        placeKnob (knobs[pedalKnob], top, juce::jmin (90, top.getHeight() - nameHeight - spacing - valueBoxHeight));
        const int w = inside.getWidth() / 2;
        const int size = juce::jmin (w - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[fromKnob], inside.removeFromLeft (w), size);
        placeKnob (knobs[toKnob], inside, size);
    }
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        const int w = inside.getWidth() / 2;
        const int size = juce::jmin (w - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[resonanceKnob], inside.removeFromLeft (w), size);
        placeKnob (knobs[mixKnob], inside, size);
    }
}

void EasyWahAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = wahProcessor.getCurrentSettings();
    const bool tempo = settings.mode == AutoWah::Mode::tempo;
    if (tempo != showingTempo)
        showTempoParts (tempo);

    display.setSettings (settings);
    const auto [low, high] = wahProcessor.takePedalRange();
    display.setPedal (low, high);
    display.setBypassed (settings.bypassed);

    const float rounded = std::round (wahProcessor.getMakeUpDb() * 10.0f) / 10.0f;
    if (settings.autoGain != showingMakeUp || std::abs (rounded - shownMakeUpDb) > 0.01f)
    {
        showingMakeUp = settings.autoGain;
        shownMakeUpDb = rounded;
        repaint (makeUpReadout);
    }
}

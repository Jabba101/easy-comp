#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    const std::array<KnobInfo, EasyFilterAudioProcessorEditor::numKnobs> knobInfo
    {{
        { FilterIDs::cutoff, "CUTOFF",
          "Where the filter sits: the middle of the movement. LOW-PASS lets through what's below it, HIGH-PASS what's above, "
          "BAND-PASS a band around it." },

        { FilterIDs::resonance, "RESONANCE",
          "A peak right at the cutoff. Low = smooth. High = a singing, whistling, squelchy edge that makes the movement easy "
          "to hear (it never howls on its own)." },

        { FilterIDs::rate, "RATE",
          "How fast it moves, in cycles a second (Hz). Slow (0.1-0.5) for sweeps, fast (4-10) for a trembling, vocal sound. "
          "With SYNC on TEMPO you pick a note length instead, locked to the beat." },

        { FilterIDs::note, "RATE",
          "How long one cycle takes, as a note length at your song's tempo - locked to the beat while the song plays. "
          "1/8 or 1/16 for pulses and wobbles; 1, 2, 4 or 8 bars for slow sweeps and build-ups." },

        { FilterIDs::depth, "DEPTH",
          "How far the movement takes the cutoff: 0 = it stays put (a plain filter), 100% = three octaves up and three down." },

        { FilterIDs::width, "WIDTH",
          "The left and right sides moving apart: 0 = together, 100% = half a cycle apart - one side opens as the other "
          "closes. Very wide. (Needs a stereo track or a stereo output.)" },

        { FilterIDs::mix, "MIX",
          "0% = your sound only, 100% = filtered only. In between blends the filtered sound back with the original - a "
          "gentler, fuller version of the effect." },
    }};

    const std::array<const char*, 5> groupNames { "TYPE", "FILTER", "HOW MUCH", "SHAPE", "SPEED" };

    constexpr const char* typeHint =
        "LOW-PASS: takes the top off - the classic synth sweep. HIGH-PASS: thins out the bottom - the build-up before a "
        "drop. BAND-PASS: only a band around the cutoff - telephone, radio, a wah-like quack.";
    constexpr const char* shapeHint =
        "How the filter moves. SINE and TRIANGLE: smooth sweeps. RAMP UP / DOWN: rising or falling every cycle, then "
        "jumping back - a pumping rhythm. SQUARE: jumping between two settings - pulses. RANDOM: a new place every cycle "
        "(sample & hold), different on each side with WIDTH.";
    constexpr const char* syncHint =
        "OFF: set RATE in cycles a second. TEMPO: set it as a note length (1/32 up to 8 bars) locked to your song's beat.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS stays as it is.";
    constexpr const char* displayHint =
        "The filter's curve right now - left side amber, right side green - over the shaded band its movement covers. "
        "Underneath: two cycles of the movement, with a dot where it is now.";
    constexpr const char* gettingStartedHint =
        "Pick a preset, or choose a TYPE and SHAPE, set CUTOFF, and turn up DEPTH. Double-click a knob to reset it, or its "
        "number to type a value.";

    // The display's frequency scale (log, 20 Hz - 20 kHz) and level scale
    constexpr float topDb = 18.0f, bottomDb = -42.0f;
}

//==============================================================================
void FilterDisplay::setSettings (const ModFilter::Settings& s, double sampleRate)
{
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    if (s.type != settings.type || s.shape != settings.shape || moved (s.cutoffHz, settings.cutoffHz) || moved (s.resonance, settings.resonance)
        || moved (s.depth, settings.depth) || moved (s.width, settings.width) || std::abs (sampleRate - rate) > 0.5)
    {
        settings = s;
        rate = sampleRate;
        repaint();
    }
}

void FilterDisplay::setNow (float p, float left, float right)
{
    if (std::abs (p - position) > 0.002f || std::abs (left / cutoff[0] - 1.0f) > 0.003f || std::abs (right / cutoff[1] - 1.0f) > 0.003f)
    {
        position = p;
        cutoff = { left, right };
        repaint();
    }
}

void FilterDisplay::setBypassed (bool b)
{
    if (bypassed != b) { bypassed = b; repaint(); }
}

void FilterDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto inner = area.reduced (14.0f, 12.0f);
    auto lfoArea = inner.removeFromBottom (juce::jmin (62.0f, inner.getHeight() * 0.26f));
    inner.removeFromBottom (14.0f);
    const auto plot = inner.withTrimmedLeft (34.0f).withTrimmedBottom (16.0f);

    const auto xOf = [&] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto hzAt = [&] (float x) { return 20.0f * std::pow (1000.0f, (x - plot.getX()) / plot.getWidth()); };
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };

    // Grid
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { 12.0f, 0.0f, -12.0f, -24.0f, -36.0f })
    {
        g.setColour (std::abs (db) < 0.01f ? Palette::textDim.withAlpha (0.5f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (xOf (hz) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    // The band the movement covers
    {
        const float spread = std::exp2 (settings.depth * ModFilter::maxOctaves);
        const float low = juce::jlimit (20.0f, 20000.0f, settings.cutoffHz / spread), high = juce::jlimit (20.0f, 20000.0f, settings.cutoffHz * spread);
        g.setColour (Palette::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (low), plot.getY(), xOf (high), plot.getBottom()));
        g.setColour (Palette::accent.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (xOf (settings.cutoffHz)), plot.getY(), plot.getBottom());
    }

    // The curves now: right (green) under left (amber)
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        for (int side = 1; side >= 0; --side)
        {
            juce::Path path;
            const int points = juce::roundToInt (plot.getWidth() / 2.0f);
            for (int i = 0; i <= points; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) points;
                const float db = ModFilter::responseDb (settings.type, cutoff[(size_t) side], settings.resonance, hzAt (x), rate);
                const juce::Point<float> pt (x, yOf (db));
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour (side == 0 ? Palette::accent : Palette::meterLow);
            g.strokePath (path, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // One cycle of the movement, with where it is now
    {
        const auto box = lfoArea.withTrimmedLeft (34.0f);
        g.setColour (Palette::panelEdge);
        g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.drawHorizontalLine (juce::roundToInt (box.getCentreY()), box.getX(), box.getRight());
        g.setColour (Palette::textDim);
        g.drawText ("high", juce::Rectangle<float> (lfoArea.getX(), box.getY(), 32.0f, 14.0f), juce::Justification::centredLeft, false);
        g.drawText ("low", juce::Rectangle<float> (lfoArea.getX(), box.getBottom() - 14.0f, 32.0f, 14.0f), juce::Justification::centredLeft, false);

        const auto inside = box.reduced (6.0f, 8.0f);
        const auto yLfo = [&] (float v) { return inside.getCentreY() - v * inside.getHeight() * 0.5f; };
        for (int side = 1; side >= 0; --side)
        {
            const double offset = side == 1 ? 0.5 * settings.width : 0.0;
            juce::Path path;
            constexpr int steps = 600, cyclesShown = 2;
            for (int i = 0; i <= steps; ++i)
            {
                const double p = (double) i / steps * cyclesShown;
                const float v = ModFilter::shapeAt (settings.shape, p + offset, std::floor (p + offset) + 3.0, side);
                const juce::Point<float> pt (inside.getX() + inside.getWidth() * (float) (p / cyclesShown), yLfo (v));
                if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
            }
            g.setColour ((side == 0 ? Palette::accent : Palette::meterLow).withAlpha (side == 0 ? 1.0f : 0.8f));
            g.strokePath (path, juce::PathStrokeType (2.0f));

            const double at = position + offset;
            const float v = ModFilter::shapeAt (settings.shape, at, std::floor (at) + 3.0, side);
            g.setColour (Palette::textBright);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ inside.getX() + inside.getWidth() * position / cyclesShown, yLfo (v) }));
        }
    }

    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.setColour (Palette::accent);
    g.drawText ("LEFT", plot.withHeight (16.0f).translated (8.0f, 4.0f), juce::Justification::centredLeft, false);
    g.setColour (Palette::meterLow);
    g.drawText ("RIGHT", plot.withHeight (16.0f).translated (54.0f, 4.0f), juce::Justification::centredLeft, false);

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
EasyFilterAudioProcessorEditor::EasyFilterAudioProcessorEditor (EasyFilterAudioProcessor& p)
    : EasyWindow (p, "EASY FILTER", "A moving filter: sweeps, wobbles and tempo pulses - hover over anything for help", gettingStartedHint),
      filterProcessor (p),
      typeSwitch (*p.parameters.getParameter (FilterIDs::type), { "LOW-PASS", "HIGH-PASS", "BAND-PASS" }, Palette::accent),
      shapeSwitch (*p.parameters.getParameter (FilterIDs::shape), { "SINE", "TRIANGLE", "RAMP UP", "RAMP DOWN", "SQUARE", "RANDOM" }, Palette::accent),
      syncSwitch (*p.parameters.getParameter (FilterIDs::sync), { "OFF", "TEMPO" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
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

    for (auto* sw : { &typeSwitch, &shapeSwitch })
    {
        addAndMakeVisible (*sw);
        sw->onUserChange = [this] { knobMoved(); };
    }
    addHint (typeSwitch, "TYPE", typeHint);
    addHint (shapeSwitch, "SHAPE", shapeHint);

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

    showSyncedKnob (juce::roundToInt (p.parameters.getRawParameterValue (FilterIDs::sync)->load()) == 1);
    finishSetup (1100, 640);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyFilterAudioProcessorEditor::showSyncedKnob (bool synced)
{
    showingSynced = synced;
    knobs[rateKnob].slider.setVisible (! synced);
    knobs[rateKnob].label.setVisible (! synced);
    knobs[noteKnob].slider.setVisible (synced);
    knobs[noteKnob].label.setVisible (synced);
}

void EasyFilterAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);
    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
}

void EasyFilterAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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

    // Middle: the display, and HOW MUCH under it
    groupPanels[2] = area.removeFromBottom (166);
    area.removeFromBottom (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        const int w = inside.getWidth() / 3;
        for (int k : { (int) depthKnob, (int) widthKnob, (int) mixKnob })
        {
            auto column = k == mixKnob ? inside : inside.removeFromLeft (w);
            placeKnob (knobs[(size_t) k], column, knobSizeFor (column));
        }
    }

    // Left: TYPE, then FILTER (CUTOFF big, RESONANCE under it)
    groupPanels[0] = left.removeFromTop (150);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    typeSwitch.setBounds (groupPanels[0].reduced (40, 12).withTrimmedTop (30).withSizeKeepingCentre (210, 3 * 32 - 4));
    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto top = inside.removeFromTop (inside.getHeight() * 11 / 20);
        placeKnob (knobs[cutoffKnob], top, knobSizeFor (top));
        inside.removeFromTop (10);
        placeKnob (knobs[resonanceKnob], inside, juce::jmin (knobSizeFor (inside), 76));
    }

    // Right: SHAPE, then SPEED
    groupPanels[3] = right.removeFromTop (6 * 30 + 50);
    right.removeFromTop (gap);
    groupPanels[4] = right;
    shapeSwitch.setBounds (groupPanels[3].reduced (40, 12).withTrimmedTop (30).withSizeKeepingCentre (210, 6 * 30 - 4));
    {
        auto inside = groupPanels[4].reduced (10, 12).withTrimmedTop (26);
        auto knobSpace = inside.removeFromLeft (inside.getWidth() * 3 / 5);
        placeKnob (knobs[rateKnob], knobSpace, knobSizeFor (knobSpace));
        placeKnob (knobs[noteKnob], knobSpace, knobSizeFor (knobSpace));
        auto c = inside.withSizeKeepingCentre (inside.getWidth(), nameHeight + 6 + 2 * 32);
        syncLabel.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (6);
        syncSwitch.setBounds (c.withSizeKeepingCentre (juce::jmin (100, c.getWidth()), c.getHeight()));
    }
}

void EasyFilterAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = filterProcessor.getCurrentSettings();
    if (settings.synced != showingSynced)
        showSyncedKnob (settings.synced);

    display.setSettings (settings, filterProcessor.getSampleRateForDisplay());
    display.setNow (filterProcessor.getPosition(), filterProcessor.getCutoffNow (0), filterProcessor.getCutoffNow (1));
    display.setBypassed (settings.bypassed);
}

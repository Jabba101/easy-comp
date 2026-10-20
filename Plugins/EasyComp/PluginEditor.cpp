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

    // In on-screen order (see knobGroups below for which panel each is in).
    const std::array<KnobInfo, 7> knobInfo
    {{
        { CompIDs::threshold, "THRESHOLD",
          "The volume where squeezing starts. Turn it down (left) until the meter moves on the loudest parts. "
          "The lower it goes, the more of the sound gets compressed." },

        { CompIDs::ratio, "RATIO",
          "How hard it squeezes once the sound goes over the threshold. "
          "2:1 is gentle, 4:1 is a solid all-rounder, 8:1 and up is heavy squashing." },

        { CompIDs::attack, "ATTACK",
          "How quickly it grabs a sound once it gets loud. Slower (15-30 ms) lets the start of each note or hit through "
          "= more punch and definition. Very fast (under 2 ms) catches the sharp edges for a softer, more even sound." },

        { CompIDs::release, "RELEASE",
          "How quickly it lets go once the sound gets quieter. Short = livelier, but can 'pump'. Long = smoother and more even. "
          "Tip: the meter should fall back between notes or hits, not stay down all the time." },

        { CompIDs::makeup, "MAKE-UP",
          "Compressing makes things quieter, so this turns the result back up. Match the volume with BYPASS on and off: "
          "louder always sounds 'better', so a fair comparison keeps your ears honest. (Or let AUTO-GAIN do it.)" },

        { CompIDs::mix, "MIX",
          "Blends your original sound with the compressed one. 100% = fully compressed. Heavy settings at 30-50% is "
          "'parallel compression': the life of the original plus the thickness of the squashed copy." },

        { CompIDs::lowCut, "IGNORE BELOW",
          "Stops low notes (kick, bass, the boom of a guitar or piano) from setting off the compressor. It only changes "
          "what it reacts to - never the tone of your sound. Try 80-150 Hz if the low end makes everything 'pump'." },
    }};

    // The four knob panels, numbered in the order you'd usually set them.
    struct KnobGroup
    {
        const char* name;
        size_t firstKnob, numKnobs;
    };

    const std::array<KnobGroup, 4> knobGroups
    {{
        { "HOW MUCH",          0, 2 },
        { "HOW FAST",          2, 2 },
        { "OUTPUT",            4, 2 },
        { "WHAT IT REACTS TO", 6, 1 },
    }};

    constexpr const char* reactsToHint =
        "PEAKS reacts to every sharp peak - tight control, best for drums, percussion and anything punchy. "
        "AVERAGE follows the overall level instead - smoother and more natural on vocals, bass, guitars and whole mixes.";

    constexpr const char* makeupAutoHint =
        "AUTO-GAIN is on, so it's setting the volume for you - the box shows how much it's adding right now. "
        "Switch AUTO-GAIN off to set MAKE-UP by hand again.";

    constexpr const char* graphHint =
        "Along the bottom: how loud your sound goes in. Up the side: how loud it comes out. Dotted line = untouched. "
        "The bend is the THRESHOLD; the flatter the line after it, the higher the RATIO. The dot is your sound as it plays.";

    constexpr const char* meterHint =
        "How much the compressor is turning the sound down right now. Around 3-6 dB on the loud parts is a natural "
        "starting point; 10 dB or more is an obvious, squashed effect. Each preset's tip says what to aim for.";

    constexpr const char* gettingStartedHint =
        "Pick a preset for your instrument, then turn THRESHOLD down until the meter moves on the loud parts. "
        "Hover over anything for an explanation. Double-click a knob to reset it; click a number to type a value.";
}

//==============================================================================
// Gain reduction meter
//==============================================================================
void GainReductionMeter::setGainReduction (float reductionDb)
{
    // Jump up instantly so short hits show, then fall back gently so your eye
    // can follow it.
    constexpr float fallPerUpdateDb = 0.75f;   // about 22 dB per second at 30 updates a second

    const float next = reductionDb >= shownDb ? reductionDb
                                              : juce::jmax (reductionDb, shownDb - fallPerUpdateDb);

    if (std::abs (next - shownDb) > 0.01f)
    {
        shownDb = next;
        repaint();
    }
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // The number at the bottom
    auto readout = area.removeFromBottom (24.0f);
    area.removeFromBottom (8.0f);

    const bool active = shownDb > 0.05f;
    g.setColour (active ? Palette::textBright : Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
    g.drawText (active ? "-" + juce::String (shownDb, 1) + " dB" : juce::String ("0.0 dB"),
                readout, juce::Justification::centred, false);

    // dB scale on the left, a column of LEDs on the right
    area.reduce (0.0f, 6.0f);   // room for the top and bottom scale numbers
    auto scale = area.removeFromLeft (area.getWidth() * 0.45f);
    const auto leds = area.withSizeKeepingCentre (juce::jmin (26.0f, area.getWidth()), area.getHeight());

    constexpr float maxDb    = 24.0f;   // bottom of the meter
    constexpr int   numLeds  = 16;      // so each LED is 1.5 dB
    constexpr float dbPerLed = maxDb / (float) numLeds;
    const float ledHeight = leds.getHeight() / (float) numLeds;

    for (int i = 0; i < numLeds; ++i)
    {
        const auto led = juce::Rectangle<float> (leds.getX(), leds.getY() + (float) i * ledHeight,
                                                 leds.getWidth(), ledHeight).reduced (0.0f, 1.5f);

        const float ledStartDb = (float) i * dbPerLed;
        const bool lit = shownDb > ledStartDb + 0.25f;

        // green up to 6 dB (gentle), amber up to 12 dB (firm), red beyond (heavy)
        const auto colour = ledStartDb < 6.0f ? Palette::meterLow
                                              : (ledStartDb < 12.0f ? Palette::meterMid : Palette::meterHigh);

        g.setColour (lit ? colour : Palette::meterOff);
        g.fillRoundedRectangle (led, 2.0f);
    }

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = 0; db <= (int) maxDb; db += 3)
    {
        const float y = leds.getY() + (float) db / maxDb * leds.getHeight();
        g.drawText (juce::String (db), scale.withY (y - 7.0f).withHeight (14.0f).withTrimmedRight (8.0f),
                    juce::Justification::centredRight, false);
    }
}

//==============================================================================
// Compression curve graph
//==============================================================================
void CurveGraph::update (const Compressor::Settings& newSettings, float inputLevelDb, float newAutoGainDb)
{
    const auto differs = [] (float a, float b) { return std::abs (a - b) > 0.001f; };

    // Only these change what the graph shows. (AUTO-GAIN moves gradually, so
    // its amount only counts once it has moved by a visible 0.05 dB.)
    const bool curveChanged = differs (newSettings.thresholdDb, settings.thresholdDb)
                           || differs (newSettings.ratio,       settings.ratio)
                           || differs (newSettings.makeupDb,    settings.makeupDb)
                           || differs (newSettings.mix,         settings.mix)
                           || newSettings.autoGain != settings.autoGain
                           || newSettings.bypassed != settings.bypassed
                           || (newSettings.autoGain && std::abs (newAutoGainDb - autoGainDb) > 0.05f);
    settings = newSettings;

    if (curveChanged)
        autoGainDb = newAutoGainDb;

    // The dot jumps up instantly and falls back gently, like the meter.
    constexpr float fallPerUpdateDb = 1.0f;   // about 30 dB per second at 30 updates a second

    const float next = inputLevelDb >= dotLevelDb ? inputLevelDb
                                                  : juce::jmax (inputLevelDb, dotLevelDb - fallPerUpdateDb);

    const bool dotMoved = differs (next, dotLevelDb) && juce::jmax (next, dotLevelDb) > lowestDb;
    dotLevelDb = next;

    if (curveChanged || dotMoved)
        repaint();
}

juce::Rectangle<float> CurveGraph::getPlotArea() const
{
    // Leave room on the left and at the bottom for the scale numbers and captions.
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (46.0f);
    area.removeFromBottom (34.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);

    const float side = juce::jmin (area.getWidth(), area.getHeight());
    return area.withSizeKeepingCentre (side, side);
}

juce::Point<float> CurveGraph::toScreen (juce::Rectangle<float> plot, float inDb, float outDb) const
{
    const auto proportion = [] (float db) { return (db - lowestDb) / (highestDb - lowestDb); };

    return { plot.getX() + proportion (inDb) * plot.getWidth(),
             plot.getBottom() - proportion (outDb) * plot.getHeight() };
}

void CurveGraph::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot.expanded (1.0f), 6.0f);

    // Grid lines and scale numbers every 12 dB
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = (int) lowestDb; db <= (int) highestDb; db += 12)
    {
        const auto gridPoint = toScreen (plot, (float) db, (float) db);

        if (db > (int) lowestDb && db < (int) highestDb)
        {
            g.setColour (Palette::panelEdge);
            g.drawVerticalLine (juce::roundToInt (gridPoint.x), plot.getY(), plot.getBottom());
            g.drawHorizontalLine (juce::roundToInt (gridPoint.y), plot.getX(), plot.getRight());
        }

        g.setColour (Palette::textDim);

        g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - 34.0f, gridPoint.y - 7.0f, 28.0f, 14.0f),
                    juce::Justification::centredRight, false);

        if (db > (int) lowestDb)   // the corner is already labelled on the side
            g.drawText (juce::String (db), juce::Rectangle<float> (gridPoint.x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 14.0f),
                        juce::Justification::centred, false);
    }

    // Axis captions
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("LEVEL IN (dB)", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 19.0f, plot.getWidth(), 14.0f),
                juce::Justification::centred, false);
    {
        juce::Graphics::ScopedSaveState rotated (g);
        const juce::Point<float> centre (plot.getX() - 39.0f, plot.getCentreY());
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, centre.x, centre.y));
        g.drawText ("LEVEL OUT (dB)", juce::Rectangle<float> (plot.getHeight(), 14.0f).withCentre (centre),
                    juce::Justification::centred, false);
    }

    // Everything from here on stays inside the plot.
    juce::Graphics::ScopedSaveState clipped (g);
    g.reduceClipRegion (plot.toNearestInt());

    // The dotted line: what comes out with no compression at all
    const float dots[] = { 3.0f, 5.0f };
    g.setColour (Palette::textDim.withAlpha (0.7f));
    g.drawDashedLine ({ toScreen (plot, lowestDb, lowestDb), toScreen (plot, highestDb, highestDb) }, dots, 2, 1.5f);

    // The threshold: where the bend starts
    const float dashes[] = { 6.0f, 4.0f };
    const float thresholdX = toScreen (plot, settings.thresholdDb, lowestDb).x;
    g.setColour (Palette::accent.withAlpha (0.35f));
    g.drawDashedLine ({ thresholdX, plot.getY(), thresholdX, plot.getBottom() }, dashes, 2, 1.0f);

    // The curve itself (the same maths the audio uses), with a soft fill underneath
    juce::Path curve;
    constexpr int steps = 240;

    for (int i = 0; i <= steps; ++i)
    {
        const float inDb  = lowestDb + (highestDb - lowestDb) * (float) i / (float) steps;
        const auto onPlot = toScreen (plot, inDb, Compressor::staticOutputDb (inDb, settings, autoGainDb));

        if (i == 0)
            curve.startNewSubPath (onPlot);
        else
            curve.lineTo (onPlot);
    }

    juce::Path underCurve (curve);
    underCurve.lineTo (plot.getBottomRight());
    underCurve.lineTo (plot.getBottomLeft());
    underCurve.closeSubPath();
    g.setColour (Palette::accent.withAlpha (0.08f));
    g.fillPath (underCurve);

    g.setColour (Palette::accent);
    g.strokePath (curve, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // The dot: your sound, right now
    if (dotLevelDb > lowestDb)
    {
        const float inDb   = juce::jmin (dotLevelDb, highestDb);
        const auto onCurve = toScreen (plot, inDb, Compressor::staticOutputDb (inDb, settings, autoGainDb));

        g.setColour (Palette::accent.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (onCurve));
        g.setColour (Palette::textBright);
        g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (onCurve));
    }

    // While bypassed, fade the graph and say so plainly.
    if (settings.bypassed)
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
// The Easy Comp window
//==============================================================================
EasyCompAudioProcessorEditor::EasyCompAudioProcessorEditor (EasyCompAudioProcessor& p)
    : EasyWindow (p, "EASY COMP", "A simple compressor - hover over anything for help", gettingStartedHint),
      compProcessor (p),
      reactsToSwitch (*p.parameters.getParameter (CompIDs::reactsTo), { "PEAKS", "AVERAGE" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        // Connects the knob to its setting: from now on they move together,
        // and the box under the knob shows the value ("-20.0 dB", "4.0 : 1"...).
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this] { knobMoved(); };

        // Hovering the knob (or its name) explains it. MAKE-UP says something
        // different while AUTO-GAIN is in charge.
        const auto hint = [this, i]
        {
            if (i == makeupKnob && showingAutoGain)
                return std::make_pair (juce::String (knobInfo[i].name), juce::String (makeupAutoHint));

            return std::make_pair (juce::String (knobInfo[i].name), juce::String (knobInfo[i].hint));
        };

        addHint (knob.slider, hint);
        addHint (knob.label, hint);
    }

    // While AUTO-GAIN is on, the MAKE-UP box shows how much it's adding.
    auto& makeupSlider = knobs[makeupKnob].slider;
    makeupSlider.textFromValueFunction = [this, manualText = makeupSlider.textFromValueFunction] (double value)
    {
        if (showingAutoGain)
            return "Auto +" + juce::String (shownAutoGainDb, 1) + " dB";

        return manualText != nullptr ? manualText (value) : juce::String (value, 1);
    };

    reactsToLabel.setText ("REACTS TO", juce::dontSendNotification);
    reactsToLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    reactsToLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (reactsToLabel);
    addAndMakeVisible (reactsToSwitch);
    addHint (reactsToLabel, "REACTS TO", reactsToHint);
    addHint (reactsToSwitch, "REACTS TO", reactsToHint);

    reactsToSwitch.onUserChange = [this] { knobMoved(); };

    addAndMakeVisible (graph);
    addHint (graph, "COMPRESSION CURVE", graphHint);

    addAndMakeVisible (meter);
    addHint (meter, "GAIN REDUCTION", meterHint);

    finishSetup (1100, 600);
}

//==============================================================================
void EasyCompAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The four knob panels, numbered in the order you'd usually set them
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), knobGroups[i].name);

    drawPanel (g, graphPanel, {}, "COMPRESSION CURVE");
    drawPanel (g, meterPanel, {}, "GAIN REDUCTION");
}

void EasyCompAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // Right-hand side: the meter, and the curve graph (square) beside it
    meterPanel = area.removeFromRight (140);
    meter.setBounds (meterPanel.reduced (14, 12).withTrimmedTop (24));
    area.removeFromRight (gap);

    graphPanel = area.removeFromRight (area.getHeight());
    graph.setBounds (graphPanel.reduced (12, 12).withTrimmedTop (24));
    area.removeFromRight (gap);

    // Left-hand side: the four knob panels, two by two
    const int panelWidth  = (area.getWidth()  - gap) / 2;
    const int panelHeight = (area.getHeight() - gap) / 2;

    for (size_t group = 0; group < knobGroups.size(); ++group)
    {
        const int column = (int) group % 2;
        const int row    = (int) group / 2;

        groupPanels[group] = { area.getX() + column * (panelWidth + gap),
                               area.getY() + row * (panelHeight + gap),
                               panelWidth, panelHeight };

        auto inside = groupPanels[group].reduced (10, 12).withTrimmedTop (26);
        const int columnWidth = inside.getWidth() / 2;

        // Each column is: name, knob, value box - kept together and centred.
        constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
        const int knobSize = juce::jmin (columnWidth,
                                         inside.getHeight() - nameHeight - spacing - valueBoxHeight);

        for (size_t k = 0; k < knobGroups[group].numKnobs; ++k)
        {
            auto& knob = knobs[knobGroups[group].firstKnob + k];
            auto columnArea = inside.removeFromLeft (columnWidth)
                                    .withSizeKeepingCentre (columnWidth, nameHeight + spacing + knobSize + valueBoxHeight);

            knob.label.setBounds (columnArea.removeFromTop (nameHeight));
            columnArea.removeFromTop (spacing);
            knob.slider.setBounds (columnArea);   // the knob, with its value box underneath
        }

        if (knobGroups[group].numKnobs == 1)
        {
            // The spare column holds REACTS TO: its name, then PEAKS / AVERAGE,
            // lined up with the IGNORE BELOW knob beside it.
            auto spare = inside.withSizeKeepingCentre (inside.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
            reactsToLabel.setBounds (spare.removeFromTop (nameHeight));
            spare.removeFromTop (spacing + 6);
            reactsToSwitch.setBounds (spare.withSizeKeepingCentre (juce::jmin (spare.getWidth() - 4, 112), 80));
        }
    }
}

//==============================================================================
void EasyCompAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = compProcessor.getCurrentSettings();
    const float autoGainDb = compProcessor.getAutoGainDb();

    meter.setGainReduction (compProcessor.takeGainReductionForMeter());
    graph.update (settings, compProcessor.takeInputLevelForGraph(), autoGainDb);

    // AUTO-GAIN takes over from MAKE-UP: fade that knob, and show the amount
    // AUTO-GAIN is adding in its box (in green, like the AUTO-GAIN light).
    auto& makeupSlider = knobs[makeupKnob].slider;
    const float roundedDb = std::round (autoGainDb * 10.0f) / 10.0f;

    if (settings.autoGain != showingAutoGain)
    {
        showingAutoGain = settings.autoGain;
        shownAutoGainDb = roundedDb;
        makeupSlider.setEnabled (! showingAutoGain);
        makeupSlider.setColour (juce::Slider::textBoxTextColourId, showingAutoGain ? Palette::meterLow : Palette::textBright);
        makeupSlider.updateText();
    }
    else if (showingAutoGain && std::abs (roundedDb - shownAutoGainDb) > 0.01f)
    {
        shownAutoGainDb = roundedDb;
        makeupSlider.updateText();
    }
}

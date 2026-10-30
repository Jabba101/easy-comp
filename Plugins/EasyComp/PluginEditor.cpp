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

    // In on-screen order (see layoutBody for which panel each is in).
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

    // The five panels, numbered in the order you'd usually set them.
    const std::array<const char*, 5> panelNames { "HOW MUCH", "HOW FAST", "OUTPUT", "WHAT IT REACTS TO", "CHARACTER" };

    constexpr const char* slotHint =
        "A and B: two sets of settings to compare. Switch between them while the track plays to hear which you prefer. B "
        "starts as a copy of A the first time you switch to it; the one you're not on is kept with your project.";
    constexpr const char* copyHint =
        "Copies the settings you're on to the other one (A to B, or B to A) - then change one of them and compare.";

    constexpr const char* reactsToHint =
        "PEAKS reacts to every sharp peak - tight control, best for drums, percussion and anything punchy. "
        "AVERAGE follows the overall level instead - smoother and more natural on vocals, bass, guitars and whole mixes.";

    constexpr const char* listensToHint =
        "THIS TRACK: it listens to your track itself (the usual). SIDECHAIN: it listens to another track your DAW sends "
        "to its sidechain input instead - e.g. the kick turning the bass down a little on every hit, or music dipping "
        "under a voice.";

    constexpr const char* styleHint =
        "MODERN: clean and precise. VINTAGE: our take on the classic 'feedback' style - it listens to its own result, "
        "follows loud passages closely and eases back gently when the sound drops away. Thicker, with more glue: great "
        "on drum buses, bass and mixes.";

    constexpr const char* lowEndHint =
        "THRUST: the compressor hears less and less of the low end the lower it goes (and a little more of the bright, "
        "edgy range), so the kick and bass punch through instead of setting it off. Only changes what it reacts to - you "
        "may need to lower THRESHOLD a little.";

    constexpr const char* makeupAutoHint =
        "AUTO-GAIN is on, so it's setting the volume for you - the box shows how much it's adding right now. "
        "Switch AUTO-GAIN off to set MAKE-UP by hand again.";

    constexpr const char* graphHint =
        "Along the bottom: how loud your sound goes in. Up the side: how loud it comes out. Dotted line = untouched. "
        "The bend is the THRESHOLD; the flatter the line after it, the higher the RATIO. The dot is your sound as it plays.";

    constexpr const char* graphSidechainHint =
        "Along the bottom: how loud the SIDECHAIN is (the other track it's listening to). Up the side: how loud your "
        "sound comes out at that moment. The bend is the THRESHOLD; the dot moves with the sidechain as it plays.";

    constexpr const char* meterHint =
        "How much the compressor is turning the sound down right now. Around 3-6 dB on the loud parts is a natural "
        "starting point; 10 dB or more is an obvious, squashed effect. Each preset's tip says what to aim for.";

    constexpr const char* gettingStartedHint =
        "Pick a preset for your instrument, then turn THRESHOLD down until the meter moves on the loud parts. "
        "Hover over anything for an explanation. Double-click a knob to reset it; click a number to type a value.";
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
                           || newSettings.vintage  != settings.vintage
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

void CurveGraph::setSidechainStatus (bool isListeningToSidechain, bool isSidechainMissing)
{
    if (isListeningToSidechain != listeningToSidechain || isSidechainMissing != sidechainMissing)
    {
        listeningToSidechain = isListeningToSidechain;
        sidechainMissing     = isSidechainMissing;
        repaint();
    }
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

    // While listening to the sidechain, say so (the dot is the other track).
    if (listeningToSidechain && ! settings.bypassed && ! sidechainMissing)
    {
        const auto caption = juce::Rectangle<float> (plot.getX() + 8.0f, plot.getY() + 8.0f, 196.0f, 24.0f);
        g.setColour (Palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (caption, 6.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (caption.reduced (0.75f), 6.0f, 1.2f);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("LISTENING TO THE SIDECHAIN", caption, juce::Justification::centred, false);
    }

    // While bypassed, or with nothing coming into the sidechain, fade the
    // graph and say so plainly.
    if (settings.bypassed || sidechainMissing)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (plot);

        if (settings.bypassed)
            drawBadge (g, plot, Palette::meterHigh, "BYPASSED", "You're hearing the original sound");
        else
            drawBadge (g, plot, Palette::meterHigh, "NOTHING IS COMING INTO THE SIDECHAIN",
                       "Send a track to Easy Comp's sidechain in your DAW, or set LISTENS TO: THIS TRACK");
    }
}

void CurveGraph::drawBadge (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour,
                            const juce::String& title, const juce::String& text) const
{
    // The title on one or two lines, then the explanation on one or two.
    const juce::Font titleFont (juce::FontOptions (17.0f, juce::Font::bold)), textFont (juce::FontOptions (12.5f));
    const float width = juce::jmin (plot.getWidth() - 24.0f,
                                    juce::jmax (230.0f, juce::GlyphArrangement::getStringWidth (titleFont, title) + 40.0f,
                                                juce::GlyphArrangement::getStringWidth (textFont, text) + 40.0f));
    const auto lines = [] (const juce::Font& font, const juce::String& t, float space)
    {
        return juce::GlyphArrangement::getStringWidth (font, t) > space ? 2 : 1;
    };

    const int titleLines = lines (titleFont, title, width - 20.0f), textLines = lines (textFont, text, width - 20.0f);
    const float height = 18.0f + 22.0f * (float) titleLines + 17.0f * (float) textLines;
    const auto badge = juce::Rectangle<float> (width, height).withCentre (plot.getCentre());

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (badge, 8.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);

    auto area = badge.reduced (10.0f, 9.0f);
    g.setFont (titleFont);
    g.drawFittedText (title, area.removeFromTop (22.0f * (float) titleLines).toNearestInt(), juce::Justification::centred, titleLines, 0.9f);
    g.setColour (Palette::textDim);
    g.setFont (textFont);
    g.drawFittedText (text, area.toNearestInt(), juce::Justification::centred, textLines, 0.9f);
}

//==============================================================================
// The Easy Comp window
//==============================================================================
EasyCompAudioProcessorEditor::Switch::Switch (juce::RangedAudioParameter& parameter, const juce::String& name,
                                              const juce::StringArray& options)
    : choices (parameter, options, Palette::accent)
{
    label.setText (name, juce::dontSendNotification);
    label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    label.setJustificationType (juce::Justification::centred);
}

EasyCompAudioProcessorEditor::EasyCompAudioProcessorEditor (EasyCompAudioProcessor& p)
    : EasyWindow (p, "EASY COMP", "A simple compressor - hover over anything for help", gettingStartedHint),
      compProcessor (p),
      reactsTo  (*p.parameters.getParameter (CompIDs::reactsTo),  "REACTS TO",  { "PEAKS", "AVERAGE" }),
      listensTo (*p.parameters.getParameter (CompIDs::listensTo), "LISTENS TO", { "THIS TRACK", "SIDECHAIN" }),
      style     (*p.parameters.getParameter (CompIDs::character), "STYLE",      { "MODERN", "VINTAGE" }),
      lowEnd    (*p.parameters.getParameter (CompIDs::lowEnd),    "LOW END",    { "NORMAL", "THRUST" })
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

    addSwitch (reactsTo,  reactsToHint);
    addSwitch (listensTo, listensToHint);   // (not part of presets: it depends on how your tracks are routed)
    addSwitch (style,     styleHint);
    addSwitch (lowEnd,    lowEndHint);

    addAndMakeVisible (graph);
    addHint (graph, [this]
    {
        return std::make_pair (juce::String ("COMPRESSION CURVE"),
                               juce::String (compProcessor.usesSidechain() ? graphSidechainHint : graphHint));
    });

    addAndMakeVisible (meter);
    addHint (meter, "GAIN REDUCTION", meterHint);

    // A / B and COPY, in the header
    for (int slot = 0; slot < 2; ++slot)
    {
        auto& button = slot == 0 ? slotA : slotB;
        button.onClick = [this, slot]
        {
            easyProcessor.switchToSlot (slot);
            knobMoved();
            refreshLiveDisplays();
        };
        addAndMakeVisible (button);
        addHint (button, "A / B", slotHint);
    }
    copyButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    copyButton.setColour (juce::TextButton::textColourOffId, Palette::textBright);
    copyButton.onClick = [this] { easyProcessor.copyToOtherSlot(); };
    addAndMakeVisible (copyButton);
    addHint (copyButton, "COPY", copyHint);

    finishSetup (1340, 600);
}

void EasyCompAudioProcessorEditor::addSwitch (Switch& choice, const char* hint)
{
    const auto name = choice.label.getText();
    addAndMakeVisible (choice.label);
    addAndMakeVisible (choice.choices);
    addHint (choice.label, name, hint);
    addHint (choice.choices, name, hint);

    // LISTENS TO isn't part of presets, so changing it doesn't "edit" one.
    if (&choice != &listensTo)
        choice.choices.onUserChange = [this] { knobMoved(); };
}

//==============================================================================
void EasyCompAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The five panels, numbered in the order you'd usually set them
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), panelNames[i]);

    drawPanel (g, graphPanel, {}, "COMPRESSION CURVE");
    drawPanel (g, meterPanel, {}, "GAIN REDUCTION");
}

void EasyCompAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // The header: A / B and COPY to the left of BYPASS
    {
        const auto bypass = bypassButton.getBounds();
        auto ab = juce::Rectangle<int> (bypass.getX() - 24 - (48 + 6 + 48 + 10 + 110), bypass.getY(), 48 + 6 + 48 + 10 + 110, bypass.getHeight());
        slotA.setBounds (ab.removeFromLeft (48));
        ab.removeFromLeft (6);
        slotB.setBounds (ab.removeFromLeft (48));
        ab.removeFromLeft (10);
        copyButton.setBounds (ab.reduced (0, 2));
    }

    // Right-hand side: the meter, and the curve graph (square) beside it
    meterPanel = area.removeFromRight (140);
    meter.setBounds (meterPanel.reduced (14, 12).withTrimmedTop (24));
    area.removeFromRight (gap);

    graphPanel = area.removeFromRight (area.getHeight());
    graph.setBounds (graphPanel.reduced (12, 12).withTrimmedTop (24));
    area.removeFromRight (gap);

    // Left-hand side: the five panels in two rows - HOW MUCH, HOW FAST and
    // OUTPUT along the top, WHAT IT REACTS TO and CHARACTER along the bottom.
    // Each panel is split into columns: a knob, or a switch with its name.
    struct Column
    {
        Knob* knob = nullptr;
        Switch* choice = nullptr;
    };

    const std::array<std::vector<Column>, 5> panels
    {{
        { { &knobs[0] }, { &knobs[1] } },                                  // THRESHOLD, RATIO
        { { &knobs[2] }, { &knobs[3] } },                                  // ATTACK, RELEASE
        { { &knobs[4] }, { &knobs[5] } },                                  // MAKE-UP, MIX
        { { &knobs[6] }, { nullptr, &reactsTo }, { nullptr, &listensTo } }, // IGNORE BELOW, REACTS TO, LISTENS TO
        { { nullptr, &style }, { nullptr, &lowEnd } },                     // STYLE, LOW END
    }};

    const std::array<std::pair<size_t, size_t>, 2> rows { { { 0, 3 }, { 3, 2 } } };   // (first panel, how many)

    constexpr int panelPadding = 20;   // (10 on each side, inside each panel)
    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
    const int rowHeight = (area.getHeight() - gap) / 2;
    const int insideHeight = rowHeight - 12 - 12 - 26;

    // How wide a column is in each row, and the knob size that fits in all of them
    const auto columnWidthOf = [&] (const std::pair<size_t, size_t>& row)
    {
        size_t columns = 0;

        for (size_t i = row.first; i < row.first + row.second; ++i)
            columns += panels[i].size();

        return (float) (area.getWidth() - gap * ((int) row.second - 1) - panelPadding * (int) row.second) / (float) columns;
    };

    int knobSize = insideHeight - nameHeight - spacing - valueBoxHeight;

    for (auto& row : rows)
        knobSize = juce::jmin (knobSize, (int) columnWidthOf (row));

    for (size_t r = 0; r < rows.size(); ++r)
    {
        auto rowArea = r == 0 ? area.removeFromTop (rowHeight) : area.withTrimmedTop (gap);
        const float columnWidth = columnWidthOf (rows[r]);

        for (size_t i = rows[r].first; i < rows[r].first + rows[r].second; ++i)
        {
            const bool last = i + 1 == rows[r].first + rows[r].second;
            const int numColumns = (int) panels[i].size();
            const int width = last ? rowArea.getWidth() : juce::roundToInt (columnWidth * (float) numColumns) + panelPadding;
            groupPanels[i] = rowArea.removeFromLeft (width);
            rowArea.removeFromLeft (gap);

            auto inside = groupPanels[i].reduced (panelPadding / 2, 12).withTrimmedTop (26);
            const int thisColumnWidth = inside.getWidth() / numColumns;

            for (auto& column : panels[i])
            {
                // Each column is: name, then the knob and its value box (or the switch) - kept together and centred.
                auto columnArea = inside.removeFromLeft (thisColumnWidth)
                                        .withSizeKeepingCentre (thisColumnWidth, nameHeight + spacing + knobSize + valueBoxHeight);

                if (column.knob != nullptr)
                {
                    column.knob->label.setBounds (columnArea.removeFromTop (nameHeight));
                    columnArea.removeFromTop (spacing);
                    column.knob->slider.setBounds (columnArea);   // the knob, with its value box underneath
                }
                else
                {
                    column.choice->label.setBounds (columnArea.removeFromTop (nameHeight));
                    columnArea.removeFromTop (spacing + 6);
                    column.choice->choices.setBounds (columnArea.withSizeKeepingCentre (juce::jmin (columnArea.getWidth() - 12, 122), 76));
                }
            }
        }
    }
}

//==============================================================================
void EasyCompAudioProcessorEditor::refreshLiveDisplays()
{
    const int slot = easyProcessor.getActiveSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "COPY A > B" : "COPY B > A");

    const auto settings = compProcessor.getCurrentSettings();
    const float autoGainDb = compProcessor.getAutoGainDb();

    meter.setGainReduction (compProcessor.takeGainReductionForMeter());
    graph.update (settings, compProcessor.takeInputLevelForGraph(), autoGainDb);
    graph.setSidechainStatus (compProcessor.usesSidechain(), compProcessor.isSidechainMissing());

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

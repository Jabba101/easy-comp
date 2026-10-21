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

    // In on-screen order (see knobGroups below for which panel each is in).
    const std::array<KnobInfo, 4> knobInfo
    {{
        { DSIDs::from, "FROM",
          "Where the harsh band starts. S sounds are usually between 4 and 10 kHz: lower for deep voices, higher for "
          "bright ones. Set it too low and it starts catching ordinary words. Not sure? Click FIND." },

        { DSIDs::to, "TO",
          "Where the harsh band ends. OFF (the usual) means everything above FROM. Set it to aim at one band only "
          "(e.g. 5-9 kHz) and leave the air above it alone. Not sure? Click FIND." },

        { DSIDs::threshold, "THRESHOLD",
          "How loud the harsh band may get compared with the rest of your sound before it's turned down. Watch the "
          "display: set it so the amber line goes over the dashed line on the S sounds only, not on ordinary words." },

        { DSIDs::range, "RANGE",
          "The most it turns down. 3-6 dB sounds natural; 8-12 dB is firm. Too much and S sounds start to lisp (like "
          "'th') - if they do, lower RANGE." },
    }};

    // The three panels, numbered in the order you'd usually set them.
    struct KnobGroup
    {
        const char* name;
        size_t firstKnob, numKnobs;
        int numColumns;
    };

    const std::array<KnobGroup, 3> knobGroups
    {{
        { "WHERE",      0, 2, 3 },   // (the third column: FIND)
        { "HOW MUCH",   2, 2, 2 },
        { "TURNS DOWN", 4, 0, 1 },   // (just the switch)
    }};

    constexpr const char* turnsDownHint =
        "HARSH BAND: only the harsh band dips during each S, so the rest of your sound is untouched - the most natural, "
        "and the usual choice. WHOLE SOUND: everything dips for the moment of each S, the classic style.";

    constexpr const char* findHint =
        "Play the part of your track with the most S sounds (or the harshest cymbals) and click FIND: it listens for "
        "8 seconds, then sets FROM and TO to where the harshness is. Click it again to stop sooner.";

    constexpr const char* listenHint =
        "Hear only what it's taking out, instead of your track - so you can check it's catching the S sounds and nothing "
        "else. It switches itself off when you close this window.";

    constexpr const char* displayHint =
        "The last 4 seconds: grey = your whole sound, amber = the harsh band, dashed line = THRESHOLD (it follows the "
        "rest of your sound), red = where it turned down. It turns down wherever the amber goes over the line.";

    constexpr const char* meterHint =
        "How far it's turning down right now. On a vocal, 3-6 dB on the S sounds is natural; much more than 10 dB starts "
        "to sound lispy. It should drop back to 0 between the S sounds.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, play your track and watch the display: set THRESHOLD so the amber line goes over the dashed line "
        "on the S sounds only. Not sure where the harshness is? Click FIND. Hover over anything for an explanation.";

    /** How red the display shades a moment: none while untouched, fully at 12 dB down or more. */
    float shadeFor (float gainDb) noexcept   { return juce::jlimit (0.0f, 1.0f, -gainDb / 12.0f); }

    juce::String bandText (float fromHz, float toHz)
    {
        if (toHz >= DeEsser::toOffHz - 0.5f)
            return "everything above " + Format::hertz (fromHz);

        return Format::hertz (std::min (fromHz, toHz)) + " to " + Format::hertz (std::max (fromHz, toHz));
    }
}

//==============================================================================
// "What it's doing" display
//==============================================================================
DSDisplay::DSDisplay()
    : history ((size_t) numMoments)
{
    for (auto& moment : history)
        moment.gainDb = 0.0f;   // (nothing to show yet: no shading)
}

void DSDisplay::addMoments (const EasyDSAudioProcessor::Moment* newMoments, int count)
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

void DSDisplay::setThreshold (float newThresholdDb)
{
    if (std::abs (newThresholdDb - thresholdDb) > 0.001f)
    {
        thresholdDb = newThresholdDb;
        repaint();
    }
}

void DSDisplay::setStatus (bool isBypassed, bool isListening)
{
    if (isBypassed != bypassed || isListening != listening)
    {
        bypassed = isBypassed;
        listening = isListening;
        repaint();
    }
}

void DSDisplay::setFindMessage (const juce::String& title, const juce::String& text, float progress)
{
    if (title != findTitle || text != findText || std::abs (progress - findProgress) > 0.002f)
    {
        findTitle = title;
        findText = text;
        findProgress = progress;
        repaint();
    }
}

juce::Rectangle<float> DSDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (22.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

void DSDisplay::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();
    const auto yFor = [&] (float db) { return plot.getY() + (topDb - juce::jlimit (bottomDb - 6.0f, topDb, db)) / (topDb - bottomDb) * plot.getHeight(); };
    const float step = plot.getWidth() / (float) (numMoments - 1);
    const auto xFor = [&] (int age) { return plot.getRight() - (float) age * step; };   // age 0 = now
    const auto momentAt = [&] (int age) -> const EasyDSAudioProcessor::Moment&
    {
        return history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
    };

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot.expanded (1.0f), 6.0f);

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());

        // Red wherever it turned the sound down (the further down, the redder),
        // drawn in stretches of the same shade
        for (int age = 0; age < numMoments;)
        {
            const float shade = shadeFor (momentAt (age).gainDb);
            int end = age + 1;

            while (end < numMoments && std::abs (shadeFor (momentAt (end).gainDb) - shade) < 0.04f)
                ++end;

            if (shade > 0.02f)
            {
                g.setColour (Palette::meterHigh.withAlpha (0.32f * shade));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (end - 1) - step * 0.5f, plot.getY(),
                                                                         xFor (age) + step * 0.5f, plot.getBottom()));
            }

            age = end;
        }
    }

    // Grid and scale, every 12 dB
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = (int) topDb; (float) db > bottomDb - 0.5f; db -= 12)
    {
        const float y = yFor ((float) db);

        if (db < (int) topDb && (float) db > bottomDb + 0.5f)
        {
            g.setColour (Palette::panelEdge);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }

        g.setColour (Palette::textDim);
        g.drawText (juce::String (db), juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 6.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());

        // The whole sound, as a grey shape; the harsh band, as an amber line;
        // the THRESHOLD line, dashed, following the rest of the sound
        juce::Path sound, band, line;
        sound.startNewSubPath (plot.getBottomRight());

        for (int age = 0; age < numMoments; ++age)
        {
            const auto& moment = momentAt (age);
            const float x = xFor (age);
            sound.lineTo (x, yFor (moment.soundDb));

            const float bandY = yFor (moment.bandDb), lineY = yFor (DeEsser::thresholdLineDb (moment.lineDb, thresholdDb));

            if (age == 0)
            {
                band.startNewSubPath (x, bandY);
                line.startNewSubPath (x, lineY);
            }
            else
            {
                band.lineTo (x, bandY);
                line.lineTo (x, lineY);
            }
        }

        sound.lineTo (plot.getBottomLeft());
        sound.closeSubPath();

        g.setColour (Palette::textDim.withAlpha (0.34f));
        g.fillPath (sound);

        juce::Path dashed;
        const float dashes[] = { 7.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Palette::textBright.withAlpha (0.75f));
        g.fillPath (dashed);

        g.setColour (Palette::accent);
        g.strokePath (band, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // THRESHOLD's setting, beside where the line is now (on a dark patch, so it reads over anything)
        const float y = yFor (DeEsser::thresholdLineDb (momentAt (0).lineDb, thresholdDb));
        const bool roomAbove = y - plot.getY() > 22.0f;
        const juce::Font labelFont (juce::FontOptions (11.5f, juce::Font::bold));
        const auto label = "THRESHOLD " + Format::signedDecibels (thresholdDb);
        const float labelWidth = juce::GlyphArrangement::getStringWidth (labelFont, label) + 12.0f;
        const auto labelArea = juce::Rectangle<float> (plot.getRight() - 8.0f - labelWidth, roomAbove ? y - 20.0f : y + 4.0f, labelWidth, 16.0f);
        g.setColour (Palette::background.withAlpha (0.8f));
        g.fillRoundedRectangle (labelArea, 4.0f);
        g.setColour (Palette::textBright.withAlpha (0.9f));
        g.setFont (labelFont);
        g.drawText (label, labelArea, juce::Justification::centred, false);
    }

    // Along the bottom: the time, and a small key
    auto bottom = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 5.0f, plot.getWidth(), 14.0f);
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("4 seconds ago", bottom.removeFromLeft (120.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", bottom.removeFromRight (60.0f), juce::Justification::centredRight, false);

    {
        const juce::Font keyFont (juce::FontOptions (12.0f));
        struct KeyItem { juce::Colour colour; float swatchHeight; bool dashed; const char* text; };
        const KeyItem items[] = { { Palette::textDim.withAlpha (0.6f),     10.0f, false, "your sound" },
                                  { Palette::accent,                        2.5f, false, "the harsh band" },
                                  { Palette::meterHigh.withAlpha (0.6f),   10.0f, false, "turned down" },
                                  { Palette::textBright.withAlpha (0.75f),  1.5f, true,  "threshold" } };

        float total = 0.0f;
        for (auto& item : items)
            total += 12.0f + 6.0f + juce::GlyphArrangement::getStringWidth (keyFont, item.text) + 16.0f;

        auto key = bottom.withSizeKeepingCentre (total, bottom.getHeight());
        g.setFont (keyFont);

        for (auto& item : items)
        {
            const auto swatch = key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, item.swatchHeight);
            g.setColour (item.colour);

            if (item.dashed)
            {
                g.fillRect (swatch.withWidth (5.0f));
                g.fillRect (swatch.withTrimmedLeft (7.0f));
            }
            else
            {
                g.fillRect (swatch);
            }

            key.removeFromLeft (6.0f);
            const float width = juce::GlyphArrangement::getStringWidth (keyFont, item.text);
            g.setColour (Palette::textDim);
            g.drawText (item.text, key.removeFromLeft (width + 2.0f), juce::Justification::centredLeft, false);
            key.removeFromLeft (14.0f);
        }
    }

    // What you're hearing, if it isn't the de-essed sound; and what FIND is up to
    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (plot);

        if (findTitle.isEmpty())
            drawBadge (g, plot, Palette::meterHigh, "BYPASSED", "You're hearing the original sound", -1.0f);
    }

    if (findTitle.isNotEmpty())
        drawBadge (g, plot, Palette::accent, findTitle, findText, findProgress);

    if (listening && ! bypassed)
    {
        const auto badge = juce::Rectangle<float> (plot.getX() + 10.0f, plot.getY() + 10.0f, 300.0f, 26.0f);
        g.setColour (Palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (badge.reduced (0.75f), 6.0f, 1.2f);
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText ("LISTENING: you hear only what it takes out", badge, juce::Justification::centred, false);
    }
}

void DSDisplay::drawBadge (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour,
                           const juce::String& title, const juce::String& text, float progress) const
{
    const juce::Font textFont (juce::FontOptions (12.5f));
    const float width = juce::jmax (260.0f, juce::GlyphArrangement::getStringWidth (textFont, text) + 40.0f);
    const float height = progress >= 0.0f ? 72.0f : 58.0f;
    const auto badge = juce::Rectangle<float> (juce::jmin (width, plot.getWidth() - 20.0f), height).withCentre (plot.getCentre());

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (badge, 8.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);

    auto lines = badge.reduced (10.0f, 9.0f);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (title, lines.removeFromTop (22.0f), juce::Justification::centred, false);
    g.setColour (Palette::textDim);
    g.setFont (textFont);
    g.drawText (text, lines.removeFromTop (18.0f), juce::Justification::centred, false);

    if (progress >= 0.0f)
    {
        // How far through its listening FIND is
        const auto bar = lines.withSizeKeepingCentre (juce::jmin (220.0f, lines.getWidth()), 6.0f).withY (lines.getY() + 6.0f);
        g.setColour (Palette::meterOff);
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 3.0f);
    }
}

//==============================================================================
// The Easy DS window
//==============================================================================
EasyDSAudioProcessorEditor::EasyDSAudioProcessorEditor (EasyDSAudioProcessor& p)
    : EasyWindow (p, "EASY DS", "Tame harsh S sounds and cymbals - hover over anything for help", gettingStartedHint),
      dsProcessor (p),
      turnsDownSwitch (*p.parameters.getParameter (DSIDs::turnsDown), { "HARSH BAND", "WHOLE SOUND" }, Palette::accent),
      incoming ((size_t) DSDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);

        if (juce::String (knobInfo[i].parameterID) == DSIDs::to)
            knob.slider.getProperties().set ("fillFromEnd", true);   // TO is off at the right-hand end

        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);

        // Connects the knob to its setting: from now on they move together.
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this] { knobMoved(); };

        addHint (knob.slider, knobInfo[i].name, knobInfo[i].hint);
        addHint (knob.label, knobInfo[i].name, knobInfo[i].hint);
    }

    // FIND, beside FROM and TO (the two it sets)
    findLabel.setText ("NOT SURE?", juce::dontSendNotification);
    findLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    findLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (findLabel);
    findButton.setClickingTogglesState (false);
    findButton.onClick = [this] { findingNow ? finishFinding() : startFinding(); };
    addAndMakeVisible (findButton);
    addHint (findButton, "FIND", findHint);
    addHint (findLabel, "FIND", findHint);

    // TURNS DOWN (its panel's name says it: "TURNS DOWN: HARSH BAND / WHOLE SOUND")
    addAndMakeVisible (turnsDownSwitch);
    turnsDownSwitch.onUserChange = [this] { knobMoved(); };
    addHint (turnsDownSwitch, "TURNS DOWN", turnsDownHint);

    // LISTEN, up in the header beside BYPASS (both change what you hear). It
    // isn't saved: it's only for while you set things up.
    listenButton.setToggleState (dsProcessor.isListening(), juce::dontSendNotification);
    listenButton.onClick = [this] { dsProcessor.setListening (listenButton.getToggleState()); };
    addAndMakeVisible (listenButton);
    putInHeader (listenButton);
    addHint (listenButton, "LISTEN", listenHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    addAndMakeVisible (meter);
    addHint (meter, "TURNED DOWN", meterHint);

    finishSetup (1100, 600);
}

EasyDSAudioProcessorEditor::~EasyDSAudioProcessorEditor()
{
    // Closing the window switches LISTEN and FIND off, so they can't be left on by accident.
    dsProcessor.setListening (false);
    dsProcessor.stopFinding();
}

//==============================================================================
void EasyDSAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The three panels, numbered in the order you'd usually set them
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), knobGroups[i].name);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
    drawPanel (g, meterPanel, {}, "TURNED DOWN");
}

void EasyDSAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // Along the top: the display, with the meter beside it
    auto top = area.removeFromTop (214);
    area.removeFromTop (gap);

    meterPanel = top.removeFromRight (150);
    meter.setBounds (meterPanel.reduced (22, 12).withTrimmedTop (26));
    top.removeFromRight (gap);
    displayPanel = top;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    // Along the bottom: the three panels, sized by how many columns each has
    int totalColumns = 0;
    for (auto& group : knobGroups)
        totalColumns += group.numColumns;

    constexpr int panelPadding = 20;   // (10 on each side, inside each panel)
    const float columnWidth = (float) (area.getWidth() - gap * ((int) knobGroups.size() - 1) - panelPadding * (int) knobGroups.size())
                            / (float) totalColumns;

    // Each column is: name, knob, value box - kept together and centred.
    constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;

    for (size_t group = 0; group < knobGroups.size(); ++group)
    {
        const bool last = group + 1 == knobGroups.size();
        const int width = last ? area.getWidth() : juce::roundToInt (columnWidth * (float) knobGroups[group].numColumns) + panelPadding;
        groupPanels[group] = area.removeFromLeft (width);
        area.removeFromLeft (gap);

        auto inside = groupPanels[group].reduced (panelPadding / 2, 12).withTrimmedTop (26);
        const int thisColumnWidth = inside.getWidth() / knobGroups[group].numColumns;
        const int knobSize = juce::jmin (thisColumnWidth, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        const int columnHeight = nameHeight + spacing + knobSize + valueBoxHeight;

        for (size_t k = 0; k < knobGroups[group].numKnobs; ++k)
        {
            auto& knob = knobs[knobGroups[group].firstKnob + k];
            auto columnArea = inside.removeFromLeft (thisColumnWidth).withSizeKeepingCentre (thisColumnWidth, columnHeight);

            knob.label.setBounds (columnArea.removeFromTop (nameHeight));
            columnArea.removeFromTop (spacing);
            knob.slider.setBounds (columnArea);   // the knob, with its value box underneath
        }

        if (group == 0)
        {
            // The spare column: FIND, lined up with the knobs beside it
            auto spare = inside.withSizeKeepingCentre (inside.getWidth(), columnHeight);
            findLabel.setBounds (spare.removeFromTop (nameHeight));
            spare.removeFromTop (spacing + 6);
            findButton.setBounds (spare.withSizeKeepingCentre (juce::jmin (spare.getWidth() - 4, 122), 34)
                                       .withY (spare.getY() + knobSize / 2 - 17));
        }

        if (last)
        {
            // TURNS DOWN: HARSH BAND / WHOLE SOUND, level with the knobs beside it
            const auto column = inside.withSizeKeepingCentre (inside.getWidth(), columnHeight);
            const int knobCentre = column.getY() + nameHeight + spacing + knobSize / 2;
            turnsDownSwitch.setBounds (juce::Rectangle<int> (juce::jmin (column.getWidth() - 4, 140), 72).withCentre ({ column.getCentreX(), knobCentre }));
        }
    }
}

//==============================================================================
void EasyDSAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = dsProcessor.getCurrentSettings();

    const int count = dsProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setThreshold (settings.thresholdDb);
    display.setStatus (settings.bypassed, dsProcessor.isListening());

    meter.setGainReduction (dsProcessor.takeReductionForMeter());

    if (listenButton.getToggleState() != dsProcessor.isListening())
        listenButton.setToggleState (dsProcessor.isListening(), juce::dontSendNotification);

    findStep();
}

//==============================================================================
// FIND
//==============================================================================
void EasyDSAudioProcessorEditor::startFinding()
{
    collected.clear();
    soundFrames = framesSinceSound = 0;
    findingNow = true;
    findButton.setToggleState (true, juce::dontSendNotification);
    dsProcessor.startFinding();
    display.setFindMessage ("FINDING THE HARSHNESS", "Keep your track playing...", 0.0f);
}

void EasyDSAudioProcessorEditor::findStep()
{
    if (findingNow)
    {
        // Collect what the audio has noted since last time
        arriving.resize (256);

        for (int n; (n = dsProcessor.takeFrames (arriving.data(), (int) arriving.size())) > 0;)
        {
            for (int i = 0; i < n; ++i)
            {
                collected.push_back (arriving[(size_t) i]);
                const bool sound = arriving[(size_t) i].total > 1.0e-6f;
                soundFrames += sound ? 1 : 0;
                framesSinceSound = sound ? 0 : framesSinceSound + 1;
            }
        }

        const int needed = (int) (findSeconds / HarshnessFinder::frameSeconds);

        if (soundFrames >= needed)
        {
            finishFinding();
            return;
        }

        const float progress = (float) soundFrames / (float) needed;
        const bool waiting = collected.empty() || framesSinceSound > 50;   // (silent for half a second)

        display.setFindMessage ("FINDING THE HARSHNESS",
                                waiting ? juce::String ("Waiting for sound: press play in your DAW")
                                        : "Keep your track playing... " + juce::String ((int) std::ceil ((1.0f - progress) * (float) findSeconds)) + " s",
                                progress);
        return;
    }

    if (messageUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > messageUntil)
    {
        messageUntil = 0.0;
        display.setFindMessage ({}, {});
    }
}

void EasyDSAudioProcessorEditor::finishFinding()
{
    if (! findingNow)
        return;

    findingNow = false;
    dsProcessor.stopFinding();
    findButton.setToggleState (false, juce::dontSendNotification);

    // (Anything noted in the last moment counts too.)
    arriving.resize (256);
    for (int n; (n = dsProcessor.takeFrames (arriving.data(), (int) arriving.size())) > 0;)
        collected.insert (collected.end(), arriving.begin(), arriving.begin() + n);

    const auto result = applyFind (collected);
    collected.clear();

    using Outcome = HarshnessFinder::Result::Outcome;

    if (result.outcome == Outcome::found)
        display.setFindMessage ("FOUND IT: " + bandText (result.fromHz, result.toHz).toUpperCase().replace ("KHZ", "kHz").replace ("HZ", "Hz"),
                                "FROM and TO are set. Now set THRESHOLD so only the S sounds go over the line.");
    else if (result.outcome == Outcome::nothingStoodOut)
        display.setFindMessage ("NOTHING HARSH STOOD OUT",
                                "FROM and TO weren't changed. Try a part with more S sounds (or harsher cymbals).");
    else
        display.setFindMessage ("NOT ENOUGH SOUND",
                                "Click FIND while your track is playing: it listens for 8 seconds.");

    messageUntil = juce::Time::getMillisecondCounterHiRes() + 7000.0;
}

HarshnessFinder::Result EasyDSAudioProcessorEditor::applyFind (const std::vector<HarshnessFinder::Frame>& frames)
{
    const auto result = HarshnessFinder::analyse (frames, [this] (int band) { return dsProcessor.canFindBand (band); });

    if (result.outcome == HarshnessFinder::Result::Outcome::found)
    {
        setParameter (DSIDs::from, result.fromHz);
        setParameter (DSIDs::to, result.toHz);
        knobMoved();
    }

    return result;
}

void EasyDSAudioProcessorEditor::setParameter (const char* id, float value)
{
    if (auto* parameter = easyProcessor.parameters.getParameter (id))
    {
        // (As one move, so your DAW can undo it.)
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
        parameter->endChangeGesture();
    }
}

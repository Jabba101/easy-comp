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
        { GateIDs::threshold, "THRESHOLD",
          "The level that opens the gate. Watch the display: set the line above the noise or bleed between the hits, "
          "and below the hits themselves. It closes again a little under the line, once HOLD has passed." },

        { GateIDs::range, "RANGE",
          "How much quieter it gets while closed. 10-20 dB tidies up and still sounds natural (a good start on toms and "
          "snare); 30-40 dB removes nearly everything; FULL is complete silence." },

        { GateIDs::attack, "ATTACK",
          "How quickly it opens. It looks 2 ms ahead, so anything under 2 ms lets the very start of each hit through "
          "untouched. Slower softens the start - try 2-10 ms if a bass or vocal clicks as it opens." },

        { GateIDs::hold, "HOLD",
          "How long it stays fully open after the sound drops under the line, before it starts to close. Longer stops it "
          "chattering on and off during a note; shorter keeps it tight between quick hits." },

        { GateIDs::release, "RELEASE",
          "How long it takes to close. Short = tight and dry (drums in fast songs); long lets each hit or note ring out "
          "naturally. Too short chops the tails off abruptly." },

        { GateIDs::lowCut, "IGNORE BELOW",
          "Stops low sounds from opening the gate (it never changes your sound itself). On a snare, 150-250 Hz stops kick "
          "bleed opening it; on a vocal, 100 Hz ignores rumble. LISTEN lets you hear the difference." },

        { GateIDs::highCut, "IGNORE ABOVE",
          "Stops high sounds from opening the gate (it never changes your sound itself). On toms or kick, 2-5 kHz ignores "
          "cymbals and hi-hat. Much lower and it opens late, softening the start of each hit. LISTEN lets you hear it." },
    }};

    // The three knob panels, numbered in the order you'd usually set them.
    struct KnobGroup
    {
        const char* name;
        size_t firstKnob, numKnobs;
        int numColumns;
    };

    const std::array<KnobGroup, 3> knobGroups
    {{
        { "HOW MUCH",           0, 2, 2 },
        { "HOW FAST",           2, 3, 3 },
        { "WHAT IT LISTENS TO", 5, 2, 3 },   // (the third column: OPENED BY)
    }};

    constexpr const char* openedByHint =
        "THIS TRACK: the gate listens to your track itself (the usual). SIDECHAIN: it listens to another track that your DAW "
        "sends to its sidechain input instead - e.g. the snare's close mic opening the room mics.";

    constexpr const char* listenHint =
        "Hear what the gate is listening to, with IGNORE BELOW and ABOVE applied, instead of your track - so you can set "
        "them by ear. It switches itself off when you close this window.";

    constexpr const char* displayHint =
        "The last 4 seconds: grey = what the gate hears, amber = what comes out, dashed line = THRESHOLD, red = where it "
        "turned the sound down. The gate opens wherever the grey goes over the line.";

    constexpr const char* lightHint =
        "Lights up while the gate is open and dims as it closes. Flickering on noise or bleed? Raise THRESHOLD or use "
        "IGNORE BELOW / ABOVE. Missing quiet hits? Lower THRESHOLD.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, play your track and watch the display: set THRESHOLD so the hits go over the line and the noise "
        "or bleed between them stays under it. Hover over anything for an explanation.";

    /** How red the display shades a moment: none while open, fully at 30 dB down or more. */
    float shadeFor (float gainDb) noexcept   { return juce::jlimit (0.0f, 1.0f, -gainDb / 30.0f); }
}

//==============================================================================
// "What it's doing" display
//==============================================================================
GateDisplay::GateDisplay()
    : history ((size_t) numMoments)
{
    for (auto& moment : history)
        moment.gainDb = 0.0f;   // (nothing to show yet: no shading)
}

void GateDisplay::addMoments (const EasyGateAudioProcessor::Moment* newMoments, int count)
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

void GateDisplay::setThreshold (float newThresholdDb)
{
    if (std::abs (newThresholdDb - thresholdDb) > 0.001f)
    {
        thresholdDb = newThresholdDb;
        repaint();
    }
}

void GateDisplay::setStatus (bool isBypassed, bool isListening, bool isSidechainMissing)
{
    if (isBypassed != bypassed || isListening != listening || isSidechainMissing != sidechainMissing)
    {
        bypassed = isBypassed;
        listening = isListening;
        sidechainMissing = isSidechainMissing;
        repaint();
    }
}

juce::Rectangle<float> GateDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (22.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

float GateDisplay::getThresholdY() const
{
    const auto plot = getPlotArea();
    return plot.getY() + (topDb - juce::jlimit (bottomDb, topDb, thresholdDb)) / (topDb - bottomDb) * plot.getHeight();
}

void GateDisplay::paint (juce::Graphics& g)
{
    const auto plot = getPlotArea();
    const auto yFor = [&] (float db) { return plot.getY() + (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb) * plot.getHeight(); };
    const float step = plot.getWidth() / (float) (numMoments - 1);
    const auto xFor = [&] (int age) { return plot.getRight() - (float) age * step; };   // age 0 = now
    const auto momentAt = [&] (int age) -> const EasyGateAudioProcessor::Moment&
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
                g.setColour (Palette::meterHigh.withAlpha (0.30f * shade));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xFor (end - 1) - step * 0.5f, plot.getY(),
                                                                         xFor (age) + step * 0.5f, plot.getBottom()));
            }

            age = end;
        }
    }

    // Grid and scale, every 12 dB
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int db = (int) topDb; (float) db > bottomDb; db -= 12)
    {
        const float y = yFor ((float) db);

        if (db < (int) topDb)
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

        // What the gate hears, as a grey shape; what comes out, as an amber line
        juce::Path heard, out;
        heard.startNewSubPath (plot.getBottomRight());

        for (int age = 0; age < numMoments; ++age)
        {
            const auto& moment = momentAt (age);
            const float x = xFor (age);
            heard.lineTo (x, yFor (moment.keyDb));

            if (age == 0)
                out.startNewSubPath (x, yFor (moment.outDb));
            else
                out.lineTo (x, yFor (moment.outDb));
        }

        heard.lineTo (plot.getBottomLeft());
        heard.closeSubPath();

        g.setColour (Palette::textDim.withAlpha (0.38f));
        g.fillPath (heard);

        g.setColour (Palette::accent);
        g.strokePath (out, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // THRESHOLD: the gate opens wherever the grey goes over this line
        const float y = yFor (thresholdDb);
        const float dashes[] = { 7.0f, 4.0f };
        g.setColour (Palette::accent.withAlpha (0.9f));
        g.drawDashedLine ({ plot.getX(), y, plot.getRight(), y }, dashes, 2, 1.5f);

        const bool roomAbove = y - plot.getY() > 18.0f;
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText ("THRESHOLD " + juce::String (thresholdDb, 1) + " dB",
                    juce::Rectangle<float> (plot.getRight() - 180.0f, roomAbove ? y - 17.0f : y + 3.0f, 172.0f, 14.0f),
                    juce::Justification::centredRight, false);
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
        const KeyItem items[] = { { Palette::textDim.withAlpha (0.6f),  10.0f, false, "what it hears" },
                                  { Palette::accent,                     2.5f, false, "what comes out" },
                                  { Palette::meterHigh.withAlpha (0.6f), 10.0f, false, "turned down" },
                                  { Palette::accent,                     1.5f, true,  "threshold" } };

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

    // What you're hearing, if it isn't the gated sound
    if (bypassed)
    {
        g.setColour (Palette::background.withAlpha (0.65f));
        g.fillRect (plot);
        drawBadge (g, plot, Palette::meterHigh, "BYPASSED", "You're hearing the original sound");
    }
    else if (sidechainMissing)
    {
        drawBadge (g, plot, Palette::meterHigh, "NOTHING IS COMING INTO THE SIDECHAIN",
                   "Send a track to Easy Gate's sidechain in your DAW, or set OPENED BY to THIS TRACK");
    }

    if (listening && ! bypassed)
    {
        const auto badge = juce::Rectangle<float> (plot.getX() + 10.0f, plot.getY() + 10.0f, 290.0f, 26.0f);
        g.setColour (Palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (badge, 6.0f);
        g.setColour (Palette::accent);
        g.drawRoundedRectangle (badge.reduced (0.75f), 6.0f, 1.2f);
        g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
        g.drawText ("LISTENING: you hear what the gate hears", badge, juce::Justification::centred, false);
    }
}

void GateDisplay::drawBadge (juce::Graphics& g, juce::Rectangle<float> plot, juce::Colour colour,
                             const juce::String& title, const juce::String& text) const
{
    const juce::Font textFont (juce::FontOptions (12.5f));
    const float width = juce::jmax (230.0f, juce::GlyphArrangement::getStringWidth (textFont, text) + 40.0f);
    const auto badge = juce::Rectangle<float> (juce::jmin (width, plot.getWidth() - 20.0f), 58.0f).withCentre (plot.getCentre());

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (badge, 8.0f);
    g.setColour (colour);
    g.drawRoundedRectangle (badge.reduced (0.75f), 8.0f, 1.5f);

    auto lines = badge.reduced (10.0f, 9.0f);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (title, lines.removeFromTop (lines.getHeight() * 0.55f), juce::Justification::centred, false);
    g.setColour (Palette::textDim);
    g.setFont (textFont);
    g.drawText (text, lines, juce::Justification::centred, false);
}

//==============================================================================
// The light
//==============================================================================
void GateLight::setState (bool wasOpen, float gainDb, float rangeDb, bool isBypassed)
{
    float newBrightness = 0.0f;
    juce::String newWord, newDetail;

    const bool full = rangeDb >= Gate::fullRangeDb - 0.05f;

    if (isBypassed)
    {
        newWord = "BYPASSED";
        newDetail = "the original sound";
    }
    else if (wasOpen)
    {
        newBrightness = 1.0f;
        newWord = "OPEN";
        newDetail = "letting it through";
    }
    else
    {
        // Dimming as it closes: dark once it's 40 dB down (or all of RANGE, if that's less)
        const float closedDb = full ? Gate::silenceDb : -rangeDb;
        const float depth = juce::jmin (full ? 40.0f : rangeDb, 40.0f);
        newBrightness = depth > 0.0f ? juce::jlimit (0.0f, 1.0f, 1.0f + gainDb / depth) : 0.0f;
        newWord = gainDb > closedDb + 0.5f ? "CLOSING" : "CLOSED";
        newDetail = rangeDb < 0.05f ? juce::String ("RANGE 0 dB: no change")
                                    : (full ? juce::String ("silent") : "turned down " + juce::String (juce::roundToInt (rangeDb)) + " dB");
    }

    if (std::abs (newBrightness - brightness) > 0.01f || newWord != word || newDetail != detail)
    {
        brightness = newBrightness;
        word = newWord;
        detail = newDetail;
        repaint();
    }
}

void GateLight::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const float diameter = juce::jmin (area.getWidth() - 50.0f, area.getHeight() * 0.42f, 72.0f);
    const auto lamp = juce::Rectangle<float> (diameter, diameter).withCentre ({ area.getCentreX(), area.getY() + 16.0f + diameter * 0.5f });

    // A soft glow when lit
    if (brightness > 0.0f)
    {
        g.setColour (Palette::meterLow.withAlpha (0.20f * brightness));
        g.fillEllipse (lamp.expanded (7.0f));
        g.setColour (Palette::meterLow.withAlpha (0.10f * brightness));
        g.fillEllipse (lamp.expanded (13.0f));
    }

    g.setColour (Palette::meterOff.interpolatedWith (Palette::meterLow, brightness));
    g.fillEllipse (lamp);
    g.setColour (brightness > 0.5f ? Palette::meterLow : Palette::panelEdge.brighter (0.3f));
    g.drawEllipse (lamp.reduced (0.75f), 1.5f);

    auto text = area.withTop (lamp.getBottom() + 14.0f);
    const auto wordColour = word == "OPEN" ? Palette::meterLow
                          : (word == "CLOSING" ? Palette::accent
                          : (word == "BYPASSED" ? Palette::meterHigh : Palette::textDim));
    g.setColour (wordColour);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText (word, text.removeFromTop (22.0f), juce::Justification::centred, false);

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawFittedText (detail, text.removeFromTop (18.0f).toNearestInt(), juce::Justification::centred, 1, 0.85f);
}

//==============================================================================
// The Easy Gate window
//==============================================================================
EasyGateAudioProcessorEditor::EasyGateAudioProcessorEditor (EasyGateAudioProcessor& p)
    : EasyWindow (p, "EASY GATE", "Silence the noise and bleed between hits - hover over anything for help", gettingStartedHint),
      gateProcessor (p),
      openedBySwitch (*p.parameters.getParameter (GateIDs::openedBy), { "THIS TRACK", "SIDECHAIN" }, Palette::accent),
      incoming ((size_t) GateDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);

        if (juce::String (knobInfo[i].parameterID) == GateIDs::highCut)
            knob.slider.getProperties().set ("fillFromEnd", true);   // IGNORE ABOVE is off at the right-hand end

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

    // OPENED BY (not part of presets: it depends on how your tracks are routed)
    openedByLabel.setText ("OPENED BY", juce::dontSendNotification);
    openedByLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    openedByLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (openedByLabel);
    addAndMakeVisible (openedBySwitch);
    addHint (openedByLabel, "OPENED BY", openedByHint);
    addHint (openedBySwitch, "OPENED BY", openedByHint);

    // LISTEN, up in the header beside BYPASS (both change what you hear). It
    // isn't saved: it's only for while you set things up.
    listenButton.setToggleState (gateProcessor.isListening(), juce::dontSendNotification);
    listenButton.onClick = [this] { gateProcessor.setListening (listenButton.getToggleState()); };
    addAndMakeVisible (listenButton);
    putInHeader (listenButton);
    addHint (listenButton, "LISTEN", listenHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    addAndMakeVisible (light);
    addHint (light, "GATE", lightHint);

    finishSetup (1100, 600);
}

EasyGateAudioProcessorEditor::~EasyGateAudioProcessorEditor()
{
    // Closing the window switches LISTEN off, so it can't be left on by accident.
    gateProcessor.setListening (false);
}

//==============================================================================
void EasyGateAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The three knob panels, numbered in the order you'd usually set them
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), knobGroups[i].name);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
    drawPanel (g, lightPanel, {}, "GATE");
}

void EasyGateAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // Along the top: the display, with the light beside it
    auto top = area.removeFromTop (214);
    area.removeFromTop (gap);

    lightPanel = top.removeFromRight (150);
    light.setBounds (lightPanel.reduced (12, 12).withTrimmedTop (24));
    top.removeFromRight (gap);
    displayPanel = top;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    // Along the bottom: the three knob panels, sized by how many columns each has
    int totalColumns = 0;
    for (auto& group : knobGroups)
        totalColumns += group.numColumns;

    constexpr int panelPadding = 20;   // (10 on each side, inside each panel)
    const float columnWidth = (float) (area.getWidth() - gap * ((int) knobGroups.size() - 1) - panelPadding * (int) knobGroups.size())
                            / (float) totalColumns;

    for (size_t group = 0; group < knobGroups.size(); ++group)
    {
        const bool last = group + 1 == knobGroups.size();
        const int width = last ? area.getWidth() : juce::roundToInt (columnWidth * (float) knobGroups[group].numColumns) + panelPadding;
        groupPanels[group] = area.removeFromLeft (width);
        area.removeFromLeft (gap);

        auto inside = groupPanels[group].reduced (panelPadding / 2, 12).withTrimmedTop (26);
        const int thisColumnWidth = inside.getWidth() / knobGroups[group].numColumns;

        // Each column is: name, knob, value box - kept together and centred.
        constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
        const int knobSize = juce::jmin (thisColumnWidth, inside.getHeight() - nameHeight - spacing - valueBoxHeight);

        for (size_t k = 0; k < knobGroups[group].numKnobs; ++k)
        {
            auto& knob = knobs[knobGroups[group].firstKnob + k];
            auto columnArea = inside.removeFromLeft (thisColumnWidth)
                                    .withSizeKeepingCentre (thisColumnWidth, nameHeight + spacing + knobSize + valueBoxHeight);

            knob.label.setBounds (columnArea.removeFromTop (nameHeight));
            columnArea.removeFromTop (spacing);
            knob.slider.setBounds (columnArea);   // the knob, with its value box underneath
        }

        if (last)
        {
            // The spare column: OPENED BY - its name, then THIS TRACK / SIDECHAIN,
            // lined up with the knobs beside it.
            auto spare = inside.withSizeKeepingCentre (inside.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);
            openedByLabel.setBounds (spare.removeFromTop (nameHeight));
            spare.removeFromTop (spacing + 6);
            openedBySwitch.setBounds (spare.withSizeKeepingCentre (juce::jmin (spare.getWidth() - 4, 122), 72));
        }
    }
}

//==============================================================================
void EasyGateAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = gateProcessor.getCurrentSettings();

    const int count = gateProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setThreshold (settings.thresholdDb);
    display.setStatus (settings.bypassed, gateProcessor.isListening(), gateProcessor.isSidechainMissing());

    light.setState (gateProcessor.takeOpenForLight(), gateProcessor.takeGainForLight(), settings.rangeDb, settings.bypassed);

    if (listenButton.getToggleState() != gateProcessor.isListening())
        listenButton.setToggleState (gateProcessor.isListening(), juce::dontSendNotification);
}

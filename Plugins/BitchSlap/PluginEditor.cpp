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

    const std::array<KnobInfo, 6> knobInfo
    {{
        { SlapIDs::mix, "MIX",
          "How much reverb. Up to 50% it only adds reverb (with BITCH-SLAP, at 50% the burst is about as loud as a snare). Above "
          "50% it also turns your original sound down, until 100% = reverb only: use that on an FX send or bus." },

        { SlapIDs::length, "LENGTH",
          "How long each burst lasts, from the hit until it slams shut. Short (100-200 ms) = tight and punchy, for fast songs. "
          "Long (400-700 ms) = the huge 80s sound, for slower ones. Tip: end the burst before the next snare hit." },

        { SlapIDs::note, "LENGTH",
          "How long each burst lasts, as a note length at your song's tempo - it follows tempo changes. 1/8 note = an eighth "
          "note. Triplets are a third shorter than the plain note, dotted notes half as long again." },

        { SlapIDs::decay, "DECAY",
          "How long the reverb takes to die away. Rooms: about 0.4 - 1 s. Clubs: 1 - 1.5 s. Halls: 1.5 - 2.5 s. Longer for big, "
          "washy pads. The highs always fade sooner than the lows, as in a real space." },

        { SlapIDs::predelay, "PRE-DELAY",
          "A short gap before the reverb starts. 0 = the sound and the space arrive together (far from the stage). 10 - 40 ms "
          "keeps vocals and snares clear and upfront, with the space behind them." },

        { SlapIDs::tone, "TONE",
          "Darker (below 0): the tail loses its highs quickly - a room full of people, curtains, wood. Brighter (above 0): "
          "hard walls, a glassy plate. 0 is a natural middle." },
    }};

    const std::array<const char*, 3> groupNames { "WHAT KIND", "HOW MUCH", "HOW LONG" };

    /** What each TYPE is, shown under the switch. */
    const std::array<const char*, 5> typeDescriptions
    {{
        "The gated burst: every hit gets a huge reverb that stops dead.",
        "A rehearsal or drum room: close walls, a short, lively tail.",
        "A small gig venue: a fuller tail - the band on stage.",
        "A theatre or concert hall: far walls, a long, smooth tail.",
        "The 60s studio plate: bright and silky, no room at all.",
    }};

    constexpr const char* typeHint =
        "What kind of reverb. BITCH-SLAP: the gated 80s burst. ROOM, CLUB and HALL: the live spaces - a band playing in a real "
        "place, bigger as you go down. PLATE: the classic studio plate, for the odd vocal or snare.";

    constexpr const char* initAllHint =
        "Sets everything back to how it starts (the Default settings preset).";

    constexpr const char* syncHint =
        "OFF: set LENGTH in milliseconds. TEMPO: set it as a note length (1/8, 1/4...) that follows your song's tempo, so "
        "every burst stops in time with the beat.";

    constexpr const char* lengthInfoHint =
        "How LENGTH fits your song: as a note length at the song's tempo, or (with SYNC on TEMPO) in milliseconds.";

    constexpr const char* displayHint =
        "The last couple of seconds: grey = your sound going in, amber = the reverb. BITCH-SLAP: every hit gets a burst that "
        "stops dead after LENGTH. The spaces: a tail fading away over DECAY. The bar in the top corner shows how long. The "
        "scale moves to fit your track.";

    constexpr const char* gettingStartedHint =
        "Pick a TYPE (or a preset), then turn up MIX until the reverb sits where you like it, and set how long it lasts. "
        "Hover over anything for an explanation. Double-click a knob to reset it.";
}

//==============================================================================
// "What it's doing" display
//==============================================================================
BurstDisplay::BurstDisplay()
    : history ((size_t) numMoments)
{
}

void BurstDisplay::addMoments (const BitchSlapAudioProcessor::Moment* newMoments, int count)
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

void BurstDisplay::fitScale()
{
    // Keep the loudest moment of the last couple of seconds 12-21 dB below the
    // top edge - about a quarter of the way down - whatever the track's level.
    constexpr float headroomDb = 12.0f, stepDb = 6.0f;

    float loudest = -100.0f;
    for (auto& moment : history)
        loudest = std::max ({ loudest, moment.inDb, moment.wetDb });

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

void BurstDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

void BurstDisplay::setLength (float newLengthMs, const juce::String& name)
{
    if (std::abs (newLengthMs - lengthMs) > 0.25f || name != lengthName)
    {
        lengthMs = newLengthMs;
        lengthName = name;
        repaint();
    }
}

juce::Rectangle<float> BurstDisplay::getPlotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (20.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

void BurstDisplay::paint (juce::Graphics& g)
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

    // Your sound as a grey shape, the reverb as an amber one on top
    juce::Path dry, wet, wetEdge;
    dry.startNewSubPath (plot.getBottomRight());
    wet.startNewSubPath (plot.getBottomRight());

    for (int age = 0; age < numMoments; ++age)
    {
        const auto& moment = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xFor (age);
        dry.lineTo (x, yFor (moment.inDb));
        wet.lineTo (x, yFor (moment.wetDb));

        if (age == 0)
            wetEdge.startNewSubPath (x, yFor (moment.wetDb));
        else
            wetEdge.lineTo (x, yFor (moment.wetDb));
    }

    dry.lineTo (plot.getBottomLeft());
    dry.closeSubPath();
    wet.lineTo (plot.getBottomLeft());
    wet.closeSubPath();

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());

        g.setColour (Palette::textDim.withAlpha (0.35f));
        g.fillPath (dry);

        g.setColour (Palette::accent.withAlpha (0.30f));
        g.fillPath (wet);
        g.setColour (Palette::accent);
        g.strokePath (wetEdge, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    }

    // A small key, top left
    auto key = plot.reduced (10.0f).removeFromTop (16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));

    const auto keyItem = [&] (juce::Colour colour, const juce::String& text, float textWidth)
    {
        g.setColour (colour);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (text, key.removeFromLeft (textWidth), juce::Justification::centredLeft, false);
        key.removeFromLeft (10.0f);
    };

    keyItem (Palette::textDim.withAlpha (0.6f), "your sound", 66.0f);
    keyItem (Palette::accent.withAlpha (0.8f), "reverb", 50.0f);

    // How long LENGTH is, on the same time scale, top right
    {
        const float width = juce::jmin (plot.getWidth() - 20.0f, lengthMs / (momentMs * (float) (numMoments - 1)) * plot.getWidth());
        const float right = plot.getRight() - 10.0f, y = plot.getY() + 30.0f;
        const auto bar = juce::Rectangle<float> (right - width, y - 4.0f, width, 8.0f);

        g.setColour (Palette::accent.withAlpha (0.85f));
        g.drawHorizontalLine (juce::roundToInt (y), bar.getX(), bar.getRight());
        g.drawVerticalLine (juce::roundToInt (bar.getX()), bar.getY(), bar.getBottom());
        g.drawVerticalLine (juce::roundToInt (bar.getRight()) - 1, bar.getY(), bar.getBottom());

        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.setColour (Palette::textDim);
        g.drawText (lengthName + " " + (lengthMs >= 1000.0f ? juce::String (lengthMs / 1000.0f, 2) + " s" : Format::milliseconds (lengthMs)), juce::Rectangle<float> (right - 200.0f, y - 22.0f, 200.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

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
// The Easy Verb window
//==============================================================================
BitchSlapAudioProcessorEditor::BitchSlapAudioProcessorEditor (BitchSlapAudioProcessor& p)
    : EasyWindow (p, "EASY VERB", "Live rooms, clubs and halls - and the Bitch-Slap burst - hover over anything for help", gettingStartedHint),
      slapProcessor (p),
      typeSwitch (*p.parameters.getParameter (SlapIDs::type), { "BITCH-SLAP", "ROOM", "CLUB", "HALL", "PLATE" }, Palette::accent),
      syncSwitch (*p.parameters.getParameter (SlapIDs::sync), { "OFF", "TEMPO" }, Palette::accent),
      incoming ((size_t) BurstDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 24);
        if (i == toneKnob)
            knob.slider.getProperties().set ("fillFromCentre", true);
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
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

    addAndMakeVisible (typeSwitch);
    typeSwitch.onUserChange = [this] { knobMoved(); };
    addHint (typeSwitch, "TYPE", typeHint);

    typeInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    typeInfo.setColour (juce::Label::textColourId, Palette::textDim);
    typeInfo.setJustificationType (juce::Justification::centred);
    typeInfo.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (typeInfo);
    addHint (typeInfo, "TYPE", typeHint);

    syncLabel.setText ("SYNC", juce::dontSendNotification);
    syncLabel.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    syncLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (syncLabel);
    addAndMakeVisible (syncSwitch);
    addHint (syncLabel, "SYNC", syncHint);
    addHint (syncSwitch, "SYNC", syncHint);
    syncSwitch.onUserChange = [this] { knobMoved(); };

    lengthInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    lengthInfo.setColour (juce::Label::textColourId, Palette::textDim);
    lengthInfo.setJustificationType (juce::Justification::centred);
    lengthInfo.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (lengthInfo);
    addHint (lengthInfo, "LENGTH AT YOUR TEMPO", lengthInfoHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    const auto settings = p.getCurrentSettings();
    showKnobs (VerbEngine::isNatural (settings.type), juce::roundToInt (p.parameters.getRawParameterValue (SlapIDs::sync)->load()) == syncTempo);
    typeInfo.setText (typeDescriptions[(size_t) settings.type], juce::dontSendNotification);

    finishSetup (1100, 640);
}

void BitchSlapAudioProcessorEditor::showKnobs (bool natural, bool synced)
{
    showingSynced = synced;
    showingSpace = natural;
    const auto show = [this] (size_t k, bool visible) { knobs[k].slider.setVisible (visible); knobs[k].label.setVisible (visible); };
    show (lengthKnob, ! natural && ! synced);
    show (noteKnob, ! natural && synced);
    for (auto k : { decayKnob, preDelayKnob, toneKnob })
        show (k, natural);
    syncLabel.setVisible (! natural);
    syncSwitch.setVisible (! natural);
    lengthInfo.setVisible (! natural);
}

juce::String BitchSlapAudioProcessorEditor::describeLength (bool synced, int noteIndex, float lengthMs, double bpm, bool tempoFromDAW)
{
    const juce::String tempoText = juce::String (juce::roundToInt (bpm)) + " BPM";
    juce::String text;

    if (synced)
    {
        const auto& note = noteLengths[juce::jlimit (0, numNoteLengths - 1, noteIndex)];
        const double exactMs = note.beats * 60000.0 / bpm;
        text << note.name << " at " << tempoText << "\n= " << Format::milliseconds (BitchSlapAudioProcessor::lengthForNote (noteIndex, bpm));

        if (exactMs > GatedReverb::maxLengthMs + 0.5)       text << " (the longest it goes)";
        else if (exactMs < GatedReverb::minLengthMs - 0.5)  text << " (the shortest it goes)";
    }
    else
    {
        // The plain notes either side of it
        struct Plain { double beats; const char* name; };
        static constexpr Plain plain[] = { { 0.125, "1/32" }, { 0.25, "1/16" }, { 0.5, "1/8" }, { 1.0, "1/4" }, { 2.0, "1/2" }, { 4.0, "whole" } };
        const double beats = lengthMs * bpm / 60000.0;

        text << "At " << tempoText << " that's\n";

        if (beats < plain[0].beats * 0.94)
            text << "shorter than a 1/32 note";
        else if (beats > 4.0 * 1.06)
            text << "longer than a whole note";
        else
        {
            for (int i = 0; i < 6; ++i)
            {
                if (std::abs (beats / plain[i].beats - 1.0) <= 0.06)
                {
                    text << "about a " << plain[i].name << " note";
                    break;
                }

                if (i < 5 && beats > plain[i].beats && beats < plain[i + 1].beats)
                {
                    text << "between a " << plain[i].name << " and a " << plain[i + 1].name << " note";
                    break;
                }
            }
        }
    }

    if (! tempoFromDAW)
        text << "\n(until your DAW sends its tempo)";

    return text;
}

//==============================================================================
void BitchSlapAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");

    // What MIX's numbers mean, beside the knob
    struct Row { const char* value; const char* meaning; };
    static constexpr Row rows[] = { { "0%", "dry only" }, { "50%", "both full" }, { "100%", "reverb only" } };

    auto area = mixScale.withSizeKeepingCentre (mixScale.getWidth(), 3 * 22 + 18);

    for (auto& row : rows)
    {
        auto line = area.removeFromTop (22);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (Palette::accent);
        g.drawText (row.value, line.removeFromLeft (40), juce::Justification::centredRight, false);
        line.removeFromLeft (8);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.setColour (Palette::textDim);
        g.drawText (row.meaning, line, juce::Justification::centredLeft, false);
    }

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("(for an FX send)", area.withTrimmedLeft (48), juce::Justification::topLeft, false);
}

void BitchSlapAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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

    auto left = area.removeFromLeft (300);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (300);
    area.removeFromRight (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    // 1 WHAT KIND: TYPE, and what it is
    groupPanels[0] = left.removeFromTop (juce::jmin (left.getHeight() - 180, 5 * 32 + 28 + 24 + 50));
    left.removeFromTop (gap);
    groupPanels[1] = left;
    {
        auto inside = groupPanels[0].reduced (14, 12).withTrimmedTop (28);
        typeInfo.setBounds (inside.removeFromBottom (40));
        inside.removeFromBottom (4);
        typeSwitch.setBounds (inside.withSizeKeepingCentre (juce::jmin (180, inside.getWidth()), inside.getHeight()));
    }

    // 2 HOW MUCH: MIX, and what its numbers mean
    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto knobSpace = inside.removeFromLeft (inside.getWidth() / 2);
        placeKnob (knobs[mixKnob], knobSpace, juce::jmin (knobSpace.getWidth() - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight));
        mixScale = inside;
    }

    // 3 HOW LONG: LENGTH (or the note) and SYNC - or DECAY, then PRE-DELAY and TONE side by side
    groupPanels[2] = right;
    {
        auto inside = right.reduced (10, 12).withTrimmedTop (26);
        auto top = inside.removeFromTop (inside.getHeight() / 2);
        const int bigSize = juce::jmin (top.getWidth() - 140, top.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[lengthKnob], top, bigSize);
        placeKnob (knobs[noteKnob], top, bigSize);
        placeKnob (knobs[decayKnob], top, bigSize);

        auto bottom = inside;
        const int smallSize = juce::jmin (bottom.getWidth() / 2 - 40, bottom.getHeight() - nameHeight - spacing - valueBoxHeight - 10);
        placeKnob (knobs[preDelayKnob], bottom.withWidth (bottom.getWidth() / 2), smallSize);
        placeKnob (knobs[toneKnob], bottom.withTrimmedLeft (bottom.getWidth() / 2), smallSize);

        auto syncArea = bottom.removeFromTop (nameHeight + 4 + 2 * 28 + 4);
        syncLabel.setBounds (syncArea.removeFromTop (nameHeight));
        syncArea.removeFromTop (4);
        syncSwitch.setBounds (syncArea.withSizeKeepingCentre (juce::jmin (130, syncArea.getWidth()), syncArea.getHeight()));
        bottom.removeFromTop (6);
        lengthInfo.setBounds (bottom);
    }
}

//==============================================================================
void BitchSlapAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = slapProcessor.getCurrentSettings();
    const bool natural = VerbEngine::isNatural (settings.type);
    const bool synced = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (SlapIDs::sync)->load()) == syncTempo;
    const int note = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (SlapIDs::note)->load());

    const int count = slapProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setBypassed (settings.bypassed);
    if (natural) display.setLength (settings.decay * 1000.0f, "DECAY");
    else         display.setLength (settings.lengthMs, "LENGTH");

    if (synced != showingSynced || natural != showingSpace)
        showKnobs (natural, synced);

    const juce::String description (typeDescriptions[(size_t) settings.type]);
    if (description != typeInfo.getText())
        typeInfo.setText (description, juce::dontSendNotification);

    const auto info = describeLength (synced, note, settings.lengthMs, slapProcessor.getTempo(), slapProcessor.isTempoFromDAW());

    if (info != lengthInfo.getText())
        lengthInfo.setText (info, juce::dontSendNotification);
}

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

    const std::array<KnobInfo, 3> knobInfo
    {{
        { SlapIDs::mix, "MIX",
          "How much reverb. Up to 50% it only adds reverb - at 50% the burst is about as loud as a snare. Above 50% it also "
          "turns your original sound down, until 100% = reverb only: use that on an FX send or bus." },

        { SlapIDs::length, "LENGTH",
          "How long each burst lasts, from the hit until it slams shut. Short (100-200 ms) = tight and punchy, for fast songs. "
          "Long (400-700 ms) = the huge 80s sound, for slower ones. Tip: end the burst before the next snare hit." },

        { SlapIDs::note, "LENGTH",
          "How long each burst lasts, as a note length at your song's tempo - it follows tempo changes. 1/8 note = an eighth "
          "note. Triplets are a third shorter than the plain note, dotted notes half as long again." },
    }};

    const std::array<const char*, 2> groupNames { "HOW MUCH", "HOW LONG" };

    constexpr const char* syncHint =
        "OFF: set LENGTH in milliseconds. TEMPO: set it as a note length (1/8, 1/4...) that follows your song's tempo, so "
        "every burst stops in time with the beat.";

    constexpr const char* lengthInfoHint =
        "How LENGTH fits your song: as a note length at the song's tempo, or (with SYNC on TEMPO) in milliseconds.";

    constexpr const char* displayHint =
        "The last couple of seconds: grey = your sound going in, amber = the reverb. Every hit gets its own burst that stops "
        "dead after LENGTH - the bar in the top corner shows how long that is. The scale moves to fit your track.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or play your drums and turn up MIX until the burst sits where you like it. Then set LENGTH so each "
        "burst ends before the next hit. Hover over anything for an explanation. Double-click a knob to reset it.";
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

void BurstDisplay::setLength (float newLengthMs)
{
    if (std::abs (newLengthMs - lengthMs) > 0.25f)
    {
        lengthMs = newLengthMs;
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
        g.drawText ("LENGTH " + Format::milliseconds (lengthMs), juce::Rectangle<float> (right - 200.0f, y - 22.0f, 200.0f, 14.0f),
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
// The Bitch-Slap window
//==============================================================================
BitchSlapAudioProcessorEditor::BitchSlapAudioProcessorEditor (BitchSlapAudioProcessor& p)
    : EasyWindow (p, "BITCH-SLAP", "Gated reverb: every hit gets a burst that stops dead - hover over anything for help", gettingStartedHint),
      slapProcessor (p),
      syncSwitch (*p.parameters.getParameter (SlapIDs::sync), { "OFF", "TEMPO" }, Palette::accent),
      incoming ((size_t) BurstDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 24);
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

    showSyncedKnob (juce::roundToInt (p.parameters.getRawParameterValue (SlapIDs::sync)->load()) == syncTempo);

    finishSetup (1100, 600);
}

void BitchSlapAudioProcessorEditor::showSyncedKnob (bool synced)
{
    showingSynced = synced;
    knobs[lengthKnob].slider.setVisible (! synced);
    knobs[lengthKnob].label.setVisible (! synced);
    knobs[noteKnob].slider.setVisible (synced);
    knobs[noteKnob].label.setVisible (synced);
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
    static constexpr Row rows[] = { { "0%", "your sound only" }, { "50%", "both at full level" }, { "100%", "reverb only" } };

    auto area = mixScale.withSizeKeepingCentre (mixScale.getWidth(), 3 * 22 + 18);

    for (auto& row : rows)
    {
        auto line = area.removeFromTop (22);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.setColour (Palette::accent);
        g.drawText (row.value, line.removeFromLeft (44), juce::Justification::centredRight, false);
        line.removeFromLeft (10);
        g.setFont (juce::Font (juce::FontOptions (12.5f)));
        g.setColour (Palette::textDim);
        g.drawText (row.meaning, line, juce::Justification::centredLeft, false);
    }

    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText ("(for an FX send)", area.withTrimmedLeft (54), juce::Justification::topLeft, false);
}

void BitchSlapAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    auto column = area.removeFromLeft (380);
    area.removeFromLeft (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    const int panelHeight = (column.getHeight() - gap) / 2;
    groupPanels[0] = column.removeFromTop (panelHeight);
    column.removeFromTop (gap);
    groupPanels[1] = column;

    // A knob in the left half of a panel
    const auto placeKnob = [] (Knob& knob, juce::Rectangle<int> space)
    {
        constexpr int nameHeight = 20, spacing = 4, valueBoxHeight = 24;
        const int knobSize = juce::jmin (space.getWidth() - 20, space.getHeight() - nameHeight - spacing - valueBoxHeight);
        auto columnArea = space.withSizeKeepingCentre (space.getWidth(), nameHeight + spacing + knobSize + valueBoxHeight);

        knob.label.setBounds (columnArea.removeFromTop (nameHeight));
        columnArea.removeFromTop (spacing);
        knob.slider.setBounds (columnArea);
    };

    // 1 HOW MUCH: MIX, and what its numbers mean
    auto inside = groupPanels[0].reduced (10, 12).withTrimmedTop (26);
    placeKnob (knobs[mixKnob], inside.removeFromLeft (inside.getWidth() / 2));
    mixScale = inside;

    // 2 HOW LONG: LENGTH (or the note, when synced); SYNC and how it fits the song beside it
    inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
    const auto knobSpace = inside.removeFromLeft (inside.getWidth() / 2);
    placeKnob (knobs[lengthKnob], knobSpace);
    placeKnob (knobs[noteKnob], knobSpace);

    inside.removeFromLeft (6);
    auto syncArea = inside.removeFromTop (20 + 4 + 2 * 28 + 8);
    syncLabel.setBounds (syncArea.removeFromTop (20));
    syncArea.removeFromTop (4);
    syncSwitch.setBounds (syncArea.withSizeKeepingCentre (juce::jmin (130, syncArea.getWidth()), syncArea.getHeight()));
    inside.removeFromTop (6);
    lengthInfo.setBounds (inside);
}

//==============================================================================
void BitchSlapAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = slapProcessor.getCurrentSettings();
    const bool synced = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (SlapIDs::sync)->load()) == syncTempo;
    const int note = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (SlapIDs::note)->load());

    const int count = slapProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);
    display.setBypassed (settings.bypassed);
    display.setLength (settings.lengthMs);

    if (synced != showingSynced)
        showSyncedKnob (synced);

    const auto info = describeLength (synced, note, settings.lengthMs, slapProcessor.getTempo(), slapProcessor.isTempoFromDAW());

    if (info != lengthInfo.getText())
        lengthInfo.setText (info, juce::dontSendNotification);
}

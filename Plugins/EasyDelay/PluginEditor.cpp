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

    const std::array<KnobInfo, EasyDelayAudioProcessorEditor::numKnobs> knobInfo
    {{
        { DelayIDs::time, "TIME",
          "How long after your sound the echo comes, in milliseconds. Under 120 ms = slapback (one quick echo behind it); "
          "a few hundred ms = a clear echo. With SYNC on TEMPO you pick a note length instead, and it follows your song." },

        { DelayIDs::note, "TIME",
          "How long after your sound the echo comes, as a note length at your song's tempo - it follows tempo changes. "
          "Dotted notes are half as long again (dotted 1/8 = the classic rolling guitar echo); triplets are a third shorter." },

        { DelayIDs::feedback, "FEEDBACK",
          "How many echoes. 0 = just one (slapback). Each repeat is this much as loud as the one before: 30% fades fast, "
          "60% keeps going for a while, near the top it goes on and on (dub)." },

        { DelayIDs::lowCut, "LOW CUT",
          "Takes the low end out of the echoes (each repeat a little thinner than the last), so they don't muddy the mix. "
          "150-300 Hz on vocals and guitars. Off at the left end." },

        { DelayIDs::highCut, "HIGH CUT",
          "Takes the top off the echoes (each repeat a little darker), so they sit behind your sound instead of competing "
          "with it - like a vintage echo unit. 3-6 kHz for a warm echo. Off at the right end." },

        { DelayIDs::duck, "DUCK",
          "Turns the echoes down while you play or sing, and lets them come up in the gaps. Keeps a vocal clear while the "
          "echoes fill the spaces between lines. 0 = always the same." },

        { DelayIDs::mix, "MIX",
          "How loud the echoes are. 0% = your sound only, 50% = both at full level, 100% = echoes only (for an FX send). "
          "On a track, 15-30% is usual." },
    }};

    const std::array<const char*, 4> groupNames { "WHEN", "REPEATS", "TONE", "OUTPUT" };

    constexpr const char* syncHint =
        "OFF: set TIME in milliseconds. TEMPO: set it as a note length (1/8, dotted 1/8, 1/4...) that follows your song's "
        "tempo, so the echoes land in time.";
    constexpr const char* modeHint =
        "STEREO: each side echoes itself. PING-PONG: the echoes bounce left, right, left... WIDE: the right side's echoes "
        "come half as late again (1/8 on the left, dotted 1/8 on the right) - wide and rolling.";
    constexpr const char* characterHint =
        "CLEAN: every repeat exact, like a digital delay. TAPE: a tape echo - the repeats saturate a little and wobble "
        "slightly, and moving TIME bends the pitch the way a tape echo does.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset). BYPASS stays as it is.";
    constexpr const char* displayHint =
        "Your sound (grey, at the start) and the echoes after it - left side above the line, right side below - each as "
        "loud as it will be. They glow while they're playing.";
    constexpr const char* gettingStartedHint =
        "Pick a preset, or set TIME (or a note with SYNC) and turn MIX up until the echoes sit where you like them. "
        "Double-click a knob to reset it, or its number to type a value.";
}

//==============================================================================
// The display
//==============================================================================
void EchoDisplay::setSettings (const StereoDelay::Settings& s)
{
    const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-4f; };
    if (moved (s.timeMs, settings.timeMs) || moved (s.feedback, settings.feedback) || s.mode != settings.mode
        || moved (s.mix, settings.mix) || s.tape != settings.tape || moved (s.duck, settings.duck))
    {
        settings = s;
        repaint();
    }
}

void EchoDisplay::setEchoLevel (float peak)
{
    const float next = juce::jmax (juce::jlimit (0.0f, 1.0f, peak * 3.0f), glow * 0.85f);
    if (std::abs (next - glow) > 0.01f)
    {
        glow = next;
        repaint();
    }
}

void EchoDisplay::setBypassed (bool isBypassed)
{
    if (bypassed != isBypassed)
    {
        bypassed = isBypassed;
        repaint();
    }
}

void EchoDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (16.0f, 14.0f).withTrimmedBottom (18.0f).withTrimmedLeft (24.0f);

    // How much time to show: at least four echoes' worth, between 0.5 and 4.5 seconds
    const float longest = settings.timeMs * (settings.mode == StereoDelay::Mode::wide ? 1.5f : 1.0f);
    spanMs = juce::jlimit (500.0f, 4500.0f, longest * 4.4f);
    const auto taps = StereoDelay::picture (settings, spanMs);

    const auto xOf = [&] (float ms) { return plot.getX() + plot.getWidth() * ms / spanMs; };
    const float centre = plot.getCentreY(), half = plot.getHeight() * 0.5f - 4.0f;

    // Grid: every half second, the centre line, L and R
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    const float step = spanMs > 2500.0f ? 1000.0f : (spanMs > 1200.0f ? 500.0f : (spanMs > 600.0f ? 250.0f : 100.0f));
    for (float ms = 0.0f; ms <= spanMs + 1.0f; ms += step)
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xOf (ms)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        g.drawText (ms >= 1000.0f ? juce::String (ms / 1000.0f, ms >= 1000.0f && std::fmod (ms, 1000.0f) > 1.0f ? 1 : 0) + " s"
                                  : juce::String ((int) ms) + " ms",
                    juce::Rectangle<float> (60.0f, 14.0f).withCentre ({ xOf (ms), plot.getBottom() + 10.0f }), juce::Justification::centred, false);
    }
    g.setColour (Palette::textDim.withAlpha (0.6f));
    g.drawHorizontalLine (juce::roundToInt (centre), plot.getX(), plot.getRight());
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText ("L", juce::Rectangle<float> (plot.getX() - 24.0f, plot.getY(), 18.0f, half), juce::Justification::centred, false);
    g.drawText ("R", juce::Rectangle<float> (plot.getX() - 24.0f, centre + 4.0f, 18.0f, half), juce::Justification::centred, false);

    // Your sound, at the start
    const float m = juce::jlimit (0.0f, 1.0f, settings.mix);
    const float dry = m <= 0.5f ? 1.0f : 2.0f * (1.0f - m), wet = m >= 0.5f ? 1.0f : 2.0f * m;
    g.setColour (Palette::textDim);
    g.fillRect (juce::Rectangle<float> (xOf (0.0f), centre - half * dry, 6.0f, half * dry * 2.0f));

    // The echoes
    const auto colour = settings.tape ? juce::Colour (0xffe8a06a) : Palette::accent;
    for (auto& t : taps)
    {
        const float x = xOf (t.ms) - 3.0f;
        if (t.left > 0.0f)
        {
            const float h = half * t.left * wet;
            g.setColour (colour.withAlpha (0.25f + 0.5f * glow));
            g.fillRect (x - 3.0f, centre - h - 3.0f, 12.0f, h + 3.0f);
            g.setColour (colour);
            g.fillRect (x, centre - h, 6.0f, h);
        }
        if (t.right > 0.0f)
        {
            const float h = half * t.right * wet;
            g.setColour (colour.withAlpha (0.25f + 0.5f * glow));
            g.fillRect (x - 3.0f, centre, 12.0f, h + 3.0f);
            g.setColour (colour);
            g.fillRect (x, centre, 6.0f, h);
        }
    }

    if (settings.duck > 0.005f)
    {
        g.setColour (Palette::meterLow);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("DUCKED WHILE YOU PLAY (" + juce::String (juce::roundToInt (settings.duck * 100.0f)) + "%)",
                    plot.withHeight (16.0f).translated (0.0f, -2.0f), juce::Justification::topRight, false);
    }

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
// The Easy Delay window
//==============================================================================
EasyDelayAudioProcessorEditor::EasyDelayAudioProcessorEditor (EasyDelayAudioProcessor& p)
    : EasyWindow (p, "EASY DELAY", "Echoes, in time with your song - hover over anything for help", gettingStartedHint),
      delayProcessor (p),
      syncSwitch (*p.parameters.getParameter (DelayIDs::sync), { "OFF", "TEMPO" }, Palette::accent),
      modeSwitch (*p.parameters.getParameter (DelayIDs::mode), { "STEREO", "PING-PONG", "WIDE" }, Palette::accent),
      characterSwitch (*p.parameters.getParameter (DelayIDs::character), { "CLEAN", "TAPE" }, Palette::accent)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 24);
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (i <= noteKnob ? 15.0f : 13.0f, juce::Font::bold)));
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
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };   // "Default settings"
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    const auto setUpSwitch = [this] (juce::Label& label, ChoiceSwitch& sw, const char* name, const char* hint)
    {
        label.setText (name, juce::dontSendNotification);
        label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
        addAndMakeVisible (sw);
        sw.onUserChange = [this] { knobMoved(); };
        addHint (label, name, hint);
        addHint (sw, name, hint);
    };
    setUpSwitch (syncLabel, syncSwitch, "SYNC", syncHint);
    setUpSwitch (modeLabel, modeSwitch, "MODE", modeHint);
    setUpSwitch (characterLabel, characterSwitch, "CHARACTER", characterHint);
    characterSwitch.setHorizontal (true);

    timeInfo.setFont (juce::Font (juce::FontOptions (12.5f)));
    timeInfo.setColour (juce::Label::textColourId, Palette::textDim);
    timeInfo.setJustificationType (juce::Justification::centredTop);
    addAndMakeVisible (timeInfo);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);

    showSyncedKnob (juce::roundToInt (p.parameters.getRawParameterValue (DelayIDs::sync)->load()) == 1);

    finishSetup (1100, 640);

    // Double-click a number to type a value
    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

void EasyDelayAudioProcessorEditor::showSyncedKnob (bool synced)
{
    showingSynced = synced;
    knobs[timeKnob].slider.setVisible (! synced);
    knobs[timeKnob].label.setVisible (! synced);
    knobs[noteKnob].slider.setVisible (synced);
    knobs[noteKnob].label.setVisible (synced);
}

juce::String EasyDelayAudioProcessorEditor::describeTime (bool synced, int noteIndex, float timeMs, double bpm, bool tempoFromDAW)
{
    const juce::String tempoText = juce::String (juce::roundToInt (bpm)) + " BPM";
    juce::String text;

    if (synced)
    {
        const auto& note = DelayNotes::lengths[juce::jlimit (0, DelayNotes::count - 1, noteIndex)];
        text << note.name << " at " << tempoText << " = " << Format::milliseconds (EasyDelayAudioProcessor::timeForNote (noteIndex, bpm));
        if (note.beats * 60000.0 / bpm > StereoDelay::maxTimeMs + 0.5)
            text << " (the longest it goes)";
    }
    else
    {
        struct Plain { double beats; const char* name; };
        static constexpr Plain plain[] = { { 0.125, "1/32" }, { 0.25, "1/16" }, { 0.5, "1/8" }, { 1.0, "1/4" }, { 2.0, "1/2" }, { 4.0, "whole" } };
        const double beats = timeMs * bpm / 60000.0;

        if (timeMs < 120.0f)
            text << "Slapback (one quick echo right behind)";
        else
        {
            text << "At " << tempoText << ": ";
            if (beats > 4.0 * 1.06)
                text << "longer than a whole note";
            else
                for (int i = 0; i < 6; ++i)
                {
                    if (std::abs (beats / plain[i].beats - 1.0) <= 0.06) { text << "about a " << plain[i].name << " note"; break; }
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
void EasyDelayAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), groupNames[i]);

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
}

void EasyDelayAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // The header: INIT small, right beside the preset box; BYPASS clear of it
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

    const auto placeSwitch = [&] (juce::Label& label, ChoiceSwitch& sw, juce::Rectangle<int> column, int options)
    {
        auto c = column.withSizeKeepingCentre (column.getWidth(), nameHeight + 6 + options * 32);
        label.setBounds (c.removeFromTop (nameHeight));
        c.removeFromTop (6);
        sw.setBounds (c.withSizeKeepingCentre (juce::jmin (130, c.getWidth()), c.getHeight()));
    };

    auto left = area.removeFromLeft (320);
    area.removeFromLeft (gap);
    auto right = area.removeFromRight (320);
    area.removeFromRight (gap);
    displayPanel = area;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (24));

    const int half = (left.getHeight() - gap) / 2;
    groupPanels[0] = left.removeFromTop (half);
    left.removeFromTop (gap);
    groupPanels[1] = left;
    groupPanels[2] = right.removeFromTop (half);
    right.removeFromTop (gap);
    groupPanels[3] = right;

    // 1 WHEN: TIME (or the note), SYNC beside it, how it fits the song underneath
    {
        auto inside = groupPanels[0].reduced (10, 12).withTrimmedTop (26);
        timeInfo.setBounds (inside.removeFromBottom (34));
        auto knobSpace = inside.removeFromLeft (inside.getWidth() * 3 / 5);
        const int size = juce::jmin (knobSpace.getWidth() - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[timeKnob], knobSpace, size);
        placeKnob (knobs[noteKnob], knobSpace, size);
        placeSwitch (syncLabel, syncSwitch, inside, 2);
    }

    // 2 REPEATS: FEEDBACK, MODE beside it
    {
        auto inside = groupPanels[1].reduced (10, 12).withTrimmedTop (26);
        auto knobSpace = inside.removeFromLeft (inside.getWidth() / 2);
        placeKnob (knobs[feedbackKnob], knobSpace, juce::jmin (knobSpace.getWidth() - 30, inside.getHeight() - nameHeight - spacing - valueBoxHeight));
        placeSwitch (modeLabel, modeSwitch, inside, 3);
    }

    // 3 TONE: LOW CUT, HIGH CUT, CHARACTER underneath
    {
        auto inside = groupPanels[2].reduced (10, 12).withTrimmedTop (26);
        auto bottom = inside.removeFromBottom (nameHeight + 6 + 30);
        characterLabel.setBounds (bottom.removeFromTop (nameHeight));
        bottom.removeFromTop (6);
        characterSwitch.setBounds (bottom.withSizeKeepingCentre (juce::jmin (220, bottom.getWidth()), 30));
        inside.removeFromBottom (6);
        const int w = inside.getWidth() / 2;
        const int size = juce::jmin (w - 20, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[lowCutKnob], inside.removeFromLeft (w), size);
        placeKnob (knobs[highCutKnob], inside, size);
    }

    // 4 OUTPUT: DUCK, MIX
    {
        auto inside = groupPanels[3].reduced (10, 12).withTrimmedTop (26);
        const int w = inside.getWidth() / 2;
        const int size = juce::jmin (w - 20, inside.getHeight() - nameHeight - spacing - valueBoxHeight);
        placeKnob (knobs[duckKnob], inside.removeFromLeft (w), size);
        placeKnob (knobs[mixKnob], inside, size);
    }
}

//==============================================================================
void EasyDelayAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = delayProcessor.getCurrentSettings();
    const bool synced = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (DelayIDs::sync)->load()) == 1;
    const int note = juce::roundToInt (easyProcessor.parameters.getRawParameterValue (DelayIDs::note)->load());

    display.setSettings (settings);
    display.setEchoLevel (delayProcessor.takeEchoPeak());
    display.setBypassed (settings.bypassed);
    delayProcessor.takeInputPeak();

    if (synced != showingSynced)
        showSyncedKnob (synced);

    const auto info = describeTime (synced, note, settings.timeMs, delayProcessor.getTempo(), delayProcessor.isTempoFromDAW());
    if (info != timeInfo.getText())
        timeInfo.setText (info, juce::dontSendNotification);
}

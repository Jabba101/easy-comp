#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    struct KnobInfo { const char* parameterID; const char* name; const char* hint; };

    constexpr const char* thresholdHint =
        "Where the compressor starts to work. For glue on a master, set it so the meter shows 1-3 dB of turning down on the "
        "loud parts - more than about 4 dB and you'll hear it squeeze.";
    constexpr const char* ratioHint =
        "How firmly it turns down above the THRESHOLD. 1.5:1 - 2:1 is glue; 3:1 and more is obvious compression. On a "
        "master, gentle almost always sounds better.";
    constexpr const char* attackHint =
        "How quickly it reacts. 20-50 ms lets the punch of the kick and snare through before it acts; fast (under 10 ms) "
        "flattens the hits.";
    constexpr const char* releaseHint =
        "How quickly it lets go. Set it to move with the song: 100-200 ms for busy rock, 300 ms+ for slow, smooth music. "
        "Too fast pumps, too slow sits on everything.";
    constexpr const char* makeupHint =
        "Turns the compressed sound back up. On a master you mostly leave this low and let the LIMITER's GAIN set the "
        "loudness.";

    const std::array<KnobInfo, EasyMasterAudioProcessorEditor::numKnobs> knobInfo
    {{
        { MasterIDs::trim, "TRIM",
          "Turns the mix up or down before anything else, so the compressor sees a sensible level. If the warning shows "
          "the mix clipping on the way in, turn the mix down in your DAW instead (the clip is already there)." },
        { MasterIDs::midThreshold, "THRESHOLD", thresholdHint },
        { MasterIDs::midRatio,     "RATIO",     ratioHint },
        { MasterIDs::midAttack,    "ATTACK",    attackHint },
        { MasterIDs::midRelease,   "RELEASE",   releaseHint },
        { MasterIDs::midMakeup,    "MAKE-UP",   makeupHint },
        { MasterIDs::sideThreshold, "THRESHOLD", thresholdHint },
        { MasterIDs::sideRatio,     "RATIO",     ratioHint },
        { MasterIDs::sideAttack,    "ATTACK",    attackHint },
        { MasterIDs::sideRelease,   "RELEASE",   releaseHint },
        { MasterIDs::sideMakeup,    "MAKE-UP",   makeupHint },
        { MasterIDs::compMix, "MIX",
          "How much of the compressed sound you hear, blended with the uncompressed. 70-85% keeps the hits' snap with most "
          "of the glue (parallel compression)." },
        { MasterIDs::ignoreBelow, "IGNORE BELOW",
          "The compressor doesn't react to the deep bass under this frequency (it still compresses it). Stops a heavy kick "
          "and bass making the whole mix pump. 60-100 Hz for heavy low end; Off for none." },
        { MasterIDs::limitGain, "GAIN",
          "Turns the master up into the ceiling: each dB more is a dB louder, with the peaks held at the ceiling. Use MATCH "
          "to hear whether it's still clean - when the kick and snare soften, back off." },
        { MasterIDs::ceiling, "CEILING",
          "The most the output may ever reach, as true peak (the peaks between the samples too). -0.3 dBTP by default; "
          "-1 dBTP for files that will be converted to MP3 / AAC." },
        { MasterIDs::release, "RELEASE",
          "With RELEASE on SET: how quickly the limiter lets go. AUTO is almost always best (quick after a lone peak, "
          "slower through a sustained passage - no pumping, no distortion)." },
    }};

    constexpr const char* gettingStartedHint =
        "Pick a preset for the song, set TRIM so the compressor shows 1-3 dB, push the limiter's GAIN to taste, then check "
        "with MATCH and BYPASS. Hover over anything for help.";
    constexpr const char* compOnHint = "Switches the compressor on or off (the rest of the chain keeps working).";
    constexpr const char* modeHint =
        "STEREO: one compressor for the whole mix. MID + SIDE: the middle (kick, bass, snare, vocal) and the sides (wide "
        "guitars, synths, reverb) each get their own compressor: the green MID row and the blue SIDE row.";
    constexpr const char* reactsHint =
        "PEAKS: reacts to every hit - tighter, more controlling. AVERAGE: reacts to the overall loudness - smoother, the "
        "usual choice for glue on a master.";
    constexpr const char* characterHint =
        "MODERN: clean and precise. VINTAGE: a softer, rounder, slightly thicker compression, like an old hardware unit.";
    constexpr const char* limitReleaseHint =
        "AUTO: lets go quickly after a lone peak and more slowly while holding back a sustained passage - no pumping, no "
        "distortion. SET: the RELEASE time you choose.";
    constexpr const char* matchHint =
        "Plays the whole chain exactly as loud as the original (it follows the loudness over a few seconds). Click BYPASS "
        "on and off with MATCH on: you're judging the sound, not the volume. Switch it off when you're done.";
    constexpr const char* listenHint =
        "For checking, not for exporting: STEREO as normal; MID = only the middle; SIDE = only the difference between "
        "left and right (the width - reverb, wide guitars); MONO = how it sounds on a phone or club speaker.";
    constexpr const char* abHint =
        "Two sets of settings to compare: A and B. COPY copies the one you're on to the other. The one you're not on is "
        "kept with your project.";
    constexpr const char* metersHint =
        "The master as it leaves the limiter. LUFS: loudness - MOMENTARY (now), SHORT-TERM (3 s), INTEGRATED (the song "
        "so far). Readings, never targets. Click to reset the integrated reading and the highest peak.";
    constexpr const char* displayHint =
        "The last six seconds: grey = into the limiter, amber = out, the dashed line = the CEILING; from the top, red = "
        "the limiter turning down, green = the compressor (MID), blue = the compressor (SIDE).";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the 'Default settings' preset). BYPASS, MATCH and LISTEN stay as they are.";
    constexpr const char* chainHint =
        "The order the sound goes through. Lit = here now; dim = coming in later stages (resonance tamer, EQ, multiband, "
        "punch and width), each in its place.";

    constexpr float topDb = 2.0f, bottomDb = -30.0f, cutScaleDb = 12.0f;
    const juce::Colour midColour = Palette::meterLow;
    const juce::Colour sideColour = Palette::meterLow.interpolatedWith (juce::Colours::deepskyblue, 0.7f);

    juce::String lufsText (float lufs) { return lufs <= -69.9f ? juce::String ("--") : juce::String (lufs, 1); }
}

//==============================================================================
MasterDisplay::MasterDisplay() : history ((size_t) numMoments) {}

void MasterDisplay::addMoments (const EasyMasterAudioProcessor::Moment* m, int count)
{
    if (count <= 0) return;
    for (int i = 0; i < count; ++i)
    {
        history[(size_t) next] = m[i];
        next = (next + 1) % numMoments;
    }
    repaint();
}

void MasterDisplay::setState (float c, bool ms, bool on, bool b)
{
    if (std::abs (c - ceilingDb) > 0.01f || ms != midSide || on != compOn || b != bypassed)
    {
        ceilingDb = c; midSide = ms; compOn = on; bypassed = b;
        repaint();
    }
}

void MasterDisplay::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (Palette::background);
    g.fillRoundedRectangle (area, 6.0f);

    auto plot = area.reduced (14.0f, 12.0f).withTrimmedLeft (34.0f).withTrimmedBottom (40.0f);
    // (two lanes: how much is being turned down on top, the levels underneath)
    const auto cutLane = plot.removeFromTop (plot.getHeight() * 0.3f);
    plot.removeFromTop (8.0f);
    const auto yOf = [&] (float db) { return plot.getY() + plot.getHeight() * (topDb - juce::jlimit (bottomDb, topDb, db)) / (topDb - bottomDb); };
    const auto xOf = [&] (int age) { return plot.getRight() - (float) age / (float) (numMoments - 1) * plot.getWidth(); };
    const auto cutY = [&] (float db) { return cutLane.getY() + cutLane.getHeight() * juce::jlimit (0.0f, 1.0f, db / cutScaleDb); };

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yOf (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 36.0f, yOf (db) - 7.0f, 32.0f, 14.0f), juce::Justification::centredRight, false);
    }
    g.setColour (Palette::panel.withAlpha (0.6f));
    g.fillRect (cutLane);
    for (float db : { 0.0f, 6.0f, 12.0f })
    {
        g.setColour (Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (cutY (db)), cutLane.getX(), cutLane.getRight());
        g.setColour (Palette::textDim);
        g.drawText (db < 0.5f ? juce::String ("0") : "-" + juce::String ((int) db), juce::Rectangle<float> (cutLane.getX() - 36.0f, cutY (db) - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }
    const auto laneName = [&g] (juce::Rectangle<float> lane, const juce::String& name)
    {
        const auto tag = juce::Rectangle<float> (lane.getX() + 4.0f, lane.getBottom() - 18.0f, (float) name.length() * 7.2f + 12.0f, 15.0f);
        g.setColour (Palette::background.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, 3.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (10.5f, juce::Font::bold)));
        g.drawText (name, tag, juce::Justification::centred, false);
    };

    g.drawText ("6 seconds ago", juce::Rectangle<float> (plot.getX(), plot.getBottom() + 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("now", juce::Rectangle<float> (plot.getRight() - 60.0f, plot.getBottom() + 3.0f, 60.0f, 14.0f), juce::Justification::centredRight, false);

    juce::Path in, out, outLine, cut, midLine, sideLine;
    in.startNewSubPath (plot.getBottomRight());
    out.startNewSubPath (plot.getBottomRight());
    cut.startNewSubPath (cutLane.getTopRight());
    for (int age = 0; age < numMoments; ++age)
    {
        const auto& m = history[(size_t) ((next - 1 - age + numMoments * 2) % numMoments)];
        const float x = xOf (age);
        in.lineTo (x, yOf (m.inDb));
        out.lineTo (x, yOf (m.outDb));
        cut.lineTo (x, cutY (m.limitCutDb));
        if (age == 0)
        {
            outLine.startNewSubPath (x, yOf (m.outDb));
            midLine.startNewSubPath (x, cutY (m.midCutDb));
            sideLine.startNewSubPath (x, cutY (m.sideCutDb));
        }
        else
        {
            outLine.lineTo (x, yOf (m.outDb));
            midLine.lineTo (x, cutY (m.midCutDb));
            sideLine.lineTo (x, cutY (m.sideCutDb));
        }
    }
    in.lineTo (plot.getBottomLeft()); in.closeSubPath();
    out.lineTo (plot.getBottomLeft()); out.closeSubPath();
    cut.lineTo (cutLane.getTopLeft()); cut.closeSubPath();

    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (Palette::textDim.withAlpha (0.3f));
        g.fillPath (in);
        g.setColour (Palette::accent.withAlpha (0.16f));
        g.fillPath (out);
        g.setColour (Palette::accent);
        g.strokePath (outLine, juce::PathStrokeType (1.4f));
    }
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (cutLane.toNearestInt());
        g.setColour (Palette::meterHigh.withAlpha (0.5f));
        g.fillPath (cut);
        if (compOn)
        {
            if (midSide)
            {
                g.setColour (sideColour);
                g.strokePath (sideLine, juce::PathStrokeType (1.8f));
            }
            g.setColour (midColour);
            g.strokePath (midLine, juce::PathStrokeType (1.8f));
        }
    }

    // The ceiling
    {
        const float y = yOf (ceilingDb);
        juce::Path line, dashed;
        line.startNewSubPath (plot.getX(), y);
        line.lineTo (plot.getRight(), y);
        const float dashes[] { 6.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, line, dashes, 2);
        g.setColour (Palette::textBright);
        g.fillPath (dashed);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("CEILING " + juce::String (ceilingDb, 1) + " dBTP", juce::Rectangle<float> (plot.getRight() - 200.0f, y + 3.0f, 196.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    laneName (cutLane, "TURNING DOWN");
    laneName (plot, "LEVEL");

    // Key
    auto key = juce::Rectangle<float> (plot.getX(), plot.getBottom() + 20.0f, plot.getWidth(), 16.0f);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    const auto item = [&] (juce::Colour c, const juce::String& t, float w)
    {
        g.setColour (c);
        g.fillRect (key.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 10.0f));
        key.removeFromLeft (6.0f);
        g.setColour (Palette::textDim);
        g.drawText (t, key.removeFromLeft (w), juce::Justification::centredLeft, false);
        key.removeFromLeft (12.0f);
    };
    item (Palette::textDim.withAlpha (0.6f), "into the limiter", 100.0f);
    item (Palette::accent, "out", 30.0f);
    item (Palette::meterHigh.withAlpha (0.7f), "limiter turning down", 130.0f);
    item (midColour, midSide ? "compressor: MID" : "compressor", midSide ? 116.0f : 76.0f);
    if (midSide)
        item (sideColour, "compressor: SIDE", 116.0f);

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
        g.drawText ("You're hearing the original mix", lines, juce::Justification::centred, false);
    }
}

//==============================================================================
void MasterMeters::setReadings (const EasyMasterAudioProcessor::Readings& readings, bool ms, bool on)
{
    r = readings;
    midSide = ms; compOn = on;
    const auto follow = [] (float& shown, float now) { shown = now > shown ? now : shown + 0.25f * (now - shown); };
    follow (shownMid, on ? r.midCut : 0.0f);
    follow (shownSide, on ? r.sideCut : 0.0f);
    follow (shownLimit, r.limitCut);
    repaint();
}

void MasterMeters::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // LUFS: three big numbers
    auto lufsRow = area.removeFromTop (52.0f);
    const float third = lufsRow.getWidth() / 3.0f;
    const auto bigNumber = [&] (juce::Rectangle<float> box, const juce::String& caption, const juce::String& value, juce::Colour colour)
    {
        g.setColour (Palette::background);
        g.fillRoundedRectangle (box.reduced (3.0f), 5.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (caption, box.removeFromTop (18.0f).translated (0.0f, 3.0f), juce::Justification::centred, false);
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
        g.drawText (value, box.translated (0.0f, -2.0f), juce::Justification::centred, false);
    };
    bigNumber (lufsRow.removeFromLeft (third), "MOMENTARY", lufsText (r.momentary), Palette::textBright);
    bigNumber (lufsRow.removeFromLeft (third), "SHORT-TERM", lufsText (r.shortTerm), Palette::textBright);
    bigNumber (lufsRow, "INTEGRATED", lufsText (r.integrated), Palette::accent);
    area.removeFromTop (4.0f);

    // True peak and PLR
    auto peakRow = area.removeFromTop (48.0f);
    const float half = peakRow.getWidth() / 2.0f;
    const bool over = r.highestPeak > -0.05f;
    bigNumber (peakRow.removeFromLeft (half), "HIGHEST TRUE PEAK",
               r.highestPeak <= -99.0f ? juce::String ("--") : juce::String (r.highestPeak, 2) + " dB", over ? Palette::meterHigh : Palette::textBright);
    const float plr = r.highestPeak - r.integrated;
    bigNumber (peakRow, "PLR (PEAK TO LOUDNESS)", (r.integrated <= -69.9f || r.highestPeak <= -99.0f) ? juce::String ("--") : juce::String (plr, 1) + " dB",
               Palette::textBright);
    area.removeFromTop (4.0f);

    // Correlation
    {
        auto row = area.removeFromTop (30.0f);
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("STEREO CORRELATION", row.removeFromTop (14.0f), juce::Justification::centredLeft, false);
        auto bar = row.reduced (0.0f, 3.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bar, 3.0f);
        const float cx = bar.getCentreX();
        const float x = bar.getX() + bar.getWidth() * (0.5f + 0.5f * juce::jlimit (-1.0f, 1.0f, r.correlation));
        const auto colour = r.correlation < 0.0f ? Palette::meterHigh : (r.correlation < 0.3f ? Palette::meterMid : Palette::meterLow);
        g.setColour (colour.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), bar.getY() + 2.0f, juce::jmax (cx, x), bar.getBottom() - 2.0f));
        g.setColour (Palette::textDim);
        g.drawVerticalLine (juce::roundToInt (cx), bar.getY(), bar.getBottom());
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText ("-1", bar.reduced (4.0f, 0.0f), juce::Justification::centredLeft, false);
        g.drawText ("+1", bar.reduced (4.0f, 0.0f), juce::Justification::centredRight, false);
    }
    area.removeFromTop (4.0f);

    // Turning down now
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    g.drawText ("TURNING DOWN NOW", area.removeFromTop (14.0f), juce::Justification::centredLeft, false);
    const auto cutBar = [&] (const juce::String& name, float db, juce::Colour colour, bool active)
    {
        auto row = area.removeFromTop (18.0f).reduced (0.0f, 2.0f);
        g.setColour (active ? Palette::textBright : Palette::textDim.withAlpha (0.5f));
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawText (name, row.removeFromLeft (110.0f), juce::Justification::centredLeft, false);
        auto value = row.removeFromRight (56.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (row, 3.0f);
        if (active && db > 0.05f)
        {
            g.setColour (colour);
            g.fillRoundedRectangle (row.withWidth (row.getWidth() * juce::jlimit (0.0f, 1.0f, db / 12.0f)), 3.0f);
        }
        g.setColour (active ? Palette::textBright : Palette::textDim.withAlpha (0.5f));
        g.drawText (active ? juce::String (db, 1) + " dB" : juce::String ("off"), value, juce::Justification::centredRight, false);
    };
    cutBar (midSide ? "COMPRESSOR MID" : "COMPRESSOR", shownMid, midColour, compOn);
    cutBar ("COMPRESSOR SIDE", shownSide, sideColour, compOn && midSide);
    cutBar ("LIMITER", shownLimit, Palette::meterHigh, true);

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("click to reset INTEGRATED and the peak", area.removeFromBottom (14.0f), juce::Justification::centredRight, false);
}

//==============================================================================
EasyMasterAudioProcessorEditor::EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor& p)
    : EasyWindow (p, "EASY MASTER", "The mastering chain for your master bus - hover over anything for help", gettingStartedHint),
      masterProcessor (p),
      modeSwitch (*p.parameters.getParameter (MasterIDs::compMode), { "STEREO", "MID + SIDE" }, Palette::accent),
      reactsSwitch (*p.parameters.getParameter (MasterIDs::reactsTo), { "PEAKS", "AVERAGE" }, Palette::accent),
      characterSwitch (*p.parameters.getParameter (MasterIDs::character), { "MODERN", "VINTAGE" }, Palette::accent),
      releaseSwitch (*p.parameters.getParameter (MasterIDs::releaseMode), { "AUTO", "SET" }, Palette::accent),
      listenSwitch (*p.parameters.getParameter (MasterIDs::listen), { "STEREO", "MID", "SIDE", "MONO" }, Palette::meterMid),
      incoming ((size_t) MasterDisplay::numMoments)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 96, 24);
        addAndMakeVisible (knob.slider);
        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        const bool big = i == gainKnob || i == midThresholdKnob || i == sideThresholdKnob;
        knob.label.setFont (juce::Font (juce::FontOptions (big ? 15.0f : 13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (easyProcessor.parameters, knobInfo[i].parameterID, knob.slider);
        knob.slider.onValueChange = [this] { knobMoved(); };
        const bool side = i >= sideThresholdKnob && i <= sideMakeupKnob, mid = i >= midThresholdKnob && i <= midMakeupKnob;
        const juce::String knobTitle = juce::String (side ? "SIDE " : (mid ? "MID " : "")) + knobInfo[i].name;
        addHint (knob.slider, knobTitle, knobInfo[i].hint);
        addHint (knob.label, knobTitle, knobInfo[i].hint);
        if (side) knob.slider.setColour (juce::Slider::rotarySliderFillColourId, sideColour);
        if (mid)  knob.slider.setColour (juce::Slider::rotarySliderFillColourId, midColour);
    }

    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

    compOnButton.setClickingTogglesState (true);
    compOnAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::compOn), compOnButton);
    addAndMakeVisible (compOnButton);
    addHint (compOnButton, "COMPRESSOR ON", compOnHint);

    for (auto* s : { &modeSwitch, &reactsSwitch, &characterSwitch, &listenSwitch })
        s->setHorizontal (true);
    for (auto [s, switchTitle, hint] : { std::tuple<ChoiceSwitch*, const char*, const char*> { &modeSwitch, "STEREO / MID + SIDE", modeHint },
                                   { &reactsSwitch, "REACTS TO", reactsHint }, { &characterSwitch, "CHARACTER", characterHint },
                                   { &releaseSwitch, "LIMITER RELEASE", limitReleaseHint }, { &listenSwitch, "LISTEN", listenHint } })
    {
        addAndMakeVisible (*s);
        s->onUserChange = [this] { knobMoved(); };
        addHint (*s, switchTitle, hint);
    }

    matchButton.setClickingTogglesState (true);
    matchAttachment = std::make_unique<juce::ButtonParameterAttachment> (*p.parameters.getParameter (MasterIDs::match), matchButton);
    addAndMakeVisible (matchButton);
    addHint (matchButton, "MATCH", matchHint);

    for (auto* l : { &matchInfo, &inputWarning, &listenLabel, &abLabel })
    {
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        l->setColour (juce::Label::textColourId, Palette::textDim);
        l->setJustificationType (juce::Justification::centred);
        addAndMakeVisible (*l);
    }
    listenLabel.setText ("LISTEN (for checking)", juce::dontSendNotification);
    listenLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    abLabel.setText ("COMPARE SETTINGS", juce::dontSendNotification);
    abLabel.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
    addHint (matchInfo, "MATCH", matchHint);
    addHint (listenLabel, "LISTEN", listenHint);
    addHint (inputWarning, "TRIM", knobInfo[trimKnob].hint);

    for (auto* b : { &slotA, &slotB })
    {
        b->setRadioGroupId (102);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
        addHint (*b, "A / B", abHint);
    }
    slotA.onClick = [this] { easyProcessor.switchToSlot (0); };
    slotB.onClick = [this] { easyProcessor.switchToSlot (1); };
    copyButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    copyButton.setColour (juce::TextButton::textColourOffId, Palette::textBright);
    copyButton.onClick = [this] { easyProcessor.copyToOtherSlot(); };
    addAndMakeVisible (copyButton);
    addHint (copyButton, "COPY", abHint);
    addHint (abLabel, "A / B", abHint);

    addAndMakeVisible (display);
    addHint (display, "WHAT IT'S DOING", displayHint);
    addAndMakeVisible (meters);
    addHint (meters, "METERS", metersHint);
    meters.onReset = [this] { masterProcessor.resetMeters(); };

    finishSetup (1680, 900);

    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
    refreshLiveDisplays();
}

void EasyMasterAudioProcessorEditor::showParts()
{
    // Both rows always showing; the SIDE row only works in MID + SIDE (in STEREO the top row does both)
    const bool ms = masterProcessor.isMidSide();
    const bool on = easyProcessor.parameters.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f;
    for (int k = midThresholdKnob; k <= ignoreKnob; ++k)
    {
        const bool sideKnob = k >= sideThresholdKnob && k <= sideMakeupKnob;
        const bool usable = on && (! sideKnob || ms);
        knobs[(size_t) k].slider.setEnabled (! sideKnob || ms);
        knobs[(size_t) k].slider.setAlpha (usable ? 1.0f : 0.3f);
        knobs[(size_t) k].label.setAlpha (usable ? 1.0f : 0.3f);
    }
    repaint (compPanel);
}

//==============================================================================
void EasyMasterAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    // The chain
    {
        struct Stage { const char* name; bool built; };
        const Stage stages[] { { "INPUT", true }, { "RESONANCE TAMER", false }, { "EQ", false }, { "MULTIBAND", false },
                               { "COMPRESSOR", true }, { "PUNCH", false }, { "WIDTH", false }, { "LIMITER", true }, { "OUTPUT", true } };
        const int count = (int) std::size (stages);
        const float arrow = 18.0f;
        const float w = ((float) chainArea.getWidth() - arrow * (float) (count - 1)) / (float) count;
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        for (int i = 0; i < count; ++i)
        {
            const auto box = juce::Rectangle<float> ((float) chainArea.getX() + (float) i * (w + arrow), (float) chainArea.getY(), w, (float) chainArea.getHeight());
            g.setColour (stages[i].built ? Palette::panel : Palette::background);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (stages[i].built ? Palette::accent.withAlpha (0.8f) : Palette::panelEdge);
            g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.2f);
            g.setColour (stages[i].built ? Palette::textBright : Palette::textDim.withAlpha (0.55f));
            if (stages[i].built)
                g.drawText (stages[i].name, box, juce::Justification::centred, false);
            else
            {
                auto b = box.reduced (2.0f, 4.0f);
                g.drawText (stages[i].name, b.removeFromTop (b.getHeight() * 0.58f), juce::Justification::centredBottom, false);
                g.setFont (juce::Font (juce::FontOptions (10.0f)));
                g.drawText ("coming", b, juce::Justification::centredTop, false);
                g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            }
            if (i < count - 1)
            {
                const float ax = box.getRight() + 4.0f, ay = box.getCentreY();
                juce::Path a;
                a.addTriangle (ax, ay - 5.0f, ax, ay + 5.0f, ax + arrow - 8.0f, ay);
                g.setColour (Palette::textDim.withAlpha (0.6f));
                g.fillPath (a);
            }
        }
    }

    drawPanel (g, displayPanel, {}, "WHAT IT'S DOING");
    drawPanel (g, metersPanel, {}, "METERS");
    drawPanel (g, inputPanel, "1", "INPUT");
    const bool ms = masterProcessor.isMidSide();
    drawPanel (g, compPanel, "2", "COMPRESSOR");
    {
        // The row names: MID (green) and SIDE (blue) - or STEREO, with the SIDE row resting
        const auto rowName = [&g] (juce::Rectangle<int> r, const juce::String& name, const juce::String& under, juce::Colour c, bool lit)
        {
            const auto box = r.toFloat().reduced (2.0f, 6.0f);
            g.setColour (lit ? c.withAlpha (0.16f) : Palette::background);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (lit ? c : Palette::panelEdge);
            g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.3f);
            g.setColour (lit ? c : Palette::textDim.withAlpha (0.5f));
            g.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
            auto text = box.reduced (2.0f, 0.0f);
            g.drawText (name, text.removeFromTop (text.getHeight() * 0.55f), juce::Justification::centredBottom, false);
            g.setFont (juce::Font (juce::FontOptions (10.0f)));
            g.drawFittedText (under, text.toNearestInt(), juce::Justification::centredTop, 2);
        };
        rowName (midRowName, ms ? "MID" : "STEREO", ms ? "kick, bass,\nsnare, vocal" : "the whole mix", midColour, true);
        rowName (sideRowName, "SIDE", ms ? "the width" : "only in\nMID + SIDE", sideColour, ms);
    }
    drawPanel (g, limiterPanel, "3", "LIMITER");
    drawPanel (g, comparePanel, "4", "COMPARE");
}

void EasyMasterAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
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
        knob.slider.setBounds (c.withSizeKeepingCentre (juce::jmin (column.getWidth(), knobSize + 20), c.getHeight()));
    };
    const auto knobSizeFor = [&] (juce::Rectangle<int> column, int most)
    {
        return juce::jmin (most, column.getWidth() - 24, column.getHeight() - nameHeight - spacing - valueBoxHeight);
    };

    chainArea = area.removeFromTop (40);
    area.removeFromTop (gap);

    auto top = area.removeFromTop ((area.getHeight() - gap) * 42 / 100);
    area.removeFromTop (gap);
    metersPanel = top.removeFromRight (400);
    top.removeFromRight (gap);
    displayPanel = top;
    display.setBounds (displayPanel.reduced (12, 12).withTrimmedTop (22));
    meters.setBounds (metersPanel.reduced (14, 8).withTrimmedTop (22));

    // Bottom row: INPUT, COMPRESSOR, LIMITER, COMPARE
    inputPanel = area.removeFromLeft (160);
    area.removeFromLeft (gap);
    comparePanel = area.removeFromRight (250);
    area.removeFromRight (gap);
    limiterPanel = area.removeFromRight (420);
    area.removeFromRight (gap);
    compPanel = area;

    {
        auto inside = inputPanel.reduced (10, 12).withTrimmedTop (26);
        inputWarning.setBounds (inside.removeFromBottom (40));
        placeKnob (knobs[trimKnob], inside, knobSizeFor (inside, 90));
    }
    {
        auto inside = compPanel.reduced (14, 12).withTrimmedTop (28);
        auto row = inside.removeFromTop (32);
        compOnButton.setBounds (row.removeFromLeft (64).withSizeKeepingCentre (64, 30));
        row.removeFromLeft (12);
        modeSwitch.setBounds (row.removeFromLeft (220).withSizeKeepingCentre (220, 30));
        row.removeFromLeft (16);
        characterSwitch.setBounds (row.removeFromRight (196).withSizeKeepingCentre (196, 30));
        row.removeFromRight (12);
        reactsSwitch.setBounds (row.removeFromRight (196).withSizeKeepingCentre (196, 30));
        inside.removeFromTop (6);

        // Two rows of five: MID (or STEREO) on top, SIDE underneath; MIX and IGNORE BELOW (for both) on the right
        auto shared = inside.removeFromRight (inside.getWidth() / 7);
        const int rowHeight = inside.getHeight() / 2;
        auto midRow = inside.removeFromTop (rowHeight), sideRow = inside;
        midRowName = midRow.removeFromLeft (84);
        sideRowName = sideRow.removeFromLeft (84);
        const int w = midRow.getWidth() / 5;
        for (int c = 0; c < 5; ++c)
        {
            placeKnob (knobs[(size_t) (midThresholdKnob + c)], juce::Rectangle<int> (midRow.getX() + c * w, midRow.getY(), w, midRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, midRow.getHeight()), 64));
            placeKnob (knobs[(size_t) (sideThresholdKnob + c)], juce::Rectangle<int> (sideRow.getX() + c * w, sideRow.getY(), w, sideRow.getHeight()),
                       knobSizeFor (juce::Rectangle<int> (0, 0, w, sideRow.getHeight()), 64));
        }
        placeKnob (knobs[mixKnob], shared.removeFromTop (rowHeight), knobSizeFor (juce::Rectangle<int> (0, 0, shared.getWidth(), rowHeight), 60));
        placeKnob (knobs[ignoreKnob], shared, knobSizeFor (shared, 60));
    }
    {
        auto inside = limiterPanel.reduced (12, 12).withTrimmedTop (26);
        auto gainColumn = inside.removeFromLeft (150);
        placeKnob (knobs[gainKnob], gainColumn, knobSizeFor (gainColumn, 100));
        auto ceilingColumn = inside.removeFromLeft (120);
        placeKnob (knobs[ceilingKnob], ceilingColumn, knobSizeFor (ceilingColumn, 72));
        auto releaseColumn = inside;
        releaseSwitch.setBounds (releaseColumn.removeFromTop (64).withSizeKeepingCentre (90, 60));
        placeKnob (knobs[releaseKnob], releaseColumn, knobSizeFor (releaseColumn, 60));
    }
    {
        auto inside = comparePanel.reduced (14, 12).withTrimmedTop (28);
        matchButton.setBounds (inside.removeFromTop (32).withSizeKeepingCentre (120, 32));
        matchInfo.setBounds (inside.removeFromTop (34));
        inside.removeFromTop (6);
        listenLabel.setBounds (inside.removeFromTop (18));
        listenSwitch.setBounds (inside.removeFromTop (30));
        inside.removeFromTop (12);
        abLabel.setBounds (inside.removeFromTop (18));
        auto ab = inside.removeFromTop (30);
        slotA.setBounds (ab.removeFromLeft (60));
        ab.removeFromLeft (8);
        slotB.setBounds (ab.removeFromLeft (60));
        ab.removeFromLeft (8);
        copyButton.setBounds (ab);
    }
    showParts();
}

void EasyMasterAudioProcessorEditor::refreshLiveDisplays()
{
    const int count = masterProcessor.takeMoments (incoming.data(), (int) incoming.size());
    display.addMoments (incoming.data(), count);

    const auto& params = easyProcessor.parameters;
    const bool ms = masterProcessor.isMidSide();
    const bool compOn = params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f;
    const bool bypassed = params.getRawParameterValue (CommonIDs::bypass)->load() > 0.5f;
    display.setState (params.getRawParameterValue (MasterIDs::ceiling)->load(), ms, compOn, bypassed);
    const auto readings = masterProcessor.getReadings();
    meters.setReadings (readings, ms, compOn);

    if (ms != shownMidSide || compOn != shownCompOn)
    {
        shownMidSide = ms;
        shownCompOn = compOn;
        showParts();
    }

    // The limiter's RELEASE dims while it's on AUTO (the compressor's knobs: see showParts)
    const bool set = juce::roundToInt (params.getRawParameterValue (MasterIDs::releaseMode)->load()) == 1;
    knobs[releaseKnob].slider.setEnabled (set);
    knobs[releaseKnob].slider.setAlpha (set ? 1.0f : 0.4f);
    knobs[releaseKnob].label.setAlpha (set ? 1.0f : 0.4f);

    const bool matching = params.getRawParameterValue (MasterIDs::match)->load() > 0.5f;
    matchInfo.setText (matching ? "Matched: " + Format::signedDecibels (readings.matchDb) + " - now BYPASS on and off"
                                : "Hear it at the original's loudness, then BYPASS on and off", juce::dontSendNotification);

    const bool clipping = masterProcessor.getInputPeakDb() > -0.01f;
    inputWarning.setText (clipping ? "The mix is CLIPPING on the way in" : "Input OK", juce::dontSendNotification);
    inputWarning.setColour (juce::Label::textColourId, clipping ? Palette::meterHigh : Palette::textDim);

    const int slot = easyProcessor.getActiveSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "COPY A > B" : "COPY B > A");
}

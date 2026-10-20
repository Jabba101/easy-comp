#include "PluginEditor.h"

namespace
{
    //==========================================================================
    // The plain-English explanations shown in the hint bar. Edit freely!
    struct KnobInfo
    {
        const char* parameterID;
        const char* name;
        const char* hint;
    };

    // In on-screen order (see knobGroups below for which panel each is in).
    const std::array<KnobInfo, 7> knobInfo
    {{
        { ParamIDs::threshold, "THRESHOLD",
          "The volume where squeezing starts. Turn it down (left) until the meter moves on your loudest hits. "
          "The lower it goes, the more of the sound gets compressed." },

        { ParamIDs::ratio, "RATIO",
          "How hard it squeezes once a hit goes over the threshold. "
          "2:1 is gentle, 4:1 is a solid all-rounder, 8:1 and up is heavy squashing." },

        { ParamIDs::attack, "ATTACK",
          "How quickly it grabs a hit. Slower (15-30 ms) lets the stick's crack through before it squeezes = more punch. "
          "Very fast (under 2 ms) shaves the hit off for a softer, flatter sound." },

        { ParamIDs::release, "RELEASE",
          "How quickly it lets go after a hit. Short = more ring and room between hits (lively, can 'pump'). "
          "Long = smoother and more even. Tip: aim for the meter to get back near zero just before the next hit." },

        { ParamIDs::makeup, "MAKE-UP",
          "Compressing makes things quieter, so this turns the result back up. Match the volume with the plugin on and off: "
          "louder always sounds 'better', so a fair comparison keeps your ears honest." },

        { ParamIDs::mix, "MIX",
          "Blends your original sound with the compressed one. 100% = fully compressed. Heavy settings at 30-50% is the "
          "classic 'parallel' drum trick: the hit of the original plus the thickness of the squashed copy." },

        { ParamIDs::lowCut, "IGNORE BELOW",
          "Stops low notes (kick, bass, floor-tom boom) from setting off the compressor. It only changes what it reacts "
          "to - never the tone of your sound. Try 80-150 Hz on a drum bus or overheads if the kick makes everything 'pump'." },
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

    constexpr const char* lowCutNote =
        "Keeps the kick and bass from setting it off. Only changes what it listens to - your sound isn't filtered.";

    constexpr const char* bypassHint =
        "Switches the compressor off so you hear your original sound - click on and off to compare. "
        "Tip: switch AUTO-GAIN on first, so you're comparing the sound itself, not one being louder than the other.";

    constexpr const char* autoGainHint =
        "Keeps the volume coming out the same as the volume going in, so BYPASS compares the sound, not the "
        "loudness. It listens over a few seconds and adjusts smoothly. While it's on, it sets MAKE-UP for you.";

    constexpr const char* makeupAutoHint =
        "AUTO-GAIN is on, so it's setting the volume for you - the box shows how much it's adding right now. "
        "Switch AUTO-GAIN off to set MAKE-UP by hand again.";

    constexpr const char* presetHint =
        "Starting points for drums. Pick one, then adjust THRESHOLD until the meter bounces about 3-6 dB on the hits "
        "(more if you want an obviously squashed sound).";

    constexpr const char* graphHint =
        "Along the bottom: how loud your sound goes in. Up the side: how loud it comes out. Dotted line = untouched. "
        "The bend is the THRESHOLD; the flatter the line after it, the higher the RATIO. The dot is your sound as it plays.";

    constexpr const char* meterHint =
        "How much the compressor is turning the sound down right now. Around 3-6 dB on each hit is a natural "
        "starting point; 10 dB or more is an obvious, squashed effect.";

    constexpr const char* startHint =
        "Hover over anything for a plain-English explanation. Pick a preset, then turn THRESHOLD down until the meter "
        "jumps on each hit. Double-click a knob to reset it; click a number to type a value.";

    //==========================================================================
    /** Draws one of the rounded panels with its caption. */
    void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& number, const juce::String& caption)
    {
        const auto bounds = area.toFloat();

        g.setColour (Palette::panel);
        g.fillRoundedRectangle (bounds, 10.0f);
        g.setColour (Palette::panelEdge);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

        auto captionArea = area.reduced (14, 0).withTrimmedTop (10).withHeight (22);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));

        if (number.isNotEmpty())
        {
            g.setColour (Palette::accent);
            g.drawText (number, captionArea.removeFromLeft (16), juce::Justification::centredLeft, false);
            g.setColour (Palette::textDim);
            g.drawText (caption, captionArea, juce::Justification::centredLeft, false);
        }
        else
        {
            g.setColour (Palette::textDim);
            g.drawText (caption, captionArea, juce::Justification::centred, false);
        }
    }
}

//==============================================================================
// Look and feel
//==============================================================================
EasyCompLookAndFeel::EasyCompLookAndFeel()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName ("Segoe UI");
   #endif

    setColour (juce::ResizableWindow::backgroundColourId,      Palette::background);

    setColour (juce::Slider::rotarySliderFillColourId,         Palette::accent);
    setColour (juce::Slider::rotarySliderOutlineColourId,      Palette::knobTrack);
    setColour (juce::Slider::thumbColourId,                    Palette::textBright);
    setColour (juce::Slider::textBoxTextColourId,              Palette::textBright);
    setColour (juce::Slider::textBoxBackgroundColourId,        juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId,           juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId,         Palette::accent.withAlpha (0.35f));

    setColour (juce::Label::textColourId,                      Palette::textBright);
    setColour (juce::Label::textWhenEditingColourId,           Palette::textBright);
    setColour (juce::Label::outlineWhenEditingColourId,        Palette::accent);
    setColour (juce::TextEditor::backgroundColourId,           Palette::knobBody);
    setColour (juce::TextEditor::textColourId,                 Palette::textBright);
    setColour (juce::TextEditor::highlightColourId,            Palette::accent.withAlpha (0.35f));
    setColour (juce::TextEditor::highlightedTextColourId,      Palette::textBright);
    setColour (juce::TextEditor::focusedOutlineColourId,       Palette::accent);
    setColour (juce::CaretComponent::caretColourId,            Palette::accent);

    setColour (juce::ComboBox::backgroundColourId,             Palette::panel);
    setColour (juce::ComboBox::outlineColourId,                Palette::panelEdge);
    setColour (juce::ComboBox::textColourId,                   Palette::textBright);
    setColour (juce::ComboBox::arrowColourId,                  Palette::accent);
    setColour (juce::ComboBox::focusedOutlineColourId,         Palette::accent);

    setColour (juce::PopupMenu::backgroundColourId,            Palette::panel);
    setColour (juce::PopupMenu::textColourId,                  Palette::textBright);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Palette::accent);
    setColour (juce::PopupMenu::highlightedTextColourId,       Palette::background);
}

void EasyCompLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPos, float startAngle, float endAngle,
                                            juce::Slider& slider)
{
    const auto area   = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
    const float size  = juce::jmin (area.getWidth(), area.getHeight());
    const auto centre = area.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    const float trackThickness = juce::jmax (4.0f, size * 0.07f);
    const float arcRadius      = (size - trackThickness) * 0.5f;
    const juce::PathStrokeType stroke (trackThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // A switched-off knob (MAKE-UP while AUTO-GAIN is on) is drawn faded.
    const float alpha = slider.isEnabled() ? 1.0f : 0.3f;

    // The grey track that the value travels along
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId).withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    // The coloured part showing the current value
    if (sliderPos > 0.0f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
        g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId).withMultipliedAlpha (alpha));
        g.strokePath (value, stroke);
    }

    // The knob itself
    const float bodyRadius = arcRadius - trackThickness * 1.5f;
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
    g.setColour (Palette::knobBody.withMultipliedAlpha (alpha));
    g.fillEllipse (body);
    g.setColour (Palette::panelEdge.brighter (0.25f).withMultipliedAlpha (alpha));
    g.drawEllipse (body, 1.0f);

    // The pointer line
    const float pointerWidth = juce::jmax (2.5f, size * 0.03f);
    juce::Path pointer;
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -bodyRadius * 0.82f,
                                 pointerWidth, bodyRadius * 0.5f, pointerWidth * 0.5f);
    g.setColour (slider.findColour (juce::Slider::thumbColourId).withMultipliedAlpha (alpha));
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
}

juce::Label* EasyCompLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* box = LookAndFeel_V4::createSliderTextBox (slider);
    box->setFont (juce::Font (juce::FontOptions (15.0f)));
    return box;
}

void EasyCompLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    // The value boxes under the knobs get a soft rounded background, so they
    // look like something you can click and type into.
    if (dynamic_cast<juce::Slider*> (label.getParentComponent()) != nullptr)
    {
        const auto bounds = label.getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (Palette::background);
        g.fillRoundedRectangle (bounds, 7.0f);

        if (label.isBeingEdited())
        {
            g.setColour (Palette::accent);
            g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        }
        else
        {
            g.setColour (label.findColour (juce::Label::textColourId));
            g.setFont (getLabelFont (label));
            g.drawFittedText (label.getText(), label.getLocalBounds(), juce::Justification::centred, 1);
        }

        return;
    }

    LookAndFeel_V4::drawLabel (g, label);
}

juce::Font EasyCompLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (15.0f));
}

juce::Font EasyCompLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (15.0f));
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
// Light-up buttons
//==============================================================================
LightButton::LightButton (const juce::String& buttonText, juce::Colour colourWhenOn)
    : juce::Button (buttonText), lightColour (colourWhenOn)
{
    setClickingTogglesState (true);   // each click flips it on/off
}

void LightButton::paintButton (juce::Graphics& g, bool isMouseOver, bool isMouseDown)
{
    const bool on = getToggleState();
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (on ? lightColour.withAlpha (0.14f) : Palette::panel);
    g.fillRoundedRectangle (bounds, 7.0f);

    g.setColour (on ? lightColour : (isMouseOver ? Palette::textDim : Palette::panelEdge));
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    // The LED, with a soft glow when lit
    auto content = bounds.reduced (12.0f, 0.0f);
    const auto led = content.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f);

    if (on)
    {
        g.setColour (lightColour.withAlpha (0.3f));
        g.fillEllipse (led.expanded (4.0f));
    }

    g.setColour (on ? lightColour : Palette::knobTrack);
    g.fillEllipse (led);

    content.removeFromLeft (9.0f);
    g.setColour (on || isMouseOver ? Palette::textBright : Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.5f, juce::Font::bold)));
    g.drawText (getButtonText(), content, juce::Justification::centredLeft, false);

    if (isMouseDown)
    {
        g.setColour (juce::Colours::black.withAlpha (0.15f));
        g.fillRoundedRectangle (bounds, 7.0f);
    }
}

//==============================================================================
// Hint bar
//==============================================================================
void HintBar::setHint (const juce::String& newTitle, const juce::String& newText)
{
    if (newTitle == title && newText == text)
        return;

    title = newTitle;
    text  = newText;
    repaint();
}

void HintBar::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (area, 10.0f);

    // The "?" badge
    const auto badge = area.removeFromLeft (area.getHeight()).withSizeKeepingCentre (26.0f, 26.0f);
    g.setColour (Palette::accent);
    g.fillEllipse (badge);
    g.setColour (Palette::background);
    g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
    g.drawText ("?", badge, juce::Justification::centred, false);

    // Title in amber, then the explanation
    juce::AttributedString message;
    message.append (title + "    ", juce::Font (juce::FontOptions (14.5f, juce::Font::bold)), Palette::accent);
    message.append (text, juce::Font (juce::FontOptions (14.5f)), Palette::textBright);
    message.setJustification (juce::Justification::centredLeft);
    message.setWordWrap (juce::AttributedString::byWord);
    message.setLineSpacing (2.0f);
    message.draw (g, area.withTrimmedRight (18.0f).reduced (0.0f, 6.0f));
}

//==============================================================================
// The plugin window
//==============================================================================
EasyCompAudioProcessorEditor::EasyCompAudioProcessorEditor (EasyCompAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& knob = knobs[i];

        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);   // drag up/down or left/right
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 24);
        knob.slider.addMouseListener (this, true);   // hovering it updates the hint bar
        addAndMakeVisible (knob.slider);

        knob.label.setText (knobInfo[i].name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.addMouseListener (this, false);
        addAndMakeVisible (knob.label);

        // Connects the knob to its parameter: from now on they move together,
        // and the box under the knob shows the value ("-20.0 dB", "4.0 : 1"...).
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            audioProcessor.parameters, knobInfo[i].parameterID, knob.slider);

        knob.slider.onValueChange = [this] { markPresetAsEdited(); };
    }

    // Preset menu
    presetMenu.setTextWhenNothingSelected ("Drum presets...");

    for (size_t i = 0; i < drumPresets.size(); ++i)
        presetMenu.addItem (drumPresets[i].name, (int) i + 1);   // menu item IDs must start at 1

    presetMenu.onChange = [this]
    {
        if (const int index = presetMenu.getSelectedItemIndex(); index >= 0)
        {
            const juce::ScopedValueSetter<bool> loading (isLoadingPreset, true);
            audioProcessor.loadPreset (index);
        }
    };

    presetMenu.addMouseListener (this, true);
    addAndMakeVisible (presetMenu);

    // BYPASS and AUTO-GAIN buttons, connected to their settings
    bypassAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.parameters, ParamIDs::bypass, bypassButton);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        audioProcessor.parameters, ParamIDs::autoGain, autoGainButton);

    for (auto* button : { &bypassButton, &autoGainButton })
    {
        button->addMouseListener (this, false);
        addAndMakeVisible (*button);
    }

    // While AUTO-GAIN is on, the MAKE-UP box shows how much it's adding.
    auto& makeupSlider = knobs[makeupKnob].slider;
    makeupSlider.textFromValueFunction = [this, manualText = makeupSlider.textFromValueFunction] (double value)
    {
        if (showingAutoGain)
            return "Auto +" + juce::String (shownAutoGainDb, 1) + " dB";

        return manualText != nullptr ? manualText (value) : juce::String (value, 1);
    };

    graph.addMouseListener (this, false);
    addAndMakeVisible (graph);

    meter.addMouseListener (this, false);
    addAndMakeVisible (meter);

    addAndMakeVisible (hintBar);
    showHintFor (nullptr);

    // Applied last, once everything has been added, so every part of the
    // window picks up the custom look (including the value boxes).
    setLookAndFeel (&lookAndFeel);

    setSize (1100, 600);
    refreshLiveDisplays();
    startTimerHz (30);   // refresh the meter, graph and AUTO-GAIN readout 30 times a second
}

EasyCompAudioProcessorEditor::~EasyCompAudioProcessorEditor()
{
    stopTimer();

    for (auto& knob : knobs)
    {
        knob.slider.removeMouseListener (this);
        knob.label.removeMouseListener (this);
    }

    presetMenu.removeMouseListener (this);
    bypassButton.removeMouseListener (this);
    autoGainButton.removeMouseListener (this);
    graph.removeMouseListener (this);
    meter.removeMouseListener (this);

    setLookAndFeel (nullptr);
}

//==============================================================================
void EasyCompAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Palette::background);

    // Title
    auto title = titleArea;
    g.setColour (Palette::textBright);
    g.setFont (juce::Font (juce::FontOptions (26.0f, juce::Font::bold)));
    g.drawText ("EASY COMP", title.removeFromTop (30), juce::Justification::bottomLeft, false);

    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.drawText ("A simple compressor - hover over anything for help", title,
                juce::Justification::centredLeft, false);

    // The four knob panels, numbered in the order you'd usually set them
    for (size_t i = 0; i < groupPanels.size(); ++i)
        drawPanel (g, groupPanels[i], juce::String ((int) i + 1), knobGroups[i].name);

    // A short note beside the IGNORE BELOW knob, since it's the least obvious one
    juce::AttributedString note;
    note.append (lowCutNote, juce::Font (juce::FontOptions (13.0f)), Palette::textDim);
    note.setJustification (juce::Justification::centredLeft);
    note.setWordWrap (juce::AttributedString::byWord);
    note.setLineSpacing (2.0f);
    note.draw (g, lowCutNoteArea.toFloat());

    drawPanel (g, graphPanel, {}, "COMPRESSION CURVE");
    drawPanel (g, meterPanel, {}, "GAIN REDUCTION");
}

void EasyCompAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    // Header: title on the left; BYPASS, AUTO-GAIN and the presets on the right
    auto header = area.removeFromTop (50);
    presetMenu.setBounds (header.removeFromRight (300).withSizeKeepingCentre (300, 34));
    header.removeFromRight (10);
    autoGainButton.setBounds (header.removeFromRight (136).withSizeKeepingCentre (136, 34));
    header.removeFromRight (10);
    bypassButton.setBounds (header.removeFromRight (116).withSizeKeepingCentre (116, 34));
    titleArea = header;
    area.removeFromTop (14);

    // Hint bar along the bottom
    hintBar.setBounds (area.removeFromBottom (66));
    area.removeFromBottom (14);

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
            lowCutNoteArea = inside.reduced (4, 0);   // the spare column holds the short note
    }
}

//==============================================================================
void EasyCompAudioProcessorEditor::timerCallback()
{
    refreshLiveDisplays();
}

void EasyCompAudioProcessorEditor::refreshLiveDisplays()
{
    const auto settings = audioProcessor.getCurrentSettings();
    const float autoGainDb = audioProcessor.getAutoGainDb();

    meter.setGainReduction (audioProcessor.takeGainReductionForMeter());
    graph.update (settings, audioProcessor.takeInputLevelForGraph(), autoGainDb);

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

void EasyCompAudioProcessorEditor::mouseEnter (const juce::MouseEvent& event)
{
    showHintFor (event.eventComponent);
}

void EasyCompAudioProcessorEditor::mouseExit (const juce::MouseEvent&)
{
    showHintFor (nullptr);
}

void EasyCompAudioProcessorEditor::showHintFor (const juce::Component* component)
{
    const auto isPartOf = [component] (const juce::Component& owner)
    {
        return component != nullptr && (component == &owner || owner.isParentOf (component));
    };

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        if (isPartOf (knobs[i].slider) || isPartOf (knobs[i].label))
        {
            hintBar.setHint (knobInfo[i].name, i == makeupKnob && showingAutoGain ? makeupAutoHint : knobInfo[i].hint);
            return;
        }
    }

    if (isPartOf (bypassButton))
        hintBar.setHint ("BYPASS", bypassHint);
    else if (isPartOf (autoGainButton))
        hintBar.setHint ("AUTO-GAIN", autoGainHint);
    else if (isPartOf (presetMenu))
        hintBar.setHint ("PRESETS", presetHint);
    else if (isPartOf (graph))
        hintBar.setHint ("COMPRESSION CURVE", graphHint);
    else if (isPartOf (meter))
        hintBar.setHint ("GAIN REDUCTION", meterHint);
    else
        hintBar.setHint ("GETTING STARTED", startHint);
}

void EasyCompAudioProcessorEditor::markPresetAsEdited()
{
    // Once you move a knob, the menu shows e.g. "Kick - Punch (edited)", and
    // picking that preset again from the menu takes you back to the original.
    if (isLoadingPreset)
        return;

    if (const int index = presetMenu.getSelectedItemIndex(); index >= 0)
        presetMenu.setText (presetMenu.getItemText (index) + " (edited)", juce::dontSendNotification);
}

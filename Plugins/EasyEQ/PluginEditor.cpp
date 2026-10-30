#include "PluginEditor.h"
#include "Parameters.h"

namespace
{
    //==========================================================================
    // The plain-English explanations shown in the help bar. Edit freely!
    const std::array<const char*, 4> bandNames { "LOW", "LOW MID", "HIGH MID", "HIGH" };

    const std::array<const char*, 4> bandHints
    {
        "Weight and boom: kick and bass live around 50-100 Hz. Turn up for more weight, down for a tighter, cleaner low end. "
        "SHELF lifts or lowers everything below the frequency; BELL just a band around it.",
        "Body and mud: 150-250 Hz is body (snare, toms, guitars); 300-600 Hz is where mud and boxiness build up. Cutting a "
        "little here is the most common EQ move there is. Hold LISTEN to find the worst spot.",
        "Presence, attack and bite: 1-3 kHz is crunch and nasal tone, 2-5 kHz brings a vocal forward and adds stick and "
        "beater attack. Easy to overdo: a little goes a long way, and too much sounds harsh.",
        "Sparkle and air: 5-9 kHz is sibilance and cymbal sizzle, above 10 kHz is air. SHELF lifts or lowers everything "
        "above the frequency; BELL just a band around it.",
    };

    constexpr const char* freqHint =
        "Where this band works. Drag the dot on the screen to set the frequency and the gain at once.";
    constexpr const char* gainHint =
        "How much this band boosts (up) or cuts (down). The harder you push it, the narrower the band gets - gentle moves "
        "stay broad and musical, big ones get precise. 0 = no change.";

    constexpr const char* hpfHint =
        "High-pass filter: removes rumble and boom below this frequency. Most things except kick and bass are cleaner with "
        "it at 60-120 Hz. 12 or 24: how steeply it cuts. Off at its lowest.";
    constexpr const char* lpfHint =
        "Low-pass filter: removes top end above this frequency - fizz from distortion, hiss, harshness. Off at its highest. "
        "12 or 24: how steeply it cuts.";
    constexpr const char* monoBassHint =
        "Keeps everything below this frequency in the middle, so the bass and kick are solid and centred (and safe for clubs "
        "and vinyl). Only does anything on stereo tracks. Off at its lowest. Try 80-120 Hz on a mix bus.";
    constexpr const char* targetHint =
        "What this band works on. ST = the whole stereo sound. MID = only what's the same in both speakers (vocal, kick, "
        "snare, bass). SIDE = only what's different (room, width, stereo guitars and keys). On a mono track only ST and MID do anything.";
    constexpr const char* shapeHint =
        "BELL boosts or cuts a band around the frequency. SHELF boosts or cuts everything beyond it (below it for LOW, above "
        "it for HIGH).";
    constexpr const char* slopeHint = "How steeply the filter cuts: 12 dB per octave is gentle and natural, 24 is steep and tight.";
    constexpr const char* listenHint =
        "Hold it down to hear only what this band covers - the quickest way to find a boomy, muddy, nasal or harsh frequency "
        "by ear. Move the frequency while you hold it.";
    constexpr const char* masterHint =
        "Linear phase, for a drum bus or a finished mix: keeps the timing of every frequency perfectly intact, so hits keep "
        "their attack. Adds a delay of about 43 ms (less with PRECISION; your DAW lines it up) and uses more computer power. Not for live recording.";
    constexpr const char* precisionHint =
        "MASTER's precision. HIGH (the usual): the most exact low end, a delay of about 43 ms. MEDIUM: about 21 ms. "
        "LOW: about 11 ms, and less faint smear just before each hit (good on drums), but bands below about 60 Hz "
        "come out a little broader. Changing it changes the delay (your DAW re-aligns).";
    constexpr const char* analyserHint =
        "Shows the sound coming out, faintly behind the curve, so you can see where the energy is. Switch it off to save a "
        "little computer power.";
    constexpr const char* displayHint =
        "The EQ's curve: up = boosted, down = cut. Drag a band's dot across for frequency, up and down for gain; double-click "
        "it to go back to 0 dB. MID (cyan) and SIDE (magenta) get a curve each when they differ.";
    constexpr const char* initAllHint =
        "Puts every setting back to its default (the same as the 'Default settings' preset): flat, nothing changed. "
        "BYPASS, AUTO-GAIN, MASTER and the analyser stay as they are.";
    constexpr const char* slotHint =
        "A and B: two sets of settings to compare. Switch between them while the track plays to hear which you prefer. B "
        "starts as a copy of A the first time you switch to it; the one you're not on is kept with your project.";
    constexpr const char* copyHint =
        "Copies the settings you're on to the other one (A to B, or B to A) - then change one of them and compare.";

    constexpr const char* gettingStartedHint =
        "Pick a preset, or drag a band's dot on the screen. Hold a band's LISTEN to hear what it covers. Cut before you "
        "boost. Double-click a knob to reset it, or its number to type a value.";
}

//==============================================================================
// The screen
//==============================================================================
EQDisplay::EQDisplay (EasyEQAudioProcessor& p) : processor (p)
{
    spectrumDb.fill (-120.0f);
}

juce::Colour EQDisplay::bandColour (int band)
{
    static const juce::Colour colours[] { juce::Colour (0xffff9f43), juce::Colour (0xfffeca57), juce::Colour (0xff48dbfb), juce::Colour (0xffa29bfe) };
    return colours[juce::jlimit (0, 3, band)];
}

juce::Rectangle<float> EQDisplay::plotArea() const
{
    auto area = getLocalBounds().toFloat();
    area.removeFromLeft (34.0f);
    area.removeFromBottom (18.0f);
    area.removeFromTop (4.0f);
    area.removeFromRight (6.0f);
    return area;
}

float EQDisplay::xFor (double hz) const
{
    const auto p = plotArea();
    return p.getX() + (float) (std::log (juce::jlimit (20.0, 20000.0, hz) / 20.0) / std::log (1000.0)) * p.getWidth();
}

double EQDisplay::hzFor (float x) const
{
    const auto p = plotArea();
    return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) ((x - p.getX()) / p.getWidth())));
}

float EQDisplay::yFor (double db) const
{
    const auto p = plotArea();
    return p.getCentreY() - (float) (juce::jlimit ((double) -rangeDb, (double) rangeDb, db) / rangeDb) * p.getHeight() * 0.5f;
}

double EQDisplay::dbFor (float y) const
{
    const auto p = plotArea();
    return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * rangeDb;
}

juce::Point<float> EQDisplay::dotPosition (int band) const
{
    const auto& b = shown.bands[(size_t) band];
    return { xFor (b.freqHz), yFor (b.gainDb) };
}

int EQDisplay::bandAt (juce::Point<float> p) const
{
    int best = -1;
    float bestDistance = 14.0f;

    for (int b = 0; b < Equalizer::numBands; ++b)
    {
        const float d = dotPosition (b).getDistanceFrom (p);
        if (d < bestDistance) { bestDistance = d; best = b; }
    }

    return best;
}

void EQDisplay::refresh (bool analyserIsOn)
{
    shown = processor.getCurrentSettings();
    bypassed = shown.bypassed;
    analyserOn = analyserIsOn;

    showMidSide = shown.monoBassHz > Equalizer::monoBassOffHz + 0.5f;
    for (auto& b : shown.bands)
        if (b.target != Equalizer::Target::stereo && std::abs (b.gainDb) > 0.001f)
            showMidSide = true;

    if (analyserOn)
    {
        // The latest 4096 samples, windowed, into the FFT; each bin falls back gently
        constexpr int n = EasyEQAudioProcessor::analyserLength;
        processor.copyRecentOutput (fftData.data(), n);
        for (int i = 0; i < n; ++i)
            fftData[(size_t) i] *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / (n - 1)));
        std::fill (fftData.begin() + n, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        for (size_t k = 0; k < spectrumDb.size(); ++k)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[k] * 4.0f / (float) n, -120.0f);
            spectrumDb[k] = db > spectrumDb[k] ? db : spectrumDb[k] - 1.2f;
        }
    }

    repaint();
}

void EQDisplay::paint (juce::Graphics& g)
{
    const auto plot = plotArea();
    const double rate = processor.getCurrentSampleRate();

    g.setColour (Palette::background);
    g.fillRoundedRectangle (plot.expanded (1.0f), 6.0f);

    // The analyser: the sound coming out, faint, behind everything (its own scale: -90 to -10 dBFS)
    if (analyserOn)
    {
        juce::Path spectrum;
        spectrum.startNewSubPath (plot.getBottomLeft());
        const double binHz = rate / EasyEQAudioProcessor::analyserLength;

        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double hz = hzFor (x);
            const auto k = (size_t) juce::jlimit (1, (int) spectrumDb.size() - 2, (int) std::round (hz / binHz));
            const float db = juce::jmax (spectrumDb[k - 1], spectrumDb[k], spectrumDb[k + 1]) + 3.0f * (float) std::log2 (juce::jmax (1.0, hz / 1000.0));   // (tilted, like a pink-noise view)
            const float y = plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 90.0f) / 80.0f) * plot.getHeight();
            spectrum.lineTo (x, y);
        }

        spectrum.lineTo (plot.getBottomRight());
        spectrum.closeSubPath();
        g.setColour (Palette::textDim.withAlpha (0.22f));
        g.fillPath (spectrum);
    }

    // Grid: octaves-ish across, every 6 dB up and down
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        g.setColour (Palette::panelEdge);
        g.drawVerticalLine (juce::roundToInt (xFor (hz)), plot.getY(), plot.getBottom());
        g.setColour (Palette::textDim);
        const juce::String label = hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz);
        g.drawText (label, juce::Rectangle<float> (xFor (hz) - 20.0f, plot.getBottom() + 2.0f, 40.0f, 14.0f), juce::Justification::centred, false);
    }

    for (int db = -12; db <= 12; db += 6)
    {
        g.setColour (db == 0 ? Palette::textDim.withAlpha (0.5f) : Palette::panelEdge);
        g.drawHorizontalLine (juce::roundToInt (yFor (db)), plot.getX(), plot.getRight());
        g.setColour (Palette::textDim);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), juce::Rectangle<float> (0.0f, yFor (db) - 7.0f, plot.getX() - 6.0f, 14.0f),
                    juce::Justification::centredRight, false);
    }

    // The curve(s)
    const auto curve = [&] (bool mid)
    {
        juce::Path p;
        bool first = true;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double db = juce::Decibels::gainToDecibels (Equalizer::responseAt (shown, hzFor (x), rate, mid), -60.0);
            const juce::Point<float> point (x, yFor (db));
            if (first) p.startNewSubPath (point); else p.lineTo (point);
            first = false;
        }
        return p;
    };

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (plot.toNearestInt());
        const juce::PathStrokeType stroke (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        if (showMidSide)
        {
            g.setColour (juce::Colour (0xffff5fd2));
            g.strokePath (curve (false), stroke);
            g.setColour (juce::Colour (0xff4fe3ff));
            g.strokePath (curve (true), stroke);
        }
        else
        {
            const auto main = curve (true);
            juce::Path fill (main);
            fill.lineTo (plot.getRight(), yFor (0.0));
            fill.lineTo (plot.getX(), yFor (0.0));
            fill.closeSubPath();
            g.setColour (Palette::accent.withAlpha (0.12f));
            g.fillPath (fill);
            g.setColour (Palette::accent);
            g.strokePath (main, stroke);
        }
    }

    if (showMidSide)
    {
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0xff4fe3ff));
        g.drawText ("MID", juce::Rectangle<float> (plot.getRight() - 90.0f, plot.getY() + 6.0f, 36.0f, 14.0f), juce::Justification::centredRight, false);
        g.setColour (juce::Colour (0xffff5fd2));
        g.drawText ("SIDE", juce::Rectangle<float> (plot.getRight() - 50.0f, plot.getY() + 6.0f, 40.0f, 14.0f), juce::Justification::centredRight, false);
    }

    // The dots
    for (int b = 0; b < Equalizer::numBands; ++b)
    {
        const auto p = dotPosition (b);
        const bool lit = b == hovered || b == dragging;
        const float r = lit ? 9.0f : 7.5f;
        g.setColour (bandColour (b).withAlpha (std::abs (shown.bands[(size_t) b].gainDb) > 0.001f ? 1.0f : 0.55f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p));
        g.setColour (Palette::background);
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText (juce::String (b + 1), juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (p), juce::Justification::centred, false);

        if (lit)
        {
            const auto& band = shown.bands[(size_t) b];
            g.setColour (Palette::textBright);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
            const juce::String text = Format::hertz (band.freqHz) + "  " + Format::signedDecibels (band.gainDb);
            auto box = juce::Rectangle<float> (130.0f, 16.0f).withCentre (p.translated (0.0f, p.y > plot.getCentreY() ? -20.0f : 20.0f));
            box = box.constrainedWithin (plot);
            g.drawText (text, box, juce::Justification::centred, false);
        }
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

void EQDisplay::setParameter (const char* id, float value)
{
    if (auto* p = processor.parameters.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EQDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b != hovered) { hovered = b; repaint(); }
}

void EQDisplay::mouseExit (const juce::MouseEvent&)
{
    if (hovered >= 0 && dragging < 0) { hovered = -1; repaint(); }
}

void EQDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = bandAt (e.position);
    if (dragging < 0) return;

    for (auto* id : { EQIDs::freq[dragging], EQIDs::gain[dragging] })
        if (auto* p = processor.parameters.getParameter (id)) p->beginChangeGesture();
}

void EQDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;

    setParameter (EQIDs::freq[dragging], (float) hzFor (e.position.x));   // (each band's own range limits it)
    setParameter (EQIDs::gain[dragging], (float) juce::jlimit ((double) -Equalizer::maxGainDb, (double) Equalizer::maxGainDb, dbFor (e.position.y)));
    if (onUserChange) onUserChange();
    refresh (analyserOn);
}

void EQDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging < 0) return;

    for (auto* id : { EQIDs::freq[dragging], EQIDs::gain[dragging] })
        if (auto* p = processor.parameters.getParameter (id)) p->endChangeGesture();
    dragging = -1;
}

void EQDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int b = bandAt (e.position);
    if (b < 0) return;

    if (auto* p = processor.parameters.getParameter (EQIDs::gain[b]))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (0.0f));
        p->endChangeGesture();
    }
    if (onUserChange) onUserChange();
    refresh (analyserOn);
}

//==============================================================================
// The Easy EQ window
//==============================================================================
EasyEQAudioProcessorEditor::EasyEQAudioProcessorEditor (EasyEQAudioProcessor& p)
    : EasyWindow (p, "EASY EQ", "Shape the tone - hover over anything for help", gettingStartedHint),
      eqProcessor (p),
      display (p)
{
    auto& parameters = easyProcessor.parameters;

    const auto setUpKnob = [&] (Knob& knob, const char* id, const juce::String& name, const juce::String& hintTitle, const juce::String& hint)
    {
        knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 22);
        addAndMakeVisible (knob.slider);
        knob.label.setText (name, juce::dontSendNotification);
        knob.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        knob.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (knob.label);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (parameters, id, knob.slider);
        knob.slider.onValueChange = [this] { knobMoved(); };
        addHint (knob.slider, hintTitle, hint);
        addHint (knob.label, hintTitle, hint);
    };

    for (int b = 0; b < 4; ++b)
    {
        const juce::String band (bandNames[(size_t) b]);
        setUpKnob (knobs[(size_t) (b * 2)],     EQIDs::freq[b], "FREQ", band + " - FREQ", juce::String (freqHint) + " " + bandHints[(size_t) b]);
        setUpKnob (knobs[(size_t) (b * 2 + 1)], EQIDs::gain[b], "GAIN", band + " - GAIN", juce::String (gainHint) + " " + bandHints[(size_t) b]);
        knobs[(size_t) (b * 2 + 1)].slider.getProperties().set ("fillFromCentre", true);
        knobs[(size_t) (b * 2 + 1)].slider.setColour (juce::Slider::rotarySliderFillColourId, EQDisplay::bandColour (b));

        targetSwitches[(size_t) b] = std::make_unique<ChoiceSwitch> (*parameters.getParameter (EQIDs::target[b]), juce::StringArray { "ST", "MID", "SIDE" }, Palette::accent);
        targetSwitches[(size_t) b]->setHorizontal (true);
        targetSwitches[(size_t) b]->onUserChange = [this] { knobMoved(); };
        addAndMakeVisible (*targetSwitches[(size_t) b]);
        addHint (*targetSwitches[(size_t) b], band + " - WORKS ON", targetHint);

        auto& listen = listenButtons[(size_t) b];
        listen.setButtonText ("LISTEN");
        listen.setColour (juce::TextButton::buttonColourId, Palette::panel);
        listen.setColour (juce::TextButton::buttonOnColourId, EQDisplay::bandColour (b));
        listen.setColour (juce::TextButton::textColourOffId, Palette::textDim);
        listen.onHold = [this, b] (bool down)
        {
            if (down)                                   eqProcessor.setListenBand (b);
            else if (eqProcessor.getListenBand() == b)  eqProcessor.setListenBand (-1);
            listenButtons[(size_t) b].setToggleState (down, juce::dontSendNotification);
        };
        addAndMakeVisible (listen);
        addHint (listen, band + " - LISTEN", listenHint);
    }

    setUpKnob (knobs[hpfKnob], EQIDs::hpf, "HPF", "HPF", hpfHint);
    setUpKnob (knobs[lpfKnob], EQIDs::lpf, "LPF", "LPF", lpfHint);
    setUpKnob (knobs[monoBassKnob], EQIDs::monoBass, "MONO BASS", "MONO BASS", monoBassHint);
    knobs[lpfKnob].slider.getProperties().set ("fillFromEnd", true);

    const auto makeSwitch = [&] (std::unique_ptr<ChoiceSwitch>& s, const char* id, juce::StringArray names, const juce::String& hintTitle, const char* hint)
    {
        s = std::make_unique<ChoiceSwitch> (*parameters.getParameter (id), names, Palette::accent);
        s->setHorizontal (true);
        s->onUserChange = [this] { knobMoved(); };
        addAndMakeVisible (*s);
        addHint (*s, hintTitle, hint);
    };

    makeSwitch (lowShape, EQIDs::lowShelf, { "BELL", "SHELF" }, "LOW - SHAPE", shapeHint);
    makeSwitch (highShape, EQIDs::highShelf, { "BELL", "SHELF" }, "HIGH - SHAPE", shapeHint);
    makeSwitch (hpfSlope, EQIDs::hpfSlope, { "12", "24" }, "HPF - SLOPE", slopeHint);
    makeSwitch (lpfSlope, EQIDs::lpfSlope, { "12", "24" }, "LPF - SLOPE", slopeHint);

    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, EQIDs::master, masterButton);
    analyserAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (parameters, EQIDs::analyser, analyserButton);
    addAndMakeVisible (masterButton);
    addAndMakeVisible (analyserButton);
    addHint (masterButton, "MASTER (LINEAR PHASE)", masterHint);
    precisionSwitch = std::make_unique<ChoiceSwitch> (*parameters.getParameter (EQIDs::precision), juce::StringArray { "LOW", "MED", "HIGH" },
                                                      juce::Colour (0xff4f8cff));
    precisionSwitch->setHorizontal (true);
    addAndMakeVisible (*precisionSwitch);
    addHint (*precisionSwitch, "MASTER - PRECISION", precisionHint);
    addHint (analyserButton, "ANALYSER", analyserHint);

    initAllButton.setColour (juce::TextButton::buttonColourId, Palette::panel);
    initAllButton.setColour (juce::TextButton::textColourOffId, Palette::meterHigh);
    initAllButton.setColour (juce::ComboBox::outlineColourId, Palette::meterHigh);
    initAllButton.onClick = [this] { presetBrowser.onPresetChosen (easyProcessor.getBuiltInPresets().front()); };   // "Default settings"
    addAndMakeVisible (initAllButton);
    addHint (initAllButton, "INIT", initAllHint);

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

    display.onUserChange = [this] { knobMoved(); };
    addAndMakeVisible (display);
    addHint (display, "THE CURVE", displayHint);

    finishSetup (1100, 800);

    // Double-click a number to type a value
    for (auto& knob : knobs)
        for (auto* child : knob.slider.getChildren())
            if (auto* box = dynamic_cast<juce::Label*> (child))
                box->setEditable (false, true, false);
}

//==============================================================================
void EasyEQAudioProcessorEditor::paintBody (juce::Graphics& g)
{
    drawPanel (g, displayPanel, {}, {});
    drawPanel (g, cleanPanel, "1", "CLEAN UP");
    drawPanel (g, shapePanel, "2", "SHAPE");

    // Each band's name, in its colour, with a thin line between bands
    for (int b = 0; b < 4; ++b)
    {
        const auto column = bandColumns[(size_t) b];
        g.setColour (EQDisplay::bandColour (b));
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (juce::String (b + 1) + "  " + bandNames[(size_t) b], column.withHeight (18), juce::Justification::centred, false);

        if (b > 0)
        {
            g.setColour (Palette::panelEdge);
            g.drawVerticalLine (column.getX() - 5, (float) column.getY(), (float) column.getBottom());
        }
    }

    // Dimmed MID / SIDE note on mono tracks
    if (monoTrack)
    {
        g.setColour (Palette::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Mono track: MID = the whole sound, SIDE does nothing", shapePanel.withTrimmedTop (8).withHeight (16).withTrimmedRight (14),
                    juce::Justification::centredRight, false);
    }

    // AUTO-GAIN readout
    if (easyProcessor.parameters.getRawParameterValue (CommonIDs::autoGain)->load() > 0.5f)
    {
        g.setColour (Palette::meterLow);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("AUTO-GAIN " + juce::String (shownAutoGainDb >= 0.05f ? "+" : "") + juce::String (shownAutoGainDb, 1) + " dB",
                    autoGainReadout, juce::Justification::centred, false);
    }

    // The frequency guide
    const auto guide = guideArea.toFloat();
    g.setColour (Palette::panel);
    g.fillRoundedRectangle (guide, 8.0f);
    g.setColour (Palette::panelEdge);
    g.drawRoundedRectangle (guide.reduced (0.5f), 8.0f, 1.0f);

    auto rows = guideArea.reduced (14, 5);
    const auto row1 = rows.removeFromTop (rows.getHeight() / 2), row2 = rows;
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    const juce::StringArray items { "WEIGHT 50-100", "BODY 150-250", "MUD 300-600", "CRUNCH 1-3k", "PRESENCE 2-5k", "ATTACK 2-5k", "SIBILANCE 5-9k", "AIR 10k +" };
    const float itemWidth = (float) row1.getWidth() / (float) items.size();

    for (int i = 0; i < items.size(); ++i)
    {
        g.setColour (Palette::textDim);
        g.drawText (items[i], juce::Rectangle<float> ((float) row1.getX() + itemWidth * (float) i, (float) row1.getY(), itemWidth, (float) row1.getHeight()),
                    juce::Justification::centredLeft, false);
    }

    g.setColour (Palette::accent);
    g.setFont (juce::Font (juce::FontOptions (12.5f, juce::Font::bold)));
    g.drawText ("VOCALS:", juce::Rectangle<float> ((float) row2.getX(), (float) row2.getY(), itemWidth, (float) row2.getHeight()), juce::Justification::centredLeft, false);
    g.setColour (Palette::textDim);
    g.setFont (juce::Font (juce::FontOptions (12.5f)));
    const juce::StringArray vocals { "BOXY VOX 300-600", "NASAL VOX 800-1.5k", "SIBILANT VOX 5-8k" };
    for (int i = 0; i < vocals.size(); ++i)
        g.drawText (vocals[i], juce::Rectangle<float> ((float) row2.getX() + itemWidth * (float) (1 + i * 2), (float) row2.getY(), itemWidth * 2.0f, (float) row2.getHeight()),
                    juce::Justification::centredLeft, false);
}

void EasyEQAudioProcessorEditor::layoutBody (juce::Rectangle<int> area)
{
    constexpr int gap = 14;

    // The header: INIT small, right beside the preset box; AUTO-GAIN between it and BYPASS
    const auto preset = presetBrowser.getBounds();
    initAllButton.setBounds (preset.getX() - 10 - 52, preset.getCentreY() - 13, 52, 26);
    autoGainButton.setBounds (autoGainButton.getBounds().withX (initAllButton.getX() - 24 - autoGainButton.getWidth()));
    bypassButton.setBounds (bypassButton.getBounds().withX (autoGainButton.getX() - 10 - bypassButton.getWidth()));

    // Top row: ANALYSER on the left, MASTER on the right, AUTO-GAIN's readout between
    auto row = area.removeFromTop (32);
    analyserButton.setBounds (row.removeFromLeft (140));
    masterButton.setBounds (row.removeFromRight (120));
    row.removeFromRight (6);
    precisionSwitch->setBounds (row.removeFromRight (186));

    // A / B and COPY in the middle; AUTO-GAIN's readout to their left
    auto abArea = row.withSizeKeepingCentre (48 + 6 + 48 + 12 + 120, row.getHeight());
    slotA.setBounds (abArea.removeFromLeft (48));
    abArea.removeFromLeft (6);
    slotB.setBounds (abArea.removeFromLeft (48));
    abArea.removeFromLeft (12);
    copyButton.setBounds (abArea.removeFromLeft (120).reduced (0, 2));
    autoGainReadout = row.withRight (slotA.getX() - 10).withTrimmedLeft (10);
    area.removeFromTop (gap);

    // The frequency guide along the bottom
    guideArea = area.removeFromBottom (44);
    area.removeFromBottom (10);

    // The screen
    displayPanel = area.removeFromTop (240);
    display.setBounds (displayPanel.reduced (10, 8));
    area.removeFromTop (gap);

    // CLEAN UP and SHAPE
    cleanPanel = area.removeFromLeft (270);
    area.removeFromLeft (gap);
    shapePanel = area;

    constexpr int labelHeight = 16, knobSize = 62, valueHeight = 22, switchHeight = 24;

    const auto placeKnob = [&] (Knob& knob, juce::Rectangle<int> column)
    {
        knob.label.setBounds (column.removeFromTop (labelHeight));
        knob.slider.setBounds (column.removeFromTop (knobSize + valueHeight).withSizeKeepingCentre (juce::jmin (column.getWidth(), 86), knobSize + valueHeight));
    };

    {
        auto inside = cleanPanel.reduced (10, 12).withTrimmedTop (30);
        const int w = inside.getWidth() / 3;
        auto hpfColumn = inside.removeFromLeft (w), monoColumn = inside.removeFromLeft (w), lpfColumn = inside;
        placeKnob (knobs[hpfKnob], hpfColumn);
        placeKnob (knobs[monoBassKnob], monoColumn);
        placeKnob (knobs[lpfKnob], lpfColumn);
        hpfColumn.removeFromTop (labelHeight + knobSize + valueHeight + 8);
        lpfColumn.removeFromTop (labelHeight + knobSize + valueHeight + 8);
        hpfSlope->setBounds (hpfColumn.removeFromTop (switchHeight).reduced (4, 0));
        lpfSlope->setBounds (lpfColumn.removeFromTop (switchHeight).reduced (4, 0));
    }

    {
        auto inside = shapePanel.reduced (12, 12).withTrimmedTop (22);
        const int w = (inside.getWidth() - 3 * 10) / 4;

        for (int b = 0; b < 4; ++b)
        {
            auto column = inside.removeFromLeft (w);
            inside.removeFromLeft (10);
            bandColumns[(size_t) b] = column;

            column.removeFromTop (20);   // (the band's name)
            auto shapeRow = column.removeFromTop (switchHeight);
            if (b == 0) lowShape->setBounds (shapeRow.reduced (8, 0));
            if (b == 3) highShape->setBounds (shapeRow.reduced (8, 0));
            column.removeFromTop (4);

            auto knobRow = column.removeFromTop (labelHeight + knobSize + valueHeight);
            const int half = knobRow.getWidth() / 2;
            placeKnob (knobs[(size_t) (b * 2)], knobRow.removeFromLeft (half));
            placeKnob (knobs[(size_t) (b * 2 + 1)], knobRow);

            column.removeFromTop (6);
            targetSwitches[(size_t) b]->setBounds (column.removeFromTop (switchHeight).reduced (2, 0));
            column.removeFromTop (6);
            listenButtons[(size_t) b].setBounds (column.removeFromTop (switchHeight).reduced (24, 0));
        }
    }
}

//==============================================================================
void EasyEQAudioProcessorEditor::refreshLiveDisplays()
{
    const int slot = easyProcessor.getActiveSlot();
    slotA.setToggleState (slot == 0, juce::dontSendNotification);
    slotB.setToggleState (slot == 1, juce::dontSendNotification);
    copyButton.setButtonText (slot == 0 ? "COPY A > B" : "COPY B > A");

    display.refresh (easyProcessor.parameters.getRawParameterValue (EQIDs::analyser)->load() > 0.5f);

    const bool mono = eqProcessor.getTotalNumOutputChannels() < 2;
    if (mono != monoTrack)
    {
        monoTrack = mono;
        for (auto& s : targetSwitches)
            s->setAlpha (mono ? 0.5f : 1.0f);
        repaint();
    }

    const float rounded = std::round (eqProcessor.getAutoGainDb() * 10.0f) / 10.0f;
    if (std::abs (rounded - shownAutoGainDb) > 0.01f)
    {
        shownAutoGainDb = rounded;
        repaint (autoGainReadout);
    }
}

#include "HoloSlabs.h"

namespace Holo
{
    namespace
    {
        const juce::String initHint =
            "Init: puts every setting in this module back to its default. Nothing else on the strip changes.";
        const juce::String prHint =
            "Presets: load or save settings for this module only - the same list as its standalone plugin.";
        const juce::String powerHint =
            "Switches this module on or off. Off, it isn't run at all and costs nothing - the sound passes straight "
            "through, on time. (The same switch as the BYPASS button in its standalone plugin.)";
    }

    //==========================================================================
    Slab::Slab (View& v, int m, juce::Colour t, const juce::String& powerText)
        : power (powerText, t, Button::Style::power),
          init ("INIT", Colours::warn, Button::Style::init),
          pr ("Pr", t),
          view (v), strip (v.getStrip()), module (m), tint (t)
    {
        power.fontSize = 16.0f;
        powerLink = std::make_unique<ButtonLink> (param (CommonIDs::bypass), power, true);
        addAndMakeVisible (power);
        view.addHint (power, juce::String (StripModules::tabs[m]) + " ON / OFF", powerHint);

        init.fontSize = 13.0f;
        init.onClick = [this]
        {
            auto& p = strip.getModule (module);
            p.loadPreset (p.getBuiltInPresets().front());   // ("Default settings")
            init.flash = true;
            init.repaint();
            juce::Component::SafePointer<Slab> safe (this);
            juce::Timer::callAfterDelay (180, [safe]
            {
                if (safe != nullptr) { safe->init.flash = false; safe->init.repaint(); }
            });
        };
        addAndMakeVisible (init);
        view.addHint (init, "INIT", initHint);

        pr.fontSize = 13.0f;
        pr.onClick = [this] { view.showPresetMenu (module, pr); };
        addAndMakeVisible (pr);
        view.addHint (pr, "PRESETS", prHint);
    }

    Slab::~Slab() = default;

    juce::RangedAudioParameter& Slab::param (const char* id)
    {
        auto* p = strip.getModule (module).parameters.getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    void Slab::hint (juce::Component& c, const juce::String& standaloneTitle, const juce::String& shownTitle)
    {
        view.addHint (c, shownTitle.isNotEmpty() ? shownTitle : standaloneTitle, view.moduleHint (module, standaloneTitle));
    }

    Ring& Slab::ring (const char* id, const juce::String& title, Format f, const juce::String& unit,
                      Ring::Size size, juce::Colour colour, const juce::String& hintTitle)
    {
        auto r = std::make_unique<Ring> (param (id), title, f, unit, size, colour.isTransparent() ? tint : colour, title.isNotEmpty());
        auto& ref = *r;
        addAndMakeVisible (ref);
        hint (ref, hintTitle.isNotEmpty() ? hintTitle : title.replace ("|", " "));
        view.registerControl (module, id, ref);
        parts.push_back (std::move (r));
        return ref;
    }

    Bar& Slab::bar (const char* id, const juce::String& title, Format f, const juce::String& unit,
                    juce::Colour colour, bool centreZero, const juce::String& hintTitle)
    {
        auto b = std::make_unique<Bar> (param (id), title, f, unit, colour.isTransparent() ? tint : colour, centreZero);
        auto& ref = *b;
        addAndMakeVisible (ref);
        hint (ref, hintTitle.isNotEmpty() ? hintTitle : title);
        view.registerControl (module, id, ref);
        parts.push_back (std::move (b));
        return ref;
    }

    Choice& Slab::choice (const char* id, const juce::StringArray& names, const juce::String& hintTitle, bool sideBySide)
    {
        auto c = std::make_unique<Choice> (param (id), names, tint, sideBySide);
        auto& ref = *c;
        addAndMakeVisible (ref);
        hint (ref, hintTitle);
        parts.push_back (std::move (c));
        return ref;
    }

    Button& Slab::toggle (const char* id, const juce::String& text, const juce::String& hintTitle, Button::Style style, juce::Colour colour)
    {
        auto b = std::make_unique<Button> (text, colour.isTransparent() ? tint : colour, style);
        auto& ref = *b;
        links.push_back (std::make_unique<ButtonLink> (param (id), ref));
        addAndMakeVisible (ref);
        hint (ref, hintTitle);
        parts.push_back (std::move (b));
        return ref;
    }

    juce::Label& Slab::caption (const juce::String& text, juce::Colour colour, float size)
    {
        auto l = std::make_unique<juce::Label> (juce::String(), text);
        l->setFont (Holo::text (size, 1));
        l->setColour (juce::Label::textColourId, colour);
        l->setJustificationType (juce::Justification::centred);
        l->setBorderSize ({});
        l->setInterceptsMouseClicks (false, false);
        auto& ref = *l;
        addAndMakeVisible (ref);
        parts.push_back (std::move (l));
        return ref;
    }

    juce::Rectangle<int> Slab::placeHeader (juce::Rectangle<int> area, bool powerAtLeft, int powerWidth)
    {
        auto row = area.removeFromTop (34);
        const int powerHeight = power.getButtonText().contains ("|") ? 34 : 30;

        auto tools = row;
        pr.setBounds (tools.removeFromRight (28).withSizeKeepingCentre (28, 28));
        tools.removeFromRight (6);
        init.setBounds (tools.removeFromRight (40).withSizeKeepingCentre (40, 28));

        if (powerAtLeft)
            power.setBounds (row.getX(), row.getCentreY() - powerHeight / 2, powerWidth, powerHeight);
        else
            power.setBounds (row.getCentreX() - powerWidth / 2, row.getCentreY() - powerHeight / 2, powerWidth, powerHeight);

        area.removeFromTop (8);
        return area;
    }

    juce::Rectangle<int> Slab::section (juce::Rectangle<int> r, int number, const juce::String& title)
    {
        sections.push_back ({ r.toFloat(), number, title });
        return r.withTrimmedTop (title.isNotEmpty() ? 27 : 8).reduced (8, 0).withTrimmedBottom (7);
    }

    void Slab::centreIn (juce::Component& c, juce::Rectangle<int> column, int top)
    {
        c.setTopLeftPosition (column.getCentreX() - c.getWidth() / 2,
                              top >= 0 ? top : column.getCentreY() - c.getHeight() / 2);
    }

    void Slab::paint (juce::Graphics& g)
    {
        drawModule (g, getLocalBounds().toFloat(), tint, deepRed);
        for (auto& s : sections)
            drawSection (g, s.area, tint, s.number, s.title, deepRed);
    }

    //==========================================================================
    // GATE
    //==========================================================================
    class GateSlab : public Slab
    {
    public:
        explicit GateSlab (View& v) : Slab (v, 0, Colours::gate, "GATE"),
            meter (-80.0f, 0.0f, { 0.0f, -20.0f, -40.0f, -60.0f, -80.0f })
        {
            stateText = &caption ("CLOSED", Colours::ink, 14.5f);
            stateText->setJustificationType (juce::Justification::centredLeft);
            addAndMakeVisible (lamp);
            hint (lamp, "GATE");
            hint (*stateText, "GATE");
            stateText->setInterceptsMouseClicks (true, false);

            inLabel = &caption ("IN");
            addAndMakeVisible (meter);
            threshold = &bar (GateIDs::threshold, "THRESHOLD", Format::db, "dB");
            range = &bar (GateIDs::range, "RANGE", Format::db, "dB");
            attack = &ring (GateIDs::attack, "ATTACK", Format::ms, "ms");
            hold = &ring (GateIDs::hold, "HOLD", Format::ms, "ms");
            release = &ring (GateIDs::release, "RELEASE", Format::ms, "ms");
            below = &ring (GateIDs::lowCut, "IGNORE|BELOW", Format::hertz, "Hz", Ring::small, Colours::frequency);
            above = &ring (GateIDs::highCut, "IGNORE|ABOVE", Format::hertz, "Hz", Ring::small, Colours::frequency);
            openedByLabel = &caption ("OPENED BY");
            openedBy = &choice (GateIDs::openedBy, { "THIS TRACK", "SIDECHAIN" }, "OPENED BY");

            listen.setClickingTogglesState (true);
            listen.onClick = [this] { strip.getGate().setListening (listen.getToggleState()); };
            addAndMakeVisible (listen);
            hint (listen, "LISTEN");
        }

        ~GateSlab() override   { strip.getGate().setListening (false); }   // (never left on by accident)

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6));
            lamp.setBounds (12, 12, 20, 26);
            stateText->setBounds (33, 12, power.getX() - 35, 26);

            auto s1 = section (body.removeFromTop (146), 1, "HOW MUCH");
            body.removeFromTop (8);
            {
                const int w = 46 + 8 + 56 + 8 + 56;
                auto row = s1.withSizeKeepingCentre (w, s1.getHeight());
                auto col = row.removeFromLeft (46);
                inLabel->setBounds (col.removeFromTop (17));
                meter.setBounds (col.withTrimmedBottom (16));
                row.removeFromLeft (8);
                threshold->setBounds (row.removeFromLeft (56));
                row.removeFromLeft (8);
                range->setBounds (row.removeFromLeft (56));
            }

            auto s2 = section (body.removeFromTop (106), 2, "HOW FAST");
            body.removeFromTop (8);
            {
                const int w = s2.getWidth() / 3;
                for (auto* r : { attack, hold, release })
                    centreIn (*r, s2.removeFromLeft (w), s2.getY());
            }

            auto s3 = section (body, 3, "WHAT IT LISTENS TO");
            {
                const int w = s3.getWidth();
                centreIn (*below, s3.removeFromLeft (w * 29 / 100), s3.getY() + 8);
                centreIn (*above, s3.removeFromLeft (w * 29 / 100), s3.getY() + 8);
                auto c = s3.reduced (2, 0);
                openedByLabel->setBounds (c.removeFromTop (16));
                c.removeFromTop (2);
                openedBy->segmentHeight = 25;
                openedBy->setBounds (c.removeFromTop (53));
                c.removeFromTop (5);
                listen.setBounds (c.removeFromTop (26));
            }
        }

        void refresh() override
        {
            auto& gate = strip.getGate();
            EasyGateAudioProcessor::Moment moments[64];
            float loudest = -200.0f;
            for (int n; (n = gate.takeMoments (moments, 64)) > 0;)
                for (int i = 0; i < n; ++i)
                {
                    view.getScreen().addMoment (0, moments[i].keyDb, moments[i].outDb, moments[i].open ? 1.0f : 0.0f);
                    loudest = juce::jmax (loudest, moments[i].keyDb);
                }

            const auto settings = gate.getCurrentSettings();
            meter.setLevels (loudest, loudest);
            meter.setLine (settings.thresholdDb);

            const bool open = gate.takeOpenForLight();
            const float gainDb = gate.takeGainForLight();
            const bool on = strip.isModuleOn (0);
            const float rangeDb = juce::jmax (1.0f, settings.rangeDb);
            const float lit = ! on ? 0.0f : open ? 1.0f : juce::jlimit (0.0f, 1.0f, 1.0f + gainDb / rangeDb);
            lamp.setLit (lit);
            stateText->setText (! on ? "OFF" : open ? "OPEN" : lit > 0.05f ? "CLOSING" : "CLOSED", juce::dontSendNotification);
            listen.setToggleState (gate.isListening(), juce::dontSendNotification);
        }

    private:
        Lamp lamp { Colours::gate };
        juce::Label* stateText = nullptr;
        juce::Label* inLabel = nullptr;
        juce::Label* openedByLabel = nullptr;
        Meter meter;
        Bar* threshold = nullptr;
        Bar* range = nullptr;
        Ring *attack = nullptr, *hold = nullptr, *release = nullptr, *below = nullptr, *above = nullptr;
        Choice* openedBy = nullptr;
        Button listen { "LISTEN", Colours::gate };
    };

    //==========================================================================
    // AMP
    //==========================================================================
    class AmpSlab : public Slab
    {
    public:
        explicit AmpSlab (View& v) : Slab (v, 1, Colours::amp, "AMP")
        {
            drive = &ring (AmpIDs::drive, {}, Format::percent, "%", Ring::big, Colours::gold, "DRIVE");
            analog = &caption ("ANALOG");
            clean = &caption ("CLEAN");
            overdrive = &caption ("OVERDRIVE");
            tone = &ring (AmpIDs::tone, "TONE", Format::signedPercent, "%", Ring::tiny);
            tone->setFromCentre (true);
            tight = &ring (AmpIDs::tight, "TIGHT", Format::hertz, "Hz", Ring::tiny, Colours::frequency);
            mix = &ring (AmpIDs::mix, "MIX", Format::percent, "%", Ring::tiny);
            autoGain = &toggle (CommonIDs::autoGain, "AUTO|GAIN", "AUTO-GAIN", Button::Style::square, Colours::gate);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), true);

            auto s1 = section (body.removeFromTop (body.getHeight() - 114), 1, "HOW MUCH");
            body.removeFromTop (8);
            centreIn (*drive, s1, s1.getBottom() - drive->getHeight() + 2);
            analog->setBounds (s1.getCentreX() - 50, drive->getY() - 16, 100, 16);
            clean->setBounds (s1.getX(), s1.getBottom() - 16, 62, 16);
            overdrive->setBounds (s1.getRight() - 64, s1.getBottom() - 16, 64, 16);

            auto row = body;
            auto s2 = section (row.removeFromLeft (row.getWidth() / 2 - 2), 2, "TONE");
            row.removeFromLeft (8);
            auto s3 = section (row, 3, "OUTPUT");
            centreIn (*tone, s2.removeFromLeft (s2.getWidth() / 2), s2.getY());
            centreIn (*tight, s2, s2.getY());
            centreIn (*mix, s3.removeFromLeft (s3.getWidth() - 52), s3.getY());
            autoGain->setBounds (s3.getRight() - 50, s3.getY() + 10, 50, 50);
        }

    private:
        Ring *drive = nullptr, *tone = nullptr, *tight = nullptr, *mix = nullptr;
        juce::Label *analog = nullptr, *clean = nullptr, *overdrive = nullptr;
        Button* autoGain = nullptr;
    };

    //==========================================================================
    // EQ
    //==========================================================================
    class EQSlab : public Slab
    {
    public:
        explicit EQSlab (View& v) : Slab (v, 2, Colours::eq, "EQUALIZER")
        {
            static const char* bandNames[] { "LOW", "LOW MID", "HIGH MID", "HIGH" };

            hpf = &ring (EQIDs::hpf, "HPF", Format::hertz, "Hz", Ring::small, Colours::frequency);
            hpfSlope = &choice (EQIDs::hpfSlope, { "12", "24" }, "HPF - SLOPE", true);
            monoBass = &ring (EQIDs::monoBass, "MONO|BASS", Format::hertz, "Hz", Ring::small, Colours::frequency);
            lpf = &ring (EQIDs::lpf, "LPF", Format::hertz, "Hz", Ring::small, Colours::frequency);
            lpfSlope = &choice (EQIDs::lpfSlope, { "12", "24" }, "LPF - SLOPE", true);
            for (auto* c : { hpfSlope, lpfSlope }) { c->tint = Colours::frequency; c->segmentHeight = 24; c->fontSize = 13.0f; }

            for (int b = 0; b < 4; ++b)
            {
                const juce::String name (bandNames[b]);
                auto& band = bands[(size_t) b];
                band.name = &caption (name, tint, 15.0f);
                band.name->setFont (text (15.0f, 2));
                if (b == 0 || b == 3)
                {
                    band.shape = &choice (b == 0 ? EQIDs::lowShelf : EQIDs::highShelf, { "BELL", "SHELF" }, name + " - SHAPE", true);
                    band.shape->segmentHeight = 24;
                    band.shape->fontSize = 13.0f;
                }
                band.freq = &ring (EQIDs::freq[b], {}, Format::hertz, "Hz", Ring::small, Colours::frequency, name + " - FREQ");
                band.gain = &bar (EQIDs::gain[b], name, Format::signedDb, "dB", Colours::level, true, name + " - GAIN");
                band.gain->showLabels = false;
                band.unit = &caption ("dB", Colours::dim, 12.0f);
                band.target = &choice (EQIDs::target[b], { "ST", "MID", "SIDE" }, name + " - WORKS ON", true);
                band.target->segmentHeight = 24;
                band.target->fontSize = 13.0f;

                band.listen = std::make_unique<Button> ("LISTEN", tint);
                band.listen->fontSize = 13.0f;
                band.listen->onStateChange = [this, b]
                {
                    auto& eq = strip.getEQ();
                    const bool down = bands[(size_t) b].listen->isDown();
                    if (down) eq.setListenBand (b);
                    else if (eq.getListenBand() == b) eq.setListenBand (-1);
                };
                addAndMakeVisible (*band.listen);
                hint (*band.listen, name + " - LISTEN");
            }

            master = &toggle (EQIDs::master, "MASTER", "MASTER (LINEAR PHASE)", Button::Style::plain, Colours::delayMode);
            autoGain = &toggle (CommonIDs::autoGain, "AUTO GAIN", "AUTO-GAIN", Button::Style::plain, Colours::gate);
            autoGain->fontSize = 13.0f;

            for (int s = 0; s < 2; ++s)
            {
                slot[s].setButtonText (s == 0 ? "A" : "B");
                slot[s].onClick = [this, s] { strip.getEQ().switchToSlot (s); };
                addAndMakeVisible (slot[s]);
                hint (slot[s], "A / B");
            }
            copy.fontSize = 13.0f;
            copy.onClick = [this] { strip.getEQ().copyToOtherSlot(); };
            addAndMakeVisible (copy);
            hint (copy, "COPY");
        }

        ~EQSlab() override   { strip.getEQ().setListenBand (-1); }

        void resized() override
        {
            clearSections();
            auto area = getLocalBounds().reduced (9, 6);
            auto body = placeHeader (area, false, 150);

            // Header: A / B / COPY and AUTO GAIN on the left, MASTER right of centre
            auto row = area.removeFromTop (34);
            auto left = row.withRight (power.getX() - 6);
            for (auto* b : { &slot[0], &slot[1] })
                b->setBounds (left.removeFromLeft (28).withSizeKeepingCentre (28, 26)), left.removeFromLeft (3);
            copy.setBounds (left.removeFromLeft (48).withSizeKeepingCentre (48, 26));
            left.removeFromLeft (8);
            autoGain->setBounds (left.removeFromLeft (left.getWidth()).withSizeKeepingCentre (juce::jmin (84, left.getWidth()), 26));
            const int masterLeft = power.getRight() + 8;
            master->setBounds (juce::Rectangle<int> (masterLeft, row.getY(), init.getX() - 8 - masterLeft, row.getHeight()).withSizeKeepingCentre (76, 28));

            headings = body.removeFromTop (20);
            body.removeFromTop (4);

            refPlate = body.removeFromBottom (66);
            body.removeFromBottom (8);

            // 1 CLEAN UP
            cleanUp = body.removeFromLeft (70);
            body.removeFromLeft (5);
            {
                auto c = cleanUp.reduced (0, 6);
                centreIn (*hpf, c, c.getY()); c.removeFromTop (hpf->getHeight() + 3);
                hpfSlope->setBounds (c.removeFromTop (24).withSizeKeepingCentre (56, 24)); c.removeFromTop (12);
                centreIn (*monoBass, c, c.getY()); c.removeFromTop (monoBass->getHeight() + 12);
                centreIn (*lpf, c, c.getY()); c.removeFromTop (lpf->getHeight() + 3);
                lpfSlope->setBounds (c.removeFromTop (24).withSizeKeepingCentre (56, 24));
            }

            // 2 SHAPE: the four bands
            const int bandWidth = (body.getWidth() - 15) / 4;
            for (int b = 0; b < 4; ++b)
            {
                auto& band = bands[(size_t) b];
                band.area = body.removeFromLeft (bandWidth);
                body.removeFromLeft (5);
                auto c = band.area.reduced (3, 6);
                band.name->setBounds (c.removeFromTop (18));
                c.removeFromTop (4);
                auto shapeRow = c.removeFromTop (24);
                if (band.shape != nullptr)
                    band.shape->setBounds (shapeRow.withSizeKeepingCentre (juce::jmin (104, shapeRow.getWidth()), 24));
                c.removeFromTop (4);
                centreIn (*band.freq, c, c.getY());
                c.removeFromTop (band.freq->getHeight() + 4);

                auto bottom = c.removeFromBottom (24);
                band.listen->setBounds (bottom.withSizeKeepingCentre (juce::jmin (96, bottom.getWidth()), 24));
                c.removeFromBottom (4);
                band.target->setBounds (c.removeFromBottom (24).withSizeKeepingCentre (juce::jmin (104, c.getWidth()), 24));
                c.removeFromBottom (2);
                band.unit->setBounds (c.removeFromBottom (14));
                band.gain->setBounds (c.withSizeKeepingCentre (50, c.getHeight()));
            }
        }

        void paint (juce::Graphics& g) override
        {
            Slab::paint (g);

            const auto cleanFill = cleanUp.toFloat();
            g.setColour (tint.withAlpha (0.08f));
            g.fillRoundedRectangle (cleanFill, 6.0f);

            for (int b = 0; b < 4; ++b)
            {
                const auto r = bands[(size_t) b].area.toFloat();
                g.setColour (tint.withAlpha (b % 2 == 1 ? 0.14f : 0.07f));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (tint.withAlpha (0.22f));
                g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            }

            drawHeading (g, { (float) cleanUp.getX(), (float) headings.getY() }, tint, 1, "CLEAN UP");
            const float shapeCentre = (float) (bands[0].area.getX() + bands[3].area.getRight()) * 0.5f;
            drawHeading (g, { shapeCentre - 36.0f, (float) headings.getY() }, tint, 2, "SHAPE");

            // The frequency reference plate
            const auto plate = refPlate.toFloat();
            g.setColour (juce::Colour (0x73040c16));
            g.fillRoundedRectangle (plate, 6.0f);
            g.setColour (tint.withAlpha (0.28f));
            g.drawRoundedRectangle (plate.reduced (0.5f), 6.0f, 1.0f);

            static const char* cells[] { "WEIGHT 50-100", "BODY 150-250", "MUD 300-600", "CRUNCH 1-3K",
                                         "PRESENCE 2-5K", "ATTACK 2-5K", "SIBILANCE 5-9K", "AIR 10K +",
                                         "VOCALS:", "BOXY VOX 300-600", "NASAL VOX 800-1.5K", "SIBILANT VOX 5-8K" };
            const auto inside = plate.reduced (12.0f, 6.0f);
            const float cw = inside.getWidth() / 4.0f, ch = inside.getHeight() / 3.0f;
            for (int i = 0; i < 12; ++i)
            {
                const int rowIndex = i / 4, column = i % 4;
                const juce::Rectangle<float> cell (inside.getX() + cw * (float) column, inside.getY() + ch * (float) rowIndex, cw, ch);
                g.setFont (text (14.0f, i == 8 ? 2 : 0));
                g.setColour (i == 8 ? tint : juce::Colour (0xff8fc8da));
                g.drawText (cells[i], cell, juce::Justification::centredLeft, false);
                if (rowIndex == 2 && column == 0)
                {
                    g.setColour (tint.withAlpha (0.22f));
                    g.fillRect (inside.getX(), cell.getY() - 1.0f, inside.getWidth(), 1.0f);
                }
            }
        }

        void refresh() override
        {
            // On a mono track, mid-side does nothing: ST / MID / SIDE go dim
            const bool mono = strip.getMainBusNumInputChannels() == 1;
            for (auto& band : bands)
                if (band.target != nullptr && std::abs (band.target->getAlpha() - (mono ? 0.35f : 1.0f)) > 0.01f)
                    band.target->setAlpha (mono ? 0.35f : 1.0f);

            const int active = strip.getEQ().getActiveSlot();
            for (int s = 0; s < 2; ++s)
                slot[s].setToggleState (s == active, juce::dontSendNotification);
            copy.setButtonText (active == 0 ? "A > B" : "B > A");
        }

    private:
        struct Band
        {
            juce::Label* name = nullptr;
            Choice* shape = nullptr;
            Ring* freq = nullptr;
            Bar* gain = nullptr;
            juce::Label* unit = nullptr;
            Choice* target = nullptr;
            std::unique_ptr<Button> listen;
            juce::Rectangle<int> area;
        };

        std::array<Band, 4> bands;
        Ring *hpf = nullptr, *monoBass = nullptr, *lpf = nullptr;
        Choice *hpfSlope = nullptr, *lpfSlope = nullptr;
        Button *master = nullptr, *autoGain = nullptr;
        Button slot[2] { { "A", Colours::eq }, { "B", Colours::eq } };
        Button copy { "A > B", Colours::eq };
        juce::Rectangle<int> headings, refPlate, cleanUp;
    };

    //==========================================================================
    // DS
    //==========================================================================
    class DSSlab : public Slab
    {
    public:
        explicit DSSlab (View& v) : Slab (v, 3, Colours::ds, "DS"),
            meter (-30.0f, 20.0f, { 20.0f, 10.0f, 0.0f, -10.0f, -20.0f, -30.0f })
        {
            addAndMakeVisible (lamp);
            hint (lamp, "TURNED DOWN");
            from = &ring (DSIDs::from, "FROM", Format::hertz, "Hz", Ring::small, Colours::frequency);
            to = &ring (DSIDs::to, "TO", Format::hertz, "Hz", Ring::small, Colours::frequency);
            notSure = &caption ("NOT SURE?", Colours::dim);
            find.onClick = [this] { if (finding) finish(); else start(); };
            addAndMakeVisible (find);
            hint (find, "FIND");
            listen.setClickingTogglesState (true);
            listen.onClick = [this] { strip.getDS().setListening (listen.getToggleState()); };
            addAndMakeVisible (listen);
            hint (listen, "LISTEN");

            bandLabel = &caption ("BAND");
            addAndMakeVisible (meter);
            hint (meter, "TURNED DOWN");
            threshold = &bar (DSIDs::threshold, "THRESHOLD", Format::signedDb, "dB");
            range = &bar (DSIDs::range, "RANGE", Format::db, "dB");
            turnsDown = &choice (DSIDs::turnsDown, { "HARSH BAND", "WHOLE SOUND" }, "TURNS DOWN", true);
        }

        ~DSSlab() override
        {
            strip.getDS().setListening (false);
            strip.getDS().stopFinding();
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6));
            lamp.setBounds (12, 10, 20, 30);

            auto top = body.removeFromTop (164);
            body.removeFromTop (8);
            auto s1 = section (top.removeFromLeft (top.getWidth() / 2 - 4), 1, "WHERE");
            top.removeFromLeft (8);
            auto s2 = section (top, 2, "HOW MUCH");

            {
                const int w = s1.getWidth() / 3;
                centreIn (*from, s1.removeFromLeft (w), s1.getY() + 22);
                centreIn (*to, s1.removeFromLeft (w), s1.getY() + 22);
                auto c = s1.withSizeKeepingCentre (juce::jmin (80, s1.getWidth()), 102);
                notSure->setBounds (c.removeFromTop (18));
                c.removeFromTop (6);
                find.setBounds (c.removeFromTop (30));
                c.removeFromTop (8);
                listen.setBounds (c.removeFromTop (30));
            }
            {
                const int w = 46 + 8 + 56 + 8 + 44;
                auto row = s2.withSizeKeepingCentre (w, s2.getHeight());
                auto col = row.removeFromLeft (46);
                bandLabel->setBounds (col.removeFromTop (17));
                meter.setBounds (col.withTrimmedBottom (16));
                row.removeFromLeft (8);
                threshold->setBounds (row.removeFromLeft (56));
                row.removeFromLeft (8);
                range->setBounds (row.removeFromLeft (44));
            }

            auto s3 = section (body, 3, "TURNS DOWN");
            turnsDown->setBounds (s3.withSizeKeepingCentre (264, 28));
        }

        void refresh() override
        {
            auto& ds = strip.getDS();
            EasyDSAudioProcessor::Moment moments[64];
            const auto settings = ds.getCurrentSettings();
            float loudest = -200.0f;
            for (int n; (n = ds.takeMoments (moments, 64)) > 0;)
                for (int i = 0; i < n; ++i)
                {
                    const auto& m = moments[i];
                    view.getScreen().addMoment (3, m.soundDb, m.bandDb, -m.gainDb);
                    if (m.bandDb > -100.0f && m.lineDb > -100.0f)
                        loudest = juce::jmax (loudest, m.bandDb - m.lineDb + settings.thresholdDb);   // (relative to the line)
                }
            meter.setLevels (loudest, loudest);
            meter.setLine (settings.thresholdDb);

            const float reduction = ds.takeReductionForMeter();
            lamp.setLit (strip.isModuleOn (3) ? juce::jlimit (0.0f, 1.0f, reduction / 3.0f) : 0.0f);
            listen.setToggleState (ds.isListening(), juce::dontSendNotification);

            if (finding)
                step();
        }

    private:
        //==== FIND (as in Easy DS) ===============================================
        void start()
        {
            collected.clear();
            soundFrames = framesSinceSound = 0;
            finding = true;
            find.setToggleState (true, juce::dontSendNotification);
            strip.getDS().startFinding();
            view.announce ("FINDING THE HARSHNESS", "Keep your track playing...", 60);
        }

        void step()
        {
            auto& ds = strip.getDS();
            HarshnessFinder::Frame arriving[256];
            for (int n; (n = ds.takeFrames (arriving, 256)) > 0;)
                for (int i = 0; i < n; ++i)
                {
                    collected.push_back (arriving[i]);
                    const bool sound = arriving[i].total > 1.0e-6f;
                    soundFrames += sound ? 1 : 0;
                    framesSinceSound = sound ? 0 : framesSinceSound + 1;
                }

            const int needed = (int) (8.0 / HarshnessFinder::frameSeconds);
            if (soundFrames >= needed)
            {
                finish();
                return;
            }

            const float progress = (float) soundFrames / (float) needed;
            const bool waiting = collected.empty() || framesSinceSound > 50;
            view.announce ("FINDING THE HARSHNESS",
                           waiting ? juce::String ("Waiting for sound: press play in your DAW")
                                   : "Keep your track playing... " + juce::String ((int) std::ceil ((1.0f - progress) * 8.0f)) + " s", 60);
        }

        void finish()
        {
            if (! finding)
                return;
            finding = false;
            auto& ds = strip.getDS();
            ds.stopFinding();
            find.setToggleState (false, juce::dontSendNotification);

            HarshnessFinder::Frame arriving[256];
            for (int n; (n = ds.takeFrames (arriving, 256)) > 0;)
                collected.insert (collected.end(), arriving, arriving + n);

            const auto result = HarshnessFinder::analyse (collected, [&ds] (int band) { return ds.canFindBand (band); });
            collected.clear();

            using Outcome = HarshnessFinder::Result::Outcome;
            if (result.outcome == Outcome::found)
            {
                from->setFromUser (from->getParameter().convertTo0to1 (result.fromHz));
                to->setFromUser (to->getParameter().convertTo0to1 (result.toHz));
                view.announce ("FOUND IT: " + from->getParameter().getCurrentValueAsText() + " TO " + to->getParameter().getCurrentValueAsText(),
                               "FROM and TO are set. Now set THRESHOLD so only the S sounds go over the line.", 7);
            }
            else if (result.outcome == Outcome::nothingStoodOut)
                view.announce ("NOTHING HARSH STOOD OUT", "FROM and TO weren't changed. Try a part with more S sounds (or harsher cymbals).", 7);
            else
                view.announce ("NOT ENOUGH SOUND", "Click FIND while your track is playing: it listens for 8 seconds.", 7);
        }

        Lamp lamp { Colours::ds };
        Ring *from = nullptr, *to = nullptr;
        juce::Label *notSure = nullptr, *bandLabel = nullptr;
        Button find { "FIND", Colours::ds }, listen { "LISTEN", Colours::ds };
        Meter meter;
        Bar *threshold = nullptr, *range = nullptr;
        Choice* turnsDown = nullptr;

        bool finding = false;
        int soundFrames = 0, framesSinceSound = 0;
        std::vector<HarshnessFinder::Frame> collected;
    };

    //==========================================================================
    // COMP
    //==========================================================================
    class CompSlab : public Slab
    {
    public:
        explicit CompSlab (View& v) : Slab (v, 4, Colours::comp, "COMP"),
            meter (-60.0f, 0.0f, { 0.0f, -15.0f, -30.0f, -45.0f, -60.0f }),
            reduction (24.0f, false, Colours::reduction)
        {
            inLabel = &caption ("IN");
            addAndMakeVisible (meter);
            threshold = &bar (CompIDs::threshold, "THRESHOLD", Format::db, "dB");
            ratio = &bar (CompIDs::ratio, "RATIO", Format::ratio, ": 1");
            grLabel = &caption ("GR", Colours::reduction);
            addAndMakeVisible (reduction);
            hint (reduction, "GAIN REDUCTION");
            hint (*grLabel, "GAIN REDUCTION");
            grLabel->setInterceptsMouseClicks (true, false);
            grValue = &caption ("0.0", Colours::dim, 12.0f);

            attack = &ring (CompIDs::attack, "ATTACK", Format::msPrecise, "ms", Ring::medium);
            release = &ring (CompIDs::release, "RELEASE", Format::msPrecise, "ms", Ring::medium);
            makeup = &ring (CompIDs::makeup, "MAKE-UP", Format::signedDb, "dB", Ring::medium, Colours::level);
            mix = &ring (CompIDs::mix, "MIX", Format::percent, "%", Ring::medium);
            autoGain = &toggle (CommonIDs::autoGain, "AUTO|GAIN", "AUTO-GAIN", Button::Style::square, Colours::gate);

            below = &ring (CompIDs::lowCut, "IGNORE|BELOW", Format::hertz, "Hz", Ring::small, Colours::frequency);
            reactsLabel = &caption ("REACTS TO");
            reactsTo = &choice (CompIDs::reactsTo, { "PEAKS", "AVERAGE" }, "REACTS TO");
            listensLabel = &caption ("LISTENS TO");
            listensTo = &choice (CompIDs::listensTo, { "THIS TRACK", "SIDECHAIN" }, "LISTENS TO");
            for (int sl = 0; sl < 2; ++sl)
            {
                slot[sl].fontSize = 13.5f;
                slot[sl].onClick = [this, sl] { strip.getComp().switchToSlot (sl); };
                addAndMakeVisible (slot[sl]);
                view.addHint (slot[sl], "A / B", view.moduleHint (4, "A / B"));
            }
            copy.fontSize = 13.0f;
            copy.onClick = [this] { strip.getComp().copyToOtherSlot(); };
            addAndMakeVisible (copy);
            view.addHint (copy, "COPY", view.moduleHint (4, "COPY"));

            styleLabel = &caption ("STYLE");
            style = &choice (CompIDs::character, { "MODERN", "VINTAGE" }, "STYLE");
            lowEndLabel = &caption ("LOW END");
            lowEnd = &choice (CompIDs::lowEnd, { "NORMAL", "THRUST" }, "LOW END");
        }

        void resized() override
        {
            clearSections();
            auto area = getLocalBounds().reduced (9, 6);
            auto body = placeHeader (area);
            {
                auto row = area.removeFromTop (34);
                slot[0].setBounds (row.removeFromLeft (28).withSizeKeepingCentre (28, 26));
                row.removeFromLeft (3);
                slot[1].setBounds (row.removeFromLeft (28).withSizeKeepingCentre (28, 26));
                row.removeFromLeft (6);
                copy.setBounds (row.removeFromLeft (48).withSizeKeepingCentre (48, 26));
            }

            auto upper = body.removeFromTop (238);
            body.removeFromTop (8);
            auto s1 = section (upper.removeFromLeft (232), 1, "HOW MUCH");
            upper.removeFromLeft (8);
            {
                auto row = s1;
                auto col = row.removeFromLeft (46);
                inLabel->setBounds (col.removeFromTop (17));
                meter.setBounds (col.withTrimmedBottom (16));
                row.removeFromLeft (6);
                threshold->setBounds (row.removeFromLeft (56));
                row.removeFromLeft (6);
                ratio->setBounds (row.removeFromLeft (56));
                row.removeFromLeft (8);
                auto gr = row.removeFromLeft (24);
                grLabel->setBounds (gr.removeFromTop (17));
                grValue->setBounds (gr.removeFromBottom (14).expanded (8, 0));
                reduction.setBounds (gr.withSizeKeepingCentre (16, gr.getHeight()));
            }

            auto s2 = section (upper.removeFromTop (115), 2, "HOW FAST");
            upper.removeFromTop (8);
            auto s3 = section (upper, 3, "OUTPUT");
            centreIn (*attack, s2.removeFromLeft (s2.getWidth() / 2), s2.getY());
            centreIn (*release, s2, s2.getY());
            {
                const int w = s3.getWidth() / 3;
                centreIn (*makeup, s3.removeFromLeft (w), s3.getY());
                centreIn (*mix, s3.removeFromLeft (w), s3.getY());
                autoGain->setBounds (s3.withSizeKeepingCentre (54, 54));
            }

            auto s4 = section (body.removeFromLeft (body.getWidth() * 60 / 100), 4, "WHAT IT REACTS TO");
            body.removeFromLeft (8);
            auto s5 = section (body, 5, "CHARACTER");
            {
                const int w = s4.getWidth() / 3;
                centreIn (*below, s4.removeFromLeft (w), s4.getY() + 4);
                placeStack (*reactsLabel, *reactsTo, s4.removeFromLeft (w));
                placeStack (*listensLabel, *listensTo, s4);
                placeStack (*styleLabel, *style, s5.removeFromLeft (s5.getWidth() / 2));
                placeStack (*lowEndLabel, *lowEnd, s5);
            }
        }

        void refresh() override
        {
            auto& comp = strip.getComp();
            const int active = comp.getActiveSlot();
            for (int sl = 0; sl < 2; ++sl)
                slot[sl].setToggleState (sl == active, juce::dontSendNotification);
            copy.setButtonText (active == 0 ? "A > B" : "B > A");

            const float level = comp.takeInputLevelForGraph();
            const float gr = comp.takeGainReductionForMeter();
            meter.setLevels (level, level);
            meter.setLine (comp.getCurrentSettings().thresholdDb);
            reduction.setGainDb (gr);
            grValue->setText (juce::String (reduction.getShownDb(), 1), juce::dontSendNotification);
            view.getScreen().addCompFrame (level, gr);
        }

    private:
        static void placeStack (juce::Label& label, Choice& c, juce::Rectangle<int> column)
        {
            auto r = column.withSizeKeepingCentre (juce::jmin (column.getWidth() - 4, 104), 17 + 6 + c.getIdealHeight());
            label.setBounds (r.removeFromTop (17));
            r.removeFromTop (6);
            c.setBounds (r);
        }

        juce::Label *inLabel = nullptr, *grLabel = nullptr, *grValue = nullptr;
        Button slot[2] { { "A", Colours::comp }, { "B", Colours::comp } };
        Button copy { "A > B", Colours::comp };
        Meter meter;
        GainBar reduction;
        Bar *threshold = nullptr, *ratio = nullptr;
        Ring *attack = nullptr, *release = nullptr, *makeup = nullptr, *mix = nullptr, *below = nullptr;
        Button* autoGain = nullptr;
        juce::Label *reactsLabel = nullptr, *listensLabel = nullptr, *styleLabel = nullptr, *lowEndLabel = nullptr;
        Choice *reactsTo = nullptr, *listensTo = nullptr, *style = nullptr, *lowEnd = nullptr;
    };

    //==========================================================================
    // TRANS
    //==========================================================================
    class TransSlab : public Slab
    {
    public:
        explicit TransSlab (View& v) : Slab (v, 5, Colours::trans, "TRANS"), change (12.0f, true, Colours::gate)
        {
            powerWidth = 76;
            punch = &bar (TransientIDs::punch, "PUNCH", Format::signedPercent, "%", {}, true);
            sustain = &bar (TransientIDs::sustain, "SUSTAIN", Format::signedPercent, "%", {}, true);
            changeLabel = &caption ("GAIN", Colours::ink, 12.5f);
            changeUnit = &caption (juce::String::fromUTF8 ("\xc2\xb1") + "12", Colours::dim, 12.0f);
            addAndMakeVisible (change);
            hint (change, "GAIN CHANGE");
            output = &ring (TransientIDs::output, "OUTPUT", Format::signedDb, "dB", Ring::small, Colours::level);
            output->setFromCentre (true);
            mix = &ring (TransientIDs::mix, "MIX", Format::percent, "%");
            autoGain = &toggle (CommonIDs::autoGain, "AUTO|GAIN", "AUTO-GAIN", Button::Style::square, Colours::gate);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), false, powerWidth);
            auto s1 = section (body.removeFromTop (body.getHeight() - 114), 1, "SHAPE");
            body.removeFromTop (8);
            {
                auto row = s1.withSizeKeepingCentre (44 + 14 + 24 + 14 + 44, s1.getHeight());
                punch->setBounds (row.removeFromLeft (44));
                row.removeFromLeft (14);
                auto c = row.removeFromLeft (24);
                changeLabel->setBounds (c.removeFromTop (17));
                changeUnit->setBounds (c.removeFromBottom (14).expanded (8, 0));
                change.setBounds (c.withSizeKeepingCentre (16, c.getHeight()));
                row.removeFromLeft (14);
                sustain->setBounds (row.removeFromLeft (44));
            }
            auto s2 = section (body, 2, "OUTPUT");
            const int w = s2.getWidth() / 3;
            centreIn (*output, s2.removeFromLeft (w), s2.getY());
            centreIn (*mix, s2.removeFromLeft (w), s2.getY());
            autoGain->setBounds (s2.withSizeKeepingCentre (54, 54));
        }

        void refresh() override
        {
            auto& t = strip.getTrans();
            const float boost = t.takeBoostForMeter(), cut = t.takeCutForMeter();
            change.setGainDb (boost >= cut ? boost : -cut);

            EasyTransientAudioProcessor::Moment moments[64];
            for (int n; (n = t.takeMoments (moments, 64)) > 0;)
                for (int i = 0; i < n; ++i)
                    view.getScreen().addMoment (5, moments[i].inDb, moments[i].outDb, 0.0f);
        }

    private:
        int powerWidth = 76;
        Bar *punch = nullptr, *sustain = nullptr;
        juce::Label *changeLabel = nullptr, *changeUnit = nullptr;
        GainBar change;
        Ring *output = nullptr, *mix = nullptr;
        Button* autoGain = nullptr;
    };

    //==========================================================================
    // BITCH-SLAP
    //==========================================================================
    class SlapSlab : public Slab
    {
    public:
        explicit SlapSlab (View& v) : Slab (v, 6, Colours::slap, "BITCH|SLAP")
        {
            deepRed = true;
            power.fontSize = 13.5f;
            mix = &ring (SlapIDs::mix, "MIX", Format::percent, "%");
            guide = &caption ("0%  DRY\n50%  BOTH\n100%  REVERB", juce::Colour (0xfff3c9d2), 13.5f);
            guide->setJustificationType (juce::Justification::centredLeft);
            length = &ring (SlapIDs::length, "LENGTH", Format::whole, "ms");
            note = &ring (SlapIDs::note, "LENGTH", Format::asText, {}, Ring::small, {}, "LENGTH");
            syncLabel = &caption ("SYNC");
            sync = &choice (SlapIDs::sync, { "OFF", "TEMPO" }, "SYNC");
        }

        void resized() override
        {
            clearSections();
            const bool narrow = getWidth() < 220;   // (its bay in the WEAPON SELECT rack)
            auto body = placeHeader (getLocalBounds().reduced (9, 6), narrow, 76);

            if (narrow)
            {
                // Tall and narrow: its bay in the WEAPON SELECT rack
                auto s1 = section (body.removeFromTop (juce::jmin (230, body.getHeight() / 2 - 4)), 1, "HOW MUCH");
                body.removeFromTop (8);
                auto s2 = section (body.removeFromTop (juce::jmin (body.getHeight(), 250)), 2, "HOW LONG");
                centreIn (*mix, s1, s1.getY() + 4);
                guide->setJustificationType (juce::Justification::centred);
                guide->setBounds (s1.withTop (mix->getBottom() + 8).withHeight (60));
                centreIn (*length, s2, s2.getY() + 4);
                centreIn (*note, s2, s2.getY() + 4);
                auto c = s2.withTop (length->getBottom() + 12).withSizeKeepingCentre (juce::jmin (110, s2.getWidth()), 17 + 6 + sync->getIdealHeight());
                syncLabel->setBounds (c.removeFromTop (17));
                c.removeFromTop (6);
                sync->setBounds (c);
                return;
            }

            guide->setJustificationType (juce::Justification::centredLeft);
            auto s1 = section (body.removeFromTop (body.getHeight() / 2 - 4), 1, "HOW MUCH");
            body.removeFromTop (8);
            auto s2 = section (body, 2, "HOW LONG");
            centreIn (*mix, s1.removeFromLeft (s1.getWidth() / 2), s1.getY());
            guide->setBounds (s1.withSizeKeepingCentre (s1.getWidth(), 60));
            guide->setBounds (guide->getBounds().withX (juce::jmax (guide->getX(), mix->getRight() + 4)).withRight (s1.getRight()));
            auto left = s2.removeFromLeft (s2.getWidth() / 2);
            centreIn (*length, left, s2.getY());
            centreIn (*note, left, s2.getY());
            auto c = s2.withSizeKeepingCentre (juce::jmin (90, s2.getWidth()), 17 + 6 + sync->getIdealHeight());
            syncLabel->setBounds (c.removeFromTop (17));
            c.removeFromTop (6);
            sync->setBounds (c);
        }

        void refresh() override
        {
            const bool tempo = sync->getChosen() == 1;
            length->setVisible (! tempo);
            note->setVisible (tempo);

            BitchSlapAudioProcessor::Moment moments[64];
            for (int n; (n = strip.getSlap().takeMoments (moments, 64)) > 0;)
                for (int i = 0; i < n; ++i)
                    view.getScreen().addMoment (6, moments[i].inDb, moments[i].wetDb, 0.0f);
        }

    private:
        Ring *mix = nullptr, *length = nullptr, *note = nullptr;
        juce::Label *guide = nullptr, *syncLabel = nullptr;
        Choice* sync = nullptr;
    };

    //==========================================================================
    // TAPE
    //==========================================================================
    class TapeSlab : public Slab
    {
    public:
        explicit TapeSlab (View& v) : Slab (v, StripModules::tapeIndex, Colours::tape, "TAPE")
        {
            drive = &ring (TapeIDs::drive, "DRIVE", Format::signedDb, "dB", Ring::small, Colours::gold);
            drive->setFromCentre (true);
            speed = &choice (TapeIDs::speed, { "15 IPS", "30 IPS", "CASSETTE" }, "SPEED");
            wow = &ring (TapeIDs::wow, "WOW", Format::percent, "%", Ring::small, {}, "WOW & FLUTTER");
            hiss = &ring (TapeIDs::hiss, "HISS", Format::percent, "%");
            autoGain = &toggle (CommonIDs::autoGain, "AUTO|GAIN", "AUTO-GAIN", Button::Style::square, Colours::gate);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), false, 76);
            auto top = body.removeFromTop (body.getHeight() - 114);
            body.removeFromTop (8);

            auto s1 = section (top.removeFromLeft (top.getWidth() / 2 - 4), 1, "RECORD LEVEL");
            top.removeFromLeft (8);
            auto s2 = section (top, 2, "MACHINE");
            centreIn (*drive, s1, s1.getY());
            {
                auto c = s2.withSizeKeepingCentre (s2.getWidth(), juce::jmin (s2.getHeight(), speed->getIdealHeight()));
                speed->setBounds (c);
            }

            auto s3 = section (body, 3, "WEAR & TEAR");
            const int w = s3.getWidth() / 3;
            centreIn (*wow, s3.removeFromLeft (w), s3.getY());
            centreIn (*hiss, s3.removeFromLeft (w), s3.getY());
            autoGain->setBounds (s3.withSizeKeepingCentre (54, 54));
        }

    private:
        Ring *drive = nullptr, *wow = nullptr, *hiss = nullptr;
        Choice* speed = nullptr;
        Button* autoGain = nullptr;
    };

    //==========================================================================
    // CLIP
    //==========================================================================
    class ClipSlab : public Slab
    {
    public:
        explicit ClipSlab (View& v) : Slab (v, 7, Colours::clip, "CLIP")
        {
            addAndMakeVisible (lamp);
            hint (lamp, "SHAVED OFF");
            view_ = &choice (ClipIDs::view, { "SIMPLE", "ADVANCED" }, "SIMPLE / ADVANCED");
            view_->tint = Colours::selected;
            addAndMakeVisible (advancedLamp);
            hint (advancedLamp, "SIMPLE / ADVANCED");
            push = &ring (ClipIDs::push, "PUSH", Format::signedDb, "dB");
            ceiling = &ring (ClipIDs::ceiling, "CEILING", Format::db, "dB", Ring::small, Colours::gold);
            softness = &ring (ClipIDs::softness, "SOFTNESS", Format::percent, "%");
            protect = &ring (ClipIDs::protectLows, "PROTECT LOWS", Format::hertz, "Hz", Ring::small, Colours::frequency);
        }

        void resized() override
        {
            clearSections();
            auto r = getLocalBounds().reduced (12, 8);

            auto tools = r.removeFromRight (74).removeFromTop (30);
            pr.setBounds (tools.removeFromRight (28).withSizeKeepingCentre (28, 28));
            tools.removeFromRight (6);
            init.setBounds (tools.removeFromRight (40).withSizeKeepingCentre (40, 28));
            r.removeFromRight (8);

            auto side = r.removeFromLeft (90);
            power.setBounds (side.withSizeKeepingCentre (84, 34).withY (side.getCentreY() - 28));
            lamp.setBounds (side.withSizeKeepingCentre (20, 20).withY (side.getCentreY() + 14));
            r.removeFromLeft (12);

            auto mode = r.removeFromLeft (104);
            view_->setBounds (mode.withSizeKeepingCentre (100, 59).withY (mode.getCentreY() - 40));
            advancedLamp.setBounds (mode.withSizeKeepingCentre (20, 20).withY (mode.getCentreY() + 24));
            r.removeFromLeft (12);

            auto s1 = section (r.removeFromLeft (r.getWidth() / 2 - 4), 1, "HOW HARD");
            r.removeFromLeft (8);
            auto s2 = section (r, 2, "HOW IT SOUNDS");
            centreIn (*push, s1.removeFromLeft (s1.getWidth() / 2), s1.getY() - 4);
            centreIn (*ceiling, s1, s1.getY() - 4);
            centreIn (*softness, s2.removeFromLeft (s2.getWidth() / 2), s2.getY() - 4);
            centreIn (*protect, s2, s2.getY() - 4);
        }

        void refresh() override
        {
            auto& clip = strip.getClip();
            const float shaved = clip.takeShavedForMeter();
            lamp.setLit (strip.isModuleOn (7) ? juce::jlimit (0.0f, 1.0f, shaved / 1.0f) : 0.0f);
            advancedLamp.setLit (clip.isAdvancedInUse() ? 1.0f : 0.0f);
            view.setClipAdvancedShown (view_->getChosen() == 1);

            EasyClipAudioProcessor::Moment moments[64];
            for (int n; (n = clip.takeMoments (moments, 64)) > 0;)
                for (int i = 0; i < n; ++i)
                    view.getScreen().addMoment (7, moments[i].pushedDb, moments[i].outDb, 0.0f);
        }

    private:
        Lamp lamp { Colours::warn }, advancedLamp { Colours::selected };
        Choice* view_ = nullptr;
        Ring *push = nullptr, *ceiling = nullptr, *softness = nullptr, *protect = nullptr;
    };

    //==========================================================================
    // CLIP - ADVANCED (3 SOUND DESIGN)
    //==========================================================================
    ClipAdvanced::ClipAdvanced (View& v) : Slab (v, 7, Colours::clip, "CLIP")
    {
        power.setVisible (false);
        pr.setVisible (false);
        init.onClick = [this] { strip.getClip().resetAdvanced(); };
        view.addHint (init, "INIT (ADVANCED)", view.moduleHint (7, "INIT (ADVANCED)"));

        auto& top = ring (ClipIDs::top, "TOP", Format::db, "dB");
        linkButton = &toggle (ClipIDs::link, "LINK", "LINK");
        auto& bottom = ring (ClipIDs::bottom, "BOTTOM", Format::db, "dB");
        top.linkWith (bottom, [this] { return linkButton->getToggleState(); });
        auto& bias = ring (ClipIDs::bias, "BIAS", Format::signedPercent, "%", Ring::small, Colours::gold);
        bias.setFromCentre (true);
        ring (ClipIDs::blend, "BLEND", Format::percent, "%");
        caption ("CLIP  -  ADVANCED", Colours::dim, 13.5f);
    }

    void ClipAdvanced::resized()
    {
        clearSections();
        auto r = getLocalBounds().reduced (14, 6);

        auto head = r.removeFromLeft (150);
        if (auto* tag = dynamic_cast<juce::Label*> (parts.back().get()))
            tag->setBounds (head.getX(), head.getY() + 4, 150, 16);
        headingAt = { head.getX(), head.getY() + 26 };
        init.setBounds (head.getX(), head.getY() + 52, 40, 28);
        r.removeFromLeft (8);

        // TOP  LINK  BOTTOM grouped in a box, then BIAS, BLEND
        auto* top = dynamic_cast<Ring*> (parts[0].get());
        auto* bottom = dynamic_cast<Ring*> (parts[2].get());
        auto* bias = dynamic_cast<Ring*> (parts[3].get());
        auto* blend = dynamic_cast<Ring*> (parts[4].get());

        linkBox = r.removeFromLeft (220).withSizeKeepingCentre (220, 84);
        auto box = linkBox.reduced (8, 4);
        centreIn (*top, box.removeFromLeft (64));
        centreIn (*bottom, box.removeFromRight (64));
        linkButton->setBounds (box.withSizeKeepingCentre (64, 30));
        r.removeFromLeft (10);
        centreIn (*bias, r.removeFromLeft (r.getWidth() / 2));
        centreIn (*blend, r);
    }

    void ClipAdvanced::paint (juce::Graphics& g)
    {
        Slab::paint (g);
        drawHeading (g, headingAt.toFloat(), tint, 3, "SOUND DESIGN");
        const auto box = linkBox.toFloat();
        g.setColour (juce::Colour (0x5a020810));
        g.fillRoundedRectangle (box, 7.0f);
        g.setColour (tint.withAlpha (0.30f));
        g.drawRoundedRectangle (box.reduced (0.5f), 7.0f, 1.0f);
    }

    //==========================================================================
    std::unique_ptr<Slab> makeSlab (View& v, int module)
    {
        switch (module)
        {
            case 0:  return std::make_unique<GateSlab> (v);
            case 1:  return std::make_unique<AmpSlab> (v);
            case 2:  return std::make_unique<EQSlab> (v);
            case 3:  return std::make_unique<DSSlab> (v);
            case 4:  return std::make_unique<CompSlab> (v);
            case 5:  return std::make_unique<TransSlab> (v);
            case 6:  return std::make_unique<SlapSlab> (v);
            case 8:  return std::make_unique<TapeSlab> (v);
            default: return std::make_unique<ClipSlab> (v);
        }
    }
}

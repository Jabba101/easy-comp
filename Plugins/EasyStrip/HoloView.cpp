#include "HoloSlabs.h"
#include "EasyWindow.h"

#ifndef JucePlugin_VersionString
 #define JucePlugin_VersionString "test"
#endif

namespace Holo
{
    namespace
    {
        // The layout at 100% (see strip_wireframe_hologram_digital.html)
        constexpr int left = 15, top = 12;
        constexpr int headerHeight = 44, viewRowY = 66, viewRowHeight = 36, mainY = 112;
        constexpr int colIn = 15, colGate = 95, colScreen = 365, colRight = 935, colRight2 = 1445, colOut = 1705;
        constexpr int wIn = 70, wGate = 260, wScreen = 560, wRight = 500, wRight2 = 250, wOut = 80;
        constexpr int mainBottom = 962;

        const juce::String trimHint =
            "Trim: sets how loud the track comes into the strip. Aim for the INPUT meter peaking around 0 "
            "on the loudest hits (with the meter's 0 set to -18 dBFS in SETUP), or around -6 dBFS.";
        const juce::String polarityHint =
            "Polarity: flips the waveform upside down. Try it on a second mic of the same drum (snare top and bottom) "
            "if they sound thin together - whichever sounds fuller is right.";
        const juce::String levelHint = "Level: the final output of the strip, after everything else.";
        const juce::String stripAutoGainHint =
            "Auto gain (the whole strip): keeps the strip's output as loud as what comes in, so BYPASS compares the sound, "
            "not the loudness. It listens over a couple of seconds, up to 12 dB either way; with CLIP on it only turns "
            "down, so CLIP's CEILING still holds. The number shows what it's doing. Switch it off before setting your final level.";
        const juce::String meterModeHint =
            "PEAK: the output meter shows peak level. LUFS: it shows loudness the way streaming services measure it "
            "(the last 3 seconds).";
        const juce::String lufsHint =
            "Loudness the way streaming services measure it. S: the last 3 seconds. I: the whole song since you last "
            "reset it - click it to start again. (Spotify and YouTube aim for about -14 LUFS.)";
        const juce::String oversamplingHint =
            "How carefully AMP, TAPE and CLIP work (one setting for all three). 4x / 8x: 4x while you mix, 8x when you export - "
            "the best of both. OFF: lightest on your computer, a little harsher. 4x and 8x add a little delay.";
        const juce::String initAllHint =
            "Init: puts every module on the strip back to its default settings and switches them all off. "
            "(Each module's own INIT resets just that module.)";
        const juce::String bypassHint =
            "Bypass: switches the whole strip off so you can compare with the untouched sound (on time).";
        const juce::String setupHint = "Setup: window size, skin, the ticker, and what 0 means on the meters.";
        const juce::String presetHint =
            "The strip's presets: every module's settings at once. Click the name for the list (and to save your own); "
            "the arrows step through them. A * means something's been changed since.";
        const juce::String pageHint = "Shows this module on the big screen. (The screen only changes when you press one of these.)";
        const juce::String stripSlotHint =
            "A and B: two versions of the WHOLE strip (every module, on or off, and its settings) to compare. Switch while "
            "the track plays to hear which you prefer. B starts as a copy of A; the one you're not on is kept with your project.";
        const juce::String stripCopyHint =
            "Copies the strip you're on to the other one (A to B, or B to A) - then change one of them and compare.";
        const juce::String weaponHint =
            "Weapon select: opens the effects rack in place of GATE, AMP, the screen, EQ, DS and COMP (they keep working). "
            "WAH, FLANGE, MOD FILTER, TREMOLO, DELAY and VERB are in there; the limiter is coming. Press it again to go back. "
            "It lights up while any of them is switched on.";
        const juce::String trackNameHint =
            "The name of the track this strip is on, as your DAW calls it. Rename the track and it follows.";
        const juce::String rackHint =
            "The effects rack (Easy FX). WAH, FLANGE, MOD FILTER, TREMOLO, DELAY and VERB work now; the empty bay is where the limiter "
            "will go. WEAPON SELECT again takes you back to the strip.";

        // The page buttons, left to right (the rack effects' panels are in the rack: no page for them)
        constexpr int pageOrder[] { 0, 1, 2, 3, 4, 5, 8, 7 };

        constexpr const char* bayNotes[] {
            "",
            "",
            "Filter pulses locked to the tempo; wide stereo sweeps.",
            "",
            "",
            "",
            "A transparent final ceiling." };
        const juce::String serialHint = "Click for the version.";
        const juce::String gettingStarted =
            "Hover over or drag any control to see what it does. Every module starts switched off: click its name to switch it on.";

        //======================================================================
        /** S or I LUFS: the number, with its label underneath. */
        struct Readout : public juce::Component
        {
            explicit Readout (juce::String l) : label (std::move (l))   {}

            void set (float lufs)
            {
                const auto t = lufs <= -69.95f ? juce::String ("--.-") : juce::String (lufs, 1);
                if (t != shown) { shown = t; repaint(); }
            }

            void paint (juce::Graphics& g) override
            {
                auto r = getLocalBounds().toFloat();
                g.setColour (Colours::value);
                g.setFont (text (18.0f, 1));
                g.drawText (shown, r.removeFromTop (20.0f), juce::Justification::centred, false);
                g.setColour (Colours::dim);
                g.setFont (text (11.5f, 0));
                g.drawText (label, r, juce::Justification::centredTop, false);
            }

            void mouseDown (const juce::MouseEvent&) override   { if (onClick) onClick(); }

            juce::String label, shown { "--.-" };
            std::function<void()> onClick;
        };

        //======================================================================
        /** The SETUP panel: choices that belong to you, for every strip. */
        struct SetupPanel : public juce::Component,
                            private juce::Timer
        {
            explicit SetupPanel (View& v) : view (v)
            {
                setDrawsOwnGlows (*this);
                auto& s = view.getSettings();

                juce::StringArray sizes;
                int chosenSize = 2;
                for (int i = 0; i < (int) std::size (StripSettings::sizes); ++i)
                {
                    sizes.add (juce::String (juce::roundToInt (StripSettings::sizes[i] * 100.0f)) + "%");
                    if (std::abs (StripSettings::sizes[i] - s.scale) < std::abs (StripSettings::sizes[chosenSize] - s.scale))
                        chosenSize = i;
                }

                add (sizeChoice, sizes, chosenSize, [this] (int i) { view.getSettings().scale = StripSettings::sizes[i]; changed(); });
                add (skinChoice, { "HOLOGRAM", "CLASSIC" }, s.skin, [this] (int i) { view.getSettings().skin = i; changed(); });
                add (tickerChoice, { "ON", "OFF" }, s.ticker ? 0 : 1, [this] (int i) { view.getSettings().ticker = i == 0; changed(); });
                add (meterChoice, { "0 dBFS", "-18 dBFS" }, s.meterZero, [this] (int i) { view.getSettings().meterZero = i; changed(); });
                add (brightnessChoice, { "LOW", "MEDIUM", "HIGH" }, s.dim, [this] (int i)
                {
                    view.getSettings().dim = i;
                    view.getSettings().save();
                    view.repaint();
                });

                // This strip only (saved with the project)
                precisionChoice = std::make_unique<Choice> (*view.getStrip().parameters.getParameter (StripIDs::moduleID (2, EQIDs::precision)),
                                                            juce::StringArray { "LOW", "MEDIUM", "HIGH" }, Colours::delayMode, true);
                addAndMakeVisible (*precisionChoice);

                // ORDER (this strip): the five switches
                for (int i = 0; i < StripIDs::numOrderSwitches; ++i)
                {
                    auto& b = orderButtons[(size_t) i];
                    b = std::make_unique<Button> (StripIDs::orderNames[i], Colours::selected);
                    b->fontSize = 13.5f;
                    orderLinks[(size_t) i] = std::make_unique<ButtonLink> (*view.getStrip().parameters.getParameter (StripIDs::orderSwitches[i]), *b);
                    addAndMakeVisible (*b);
                }

                close.onClick = [this] { view.showSetup (false); };
                addAndMakeVisible (close);
                setSize (640, 676);
                shownFlow = EasyStripAudioProcessor::describe (view.getStrip().getWantedOrder());
                startTimerHz (10);
            }

            void timerCallback() override
            {
                const auto flow = EasyStripAudioProcessor::describe (view.getStrip().getWantedOrder());
                if (flow != shownFlow) { shownFlow = flow; repaint(); }
            }

            void add (std::unique_ptr<Choice>& c, const juce::StringArray& names, int chosen, std::function<void (int)> onChoice)
            {
                c = std::make_unique<Choice> (names, Colours::cyan, true);
                c->setChosen (chosen);
                c->onUserChoice = std::move (onChoice);
                addAndMakeVisible (*c);
            }

            void changed()
            {
                view.getSettings().save();
                if (view.onSettingsChanged)
                    view.onSettingsChanged();
            }

            static constexpr int rowTop = 60, rowHeight = 46, labelWidth = 200;

            void resized() override
            {
                int y = rowTop;
                for (auto* c : { sizeChoice.get(), skinChoice.get(), tickerChoice.get(), meterChoice.get(), brightnessChoice.get() })
                {
                    const int w = c == sizeChoice.get() ? 400 : 290;
                    c->setBounds (24 + labelWidth, y + 8, w, 30);
                    y += rowHeight;
                }
                precisionChoice->setBounds (24 + labelWidth, rowTop + rowHeight * 5 + 44, 290, 30);
                const int orderTop = rowTop + rowHeight * 5 + 10 + 92;
                for (int i = 0; i < StripIDs::numOrderSwitches; ++i)
                    orderButtons[(size_t) i]->setBounds (24 + labelWidth + (i % 2) * 196, orderTop + (i / 2) * 36, 190, 30);
                close.setBounds (getWidth() - 24 - 100, getHeight() - 20 - 32, 100, 32);
            }

            void paint (juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat();
                g.setColour (juce::Colour (0xf2061220));
                g.fillPath (chamfered (r, 12.0f));
                strokeGlow (g, chamfered (r.reduced (0.5f), 12.0f), Colours::cyan, 1.2f);

                drawGlowText (g, "SETUP", { 24.0f, 16.0f, 300.0f, 26.0f }, juce::Justification::centredLeft, text (22.0f, 2), Colours::cyan);
                g.setColour (Colours::dim);
                g.setFont (text (13.0f, 0));
                g.drawText ("For every Easy Strip, not just this one.", juce::Rectangle<float> (300.0f, 18.0f, r.getWidth() - 324.0f, 24.0f),
                            juce::Justification::centredRight, false);

                static const char* names[] { "WINDOW SIZE", "SKIN", "TICKER", "METERS: 0 MEANS", "DISPLAY BRIGHTNESS" };
                g.setFont (text (15.0f, 1));
                g.setColour (Colours::ink);
                for (int i = 0; i < 5; ++i)
                    g.drawText (names[i], juce::Rectangle<float> (24.0f, (float) (rowTop + rowHeight * i), (float) labelWidth, (float) rowHeight),
                                juce::Justification::centredLeft, false);

                // This strip only
                const float line = (float) (rowTop + rowHeight * 5 + 10);
                g.setColour (Colours::cyan.withAlpha (0.3f));
                g.fillRect (24.0f, line, r.getWidth() - 48.0f, 1.0f);
                g.setColour (Colours::dim);
                g.setFont (text (13.0f, 0));
                g.drawText ("This strip only (saved with your project):", juce::Rectangle<float> (24.0f, line + 6.0f, 400.0f, 20.0f),
                            juce::Justification::centredLeft, false);
                g.setFont (text (15.0f, 1));
                g.setColour (Colours::ink);
                g.drawText ("EQ MASTER PRECISION", juce::Rectangle<float> (24.0f, line + 30.0f, (float) labelWidth, 46.0f), juce::Justification::centredLeft, false);

                g.drawText ("ORDER", juce::Rectangle<float> (24.0f, line + 88.0f, (float) labelWidth, 38.0f), juce::Justification::centredLeft, false);
                g.setColour (Colours::selected);
                g.setFont (text (14.0f, 1));
                g.drawText (shownFlow, juce::Rectangle<float> (24.0f, line + 198.0f, r.getWidth() - 48.0f, 22.0f), juce::Justification::centredLeft, false);
                paintGlows (g, *this);

                g.setColour (Colours::dim);
                g.setFont (text (13.0f, 0));
                g.drawFittedText ("PRECISION (with the EQ's MASTER on): HIGH = the most exact low end, about 43 ms of delay. MEDIUM = about 21 ms. "
                                  "LOW = about 11 ms and less smear before each hit (good on drums), but bands below about 60 Hz come out broader.\n"
                                  "ORDER changes only the order the sound goes through - the panels stay where they are. Also on the ORDER button by the presets.\n"
                                  "You can also drag the window's corner. Meters: 0 dBFS suits the master; -18 dBFS (like a VU meter) suits single tracks.",
                                  juce::Rectangle<int> (24, (int) line + 228, (int) r.getWidth() - 48, 74), juce::Justification::topLeft, 5);
                g.drawText ("Easy Strip " JucePlugin_VersionString "  -  built " __DATE__,
                            juce::Rectangle<float> (24.0f, r.getHeight() - 52.0f, 380.0f, 32.0f), juce::Justification::centredLeft, false);
            }

            View& view;
            std::unique_ptr<Choice> sizeChoice, skinChoice, tickerChoice, meterChoice, brightnessChoice, precisionChoice;
            std::array<std::unique_ptr<Button>, StripIDs::numOrderSwitches> orderButtons;
            std::array<std::unique_ptr<ButtonLink>, StripIDs::numOrderSwitches> orderLinks;
            juce::String shownFlow;
            Button close { "CLOSE", Colours::cyan };
        };
    }

    //==========================================================================
    /** A part of the panel drawn by a function (the cached backdrop, the ticker, the sweep). */
    struct Painter : public juce::Component
    {
        explicit Painter (std::function<void (juce::Graphics&)> f) : draw (std::move (f))   { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics& g) override   { g.setOrigin (-getPosition()); draw (g); }
        std::function<void (juce::Graphics&)> draw;
    };

    //==========================================================================
    /** WEAPON SELECT's effects rack: seven bays side by side. VERB's
        panel sits in its own bay; the rest say what's coming. */
    struct RackPanel : public juce::Component
    {
        RackPanel()   { setInterceptsMouseClicks (true, true); }

        std::array<juce::Rectangle<int>, View::numBays> bays;

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            const auto shape = chamfered (r.reduced (0.5f), 12.0f);
            g.setColour (juce::Colour (0xfa040b14));
            g.fillPath (shape);
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (shape);
                g.setColour (Colours::hazard.withAlpha (0.035f));
                for (float x = r.getX(); x < r.getRight(); x += 24.0f) g.fillRect (x, r.getY(), 1.0f, r.getHeight());
                for (float y = r.getY(); y < r.getBottom(); y += 24.0f) g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
            }
            strokeGlow (g, shape, Colours::hazard, 1.2f, 0.4f);

            drawGlowText (g, "EASY FX", juce::Rectangle<float> (18.0f, 6.0f, 300.0f, 30.0f), juce::Justification::centredLeft, display (22.0f), Colours::hazard, 0.7f);
            g.setColour (Colours::dim);
            g.setFont (text (14.0f, 1));
            g.drawText ("THE EFFECTS RACK   -   WEAPON SELECT AGAIN: BACK TO THE STRIP", juce::Rectangle<float> (170.0f, 6.0f, r.getWidth() - 190.0f, 30.0f),
                        juce::Justification::centredRight, false);

            for (int b = 0; b < View::numBays; ++b)
            {
                if (b == View::slapBay || b == View::delayBay || b == View::tremBay || b == View::wahBay || b == View::flangeBay || b == View::filterBay)
                    continue;   // (that effect's own panel is there)

                const auto bay = bays[(size_t) b].toFloat();
                const auto bayShape = chamfered (bay, 9.0f);
                g.setColour (juce::Colour (0x80071423));
                g.fillPath (bayShape);
                const float dashes[] = { 6.0f, 5.0f };
                juce::Path dashed;
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, bayShape, dashes, 2);
                g.setColour (Colours::offEdge);
                g.fillPath (dashed);

                g.setColour (Colours::offText);
                g.setFont (text (13.0f, 1));
                g.drawText ("BAY " + juce::String (b + 1), bay.withHeight (30.0f), juce::Justification::centred, false);
                drawGlowText (g, View::bayNames[b], bay.withTrimmedTop (34.0f).withHeight (24.0f), juce::Justification::centred, text (16.0f, 2),
                              Colours::ink.withAlpha (0.55f), 0.3f);
                g.setColour (Colours::hazard.withAlpha (0.55f));
                g.setFont (text (14.0f, 2));
                g.drawText ("COMING SOON", bay.withSizeKeepingCentre (bay.getWidth(), 20.0f), juce::Justification::centred, false);
                g.setColour (Colours::dim);
                g.setFont (text (13.0f, 0));
                g.drawFittedText (bayNotes[b], bay.withTrimmedTop (bay.getHeight() * 0.5f + 16.0f).withHeight (60.0f).reduced (10.0f, 0.0f).toNearestInt(),
                                  juce::Justification::centredTop, 3);
            }
        }
    };

    //==========================================================================
    /** The header's preset window: the strip preset's name; click for the list. */
    struct View::PresetWindow : public juce::Component
    {
        explicit PresetWindow (View& v) : view (v)   { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (juce::Colour (0xb3040c16));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Colours::selected);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);

            auto& strip = view.getStrip();
            auto name = strip.getPresetName();
            if (name.isEmpty()) name = "PRESETS...";
            if (strip.isPresetEdited()) name += " *";
            drawGlowText (g, name.toUpperCase(), r.reduced (10.0f, 0.0f), juce::Justification::centred, text (18.0f, 1), Colours::selected, 0.6f);
        }

        void mouseDown (const juce::MouseEvent&) override   { view.showPresetMenu (-1, *this); }

        View& view;
    };

    //==========================================================================
    View::View (EasyStripAudioProcessor& p, StripSettings& s)
        : strip (p), settings (s), screen (p)
    {
        setOpaque (true);
        collectHints();

        // What never moves is drawn once and kept as a picture (sharp at any size)
        backdrop = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintBackdrop (g); });
        backdrop->setBufferedToImage (true);
        backdrop->setOpaque (true);
        addAndMakeVisible (*backdrop);
        standby = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintStandby (g); });
        addAndMakeVisible (*standby);
        ticker = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintTicker (g); });
        addAndMakeVisible (*ticker);

        // The light of lit buttons spilling onto the console (over the backdrop, under the controls)
        glowLayer = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintGlows (g, *this); paintHazardFrame (g); });
        addAndMakeVisible (*glowLayer);

        // The track's name, top left (as your DAW calls it)
        trackName = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintTrackName (g); });
        trackName->setInterceptsMouseClicks (true, false);   // (for its hint)
        addAndMakeVisible (*trackName);
        addHint (*trackName, "TRACK", trackNameHint);

        // Header
        presetWindowComponent = std::make_unique<PresetWindow> (*this);
        addAndMakeVisible (*presetWindowComponent);
        addHint (*presetWindowComponent, "PRESETS", presetHint);
        for (auto* b : { &presetPrevious, &presetNext })
        {
            b->fontSize = 16.0f;
            addAndMakeVisible (*b);
            addHint (*b, "PRESETS", presetHint);
        }
        presetPrevious.onClick = [this] { stepPreset (-1); };
        presetNext.onClick = [this] { stepPreset (1); };

        for (int slot = 0; slot < 2; ++slot)
        {
            auto& b = slot == 0 ? slotA : slotB;
            b.fontSize = 15.0f;
            b.onClick = [this, slot]
            {
                strip.switchToSlot (slot);
                strip.pushSettingsIfIdle (lastAudioCount, quietFrames = 100);
                timerCallback();
            };
            addAndMakeVisible (b);
            addHint (b, "A / B (THE WHOLE STRIP)", stripSlotHint);
        }
        orderButton.fontSize = 14.0f;
        orderButton.onClick = [this] { showOrderMenu(); };
        addAndMakeVisible (orderButton);
        addHint (orderButton, "ORDER", {});   // (its text follows the order: see showHintFor)

        copySlot.fontSize = 13.5f;
        copySlot.onClick = [this] { strip.copyToOtherSlot(); announce ("COPIED", strip.getActiveSlot() == 0 ? "A to B" : "B to A", 3); };
        addAndMakeVisible (copySlot);
        addHint (copySlot, "COPY", stripCopyHint);

        stripPresets.setPresets ("Easy Strip", strip.getBuiltInPresets());
        stripPresets.getCurrentValues = [this] { return strip.getCurrentPresetValues(); };
        stripPresets.onPresetChosen = [this] (const Preset& preset) { loadStripPreset (preset); };

        oversampling = std::make_unique<Choice> (*strip.parameters.getParameter (StripIDs::oversampling),
                                                 juce::StringArray { "4x / 8x", "OFF", "4x", "8x" }, Colours::delayMode, true);
        addAndMakeVisible (*oversampling);
        addHint (*oversampling, "OVERSAMPLING", oversamplingHint);


        initAllButton.onClick = [this]
        {
            loadStripPreset (strip.getBuiltInPresets().front());
            initAllButton.flash = true;
            initAllButton.repaint();
            juce::Component::SafePointer<View> safe (this);
            juce::Timer::callAfterDelay (180, [safe]
            {
                if (safe != nullptr) { safe->initAllButton.flash = false; safe->initAllButton.repaint(); }
            });
        };
        addAndMakeVisible (initAllButton);
        addHint (initAllButton, "INIT", initAllHint);

        bypassLink = std::make_unique<ButtonLink> (*strip.parameters.getParameter (CommonIDs::bypass), bypassButton);
        addAndMakeVisible (bypassButton);
        addHint (bypassButton, "BYPASS", bypassHint);

        setupButton.onClick = [this] { showSetup (! isSetupShowing()); };
        addAndMakeVisible (setupButton);
        addHint (setupButton, "SETUP", setupHint);

        // The page buttons (by module; the rack effects' aren't shown - their panels are in the rack)
        for (int m = 0; m < StripModules::numModules; ++m)
        {
            pageButtons[(size_t) m] = std::make_unique<Button> (juce::String (StripModules::tabs[m]),
                                                                Colours::selected, Button::Style::view);
            auto& b = *pageButtons[(size_t) m];
            b.fontSize = m == 6 ? 13.0f : 14.5f;
            b.onClick = [this, m] { showPage (m); };
            if (! StripModules::isRackEffect (m))
                addAndMakeVisible (b);
            addHint (b, "DISPLAY FOCUS: " + juce::String (StripModules::tabs[m]), pageHint);
        }

        addAndMakeVisible (screen);
        addHint (screen, "THE BIG SCREEN", moduleHint (2, "THE CURVE"));

        // INPUT
        addAndMakeVisible (inputMeter);
        addHint (inputMeter, "INPUT", "How loud the sound is going in, after TRIM. 0 is the reference chosen in SETUP.");
        trim = std::make_unique<Ring> (*strip.parameters.getParameter (StripIDs::trim), "TRIM", Format::signedDb, "dB", Ring::small, Colours::cyan);
        trim->setFromCentre (true);
        addAndMakeVisible (*trim);
        addHint (*trim, "TRIM", trimHint);
        registerControl (-1, StripIDs::trim, *trim);
        polarityLink = std::make_unique<ButtonLink> (*strip.parameters.getParameter (StripIDs::polarity), polarityButton);
        polarityButton.fontSize = 16.0f;
        addAndMakeVisible (polarityButton);
        addHint (polarityButton, "POLARITY", polarityHint);

        // The rack (WEAPON SELECT), behind the modules' panels
        rack = std::make_unique<RackPanel>();
        addChildComponent (*rack);
        addHint (*rack, "EASY FX", rackHint);

        // The modules
        for (int m = 0; m < StripModules::numModules; ++m)
        {
            slabs.push_back (makeSlab (*this, m));
            addAndMakeVisible (*slabs.back());
        }
        for (int m : { (int) StripModules::slapIndex, (int) StripModules::delayIndex, (int) StripModules::tremIndex, (int) StripModules::wahIndex, (int) StripModules::flangeIndex, (int) StripModules::filterIndex })
            slabs[(size_t) m]->setVisible (false);   // (their panels are in the WEAPON SELECT rack)
        clipAdvanced = std::make_unique<ClipAdvanced> (*this);
        addChildComponent (*clipAdvanced);

        weaponSelect.fontSize = 14.0f;
        weaponSelect.displayFont = true;
        weaponSelect.glowFrom = 6.0f;      // (from outside its hazard frame)
        weaponSelect.glowSize = 34;        // (a wide, gentle bleed onto the console - it never swamps it)
        weaponSelect.glowStrength = 2.4f;
        weaponSelect.onClick = [this] { showRack (! rackShown); };
        addAndMakeVisible (weaponSelect);
        addHint (weaponSelect, "WEAPON SELECT", weaponHint);

        // OUTPUT
        autoGainLink = std::make_unique<ButtonLink> (*strip.parameters.getParameter (CommonIDs::autoGain), autoGainButton);
        autoGainButton.fontSize = 14.0f;
        addAndMakeVisible (autoGainButton);
        addHint (autoGainButton, "AUTO GAIN (THE WHOLE STRIP)", stripAutoGainHint);
        autoGainReadout.setFont (text (13.5f, 1));
        autoGainReadout.setJustificationType (juce::Justification::centred);
        autoGainReadout.setColour (juce::Label::textColourId, Colours::dim);
        autoGainReadout.setBorderSize ({});
        addAndMakeVisible (autoGainReadout);
        addHint (autoGainReadout, "AUTO GAIN (THE WHOLE STRIP)", stripAutoGainHint);

        meterMode = std::make_unique<Choice> (juce::StringArray { "PEAK", "LUFS" }, Colours::cyan, false);
        meterMode->setChosen (settings.lufsMeter ? 1 : 0);
        meterMode->segmentHeight = 26;
        meterMode->onUserChoice = [this] (int i) { settings.lufsMeter = i == 1; settings.save(); };
        addAndMakeVisible (*meterMode);
        addHint (*meterMode, "PEAK / LUFS", meterModeHint);
        addAndMakeVisible (outputMeter);
        addHint (outputMeter, "OUTPUT", "How loud the sound is coming out, after LEVEL. Keep the peaks below 0 dBFS.");

        auto shortTerm = std::make_unique<Readout> ("LUFS S");
        auto integrated = std::make_unique<Readout> ("LUFS I");
        integrated->onClick = [this] { strip.resetIntegratedLufs(); };
        integrated->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (*shortTerm);
        addAndMakeVisible (*integrated);
        addHint (*shortTerm, "LUFS", lufsHint);
        addHint (*integrated, "LUFS", lufsHint);
        lufsShortTerm = std::move (shortTerm);
        lufsIntegrated = std::move (integrated);

        level = std::make_unique<Ring> (*strip.parameters.getParameter (StripIDs::level), "LEVEL", Format::signedDb, "dB", Ring::small, Colours::level);
        level->setFromCentre (true);
        addAndMakeVisible (*level);
        addHint (*level, "LEVEL", levelHint);
        registerControl (-1, StripIDs::level, *level);

        // Typing a value
        entry.setJustification (juce::Justification::centred);
        entry.setFont (text (17.0f, 1));
        entry.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff04101e));
        entry.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        entry.setColour (juce::TextEditor::outlineColourId, Colours::selected);
        entry.setColour (juce::TextEditor::focusedOutlineColourId, Colours::selected);
        entry.setColour (juce::TextEditor::highlightColourId, Colours::selected.withAlpha (0.4f));
        entry.onReturnKey = [this] { const auto t = entry.getText(); entry.setVisible (false); if (onEntry) onEntry (t); onEntry = nullptr; };
        entry.onEscapeKey = [this] { entry.setVisible (false); onEntry = nullptr; valueShown(); };
        entry.onFocusLost = [this] { if (entry.isVisible()) entry.onReturnKey(); };
        addChildComponent (entry);

        // Module preset menus
        for (int m = 0; m < StripModules::numModules; ++m)
        {
            auto& module = strip.getModule (m);
            auto browser = std::make_unique<PresetBrowser>();
            browser->setPresets (module.getPresetFolderName(), module.getBuiltInPresets());
            browser->getCurrentValues = [&module] { return module.getCurrentPresetValues(); };
            browser->onPresetChosen = [&module] (const Preset& preset) { module.loadPreset (preset); };
            modulePresets[(size_t) m] = std::move (browser);
        }

        setTicker ("", gettingStarted, Colours::cyan);
        showPage (2);
        setSize (width, heightFor (settings.ticker));
        startTimerHz (30);
    }

    View::~View()
    {
        stopTimer();
        clipAdvanced.reset();
        slabs.clear();
    }

    //==========================================================================
    void View::collectHints()
    {
        // The standalone windows' own hints, word for word: each one is opened
        // (out of sight) once, and its hints copied.
        static std::array<std::map<juce::String, juce::String>, StripModules::numModules> cache;
        static bool collected = false;

        if (! collected)
        {
            for (int m = 0; m < StripModules::numModules; ++m)
            {
                auto& module = strip.getModule (m);
                if (module.getActiveEditor() != nullptr)
                    continue;

                std::unique_ptr<juce::AudioProcessorEditor> editor (module.createEditorAndMakeActive());
                if (auto* window = dynamic_cast<EasyWindow*> (editor.get()))
                    for (auto& [title, text] : window->getAllHints())
                        if (cache[(size_t) m].count (title) == 0)
                            cache[(size_t) m][title] = text;
                module.editorBeingDeleted (editor.get());
            }
            collected = true;
        }

        standaloneHints = cache;
    }

    juce::String View::moduleHint (int module, const juce::String& title) const
    {
        const auto& hintsForModule = standaloneHints[(size_t) module];
        const auto found = hintsForModule.find (title);
        return found != hintsForModule.end() ? found->second : juce::String();
    }

    void View::registerControl (int module, const juce::String& parameterID, ParamControl& control)
    {
        controls[juce::String (module) + "/" + parameterID] = &control;
    }

    ParamControl* View::findControl (int module, const juce::String& parameterID) const
    {
        const auto found = controls.find (juce::String (module) + "/" + parameterID);
        return found != controls.end() ? found->second : nullptr;
    }

    //==========================================================================
    void View::resized()
    {
        const int tickerHeight = settings.ticker ? 46 : 0;

        headerArea = { left, top, width - 2 * left, headerHeight };
        {
            auto h = headerArea.reduced (14, 6);

            setupButton.setBounds (h.removeFromRight (96));
            h.removeFromRight (12);
            bypassButton.setBounds (h.removeFromRight (90));
            h.removeFromRight (12);
            initAllButton.setBounds (h.removeFromRight (54));
            h.removeFromRight (24);
            oversampling->setBounds (h.removeFromRight (230).withSizeKeepingCentre (230, 28));
            // The OVERSAMPLING lettering (drawn ending 6 px before its buttons), and the same 6 px gap before ORDER
            const int lettering = juce::roundToInt (std::ceil (juce::GlyphArrangement::getStringWidth (text (13.5f, 1), "OVERSAMPLING")));
            h.removeFromRight (6 + lettering + 6);

            // The presets to ORDER, right up against OVERSAMPLING
            orderButton.setBounds (h.removeFromRight (76));
            h.removeFromRight (16);
            copySlot.setBounds (h.removeFromRight (52));
            h.removeFromRight (8);
            slotB.setBounds (h.removeFromRight (32));
            h.removeFromRight (4);
            slotA.setBounds (h.removeFromRight (32));
            h.removeFromRight (16);
            presetNext.setBounds (h.removeFromRight (30));
            h.removeFromRight (4);
            presetWindow = h.removeFromRight (560);
            presetWindowComponent->setBounds (presetWindow);
            h.removeFromRight (4);
            presetPrevious.setBounds (h.removeFromRight (30));
            h.removeFromRight (18);

            // ...and the track's name in the space left of them
            trackNameArea = h;
        }

        viewRow = { left, viewRowY, width - 2 * left, viewRowHeight };
        {
            // Narrow buttons, all over the big screen
            constexpr int shown = (int) std::size (pageOrder);
            constexpr int buttonWidth = 64, gap = (wScreen - shown * buttonWidth) / (shown - 1);
            int x = colScreen;
            for (int m : pageOrder)
            {
                pageButtons[(size_t) m]->setBounds (x, viewRowY, buttonWidth, viewRowHeight);
                x += buttonWidth + gap;
            }
        }

        const int bottom = mainBottom;
        inputColumn = { colIn, mainY, wIn, bottom - mainY };
        outputColumn = { colOut, mainY, wOut, bottom - mainY };

        // INPUT: name, meter, TRIM, polarity
        {
            auto c = inputColumn.reduced (6, 10);
            c.removeFromTop (22);
            polarityButton.setBounds (c.removeFromBottom (30).withSizeKeepingCentre (46, 30));
            c.removeFromBottom (8);
            trim->setBounds (c.removeFromBottom (trim->getIdealHeight()).withSizeKeepingCentre (trim->getIdealWidth(), trim->getIdealHeight()));
            c.removeFromBottom (12);
            inputMeter.setBounds (c.withTrimmedTop (8));
        }

        // OUTPUT: name, PEAK / LUFS, meter, LUFS S and I, LEVEL
        {
            auto c = outputColumn.reduced (6, 10);
            c.removeFromTop (22);
            autoGainButton.setBounds (c.removeFromTop (46).withSizeKeepingCentre (64, 46));
            c.removeFromTop (3);
            autoGainReadout.setBounds (c.removeFromTop (16));
            c.removeFromTop (10);
            meterMode->setBounds (c.removeFromTop (meterMode->getIdealHeight()).withSizeKeepingCentre (64, meterMode->getIdealHeight()));
            c.removeFromTop (10);
            level->setBounds (c.removeFromBottom (level->getIdealHeight()).withSizeKeepingCentre (level->getIdealWidth(), level->getIdealHeight()));
            c.removeFromBottom (10);
            lufsIntegrated->setBounds (c.removeFromBottom (36).expanded (6, 0));
            c.removeFromBottom (4);
            lufsShortTerm->setBounds (c.removeFromBottom (36).expanded (6, 0));
            c.removeFromBottom (12);
            outputMeter.setBounds (c);
        }

        // Gate, Amp; the screen and EQ; the bay under them
        slabs[0]->setBounds (colGate, mainY, wGate, 458);
        slabs[1]->setBounds (colGate, mainY + 468, wGate, 852 - (mainY + 468));
        screen.setBounds (colScreen, mainY, wScreen, 236);
        slabs[2]->setBounds (colScreen, mainY + 246, wScreen, 852 - (mainY + 246));
        logoArea = { colGate, 862, wGate, bottom - 862 };
        standbyArea = { colScreen, 862, wScreen, bottom - 862 };
        clipAdvanced->setBounds (standbyArea);

        // The right-hand block
        slabs[3]->setBounds (colRight, mainY, wRight, 268);
        slabs[4]->setBounds (colRight, mainY + 278, wRight, 842 - (mainY + 278));
        slabs[5]->setBounds (colRight2, mainY, wRight2, 320);
        slabs[StripModules::tapeIndex]->setBounds (colRight2, mainY + 330, wRight2, 298);   // (VERB's old place)
        weaponArea = { colRight2, mainY + 638, wRight2, 842 - (mainY + 638) };
        weaponSelect.setBounds (weaponArea.withSizeKeepingCentre (212, 46));
        slabs[7]->setBounds (colRight, 852, colRight2 + wRight2 - colRight, bottom - 852);

        layoutRack();

        tickerArea = settings.ticker ? juce::Rectangle<int> (left, bottom + 10, width - 2 * left, tickerHeight) : juce::Rectangle<int>();
        backdrop->setBounds (getLocalBounds());
        glowLayer->setBounds (getLocalBounds());
        trackName->setBounds (trackNameArea);
        standby->setBounds (standbyArea);
        ticker->setBounds (tickerArea);
        ticker->setVisible (! tickerArea.isEmpty());

        if (setupPanel != nullptr)
            setupPanel->setCentrePosition (screen.getBounds().getCentreX(), 440);
    }

    //==========================================================================
    void View::paint (juce::Graphics&) {}

    void View::paintHazardFrame (juce::Graphics& g)
    {
        // WEAPON SELECT's hazard surround: solid paint - its glow shines out around it, never through it
        const auto b = weaponSelect.getBounds().toFloat().expanded (6.0f);
        juce::Graphics::ScopedSaveState save (g);
        juce::Path frame;
        frame.addRoundedRectangle (b, 9.0f);
        g.reduceClipRegion (frame);
        g.setColour (juce::Colour (0xff121212));
        g.fillRect (b);
        g.setColour (Colours::hazard);
        for (float x = b.getX() - b.getHeight(); x < b.getRight(); x += 18.0f)
        {
            juce::Path stripe;
            stripe.addQuadrilateral (x, b.getBottom(), x + 9.0f, b.getBottom(), x + 9.0f + b.getHeight(), b.getY(), x + b.getHeight(), b.getY());
            g.fillPath (stripe);
        }
    }

    void View::paintTrackName (juce::Graphics& g)
    {
        if (shownTrackName.isEmpty())
            return;

        const auto area = trackNameArea.toFloat();
        auto font = display (24.0f);
        const float natural = juce::GlyphArrangement::getStringWidth (font, shownTrackName.toUpperCase());
        if (natural > area.getWidth())   // (a long name: smaller, down to a point, then cut short with "...")
            font = display (juce::jmax (14.0f, 24.0f * area.getWidth() / natural));

        auto text = shownTrackName.toUpperCase();
        while (text.length() > 1 && juce::GlyphArrangement::getStringWidth (font, text) > area.getWidth())
            text = text.dropLastCharacters (2) + juce::String::fromUTF8 ("\xe2\x80\xa6");

        drawGlowText (g, text, area, juce::Justification::centredLeft, font, Colours::cyan, 0.6f);
    }

    void View::paintBackdrop (juce::Graphics& g)
    {
        const auto all = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d2338), all.getCentreX(), all.getHeight() * 0.08f,
                                                 juce::Colour (0xff02050a), all.getCentreX(), all.getHeight() * 1.1f, true));
        g.fillAll();

        // The panel: chamfered glass with a faint projection grid behind everything
        const auto panel = all.reduced (5.0f);
        const auto shape = chamfered (panel, 16.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xeb0a1a2a), 0.0f, panel.getY(), juce::Colour (0xf5050d17), 0.0f, panel.getBottom(), false));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);
            g.setColour (Colours::cyan.withAlpha (0.025f));
            for (float x = panel.getX(); x < panel.getRight(); x += 24.0f)
                g.fillRect (x, panel.getY(), 1.0f, panel.getHeight());
            for (float y = panel.getY(); y < panel.getBottom(); y += 24.0f)
                g.fillRect (panel.getX(), y, panel.getWidth(), 1.0f);
        }
        g.setColour (Colours::cyan.withAlpha (0.10f));
        g.strokePath (shape, juce::PathStrokeType (8.0f));
        g.setColour (Colours::cyan.withAlpha (0.6f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));

        // Header strip
        {
            const auto h = headerArea.toFloat();
            g.setColour (juce::Colour (0xb3061220));
            g.fillPath (chamfered (h, 8.0f));
            g.setColour (Colours::cyan.withAlpha (0.4f));
            g.strokePath (chamfered (h.reduced (0.5f), 8.0f), juce::PathStrokeType (1.0f));

            g.setFont (text (13.5f, 1));
            g.setColour (Colours::dim);
            g.drawText ("OVERSAMPLING", juce::Rectangle<int> (oversampling->getX() - 100, oversampling->getY(), 94, 28), juce::Justification::centredRight, false);
        }

        // DISPLAY FOCUS: (beside the page buttons)
        {
            const auto first = pageButtons[0]->getBounds();
            drawGlowText (g, "DISPLAY FOCUS:", juce::Rectangle<float> ((float) first.getX() - 180.0f, (float) first.getY(), 170.0f, (float) first.getHeight()),
                          juce::Justification::centredRight, text (16.0f, 2), Colours::cyan, 0.5f);
        }

        // INPUT and OUTPUT columns
        for (auto [area, name] : { std::pair<juce::Rectangle<int>, const char*> { inputColumn, "INPUT" }, { outputColumn, "OUTPUT" } })
        {
            drawModule (g, area.toFloat(), Colours::cyan);
            drawGlowText (g, name, area.toFloat().removeFromTop (34.0f).withTrimmedTop (8.0f), juce::Justification::centred, text (15.5f, 2), Colours::cyan);
        }

        // The name, under Amp
        {
            const auto r = logoArea.toFloat();
            const auto big = display (34.0f);
            const float wide = widthOf (big, "AARDVARK");
            auto small = display (22.0f);
            small = small.withHeight (22.0f * wide / juce::jmax (1.0f, widthOf (small, "EASY STRIP")));
            drawGlowText (g, "AARDVARK", r.withHeight (r.getHeight() * 0.56f), juce::Justification::centredBottom, big, juce::Colour (0xffe6fbff), 0.9f);
            drawGlowText (g, "EASY STRIP", r.withTrimmedTop (r.getHeight() * 0.58f), juce::Justification::centredTop, small, juce::Colour (0xff9fe9ff), 0.9f);
        }

        // The standby panel under the EQ (when Clip isn't showing ADVANCED there)
        {
            const auto r = standbyArea.toFloat();
            const auto shape2 = chamfered (r, 9.0f);
            g.setColour (juce::Colour (0x8c030a12));
            g.fillPath (shape2);
            {
                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (shape2);
                g.setColour (Colours::cyan.withAlpha (0.22f));
                for (float y = r.getY() + 8.0f; y < r.getBottom(); y += 12.0f)
                    for (float x = r.getX() + 8.0f; x < r.getRight(); x += 12.0f)
                        g.fillEllipse (x - 0.9f, y - 0.9f, 1.8f, 1.8f);

            }
            g.setColour (Colours::cyan.withAlpha (0.2f));
            g.strokePath (shape2, juce::PathStrokeType (1.0f));

            g.setColour (Colours::cyan);
            for (auto corner : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
            {
                const float dx = corner.x < r.getCentreX() ? 8.0f : -8.0f, dy = corner.y < r.getCentreY() ? 8.0f : -8.0f;
                const juce::Point<float> c (corner.x + dx, corner.y + dy);
                g.fillRect (juce::Rectangle<float> (c, c.translated (dx * 2.0f, dy > 0 ? 2.0f : -2.0f)));
                g.fillRect (juce::Rectangle<float> (c, c.translated (dx > 0 ? 2.0f : -2.0f, dy * 2.0f)));
            }

            const auto centre = r.getCentre();
            g.setColour (Colours::cyan.withAlpha (0.45f));
            g.drawEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (centre), 1.0f);
            g.fillRect (centre.x - 20.0f, centre.y - 0.5f, 40.0f, 1.0f);
            g.fillRect (centre.x - 0.5f, centre.y - 20.0f, 1.0f, 40.0f);
        }

        // (WEAPON SELECT's hazard surround: drawn over its glow, in paintHazardFrame)

    }

    void View::paintStandby (juce::Graphics& g)
    {
        // (only the slow sweep moves: the rest is part of the backdrop)
        const auto r = standbyArea.toFloat();
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (chamfered (r, 9.0f));
        const float sx = r.getX() - r.getWidth() * 0.3f + (r.getWidth() * 1.3f) * sweep;
        juce::ColourGradient grad (Colours::cyan.withAlpha (0.0f), sx, 0.0f, Colours::cyan.withAlpha (0.0f), sx + r.getWidth() * 0.3f, 0.0f, false);
        grad.addColour (0.5, Colours::cyan.withAlpha (0.10f));
        g.setGradientFill (grad);
        g.fillRect (sx, r.getY(), r.getWidth() * 0.3f, r.getHeight());
    }

    void View::paintTicker (juce::Graphics& g)
    {
        // The ticker
        {
            const auto r = tickerArea.toFloat();
            g.setColour (juce::Colour (0xbf040c16));
            g.fillPath (chamfered (r, 7.0f));
            g.setColour (Colours::cyan.withAlpha (0.45f));
            g.strokePath (chamfered (r.reduced (0.5f), 7.0f), juce::PathStrokeType (1.0f));

            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (tickerArea.reduced (14, 2));
            float x = r.getX() + 18.0f - tickerScroll;
            if (tickerTitle.isNotEmpty())
            {
                const auto f = text (19.5f, 2);
                drawGlowText (g, tickerTitle, { x, r.getY(), 2000.0f, r.getHeight() }, juce::Justification::centredLeft, f, tickerColour, 0.6f);
                x += widthOf (f, tickerTitle) + 14.0f;
            }
            drawGlowText (g, tickerText, { x, r.getY(), 6000.0f, r.getHeight() }, juce::Justification::centredLeft, text (19.5f, 0),
                          juce::Colour (0xff9fe9ff), 0.5f);
        }
    }

    void View::paintOverChildren (juce::Graphics& g)
    {
        if (settings.dim < 2)   // DISPLAY BRIGHTNESS (SETUP)
        {
            g.setColour (juce::Colours::black.withAlpha (settings.dim == 0 ? 0.45f : 0.25f));
            g.fillAll();
        }
    }

    //==========================================================================
    // Hints, values, the ticker
    //==========================================================================
    void View::addHint (juce::Component& c, const juce::String& title, const juce::String& text)
    {
        c.addMouseListener (this, true);
        hints.push_back ({ &c, title, text });
    }

    void View::mouseEnter (const juce::MouseEvent& e)
    {
        hovered = e.eventComponent;
        if (! showingValue && announceUntil <= 0.0)
            showHintFor (hovered.getComponent());
    }

    void View::mouseExit (const juce::MouseEvent&)
    {
        hovered = nullptr;
    }

    juce::Colour View::tintFor (juce::Component* c) const
    {
        if (c != nullptr)
            if (auto* slab = dynamic_cast<Slab*> (c) != nullptr ? dynamic_cast<Slab*> (c) : c->findParentComponentOfClass<Slab>())
                return slab->power.tint;
        return Colours::cyan;
    }

    void View::showHintFor (juce::Component* c)
    {
        for (auto* walk = c; walk != nullptr && walk != this; walk = walk->getParentComponent())
            for (auto& h : hints)
                if (h.component == walk)
                {
                    juce::String title = h.title;
                    if (walk == &orderButton)
                    {
                        setTicker ("ORDER", "The sound goes " + EasyStripAudioProcessor::describe (strip.getWantedOrder())
                                   + ". Click for the five switches (also in SETUP). The panels never move.", Colours::selected);
                        return;
                    }
                    if (auto* slab = walk->findParentComponentOfClass<Slab>())
                        if (! title.startsWith (StripModules::tabs[slab->getModule()]))
                            title = juce::String (StripModules::tabs[slab->getModule()]) + " " + juce::String::fromUTF8 ("\xc2\xb7") + " " + title;
                    setTicker (title, h.text, tintFor (walk));
                    return;
                }
    }

    void View::setTicker (const juce::String& title, const juce::String& t, juce::Colour colour)
    {
        if (title == tickerTitle && t == tickerText)
            return;
        tickerTitle = title;
        tickerText = t;
        tickerColour = colour;
        tickerScroll = 0.0f;
        tickerPause = 45;
        ticker->repaint();
    }

    void View::showValue (const juce::String& title, const juce::String& valueWithUnits)
    {
        showingValue = true;
        announceUntil = 0.0;
        setTicker (title.toUpperCase(), valueWithUnits, Colours::selected);
    }

    void View::valueShown()
    {
        showingValue = false;
        showHintFor (hovered.getComponent());
    }

    void View::announce (const juce::String& title, const juce::String& t, int seconds)
    {
        announceUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        setTicker (title, t, Colours::selected);
    }

    void View::typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
                          std::function<void (const juce::String&)> onEntered)
    {
        const auto inView = getLocalArea (&owner, area);
        const int w = juce::jmax (90, inView.getWidth() + 24);
        entry.setBounds (inView.getCentreX() - w / 2, inView.getCentreY() - 15, w, 30);
        entry.setText (current, false);
        entry.setVisible (true);
        entry.toFront (true);
        if (entry.isShowing())
            entry.grabKeyboardFocus();
        entry.selectAll();
        onEntry = std::move (onEntered);
        setTicker ("TYPE A VALUE", "and press Enter (Esc cancels). Units are optional; k means thousand, e.g. 4.5k. OFF works where there's an OFF.", Colours::selected);
    }

    void View::settingMoved (juce::RangedAudioParameter& p)
    {
        for (int m = 0; m < StripModules::numModules; ++m)
        {
            auto& module = strip.getModule (m);
            if (module.parameters.getParameter (p.paramID) == &p)
                module.markPresetEdited();
        }
        strip.markPresetEdited();
        presetWindowComponent->repaint();
    }

    //==========================================================================
    void View::timerCallback()
    {
        strip.pushSettingsIfIdle (lastAudioCount, quietFrames);

        for (auto& slab : slabs)
            slab->refresh();
        screen.refresh();

        const float zero = settings.getMeterZeroDb();
        const float in = juce::Decibels::gainToDecibels (strip.takeInputPeak(), -200.0f) - zero;
        inputMeter.setLevels (in, in);

        const float outPeak = juce::Decibels::gainToDecibels (strip.takeOutputPeak(), -200.0f) - zero;
        const float out = settings.lufsMeter ? strip.getShortTermLufs() : outPeak;
        outputMeter.setLevels (out, out);

        {
            const bool on = autoGainButton.getToggleState();
            const auto text = (on ? "" : "(") + Holo::formatValue (Format::signedDb, strip.getAutoGainDb()) + " dB" + (on ? "" : ")");
            if (autoGainReadout.getText() != text)
                autoGainReadout.setText (text, juce::dontSendNotification);
            autoGainReadout.setColour (juce::Label::textColourId, on ? Colours::gate : Colours::dim);
        }

        static_cast<Readout*> (lufsShortTerm.get())->set (strip.getShortTermLufs());
        static_cast<Readout*> (lufsIntegrated.get())->set (strip.getIntegratedLufs());

        for (int m = 0; m < StripModules::numModules; ++m)
            pageButtons[(size_t) m]->setToggleState (m == screen.getPage(), juce::dontSendNotification);
        // WEAPON SELECT lights up while any of the EASY FX are on (or the rack is open)
        {
            bool anyOn = false;
            for (int m = 0; m < StripModules::numModules; ++m)
                anyOn = anyOn || (StripModules::isRackEffect (m) && strip.isModuleOn (m));
            weaponSelect.setToggleState (rackShown || anyOn, juce::dontSendNotification);
        }

        // The track's name (your DAW tells the strip when it changes)
        {
            const auto name = strip.getTrackName();
            if (name != shownTrackName) { shownTrackName = name; trackName->repaint(); }
        }

        {
            const int slot = strip.getActiveSlot();
            slotA.setToggleState (slot == 0, juce::dontSendNotification);
            slotB.setToggleState (slot == 1, juce::dontSendNotification);
            copySlot.setButtonText (slot == 0 ? "A > B" : "B > A");
            orderButton.setToggleState (! strip.isStandardOrder(), juce::dontSendNotification);
        }

        {
            const auto shown = strip.getPresetName() + (strip.isPresetEdited() ? "*" : "");
            if (shown != shownPresetText) { shownPresetText = shown; presetWindowComponent->repaint(); }
        }

        // The standby panel's slow sweep (6 s across)
        if (! clipAdvanced->isVisible())
        {
            sweep += 1.0f / 180.0f;
            if (sweep > 1.0f) sweep = 0.0f;
            standby->repaint();
        }

        // Announcements time out; long hints scroll
        if (announceUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > announceUntil)
        {
            announceUntil = 0.0;
            showHintFor (hovered.getComponent());
        }

        if (! tickerArea.isEmpty())
        {
            const float full = (tickerTitle.isNotEmpty() ? widthOf (text (19.5f, 2), tickerTitle) + 14.0f : 0.0f) + widthOf (text (19.5f, 0), tickerText);
            const float room = (float) tickerArea.getWidth() - 40.0f;
            if (full > room)
            {
                if (tickerPause > 0)
                    --tickerPause;
                else
                {
                    tickerScroll += 1.6f;
                    if (tickerScroll > full - room + 40.0f)
                    {
                        tickerScroll = 0.0f;
                        tickerPause = 60;
                    }
                    ticker->repaint();
                }
            }
        }
    }

    //==========================================================================
    void View::showPage (int module)
    {
        screen.setPage (module);
        for (int m = 0; m < StripModules::numModules; ++m)
            pageButtons[(size_t) m]->setToggleState (m == module, juce::dontSendNotification);

        static const char* screenHints[] { "WHAT IT'S DOING", "WHAT IT'S DOING", "THE CURVE", "WHAT IT'S DOING",
                                           "COMPRESSION CURVE", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING", "WHAT IT'S DOING" };
        for (auto& h : hints)
            if (h.component == &screen)
                h.text = moduleHint (module, screenHints[module]);
    }

    void View::layoutRack()
    {
        // From the page buttons' row down to CLIP, from GATE's left edge to COMP's right
        const juce::Rectangle<int> area (colGate, viewRowY, colRight + wRight - colGate, 842 - viewRowY);
        rack->setBounds (area);

        auto* panel = static_cast<RackPanel*> (rack.get());
        auto inside = rack->getLocalBounds().reduced (10, 0).withTrimmedTop (40).withTrimmedBottom (10);
        constexpr int gap = 10;
        const int bayWidth = (inside.getWidth() - (numBays - 1) * gap) / numBays;
        for (int b = 0; b < numBays; ++b)
        {
            panel->bays[(size_t) b] = inside.removeFromLeft (bayWidth);
            inside.removeFromLeft (gap);
        }

        // The rack effects: in their bays while the rack's open (VERB's old place on the main page is TAPE's)
        slabs[StripModules::slapIndex]->setBounds (panel->bays[(size_t) slapBay] + rack->getPosition());
        slabs[StripModules::delayIndex]->setBounds (panel->bays[(size_t) delayBay] + rack->getPosition());
        slabs[StripModules::tremIndex]->setBounds (panel->bays[(size_t) tremBay] + rack->getPosition());
        slabs[StripModules::wahIndex]->setBounds (panel->bays[(size_t) wahBay] + rack->getPosition());
        slabs[StripModules::flangeIndex]->setBounds (panel->bays[(size_t) flangeBay] + rack->getPosition());
        slabs[StripModules::filterIndex]->setBounds (panel->bays[(size_t) filterBay] + rack->getPosition());
    }

    void View::showRack (bool show)
    {
        rackShown = show;
        rack->setVisible (show);
        for (int m : { 0, 1, 2, 3, 4 })
            slabs[(size_t) m]->setVisible (! show);
        screen.setVisible (! show);
        for (int m : pageOrder)
            pageButtons[(size_t) m]->setVisible (! show);
        for (int m : { (int) StripModules::slapIndex, (int) StripModules::delayIndex, (int) StripModules::tremIndex, (int) StripModules::wahIndex, (int) StripModules::flangeIndex, (int) StripModules::filterIndex })
        {
            slabs[(size_t) m]->setVisible (show);
            if (show)
                slabs[(size_t) m]->toFront (false);
        }
        weaponSelect.setToggleState (show || weaponSelect.getToggleState(), juce::dontSendNotification);   // (the timer sets it properly)
    }

    void View::setClipAdvancedShown (bool shown)
    {
        if (clipAdvanced->isVisible() != shown)
        {
            clipAdvanced->setVisible (shown);
            standby->setVisible (! shown);
        }
    }

    void View::showSetup (bool show)
    {
        if (show && setupPanel == nullptr)
        {
            setupPanel = std::make_unique<SetupPanel> (*this);
            addAndMakeVisible (*setupPanel);
            setupPanel->setCentrePosition (screen.getBounds().getCentreX(), 440);
        }
        else if (! show)
        {
            setupPanel.reset();
        }
        setupButton.setToggleState (show, juce::dontSendNotification);
    }

    bool View::isSetupShowing() const   { return setupPanel != nullptr; }

    //==========================================================================
    // Presets
    //==========================================================================
    void View::loadStripPreset (const Preset& preset)
    {
        strip.loadPreset (preset);
        announce (preset.name.toUpperCase(), preset.tip, 10);
        strip.pushSettingsIfIdle (lastAudioCount, quietFrames = 100);   // (so the panel follows at once)
        presetWindowComponent->repaint();
    }

    void View::stepPreset (int direction)
    {
        auto all = strip.getBuiltInPresets();
        for (auto& mine : stripPresets.readUserPresets())
            all.push_back (mine);
        if (all.empty())
            return;

        int index = -1;
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].name == strip.getPresetName())
                index = i;

        index = (index + direction + (int) all.size()) % (int) all.size();
        loadStripPreset (all[(size_t) index]);
    }

    void View::showOrderMenu()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("THE SOUND GOES: " + EasyStripAudioProcessor::describe (strip.getWantedOrder()));
        for (int i = 0; i < StripIDs::numOrderSwitches; ++i)
            menu.addItem (i + 1, StripIDs::orderNames[i], true, strip.parameters.getRawParameterValue (StripIDs::orderSwitches[i])->load() > 0.5f);
        menu.addSeparator();
        menu.addItem (10, "STANDARD ORDER", ! strip.isStandardOrder());

        juce::Component::SafePointer<View> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&orderButton).withMinimumWidth (260), [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& v = *safe;
            for (int i = 0; i < StripIDs::numOrderSwitches; ++i)
            {
                auto* p = v.strip.parameters.getParameter (StripIDs::orderSwitches[i]);
                const bool now = p->getValue() > 0.5f;
                const bool wanted = result == 10 ? false : (result == i + 1 ? ! now : now);
                if (wanted != now)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (wanted ? 1.0f : 0.0f);
                    p->endChangeGesture();
                    v.settingMoved (*p);
                }
            }
            v.announce ("ORDER", "The sound now goes " + EasyStripAudioProcessor::describe (v.strip.getWantedOrder()) + ".", 6);
        });
    }

    void View::showPresetMenu (int module, juce::Component& under)
    {
        auto& browser = module < 0 ? stripPresets : *modulePresets[(size_t) module];
        auto menu = browser.buildMenu();
        juce::Component::SafePointer<View> safe (this);
        juce::Component::SafePointer<juce::Component> anchor (&under);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&under).withMinimumWidth (under.getWidth()),
                            [safe, anchor, module] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& view = *safe;
            auto& b = module < 0 ? view.stripPresets : *view.modulePresets[(size_t) module];

            if (result == PresetBrowser::saveItem)
            {
                // Name it: the same typing box as for values
                auto* owner = anchor != nullptr ? anchor.getComponent() : static_cast<juce::Component*> (&view);
                view.typeValue (*owner, owner->getLocalBounds().withSizeKeepingCentre (juce::jmax (220, owner->getWidth()), 30), {},
                                [safe, module] (const juce::String& name)
                {
                    if (safe == nullptr || name.trim().isEmpty())
                        return;
                    auto& v = *safe;
                    auto& br = module < 0 ? v.stripPresets : *v.modulePresets[(size_t) module];
                    const auto savedAs = br.saveUserPreset (name.trim());
                    for (auto& saved : br.readUserPresets())
                        if (saved.name == savedAs)
                            br.onPresetChosen (saved);
                    v.announce ("PRESET SAVED", savedAs, 4);
                });
                view.setTicker ("NAME YOUR PRESET", "then press Enter (Esc cancels).", Colours::selected);
                return;
            }

            b.handleMenuResult (result);
            view.presetWindowComponent->repaint();
        });
    }
}

#include "HoloSolo.h"
#include "EasyWindow.h"

namespace Holo
{
    namespace
    {
        constexpr int margin = 16, headerHeight = 44, gap = 10, tickerHeight = 46;

        enum ScreenPlace { noScreen, screenAbove, screenRight, logoRight };

        /** Each module's panel at the size the Easy Strip gives it, and where its screen page goes. */
        struct Shape { int width, height; ScreenPlace screen; };

        Shape shapeFor (int module)
        {
            switch (module)
            {
                case StripModules::gateIndex:  return { 260, 458, screenRight };
                case StripModules::ampIndex:   return { 260, 272, screenRight };
                case StripModules::eqIndex:    return { 560, 494, screenRight };
                case StripModules::dsIndex:    return { 500, 290, screenRight };
                case StripModules::compIndex:  return { 500, 452, screenRight };
                case StripModules::transIndex: return { 250, 320, screenRight };
                case StripModules::slapIndex:  return { 180, 600, screenRight };
                case StripModules::clipIndex:  return { 760, 110, screenRight };
                case StripModules::tapeIndex:  return { 250, 298, screenRight };
                default:                       return { 180, 726, logoRight };   // (the rack effects: no screen page - the name beside them)
            }
        }

        constexpr int screenWidthBeside = 560, screenHeightAbove = 236, clipAdvancedHeight = 100;

        const char* screenHintTitle (int module)
        {
            switch (module)
            {
                case StripModules::eqIndex:   return "THE CURVE";
                case StripModules::compIndex: return "COMPRESSION CURVE";
                default:                      return "WHAT IT'S DOING";
            }
        }

        struct Painter : public juce::Component
        {
            explicit Painter (std::function<void (juce::Graphics&)> f) : draw (std::move (f))   { setInterceptsMouseClicks (false, false); }
            void paint (juce::Graphics& g) override   { g.setOrigin (-getPosition()); draw (g); }
            std::function<void (juce::Graphics&)> draw;
        };

        /** The header's preset window: the preset's name; click for the list. */
        struct PresetWindow : public juce::Component
        {
            PresetWindow (EasyProcessor& p, std::function<void (juce::Component&)> click)
                : processor (p), onClick (std::move (click))   { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

            void paint (juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (juce::Colour (0xb3040c16));
                g.fillRoundedRectangle (r, 4.0f);
                g.setColour (Colours::selected);
                g.drawRoundedRectangle (r, 4.0f, 1.0f);

                auto name = processor.getPresetName();
                if (name.isEmpty()) name = "PRESETS...";
                if (processor.isPresetEdited()) name += " *";
                drawGlowText (g, name.toUpperCase(), r.reduced (10.0f, 0.0f), juce::Justification::centred, text (18.0f, 1), Colours::selected, 0.6f);
            }

            void mouseDown (const juce::MouseEvent&) override   { onClick (*this); }

            EasyProcessor& processor;
            std::function<void (juce::Component&)> onClick;
        };

        const juce::String gettingStarted = "Hover over anything for what it does. Drag a control (or use the mouse wheel); "
                                            "double-click for its default; right-click to type a value.";
    }

    //==========================================================================
    std::map<juce::String, juce::String> collectHints (juce::AudioProcessorEditor& editor)
    {
        std::map<juce::String, juce::String> found;
        if (auto* window = dynamic_cast<EasyWindow*> (&editor))
            for (auto& [title, hintText] : window->getAllHints())
                if (found.count (title) == 0)
                    found[title] = hintText;
        return found;
    }

    //==========================================================================
    SoloView::SoloView (EasyProcessor& p, int m, std::map<juce::String, juce::String> h, SoloSettings& s)
        : processor (p), module (m), settings (s), standaloneHints (std::move (h)), screen (*this)
    {
        setOpaque (true);

        backdrop = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintBackdrop (g); });
        backdrop->setBufferedToImage (true);
        backdrop->setOpaque (true);
        addAndMakeVisible (*backdrop);
        ticker = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintTicker (g); });
        addAndMakeVisible (*ticker);
        glowLayer = std::make_unique<Painter> ([this] (juce::Graphics& g) { paintGlows (g, *this); });
        addAndMakeVisible (*glowLayer);

        // The module's own panel, exactly as on the strip
        slab = makeSlab (*this, module);
        jassert (slab != nullptr);
        tint = slab->power.tint;
        addAndMakeVisible (*slab);

       #if HOLO_WANTS(7)
        if (module == StripModules::clipIndex)
        {
            clipAdvanced = std::make_unique<ClipAdvanced> (*this);
            addChildComponent (*clipAdvanced);
        }
       #endif

        // Its page of the big screen
        const auto shape = shapeFor (module);
        screen.setPage (module);
        screen.setVisible (shape.screen == screenAbove || shape.screen == screenRight);
        addChildComponent (screen);
        addHint (screen, "THE BIG SCREEN", moduleHint (module, screenHintTitle (module)));

        // The header
        presetWindow = std::make_unique<PresetWindow> (processor, [this] (juce::Component& c) { showPresetMenu (module, c); });
        addAndMakeVisible (*presetWindow);
        addHint (*presetWindow, "PRESETS", moduleHint (module, "PRESETS").isNotEmpty() ? moduleHint (module, "PRESETS")
                                                                                       : juce::String ("Click for the list: built-in presets, your own, and SAVE."));
        for (auto* b : { &presetPrevious, &presetNext })
        {
            b->fontSize = 16.0f;
            addAndMakeVisible (*b);
        }
        presetPrevious.onClick = [this] { stepPreset (-1); };
        presetNext.onClick = [this] { stepPreset (1); };
        addHint (presetPrevious, "PREVIOUS PRESET", "The preset before this one in the list.");
        addHint (presetNext, "NEXT PRESET", "The next preset in the list.");

        setupButton.fontSize = 14.5f;
        setupButton.onClick = [this] { showSetupMenu(); };
        addAndMakeVisible (setupButton);
        addHint (setupButton, "SETUP", "The window's size, the look (HOLOGRAM or CLASSIC), the ticker and the brightness. "
                                       "Remembered for every Easy plugin.");

        presets.setPresets (processor.getPresetFolderName(), processor.getBuiltInPresets());
        presets.getCurrentValues = [this] { return processor.getCurrentPresetValues(); };
        presets.onPresetChosen = [this] (const Preset& preset) { processor.loadPreset (preset); };

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

        // The size at 100%
        {
            int contentW = shape.width, contentH = shape.height;
            if (clipAdvanced != nullptr)      contentH += gap + clipAdvancedHeight;
            if (shape.screen == screenAbove)  contentH += screenHeightAbove + gap;
            if (shape.screen == screenRight || shape.screen == logoRight)  contentW += gap + screenWidthBeside;
            twoRowHeader = contentW < 720;
            const int headerH = twoRowHeader ? 2 * headerHeight + 6 : headerHeight;
            native = { contentW + 2 * margin, margin + headerH + gap + contentH + margin + (settings.ticker ? tickerHeight + gap : 0) };
        }

        setTicker ("", gettingStarted, Colours::cyan);
        setSize (native.x, native.y);
        startTimerHz (30);
    }

    SoloView::~SoloView()
    {
        stopTimer();
        clipAdvanced.reset();
        slab.reset();
    }

    //==========================================================================
    bool SoloView::isModuleOn (int) const
    {
        auto* bypass = processor.parameters.getRawParameterValue (CommonIDs::bypass);
        return bypass == nullptr || bypass->load() < 0.5f;
    }

    juce::String SoloView::moduleHint (int, const juce::String& title) const
    {
        const auto found = standaloneHints.find (title);
        return found != standaloneHints.end() ? found->second : juce::String();
    }

    void SoloView::registerControl (int, const juce::String& parameterID, ParamControl& control)
    {
        controls[parameterID] = &control;
    }

    ParamControl* SoloView::findControl (const juce::String& parameterID) const
    {
        const auto found = controls.find (parameterID);
        return found != controls.end() ? found->second : nullptr;
    }

    void SoloView::setClipAdvancedShown (bool shown)
    {
        if (clipAdvanced != nullptr && clipAdvanced->isVisible() != shown)
        {
            clipAdvanced->setVisible (shown);
            backdrop->repaint();
        }
    }

    //==========================================================================
    void SoloView::resized()
    {
        const auto shape = shapeFor (module);
        auto area = getLocalBounds().reduced (margin);

        if (settings.ticker)
        {
            tickerArea = area.removeFromBottom (tickerHeight);
            area.removeFromBottom (gap);
        }
        else
            tickerArea = {};

        // Header: the name on the left; the presets and SETUP on the right (or under it, when the panel is narrow)
        headerArea = area.removeFromTop (twoRowHeader ? 2 * headerHeight + 6 : headerHeight);
        area.removeFromTop (gap);
        {
            auto h = headerArea.reduced (12, 6);
            auto row = twoRowHeader ? h.removeFromBottom (headerHeight - 12) : h;
            if (twoRowHeader) { titleArea = h.withTrimmedBottom (12); }

            setupButton.setBounds (row.removeFromRight (86));
            row.removeFromRight (twoRowHeader ? 8 : 16);
            presetNext.setBounds (row.removeFromRight (30));
            row.removeFromRight (4);
            const int presetWidth = twoRowHeader ? row.getWidth() - 34 : juce::jmin (300, row.getWidth() - 34 - 220);
            presetWindow->setBounds (row.removeFromRight (presetWidth));
            row.removeFromRight (4);
            presetPrevious.setBounds (row.removeFromRight (30));
            if (! twoRowHeader) { row.removeFromRight (16); titleArea = row; }
        }

        // The screen and the panel
        auto content = area;
        if (shape.screen == screenAbove)
        {
            screen.setBounds (content.removeFromTop (screenHeightAbove));
            content.removeFromTop (gap);
        }
        auto column = content.removeFromLeft (shape.width);
        slab->setBounds (column.removeFromTop (shape.height));
        standbyArea = logoArea = {};
        if (clipAdvanced != nullptr)
        {
            standbyArea = column.withTrimmedTop (gap).removeFromTop (clipAdvancedHeight).withWidth (560);
            clipAdvanced->setBounds (standbyArea);
        }
        if (shape.screen == screenRight)
        {
            content.removeFromLeft (gap);
            screen.setBounds (content);
        }
        if (shape.screen == logoRight)
            logoArea = content.withTrimmedLeft (gap);

        backdrop->setBounds (getLocalBounds());
        glowLayer->setBounds (getLocalBounds());
        ticker->setBounds (tickerArea);
        ticker->setVisible (! tickerArea.isEmpty());
    }

    //==========================================================================
    void SoloView::paint (juce::Graphics&) {}

    void SoloView::paintBackdrop (juce::Graphics& g)
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
        }

        // Clip's ADVANCED row, while it's set to SIMPLE: the standby plate
        if (! standbyArea.isEmpty() && (clipAdvanced == nullptr || ! clipAdvanced->isVisible()))
        {
            const auto r = standbyArea.toFloat();
            paintStandby (g, r);
            g.setColour (Colours::dim);
            g.setFont (text (14.0f, 1));
            g.drawText ("SOUND DESIGN: SET CLIP TO ADVANCED", r, juce::Justification::centred, false);
        }

        // The rack effects (no screen page): their name, big, on the standby plate
        if (! logoArea.isEmpty())
        {
            const auto r = logoArea.toFloat();
            paintStandby (g, r);
            auto name = processor.getName().toUpperCase();
            const auto target = r.getWidth() * 0.78f;
            const auto big = display (juce::jmin (64.0f, 34.0f * target / juce::jmax (1.0f, widthOf (display (34.0f), name))));
            const auto small = display (big.getHeight() * 0.45f);
            const float total = small.getHeight() + 12.0f + big.getHeight();
            auto lines = r.withSizeKeepingCentre (r.getWidth(), total);
            drawGlowText (g, "AARDVARK", lines.removeFromTop (small.getHeight()), juce::Justification::centred, small, juce::Colour (0xffe6fbff), 0.9f);
            lines.removeFromTop (12.0f);
            drawGlowText (g, name, lines, juce::Justification::centred, big, tint, 1.0f);
        }

        // The name: AARDVARK small over the plugin's name, in its module's colour
        {
            const auto r = titleArea.toFloat();
            auto name = processor.getName().toUpperCase();
            const float nameHeight = juce::jmin (25.0f, 25.0f * (r.getWidth() - 120.0f) / juce::jmax (1.0f, widthOf (display (25.0f), name)));
            const auto nameFont = display (nameHeight);
            const auto smallFont = display (11.0f);
            const float smallWidth = widthOf (smallFont, "AARDVARK");
            auto line = r;
            drawGlowText (g, "AARDVARK", line.removeFromLeft (smallWidth + 4.0f), juce::Justification::centredLeft, smallFont, juce::Colour (0xff9fe9ff), 0.7f);
            line.removeFromLeft (10.0f);
            drawGlowText (g, name, line, juce::Justification::centredLeft, nameFont, tint, 0.9f);
        }
    }

    void SoloView::paintStandby (juce::Graphics& g, juce::Rectangle<float> r)
    {
        // (the strip's standby panel: a dotted plate with corner marks)
        const auto shape = chamfered (r, 9.0f);
        g.setColour (juce::Colour (0x8c030a12));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);
            g.setColour (Colours::cyan.withAlpha (0.22f));
            for (float y = r.getY() + 8.0f; y < r.getBottom(); y += 12.0f)
                for (float x = r.getX() + 8.0f; x < r.getRight(); x += 12.0f)
                    g.fillEllipse (x - 0.9f, y - 0.9f, 1.8f, 1.8f);
        }
        g.setColour (Colours::cyan.withAlpha (0.2f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        g.setColour (Colours::cyan);
        for (auto corner : { r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight() })
        {
            const float dx = corner.x < r.getCentreX() ? 8.0f : -8.0f, dy = corner.y < r.getCentreY() ? 8.0f : -8.0f;
            const juce::Point<float> c (corner.x + dx, corner.y + dy);
            g.fillRect (juce::Rectangle<float> (c, c.translated (dx * 2.0f, dy > 0 ? 2.0f : -2.0f)));
            g.fillRect (juce::Rectangle<float> (c, c.translated (dx > 0 ? 2.0f : -2.0f, dy * 2.0f)));
        }
    }

    void SoloView::paintTicker (juce::Graphics& g)
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

    void SoloView::paintOverChildren (juce::Graphics& g)
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
    void SoloView::addHint (juce::Component& c, const juce::String& title, const juce::String& hintText)
    {
        c.addMouseListener (this, true);
        hints.push_back ({ &c, title, hintText });
    }

    void SoloView::mouseEnter (const juce::MouseEvent& e)
    {
        hovered = e.eventComponent;
        if (! showingValue && announceUntil <= 0.0)
            showHintFor (hovered.getComponent());
    }

    void SoloView::mouseExit (const juce::MouseEvent&)
    {
        hovered = nullptr;
    }

    void SoloView::showHintFor (juce::Component* c)
    {
        for (auto* walk = c; walk != nullptr && walk != this; walk = walk->getParentComponent())
            for (auto& h : hints)
                if (h.component == walk)
                {
                    const bool inPanel = walk->findParentComponentOfClass<Slab>() != nullptr || dynamic_cast<Slab*> (walk) != nullptr;
                    setTicker (h.title, h.text, inPanel ? tint : Colours::cyan);
                    return;
                }
        setTicker ("", gettingStarted, Colours::cyan);
    }

    void SoloView::setTicker (const juce::String& title, const juce::String& t, juce::Colour colour)
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

    void SoloView::showValue (const juce::String& title, const juce::String& valueWithUnits)
    {
        showingValue = true;
        announceUntil = 0.0;
        setTicker (title.toUpperCase(), valueWithUnits, Colours::selected);
    }

    void SoloView::valueShown()
    {
        showingValue = false;
        showHintFor (hovered.getComponent());
    }

    void SoloView::announce (const juce::String& title, const juce::String& t, int seconds)
    {
        announceUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        setTicker (title, t, Colours::selected);
    }

    void SoloView::typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
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

    void SoloView::settingMoved (juce::RangedAudioParameter& p)
    {
        if (processor.parameters.getParameter (p.paramID) == &p)
            processor.markPresetEdited();
        presetWindow->repaint();
    }

    //==========================================================================
    void SoloView::timerCallback()
    {
        slab->refresh();
        if (clipAdvanced != nullptr && clipAdvanced->isVisible())
            clipAdvanced->refresh();
        if (screen.isVisible())
            screen.refresh();

        {
            const auto shown = processor.getPresetName() + (processor.isPresetEdited() ? "*" : "");
            if (shown != shownPresetText) { shownPresetText = shown; presetWindow->repaint(); }
        }

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
    // Presets and SETUP
    //==========================================================================
    void SoloView::stepPreset (int direction)
    {
        auto all = processor.getBuiltInPresets();
        for (auto& mine : presets.readUserPresets())
            all.push_back (mine);
        if (all.empty())
            return;

        int index = -1;
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].name == processor.getPresetName())
                index = i;

        index = (index + direction + (int) all.size()) % (int) all.size();
        processor.loadPreset (all[(size_t) index]);
        presetWindow->repaint();
    }

    void SoloView::showPresetMenu (int, juce::Component& under)
    {
        auto menu = presets.buildMenu();
        juce::Component::SafePointer<SoloView> safe (this);
        juce::Component::SafePointer<juce::Component> anchor (&under);

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&under).withMinimumWidth (under.getWidth()),
                            [safe, anchor] (int result)
        {
            if (safe == nullptr || result == 0)
                return;

            auto& view = *safe;
            if (result == PresetBrowser::saveItem)
            {
                auto* owner = anchor != nullptr ? anchor.getComponent() : static_cast<juce::Component*> (&view);
                view.typeValue (*owner, owner->getLocalBounds().withSizeKeepingCentre (juce::jmax (220, owner->getWidth()), 30), {},
                                [safe] (const juce::String& name)
                {
                    if (safe == nullptr || name.trim().isEmpty())
                        return;
                    auto& v = *safe;
                    const auto savedAs = v.presets.saveUserPreset (name.trim());
                    for (auto& saved : v.presets.readUserPresets())
                        if (saved.name == savedAs)
                            v.presets.onPresetChosen (saved);
                    v.announce ("PRESET SAVED", savedAs, 4);
                });
                view.setTicker ("NAME YOUR PRESET", "then press Enter (Esc cancels).", Colours::selected);
                return;
            }

            view.presets.handleMenuResult (result);
            view.presetWindow->repaint();
        });
    }

    void SoloView::showSetupMenu()
    {
        juce::PopupMenu sizes, look, tickerMenu, brightness;
        for (int i = 0; i < (int) std::size (SoloSettings::sizes); ++i)
            sizes.addItem (100 + i, juce::String (juce::roundToInt (SoloSettings::sizes[i] * 100.0f)) + "%", true,
                           std::abs (SoloSettings::sizes[i] - settings.scale) < 0.01f);
        look.addItem (200, "HOLOGRAM", true, true);
        look.addItem (201, "CLASSIC", true, false);
        tickerMenu.addItem (300, "ON", true, settings.ticker);
        tickerMenu.addItem (301, "OFF", true, ! settings.ticker);
        brightness.addItem (400, "LOW", true, settings.dim == 0);
        brightness.addItem (401, "MEDIUM", true, settings.dim == 1);
        brightness.addItem (402, "HIGH", true, settings.dim == 2);

        juce::PopupMenu menu;
        menu.addSectionHeader ("SETUP (EVERY EASY PLUGIN)");
        menu.addSubMenu ("SIZE", sizes);
        menu.addSubMenu ("LOOK", look);
        menu.addSubMenu ("TICKER", tickerMenu);
        menu.addSubMenu ("DISPLAY BRIGHTNESS", brightness);

        juce::Component::SafePointer<SoloView> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&setupButton), [safe] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& s = safe->settings;
            if (result >= 100 && result < 200)       s.scale = SoloSettings::sizes[result - 100];
            else if (result == 201)                  s.skin = StripSettings::classic;
            else if (result == 300 || result == 301) s.ticker = result == 300;
            else if (result >= 400 && result <= 402) { s.dim = result - 400; safe->repaint(); }
            s.save();
            if (safe->onSettingsChanged)
                safe->onSettingsChanged();
        });
    }

    //==========================================================================
    // The window
    //==========================================================================
    SoloEditor::SoloEditor (EasyProcessor& p, int m, ClassicFactory factory)
        : AudioProcessorEditor (&p), processor (p), module (m), makeClassic (std::move (factory)), settings (SoloSettings::load())
    {
        setOpaque (true);
        applySettings();
    }

    SoloEditor::~SoloEditor()
    {
        settings.save();
        hologram.reset();
        classic.reset();
    }

    juce::Point<int> SoloEditor::nativeSize() const
    {
        if (hologram != nullptr) return hologram->nativeSize();
        if (classic != nullptr)  return { classic->getWidth(), classic->getHeight() };
        return { 800, 600 };
    }

    void SoloEditor::applySettings()
    {
        const juce::ScopedValueSetter<bool> busy (applying, true);
        juce::Component::SafePointer<SoloEditor> safe (this);
        const auto later = [safe]
        {
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->applySettings(); });
        };

        if (builtSkin != settings.skin || (hologram != nullptr && builtTicker != settings.ticker))
        {
            hologram.reset();
            classic.reset();

            if (settings.skin == StripSettings::classic)
            {
                classic = makeClassic();
                if (auto* window = dynamic_cast<EasyWindow*> (classic.get()))
                    window->offerLookSwitch ([this, later]
                    {
                        settings.skin = StripSettings::hologram;
                        settings.save();
                        later();
                    });
                addAndMakeVisible (*classic);
            }
            else
            {
                std::map<juce::String, juce::String> hints;
                if (auto window = makeClassic())   // (made out of sight, just to read its hints)
                    hints = collectHints (*window);
                hologram = std::make_unique<SoloView> (processor, module, std::move (hints), settings);
                hologram->onSettingsChanged = later;
                addAndMakeVisible (*hologram);
            }

            builtSkin = settings.skin;
            builtTicker = settings.ticker;
        }

        const auto native = nativeSize();
        if (hologram != nullptr)
        {
            setResizable (true, true);
            if (auto* c = getConstrainer())
            {
                c->setFixedAspectRatio ((double) native.x / (double) native.y);
                c->setSizeLimits (native.x / 2, native.y / 2, native.x * 2, native.y * 2);
            }
        }
        else
            setResizable (false, false);

        const float scale = hologram != nullptr ? settings.scale : 1.0f;
        const int w = juce::roundToInt ((float) native.x * scale);
        const int h = juce::roundToInt ((float) native.y * scale);
        if (getWidth() != w || getHeight() != h)
            setSize (w, h);
        else
            resized();
    }

    void SoloEditor::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colour (0xff02050a));
    }

    void SoloEditor::resized()
    {
        if (classic != nullptr)
        {
            classic->setBounds (getLocalBounds());
            return;
        }
        if (hologram == nullptr)
            return;

        const auto native = hologram->nativeSize();
        const float scale = (float) getWidth() / (float) native.x;
        hologram->setBounds (0, 0, native.x, native.y);
        hologram->setTransform (juce::AffineTransform::scale (scale));

        if (! applying)
            settings.scale = juce::jlimit (0.5f, 2.0f, scale);   // (dragged: remembered when the window closes)
    }
}

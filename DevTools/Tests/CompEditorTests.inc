    // (Included inside CompTests.cpp's anonymous namespace.)
    //==========================================================================
    struct EditorParts
    {
        EasyWindow* window = nullptr;
        std::vector<juce::Slider*> sliders;
        GainReductionMeter* meter = nullptr;
        CurveGraph* graph = nullptr;
        std::vector<ChoiceSwitch*> switches;   // in the order they're added: REACTS TO, LISTENS TO, STYLE, LOW END
        ChoiceSwitch* reactsTo = nullptr;
        ChoiceSwitch* worksOn = nullptr;
        ChoiceSwitch* listensTo = nullptr;
        ChoiceSwitch* style = nullptr;
        ChoiceSwitch* lowEnd = nullptr;
    };

    EditorParts findParts (juce::AudioProcessorEditor& editor)
    {
        EditorParts parts;
        parts.window = dynamic_cast<EasyWindow*> (&editor);
        for (auto* child : editor.getChildren())
        {
            if (auto* s = dynamic_cast<juce::Slider*> (child)) parts.sliders.push_back (s);
            if (auto* m = dynamic_cast<GainReductionMeter*> (child)) parts.meter = m;
            if (auto* gr = dynamic_cast<CurveGraph*> (child)) parts.graph = gr;
            if (auto* cs = dynamic_cast<ChoiceSwitch*> (child)) parts.switches.push_back (cs);
        }
        if (parts.switches.size() == 5)
        {
            parts.worksOn   = parts.switches[4];
            parts.reactsTo  = parts.switches[0];
            parts.listensTo = parts.switches[1];
            parts.style     = parts.switches[2];
            parts.lowEnd    = parts.switches[3];
        }
        return parts;
    }

    struct MenuItemInfo { bool found = false; int id = 0; bool enabled = false, ticked = false; juce::String parent; };

    MenuItemInfo findMenuItem (const juce::PopupMenu& menu, const juce::String& text, const juce::String& parent = {})
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
        {
            auto& item = it.getItem();
            if (item.text == text && item.subMenu == nullptr)
                return { true, item.itemID, item.isEnabled, item.isTicked, parent };
            if (item.subMenu != nullptr)
                if (auto found = findMenuItem (*item.subMenu, text, item.text); found.found)
                    return found;
        }
        return {};
    }

    bool hasSubMenu (const juce::PopupMenu& menu, const juce::String& text, bool* ticked = nullptr)
    {
        for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            if (it.getItem().text == text && it.getItem().subMenu != nullptr)
            {
                if (ticked != nullptr) *ticked = it.getItem().isTicked;
                return true;
            }
        return false;
    }

    juce::TextEditor* findNameBox (PresetBrowser& browser)
    {
        for (auto* child : browser.getChildren())
            if (auto* box = dynamic_cast<juce::TextEditor*> (child))
                return box;
        return nullptr;
    }

    //==========================================================================
    void testEditor (const juce::File& outDir)
    {
        std::cout << "\n[plugin window]\n";
        const auto presetFolder = juce::File::createTempFile ("easy-comp-presets");
        presetFolder.deleteRecursively();

        EasyCompAudioProcessor p; prepare (p, 48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditor());
        check (editor != nullptr && editor->getWidth() == 1340 && editor->getHeight() == 600, "window is 1340 x 600");

        auto parts = findParts (*editor);
        check (parts.window != nullptr && parts.sliders.size() == 7 && parts.meter != nullptr && parts.graph != nullptr && parts.reactsTo != nullptr,
               "7 knobs, 5 switches (REACTS TO, LISTENS TO, STYLE, LOW END, WORKS ON), curve graph, meter and the shared header present");
        if (parts.window == nullptr || parts.sliders.size() != 7 || parts.meter == nullptr || parts.graph == nullptr || parts.reactsTo == nullptr)
            return;

        auto& window  = *parts.window;
        auto& sliders = parts.sliders;
        auto& browser = window.presetBrowser;
        browser.setUserPresetFolder (presetFolder);

        check (window.bypassButton.getRight() < window.autoGainButton.getX() && window.autoGainButton.getRight() < browser.getX()
                 && std::abs (window.bypassButton.getBounds().getCentreY() - browser.getBounds().getCentreY()) <= 1,
               "BYPASS and AUTO-GAIN sit in a row just left of the preset box");

        juce::Label* valueBox = nullptr;
        for (auto* c : sliders[0]->getChildren())
            if (auto* l = dynamic_cast<juce::Label*> (c)) valueBox = l;
        check (valueBox != nullptr && valueBox->findColour (juce::Label::outlineColourId).isTransparent()
                 && dynamic_cast<EasyLookAndFeel*> (&valueBox->getLookAndFeel()) != nullptr,
               "value boxes use the custom look (no default outline)");

        check (sliders[0]->getTextFromValue (sliders[0]->getValue()) == "-20.0 dB", "threshold box reads '-20.0 dB'");
        check (sliders[1]->getTextFromValue (sliders[1]->getValue()) == "4.0 : 1", "ratio box reads '4.0 : 1'");
        check (sliders[4]->getTextFromValue (sliders[4]->getValue()) == "0.0 dB" && sliders[4]->isEnabled(), "make-up box reads '0.0 dB' and is usable");
        check (sliders[6]->getTextFromValue (sliders[6]->getValue()) == "Off", "ignore-below box reads 'Off'");
        check (browser.getDisplayedText() == "Presets...", "the preset box starts as 'Presets...'");
        check (parts.reactsTo->getOption (0).getToggleState() && ! parts.reactsTo->getOption (1).getToggleState(), "REACTS TO starts on PEAKS");
        check (parts.listensTo->getOption (0).getButtonText() == "THIS TRACK" && parts.listensTo->getOption (0).getToggleState()
                 && parts.style->getOption (0).getButtonText() == "MODERN" && parts.style->getOption (0).getToggleState()
                 && parts.lowEnd->getOption (1).getButtonText() == "THRUST" && parts.lowEnd->getOption (0).getToggleState(),
               "LISTENS TO starts on THIS TRACK, STYLE on MODERN, LOW END on NORMAL");

        bool allInside = true, noOverlap = true;
        std::vector<juce::Rectangle<int>> boxes;
        for (auto* child : editor->getChildren())
        {
            if (! child->isVisible()) continue;
            if (! editor->getLocalBounds().contains (child->getBounds()) || child->getWidth() <= 0 || child->getHeight() <= 0) allInside = false;
            for (auto& other : boxes)
                if (other.intersects (child->getBounds())) noOverlap = false;
            boxes.push_back (child->getBounds());
        }
        check (allInside, "every control sits fully inside the window");
        check (noOverlap, "no two controls overlap");

        // Knobs and the DAW
        sliders[0]->setValue (-30.0, juce::sendNotificationSync);
        check (near (getParam (p, CompIDs::threshold), -30.0f, 1e-3f), "moving the threshold knob changes the setting");
        setParam (p, CompIDs::ratio, 8.0f);
        check (near ((float) sliders[1]->getValue(), 8.0f, 1e-3f), "automation moves the ratio knob");
        sliders[6]->setValue (120.0, juce::sendNotificationSync);
        check (near (getParam (p, CompIDs::lowCut), 120.0f, 1e-3f), "moving the ignore-below knob changes the setting");
        check (browser.getDisplayedText() == "Presets...", "moving knobs with no preset loaded doesn't say '(edited)'");

        // The preset menu
        {
            const auto menu = browser.buildMenu();
            const auto defaults = findMenuItem (menu, "Default settings");
            const auto kick = findMenuItem (menu, "Kick - Punch");
            const auto none = findMenuItem (menu, "My presets (none saved yet)");
            check (defaults.found && defaults.parent.isEmpty(), "'Default settings' sits at the top of the menu");
            check (kick.found && kick.parent == "Drums" && hasSubMenu (menu, "Drums"), "presets are sorted into sub-menus ('Kick - Punch' is under 'Drums')");
            check (none.found && ! none.enabled, "'My presets (none saved yet)' shows, greyed out, before you save any");
            check (findMenuItem (menu, "Save current settings as a preset...").found && findMenuItem (menu, "Open my presets folder").found,
                   "the menu offers saving and opening your presets folder");

            browser.handleMenuResult (kick.id);
            check (near (getParam (p, CompIDs::attack), 25.0f, 0.01f) && near (getParam (p, CompIDs::lowCut), 20.0f, 0.01f),
                   "choosing 'Kick - Punch' loads it");
            check (browser.getDisplayedText() == "Kick - Punch", "the preset box shows 'Kick - Punch'");

            bool drumsTicked = false;
            const auto menuAgain = browser.buildMenu();
            check (findMenuItem (menuAgain, "Kick - Punch").ticked && hasSubMenu (menuAgain, "Drums", &drumsTicked) && drumsTicked,
                   "the menu ticks the current preset (and its sub-menu)");
        }

        // The help bar: the preset's tip when the mouse is over nothing, knob help when over a knob
        editor->mouseExit (hoverEvent (editor.get()));
        check (window.hintBar.getTitle() == "Kick - Punch" && window.hintBar.getText().contains ("beater"), "with nothing hovered, the help bar shows the preset's tip");
        editor->mouseEnter (hoverEvent (sliders[0]));
        check (window.hintBar.getTitle() == "THRESHOLD", "hovering a knob explains it");
        editor->mouseEnter (hoverEvent (&parts.reactsTo->getOption (1)));
        check (window.hintBar.getTitle() == "REACTS TO", "hovering PEAKS/AVERAGE explains them");
        editor->mouseEnter (hoverEvent (&parts.listensTo->getOption (1)));
        check (window.hintBar.getTitle() == "LISTENS TO" && window.hintBar.getText().contains ("sidechain"), "hovering THIS TRACK/SIDECHAIN explains them");
        editor->mouseEnter (hoverEvent (&parts.style->getOption (1)));
        check (window.hintBar.getTitle() == "STYLE" && window.hintBar.getText().contains ("VINTAGE"), "hovering MODERN/VINTAGE explains them");
        editor->mouseEnter (hoverEvent (&parts.lowEnd->getOption (1)));
        check (window.hintBar.getTitle() == "LOW END" && window.hintBar.getText().contains ("THRUST"), "hovering NORMAL/THRUST explains them");
        editor->mouseEnter (hoverEvent (parts.graph));
        const auto ownGraphHint = window.hintBar.getText();
        setParam (p, CompIDs::listensTo, 1.0f);
        editor->mouseEnter (hoverEvent (parts.graph));
        check (window.hintBar.getText() != ownGraphHint && window.hintBar.getText().contains ("SIDECHAIN"),
               "while listening to the sidechain, the graph's explanation says the dot is the sidechain");
        setParam (p, CompIDs::listensTo, 0.0f);
        editor->mouseEnter (hoverEvent (&window.autoGainButton));
        check (window.hintBar.getTitle() == "AUTO-GAIN", "hovering AUTO-GAIN explains it");

        // "(edited)"
        sliders[2]->setValue (40.0, juce::sendNotificationSync);
        check (browser.getDisplayedText() == "Kick - Punch (edited)", "tweaking a knob shows '(edited)': '" + browser.getDisplayedText() + "'");
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Kick - Punch").id);
        check (browser.getDisplayedText() == "Kick - Punch" && near (getParam (p, CompIDs::attack), 25.0f, 0.01f), "choosing the same preset again restores it");

        // REACTS TO switch
        parts.reactsTo->getOption (1).setToggleState (true, juce::sendNotificationSync);
        check (getParam (p, CompIDs::reactsTo) > 0.5f && ! parts.reactsTo->getOption (0).getToggleState(), "clicking AVERAGE switches it (and PEAKS goes off)");
        check (browser.getDisplayedText() == "Kick - Punch (edited)", "...and counts as an edit");
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Kick - Punch").id);
        check (parts.reactsTo->getOption (0).getToggleState() && getParam (p, CompIDs::reactsTo) < 0.5f, "loading a preset puts the switch back where the preset says");
        setParam (p, CompIDs::reactsTo, 1.0f);
        check (parts.reactsTo->getOption (1).getToggleState(), "the switch follows the DAW too");
        setParam (p, CompIDs::reactsTo, 0.0f);

        // STYLE and LOW END: part of presets, like REACTS TO
        parts.style->getOption (1).setToggleState (true, juce::sendNotificationSync);
        parts.lowEnd->getOption (1).setToggleState (true, juce::sendNotificationSync);
        check (getParam (p, CompIDs::character) > 0.5f && getParam (p, CompIDs::lowEnd) > 0.5f && ! parts.style->getOption (0).getToggleState(),
               "clicking VINTAGE and THRUST switches them on");
        check (browser.getDisplayedText() == "Kick - Punch (edited)", "...and counts as an edit");
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Kick - Punch").id);
        check (parts.style->getOption (0).getToggleState() && parts.lowEnd->getOption (0).getToggleState() && getParam (p, CompIDs::character) < 0.5f,
               "loading a MODERN preset puts them back");
        setParam (p, CompIDs::character, 1.0f);
        check (parts.style->getOption (1).getToggleState(), "STYLE follows the DAW too");
        setParam (p, CompIDs::character, 0.0f);

        // LISTENS TO: not part of presets
        parts.listensTo->getOption (1).setToggleState (true, juce::sendNotificationSync);
        check (getParam (p, CompIDs::listensTo) > 0.5f && browser.getDisplayedText() == "Kick - Punch",
               "clicking SIDECHAIN switches it, without marking the preset as edited (it isn't part of presets)");
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Snare - Crack").id);
        check (parts.listensTo->getOption (1).getToggleState(), "...and loading a preset leaves it alone");
        parts.listensTo->getOption (0).setToggleState (true, juce::sendNotificationSync);
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Kick - Punch").id);

        // Saving your own presets
        {
            sliders[0]->setValue (-27.5, juce::sendNotificationSync);
            sliders[5]->setValue (60.0, juce::sendNotificationSync);
            browser.startNaming();
            auto* nameBox = findNameBox (browser);
            check (nameBox != nullptr && nameBox->isVisible() && nameBox->getText() == "Kick - Punch", "saving opens a name box, suggesting the current name");
            if (nameBox != nullptr)
            {
                nameBox->setText ("My Snare", juce::dontSendNotification);
                nameBox->onReturnKey();
            }
            check (presetFolder.getChildFile ("My Snare.xml").existsAsFile(), "pressing Enter saves 'My Snare.xml' in the presets folder");
            check (browser.getDisplayedText() == "My Snare" && p.getPresetName() == "My Snare", "...and it becomes the current preset");

            const auto menu = browser.buildMenu();
            const auto mine = findMenuItem (menu, "My Snare");
            check (mine.found && mine.parent == "My presets" && ! findMenuItem (menu, "My presets (none saved yet)").found,
                   "it appears under 'My presets'");

            sliders[0]->setValue (-5.0, juce::sendNotificationSync);
            sliders[5]->setValue (100.0, juce::sendNotificationSync);
            browser.handleMenuResult (mine.id);
            check (near (getParam (p, CompIDs::threshold), -27.5f, 0.051f) && near (getParam (p, CompIDs::mix), 60.0f, 0.51f),
                   "choosing it again brings those settings back");

            browser.startNaming();
            if (auto* box = findNameBox (browser)) { box->setText ("Kick - Punch", juce::dontSendNotification); box->onReturnKey(); }
            check (presetFolder.getChildFile ("Kick - Punch (mine).xml").existsAsFile() && p.getPresetName() == "Kick - Punch (mine)",
                   "saving with a built-in preset's name keeps both: 'Kick - Punch (mine)'");

            browser.startNaming();
            if (auto* box = findNameBox (browser)) { box->setText ("Never Saved", juce::dontSendNotification); box->onEscapeKey(); }
            check (! presetFolder.getChildFile ("Never Saved.xml").existsAsFile() && ! browser.isNaming(), "Escape cancels saving");

            const auto saved = browser.readUserPresets();
            check (saved.size() == 2 && saved[0].name == "Kick - Punch (mine)" && saved[1].name == "My Snare", "your presets are listed in name order");

            EasyCompAudioProcessor other;
            std::unique_ptr<juce::AudioProcessorEditor> otherEditor (other.createEditor());
            auto& otherBrowser = dynamic_cast<EasyWindow&> (*otherEditor).presetBrowser;
            otherBrowser.setUserPresetFolder (presetFolder);
            check (findMenuItem (otherBrowser.buildMenu(), "My Snare").found, "your presets show up in every copy of the plugin");
        }

        // The buttons
        window.autoGainButton.setToggleState (true, juce::sendNotificationSync);
        check (getParam (p, CommonIDs::autoGain) > 0.5f, "clicking AUTO-GAIN switches it on");
        window.bypassButton.setToggleState (true, juce::sendNotificationSync);
        check (getParam (p, CommonIDs::bypass) > 0.5f, "clicking BYPASS switches it on");
        setParam (p, CommonIDs::bypass, 0.0f);
        check (! window.bypassButton.getToggleState(), "the DAW switching bypass off turns the button off too");
        browser.handleMenuResult (findMenuItem (browser.buildMenu(), "Room Mics - Crushed").id);
        check (getParam (p, CommonIDs::autoGain) > 0.5f, "loading a preset leaves AUTO-GAIN alone");

        // MAKE-UP while AUTO-GAIN is on (as the window shows it after some drums have played)
        {
            EasyCompAudioProcessor q; loadPresetNumber (q, 5); setParam (q, CommonIDs::autoGain, 1.0f);
            prepare (q, 48000.0, 480);
            run (q, drumLoop (48000.0, 8.0), 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto e = findParts (*ed);
            const auto text = e.sliders[4]->getTextFromValue (e.sliders[4]->getValue());
            check (! e.sliders[4]->isEnabled() && text.startsWith ("Auto +"), "with AUTO-GAIN on, MAKE-UP is faded and shows '" + text + "'");
            ed->mouseEnter (hoverEvent (e.sliders[4]));
            check (e.window->hintBar.getText().startsWith ("AUTO-GAIN is on"), "hovering MAKE-UP then explains that AUTO-GAIN is in charge");
        }

        run (p, sine (48000.0, 1000.0, fromDb (-9.0f), 0.3), 512);
        parts.graph->update (p.getCurrentSettings(), p.takeInputLevelForGraph(), p.getAutoGainDb());
        for (int i = 0; i < 100; ++i) parts.graph->update (p.getCurrentSettings(), p.takeInputLevelForGraph(), p.getAutoGainDb());
        check (true, "graph accepts live updates");

        presetFolder.deleteRecursively();

        // Renders
        auto render = [&] (const char* fileName, const char* presetName, bool autoGain, bool bypass, bool average, bool playDrums,
                           std::function<juce::Component* (EditorParts&)> hover, float meterDb, float dotDb,
                           std::function<void (EasyCompAudioProcessor&)> extra = {})
        {
            EasyCompAudioProcessor q;
            prepare (q, 48000.0, 480);
            for (auto& preset : q.getBuiltInPresets())
                if (preset.name == presetName) q.loadPreset (preset);
            setParam (q, CommonIDs::autoGain, autoGain ? 1.0f : 0.0f);
            setParam (q, CommonIDs::bypass, bypass ? 1.0f : 0.0f);
            if (average) setParam (q, CompIDs::reactsTo, 1.0f);
            if (extra) extra (q);
            if (playDrums)
            {
                run (q, drumLoop (48000.0, 8.0), 480);
                q.takeGainReductionForMeter(); q.takeInputLevelForGraph();
                run (q, drumLoop (48000.0, 0.03), 480);   // the start of a kick hit
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto e = findParts (*ed);
            if (meterDb >= 0.0f) e.meter->setGainReduction (meterDb);
            if (dotDb > Compressor::silenceDb) e.graph->update (q.getCurrentSettings(), dotDb, q.getAutoGainDb());
            if (hover != nullptr)
                if (auto* c = hover (e)) ed->mouseEnter (hoverEvent (c));
            savePng (*ed, outDir.getChildFile (fileName), 1.0f);
        };

        render ("ui_default.png", "", false, false, false, false, nullptr, -1.0f, Compressor::silenceDb);
        render ("ui_midside.png", "Mix Bus - Mid & Side Glue", false, false, false, true, [] (EditorParts& e) { return (juce::Component*) e.worksOn; }, -1.0f, Compressor::silenceDb);
        render ("ui_snare_tip.png", "Snare - Crack", false, false, false, false, nullptr, 5.3f, -9.0f);
        render ("ui_snare_attack.png", "Snare - Crack", false, false, false, false, [] (EditorParts& e) { return (juce::Component*) e.sliders[2]; }, 5.3f, -9.0f);
        render ("ui_autogain.png", "Room Mics - Crushed", true, false, false, true, [] (EditorParts& e) { return (juce::Component*) &e.window->autoGainButton; }, -1.0f, Compressor::silenceDb);
        render ("ui_average.png", "Default settings", false, false, true, true, [] (EditorParts& e) { return (juce::Component*) &e.reactsTo->getOption (1); }, -1.0f, Compressor::silenceDb);
        render ("ui_bypassed.png", "Drum Bus - Parallel Smash", true, true, false, true, [] (EditorParts& e) { return (juce::Component*) &e.window->bypassButton; }, -1.0f, Compressor::silenceDb);
        render ("ui_vintage.png", "Drum Bus - Vintage Glue", false, false, false, true, [] (EditorParts& e) { return (juce::Component*) &e.style->getOption (1); }, -1.0f, Compressor::silenceDb);
        render ("ui_thrust.png", "Drum Bus - Punch (THRUST)", false, false, false, true, [] (EditorParts& e) { return (juce::Component*) &e.lowEnd->getOption (1); }, -1.0f, Compressor::silenceDb);

        // Listening to the sidechain (the kick ducking a bass), and with nothing coming in
        {
            EasyCompAudioProcessor q;
            loadPresetNamed (q, "Bass - Ducked by the Kick");
            setParam (q, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (q, 48000.0, 480);
            const auto bass = sine (48000.0, 55.0, fromDb (-12.0f), 2.03);
            const auto kicks = drumLoop (48000.0, 2.03, -6.0f, -100.0f, -100.0f);
            runWithKey (q, bass, &kicks, 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            auto e = findParts (*ed);
            ed->mouseEnter (hoverEvent (&e.listensTo->getOption (1)));
            savePng (*ed, outDir.getChildFile ("ui_sidechain.png"), 1.0f);
        }
        {
            EasyCompAudioProcessor q;
            loadPresetNamed (q, "Bass - Ducked by the Kick");
            setParam (q, CompIDs::listensTo, 1.0f);
            prepareWithSidechain (q, 48000.0, 480, 2, 0);
            runWithKey (q, sine (48000.0, 55.0, fromDb (-12.0f), 2.0), nullptr, 480);
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            savePng (*ed, outDir.getChildFile ("ui_sidechain_missing.png"), 1.0f);
        }

        // The longest preset name, edited, in the preset box
        {
            EasyCompAudioProcessor q;
            for (auto& preset : q.getBuiltInPresets())
                if (preset.name == "Acoustic Guitar - Fingerpicked") q.loadPreset (preset);
            q.markPresetEdited();
            std::unique_ptr<juce::AudioProcessorEditor> ed (q.createEditor());
            savePng (*ed, outDir.getChildFile ("ui_long_name.png"), 1.0f);

        }
    }

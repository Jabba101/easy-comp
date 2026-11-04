#include "MasterConsole.h"
#include "MasterHints.h"
#include "Parameters.h"

using namespace Holo;
using namespace MasterHints;

namespace MasterHolo
{
    namespace
    {
        const juce::String initHint = "Puts this module back to its default settings. Nothing else in the chain changes.";
        const juce::String prHint = "This module's own presets: they change only this module and leave the rest of the chain as it is. "
                                    "Save your own from the same menu.";
        const juce::String powerHint = "Switches this module on or off (lit = on). The rest of the chain keeps working.";
        const juce::String presetHint = "The whole chain's presets - starting points to fine-tune. Click for the list (and to save your own); "
                                        "< and > step through them. Each module also has its own (its Pr button).";
        const juce::String focusHint = "What the big screen shows. Each module has its own picture: the resonances, the EQ curve, the four "
                                       "bands, the last six seconds of compressor and limiter, PUNCH at work, the stereo picture.";
        const juce::String bayHint = "The big bay holds the EQ or the MULTIBAND - click to switch. Both keep working whichever is showing.";
        const juce::String matchInfoHint = matchHint;

        /** A plain component that paints with a function (the preset window). */
        struct Painter : public juce::Component
        {
            std::function<void (juce::Graphics&)> painter;
            std::function<void()> onClick;
            void paint (juce::Graphics& g) override   { if (painter) painter (g); }
            void mouseDown (const juce::MouseEvent&) override   { if (onClick) onClick(); }
        };

        void placeStack (juce::Label& label, Choice& c, juce::Rectangle<int> column, int maxWidth = 104)
        {
            auto r = column.withSizeKeepingCentre (juce::jmin (column.getWidth() - 4, maxWidth), 17 + 6 + c.getIdealHeight());
            label.setBounds (r.removeFromTop (17));
            r.removeFromTop (6);
            c.setBounds (r);
        }
    }

    const juce::Colour Module::midTint = Colours::gate;
    const juce::Colour Module::sideTint = Colours::cyan;

    //==========================================================================
    Module::Module (Console& c, int pm, juce::Colour t, const juce::String& n, const char* onID)
        : power (n, t, Button::Style::power), init ("INIT", Colours::warn, Button::Style::init), pr ("Pr", t),
          tint (t), console (c), processor (c.getProcessor()), presetModule (pm), name (n)
    {
        setDrawsOwnGlows (*this);
        power.fontSize = 16.0f;
        addAndMakeVisible (power);
        if (onID != nullptr)
        {
            links.push_back (std::make_unique<ButtonLink> (param (onID), power));
            console.addHint (power, n + " ON / OFF", powerHint);
        }
        else
        {
            power.setToggleState (true, juce::dontSendNotification);   // (always working)
            power.setInterceptsMouseClicks (false, false);
        }
        if (presetModule >= 0)
        {
            init.fontSize = 13.0f;
            init.onClick = [this]
            {
                console.applyModulePreset (presetModule, nullptr);
                init.flash = true;
                init.repaint();
                juce::Component::SafePointer<Module> safe (this);
                juce::Timer::callAfterDelay (180, [safe] { if (safe != nullptr) { safe->init.flash = false; safe->init.repaint(); } });
            };
            addAndMakeVisible (init);
            console.addHint (init, "INIT", initHint);
            pr.fontSize = 13.0f;
            pr.onClick = [this] { console.showModulePresetMenu (presetModule, pr); };
            addAndMakeVisible (pr);
            console.addHint (pr, "PRESETS", prHint);
        }
    }

    Module::~Module() = default;

    juce::RangedAudioParameter& Module::param (const char* id)
    {
        auto* p = processor.parameters.getParameter (id);
        jassert (p != nullptr);
        return *p;
    }

    void Module::hint (juce::Component& c, const juce::String& title, const juce::String& text)   { console.addHint (c, title, text); }

    Ring& Module::ring (const char* id, const juce::String& title, Holo::Format f, const juce::String& unit, Ring::Size size,
                        juce::Colour colour, const juce::String& hintTitle, const juce::String& hintText)
    {
        auto r = std::make_unique<Ring> (param (id), title, f, unit, size, colour.isTransparent() ? tint : colour, title.isNotEmpty());
        auto& ref = *r;
        addAndMakeVisible (ref);
        hint (ref, hintTitle.isNotEmpty() ? hintTitle : title.replace ("|", " "), hintText);
        console.registerControl (id, ref);
        parts.push_back (std::move (r));
        return ref;
    }

    Bar& Module::bar (const char* id, const juce::String& title, Holo::Format f, const juce::String& unit, juce::Colour colour, bool centreZero,
                      const juce::String& hintTitle, const juce::String& hintText)
    {
        auto b = std::make_unique<Bar> (param (id), title, f, unit, colour.isTransparent() ? tint : colour, centreZero);
        auto& ref = *b;
        addAndMakeVisible (ref);
        hint (ref, hintTitle.isNotEmpty() ? hintTitle : title, hintText);
        console.registerControl (id, ref);
        parts.push_back (std::move (b));
        return ref;
    }

    Choice& Module::choice (const char* id, const juce::StringArray& names, const juce::String& hintTitle, const juce::String& hintText,
                            bool sideBySide, juce::Colour colour)
    {
        auto c = std::make_unique<Choice> (param (id), names, colour.isTransparent() ? tint : colour, sideBySide);
        auto& ref = *c;
        addAndMakeVisible (ref);
        hint (ref, hintTitle, hintText);
        parts.push_back (std::move (c));
        return ref;
    }

    Button& Module::toggle (const char* id, const juce::String& text, const juce::String& hintTitle, const juce::String& hintText,
                            Button::Style style, juce::Colour colour)
    {
        auto b = std::make_unique<Button> (text, colour.isTransparent() ? tint : colour, style);
        auto& ref = *b;
        links.push_back (std::make_unique<ButtonLink> (param (id), ref));
        addAndMakeVisible (ref);
        hint (ref, hintTitle, hintText);
        parts.push_back (std::move (b));
        return ref;
    }

    juce::Label& Module::caption (const juce::String& text, juce::Colour colour, float size)
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

    juce::Rectangle<int> Module::placeHeader (juce::Rectangle<int> area, juce::Component* mode, int modeWidth, int leftReserved)
    {
        auto row = area.removeFromTop (34);
        if (presetModule >= 0)
        {
            pr.setBounds (row.removeFromRight (28).withSizeKeepingCentre (28, 28));
            row.removeFromRight (6);
            init.setBounds (row.removeFromRight (40).withSizeKeepingCentre (40, 28));
            row.removeFromRight (8);
        }
        row.removeFromLeft (leftReserved);
        if (mode != nullptr)
        {
            mode->setBounds (row.removeFromLeft (modeWidth).withSizeKeepingCentre (modeWidth, 24));
            row.removeFromLeft (8);
        }
        const int w = juce::jmax (76, (int) widthOf (Holo::text (16.0f, 2), name) + 26);
        power.setBounds (row.withSizeKeepingCentre (juce::jmin (w, row.getWidth()), 30));
        area.removeFromTop (8);
        return area;
    }

    juce::Rectangle<int> Module::section (juce::Rectangle<int> r, int number, const juce::String& title)
    {
        sections.push_back ({ r.toFloat(), number, title });
        return r.withTrimmedTop (title.isNotEmpty() ? 27 : 8).reduced (8, 0).withTrimmedBottom (7);
    }

    void Module::centreIn (juce::Component& c, juce::Rectangle<int> column, int top)
    {
        c.setTopLeftPosition (column.getCentreX() - c.getWidth() / 2, top >= 0 ? top : column.getCentreY() - c.getHeight() / 2);
    }

    void Module::showParts (bool midSide, std::initializer_list<ParamControl*> midControls, std::initializer_list<juce::Component*> sideControls)
    {
        for (auto* c : midControls)
        {
            const auto wanted = midSide ? midTint : tint;
            if (c->tint != wanted) { c->tint = wanted; c->repaint(); }
        }
        for (auto* c : sideControls)
        {
            c->setEnabled (midSide);
            c->setAlpha (midSide ? 1.0f : 0.28f);
        }
    }

    void Module::paint (juce::Graphics& g)
    {
        drawModule (g, getLocalBounds().toFloat(), tint);
        for (auto& s : sections)
            drawSection (g, s.area, tint, s.number, s.title);
        paintGlows (g, *this);
    }

    //==========================================================================
    // RESONANCE TAMER
    //==========================================================================
    class TamerModule : public Module
    {
    public:
        explicit TamerModule (Console& c) : Module (c, MasterModules::tamer, MasterSkin::tamer, "TAMER", MasterIDs::tamerOn)
        {
            using namespace MasterIDs;
            mode = &choice (tamerMode, { "STEREO", "MID+SIDE" }, "STEREO / MID + SIDE", tamerModeHint, true);
            mode->segmentHeight = 24; mode->fontSize = 13.0f;
            const char* amounts[2] { tMidAmount, tSideAmount };
            const char* senses[2] { tMidSens, tSideSens };
            const char* sharps[2] { tMidSharp, tSideSharp };
            for (int p = 0; p < 2; ++p)
            {
                const auto colour = p == 0 ? midTint : sideTint;
                const juce::String part = p == 0 ? "MID" : "SIDE";
                partName[p] = &caption (part, colour, 13.0f);
                amount[p] = &bar (amounts[p], "AMOUNT", Holo::Format::db, "dB", colour, false, part + " AMOUNT", tamerAmountHint);
                sens[p] = &ring (senses[p], "SENS", Holo::Format::percent, "%", Ring::tiny, colour, part + " SENSITIVITY", tamerSensHint);
                sharp[p] = &ring (sharps[p], "SHARP", Holo::Format::percent, "%", Ring::tiny, colour, part + " SHARPNESS", tamerSharpHint);
                cut[p] = std::make_unique<GainBar> (12.0f, false, Colours::reduction);
                addAndMakeVisible (*cut[p]);
                hint (*cut[p], part + " TURNING DOWN", "How far the tamer is turning down its deepest spot right now (up to 12 dB).");
            }
            from = &ring (tFrom, "FROM", Holo::Format::hertz, "Hz", Ring::tiny, Colours::frequency, "FROM", tamerFromHint);
            to = &ring (tTo, "TO", Holo::Format::hertz, "Hz", Ring::tiny, Colours::frequency, "TO", tamerToHint);
            speed = &ring (tSpeed, "SPEED", Holo::Format::percent, "%", Ring::tiny, tint, "SPEED", tamerSpeedHint);
            listen = &toggle (tListen, "LISTEN|CUTS", "LISTEN", tamerListenHint, Button::Style::square, Colours::hazard);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), mode, 116);
            auto s1 = section (body.removeFromTop (210), 1, "HOW MUCH");
            body.removeFromTop (8);
            auto s2 = section (body, 2, "WHERE & HOW FAST");

            const int half = s1.getWidth() / 2;
            for (int p = 0; p < 2; ++p)
            {
                auto col = p == 0 ? s1.removeFromLeft (half) : s1;
                partName[p]->setBounds (col.removeFromTop (15));
                col = col.reduced (4, 0);
                amount[p]->setBounds (col.removeFromLeft (50));
                col.removeFromLeft (3);
                cut[p]->setBounds (col.removeFromLeft (10).withTrimmedTop (17).withTrimmedBottom (14));
                auto rings = col.withTrimmedLeft (4);
                centreIn (*sens[p], rings.removeFromTop (rings.getHeight() / 2), rings.getY());
                centreIn (*sharp[p], rings, rings.getY() + 4);
            }
            const int w = s2.getWidth() / 4;
            centreIn (*from, s2.removeFromLeft (w), s2.getY());
            centreIn (*to, s2.removeFromLeft (w), s2.getY());
            centreIn (*speed, s2.removeFromLeft (w), s2.getY());
            listen->setBounds (s2.withSizeKeepingCentre (60, 52).withY (s2.getY() + 6));
        }

        void refresh() override
        {
            const auto r = processor.getReadings();
            cut[0]->setGainDb (r.tamerMidCut);
            cut[1]->setGainDb (r.tamerSideCut);
            showParts (processor.isTamerMidSide(), { amount[0], sens[0], sharp[0] }, { amount[1], sens[1], sharp[1], cut[1].get(), partName[1] });
            partName[0]->setText (processor.isTamerMidSide() ? "MID" : "STEREO", juce::dontSendNotification);
            partName[0]->setColour (juce::Label::textColourId, processor.isTamerMidSide() ? midTint : tint);
        }

    private:
        Choice* mode = nullptr;
        juce::Label* partName[2] {};
        Bar* amount[2] {};
        Ring *sens[2] {}, *sharp[2] {}, *from = nullptr, *to = nullptr, *speed = nullptr;
        std::unique_ptr<GainBar> cut[2];
        Button* listen = nullptr;
    };

    //==========================================================================
    // EQ (in the big bay)
    //==========================================================================
    class EQModule : public Module
    {
    public:
        explicit EQModule (Console& c) : Module (c, MasterModules::eq, MasterSkin::eq, "EQ", MasterEQIDs::on)
        {
            mode = &choice (MasterEQIDs::mode, { "STEREO", "MID+SIDE" }, "EQ - STEREO / MID + SIDE", eqModeHint, true);
            mode->segmentHeight = 24; mode->fontSize = 13.0f;
            for (int p = 0; p < 2; ++p)
            {
                const auto colour = p == 0 ? midTint : sideTint;
                const juce::String part = p == 0 ? "MID" : "SIDE";
                rowName[p] = &caption (part, colour, 15.0f);
                for (int b = 0; b < 5; ++b)
                {
                    const juce::String band = MasterEQIDs::bands[b].name;
                    freq[p][b] = &ring (MasterEQIDs::freq[p][b], "FREQ", Holo::Format::hertz, "Hz", Ring::small, colour, part + " " + band + " FREQ", eqFreqHint);
                    gain[p][b] = &bar (MasterEQIDs::gain[p][b], "GAIN", Holo::Format::signedDb, "dB", colour, true, part + " " + band + " GAIN", eqGainHint);
                }
                lowCut[p] = &ring (MasterEQIDs::lowCut[p], "LOW CUT", Holo::Format::hertz, "Hz", Ring::small, colour, part + " LOW CUT", eqLowCutHint);
            }
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), mode, 150, 230);
            auto names = body.removeFromLeft (54);
            body.removeFromLeft (4);
            const int cleanWidth = 112, gap = 8;
            const int bandWidth = (body.getWidth() - cleanWidth - gap * 5) / 5;
            const char* titles[5] { "LOW SHELF", "LOW MID", "MID", "HIGH MID", "HIGH SHELF" };
            for (int b = 0; b <= 5; ++b)
            {
                auto inside = b < 5 ? section (body.removeFromLeft (bandWidth), b + 1, titles[b]) : section (body, 0, "CLEAN UP");
                if (b < 5) body.removeFromLeft (gap);
                rowSplit = inside.getCentreY();
                for (int p = 0; p < 2; ++p)
                {
                    auto row = p == 0 ? inside.removeFromTop (inside.getHeight() / 2) : inside;
                    row = row.reduced (0, 4);
                    if (b < 5)
                    {
                        auto ringCol = row.removeFromLeft (row.getWidth() - 56);
                        centreIn (*freq[p][b], ringCol);
                        gain[p][b]->setBounds (row.withTrimmedLeft (4));
                    }
                    else
                        centreIn (*lowCut[p], row);
                }
            }
            // (the row names: level with each row)
            const auto top = names.getY() + 27, bottom = names.getBottom() - 7;
            rowName[0]->setBounds (names.withY (top).withBottom (rowSplit));
            rowName[1]->setBounds (names.withY (rowSplit).withBottom (bottom));
            lineFrom = names.getRight() + 4;
            lineTo = getWidth() - 17;
        }

        void paint (juce::Graphics& g) override
        {
            Module::paint (g);
            // (a faint line between the MID and the SIDE rows, right across the bands)
            g.setColour (Colours::cyan.withAlpha (0.18f));
            const float dashes[] { 4.0f, 4.0f };
            g.drawDashedLine (juce::Line<float> ((float) lineFrom, (float) rowSplit, (float) lineTo, (float) rowSplit), dashes, 2, 1.0f);
        }

        void refresh() override
        {
            const bool ms = processor.isEQMidSide();
            showParts (ms, { freq[0][0], freq[0][1], freq[0][2], freq[0][3], freq[0][4], gain[0][0], gain[0][1], gain[0][2], gain[0][3], gain[0][4], lowCut[0] },
                       { freq[1][0], freq[1][1], freq[1][2], freq[1][3], freq[1][4], gain[1][0], gain[1][1], gain[1][2], gain[1][3], gain[1][4], lowCut[1], rowName[1] });
            rowName[0]->setText (ms ? "MID" : "STEREO", juce::dontSendNotification);
            rowName[0]->setColour (juce::Label::textColourId, ms ? midTint : tint);
        }

    private:
        Choice* mode = nullptr;
        juce::Label* rowName[2] {};
        Ring* freq[2][5] {};
        Bar* gain[2][5] {};
        Ring* lowCut[2] {};
        int rowSplit = 0, lineFrom = 0, lineTo = 0;
    };

    //==========================================================================
    // MULTIBAND (in the big bay)
    //==========================================================================
    class MultibandModule : public Module
    {
    public:
        explicit MultibandModule (Console& c) : Module (c, MasterModules::multiband, MasterSkin::multiband, "MULTIBAND", MasterMBIDs::on)
        {
            mode = &choice (MasterMBIDs::mode, { "STEREO", "MID+SIDE" }, "MULTIBAND - STEREO / MID + SIDE", mbModeHint, true);
            mode->segmentHeight = 24; mode->fontSize = 13.0f;
            for (int p = 0; p < 2; ++p)
            {
                const auto colour = p == 0 ? midTint : sideTint;
                const juce::String part = p == 0 ? "MID" : "SIDE";
                rowName[p] = &caption (part, colour, 15.0f);
                for (int b = 0; b < 4; ++b)
                {
                    const juce::String band = juce::String (MasterMBIDs::bandNames[b]);
                    threshold[p][b] = &bar (MasterMBIDs::threshold[p][b], "THRESH", Holo::Format::db, "dB", colour, false, part + " " + band + " THRESHOLD", mbThresholdHint);
                    ratio[p][b] = &ring (MasterMBIDs::ratio[p][b], "RATIO", Holo::Format::ratio, ": 1", Ring::tiny, colour, part + " " + band + " RATIO", mbRatioHint);
                    speed[p][b] = &ring (MasterMBIDs::speed[p][b], "SPEED", Holo::Format::percent, "%", Ring::tiny, colour, part + " " + band + " SPEED", mbSpeedHint);
                    gain[p][b] = &bar (MasterMBIDs::gain[p][b], "GAIN", Holo::Format::signedDb, "dB", colour, true, part + " " + band + " GAIN", mbGainHint);
                    cut[p][b] = std::make_unique<GainBar> (12.0f, false, Colours::reduction);
                    addAndMakeVisible (*cut[p][b]);
                    hint (*cut[p][b], part + " " + band + " TURNING DOWN", "How much this band is being turned down right now (up to 12 dB).");
                }
            }
            for (int b = 0; b < 4; ++b)
            {
                bandOn[b] = &toggle (MasterMBIDs::bandOn[b], "ON", juce::String (MasterMBIDs::bandNames[b]) + " ON", mbBandOnHint, Button::Style::plain, Colours::gate);
                solo[b] = &toggle (MasterMBIDs::solo[b], "SOLO", juce::String (MasterMBIDs::bandNames[b]) + " SOLO", mbSoloHint, Button::Style::plain, Colours::hazard);
                bandOn[b]->fontSize = solo[b]->fontSize = 12.5f;
            }
            for (int sp = 0; sp < 3; ++sp)
                split[sp] = &ring (MasterMBIDs::split[sp], sp == 0 ? "LOW | LOW MID" : sp == 1 ? "LOW MID | HIGH MID" : "HIGH MID | HIGH",
                                   Holo::Format::hertz, "Hz", Ring::small, Colours::frequency, "SPLIT " + juce::String (sp + 1), mbSplitHint);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), mode, 150, 230);
            auto names = body.removeFromLeft (54);
            body.removeFromLeft (4);
            const int splitWidth = 132, gap = 8;
            const int bandWidth = (body.getWidth() - splitWidth - gap * 4) / 4;
            for (int b = 0; b < 4; ++b)
            {
                auto outer = body.removeFromLeft (bandWidth);
                body.removeFromLeft (gap);
                auto inside = section (outer, b + 1, MasterMBIDs::bandNames[b]);
                {
                    auto heading = outer.withHeight (26).reduced (6, 3);
                    solo[b]->setBounds (heading.removeFromRight (46));
                    heading.removeFromRight (4);
                    bandOn[b]->setBounds (heading.removeFromRight (36));
                }
                rowSplit = inside.getCentreY();
                for (int p = 0; p < 2; ++p)
                {
                    auto row = (p == 0 ? inside.removeFromTop (inside.getHeight() / 2) : inside).reduced (0, 4);
                    threshold[p][b]->setBounds (row.removeFromLeft (44));
                    row.removeFromLeft (3);
                    cut[p][b]->setBounds (row.removeFromLeft (10).withTrimmedTop (17).withTrimmedBottom (14));
                    gain[p][b]->setBounds (row.removeFromRight (44));
                    row.removeFromRight (2);
                    centreIn (*ratio[p][b], row.removeFromTop (row.getHeight() / 2), row.getY());
                    centreIn (*speed[p][b], row, row.getY() + 2);
                }
            }
            auto splits = section (body, 5, "SPLITS");
            const int h = splits.getHeight() / 3;
            for (int sp = 0; sp < 3; ++sp)
                centreIn (*split[sp], splits.removeFromTop (h));
            const auto top = names.getY() + 27, bottom = names.getBottom() - 7;
            rowName[0]->setBounds (names.withY (top).withBottom (rowSplit));
            rowName[1]->setBounds (names.withY (rowSplit).withBottom (bottom));
            lineFrom = names.getRight() + 4;
            lineTo = getWidth() - 17 - splitWidth - gap;
        }

        void paint (juce::Graphics& g) override
        {
            Module::paint (g);
            g.setColour (Colours::cyan.withAlpha (0.18f));
            const float dashes[] { 4.0f, 4.0f };
            g.drawDashedLine (juce::Line<float> ((float) lineFrom, (float) rowSplit, (float) lineTo, (float) rowSplit), dashes, 2, 1.0f);
        }

        void setPicture (const EasyMasterAudioProcessor::MultibandPicture& picture)
        {
            const bool ms = processor.isMultibandMidSide();
            for (int b = 0; b < 4; ++b)
            {
                cut[0][b]->setGainDb (picture.cutDb[(size_t) b][0]);
                cut[1][b]->setGainDb (ms ? picture.cutDb[(size_t) b][1] : 0.0f);
            }
        }

        void refresh() override
        {
            const bool ms = processor.isMultibandMidSide();
            for (int b = 0; b < 4; ++b)
                showParts (ms, { threshold[0][b], ratio[0][b], speed[0][b], gain[0][b] }, { threshold[1][b], ratio[1][b], speed[1][b], gain[1][b], cut[1][b].get() });
            rowName[1]->setAlpha (ms ? 1.0f : 0.28f);
            rowName[0]->setText (ms ? "MID" : "STEREO", juce::dontSendNotification);
            rowName[0]->setColour (juce::Label::textColourId, ms ? midTint : tint);
        }

    private:
        Choice* mode = nullptr;
        juce::Label* rowName[2] {};
        Bar *threshold[2][4] {}, *gain[2][4] {};
        Ring *ratio[2][4] {}, *speed[2][4] {}, *split[3] {};
        std::unique_ptr<GainBar> cut[2][4];
        Button *bandOn[4] {}, *solo[4] {};
        int rowSplit = 0, lineFrom = 0, lineTo = 0;
    };

    //==========================================================================
    // COMPRESSOR
    //==========================================================================
    class CompModule : public Module
    {
    public:
        explicit CompModule (Console& c) : Module (c, MasterModules::comp, MasterSkin::comp, "COMP", MasterIDs::compOn)
        {
            using namespace MasterIDs;
            mode = &choice (compMode, { "STEREO", "MID+SIDE" }, "COMP - STEREO / MID + SIDE", modeHint, true);
            mode->segmentHeight = 24; mode->fontSize = 13.0f;
            const char* thresholds[2] { midThreshold, sideThreshold };
            const char* ratios[2] { midRatio, sideRatio };
            const char* attacks[2] { midAttack, sideAttack };
            const char* releases[2] { midRelease, sideRelease };
            const char* makeups[2] { midMakeup, sideMakeup };
            for (int p = 0; p < 2; ++p)
            {
                const auto colour = p == 0 ? midTint : sideTint;
                const juce::String part = p == 0 ? "MID" : "SIDE";
                partName[p] = &caption (part, colour, 13.0f);
                fastName[p] = &caption (part, colour, 13.0f);
                threshold[p] = &bar (thresholds[p], "THRESH", Holo::Format::db, "dB", colour, false, part + " THRESHOLD", thresholdHint);
                ratio[p] = &bar (ratios[p], "RATIO", Holo::Format::ratio, ": 1", colour, false, part + " RATIO", ratioHint);
                attack[p] = &ring (attacks[p], "ATTACK", Holo::Format::msPrecise, "ms", Ring::tiny, colour, part + " ATTACK", attackHint);
                release[p] = &ring (releases[p], "RELEASE", Holo::Format::msPrecise, "ms", Ring::tiny, colour, part + " RELEASE", releaseHint);
                makeup[p] = &ring (makeups[p], "MAKE-UP", Holo::Format::signedDb, "dB", Ring::tiny, colour, part + " MAKE-UP", makeupHint);
                gr[p] = std::make_unique<GainBar> (12.0f, false, Colours::reduction);
                addAndMakeVisible (*gr[p]);
                hint (*gr[p], part + " GAIN REDUCTION", "How much the compressor is turning this part down right now (up to 12 dB).");
                grName[p] = &caption ("GR", Colours::reduction, 11.5f);
            }
            mix = &ring (compMix, "MIX", Holo::Format::percent, "%", Ring::tiny, tint, "MIX", compMixHint);
            below = &ring (ignoreBelow, "IGNORE|BELOW", Holo::Format::hertz, "Hz", Ring::tiny, Colours::frequency, "IGNORE BELOW", ignoreBelowHint);
            reactsLabel = &caption ("PEAKS / AVERAGE", Colours::dim, 12.0f);
            reactsTo = &choice (MasterIDs::reactsTo, { "PEAKS", "AVERAGE" }, "REACTS TO", reactsHint, false);
            styleLabel = &caption ("SOUND", Colours::dim, 12.0f);
            style = &choice (character, { "MODERN", "VINTAGE" }, "STYLE", characterHint, false);
            for (auto* ch : { reactsTo, style }) { ch->segmentHeight = 26; ch->fontSize = 13.0f; }
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), mode, 120);
            auto upper = body.removeFromTop (262);
            body.removeFromTop (8);
            auto s1 = section (upper.removeFromLeft (222), 1, "HOW MUCH");
            upper.removeFromLeft (8);
            auto s2 = section (upper, 2, "HOW FAST");

            // HOW MUCH: per part THRESH and RATIO, and its GR right beside them
            const int half = s1.getWidth() / 2;
            for (int p = 0; p < 2; ++p)
            {
                auto col = (p == 0 ? s1.removeFromLeft (half) : s1).reduced (2, 0);
                partName[p]->setBounds (col.removeFromTop (15));
                threshold[p]->setBounds (col.removeFromLeft (40));
                col.removeFromLeft (3);
                ratio[p]->setBounds (col.removeFromLeft (40));
                col.removeFromLeft (3);
                auto grCol = col.removeFromLeft (16);
                grName[p]->setBounds (grCol.removeFromTop (17));
                gr[p]->setBounds (grCol.withTrimmedBottom (14));
            }
            // HOW FAST: MID and SIDE columns, ATTACK over RELEASE
            {
                auto names = s2.removeFromTop (15);
                const int w = s2.getWidth() / 2;
                fastName[0]->setBounds (names.removeFromLeft (w));
                fastName[1]->setBounds (names);
                for (int p = 0; p < 2; ++p)
                {
                    auto col = p == 0 ? s2.removeFromLeft (w) : s2;
                    centreIn (*attack[p], col.removeFromTop (col.getHeight() / 2), col.getY() + 6);
                    centreIn (*release[p], col, col.getY() + 6);
                }
            }
            // OUTPUT | WHAT IT REACTS TO | STYLE
            auto s3 = section (body.removeFromLeft (166), 3, "OUTPUT");
            body.removeFromLeft (8);
            auto s4 = section (body.removeFromLeft (132), 4, "REACTS TO");
            body.removeFromLeft (8);
            auto s5 = section (body, 5, "STYLE");
            {
                const int w = s3.getWidth() / 3;
                centreIn (*makeup[0], s3.removeFromLeft (w), s3.getY() + 8);
                centreIn (*makeup[1], s3.removeFromLeft (w), s3.getY() + 8);
                centreIn (*mix, s3, s3.getY() + 8);
                centreIn (*below, s4.removeFromLeft (52), s4.getY() + 8);
                placeStack (*reactsLabel, *reactsTo, s4.withTrimmedLeft (4), 70);
                placeStack (*styleLabel, *style, s5, 72);
            }
        }

        void refresh() override
        {
            const auto r = processor.getReadings();
            const bool ms = processor.isMidSide();
            gr[0]->setGainDb (r.midCut);
            gr[1]->setGainDb (ms ? r.sideCut : 0.0f);
            showParts (ms, { threshold[0], ratio[0], attack[0], release[0], makeup[0] },
                       { threshold[1], ratio[1], attack[1], release[1], makeup[1], gr[1].get(), grName[1], partName[1], fastName[1] });
            for (auto* l : { partName[0], fastName[0] })
            {
                l->setText (ms ? "MID" : "STEREO", juce::dontSendNotification);
                l->setColour (juce::Label::textColourId, ms ? midTint : tint);
            }
        }

    private:
        Choice *mode = nullptr, *reactsTo = nullptr, *style = nullptr;
        juce::Label *partName[2] {}, *fastName[2] {}, *grName[2] {}, *reactsLabel = nullptr, *styleLabel = nullptr;
        Bar *threshold[2] {}, *ratio[2] {};
        Ring *attack[2] {}, *release[2] {}, *makeup[2] {}, *mix = nullptr, *below = nullptr;
        std::unique_ptr<GainBar> gr[2];
    };

    //==========================================================================
    // PUNCH
    //==========================================================================
    class PunchModule : public Module
    {
    public:
        explicit PunchModule (Console& c) : Module (c, MasterModules::punch, MasterSkin::punch, "PUNCH", MasterPunchIDs::on)
        {
            mode = &choice (MasterPunchIDs::mode, { "STEREO", "MID+SIDE" }, "PUNCH - STEREO / MID + SIDE", punchModeHint, true);
            mode->segmentHeight = 24; mode->fontSize = 13.0f;
            for (int p = 0; p < 2; ++p)
            {
                const auto colour = p == 0 ? midTint : sideTint;
                const juce::String part = p == 0 ? "MID" : "SIDE";
                partName[p] = &caption (part, colour, 13.0f);
                punch[p] = &bar (MasterPunchIDs::punch[p], "PUNCH", Holo::Format::signedPercent, "%", colour, true, part + " PUNCH", punchHint);
                sustain[p] = &bar (MasterPunchIDs::sustain[p], "SUSTAIN", Holo::Format::signedPercent, "%", colour, true, part + " SUSTAIN", sustainHint);
            }
            change = std::make_unique<GainBar> (9.0f, true, Colours::gate);
            addAndMakeVisible (*change);
            hint (*change, "PUNCH AT WORK", "What PUNCH is doing right now: up = turning the hits (or tails) up, down = softening them.");
            mix = &ring (MasterPunchIDs::mix, "MIX", Holo::Format::percent, "%", Ring::small, tint, "MIX", punchMixHint);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6), mode, 120);
            auto s1 = section (body.removeFromLeft (262), 1, "SHAPE");
            body.removeFromLeft (8);
            auto s2 = section (body, 2, "OUTPUT");
            change->setBounds (s1.removeFromRight (12).withTrimmedTop (32).withTrimmedBottom (14));
            s1.removeFromRight (6);
            const int half = s1.getWidth() / 2;
            for (int p = 0; p < 2; ++p)
            {
                auto col = (p == 0 ? s1.removeFromLeft (half) : s1).reduced (2, 0);
                partName[p]->setBounds (col.removeFromTop (15));
                punch[p]->setBounds (col.removeFromLeft (col.getWidth() / 2).reduced (2, 0));
                sustain[p]->setBounds (col.reduced (2, 0));
            }
            centreIn (*mix, s2);
        }

        void refresh() override
        {
            const bool ms = processor.isPunchMidSide();
            showParts (ms, { punch[0], sustain[0] }, { punch[1], sustain[1], partName[1] });
            partName[0]->setText (ms ? "MID" : "STEREO", juce::dontSendNotification);
            partName[0]->setColour (juce::Label::textColourId, ms ? midTint : tint);
        }

        void setChange (float upDb, float downDb)   { change->setGainDb (upDb >= downDb ? upDb : -downDb); }

    private:
        Choice* mode = nullptr;
        juce::Label* partName[2] {};
        Bar *punch[2] {}, *sustain[2] {};
        Ring* mix = nullptr;
        std::unique_ptr<GainBar> change;
    };

    //==========================================================================
    // WIDTH
    //==========================================================================
    class WidthModule : public Module
    {
    public:
        explicit WidthModule (Console& c) : Module (c, MasterModules::width, MasterSkin::width, "WIDTH", MasterWidthIDs::on)
        {
            width = &ring (MasterWidthIDs::width, "WIDTH", Holo::Format::percent, "%", Ring::medium, Module::sideTint, "WIDTH", widthHint);
            monoBass = &ring (MasterWidthIDs::monoBass, "MONO BASS", Holo::Format::hertz, "Hz", Ring::medium, Colours::frequency, "MONO BASS", monoBassHint);
            sidesReading = &caption ("", Colours::dim, 13.0f);
            monoNote = &caption ("below it:|bass + kick|in the centre", Colours::dim, 13.0f);
            monoNote->setText ("below it: the bass\nand kick in the centre", juce::dontSendNotification);
            sidesReading->setJustificationType (juce::Justification::centredLeft);
            monoNote->setJustificationType (juce::Justification::centredLeft);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6));
            auto s1 = section (body.removeFromLeft (body.getWidth() / 2 - 4), 1, "HOW WIDE");
            body.removeFromLeft (8);
            auto s2 = section (body, 2, "MONO BASS");
            centreIn (*width, s1.removeFromLeft (84));
            sidesReading->setBounds (s1.reduced (2, 8));
            centreIn (*monoBass, s2.removeFromLeft (84));
            monoNote->setBounds (s2.reduced (2, 8));
        }

        void refresh() override
        {
            const auto r = processor.getReadings();
            sidesReading->setText (r.sideToMidDb <= -99.0f ? juce::String ("sides: --")
                                                           : "sides " + juce::String (r.sideToMidDb, 1) + " dB\nnext to the middle\ncorrelation " + juce::String (r.correlation, 2),
                                   juce::dontSendNotification);
        }

    private:
        Ring *width = nullptr, *monoBass = nullptr;
        juce::Label *sidesReading = nullptr, *monoNote = nullptr;
    };

    //==========================================================================
    // LIMITER
    //==========================================================================
    class LimiterModule : public Module
    {
    public:
        explicit LimiterModule (Console& c) : Module (c, MasterModules::limiter, MasterSkin::limiter, "LIMITER", nullptr)
        {
            using namespace MasterIDs;
            gain = &bar (limitGain, "GAIN", Holo::Format::signedDb, "dB", Colours::level, false, "GAIN", limitGainHint);
            ceilingBar = &bar (ceiling, "CEILING", Holo::Format::db, "dBTP", Colours::gold, false, "CEILING", ceilingHint);
            gr = std::make_unique<GainBar> (12.0f, false, Colours::reduction);
            addAndMakeVisible (*gr);
            hint (*gr, "LIMITER - TURNING DOWN", "How much the limiter is turning the peaks down right now. A few dB on the loudest hits is normal.");
            grName = &caption ("GR", Colours::reduction, 11.5f);
            releaseLabel = &caption ("MODE", Colours::dim, 13.0f);
            releaseMode = &choice (MasterIDs::releaseMode, { "AUTO", "SET" }, "RELEASE", limitReleaseHint, false);
            releaseMode->segmentHeight = 26; releaseMode->fontSize = 13.0f;
            releaseTime = &ring (release, "TIME", Holo::Format::ms, "ms", Ring::tiny, tint, "RELEASE TIME", limitReleaseTimeHint);
        }

        void resized() override
        {
            clearSections();
            auto body = placeHeader (getLocalBounds().reduced (9, 6));
            auto s1 = section (body.removeFromLeft (154), 1, "HOW LOUD");
            body.removeFromLeft (8);
            auto s2 = section (body, 2, "RELEASE");
            gain->setBounds (s1.removeFromLeft (54));
            s1.removeFromLeft (4);
            auto grCol = s1.removeFromLeft (16);
            grName->setBounds (grCol.removeFromTop (17));
            gr->setBounds (grCol.withTrimmedBottom (14));
            s1.removeFromLeft (4);
            ceilingBar->setBounds (s1.removeFromLeft (54));
            placeStack (*releaseLabel, *releaseMode, s2.removeFromTop (88), 80);
            centreIn (*releaseTime, s2, s2.getY() + 4);
        }

        void refresh() override
        {
            gr->setGainDb (processor.getReadings().limitCut);
            const bool set = juce::roundToInt (processor.parameters.getRawParameterValue (MasterIDs::releaseMode)->load()) == 1;
            releaseTime->setEnabled (set);
            releaseTime->setAlpha (set ? 1.0f : 0.35f);
        }

    private:
        Bar *gain = nullptr, *ceilingBar = nullptr;
        std::unique_ptr<GainBar> gr;
        juce::Label *grName = nullptr, *releaseLabel = nullptr;
        Choice* releaseMode = nullptr;
        Ring* releaseTime = nullptr;
    };

    //==========================================================================
    // COMPARE
    //==========================================================================
    class CompareModule : public Module
    {
    public:
        explicit CompareModule (Console& c) : Module (c, -1, Colours::gold, "COMPARE", nullptr)
        {
            power.setVisible (false);
            match = &toggle (MasterIDs::match, "MATCH", "MATCH", matchHint, Button::Style::square, Colours::gold);
            match->fontSize = 17.0f;
            info = &caption ("", Colours::ink, 15.0f);
            info->setJustificationType (juce::Justification::centredLeft);
            listenLabel = &caption ("LISTEN (for checking)", Colours::dim, 13.0f);
            listen = &choice (MasterIDs::listen, { "STEREO", "MID", "SIDE", "MONO" }, "LISTEN", listenHint, true, Colours::selected);
            listen->segmentHeight = 28; listen->fontSize = 14.0f;
        }

        void resized() override
        {
            clearSections();
            auto area = getLocalBounds().reduced (9, 6);
            area.removeFromTop (30);
            auto s1 = section (area.removeFromTop (110), 1, "SAME LOUDNESS");
            area.removeFromTop (8);
            auto s2 = section (area, 2, "LISTEN");
            match->setBounds (s1.removeFromLeft (80).withSizeKeepingCentre (76, 54));
            s1.removeFromLeft (8);
            info->setBounds (s1);
            listen->setBounds (s2.withSizeKeepingCentre (s2.getWidth(), 28).withY (s2.getY() + 8));
            listenLabel->setBounds (s2.withTrimmedTop (44).withHeight (16));
        }

        void paint (juce::Graphics& g) override
        {
            Module::paint (g);
            drawGlowText (g, "COMPARE", getLocalBounds().reduced (12, 8).removeFromTop (24).toFloat(), juce::Justification::centredLeft,
                          Holo::text (17.0f, 2), tint, 0.6f);
        }

        void refresh() override
        {
            const bool matching = match->getToggleState();
            const float db = processor.getReadings().matchDb;
            const auto text = matching ? "Matched: " + juce::String (db >= 0.0f ? "+" : "") + juce::String (db, 1) + " dB.\nNow click BYPASS on and off."
                                       : juce::String ("The chain as loud as the original, so BYPASS compares the sound, not the volume.");
            if (info->getText() != text) info->setText (text, juce::dontSendNotification);
        }

    private:
        Button* match = nullptr;
        juce::Label *info = nullptr, *listenLabel = nullptr;
        Choice* listen = nullptr;
    };

    //==========================================================================
    // THE CONSOLE
    //==========================================================================
    Console::Console (EasyMasterAudioProcessor& p)
        : processor (p), eqDisplay (p.parameters), mbDisplay (p.parameters), incoming ((size_t) MasterDisplay::numMoments)
    {
        setOpaque (true);

        // Modules (made before anything finds them)
        modules.push_back (std::make_unique<TamerModule> (*this));
        modules.push_back (std::make_unique<EQModule> (*this));
        modules.push_back (std::make_unique<MultibandModule> (*this));
        modules.push_back (std::make_unique<CompModule> (*this));
        modules.push_back (std::make_unique<PunchModule> (*this));
        modules.push_back (std::make_unique<WidthModule> (*this));
        modules.push_back (std::make_unique<LimiterModule> (*this));
        modules.push_back (std::make_unique<CompareModule> (*this));
        for (auto& m : modules) addAndMakeVisible (*m);

        // The big screen's pictures
        for (auto* c : { (juce::Component*) &compDisplay, (juce::Component*) &tamerDisplay, (juce::Component*) &eqDisplay,
                         (juce::Component*) &mbDisplay, (juce::Component*) &punchDisplay, (juce::Component*) &widthDisplay })
            addChildComponent (*c);
        addHint (compDisplay, "WHAT THE COMPRESSOR AND LIMITER ARE DOING", displayHint);
        addHint (tamerDisplay, "WHAT THE TAMER IS DOING", tamerDisplayHint);
        addHint (eqDisplay, "THE EQ'S CURVE", eqDisplayHint);
        addHint (mbDisplay, "THE MULTIBAND", "The four bands across the spectrum: drag the lines between them to move the splits. From the top: how "
                 "much each band is being turned down (red = the middle or both, blue = the sides).");
        addHint (punchDisplay, "WHAT PUNCH IS DOING", "The last six seconds: the master's level on top; underneath, how much PUNCH is turning up "
                 "(green: the attacks, or longer tails) and down (blue: softer attacks, shorter tails).");
        addHint (widthDisplay, "THE STEREO PICTURE", "The master's stereo picture: the middle up and down, the sides across. Tall and thin = "
                 "nearly mono; a wide, round cloud = very wide; flat and sideways = trouble in mono.");
        eqDisplay.onEdit = [this] { markEdited(); };
        mbDisplay.onEdit = [this] { markEdited(); };
        addAndMakeVisible (meters);
        addHint (meters, "METERS", metersHint);
        meters.onReset = [this] { processor.resetMeters(); };

        // DISPLAY FOCUS and the bay's tabs
        const char* focusNames[numFocus] { "TAMER", "EQ", "MULTIBAND", "COMP", "PUNCH", "WIDTH" };
        for (int f = 0; f < numFocus; ++f)
        {
            focusButtons[(size_t) f] = std::make_unique<Button> (focusNames[f], Colours::selected, Button::Style::view);
            focusButtons[(size_t) f]->fontSize = 14.5f;
            focusButtons[(size_t) f]->onClick = [this, f] { showFocus (f); };
            addAndMakeVisible (*focusButtons[(size_t) f]);
            addHint (*focusButtons[(size_t) f], "DISPLAY FOCUS", focusHint);
        }
        for (int b = 0; b < 2; ++b)
        {
            bayTabs[(size_t) b] = std::make_unique<Button> (b == 0 ? "EQ" : "MULTIBAND", Colours::selected, Button::Style::view);
            bayTabs[(size_t) b]->fontSize = 14.5f;
            bayTabs[(size_t) b]->onClick = [this, b] { showBay (b); showFocus (b == 0 ? eqFocus : mbFocus); };
            addAndMakeVisible (*bayTabs[(size_t) b]);
            addHint (*bayTabs[(size_t) b], "THE BIG BAY", bayHint);
        }

        // INPUT
        trim = std::make_unique<Ring> (*p.parameters.getParameter (MasterIDs::trim), "TRIM", Holo::Format::signedDb, "dB", Ring::tiny, Colours::cyan);
        addAndMakeVisible (*trim);
        addHint (*trim, "TRIM", trimHint);
        registerControl (MasterIDs::trim, *trim);
        addAndMakeVisible (inputMeter);
        addHint (inputMeter, "INPUT", "The mix coming in (after TRIM). Peaks near 0 are fine - the chain has plenty of room.");
        inputState.setFont (Holo::text (13.0f, 1));
        inputState.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (inputState);
        addHint (inputState, "INPUT", trimHint);

        // Header
        auto window = std::make_unique<Painter>();
        window->painter = [this, w = window.get()] (juce::Graphics& g)
        {
            const auto r = w->getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (juce::Colour (0xb3040c16));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (Colours::selected);
            g.drawRoundedRectangle (r, 4.0f, 1.0f);
            drawGlowText (g, getPresetText(), r.reduced (10.0f, 0.0f), juce::Justification::centred, Holo::text (18.0f, 1), Colours::selected, 0.6f);
        };
        window->onClick = [this, w = window.get()] { showMainPresetMenu (*w); };
        window->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        presetWindow = std::move (window);
        addAndMakeVisible (*presetWindow);
        addHint (*presetWindow, "PRESETS", presetHint);
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
            b.onClick = [this, slot] { processor.switchToSlot (slot); };
            addAndMakeVisible (b);
            addHint (b, "A / B", abHint);
        }
        copySlot.fontSize = 13.5f;
        copySlot.onClick = [this] { processor.copyToOtherSlot(); };
        addAndMakeVisible (copySlot);
        addHint (copySlot, "COPY", abHint);
        initAll.fontSize = 14.0f;
        initAll.onClick = [this] { loadMainPreset (processor.getBuiltInPresets().front()); };
        addAndMakeVisible (initAll);
        addHint (initAll, "INIT", initAllHint);
        bypass.fontSize = 14.5f;
        bypassLink = std::make_unique<ButtonLink> (*p.parameters.getParameter (CommonIDs::bypass), bypass);
        addAndMakeVisible (bypass);
        addHint (bypass, "BYPASS", "Switches the whole chain off so you hear the original mix - on time, at the same delay. "
                 "Switch MATCH on first, so you compare the sound, not the loudness.");

        // Presets: the whole chain's, and each module's own
        mainPresets.setPresets (processor.getPresetFolderName(), processor.getBuiltInPresets());
        mainPresets.onPresetChosen = [this] (const Preset& preset) { loadMainPreset (preset); };
        mainPresets.getCurrentValues = [this] { return processor.getCurrentPresetValues(); };
        for (int m = 0; m < MasterModules::count; ++m)
        {
            auto browser = std::make_unique<PresetBrowser>();
            browser->setPresets (juce::String ("Easy Master - ") + MasterModules::names[m], MasterModules::presets (m));
            browser->getCurrentValues = [this, m]
            {
                std::vector<std::pair<juce::String, float>> values;
                for (auto& id : MasterModules::ids (m))
                    values.push_back ({ id, processor.parameters.getRawParameterValue (id)->load() });
                return values;
            };
            browser->onPresetChosen = [this, m] (const Preset& preset) { applyModulePreset (m, &preset); };
            moduleBrowsers[(size_t) m] = std::move (browser);
        }

        // The box for typing a value (or a preset's name)
        entry.setJustification (juce::Justification::centred);
        entry.setFont (Holo::text (17.0f, 1));
        entry.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff04101e));
        entry.setColour (juce::TextEditor::textColourId, juce::Colours::white);
        entry.setColour (juce::TextEditor::outlineColourId, Colours::selected);
        entry.setColour (juce::TextEditor::focusedOutlineColourId, Colours::selected);
        entry.setColour (juce::TextEditor::highlightColourId, Colours::selected.withAlpha (0.4f));
        entry.onReturnKey = [this] { const auto t = entry.getText(); entry.setVisible (false); if (onEntry) onEntry (t); onEntry = nullptr; };
        entry.onEscapeKey = [this] { entry.setVisible (false); onEntry = nullptr; valueShown(); };
        entry.onFocusLost = [this] { if (entry.isVisible()) entry.onReturnKey(); };
        addChildComponent (entry);

        focus = juce::jlimit (0, numFocus - 1, processor.shownPage);
        bay = juce::jlimit (0, 1, processor.shownBay);
        setSize (width, height);
        showBay (bay);
        showFocus (focus);
        showHintFor (nullptr);
        timerCallback();
        startTimerHz (30);
    }

    Console::~Console()
    {
        stopTimer();
    }

    juce::Component* Console::getScreenComponent (int f) const
    {
        switch (f)
        {
            case tamerFocus: return const_cast<TamerDisplay*> (&tamerDisplay);
            case eqFocus:    return const_cast<EQDisplay*> (&eqDisplay);
            case mbFocus:    return const_cast<MultibandDisplay*> (&mbDisplay);
            case punchFocus: return const_cast<PunchDisplay*> (&punchDisplay);
            case widthFocus: return const_cast<WidthDisplay*> (&widthDisplay);
            default:         return const_cast<MasterDisplay*> (&compDisplay);
        }
    }

    Module* Console::getModule (int masterModule) const
    {
        for (auto& m : modules)
            if (m->getPresetModule() == masterModule)
                return m.get();
        return nullptr;
    }

    //==========================================================================
    void Console::resized()
    {
        // (the layout is fixed at 1800 x 1030; the window scales it)
        headerArea = { 12, 10, 1776, 46 };
        focusRow = { 438, 64, 634, 32 };
        inputColumn = { 12, 104, 80, 860 };
        screenArea = { 438, 104, 634, 396 };
        metersArea = { 1488, 104, 300, 300 };
        tickerArea = { 12, 972, 1776, 46 };

        auto& tamer = *getModule (MasterModules::tamer);
        tamer.setBounds (100, 104, 330, 396);
        getModule (MasterModules::eq)->setBounds (100, 508, 972, 456);
        getModule (MasterModules::multiband)->setBounds (100, 508, 972, 456);
        getModule (MasterModules::comp)->setBounds (1080, 104, 400, 480);
        getModule (MasterModules::punch)->setBounds (1080, 592, 400, 192);
        getModule (MasterModules::width)->setBounds (1080, 792, 400, 172);
        getModule (MasterModules::limiter)->setBounds (1488, 412, 300, 280);
        modules.back()->setBounds (1488, 700, 300, 264);   // COMPARE

        // The bay's tabs, in the top left of the bay
        bayTabs[0]->setBounds (112, 514, 90, 28);
        bayTabs[1]->setBounds (208, 514, 118, 28);

        for (int f = 0; f < numFocus; ++f)
            focusButtons[(size_t) f]->setBounds (focusRow.getX() + f * 107, focusRow.getY(), 100, focusRow.getHeight());
        for (int f = 0; f < numFocus; ++f)
            getScreenComponent (f)->setBounds (screenArea.reduced (10, 10).withTrimmedTop (2));
        meters.setBounds (metersArea.reduced (12, 8).withTrimmedTop (28));

        // INPUT
        inputMeter.setBounds (inputColumn.getX() + 14, inputColumn.getY() + 44, inputColumn.getWidth() - 28, 640);
        trim->setTopLeftPosition (inputColumn.getCentreX() - trim->getWidth() / 2, inputColumn.getBottom() - trim->getHeight() - 52);
        inputState.setBounds (inputColumn.getX() + 4, inputColumn.getBottom() - 46, inputColumn.getWidth() - 8, 36);

        // Header
        auto h = headerArea.reduced (8, 7);
        h.removeFromLeft (300);
        presetPrevious.setBounds (h.removeFromLeft (34));
        h.removeFromLeft (6);
        presetWindow->setBounds (h.removeFromLeft (500));
        h.removeFromLeft (6);
        presetNext.setBounds (h.removeFromLeft (34));
        h.removeFromLeft (24);
        slotA.setBounds (h.removeFromLeft (36));
        h.removeFromLeft (4);
        slotB.setBounds (h.removeFromLeft (36));
        h.removeFromLeft (4);
        copySlot.setBounds (h.removeFromLeft (56));
        bypass.setBounds (h.removeFromRight (110));
        h.removeFromRight (10);
        initAll.setBounds (h.removeFromRight (70));
    }

    void Console::paint (juce::Graphics& g)
    {
        const auto all = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d2338), all.getCentreX(), all.getHeight() * 0.08f,
                                                 juce::Colour (0xff02050a), all.getCentreX(), all.getHeight() * 1.1f, true));
        g.fillAll();
        const auto panel = all.reduced (5.0f);
        const auto shape = chamfered (panel, 16.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xeb0a1a2a), 0.0f, panel.getY(), juce::Colour (0xf5050d17), 0.0f, panel.getBottom(), false));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (shape);
            g.setColour (Colours::cyan.withAlpha (0.025f));
            for (float x = panel.getX(); x < panel.getRight(); x += 24.0f) g.fillRect (x, panel.getY(), 1.0f, panel.getHeight());
            for (float y = panel.getY(); y < panel.getBottom(); y += 24.0f) g.fillRect (panel.getX(), y, panel.getWidth(), 1.0f);
        }
        g.setColour (Colours::cyan.withAlpha (0.10f));
        g.strokePath (shape, juce::PathStrokeType (8.0f));
        g.setColour (Colours::cyan.withAlpha (0.6f));
        g.strokePath (shape, juce::PathStrokeType (1.2f));

        // Header strip, and the name
        {
            const auto hr = headerArea.toFloat();
            g.setColour (juce::Colour (0xb3061220));
            g.fillPath (chamfered (hr, 8.0f));
            g.setColour (Colours::cyan.withAlpha (0.4f));
            g.strokePath (chamfered (hr.reduced (0.5f), 8.0f), juce::PathStrokeType (1.0f));
            drawGlowText (g, "EASY MASTER", hr.withTrimmedLeft (18.0f).withWidth (280.0f), juce::Justification::centredLeft, display (27.0f), Colours::cyan, 0.9f);
        }
        drawGlowText (g, "DISPLAY FOCUS:", juce::Rectangle<float> ((float) focusRow.getX() - 190.0f, (float) focusRow.getY(), 180.0f, (float) focusRow.getHeight()),
                      juce::Justification::centredRight, Holo::text (16.0f, 2), Colours::cyan, 0.5f);

        // INPUT, the screen, the meters
        drawModule (g, inputColumn.toFloat(), Colours::cyan);
        drawGlowText (g, "INPUT", inputColumn.toFloat().removeFromTop (34.0f).withTrimmedTop (8.0f), juce::Justification::centred, Holo::text (15.5f, 2), Colours::cyan);
        drawModule (g, screenArea.toFloat(), Colours::cyan);
        drawModule (g, metersArea.toFloat(), Colours::cyan);
        drawGlowText (g, "OUTPUT", metersArea.toFloat().reduced (12.0f, 8.0f).removeFromTop (22.0f), juce::Justification::centredLeft, Holo::text (16.0f, 2), Colours::cyan, 0.6f);

        // The ticker
        {
            const auto r = tickerArea.toFloat();
            g.setColour (juce::Colour (0xbf040c16));
            g.fillPath (chamfered (r, 7.0f));
            g.setColour (Colours::cyan.withAlpha (0.45f));
            g.strokePath (chamfered (r.reduced (0.5f), 7.0f), juce::PathStrokeType (1.0f));
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (tickerArea.reduced (14, 2));
            float x = r.getX() + 18.0f;
            if (tickerTitle.isNotEmpty())
            {
                const auto f = Holo::text (19.5f, 2);
                drawGlowText (g, tickerTitle, { x, r.getY(), 2000.0f, r.getHeight() }, juce::Justification::centredLeft, f, tickerColour, 0.6f);
                x += widthOf (f, tickerTitle) + 14.0f;
            }
            const auto body = juce::Rectangle<float> (x, r.getY(), r.getRight() - x - 16.0f, r.getHeight());
            auto f = Holo::text (19.5f, 0);
            if (widthOf (f, tickerText) > body.getWidth()) f = Holo::text (16.0f, 0);
            g.setColour (juce::Colour (0xff9fe9ff));
            g.setFont (f);
            g.drawFittedText (tickerText, body.toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
        }

        paintGlows (g, *this);
    }

    //==========================================================================
    void Console::showFocus (int f)
    {
        focus = juce::jlimit (0, numFocus - 1, f);
        processor.shownPage = focus;
        for (int i = 0; i < numFocus; ++i)
        {
            focusButtons[(size_t) i]->setToggleState (i == focus, juce::dontSendNotification);
            getScreenComponent (i)->setVisible (i == focus);
        }
        if (focus == eqFocus && bay != 0) showBay (0);
        if (focus == mbFocus && bay != 1) showBay (1);
        timerCallback();
        repaint();
    }

    void Console::showBay (int b)
    {
        bay = juce::jlimit (0, 1, b);
        processor.shownBay = bay;
        getModule (MasterModules::eq)->setVisible (bay == 0);
        getModule (MasterModules::multiband)->setVisible (bay == 1);
        for (int i = 0; i < 2; ++i)
        {
            bayTabs[(size_t) i]->setToggleState (i == bay, juce::dontSendNotification);
            bayTabs[(size_t) i]->toFront (false);
        }
        if (focus == eqFocus && bay == 1) showFocus (mbFocus);
        else if (focus == mbFocus && bay == 0) showFocus (eqFocus);
        repaint();
    }

    //==========================================================================
    void Console::addHint (juce::Component& c, const juce::String& title, const juce::String& text)
    {
        c.addMouseListener (this, true);
        hints.push_back ({ &c, title, text });
    }

    void Console::registerControl (const juce::String& id, ParamControl& c)   { controls[id] = &c; }

    ParamControl* Console::findControl (const juce::String& id) const
    {
        const auto it = controls.find (id);
        return it != controls.end() ? it->second : nullptr;
    }

    void Console::mouseEnter (const juce::MouseEvent& e)
    {
        hovered = e.eventComponent;
        announceUntil = 0.0;   // (pointing at something new: its explanation, not the last announcement)
        if (! showingValue)
            showHintFor (hovered.getComponent());
    }

    void Console::mouseExit (const juce::MouseEvent&)   { hovered = nullptr; }

    juce::Colour Console::tintFor (juce::Component* c) const
    {
        if (c != nullptr)
            if (auto* m = dynamic_cast<Module*> (c) != nullptr ? dynamic_cast<Module*> (c) : c->findParentComponentOfClass<Module>())
                return m->tint;
        return Colours::cyan;
    }

    void Console::showHintFor (juce::Component* c)
    {
        for (auto* walk = c; walk != nullptr && walk != this; walk = walk->getParentComponent())
            for (auto& h : hints)
                if (h.component == walk)
                {
                    juce::String title = h.title;
                    if (auto* m = walk->findParentComponentOfClass<Module>())
                        if (! title.startsWith (m->getName()))
                            title = m->getName() + " " + juce::String::fromUTF8 ("\xc2\xb7") + " " + title;
                    setTicker (title, h.text, tintFor (walk));
                    return;
                }

        // Over nothing in particular: the preset's tip
        if (const auto* preset = mainPresets.findPreset (processor.getPresetName()))
            setTicker (preset->name.toUpperCase(), preset->tip, Colours::selected);
        else
            setTicker ("GETTING STARTED", gettingStartedHint, Colours::selected);
    }

    void Console::setTicker (const juce::String& title, const juce::String& t, juce::Colour colour)
    {
        if (title == tickerTitle && t == tickerText)
            return;
        tickerTitle = title;
        tickerText = t;
        tickerColour = colour;
        repaint (tickerArea);
    }

    void Console::showValue (const juce::String& title, const juce::String& valueWithUnits)
    {
        showingValue = true;
        announceUntil = 0.0;
        setTicker (title.toUpperCase(), valueWithUnits, Colours::selected);
    }

    void Console::valueShown()
    {
        showingValue = false;
        showHintFor (hovered.getComponent());
    }

    void Console::announce (const juce::String& title, const juce::String& t, int seconds)
    {
        announceUntil = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
        setTicker (title, t, Colours::selected);
    }

    void Console::typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
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

    void Console::settingMoved (juce::RangedAudioParameter&)   { markEdited(); }

    void Console::markEdited()
    {
        processor.markPresetEdited();
        presetWindow->repaint();
    }

    //==========================================================================
    juce::String Console::getPresetText() const
    {
        auto name = processor.getPresetName();
        if (name.isEmpty()) name = "PRESETS...";
        if (processor.isPresetEdited()) name += " *";
        return name.toUpperCase();
    }

    void Console::loadMainPreset (const Preset& preset)
    {
        processor.loadPreset (preset);
        for (auto& n : processor.modulePresetNames) n.clear();
        presetWindow->repaint();
        announce (preset.name.toUpperCase(), preset.tip, 5);
    }

    void Console::stepPreset (int direction)
    {
        auto all = processor.getBuiltInPresets();
        for (auto& mine : mainPresets.readUserPresets())
            all.push_back (mine);
        if (all.empty()) return;
        int index = -1;
        for (int i = 0; i < (int) all.size(); ++i)
            if (all[(size_t) i].name == processor.getPresetName())
                index = i;
        index = (index + direction + (int) all.size()) % (int) all.size();
        loadMainPreset (all[(size_t) index]);
    }

    void Console::showMainPresetMenu (juce::Component& under)   { showModulePresetMenu (-1, under); }

    void Console::showModulePresetMenu (int module, juce::Component& under)
    {
        auto& browser = module < 0 ? mainPresets : *moduleBrowsers[(size_t) module];
        auto menu = browser.buildMenu();
        juce::Component::SafePointer<Console> safe (this);
        juce::Component::SafePointer<juce::Component> anchor (&under);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&under).withMinimumWidth (juce::jmax (220, under.getWidth())),
                            [safe, anchor, module] (int result)
        {
            if (safe == nullptr || result == 0) return;
            auto& console = *safe;
            auto& b = module < 0 ? console.mainPresets : *console.moduleBrowsers[(size_t) module];
            if (result == PresetBrowser::saveItem)
            {
                auto* owner = anchor != nullptr ? anchor.getComponent() : static_cast<juce::Component*> (&console);
                console.typeValue (*owner, owner->getLocalBounds().withSizeKeepingCentre (juce::jmax (220, owner->getWidth()), 30), {},
                                   [safe, module] (const juce::String& name)
                {
                    if (safe == nullptr || name.trim().isEmpty()) return;
                    auto& c = *safe;
                    auto& br = module < 0 ? c.mainPresets : *c.moduleBrowsers[(size_t) module];
                    const auto savedAs = br.saveUserPreset (name.trim());
                    for (auto& saved : br.readUserPresets())
                        if (saved.name == savedAs)
                            br.onPresetChosen (saved);
                    c.announce ("PRESET SAVED", savedAs, 4);
                });
                console.setTicker ("NAME YOUR PRESET", "then press Enter (Esc cancels).", Colours::selected);
                return;
            }
            b.handleMenuResult (result);
            console.presetWindow->repaint();
        });
    }

    void Console::applyModulePreset (int module, const Preset* preset)
    {
        // Only this module's settings: the ones the preset gives, the rest of the module back to its defaults
        for (auto& id : MasterModules::ids (module))
            if (auto* param = processor.parameters.getParameter (id))
            {
                float normalised = param->getDefaultValue();
                if (preset != nullptr)
                    for (auto& [pid, value] : preset->values)
                        if (pid == id) normalised = param->convertTo0to1 (value);
                param->beginChangeGesture();
                param->setValueNotifyingHost (normalised);
                param->endChangeGesture();
            }
        processor.modulePresetNames[(size_t) module] = preset != nullptr ? preset->name : juce::String();
        markEdited();
        const juce::String moduleName = juce::String (MasterModules::names[module]).toUpperCase();
        announce (moduleName + (preset != nullptr ? " PRESET" : " - INIT"), preset != nullptr ? preset->name + ": " + preset->tip
                                                                                               : juce::String ("back to its default settings."), 4);
    }

    //==========================================================================
    void Console::timerCallback()
    {
        if (announceUntil > 0.0 && juce::Time::getMillisecondCounterHiRes() > announceUntil)
        {
            announceUntil = 0.0;
            showHintFor (hovered.getComponent());
        }
        else if (! showingValue && announceUntil <= 0.0)
            showHintFor (hovered.getComponent());

        for (auto& m : modules) m->refresh();

        const auto& params = processor.parameters;
        const bool bypassed = params.getRawParameterValue (CommonIDs::bypass)->load() > 0.5f;
        const auto readings = processor.getReadings();

        // The big screen (and the parts beside it that share its numbers)
        const int count = processor.takeMoments (incoming.data(), (int) incoming.size());
        compDisplay.addMoments (incoming.data(), count);
        punchDisplay.addMoments (incoming.data(), count);
        {
            float up = 0.0f, down = 0.0f;
            for (int i = juce::jmax (0, count - 3); i < count; ++i) { up = juce::jmax (up, incoming[(size_t) i].punchUpDb); down = juce::jmax (down, incoming[(size_t) i].punchDownDb); }
            if (count > 0) static_cast<PunchModule*> (getModule (MasterModules::punch))->setChange (up, down);
        }
        compDisplay.setState (params.getRawParameterValue (MasterIDs::ceiling)->load(), processor.isMidSide(),
                              params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f, bypassed);
        const auto picture = processor.takeMultibandPicture();
        static_cast<MultibandModule*> (getModule (MasterModules::multiband))->setPicture (picture);
        const double rate = processor.getSampleRate() > 0 ? processor.getSampleRate() : 48000.0;
        if (focus == mbFocus)
            mbDisplay.setState (processor.multibandSettings(), picture, processor.getTamerPicture(), params.getRawParameterValue (MasterMBIDs::on)->load() > 0.5f, bypassed);
        if (focus == eqFocus)
            eqDisplay.setState (processor.eqSettings(), processor.getTamerPicture(), params.getRawParameterValue (MasterEQIDs::on)->load() > 0.5f, bypassed, rate);
        if (focus == tamerFocus)
            tamerDisplay.setPicture (processor.getTamerPicture(), processor.isTamerMidSide(), params.getRawParameterValue (MasterIDs::tamerOn)->load() > 0.5f,
                                     params.getRawParameterValue (MasterIDs::tFrom)->load(), params.getRawParameterValue (MasterIDs::tTo)->load(), bypassed);
        punchDisplay.setState (params.getRawParameterValue (MasterPunchIDs::on)->load() > 0.5f, bypassed);
        {
            scopeLeft.resize ((size_t) EasyMasterAudioProcessor::scopeSize);
            scopeRight.resize ((size_t) EasyMasterAudioProcessor::scopeSize);
            const int points = processor.takeScope (scopeLeft.data(), scopeRight.data(), EasyMasterAudioProcessor::scopeSize);
            if (focus == widthFocus)
            {
                widthDisplay.addPoints (scopeLeft.data(), scopeRight.data(), points);
                widthDisplay.setState (processor.widthSettings(), readings.sideToMidDb, readings.correlation,
                                       params.getRawParameterValue (MasterWidthIDs::on)->load() > 0.5f, bypassed, rate);
            }
        }

        meters.setReadings (readings, processor.isMidSide(), params.getRawParameterValue (MasterIDs::compOn)->load() > 0.5f,
                            params.getRawParameterValue (MasterIDs::tamerOn)->load() > 0.5f);

        // INPUT
        const float in = juce::Decibels::gainToDecibels (processor.takeInputPeak(), -200.0f);
        inputMeter.setLevels (in, in);
        const bool clipping = processor.getInputPeakDb() > -0.01f;
        inputState.setText (clipping ? "CLIPPING\nON THE WAY IN" : "INPUT OK", juce::dontSendNotification);
        inputState.setColour (juce::Label::textColourId, clipping ? Colours::warn : Colours::dim);

        // Header
        const int slot = processor.getActiveSlot();
        slotA.setToggleState (slot == 0, juce::dontSendNotification);
        slotB.setToggleState (slot == 1, juce::dontSendNotification);
        copySlot.setButtonText (slot == 0 ? "A > B" : "B > A");
        presetWindow->repaint();
    }
}

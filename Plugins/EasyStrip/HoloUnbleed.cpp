// Easy Unbleed's panel in the HOLOGRAM look (Easy Unbleed isn't part of the
// Easy Strip, so it's the one panel built only for its own window).

#include "HoloSlabs.h"

#if HOLO_ONLY_MODULE == 15
#include "../EasyUnbleed/PluginProcessor.h"

namespace Holo
{
    namespace
    {
        const juce::Colour unbleedTint { 0xff6ff2c4 };   // (mint: cleaning)
        const juce::Colour gridFaint   { 0xff10324a };
        const juce::Colour gridLabel   { 0xff5fb8d8 };
        const juce::Colour stemColour  = Colours::gold;
        const juce::Colour otherColour = Colours::frequency;
        const juce::Colour cutColour   = Colours::level;

        constexpr float topDb = 6.0f, rangeDb = 72.0f, cutScaleDb = 40.0f;

        //======================================================================
        /** WHAT IT'S DOING, band by band: this stem (amber), the other stems
            (blue), and how much it's turning each band down (red, from the top).
            Outside FROM / TO is shaded: left alone. */
        class UnbleedScreen : public juce::Component
        {
        public:
            void setPicture (const EasyUnbleedAudioProcessor::Picture& p, float from, float to, bool isBypassed, const juce::String& warning)
            {
                picture = p;
                fromHz = from; toHz = to; bypassed = isBypassed; status = warning;
                for (int b = 0; b < p.numBands; ++b)
                {
                    const auto follow = [] (float& shown, float now) { shown = now > shown ? now : shown + 0.3f * (now - shown); };
                    follow (shownStem[(size_t) b], p.stemDb[(size_t) b]);
                    follow (shownOthers[(size_t) b], p.othersDb[(size_t) b]);
                    shownCut[(size_t) b] = p.reductionDb[(size_t) b];
                }
                repaint();
            }

            void paint (juce::Graphics& g) override
            {
                const auto r = getLocalBounds().toFloat();
                const auto shape = chamfered (r.reduced (0.5f), 10.0f);
                g.setColour (Colours::cyan.withAlpha (0.10f));
                g.strokePath (shape, juce::PathStrokeType (6.0f));
                g.setColour (Colours::screen);
                g.fillPath (shape);
                g.setGradientFill (juce::ColourGradient (Colours::cyan.withAlpha (0.07f), r.getCentreX(), r.getCentreY(),
                                                         juce::Colours::transparentBlack, r.getX(), r.getY(), true));
                g.fillPath (shape);
                g.setColour (Colours::cyan);
                g.strokePath (shape, juce::PathStrokeType (1.2f));

                juce::Graphics::ScopedSaveState save (g);
                g.reduceClipRegion (shape);

                const auto plot = r.reduced (6.0f).withTrimmedLeft (30.0f).withTrimmedRight (8.0f).withTrimmedTop (18.0f).withTrimmedBottom (status.isNotEmpty() ? 62.0f : 40.0f);
                g.setFont (text (14.0f, 1));
                g.setColour (gridLabel);
                g.drawText ("UNBLEED:  THIS STEM (AMBER)   THE OTHER STEMS (BLUE)   TURNED DOWN (RED)",
                            juce::Rectangle<float> (plot.getX(), plot.getY() - 17.0f, plot.getWidth(), 16.0f), juce::Justification::centredRight, false);

                const auto levelY = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, (db - (topDb - rangeDb)) / rangeDb); };
                g.setFont (text (12.0f, 0));
                for (float db : { 0.0f, -12.0f, -24.0f, -36.0f, -48.0f, -60.0f })
                {
                    const float y = levelY (db);
                    g.setColour (db == 0.0f ? juce::Colour (0xff2a6a8a) : gridFaint);
                    g.fillRect (plot.getX(), y, plot.getWidth(), 1.0f);
                    g.setColour (gridLabel);
                    g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 34.0f, y - 7.0f, 29.0f, 14.0f), juce::Justification::centredRight, false);
                }

                const int bands = juce::jmax (1, picture.numBands);
                const float slot = plot.getWidth() / (float) bands;
                for (int b = 0; b < picture.numBands; ++b)
                {
                    const float x = plot.getX() + slot * (float) b;
                    const float hz = picture.centreHz[(size_t) b];
                    if (! (hz >= fromHz * 0.89f && hz <= toHz * 1.12f))
                    {
                        g.setColour (juce::Colours::black.withAlpha (0.45f));
                        g.fillRect (juce::Rectangle<float> (x, plot.getY(), slot, plot.getHeight()));
                    }
                    const float w = juce::jmax (2.0f, slot - 3.0f);
                    const float yo = levelY (shownOthers[(size_t) b]), ys = levelY (shownStem[(size_t) b]);
                    g.setColour (otherColour.withAlpha (0.40f));
                    g.fillRect (juce::Rectangle<float> (x + 1.0f, yo, w, plot.getBottom() - yo));
                    g.setColour (otherColour.withAlpha (0.9f));
                    g.fillRect (juce::Rectangle<float> (x + 1.0f, yo, w, 1.5f));
                    g.setColour (stemColour.withAlpha (0.85f));
                    g.fillRect (juce::Rectangle<float> (x + 1.0f + w * 0.25f, ys, w * 0.5f, plot.getBottom() - ys));
                    const float cut = juce::jlimit (0.0f, 1.0f, shownCut[(size_t) b] / cutScaleDb);
                    if (cut > 0.005f)
                    {
                        g.setColour (cutColour.withAlpha (0.75f));
                        g.fillRect (juce::Rectangle<float> (x + 1.0f, plot.getY(), w, plot.getHeight() * 0.5f * cut));
                    }
                }

                // Frequencies along the bottom
                g.setColour (gridLabel.withAlpha (0.8f));
                g.setFont (text (12.0f, 0));
                for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
                {
                    if (picture.numBands <= 0) break;
                    int nearest = 0;
                    for (int b = 0; b < picture.numBands; ++b)
                        if (std::abs (std::log (picture.centreHz[(size_t) b] / hz)) < std::abs (std::log (picture.centreHz[(size_t) nearest] / hz))) nearest = b;
                    const float x = plot.getX() + slot * ((float) nearest + 0.5f);
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "K" : juce::String ((int) hz),
                                juce::Rectangle<float> (x - 20.0f, plot.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centred, false);
                }

                // The sidechain warning
                if (status.isNotEmpty())
                    drawGlowText (g, status.toUpperCase(), juce::Rectangle<float> (plot.getX(), plot.getBottom() + 26.0f, plot.getWidth(), 20.0f),
                                  juce::Justification::centred, text (15.0f, 2), Colours::warn, 0.6f);

                if (bypassed)
                {
                    g.setColour (juce::Colours::black.withAlpha (0.55f));
                    g.fillPath (shape);
                    const auto badge = juce::Rectangle<float> (260.0f, 64.0f).withCentre (r.getCentre());
                    g.setColour (juce::Colour (0xe6040c16));
                    g.fillPath (chamfered (badge, 8.0f));
                    strokeGlow (g, chamfered (badge.reduced (0.5f), 8.0f), Colours::warn, 1.2f, 0.5f);
                    auto lines = badge.reduced (10.0f, 8.0f);
                    drawGlowText (g, "BYPASSED", lines.removeFromTop (lines.getHeight() * 0.58f), juce::Justification::centred, display (20.0f), Colours::warn, 0.7f);
                    g.setColour (Colours::dim);
                    g.setFont (text (14.0f, 1));
                    g.drawText ("YOU'RE HEARING THE ORIGINAL STEM", lines, juce::Justification::centred, false);
                }
            }

        private:
            EasyUnbleedAudioProcessor::Picture picture;
            std::array<float, SpectralUnbleed::numBandsMax> shownStem {}, shownOthers {}, shownCut {};
            float fromHz = 20.0f, toHz = 20000.0f;
            bool bypassed = false;
            juce::String status;
        };

        //======================================================================
        class UnbleedSlab : public Slab
        {
        public:
            explicit UnbleedSlab (SlabHost& v) : Slab (v, 15, unbleedTint, "UNBLEED")
            {
                addAndMakeVisible (lamp);
                hint (lamp, "WHAT IT'S DOING", "TURNED DOWN");
                amount = &ring (UnbleedIDs::amount, "AMOUNT", Format::db, "dB", Ring::large, Colours::level);
                sensitivity = &ring (UnbleedIDs::sensitivity, "SENSITIVITY", Format::percent, "%", Ring::medium);
                smoothness = &ring (UnbleedIDs::smoothness, "SMOOTHNESS", Format::percent, "%", Ring::medium);
                from = &ring (UnbleedIDs::from, "FROM", Format::hertz, "Hz", Ring::medium, Colours::frequency);
                to = &ring (UnbleedIDs::to, "TO", Format::hertz, "Hz", Ring::medium, Colours::frequency);
                listen = &toggle (UnbleedIDs::listen, "REMOVED ONLY", "REMOVED ONLY", Button::Style::plain, Colours::warn);
                listenNote = &caption ("HEAR ONLY WHAT IT TAKES OUT: OTHER INSTRUMENTS, NEVER THIS STEM'S OWN SOUND", Colours::dim, 13.0f);
                listenNote->setJustificationType (juce::Justification::centred);
                listenNote->setMinimumHorizontalScale (1.0f);

                view.addHint (screen, "WHAT IT'S DOING", view.moduleHint (module, "WHAT IT'S DOING"));
            }

            juce::Component* getSideDisplay() override   { return &screen; }

            void resized() override
            {
                clearSections();
                auto body = placeHeader (getLocalBounds().reduced (9, 6));
                lamp.setBounds (12, 10, 20, 30);

                auto top = body.removeFromTop ((body.getHeight() - 8) * 11 / 20);
                body.removeFromTop (8);

                auto s1 = section (top.removeFromLeft (top.getWidth() / 2 - 4), 1, "HOW MUCH");
                top.removeFromLeft (8);
                auto s2 = section (top, 2, "HOW");
                centreIn (*amount, s1);
                centreIn (*sensitivity, s2.removeFromLeft (s2.getWidth() / 2));
                centreIn (*smoothness, s2);

                auto s3 = section (body.removeFromLeft (body.getWidth() / 2 - 4), 3, "WHERE");
                body.removeFromLeft (8);
                auto s4 = section (body, 4, "LISTEN");
                centreIn (*from, s3.removeFromLeft (s3.getWidth() / 2));
                centreIn (*to, s3);
                {
                    auto c = s4.withSizeKeepingCentre (s4.getWidth(), 34 + 8 + 40);
                    listen->setBounds (c.removeFromTop (34).withSizeKeepingCentre (150, 34));
                    c.removeFromTop (8);
                    listenNote->setBounds (c.reduced (4, 0));
                }
            }

            void paint (juce::Graphics& g) override
            {
                Slab::paint (g);
            }

            void refresh() override
            {
                auto& p = static_cast<EasyUnbleedAudioProcessor&> (strip.getModule (module));
                const auto s = p.getCurrentSettings();
                const auto picture = p.getPicture();

                juce::String warning;
                if (p.isSidechainACopy())        warning = "The sidechain is a copy of this stem - ignored, nothing is removed";
                else if (p.isSidechainMissing()) warning = "Nothing in the sidechain: send the OTHER stems to it";
                screen.setPicture (picture, s.fromHz, s.toHz, s.bypassed, warning);

                float most = 0.0f;
                for (int b = 0; b < picture.numBands; ++b)
                    most = juce::jmax (most, picture.reductionDb[(size_t) b]);
                lamp.setLit (strip.isModuleOn (module) ? juce::jlimit (0.0f, 1.0f, most / 12.0f) : 0.0f);
            }

        private:
            Lamp lamp { unbleedTint };
            Ring *amount = nullptr, *sensitivity = nullptr, *smoothness = nullptr, *from = nullptr, *to = nullptr;
            Button* listen = nullptr;
            juce::Label* listenNote = nullptr;
            UnbleedScreen screen;
        };
    }

    std::unique_ptr<Slab> makeUnbleedSlab (SlabHost& v)   { return std::make_unique<UnbleedSlab> (v); }
}
#endif

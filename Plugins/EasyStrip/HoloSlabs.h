#pragma once

#include "HoloView.h"

namespace Holo
{
    //==========================================================================
    /**
        One module's panel ("slab"): its power button (lit = on; the same
        switch as its BYPASS), INIT (just this module back to its defaults) and
        Pr (this module's presets - the standalone's own list), then its
        controls in the standalone's numbered sections.
    */
    class Slab : public juce::Component
    {
    public:
        Slab (View&, int module, juce::Colour tint, const juce::String& powerText);
        ~Slab() override;

        void paint (juce::Graphics&) override;

        /** Called ~30 times a second: meters, lamps, and the big screen's trace. */
        virtual void refresh() {}

        int getModule() const noexcept   { return module; }

        Button power, init, pr;

    protected:
        juce::RangedAudioParameter& param (const char* id);
        Ring& ring (const char* id, const juce::String& title, Format, const juce::String& unit,
                    Ring::Size = Ring::small, juce::Colour = {}, const juce::String& hintTitle = {});
        Bar& bar (const char* id, const juce::String& title, Format, const juce::String& unit,
                  juce::Colour = {}, bool centreZero = false, const juce::String& hintTitle = {});
        Choice& choice (const char* id, const juce::StringArray& names, const juce::String& hintTitle, bool sideBySide = false);
        Button& toggle (const char* id, const juce::String& text, const juce::String& hintTitle,
                        Button::Style = Button::Style::plain, juce::Colour = {});
        juce::Label& caption (const juce::String& text, juce::Colour = Colours::ink, float size = 14.0f);
        void hint (juce::Component&, const juce::String& standaloneTitle, const juce::String& shownTitle = {});

        /** Lays out the top row (power in the middle, or at the left; INIT and Pr on the right)
            and returns what's left below it. */
        juce::Rectangle<int> placeHeader (juce::Rectangle<int> area, bool powerAtLeft = false, int powerWidth = 84);

        /** A numbered section's box (drawn behind its controls); returns its inside. */
        juce::Rectangle<int> section (juce::Rectangle<int>, int number, const juce::String& title);
        void clearSections()   { sections.clear(); }

        /** Places a control centred in a column. */
        static void centreIn (juce::Component&, juce::Rectangle<int> column, int top = -1);

        View& view;
        EasyStripAudioProcessor& strip;
        const int module;
        juce::Colour tint;
        bool deepRed = false;

        std::vector<std::unique_ptr<juce::Component>> parts;
        std::vector<std::unique_ptr<ButtonLink>> links;

    private:
        struct Section { juce::Rectangle<float> area; int number; juce::String title; };
        std::vector<Section> sections;
        std::unique_ptr<ButtonLink> powerLink;
    };

    //==========================================================================
    /** Clip's ADVANCED row (3 SOUND DESIGN), shown under Amp and the EQ in place
        of the standby panel while Clip is set to ADVANCED. */
    class ClipAdvanced : public Slab
    {
    public:
        explicit ClipAdvanced (View&);
        void resized() override;
        void paint (juce::Graphics&) override;

        Button& getLinkButton()   { return *linkButton; }

    private:
        Button* linkButton = nullptr;
        juce::Rectangle<int> linkBox;
        juce::Point<int> headingAt;
    };

    std::unique_ptr<Slab> makeSlab (View&, int module);
}

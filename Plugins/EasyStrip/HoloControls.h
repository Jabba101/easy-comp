#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "HoloLook.h"

//==============================================================================
/**
    The HOLOGRAM skin's controls - digital, after BLEASS: values inside the
    controls (thin rings with the number in the middle, tall fill bars),
    choices as stacks of lit segments, on = solid fill, off = outline.

    Every control follows the DOUBLE-CLICK RULE: double-click the control =
    back to its default; double-click its NUMBER = type an exact value
    (Enter sets it, Esc cancels; units optional, "k" = thousand, OFF where
    the control has an OFF position). Drag up / down (Shift: fine), or use
    the mouse wheel.
*/
namespace Holo
{
    /** What a control needs from the window around it: somewhere to show
        hints and values (the ticker), and a box to type a value into. */
    struct Host
    {
        virtual ~Host() = default;

        /** While a control is being moved: its name and value with units. */
        virtual void showValue (const juce::String& title, const juce::String& valueWithUnits) = 0;
        virtual void valueShown() = 0;   // (released: back to the hint)

        /** Opens the typing box over `area` (in `owner`'s coordinates). */
        virtual void typeValue (juce::Component& owner, juce::Rectangle<int> area, const juce::String& current,
                                std::function<void (const juce::String&)> onEntered) = 0;

        /** Any setting was changed here by hand (so presets show "edited"). */
        virtual void settingMoved (juce::RangedAudioParameter&) {}
    };

    /** How a value is written inside a control (the unit goes underneath). */
    enum class Format
    {
        signedDb,     // +3.0
        db,           // -20.0
        ratio,        // 4.0
        msPrecise,    // 0.35, 23.5, 187.3, 1200   (Comp's ATTACK / RELEASE)
        ms,           // 0.50, 5.0, 150
        percent,      // 35
        signedPercent,// +20
        hertz,        // 60, 3.0k, 12k
        whole,        // 300
        asText        // the setting's own words (e.g. a note length)
    };

    juce::String formatValue (Format, float value);

    //==========================================================================
    /** A control connected to one setting: dragging, the wheel, the
        double-click rule, gestures for the DAW's automation. */
    class ParamControl : public juce::Component
    {
    public:
        ParamControl (juce::RangedAudioParameter&, const juce::String& title, Format, const juce::String& unit);

        juce::RangedAudioParameter& getParameter() noexcept   { return parameter; }
        float getNormalised() const noexcept                   { return parameter.convertTo0to1 (current); }
        float getValue() const noexcept                        { return current; }
        const juce::String& getTitle() const noexcept          { return title; }

        /** The number shown (OFF when the setting says so). */
        juce::String getValueText() const;
        juce::String getUnit() const;

        /** Moves it as if dragged by hand (normalised 0..1). */
        void setFromUser (float normalised);
        void resetToDefault();

        /** LINK (Clip's TOP / BOTTOM): while `isLinked` says so, moving this
            one moves the partner by the same amount, keeping the gap. */
        void linkWith (ParamControl& partner, std::function<bool()> isLinked);

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        juce::Colour tint { Colours::cyan };

    protected:
        /** Where the number is (double-click there to type). */
        virtual juce::Rectangle<float> getNumberArea() const = 0;

        juce::RangedAudioParameter& parameter;
        juce::String title, unit;
        Format format;
        float current = 0.0f;

    private:
        void startMove();
        void endMove();
        void moveBy (float newNormalised);
        Host* host();

        juce::ParameterAttachment attachment;
        ParamControl* partner = nullptr;
        std::function<bool()> linked;
        float dragStartY = 0.0f, dragStartValue = 0.0f;
        bool moving = false;
    };

    //==========================================================================
    /** A thin value ring with the number in the middle (and its name above). */
    class Ring : public ParamControl
    {
    public:
        enum Size { tiny = 50, small = 58, medium = 66, big = 94, large = 112 };

        Ring (juce::RangedAudioParameter&, const juce::String& title, Format, const juce::String& unit,
              Size = small, juce::Colour = Colours::cyan, bool showTitle = true);

        /** The lit arc grows out from the middle (for settings where the middle changes nothing). */
        void setFromCentre (bool fromCentre)   { centred = fromCentre; repaint(); }

        /** The best size for it (with its name, on one or two lines). */
        int getIdealWidth() const;
        int getIdealHeight() const;

        void paint (juce::Graphics&) override;

    private:
        juce::Rectangle<float> getNumberArea() const override;
        juce::Rectangle<float> ringArea() const;

        Size size;
        bool showTitle, centred = false;
    };

    //==========================================================================
    /** A tall fill bar: drag the fill line up and down; the value sits at the line.
        Centre-zero bars fill from the middle (the middle changes nothing). */
    class Bar : public ParamControl
    {
    public:
        Bar (juce::RangedAudioParameter&, const juce::String& title, Format, const juce::String& unit,
             juce::Colour = Colours::cyan, bool centreZero = false);

        void paint (juce::Graphics&) override;
        juce::Rectangle<float> getBarArea() const;

        bool showLabels = true;   // its name above and unit below

    private:
        juce::Rectangle<float> getNumberArea() const override;
        bool centreZero;
    };

    //==========================================================================
    /** A button: outlined, filled with its colour when on. */
    class Button : public juce::Button
    {
    public:
        enum class Style
        {
            plain,      // a rectangle with its text
            square,     // a square two-line button (AUTO over GAIN)
            power,      // a module's on/off switch (filled = on)
            init,       // red INIT
            view        // the page buttons: amber when chosen
        };

        Button (const juce::String& text, juce::Colour tint = Colours::cyan, Style = Style::plain);

        void paintButton (juce::Graphics&, bool isMouseOver, bool isMouseDown) override;

        juce::Colour tint;
        Style style;
        float fontSize = 14.0f;
        bool flash = false;
        bool displayFont = false;   // (Orbitron, like the strip's name: WEAPON SELECT)   // (INIT lights up briefly when pressed)
    };

    /** Connects a Button to an on/off setting. `inverted`: on = the setting
        is 0 (a module's power switch, which is its BYPASS turned round). */
    class ButtonLink : private juce::Button::Listener
    {
    public:
        ButtonLink (juce::RangedAudioParameter&, juce::Button&, bool inverted = false);
        ~ButtonLink() override;

    private:
        void buttonClicked (juce::Button*) override;
        juce::RangedAudioParameter& parameter;
        juce::Button& button;
        bool inverted;
        juce::ParameterAttachment attachment;
    };

    //==========================================================================
    /** A choice as a stack of segments, one lit (THIS TRACK / SIDECHAIN...). */
    class Choice : public juce::Component
    {
    public:
        Choice (juce::RangedAudioParameter&, const juce::StringArray& names, juce::Colour tint, bool sideBySide = false);

        /** A choice that isn't a setting of the sound (DIM, PEAK / LUFS...): set it with setChosen. */
        Choice (const juce::StringArray& names, juce::Colour tint, bool sideBySide = false);
        void setChosen (int index)         { chosen = index; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        int getChosen() const noexcept     { return chosen; }
        void choose (int index);            // (as if clicked)
        int getIdealHeight() const         { return sideBySide ? segmentHeight : names.size() * (segmentHeight + 3) - 3; }

        juce::Colour tint;
        int segmentHeight = 28;
        float fontSize = 14.0f;
        std::function<void (int)> onUserChoice;

    private:
        juce::Rectangle<float> segment (int index) const;

        juce::RangedAudioParameter* parameter = nullptr;
        juce::StringArray names;
        bool sideBySide;
        int chosen = 0;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    //==========================================================================
    /** A round lamp. */
    class Lamp : public juce::Component
    {
    public:
        explicit Lamp (juce::Colour c = Colours::cyan) : colour (c)   { setInterceptsMouseClicks (false, false); }
        void setLit (float amount);   // 0 = dark, 1 = fully lit
        void paint (juce::Graphics&) override;
        juce::Colour colour;
        float lit = 0.0f;
    };

    //==========================================================================
    /** A module's level meter: two thin bars, a dB scale on the left, and the
        THRESHOLD line across (when a bar beside it sets one). */
    class Meter : public juce::Component
    {
    public:
        Meter (float lowestDb, float highestDb, std::vector<float> marks);

        void setLevels (float leftDb, float rightDb);
        void setLine (float db, bool show = true)   { lineDb = db; showLine = show; repaint(); }
        void paint (juce::Graphics&) override;

        juce::Colour lineColour { Colours::ink };

    private:
        float lowest, highest;
        std::vector<float> marks;
        float shown[2] { -200.0f, -200.0f };
        float lineDb = 0.0f;
        bool showLine = false;
    };

    /** Gain reduction (fills down from the top) or gain change (from the
        middle: up = louder, down = quieter). */
    class GainBar : public juce::Component
    {
    public:
        GainBar (float rangeDb, bool centreZero, juce::Colour);
        void setGainDb (float db);   // reduction: positive; change: + louder, - quieter
        void paint (juce::Graphics&) override;
        float getShownDb() const noexcept   { return shownDb; }

    private:
        float range;
        bool centreZero;
        juce::Colour colour;
        float shownDb = 0.0f;
    };

    /** INPUT / OUTPUT: a tall two-bar meter, -24 dB at the bottom, 0 two
        thirds of the way up (where the scale is finest), +12 at the top.
        "0" means 0 dBFS or a reference such as -18 dBFS (SETUP). */
    class BigMeter : public juce::Component
    {
    public:
        void setLevels (float leftDb, float rightDb);   // relative to the reference already
        void setReference (float referenceDb)           { reference = referenceDb; repaint(); }
        void paint (juce::Graphics&) override;
        static float positionOf (float db);             // 0 = top, 1 = bottom

        float getShownDb (int channel) const noexcept   { return shown[channel]; }

    private:
        float shown[2] { -200.0f, -200.0f }, held[2] { -200.0f, -200.0f };
        int holdFor[2] { 0, 0 };
        float reference = 0.0f;
    };
}

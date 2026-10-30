#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>

//==============================================================================
/** The big screen: the EQ's curve over a faint spectrum of the sound
    coming out, with a dot for each band that you can drag (across =
    frequency, up and down = gain; double-click = back to 0 dB). With any
    band on MID or SIDE (or MONO BASS on), the middle and the sides get a
    curve each: MID in cyan, SIDE in magenta. */
class EQDisplay : public juce::Component
{
public:
    explicit EQDisplay (EasyEQAudioProcessor&);

    /** Called ~30 times a second: re-reads the settings and the analyser. */
    void refresh (bool analyserOn);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    static juce::Colour bandColour (int band);
    static constexpr float rangeDb = 15.0f;   // the screen shows +-15 dB

    /** (Tests) where a band's dot is drawn. */
    juce::Point<float> dotPosition (int band) const;
    int getHoveredBand() const noexcept   { return hovered; }

    std::function<void()> onUserChange;

private:
    juce::Rectangle<float> plotArea() const;
    float xFor (double hz) const;
    double hzFor (float x) const;
    float yFor (double db) const;
    double dbFor (float y) const;
    int bandAt (juce::Point<float>) const;
    void setParameter (const char* id, float value);

    EasyEQAudioProcessor& processor;
    Equalizer::Settings shown;
    bool bypassed = false, analyserOn = true, showMidSide = false;

    juce::dsp::FFT fft { 12 };
    std::array<float, EasyEQAudioProcessor::analyserLength * 2> fftData {};
    std::array<float, EasyEQAudioProcessor::analyserLength / 2> spectrumDb {};

    int hovered = -1, dragging = -1;
};

//==============================================================================
/** A button that's on only while held down (LISTEN). */
class HoldButton : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void (bool)> onHold;

private:
    void buttonStateChanged() override
    {
        const bool down = isDown();
        if (down != held)
        {
            held = down;
            if (onHold) onHold (down);
        }
    }

    bool held = false;
};

//==============================================================================
/** The Easy EQ window: the shared header and help bar (from EasyWindow),
    the screen, ① CLEAN UP (HPF, MONO BASS, LPF) and ② SHAPE (the four
    bands), and the frequency guide. */
class EasyEQAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyEQAudioProcessorEditor (EasyEQAudioProcessor&);

    // (public so the tests can find them)
    enum KnobIndex { lowFreq, lowGain, lowMidFreq, lowMidGain, highMidFreq, highMidGain, highFreq, highGain,
                     hpfKnob, monoBassKnob, lpfKnob, numKnobs };
    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    juce::Button& getInitAllButton()           { return initAllButton; }
    HoldButton& getListenButton (int band)     { return listenButtons[(size_t) band]; }
    EQDisplay& getDisplay()                    { return display; }
    juce::Button& getSlotButton (int slot)     { return slot == 0 ? (juce::Button&) slotA : (juce::Button&) slotB; }
    juce::Button& getCopyButton()              { return copyButton; }

    /** (Tests only: what the 30-times-a-second update does, right now.) */
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyEQAudioProcessor& eqProcessor;

    std::array<Knob, numKnobs> knobs;
    std::array<std::unique_ptr<ChoiceSwitch>, 4> targetSwitches;
    std::unique_ptr<ChoiceSwitch> lowShape, highShape, hpfSlope, lpfSlope, precisionSwitch;
    std::array<HoldButton, 4> listenButtons;
    LightButton masterButton { "MASTER", juce::Colour (0xff4f8cff) }, analyserButton { "ANALYSER", Palette::accent };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> masterAttachment, analyserAttachment;
    juce::TextButton initAllButton { "INIT" };
    LightButton slotA { "A", Palette::accent }, slotB { "B", Palette::accent };
    juce::TextButton copyButton { "COPY A > B" };
    EQDisplay display;

    bool monoTrack = false;
    float shownAutoGainDb = 0.0f;

    juce::Rectangle<int> displayPanel, cleanPanel, shapePanel, guideArea, autoGainReadout;
    std::array<juce::Rectangle<int>, 4> bandColumns;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyEQAudioProcessorEditor)
};

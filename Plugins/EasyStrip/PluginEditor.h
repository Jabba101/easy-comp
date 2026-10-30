#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"
#include "HoloView.h"

//==============================================================================
/** A plain level meter (-60 to +6 dB), with a peak line that falls back slowly. */
class LevelMeter : public juce::Component
{
public:
    void setLevel (float gain);
    void paint (juce::Graphics&) override;
    float getShownDb() const noexcept   { return shownDb; }

private:
    float shownDb = -100.0f, holdDb = -100.0f;
    int holdFor = 0;
};

//==============================================================================
/**
    The CLASSIC skin: a plain working layout that shows each module's own
    standalone window in the middle (chosen in SETUP; HOLOGRAM is the default):

        header      BYPASS (the whole strip), INIT (the whole strip), presets
        INPUT       TRIM, the polarity flip, a meter
        middle      a tab per module, each with its ON lamp; OVERSAMPLING
                    (AMP and CLIP); and the chosen module's own window
        OUTPUT      LEVEL, a meter, short-term and integrated LUFS (click
                    the integrated number to start it again)
*/
class ClassicStripWindow : public EasyWindow
{
public:
    explicit ClassicStripWindow (EasyStripAudioProcessor&);
    ~ClassicStripWindow() override;

    std::function<void()> onSwitchToHologram;
    juce::TextButton hologramButton { "HOLOGRAM LOOK" };

    /** Shows module m's own window in the middle. */
    void showModule (int m);
    int getShownModule() const noexcept                   { return shownModule; }
    juce::AudioProcessorEditor* getModuleWindow() const   { return moduleWindow.get(); }

    //==== (public so the parts can be found and tested) =======================
    std::array<juce::TextButton, EasyStripAudioProcessor::numModules> tabs;
    std::array<std::unique_ptr<LightButton>, EasyStripAudioProcessor::numModules> onLamps;
    juce::TextButton initAllButton { "INIT" };
    LightButton polarityButton { juce::String::fromUTF8 ("\xc3\x98"), Palette::meterHigh };
    juce::Slider trimKnob, levelKnob;
    ChoiceSwitch oversamplingSwitch;
    LevelMeter inputMeter, outputMeter;
    juce::Label shortTermReadout, integratedReadout;

    void updateDisplaysNow()   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void mouseDown (const juce::MouseEvent&) override;

    EasyStripAudioProcessor& strip;
    void closeModuleWindow();

    juce::Component moduleHolder;   // (the module's window goes inside this, which is shrunk to fit)
    std::unique_ptr<juce::AudioProcessorEditor> moduleWindow;
    int shownModule = -1;

    juce::Label trimLabel, levelLabel, oversamplingLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> trimAttachment, levelAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> polarityAttachment;

    juce::Rectangle<int> inputPanel, outputPanel, modulePanel, moduleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClassicStripWindow)
};

//==============================================================================
/**
    The Easy Strip's window. It shows one of two skins:

        HOLOGRAM   (the default) every module's controls on one panel
                   around the big screen - see HoloView.h
        CLASSIC    the plain layout above, with each module's own window

    and can be any size: pick one in SETUP or drag the corner (the shape stays
    the same). The size, skin and other SETUP choices are remembered for
    every Easy Strip.
*/
class EasyStripAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit EasyStripAudioProcessorEditor (EasyStripAudioProcessor&);
    ~EasyStripAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    Holo::View* getHologram() const noexcept         { return hologram.get(); }
    ClassicStripWindow* getClassic() const noexcept  { return classic.get(); }
    StripSettings& getSettings() noexcept            { return settings; }

    /** Builds the chosen skin at the chosen size (after a SETUP change). */
    void applySettings();

private:
    juce::Component* currentView() const;
    juce::Point<int> nativeSize() const;

    EasyStripAudioProcessor& strip;
    StripSettings settings;
    std::unique_ptr<Holo::View> hologram;
    std::unique_ptr<ClassicStripWindow> classic;
    juce::Component classicHolder;   // (an editor inside an editor mustn't be transformed itself)
    int builtSkin = -1;
    bool builtTicker = true, applying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyStripAudioProcessorEditor)
};

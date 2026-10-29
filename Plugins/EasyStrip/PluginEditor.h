#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

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
    The Easy Strip window - for now a plain working layout (the HOLOGRAM look
    comes later):

        header      BYPASS (the whole strip), INIT (the whole strip), presets
        INPUT       TRIM, the polarity flip, a meter
        middle      a tab per module, each with its ON lamp; OVERSAMPLING
                    (AMP and CLIP); and the chosen module's own window
        OUTPUT      LEVEL, a meter, short-term and integrated LUFS (click
                    the integrated number to start it again)
*/
class EasyStripAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyStripAudioProcessorEditor (EasyStripAudioProcessor&);
    ~EasyStripAudioProcessorEditor() override;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyStripAudioProcessorEditor)
};

#pragma once

#include "EasyWindow.h"
#include "PluginProcessor.h"

//==============================================================================
/** "WHAT IT'S DOING": the last six seconds - into the limiter (grey), out
    (amber), the ceiling, and from the top how much the limiter (red) and
    the compressor (green = MID or both, blue = SIDE) are turning down. */
class MasterDisplay : public juce::Component
{
public:
    MasterDisplay();
    void addMoments (const EasyMasterAudioProcessor::Moment*, int count);
    void setState (float ceilingDb, bool midSide, bool compOn, bool bypassed);
    void paint (juce::Graphics&) override;

    static constexpr int numMoments = 600;   // 6 seconds of 10 ms moments

private:
    std::vector<EasyMasterAudioProcessor::Moment> history;
    int next = 0;
    float ceilingDb = -0.3f;
    bool midSide = false, compOn = true, bypassed = false;
};

//==============================================================================
/** The RESONANCE TAMER page's display: the mix a sixth of an octave at a
    time, and from the top how much each spot is turned down. */
class TamerDisplay : public juce::Component
{
public:
    void setPicture (const EasyMasterAudioProcessor::TamerPicture&, bool midSide, bool on, float fromHz, float toHz, bool bypassed);
    void paint (juce::Graphics&) override;

private:
    EasyMasterAudioProcessor::TamerPicture picture;
    std::array<std::array<float, ResonanceTamer::numBands>, 2> shownLevel {};
    bool midSide = false, on = true, bypassed = false;
    float fromHz = 200.0f, toHz = 16000.0f;
};

//==============================================================================
/** The EQ page's display: the curve for the middle (green, or amber for the
    whole mix in STEREO) and the sides (blue), over a faint picture of the
    mix, with a numbered dot for each band. */
class EQDisplay : public juce::Component
{
public:
    /** Dragging a dot moves its band (FREQ across, GAIN up and down); double-click puts the band back to 0 dB. */
    explicit EQDisplay (juce::AudioProcessorValueTreeState& p) : parameters (p) {}
    void setState (const MasteringEQ::Settings&, const EasyMasterAudioProcessor::TamerPicture&, bool on, bool bypassed, double sampleRate);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override   { hovered = {}; repaint(); }

    /** A dot: part 0 = MID (or both), 1 = SIDE; band 0..4, or 5 = the LOW CUT. */
    struct Node { int part = -1, band = -1; bool valid() const noexcept { return part >= 0; } bool operator== (const Node& o) const noexcept { return part == o.part && band == o.band; } };
    juce::Point<float> nodePosition (Node) const;   // (public so it can be tested)
    std::function<void()> onEdit;                    // (a band was dragged: the preset box shows "(edited)")

private:
    juce::Rectangle<float> plotArea() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    float yOf (float db) const;
    float dbAt (float y) const;
    float scale() const;
    Node nodeAt (juce::Point<float>) const;
    void setParameter (const char* id, float value);

    juce::AudioProcessorValueTreeState& parameters;
    Node dragging, hovered;
    float dragRange = 6.0f;
    MasteringEQ::Settings settings;
    std::array<float, ResonanceTamer::numBands> shownLevel {};
    std::array<float, ResonanceTamer::numBands> centreHz {};
    bool on = true, bypassed = false;
    double rate = 48000.0;
};

//==============================================================================
/** The MULTIBAND page's display: the four bands across the spectrum (drag
    the lines between them to move the splits), each showing how much it's
    being turned down (red = the middle or both, blue = the sides). */
class MultibandDisplay : public juce::Component
{
public:
    explicit MultibandDisplay (juce::AudioProcessorValueTreeState& p) : parameters (p) {}
    void setState (const MultibandCompressor::Settings&, const EasyMasterAudioProcessor::MultibandPicture&,
                   const EasyMasterAudioProcessor::TamerPicture&, bool on, bool bypassed);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    float splitX (int split) const;   // (public so it can be tested)
    std::function<void()> onEdit;

private:
    juce::Rectangle<float> plotArea() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    int splitAt (float x) const;

    juce::AudioProcessorValueTreeState& parameters;
    MultibandCompressor::Settings settings;
    std::array<std::array<float, 2>, 4> shownCut {}, shownPeak {};
    std::array<float, ResonanceTamer::numBands> shownLevel {}, centreHz {};
    bool on = false, bypassed = false;
    int dragging = -1, hovered = -1;
};

//==============================================================================
/** The readings: LUFS momentary / short-term / integrated, the highest true
    peak, PLR, stereo correlation, and how much each part turns down now.
    Click it to reset the integrated LUFS and the highest peak. */
class MasterMeters : public juce::Component
{
public:
    void setReadings (const EasyMasterAudioProcessor::Readings&, bool midSide, bool compOn, bool tamerOn);
    void paint (juce::Graphics&) override;
    std::function<void()> onReset;
    void mouseDown (const juce::MouseEvent&) override   { if (onReset) onReset(); }

private:
    EasyMasterAudioProcessor::Readings r;
    float shownMid = 0.0f, shownSide = 0.0f, shownLimit = 0.0f, shownTamer = 0.0f;
    bool midSide = false, compOn = true, tamerOn = true;
};

//==============================================================================
/** The Easy Master window: the chain along the top (click a stage to show
    its page: its own display and controls), INPUT on the left, the page in
    the middle, and the METERS, LIMITER and COMPARE always on the right. */
class EasyMasterAudioProcessorEditor : public EasyWindow
{
public:
    explicit EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor&);

    enum KnobIndex { trimKnob,
                     midThresholdKnob, midRatioKnob, midAttackKnob, midReleaseKnob, midMakeupKnob,
                     sideThresholdKnob, sideRatioKnob, sideAttackKnob, sideReleaseKnob, sideMakeupKnob,
                     mixKnob, ignoreKnob, gainKnob, ceilingKnob, releaseKnob,
                     tMidAmountKnob, tMidSensKnob, tMidSharpKnob, tSideAmountKnob, tSideSensKnob, tSideSharpKnob,
                     tSpeedKnob, tFromKnob, tToKnob, numKnobs };

    /** The pages the chain along the top switches between. */
    enum Page { tamerPage = 0, eqPage, mbPage, compPage, numPages };
    juce::Slider& getMBKnob (int part, int band, int which)   { return mbKnobs[(size_t) part][(size_t) (band * 4 + which)].slider; }   // which: 0 THRESHOLD, 1 RATIO, 2 SPEED, 3 GAIN
    juce::Slider& getMBSplit (int split)                       { return mbSplits[(size_t) split]; }
    MultibandDisplay& getMBDisplay()                           { return mbDisplay; }
    EQDisplay& getEQDisplay()                 { return eqDisplay; }
    juce::Slider& getEQKnob (int part, int column)   { return eqKnobs[(size_t) part][(size_t) column].slider; }   // column: 0..9 FREQ / GAIN per band, 10 LOW CUT
    void showPage (int page);
    int getShownPage() const noexcept          { return shownPage; }

    juce::Slider& getKnob (int index)          { return knobs[(size_t) index].slider; }
    bool isKnobShowing (int index) const       { return knobs[(size_t) index].slider.isVisible(); }
    bool isKnobUsable (int index) const        { return knobs[(size_t) index].slider.isEnabled(); }
    juce::Button& getInitAllButton()           { return initAllButton; }
    juce::Button& getSlotButton (int slot)     { return slot == 0 ? slotA : slotB; }
    juce::Button& getCopyButton()              { return copyButton; }
    void updateDisplaysNow()                   { refreshLiveDisplays(); }

private:
    void layoutBody (juce::Rectangle<int> area) override;
    void paintBody (juce::Graphics&) override;
    void refreshLiveDisplays() override;
    void showParts();

    // Each module's own presets: Pr (its menu, with saving your own) and INIT (its defaults)
    struct ModulePresets
    {
        PresetBrowser browser;   // (not shown: it builds the menu and keeps your own presets for this module)
        juce::TextButton pr { "Pr" }, init { "INIT" };
        juce::Label name;
        juce::StringArray ids;
    };
    std::array<ModulePresets, MasterModules::count> modulePresets;
    void applyModulePreset (int module, const Preset* preset);   // nullptr = INIT
    void showModuleMenu (int module);
    void placeModuleButtons (int module, juce::Rectangle<int> panel);
public:
    ModulePresets& getModulePresets (int module)   { return modulePresets[(size_t) module]; }
private:
    void mouseDown (const juce::MouseEvent&) override;
    std::vector<juce::Component*> pageComponents (int page);

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    EasyMasterAudioProcessor& masterProcessor;

    std::array<Knob, numKnobs> knobs;
    juce::TextButton initAllButton { "INIT" };
    LightButton compOnButton { "ON", Palette::meterLow };
    std::unique_ptr<juce::ButtonParameterAttachment> compOnAttachment;
    ChoiceSwitch modeSwitch, reactsSwitch, characterSwitch, releaseSwitch, listenSwitch;
    LightButton matchButton { "MATCH", Palette::accent };
    std::unique_ptr<juce::ButtonParameterAttachment> matchAttachment;
    LightButton slotA { "A", Palette::accent }, slotB { "B", Palette::accent };
    juce::TextButton copyButton { "COPY" };
    juce::Label matchInfo, inputWarning, listenLabel, abLabel;
    MasterDisplay display;
    TamerDisplay tamerDisplay;
    EQDisplay eqDisplay { easyProcessor.parameters };
    MultibandDisplay mbDisplay { easyProcessor.parameters };
    std::array<std::array<Knob, 16>, 2> mbKnobs;
    std::array<juce::Slider, 3> mbSplits;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 3> mbSplitAttachments;
    juce::Label mbSplitLabel;
    std::array<LightButton, 4> mbBandOn { LightButton { "ON", Palette::meterLow }, LightButton { "ON", Palette::meterLow }, LightButton { "ON", Palette::meterLow }, LightButton { "ON", Palette::meterLow } };
    std::array<LightButton, 4> mbSolo { LightButton { "SOLO", Palette::meterMid }, LightButton { "SOLO", Palette::meterMid }, LightButton { "SOLO", Palette::meterMid }, LightButton { "SOLO", Palette::meterMid } };
    std::array<std::unique_ptr<juce::ButtonParameterAttachment>, 4> mbBandOnAttachments, mbSoloAttachments;
    LightButton mbOnButton { "ON", Palette::meterLow };
    std::unique_ptr<juce::ButtonParameterAttachment> mbOnAttachment;
    ChoiceSwitch mbModeSwitch;
    juce::Rectangle<int> mbMidName, mbSideName;
    std::array<juce::Rectangle<int>, 4> mbBandNames;
    bool shownMBMidSide = false, shownMBOn = false;
    // Each page's groups: the knobs that work together share a panel of their own tone, alternating (like a console's modules)
    std::array<std::vector<juce::Rectangle<int>>, numPages> groupTiles;
    std::array<std::array<Knob, 11>, 2> eqKnobs;
    LightButton eqOnButton { "ON", Palette::meterLow };
    std::unique_ptr<juce::ButtonParameterAttachment> eqOnAttachment;
    ChoiceSwitch eqModeSwitch;
    juce::Rectangle<int> eqMidName, eqSideName;
    std::array<juce::Rectangle<int>, 6> eqBandNames;   // the captions over each band's pair of knobs (and LOW CUT)
    bool shownEQMidSide = false, shownEQOn = true;
    LightButton tamerOnButton { "ON", Palette::meterLow }, tamerListenButton { "LISTEN: REMOVED ONLY", Palette::meterMid };
    std::unique_ptr<juce::ButtonParameterAttachment> tamerOnAttachment, tamerListenAttachment;
    ChoiceSwitch tamerModeSwitch;
    juce::Rectangle<int> tamerMidName, tamerSideName, pagePanel, pageDisplayPanel;
    std::array<juce::Rectangle<float>, 9> chainBoxes;
    int shownPage = compPage;
    bool shownTamerMidSide = false, shownTamerOn = true;
    MasterMeters meters;
    std::vector<EasyMasterAudioProcessor::Moment> incoming;
    bool shownMidSide = false, shownCompOn = true;

    juce::Rectangle<int> midRowName, sideRowName, chainArea, metersPanel, inputPanel, limiterPanel, comparePanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterAudioProcessorEditor)
};

#pragma once

#include "PluginProcessor.h"
#include "MasterLook.h"

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
/** The PUNCH page's display: the last six seconds - the master's level on
    top, and underneath how much PUNCH is turning up (the attacks, or longer
    tails: above the line) and down (softer attacks, shorter tails: below). */
class PunchDisplay : public juce::Component
{
public:
    PunchDisplay();
    void addMoments (const EasyMasterAudioProcessor::Moment*, int count);
    void setState (bool on, bool bypassed);
    void paint (juce::Graphics&) override;

private:
    std::vector<EasyMasterAudioProcessor::Moment> history;
    int next = 0;
    bool on = false, bypassed = false;
};

//==============================================================================
/** The WIDTH page's display: the stereo picture (the middle up and down, the
    sides left and right - a narrow upright shape is near mono, a wide round
    cloud is very wide), with how loud the sides are next to the middle, the
    correlation, and what WIDTH and MONO BASS do to the sides across the range. */
class WidthDisplay : public juce::Component
{
public:
    void addPoints (const float* left, const float* right, int count);
    void setState (const StereoWidth::Settings&, float sideToMidDb, float correlation, bool on, bool bypassed, double sampleRate);
    void paint (juce::Graphics&) override;
    static constexpr int numPoints = 2048;

private:
    std::array<float, numPoints> pointL {}, pointR {};
    int next = 0;
    float zoom = 1.0f, sideToMid = -100.0f, correlation = 1.0f;
    StereoWidth::Settings settings;
    bool on = false, bypassed = false;
    double rate = 48000.0;
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
namespace MasterHolo { class Console; }

/** The Easy Master window: the console (laid out at 1800 x 1030) at any size -
    drag the corner; the shape stays the same, and the size is remembered. */
class EasyMasterAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit EasyMasterAudioProcessorEditor (EasyMasterAudioProcessor&);
    ~EasyMasterAudioProcessorEditor() override;

    void resized() override;
    MasterHolo::Console& getConsole() noexcept   { return *console; }

private:
    EasyMasterAudioProcessor& masterProcessor;
    MasterLook look;   // (the menus, typing boxes and the displays' parts)
    std::unique_ptr<MasterHolo::Console> console;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EasyMasterAudioProcessorEditor)
};

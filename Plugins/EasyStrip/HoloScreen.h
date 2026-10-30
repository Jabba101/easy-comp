#pragma once

#include <juce_dsp/juce_dsp.h>
#include "HoloControls.h"
#include "PluginProcessor.h"

namespace Holo
{
    //==========================================================================
    /**
        The big screen in the middle. It shows ONE module at a time - whichever
        page button you pressed (it never switches by itself):

          GATE   what the gate hears, what comes out, the THRESHOLD line
          AMP    the amp's curve at this DRIVE, and the level matching
          EQ     the curve (MID cyan, SIDE magenta), drag the dots; the analyser
          DS     the whole sound, the harsh band, the line, how far it turns down
          COMP   the compression curve, and gain reduction over time
          TRANS  before and after
          SLAP   your sound and the burst
          CLIP   what would have been, what comes out, the CEILING, what's shaved

        The window feeds it what the modules measured, ~30 times a second.
    */
    class Screen : public juce::Component
    {
    public:
        explicit Screen (EasyStripAudioProcessor&);
        ~Screen() override;

        void setPage (int module);
        int getPage() const noexcept   { return page; }

        // One trace per module: up to three values per 5 ms moment
        void addMoment (int module, float a, float b, float c);
        void addCompFrame (float levelDb, float reductionDb);

        /** Called each frame: repaints what's moving (and reads the EQ's analyser). */
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

        /** (Tests) where an EQ band's dot is drawn. */
        juce::Point<float> eqDotPosition (int band) const;

        Button analyserButton { "ANALYSER", Colours::eq };

        static constexpr int historyLength = 480;   // 2.4 s of 5 ms moments

    private:
        struct Trace
        {
            std::vector<std::array<float, 3>> points = std::vector<std::array<float, 3>> (historyLength, { -200.0f, -200.0f, 0.0f });
            int next = 0;
            void add (float a, float b, float c)   { points[(size_t) next] = { a, b, c }; next = (next + 1) % historyLength; }
            const std::array<float, 3>& at (int age) const   { return points[(size_t) ((next + age) % historyLength)]; }   // 0 = oldest
        };

        juce::Rectangle<float> plotArea() const;
        void drawFrame (juce::Graphics&, const juce::String& caption);
        void drawLevelGrid (juce::Graphics&, juce::Rectangle<float>, float lowDb, float highDb, std::initializer_list<float> marks);
        juce::Path tracePath (const Trace&, int field, juce::Rectangle<float>, float lowDb, float highDb, bool closed) const;

        void paintEQ (juce::Graphics&);
        void paintGate (juce::Graphics&);
        void paintAmp (juce::Graphics&);
        void paintDS (juce::Graphics&);
        void paintComp (juce::Graphics&);
        void paintTrans (juce::Graphics&);
        void paintSlap (juce::Graphics&);
        void paintClip (juce::Graphics&);

        // EQ page
        float xForHz (float hz) const;
        float hzForX (float x) const;
        float yForDb (float db) const;
        float dbForY (float y) const;
        juce::Rectangle<float> eqPlot() const;
        int dotAt (juce::Point<float>) const;
        void readAnalyser();

        EasyStripAudioProcessor& strip;
        int page = 2;
        std::array<Trace, 8> traces;
        std::vector<std::array<float, 2>> compFrames = std::vector<std::array<float, 2>> (120, { -200.0f, 0.0f });
        int compNext = 0;
        float compLevelDb = -200.0f;

        // Dragging an EQ dot
        int dragging = -1, hovering = -1;
        juce::RangedAudioParameter* dragFreq = nullptr;
        juce::RangedAudioParameter* dragGain = nullptr;

        // The analyser
        static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> fftData, window, spectrumDb;
        std::unique_ptr<ButtonLink> analyserLink;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Screen)
    };
}

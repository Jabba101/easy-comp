#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "LoudnessMatcher.h"
#include <pffft.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

//==============================================================================
/**
    A frequency picture a sixth of an octave at a time (20 Hz - 18 kHz), the
    same way for the reference and for the master so the two can be laid
    over each other. Each band holds the TOTAL power in it, so pink noise
    (and most music) looks roughly flat.
*/
class BandSpectrum
{
public:
    static constexpr int fftSize = 4096, numBands = 60;

    static double centreHz (int band) noexcept   { return 20.0 * std::pow (2.0, band / 6.0); }

    void prepare (double sampleRate)
    {
        setup.reset (pffft_new_setup (fftSize, PFFFT_REAL));
        frame = aligned (fftSize); spectrum = aligned (fftSize); work = aligned (fftSize);
        window.resize ((size_t) fftSize);
        double windowPower = 0.0;
        for (int i = 0; i < fftSize; ++i)
        {
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) fftSize);
            windowPower += (double) window[(size_t) i] * window[(size_t) i];
        }
        scale = (float) (2.0 / (windowPower * fftSize));
        for (int b = 0; b < numBands; ++b)
        {
            const double lo = centreHz (b) * std::pow (2.0, -1.0 / 12.0), hi = centreHz (b) * std::pow (2.0, 1.0 / 12.0);
            int from = (int) std::ceil (lo * fftSize / sampleRate), to = (int) std::floor (hi * fftSize / sampleRate);
            from = juce::jlimit (1, fftSize / 2 - 1, from);
            to = juce::jlimit (from, fftSize / 2 - 1, to);   // (at least one bin, even down low)
            bins[(size_t) b] = { from, to };
        }
    }

    /** fftSize samples of one channel in, numBands powers out. */
    void analyse (const float* samples, float* bandPower) noexcept
    {
        for (int i = 0; i < fftSize; ++i) frame.get()[i] = samples[i] * window[(size_t) i];
        pffft_transform_ordered (setup.get(), frame.get(), spectrum.get(), work.get(), PFFFT_FORWARD);
        const float* s = spectrum.get();
        for (int b = 0; b < numBands; ++b)
        {
            double total = 0.0;
            const auto [from, to] = bins[(size_t) b];
            for (int k = from; k <= to; ++k) total += (double) s[2 * k] * s[2 * k] + (double) s[2 * k + 1] * s[2 * k + 1];
            // (a band too narrow for its bins, down low, counts as wide as it should be)
            const double binsWanted = juce::jmax (1.0, (centreHz (b) * (std::pow (2.0, 1.0 / 12.0) - std::pow (2.0, -1.0 / 12.0))) * fftSize / rate);
            bandPower[b] = (float) (total * scale * juce::jmax (1.0, binsWanted / (double) (to - from + 1)));
        }
    }

    void setRate (double r) noexcept   { rate = r; }

private:
    struct AlignedFree { void operator() (float* p) const noexcept { pffft_aligned_free (p); } };
    struct SetupFree   { void operator() (PFFFT_Setup* s) const noexcept { if (s != nullptr) pffft_destroy_setup (s); } };
    static std::unique_ptr<float, AlignedFree> aligned (int count)
    {
        return std::unique_ptr<float, AlignedFree> (static_cast<float*> (pffft_aligned_malloc ((size_t) count * sizeof (float))));
    }
    std::unique_ptr<PFFFT_Setup, SetupFree> setup;
    std::unique_ptr<float, AlignedFree> frame, spectrum, work;
    std::vector<float> window;
    std::array<std::pair<int, int>, numBands> bins {};
    float scale = 1.0f;
    double rate = 48000.0;
};

//==============================================================================
/**
    The REFERENCE: a finished song to compare the master with.

    LOAD reads the file (WAV, FLAC, MP3, AIFF, OGG) on its own thread,
    converts it to the session's sample rate and measures it: its loudness
    every 100 ms (for the overview and for matching) and its frequency
    picture every second (for the comparison page).

    While the DAW plays, the reference plays too - from START, looping the
    chosen part (or to the end of the song). The one-button A / B
    (setListening) fades between the master and the reference in 25 ms.
    MATCH turns the reference up or down so it's as loud as the master you
    hear (the last 20 seconds of it, against the chosen part of the
    reference), so the comparison is of the sound, not the volume. The
    reference never goes through the chain.
*/
class ReferenceTrack
{
public:
    enum Status { empty = 0, loading, ready, failed };
    static constexpr double maxSeconds = 15.0 * 60.0;
    static constexpr int blocksPerSecond = 10;          // loudness blocks: 100 ms

    /** The loaded song: the audio at the session's rate, and what was measured. */
    struct Song
    {
        juce::AudioBuffer<float> audio;                  // 2 channels
        std::vector<double> blockEnergy;                 // ear-weighted mean square per 100 ms (both channels added)
        std::vector<std::array<float, BandSpectrum::numBands>> secondBands;   // the mid's band powers, per second
        double rate = 48000.0;
        juce::String name;
    };

    ReferenceTrack() { formats.registerBasicFormats(); }
    ~ReferenceTrack() { stopLoader(); }

    //==== Setting up (audio thread stopped) ===================================
    void prepare (double sampleRate, int maxBlock)
    {
        const bool rateChanged = std::abs (sampleRate - rate) > 0.5;
        rate = sampleRate;
        ours.prepare (sampleRate, 100.0f);
        oursBlockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
        oursFilled = 0; oursSum = 0.0; oursCount = 0; oursPos = 0;
        oursBlocks.assign ((size_t) oursMaxBlocks, 0.0);
        listenFade.reset (sampleRate, 0.025);
        listenFade.setCurrentAndTargetValue (listening.load() ? 1.0f : 0.0f);
        playFade.reset (sampleRate, 0.01);
        playFade.setCurrentAndTargetValue (0.0f);
        gain.reset (sampleRate, 0.3);
        fadeLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.01));
        juce::ignoreUnused (maxBlock);
        if (rateChanged && file.existsAsFile()) load (file);   // (the song again, at the new rate)
    }

    //==== The window's side ====================================================
    /** Reads the song on its own thread (the old one keeps playing until the new one is ready). */
    void load (const juce::File& newFile)
    {
        stopLoader();
        file = newFile;
        status.store (loading);
        loader = std::make_unique<Loader> (*this, newFile, rate);
        loader->startThread (juce::Thread::Priority::low);
    }

    void clear()
    {
        stopLoader();
        file = juce::File();
        install (nullptr);
        status.store (empty);
        listening.store (false);
    }

    Status getStatus() const noexcept           { return (Status) status.load(); }
    const juce::File& getFile() const noexcept  { return file; }
    juce::String getError() const               { const juce::SpinLock::ScopedLockType l (lock); return error; }
    juce::String getName() const                { const juce::SpinLock::ScopedLockType l (lock); return song != nullptr ? song->name : juce::String(); }
    double getLengthSeconds() const             { const juce::SpinLock::ScopedLockType l (lock); return song != nullptr ? song->audio.getNumSamples() / song->rate : 0.0; }

    /** The part to compare: START to END in seconds (END at or before START: to the end of the song). */
    void setSection (double startSeconds, double endSeconds)
    {
        start.store (juce::jmax (0.0, startSeconds));
        end.store (endSeconds > startSeconds + 0.5 ? endSeconds : 0.0);
        updateSectionLoudness();
    }
    double getStart() const noexcept   { return start.load(); }
    double getEnd() const noexcept     { return end.load(); }      // 0 = to the end

    void setListening (bool shouldListen) noexcept   { listening.store (shouldListen && getStatus() == ready); }
    bool isListening() const noexcept                { return listening.load(); }
    void setMatching (bool shouldMatch) noexcept     { matching.store (shouldMatch); }
    bool isMatching() const noexcept                 { return matching.load(); }

    float getSectionLufs() const noexcept   { return sectionLufs.load(); }   // the chosen part's loudness (integrated)
    float getOurLufs() const noexcept       { return ourLufs.load(); }       // the master you hear, over the last 20 s
    float getGainDb() const noexcept        { return gainDb.load(); }        // what MATCH does to the reference
    double getPositionSeconds() const noexcept   { return position.load(); }
    bool isPlaying() const noexcept         { return hostPlaying.load(); }

    /** The reference's loudness every 100 ms (momentary: 400 ms), for the overview. */
    std::vector<float> getOverview() const
    {
        const juce::SpinLock::ScopedLockType l (lock);
        std::vector<float> out;
        if (song == nullptr) return out;
        const auto& e = song->blockEnergy;
        out.resize (e.size());
        for (size_t i = 0; i < e.size(); ++i)
        {
            double m = 0.0; int count = 0;
            for (size_t k = i >= 3 ? i - 3 : 0; k <= i; ++k) { m += e[k]; ++count; }
            out[i] = lufsOf (m / count);
        }
        return out;
    }

    /** The chosen part's frequency picture (dB per band, relative to its loudness: so a louder song isn't "more of everything"). */
    std::vector<float> getSectionSpectrum() const
    {
        const juce::SpinLock::ScopedLockType l (lock);
        std::vector<float> out;
        if (song == nullptr || song->secondBands.empty()) return out;
        const int seconds = (int) song->secondBands.size();
        int from = juce::jlimit (0, seconds - 1, (int) start.load());
        int to = end.load() > 0.0 ? juce::jlimit (from + 1, seconds, (int) std::ceil (end.load())) : seconds;
        std::array<double, BandSpectrum::numBands> total {};
        int used = 0;
        for (int s = from; s < to; ++s)
        {
            // (only seconds that aren't near-silent: a fade-out mustn't drag the picture down)
            const int b0 = s * blocksPerSecond, b1 = juce::jmin ((int) song->blockEnergy.size(), b0 + blocksPerSecond);
            double e = 0.0; for (int b = b0; b < b1; ++b) e += song->blockEnergy[(size_t) b];
            if (b1 <= b0 || lufsOf (e / (b1 - b0)) < sectionLufs.load() - 20.0f) continue;
            for (int b = 0; b < BandSpectrum::numBands; ++b) total[(size_t) b] += song->secondBands[(size_t) s][(size_t) b];
            ++used;
        }
        if (used == 0) return out;
        out.resize ((size_t) BandSpectrum::numBands);
        for (int b = 0; b < BandSpectrum::numBands; ++b)
            out[(size_t) b] = (float) (10.0 * std::log10 (total[(size_t) b] / used + 1.0e-20)) - sectionLufs.load();
        return out;
    }

    /** The master you hear (the middle), for the window's picture of it - only while the DAW plays. */
    int takeOurs (float* destination, int maxSamples) noexcept
    {
        const auto read = oursFifo.read (juce::jmin (maxSamples, oursFifo.getNumReady()));
        int n = 0;
        for (int i = 0; i < read.blockSize1; ++i) destination[n++] = oursSamples[(size_t) (read.startIndex1 + i)];
        for (int i = 0; i < read.blockSize2; ++i) destination[n++] = oursSamples[(size_t) (read.startIndex2 + i)];
        return n;
    }

    //==== The audio thread's side ==============================================
    /** Once per block, before render(): is the DAW playing? */
    void beginBlock (bool playing) noexcept
    {
        hostPlaying.store (playing);
        current = nullptr;
        if (lock.tryEnter()) { current = song.get(); lock.exit(); }
        listenFade.setTargetValue (listening.load() && current != nullptr ? 1.0f : 0.0f);
        playFade.setTargetValue (playing && current != nullptr ? 1.0f : 0.0f);

        // MATCH: the reference as loud as the master you hear
        float db = 0.0f;
        if (matching.load() && current != nullptr)
            db = (ourLufs.load() > -70.0f && sectionLufs.load() > -70.0f) ? juce::jlimit (-40.0f, 12.0f, ourLufs.load() - sectionLufs.load())
                                                                          : gainDb.load();
        gainDb.store (db);
        gain.setTargetValue (juce::Decibels::decibelsToGain (db));

        if (playing && ! wasPlaying) startPlaying = true;   // (every press of play starts at START)
        wasPlaying = playing;
    }

    /** The reference for the next n samples (silent when the DAW is stopped or nothing's loaded). */
    void render (float* left, float* right, int n) noexcept
    {
        if (current == nullptr || current->audio.getNumSamples() < 2 * fadeLength + 2)
        {
            std::fill (left, left + n, 0.0f); std::fill (right, right + n, 0.0f);
            playFade.skip (n); gain.skip (n);
            return;
        }
        const int length = current->audio.getNumSamples();
        const int64_t from = juce::jlimit<int64_t> (0, length - 1, (int64_t) (start.load() * rate));
        int64_t to = end.load() > 0.0 ? juce::jlimit<int64_t> (from + 2 * fadeLength + 1, length, (int64_t) (end.load() * rate)) : length;
        if (to - from < 2 * fadeLength + 1) to = juce::jmin<int64_t> (length, from + 2 * fadeLength + 1);
        if (startPlaying || pos < from || pos >= to) { pos = from; startPlaying = false; }

        const float* srcL = current->audio.getReadPointer (0);
        const float* srcR = current->audio.getReadPointer (1);
        const int64_t fadeFrom = to - fadeLength;
        for (int i = 0; i < n; ++i)
        {
            const float p = playFade.getNextValue(), g = gain.getNextValue() * p;
            if (p <= 0.0f && ! hostPlaying.load()) { left[i] = right[i] = 0.0f; continue; }
            float l = srcL[pos], r = srcR[pos];
            if (pos >= fadeFrom)   // (the loop's seam - or the song's end - fades into START: no click)
            {
                const int64_t back = from + (pos - fadeFrom);
                const float w = (float) (pos - fadeFrom) / (float) fadeLength;
                if (back < length) { l = l * (1.0f - w) + srcL[back] * w; r = r * (1.0f - w) + srcR[back] * w; }
            }
            left[i] = l * g; right[i] = r * g;
            if (++pos >= to) pos = from + fadeLength;   // (it carries on through the 10 ms fade when the DAW stops)
        }
        position.store ((double) pos / rate);
    }

    /** How much of the reference to hear (0 = the master, 1 = the reference), sample by sample. */
    float nextListen() noexcept   { return listenFade.getNextValue(); }
    bool isFadeActive() const noexcept   { return listenFade.isSmoothing() || listenFade.getCurrentValue() > 0.0f; }
    void skipListen (int n) noexcept     { listenFade.skip (n); }

    /** The master as you hear it (after MATCH / BYPASS): measured for MATCH and the picture, while the DAW plays. */
    void measureOurs (const float* left, const float* right, int n, bool playing) noexcept
    {
        if (! playing) return;
        const auto write = oursFifo.write (juce::jmin (n, oursFifo.getFreeSpace()));
        int at = 0;
        for (int i = 0; i < write.blockSize1; ++i, ++at) oursSamples[(size_t) (write.startIndex1 + i)] = 0.5f * (left[at] + right[at]);
        for (int i = 0; i < write.blockSize2; ++i, ++at) oursSamples[(size_t) (write.startIndex2 + i)] = 0.5f * (left[at] + right[at]);
        for (int i = 0; i < n; ++i)
        {
            oursSum += ours.measure (0, left[i]) + ours.measure (1, right[i]);
            if (++oursFilled >= oursBlockLength)
            {
                oursBlocks[(size_t) oursPos] = oursSum / oursBlockLength;
                oursPos = (oursPos + 1) % oursMaxBlocks;
                oursCount = juce::jmin (oursCount + 1, oursMaxBlocks);
                oursSum = 0.0; oursFilled = 0;
                // (every 100 ms: the last 20 s, gated like integrated LUFS)
                double windows[oursMaxBlocks]; int count = 0;
                for (int k = 0; k + 3 < oursCount; ++k)
                {
                    double m = 0.0;
                    for (int j = 0; j < 4; ++j) m += oursBlocks[(size_t) ((oursPos - 1 - k - j + 2 * oursMaxBlocks) % oursMaxBlocks)];
                    windows[count++] = m * 0.25;
                }
                const float l = gated (windows, count);
                if (l > -70.0f) ourLufs.store (l);
            }
        }
    }

    static float lufsOf (double meanSquare) noexcept
    {
        return meanSquare > 1.0e-20 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : -200.0f;
    }

    /** Integrated loudness of a run of 400 ms mean squares: -70 LUFS, then 10 LU below the rest, ignored. */
    static float gated (const double* windows, int count) noexcept
    {
        double e = 0.0; int c = 0;
        for (int i = 0; i < count; ++i) if (lufsOf (windows[i]) > -70.0f) { e += windows[i]; ++c; }
        if (c == 0) return -200.0f;
        const float gate = lufsOf (e / c) - 10.0f;
        e = 0.0; c = 0;
        for (int i = 0; i < count; ++i) if (lufsOf (windows[i]) > gate) { e += windows[i]; ++c; }
        return c > 0 ? lufsOf (e / c) : -200.0f;
    }

private:
    //==== Reading and measuring the song =======================================
    class Loader : public juce::Thread
    {
    public:
        Loader (ReferenceTrack& o, juce::File f, double r) : juce::Thread ("Easy Master reference"), owner (o), source (std::move (f)), rate (r) {}

        void run() override
        {
            auto song = std::make_shared<Song>();
            const juce::String problem = read (*song);
            if (threadShouldExit()) return;
            if (problem.isNotEmpty()) { owner.loadFailed (problem); return; }
            owner.install (std::move (song));
        }

    private:
        juce::String read (Song& song)
        {
            std::unique_ptr<juce::AudioFormatReader> reader (owner.formats.createReaderFor (source));
            if (reader == nullptr) return "Can't read " + source.getFileName() + " (WAV, FLAC, MP3, AIFF or OGG).";
            if (reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0) return source.getFileName() + " is empty.";
            if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds) return source.getFileName() + " is longer than 15 minutes.";

            const int inLength = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> in (2, inLength + 64);
            in.clear();
            reader->read (&in, 0, inLength, 0, true, reader->numChannels > 1);
            if (reader->numChannels == 1) in.copyFrom (1, 0, in, 0, 0, inLength);   // (a mono song: in both speakers)
            if (threadShouldExit()) return {};

            // To the session's rate (a windowed-sinc resampler: clean)
            const double ratio = reader->sampleRate / rate;
            const int outLength = juce::jmax (1, (int) std::floor ((inLength - 1) / ratio));
            song.audio.setSize (2, outLength);
            for (int c = 0; c < 2; ++c)
            {
                if (std::abs (ratio - 1.0) < 1.0e-9) { song.audio.copyFrom (c, 0, in, c, 0, outLength); continue; }
                juce::WindowedSincInterpolator interpolator;
                interpolator.process (ratio, in.getReadPointer (c), song.audio.getWritePointer (c), outLength);
            }
            song.rate = rate;
            song.name = source.getFileNameWithoutExtension();
            if (threadShouldExit()) return {};

            // Its loudness every 100 ms
            LoudnessMatcher ears; ears.prepare (rate, 100.0f);
            const int block = juce::jmax (1, juce::roundToInt (rate * 0.1));
            const float* l = song.audio.getReadPointer (0);
            const float* r = song.audio.getReadPointer (1);
            for (int b = 0; b + block <= outLength; b += block)
            {
                double sum = 0.0;
                for (int i = b; i < b + block; ++i) sum += ears.measure (0, l[i]) + ears.measure (1, r[i]);
                song.blockEnergy.push_back (sum / block);
            }
            if (threadShouldExit()) return {};

            // Its frequency picture every second (the middle; frames every half a window)
            BandSpectrum spectrum; spectrum.prepare (rate); spectrum.setRate (rate);
            const int seconds = juce::jmax (1, (int) std::ceil (outLength / rate));
            song.secondBands.assign ((size_t) seconds, {});
            std::vector<int> frames ((size_t) seconds, 0);
            std::vector<float> mono ((size_t) BandSpectrum::fftSize);
            std::array<float, BandSpectrum::numBands> bands {};
            for (int f = 0; f + BandSpectrum::fftSize <= outLength; f += BandSpectrum::fftSize / 2)
            {
                for (int i = 0; i < BandSpectrum::fftSize; ++i) mono[(size_t) i] = 0.5f * (l[f + i] + r[f + i]);
                spectrum.analyse (mono.data(), bands.data());
                const int s = juce::jlimit (0, seconds - 1, (int) ((f + BandSpectrum::fftSize / 2) / rate));
                for (int b = 0; b < BandSpectrum::numBands; ++b) song.secondBands[(size_t) s][(size_t) b] += bands[(size_t) b];
                ++frames[(size_t) s];
                if (threadShouldExit()) return {};
            }
            for (int s = 0; s < seconds; ++s)
                if (frames[(size_t) s] > 0)
                    for (auto& v : song.secondBands[(size_t) s]) v /= (float) frames[(size_t) s];
            return {};
        }

        ReferenceTrack& owner;
        juce::File source;
        double rate;
    };

    void stopLoader()
    {
        if (loader != nullptr) { loader->signalThreadShouldExit(); loader->stopThread (5000); loader.reset(); }
    }

    void install (std::shared_ptr<Song> newSong)
    {
        {
            const juce::SpinLock::ScopedLockType l (lock);
            retired = std::move (song);   // (kept a while: the audio thread may still be finishing a block with it)
            song = std::move (newSong);
            error = {};
        }
        status.store (song != nullptr ? ready : empty);
        if (song == nullptr) listening.store (false);
        updateSectionLoudness();
    }

    void loadFailed (const juce::String& message)
    {
        const juce::SpinLock::ScopedLockType l (lock);
        error = message;
        status.store (song != nullptr ? (int) ready : (int) Status::failed);
    }

    void updateSectionLoudness()
    {
        const juce::SpinLock::ScopedLockType l (lock);
        if (song == nullptr) { sectionLufs.store (-200.0f); return; }
        const auto& e = song->blockEnergy;
        const int blocks = (int) e.size();
        const int from = juce::jlimit (0, juce::jmax (0, blocks - 1), (int) (start.load() * blocksPerSecond));
        const int to = end.load() > 0.0 ? juce::jlimit (from + 1, blocks, (int) (end.load() * blocksPerSecond)) : blocks;
        std::vector<double> windows;
        for (int b = from; b + 3 < to; ++b) windows.push_back ((e[(size_t) b] + e[(size_t) b + 1] + e[(size_t) b + 2] + e[(size_t) b + 3]) * 0.25);
        sectionLufs.store (gated (windows.data(), (int) windows.size()));
    }

    juce::AudioFormatManager formats;
    std::unique_ptr<Loader> loader;
    juce::File file;
    double rate = 48000.0;

    mutable juce::SpinLock lock;
    std::shared_ptr<Song> song, retired;
    juce::String error;
    std::atomic<int> status { empty };
    std::atomic<bool> listening { false }, matching { true }, hostPlaying { false };
    std::atomic<double> start { 0.0 }, end { 0.0 }, position { 0.0 };
    std::atomic<float> sectionLufs { -200.0f }, ourLufs { -200.0f }, gainDb { 0.0f };

    // (the audio thread's)
    const Song* current = nullptr;
    int64_t pos = 0;
    bool wasPlaying = false, startPlaying = true;
    int fadeLength = 480;
    juce::SmoothedValue<float> listenFade, playFade, gain { 1.0f };

    // The master as heard: loudness over the last 20 s, and samples for the picture
    static constexpr int oursMaxBlocks = 200;
    LoudnessMatcher ours;
    std::vector<double> oursBlocks;
    int oursBlockLength = 4800, oursFilled = 0, oursCount = 0, oursPos = 0;
    double oursSum = 0.0;
    juce::AbstractFifo oursFifo { 32768 };
    std::array<float, 32768> oursSamples {};
};

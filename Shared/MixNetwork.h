#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "LoudnessMatcher.h"
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <vector>

//==============================================================================
/**
    THE MIX: every Easy Strip in a project, seeing the others.

    The strips all run inside the same DAW, so they can find each other
    here (nothing leaves the computer, nothing touches other projects:
    only strips loaded side by side in this session). Each one tells the
    network its track's name and ROLE (kick, bass, lead vocal...), and,
    while BALANCE listens, how loud it is.

    BALANCE: press it, play a busy part of the song (a chorus is ideal),
    press it again (or let it finish after 30 seconds of playing). Each
    track's loudness on its busy parts is compared with where its role
    usually sits in a mix of that STYLE, and each strip's LEVEL is set so
    the tracks land there - with the whole mix around -18 LUFS, leaving
    the master bus plenty of room for Easy Master. A starting point to
    tweak, not a finished mix. UNDO puts every LEVEL back.
*/
namespace Mix
{
    //==========================================================================
    enum Role
    {
        automatic = -1,
        kick = 0, snare, toms, overheads, room, hihat, percussion, drumsAll,
        bass, rhythmGuitar, leadGuitar, keys, synthLead, strings,
        leadVocal, backingVocals, fx, leaveAlone,
        numRoles
    };

    inline const char* roleName (int r)
    {
        static const char* names[numRoles] { "Kick", "Snare", "Toms", "Overheads", "Room mics", "Hi-hat", "Percussion", "Drums (all in one)",
                                             "Bass", "Rhythm guitar", "Lead guitar", "Keys & pads", "Synth lead & arps", "Strings & brass",
                                             "Lead vocal", "Backing vocals", "Effects", "Leave alone (buses, sends)" };
        return r >= 0 && r < numRoles ? names[r] : "Automatic";
    }

    enum Style { rock = 0, heavy, synthwave, jazz, numStyles };
    inline const char* styleName (int s)
    {
        static const char* names[numStyles] { "ROCK", "PUNK & METAL", "SYNTHWAVE", "JAZZ" };
        return s >= 0 && s < numStyles ? names[s] : "ROCK";
    }

    /** Where each role sits in a mix of each style: its loudness (all the tracks of that role together),
        in dB against the lead vocal. Rules of thumb from how such records are usually balanced - a start, not a law. */
    inline float target (int style, int role)
    {
        //                                          kick  snare toms   OH   room  hat  perc  drums bass  rhyG  leadG keys  synL strng  vox  bvox  fx   (leave)
        static const float t[numStyles][numRoles] { { -3.0f, -2.0f, -6.0f, -8.0f, -11.0f, -13.0f, -10.0f, -1.0f, -3.0f, -2.0f, -2.0f, -8.0f, -4.0f, -8.0f, 0.0f, -6.0f, -10.0f, 0.0f },   // rock
                                                    { -1.5f, -1.5f, -5.0f, -8.0f, -10.0f, -14.0f, -12.0f,  0.0f, -3.0f,  0.0f, -1.0f, -10.0f, -5.0f, -9.0f, 0.0f, -6.0f, -11.0f, 0.0f },  // punk & metal
                                                    { -3.0f, -2.5f, -7.0f, -12.0f, -10.0f, -11.0f, -9.0f, -1.0f, -3.0f, -6.0f, -3.0f, -4.0f, -1.0f, -6.0f, 0.0f, -6.0f, -8.0f, 0.0f },   // synthwave
                                                    { -9.0f, -8.0f, -10.0f, -4.0f, -8.0f, -10.0f, -9.0f, -4.0f, -2.0f, -4.0f, -2.0f, -3.0f, -4.0f, -4.0f, 0.0f, -7.0f, -12.0f, 0.0f } }; // jazz
        return t[juce::jlimit (0, numStyles - 1, style)][juce::jlimit (0, numRoles - 1, role)];
    }

    /** The whole rough mix lands about here (LUFS): room for the master bus and Easy Master. */
    static constexpr float mixTargetLufs = -18.0f;

    /** A good guess at a track's role, from its name in the DAW and the strip's preset. */
    inline int guessRole (const juce::String& trackName, const juce::String& presetName)
    {
        const auto has = [] (const juce::String& text, std::initializer_list<const char*> words)
        {
            const auto t = " " + text.toLowerCase().replaceCharacters ("_-.()[]/", "        ") + " ";
            for (auto* w : words)
                if (t.contains (juce::String (" ") + w)) return true;
            return false;
        };
        const auto fromText = [&] (const juce::String& n) -> int
        {
            if (n.trim().isEmpty()) return automatic;
            if (has (n, { "bus", "group", "send", "aux", "master", "mix bus", "fx return", "reverb", "verb ", "delay " })) return leaveAlone;
            if (has (n, { "kick", "bd ", "bassdrum", "bass drum", "kik" })) return kick;
            if (has (n, { "snare", "snr", "sn ", "sd " })) return snare;
            if (has (n, { "tom", "floor" })) return toms;
            if (has (n, { "overhead", "oh ", "ohs", "cymbal" })) return overheads;
            if (has (n, { "room", "amb" })) return room;
            if (has (n, { "hat", "hh ", "hihat", "hi-hat" })) return hihat;
            if (has (n, { "perc", "shaker", "tamb", "conga", "bongo", "clap", "cowbell" })) return percussion;
            if (has (n, { "drums", "drum machine", "kit", "beat", "loop" })) return drumsAll;
            if (has (n, { "bass", "sub", "808" })) return bass;
            if (has (n, { "backing", "bv", "bvs", "bgv", "harmony", "harm", "choir", "double", "dbl" })) return backingVocals;
            if (has (n, { "vox", "vocal", "voc", "voice", "lead vox", "singer", "rap", "speech" })) return leadVocal;
            if (has (n, { "lead gtr", "solo", "lead guitar" }) || (has (n, { "lead" }) && has (n, { "gtr", "guitar" }))) return leadGuitar;
            if (has (n, { "gtr", "guitar", "acoustic", "rhythm", "crunch", "riff" })) return rhythmGuitar;
            if (has (n, { "synth lead", "lead synth", "arp", "pluck", "lead" })) return synthLead;
            if (has (n, { "string", "brass", "horn", "violin", "cello", "sax", "trumpet" })) return strings;
            if (has (n, { "key", "piano", "organ", "pad", "rhodes", "synth", "ep " })) return keys;
            if (has (n, { "fx", "sfx", "riser", "impact", "noise" })) return fx;
            return automatic;
        };
        const int byName = fromText (trackName);
        return byName != automatic ? byName : fromText (presetName);
    }

    //==========================================================================
    /** How loud a track is on its busy parts (ear-weighted, 400 ms at a time, quiet gaps left out). */
    class Listener
    {
    public:
        void prepare (double sampleRate)
        {
            ears.prepare (sampleRate, 100.0f);
            blockLength = juce::jmax (1, juce::roundToInt (sampleRate * 0.1));
            reset();
        }

        void reset() noexcept
        {
            filled = count = 0; sum = 0.0; blocks.fill (0.0);
            windowCount.store (0); soundBlocks.store (0);
        }

        void process (const float* const* channels, int numChannels, int numSamples) noexcept
        {
            for (int i = 0; i < numSamples; ++i)
            {
                for (int c = 0; c < juce::jmin (2, numChannels); ++c) sum += ears.measure (c, channels[c][i]);
                if (++filled >= blockLength)
                {
                    blocks[(size_t) (count % 4)] = sum / blockLength;
                    if (blocks[(size_t) (count % 4)] > silentEnergy) soundBlocks.store (soundBlocks.load() + 1);
                    sum = 0.0; filled = 0; ++count;
                    if (count >= 4 && count - 4 < maxWindows)
                    {
                        windows[(size_t) (count - 4)] = 0.25 * (blocks[0] + blocks[1] + blocks[2] + blocks[3]);
                        windowCount.store (count - 3);
                    }
                }
            }
        }

        double soundSeconds() const noexcept   { return soundBlocks.load() * 0.1; }

        /** LUFS on the busy parts: above -70, and no more than 20 LU under their average. */
        float busyLufs() const noexcept
        {
            const int n = windowCount.load();
            double e = 0.0; int c = 0;
            for (int i = 0; i < n; ++i) if (windows[(size_t) i] > silentEnergy) { e += windows[(size_t) i]; ++c; }
            if (c == 0) return -200.0f;
            const double gate = e / c * 0.01;
            e = 0.0; c = 0;
            for (int i = 0; i < n; ++i) if (windows[(size_t) i] > juce::jmax (gate, silentEnergy)) { e += windows[(size_t) i]; ++c; }
            return c > 0 ? (float) (-0.691 + 10.0 * std::log10 (e / c)) : -200.0f;
        }

    private:
        static constexpr int maxWindows = 3000;
        static constexpr double silentEnergy = 1.0e-7;   // (-70 LUFS)
        LoudnessMatcher ears;
        int blockLength = 4800, filled = 0, count = 0;
        double sum = 0.0;
        std::array<double, 4> blocks {};
        std::array<double, maxWindows> windows {};
        std::atomic<int> windowCount { 0 }, soundBlocks { 0 };
    };

    //==========================================================================
    /** What a strip shows the network (all called on the message thread, except where it says). */
    class Member
    {
    public:
        virtual ~Member() = default;
        virtual juce::String getMixName() const = 0;      // the track's name (or "Easy Strip")
        virtual juce::String getMixPreset() const = 0;    // the strip's preset (for the role guess)
        virtual int getChosenRole() const = 0;            // automatic, or one chosen
        virtual void setChosenRole (int) = 0;
        virtual float getMixLevelDb() const = 0;          // the strip's LEVEL
        virtual void setMixLevelDb (float) = 0;
        virtual float getMixTrimDb() const = 0;
        virtual void startMixLearn() = 0;                 // LEARN ALL
        virtual int getMixDesk() const = 0;               // DESK (ConsoleColour's numbers)
        virtual int getMixDeskDrive() const = 0;
        virtual void setMixDesk (int desk, int drive) = 0;
        virtual bool isMixLearning() const = 0;
        virtual Listener& getMixListener() = 0;           // (filled by its audio thread while BALANCE listens)
        virtual float getLevelWhileListening() const = 0; // the LEVEL the listener heard it at

        int getRole() const   { const int r = getChosenRole(); return r != automatic ? r : guessRole (getMixName(), getMixPreset()); }
        bool isRoleGuessed() const   { return getChosenRole() == automatic; }
    };

    //==========================================================================
    class Network
    {
    public:
        static Network& get()   { static Network network; return network; }

        struct Change { Member* member; juce::String name; int role; float heardLufs, oldLevel, newLevel; bool heard, limited; };

        void add (Member* m)    { const std::lock_guard<std::mutex> l (lock); members.push_back (m); }
        void remove (Member* m)
        {
            const std::lock_guard<std::mutex> l (lock);
            members.erase (std::remove (members.begin(), members.end(), m), members.end());
            undoLevels.erase (m);
            lastChanges.erase (std::remove_if (lastChanges.begin(), lastChanges.end(), [m] (const Change& c) { return c.member == m; }), lastChanges.end());
        }
        std::vector<Member*> getMembers() const   { const std::lock_guard<std::mutex> l (lock); return members; }

        //==== BALANCE ===========================================================
        /** Every strip starts listening afresh (their audio threads see the new round). */
        void startListening()   { round.fetch_add (1); playedBlocks.store (0); listening.store (true); const std::lock_guard<std::mutex> l (lock); lastChanges.clear(); }
        void stopListening()    { listening.store (false); }
        bool isListening() const noexcept   { return listening.load(); }
        int getRound() const noexcept       { return round.load(); }
        double playedSeconds() const noexcept   { return playedBlocks.load() * 0.1; }

        /** A strip's audio thread, each block while the DAW plays: the longest-playing strip's count is the clock. */
        void notePlayed (double seconds) noexcept
        {
            const int blocks = (int) (seconds * 10.0);
            int current = playedBlocks.load();
            while (blocks > current && ! playedBlocks.compare_exchange_weak (current, blocks)) {}
        }
        static constexpr double listenSeconds = 30.0;


        /** Works out every strip's LEVEL from what was heard (and sets them unless `dryRun`). Message thread. */
        std::vector<Change> balance (bool dryRun = false)
        {
            listening.store (false);
            std::vector<Change> changes;
            const auto all = getMembers();

            // Each role's tracks, and how loud each was before its LEVEL
            std::map<int, int> perRole;
            for (auto* m : all)
            {
                Change c { m, m->getMixName(), m->getRole(), m->getMixListener().busyLufs(), m->getMixLevelDb(), m->getMixLevelDb(), false, false };
                c.heard = c.role != leaveAlone && m->getMixListener().soundSeconds() >= 1.0 && c.heardLufs > -70.0f;
                if (c.heard) ++perRole[c.role];
                changes.push_back (c);
            }
            // Where each lands (before the whole mix is set): its role's place, shared between the role's tracks
            double total = 0.0;
            for (auto& c : changes)
                if (c.heard)
                {
                    c.newLevel = target (style.load(), c.role) - 10.0f * std::log10 ((float) perRole[c.role]);   // (relative, for now)
                    total += std::pow (10.0, c.newLevel / 10.0);
                }
            if (total <= 0.0) return changes;
            const float shift = mixTargetLufs - (float) (10.0 * std::log10 (total));
            for (auto& c : changes)
            {
                if (! c.heard) continue;
                const float before = c.heardLufs - c.member->getLevelWhileListening();   // (how loud it is with LEVEL at 0)
                const float wanted = c.newLevel + shift - before;
                c.newLevel = juce::jlimit (-24.0f, 24.0f, std::round (wanted * 10.0f) / 10.0f);
                c.limited = std::abs (c.newLevel - wanted) > 0.1f;
            }
            if (! dryRun)
            {
                const std::lock_guard<std::mutex> l (lock);
                undoLevels.clear();
                for (auto& c : changes) if (c.heard) undoLevels[c.member] = c.oldLevel;
                lastChanges = changes;
            }
            if (! dryRun)
                for (auto& c : changes) if (c.heard) c.member->setMixLevelDb (c.newLevel);
            return changes;
        }

        bool canUndo() const   { const std::lock_guard<std::mutex> l (lock); return ! undoLevels.empty(); }
        void undo()
        {
            std::map<Member*, float> levels;
            { const std::lock_guard<std::mutex> l (lock); levels.swap (undoLevels); lastChanges.clear(); }
            for (auto& [m, level] : levels) m->setMixLevelDb (level);
        }

        /** What the last BALANCE did (for the MIX page). */
        std::vector<Change> getLastChanges() const   { const std::lock_guard<std::mutex> l (lock); return lastChanges; }

        /** One desk for every track (buses and sends left alone): the summing colour. */
        void setDeskForAll (int desk, int drive)   { for (auto* m : getMembers()) if (m->getRole() != leaveAlone) m->setMixDesk (desk, drive); }

        void learnAll()   { for (auto* m : getMembers()) if (m->getRole() != leaveAlone) m->startMixLearn(); }

        std::atomic<int> style { rock };

    private:
        Network() = default;
        mutable std::mutex lock;
        std::vector<Member*> members;
        std::map<Member*, float> undoLevels;
        std::vector<Change> lastChanges;
        std::atomic<int> round { 0 }, playedBlocks { 0 };
        std::atomic<bool> listening { false };
    };
}

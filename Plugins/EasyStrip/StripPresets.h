#pragma once

#include <vector>

//==============================================================================
/**
    The Easy Strip's built-in presets. Each one is a recipe: which modules are
    switched on, and which of THEIR OWN presets each one uses (by name - the
    same presets as the standalone plugins, so every module keeps its tuned
    settings). Modules a recipe doesn't mention are switched off.

    Gates and thresholds depend on how loud your recording is: the tips say
    which knob to check first.
*/
namespace StripPresetRecipes
{
    enum Module { gate = 0, amp, eq, ds, comp, trans, slap, clip };

    struct Use { Module module; const char* preset; };

    struct Recipe
    {
        const char* category;
        const char* name;
        const char* tip;
        std::vector<Use> uses;
    };

    inline const std::vector<Recipe>& all()
    {
        static const std::vector<Recipe> recipes
        {
            //==== Kick ========================================================
            { "Drums", "Kick - Rock",
              "Gate, EQ, compressor, more click, clipped peaks. Check the GATE's THRESHOLD first: it should open on every kick, not on the snare.",
              { { gate, "Kick - Tight" }, { eq, "Kick - Punch & Click" }, { comp, "Kick - Rock & Metal" }, { trans, "Kick - More Click" }, { clip, "Kick - Tight & Loud" } } },
            { "Drums", "Kick - Natural",
              "A gentle gate, punch and a little compression - keeps the drum's own sound. Check the GATE's THRESHOLD first.",
              { { gate, "Kick - Natural" }, { eq, "Kick - Punch & Click" }, { comp, "Kick - Punch" } } },
            { "Drums", "Kick - Modern & Hip-Hop",
              "Tight, deep and loud: a modern EQ, a big boom and the peaks shaved with the lows kept clean.",
              { { eq, "Kick - Tight & Modern" }, { comp, "Kick - Hip-Hop & R&B" }, { trans, "Kick - Bigger Boom" }, { clip, "Kick - Keep the Boom" } } },
            { "Drums", "Kick - Punk Thump",
              "Tight and slamming, with a short gated burst behind it. Check the GATE's THRESHOLD first.",
              { { gate, "Kick - Tight" }, { comp, "Kick - Rock & Metal" }, { trans, "Kick - Tighter" }, { slap, "Kick - Gated Thump" } } },

            //==== Snare =======================================================
            { "Drums", "Snare - Rock",
              "Fat and cracking, with the spikes shaved. Check the GATE's THRESHOLD first: open on every hit, closed on the kick and hats.",
              { { gate, "Snare - Natural" }, { eq, "Snare - Fat & Cracking" }, { comp, "Snare - Crack" }, { trans, "Snare - More Crack" }, { clip, "Snare - Shave the Spikes" } } },
            { "Drums", "Snare - 80s Gated",
              "The big 80s snare: a tight gate, a fat compressor and the gated reverb burst. Raise BITCH-SLAP's MIX for more.",
              { { gate, "Snare - Tight & Dry" }, { eq, "Snare - Fat & Cracking" }, { comp, "Snare - Fat" }, { slap, "Snare - 80s Power" } } },
            { "Drums", "Snare - Punk",
              "Dry, cracking and slapped, with a crunchy clip. Check the GATE's THRESHOLD first.",
              { { gate, "Snare - Tight & Dry" }, { comp, "Snare - Crack" }, { slap, "Snare - Punk Slap" }, { clip, "Snare - Crunch" } } },
            { "Drums", "Snare - Synthwave",
              "A fat snare inside a huge gated wall of reverb.",
              { { eq, "Snare - Fat & Cracking" }, { comp, "Snare - Fat" }, { slap, "Snare - Synthwave Wall" } } },
            { "Drums", "Snare - Darksynth",
              "Dirty, cracking and slammed: valve grit, a hard gated burst and a crunchy clip.",
              { { amp, "Snare - Dirty Crack" }, { comp, "Snare - Crack" }, { slap, "Snare - Darksynth Slam" }, { clip, "Snare - Crunch" } } },
            { "Drums", "Snare - Jazz & Funk",
              "Natural and lively: less ring, gentle compression, a softer attack.",
              { { eq, "Snare - Less Ring" }, { comp, "Snare - Jazz & Funk" }, { trans, "Snare - Softer" } } },
            { "Drums", "Snare - Vintage Thick",
              "Thick and warm: valve warmth into a vintage-style compressor, less ring.",
              { { amp, "Drum Bus - Valve Warmth" }, { eq, "Snare - Less Ring" }, { comp, "Snare - Vintage Thick" } } },

            //==== Toms ========================================================
            { "Drums", "Toms - Rock",
              "Big and clear with the bleed gated away, and louder fills. Check the GATE's THRESHOLD first: open on every tom hit, closed on the snare.",
              { { gate, "Toms - Tight & Clean" }, { eq, "Toms - Big & Clear" }, { comp, "Toms - Fuller" }, { trans, "Toms - Bigger" }, { clip, "Toms - Louder Fills" } } },
            { "Drums", "Toms - 80s Fills",
              "Gated, big and bursting: the 80s tom fill.",
              { { gate, "Toms - Tight & Clean" }, { eq, "Toms - Big & Clear" }, { comp, "Toms - Fuller" }, { slap, "Toms - Big 80s Fills" } } },
            { "Drums", "Toms - Natural",
              "A gentle gate and a little less ring - the toms' own sound, just cleaner.",
              { { gate, "Toms - Natural" }, { eq, "Toms - Big & Clear" }, { trans, "Toms - Less Ring" } } },

            //==== Cymbals and rooms ===========================================
            { "Drums", "Hi-Hat - Crisp & Even",
              "Less harshness, even level, crisper chick.",
              { { ds, "Hi-Hat - Too Bright" }, { comp, "Hi-Hat - Even" }, { trans, "Hi-Hat - Crisper" } } },
            { "Drums", "Overheads - Rock",
              "Cymbals, not kit: the lows thinned out, the harsh edge tamed, firm compression.",
              { { eq, "Overheads - Cymbals Not Kit" }, { ds, "Overheads - Harsh Cymbals" }, { comp, "Overheads - Rock" } } },
            { "Drums", "Overheads - Smooth",
              "Smooth and controlled, with less room.",
              { { eq, "Overheads - Cymbals Not Kit" }, { ds, "Overheads - Harsh Cymbals" }, { comp, "Overheads - Smooth" }, { trans, "Overheads - Less Room" } } },
            { "Drums", "Room Mics - Crushed",
              "Big, dark and destroyed: blend it under the close mics with your fader.",
              { { amp, "Room Mics - Crushed Valve" }, { eq, "Room Mics - Big & Dark" }, { comp, "Room Mics - Crushed" }, { clip, "Room Mics - Trashy" } } },
            { "Drums", "Room Mics - 80s Gated",
              "The room opens with each snare hit and slams shut. Check the GATE's THRESHOLD: it should open on the snare only.",
              { { gate, "Room Mics - 80s Gated Snare" }, { eq, "Room Mics - Big & Dark" }, { comp, "Room Mics - Crushed" } } },

            //==== Drum bus ====================================================
            { "Drum Bus", "Drum Bus - Rock & Pop",
              "Warmth, polish, glue and punch, with the peaks caught.",
              { { amp, "Drum Bus - Valve Warmth" }, { eq, "Drum Bus - Polish" }, { comp, "Drum Bus - Rock & Pop" }, { trans, "Drum Bus - Punchier" }, { clip, "Drum Bus - Glue the Peaks" } } },
            { "Drum Bus", "Drum Bus - Glue",
              "Just holds the kit together: polish, gentle glue, the peaks caught.",
              { { eq, "Drum Bus - Polish" }, { comp, "Drum Bus - Glue" }, { clip, "Drum Bus - Glue the Peaks" } } },
            { "Drum Bus", "Drum Bus - Punk Smash",
              "Dirty, smashed and roomy. Pull the COMP's MIX down if it's too much.",
              { { amp, "Drum Bus - Parallel Dirt" }, { comp, "Drum Bus - Parallel Smash" }, { slap, "Drum Bus - Punk Room" }, { clip, "Drum Bus - Smashed (Punk)" } } },
            { "Drum Bus", "Drum Bus - 80s",
              "Vintage glue and a gated burst over the whole kit.",
              { { eq, "Drum Bus - Polish" }, { comp, "Drum Bus - Vintage Glue" }, { slap, "Drum Bus - 80s Kit" } } },
            { "Drum Bus", "Drum Bus - Hip-Hop & EDM",
              "Hard-hitting: punchy, compressed and crushed in parallel.",
              { { comp, "Drum Bus - Hip-Hop & EDM" }, { trans, "Drum Bus - Punchier" }, { clip, "Drum Bus - Parallel Crush" } } },
            { "Drum Bus", "Drum Bus - Jazz & Acoustic",
              "Light glue and just a touch of space.",
              { { comp, "Drum Bus - Jazz & Acoustic" }, { slap, "Jazz Kit - Just a Touch" } } },
            { "Drum Bus", "Drum Machine - Cyberpunk",
              "Punchy low end and a hard gated hit on a drum machine.",
              { { comp, "Drum Bus - Punch (THRUST)" }, { slap, "Drum Machine - Cyberpunk Hit" }, { clip, "Drum Bus - Glue the Peaks" } } },

            //==== Bass ========================================================
            { "Bass", "Bass - Clean & Solid",
              "Clear, steady and even.",
              { { eq, "Bass - Clear & Solid" }, { comp, "Bass - Steady" }, { clip, "Bass - Even Out the Peaks" } } },
            { "Bass", "Bass - Rock",
              "Valve grit, room for the guitars, punchy and defined.",
              { { amp, "Bass - Rock Grit" }, { eq, "Bass - Under Distorted Guitars" }, { comp, "Bass - Rock & Punk" }, { trans, "Bass - More Definition" } } },
            { "Bass", "Bass - Overdriven Growl",
              "A Motorhead-style growl that still sits under the guitars.",
              { { amp, "Bass - Overdriven Growl" }, { eq, "Bass - Under Distorted Guitars" }, { comp, "Bass - Rock & Punk" }, { clip, "Bass - Growl" } } },
            { "Bass", "Bass - Metal",
              "Growl on top, clean solid lows underneath, hum gated between parts.",
              { { gate, "Bass - Amp Hum" }, { amp, "Bass - Growl Under Clean Lows" }, { comp, "Bass - Metal" }, { clip, "Bass - Even Out the Peaks" } } },
            { "Bass", "Bass - Funk & Slap",
              "Even slaps and pops without losing the bounce.",
              { { eq, "Bass - Clear & Solid" }, { comp, "Bass - Slap & Funk" }, { trans, "Bass - Softer Slaps" } } },
            { "Bass", "Bass - Vintage Warm",
              "Round and warm: valve warmth and a vintage compressor.",
              { { amp, "Bass - Valve Warmth" }, { eq, "Bass - Clear & Solid" }, { comp, "Bass - Vintage Thick" } } },
            { "Bass", "Upright Bass - Jazz & Folk",
              "Natural and even, with the notes defined.",
              { { comp, "Upright Bass - Jazz & Folk" }, { trans, "Bass - More Definition" } } },
            { "Bass", "Synth Bass & 808",
              "Deep, tight and knocking, with a little grit so it's heard on small speakers.",
              { { amp, "Synth Bass - Grit" }, { eq, "Synth Bass - Deep & Tight" }, { comp, "Synth Bass & 808" }, { trans, "808 & Synth Bass - More Knock" } } },
            { "Bass", "Bass - Ducked by the Kick",
              "Send the kick to the strip's sidechain: the bass dips on every kick, so they never fight.",
              { { eq, "Bass - Clear & Solid" }, { comp, "Bass - Ducked by the Kick" } } },

            //==== Guitars =====================================================
            { "Guitars", "Distorted Guitar - Rock",
              "Tightened, controlled and silent between parts. Check the GATE's THRESHOLD for the stops.",
              { { gate, "Distorted Guitar - Tight Stops" }, { eq, "Distorted Guitar - Tighten" }, { comp, "Electric Guitar - Distorted" }, { clip, "Distorted Guitar - Tame the Spikes" } } },
            { "Guitars", "Distorted Guitar - Metal Chugs",
              "Tight chugs and dead-silent stops.",
              { { gate, "Distorted Guitar - Tight Stops" }, { eq, "Distorted Guitar - Tighten" }, { comp, "Electric Guitar - Distorted" }, { trans, "Electric Guitar - Tighter Chugs" } } },
            { "Guitars", "Guitar - Classic Rock Crunch",
              "For a clean or DI guitar: the valve amp does the crunch.",
              { { amp, "Guitar - Classic Rock Crunch" }, { eq, "Distorted Guitar - Tighten" }, { comp, "Electric Guitar - Distorted" } } },
            { "Guitars", "Guitar - Hot Lead",
              "For a clean or DI guitar: a singing overdriven lead.",
              { { amp, "Guitar - Hot Lead" }, { comp, "Electric Guitar - Lead" } } },
            { "Guitars", "Clean Guitar - Sparkle",
              "Bright and even, with the hum gated between parts.",
              { { gate, "Clean Guitar - Hum Between Parts" }, { eq, "Clean Guitar - Sparkle" }, { comp, "Electric Guitar - Clean" } } },
            { "Guitars", "Clean Guitar - Funk",
              "Snappy, even and bright.",
              { { eq, "Clean Guitar - Sparkle" }, { comp, "Electric Guitar - Funk" }, { trans, "Electric Guitar - Funk Snap" } } },
            { "Guitars", "Acoustic Guitar - Strummed",
              "Less boom, fewer squeaks, even strums.",
              { { eq, "Acoustic Guitar - Less Boom" }, { ds, "Acoustic Guitar - String Squeaks" }, { comp, "Acoustic Guitar - Strummed" }, { trans, "Acoustic Guitar - Softer Strums" } } },
            { "Guitars", "Acoustic Guitar - Fingerpicked",
              "Less boom, even notes, a little more attack.",
              { { eq, "Acoustic Guitar - Less Boom" }, { comp, "Acoustic Guitar - Fingerpicked" }, { trans, "Acoustic Guitar - More Attack" } } },

            //==== Vocals ======================================================
            { "Vocals", "Vocal - Pop",
              "Clear, present, smooth S sounds, upfront. Check DS's THRESHOLD on the S sounds.",
              { { eq, "Vocal - Clear & Present" }, { ds, "Female Vocal" }, { comp, "Vocal - Pop & R&B" } } },
            { "Vocals", "Vocal - Rock",
              "Headphone bleed gated, present and firmly compressed, the loudest words caught.",
              { { gate, "Vocal - Remove Headphone Bleed" }, { eq, "Vocal - Clear & Present" }, { ds, "Male Vocal" }, { comp, "Vocal - Rock & Metal" }, { clip, "Vocal - Catch the Loud Words" } } },
            { "Vocals", "Vocal - Punk Shout",
              "Distorted, in-your-face and squashed.",
              { { amp, "Vocal - Distorted Shout" }, { eq, "Vocal - Less Boxy" }, { ds, "Male Vocal" }, { comp, "Vocal - Rock & Metal" } } },
            { "Vocals", "Vocal - 80s Shout",
              "A rock vocal with the 80s gated burst on it.",
              { { eq, "Vocal - Clear & Present" }, { ds, "Male Vocal" }, { comp, "Vocal - Rock & Metal" }, { slap, "Vocal - 80s Shout" } } },
            { "Vocals", "Vocal - Ballad",
              "Gentle and natural: less boxy, soft S sounds, smooth compression.",
              { { eq, "Vocal - Less Boxy" }, { ds, "Soft & Breathy Vocal" }, { comp, "Vocal - Ballad & Acoustic" } } },
            { "Vocals", "Vocal - Rap",
              "Upfront, consistent and clear, with every word heard.",
              { { eq, "Vocal - Clear & Present" }, { ds, "Rap & Fast Delivery" }, { comp, "Vocal - Rap & Hip-Hop" }, { clip, "Vocal - Catch the Loud Words" } } },
            { "Vocals", "Vocal - Jazz & Soul",
              "Warm and natural, the dynamics kept.",
              { { eq, "Vocal - Less Boxy" }, { ds, "Soft & Breathy Vocal" }, { comp, "Vocal - Jazz & Soul" } } },
            { "Vocals", "Vocal - Vintage Warm",
              "Valve warmth, less nasal, classic de-essing, a smooth vintage compressor.",
              { { amp, "Vocal - Valve Warmth" }, { eq, "Vocal - Less Nasal" }, { ds, "Classic (Whole Sound)" }, { comp, "Vocal - Vintage Smooth" } } },
            { "Vocals", "Vocal - Radio Telephone",
              "The telephone / radio effect, squashed.",
              { { amp, "Vocal - Valve Warmth" }, { eq, "Vocal - Radio Telephone" }, { comp, "Vocal - Rock & Metal" } } },
            { "Vocals", "Backing Vocals",
              "Tucked in behind the lead: less boxy, soft S sounds, compressed.",
              { { eq, "Vocal - Less Boxy" }, { ds, "Backing Vocals" }, { comp, "Backing Vocals - Tucked In" } } },
            { "Vocals", "Speech & Podcast",
              "Room noise gated, clear, even and safe. Check the GATE's THRESHOLD between sentences.",
              { { gate, "Speech - Room Noise" }, { eq, "Vocal - Less Boxy" }, { ds, "Speech & Podcasts" }, { comp, "Spoken Word & Podcast" }, { clip, "Vocal - Catch the Loud Words" } } },

            //==== Keys and synths =============================================
            { "Keys & Synths", "Piano - Pop & Rock",
              "Clear, even and defined.",
              { { eq, "Piano - Clear" }, { comp, "Piano - Pop & Rock" }, { trans, "Piano - More Definition" } } },
            { "Keys & Synths", "Piano - Ballad & Jazz",
              "Clear and gentle, with a softer attack.",
              { { eq, "Piano - Clear" }, { comp, "Piano - Ballad & Jazz" }, { trans, "Piano - Softer Attack" } } },
            { "Keys & Synths", "Electric Piano - Smooth",
              "Warm and smooth.",
              { { amp, "Synth Pad - Warmth" }, { eq, "Piano - Clear" }, { comp, "Electric Piano - Smooth" } } },
            { "Keys & Synths", "Organ - Rock",
              "Steady, with the valve amp just breaking up.",
              { { amp, "Guitar - Edge of Breakup" }, { comp, "Organ - Steady" } } },
            { "Keys & Synths", "Synth Lead - Synthwave",
              "Overdriven, upfront and biting.",
              { { amp, "Synth Lead - Overdrive" }, { comp, "Synth Lead - Upfront" }, { clip, "Synth Lead - Bite" } } },
            { "Keys & Synths", "Synth Pad - Out of the Way",
              "Warm and smooth, with room left for the vocal and the drums.",
              { { amp, "Synth Pad - Warmth" }, { eq, "Synth Pad - Out of the Way" }, { comp, "Synth Pad - Smooth" } } },
            { "Keys & Synths", "Synth Pad - Pumping",
              "Send the kick to the strip's sidechain: the pad pumps in time.",
              { { eq, "Synth Pad - Out of the Way" }, { comp, "Synth Pad - Pumping to the Kick" } } },
            { "Keys & Synths", "Synth Pluck & Arp",
              "Snappy and even.",
              { { comp, "Synth Pluck & Arp" }, { trans, "Synth Pluck - Snappier" } } },
            { "Keys & Synths", "Synth Bass - Darksynth",
              "Gritty, deep and growling.",
              { { amp, "Synth Bass - Grit" }, { eq, "Synth Bass - Deep & Tight" }, { comp, "Synth Bass & 808" }, { clip, "Bass - Growl" } } },
            { "Keys & Synths", "Synth Stab - Gated Burst",
              "Upfront with a gated burst on every stab.",
              { { comp, "Synth Lead - Upfront" }, { slap, "Synth Stab - Gated Burst" } } },
            { "Keys & Synths", "Clap - Synthwave",
              "Even, with the classic gated clap reverb.",
              { { comp, "Percussion - Even" }, { slap, "Clap - Synthwave" } } },

            //==== Strings, brass ==============================================
            { "Strings & Brass", "Strings - Section",
              "Even and smooth, a softer attack.",
              { { comp, "Strings - Section" }, { trans, "Strings - Softer Attack" } } },
            { "Strings & Brass", "Brass Section - Punchy",
              "Punchy and controlled.",
              { { comp, "Brass Section - Punchy" }, { trans, "Brass - More Punch" } } },
            { "Strings & Brass", "Horns - Solo",
              "Even and natural, for a solo trumpet or sax.",
              { { comp, "Horns - Solo Trumpet & Sax" } } },

            //==== Buses and master ============================================
            { "Mix Bus & Master", "Mix Bus - Pop & Rock Glue",
              "Gentle valve warmth, polish and glue on the whole mix.",
              { { amp, "Mix Bus - Gentle Valve Glue" }, { eq, "Mix Bus - Gentle Polish" }, { comp, "Mix Bus - Glue (Pop & Rock)" } } },
            { "Mix Bus & Master", "Mix Bus - Rock & Punk",
              "Glued, a little more punch, and a burst of size.",
              { { comp, "Mix Bus - Glue (Pop & Rock)" }, { trans, "Mix Bus - A Little More Punch" }, { slap, "Mix Bus - Rock & Punk Punch" } } },
            { "Mix Bus & Master", "Mix Bus - Hip-Hop & EDM",
              "Polished, glued and punchy.",
              { { eq, "Mix Bus - Gentle Polish" }, { comp, "Mix Bus - Hip-Hop & EDM" }, { trans, "Mix Bus - A Little More Punch" } } },
            { "Mix Bus & Master", "Mix Bus - Synthwave",
              "Wide, vintage-glued, with 80s space.",
              { { eq, "Mix Bus - Wider (Side Air)" }, { comp, "Mix Bus - Vintage Glue" }, { slap, "Mix Bus - 80s Space" } } },
            { "Mix Bus & Master", "Mix Bus - Jazz & Acoustic",
              "A light polish and gentle glue that keeps the dynamics.",
              { { eq, "Mix Bus - Gentle Polish" }, { comp, "Mix Bus - Jazz, Folk & Classical" } } },
            { "Mix Bus & Master", "Group Bus - Glue",
              "For a group of tracks (guitars, vocals, keys): holds them together.",
              { { comp, "Group Bus - Glue" } } },
            { "Mix Bus & Master", "Master - Light Touch",
              "Mono lows, air, a light touch of compression, a safety net at the ceiling. (Try the EQ's MASTER button here.)",
              { { eq, "Master - Mono Bass & Air" }, { comp, "Master - Light Touch" }, { clip, "Master - Gentle Safety Net" } } },
            { "Mix Bus & Master", "Master - Clean Loudness",
              "Louder without losing the punch: watch LUFS I on the OUTPUT.",
              { { eq, "Master - Mono Bass & Air" }, { comp, "Master - Light Touch" }, { clip, "Master - Clean Loudness" } } },
            { "Mix Bus & Master", "Master - Loud Rock & Punk",
              "Loud and aggressive. Watch LUFS I on the OUTPUT.",
              { { eq, "Master - Mono Bass & Air" }, { comp, "Master - Light Touch" }, { clip, "Master - Loud Rock & Punk" } } },
            { "Mix Bus & Master", "Master - Synthwave Shine",
              "Wide, glued and shining.",
              { { eq, "Mix Bus - Wider (Side Air)" }, { comp, "Mix Bus - Vintage Glue" }, { clip, "Master - Synthwave Shine" } } },

            //==== Sends =======================================================
            { "FX Send", "Send - 80s Gated Reverb",
              "On an FX track: send snare and toms to it. Reverb only (MIX 100%).",
              { { slap, "Send - 80s Snare & Toms" } } },
            { "FX Send", "Send - Tight Room",
              "On an FX track: a short, tight room, reverb only.",
              { { slap, "Send - Tight Room" } } },
        };
        return recipes;
    }
}

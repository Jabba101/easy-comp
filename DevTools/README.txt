DEVTOOLS - how the Easy plugins are tested, tuned and checked
=============================================================
Nothing in this folder is part of the plugins. GitHub's build ignores
it, and you never need it to build or install them. It's kept here so
the testing and tuning work can never be lost: if the workspace these
tools ran in disappears, a new session can rebuild it from here.

For a developer (or a new Claude session): clone the repository to
/home/claude/easy-comp, then run

    bash DevTools/setup_workspace.sh            the tools, JUCE 9.0.4, pluginval
    bash DevTools/setup_workspace.sh --audio    ...and the test audio (~1 GB)

It puts everything back where it was (under /home/claude), so the
paths inside the tools work unchanged. The plugins' working copy is
/home/claude/EasyPlugins; the git clone is /home/claude/easy-comp (copy
changed files into it before committing).


====================================================================
WHAT'S HERE
====================================================================
Tests/                 -> /home/claude/verify2
    One program per plugin: CompTests, TransientTests, GateTests, DSTests,
    SlapTests (Bitch-Slap: its reverb against a plain, slow convolution),
    ClipTests (Easy Clip: the ceiling under every setting and while knobs
    move, the delay, aliasing per OVERSAMPLING setting, PROTECT LOWS,
    LINK, the ADVANCED INIT, speed), AmpTests (Easy Amp: clean at DRIVE 0,
    harmonics rising with DRIVE (even and odd reported), level matching on
    drums and bass, TIGHT, aliasing, the breathing, speed), EQTests (Easy
    EQ: flat = untouched, each band measured against the drawn curve, the
    matched (uncramped) filters, proportional Q, the filters, mid-side and
    MONO BASS, no clicks, LISTEN, AUTO-GAIN, MASTER's linear phase and
    delay, dragging the dots, speed), StripTests (Easy Strip: each module
    against its standalone plugin sample for sample, all eight in a row,
    the sidechain, switched-off modules skipped with no clicks, settings
    both ways, the delay (and the EQ's MASTER changing it), INPUT /
    OUTPUT / BYPASS, LUFS against known tones and an independent
    measurement, saving, the window, speed).
    The sound (against theory and real material), the DAW plumbing
    (settings, saving, presets, buses), the window (and PNG renders of
    it), skipping silence (must match working through it), and speed
    budgets (in an optimised build only). They print "N / N checks
    passed" and exit 1 on any failure.
      cmake -S /home/claude/verify2 -B /home/claude/verify2/build -G Ninja -DCMAKE_BUILD_TYPE=Debug
      cmake -S /home/claude/verify2 -B /home/claude/verify2/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
      cmake --build /home/claude/verify2/build --target CompTests
      /home/claude/verify2/build/CompTests_artefacts/Debug/CompTests <folder for the PNGs>
    GateTests and DSTests read material from /home/claude/presetcheck;
    SlapTests also /home/claude/presetcheck/slap_material (the approved
    listening examples, from TestAudio/BitchSlap).

PresetTuning/          -> /home/claude/presetcheck
    presets_comp.tsv, presets_transient.tsv, presets_ds.tsv: the preset
    tables (with a target meter reading and the material each is tuned
    on). Edit them, then  python3 gen_presets.py  rewrites the tables in
    those three plugins' Presets.h. (Easy Gate's Presets.h is edited by
    hand; presets_gate.tsv is only for GateCheck.)
    CompCheck, TransientCheck, GateCheck, DSCheck: run every preset over
    real material and report what the meter shows. CompCheck --tune finds
    the THRESHOLD and MAKE-UP that hit each preset's target:
      CompCheck_artefacts/Release/CompCheck ../material ../presets_comp.tsv [--tune] [name filter]
    A material written "bass_picked<kick_rock" means: that track, with
    the second one sent to the sidechain.
    make_material.py, gate_material.py, ds_material.py build the test
    tracks from the sample collections (see TestAudio).

TestAudio/
    Where the test audio comes from (SOURCES.txt) and the download lists.
    BitchSlap/: the Bitch-Slap listening examples the user approved (FLAC;
    setup_workspace.sh decodes them to presetcheck/slap_material).

Checks/GateEngine/     -> /home/claude/gatecheck
Checks/DSEngine/       -> /home/claude/dscheck
    The gate and de-esser engines checked directly (DSEngine against a
    plain, slow reference version of the de-esser: DeEsserReference.h).
Checks/Host/           -> /home/claude/gatecheck/host
    Loads the built VST3s the way a DAW does. GateHost: names, latency,
    buses. CompHost: the settings the DAW sees, the sidechain bus, and
    that a sound in the sidechain turns the track down. SlapHost: Bitch-
    Slap's settings, buses, delay and tail, a hit's burst, saving.
      xvfb-run -a CompHost_artefacts/Release/CompHost "<path>/Easy Comp.vst3"
Checks/Golden/         -> /home/claude/golden
    Recordings of Easy Comp 1.2 (v12-source) for a set of scenarios, and
    a comparer - used to prove a later rewrite didn't change the sound.

Compare/               -> /home/claude/opt/harness2
    The "before and after" harness for Easy Comp and Easy Trans: the
    previous release's Effects folder against the current one.
      compare          old settings must come out bit for bit the same
                       ("diff -999.0 dB" on every line)
      compare-rest...  skipping silence vs working through it (also
                       -vintage, -thrust, -both)
      bench, bench-modes, thrust-response
    "before" lives in /home/claude/opt/v151-src/Effects. For a new
    release, refill it from the last release's commit:
      git -C /home/claude/easy-comp archive <commit> Effects | tar -x -C /home/claude/opt/v151-src
      touch /home/claude/opt/v151-src/Effects/*
    (The touch matters: GCC's #pragma once treats two identical files
    with the same date as one file, and the build then fails oddly.)

Bench/                 -> /home/claude/bench2
    Older speed tests of the whole plugins.

FastMathFit/           -> /home/claude/opt/fit
    The fits behind FastMath's quick log2 / exp2 (and their checks).

Prototypes/
    CompVintage: Python models used to design VINTAGE and THRUST (lib.py
    holds the shared pieces; v3.py, thd.py, hiratio.py the comparisons).
    DeEsser: models used to design Easy DS's detector.
    BitchSlap: the gated reverb models (slap.py: the slammed room and the
    burst; kit.py builds real-drum test grooves from the Virtuosity kit,
    every mic of each hit; render.py writes the listening examples).
    BitchSlap/Engine (-> /home/claude/slapdev): the C++ engine on its own.
      EngineCheck  against a plain direct convolution, LENGTH changes, speed
      SlapBench    JUCE's FFT vs PFFFT, and JUCE's Convolution
      SpikeProbe   time per 64-sample block, by position in the cycle
      MixBusCheck  how loud the reverb is on full mixes, per LENGTH (wet vs
                   dry, K-weighted): what the Mix Bus presets' MIX is set from

Look/
    The photoreal "field case" knob test (Blender scene and renders) for
    the optional Cold War look. The look mockups themselves are the
    claude.ai artifact "Easy Plugins Cold War Look".
Look/Strip/
    The Aardvark Easy Strip's look: the chosen reference render, the
    wireframe it was made from, the prompt, and the layout and style
    decisions (README.txt there).

SpeedUpWorkbook/
    The before/after speed workbook from the 1.5 speed-up, and its script.


====================================================================
EVERY CHANGE, IN THIS ORDER
====================================================================
 1. Old settings must sound exactly as before. New settings get their
    own parameter IDs with the next version hint, default to "off", and
    presets/projects that don't mention them open with them off.
 2. Prototype anything new in Python first (Prototypes/), measured on
    real material, before writing the C++.
 3. Tests in Debug and Release (speed budgets), and Compare against the
    last release: "diff -999.0 dB" for everything the old version could
    do. Skipping silence must stay within tolerance.
 4. Presets: TSV -> --tune -> gen_presets.py.
 5. The real plugin, optimised (LTO), on Linux:
      copy the source to /home/claude/build-plugins/EasyPlugins
      cmake --preset release -DFETCHCONTENT_SOURCE_DIR_JUCE=/home/claude/deps/juce-9.0.4
      cmake --build out/build/release --target EasyComp_VST3    (5-10 minutes: run it in the background)
    then pluginval at the strictest level, and the hosted check:
      xvfb-run -a /home/claude/deps/pluginval/extracted/pluginval --strictness-level 10 --validate-in-process "<path>.vst3"
 6. Docs (HOW TO BUILD.txt, PLAN.txt), then commit and push. GitHub
    builds the Windows plugins (Actions, about 20 minutes; the download
    is kept 30 days). Commit messages end with:
      Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
      Claude-Session: https://claude.ai/code/session_01F6HQht2ihBvTsV3xzXSMJc
    (or whatever attribution the current session is given).


====================================================================
DECISIONS WORTH REMEMBERING
====================================================================
Easy Comp (Effects/Compressor.h explains the rest)
  - MODERN: per-sample peak (or AVERAGE: 15 ms mean square x sqrt 2, so
    a steady tone reads the same), dB gain computer with a 6 dB soft
    knee, one-pole attack/release in dB. Runs of 64 samples.
  - VINTAGE: a zero-delay feedback loop in dB. Loop slope RATIO - 1 (so
    a steady sound settles at exactly RATIO), knee 18/(RATIO + 1) dB on
    the turned-down sound (~9 dB going in; within 0.53 dB of MODERN's
    curve), attack time x RATIO (= ATTACK while grabbing), release time
    x sqrt(RATIO) (follows loud passages sqrt(RATIO) faster, eases back
    sqrt(RATIO) slower once it drops away). Tried and rejected: the
    knobs as plain analog RC times (grabs RATIO times faster, more
    distortion), release x RATIO (holds far too much: +90% average GR),
    attack x RATIO with release x 1 (exactly the same as MODERN - no
    character). The chosen one: same peak GR as MODERN, a bit more
    average, 1-9 dB less low-frequency distortion at every setting
    measured.
  - THRUST: four first-order shelves from 20 Hz, corners 1.94x apart up
    to ~2 kHz (+3 dB/octave on average), 0 dB at 1 kHz, +4.8 dB at the
    top. Direct form. Adds about a quarter to the compressor's CPU.
  - The sidechain: the detector reads the key; AUTO-GAIN always measures
    your track. With no key coming in it hears silence and the window
    says so after 1.5 s of your track playing.
Easy DS
  - THRESHOLD is relative: the harsh band is compared with a line at
    sqrt(rest-of-sound x 300 ms slow total) (rest alone failed on
    cymbals over the kit). Followers 0.3 ms up / 12 ms down; 6 dB knee
    capped at RANGE; S-curve smoothing 2 ms / 40 ms; 2 ms look-ahead.
    Tuned on real speech (AudioMNIST), vocals and a real kit (Virtuosity
    Drums).
Bitch-Slap (Effects/GatedReverb.h explains the rest)
  - The "burst" kind of gated reverb, chosen by ear over a gated room:
    the mono sum convolved with noise (L and R different), shaped: 2 ms
    gap, opens over 2 ms, sags 4 dB across LENGTH, slams shut over the
    last 12 ms; tone 180 Hz - 9 kHz (2nd-order filters on the output,
    which is the same as filtering the burst). LENGTH = hit to shut.
  - Level fixed (not per LENGTH): noise 0.0100 per sample at 48 kHz
    (x sqrt(48k / rate)) = the approved snare example at MIX 50%.
    MIX: dry = min(1, 2(1 - mix)), wet = min(1, 2 mix).
  - FFT convolution with PFFFT (ThirdParty/pffft): ~15x faster than
    JUCE's FFT. JUCE's own Convolution took 1.5-7.8% of a core for a
    1 s burst; ours ~0.45%. Pieces: 7 x B, 7 x 8B, then 64B ones (B = 64
    at 44.1/48 kHz: the biggest power of 2 inside the 2 ms gap, so the
    engine's wait costs no plugin delay). Each size starts exactly when
    its first output is due (offset = size - B).
  - Overlap-ADD (not overlap-save) and LENGTH changes only just after a
    64B boundary: so each stretch of input is in one slot only and every
    sound keeps exactly one LENGTH. (Overlap-save shared each stretch
    between two slots, and a change chopped recent bursts into 85 ms
    on/off pieces - caught by the test that moves LENGTH 0-200 ms after
    a hit.) Pieces are rebuilt in place, lazily, when a slot of a newer
    LENGTH first reaches them.
  - Rests after LENGTH-max + 3 x 64B + 50 ms of silence; MIX 0% and
    BYPASS rest at once (after the 20 ms fade).
All plugins
  - Anything switched off costs nothing; digital silence is skipped with
    rest(), which must end up where working through would have.
  - AUTO-GAIN (LoudnessMatcher) matches loudness as you hear it
    (K-weighted), predicting from the curve when a knob moves.
  - CPU budgets on the user's old PC (i7-2600K, no AVX2): SSE2 only.

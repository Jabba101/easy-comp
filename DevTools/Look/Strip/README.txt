THE AARDVARK EASY STRIP - THE LOOK (reference, 22 October 2026)
================================================================
What it's for: the default plugin on EVERY track - drums, bass,
guitars, keys, synths, vocals, buses, and the master if it's good
enough. (It started with drums; it isn't a drum strip.) So presets,
hints and reference plates cover the whole mix, not just drums.

strip_reference_v1.png   the chosen render: the look is settled on this.
                         Made with GPT Image 2 Edit from strip_wireframe.html
                         (screenshot attached) and the prompt below.
strip_wireframe.html     the layout wireframe it was made from (open in a
                         browser).

The look, not the controls: the render invented most of the knob labels
and the EQ section. The exact controls of each module are still to be
designed (see "Still to decide").


====================================================================
LAYOUT
====================================================================
 - BIG by default: designed to be comfortable at 100% (about 1560 wide
   in the full-size wireframe; at 67% it strains the eyes). You open it,
   work on a track, close it - like a big hardware strip. Fixed zoom
   steps 100 / 125 / 150 / 175 / 200%, chosen in SETUP and remembered
   for every strip (the user usually works at 125-150%), each checked so
   nothing blurs or misaligns.
 - Future-proof for 4K: at Windows' 200% scaling it shows at the same
   physical size, sharper. The photoreal art (case, slabs, knobs) is
   rendered at 3x the base size; labels, counters, screens and meters
   are drawn live as vectors, sharp at any size. JUCE 9 draws through
   Direct2D (the graphics card), so bigger art costs memory, not CPU,
   and only while the window is open.
 - Widescreen proportions (16:9 or a little taller).
 - Header, left to right: the title in white military stencil capitals,
   AARDVARK centred over EASY STRIP (sized to exactly the same width);
   the preset window right beside it (wide, with arrow buttons; clicking
   the name opens the preset list); then, grouped on the right:
   OVERSAMPLING as three interlocking pushbuttons OFF / 4x / 8x (OFF lit
   white, 4x and 8x lit blue), DIM, a BYPASS pushbutton lit red when on,
   and SETUP (a cog button: ticker on/off, window size, DAY / DUSK /
   NIGHT, hover-shows-value, the EQ's MASTER precision; more can be
   added later).
 - A centred row of view buttons (Gate, Amp, EQ, DS, Comp, Trans,
   Bitch-Slap, Clip): warm white backlit faces like old studio pushbuttons; the one
   on the big screen is lit amber. The big screen changes ONLY when you
   press one (no automatic switching, no HOLD button). A MODEL plate on
   the left, the serial number plate on the right (click it: version
   and build date).
 - One big CRT screen in the centre. The EQ sits right under it, bands
   running low to high left to right, so each knob is under the part of
   the curve it changes.
 - Module slabs on both sides, in chain order: Input, Gate, Amp (left),
   EQ (under the screen), DS, Comp, Trans, Bitch-Slap, Clip (right), Output.
   Tall input and output meters at the two ends.
 - Every knob is always on the panel: adjusting never needs a screen
   change. The small option buttons sit inside their own module.
 - The ticker (tooltips and values) runs the full width along the
   bottom, double height. It can be hidden in the settings (the window
   just gets shorter); remembered for every strip.
 - No small monitors along the top (tried: they repeated what the panel
   already shows). Activity lamps instead, where it matters: Gate
   opening, DS cutting, Clip shaving peaks.

====================================================================
STYLE RULES
====================================================================
 - Tough, chunky, retro-futuristic: heavy late-70s/80s sci-fi hardware.
   Armoured case, chamfered edges, rubber corners, big handles. (Not a
   "Cold War" museum piece: prompts using that come out dull.)
 - A unit kept in service for years with modules replaced: each slab a
   different batch - paint (dark olive, grey-green, sand, gunmetal),
   label style (stencil, engraved plate, embossed tape), fasteners.
   Neighbouring slabs always contrast. Light wear only. The outer case
   is original and uniform.
 - Uniform where the job is the same: the module on/off buttons
   (identical square pushbuttons with the module name: the button stays
   dark, and its LETTERING glows green when the module is on, dull grey
   when off - so they read as switches, and a strip full of modules
   that are on isn't a wall of bright buttons), the drum counters, the view buttons. Varied where the job
   differs: knob sizes by importance (one hero knob per module), knob
   colour by function (blue = frequency, red = level), section sizes.
 - No grids of identical knobs: group them (each EQ band its own tinted
   panel, frequency knob above and offset from its gain knob).
 - Light colours mean the same everywhere: GREEN = a module or feature is
   on (module buttons, AUTO GAIN);
   AMBER = selected or a sound character choice (the view on the big
   screen, VINTAGE); BLUE = a mode that adds delay and CPU (the EQ's
   MASTER, oversampling at 4x and 8x); RED = a warning (BYPASS on, the
   clip and peak lamps).
 - A drum counter under EVERY knob, sized to its knob (big under the
   main knob, compact under small ones, never too small to read at
   Medium). Numbers only on the drums (words like OFF too); the unit
   (kHz, dB, ms) is printed small on the panel underneath, like the
   HOURS / MINUTES under an old clock's counters. Exception: % sits to
   the RIGHT of the drums, printed as big as the digits ("35 %"), on
   every percentage. Other units may move beside their numbers too,
   case by case, if underneath feels wrong after real use.
   Each counter has a FIXED number of drums, enough for its knob's
   whole range (e.g. PUNCH / SUSTAIN, -100 to +100 %: a sign drum plus
   three digits); unused drums show blank, so a counter never changes
   width. The ticker shows the
   full value with its unit while you turn. (This replaces the "one
   counter per section" idea below.)
 - Mechanical drum counters (white digits on black drums, each digit on
   its own drum) are BIG - the largest text on the panel after the
   title - so the exact values read even with the window small. They
   show values:
   one per section, showing whichever knob you touch (or hover), back to
   the section's main value after a couple of seconds, with a small
   label saying which knob it is.
 - Comfortable for hours: dark green screens with a soft trace, no
   bloom, no flicker or moving CRT effects, gentle glow on lit parts,
   no glare. The final art is rendered with even, shadowless light;
   any glass reflection is a separate faint layer added on top.
 - Buttons look engineered: lettering centred on the button, and
   buttons that sit together are all the same width (never sized to
   their own word).
 - Two-word SQUARE buttons stack their words, one over the other,
   centred, with no hyphen (wide rectangular choice buttons, like THIS
   TRACK, keep one line) (BITCH over SLAP, AUTO over GAIN). AUTO GAIN is a big
   square pushbutton, lit green when on, centred in its section's full
   height (not just the row under the heading).
 - All panel labels in CAPITALS (knobs, buttons, sections, presets): they
   stay clear when the window is small. The ticker's hints stay in
   normal sentence case, as they're read rather than glanced at.
 - Labels are drawn by the plugin as crisp text, not painted into the
   artwork (sharp at every size, easy to fix).
 - Later: DAY / DUSK (warm reddish-brown) / NIGHT (deep blue) panel
   lighting, from three renders of the same base; labels softly
   backlit in the dark modes. One setting for every strip.

====================================================================
THE CONTROLS
====================================================================
The standalones' current windows are function tests: once the strip's
look is final, the standalones get the same look too. Varied control
sizes are encouraged (authentic military-grade hardware), and every
element sits centred in its own space - nothing left-aligned like text
in a document.

THE RULE: each module on the strip IS its standalone plugin, restyled -
the same controls, the same names, the same numbered sections in the
same order (e.g. Easy DS: 1 WHERE, 2 HOW MUCH, 3 TURNS DOWN), the same
choices (as lit pushbuttons), the same hints word for word. Someone who
knows the standalone knows the strip's section instantly.
The difference: the standalone has room for one plugin (a value under
every knob, its own big display, GETTING STARTED) - best for learning
it or detailed work on one track. The strip is for speed: all modules
in one window, more compact (one counter per section, the big screen
shared, hints on the ticker). Bonus: as the settings are identical,
presets can be shared - load an Easy Comp preset into the strip's Comp
section, or save the strip's DS settings as an Easy DS preset.
Every module also gets its own PRESET button: a small square "Pr" button
in the same place on every slab (top-right corner). It opens that
module's presets - the same list as its standalone - and can save the
module's settings. Loading one changes only that module; the strip's
preset name then shows it's been changed (e.g. "SNARE: ROCK *").
(The list below predates this rule: where it differs from a
standalone, the standalone wins.)

"Easy" means every control explained in plain language (the ticker),
not fewer controls: each module keeps all of its standalone's controls.
The ticker is on by default and its hints matter as much as the sound.

 Input   TRIM, polarity flip
 Gate    the GATE lamp beside its on/off button, its state written next
         to it (OPEN / CLOSING / CLOSED); 1 HOW MUCH: THRESHOLD, RANGE;
         2 HOW FAST: ATTACK, HOLD, RELEASE; 3 WHAT IT LISTENS TO: IGNORE
         BELOW, IGNORE ABOVE (blue), OPENED BY: THIS TRACK / SIDECHAIN,
         and LISTEN (moved in from the standalone's header)
 Amp     one big knob, no name: its scale says what it does - CLEAN at
         the bottom left (fully off), ANALOG at the top (the middle),
         OVERDRIVE at the bottom right (fully up)
 EQ      Its on/off button is a wide one reading EQUALIZER (the view
         button stays EQ, square like the others), centred across the
         slab; MASTER sits on the same centre line, exactly halfway
         between EQUALIZER and the Pr button. Layout: frequency knob
         (blue, smaller) up-left, gain knob (red, bigger) offset down-right,
         painted scales as large as possible (13-14 px at 100%); bands of
         slightly different widths and knob positions, toggles and screws
         in different spots - "the circuit board decided" - while the
         logic stays the same in every band. No grid of identical knobs.
         Each band's frequency and gain knobs stay CLOSE together, near
         the top, so working quickly needs little mouse movement; spare
         height below goes to atmosphere (a ventilation grille) and the
         frequency reference plate, never to spreading the knobs out.
         In the zone of API's 550 (not a clone): four overlapping bands
         LOW, LOW MID, HIGH MID, HIGH, plus HPF and LPF; BELL / SHELF
         switches on LOW and HIGH; proportional Q (narrower the harder
         you boost or cut, so no Q knob); gain and each band's
         frequencies printed around the knobs (gain in 2 / 4 / 6 / 9 / 12
         steps either side of 0) as well as the counters. SEPARATE
         frequency and gain knobs (faster than stacked/concentric ones,
         which feel fiddly with a mouse); dragging a point on the big
         screen's curve sets both at once. Knobs are SMOOTH by default;
         SETUP has STEPPED (click between fixed musical points) - it only
         changes how knobs move from then on: an in-between value from a
         preset or project stays until that knob is touched. MID-SIDE: each band
         gets a small three-way target selector, STEREO / MID / SIDE (the
         knobs never change meaning); the big screen draws MID and SIDE
         in two colours; a MONO BASS knob keeps everything below its
         frequency in the centre; the selectors go dim on mono tracks.
         A lit MASTER
         pushbutton (blue) for linear phase. Its hint: keeps the timing of
         every sound intact, best on a drum bus or a finished mix; adds a
         short delay and uses more power, so leave it off on single tracks.
 DS      top: a HARSH BAND panel - FROM and TO (blue) with FIND and
         LISTEN between them; below: THRESHOLD (biggest), RANGE, and a
         BAND / WHOLE toggle (what gets turned down); counter at the foot
 Comp    THRESHOLD (biggest), RATIO, ATTACK, RELEASE, MAKE-UP, MIX,
         IGNORE BELOW, REACTS TO (peaks / average), STYLE (modern /
         vintage), LOW END (normal / thrust), LISTENS TO (this track /
         sidechain); its gain reduction meter
 Trans   a centre-zero GAIN CHANGE needle meter (-12 / 0 / +12) beside its
         on/off button; 1 SHAPE: PUNCH, SUSTAIN (marked - 0 +, the middle
         changes nothing); 2 OUTPUT: OUTPUT, MIX, AUTO GAIN (square,
         centred) - as the standalone
 Bitch-Slap  (always BITCH-SLAP, never shortened: on square buttons it
         sits on two lines, BITCH over SLAP with no hyphen, both centred,
         same size as the others)
         1 HOW MUCH: MIX, its scale printed with the standalone's guide -
         0% your sound only, 50% both at full level, 100% reverb only;
         2 HOW LONG: LENGTH, SYNC as OFF / TEMPO pushbuttons (LENGTH's
         counter shows the note, e.g. 1/8, when synced)
 Clip    SIMPLE / ADVANCED (see PLAN.txt): the slab flips in place
         between the four everyday knobs (PUSH, CEILING, SOFTNESS,
         PROTECT LOWS) and the sound-design ones (TOP, BOTTOM, BIAS,
         BLEND); CEILING never moves; a lamp on ADVANCED when any hidden
         setting isn't neutral
 Output  LEVEL (the modules keep their own AUTO GAIN and output controls,
         exactly as their standalones); the tall meter switches PEAK dB /
         LUFS with a small pushbutton; a drum counter shows the
         integrated LUFS (click to reset)

====================================================================
ATMOSPHERE IN THE DEAD SPACE
====================================================================
Empty areas get military-tech character: ventilation grilles (slotted or
perforated), stencilled markings (unit code, DO NOT OPEN - NO
USER-SERVICEABLE PARTS, hazard stripes), inspection / CALIBRATED tags
with a date and initials, a riveted QC stamp, torque-spec labels near
bolts, an engraved signal-flow plate showing the module order. A useful
one for the EQ's spare space: an engraved FREQUENCY REFERENCE plate for
the whole mix (kick and bass weight, vocal body and presence, guitar
crunch, boxiness, sibilance, air - with drum landmarks among them).
Rules: static and subtle (part of the pre-rendered art, so no CPU cost;
darker and quieter than the labels and controls), and never next to a
control, so nothing is mistaken for a button.

====================================================================
LATER: EASY SFX
====================================================================
A huge pushbutton at the bottom of the strip reading WEAPON SELECT opens
the effects window, EASY SFX (the "effects window" in PLAN.txt: easy
versions of effects used a lot, e.g. slapback delay on vocals). Not
designed yet - the strip itself comes first. Its place: a long oblong
button under Comp, above the ticker, in the stencil lettering, set in a
classic black-and-yellow hazard-striped
surround (in strip_wireframe_full_size.html).

====================================================================
STILL TO DECIDE
====================================================================
 - Each module's control sizes and places.
 - What the big screen shows for each module (EQ: the curve, MID and
   SIDE in two colours; and so on). CLIP is decided: the waveform
   scrolling past with the ceiling as a line, every cut peak in amber,
   the "peaks cut by up to" readout; in ADVANCED separate TOP and BOTTOM
   lines; the low end PROTECT LOWS leaves alone drawn in another shade.
 - The final artwork: built to exact measurements (as with the Blender
   knob test in ../), using strip_reference_v1.png as the style guide.

====================================================================
THE PROMPT (attached: a screenshot of strip_wireframe.html)
====================================================================
Use the attached wireframe as a guide to the overall arrangement: a
header bar across the top, a row of view buttons with a metal plate at
each end, one wide screen in the centre with the EQ section beneath it,
module sections on both sides, tall meters at both ends, and a
full-width readout strip along the bottom. The light green area is a
CRT screen, amber areas are backlit amber displays. Exact positions and
proportions can be adjusted to make the design work better.

Design it as a photoreal audio channel strip with a tough, chunky,
retro-futuristic character, like heavy-duty late-70s and 80s sci-fi
hardware: an original armoured outer case, uniform throughout, with
thick chamfered armour-plate edges, rubber-armoured corners and heavy
carrying handles on both sides. The header panel reads AARDVARK EASY
STRIP in white military stencil capitals sprayed onto dark gunmetal,
with a small amber preset readout, a rotary switch, a small knob and a
big bat-handle bypass toggle on the right.

The unit has been in service for years and some modules have been
replaced: each module slab is a field-replaceable unit from a different
batch, so they differ in paint colour (original dark olive, grey-green,
sand, gunmetal), label style (stencilled, engraved metal plates,
embossed tape) and fasteners (hex bolts, slotted screws), with sparing
hazard-orange accents. Neighbouring modules always contrast. Well
maintained: light wear only, no grime.

Below the header, a centred row of eight identical square pushbuttons
with warm white backlit faces and black lettering, like vintage
illuminated studio buttons: Gate, Amp, EQ, DS, Comp, Trans, Slap, Clip.
The EQ button glows amber. A small riveted metal plate at the left end
reads MODEL ES-8; one at the right end reads SER. NO. 0417-66.

The centre screen is a large, wide CRT set deep into an armoured
housing, dark green, almost black, showing a bright green EQ curve over
a faint grid. The EQ knobs beneath it run from low frequencies on the
left to high on the right, in alternating tinted band panels.

Oversized knobs with knurled metal skirts in varied sizes, the most
important control in each module the biggest; blue caps for frequency,
red caps for level. Each module's on/off control is a large, clearly
pressable square pushbutton at the top of its slab with the module name
on it, all the same size, backlit green when on (Slap's is unlit), with
a small amber activity lamp beside it on Gate, DS and Clip. Beneath the
knobs, small mechanical drum counters with white digits on black
rolling drums show values. Comp has a small analogue gain reduction
meter and a glowing orange square pushbutton. Big bat-handle toggle
switches for polarity and EQ mode. Tall amber-lit level meters at the
left and right ends.

Along the bottom, across the full width, a tall amber dot-matrix
readout behind dark glass, glowing softly, showing one line of text.

Soft, even, shadowless front lighting, matte surfaces, no reflections
or glare; the glow of the screen, buttons, lamps and amber displays is
gentle, comfortable to look at for hours. Labels can be placeholders.
Straight-on front view filling a widescreen frame, plain dark
background. Must stay clear and easy to read when shown small.

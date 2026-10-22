THE AARDVARK EASY STRIP - THE LOOK (reference, 22 October 2026)
================================================================
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
 - Widescreen (16:9 or a little taller), sized for a 1920x1080 screen
   but used in a window beside the DAW's mixer, so it must read well
   small (about 960 wide). Fixed sizes (Small / Medium / Large), not
   free stretching.
 - Header, left to right: the title in white military stencil capitals,
   AARDVARK centred over EASY STRIP (sized to exactly the same width);
   the preset window right beside it (wide, with arrow buttons; clicking
   the name opens the preset list); then, grouped on the right:
   OVERSAMPLING as three interlocking pushbuttons OFF / 4x / 8x (OFF lit
   white, 4x and 8x lit blue), DIM, a BYPASS pushbutton lit red when on,
   and SETUP (a cog button: ticker on/off, window size, DAY / DUSK /
   NIGHT, hover-shows-value, the EQ's MASTER precision; more can be
   added later).
 - A centred row of view buttons (Gate, Amp, EQ, DS, Comp, Trans, Slap,
   Clip): warm white backlit faces like old studio pushbuttons; the one
   on the big screen is lit amber. The big screen changes ONLY when you
   press one (no automatic switching, no HOLD button). A MODEL plate on
   the left, the serial number plate on the right (click it: version
   and build date).
 - One big CRT screen in the centre. The EQ sits right under it, bands
   running low to high left to right, so each knob is under the part of
   the curve it changes.
 - Module slabs on both sides, in chain order: Input, Gate, Amp (left),
   EQ (under the screen), DS, Comp, Trans, Slap, Clip (right), Output.
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
   (identical square pushbuttons with the module name, backlit green
   when on), the drum counters, the view buttons. Varied where the job
   differs: knob sizes by importance (one hero knob per module), knob
   colour by function (blue = frequency, red = level), section sizes.
 - No grids of identical knobs: group them (each EQ band its own tinted
   panel, frequency knob above and offset from its gain knob).
 - Light colours mean the same everywhere: GREEN = a module is on;
   AMBER = selected or a sound character choice (the view on the big
   screen, VINTAGE); BLUE = a mode that adds delay and CPU (the EQ's
   MASTER, oversampling at 4x and 8x); RED = a warning (BYPASS on, the
   clip and peak lamps).
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
"Easy" means every control explained in plain language (the ticker),
not fewer controls: each module keeps all of its standalone's controls.
The ticker is on by default and its hints matter as much as the sound.

 Input   TRIM, polarity flip
 Gate    THRESHOLD (biggest), RANGE, ATTACK, HOLD, RELEASE, IGNORE BELOW,
         IGNORE ABOVE, OPENED BY (this track / sidechain)
 Amp     one big knob, no name: its scale says what it does - CLEAN at
         the bottom left (fully off), ANALOG at the top (the middle),
         OVERDRIVE at the bottom right (fully up)
 EQ      HPF, LPF, low shelf, bell 1, bell 2, high shelf - a frequency and
         a gain knob on every band, shelves included; a lit MASTER
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
 Trans   PUNCH, SUSTAIN, MIX (no OUTPUT knob on the strip)
 Slap    LENGTH, MIX, SYNC (LENGTH reads as notes when synced)
 Clip    CEILING
 Output  LEVEL, and one AUTO-GAIN for the whole strip (the modules don't
         get their own AUTO-GAIN or output on the strip)

====================================================================
STILL TO DECIDE
====================================================================
 - Each module's control sizes and places.
 - What the big screen shows for each module (EQ: the curve; Clip: the
   waveform with the ceiling and the cut peaks; and so on).
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

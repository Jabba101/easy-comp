PFFFT - "a pretty fast FFT"
===========================
Used by Bitch-Slap (Effects/GatedReverb.h) to do its reverb with FFTs.
It's about 15 times quicker than the FFT built into JUCE, which is what
keeps a one-second burst of reverb down to a fraction of a percent of
your processor.

Where it's from: https://github.com/marton78/pffft
                 commit aa16fd3db58de4ba5dae8b0438440bb9da46b6fa (29 Sep 2026)
Only the single-precision ("float") files are here, unchanged:
    pffft.h  pffft.c  pffft_common.c  pffft_priv_impl.h  simd/
(On Windows, CMakeLists.txt adds the one header pffft.c forgets to
include, malloc.h, rather than editing the file.)

Licence: LICENSE.txt (a BSD-style licence, from FFTPACK). Free to use. If
you ever give the plugins to anyone else, include the text of LICENSE.txt
with them (in a "licences" text file next to the plugins, or in a manual).

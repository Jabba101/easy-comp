#!/usr/bin/env python3
"""
Material for tuning Easy DS's presets: 48 kHz stereo float WAVs, each with a
label file saying, for every 5 ms, what's going on:

    0  other (silence, pauses, breaths)
    1  ordinary (vowels and voiced sounds; drums without the harsh cymbals)
    2  harsh (S, SH and Z sounds; the harsh cymbal and hi-hat hits)

The S sounds are all real: spoken words ("six", "seven", "zero" and the
other digits) recorded at 48 kHz by 16 different speakers (AudioMNIST, MIT
licence). The speech tracks are those recordings as they are; the sung
tracks put their S sounds around sung vowels (synthesised, as in
make_material.py), the way S sounds sit between sung words.
"""
import glob
import json
import os

import numpy as np
from scipy import signal
from scipy.io import wavfile

import make_material as mm

SR = mm.SR
OUT = '/home/claude/presetcheck/ds_material'
AMN = '/home/claude/samples/audiomnist/wav'
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(4242)
FRAME = int(0.005 * SR)

MALE = ['01', '02', '03', '04', '05', '06', '07', '08']
FEMALE = ['12', '26', '28', '36', '43', '47', '52', '56']


def gain(db):
    return 10.0 ** (db / 20.0)


def db(x):
    return 10.0 * np.log10(x + 1e-30)


# ---------------------------------------------------------------- the real recordings
def load_word(speaker, digit, rep):
    sr, x = wavfile.read(f'{AMN}/{digit}_{speaker}_{rep}.wav')
    assert sr == SR
    x = x.astype(np.float64) / 32768.0
    x = mm.highpass(x, 60.0)                       # (the recordings have a little rumble)
    env = np.abs(x)
    loud = np.nonzero(env > 0.02 * env.max())[0]
    a, b = max(0, loud[0] - int(0.012 * SR)), min(len(x), loud[-1] + int(0.025 * SR))
    x = x[a:b].copy()
    f = int(0.004 * SR)
    x[:f] *= np.linspace(0, 1, f)
    x[-f:] *= np.linspace(1, 0, f)
    return x / np.max(np.abs(x))


def frame_powers(x):
    """Per 5 ms: power below 2 kHz, power above 4 kHz, and in total."""
    n = len(x) // FRAME
    lo = mm.lowpass(x, 2000.0, 4)
    hi = mm.highpass(x, 4000.0, 4)
    p = lambda y: np.mean(y[:n * FRAME].reshape(n, FRAME) ** 2, axis=1)
    return p(lo), p(hi), p(x)


def label_speech(x, quietest_db=-40.0):
    """Which 5 ms frames are S sounds, which are vowels, which are neither."""
    lo, hi, tot = frame_powers(x)
    loudest = np.percentile(tot[tot > 0], 99)
    level = db(tot / loudest)
    ratio = db(hi) - db(lo)
    labels = np.zeros(len(tot), dtype=int)
    labels[(level > quietest_db) & (ratio > 6.0)] = 2
    labels[(level > quietest_db + 10) & (ratio < -12.0)] = 1
    return labels


def sibilants():
    """The S, Z and SH sounds out of every recording of "six", "seven" and "zero", by speaker."""
    found = {}
    for path in sorted(glob.glob(f'{AMN}/[067]_*.wav')):
        digit, speaker, rep = os.path.basename(path)[:-4].split('_')
        x = load_word(speaker, int(digit), int(rep))
        labels = label_speech(x, -35.0)
        # contiguous runs of S frames, at least 30 ms long
        runs, start = [], None
        for i, l in enumerate(list(labels) + [0]):
            if l == 2 and start is None:
                start = i
            elif l != 2 and start is not None:
                if i - start >= 6:
                    runs.append((start, i))
                start = None
        for a, b in runs:
            s = x[max(0, a * FRAME - int(0.008 * SR)):min(len(x), b * FRAME + int(0.008 * SR))].copy()
            f = int(0.008 * SR)
            s[:f] *= np.hanning(2 * f)[:f]
            s[-f:] *= np.hanning(2 * f)[f:]
            found.setdefault(speaker, []).append(s)
    return found


SIBILANTS = sibilants()


# ---------------------------------------------------------------- building tracks with labels
class Labelled:
    def __init__(self, seconds):
        self.x = np.zeros(int(seconds * SR))
        self.labels = np.zeros(len(self.x) // FRAME, dtype=int)

    def add(self, sound, t, level_db=0.0, label=None, labels=None):
        i = int(round(t * SR))
        if i >= len(self.x):
            return
        n = min(len(sound), len(self.x) - i)
        self.x[i:i + n] += sound[:n] * gain(level_db)
        a = i // FRAME
        if labels is not None:
            m = min(len(labels), len(self.labels) - a)
            self.labels[a:a + m] = np.maximum(self.labels[a:a + m], labels[:m])
        elif label is not None:
            b = min(len(self.labels), (i + n) // FRAME)
            self.labels[a:b] = np.maximum(self.labels[a:b], label)


def save(name, track, peak_db=-8.0, stereo_width=0.0):
    x = track.x
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    x = x * (gain(peak_db) / np.max(np.abs(x)))
    wavfile.write(f'{OUT}/{name}.wav', SR, x.astype(np.float32))
    labels = [int(v) for v in track.labels[:len(x) // FRAME]]
    with open(f'{OUT}/{name}.json', 'w') as f:
        json.dump({'frame': FRAME, 'labels': labels}, f)
    counts = np.bincount(labels, minlength=3)
    print(f'  {name:26s} {len(x) / SR:5.1f} s   harsh {100 * counts[2] / len(labels):4.1f}%   '
          f'ordinary {100 * counts[1] / len(labels):4.1f}%')


# ---------------------------------------------------------------- speech (real, as recorded)
def speech(speakers, seconds=26.0, fast=False):
    T = Labelled(seconds)
    t = 0.3
    digits = [0, 1, 2, 3, 4, 5, 6, 6, 7, 7, 8, 9]          # (the S words a little more often)
    while t < seconds - 1.2:
        speaker = rng.choice(speakers)
        level = rng.normal(0.0, 1.5)
        for _ in range(rng.integers(4, 9)):
            word = load_word(speaker, rng.choice(digits), rng.integers(6))
            T.add(word, t, level + rng.normal(0.0, 1.5), labels=label_speech(word))
            t += len(word) / SR + (rng.uniform(0.0, 0.04) if fast else rng.uniform(0.03, 0.15))
            if t > seconds - 1.0:
                break
        t += rng.uniform(0.15, 0.3) if fast else rng.uniform(0.35, 0.8)
    T.x = T.x + 0.03 * mm.room_reverb(T.x, 0.35)
    return T


# ---------------------------------------------------------------- sung (synthesised vowels, real S sounds)
def sung(speakers, low, high, seconds=24.0, bpm=84, s_level=-4.0, breath=0.02, brightness=1.3,
         s_chance=0.55, voices=1, spread=0.012):
    T = Labelled(seconds)
    b = 60.0 / bpm
    t, note = 0.4, (low + high) // 2
    sib = [s for sp in speakers for s in SIBILANTS.get(sp, [])]
    while t < seconds - 1.5:
        for _ in range(rng.integers(4, 8)):
            d = rng.choice([0.5, 0.5, 1.0, 1.0, 1.5, 2.0]) * b
            note = int(np.clip(note + rng.choice([-4, -2, -1, 0, 1, 2, 3, 5]), low, high))
            level = rng.normal(0.0, 2.0)
            # an S before the word ("sing", "so") ...
            if rng.random() < s_chance:
                s = sib[rng.integers(len(sib))]
                for v in range(voices):
                    off = rng.uniform(-spread, spread) if voices > 1 else 0.0
                    T.add(s, t + off, level + s_level + rng.normal(0.0, 2.0) - 3.0 * (voices > 1), label=2)
                t += len(s) / SR * 0.85
            for v in range(voices):
                harmony = [0, 4, 7][v] if voices > 1 else 0
                x = mm.sung_note(note + harmony, d * 0.92, brightness, breath=breath)
                T.add(x, t + (rng.uniform(0, 0.02) if voices > 1 else 0.0), level - 3.0 * (voices > 1), label=1)
            t += d
            # ... or after it ("dreams", "is")
            if rng.random() < s_chance * 0.5:
                s = sib[rng.integers(len(sib))]
                T.add(s, t - 0.03, level + s_level + rng.normal(0.0, 2.0), label=2)
                t += len(s) / SR * 0.7
        t += rng.choice([1.0, 1.5, 2.0]) * b
    T.x = T.x + 0.06 * mm.room_reverb(T.x, 0.6)
    return T


# ---------------------------------------------------------------- cymbals and hats
def cymbal(seconds, centre, hump_db, decay, seed, partials=140, lowest=350.0, ping=0.0):
    """A cymbal: lots of inharmonic partials, the high ones dying away faster,
    with a broad hump of extra energy around 'centre' (where it's harsh)."""
    r = np.random.default_rng(seed)
    n = int(seconds * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    freqs = np.exp(r.uniform(np.log(lowest), np.log(16000.0), partials))
    for f in freqs:
        a = (1000.0 / f) ** 0.35 * gain(hump_db * np.exp(-0.5 * (np.log2(f / centre) / 0.6) ** 2))
        tau = decay * (2000.0 / f) ** 0.45
        x += a * np.sin(2 * np.pi * f * t + r.uniform(0, 2 * np.pi)) * np.exp(-t / tau)
    hit = r.standard_normal(n) * np.exp(-t / 0.015)
    x = x / np.max(np.abs(x)) + 0.5 * mm.highpass(hit, 2500.0)
    if ping:
        x += ping * np.sin(2 * np.pi * 420.0 * t) * np.exp(-t / 0.25)
    x *= np.minimum(1.0, t / 0.0015)
    return x / np.max(np.abs(x))


def kit_part(seconds, bpm=110, hats=True):
    T = Labelled(seconds)
    b = 60.0 / bpm
    for k in range(int(seconds / (b / 2))):
        t = k * b / 2
        if k % 8 in (0, 3, 5):
            T.add(mm.kit['kick'], t, -1.0 + rng.normal(0, 1), label=1)
        if k % 8 in (2, 6):
            T.add(mm.kit['snare'], t, -2.0 + rng.normal(0, 1), label=1)
        if hats and k % 1 == 0:
            T.add(mm.kit['hihat'], t, -14.0 + rng.normal(0, 1.5))
    return T


def overheads(seconds=20.0, room=False):
    T = kit_part(seconds)
    b = 60.0 / 110
    crashes = [cymbal(2.6, 5500.0, 9.0, 1.4, s) for s in (1, 2, 3)]
    ride = cymbal(1.5, 3000.0, 4.0, 1.2, 9, ping=0.4)
    bar = 4 * b
    for k in range(int(seconds / bar)):
        if k % 2 == 0:
            T.add(crashes[k % 3], k * bar, -4.0 + rng.normal(0, 1.5), labels=np.r_[np.full(40, 2), np.zeros(10, int)])
        else:
            for e in range(8):
                T.add(ride, k * bar + e * b / 2, -14.0 + rng.normal(0, 1))
    if room:
        T.x = 0.35 * T.x + mm.room_reverb(T.x, 1.3, predelay=0.015, tone=9000.0)
    return T


def hihat_track(seconds=16.0):
    T = Labelled(seconds)
    b = 60.0 / 100
    hats = [mm.drum(f'drum-samples/{k}/hihat.mp3') for k in ('acoustic-kit', 'Kit3', 'Kit8')]
    for k in range(int(seconds / (b / 4))):
        t = k * b / 4
        accent = k % 4 == 2 and rng.random() < 0.8
        h = hats[k % 3 if not accent else 0]
        if accent:   # the sharp, too-bright hits (an open hat or a hard stick)
            h = h + 2.2 * mm.highpass(h, 7000.0, 2)
            T.add(h / np.max(np.abs(h)), t, -1.0 + rng.normal(0, 1), labels=np.r_[np.full(16, 2), np.zeros(4, int)])
        else:
            T.add(h, t, -8.0 + rng.normal(0, 1.5), labels=np.r_[np.full(12, 1), np.zeros(4, int)])
    return T


def clangy(seconds=18.0):
    T = kit_part(seconds, hats=False)
    b = 60.0 / 96
    ride = cymbal(2.0, 3200.0, 14.0, 1.6, 21, ping=0.2)
    crash = cymbal(2.8, 3800.0, 12.0, 1.6, 22)
    for k in range(int(seconds / (b / 2))):
        t = k * b / 2
        if k % 16 == 0:
            T.add(crash, t, -2.0, labels=np.r_[np.full(60, 2), np.zeros(10, int)])
        else:
            T.add(ride, t, -9.0 + (3.0 if k % 2 == 0 else 0.0) + rng.normal(0, 1),
                  labels=np.r_[np.full(10, 2 if k % 2 == 0 else 1), np.zeros(4, int)])
    return T


# ---------------------------------------------------------------- a real drum kit (Virtuosity Drums, CC0)
VD = '/home/claude/samples/virtuosity/flac'
_vd_cache = {}


def vd(mic, name, vl, rr=None):
    """One real hit, as the overhead ('oh') or room ('room') mics heard it (stereo)."""
    path = f'{VD}/{mic}_{name}_vl{vl}' + (f'_rr{rr}' if rr else '') + '.flac'
    if path not in _vd_cache:
        x = mm.decode(path, 2)
        level = np.max(np.abs(x), axis=1)
        above = np.nonzero(level > 0.02 * level.max())[0]
        _vd_cache[path] = x[max(0, above[0] - int(0.001 * SR)):]
    return _vd_cache[path]


class StereoLabelled(Labelled):
    def __init__(self, seconds):
        super().__init__(seconds)
        self.x = np.zeros((int(seconds * SR), 2))


def real_kit(mic, seconds=24.0, bpm=116, ride_bars=(), bell=False, crash_every=4):
    """A groove on the real kit: kick, snare, hi-hat (or ride) eighths, and a
    loud crash at the start of every few bars - with the kick, as drummers do.
    Harsh: the first 300 ms of each crash (or each ride bell)."""
    T = StereoLabelled(seconds)
    b = 60.0 / bpm
    bar = 4 * b
    rr = lambda: int(rng.integers(1, 5))
    crashes = []
    for k in range(int(seconds / bar)):
        t0 = k * bar
        on_ride = k % 8 in ride_bars
        for e in range(8):
            t = t0 + e * b / 2
            if on_ride:
                if bell and e in (2, 6):
                    T.add(vd(mic, 'ride_bell', 3, int(rng.integers(1, 4))), t, rng.normal(-2, 1),
                          labels=np.r_[np.full(60, 2), np.zeros(10, int)])
                else:
                    T.add(vd(mic, 'ride_ride', 3 if e % 2 == 0 else 2, rr()), t, rng.normal(-4, 1), label=1)
            else:
                T.add(vd(mic, 'hh_closed', 3 if e % 2 == 0 else 2, rr()), t, rng.normal(-6, 1), label=1)
            if e in (0, 5) or (e == 3 and k % 2):
                T.add(vd(mic, 'kick_snon', 4 if e == 0 else 3, rr()), t, rng.normal(0, 1), label=1)
            if e in (2, 6):
                T.add(vd(mic, 'snare_center', int(rng.integers(25, 37))), t, rng.normal(0, 1), label=1)
        if k % crash_every == 0 and k > 0:
            T.add(vd(mic, 'crash_crash', 3, rr()), t0, rng.normal(1, 1), labels=np.r_[np.full(60, 2), np.zeros(10, int)])
            crashes.append(int(t0 * SR) // FRAME)
    # (A crash rings on for seconds: its tail is neither the harsh moment nor
    # the ordinary kit, so it isn't counted as either.)
    for a in crashes:
        T.labels[a + 60:a + 400] = 0
    return T


# ---------------------------------------------------------------- guitars and the mix
def from_material(name, quietest_db=-35.0):
    sr, x = wavfile.read(f'/home/claude/presetcheck/material/{name}.wav')
    T = Labelled(len(x) / SR)
    T.x = x.astype(np.float64)
    T.labels = label_speech(np.mean(T.x, axis=1) if T.x.ndim > 1 else T.x, quietest_db)[:len(T.labels)]
    T.labels = np.pad(T.labels, (0, max(0, len(T.x) // FRAME - len(T.labels))))
    return T


def squeaks(seconds_guitar='gtr_acoustic_strum'):
    T = from_material(seconds_guitar)
    T.labels[T.labels == 2] = 1        # (the strums themselves are ordinary)
    mono = np.mean(T.x, axis=1)
    peak = np.max(np.abs(mono))
    b = 60.0 / 96
    for k in range(int(len(mono) / SR / (2 * b))):
        n = int(rng.uniform(0.06, 0.15) * SR)
        f = np.linspace(rng.uniform(1300, 1800), rng.uniform(2800, 4200), n)
        squeak = np.sin(2 * np.pi * np.cumsum(f) / SR) * (1 + 0.6 * rng.standard_normal(n))
        squeak = mm.bandpass(squeak, 1200, 5000) * np.hanning(n)
        squeak *= peak * gain(-9.0 + rng.normal(0, 2)) / np.max(np.abs(squeak))
        t = (k * 2 + 1.75) * b
        i = int(t * SR)
        if i + n < len(mono):
            T.x[i:i + n, :] += squeak[:, None]
            T.labels[i // FRAME:(i + n) // FRAME] = 2
    return T


def main():
    print('Speech (real recordings)')
    save('speech_male', speech(MALE))
    save('speech_female', speech(FEMALE))
    save('speech_fast', speech(MALE[:4] + FEMALE[:4], fast=True))
    print('Sung vocals (real S sounds)')
    save('vocal_male', sung(MALE, 45, 64))
    save('vocal_female', sung(FEMALE, 60, 76))
    save('vocal_breathy', sung(FEMALE, 60, 74, s_level=-9.0, breath=0.09, brightness=1.6))
    save('vocal_backing', sung(FEMALE + MALE, 52, 67, voices=3, s_chance=0.5))
    save('vocal_harsh', sung(FEMALE, 58, 74, s_level=1.0))
    print('Drums')
    save('overheads_cymbals', real_kit('oh', ride_bars=(2, 3, 6, 7)))
    save('room_splashy', real_kit('room', ride_bars=(2, 3, 6, 7)))
    save('hihat_bright', hihat_track())
    save('ride_clangy', real_kit('oh', ride_bars=tuple(range(8)), bell=True, crash_every=8))
    print('Guitars and the mix')
    save('gtr_fizz', from_material('gtr_distorted'))
    save('gtr_squeaks', squeaks())
    save('mix_song', from_material('mix_song'), -6.0)


if __name__ == '__main__':
    main()

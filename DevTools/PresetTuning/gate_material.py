#!/usr/bin/env python3
"""
Material for tuning Easy Gate's presets. Each track is saved three ways: the
mix (what the gate gets), and its two parts separately - the sound you want
to keep, and the bleed or noise you don't - plus the times where each wanted
hit or phrase starts. So the check can measure both what's kept and what's
removed.

Levels as in make_material.py: single tracks peak at -8 dBFS.
"""
import json
import os

import numpy as np
from scipy import signal

import make_material as mm

SR = mm.SR
OUT = '/home/claude/presetcheck/gate_material'
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(77)
kit = mm.kit


def gain(db):
    return 10.0 ** (db / 20.0)


def later(x, ms):
    """Bleed reaches the other mics a little later (sound takes ~3 ms per metre)."""
    n = int(ms * 0.001 * SR)
    return np.concatenate([np.zeros(n), x])[:len(x)]


def crash():
    """A crash cymbal: bright noise ringing for a couple of seconds (no sample of one in the kit)."""
    n = int(2.5 * SR)
    t = np.arange(n) / SR
    x = rng.standard_normal(n) * (np.exp(-t / 0.7) + 0.6 * np.exp(-t / 0.08))
    return mm.bandpass(x, 3000, 14000) / 3.0


def pink(n, seed):
    r = np.random.default_rng(seed)
    white = r.standard_normal(n)
    b, a = [0.049922035, -0.095993537, 0.050612699, -0.004408786], [1, -2.494956002, 2.017265875, -0.522189400]
    return signal.lfilter(b, a, white) * 8.0


def hum(n, base=50.0, seed=3):
    """Mains hum with its buzzy harmonics, as a guitar or bass amp makes it."""
    t = np.arange(n) / SR
    x = np.zeros(n)
    r = np.random.default_rng(seed)
    for k, db in [(1, 0), (2, -6), (3, -4), (4, -12), (5, -10), (7, -16), (9, -20), (11, -24)]:
        x += gain(db) * np.sin(2 * np.pi * base * k * t + r.uniform(0, 2 * np.pi))
    return x / np.max(np.abs(x))


class Kit:
    """One performance, heard by each close mic: rock beat at 100 BPM, tom fills, a crash every other bar."""

    def __init__(self, seconds=16):
        b = 0.6
        self.n = int(seconds * SR)
        self.parts = {k: np.zeros(self.n) for k in ['kick', 'snare', 'hats', 'toms', 'crash']}
        self.onsets = {k: [] for k in self.parts}
        cr = crash()

        def add(part, sound, t, db):
            i = int(round(t * SR))
            if i >= self.n:
                return
            m = min(len(sound), self.n - i)
            self.parts[part][i:i + m] += sound[:m] * gain(db)
            self.onsets[part].append(t)

        for bar in range(int(seconds / (4 * b))):
            t0 = bar * 4 * b
            fill = bar % 2 == 1
            for h in ([0, 1.5, 2] if not fill else [0]):
                add('kick', kit['kick'], t0 + h * b, mm.vel(1.2))
            for s in ([1, 3] if not fill else [1]):
                add('snare', kit['snare'], t0 + s * b, mm.vel(1.2))
            for e in range(8 if not fill else 4):
                add('hats', mm.cut(kit['hihat'], 0.3), t0 + e * b / 2, (1 if e % 2 == 0 else -2) + mm.vel(1.0))
            if bar % 2 == 0:
                add('crash', cr, t0, -4 + mm.vel(1.0))
            if fill:                                             # a fill over beats 3 and 4, high tom to floor tom
                for k in range(8):
                    tom = ['tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom3', 'tom3'][k]
                    add('toms', kit[tom], t0 + 2 * b + k * b / 4, (-2 if k % 2 else 0) + mm.vel(1.5))

        for k in self.parts:
            self.parts[k] = mm.normalised(self.parts[k])

    def mic(self, wanted, bleed):
        """The close mic on 'wanted', with the rest of the kit bleeding in: {part: (dB, ms later, darker above Hz or None)}."""
        keep = self.parts[wanted].copy()
        other = np.zeros(self.n)
        for part, (db, ms, tone) in bleed.items():
            x = later(self.parts[part], ms)
            if tone:
                x = mm.lowpass(x, tone)
            other += x * gain(db)
        return keep, other, self.onsets[wanted]


def riff(seconds=16):
    """Metal rhythm guitar with stops: palm-muted chugs, a stabbed chord, then silence."""
    T, b = mm.Track(seconds), 0.6
    onsets = []
    for bar in range(int(seconds / (4 * b))):
        t0 = bar * 4 * b
        if bar % 4 == 3:                                     # a big chord left to ring, then a long stop
            for n in ['E2', 'B2', 'E3']:
                T.add(mm.egtr.note(n, 1.6, 0.08), t0, mm.vel(0.8))
            onsets.append(t0)
            continue
        for k in range(8):                                   # beats 1 and 2: chugs in 16ths
            for n in ['E2', 'B2']:
                T.add(mm.lowpass(mm.egtr.note(n, 0.11, 0.015), 1500), t0 + k * b / 4, (1 if k % 4 == 0 else 0) + mm.vel(0.8))
            onsets.append(t0 + k * b / 4)
        for n in ['G2', 'D3', 'G3']:                         # beat 3: a short stabbed chord
            T.add(mm.egtr.note(n, 0.5, 0.04), t0 + 2 * b, mm.vel(0.8))
        onsets.append(t0 + 2 * b)
    return mm.distort(T.x, 8.0), onsets                     # (beat 4: silence)


def clean_part(seconds=16):
    """Clean guitar: picked chords that ring for three beats, then a rest."""
    T, b = mm.Track(seconds), 0.6
    chords = [['E3', 'G3', 'B3', 'E4'], ['C3', 'E3', 'G3', 'C4'], ['A2', 'E3', 'A3', 'C4'], ['D3', 'A3', 'D4', 'F#4']]
    onsets = []
    for bar in range(int(seconds / (4 * b))):
        for k, n in enumerate(chords[bar % 4]):                # an arpeggio, each note left to ring
            t = bar * 4 * b + k * b / 2
            T.add(mm.egtr.note(n, 3 * b - k * b / 2, 0.12), t, mm.vel(1.0) - 4)
            onsets.append(t)
    return T.x, onsets


def bass_with_rests(seconds=16):
    """Fingered bass: a riff in 8ths with a rest at the end of each bar."""
    T, b = mm.Track(seconds), 0.6
    roots = ['E1', 'G1', 'A1', 'C2']
    onsets = []
    for bar in range(int(seconds / (4 * b))):
        root = mm.midi(roots[bar % 4])
        for k in range(6):                                   # 3 beats of 8ths, then a rest
            T.add(mm.bass.note(root + [0, 0, 12, 0, 7, 10][k], b / 2 * 0.85, 0.015), bar * 4 * b + k * b / 2, (2 if k == 0 else 0) + mm.vel(2.0))
            onsets.append(bar * 4 * b + k * b / 2)
    return T.x, onsets


def phrases(x, quiet_db=-40.0, gap=0.25):
    """Where the phrases (or notes) of a melodic part start: after at least 'gap' seconds under quiet_db."""
    env = np.abs(x)
    win = int(0.005 * SR)
    frames = env[:len(env) // win * win].reshape(-1, win).max(axis=1)
    loud = frames > np.max(frames) * gain(quiet_db)
    starts, quiet_for = [], gap
    for i, is_loud in enumerate(loud):
        if is_loud and quiet_for >= gap:
            starts.append(i * win / SR)
        quiet_for = 0.0 if is_loud else quiet_for + win / SR
    return starts


def save(name, keep, other, onsets, peak_db=-8.0):
    mix = keep + other
    scale = gain(peak_db) / np.max(np.abs(mix))
    for suffix, x in [('', mix), ('.keep', keep), ('.other', other)]:
        stereo = np.stack([x, x], axis=1) * scale
        mm.wavfile.write(f'{OUT}/{name}{suffix}.wav', SR, stereo.astype(np.float32))
    with open(f'{OUT}/{name}.json', 'w') as f:
        json.dump({'onsets': [round(t, 4) for t in onsets]}, f)
    keep_db = 20 * np.log10(np.max(np.abs(keep)) * scale)
    other_db = 20 * np.log10(np.max(np.abs(other)) * scale + 1e-12)
    print(f'  {name:22s} keep peaks {keep_db:6.1f} dBFS, bleed/noise peaks {other_db:6.1f} dBFS, {len(onsets)} hits/phrases')


def main():
    k = Kit()

    # Close mics on a kit: their own drum, plus the rest of the kit a little later
    save('toms', *k.mic('toms', {'snare': (-13, 1.5, None), 'kick': (-22, 2.0, None), 'hats': (-20, 2.0, None), 'crash': (-14, 2.5, None)}))
    save('snare', *k.mic('snare', {'kick': (-15, 1.0, 3000), 'hats': (-14, 0.8, None), 'toms': (-16, 1.5, None), 'crash': (-20, 2.5, None)}))
    save('kick', *k.mic('kick', {'snare': (-18, 1.2, 6000), 'hats': (-26, 1.5, 6000), 'toms': (-18, 1.5, None), 'crash': (-28, 2.5, 6000)}))

    # Room mics: the whole kit in a big room. The snare's close mic (above) can be its key.
    whole = sum(k.parts[p] * gain(db) for p, db in [('kick', 0), ('snare', 0), ('hats', -8), ('toms', -2), ('crash', -10)])
    room = mm.room_reverb(whole, t60=1.4, predelay=0.015, tone=6000, seed=4)
    save('room', 0.3 * mm.normalised(whole) + mm.normalised(room), np.zeros(k.n), k.onsets['snare'])

    # A high-gain guitar amp: hiss and hum when you're not playing
    g, onsets = riff()
    g = mm.normalised(g)
    n = len(g)
    amp = gain(-44) * mm.normalised(hum(n, 50.0)) + gain(-44) * mm.normalised(mm.highpass(pink(n, 5), 200))
    save('guitar_noisy', g, amp, onsets)

    # A clean guitar through a buzzy amp
    c, onsets = clean_part()
    c = mm.normalised(c)
    buzz = gain(-50) * mm.normalised(hum(len(c), 50.0, seed=6)) + gain(-54) * mm.normalised(mm.highpass(pink(len(c), 7), 200))
    save('guitar_clean_noisy', c, buzz, onsets)

    # A bass amp with a quieter hum
    bass, onsets = bass_with_rests()
    bass = mm.normalised(bass)
    noise = gain(-50) * mm.normalised(hum(len(bass), 50.0, seed=8)) + gain(-56) * mm.normalised(pink(len(bass), 9))
    save('bass_noisy', bass, noise, onsets)

    # A vocal recorded in a home studio: room noise, plus the backing track leaking from the headphones
    v = mm.normalised(mm.sing(20, 52, 71, 120, sd=2.0, brightness=1.0, phrase_db=(0, 2)))
    leak = mm.highpass(mm.lowpass(whole[:len(v)] if len(whole) >= len(v) else np.resize(whole, len(v)), 5000), 300)
    room_noise = gain(-50) * mm.normalised(pink(len(v), 12)) + gain(-40) * mm.normalised(leak)
    save('vocal_noisy', v, room_noise, phrases(v, gap=0.08))

    # Speech (a podcast or voice-over) with room noise
    s = mm.normalised(mm.speech())
    save('speech_noisy', s, gain(-46) * mm.normalised(pink(len(s), 13)), phrases(s, gap=0.08))


if __name__ == '__main__':
    main()

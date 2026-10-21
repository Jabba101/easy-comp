#!/usr/bin/env python3
"""
Builds realistic test material for checking the Easy plugins' presets:
48 kHz stereo float WAVs, one per kind of track.

Made from real drum hits, real instrument notes (CC-BY samples) and a few
real recordings, arranged into parts, plus a little synthesis where no
recording was available (808, synths, electric piano, a sung-vocal
stand-in). Single tracks peak at -8 dBFS (a typical home-recording level),
buses and mixes at -6 dBFS, finished masters at -1 dBFS.
"""
import glob
import os
import re
import subprocess

import numpy as np
from scipy import signal
from scipy.io import wavfile

SR = 48000
SAMPLES = '/home/claude/samples'
OUT = '/home/claude/presetcheck/material'
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(2026)

TRACK_PEAK, BUS_PEAK, MASTER_PEAK = -8.0, -6.0, -1.0


def gain(db):
    return 10.0 ** (db / 20.0)


def decode(path, channels=1):
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', str(channels),
                          '-ar', str(SR), '-'], capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype=np.float32).astype(np.float64)
    return x.reshape(-1, channels) if channels > 1 else x


def trim_start(x, rel=0.02):
    """Drop the silence before the first sound, so hits land on the beat."""
    level = np.abs(x)
    above = np.nonzero(level > rel * level.max())[0]
    return x[max(0, above[0] - int(0.001 * SR)):] if len(above) else x


def cut(x, seconds, fade=0.02):
    n = min(len(x), max(1, int(seconds * SR)))
    y = x[:n].copy()
    f = min(int(fade * SR), n)
    if f > 0:
        y[n - f:] *= np.linspace(1.0, 0.0, f)
    return y


def lowpass(x, hz, order=2):
    return signal.sosfilt(signal.butter(order, hz, 'lowpass', fs=SR, output='sos'), x, axis=0)


def highpass(x, hz, order=2):
    return signal.sosfilt(signal.butter(order, hz, 'highpass', fs=SR, output='sos'), x, axis=0)


def bandpass(x, lo, hi, order=2):
    return signal.sosfilt(signal.butter(order, [lo, hi], 'bandpass', fs=SR, output='sos'), x, axis=0)


def loop_to(x, seconds):
    n = int(seconds * SR)
    reps = int(np.ceil(n / len(x)))
    return np.concatenate([x] * reps, axis=0)[:n]


def room_reverb(x, t60=0.8, predelay=0.008, tone=6000.0, seed=1):
    """A simple room: decaying filtered noise as the impulse response."""
    r = np.random.default_rng(seed)
    n = int(t60 * 1.2 * SR)
    t = np.arange(n) / SR
    ir = r.standard_normal(n) * np.exp(-6.91 * t / t60)
    ir = lowpass(ir, tone)
    ir = np.concatenate([np.zeros(int(predelay * SR)), ir])
    ir /= np.sqrt(np.sum(ir ** 2))
    return signal.fftconvolve(x, ir)[:len(x)]


class Track:
    def __init__(self, seconds):
        self.x = np.zeros(int(seconds * SR))

    def add(self, sound, t, db=0.0):
        i = int(round(t * SR))
        if i >= len(self.x) or i < 0:
            return
        n = min(len(sound), len(self.x) - i)
        self.x[i:i + n] += sound[:n] * gain(db)


def save(name, x, peak_db):
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    x = x * (gain(peak_db) / np.max(np.abs(x)))
    wavfile.write(f'{OUT}/{name}.wav', SR, x.astype(np.float32))
    rms = 20 * np.log10(np.sqrt(np.mean(x ** 2)) + 1e-12)
    print(f'  {name:24s} {len(x) / SR:5.1f} s   peak {peak_db:5.1f}   rms {rms:6.1f} dBFS')
    return x


def normalised(x, peak_db=0.0):
    return x * (gain(peak_db) / np.max(np.abs(x)))


def vel(sd):
    return rng.normal(0.0, sd)


# ---------------------------------------------------------------- note banks
NOTE_INDEX = {'C': 0, 'Cs': 1, 'D': 2, 'Ds': 3, 'E': 4, 'F': 5, 'Fs': 6, 'G': 7, 'Gs': 8, 'A': 9, 'As': 10, 'B': 11}


def midi(name):
    m = re.fullmatch(r'([A-G])(#?)(\d)', name)
    return 12 * (int(m.group(3)) + 1) + NOTE_INDEX[m.group(1) + ('s' if m.group(2) else '')]


def hz(note):
    return 440.0 * 2.0 ** ((note - 69) / 12.0)


class Bank:
    def __init__(self, folder):
        self.files = {}
        for f in glob.glob(f'{SAMPLES}/tonejs-instruments/samples/{folder}/*.wav'):
            m = re.fullmatch(r'([A-G]s?)(\d)\.wav', os.path.basename(f))
            if m:
                self.files[12 * (int(m.group(2)) + 1) + NOTE_INDEX[m.group(1)]] = f
        self.cache = {}

    def note(self, note, seconds=None, fade=0.03):
        if isinstance(note, str):
            note = midi(note)
        nearest = min(self.files, key=lambda k: (abs(k - note), k))
        if nearest not in self.cache:
            self.cache[nearest] = normalised(trim_start(decode(self.files[nearest])))
        x = self.cache[nearest]
        if note != nearest:
            ratio = 2.0 ** ((note - nearest) / 12.0)
            x = np.interp(np.arange(0, len(x) - 1, ratio), np.arange(len(x)), x)
        return cut(x, seconds, fade) if seconds else x.copy()


def drum(path):
    return normalised(trim_start(decode(f'{SAMPLES}/tonejs-audio/{path}')))


kit = {k: drum(f'drum-samples/acoustic-kit/{k}.mp3') for k in ['kick', 'snare', 'hihat', 'tom1', 'tom2', 'tom3']}


# ---------------------------------------------------------------- drums
def kick_rock():
    T, b = Track(16), 0.6                                   # 100 BPM
    for bar in range(7):
        t0 = bar * 4 * b
        for h in ([0, 2, 2.5] if bar % 2 else [0, 1.5, 2]):
            T.add(kit['kick'], t0 + h * b, vel(1.5) - (2 if h % 1 else 0))
        for s in [1, 3]:
            T.add(kit['snare'], t0 + s * b, -18 + vel(1))       # snare spill into the kick mic
    return T.x


def kick_metal():
    T, b = Track(14), 60 / 170
    sixteenth = b / 4
    t = 0.0
    while t < 13.6:
        bar_beat = (t / b) % 8
        if bar_beat < 6 or (t / sixteenth) % 2 < 1:          # double kick, with a few gaps
            T.add(cut(kit['kick'], sixteenth + 0.01), t, vel(1.0))
        t += sixteenth
    for k in range(int(13.6 / b)):
        if k % 2:
            T.add(kit['snare'], k * b, -20 + vel(1))
    return T.x


def kick_hiphop():
    stem = trim_start(decode(f'{SAMPLES}/tonejs-audio/loop/kick.mp3'))
    return loop_to(stem, 16)


def snare_rock():
    T, b = Track(16), 0.6
    for beat in range(26):
        if beat % 2:
            T.add(kit['snare'], beat * b, vel(1.5))
        else:
            T.add(kit['kick'], beat * b, -14 + vel(1))
        T.add(kit['hihat'], beat * b, -16 + vel(1))
        T.add(kit['hihat'], beat * b + b / 2, -19 + vel(1))
    return T.x


def snare_funk():
    T, b = Track(16), 60 / 96
    s16 = b / 4
    for i in range(int(16 / s16)):
        beat, pos = divmod(i, 4)
        if pos == 0 and beat % 2:
            T.add(kit['snare'], i * s16, vel(1.5))
        elif pos in (1, 3) and rng.random() < 0.45:
            T.add(kit['snare'], i * s16, -18 + vel(2.5))         # ghost notes
        if pos % 2 == 0:
            T.add(kit['hihat'], i * s16, -15 + vel(1))
    return T.x


def snare_jazz():
    real = trim_start(decode(f'{SAMPLES}/librosa-data/audio/snare-accelerate.hq.ogg'))
    T, b = Track(10), 60 / 140
    for i in range(int(10 / (b / 3))):
        if i % 3 != 1 and rng.random() < 0.35:                  # comping on a swung triplet grid
            T.add(kit['snare'], i * b / 3, rng.uniform(-14, -2))
        if i % 6 == 0:
            T.add(kit['hihat'], i * b / 3, -20)
    return np.concatenate([normalised(real[:int(8 * SR)], -2), T.x])


def toms():
    T, b = Track(16), 0.6
    for bar in range(7):
        t0 = bar * 4 * b
        for beat in range(4):
            T.add(kit['kick'], t0 + beat * b, -22) if beat % 2 == 0 else T.add(kit['snare'], t0 + beat * b, -14)
        if bar % 2:
            for k in range(8):                                  # a fill over beats 3 and 4
                T.add(kit[['tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom3', 'tom3'][k]],
                      t0 + 2 * b + k * b / 4, vel(2.0))
    return T.x


def hihat():
    T, b = Track(16), 0.6
    for i in range(52):
        T.add(cut(kit['hihat'], 0.25), i * b / 2, (3 if i % 2 == 0 else 0) + vel(1.5))
        if i % 4 == 2:
            T.add(kit['snare'], i * b / 2, -10 + vel(1))         # snare spill into the hat mic
        if i % 4 == 0:
            T.add(kit['kick'], i * b / 2, -24)
    return T.x


def programmed_kit(seconds=16, kick=0.0, snare=0.0, hats=0.0, toms_db=0.0, b=0.6):
    T = Track(seconds)
    bars = int(seconds / (4 * b))
    for bar in range(bars):
        t0 = bar * 4 * b
        for h in ([0, 2, 2.5] if bar % 2 else [0, 1.5, 2]):
            T.add(kit['kick'], t0 + h * b, kick + vel(1.2) - (2 if h % 1 else 0))
        for s in [1, 3]:
            T.add(kit['snare'], t0 + s * b, snare + vel(1.2))
        for e in range(8):
            if not (bar % 4 == 3 and e >= 4):
                T.add(cut(kit['hihat'], 0.3), t0 + e * b / 2, hats + (2 if e % 2 == 0 else -1) + vel(1.2))
        if bar % 4 == 3:
            for k in range(8):
                T.add(kit[['tom1', 'tom1', 'tom2', 'tom2', 'tom3', 'tom3', 'tom3', 'tom3'][k]],
                      t0 + 2 * b + k * b / 4, toms_db + vel(1.5))
    return T.x


def breaks():
    loops = [trim_start(decode(f'{SAMPLES}/tonejs-audio/drum-samples/loops/{n}.mp3')) for n in ['break28', 'break12', 'break29']]
    return np.concatenate([normalised(l) for l in loops])[:int(16 * SR)]


def electronic_drums():
    parts = [trim_start(decode(f'{SAMPLES}/tonejs-audio/loop/{n}.mp3')) for n in ['kick', 'snare', 'hh']]
    return loop_to(parts[0], 16) + 0.7 * loop_to(parts[1], 16) + 0.35 * loop_to(parts[2], 16)


def percussion():
    conga = trim_start(decode(f'{SAMPLES}/tonejs-audio/drum-samples/conga-rhythm.mp3'))
    hand = trim_start(decode(f'{SAMPLES}/tonejs-audio/drum-samples/handdrum-loop.mp3'))
    return np.concatenate([loop_to(normalised(conga), 8), loop_to(normalised(hand), 8)])


# ---------------------------------------------------------------- bass
bass = Bank('bass-electric')


def bass_line(T, b, note_gap=0.85, sd=2.5, pick=False, notes_per_bar=8, bars=7, lowest='E1'):
    roots = [midi('E1'), midi('C#2'), midi('A1'), midi('B1')]
    shape = [0, 0, 12, 0, 7, 0, 12, 10]
    step = 4 * b / notes_per_bar
    for bar in range(bars):
        root = roots[bar % 4]
        for k in range(notes_per_bar):
            t = bar * 4 * b + k * step
            T.add(bass.note(root + shape[k % 8], step * note_gap, 0.015), t, (2 if k == 0 else 0) + vel(sd))
            if pick:
                T.add(highpass(rng.standard_normal(int(0.004 * SR)), 2000) * 0.05, t)
    return T


def bass_fingered():
    return bass_line(Track(16), 0.6).x


def bass_picked():
    return bass_line(Track(16), 0.6, note_gap=0.8, sd=1.5, pick=True).x


def bass_slap():
    T, b = Track(16), 0.6
    roots = [midi('E1'), midi('G1'), midi('A1'), midi('G1')]
    for bar in range(7):
        root = roots[bar % 4]
        for k in range(16):
            t = bar * 4 * b + k * b / 4
            if k % 4 == 0 or k % 8 == 3:
                T.add(bass.note(root, 0.22, 0.01), t, vel(1.5))                      # thumb
            elif k % 4 == 2:
                T.add(bass.note(root + 12, 0.12, 0.01), t, 6 + vel(1.5))             # pop
            elif rng.random() < 0.5:
                T.add(bass.note(root, 0.03, 0.005), t, -14 + vel(2))                 # ghost
    return T.x


def bass_upright():
    T, b = Track(16), 0.5                                      # walking, 120 BPM
    walk = [midi(n) for n in ['A1', 'C#2', 'E2', 'F#2', 'G2', 'F#2', 'E2', 'C#2', 'D2', 'F#2', 'A2', 'B2', 'C3', 'B2', 'A2', 'F#2']]
    for i in range(32):
        T.add(lowpass(bass.note(walk[i % len(walk)], b * 0.95, 0.04), 900), i * b, vel(3.5) + (1 if i % 2 == 0 else 0))
    return T.x


def bass_808():
    T, b = Track(16), 60 / 85
    pattern = [(0, midi('F1')), (1.75, midi('F1')), (2.5, midi('G#1')), (4, midi('C2')), (5.5, midi('A#1')), (6.75, midi('G1'))]
    for rep in range(int(16 / (8 * b)) + 1):
        for i, (beat, note) in enumerate(pattern):
            start = rep * 8 * b + beat * b
            nxt = pattern[i + 1][0] if i + 1 < len(pattern) else 8
            length = (nxt - beat) * b
            t = np.arange(int(length * SR)) / SR
            f = hz(note) * (1.0 + 1.2 * np.exp(-t / 0.025))
            x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-t / 0.7)
            x = np.tanh(1.8 * x)
            T.add(cut(x, length, 0.02), start, vel(1.0))
    return T.x


def bass_metal():
    T, b = Track(14), 60 / 160
    t, k = 0.0, 0
    while t < 13.6:
        note = midi('E1') if (k // 16) % 4 != 3 else midi('G1')
        T.add(bass.note(note, b / 4 * 0.9, 0.008), t, (2 if k % 4 == 0 else 0) + vel(1.0))
        t += b / 4
        k += 1
    x = normalised(T.x)
    return lowpass(np.tanh(4.0 * x), 3000) + 0.4 * x


def saw(f, n, detune_cents=0.0, phase=0.0):
    ph = phase + np.cumsum(np.full(n, f * 2 ** (detune_cents / 1200)) / SR)
    return 2.0 * (ph % 1.0) - 1.0


def synth_voice(note, seconds, attack=0.005, release=0.05, cutoff=3000.0, env_cutoff=None, decay=None, voices=(0.0,), sub=0.0):
    n = int((seconds + release) * SR)
    x = sum(saw(hz(note), n, d, rng.random()) for d in voices) / len(voices)
    if sub:
        x += sub * np.sin(2 * np.pi * hz(note - 12) * np.arange(n) / SR)
    t = np.arange(n) / SR
    if env_cutoff:                                             # a filter that closes over time
        lo, hi, tau = env_cutoff
        blocks = []
        zi = None
        sos_cache = {}
        for start in range(0, n, 256):
            fc = lo + (hi - lo) * np.exp(-start / SR / tau)
            key = int(fc / 50)
            if key not in sos_cache:
                sos_cache[key] = signal.butter(2, max(80.0, key * 50.0), 'lowpass', fs=SR, output='sos')
            sos = sos_cache[key]
            if zi is None:
                zi = np.zeros((sos.shape[0], 2))
            y, zi = signal.sosfilt(sos, x[start:start + 256], zi=zi)
            blocks.append(y)
        x = np.concatenate(blocks)
    else:
        x = lowpass(x, cutoff)
    amp = np.minimum(1.0, t / attack)
    if decay:
        amp *= np.exp(-t / decay)
    amp *= np.where(t > seconds, np.maximum(0.0, 1 - (t - seconds) / release), 1.0)
    return x * amp


def bass_synth():
    T, b = Track(16), 60 / 124
    roots = [midi('A1'), midi('A1'), midi('F1'), midi('G1')]
    for bar in range(int(16 / (4 * b))):
        for k in range(8):
            if k % 2 == 1 or rng.random() < 0.3:               # off-beat bass, EDM style
                T.add(synth_voice(roots[bar % 4] + (12 if k == 7 else 0), b / 2 * 0.8, 0.003, 0.03,
                                  env_cutoff=(300, 2500, 0.08), voices=(-6, 6), sub=0.6),
                      bar * 4 * b + k * b / 2, vel(1.0))
    return T.x


# ---------------------------------------------------------------- guitars
egtr, agtr, ngtr = Bank('guitar-electric'), Bank('guitar-acoustic'), Bank('guitar-nylon')


def gtr_clean():
    T, b = Track(16), 60 / 90
    chords = [['E3', 'G3', 'B3', 'E4'], ['C3', 'E3', 'G3', 'C4'], ['G2', 'B2', 'D3', 'G3'], ['D3', 'F#3', 'A3', 'D4']]
    order = [0, 1, 2, 3, 2, 1, 2, 3]
    for bar in range(int(16 / (4 * b))):
        chord = chords[bar % 4]
        for k in range(8):
            ring = 4 * b - k * b / 2
            T.add(egtr.note(chord[order[k]], ring, 0.06), bar * 4 * b + k * b / 2, (2 if k == 0 else 0) + vel(3.0))
    return T.x


def gtr_funk():
    T, b = Track(16), 0.6
    voicing = ['G#3', 'D4', 'F#4', 'B4']
    pattern = '1x0x1x1x0x1x1xx1'
    for bar in range(7):
        for k, c in enumerate(pattern):
            t = bar * 4 * b + k * b / 4
            if c == '1':
                for j, n in enumerate(voicing):
                    T.add(egtr.note(n, 0.075, 0.015), t + j * 0.003, (3 if k % 4 == 0 else 0) + vel(2.5) - 6)
            elif c == 'x':
                T.add(bandpass(rng.standard_normal(int(0.025 * SR)), 1000, 4000) * 0.05, t, vel(2))
    return T.x


def distort(x, drive=6.0):
    x = normalised(x)
    return highpass(lowpass(np.tanh(drive * x), 5000, 4), 90)


def gtr_distorted():
    T, b = Track(16), 0.6
    chords = [['E2', 'B2', 'E3'], ['G2', 'D3', 'G3'], ['A2', 'E3', 'A3'], ['C3', 'G3', 'C4']]
    for bar in range(7):
        chord = chords[bar % 4]
        if bar % 2 == 0:
            for n in chord:
                T.add(egtr.note(n, 4 * b * 0.98, 0.05), bar * 4 * b, vel(1.0))
        else:
            for k in range(8):                                  # palm-muted chugs
                for n in chord[:2]:
                    T.add(lowpass(egtr.note(n, 0.13, 0.02), 1500), bar * 4 * b + k * b / 2, (1 if k % 2 == 0 else 0) + vel(1.0))
    return distort(T.x, 8.0)


def gtr_lead():
    T, b = Track(16), 60 / 100
    phrase = [('E4', 1), ('G4', .5), ('A4', .5), ('B4', 2), ('D5', .5), ('B4', .5), ('A4', 1), ('G4', 1.5), ('E4', .5),
              ('D4', 1), ('E4', 3), ('B4', .5), ('C5', .5), ('D5', 1), ('E5', 2), ('D5', .5), ('B4', .5), ('A4', 1), ('B4', 3)]
    t = 0.0
    while t < 15:
        for n, d in phrase:
            T.add(egtr.note(n, d * b + 0.04, 0.04), t, vel(2.0))
            t += d * b
        t += 2 * b
    return distort(T.x, 3.0) * 0.7 + 0.3 * normalised(T.x)


def strum(T, bank, chord, t, down=True, db=0.0, gap=0.008, ring=0.6):
    strings = chord if down else list(reversed(chord))[:4]
    for j, n in enumerate(strings):
        T.add(bank.note(n, ring, 0.03), t + j * gap, db + (0 if down else -4))


def gtr_acoustic_strum():
    T, b = Track(16), 0.6
    chords = [['G2', 'B2', 'D3', 'G3', 'B3', 'G4'], ['E2', 'B2', 'E3', 'G3', 'B3', 'E4'],
              ['C3', 'E3', 'G3', 'C4', 'E4'], ['D3', 'A3', 'D4', 'F#4']]
    strokes = [(0, True), (1, True), (1.5, False), (2.5, False), (3, True), (3.5, False)]
    for bar in range(7):
        for i, (beat, down) in enumerate(strokes):
            nxt = strokes[i + 1][0] if i + 1 < len(strokes) else 4
            strum(T, agtr, chords[bar % 4], bar * 4 * b + beat * b, down,
                  (1.5 if beat in (0, 2) else 0) + vel(1.5), ring=(nxt - beat) * b + 0.02)
    return T.x


def gtr_acoustic_fingerpicked():
    T, b = Track(16), 60 / 80
    chords = [['C3', 'G3', 'E4', 'C4'], ['A2', 'E3', 'C4', 'A3'], ['F2', 'C3', 'A3', 'F3'], ['G2', 'D3', 'B3', 'G3']]
    for bar in range(int(16 / (4 * b))):
        bass_notes, treble = chords[bar % 4][:2], chords[bar % 4][2:]
        swell = 3 * np.sin(2 * np.pi * bar / 4)
        for k in range(8):
            t = bar * 4 * b + k * b / 2
            if k % 2 == 0:
                T.add(ngtr.note(bass_notes[(k // 2) % 2], 2 * b, 0.05), t, swell + vel(2.5))
            else:
                T.add(ngtr.note(treble[(k // 2) % 2], 1.5 * b, 0.05), t, swell - 2 + vel(3.5))
    return T.x


# ---------------------------------------------------------------- keys
piano, organ_bank = Bank('piano'), Bank('organ')


def piano_pop():
    T, b = Track(16), 0.6
    chords = [['C4', 'E4', 'G4'], ['A3', 'C4', 'E4'], ['F3', 'A3', 'C4'], ['G3', 'B3', 'D4']]
    lows = ['C2', 'A1', 'F1', 'G1']
    melody = ['E5', 'D5', 'C5', 'D5', 'E5', 'G5', 'E5', 'D5']
    for bar in range(7):
        t0 = bar * 4 * b
        for beat in range(4):
            for n in chords[bar % 4]:
                T.add(piano.note(n, b * 0.95, 0.05), t0 + beat * b, (2 if beat == 0 else 0) + vel(2.0) - 4)
        for half in range(2):
            low = midi(lows[bar % 4])
            for n in (low, low + 12):
                T.add(piano.note(n, 2 * b, 0.08), t0 + half * 2 * b, vel(2.0) - 3)
        for k in range(8):
            T.add(piano.note(melody[(k + bar) % 8], b / 2 + 0.05, 0.04), t0 + k * b / 2, vel(2.5))
    return T.x


def piano_ballad():
    T, b = Track(18), 60 / 70
    chords = [['C3', 'G3', 'C4', 'E4'], ['A2', 'E3', 'A3', 'C4'], ['F2', 'C3', 'F3', 'A3'], ['G2', 'D3', 'G3', 'B3']]
    for bar in range(int(18 / (4 * b))):
        swell = -3 + 5 * np.sin(2 * np.pi * bar / 5)
        notes = chords[bar % 4]
        for k in range(8):
            ring = 4 * b - k * b / 2 + 0.3
            T.add(piano.note(notes[[0, 1, 2, 3, 2, 1, 2, 3][k]], ring, 0.3), bar * 4 * b + k * b / 2, swell + vel(2.5))
    return T.x


def real(name, start, seconds, channels=2):
    x = decode(f'{SAMPLES}/librosa-data/audio/{name}.hq.ogg', channels)
    return x[int(start * SR):int((start + seconds) * SR)]


def electric_piano():
    T, b = Track(16), 60 / 85
    chords = [['D3', 'F3', 'A3', 'C4'], ['G2', 'F3', 'B3', 'E4'], ['C3', 'E3', 'G3', 'B3'], ['A2', 'G3', 'C4', 'E4']]
    hits = [(0, 1.4), (1.5, 0.4), (2.5, 1.4)]
    for bar in range(int(16 / (4 * b))):
        for beat, length in hits:
            for n in chords[bar % 4]:
                m = midi(n)
                seconds = length * b
                nn = int((seconds + 0.3) * SR)
                t = np.arange(nn) / SR
                f = hz(m)
                tone = np.sin(2 * np.pi * f * t) + 0.25 * np.sin(2 * np.pi * 2 * f * t) * np.exp(-t / 0.4)
                tone += 0.3 * np.sin(2 * np.pi * 7.1 * f * t) * np.exp(-t / 0.03)          # the "tine" bark
                tone *= np.exp(-t / 1.2) * np.minimum(1, t / 0.002)
                tone *= np.where(t > seconds, np.maximum(0, 1 - (t - seconds) / 0.3), 1)
                T.add(tone, bar * 4 * b + beat * b, vel(2.5) - 6)
    return T.x


def organ():
    T, b = Track(16), 0.6
    chords = [['C3', 'E3', 'G3', 'C4'], ['F3', 'A3', 'C4', 'F4'], ['G3', 'B3', 'D4', 'G4'], ['C3', 'E3', 'G3', 'C4']]
    for k in range(13):
        pedal = 2 * np.sin(2 * np.pi * k / 6)
        for n in chords[k % 4]:
            T.add(organ_bank.note(n, 2 * b + 0.02, 0.02), k * 2 * b, pedal - 6)
    return T.x


def synth_pad():
    T, b = Track(18), 60 / 110
    chords = [['A3', 'C4', 'E4', 'G4'], ['F3', 'A3', 'C4', 'E4'], ['C4', 'E4', 'G4', 'B4'], ['G3', 'B3', 'D4', 'F#4']]
    for k in range(int(18 / (8 * b))):
        for n in chords[k % 4]:
            T.add(synth_voice(midi(n), 8 * b, 0.4, 0.6, cutoff=2200, voices=(-9, 0, 9)), k * 8 * b, -8)
    return T.x


def synth_lead():
    T, b = Track(16), 60 / 124
    phrase = ['A4', 'C5', 'E5', 'A5', 'G5', 'E5', 'D5', 'E5', 'C5', 'D5', 'E5', 'G5', 'A5', 'G5', 'E5', 'D5']
    for k in range(int(16 / (b / 2))):
        if (k // 16) % 2 == 1 and k % 16 >= 12:
            continue
        T.add(synth_voice(midi(phrase[k % 16]), b / 2 * 0.9, 0.004, 0.04, env_cutoff=(1500, 6000, 0.15), voices=(-5, 5)),
              k * b / 2, (2 if k % 4 == 0 else 0) + vel(1.0))
    return T.x


def synth_pluck():
    T, b = Track(16), 60 / 124
    arp = ['A3', 'E4', 'A4', 'C5', 'E5', 'C5', 'A4', 'E4']
    for k in range(int(16 / (b / 4))):
        T.add(synth_voice(midi(arp[k % 8]), 0.12, 0.002, 0.15, env_cutoff=(300, 8000, 0.05), decay=0.18),
              k * b / 4, vel(1.5))
    return T.x


# ---------------------------------------------------------------- voices
VOWELS = {'a': (800, 1200, 2500), 'e': (500, 1900, 2550), 'i': (320, 2300, 3000), 'o': (500, 900, 2400), 'u': (350, 800, 2300)}


def formant_filter(x, vowel, gains=(0.0, -5.0, -11.0)):
    y = 0.05 * x
    for fc, g in zip(VOWELS[vowel], gains):
        bw = 80 + fc * 0.06
        b_, a_ = signal.iirpeak(fc, fc / bw, fs=SR)
        y = y + gain(g) * signal.lfilter(b_, a_, x)
    return y


def sung_note(note, seconds, brightness=1.3, vibrato_cents=25.0, breath=0.02, scream=0.0, swell=0.0):
    n = int((seconds + 0.08) * SR)
    t = np.arange(n) / SR
    ramp = np.clip((t - 0.15) / 0.3, 0, 1)
    f0 = hz(note) * 2 ** (vibrato_cents / 1200 * np.sin(2 * np.pi * 5.5 * t) * ramp) * (1 + 0.003 * rng.standard_normal())
    phase = 2 * np.pi * np.cumsum(f0) / SR
    x = np.zeros(n)
    for k in range(1, int(7500 / hz(note))):
        x += np.sin(k * phase) * k ** -brightness
    x /= np.max(np.abs(x))
    if scream:
        x = x * (1 + scream * lowpass(rng.standard_normal(n), 300)) + scream * 0.5 * bandpass(rng.standard_normal(n), 500, 6000)
    x += breath * bandpass(rng.standard_normal(n), 1500, 6000)
    x = formant_filter(x, rng.choice(list(VOWELS)))
    env = np.minimum(1, t / 0.03) * np.where(t > seconds, np.maximum(0, 1 - (t - seconds) / 0.08), 1)
    env *= gain(swell * np.clip(t / max(seconds, 0.01), 0, 1))
    env *= 1 + 0.04 * np.sin(2 * np.pi * rng.uniform(3, 7) * t)
    return x * env


def consonant(kind):
    if kind == 's':
        return bandpass(rng.standard_normal(int(0.08 * SR)), 5000, 9500) * np.hanning(int(0.08 * SR)) * 0.35
    if kind == 't':
        n = int(0.015 * SR)
        return highpass(rng.standard_normal(n), 1500) * np.exp(-np.arange(n) / (0.004 * SR)) * 0.6
    n = int(0.06 * SR)                                         # m / n: a quiet hum
    return np.sin(2 * np.pi * 220 * np.arange(n) / SR) * np.hanning(n) * 0.15


def sing(seconds, low, high, bpm, sd=2.5, brightness=1.3, phrase_db=(0.0,), scream=0.0, breathiness=0.02, swell=0.0,
         consonants=0.6):
    T, b = Track(seconds), 60 / bpm
    t, phrase = 0.3, 0
    note = (low + high) // 2
    while t < seconds - 1:
        level = phrase_db[phrase % len(phrase_db)]
        for _ in range(rng.integers(5, 10)):
            d = rng.choice([0.5, 0.5, 1.0, 1.0, 1.5, 2.0]) * b
            note = int(np.clip(note + rng.choice([-4, -2, -1, 0, 1, 2, 3, 5]), low, high))
            if rng.random() < consonants:
                c = consonant(rng.choice(['s', 't', 'm']))
                T.add(c, t, level + vel(2))
                t += len(c) / SR * 0.7
            T.add(sung_note(note, d * 0.92, brightness, breath=breathiness, scream=scream, swell=swell), t, level + vel(sd))
            t += d
        T.add(bandpass(rng.standard_normal(int(0.3 * SR)), 1000, 4000) * np.hanning(int(0.3 * SR)) * 0.01, t + 0.2)
        t += rng.choice([1.0, 1.5, 2.0]) * b
        phrase += 1
    return T.x + 0.06 * room_reverb(T.x, 0.6)


def backing_vocals():
    T, b = Track(16), 60 / 90
    chords = [(57, 60, 64), (53, 57, 60), (55, 59, 62), (52, 55, 59)]
    for k in range(int(16 / (2 * b))):
        for n in chords[k % 4]:
            T.add(sung_note(n, 2 * b * 0.95, brightness=1.6, vibrato_cents=12), k * 2 * b + rng.uniform(0, 0.03), vel(1.5) - 4)
    return T.x + 0.1 * room_reverb(T.x, 0.8)


def speech():
    parts = [decode(f'{SAMPLES}/librosa-data/audio/{n}.hq.ogg') for n in ['198-209-0000', '3436-172162-0000', '5703-47212-0000']]
    gap = np.zeros(int(0.6 * SR))
    return np.concatenate([parts[0], gap, normalised(parts[1], -1.5), gap, normalised(parts[2], 0.5)])[:int(30 * SR)]


# ---------------------------------------------------------------- strings, brass, wind
violin, cello, sax, flute = Bank('violin'), Bank('cello'), Bank('saxophone'), Bank('flute')
trumpet, trombone, horn = Bank('trumpet'), Bank('trombone'), Bank('french-horn')


def legato(bank, notes, b, seconds, sd=2.5, swell=3.0):
    T = Track(seconds)
    t = 0.2
    while t < seconds - 1:
        for n, d in notes:
            x = bank.note(n, d * b + 0.06, 0.06)
            x *= gain(swell * np.sin(np.pi * np.arange(len(x)) / len(x)) - swell / 2)
            T.add(x, t, vel(sd))
            t += d * b
        t += 1.5 * b
    return T.x


def violin_solo():
    return legato(violin, [('A4', 1), ('C5', .5), ('E5', .5), ('A5', 2), ('G5', 1), ('E5', 1), ('F5', 1.5), ('D5', .5),
                           ('E5', 3)], 60 / 80, 16)


def cello_solo():
    return legato(cello, [('C3', 2), ('G3', 1), ('E3', 1), ('F3', 1.5), ('A3', .5), ('G3', 2), ('D3', 1), ('E3', 1),
                          ('C3', 3)], 60 / 70, 16)


def sax_solo():
    return legato(sax, [('D4', .5), ('F4', .5), ('A4', 1), ('G4', .5), ('F4', .5), ('D4', 1), ('C4', .5), ('D4', 1.5),
                        ('A4', .5), ('C5', .5), ('D5', 1), ('C5', .5), ('A4', .5), ('G4', 2)], 60 / 100, 16, sd=3.5, swell=4)


def flute_solo():
    return legato(flute, [('E5', 1), ('G5', .5), ('A5', .5), ('B5', 2), ('A5', 1), ('G5', 1), ('E5', 2), ('D5', 1),
                          ('E5', 3)], 60 / 90, 16, sd=2.0)


def brass_section():
    T, b = Track(16), 0.6
    chords = [('C5', 'G4', 'E4', 'C4'), ('F5', 'C5', 'A4', 'F4'), ('G5', 'D5', 'B4', 'G4'), ('C5', 'G4', 'E4', 'C4')]
    hits = [(0, 0.25, 2), (1.5, 0.25, 0), (2.5, 1.2, 1), (3.75, 0.2, -1)]
    for bar in range(7):
        chord = chords[bar % 4]
        for beat, length, accent in hits:
            t = bar * 4 * b + beat * b
            T.add(trumpet.note(chord[0], length * b + 0.05, 0.04), t, accent + vel(2))
            T.add(trumpet.note(chord[1], length * b + 0.05, 0.04), t, accent - 2 + vel(2))
            T.add(horn.note(midi(chord[2]) - 12, length * b + 0.05, 0.04), t, accent - 3 + vel(2))
            T.add(trombone.note(midi(chord[3]) - 12, length * b + 0.05, 0.04), t, accent - 1 + vel(2))
    return T.x


def trumpet_solo():
    x = real('sorohanro_-_solo-trumpet-06', 0, 6, 1)
    return np.concatenate([x, 0.7 * x, 1.1 * x])


# ---------------------------------------------------------------- build
def main():
    m = {}
    print('Drums')
    m['kick_rock'] = save('kick_rock', kick_rock(), TRACK_PEAK)
    m['kick_metal'] = save('kick_metal', kick_metal(), TRACK_PEAK)
    m['kick_hiphop'] = save('kick_hiphop', kick_hiphop(), TRACK_PEAK)
    m['snare_rock'] = save('snare_rock', snare_rock(), TRACK_PEAK)
    m['snare_funk'] = save('snare_funk', snare_funk(), TRACK_PEAK)
    m['snare_jazz'] = save('snare_jazz', snare_jazz(), TRACK_PEAK)
    m['toms'] = save('toms', toms(), TRACK_PEAK)
    m['hihat'] = save('hihat', hihat(), TRACK_PEAK)
    overheads = programmed_kit(16, kick=-10, snare=-2, hats=-3, toms_db=-3)
    m['overheads_kit'] = save('overheads_kit', overheads, TRACK_PEAK)
    m['overheads_breaks'] = save('overheads_breaks', breaks(), TRACK_PEAK)
    m['room'] = save('room', 0.25 * normalised(overheads) + normalised(room_reverb(overheads, 0.9, 0.012, 5000)), TRACK_PEAK)
    kitmix = programmed_kit(16, kick=0, snare=-1, hats=-9, toms_db=-4)
    m['drumbus_rock'] = save('drumbus_rock', kitmix + 0.3 * room_reverb(kitmix, 0.9), BUS_PEAK)
    m['drumbus_breaks'] = save('drumbus_breaks', decode(f'{SAMPLES}/tonejs-audio/drum-samples/loops/blueyellow.mp3', 2)[:16 * SR], BUS_PEAK)
    m['drumbus_electronic'] = save('drumbus_electronic', electronic_drums(), BUS_PEAK)
    m['percussion'] = save('percussion', percussion(), TRACK_PEAK)

    print('Bass')
    m['bass_fingered'] = save('bass_fingered', bass_fingered(), TRACK_PEAK)
    m['bass_picked'] = save('bass_picked', bass_picked(), TRACK_PEAK)
    m['bass_slap'] = save('bass_slap', bass_slap(), TRACK_PEAK)
    m['bass_upright'] = save('bass_upright', bass_upright(), TRACK_PEAK)
    m['bass_808'] = save('bass_808', bass_808(), TRACK_PEAK)
    m['bass_metal'] = save('bass_metal', bass_metal(), TRACK_PEAK)
    m['bass_synth'] = save('bass_synth', bass_synth(), TRACK_PEAK)
    m['bass_drumbass'] = save('bass_drumbass', real('admiralbob77_-_Choice_-_Drum-bass', 0, 25), TRACK_PEAK)

    print('Guitars')
    m['gtr_clean'] = save('gtr_clean', gtr_clean(), TRACK_PEAK)
    m['gtr_funk'] = save('gtr_funk', gtr_funk(), TRACK_PEAK)
    m['gtr_distorted'] = save('gtr_distorted', gtr_distorted(), TRACK_PEAK)
    m['gtr_lead'] = save('gtr_lead', gtr_lead(), TRACK_PEAK)
    m['gtr_acoustic_strum'] = save('gtr_acoustic_strum', gtr_acoustic_strum(), TRACK_PEAK)
    m['gtr_acoustic_finger'] = save('gtr_acoustic_finger', gtr_acoustic_fingerpicked(), TRACK_PEAK)

    print('Keys')
    m['piano_pop'] = save('piano_pop', piano_pop(), TRACK_PEAK)
    m['piano_ballad'] = save('piano_ballad', piano_ballad(), TRACK_PEAK)
    m['piano_ragtime'] = save('piano_ragtime', real('442789__lena-orsa__happy-music-pistachio-ice-cream-ragtime', 5, 30), TRACK_PEAK)
    m['epiano'] = save('epiano', electric_piano(), TRACK_PEAK)
    m['organ'] = save('organ', organ(), TRACK_PEAK)
    m['synth_pad'] = save('synth_pad', synth_pad(), TRACK_PEAK)
    m['synth_lead'] = save('synth_lead', synth_lead(), TRACK_PEAK)
    m['synth_pluck'] = save('synth_pluck', synth_pluck(), TRACK_PEAK)

    print('Vocals')
    m['vocal_pop'] = save('vocal_pop', sing(20, 57, 76, 100, sd=2.5, brightness=1.3), TRACK_PEAK)
    m['vocal_rock'] = save('vocal_rock', sing(20, 52, 71, 120, sd=2.0, brightness=1.0, phrase_db=(0, 2)), TRACK_PEAK)
    m['vocal_ballad'] = save('vocal_ballad', sing(24, 55, 74, 70, sd=3.0, brightness=1.5, phrase_db=(-9, -8, 0, 1),
                                                  breathiness=0.05), TRACK_PEAK)
    m['vocal_jazz'] = save('vocal_jazz', sing(24, 55, 74, 90, sd=4.0, brightness=1.4, phrase_db=(-4, 0, -2), swell=4.0),
                           TRACK_PEAK)
    m['vocal_country'] = save('vocal_country', sing(20, 55, 74, 110, sd=2.5, brightness=1.15, phrase_db=(-2, 0)), TRACK_PEAK)
    m['vocal_metal'] = save('vocal_metal', sing(20, 50, 66, 140, sd=1.5, brightness=0.9, scream=0.8), TRACK_PEAK)
    m['vocal_backing'] = save('vocal_backing', backing_vocals(), TRACK_PEAK)
    m['speech'] = save('speech', speech(), TRACK_PEAK)

    print('Strings, brass & wind')
    m['strings_section'] = save('strings_section', real('Hungarian_Dance_number_5_-_Allegro_in_F_sharp_minor_(string_orchestra)', 0, 24),
                                TRACK_PEAK)
    m['violin_solo'] = save('violin_solo', violin_solo(), TRACK_PEAK)
    m['cello_solo'] = save('cello_solo', cello_solo(), TRACK_PEAK)
    m['brass_section'] = save('brass_section', brass_section(), TRACK_PEAK)
    m['trumpet_solo'] = save('trumpet_solo', trumpet_solo(), TRACK_PEAK)
    m['sax_solo'] = save('sax_solo', sax_solo(), TRACK_PEAK)
    m['flute_solo'] = save('flute_solo', flute_solo(), TRACK_PEAK)

    print('Buses and mixes')
    def stereo(x):
        return x if x.ndim == 2 else np.stack([x, x], axis=1)

    def length(x, seconds):
        return x[:int(seconds * SR)]

    def mix(parts, seconds=16):
        out = np.zeros((int(seconds * SR), 2))
        for x, db, pan in parts:
            s = stereo(length(x, seconds))
            s = s * np.array([np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)]) * np.sqrt(2)
            out[:len(s)] += s * gain(db)
        return out

    m['bus_instruments'] = save('bus_instruments', mix([(m['gtr_acoustic_strum'], 0, -0.5), (m['piano_pop'], -2, 0.4),
                                                        (m['gtr_clean'], -3, 0.0)]), BUS_PEAK)
    m['mix_rock'] = save('mix_rock', mix([(m['drumbus_rock'], 0, 0), (m['bass_picked'], -1, 0),
                                          (m['gtr_distorted'], -4, -0.7), (np.roll(m['gtr_distorted'], 2400, axis=0), -4, 0.7),
                                          (m['vocal_rock'], 0, 0)]), BUS_PEAK)
    m['mix_edm'] = save('mix_edm', mix([(m['drumbus_electronic'], 0, 0), (m['bass_synth'], -1, 0), (m['synth_pad'], -8, 0),
                                        (m['synth_lead'], -6, 0.2), (m['synth_pluck'], -10, -0.3)]), BUS_PEAK)
    m['mix_hiphop'] = save('mix_hiphop', mix([(m['kick_hiphop'], 0, 0), (m['bass_808'], -3, 0), (m['hihat'], -12, 0.2),
                                              (m['speech'], -2, 0), (m['epiano'], -10, -0.2)]), BUS_PEAK)
    m['mix_acoustic'] = save('mix_acoustic', mix([(m['gtr_acoustic_finger'], -2, -0.3), (m['vocal_country'], 0, 0),
                                                  (m['bass_upright'], -4, 0), (m['overheads_kit'], -12, 0)]), BUS_PEAK)
    m['mix_song'] = save('mix_song', real("Karissa_Hobbs_-_Let's_Go_Fishin'", 20, 30), BUS_PEAK)
    m['mix_jazz'] = save('mix_jazz', real('Kevin_MacLeod_-_Vibe_Ace', 0, 30), BUS_PEAK)
    m['mix_orchestral'] = save('mix_orchestral', real('Kevin_MacLeod_-_P_I_Tchaikovsky_Dance_of_the_Sugar_Plum_Fairy', 0, 30), BUS_PEAK)
    m['master_song'] = save('master_song', m['mix_song'], MASTER_PEAK)
    m['master_rock'] = save('master_rock', m['mix_rock'], -3.0)


if __name__ == '__main__':
    main()

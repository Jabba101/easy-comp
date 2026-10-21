"""Prototype: MODERN (feed-forward) vs VINTAGE (zero-delay feedback) gain computers,
and the THRUST detector tilt. Not shipped."""
import numpy as np
from scipy.io import wavfile
from scipy import signal
import sys

SR = 48000
M = '/home/claude/presetcheck/material'


def load(name, seconds=None):
    sr, x = wavfile.read(f'{M}/{name}.wav')
    x = x.astype(np.float64)
    if seconds:
        x = x[:int(seconds * SR)]
    return x


def coef(ms):
    return np.exp(-1.0 / max(1.0, ms * 1e-3 * SR))


def gc(over, slope, knee):
    """Reduction (dB) for 'over' dB above threshold, soft knee 'knee' dB wide."""
    h = knee / 2
    if over <= -h:
        return 0.0
    if over >= h:
        return slope * over
    return slope * (over + h) ** 2 / (2 * knee)


def modern(levels, T, R, att, rel, knee=6.0):
    s = 1 - 1 / R
    a, rl = coef(att), coef(rel)
    r = 0.0
    out = np.empty_like(levels)
    for n, L in enumerate(levels):
        t = gc(L - T, s, knee)
        c = a if t > r else rl
        r = t + c * (r - t)
        out[n] = r
    return out


def solve_zdf(L, T, s, ko, c, rprev):
    """r = c*rprev + (1-c)*gc(L - r - T) with slope s and knee ko (output-domain)."""
    h = ko / 2
    b = 1 - c
    # region above knee: L - r - T >= h
    r = (c * rprev + b * s * (L - T)) / (1 + b * s)
    if L - r - T >= h:
        return r
    # below knee: r = c*rprev, valid if L - r - T <= -h
    r = c * rprev
    if L - r - T <= -h:
        return r
    # in knee: r = c rprev + b s (u - r)^2/(2 ko), u = L - T + h
    u = L - T + h
    A = b * s / (2 * ko)
    # A (u - r)^2 - r + c rprev = 0 -> A r^2 - (2 A u + 1) r + (A u^2 + c rprev) = 0
    if A < 1e-12:
        return c * rprev
    B = -(2 * A * u + 1)
    C = A * u * u + c * rprev
    disc = max(0.0, B * B - 4 * A * C)
    r1 = (-B - np.sqrt(disc)) / (2 * A)
    return r1


def vintage(levels, T, R, att, rel, alpha=1.0, cap=6.0, knee_in=9.0):
    s = R - 1
    F = min((1 + s) ** alpha, cap)
    a, rl = coef(att * F), coef(rel * F)
    ko = knee_in * 2 / (R + 1) if R > 1 else knee_in   # output-domain knee giving ~knee_in dB in input terms
    r = 0.0
    out = np.empty_like(levels)
    for n, L in enumerate(levels):
        ra = solve_zdf(L, T, s, ko, a, r)
        if ra >= r:
            r = ra
        else:
            r = solve_zdf(L, T, s, ko, rl, r)
        out[n] = r
    return out


def thrust_filter(x, lo=20.0, hi=4000.0, sections=4):
    """+3 dB/octave from lo to hi (flat outside), 0 dB at 1 kHz."""
    q = (hi / lo) ** (1.0 / (2 * sections))
    y = x.copy()
    f = lo
    for i in range(sections):
        fz, fp = f, f * q
        wz, wp = 2 * np.pi * fz, 2 * np.pi * fp
        # analog (s + wz)/(s + wp) -> bilinear with prewarp
        b, a = signal.bilinear([1, wz], [1, wp], fs=SR)
        y = signal.lfilter(b, a, y, axis=0)
        f = fp * q
    # normalise at 1 kHz
    w, h = signal.freqz(*signal.bilinear([1, 2 * np.pi * lo], [1, 2 * np.pi * lo * q], fs=SR), worN=[1000.0], fs=SR)
    return y


def thrust_response():
    lo, hi, sections = 20.0, 4000.0, 4
    q = (hi / lo) ** (1.0 / (2 * sections))
    freqs = np.array([20, 30, 50, 100, 200, 500, 1000, 2000, 4000, 8000, 16000.0])
    H = np.ones(len(freqs), dtype=complex)
    f = lo
    for i in range(sections):
        b, a = signal.bilinear([1, 2 * np.pi * f], [1, 2 * np.pi * f * q], fs=SR)
        _, h = signal.freqz(b, a, worN=freqs, fs=SR)
        H *= h
        f = f * q * q
    db = 20 * np.log10(np.abs(H))
    ref = np.interp(1000, freqs, db)
    print('THRUST response (dB, 0 at 1 kHz):', ' '.join(f'{int(fr)}:{d - ref:+.1f}' for fr, d in zip(freqs, db)))


def peak_level_db(x):
    p = np.max(np.abs(x), axis=1) if x.ndim > 1 else np.abs(x)
    return 20 * np.log10(np.maximum(p, 1e-6))



def onsets(x, min_gap_ms=80):
    mono = np.max(np.abs(x), axis=1) if x.ndim > 1 else np.abs(x)
    # fast and slow envelopes in dB; onset when fast exceeds slow by 9 dB
    def env(p, ms):
        c = np.exp(-1/(ms*1e-3*SR)); y = np.empty_like(p); s = 0.0
        for i, v in enumerate(p):
            s = v if v > s else v + c*(s - v); y[i] = s
        return y
    f = 20*np.log10(env(mono, 2) + 1e-6); s = 20*np.log10(env(mono, 60) + 1e-6)
    s = np.concatenate([np.full(240, -120.0), s[:-240]])   # slow envelope 5 ms ago
    idx = []; last = -10**9
    for i in np.nonzero(f - s > 9)[0]:
        if i - last > min_gap_ms*1e-3*SR:
            idx.append(i); last = i
    return np.array(idx)

def report(name, T, R, att, rel, variants):
    x = load(name, 8.0)
    L = peak_level_db(x)
    on = onsets(x)
    on = on[on + 4800 < len(L)]
    print(f'{name}: T {T} R {R} att {att} rel {rel}  ({len(on)} onsets)')
    mono = x.mean(axis=1) if x.ndim > 1 else x
    for label, fn in variants:
        g = fn(L, T, R, att, rel)
        y = mono * 10 ** (-g / 20)
        crest_out = 20*np.log10(np.max(np.abs(y))/np.sqrt(np.mean(y**2)))
        prof = ''
        if len(on):
            prof = ' GR after onset ' + ' '.join(f'{ms}ms:{np.mean(g[on + int(ms*48)]):4.1f}' for ms in (0, 2, 5, 10, 30, 80))
        print(f'   {label:12s} avg {np.mean(g):5.2f} max {np.max(g):5.2f} p90 {np.percentile(g,90):5.2f} crest {crest_out:5.1f}{prof}')


def vintage2(levels, T, R, att, rel, Fa, Fr, knee_in=9.0):
    s = R - 1
    a, rl = coef(att * Fa(s)), coef(rel * Fr(s))
    ko = knee_in * 2 / (R + 1)
    r = 0.0
    out = np.empty_like(levels)
    for n, L in enumerate(levels):
        rising = gc(L - T - r, s, ko) > r
        r = solve_zdf(L, T, s, ko, a if rising else rl, r)
        out[n] = r
    return out


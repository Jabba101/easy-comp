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


if __name__ == '__main__':
    thrust_response()
    # static curves (a steady sine at each level, after settling)
    for R in (2.0, 4.0, 10.0):
        row_m, row_v = [], []
        for over in (-6, -3, 0, 3, 6, 12, 24):
            L = np.full(48000, -20.0 + over)
            row_m.append(modern(L, -20.0, R, 10, 100)[-1])
            row_v.append(vintage(L, -20.0, R, 10, 100)[-1])
        print(f'R {R:4.1f} static GR  over: ' + ' '.join(f'{o:+d}' for o in (-6, -3, 0, 3, 6, 12, 24)))
        print('   modern  ', ' '.join(f'{v:5.2f}' for v in row_m))
        print('   vintage ', ' '.join(f'{v:5.2f}' for v in row_v))

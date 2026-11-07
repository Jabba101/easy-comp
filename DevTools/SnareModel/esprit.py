"""Fit a hit's rings as exponentially decaying sines (ESPRIT) - pitch, decay, strength, phase for each."""
import numpy as np
from scipy.signal import butter, sosfiltfilt

def poles(y, fs, order):
    N = len(y); L = N // 2
    H = np.lib.stride_tricks.sliding_window_view(y, L)[: N - L + 1]
    U, s, Vt = np.linalg.svd(H.T, full_matrices=False)
    Us = U[:, :order]
    Phi = np.linalg.lstsq(Us[:-1], Us[1:], rcond=None)[0]
    z = np.linalg.eigvals(Phi)
    return np.angle(z) * fs / (2 * np.pi), -np.log(np.abs(z)) * fs

def fit_band(hit, sr, lo, hi, dec, seg_s, order, start_ms=3.0, max_t60=3.0):
    sos = butter(8, [lo, hi], 'band', fs=sr, output='sos')
    full = sosfiltfilt(sos, hit)
    i0 = int(start_ms / 1000 * sr)
    f, d = poles(full[i0:i0 + int(seg_s * sr)][::dec], sr / dec, order)
    keep = (f > lo) & (f < hi)
    f, d = f[keep], np.clip(d[keep], 6.9078 / max_t60, 600)
    # strengths and phases: least squares against the whole ring (from start_ms), poles fixed
    y = full[i0:]; t = np.arange(len(y)) / sr
    step = 4; ts = t[::step]; ys = y[::step]
    V = np.exp(-d[None, :] * ts[:, None]) * np.exp(2j * np.pi * f[None, :] * ts[:, None])
    A = np.concatenate([V.real, -V.imag], axis=1)
    c = np.linalg.lstsq(A, ys, rcond=None)[0]
    a = c[:len(f)] + 1j * c[len(f):]
    modes = []
    for fi, di, ai in zip(f, d, a):
        amp = np.abs(ai) * np.exp(di * start_ms / 1000); ph = np.angle(ai) - 2 * np.pi * fi * start_ms / 1000
        modes.append({'f': float(fi), 't60': float(6.9078 / di), 'amp': float(amp), 'phase': float(ph)})
    return modes, full

def fit_modes(hit, sr, lowest=60.0, start_ms=3.0):
    low, a = fit_band(hit, sr, lowest, 1900, 12, 1.2, 90, start_ms=start_ms)
    high, b = fit_band(hit, sr, 1900, 5500, 4, 0.4, 70, start_ms=start_ms)
    modes = low + high
    modes.sort(key=lambda m: -m['amp'] * np.sqrt(m['t60']))
    return modes

def synth(modes, n, sr, lift=None):
    t = np.arange(n) / sr; y = np.zeros(n)
    for m in modes:
        ph = 2 * np.pi * (np.cumsum(m['f'] * lift) / sr if lift is not None else m['f'] * t) + m['phase']
        y += m['amp'] * np.exp(-6.9078 * t / m['t60']) * np.cos(ph)
    return y

import numpy as np
from lib import *
from scipy import signal

def thrust_sos(lo=20.0, hi=4000.0, sections=4):
    q = (hi / lo) ** (1.0 / (2 * sections))
    sos = []; f = lo
    for i in range(sections):
        b, a = signal.bilinear([1, 2*np.pi*f], [1, 2*np.pi*f*q], fs=SR)
        sos.append(np.concatenate([np.pad(b, (0, 1)), np.pad(a, (0, 1))]))
        f = f * q * q
    sos = np.array(sos)
    _, h = signal.sosfreqz(sos, worN=[1000.0], fs=SR)
    sos[0, :3] /= abs(h[0])
    return sos

sos = thrust_sos()
for name in ['drumbus_rock', 'drumbus_breaks', 'drumbus_electronic', 'mix_song', 'mix_rock', 'master_rock', 'bass_picked', 'kick_rock', 'snare_rock', 'overheads_kit', 'vocal_rock', 'gtr_distorted', 'piano_pop']:
    try:
        x = load(name, 8.0)
    except Exception as e:
        continue
    y = signal.sosfilt(sos, x, axis=0)
    p = lambda z: 20*np.log10(np.percentile(np.max(np.abs(z), axis=1) if z.ndim > 1 else np.abs(z), 99.9))
    r = lambda z: 10*np.log10(np.mean(z**2))
    print(f'{name:20s} peak(99.9%) {p(x):6.1f} -> {p(y):6.1f} ({p(y)-p(x):+5.1f})   rms {r(x):6.1f} -> {r(y):6.1f} ({r(y)-r(x):+5.1f})')

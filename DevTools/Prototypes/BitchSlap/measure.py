import numpy as np
from scipy.special import erfc
import slap

SR = slap.SR


def ned(h, window_ms=20.0):
    """Normalised echo density (Abel & Huang): ~1 means as dense as noise."""
    w = int(window_ms * 1e-3 * SR) | 1
    win = np.hanning(w); win /= win.sum()
    out = []
    for c in range(h.shape[1]):
        x = h[:, c]
        res = np.zeros(len(x))
        half = w // 2
        for i in range(half, len(x) - half, 48):
            seg = x[i - half:i + half + 1]
            sigma = np.sqrt(np.sum(win * seg ** 2))
            res[i] = np.sum(win * (np.abs(seg) > sigma)) / erfc(1 / np.sqrt(2))
        out.append(res)
    return np.mean(out, axis=0)


def report(name, ir):
    e = np.sum(ir ** 2, axis=1)
    peak = max(e[i:i + 480].mean() for i in range(0, 9600, 48))
    d = ned(ir[:int(0.12 * SR)])
    lev = ' '.join(f'{ms}:{10*np.log10(e[max(0,int(ms*48)-240):int(ms*48)+240].mean()/peak+1e-30):5.1f}' for ms in (2, 5, 10, 20, 40, 80, 150, 300))
    den = ' '.join(f'{ms}:{d[int(ms*48)]:.2f}' for ms in (5, 10, 20, 40, 80))
    corr = np.corrcoef(ir[:int(0.3*SR), 0], ir[:int(0.3*SR), 1])[0, 1]
    print(f'{name:10s} level dB {lev} | density {den} | L/R corr {corr:+.2f}')

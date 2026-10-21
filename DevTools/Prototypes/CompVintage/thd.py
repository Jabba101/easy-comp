import numpy as np
from lib import *

V = [('modern', modern),
     ('A R/1', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: 1)),
     ('B R/sqR', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: np.sqrt(1+s)))]

def thd_db(x, g, f):
    y = x * 10 ** (-g / 20)
    n = int(round(SR / f)) * int(f * 0.5)     # ~0.5 s of whole cycles at the end
    seg = y[-n:]
    spec = np.abs(np.fft.rfft(seg * np.hanning(n)))
    k = int(round(f * n / SR))
    fund = np.sqrt(np.sum(spec[k-2:k+3]**2))
    harm = np.sqrt(sum(np.sum(spec[m*k-2:m*k+3]**2) for m in range(2, 12)))
    return 20*np.log10(harm / fund)

t = np.arange(int(1.5*SR))/SR
for (R, a, r) in [(4, 10, 100), (4, 1, 50), (4, 1, 20), (10, 1, 50), (10, 0.3, 20), (2, 10, 100)]:
    for f in (40.0, 80.0, 160.0):
        for over in (6, 15):
            x = 10**((-20+over)/20)*np.sin(2*np.pi*f*t)
            L = peak_level_db(x)
            vals = [f'{lab} {thd_db(x, fn(L, -20, R, a, r), f):6.1f}' for lab, fn in V]
            print(f'R{R:<2} {a:>4}/{r:<4} {f:4.0f} Hz +{over:2d}: ' + '  '.join(vals))

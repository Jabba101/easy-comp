import numpy as np
from lib import *
exec(open('thd.py').read().split("t = np.arange")[0].split("V = [")[0])
def thd_db(x, g, f):
    y = x * 10 ** (-g / 20)
    n = int(round(SR / f)) * int(f * 0.5)
    seg = y[-n:]
    spec = np.abs(np.fft.rfft(seg * np.hanning(n)))
    k = int(round(f * n / SR))
    fund = np.sqrt(np.sum(spec[k-2:k+3]**2))
    harm = np.sqrt(sum(np.sum(spec[m*k-2:m*k+3]**2) for m in range(2, 12)))
    return 20*np.log10(harm / fund)
V = [('modern', modern),
     ('sqR', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: np.sqrt(1+s))),
     ('min(sqR,3)', lambda L,T,R,a,r: vintage2(L,T,R,a,r, lambda s: 1+s, lambda s: min(np.sqrt(1+s), 3.0)))]
t = np.arange(int(1.5*SR))/SR
for (R, a, r) in [(20, 1, 100), (20, 0.3, 50), (20, 5, 200), (10, 1, 100)]:
    for f in (40.0, 80.0):
        for over in (6, 15):
            x = 10**((-20+over)/20)*np.sin(2*np.pi*f*t)
            # plus a level step: loud for 0.5 s then 6 dB quieter (still above) to probe closed-loop release distortion
            L = peak_level_db(x)
            vals = [f'{lab} {thd_db(x, fn(L, -20, R, a, r), f):6.1f}' for lab, fn in V]
            print(f'R{R:<2} {a:>4}/{r:<4} {f:4.0f} Hz +{over:2d}: ' + '  '.join(vals))

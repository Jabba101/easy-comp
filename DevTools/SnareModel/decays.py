import sys, numpy as np, soundfile as sf
from scipy.signal import find_peaks
files = sys.argv[1:]
on = [0.000, 2.003, 4.000]
def analyse(f):
    x, sr = sf.read(f); m = x.mean(axis=1)
    res = {}
    n = 1 << 16; hop = 256; win = 4096
    for t0 in on[:2]:
        i0 = int(t0*sr); seg = m[i0:i0+int(1.9*sr)]
        frames = []
        for k in range(0, len(seg)-win, hop):
            frames.append(np.abs(np.fft.rfft(seg[k:k+win]*np.hanning(win), n)))
        S = np.array(frames); fr = np.fft.rfftfreq(n, 1/sr); tt = (np.arange(len(S))*hop + win/2)/sr
        res.setdefault('S', []).append(S)
    S = np.mean(res['S'], axis=0)
    return S, fr, tt, sr
Sref, fr, tt, sr = analyse(files[0])
# modes from the open drum, early (40-200 ms)
early = 20*np.log10(Sref[(tt>0.04)&(tt<0.25)].mean(axis=0)+1e-12); early -= early.max()
band = (fr > 100) & (fr < 4000)
pk, _ = find_peaks(early[band], prominence=5, distance=int(6/fr[1]))
cands = sorted([(early[band][p], fr[band][p]) for p in pk], reverse=True)[:22]
modes = sorted(f_ for d, f_ in cands)
print('%8s' % 'Hz', ''.join('%22s' % f.split('/')[-1].replace('SD296_','').replace('.wav','') for f in files))
table = []
spectra = [analyse(f)[0] for f in files]
for f0 in modes:
    k = np.argmin(np.abs(fr - f0)); row = []
    for S in spectra:
        lvl = 20*np.log10(S[:, k-2:k+3].max(axis=1)+1e-12)
        start = lvl[(tt>0.03)&(tt<0.06)].max()
        # fit slope over the first 30 dB of fall (from 40 ms)
        sel = (tt > 0.04); y = lvl[sel]; xx = tt[sel]
        stop = np.where(y < start - 30)[0]; end = stop[0] if len(stop) else len(y)
        end = max(end, 6)
        a, b = np.polyfit(xx[:end], y[:end], 1)
        t60 = -60/a if a < 0 else 9.9
        row.append((start, t60))
    table.append((f0, row))
ref0 = None
for f0, row in table:
    print('%8.0f' % f0, ''.join('     %6.1f dB %5.2f s' % (s - 0, t) for s, t in row))

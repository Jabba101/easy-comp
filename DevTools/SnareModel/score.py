import sys, os; sys.path.insert(0, '/home/claude/snare/tools')
import numpy as np, soundfile as sf, model, snare
from scipy.signal import butter, sosfiltfilt
SR = 48000; D = os.environ['DRUM']
def lvl(y, lo, hi, a, b):
    z = sosfiltfilt(butter(4, [lo, hi], 'band', fs=SR, output='sos'), y)[int(a * SR):int(b * SR)]
    return 10 * np.log10((z ** 2).mean() + 1e-20)
for dmp in sys.argv[1:]:
    x, sr = sf.read('/home/claude/snare/rec/%s_%s.wav' % (D, dmp)); m = x.mean(axis=1)
    reals = [m[model.onset(m, t0):model.onset(m, t0) + int(1.9 * SR)] for t0 in (0.0, 2.003)]
    fakes = [snare.render(dmp, velocity=0.8, seed=s, length=1.9) for s in (3, 4, 5)]
    rows, real_lv = [], []
    for lo, hi in ((25, 60), (60, 120), (120, 300), (300, 1000), (1000, 3000), (3000, 8000), (8000, 20000)):
        rows.append([np.mean([lvl(y, lo, hi, a, b) for y in fakes]) - np.mean([lvl(r, lo, hi, a, b) for r in reals]) for a, b in ((0, .01), (.01, .05), (.05, .3))])
        real_lv.append([np.mean([lvl(r, lo, hi, a, b) for r in reals]) for a, b in ((0, .01), (.01, .05), (.05, .3))])
    rows, real_lv = np.array(rows), np.array(real_lv)
    rows = rows[real_lv > real_lv.max() - 45]          # (only what's within 45 dB of the loudest part: the rest is inaudible)
    pk = np.mean([20 * np.log10(np.abs(y).max()) for y in fakes]) - np.mean([20 * np.log10(np.abs(r).max()) for r in reals])
    score = np.sqrt(np.mean(rows ** 2) + (pk / 2) ** 2)
    print('%s %s score %.2f | peak %+.1f dB | worst band %.1f dB | typical %.1f dB' % (os.environ.get('FITNAME', D), dmp, score, pk, np.abs(rows).max(), np.sqrt(np.mean(rows ** 2))))

"""The air push: whatever the real hit has below 150 Hz in its first 200 ms that the rings don't (stored at 6 kHz)."""
import sys, os, json; sys.path.insert(0, '/home/claude/snare/tools')
import numpy as np, soundfile as sf, model, snare
from scipy.signal import butter, sosfiltfilt
SR = 48000; L = int(0.2 * SR)
for dmp in sys.argv[1:]:
    snare.FITS[dmp].pop('push', None)
    x, _ = sf.read('/home/claude/snare/rec/%s_%s.wav' % (snare.DRUM, dmp)); m = x.mean(axis=1)
    j = model.onset(m, 0.0); r = m[max(0, j - 96):j - 96 + L + 4800]
    y = np.concatenate([np.zeros(96), snare.render(dmp, seed=3, length=(L + 4800 - 96) / SR, wires=0, click=False)])
    lp = butter(4, 150, 'low', fs=SR, output='sos')
    d = sosfiltfilt(lp, r) - sosfiltfilt(lp, y); d = d[96:96 + L]
    d *= np.where(np.arange(L) > L - 2400, 0.5 + 0.5 * np.cos(np.pi * (np.arange(L) - (L - 2400)) / 2400), 1.0)   # (fade out)
    snare.FITS[dmp]['push'] = d[::8].tolist()
    print(dmp, 'push peak %.3f' % np.abs(d).max())
json.dump(snare.FITS, open('/home/claude/snare/fit/%s.json' % snare.FITNAME, 'w'))

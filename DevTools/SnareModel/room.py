"""What the room adds: the pitches every drum shares. Each recording's fine detail (its spectrum against its own
smoothed outline) is averaged across all the drums - each drum's own rings average away, the room's stay."""
import sys; sys.path.insert(0, '/home/claude/snare/tools')
import json, numpy as np, soundfile as sf, model
from scipy.signal import find_peaks
SR = 48000
RECS = {'SD296': ['No_Dampening', 'Dampening_Gel', 'Dampening_Ring'], 'Sonor_Maple_35': ['No_Dampening', 'Dampening_Gel', 'Dampening_Ring'],
        'Kick20': ['No_Dampening', 'Cushion'], 'Tom10': ['No_Dampening', 'Dampening_Gel'], 'Tom12': ['No_Dampening', 'Dampening_Gel'],
        'Tom14': ['No_Dampening', 'Dampening_Gel']}
n = 1 << 16; fr = np.fft.rfftfreq(n, 1 / SR)
band = (fr >= 35) & (fr <= 700)
def fine(drum, dmp):
    x, sr = sf.read('/home/claude/snare/rec/%s_%s.wav' % (drum, dmp)); m = x.mean(axis=1)
    specs = []
    for t0 in (0.0, 2.003, 4.0):
        j = model.onset(m, t0); seg = m[j + int(0.01 * SR): j + int(0.9 * SR)]
        specs.append(np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2)
    db = 10 * np.log10(np.mean(specs, axis=0) + 1e-20)
    # its own outline: a 1/2-octave running average in dB
    out = np.empty_like(db)
    for i, f in enumerate(fr):
        if not band[i]: out[i] = db[i]; continue
        lo, hi = f / 2 ** 0.25, f * 2 ** 0.25; sel = (fr >= lo) & (fr <= hi); out[i] = db[sel].mean()
    return db - out
per_drum = {}
for drum, dmps in RECS.items():
    per_drum[drum] = np.mean([fine(drum, d) for d in dmps], axis=0)
avg = np.mean([per_drum[d] for d in RECS], axis=0)
agree = np.mean([per_drum[d] > 3 for d in RECS], axis=0)       # how many drums have a peak here
f_b, a_b, g_b = fr[band], avg[band], agree[band]
pk, prop = find_peaks(a_b, prominence=2.5, distance=int(4 / fr[1]))
rows = sorted([(f_b[p], a_b[p], g_b[p]) for p in pk if g_b[p] >= 0.5], key=lambda r: r[0])
print('shared peaks (Hz: average height above each drum\'s outline, share of drums peaking there)')
for f, a, g in rows: print('   %6.1f Hz  %+5.1f dB  %d of %d drums' % (f, a, round(g * len(RECS)), len(RECS)))
json.dump({'freq': fr[band].tolist(), 'avg': avg[band].tolist(), 'agree': agree[band].tolist(), 'peaks': rows}, open('/home/claude/snare/fit/room_scan.json', 'w'))

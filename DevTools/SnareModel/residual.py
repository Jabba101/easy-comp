import sys; sys.path.insert(0, '/home/claude/snare/tools')
import os, json, numpy as np, soundfile as sf, model, esprit
DRUM = os.environ.get('DRUM', 'SD296')
KICK = DRUM.startswith('Kick')
START_MS = float(os.environ.get('START_MS', '8' if KICK else '3'))
FITNAME = os.environ.get('FITNAME', DRUM)
from scipy.signal import butter, sosfiltfilt, sosfilt, firwin2, fftconvolve
SR = 48000
def env(y, ms=2.0):
    w = int(ms / 1000 * SR); return np.sqrt(np.convolve(y * y, np.ones(w) / w, 'same'))
def colour_of(y):
    n = 1 << 14; seg = y[:n] if len(y) >= n else np.pad(y, (0, n - len(y)))
    sp = np.abs(np.fft.rfft(seg * np.hanning(n))); fq = np.fft.rfftfreq(n, 1 / SR)
    sm = np.exp(np.convolve(np.log(sp + 1e-12), np.ones(61) / 61, 'same'))
    grid = np.linspace(0, SR / 2, 257); c = np.interp(grid, fq, sm); return c / c.max()
out = {}
for name in sys.argv[1:]:
    x, sr = sf.read('/home/claude/snare/rec/%s_%s.wav' % (DRUM, name)); m = x.mean(axis=1)
    res_all = []; hits = []
    for t0 in (0.0, 2.003):
        j = model.onset(m, t0); hit = m[j:j + int(1.9 * SR)]
        modes = esprit.fit_modes(hit, SR, 20.0 if KICK else 60.0, START_MS)
        res_all.append(hit - esprit.synth(modes, len(hit), SR)); hits.append((hit, modes))
    res = np.mean([r[:int(0.6 * SR)] for r in res_all], axis=0) if False else res_all[0]
    hp = sosfiltfilt(butter(6, 2500, 'high', fs=SR, output='sos'), res)
    e = env(hp, 3.0); e /= e.max()
    wire_colour = colour_of(np.concatenate([r[int(0.006 * SR):int(0.15 * SR)] for r in res_all]))
    click_colour = colour_of(np.concatenate([r[:int(0.004 * SR)] for r in res_all]))
    pk = np.argmax(e); db = 20 * np.log10(e + 1e-9)
    t20 = np.where(db[pk:] < -20)[0][0] / SR; t40 = np.where(db[pk:] < -40)[0][0] / SR
    peak_hit = np.abs(hits[0][0]).max()
    print('%-15s wires (residual >2.5k): peak %.1f ms, -20 dB %.0f ms, -40 dB %.0f ms; residual level %.1f dB below the hit peak'
          % (name, pk / SR * 1000, t20 * 1000, t40 * 1000, 20 * np.log10(np.abs(hp).max() / peak_hit)))
    out[name] = {'contact': START_MS / 1000.0, 'modes': hits[0][1], 'wireColour': wire_colour.tolist(), 'clickColour': click_colour.tolist(),
                 'wireEnv': (e[::48]).tolist(), 'wireEnvMax': float(env(hp, 3.0).max()), 'wirePeak': float(np.abs(hp).max()), 'hitPeak': float(peak_hit),
                 'clickRms': float(np.sqrt((res_all[0][:int(0.004 * SR)] ** 2).mean()))}
json.dump(out, open('/home/claude/snare/fit/%s.json' % FITNAME, 'w'))

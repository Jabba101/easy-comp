import numpy as np, json
from scipy.io import wavfile
from scipy import signal
SR = 48000; F = 240
M = '/home/claude/presetcheck/ds_material'
def follow_dec(p, k, up_ms=0.3, down_ms=12.0):
    up = np.exp(-k/(up_ms*1e-3*SR)); dn = np.exp(-k/(down_ms*1e-3*SR))
    y = np.empty_like(p); s = 0.0
    for i, v in enumerate(p):
        c = up if v > s else dn
        s = v + c*(s - v); y[i] = s
    return y
def smooth_dec(p, k, ms):
    a = np.exp(-k/(ms*1e-3*SR)); return signal.lfilter([1-a], [1, -a], p)
cache = {}
def feats(name, fromHz):
    key = (name, fromHz)
    if key in cache: return cache[key]
    sr, x = wavfile.read(f'{M}/{name}.wav'); x = x.mean(axis=1).astype(np.float64)
    lab = np.array(json.load(open(f'{M}/{name}.json'))['labels'])
    band = signal.sosfilt(signal.butter(4, fromHz, 'highpass', fs=SR, output='sos'), x)**2
    rest = signal.sosfilt(signal.butter(4, fromHz, 'lowpass', fs=SR, output='sos'), x)**2
    k = 24; n = len(x)//k
    bf = follow_dec(band[:n*k].reshape(n,k).max(axis=1), k); rf = follow_dec(rest[:n*k].reshape(n,k).max(axis=1), k)
    tt = (x**2)[:n*k].reshape(n,k).mean(axis=1)
    s300 = smooth_dec(tt, k, 300.0); s100 = smooth_dec(tt, k, 100.0)
    # a peak-ish slow follower of the total: up fast, down over 300 ms
    sf = follow_dec(tt, k, 5.0, 300.0)
    per = F//k; m = min(len(bf)//per, len(lab))
    f = lambda y, fn: fn(y[:m*per].reshape(m, per), axis=1)
    out = (f(bf, np.max), f(rf, np.mean), f(s300, np.mean), f(s100, np.mean), f(sf, np.mean), lab[:m])
    cache[key] = out; return out
def score(r, L):
    h, o = r[L==2], r[L==1]
    best = (0, 0)
    for t in np.linspace(-40, 30, 281):
        acc = 0.5*((h>t).mean() + (o<=t).mean())
        if acc > best[0]: best = (acc, t)
    return best
mats = [('speech_male',4500),('speech_female',4500),('speech_fast',4500),('vocal_male',4000),('vocal_female',5500),('vocal_harsh',5000),('vocal_breathy',5000),
        ('overheads_cymbals',3500),('room_splashy',4000),('hihat_bright',6000),('gtr_squeaks',2000),('mix_song',5000)]
refs = {
  'rest': lambda B,R,S3,S1,SF: R,
  'slow300': lambda B,R,S3,S1,SF: S3,
  'min(rest, slow300-3)': lambda B,R,S3,S1,SF: np.minimum(R, S3*10**(-0.3)),
  'min(rest, slow300-6)': lambda B,R,S3,S1,SF: np.minimum(R, S3*10**(-0.6)),
  'min(rest, slowpk-6)': lambda B,R,S3,S1,SF: np.minimum(R, SF*10**(-0.6)),
  'min(rest, slowpk-10)': lambda B,R,S3,S1,SF: np.minimum(R, SF*10**(-1.0)),
  'sqrt(rest*slow300)': lambda B,R,S3,S1,SF: np.sqrt(R*S3),
  'rest+slow300*0.1': lambda B,R,S3,S1,SF: R + 0.1*S3,
}
print(' '*22 + ''.join(f'{m[:13]:>14s}' for m,_ in mats))
for nm, fn in refs.items():
    row = []
    for name, fr in mats:
        B,R,S3,S1,SF,L = feats(name, fr)
        r = 10*np.log10((B+1e-20)/(fn(B,R,S3,S1,SF)+1e-12))
        acc, t = score(r, L)
        row.append(f'{100*acc:5.1f}@{t:5.1f}  ')
    print(f'{nm:22s}' + ''.join(f'{c:>14s}' for c in row))

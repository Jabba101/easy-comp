import numpy as np, json, sys
from scipy.io import wavfile
from scipy import signal
SR = 48000; F = 240
M = '/home/claude/presetcheck/ds_material'

def follow(p, up_ms=0.3, down_ms=12.0):
    up = np.exp(-1/(up_ms*1e-3*SR)); dn = np.exp(-1/(down_ms*1e-3*SR))
    y = np.empty_like(p); s = 0.0
    for i, v in enumerate(p):
        c = up if v > s else dn
        s = v + c*(s - v); y[i] = s
    return y

def smooth(p, ms):
    a = np.exp(-1/(ms*1e-3*SR))
    return signal.lfilter([1-a], [1, -a], p)

def frames_max(y):
    n = len(y)//F
    return y[:n*F].reshape(n, F).max(axis=1)

def frames_mean(y):
    n = len(y)//F
    return y[:n*F].reshape(n, F).mean(axis=1)

def analyse(name, fromHz):
    sr, x = wavfile.read(f'{M}/{name}.wav'); x = x.mean(axis=1).astype(np.float64)
    lab = np.array(json.load(open(f'{M}/{name}.json'))['labels'])
    sos_lo = signal.butter(4, fromHz, 'lowpass', fs=SR, output='sos')
    sos_hi = signal.butter(4, fromHz, 'highpass', fs=SR, output='sos')
    band = signal.sosfilt(sos_hi, x)**2; rest = signal.sosfilt(sos_lo, x)**2; tot = x**2
    # decimate the followers for speed: per 24 samples (0.5 ms) peak power
    k = 24; n = len(x)//k
    bd = band[:n*k].reshape(n, k).max(axis=1); rs = rest[:n*k].reshape(n, k).max(axis=1); tt = tot[:n*k].reshape(n,k).mean(axis=1)
    global SR_
    bf = follow_dec(bd, k); rf = follow_dec(rs, k)
    slow = smooth_dec(tt, k, 300.0); slow2 = smooth_dec(tt, k, 1000.0)
    per = F//k
    def fr(y, fn=np.max):
        m = len(y)//per; return fn(y[:m*per].reshape(m, per), axis=1)
    B = fr(bf); R = fr(rf, np.mean); S = fr(slow, np.mean); S2 = fr(slow2, np.mean)
    m = min(len(B), len(lab)); B, R, S, S2, L = B[:m], R[:m], S[:m], S2[:m], lab[:m]
    out = {}
    for nm, ref in [('rest(fast)', R), ('total(300ms)', S), ('total(1s)', S2), ('max(rest,tot300-10)', np.maximum(R, S*0.1))]:
        r = 10*np.log10((B+1e-20)/(ref+1e-12))
        h, o = r[L==2], r[L==1]
        # how well a threshold separates: best balanced accuracy over thresholds
        best = (0, 0)
        for t in np.linspace(-40, 30, 141):
            acc = 0.5*((h>t).mean() + (o<=t).mean()) if len(h) and len(o) else 0
            if acc > best[0]: best = (acc, t)
        out[nm] = (np.percentile(h, 50) if len(h) else 0, np.percentile(o, 99) if len(o) else 0, best)
    print(f'{name:18s}', '  '.join(f'{nm}: S50 {v[0]:5.1f} O99 {v[1]:5.1f} sep {100*v[2][0]:4.1f}% @ {v[2][1]:5.1f}' for nm, v in out.items()))

def follow_dec(p, k, up_ms=0.3, down_ms=12.0):
    up = np.exp(-k/(up_ms*1e-3*SR)); dn = np.exp(-k/(down_ms*1e-3*SR))
    y = np.empty_like(p); s = 0.0
    for i, v in enumerate(p):
        c = up if v > s else dn
        s = v + c*(s - v); y[i] = s
    return y

def smooth_dec(p, k, ms):
    a = np.exp(-k/(ms*1e-3*SR))
    return signal.lfilter([1-a], [1, -a], p)

for name, f in [('vocal_male',4000),('vocal_female',5500),('speech_male',4500),('speech_female',4500),('speech_fast',4500),('vocal_breathy',5000),('vocal_backing',4500),('vocal_harsh',5000),
                ('overheads_cymbals',3500),('hihat_bright',6000),('ride_clangy',2500),('room_splashy',4000),('gtr_squeaks',2000),('mix_song',5000)]:
    analyse(name, f)

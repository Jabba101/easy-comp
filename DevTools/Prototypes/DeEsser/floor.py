import numpy as np, json
from scipy.io import wavfile
from scipy import signal
SR = 48000; F = 240
M = '/home/claude/presetcheck/ds_material'
exec(open('refs.py').read().split("for name, f in")[0].split("def analyse")[0])   # follow, smooth etc.

def follow_dec(p, k, up_ms=0.3, down_ms=12.0):
    up = np.exp(-k/(up_ms*1e-3*SR)); dn = np.exp(-k/(down_ms*1e-3*SR))
    y = np.empty_like(p); s = 0.0
    for i, v in enumerate(p):
        c = up if v > s else dn
        s = v + c*(s - v); y[i] = s
    return y

def detect(name, fromHz, toHz=None):
    sr, x = wavfile.read(f'{M}/{name}.wav'); x = x.mean(axis=1).astype(np.float64)
    lab = np.array(json.load(open(f'{M}/{name}.json'))['labels'])
    band = signal.sosfilt(signal.butter(4, fromHz, 'highpass', fs=SR, output='sos'), x)
    if toHz: band = signal.sosfilt(signal.butter(2, toHz, 'lowpass', fs=SR, output='sos'), band)
    rest = signal.sosfilt(signal.butter(4, fromHz, 'lowpass', fs=SR, output='sos'), x)
    k = 24; n = len(x)//k
    bf = follow_dec((band**2)[:n*k].reshape(n,k).max(axis=1), k); rf = follow_dec((rest**2)[:n*k].reshape(n,k).max(axis=1), k)
    per = F//k; m = min(len(bf)//per, len(lab))
    B = bf[:m*per].reshape(m, per).max(axis=1); R = rf[:m*per].reshape(m, per).mean(axis=1)
    return B, R, lab[:m]

def sep(B, R, L, T, Fdb):
    line = R*10**(T/10) + 10**(Fdb/10)
    r = 10*np.log10((B+1e-20)/line)
    h, o = r[L==2], r[L==1]
    trig_h = (h > 0).mean(); trig_o = (o > 0).mean()
    return trig_h, trig_o

for name, f, to in [('overheads_cymbals',3500,10000),('room_splashy',4000,None),('ride_clangy',2500,6000),('hihat_bright',6000,None)]:
    B, R, L = detect(name, f, to)
    bandDb = 10*np.log10(B+1e-20)
    print(f'{name}: band level dB  harsh p10 {np.percentile(bandDb[L==2],10):.1f} p50 {np.percentile(bandDb[L==2],50):.1f}   ordinary p50 {np.percentile(bandDb[L==1],50):.1f} p90 {np.percentile(bandDb[L==1],90):.1f} p99 {np.percentile(bandDb[L==1],99):.1f}')
    best = None
    for T in [-30, -20, -15, -10, -6, 0]:
        for Fdb in np.arange(-40, -4, 1.0):
            th, to_ = sep(B, R, L, T, Fdb)
            score = 0.5*(th + (1-to_))
            if best is None or score > best[0]: best = (score, T, Fdb, th, to_)
    print(f'   best: T {best[1]} floor {best[2]:.0f} dB -> harsh frames over the line {100*best[3]:.0f}%, ordinary {100*best[4]:.1f}%  (score {100*best[0]:.1f}%)')

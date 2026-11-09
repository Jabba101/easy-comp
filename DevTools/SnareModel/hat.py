"""A closed hi-hat: metallic noise with the recording's fine colour (its tangle of high partials), each band
fading the way the real hat's does, and harder hits brighter as measured. Clean: no pulsing, no saturation.

    python3 hat.py fit <recording.wav> <name>      -> fit/<name>.json
    render(velocity, seed) from Python
"""
import sys, json, numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt, sosfilt

SR = 48000
EDGES = [300, 600, 1000, 1500, 2200, 3000, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000, 23900]
FRAMES = 300            # 300 ms of envelope, 1 ms frames
NFFT = 4096             # fine colour resolution (about 12 Hz)
V_REF = 0.8

def find_hits(m, rel=0.12, hop=0.06):
    env = np.abs(m); th = env.max() * rel; k = 0; hits = []
    while k < len(env):
        if env[k] > th:
            j = k + np.argmax(env[k:k + 240])   # (the peak, then back to the start)
            s = k - 24
            hits.append(max(s, 0)); k += int(hop * SR)
        else: k += 1
    return hits

def band_env(y, lo, hi):
    z = sosfiltfilt(butter(4, [lo, min(hi, 23900)], 'band', fs=SR, output='sos'), y)
    w = 48; e = np.convolve(z * z, np.ones(w) / w, 'same')
    return e[:FRAMES * 48:48]

def fit(path, name, start=0.0, end=None):
    x, sr = sf.read(path); assert sr == SR
    m = x.mean(axis=1) if x.ndim > 1 else x
    m = m[int(start * SR):int(end * SR) if end else None]
    hits = find_hits(m, hop=0.15)
    gaps = np.diff(hits + [len(m)]) / SR
    clean = [h for h, g in zip(hits, gaps) if g > 0.35 and h + FRAMES * 48 + NFFT < len(m)]
    pk = np.array([20 * np.log10(np.abs(m[h:h + 480]).max()) for h in hits])
    pkc = np.array([20 * np.log10(np.abs(m[h:h + 480]).max()) for h in clean])
    loud = [h for h, p in zip(clean, pkc) if p > pkc.max() - 4]
    ref_db = float(np.median([p for p in pkc if p > pkc.max() - 4]))
    # each band's envelope (power, per ms), averaged over the loud clean hits
    envs = np.array([[band_env(m[h:h + FRAMES * 48 + 4800], lo, hi) for lo, hi in zip(EDGES[:-1], EDGES[1:])] for h in loud])
    env = envs.mean(axis=0)
    # the fine colour: the hit's spectrum in its first 40 ms, flattened inside each band (the bands carry the level)
    sp = np.mean([np.abs(np.fft.rfft(np.pad(m[h:h + 1920], (0, NFFT - 1920)) * 1.0)) ** 2 for h in loud], axis=0)
    fq = np.fft.rfftfreq(NFFT, 1 / SR)
    colour = np.zeros_like(sp)
    for lo, hi in zip(EDGES[:-1], EDGES[1:]):
        k = (fq >= lo) & (fq < hi); colour[k] = sp[k] / sp[k].mean()
    # how each band follows the hit's strength: band level (first 40 ms) vs peak, over every hit (fast ones too)
    slopes = []
    for lo, hi in zip(EDGES[:-1], EDGES[1:]):
        lv = np.array([10 * np.log10(band_env(m[h:h + FRAMES * 48 + 4800], lo, hi)[:40].mean() + 1e-20) for h in hits])
        A = np.stack([pk - ref_db, np.ones_like(pk)], axis=1)
        (s, c), *_ = np.linalg.lstsq(A, lv, rcond=None); slopes.append(float(s))
    # ... and how that changes through the hit (a soft hit's tail doesn't drop as much as its start)
    segs = [(0, 20), (20, 60), (60, 150), (150, 300)]
    slope_t = []
    for lo, hi in zip(EDGES[:-1], EDGES[1:]):
        row = []
        for a, b in segs:
            use = [(h, q) for h, q, g in zip(hits, pk, gaps) if g * 1000 > b + 20]
            lv = np.array([10 * np.log10(band_env(m[h:h + FRAMES * 48 + 4800], lo, hi)[a:b].mean() + 1e-20) for h, q in use])
            A = np.stack([np.array([q for h, q in use]) - ref_db, np.ones(len(use))], axis=1)
            (sl, c), *_ = np.linalg.lstsq(A, lv, rcond=None); row.append(float(max(sl, 0.0)))   # (never a louder tail for a softer hit)
        slope_t.append(row)
    out = {'edges': EDGES, 'env': env.tolist(), 'colour': colour.tolist(), 'slopes': slopes, 'slopeT': slope_t, 'slopeSegs': segs, 'refDb': ref_db,
           'hits': len(hits), 'loudHits': len(loud)}
    json.dump(out, open('/home/claude/snare/fit/%s.json' % name, 'w'))
    print('%s: %d hits (%d clean, %d loud used); level vs strength per band (dB per dB), start -> tail:' % (name, len(hits), len(clean), len(loud)))
    for (lo, hi), row in zip(zip(EDGES[:-1], EDGES[1:]), slope_t): print('   %5d-%5d Hz  %s' % (lo, hi, ' '.join('%.2f' % q for q in row)))
    return out

_cache = {}
def load(name):
    if name not in _cache: _cache[name] = json.load(open('/home/claude/snare/fit/%s.json' % name))
    return _cache[name]

def render(name='HatClosed', velocity=0.8, seed=1, length=0.4, feel=1.0):
    p = load(name); rng = np.random.default_rng(seed)
    velocity = V_REF * (max(velocity, 1e-3) / V_REF) ** feel
    rel_db = 30 * np.log10(velocity / V_REF)          # (same law as the drums: amplitude ~ v^1.5)
    n = int(length * SR); t = np.arange(n) / SR
    # metallic noise in the hat's fine colour (random phases - every hit a little different, like a real hat)
    # (made in overlapping, crossfaded blocks so there are no seams)
    win = np.hanning(NFFT); hop = NFFT // 2; blocks = int(np.ceil(n / hop)) + 3; noise = np.zeros(blocks * hop + NFFT)
    for b in range(blocks):
        seg = np.fft.irfft(np.sqrt(np.array(p['colour'])) * np.exp(2j * np.pi * rng.random(len(p['colour']))), NFFT)
        i = b * hop
        if i + NFFT > len(noise): break
        noise[i:i + NFFT] += seg * win
    noise = noise[hop:hop + n]   # (skip the first half-block, where the crossfade is still fading in)
    out = np.zeros(n)
    env = np.array(p['env']); te = np.arange(env.shape[1]) / 1000.0
    for k, (lo, hi) in enumerate(zip(p['edges'][:-1], p['edges'][1:])):
        z = sosfilt(butter(4, [lo, min(hi, 23900)], 'band', fs=SR, output='sos'), noise)
        z /= np.sqrt((z ** 2).mean()) + 1e-12
        if 'slopeT' in p:   # (strength changes the start more than the tail)
            mids = [0.001 * (a + b) / 2 for a, b in p['slopeSegs']]
            sl = np.interp(t, mids, p['slopeT'][k])
        else: sl = p['slopes'][k]
        e = np.interp(t, te, env[k], right=0.0) * 10 ** (sl * rel_db / 10)
        e[t > te[-1]] = 0.0
        out += z * np.sqrt(e)
    a = int(0.0008 * SR); out[:a] *= np.sin(0.5 * np.pi * np.arange(a) / a) ** 2   # (no click at the very start)
    return out

if __name__ == '__main__' and sys.argv[1] == 'fit':
    fit(sys.argv[2], sys.argv[3], *[float(a) for a in sys.argv[4:6]])

def fit_splash(path, name, start, end, like='HatLoose', seconds=3.0):
    """A splash / open hat: one long event (the hats opened and left to ring). Its own colour and long per-band fades;
    how hit strength changes it is borrowed from a closed set (`like`) - one splash can't show that."""
    x, sr = sf.read(path); m = x.mean(axis=1) if x.ndim > 1 else x
    m = m[int(start * SR):int(end * SR)]
    h = find_hits(m, hop=0.15)[0]
    frames = int(seconds * 1000)
    seg = np.pad(m[h:], (0, max(0, frames * 48 + 4800 - len(m[h:]))))
    def benv(lo, hi):
        z = sosfiltfilt(butter(4, [lo, min(hi, 23900)], 'band', fs=SR, output='sos'), seg)
        e = np.convolve(z * z, np.ones(48) / 48, 'same'); return e[:frames * 48:48]
    env = np.array([benv(lo, hi) for lo, hi in zip(EDGES[:-1], EDGES[1:])])
    sp = np.mean([np.abs(np.fft.rfft(seg[k:k + NFFT] * np.hanning(NFFT))) ** 2 for k in range(0, int(0.4 * SR), NFFT // 2)], axis=0)
    fq = np.fft.rfftfreq(NFFT, 1 / SR); colour = np.zeros_like(sp)
    for lo, hi in zip(EDGES[:-1], EDGES[1:]):
        k = (fq >= lo) & (fq < hi); colour[k] = sp[k] / sp[k].mean()
    base = load(like)
    out = {'edges': EDGES, 'env': env.tolist(), 'colour': colour.tolist(), 'slopes': base['slopes'], 'slopeT': base['slopeT'],
           'slopeSegs': base['slopeSegs'], 'refDb': float(20 * np.log10(np.abs(seg[:480]).max())), 'hits': 1, 'loudHits': 1}
    json.dump(out, open('/home/claude/snare/fit/%s.json' % name, 'w'))
    e = 10 * np.log10(env.sum(axis=0) + 1e-20); e -= e.max()
    print('%s: falls 20 dB by %.0f ms, 40 dB by %.0f ms' % (name, np.argmax(e < -20), np.argmax(e < -40)))
